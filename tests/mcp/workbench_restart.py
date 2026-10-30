"""Verify dock persistence across two normally closed private application hosts."""

import argparse
import hashlib
import json
import logging
import math
import os
import shutil
import subprocess
import sys
import threading
import time
from contextlib import ExitStack
from pathlib import Path

from parity_payload import prepare_payload, private_ipc_environment
from workbench_contracts import ParityError, compare_json, require_operations

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/benchmarks"))
from workbench_runtime import BenchmarkHost, RunError, executable_arguments, file_hash

LOG = logging.getLogger("workbench.restart")
CASES = {
    "regex-lab": {
        "get": "get_regex_lab",
        "set": "set_regex_lab",
        "run": "regexLabRun",
        "apply": "regexLabApply",
        "text": "regexLabText",
        "default": "",
        "token": "G03F_REGEX_RUNTIME_ONLY",
        "floating": True,
        "rows": 2,
    },
    "value-counts": {
        "get": "get_value_counts",
        "set": "set_value_counts",
        "run": "valueCountsRun",
        "apply": "valueCountsApply",
        "text": "valueCountsText",
        "default": r"\S+",
        "token": "G03F_COUNTS_RUNTIME_ONLY",
        "floating": False,
        "rows": 1,
    },
}


def pattern(case):
    return "(?<v>" + case["token"] + ")"


def pasted_text(case):
    return "שלום 😀 " + case["token"] + "\n" + case["token"]


def require(condition, reason):
    if not condition:
        LOG.error("restart_rejected reason=%s", reason)
        raise ParityError(reason)


def cli_arguments(action, arguments, request):
    if action == "open_file":
        return ["--file", arguments["file"]]
    require(
        action in {case["set"] for case in CASES.values()}, "restart_cli_action_invalid"
    )
    request.write_text(
        json.dumps(arguments["definition"], ensure_ascii=False), encoding="utf-8"
    )
    return [
        "--window-index",
        str(arguments["window_index"]),
        "--json-file",
        str(request),
    ]


def load_cli_output(path):
    require(path.stat().st_size <= 2 * 1024 * 1024, "restart_cli_output_limit")
    text = path.read_text(encoding="utf-8")
    reply = json.loads(text) if text.strip() else {}
    require(isinstance(reply, dict), "restart_cli_reply_invalid")
    return reply


