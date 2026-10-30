"""Bounded Windows sustained analysis/release workload; not a full release gate."""

import argparse
import json
import logging
import math
import os
import platform
import shutil
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path
from time import perf_counter as clock

from workbench_corpus import validate_corpus
from workbench_resident import ResidentMemorySampler
from workbench_runtime import BenchmarkHost, RunError, file_hash

LOG = logging.getLogger("workbench.sustained")
TOKEN = "G02F_TOKEN"
APPEND = (TOKEN + "\n").encode() * 16
MAX_SAMPLES = 8192
MAX_CYCLES = 100
DEFAULT_LIMITS = {"treeRssBytes": 512 * 1024**2, "hostGrowthBytes": 32 * 1024**2}


def require(condition, reason):
    if not condition:
        LOG.error("sustained_rejected reason=%s", reason)
        raise RunError(reason)


def finite(value, minimum=0, maximum=2**64):
    return (
        type(value) in (int, float)
        and minimum <= value <= maximum
        and math.isfinite(value)
    )


def remaining(deadline):
    value = deadline - clock()
    require(value > 0, "sustained_timeout")
    return value


def summarize(points, duration):
    require(10 <= len(points) <= MAX_SAMPLES, "sustained_points_missing_or_excessive")
    previous = -1
    for row in points:
        t, value = row["seconds"], row["hostRssBytes"]
        require(
            finite(t, 0, 3600)
            and type(value) is int
            and 0 < value < 2**64
            and t > previous,
            "sustained_invalid_point",
        )
        previous = t
    span = points[-1]["seconds"] - points[0]["seconds"]
    require(
        finite(duration, 1, 600) and span >= duration, "sustained_duration_incomplete"
    )
    times = [row["seconds"] - points[0]["seconds"] for row in points]
    values = [row["hostRssBytes"] for row in points]
    mean_t, mean_v = sum(times) / len(times), sum(values) / len(values)
    slope = sum((t - mean_t) * (v - mean_v) for t, v in zip(times, values)) / sum(
        (t - mean_t) ** 2 for t in times
    )
    return {
        "durationSeconds": span,
        "releasedSamples": len(points),
        "hostSlopeBytesPerMinute": slope * 60,
        "hostGrowthBytes": values[-1] - values[0],
        "baselineHostRssBytes": values[0],
        "finalHostRssBytes": values[-1],
        "maximumReleasedSampleGapSeconds": max(b - a for a, b in zip(times, times[1:])),
    }


def gates(tree_peak, growth, limits):
    require(
        finite(tree_peak, 1) and type(tree_peak) is int and finite(growth, -(2**64)),
        "sustained_memory_missing",
    )
    require(
        set(limits) == set(DEFAULT_LIMITS)
        and all(
            type(value) is int and 0 < value <= 16 * 1024**3
            for value in limits.values()
        ),
        "sustained_invalid_limits",
    )
    return [
        {
            "name": name,
            "observed": value,
            "maximum": limits[name],
            "passed": value <= limits[name],
        }
        for name, value in (("treeRssBytes", tree_peak), ("hostGrowthBytes", growth))
    ]


def counter(value):
    require(
        type(value) is int
        or (
            isinstance(value, str)
            and value.isascii()
            and value.isdigit()
            and len(value) <= 20
        ),
        "sustained_invalid_counter",
    )
    result = int(value)
    require(0 <= result < 2**64, "sustained_invalid_counter")
    return result


def source_metadata(status):
    require(
        isinstance(status, dict) and isinstance(status.get("source"), dict),
        "sustained_invalid_source",
    )
    source = status["source"]
    require(
        isinstance(source.get("sourceId"), str)
        and 0 < len(source["sourceId"]) <= 256
        and isinstance(source.get("generation"), str),
        "sustained_invalid_source_identity",
    )
    counter(source["generation"])
    return source


def verify_job(status, size, rows, identity):
    source = source_metadata(status)
    require(
        status.get("state") == "completed"
        and status.get("complete") is True
        and counter(status.get("scannedLines")) == rows
        and counter(source.get("indexedBytes")) == size
        and all(
            isinstance(value, str) and source.get(key) == value and bool(value)
            for key, value in identity.items()
        )
        and set(identity) == {"sourceId", "generation"},
        "sustained_job_oracle_mismatch",
    )


