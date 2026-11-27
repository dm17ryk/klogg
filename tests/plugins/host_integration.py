"""Opt-in tests against a built, isolated CILogg. Uses only Python's standard library."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import queue
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time

if os.name == "nt":
    import ctypes
    from ctypes import wintypes

    _kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    _kernel32.CreateJobObjectW.restype = wintypes.HANDLE
    _kernel32.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
    _kernel32.OpenProcess.restype = wintypes.HANDLE
    _kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    _kernel32.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    _kernel32.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p,
                                                  wintypes.DWORD]
    _kernel32.QueryInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p,
                                                    wintypes.DWORD, ctypes.c_void_p]
    _kernel32.TerminateJobObject.argtypes = [wintypes.HANDLE, wintypes.UINT]
    _kernel32.CloseHandle.argtypes = [wintypes.HANDLE]

    class _BasicLimits(ctypes.Structure):  # JOBOBJECT_BASIC_LIMIT_INFORMATION
        _fields_ = [("PerProcessUserTimeLimit", ctypes.c_int64), ("PerJobUserTimeLimit", ctypes.c_int64),
                    ("LimitFlags", wintypes.DWORD), ("MinimumWorkingSetSize", ctypes.c_size_t),
                    ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", wintypes.DWORD),
                    ("Affinity", ctypes.c_size_t), ("PriorityClass", wintypes.DWORD),
                    ("SchedulingClass", wintypes.DWORD)]

    class _ExtendedLimits(ctypes.Structure):  # JOBOBJECT_EXTENDED_LIMIT_INFORMATION
        _fields_ = [("BasicLimitInformation", _BasicLimits), ("IoInfo", ctypes.c_uint64 * 6),
                    ("ProcessMemoryLimit", ctypes.c_size_t), ("JobMemoryLimit", ctypes.c_size_t),
                    ("PeakProcessMemoryUsed", ctypes.c_size_t), ("PeakJobMemoryUsed", ctypes.c_size_t)]

    class _Accounting(ctypes.Structure):  # JOBOBJECT_BASIC_ACCOUNTING_INFORMATION
        _fields_ = [("Times", ctypes.c_int64 * 4), ("TotalPageFaultCount", wintypes.DWORD),
                    ("TotalProcesses", wintypes.DWORD), ("ActiveProcesses", wintypes.DWORD),
                    ("TotalTerminatedProcesses", wintypes.DWORD)]

    _JOB_BASIC_ACCOUNTING = 1
    _JOB_EXTENDED_LIMITS = 9
    _KILL_ON_JOB_CLOSE = 0x2000
    _PROCESS_SET_QUOTA_AND_TERMINATE = 0x0100 | 0x0001


class ProcessTree:
    """The processes this test starts and everything they start in turn (plugins included).

    The application runs each plugin as a child process whose working directory is the plugin's
    folder. Ending the application alone leaves those children running, and Windows refuses to
    remove a folder that a live process works in (#162). On Windows the processes go into a job
    object, which their children join by themselves; elsewhere each gets its own process group.
    """

    popen_options: dict = {} if os.name == "nt" else {"start_new_session": True}

    def __init__(self) -> None:
        self.processes: list[subprocess.Popen] = []
        self.job = None
        if os.name == "nt":
            job = _kernel32.CreateJobObjectW(None, None)
            if not job:
                raise ctypes.WinError(ctypes.get_last_error())
            self.job = job
            limits = _ExtendedLimits()
            limits.BasicLimitInformation.LimitFlags = _KILL_ON_JOB_CLOSE
            if not _kernel32.SetInformationJobObject(job, _JOB_EXTENDED_LIMITS, ctypes.byref(limits),
                                                     ctypes.sizeof(limits)):
                error = ctypes.WinError(ctypes.get_last_error())
                self.close()
                raise error

    def add(self, process: subprocess.Popen) -> None:
        """Adds a process started with popen_options, before it starts children of its own."""
        self.processes.append(process)
        if self.job:
            handle = _kernel32.OpenProcess(_PROCESS_SET_QUOTA_AND_TERMINATE, False, process.pid)
            if not handle:
                raise ctypes.WinError(ctypes.get_last_error())
            try:
                if not _kernel32.AssignProcessToJobObject(self.job, handle):
                    raise ctypes.WinError(ctypes.get_last_error())
            finally:
                _kernel32.CloseHandle(handle)

    def alive(self) -> int:
        """How many processes of the tree still run."""
        if self.job:
            accounting = _Accounting()
            if not _kernel32.QueryInformationJobObject(self.job, _JOB_BASIC_ACCOUNTING,
                                                       ctypes.byref(accounting),
                                                       ctypes.sizeof(accounting), None):
                raise ctypes.WinError(ctypes.get_last_error())
            return accounting.ActiveProcesses
        count = 0
        for process in self.processes:
            process.poll()  # reaps an ended leader, so its group can empty
            try:
                os.killpg(process.pid, 0)
                count += 1
            except ProcessLookupError:
                pass
        return count

    def wait_until_ended(self, seconds: float) -> bool:
        deadline = time.monotonic() + seconds
        while self.alive() and time.monotonic() < deadline:
            time.sleep(0.1)
        return not self.alive()

    def kill(self) -> None:
        if self.job:
            _kernel32.TerminateJobObject(self.job, 1)
        else:
            for process in self.processes:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        for process in self.processes:
            process.poll()

    def close(self) -> None:
        if self.job:
            _kernel32.CloseHandle(self.job)  # kills what is left (KILL_ON_JOB_CLOSE)
            self.job = None


def shut_down(application: subprocess.Popen, tree: ProcessTree, close=None) -> list[str]:
    """Ends the application and everything it started; returns what went wrong, never raises.

    `close` asks the application to quit (only once the instance is known to be this test's).
    """
    problems: list[str] = []
    try:
        asked = bool(close) and application.poll() is None
        if asked:
            try:
                close()
            except (AssertionError, subprocess.SubprocessError, ValueError) as error:
                problems.append(f"close_cilogg failed: {error}")
            try:
                application.wait(timeout=20)
            except subprocess.TimeoutExpired:
                problems.append("the application still ran 20 s after close_cilogg")
            # The host gives its plugins 3 s to stop once it quits.
            if not problems and not tree.wait_until_ended(10):
                problems.append(f"{tree.alive()} process(es) outlived the application by 10 s")
        # Not asked (an instance not proven to be this test's, or one that already ended): what is
        # left is killed.
        if tree.alive():
            tree.kill()
            if not tree.wait_until_ended(30):
                problems.append(f"{tree.alive()} process(es) still ran 30 s after they were killed")
    except Exception as error:  # a teardown problem must not hide the checks' result
        problems.append(f"teardown error: {error!r}")
    finally:
        tree.close()
    return problems


def remove_folder(path: Path) -> str | None:
    """Removes the run's folder once nothing runs in it; a scanner may still hold a file briefly."""
    deadline = time.monotonic() + 10
    while True:
        try:
            shutil.rmtree(path)
            return None
        except FileNotFoundError:
            return None
        except OSError as error:
            if time.monotonic() >= deadline:
                return f"cannot remove {path}: {error}"
            time.sleep(0.2)


class McpClient:
    def __init__(self, executable: Path, environment: dict[str, str], diagnostics: object,
                 tree: ProcessTree) -> None:
        self.process = subprocess.Popen([str(executable), "mcp", "serve"], env=environment,
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=diagnostics, **ProcessTree.popen_options)
        tree.add(self.process)
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


def run(build: Path, artifacts: Path) -> list[str]:
    """Runs the checks (a failed one raises) and returns the teardown's problems."""
    artifacts.mkdir(parents=True, exist_ok=True)
    teardown_problems: list[str] = []
    # The folder is removed below, once nothing runs in it; a failure to remove it is reported as
    # a teardown problem, never as a failed check.
    with tempfile.TemporaryDirectory(prefix="plugin-host-", dir=artifacts,
                                     ignore_cleanup_errors=True) as directory:
        root = Path(directory)
        extension = ".exe" if os.name == "nt" else ""
        # CILogg's IPC identity uses the executable basename, not its directory.
        # Give every isolated run its own identity, including after an interrupted run.
        executable = root / ("cilogg_plugin_integration_" + root.name + extension)
        shutil.copy2(build / ("cilogg_portable" + extension), executable)
        for library in build.glob("*.dll"):
            shutil.copy2(library, root / library.name)
        for subdirectory in ("platforms", "plugins"):
            if (build / subdirectory).exists():
                shutil.copytree(build / subdirectory, root / subdirectory)
        environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", CILOGG_AUTOMATION="1",
                           CILOGG_SCRIPT_PYTHON=sys.executable)
        with (artifacts / "host.log").open("w", encoding="utf-8") as diagnostics:
            tree = ProcessTree()
            application = subprocess.Popen([str(executable), "--multi", "--new-session", "-d", "1"], env=environment,
                                           stdout=diagnostics, stderr=diagnostics,
                                           **ProcessTree.popen_options)
            # It starts no plugin before it answers get_info below.
            tree.add(application)
            mcp = None
            verified = False
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
                mcp = McpClient(executable, environment, diagnostics, tree)
                discovery = mcp.request("tools/call", {"name": "cilogg_commands", "arguments": {"area": "commander"}})
                catalog = json.loads(discovery["content"][0]["text"])
                assert "invoke_plugin" in catalog["commander"]["commands"]
                plugins = cli("get_plugins")["plugins"]
                identifiers = {p["id"] for p in plugins}
                assert {"org.cilogg.generated-source", "org.cilogg.json-lines",
                        "org.cilogg.log-summary", "org.cilogg.adb-logcat",
                        "org.cilogg.syslog-server"} <= identifiers, plugins
                assert all(Path(plugin["directory"]).resolve().is_relative_to(root.resolve())
                           for plugin in plugins), "Commander reached another application instance"
                verified = True
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
                # Plugin views: open the view-demo dock from its menu entry (GUI), fill a field
                # through MCP set_ui, press its job button, and cancel a CLI job (grace state).
                demo = "org.cilogg.view-demo"
                if demo in identifiers:
                    cli("activate_ui", "--object-name", f"pluginViewAction_{demo}_main")
                    deadline = time.monotonic() + 30
                    process_button = f"pluginView_{demo}_main_process"
                    while not any(o.get("objectName") == process_button and o.get("enabled")
                                  for o in cli("get_ui")["objects"]):
                        assert time.monotonic() < deadline, "View demo dock was not rendered"
                        time.sleep(0.1)
                    mcp.command("set_ui", {"object_name": f"pluginView_{demo}_main_name",
                                           "definition": {"text": "integration"}})
                    prior = {j["jobId"] for j in cli("get_plugins")["jobs"]}
                    cli("activate_ui", "--object-name", process_button)
                    deadline = time.monotonic() + 10
                    while True:
                        new_jobs = [j for j in cli("get_plugins")["jobs"] if j["jobId"] not in prior]
                        if new_jobs:
                            job = completed(new_jobs[0]["jobId"])
                            assert job["result"]["inputs"]["name"] == "integration", job
                            break
                        assert time.monotonic() < deadline, "View button did not start a job"
                        time.sleep(0.05)
                    started = cli("invoke_plugin", "--plugin-id", demo, "--plugin-action", "process")
                    deadline = time.monotonic() + 10
                    while cli("get_plugin_job", "--plugin-job-id", started["jobId"])["state"] != "running":
                        assert time.monotonic() < deadline, "View demo job did not start"
                        time.sleep(0.05)
                    cancelled = mcp.command("cancel_plugin_job", {"plugin_job_id": started["jobId"]})
                    assert cancelled["state"] == "cancelling", cancelled
                    deadline = time.monotonic() + 15
                    while cli("get_plugin_job", "--plugin-job-id", started["jobId"])["state"] != "cancelled":
                        assert time.monotonic() < deadline, "Cancelling job did not finish"
                        time.sleep(0.05)
                # E17a: the ui-gallery view answers advanced tooltips through the host's query
                # path; the CLI and MCP read the same tooltip, and a table cell asks per cell.
                gallery = "org.cilogg.ui-gallery"
                if gallery in identifiers:
                    # E17b: the gallery declares a Plugins menu, which replaces its flat entries;
                    # its "Open gallery" entry opens the view.
                    entries = cli("get_plugin_menus", "--location", "plugins", "--plugin-id",
                                  gallery)["entries"]
                    assert any(e["itemId"] == "open_gallery" and e["shown"] for e in entries), entries
                    cli("trigger_plugin_menu", "--plugin-id", gallery, "--menu-id", "main",
                        "--item-id", "open_gallery")
                    summary_button = f"pluginView_{gallery}_gallery_summary"
                    deadline = time.monotonic() + 30
                    while not any(o.get("objectName") == summary_button and o.get("enabled")
                                  for o in cli("get_ui")["objects"]):
                        assert time.monotonic() < deadline, "UI gallery was not rendered"
                        time.sleep(0.1)
                    through_cli = cli("get_plugin_tooltip", "--plugin-id", gallery, "--view-id",
                                      "gallery", "--element-id", "summary", "--no-cache")
                    through_mcp = mcp.command("get_plugin_tooltip", {
                        "plugin_id": gallery, "view_id": "gallery", "element_id": "summary",
                        "no_cache": True})
                    assert through_cli["source"] == "plugin", through_cli
                    assert through_cli["tooltip"] == through_mcp["tooltip"], (through_cli, through_mcp)
                    cell = cli("get_plugin_tooltip", "--plugin-id", gallery, "--view-id", "gallery",
                               "--element-id", "boards", "--row-id", "b1", "--column-id", "state")
                    assert cell["tooltip"]["title"] == "Connected", cell
                    # E17e: the explorer view's tree answers node tooltips by node id.
                    cli("trigger_plugin_menu", "--plugin-id", gallery, "--menu-id", "main",
                        "--item-id", "open_explorer")
                    benches = f"pluginView_{gallery}_explorer_benches"
                    deadline = time.monotonic() + 30
                    while not any(o.get("objectName") == benches and o.get("enabled")
                                  for o in cli("get_ui")["objects"]):
                        assert time.monotonic() < deadline, "UI gallery explorer was not rendered"
                        time.sleep(0.1)
                    node = cli("get_plugin_tooltip", "--plugin-id", gallery, "--view-id",
                               "explorer", "--element-id", "benches", "--node-id", "bench1",
                               "--no-cache")
                    node_mcp = mcp.command("get_plugin_tooltip", {
                        "plugin_id": gallery, "view_id": "explorer", "element_id": "benches",
                        "node_id": "bench1", "no_cache": True})
                    assert node["tooltip"]["title"] == "Bench 1", node
                    assert node["tooltip"] == node_mcp["tooltip"], (node, node_mcp)
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
                # Nothing here raises: a failed shutdown is reported on its own and cannot hide or
                # replace the checks' result.
                problems: list[str] = []
                if mcp:
                    try:
                        mcp.close()
                    except Exception as error:
                        problems.append(f"the MCP server did not stop: {error!r}")
                # close_cilogg quits the way the user does (its plugins get their stop). The main
                # window's exitAction has no object name, so `invoke_action exitAction` was never
                # found (#162). Only an instance proven to be this test's is asked.
                problems += shut_down(application, tree,
                                      (lambda: cli("close_cilogg")) if verified else None)
                if problem := remove_folder(root):
                    problems.append(problem)
                for problem in problems:
                    print("TEARDOWN FAILED: " + problem, file=sys.stderr, flush=True)
                teardown_problems += problems
    return teardown_problems


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    options = parser.parse_args()
    # A failed check raises (exit code 1); checks that passed with a failed teardown exit with 3.
    sys.exit(3 if run(options.build.resolve(), options.artifacts.resolve()) else 0)