def validate_report(report, artifacts, binary_hash):
    """Recheck semantic observations, sequential host exits and archived bytes."""
    try:
        require(
            type(report["schemaVersion"]) is int
            and report["schemaVersion"] == 1
            and report["probeKind"] == "process-restart",
            "restart_schema_mismatch",
        )
        require(report["binarySha256"] == binary_hash, "restart_binary_mismatch")
        relative = Path(report["executedBinary"])
        executable = artifacts / relative
        require(
            not relative.is_absolute()
            and ".." not in relative.parts
            and artifacts.resolve() in executable.resolve().parents
            and not executable.is_symlink()
            and executable.is_file()
            and 0 < executable.stat().st_size <= 512 * 1024 * 1024,
            "restart_executed_binary_invalid",
        )
        require(
            file_hash(executable) == report["executedBinarySha256"],
            "restart_executed_binary_mismatch",
        )
        epochs = report["epochs"]
        require(
            isinstance(epochs, list) and len(epochs) == 2, "restart_requires_two_hosts"
        )
        for epoch in epochs:
            require(
                type(epoch["pid"]) is int
                and epoch["pid"] > 0
                and type(epoch["exitCode"]) is int
                and epoch["exitCode"] == 0
                and type(epoch["serverExitCode"]) is int
                and epoch["serverExitCode"] == 0,
                "restart_host_not_closed_normally",
            )
            for key in ("started", "exited"):
                value = epoch[key]
                require(
                    type(value) in (int, float)
                    and 0 <= value <= 1e12
                    and math.isfinite(value),
                    "restart_invalid_process_interval",
                )
            require(
                epoch["exited"] > epoch["started"], "restart_invalid_process_interval"
            )
            require(
                bool(epoch["windowId"])
                and epoch["sourceFiles"] == [report["sourcePath"]],
                "restart_profile_not_restored",
            )
        require(epochs[1]["started"] >= epochs[0]["exited"], "restart_hosts_overlap")
        compare_json(epochs[0]["windowId"], epochs[1]["windowId"], "$.windowId")
        cases = report["cases"]
        require(
            isinstance(cases, list) and len(cases) == len(CASES),
            "restart_cases_missing",
        )
        require({row["id"] for row in cases} == set(CASES), "restart_cases_missing")
        jobs = set()
        for row in cases:
            spec = CASES[row["id"]]
            before, after = row["before"], row["after"]
            for state in (before, after):
                require(
                    state["visible"] is True and state["floating"] is spec["floating"],
                    "restart_layout_mismatch",
                )
            text = pasted_text(spec)
            require(
                before["pattern"] == pattern(spec)
                and before["scope"] == "text"
                and before["textUnits"] == len(text.encode("utf-16-le")) // 2
                and before["textSha256"] == hashlib.sha256(text.encode()).hexdigest(),
                "restart_draft_precondition_missing",
            )
            require(
                isinstance(before["jobId"], str)
                and bool(before["jobId"])
                and before["jobId"] not in jobs
                and before["jobComplete"] is True
                and before["jobState"] == "completed",
                "restart_job_precondition_missing",
            )
            jobs.add(before["jobId"])
            require(
                isinstance(before["revision"], str)
                and before["revision"].isdigit()
                and before["jobRevision"] == before["revision"],
                "restart_job_revision_mismatch",
            )
            require(
                type(row["resultRows"]) is int and row["resultRows"] == spec["rows"],
                "restart_result_precondition_missing",
            )
            require(
                after["pattern"] == spec["default"]
                and after["scope"] == "sample"
                and type(after["textUnits"]) is int
                and after["textUnits"] == 0
                and after["textSha256"] == hashlib.sha256(b"").hexdigest()
                and after["jobId"] == ""
                and after["jobComplete"] is False
                and after["jobState"] == ""
                and after["jobRevision"] == ""
                and after["applyEnabled"] is False,
                "restart_runtime_data_retained",
            )
            require(row["oldJobReason"] == "job_expired", "restart_old_job_not_expired")
        saved = report["savedSettings"]
        require(isinstance(saved, list) and len(saved) == 2, "restart_settings_missing")
        require(
            {row["file"] for row in saved}
            == {"saved-first/cilogg.conf", "saved-first/cilogg_session.conf"},
            "restart_settings_missing",
        )
        for row in saved:
            path = artifacts / row["file"]
            require(
                path.resolve().parent == artifacts.resolve() / "saved-first"
                and not path.is_symlink()
                and path.is_file()
                and 0 < path.stat().st_size <= 2 * 1024 * 1024,
                "restart_settings_missing_or_oversized",
            )
            data = path.read_bytes()
            require(
                hashlib.sha256(data).hexdigest() == row["sha256"],
                "restart_settings_hash_mismatch",
            )
            require(
                all(spec["token"].encode() not in data for spec in CASES.values()),
                "restart_runtime_data_serialized",
            )
    except (KeyError, TypeError, IndexError, OverflowError) as error:
        raise ParityError("restart_report_invalid") from error
    LOG.info("restart_evidence_verified hosts=2 cases=2 saved_files=2")


