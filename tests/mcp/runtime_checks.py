"""Opt-in checks of native CLI jobs, MCP cancellation, and transport recovery."""

from __future__ import annotations

import argparse
import asyncio
import json
import logging
import queue
import socket
import sys
import subprocess
import threading
import time
from pathlib import Path

from workbench_contracts import require_operations, sdk_tool_failed

LOG = logging.getLogger("workbench.runtime")
CONTROLLER_ATTEMPTS = 3


def controller_arguments(directory: Path) -> list[str]:
    directory.mkdir(parents=True, exist_ok=True)
    token = directory / "token.txt"
    token.write_text("cilogg-local-integration-test-token", encoding="utf-8")
    for _ in range(50):
        with socket.socket() as http, socket.socket() as agent:
            if sys.platform == "win32":
                # Match Qt's exclusive listener policy; ordinary Windows bind can
                # accept a port that the actual controller cannot subsequently use.
                http.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
                agent.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            http.bind(("127.0.0.1", 0))
            port = http.getsockname()[1]
            if port >= 65535:
                continue
            try:
                agent.bind(("127.0.0.1", port + 1))
            except OSError:
                continue
            return [
                "serve",
                "--listen",
                f"127.0.0.1:{port}",
                "--state-dir",
                str(directory / "state"),
                "--token-file",
                str(token),
            ]
    raise RuntimeError("Could not reserve consecutive local test ports")


def bind_failure(text: str) -> bool:
    return any(marker in text.lower() for marker in (
        "already in use", "address is protected", "only one usage of each socket address",
    ))


async def controller_job(session, directory: Path):
    for attempt in range(CONTROLLER_ATTEMPTS):
        args = controller_arguments(directory / str(attempt))
        LOG.info("controller_start check=background attempt=%s", attempt + 1)
        started = await session.call_tool(
            "cilogg_cli", {"mode": "lab-controller", "arguments": args, "background": True}
        )
        assert not sdk_tool_failed(started), "controller_start_failed"
        job = json.loads(started.content[0].text)
        deadline = time.monotonic() + 10
        while True:
            read = await session.call_tool(
                "cilogg_jobs", {"operation": "read", "job_id": job["job_id"]}
            )
            assert not sdk_tool_failed(read), "controller_read_failed"
            state = json.loads(read.content[0].text)
            if state["state"] != "running":
                if state["state"] != "failed" or not bind_failure(state["stderr"]):
                    raise AssertionError("controller_startup_failed")
                LOG.warning("controller_bind_retry check=background attempt=%s job=%s",
                            attempt + 1, job["job_id"])
                break
            if "listening" in state["stdout"].lower():
                assert state.get("pid", job["pid"]) > 0
                LOG.info("controller_ready attempt=%s job=%s", attempt + 1, job["job_id"])
                return job, args
            if time.monotonic() >= deadline:
                await session.call_tool(
                    "cilogg_jobs", {"operation": "cancel", "job_id": job["job_id"]}
                )
                raise AssertionError("controller_startup_timeout")
            await asyncio.sleep(0.1)
    raise AssertionError("controller_bind_retries_exhausted")


async def controller_timeout(session, directory: Path):
    for attempt in range(CONTROLLER_ATTEMPTS):
        result = await session.call_tool("cilogg_cli", {
            "mode": "lab-controller",
            "arguments": controller_arguments(directory / str(attempt)), "timeout_ms": 300,
        })
        assert sdk_tool_failed(result), "controller_timeout_failed"
        text = result.content[0].text
        if "timed out" in text:
            LOG.info("controller_timeout_verified attempt=%s", attempt + 1)
            return
        if not bind_failure(text):
            raise AssertionError("controller_timeout_failed")
        LOG.warning("controller_bind_retry check=timeout attempt=%s", attempt + 1)
    raise AssertionError("controller_bind_retries_exhausted")


def cancel_controller(send, receive, directory: Path):
    for attempt in range(CONTROLLER_ATTEMPTS):
        ident = 2 + attempt * 2
        send({"jsonrpc": "2.0", "id": ident, "method": "tools/call", "params": {
            "name": "cilogg_cli", "arguments": {"mode": "lab-controller",
                "arguments": controller_arguments(directory / str(attempt)), "timeout_ms": 30000},
        }})
        send({"jsonrpc": "2.0", "id": ident + 1, "method": "ping"})
        first = receive()
        notified = False
        if first["id"] == ident:
            # Startup can fail before ping; drain its reply before retrying.
            assert receive()["id"] == ident + 1
            outcome = first
        else:
            assert first["id"] == ident + 1, "Ping must remain responsive during a CLI call"
            send({"jsonrpc": "2.0", "method": "notifications/cancelled",
                  "params": {"requestId": ident, "reason": "integration test"}})
            notified = True
            outcome = receive()
        assert outcome["id"] == ident
        assert outcome["result"]["isError"]
        text = outcome["result"]["content"][0]["text"]
        if notified and "cancelled" in text:
            LOG.info("controller_cancellation_verified attempt=%s", attempt + 1)
            return
        if not bind_failure(text):
            raise AssertionError("controller_cancellation_failed")
        LOG.warning("controller_bind_retry check=cancellation attempt=%s", attempt + 1)
    raise AssertionError("controller_bind_retries_exhausted")


