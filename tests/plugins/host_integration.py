"""Opt-in tests against a built, isolated CILogg. Uses only Python's standard library."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import queue
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time


class McpClient:
    def __init__(self, executable: Path, environment: dict[str, str], diagnostics: object) -> None:
        self.process = subprocess.Popen([str(executable), "mcp", "serve"], env=environment,
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=diagnostics)
        self.inbox: queue.Queue[dict] = queue.Queue()
        self.identifier = 0
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()
        try:
            self.request("initialize", {"protocolVersion": "2024-11-05", "capabilities": {},
                                        "clientInfo": {"name": "plugin-integration", "version": "1"}})
            self.send({"jsonrpc": "2.0", "method": "notifications/initialized"})
        except BaseException:
            self.close()
            raise

    def read(self) -> None:
        for line in self.process.stdout:
            self.inbox.put(json.loads(line))

    def send(self, frame: dict) -> None:
        self.process.stdin.write(json.dumps(frame).encode() + b"\n")
        self.process.stdin.flush()

    def request(self, method: str, params: dict) -> dict:
        self.identifier += 1
        self.send({"jsonrpc": "2.0", "id": self.identifier, "method": method, "params": params})
        while True:
            result = self.inbox.get(timeout=40)
            if result.get("id") == self.identifier:
                assert "error" not in result, result
                return result["result"]

    def command(self, action: str, arguments: dict | None = None) -> dict:
        result = self.request("tools/call", {"name": "cilogg_execute", "arguments": {
            "action": action, "arguments": arguments or {}}})
        assert not result.get("isError"), result
        return json.loads(result["content"][0]["text"])["payload"]

    def close(self) -> None:
        self.process.stdin.close()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.terminate()
            self.process.wait(timeout=5)
        self.reader.join(timeout=1)
        self.process.stdout.close()


def run(build: Path, artifacts: Path) -> None:
    artifacts.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="plugin-host-", dir=artifacts) as directory:
        root = Path(directory)
        extension = ".exe" if os.name == "nt" else ""
        executable = root / ("cilogg_plugin_integration" + extension)
        shutil.copy2(build / ("cilogg_portable" + extension), executable)
        for library in build.glob("*.dll"):
            shutil.copy2(library, root / library.name)
        for subdirectory in ("platforms", "plugins"):
            if (build / subdirectory).exists():
                shutil.copytree(build / subdirectory, root / subdirectory)
        environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", CILOGG_AUTOMATION="1",
                           CILOGG_SCRIPT_PYTHON=sys.executable)
        with (artifacts / "host.log").open("w", encoding="utf-8") as diagnostics:
            application = subprocess.Popen([str(executable), "--new-session"], env=environment,
                                           stdout=diagnostics, stderr=diagnostics)
            mcp = None
            def cli(action: str, *options: str) -> dict:
                response = subprocess.run([str(executable), "command", "--action", action,
                                           "--require-running", *options], env=environment,
                                          capture_output=True, timeout=40, encoding="utf-8")
                assert response.returncode == 0, response.stderr + response.stdout
                return json.loads(response.stdout or "{}")

            def completed(identifier: str) -> dict:
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline:
                    job = cli("get_plugin_job", "--plugin-job-id", identifier)
                    if job["state"] in ("completed", "failed", "cancelled"):
                        assert job["state"] == "completed", job
                        return job
                    time.sleep(0.05)
                raise AssertionError("Plugin job timed out")

            try:
                deadline = time.monotonic() + 30
                while True:
                    assert application.poll() is None, f"Application startup failed ({application.returncode}); inspect host.log"
                    try:
                        info = cli("get_info")
                        break
                    except AssertionError:
                        if time.monotonic() >= deadline:
                            raise
                        time.sleep(0.1)
                mcp = McpClient(executable, environment, diagnostics)
                discovery = mcp.request("tools/call", {"name": "cilogg_commands", "arguments": {"area": "commander"}})
                catalog = json.loads(discovery["content"][0]["text"])
                assert "invoke_plugin" in catalog["commander"]["commands"]
                plugins = cli("get_plugins")["plugins"]
                identifiers = {p["id"] for p in plugins}
                assert {"org.cilogg.generated-source", "org.cilogg.json-lines",
                        "org.cilogg.log-summary", "org.cilogg.adb-logcat",
                        "org.cilogg.syslog-server"} <= identifiers, plugins
                preinstalled = {"org.cilogg.adb-logcat", "org.cilogg.syslog-server"}
                for installed in plugins:
                    if installed["id"] in preinstalled:
                        assert installed["enabled"], installed
                        assert installed["state"] == "stopped", installed
                initial_ui = cli("get_ui")["objects"]
                for identifier in preinstalled:
                    assert any(o.get("objectName") == f"pluginAction_{identifier}_start"
                               for o in initial_ui), "Preinstalled action is missing"
                for identifier in identifiers:
                    if identifier not in preinstalled:
                        cli("plugin_manage", "--plugin-operation", "enable", "--plugin-id", identifier)
                source = "org.cilogg.generated-source"
                converter = "org.cilogg.json-lines"
                summary = "org.cilogg.log-summary"
                actions = [(source, "start", {"count": 3, "interval": 0.01}),
                           (converter, "convert", {}), (summary, "summarize", {})]
                start = mcp.command("invoke_plugin", {"plugin_id": source, "plugin_action": "start",
                                    "definition": {"count": 3, "interval": 0.01}})
                completed(start["jobId"])
                tabs = cli("get_info")["windows"][0]["tabs"]
                assert tabs, "Live capture did not open a source tab"
                capture = Path(tabs[-1]["filePath"])
                source_tab_id = tabs[-1]["tabId"]
                assert len(capture.read_text(encoding="utf-8").splitlines()) == 3
                original = root / "input.jsonl"
                original_bytes = b'{"level":"INFO","message":"hello"}\n'
                original.write_bytes(original_bytes)
                arguments = root / "arguments.json"
                arguments.write_text(json.dumps({"input_path": str(original)}), encoding="utf-8")
                converted = cli("invoke_plugin", "--plugin-id", converter, "--plugin-action", "convert",
                                "--json-file", str(arguments))
                completed(converted["jobId"])
                assert original.read_bytes() == original_bytes, "Converter changed the original input"
                # Activate the shipped action through the actual Qt QAction surface.
                time.sleep(0.25)
                cli("activate_ui", "--object-name", f"pluginAction_{summary}_summarize")
                deadline = time.monotonic() + 5
                while True:
                    ui = cli("get_ui")
                    if any(o.get("objectName") == f"pluginPanel_{summary}_summary" for o in ui["objects"]):
                        break
                    assert time.monotonic() < deadline, "Embedded panel was not rendered"
                    time.sleep(0.1)
                assert cli("get_info")["windows"]
                # Exercise each capability through both public protocol surfaces.
                for protocol in ("cli", "mcp"):
                    for identifier, action, parameters in actions:
                        if action in ("convert", "summarize"):
                            parameters = {"input_path": str(original)}
                        if protocol == "mcp":
                            started = mcp.command("invoke_plugin", {"plugin_id": identifier,
                                "plugin_action": action, "definition": parameters})
                        else:
                            arguments.write_text(json.dumps(parameters), encoding="utf-8")
                            started = cli("invoke_plugin", "--plugin-id", identifier,
                                          "--plugin-action", action, "--json-file", str(arguments))
                        completed(started["jobId"])
                # Converter and source GUI actions exercise native action forms as well.
                cli("open_file", "--file", str(original))
                prior = {j["jobId"] for j in cli("get_plugins")["jobs"]}
                activation_errors = []
                def activate_converter() -> None:
                    try:
                        cli("activate_ui", "--object-name", f"pluginAction_{converter}_convert")
                    except BaseException as error:
                        activation_errors.append(error)
                activation = threading.Thread(target=activate_converter, daemon=True)
                activation.start()
                deadline = time.monotonic() + 5
                while True:
                    form = cli("get_ui")["objects"]
                    if any(o.get("objectName") == "pluginField_input_path" for o in form):
                        break
                    assert time.monotonic() < deadline, "Converter form did not open"
                    time.sleep(0.05)
                cli("activate_ui", "--object-name", "pluginFormAccept")
                activation.join(timeout=5)
                assert not activation.is_alive() and not activation_errors, activation_errors
                deadline = time.monotonic() + 5
                while True:
                    new_jobs = [j for j in cli("get_plugins")["jobs"] if j["jobId"] not in prior]
                    if new_jobs:
                        completed(new_jobs[0]["jobId"])
                        break
                    assert time.monotonic() < deadline
                    time.sleep(0.05)
                prior = {j["jobId"] for j in cli("get_plugins")["jobs"]}
                activation_errors = []
                def activate_source() -> None:
                    try:
                        cli("activate_ui", "--object-name", f"pluginAction_{source}_start")
                    except BaseException as error:
                        activation_errors.append(error)
                activation = threading.Thread(target=activate_source, daemon=True)
                activation.start()
                deadline = time.monotonic() + 5
                while True:
                    form = cli("get_ui")["objects"]
                    if any(o.get("objectName") == "pluginField_count" for o in form):
                        break
                    assert time.monotonic() < deadline, "Action form did not open"
                    time.sleep(0.05)
                mcp.command("set_ui", {"object_name": "pluginField_count", "definition": {"text": "3"}})
                mcp.command("set_ui", {"object_name": "pluginField_interval", "definition": {"text": "0.01"}})
                cli("activate_ui", "--object-name", "pluginFormAccept")
                activation.join(timeout=5)
                assert not activation.is_alive() and not activation_errors, activation_errors
                deadline = time.monotonic() + 5
                while True:
                    new_jobs = [j for j in cli("get_plugins")["jobs"] if j["jobId"] not in prior]
                    if new_jobs:
                        completed(new_jobs[0]["jobId"])
                        break
                    assert time.monotonic() < deadline
                    time.sleep(0.05)
                # Syslog crosses real UDP, the plugin protocol and the native capture writer.
                syslog = "org.cilogg.syslog-server"
                for protocol in ("cli", "mcp", "gui"):
                    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                        probe.bind(("127.0.0.1", 0))
                        port = probe.getsockname()[1]
                    parameters = {"bind": "127.0.0.1", "port": port, "count": 1}
                    before_tabs = {tab["tabId"] for window in cli("get_info")["windows"]
                                   for tab in window["tabs"]}
                    if protocol == "mcp":
                        started = mcp.command("invoke_plugin", {"plugin_id": syslog,
                            "plugin_action": "start", "definition": parameters})
                    elif protocol == "cli":
                        arguments.write_text(json.dumps(parameters), encoding="utf-8")
                        started = cli("invoke_plugin", "--plugin-id", syslog,
                                      "--plugin-action", "start", "--json-file", str(arguments))
                    else:
                        previous_jobs = {j["jobId"] for j in cli("get_plugins")["jobs"]}
                        activation_errors = []
                        def activate_syslog() -> None:
                            try:
                                cli("activate_ui", "--object-name", f"pluginAction_{syslog}_start")
                            except BaseException as error:
                                activation_errors.append(error)
                        activation = threading.Thread(target=activate_syslog, daemon=True)
                        activation.start()
                        deadline = time.monotonic() + 5
                        while not any(o.get("objectName") == "pluginField_port"
                                      for o in cli("get_ui")["objects"]):
                            assert time.monotonic() < deadline, "Syslog form did not open"
                            time.sleep(0.05)
                        for key, value in parameters.items():
                            mcp.command("set_ui", {"object_name": "pluginField_" + key,
                                        "definition": {"text": str(value)}})
                        cli("activate_ui", "--object-name", "pluginFormAccept")
                        activation.join(timeout=5)
                        assert not activation.is_alive() and not activation_errors, activation_errors
                        deadline = time.monotonic() + 5
                        while True:
                            added = [j for j in cli("get_plugins")["jobs"]
                                     if j["jobId"] not in previous_jobs]
                            if added:
                                started = added[0]
                                break
                            assert time.monotonic() < deadline, "Syslog GUI job did not start"
                            time.sleep(0.05)
                    deadline = time.monotonic() + 10
                    while True:
                        new_tabs = [tab for window in cli("get_info")["windows"]
                                    for tab in window["tabs"] if tab["tabId"] not in before_tabs]
                        if new_tabs:
                            break
                        assert time.monotonic() < deadline, "Syslog did not open its capture"
                        time.sleep(0.05)
                    message = f"<166>1 2026-10-13T00:00:00Z test-host app 1 ID - syslog-{protocol} שלום"
                    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
                        sender.sendto(message.encode("utf-8"), ("127.0.0.1", port))
                    completed(started["jobId"])
                    data = Path(new_tabs[-1]["filePath"]).read_text(encoding="utf-8")
                    assert message in data and "severity=INFO" in data, data
                    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                        probe.bind(("127.0.0.1", port))
                # ADB configuration and launch failures must survive the same public surfaces.
                adb = "org.cilogg.adb-logcat"
                arguments.write_text(json.dumps({"configuration": {
                    "adb_path": str(root / "missing-adb-executable")}}), encoding="utf-8")
                cli("plugin_manage", "--plugin-id", adb, "--plugin-operation", "configure",
                    "--json-file", str(arguments))
                for protocol in ("cli", "mcp"):
                    if protocol == "mcp":
                        started = mcp.command("invoke_plugin", {"plugin_id": adb,
                                              "plugin_action": "devices"})
                    else:
                        started = cli("invoke_plugin", "--plugin-id", adb,
                                      "--plugin-action", "devices")
                    deadline = time.monotonic() + 10
                    while True:
                        job = cli("get_plugin_job", "--plugin-job-id", started["jobId"])
                        if job["state"] == "failed":
                            assert "Cannot start ADB" in job["error"], job
                            break
                        assert time.monotonic() < deadline, "Missing ADB did not report failure"
                        time.sleep(0.05)
                ui = cli("get_ui")
                (artifacts / "ui.json").write_text(json.dumps(ui, indent=2), encoding="utf-8")
                (artifacts / "plugins.json").write_text(json.dumps(cli("get_plugins"), indent=2), encoding="utf-8")
                cli("close_tab", "--tab-id", source_tab_id)
                assert capture.exists(), "Closing tabs must retain captures"
                print("PASS: examples and syslog via GUI, CLI and MCP; ADB launch failures; capture retention and discovery")
            finally:
                if mcp:
                    mcp.close()
                if application.poll() is None:
                    try:
                        cli("invoke_action", "--object-name", "exitAction")
                        application.wait(timeout=5)
                    except (AssertionError, subprocess.TimeoutExpired):
                        application.terminate()
                        application.wait(timeout=5)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    options = parser.parse_args()
    run(options.build.resolve(), options.artifacts.resolve())
