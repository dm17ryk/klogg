"""Public viewer baseline; paired idle docks, not a renderer or release gate."""

import logging
import json
import math
import os
import shutil
import sys
import time
from contextlib import ExitStack
from pathlib import Path
from time import perf_counter as clock

from workbench_runtime import RunError, file_hash

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests/mcp"))
from parity_payload import prepare_payload, private_ipc_environment  # noqa: E402
from workbench_contracts import require_operations  # noqa: E402
from workbench_restart import EpochHost  # noqa: E402

LOG = logging.getLogger("workbench.viewer")
VIEWER_BUDGETS = {"openIndexSeconds": 30.0, "searchSeconds": 30.0}
PROFILES = ("analysis_docks_hidden", "analysis_docks_idle")


def require(condition, reason):
    if not condition:
        LOG.error("viewer_rejected reason=%s", reason)
        raise RunError(reason)


def fixture(manifest):
    rows = manifest.get("rows")
    entries = [
        entry for entry in manifest.get("files", []) if entry.get("path") == "ascii.log"
    ]
    if type(rows) is not int or not 1 <= rows <= 1000000 or len(entries) != 1:
        raise ValueError("viewer_fixture_oracle")
    expected = entries[0].get("expected")
    errors = (rows + 9) // 10
    if (
        not isinstance(expected, dict)
        or type(expected.get("physicalLines")) is not int
        or expected["physicalLines"] != rows
        or type(expected.get("errorLines")) is not int
        or expected["errorLines"] != errors
    ):
        raise ValueError("viewer_fixture_oracle")
    return dict(
        rows=rows,
        errorLines=errors,
        bytes=entries[0]["bytes"],
        sha256=entries[0]["sha256"],
    )


def remaining(deadline):
    left = deadline - clock()
    require(left > 0, "viewer_phase_timeout")
    return left


def read_state(host, source, deadline):
    reply = host.command("dump_state", {"window_index": 0}, timeout=remaining(deadline))
    remaining(deadline)  # Completed requests also count against the phase budget.
    state = reply.get("ciloggState") if isinstance(reply, dict) else None
    require(isinstance(state, dict), "viewer_state_invalid")
    require(
        type(state.get("loadingInProgress")) is bool
        and type(state.get("searchInProgress")) is bool
        and type(state.get("activeTabIndex")) is int
        and 0 <= state["activeTabIndex"] <= 1024
        and type(state.get("matchCount")) is int
        and 0 <= state["matchCount"] <= 1000000
        and isinstance(state.get("searchText"), str)
        and isinstance(state.get("lastErrorText"), str),
        "viewer_state_invalid",
    )
    path = state.get("activeFile")
    require(
        isinstance(path, str)
        and 0 < len(path) <= 32768
        and Path(path).resolve() == source.resolve(),
        "viewer_source_mismatch",
    )
    require(not state["lastErrorText"], "viewer_search_error")
    return state


def wait_state(host, source, deadline, accept):
    polls = 0
    while True:
        state = read_state(host, source, deadline)
        polls += 1
        if accept(state):
            return state, polls
        LOG.debug(
            "viewer_wait file=%s loading=%s searching=%s matches=%d polls=%d",
            source.name,
            state["loadingInProgress"],
            state["searchInProgress"],
            state["matchCount"],
            polls,
        )
        time.sleep(min(0.02, remaining(deadline)))


