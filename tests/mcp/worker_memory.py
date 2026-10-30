"""Verify supervisor worker high-water diagnostics through GUI, CLI and native MCP.

Uses a private copied runtime and only owned processes. Windows measures the
largest single worker peak through published observations, not a process-tree
sum or a guaranteed post-exit peak. Other platforms must report unsupported.
"""

import argparse
import json
import logging
import os
import subprocess
import sys
from pathlib import Path
from time import perf_counter as clock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts/benchmarks"))
from workbench_runtime import BenchmarkHost, RunError  # noqa: E402

LOG = logging.getLogger("workbench.worker_memory")
SCOPE = "largest_single_worker_high_water_through_observation"


def decimal(value):
    if (not isinstance(value, str) or not 1 <= len(value) <= 20
            or not value.isascii() or not value.isdecimal()
            or str(int(value)) != value or int(value) > 2**64 - 1):
        raise ValueError("invalid_decimal_counter")
    return int(value)


def validate_memory(memory, windows):
    if not isinstance(memory, dict):
        raise ValueError("missing_worker_memory")
    if memory.get("unit") != "bytes" or memory.get("scope") != SCOPE:
        raise ValueError("invalid_worker_memory_scope")
    measured = decimal(memory.get("measuredObservations"))
    missing = decimal(memory.get("missingObservations"))
    if memory.get("lastObservation") != "response" or memory.get("lastNativeError") is not None:
        raise ValueError("unexpected_worker_observation")
    if windows:
        if (memory.get("status") != "measured"
                or memory.get("backend") != "windows_peak_working_set"
                or measured == 0 or missing != 0):
            raise ValueError("worker_memory_not_measured")
        peak = decimal(memory.get("peakRssBytes"))
        if peak == 0:
            raise ValueError("zero_worker_peak")
        return peak
    if (memory.get("status") != "unsupported" or memory.get("backend") != "none"
            or measured != 0 or missing == 0 or memory.get("peakRssBytes") is not None):
        raise ValueError("unsupported_memory_misreported")
    return None


class MemoryHost(BenchmarkHost):
    def __init__(self, executable, output):
        super().__init__(executable, output)
        self.deadline = clock() + 180

    def remaining(self):
        remaining = self.deadline - clock()
        if remaining <= 0:
            raise RunError("probe_deadline")
        return remaining

    def send(self, method, parameters, timeout=15):
        return super().send(method, parameters, min(timeout, self.remaining()))

    def cli(self, action, *arguments):
        result = subprocess.run(
            self.prefix + ["command", "--action", action, "--require-running", *arguments],
            capture_output=True, encoding="utf-8", env=self.environment,
            timeout=min(15, self.remaining()),
        )
        if result.returncode:
            raise RunError("cli_failed_" + action)
        return json.loads(result.stdout)

    def __exit__(self, *arguments):
        # Give graceful cleanup its own existing bounded budget after probe expiry.
        self.deadline = clock() + 20
        return super().__exit__(*arguments)


def exercise(host, output, report):
    request = {"scope": "text", "pattern": "(\\d+)",
               "text": "\n".join(f"record {index}" for index in range(500))}
    request_path = output / "request.json"
    request_path.write_text(json.dumps(request), encoding="utf-8")
    for action, suffix, button in (("regex_evaluate", "regex_lab", "regexLabRun"),
                                   ("value_counts", "value_counts", "valueCountsRun")):
        for origin in ("cli", "mcp", "gui"):
            started = clock()
            if origin == "cli":
                job = host.cli(action, "--json-file", str(request_path))["jobId"]
            elif origin == "mcp":
                job = host.start_job(request, action)["jobId"]
            else:
                host.command("show_" + suffix, {"window_index": 0})
                host.command("set_" + suffix, {"window_index": 0, "definition": request})
                previous = host.command("get_" + suffix, {"window_index": 0})["jobId"]
                host.command("activate_ui", {"window_index": 0, "object_name": button})
                job = host.command("get_" + suffix, {"window_index": 0})["jobId"]
                if not job or job == previous:
                    raise RunError("gui_run_missing")
            host.jobs.add(job)
            LOG.info("job_started action=%s origin=%s id=%s", action, origin, job)
            terminal, probes = host.finish(job, min(30, host.remaining()))
            if not terminal.get("complete"):
                raise RunError("incomplete_job_" + terminal.get("reason", "unknown"))
            memory = terminal.get("metrics", {}).get("workerMemory")
            peak = validate_memory(memory, os.name == "nt")
            cli_memory = host.cli("get_analysis_job", "--job-id", job)["metrics"]["workerMemory"]
            if cli_memory != memory or host.status(job)["metrics"]["workerMemory"] != memory:
                raise RunError("cli_mcp_memory_mismatch")
            if origin == "gui":
                dock = host.command("get_" + suffix, {"window_index": 0})
                if dock["job"]["metrics"]["workerMemory"] != memory:
                    raise RunError("gui_memory_mismatch")
            report["cases"].append(dict(
                action=action, origin=origin, jobId=job, workerMemory=memory,
                sameJobCliMcp=True, guiJob=origin == "gui", seconds=clock() - started,
                responsivenessProbeCount=len(probes), peakRssBytes=peak,
            ))
            if origin == "gui":
                host.command("set_" + suffix, {"window_index": 0,
                                               "definition": {"operation": "release"}})
                host.jobs.discard(job)
            else:
                host.release(job)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--artifacts", required=True, type=Path)
    parser.add_argument("--max-worker-bytes", type=int, default=512 * 1024 * 1024)
    options = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(name)s %(levelname)s %(message)s")
    if options.artifacts.exists() or options.artifacts.is_symlink() or options.max_worker_bytes < 1:
        return 2
    options.artifacts.mkdir(parents=True)
    output = options.artifacts.resolve()
    report = dict(schemaVersion=1, passed=False, releaseGateStatus="pending", cases=[],
                  scope=SCOPE, maxWorkerBytes=options.max_worker_bytes,
                  limitations=["Only Windows has native worker RSS observations.",
                               "Peaks after the last observation remain unmeasured.",
                               "This is not total-tree RSS or committed memory.",
                               "Offscreen GUI activation is not native layout validation."])
    code = 1
    try:
        executable = options.executable.resolve(strict=True)
        if executable.suffix.lower() == ".py":
            raise ValueError("native_executable_required")
        with MemoryHost(executable, output) as host:
            report.update(binarySha256=host.binary_hash, version=host.version)
            exercise(host, output, report)
        peaks = [case["peakRssBytes"] for case in report["cases"] if case["peakRssBytes"] is not None]
        report["workerMemoryGate"] = "measured" if peaks else "unsupported"
        if peaks and max(peaks) > options.max_worker_bytes:
            raise ValueError("worker_peak_budget_exceeded")
        report["passed"] = True
        code = 0
    except (OSError, ValueError, RuntimeError, KeyError, TypeError,
            subprocess.SubprocessError) as error:
        report["reason"] = str(error)[:512]
        LOG.error("probe_failed reason=%s", report["reason"])
    finally:
        (output / "result.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n",
                                            encoding="utf-8")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