def verify_count(page, count):
    rows = page.get("rows")
    require(
        isinstance(rows, list)
        and len(rows) == 1
        and isinstance(rows[0], dict)
        and rows[0].get("value") == TOKEN
        and rows[0].get("count") == str(count)
        and not page.get("nextCursor"),
        "sustained_count_oracle_mismatch",
    )


def verify_page(page, job, status):
    revision = status.get("requestRevision")
    require(isinstance(revision, str), "sustained_invalid_revision")
    counter(revision)
    require(
        page.get("jobId") == job
        and page.get("source") == status.get("source")
        and isinstance(status.get("requestRevision"), str)
        and page.get("requestRevision") == status["requestRevision"],
        "sustained_page_provenance_mismatch",
    )


def release_checked(host, job, deadline=None):
    deadline = clock() + 15 if deadline is None else deadline
    host.command(
        "release_analysis_job", {"job_id": job}, timeout=min(15, remaining(deadline))
    )
    host.jobs.discard(job)
    expired = host.command(
        "get_analysis_job",
        {"job_id": job},
        allow_error=True,
        timeout=min(15, remaining(deadline)),
    )
    require(expired.get("reason") == "job_expired", "sustained_job_not_released")
    LOG.debug("sustained_job_released job_id=%s", job)


def submit_job(host, request, action, deadline):
    result = host.command(
        action,
        {"analysis_json": request},
        allow_error=True,
        timeout=min(15, remaining(deadline)),
    )
    if "jobId" in result:
        job = result["jobId"]
        require(
            isinstance(job, str) and 0 < len(job) <= 128, "sustained_invalid_job_id"
        )
        host.jobs.add(job)
        LOG.debug("sustained_submitted action=%s job_id=%s", action, job)
    return result


class DeadlineHost(BenchmarkHost):
    def __init__(self, executable, output, deadline):
        super().__init__(executable, output)
        self.deadline = deadline

    def send(self, method, parameters, timeout=15):
        return super().send(method, parameters, min(timeout, remaining(self.deadline)))


class MemoryTrace:
    def __init__(self, sampler, host_pid):
        require(sampler.backend is not None, "sustained_sampler_unsupported")
        self.sampler, self.host_pid = sampler, host_pid
        self.origin = clock()
        self.samples, self.points = [], []
        self.tree_peak = 0

    def sample(self, phase, released=False):
        started = clock()
        if (
            self.samples
            and not released
            and started - self.origin - self.samples[-1]["seconds"] < 0.1
        ):
            return
        require(len(self.samples) < MAX_SAMPLES, "sustained_sample_limit")
        snapshot = self.sampler.backend.snapshot()
        finished = clock()
        self.sampler.samples.record(snapshot, started, finished)
        values = snapshot["rssByPid"]
        require(self.host_pid in values, "sustained_host_not_observed")
        row = {
            "seconds": finished - self.origin,
            "hostRssBytes": values[self.host_pid],
            "treeRssBytes": sum(values.values()),
            "phase": phase,
        }
        self.samples.append(row)
        self.tree_peak = max(self.tree_peak, row["treeRssBytes"])
        if released:
            self.points.append({key: row[key] for key in ("seconds", "hostRssBytes")})
            LOG.info(
                "sustained_released_sample elapsed=%.3f host_rss=%d tree_rss=%d",
                row["seconds"],
                row["hostRssBytes"],
                row["treeRssBytes"],
            )


def wait_initial_source(host, source, deadline):
    deadline = min(deadline, clock() + 15)
    while True:
        try:
            state = host.command(
                "dump_state", {"window_index": 0}, timeout=min(15, remaining(deadline))
            )["ciloggState"]
        except RunError as error:
            if str(error) != "command_failed_dump_state":
                raise
            LOG.debug("sustained_initial_source_retry")
        else:
            require(isinstance(state, dict), "sustained_invalid_initial_state")
            if (
                state.get("loadingInProgress") is False
                and Path(state.get("activeFile", "")).resolve() == source.resolve()
            ):
                LOG.info(
                    "sustained_initial_source_ready bytes=%d",
                    source.stat().st_size if source.exists() else 0,
                )
                return
        remaining(deadline)
        time.sleep(0.02)


