"""Opt-in checks of native CLI jobs, MCP cancellation, and transport recovery."""

from __future__ import annotations

import argparse
import asyncio
import json
import queue
import socket
import subprocess
import threading
import time
from pathlib import Path

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


def controller_arguments(directory: Path) -> list[str]:
    directory.mkdir(parents=True, exist_ok=True)
    token = directory / "token.txt"
    token.write_text("cilogg-local-integration-test-token", encoding="utf-8")
    for _ in range(50):
        with socket.socket() as http, socket.socket() as agent:
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


async def jobs(executable: str, artifacts: Path) -> None:
    with (artifacts / "jobs-stderr.log").open("w", encoding="utf-8") as diagnostics:
        async with (
            stdio_client(
                StdioServerParameters(command=executable, args=["mcp", "serve"]),
                errlog=diagnostics,
            ) as (reader, writer),
            ClientSession(reader, writer) as session,
        ):
            await session.initialize()
            args = controller_arguments(artifacts / "controller")
            started = await session.call_tool(
                "cilogg_cli",
                {"mode": "lab-controller", "arguments": args, "background": True},
            )
            assert not started.isError, started.content
            job = json.loads(started.content[0].text)
            assert job["pid"] > 0
            for _ in range(50):
                read = await session.call_tool(
                    "cilogg_jobs", {"operation": "read", "job_id": job["job_id"]}
                )
                state = json.loads(read.content[0].text)
                assert state["state"] == "running", state
                if "listening" in state["stdout"].lower():
                    break
                await asyncio.sleep(0.1)
            else:
                raise AssertionError(state)
            listed = await session.call_tool("cilogg_jobs", {})
            assert len(json.loads(listed.content[0].text)["jobs"]) == 1
            cancelled = await session.call_tool(
                "cilogg_jobs", {"operation": "cancel", "job_id": job["job_id"]}
            )
            assert json.loads(cancelled.content[0].text)["state"] == "cancelled"
            timed_out = await session.call_tool(
                "cilogg_cli",
                {"mode": "lab-controller", "arguments": args, "timeout_ms": 300},
            )
            assert timed_out.isError
            assert "timed out" in timed_out.content[0].text
            assert not (
                await session.call_tool(
                    "cilogg_cli", {"mode": "scenario", "arguments": ["--help"]}
                )
            ).isError


def cancellation(executable: str, artifacts: Path) -> None:
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
            send(
                {
                    "jsonrpc": "2.0",
                    "id": 2,
                    "method": "tools/call",
                    "params": {
                        "name": "cilogg_cli",
                        "arguments": {
                            "mode": "lab-controller",
                            "arguments": controller_arguments(
                                artifacts / "cancel-controller"
                            ),
                            "timeout_ms": 30000,
                        },
                    },
                }
            )
            send({"jsonrpc": "2.0", "id": 3, "method": "ping"})
            assert receive()["id"] == 3, (
                "Ping must remain responsive during a foreground CLI call"
            )
            send(
                {
                    "jsonrpc": "2.0",
                    "method": "notifications/cancelled",
                    "params": {"requestId": 2, "reason": "integration test"},
                }
            )
            cancelled = receive()
            assert cancelled["id"] == 2
            assert cancelled["result"]["isError"]
            assert "cancelled" in cancelled["result"]["content"][0]["text"]
            send({"jsonrpc": "2.0", "id": 4, "method": "ping"})
            assert receive()["id"] == 4
            # Detached applications must never inherit the MCP input/output
            # streams, including a native invocation that prints plain text.
            send(
                {
                    "jsonrpc": "2.0",
                    "id": 5,
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
            assert receive()["id"] == 5
            time.sleep(0.5)
            send({"jsonrpc": "2.0", "id": 6, "method": "ping"})
            assert receive()["id"] == 6
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