class EpochHost(BenchmarkHost):
    """Reuse the bounded MCP transport; startup permits real session restoration."""

    def __init__(self, executable, output, environment, restore, deadline, platform):
        super().__init__(executable, output)
        self.executable = executable
        self.environment = environment
        self.restore = restore
        self.deadline = deadline
        self.platform = platform
        self.prefix = executable_arguments(executable)
        self.started = None
        self.exited = None

    def remaining(self, maximum=15):
        left = self.deadline - time.perf_counter()
        require(left > 0, "restart_timeout")
        return min(left, maximum)

    def send(self, method, parameters, timeout=15):
        return super().send(method, parameters, min(timeout, self.remaining()))

    def start(self):
        self.output.mkdir()
        host_log = (self.output / "host.log").open("wb")
        server_log = (self.output / "mcp.log").open("wb")
        self.logs.extend((host_log, server_log))
        arguments = self.prefix + [
            "--multi",
            "--load-session" if self.restore else "--new-session",
            "-d",
            "4",
        ]
        if self.platform == "offscreen":
            arguments += ["-platform", "offscreen"]
        self.started = time.perf_counter()
        self.host = self.owner.spawn(
            arguments,
            env=self.environment,
            cwd=self.executable.parent,
            stdout=host_log,
            stderr=host_log,
        )
        startup_deadline = min(self.deadline, self.started + 30)
        while True:
            require(self.host.poll() is None, "restart_host_startup_failed")
            with (self.output / "host.log").open("rb") as stream:
                ready = b"Klogg instance" in stream.read(1024 * 1024)
            if ready:
                break
            require(time.perf_counter() < startup_deadline, "restart_startup_timeout")
            time.sleep(0.02)
        self.server = self.owner.spawn(
            self.prefix + ["mcp", "serve"],
            env=self.environment,
            cwd=self.executable.parent,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=server_log,
        )
        self.reader = threading.Thread(target=self.read_replies, daemon=True)
        self.writer = threading.Thread(target=self.write_requests, daemon=True)
        self.reader.start()
        self.writer.start()
        self.send(
            "initialize",
            {
                "protocolVersion": "2025-11-25",
                "capabilities": {},
                "clientInfo": {"name": "workbench-restart", "version": "1"},
            },
        )
        self.notify("notifications/initialized", {})
        catalog = self.send(
            "tools/call",
            {
                "name": "cilogg_commands",
                "arguments": {"area": "commander"},
            },
        )["structuredContent"]
        require_operations(
            catalog,
            {
                "get_info",
                "open_file",
                "activate_ui",
                "get_ui",
                "close_cilogg",
                "dump_state",
                "show_regex_lab",
                "show_value_counts",
                "get_analysis_job",
                "get_analysis_results",
                *(case[key] for case in CASES.values() for key in ("get", "set")),
            },
        )
        while True:
            try:
                if len(self.command("get_info").get("windows", [])) == 1:
                    break
            except RunError as error:
                if str(error) != "command_failed_get_info":
                    raise
                require(
                    time.perf_counter() < startup_deadline, "restart_startup_timeout"
                )
                time.sleep(0.02)
            require(time.perf_counter() < startup_deadline, "restart_startup_timeout")
            time.sleep(0.02)
        LOG.info("restart_host_ready pid=%d restore=%s", self.host.pid, self.restore)

    def cli(self, action, arguments):
        request = self.output / "request.json"
        parameters = cli_arguments(action, arguments, request)
        output = self.output / "cli.json"
        with (
            output.open("wb") as stdout,
            (self.output / "cli.log").open("ab") as stderr,
        ):
            process = self.owner.spawn(
                self.prefix
                + ["command", "--action", action, "--require-running", *parameters],
                env=self.environment,
                cwd=self.executable.parent,
                stdout=stdout,
                stderr=stderr,
            )
            require(
                process.wait(timeout=self.remaining(30)) == 0,
                "restart_cli_failed_" + action,
            )
        LOG.info(
            "restart_cli_completed action=%s bytes=%d", action, output.stat().st_size
        )
        return load_cli_output(output)

    def stop(self, info):
        self.command("close_cilogg")
        require(
            self.host.wait(timeout=self.remaining()) == 0, "restart_host_exit_failed"
        )
        self.exited = time.perf_counter()
        self.server.stdin.close()
        require(
            self.server.wait(timeout=self.remaining()) == 0,
            "restart_server_exit_failed",
        )
        window = info["windows"][0]
        LOG.info("restart_host_closed pid=%d exit=0", self.host.pid)
        return {
            "pid": self.host.pid,
            "started": self.started,
            "exited": self.exited,
            "exitCode": 0,
            "serverExitCode": 0,
            "windowId": window["windowId"],
            "sourceFiles": [
                Path(tab["filePath"]).resolve().as_posix() for tab in window["tabs"]
            ],
        }


def ui_object(host, name):
    objects = host.command("get_ui", {"window_index": 0})["objects"]
    found = [obj for obj in objects if obj["objectName"] == name]
    require(
        len(found) == 1 and bool(found[0].get("accessibleName")),
        "restart_ui_control_missing",
    )
    return found[0]


