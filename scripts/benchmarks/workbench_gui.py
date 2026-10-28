"""Opt-in Qt measurements; event dispatch, CPU painting and cancellation differ."""

import json
import logging
import math
import os
import shutil
import subprocess
import uuid

from workbench_corpus import json_bytes
from workbench_process import ProcessOwner
from workbench_resident import ResidentError, ResidentMemorySampler
from workbench_runtime import RunError, executable_arguments, file_hash

LOG = logging.getLogger("workbench.gui")
GUI_BUDGETS = {
    "guiEventP95Ms": 100.0,
    "scrollPaintP95Ms": 100.0,
    "serviceCancellationMs": 1000.0,
    "nativeSuiteSeconds": 120.0,
}
if os.name == "nt":
    GUI_BUDGETS["processTreePeakCommitBytes"] = 512 * 1024 * 1024


def native_measurements(data, expected, samples):
    if (
        not isinstance(data, dict)
        or type(data.get("schemaVersion")) is not int
        or data.get("schemaVersion") != 1
        or data.get("scenario") != "ui-service"
    ):
        raise RunError("invalid_native_report")
    if data.get("source") != expected:
        raise RunError("native_source_mismatch")
    if (
        data.get("workerActiveDuringSamples") is not True
        or data.get("recoveryComplete") is not True
    ):
        raise RunError("invalid_native_report")
    qt = data.get("qt")
    if not isinstance(qt, dict) or any(
        not isinstance(qt.get(key), str) or not qt[key]
        for key in ("compile", "runtime", "platform")
    ):
        raise RunError("invalid_native_report")

    def timing(value):
        if (
            isinstance(value, bool)
            or not isinstance(value, (int, float))
            or not 0 < value <= 600000
            or not math.isfinite(value)
        ):
            raise RunError("invalid_native_report")
        return value

    contexts = data.get("contexts")
    if not isinstance(contexts, list) or len(contexts) != 2:
        raise RunError("invalid_native_report")
    summaries = {}
    for context in contexts:
        if not isinstance(context, dict) or context.get("name") not in (
            "idle",
            "analysis_active",
        ):
            raise RunError("invalid_native_report")
        name = context["name"]
        values = context.get("samplesMs")
        if name in summaries or not isinstance(values, list) or len(values) != samples:
            raise RunError("invalid_native_report")
        columns = [[], []]
        for pair in values:
            if not isinstance(pair, list) or len(pair) != 2:
                raise RunError("invalid_native_report")
            for column in range(2):
                columns[column].append(timing(pair[column]))
        summaries[name] = {
            "guiEventP95Ms": sorted(columns[0])[math.ceil(samples * 0.95) - 1],
            "scrollPaintP95Ms": sorted(columns[1])[math.ceil(samples * 0.95) - 1],
            "samples": samples,
        }
    cancellation = data.get("cancellation")
    if not isinstance(cancellation, dict) or cancellation.get("state") != "cancelled":
        raise RunError("invalid_native_report")
    ack = timing(cancellation.get("ackMs"))
    terminal = timing(cancellation.get("terminalMs"))
    if ack > terminal:
        raise RunError("invalid_native_report")
    return {
        "guiEventP95Ms": max(c["guiEventP95Ms"] for c in summaries.values()),
        "scrollPaintP95Ms": max(c["scrollPaintP95Ms"] for c in summaries.values()),
        "serviceCancellationMs": terminal,
        "cancellationAckMs": ack,
        "contexts": summaries,
    }