def cycle(host, trace, source, rows, count, identity, deadline):
    with source.open("ab") as stream:
        stream.write(APPEND)
    rows, count = rows + 16, count + 16
    size = source.stat().st_size
    for action in ("regex_evaluate", "value_counts"):
        ready_deadline = min(deadline, clock() + 15)
        while True:
            remaining(ready_deadline)
            result = submit_job(
                host,
                {
                    "scope": "whole_file",
                    "pattern": "G02F_NEVER_MATCH"
                    if action == "regex_evaluate"
                    else TOKEN,
                    "limits": {"timeoutMs": 30000},
                },
                action,
                ready_deadline,
            )
            if "jobId" not in result:
                require(
                    result.get("reason") == "source_not_ready",
                    "sustained_submit_failed",
                )
                time.sleep(0.02)
                continue
            job = result["jobId"]
            status = host.status(job, min(15, remaining(ready_deadline)))
            if counter(source_metadata(status).get("indexedBytes")) == size:
                break
            LOG.debug("sustained_stale_snapshot job_id=%s expected_bytes=%d", job, size)
            host.command(
                "cancel_analysis_job",
                {"job_id": job},
                timeout=min(15, remaining(ready_deadline)),
            )
            release_checked(host, job, ready_deadline)
            time.sleep(0.02)
        terminal, _ = host.finish(
            job, min(30, remaining(deadline)), lambda _: trace.sample("matching")
        )
        metadata = source_metadata(terminal)
        current = {key: metadata[key] for key in ("sourceId", "generation")}
        if identity is None:
            identity = current
        verify_job(terminal, size, rows, identity)
        page = host.command("get_analysis_results", {"job_id": job})
        verify_page(page, job, terminal)
        if action == "value_counts":
            verify_count(page, count)
        else:
            require(
                page.get("rows") == [] and not page.get("nextCursor"),
                "sustained_regex_oracle_mismatch",
            )
        release_checked(host, job, deadline)
        LOG.debug(
            "sustained_job_verified action=%s job_id=%s source_id=%s generation=%s bytes=%d rows=%d count=%d",
            action,
            job,
            identity["sourceId"],
            identity["generation"],
            size,
            rows,
            count,
        )
    require(not host.jobs, "sustained_jobs_retained")
    return rows, count, identity


def run(options, manifest, report):
    output = options.output / ("run-" + str(report["repetition"]))
    output.mkdir()
    source = output / "source.log"
    shutil.copyfile(options.corpus / "ascii.log", source)
    entry = next(row for row in manifest["files"] if row["path"] == "ascii.log")
    require(file_hash(source) == entry["sha256"], "sustained_corpus_changed")
    deadline = clock() + options.duration_seconds + 120
    host = DeadlineHost(options.executable, output, deadline)
    try:
        with host:
            report.update(
                hostPid=host.host.pid,
                binarySha256=host.binary_hash,
                version=host.version,
            )
            trace = MemoryTrace(
                ResidentMemorySampler(host.owner, host.host.pid), host.host.pid
            )
            report.update(samples=trace.samples, releasedPoints=trace.points)
            host.command("open_file", {"file": str(source)})
            wait_initial_source(host, source, deadline)
            rows, count, identity = manifest["rows"], 0, None
            for _ in range(3):
                rows, count, identity = cycle(
                    host, trace, source, rows, count, identity, deadline
                )
            trace.sample("baseline", released=True)
            started = trace.origin + trace.points[0]["seconds"]
            cycles = 0
            interval = options.duration_seconds / 60
            while clock() - started < options.duration_seconds:
                require(cycles < MAX_CYCLES, "sustained_cycle_limit")
                rows, count, identity = cycle(
                    host, trace, source, rows, count, identity, deadline
                )
                cycles += 1
                trace.sample("released", released=True)
                until = min(
                    started + options.duration_seconds, started + cycles * interval
                )
                while clock() < until:
                    trace.sample("idle")
                    time.sleep(min(0.1, until - clock()) if until > clock() else 0)
            trace.sample("final", released=True)
            report.update(
                cycles=cycles,
                warmupCycles=3,
                source=identity,
                finalSourceSha256=file_hash(source),
                finalSourceBytes=source.stat().st_size,
                finalRows=rows,
                appendedTokenCount=count,
            )
            summary = summarize(trace.points, options.duration_seconds)
            resident = trace.sampler.result()
            resident.update(
                scope="application_host_and_owned_descendants",
                workerRole="all_owned_non_host_members_including_mcp",
                start="first_poll_during_first_warmup_matching_after_source_readiness",
                end="after_final_released_sample_before_host_cleanup",
                opportunisticProbeMinimumSpacingMs=100,
                releasedSamplesForced=True,
            )
            resident.pop("intervalRequestedMs", None)
            report.update(
                summary=summary,
                resident=resident,
                gates=gates(
                    trace.tree_peak, summary["hostGrowthBytes"], options.limits
                ),
            )
            require(
                all(row["passed"] for row in report["gates"]),
                "sustained_memory_gate_failed",
            )
    finally:
        report["ownedProcessesReaped"] = all(
            p.poll() is not None for p in host.owner.processes
        )
    require(report["ownedProcessesReaped"], "sustained_process_leak")