def snapshot(host, spec):
    state = host.command(spec["get"], {"window_index": 0})
    draft, job = state["draft"], state.get("job", {})
    text = ui_object(host, spec["text"])["properties"]["plainText"]
    return {
        "visible": state["visible"],
        "floating": state["floating"],
        "pattern": draft["pattern"],
        "scope": draft["scope"],
        "textUnits": len(text.encode("utf-16-le")) // 2,
        "textSha256": hashlib.sha256(text.encode()).hexdigest(),
        "jobId": state["jobId"],
        "jobComplete": job.get("complete", False),
        "jobState": job.get("state", ""),
        "revision": draft["requestRevision"],
        "jobRevision": job.get("requestRevision", ""),
        "applyEnabled": ui_object(host, spec["apply"])["enabled"],
    }


def wait_for_source(host, source):
    deadline = time.perf_counter() + min(30, host.remaining(30))
    while True:
        remaining = min(deadline - time.perf_counter(), host.remaining(30))
        require(remaining > 0, "restart_source_timeout")
        try:
            state = host.command("dump_state", {"window_index": 0}, timeout=remaining)[
                "ciloggState"
            ]
        except RunError as error:
            if str(error) != "command_failed_dump_state":
                raise
            LOG.info("restart_source_probe_retry remaining_seconds=%.3f", remaining)
            time.sleep(0.02)
            continue
        if (
            state.get("loadingInProgress") is False
            and Path(state.get("activeFile", "")).resolve() == source.resolve()
        ):
            LOG.info("restart_source_ready")
            return
        host.remaining()
        time.sleep(0.02)


def verify_rows(name, rows):
    if name == "regex-lab":
        token = CASES[name]["token"]
        expected = []
        for number, start in ((1, 8), (2, 0)):
            groups = [
                {
                    "number": group,
                    "name": "" if group == 0 else "v",
                    "matched": True,
                    "value": token,
                    "startUtf16": start,
                    "lengthUtf16": len(token),
                }
                for group in (0, 1)
            ]
            expected.append(
                {
                    "lineNumber": number,
                    "text": ("שלום 😀 " if number == 1 else "") + token,
                    "matched": True,
                    "offsetConvention": "zero-based UTF-16",
                    "subpatterns": [
                        {
                            "index": 0,
                            "matched": True,
                            "matches": [
                                {
                                    "startUtf16": start,
                                    "lengthUtf16": len(token),
                                    "groups": groups,
                                }
                            ],
                        }
                    ],
                }
            )
        try:
            actual = [{key: row[key] for key in expected[0]} for row in rows]
        except (KeyError, TypeError) as error:
            raise ParityError("restart_regex_row_invalid") from error
        compare_json(expected, actual)
    else:
        compare_json(
            [[CASES[name]["token"], "2"]],
            [[row["value"], row["count"]] for row in rows],
        )


