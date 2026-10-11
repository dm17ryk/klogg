"""Protocol-level checks for the actual shipped plugins; no application or hardware needed."""
import importlib.util
import json
import pathlib
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest
import io
import time
import os
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[2]


class Host:
    def __init__(self, package):
        self.manifest = json.loads((package / "plugin.json").read_text(encoding="utf-8"))
        self.process = subprocess.Popen(
            [sys.executable, "-u", "main.py"], cwd=package,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        self.inbox = queue.Queue(maxsize=256)
        self.records = []
        self.panels = []
        self.errors = []

        def read():
            for line in self.process.stdout:
                try:
                    self.inbox.put(json.loads(line), timeout=5)
                except Exception as error:
                    self.errors.append(str(error))
                    return
        self.reader = threading.Thread(target=read, daemon=True)
        self.reader.start()
        self.send("initialize", {"protocol_version": 1, "plugin_id": self.manifest["id"], "configuration": {}}, "initialize")
        try:
            self.initialization = self.result("initialize")
        except BaseException:
            self.close()
            raise

    def send(self, method, params, identifier=None):
        message = {"jsonrpc": "2.0", "method": method, "params": params}
        if identifier is not None:
            message["id"] = identifier
        self.process.stdin.write(json.dumps(message).encode("utf-8") + b"\n")
        self.process.stdin.flush()

    def result(self, identifier):
        while True:
            frame = self.inbox.get(timeout=10)
            if frame.get("id") == identifier:
                if "error" in frame:
                    raise RuntimeError(frame["error"]["message"])
                return frame["result"]
            method = frame.get("method")
            params = frame.get("params", {})
            if method == "stream/open":
                self.send("stream/opened", {"stream_id": params["stream_id"]})
            elif method == "stream/data":
                self.records.extend(params["records"])
                self.send("stream/ack", {"stream_id": params["stream_id"], "sequence": params["sequence"]})
            elif method == "panel/update":
                self.panels.append(params["panel"])

    def invoke(self, action, arguments=None, context=None):
        self.send("invoke", {"job_id": "job", "action": action, "arguments": arguments or {}, "context": context or {}}, "job")
        return self.result("job")

    def close(self):
        try:
            if self.process.poll() is None:
                self.send("shutdown", {})
                self.process.wait(timeout=3)
        finally:
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait(timeout=3)
            self.process.stdin.close()
            self.reader.join(timeout=1)
            self.process.stdout.close()
            self.process.stderr.close()


class PortableExamples(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)

    def host(self, example):
        package = self.root / example
        shutil.copytree(ROOT / "plugins/examples" / example, package)
        shutil.copy2(ROOT / "plugins/sdk/python/cilogg_plugin.py", package)
        host = Host(package)
        self.addCleanup(host.close)
        self.assertEqual(host.initialization["protocol_version"], 1)
        return host

    def test_source_acknowledges_and_preserves_records(self):
        host = self.host("generated-source")
        result = host.invoke("start", {"count": 3, "interval": 0.01})
        self.assertEqual(result["records"], 3)
        self.assertEqual(len(host.records), 3)
        self.assertIn("record=2", host.records[-1])
        self.assertFalse(host.errors)

    def test_external_window_keeps_protocol_responsive(self):
        if sys.platform not in ("win32", "darwin") and not os.environ.get("DISPLAY"):
            self.skipTest("Tk requires a display")
        try:
            import tkinter as tk
        except ImportError as error:
            self.skipTest(str(error))
        try:
            probe = tk.Tk()
            probe.withdraw()
            probe.destroy()
        except (tk.TclError, RuntimeError) as error:
            self.skipTest(str(error))
        package = self.root / "external-window"
        shutil.copytree(ROOT / "plugins/sdk/python/external-window", package)
        shutil.copy2(ROOT / "plugins/sdk/python/cilogg_plugin.py", package)
        host = Host(package)
        self.addCleanup(host.close)
        self.assertTrue(host.invoke("show")["shown"])
        time.sleep(0.15)  # The main thread opens the window on its next Tk tick.
        host.send("configure", {"configuration": {"responsive": True}})
        self.assertTrue(host.invoke("show")["shown"])
        host.send("shutdown", {})
        self.assertEqual(host.process.wait(timeout=3), 0)
        self.assertFalse(host.errors)

    def test_converter_preserves_input(self):
        host = self.host("json-lines")
        incoming = self.root / "source.jsonl"
        output = self.root / "converted.log"
        original = '{"severity":"INFO","message":"שלום"}\n'
        incoming.write_text(original, encoding="utf-8")
        result = host.invoke("convert", {"input_path": str(incoming), "output_path": str(output)})
        self.assertEqual(result["records"], 1)
        self.assertEqual(incoming.read_text(encoding="utf-8"), original)
        self.assertIn("severity=INFO", output.read_text(encoding="utf-8"))

    def test_summary_has_embedded_panel_and_stable_context(self):
        host = self.host("log-summary")
        log = self.root / "source.log"
        log.write_text("INFO one\nERROR two\nWARN three\n", encoding="utf-8")
        result = host.invoke("summarize", context={"filePath": str(log), "tabId": "tab-2", "windowId": "window-2"})
        self.assertEqual(result["records"], 3)
        self.assertEqual(result["counts"], {"ERROR": 1, "WARN": 1, "INFO": 1})
        self.assertEqual(host.panels[0]["id"], "summary")

    def test_invalid_converter_input_reports_failure(self):
        host = self.host("json-lines")
        with self.assertRaises(RuntimeError):
            host.invoke("convert", {"input_path": str(self.root / "missing"), "output_path": str(self.root / "out")})

    def test_packages_are_self_contained(self):
        spec = importlib.util.spec_from_file_location("package_examples", ROOT / "scripts/plugins/package_examples.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        import zipfile
        for manifest, archive in module.package_examples(self.root / "packages"):
            with zipfile.ZipFile(archive) as package:
                self.assertIn("cilogg_plugin.py", package.namelist())
                self.assertEqual(json.loads(package.read("plugin.json"))["id"], manifest["id"])

    def test_packages_ignore_checkout_timestamps(self):
        spec = importlib.util.spec_from_file_location("package_examples", ROOT / "scripts/plugins/package_examples.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        fixture = self.root / "checkout"
        shutil.copytree(ROOT / "plugins", fixture / "plugins", ignore=shutil.ignore_patterns("__pycache__"))
        module.ROOT = fixture
        first = {p.name: p.read_bytes() for _m, p in module.package_examples(self.root / "first")}
        for file in fixture.rglob("*"):
            if file.is_file():
                os.utime(file, (1000000000, 1000000000))
        second = {p.name: p.read_bytes() for _m, p in module.package_examples(self.root / "second")}
        self.assertEqual(first, second)

    def test_packages_ignore_checkout_line_endings(self):
        spec = importlib.util.spec_from_file_location("package_examples", ROOT / "scripts/plugins/package_examples.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        fixture = self.root / "checkout"
        shutil.copytree(ROOT / "plugins", fixture / "plugins", ignore=shutil.ignore_patterns("__pycache__"))
        module.ROOT = fixture
        first = {p.name: p.read_bytes() for _m, p in module.package_examples(self.root / "first")}
        for file in fixture.rglob("*"):
            if file.is_file() and file.suffix.lower() in {".py", ".json", ".md", ".txt"}:
                data = file.read_bytes().replace(b"\r\n", b"\n")
                file.write_bytes(data.replace(b"\n", b"\r\n"))
        second = {p.name: p.read_bytes() for _m, p in module.package_examples(self.root / "second")}
        self.assertEqual(first, second)

    def test_producer_waits_for_ack_and_can_cancel(self):
        host = self.host("generated-source")
        host.send("invoke", {"job_id": "job", "action": "start",
                             "arguments": {"count": 20, "interval": 0.01}}, "job")
        opened = host.inbox.get(timeout=3)
        self.assertEqual(opened["method"], "stream/open")
        host.send("stream/opened", {"stream_id": opened["params"]["stream_id"]})
        first = host.inbox.get(timeout=3)
        self.assertEqual(first["method"], "stream/data")
        with self.assertRaises(queue.Empty):
            host.inbox.get(timeout=0.25)
        host.send("cancel", {"job_id": "job"})
        with self.assertRaises(RuntimeError):
            host.result("job")

    def sdk(self):
        spec = importlib.util.spec_from_file_location("plugin_test_sdk", ROOT / "plugins/sdk/python/cilogg_plugin.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        manifest = self.root / "plugin.json"
        manifest.write_text(json.dumps({"id": "test", "capabilities": ["tool"]}), encoding="utf-8")
        plugin = module.Plugin(str(manifest))
        plugin.send = lambda _message: None
        return module, plugin

    def test_cancelled_stream_open_releases_registration(self):
        sdk, plugin = self.sdk()
        for index in range(130):
            job = sdk.Job(plugin, str(index), {})
            job.cancelled.set()
            with self.assertRaises(sdk.Cancelled):
                sdk.Stream(job)
        self.assertEqual(plugin.streams, {})

    def test_shutdown_joins_worker_cleanup(self):
        _sdk, plugin = self.sdk()
        started = threading.Event()
        cleaned = threading.Event()

        @plugin.action("wait")
        def action(job, _arguments):
            try:
                started.set()
                job.cancelled.wait(3)
                job.check_cancelled()
            finally:
                time.sleep(0.2)
                cleaned.set()

        plugin._invoke("job", {"job_id": "job", "action": "wait"})
        self.assertTrue(started.wait(1))
        with patch.object(sys, "stdin", io.TextIOWrapper(io.BytesIO(b""))):
            plugin.serve()
        self.assertTrue(cleaned.is_set())
        self.assertFalse(plugin.workers)


if __name__ == "__main__":
    unittest.main()
