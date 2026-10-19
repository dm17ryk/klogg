"""Exercise analysis through public CLI and native MCP against an isolated host.

Uses only the Python standard library. The host uses its own runtime directory
and portable settings; only processes created here are terminated.
"""

import argparse
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import sys
import threading
import time
import uuid
from typing import Optional


def peak_working_set(pid: int) -> Optional[int]:
    """Return the Windows host peak; other platforms explicitly report unavailable."""
    if os.name != "nt":
        return None
    import ctypes
    from ctypes import wintypes

    class Counters(ctypes.Structure):
        _fields_ = [("cb", wintypes.DWORD), ("faults", wintypes.DWORD)] + [
            (name, ctypes.c_size_t) for name in (
                "peak", "working", "peak_paged", "paged", "peak_nonpaged", "nonpaged",
                "pagefile", "peak_pagefile",
            )
        ]

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    kernel.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
    psapi.GetProcessMemoryInfo.argtypes = (wintypes.HANDLE, ctypes.POINTER(Counters), wintypes.DWORD)
    handle = kernel.OpenProcess(0x410, False, pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        counters = Counters()
        counters.cb = ctypes.sizeof(counters)
        if not psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb):
            raise ctypes.WinError(ctypes.get_last_error())
        return counters.peak
    finally:
        kernel.CloseHandle(handle)


def measure_file_analysis(cli, host, artifacts: Path) -> None:
    """Measure a frozen 64 MiB scan, append coexistence and public UI round trips."""
    fixture = artifacts / "throughput.log"
    block = (b"row " + b"x" * 59 + b"\n") * 16384
    with fixture.open("wb") as output:
        for _ in range(64):
            output.write(block)
    frozen_bytes = fixture.stat().st_size
    frozen_lines = 64 * 16384
    cli("open_file", "--file", str(fixture))
    request = json.dumps({"pattern": "this_marker_is_absent", "scope": "whole_file",
                          "limits": {"timeoutMs": 120000}})
    deadline = time.monotonic() + 60
    while True:
        started = time.monotonic()
        result = cli("regex_evaluate", "--analysis-json", request, success=None)
        if "jobId" not in result:
            assert result.get("reason") == "source_not_ready", result
        else:
            job = result["jobId"]
            initial = cli("get_analysis_job", "--job-id", job)
            if int(initial["source"]["indexedBytes"]) == frozen_bytes:
                break
            cli("release_analysis_job", "--job-id", job)
        assert time.monotonic() < deadline, "Benchmark source did not finish indexing"
        time.sleep(0.02)
    peak_before = peak_working_set(host.pid)
    with fixture.open("ab") as output:
        output.write(b"appended\n")
    after_append = cli("get_analysis_job", "--job-id", job)
    appended_while_active = after_append["state"] in ("queued", "running")
    latencies = []
    deadline = time.monotonic() + 140
    while True:
        probe = time.monotonic()
        cli("get_info")
        latencies.append((time.monotonic() - probe) * 1000)
        status = cli("get_analysis_job", "--job-id", job)
        if status["state"] in ("completed", "cancelled", "failed"):
            break
        assert time.monotonic() < deadline, "Benchmark analysis did not terminate"
    elapsed = time.monotonic() - started
    assert status["complete"] and status["scannedLines"] == frozen_lines, status
    assert int(status["source"]["indexedBytes"]) == frozen_bytes, status
    deadline = time.monotonic() + 15
    while not status["newerDataAvailable"]:
        assert time.monotonic() < deadline, "Append publication was not observed"
        status = cli("get_analysis_job", "--job-id", job)
        time.sleep(0.02)
    measurements = {"rawBytes": frozen_bytes, "physicalLines": frozen_lines,
                    "elapsedSecondsIncludingCli": elapsed,
                    "scanMiBPerSecondIncludingCli": frozen_bytes / (1024 * 1024) / elapsed,
                    "appendWhileActive": appended_while_active,
                    "uiCommanderRoundTripMs": latencies,
                    "peakHostWorkingSetBytesBefore": peak_before,
                    "peakHostWorkingSetBytesAfter": peak_working_set(host.pid),
                    "workerMetrics": status["metrics"], "serviceTiming": status["timing"]}
    (artifacts / "measurements.json").write_text(json.dumps(measurements, indent=2), encoding="utf-8")
    cli("release_analysis_job", "--job-id", job)
    print("PASS: frozen-file throughput, append publication and UI round trips measured")