async def jobs(executable: str, artifacts: Path, environment=None) -> None:
    from mcp import ClientSession, StdioServerParameters
    from mcp.client.stdio import stdio_client

    with (artifacts / "jobs-stderr.log").open("w", encoding="utf-8") as diagnostics:
        async with (
            stdio_client(
                StdioServerParameters(command=executable, args=["mcp", "serve"], env=environment),
                errlog=diagnostics,
            ) as (reader, writer),
            ClientSession(reader, writer) as session,
        ):
            await session.initialize()
            discovery = await session.call_tool("cilogg_commands", {"area": "commander"})
            assert not sdk_tool_failed(discovery), discovery.content
            catalog = getattr(discovery, "structured_content", getattr(discovery, "structuredContent", None))
            if catalog is None:
                catalog = json.loads(discovery.content[0].text)
            require_operations(catalog, ["get_info", "regex_evaluate", "get_analysis_job"])
            job, _ = await controller_job(session, artifacts / "controller")
            listed = await session.call_tool("cilogg_jobs", {})
            assert not sdk_tool_failed(listed), listed.content
            retained = json.loads(listed.content[0].text)["jobs"]
            assert [item["job_id"] for item in retained if item["state"] == "running"] == [job["job_id"]]
            assert all(item["state"] == "failed" or item["job_id"] == job["job_id"] for item in retained)
            cancelled = await session.call_tool(
                "cilogg_jobs", {"operation": "cancel", "job_id": job["job_id"]}
            )
            assert json.loads(cancelled.content[0].text)["state"] == "cancelled"
            await controller_timeout(session, artifacts / "timeout-controller")
            assert not sdk_tool_failed(
                await session.call_tool(
                    "cilogg_cli", {"mode": "scenario", "arguments": ["--help"]}
                )
            )


def cancellation(executable: str, artifacts: Path, environment=None) -> None:
    lines: queue.Queue[str | None] = queue.Queue()
    with (artifacts / "cancellation-stderr.log").open(
        "w", encoding="utf-8"
    ) as diagnostics:
        process = subprocess.Popen(
            [executable, "mcp", "serve"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=diagnostics,
            text=True,
            encoding="utf-8",
            env=environment,
        )
        assert process.stdin and process.stdout

        def read() -> None:
            for line in process.stdout:
                lines.put(line)
            lines.put(None)

        reader = threading.Thread(target=read, daemon=True)
        reader.start()

        def send(message: dict) -> None:
            process.stdin.write(json.dumps(message) + "\n")
            process.stdin.flush()

        def receive() -> dict:
            line = lines.get(timeout=30)
            assert line is not None, "Server unexpectedly closed stdout"
            return json.loads(line)

        try:
            send(
                {
                    "jsonrpc": "2.0",
                    "id": 1,
                    "method": "initialize",
                    "params": {
                        "protocolVersion": "2025-11-25",
                        "capabilities": {},
                        "clientInfo": {"name": "native-runtime-test", "version": "1"},
                    },
                }
            )
            assert receive()["id"] == 1
            send({"jsonrpc": "2.0", "method": "notifications/initialized"})
            process.stdin.write("invalid json\n")
            process.stdin.flush()
            assert receive()["error"]["code"] == -32700
            cancel_controller(send, receive, artifacts / "cancel-controller")
            send({"jsonrpc": "2.0", "id": 8, "method": "ping"})
            assert receive()["id"] == 8
            # Detached applications must never inherit the MCP input/output
            # streams, including a native invocation that prints plain text.
            send(
                {
                    "jsonrpc": "2.0",
                    "id": 9,
                    "method": "tools/call",
                    "params": {
                        "name": "cilogg_cli",
                        "arguments": {
                            "mode": "application",
                            "arguments": ["--version"],
                        },
                    },
                }
            )
            assert receive()["id"] == 9
            time.sleep(0.5)
            send({"jsonrpc": "2.0", "id": 10, "method": "ping"})
            assert receive()["id"] == 10
        finally:
            process.stdin.close()
            try:
                process.wait(timeout=10)
            finally:
                if process.poll() is None:
                    process.kill()  # Only this test-owned MCP subprocess.
                    process.wait(timeout=10)
            reader.join(timeout=2)
            assert not reader.is_alive(), (
                "A detached child retained the MCP stdout pipe"
            )
            while not lines.empty():
                trailing = lines.get_nowait()
                if trailing is not None:
                    json.loads(trailing)  # Reject any delayed non-protocol output.
    assert process.returncode == 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    options = parser.parse_args()
    root = options.artifacts.resolve()
    root.mkdir(parents=True, exist_ok=True)
    cancellation(options.executable, root)
    asyncio.run(jobs(options.executable, root))
    (root / "runtime-result.json").write_text(
        json.dumps(
            {
                "passed": True,
                "checks": [
                    "parse-error-recovery",
                    "ping-during-call",
                    "MCP-cancellation",
                    "background-controller",
                    "job-status",
                    "job-cancel",
                    "foreground-timeout",
                    "scenario-cli",
                    "detached-stdout-isolation",
                ],
            },
            indent=2,
        ),
        encoding="utf-8",
    )
    print("Native MCP CLI job and cancellation checks passed")
