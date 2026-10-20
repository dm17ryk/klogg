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


def seed_routing_session(payload: Path, artifacts: Path) -> None:
    """Restore two paused, nonexistent ports; never open a real communication device."""
    settings = ["[General]", "version=2", "", "[Window]", "version=2", r"windows\size=2"]
    captures = artifacts / "captures"
    captures.mkdir(exist_ok=True)
    for index in range(2):
        capture = captures / f"routing-{index}.log"
        capture.write_text(f"Routing fixture {index}\n", encoding="utf-8")
        stream = {"portName": f"CILOGG_TEST_UNAVAILABLE_{uuid.uuid4().hex}",
                  "filePath": capture.as_posix(), "useForActions": True}
        prefix = f"windows\\{index + 1}\\"
        fields = {"id": f"AutoRouting{index}", "OpenFiles\\version": 2,
                  "OpenFiles\\openFiles\\size": 1,
                  "OpenFiles\\openFiles\\1\\fileName": capture.as_posix(),
                  "OpenFiles\\openFiles\\1\\streamContext": json.dumps(stream)}
        settings.extend(prefix + key + "=" + json.dumps(value, ensure_ascii=False)
                        for key, value in fields.items())
    (payload / "cilogg_session.conf").write_text("\n".join(settings) + "\n", encoding="utf-8")


def run(executable: Path, artifacts: Path, auto_responses: bool = False) -> None:
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
    if auto_responses:
        seed_routing_session(payload, artifacts)
    # Normal analysis fixtures skip restoration; routing fixtures explicitly test it.
    environment = dict(os.environ, CILOGG_AUTOMATION="0" if auto_responses else "1", QT_QPA_PLATFORM="offscreen")
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
            host_arguments = [str(isolated), "--multi", "-d", "4", "-platform", "offscreen"]
            if not auto_responses:
                host_arguments.append("--new-session")
            else:
                host_arguments.append("--load-session")
            host = subprocess.Popen(host_arguments, env=environment, stdout=host_log, stderr=host_log)
            deadline = time.monotonic() + 40
            # A probe started first can claim the single-instance lock itself.
            # Wait until the host has constructed its IPC server before launching a probe.
            while "Klogg instance" not in (artifacts / "host.log").read_text(encoding="utf-8", errors="replace"):
                assert host.poll() is None, "Isolated host exited during construction"
                assert time.monotonic() < deadline, "Host did not publish its startup marker"
                time.sleep(0.02)
            if auto_responses:
                # Restoration pumps events before Commander is ready; wait for its completion.
                while "startBackgroundTasks" not in (artifacts / "host.log").read_text(encoding="utf-8", errors="replace"):
                    assert host.poll() is None, "Routing host exited during restoration"
                    assert time.monotonic() < deadline, "Routing host did not finish restoration"
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
                "regex_evaluate", "get_analysis_job", "get_analysis_results", "cancel_analysis_job", "release_analysis_job",
                "set_auto_responses_enabled"))
            # Exercise the public operation without opening any real communication device.
            cli("set_auto_responses_enabled", "--disabled", "--tab-id", "missing-auto-tab", success=False)
            rejected_toggle = send("tools/call", {"name": "cilogg_execute", "arguments": {
                "action": "set_auto_responses_enabled",
                "arguments": {"disabled": True, "tab_id": "missing-auto-tab"},
            }})
            assert rejected_toggle.get("isError") and "error" in rejected_toggle["structuredContent"], rejected_toggle
            if auto_responses:
                def routing_state():
                    windows = cli("get_info")["windows"]
                    assert len(windows) == 2, windows
                    for window in windows:
                        assert len(window["tabs"]) == 1, windows
                        source = window["tabs"][0]["com"]
                        assert source["portName"].startswith("CILOGG_TEST_UNAVAILABLE_"), source
                        assert not source["connected"] and source["paused"], source
                    return {window["windowIndex"]: window for window in windows}

                def flags():
                    return {index: window["tabs"][0]["com"]["autoResponsesEnabled"]
                            for index, window in routing_state().items()}

                deadline = time.monotonic() + 30
                while True:
                    try:
                        windows = routing_state()
                        break
                    except (AssertionError, KeyError) as error:
                        if time.monotonic() >= deadline:
                            raise AssertionError("Two-window routing session did not become ready") from error
                        time.sleep(0.02)
                first, second = sorted(windows)
                assert flags() == {first: True, second: True}
                changed = cli("set_auto_responses_enabled", "--disabled", "--window-index", str(second), "--tab-index", "0")
                assert changed["windowIndex"] == second, changed
                assert flags() == {first: True, second: False}
                changed = command("set_auto_responses_enabled", {"enabled": True, "window_index": second, "tab_index": 0})
                assert changed["windowIndex"] == second, changed
                assert flags() == {first: True, second: True}
                tab = windows[first]["tabs"][0]["tabId"]
                command("set_auto_responses_enabled", {"disabled": True, "tab_id": tab})
                assert flags() == {first: False, second: True}
                cli("set_auto_responses_enabled", "--enabled", "--tab-id", tab)
                cli("focus_tab", "--window-index", str(second), "--tab-index", "0")
                deadline = time.monotonic() + 15
                while not routing_state()[second]["isActiveWindow"]:
                    assert time.monotonic() < deadline, "Second routing window did not become active"
                    time.sleep(0.02)
                cli("set_auto_responses_enabled", "--disabled")
                assert flags() == {first: True, second: False}
                command("set_auto_responses_enabled", {"enabled": True})
                assert flags() == {first: True, second: True}
                print("PASS: two-window CLI/native-MCP index, tab ID and active-window response routing")
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
            error = cli("get_analysis_results", "--job-id", cli_job, "--cursor", "invalid", success=False)
            assert error["reason"] == command("get_analysis_results", {"job_id": mcp_job, "cursor": "invalid"}, success=False)["reason"] == "invalid_cursor"
            # Exceeds Windows argv and Linux per-argument limits; MCP must spool JSON.
            large = dict(request, text="a" * 200000, pattern="never")
            large_job = command("regex_evaluate", {"analysis_json": large})["jobId"]
            assert finish(large_job, True)["complete"]
            # Quadratic retries across starting positions avoid a quick per-attempt
            # backtracking limit, leaving work running during CLI/MCP startup.
            cancel = dict(request, text="a" * 100000 + "!",
                          pattern="(*NO_JIT)(*NO_START_OPT)(*NO_AUTO_POSSESS)a+b",
                          limits={"batchTimeoutMs": 30000})
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
    parser.add_argument("--auto-responses", action="store_true")
    options = parser.parse_args()
    run(options.executable.resolve(), options.artifacts.resolve(), options.auto_responses)