def run(executable: Path, artifacts: Path, regex_lab: bool = False, measure: bool = False) -> None:
    artifacts.mkdir(parents=True, exist_ok=True)
    payload = artifacts / f"payload-{uuid.uuid4().hex}"
    payload.mkdir()
    isolated = payload / f"cilogg-analysis-{uuid.uuid4().hex}{executable.suffix}"
    shutil.copy2(executable, isolated)
    for file in executable.parent.iterdir():
        if file.is_file() and file.suffix.lower() in (".dll", ".qm", ".rcc"):
            shutil.copy2(file, payload / file.name)
    for name in ("platforms", "styles", "imageformats", "iconengines", "tls", "networkinformation", "translations", "resources"):
        folder = executable.parent / name
        if folder.is_dir():
            shutil.copytree(folder, payload / name)
    (payload / "cilogg.conf").write_text("[General]\n", encoding="utf-8")
    environment = dict(os.environ, CILOGG_AUTOMATION="1", QT_QPA_PLATFORM="offscreen")
    home = artifacts / "settings"
    home.mkdir(exist_ok=True)
    environment.update(APPDATA=str(home), LOCALAPPDATA=str(home), XDG_CONFIG_HOME=str(home))
    host = None
    server = None
    sequence = 0
    replies = queue.Queue()

    def cli(action: str, *arguments: str, success=True) -> dict:
        result = subprocess.run(
            [str(isolated), "command", "--action", action, "--require-running", *arguments],
            capture_output=True, encoding="utf-8", env=environment, timeout=45,
        )
        if success is not None:
            assert (result.returncode == 0) == success, (result.returncode, result.stderr, result.stdout)
        return json.loads(result.stdout) if result.stdout.strip() else {}

    def send(method: str, params: dict, notification=False) -> dict:
        nonlocal sequence
        sequence += 1
        request = {"jsonrpc": "2.0", "method": method, "params": params}
        if not notification:
            request["id"] = sequence
        server.stdin.write(json.dumps(request, ensure_ascii=False) + "\n")
        server.stdin.flush()
        if notification:
            return {}
        reply = replies.get(timeout=50)
        assert reply is not None, "MCP server exited"
        assert reply.get("id") == sequence, reply
        assert "error" not in reply, reply
        return reply["result"]

    def command(action: str, arguments=None, success=True) -> dict:
        result = send("tools/call", {"name": "cilogg_execute", "arguments": {
            "action": action, "arguments": arguments or {},
        }})
        assert bool(result.get("isError")) != success, result
        return result["structuredContent"]["payload"]

    def finish(job: str, through_mcp: bool) -> dict:
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            status = command("get_analysis_job", {"job_id": job}) if through_mcp else cli("get_analysis_job", "--job-id", job)
            if status["state"] in ("completed", "cancelled", "failed"):
                return status
            time.sleep(0.02)
        raise AssertionError(f"Analysis did not finish: {job}")

    def rows(job: str, through_mcp: bool) -> list:
        result = []
        cursor = ""
        seen = set()
        deadline = time.monotonic() + 60
        while True:
            assert time.monotonic() < deadline and cursor not in seen, "Pagination stalled"
            seen.add(cursor)
            page = command("get_analysis_results", {"job_id": job, "page_size": 1, "cursor": cursor}) if through_mcp else cli(
                "get_analysis_results", "--job-id", job, "--page-size", "1", "--cursor", cursor)
            result.extend(page["rows"])
            cursor = page["nextCursor"]
            if not cursor:
                return result

    def normalized(values: list) -> list:
        return [{key: value for key, value in row.items() if key not in ("jobId", "requestRevision", "source")} for row in values]

    try:
        with (artifacts / "host.log").open("w", encoding="utf-8") as host_log, (artifacts / "mcp.log").open("w", encoding="utf-8") as mcp_log:
            host = subprocess.Popen([str(isolated), "--multi", "--new-session", "-d", "4"], env=environment, stdout=host_log, stderr=host_log)
            deadline = time.monotonic() + 40
            # A probe started first can claim the single-instance lock itself.
            # Wait until the host has constructed its IPC server before launching a probe.
            while "Klogg instance" not in (artifacts / "host.log").read_text(encoding="utf-8", errors="replace"):
                assert host.poll() is None, "Isolated host exited during construction"
                assert time.monotonic() < deadline, "Host did not publish its startup marker"
                time.sleep(0.02)
            while True:
                assert host.poll() is None, "Isolated host exited during startup"
                probe = subprocess.run([str(isolated), "command", "--action", "get_info", "--require-running", "--timeout-ms", "1000"], capture_output=True, env=environment, timeout=10)
                if probe.returncode == 0:
                    break
                assert time.monotonic() < deadline, probe.stderr
                time.sleep(0.05)
            server = subprocess.Popen([str(isolated), "mcp", "serve"], env=environment, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=mcp_log, encoding="utf-8", bufsize=1)

            def read_replies():
                try:
                    for line in server.stdout:
                        replies.put(json.loads(line))
                finally:
                    replies.put(None)

            reader = threading.Thread(target=read_replies, daemon=True)
            reader.start()
            send("initialize", {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "analysis-parity", "version": "1"}})
            send("notifications/initialized", {}, notification=True)
            catalog = send("tools/call", {"name": "cilogg_commands", "arguments": {"area": "commander"}})["structuredContent"]
            assert all(name in catalog["commander"]["commands"] for name in (
                "regex_evaluate", "get_analysis_job", "get_analysis_results", "cancel_analysis_job", "release_analysis_job"))
            request = {"scope": "text", "pattern": r"(?<value>\d)(?<optional>x)?", "text": "ANALYSIS_PRIVATE_BODY_SENTINEL שלום 😀1\r\nsecond2\nlast3", "requestRevision": "9007199254740993"}
            request_path = artifacts / "request.json"
            request_path.write_text(json.dumps(request, ensure_ascii=False), encoding="utf-8")
            cli_job = cli("regex_evaluate", "--json-file", str(request_path))["jobId"]
            mcp_job = command("regex_evaluate", {"analysis_json": request})["jobId"]
            for job, mcp in ((cli_job, False), (mcp_job, True)):
                status = finish(job, mcp)
                assert status["complete"] and status["captureBackend"] == "Qt QRegularExpression", status
                assert status["requestRevision"] == request["requestRevision"], status
            cli_rows, mcp_rows = rows(cli_job, False), rows(mcp_job, True)
            assert normalized(cli_rows) == normalized(mcp_rows), (cli_rows, mcp_rows)
            assert [row["lineNumber"] for row in cli_rows] == [1, 2, 3]
            if regex_lab:
                assert all(name in catalog["commander"]["commands"] for name in (
                    "show_regex_lab", "get_regex_lab", "set_regex_lab", "apply_regex_lab_pattern"))
                cli("show_regex_lab", "--window-index", "0")
                definition = {key: value for key, value in request.items() if key != "requestRevision"}
                command("set_regex_lab", {"window_index": 0, "definition": definition})
                previous_job = cli("get_regex_lab", "--window-index", "0")["jobId"]
                # Activate the real GUI Run control; it must use the same service
                # and produce exact captures equivalent to CLI/native MCP runs.
                command("activate_ui", {"window_index": 0, "object_name": "regexLabRun"})
                deadline = time.monotonic() + 30
                while True:
                    lab_job = cli("get_regex_lab", "--window-index", "0")["jobId"]
                    if lab_job and lab_job != previous_job:
                        break
                    assert time.monotonic() < deadline, "GUI Run did not publish a new analysis job"
                    time.sleep(0.02)
                assert finish(lab_job, True)["complete"]
                assert normalized(rows(lab_job, True)) == normalized(cli_rows)
                invalid = command("set_regex_lab", {"definition": {"activateResultIndex": None}}, success=False)
                assert invalid["reason"] == "invalid_result_index", invalid
                # A changed draft is not applicable until a new worker compiles it.
                command("set_regex_lab", {"definition": {"pattern": "["}})
                invalid = command("apply_regex_lab_pattern", {}, success=False)
                assert invalid["reason"] == "pattern_not_validated", invalid
                command("set_regex_lab", {"definition": {"operation": "release"}})
                print("PASS: GUI Regex Lab Run, CLI/native-MCP captures, semantic errors and draft guards")
            if measure:
                measure_file_analysis(cli, host, artifacts)
            error = cli("get_analysis_results", "--job-id", cli_job, "--cursor", "invalid", success=False)
            assert error["reason"] == command("get_analysis_results", {"job_id": mcp_job, "cursor": "invalid"}, success=False)["reason"] == "invalid_cursor"
            # Exceeds Windows argv and Linux per-argument limits; MCP must spool JSON.
            large = dict(request, text="a" * 200000, pattern="never")
            large_job = command("regex_evaluate", {"analysis_json": large})["jobId"]
            assert finish(large_job, True)["complete"]
            cancel = dict(request, text="a" * 100000 + "!", pattern="(*NO_JIT)(*LIMIT_MATCH=1000000000)(a+)+$")
            cancelled = command("regex_evaluate", {"analysis_json": cancel})["jobId"]
            started = time.monotonic()
            command("cancel_analysis_job", {"job_id": cancelled})
            command("cancel_analysis_job", {"job_id": cancelled})
            cancel_status = finish(cancelled, True)
            cancel_seconds = time.monotonic() - started
            assert cancel_status["state"] == "cancelled", cancel_status
            assert cancel_seconds < 10, cancel_seconds  # Includes CLI process startup; service tests enforce <1s.
            for job in (cli_job, mcp_job, large_job, cancelled):
                command("release_analysis_job", {"job_id": job})
                assert command("get_analysis_job", {"job_id": job}, success=False)["reason"] == "job_expired"
            for log in (artifacts / "host.log", artifacts / "mcp.log"):
                assert "ANALYSIS_PRIVATE_BODY_SENTINEL" not in log.read_text(encoding="utf-8", errors="replace"), f"Source body leaked to {log}"
            (artifacts / "result.json").write_text(json.dumps({"passed": True, "rows": normalized(cli_rows), "cancelSeconds": cancel_seconds}, ensure_ascii=False, indent=2), encoding="utf-8")
            print("PASS: CLI/native-MCP identity, captures, paging, errors, large JSON, cancellation and release")
    finally:
        cleanup_errors = []
        if server and server.stdin:
            try:
                server.stdin.close()
            except (BrokenPipeError, OSError) as error:
                cleanup_errors.append(str(error))
        for process in (server, host):
            if process:
                try:
                    if process is host and process.poll() is None:
                        process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=10)
                except (OSError, subprocess.TimeoutExpired) as error:
                    cleanup_errors.append(str(error))
        try:
            isolated.unlink(missing_ok=True)
        except OSError as error:
            cleanup_errors.append(str(error))
        if cleanup_errors:
            message = "Smoke cleanup: " + "; ".join(cleanup_errors)
            if sys.exc_info()[0] is None:
                raise RuntimeError(message)
            print(message, file=sys.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--artifacts", required=True, type=Path)
    parser.add_argument("--regex-lab", action="store_true", help="Also verify dock commands and GUI Run parity")
    parser.add_argument("--measure", action="store_true", help="Measure a 64 MiB frozen scan and append coexistence")
    options = parser.parse_args()
    run(options.executable.resolve(), options.artifacts.resolve(), options.regex_lab, options.measure)