def baseline_cycle(host, source, expected, budgets, record):
    record.update(complete=False, phase="open_index", searches=[])
    started = clock()
    deadline = min(host.deadline, started + budgets["openIndexSeconds"])
    host.command("open_file", {"file": str(source)}, timeout=remaining(deadline))
    remaining(deadline)
    source_state, polls = wait_state(
        host, source, deadline, lambda state: not state["loadingInProgress"]
    )
    record.update(
        openIndexSecondsIncludingProtocol=clock() - started, openIndexPolls=polls
    )
    LOG.info(
        "viewer_index_complete source=%s seconds=%.6f polls=%d",
        source.name,
        record["openIndexSecondsIncludingProtocol"],
        polls,
    )
    for pattern, regex, count in (
        (" ERROR ", False, expected["errorLines"]),
        (" INFO ", True, expected["rows"] - expected["errorLines"]),
    ):
        record["phase"] = "regex_search" if regex else "literal_search"
        observation = dict(regex=regex, expectedMatches=count, complete=False)
        record["searches"].append(observation)
        started = clock()
        deadline = min(host.deadline, started + budgets["searchSeconds"])
        host.command(
            "search",
            {
                "window_index": 0,
                "tab_index": source_state["activeTabIndex"],
                "text": pattern,
                "regex": regex,
            },
            timeout=remaining(deadline),
        )
        remaining(deadline)
        state, polls = wait_state(
            host,
            source,
            deadline,
            lambda state: (
                state["searchText"] == pattern and not state["searchInProgress"]
            ),
        )
        observation.update(
            secondsIncludingProtocol=clock() - started,
            matches=state["matchCount"],
            polls=polls,
        )
        require(state["matchCount"] == count, "viewer_count_mismatch")
        require(not state["loadingInProgress"], "viewer_source_not_ready")
        observation["complete"] = True
        LOG.info(
            "viewer_search_complete source=%s regex=%s matches=%d seconds=%.6f polls=%d",
            source.name,
            regex,
            count,
            observation["secondsIncludingProtocol"],
            polls,
        )
    record.update(complete=True, phase="completed")


def paired_overhead(baseline, idle):
    def difference(first, second):
        require(
            type(first) in (int, float)
            and type(second) in (int, float)
            and 0 <= first <= 600
            and 0 <= second <= 600
            and math.isfinite(first)
            and math.isfinite(second),
            "viewer_timing_invalid",
        )
        return dict(
            deltaSeconds=second - first, ratio=second / first if first > 0 else None
        )

    return dict(
        openIndex=difference(
            baseline["openIndexSecondsIncludingProtocol"],
            idle["openIndexSecondsIncludingProtocol"],
        ),
        searches=[
            difference(
                first["secondsIncludingProtocol"], second["secondsIncludingProtocol"]
            )
            for first, second in zip(baseline["searches"], idle["searches"])
        ],
    )


class ViewerHost(EpochHost):
    def send(self, method, parameters, timeout=15):
        result = super().send(method, parameters, timeout)
        if result.get("isError"):
            path = self.output / "command-error.json"
            path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
            LOG.error(
                "viewer_command_failed method=%s diagnostics=%s", method, path.name
            )
        return result

    def start(self):
        super().start()
        deadline = min(self.deadline, self.started + 30)
        while True:
            try:
                state = self.command(
                    "dump_state", {"window_index": 0}, timeout=remaining(deadline)
                ).get("ciloggState")
            except RunError as error:
                if str(error) != "command_failed_dump_state":
                    raise
                LOG.debug("viewer_host_pending window_state_unavailable")
                time.sleep(min(0.02, remaining(deadline)))
                continue
            remaining(deadline)
            require(
                isinstance(state, dict) and type(state.get("startupReady")) is bool,
                "viewer_startup_state_invalid",
            )
            if state["startupReady"]:
                LOG.info(
                    "viewer_host_ready startup_seconds=%.6f", clock() - self.started
                )
                return
            LOG.debug("viewer_host_pending startupReady=false")
            time.sleep(min(0.02, remaining(deadline)))

    def remaining(self, maximum=15):
        return min(maximum, remaining(self.deadline))

    def __exit__(self, *arguments):
        self.deadline = clock() + 15  # Existing bounded cleanup gets its own allowance.
        return super().__exit__(*arguments)


def configure_profile(host, visible):
    for suffix in ("regex_lab", "value_counts"):
        host.command(
            "set_" + suffix, {"window_index": 0, "definition": {"visible": visible}}
        )
        state = host.command("get_" + suffix, {"window_index": 0})
        require(
            isinstance(state, dict)
            and state.get("visible") is visible
            and not state.get("jobId")
            and not state.get("job"),
            "viewer_dock_not_idle",
        )
    LOG.info("viewer_profile_ready visible=%s jobs=0", visible)