def run_gui(options, manifest, report):
    """Run only private test/application copies; own their worker descendants."""
    output = options.output
    entry = next(item for item in manifest["files"] if item["path"] == "ascii.log")
    source = output / "source.log"
    shutil.copy2(options.corpus / entry["path"], source)
    expected = {
        "sha256": entry["sha256"],
        "bytes": str(entry["bytes"]),
        "physicalLines": str(entry["expected"]["physicalLines"]),
    }
    if file_hash(source) != expected["sha256"]:
        raise RunError("native_source_mismatch")
    payload = output / "native-payload"
    payload.mkdir()
    private = []
    for label, original in (
        ("app", options.executable),
        ("tests", options.test_executable),
    ):
        copied = payload / (
            "cilogg-g02-" + label + "-" + uuid.uuid4().hex + original.suffix
        )
        shutil.copy2(original, copied)
        private.append(copied)
    for path in options.test_executable.parent.iterdir():
        if path.is_file() and path.suffix.lower() in (".dll", ".qm", ".rcc"):
            shutil.copy2(path, payload / path.name)
    for name in (
        "platforms",
        "styles",
        "imageformats",
        "iconengines",
        "tls",
        "networkinformation",
        "translations",
        "resources",
    ):
        path = options.test_executable.parent / name
        if path.is_dir():
            shutil.copytree(path, payload / name)
    (payload / "cilogg.conf").write_text(
        "[General]\nversion=1\nversionchecker.enabled=false\n", encoding="utf-8"
    )
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
        CILOGG_G02_SOURCE=str(source),
        CILOGG_G02_ROWS=expected["physicalLines"],
        CILOGG_G02_SAMPLES=str(options.samples),
    )
    environment.pop("QT_QPA_PLATFORM", None)
    if options.platform == "offscreen":
        environment["QT_QPA_PLATFORM"] = "offscreen"
    report["build"]["testBinarySha256"] = file_hash(private[1])
    owner = ProcessOwner()
    try:
        with (output / "version.log").open("wb") as version_log:
            probe = owner.spawn(
                executable_arguments(private[0]) + ["--version"],
                env=environment,
                stdout=version_log,
                stderr=version_log,
                cwd=payload,
            )
            if probe.wait(timeout=20):
                raise RunError("version_process_failed")
        with (output / "version.log").open("rb") as version_log:
            report["build"]["reportedVersion"] = version_log.read(4096).decode(
                "utf-8", errors="replace"
            )
        for repetition in range(options.repetitions):
            # Exclude the version probe and previous repetitions from this peak.
            owner.close()
            owner = ProcessOwner()
            LOG.debug("native_accounting_started repetition=%d", repetition)
            artifact = output / f"native-{repetition}.json"
            environment["CILOGG_G02_REPORT"] = str(artifact)
            arguments = executable_arguments(private[1]) + ["[workbench-native]"]
            if options.platform == "offscreen":
                arguments += ["-platform", "offscreen"]
            LOG.info(
                "native_started repetition=%d samples=%d platform=%s source_sha256=%s",
                repetition,
                options.samples,
                options.platform,
                expected["sha256"],
            )
            with (
                (output / f"native-{repetition}-stdout.log").open("wb") as stdout,
                (output / f"native-{repetition}-stderr.log").open("wb") as stderr,
            ):
                process = owner.spawn(
                    arguments,
                    env=environment,
                    cwd=payload,
                    stdin=subprocess.PIPE,
                    stdout=stdout,
                    stderr=stderr,
                )
                sampler = None
                resident = {
                    "status": "disabled",
                    "backend": "disabled",
                    "measurements": {},
                }
                try:
                    if options.sample_resident_memory:
                        sampler = ResidentMemorySampler(owner, process.pid)
                        sampler.sample()
                    else:
                        LOG.debug(
                            "resident_sampling_disabled repetition=%d", repetition
                        )
                    # The child cannot create analysis workers until it is owned.
                    process.stdin.write(b"start\n")
                    process.stdin.flush()
                    process.stdin.close()
                    process.stdin = None
                    timeout = report["budgets"]["nativeSuiteSeconds"]
                    code = (
                        sampler.wait(process, timeout)
                        if sampler
                        else process.wait(timeout=timeout)
                    )
                    if sampler:
                        resident = sampler.result()
                        # Retain trusted OS observations before independent native
                        # report validation or committed-memory queries can fail.
                        report.setdefault("residentMemoryObservations", []).append(
                            {
                                "repetition": repetition,
                                "hostPid": process.pid,
                                **resident,
                            }
                        )
                        LOG.debug(
                            "resident_observation_retained repetition=%d pid=%d status=%s",
                            repetition,
                            process.pid,
                            resident["status"],
                        )
                except subprocess.TimeoutExpired as error:
                    if sampler:
                        report["residentMemoryFailure"] = sampler.result("incomplete")
                        report["residentMemoryFailure"].update(
                            repetition=repetition,
                            reason="native_timeout",
                            stage="sampling",
                        )
                    raise RunError("native_timeout") from error
                except ResidentError as error:
                    failure = (
                        sampler.result("failed")
                        if sampler
                        else {
                            "status": "failed",
                            "kind": "sampled_working_set_bytes",
                            "backend": "windows_job_members_working_set"
                            if os.name == "nt"
                            else "unavailable",
                            "scope": "native_test_host_and_owned_descendants",
                            "hostPid": process.pid,
                            "samples": 0,
                            "measurements": {},
                            "includesHarness": False,
                        }
                    )
                    failure.update(
                        repetition=repetition,
                        reason=str(error),
                        stage="sampling" if sampler else "initialization",
                    )
                    report["residentMemoryFailure"] = failure
                    LOG.error(
                        "resident_failure_retained repetition=%d pid=%d stage=%s reason=%s",
                        repetition,
                        process.pid,
                        failure["stage"],
                        str(error),
                    )
                    raise
            if code:
                if sampler:
                    report["residentMemoryFailure"] = sampler.result("incomplete")
                raise RunError("native_process_failed")
            if (
                artifact.is_symlink()
                or not artifact.is_file()
                or artifact.stat().st_size > 1024 * 1024
            ):
                raise RunError("native_report_missing_or_oversized")
            try:
                data = json.loads(artifact.read_text(encoding="utf-8"))
                # Validate retained extension fields too, before mutating the report.
                json_bytes(data)
                measurements = native_measurements(data, expected, options.samples)
            except (ValueError, TypeError, KeyError) as error:
                raise RunError("invalid_native_report") from error
            peak = owner.peak_tree_commit_bytes()
            memory = {
                "status": "unsupported" if peak is None else "measured",
                "kind": "committed_bytes",
                "backend": "unavailable" if peak is None else "windows_job_object",
                "scope": "native_test_host_and_descendants",
                "start": "job_assignment_before_start_handshake",
                "end": "native_test_host_exit",
                "includesExitedDescendants": peak is not None,
                "includesHarness": False,
            }
            if peak is not None:
                measurements["processTreePeakCommitBytes"] = peak
                report["unmeasured"] = [
                    item
                    for item in report["unmeasured"]
                    if item != "processTreePeakCommitBytes"
                ]
            gates = {
                key: measurements[key] < report["budgets"][key]
                for key in (
                    "guiEventP95Ms",
                    "scrollPaintP95Ms",
                    "serviceCancellationMs",
                )
            }
            if peak is not None:
                gates["processTreePeakCommitBytes"] = (
                    peak < report["budgets"]["processTreePeakCommitBytes"]
                )
            measurements.update(resident["measurements"])
            if resident["status"] == "sampled":
                metric = "processTreeSampledRssPeakBytes"
                gates[metric] = measurements[metric] < report["budgets"][metric]
                report["unmeasured"] = [
                    item
                    for item in report["unmeasured"]
                    if item not in resident["measurements"]
                ]
            report["runs"].append(
                {
                    "repetition": repetition,
                    "measurements": measurements,
                    "gates": gates,
                    "nativeReportSha256": file_hash(artifact),
                    "native": data,
                    "memory": memory,
                    "residentMemory": resident,
                }
            )
            report["unmeasured"] = [
                item
                for item in report["unmeasured"]
                if item
                not in (
                    "guiEventLatency",
                    "scrollLatency",
                    "serviceCancellationLatency",
                )
            ]
            LOG.info("native_completed repetition=%d gates=%s", repetition, gates)
            if not all(gates.values()):
                raise RunError("budget_exceeded")
    finally:
        owner.close()