def exercise(options, artifacts, report):
    deadline = time.perf_counter() + options.timeout
    payload, private = prepare_payload(options.executable, artifacts)
    settings = artifacts / "settings"
    settings.mkdir()
    source = artifacts / "source.log"
    source.write_bytes(b"first1\nsecond2")
    environment = dict(
        os.environ,
        CILOGG_AUTOMATION="0",
        PYTHONUTF8="1",
        APPDATA=str(settings),
        LOCALAPPDATA=str(settings),
        XDG_CONFIG_HOME=str(settings),
        XDG_DATA_HOME=str(settings),
    )
    environment.pop("QT_QPA_PLATFORM", None)
    if options.platform == "offscreen":
        environment["QT_QPA_PLATFORM"] = "offscreen"
    report.update(
        schemaVersion=1,
        probeKind="process-restart",
        binarySha256=file_hash(options.executable),
        executedBinary=private.relative_to(artifacts).as_posix(),
        executedBinarySha256=file_hash(private),
        sourcePath=source.as_posix(),
        cases=[],
        epochs=[],
        savedSettings=[],
    )
    with ExitStack() as stack:
        environment = stack.enter_context(private_ipc_environment(environment))
        with EpochHost(
            private, artifacts / "first", environment, False, deadline, options.platform
        ) as host:
            host.cli("open_file", {"file": str(source)})
            wait_for_source(host, source)
            for name, spec in CASES.items():
                host.command(
                    spec["get"].replace("get_", "show_", 1), {"window_index": 0}
                )
                host.cli(
                    spec["set"],
                    {
                        "window_index": 0,
                        "definition": {
                            "pattern": pattern(spec),
                            "scope": "text",
                            "text": pasted_text(spec),
                            "visible": True,
                            "floating": spec["floating"],
                        },
                    },
                )
                host.command(
                    "activate_ui", {"window_index": 0, "object_name": spec["run"]}
                )
                while True:
                    state = host.command(spec["get"], {"window_index": 0})
                    job = state.get("job", {})
                    if job.get("state") in ("completed", "failed", "cancelled"):
                        break
                    host.remaining()
                    time.sleep(0.02)
                require(
                    job.get("state") == "completed" and job.get("complete") is True,
                    "restart_analysis_not_complete",
                )
                rows = host.command("get_analysis_results", {"job_id": state["jobId"]})[
                    "rows"
                ]
                verify_rows(name, rows)
                report["cases"].append(
                    {
                        "id": name,
                        "before": snapshot(host, spec),
                        "resultRows": len(rows),
                    }
                )
            info = host.command("get_info")
            require(len(info["windows"]) == 1, "restart_window_count")
            report["epochs"].append(host.stop(info))
        archive = artifacts / "saved-first"
        archive.mkdir()
        for name in ("cilogg.conf", "cilogg_session.conf"):
            saved = payload / name
            require(
                saved.is_file() and 0 < saved.stat().st_size <= 2 * 1024 * 1024,
                "restart_settings_missing_or_oversized",
            )
            shutil.copy2(saved, archive / name)
            report["savedSettings"].append(
                {"file": "saved-first/" + name, "sha256": file_hash(archive / name)}
            )
        with EpochHost(
            private, artifacts / "second", environment, True, deadline, options.platform
        ) as host:
            wait_for_source(host, source)
            for row in report["cases"]:
                spec = CASES[row["id"]]
                row["after"] = snapshot(host, spec)
                expired = host.command(
                    "get_analysis_job",
                    {"job_id": row["before"]["jobId"]},
                    allow_error=True,
                )
                row["oldJobReason"] = expired.get("reason")
            info = host.command("get_info")
            require(len(info["windows"]) == 1, "restart_window_count")
            report["epochs"].append(host.stop(info))
    return report


def main(arguments=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--artifacts", required=True, type=Path)
    parser.add_argument(
        "--platform", choices=("offscreen", "native"), default="offscreen"
    )
    parser.add_argument("--timeout", type=float, default=180)
    options = parser.parse_args(arguments)
    logging.basicConfig(level=logging.INFO)
    artifacts = options.artifacts.resolve()
    if artifacts.exists():
        LOG.error("restart_setup_rejected reason=artifacts_exist")
        return 2
    artifacts.mkdir(parents=True)
    report = {
        "schemaVersion": 1,
        "passed": False,
        "state": "failed",
        "releaseGateStatus": "pending",
        "evidenceKind": "test_double"
        if options.executable.suffix.lower() == ".py"
        else "application",
        "platformRequested": options.platform,
    }
    code = 1
    try:
        require(
            math.isfinite(options.timeout) and 1 <= options.timeout <= 600,
            "restart_invalid_timeout",
        )
        require(
            options.executable.is_file()
            and not options.executable.is_symlink()
            and 0 < options.executable.stat().st_size <= 512 * 1024 * 1024,
            "restart_executable_invalid",
        )
        options.executable = options.executable.resolve()
        report["binarySha256"] = file_hash(options.executable)
        report["restart"] = {}
        native = exercise(options, artifacts, report["restart"])
        report["restart"] = native
        validate_report(native, artifacts, report["binarySha256"])
        report.update(passed=True, state="passed", restart=native)
        code = 0
    except (
        OSError,
        ValueError,
        TypeError,
        KeyError,
        RuntimeError,
        subprocess.SubprocessError,
    ) as error:
        report["reason"] = (
            str(error)
            if isinstance(error, (ParityError, RunError))
            else type(error).__name__
        )
        LOG.error("restart_failed reason=%s", report["reason"])
    finally:
        try:
            output = json.dumps(report, ensure_ascii=False, indent=2, allow_nan=False)
            require(
                len(output.encode("utf-8")) <= 2 * 1024 * 1024, "restart_report_limit"
            )
        except (ValueError, TypeError, OverflowError, ParityError):
            report.pop("restart", None)
            report.update(
                passed=False,
                state="failed",
                reason="restart_report_invalid_or_oversized",
            )
            output = json.dumps(report, indent=2, allow_nan=False)
            code = 1
        (artifacts / "result.json").write_text(output, encoding="utf-8")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
