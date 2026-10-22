"""Bounded native MCP client for a private, process-supervised benchmark host."""

import hashlib
import json
import logging
import os
import queue
import shutil
import subprocess
import sys
import threading
import time
import uuid
from time import perf_counter as clock

from workbench_process import ProcessOwner, peak_host_working_set

LOG = logging.getLogger("workbench.runtime")
MESSAGE_BYTES = 8 * 1024 * 1024
# Windows Python 3.12 can use a coarse monotonic clock. The monotonic
# performance counter resolves fast fixtures and also supplies all deadlines.


class RunError(RuntimeError):
    operation = None


def executable_arguments(path):
    # Python protocol doubles are explicitly labelled as synthetic evidence.
    return [sys.executable, str(path)] if path.suffix.lower() == ".py" else [str(path)]


class BenchmarkHost:
    def __init__(self, executable, output):
        self.original = executable.resolve()
        self.output = output.resolve()
        self.owner = ProcessOwner()
        self.replies = queue.Queue(maxsize=8)
        self.outgoing = queue.Queue(maxsize=8)
        self.sequence = 0
        self.jobs = set()
        self.server = None
        self.host = None
        self.logs = []
        self.reader = None
        self.writer = None

    def __enter__(self):
        try:
            self.start()
        except BaseException:
            self.__exit__(*sys.exc_info())
            raise
        return self

    def start(self):
        payload = self.output / "payload"
        payload.mkdir()
        self.executable = payload / (
            "cilogg-benchmark-" + uuid.uuid4().hex + self.original.suffix
        )
        shutil.copy2(self.original, self.executable)
        self.binary_hash = file_hash(self.executable)
        for path in self.original.parent.iterdir():
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
            folder = self.original.parent / name
            if folder.is_dir():
                shutil.copytree(folder, payload / name)
        (payload / "cilogg.conf").write_text(
            "[General]\nversion=1\nversionchecker.enabled=false\n", encoding="utf-8"
        )
        settings = self.output / "settings"
        settings.mkdir()
        self.environment = dict(
            os.environ,
            CILOGG_AUTOMATION="1",
            QT_QPA_PLATFORM="offscreen",
            PYTHONUTF8="1",
            APPDATA=str(settings),
            LOCALAPPDATA=str(settings),
            XDG_CONFIG_HOME=str(settings),
            XDG_DATA_HOME=str(settings),
        )
        self.prefix = executable_arguments(self.executable)
        version_process = self.owner.spawn(
            self.prefix + ["--version"],
            env=self.environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        version_output, _ = version_process.communicate(timeout=20)
        if version_process.returncode:
            raise RunError("version_process_failed")
        self.version = version_output.decode("utf-8", errors="replace")[:4096]
        host_log = (self.output / "host.log").open("wb")
        server_log = (self.output / "mcp.log").open("wb")
        self.logs.extend((host_log, server_log))
        started = clock()
        self.host = self.owner.spawn(
            self.prefix
            + ["--multi", "--new-session", "-d", "4", "-platform", "offscreen"],
            env=self.environment,
            stdout=host_log,
            stderr=host_log,
        )
        deadline = started + 30
        while True:
            if self.host.poll() is not None:
                raise RunError("host_startup_failed")
            with (self.output / "host.log").open("rb") as source:
                if b"Klogg instance" in source.read(1024 * 1024):
                    break
            if clock() >= deadline:
                raise RunError("host_startup_timeout")
            time.sleep(0.02)
        self.server = self.owner.spawn(
            self.prefix + ["mcp", "serve"],
            env=self.environment,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=server_log,
        )
        self.reader = threading.Thread(target=self.read_replies, daemon=True)
        self.reader.start()
        self.writer = threading.Thread(target=self.write_requests, daemon=True)
        self.writer.start()
        self.send(
            "initialize",
            {
                "protocolVersion": "2025-11-25",
                "capabilities": {},
                "clientInfo": {"name": "cilogg-workbench-benchmark", "version": "1"},
            },
        )
        self.notify("notifications/initialized", {})
        catalog = self.send(
            "tools/call",
            {"name": "cilogg_commands", "arguments": {"area": "commander"}},
        )
        commands = catalog["structuredContent"]["commander"]["commands"]
        required = {
            "regex_evaluate",
            "value_counts",
            "get_analysis_job",
            "get_analysis_results",
            "cancel_analysis_job",
            "release_analysis_job",
        }
        if not required.issubset(commands):
            raise RunError("required_operations_missing")
        self.command("get_info")
        self.startup_seconds = clock() - started
        LOG.info(
            "host_ready pid=%d startup_seconds=%.6f",
            self.host.pid,
            self.startup_seconds,
        )

    def read_replies(self):
        try:
            while True:
                line = self.server.stdout.readline(MESSAGE_BYTES + 1)
                if not line:
                    raise RunError("worker_exited")
                if len(line) > MESSAGE_BYTES:
                    raise RunError("protocol_message_limit")
                self.replies.put(json.loads(line.decode("utf-8")), timeout=1)
        except (OSError, ValueError, RuntimeError, queue.Full) as error:
            try:
                self.replies.put(
                    {
                        "runtimeError": "protocol_failure",
                        "detail": type(error).__name__,
                    },
                    timeout=1,
                )
            except queue.Full:
                pass

    def write_requests(self):
        # A peer that stops reading must not block the caller or its deadline.
        try:
            while True:
                encoded = self.outgoing.get()
                if encoded is None:
                    return
                self.server.stdin.write(encoded)
                self.server.stdin.flush()
        except (OSError, ValueError):
            try:
                self.replies.put({"runtimeError": "protocol_failure"}, timeout=1)
            except queue.Full:
                pass

    def notify(self, method, parameters):
        self.outgoing.put(
            (
                json.dumps({"jsonrpc": "2.0", "method": method, "params": parameters})
                + "\n"
            ).encode("utf-8"),
            timeout=1,
        )

    def send(self, method, parameters, timeout=15):
        deadline = clock() + max(0.001, timeout)
        self.sequence += 1
        request = {
            "jsonrpc": "2.0",
            "id": self.sequence,
            "method": method,
            "params": parameters,
        }
        encoded = (json.dumps(request, ensure_ascii=False) + "\n").encode("utf-8")
        if len(encoded) > MESSAGE_BYTES:
            raise RunError("request_message_limit")
        try:
            self.outgoing.put(encoded, timeout=max(0.001, deadline - clock()))
            reply = self.replies.get(timeout=max(0.001, deadline - clock()))
        except (queue.Empty, queue.Full) as error:
            raise RunError("transport_timeout") from error
        if (
            not isinstance(reply, dict)
            or "runtimeError" in reply
            or reply.get("id") != self.sequence
            or "error" in reply
            or not isinstance(reply.get("result"), dict)
        ):
            raise RunError("protocol_failure")
        return reply["result"]

    def command(self, action, arguments=None, timeout=15, allow_error=False):
        started = clock()
        LOG.debug(
            "commander_request action=%s sequence=%d timeout_seconds=%.6f",
            action,
            self.sequence + 1,
            timeout,
        )
        try:
            result = self.send(
                "tools/call",
                {
                    "name": "cilogg_execute",
                    "arguments": {"action": action, "arguments": arguments or {}},
                },
                timeout,
            )
        except RunError as error:
            error.operation = action
            LOG.error(
                "commander_failed action=%s reason=%s elapsed_seconds=%.6f",
                action,
                error,
                clock() - started,
            )
            raise
        LOG.debug(
            "commander_response action=%s elapsed_seconds=%.6f",
            action,
            clock() - started,
        )
        structured = result.get("structuredContent", {})
        if not isinstance(structured, dict):
            raise RunError("protocol_failure")
        if result.get("isError") and not allow_error:
            raise RunError("command_failed_" + action)
        payload = structured.get("payload", structured)
        if not isinstance(payload, dict):
            raise RunError("protocol_failure")
        return payload

    def start_job(self, request, action="regex_evaluate"):
        result = self.command(action, {"analysis_json": request}, allow_error=True)
        if "jobId" in result:
            self.jobs.add(result["jobId"])
            LOG.debug("analysis_submitted action=%s job_id=%s", action, result["jobId"])
        return result

    def status(self, job, timeout=15):
        return self.command("get_analysis_job", {"job_id": job}, timeout=timeout)

    def finish(self, job, seconds, observe=None):
        deadline = clock() + seconds
        probes = []
        while clock() < deadline:
            started = clock()
            self.command("get_info", timeout=deadline - started)
            probes.append(clock() - started)
            status = self.status(job, max(0.001, deadline - clock()))
            LOG.debug(
                "analysis_observed job_id=%s state=%s complete=%s scanned=%s",
                job,
                status.get("state"),
                status.get("complete"),
                status.get("scannedLines"),
            )
            if observe is not None:
                observe(status)
            if status["state"] in ("completed", "failed", "cancelled"):
                return status, probes
            time.sleep(min(0.02, max(0, deadline - clock())))
        raise RunError("job_timeout")

    def release(self, job):
        self.command("release_analysis_job", {"job_id": job})
        self.jobs.discard(job)

    def __exit__(self, error_type, error, traceback):
        cleanup_error = None
        try:
            if self.server and self.server.poll() is None:
                if error is None:
                    for job in list(self.jobs):
                        self.command("cancel_analysis_job", {"job_id": job}, timeout=2)
                        self.release(job)
                    self.command("close_cilogg", timeout=2)
                if error is None:
                    self.server.stdin.close()
                    try:
                        self.server.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        pass
        except (OSError, ValueError, RuntimeError) as failure:
            cleanup_error = failure
        finally:
            self.owner.close()
            self.outgoing.put_nowait(None)
            if self.writer:
                self.writer.join(timeout=2)
            if self.reader:
                self.reader.join(timeout=2)
            for log in self.logs:
                log.close()
        if cleanup_error is not None:
            LOG.error("graceful_cleanup_failed kind=%s", type(cleanup_error).__name__)
            if error is None:
                raise RunError("cleanup_failure") from cleanup_error


def file_hash(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def measure_analysis(host, corpus, manifest, output, budgets, repetition):
    source = output / f"scan-{repetition}.log"
    shutil.copyfile(corpus / "ascii.log", source)
    source_entry = next(
        entry for entry in manifest["files"] if entry["path"] == "ascii.log"
    )
    if file_hash(source) != source_entry["sha256"]:
        raise RunError("corpus_changed_during_run")
    size = source.stat().st_size
    rows = manifest["rows"]
    indexing_started = clock()
    deadline = indexing_started + budgets["jobSeconds"]
    host.command("open_file", {"file": str(source)}, timeout=budgets["jobSeconds"])
    request = {
        "scope": "whole_file",
        "pattern": "WORKBENCH_NO_SUCH_TOKEN",
        "limits": {"timeoutMs": 60000},
    }
    while clock() < deadline:
        result = host.start_job(request)
        if "jobId" in result:
            job = result["jobId"]
            snapshot = host.status(job, deadline - clock())
            if int(snapshot["source"]["indexedBytes"]) == size:
                LOG.debug(
                    "snapshot_frozen job_id=%s source_id=%s generation=%s indexed_bytes=%d lines=%d",
                    job,
                    snapshot["source"].get("sourceId"),
                    snapshot["source"].get("generation"),
                    size,
                    rows,
                )
                break
            host.command("cancel_analysis_job", {"job_id": job})
            host.release(job)
        elif result.get("reason") != "source_not_ready":
            raise RunError("source_snapshot_failed")
        time.sleep(0.02)
    else:
        raise RunError("job_timeout")
    ready_seconds = clock() - indexing_started
    if snapshot["state"] not in ("queued", "running"):
        raise RunError("append_workload_already_terminal")
    # This is a frozen run: the mutable working copy is separate from the corpus.
    with source.open("ab") as stream:
        stream.write(b"appended\n")
    append_started = clock()
    publication = []

    def observe_append(status):
        elapsed = clock() - append_started
        if status.get("newerDataAvailable") and not publication:
            publication.append(elapsed)
        if not publication and elapsed > budgets["appendSeconds"]:
            raise RunError("append_publication_timeout")

    terminal, probes = host.finish(job, max(0.001, deadline - clock()), observe_append)
    elapsed = clock() - indexing_started
    if not terminal.get("complete") or int(terminal["scannedLines"]) != rows:
        raise RunError("scan_incomplete")
    if int(terminal["source"]["indexedBytes"]) != size:
        raise RunError("snapshot_extended_after_append")
    while not terminal.get("newerDataAvailable"):
        if clock() - append_started >= budgets["appendSeconds"]:
            raise RunError("append_publication_timeout")
        terminal = host.status(job, budgets["appendSeconds"])
        time.sleep(0.02)
    observe_append(terminal)
    append_seconds = publication[0]
    if append_seconds > budgets["appendSeconds"]:
        raise RunError("append_publication_timeout")
    host.release(job)
    count_job = host.start_job(
        {"scope": "whole_file", "pattern": " ERROR "}, "value_counts"
    ).get("jobId")
    if not count_job:
        raise RunError("count_job_rejected")
    count_status, _ = host.finish(count_job, budgets["jobSeconds"])
    page = host.command("get_analysis_results", {"job_id": count_job})
    if (
        not count_status.get("complete")
        or sum(int(row["count"]) for row in page["rows"]) != (rows + 9) // 10
    ):
        raise RunError("count_oracle_mismatch")
    if page.get("nextCursor"):
        raise RunError("unexpected_count_page")
    host.release(count_job)
    cancel_job = host.start_job(
        {
            "scope": "text",
            "text": "a" * 100000 + "!",
            "pattern": "(*NO_JIT)(*NO_START_OPT)(*NO_AUTO_POSSESS)a+b",
            "limits": {"batchTimeoutMs": 30000},
        }
    ).get("jobId")
    if not cancel_job:
        raise RunError("cancel_job_rejected")
    before_cancel = host.status(cancel_job)
    if before_cancel["state"] not in ("queued", "running"):
        raise RunError("cancel_workload_already_terminal")
    cancel_started = clock()
    host.command(
        "cancel_analysis_job",
        {"job_id": cancel_job},
        timeout=budgets["publicCancellationSeconds"],
    )
    cancelled, _ = host.finish(
        cancel_job,
        max(
            0.001,
            budgets["publicCancellationSeconds"] - (clock() - cancel_started),
        ),
    )
    cancel_seconds = clock() - cancel_started
    if (
        cancelled["state"] != "cancelled"
        or cancel_seconds > budgets["publicCancellationSeconds"]
    ):
        raise RunError("cancellation_gate_failed")
    host.release(cancel_job)
    peak = peak_host_working_set(host.host)
    if peak is not None and peak > budgets["hostPeakBytes"]:
        raise RunError("host_memory_gate_failed")
    LOG.info(
        "analysis_measured repetition=%d lines=%d bytes=%d elapsed_seconds=%.6f cancel_seconds=%.6f",
        repetition,
        rows,
        size,
        elapsed,
        cancel_seconds,
    )
    if elapsed <= 0:
        LOG.warning("scan_rate_unavailable reason=clock_resolution bytes=%d", size)
    return {
        "repetition": repetition,
        "processState": "cold_process" if repetition == 0 else "warm_process",
        "rawBytes": size,
        "scannedLines": rows,
        "indexAndSubmissionSeconds": ready_seconds,
        "scanSecondsIncludingProtocol": elapsed,
        "scanMiBPerSecondIncludingProtocol": size / (1024 * 1024) / elapsed
        if elapsed > 0
        else None,
        "scanTimingStatus": "measured" if elapsed > 0 else "below_clock_resolution",
        "timingClock": "perf_counter",
        "appendPublicationSecondsIncludingProtocol": append_seconds,
        "appendWhileActive": snapshot["state"] in ("queued", "running"),
        "cancelSecondsIncludingProtocol": cancel_seconds,
        "commanderRoundTripSeconds": probes,
        "hostPeakWorkingSetBytes": peak,
        "workerMetrics": terminal.get("metrics", {}),
        "serviceTiming": terminal.get("timing", {}),
        "captureBackend": terminal.get("captureBackend", "unreported"),
        "sourceSnapshot": {
            key: terminal["source"].get(key)
            for key in ("sourceId", "generation", "revision", "indexedBytes")
        },
        "gates": {
            "frozenScan": True,
            "counts": True,
            "cancellation": True,
            "appendPublication": True,
            "hostPeakMemory": "passed" if peak is not None else "unavailable",
        },
    }