def run_viewer(options, manifest, report):
    expected = fixture(manifest)
    deadline = clock() + min(900, 60 + options.repetitions * 120)
    report.update(
        pairedOverhead=[],
        timingBoundary="Commander submission through observed completion",
        overheadGate="unmeasured_calibration",
        sourcePolicy="immutable copied ASCII fixture",
    )
    report["unmeasured"] += [
        "pureIndexCpuTime",
        "pureSearchCpuTime",
        "calibratedIdleDockOverhead",
    ]
    for repetition in range(options.repetitions):
        profiles = PROFILES if repetition % 2 == 0 else tuple(reversed(PROFILES))
        pair = {}
        for profile in profiles:
            remaining(deadline)
            output = options.output / f"viewer-{repetition}-{profile}"
            output.mkdir()
            payload, private = prepare_payload(options.executable, output)
            settings = output / "settings"
            settings.mkdir()
            environment = dict(
                os.environ,
                CILOGG_AUTOMATION="1",
                PYTHONUTF8="1",
                APPDATA=str(settings),
                LOCALAPPDATA=str(settings),
                XDG_CONFIG_HOME=str(settings),
                XDG_DATA_HOME=str(settings),
            )
            environment.pop("QT_QPA_PLATFORM", None)
            if options.platform == "offscreen":
                environment["QT_QPA_PLATFORM"] = "offscreen"
            run = dict(
                repetition=repetition,
                profile=profile,
                platform=options.platform,
                complete=False,
                executedBinary=private.relative_to(options.output).as_posix(),
                executedBinarySha256=file_hash(private),
                cycles=[],
            )
            report["runs"].append(run)
            with ExitStack() as stack:
                environment = stack.enter_context(private_ipc_environment(environment))
                with ViewerHost(
                    private,
                    output / "host",
                    environment,
                    False,
                    deadline,
                    options.platform,
                ) as host:
                    if "reportedVersion" not in report["build"]:
                        version = output / "version.txt"
                        with (
                            version.open("wb") as stdout,
                            (output / "version.log").open("wb") as stderr,
                        ):
                            process = host.owner.spawn(
                                host.prefix + ["--version"],
                                env=environment,
                                cwd=private.parent,
                                stdout=stdout,
                                stderr=stderr,
                            )
                            require(
                                process.wait(timeout=host.remaining()) == 0,
                                "viewer_version_failed",
                            )
                        require(version.stat().st_size <= 4096, "viewer_version_limit")
                        report["build"]["reportedVersion"] = version.read_text(
                            encoding="utf-8"
                        )
                    catalog = host.send(
                        "tools/call",
                        {"name": "cilogg_commands", "arguments": {"area": "commander"}},
                    )["structuredContent"]
                    require_operations(
                        catalog,
                        {
                            "open_file",
                            "search",
                            "dump_state",
                            "set_regex_lab",
                            "set_value_counts",
                        },
                    )
                    configure_profile(host, profile == "analysis_docks_idle")
                    for cycle in range(2):
                        source = output / f"viewer-{cycle}.log"
                        shutil.copyfile(options.corpus / "ascii.log", source)
                        require(
                            source.stat().st_size == expected["bytes"]
                            and file_hash(source) == expected["sha256"],
                            "viewer_fixture_changed",
                        )
                        record = dict(
                            cycle=cycle,
                            processState="cold_process"
                            if cycle == 0
                            else "warm_process",
                            sourceSha256=expected["sha256"],
                            rawBytes=expected["bytes"],
                            physicalLines=expected["rows"],
                        )
                        run["cycles"].append(record)
                        baseline_cycle(
                            host, source, expected, report["budgets"], record
                        )
                        configure_profile(host, profile == "analysis_docks_idle")
                        require(
                            file_hash(source) == expected["sha256"],
                            "viewer_fixture_changed",
                        )
            run["complete"] = True
            pair[profile] = run
            LOG.info(
                "viewer_profile_complete repetition=%d profile=%s cycles=2",
                repetition,
                profile,
            )
        report["pairedOverhead"].append(
            dict(
                repetition=repetition,
                cycles=[
                    paired_overhead(first, second)
                    for first, second in zip(
                        pair[PROFILES[0]]["cycles"], pair[PROFILES[1]]["cycles"]
                    )
                ],
            )
        )