def supported():
    return os.name == "nt"


def main(arguments=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--corpus", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--duration-seconds", type=int, default=120)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--debug", action="store_true")
    parser.add_argument(
        "--max-tree-rss-bytes", type=int, default=DEFAULT_LIMITS["treeRssBytes"]
    )
    parser.add_argument(
        "--max-host-growth-bytes", type=int, default=DEFAULT_LIMITS["hostGrowthBytes"]
    )
    options = parser.parse_args(arguments)
    logging.basicConfig(
        level=logging.DEBUG if options.debug else logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s %(message)s",
    )
    if options.output.exists():
        LOG.error("sustained_setup_rejected reason=output_exists")
        return 2
    options.output = options.output.resolve()
    options.output.mkdir(parents=True)
    report = {
        "schemaVersion": 1,
        "scenario": "sustained-analysis",
        "passed": False,
        "state": "invalid_setup",
        "releaseGateStatus": "pending",
        "runs": [],
        "evidenceKind": "test_double"
        if options.executable.suffix.lower() == ".py"
        else "application",
        "recordedAtUtc": datetime.now(timezone.utc).isoformat(),
        "machine": {
            "platform": platform.platform(),
            "python": platform.python_version(),
        },
        "platformRequested": "offscreen",
        "cachePolicy": "os_cache_uncontrolled",
        "sampling": "between_commands_and_job_polls; gaps recorded; non-atomic; shared pages per process",
        "growth": "final minus baseline host RSS after completed/released jobs; growing source remains open",
        "slope": "least-squares host RSS trend after release; observed, not a calibrated budget",
        "unmeasured": [
            "inter_poll_peaks",
            "worker_peak_rss",
            "compositor",
            "native_linux_macos",
            "hours_long_memory_slope",
        ],
    }
    code = 2
    try:
        if not supported():
            raise ValueError("sustained_windows_required")
        if (
            not 10 <= options.duration_seconds <= 600
            or not 1 <= options.repetitions <= 3
        ):
            raise ValueError("sustained_invalid_configuration")
        options.limits = {
            "treeRssBytes": options.max_tree_rss_bytes,
            "hostGrowthBytes": options.max_host_growth_bytes,
        }
        gates(1, 0, options.limits)
        options.executable, options.corpus = (
            options.executable.resolve(),
            options.corpus.resolve(),
        )
        if (
            not options.executable.is_file()
            or not 0 < options.executable.stat().st_size <= 512 * 1024**2
        ):
            raise ValueError("sustained_invalid_executable")
        manifest, digest = validate_corpus(options.corpus)
        report.update(
            binarySha256=file_hash(options.executable),
            corpusManifestSha256=digest,
            seed=manifest.get("seed"),
            rows=manifest["rows"],
            limits=options.limits,
            durationRequestedSeconds=options.duration_seconds,
            harnessSha256=file_hash(Path(__file__)),
            state="running",
        )
        code = 1
        for repetition in range(1, options.repetitions + 1):
            item = {"repetition": repetition}
            report["runs"].append(item)
            run(options, manifest, item)
        report.update(passed=True, state="passed")
        code = 0
    except (
        OSError,
        ValueError,
        RuntimeError,
        TypeError,
        AttributeError,
        KeyError,
        StopIteration,
        OverflowError,
        subprocess.SubprocessError,
    ) as error:
        report.update(
            state="failed" if code == 1 else "invalid_setup",
            reason=str(error)
            if isinstance(error, (RunError, ValueError))
            else type(error).__name__,
        )
        LOG.error("sustained_failed reason=%s", report["reason"])
    finally:
        try:
            output = json.dumps(report, indent=2, allow_nan=False)
            require(len(output.encode()) <= 4 * 1024**2, "sustained_report_limit")
        except (ValueError, TypeError, OverflowError, RunError):
            report.update(
                passed=False, state="failed", reason="sustained_report_invalid"
            )
            report.pop("runs", None)
            output = json.dumps(report, indent=2, allow_nan=False)
            code = 1
        (options.output / "result.json").write_text(output, encoding="utf-8")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
