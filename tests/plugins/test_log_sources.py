"""Log-source tests using real child processes and loopback UDP; no hardware needed."""
from __future__ import annotations

import importlib.util
import json
import pathlib
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

from test_python_plugins import Host, ROOT


class Capture:
    def __init__(self):
        self.records = []
        self.closed = threading.Event()

    def write(self, records):
        self.records.extend(records)

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        self.closed.set()


class LogSources(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)

    def module(self, example):
        spec = importlib.util.spec_from_file_location("log_source_sdk", ROOT / "plugins/sdk/python/cilogg_plugin.py")
        sdk = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(sdk)
        constructor = sdk.Plugin
        manifest = ROOT / "plugins/examples" / example / "plugin.json"
        spec = importlib.util.spec_from_file_location("log_source", manifest.parent / "main.py")
        module = importlib.util.module_from_spec(spec)
        with patch.dict(sys.modules, {"cilogg_plugin": sdk}), patch.object(
                sdk, "Plugin", lambda: constructor(str(manifest))):
            spec.loader.exec_module(module)
        frames = []
        module.plugin.send = frames.append
        job = sdk.Job(module.plugin, "test", {})
        capture = Capture()
        job.stream = lambda: capture
        return module, sdk, job, capture, frames

    def fake_adb(self, module):
        module.plugin.configuration.setdefault("adb_path", "fake-adb")
        script = self.root / "fake_adb.py"
        script.write_text('''import sys, time
sys.stdout.reconfigure(encoding="utf-8")
if "devices" in sys.argv:
    print("List of devices attached")
    print("test-serial device model:TestDevice")
    print("locked unauthorized")
elif "failure" in sys.argv:
    print("device unauthorized", file=sys.stderr, flush=True)
    sys.exit(3)
elif "idle" in sys.argv:
    time.sleep(30)
elif "huge" in sys.argv:
    print("x" * 100000)
else:
    sys.stderr.write("diagnostic\\n" * 20000)
    sys.stderr.flush()
    print("10-13 12:00:00.000 1 2 I Tag: שלום", flush=True)
    print("second record", flush=True)
''', encoding="utf-8")
        original = subprocess.Popen
        invocations = []
        processes = []

        def launch(argv, **kwargs):
            invocations.append(argv)
            process = original([sys.executable, "-u", str(script), *argv[1:]], **kwargs)
            processes.append(process)
            return process

        return patch.object(module.subprocess, "Popen", launch), invocations, processes

    def host(self, example):
        package = self.root / example
        shutil.copytree(ROOT / "plugins/examples" / example, package)
        shutil.copy2(ROOT / "plugins/sdk/python/cilogg_plugin.py", package)
        host = Host(package)
        self.addCleanup(host.close)
        return host

    def available_port(self, family=socket.AF_INET):
        with socket.socket(family, socket.SOCK_DGRAM) as sock:
            sock.bind(("::1" if family == socket.AF_INET6 else "127.0.0.1", 0))
            return sock.getsockname()[1]

    def async_server(self, host, port, count=1, bind="127.0.0.1"):
        result = {}
        done = threading.Event()

        def invoke():
            try:
                result["value"] = host.invoke("start", {"bind": bind, "port": port, "count": count})
            except Exception as error:
                result["error"] = error
            finally:
                done.set()

        thread = threading.Thread(target=invoke, daemon=True)
        thread.start()
        deadline = time.monotonic() + 5
        while not host.panels and not done.is_set() and time.monotonic() < deadline:
            done.wait(0.01)
        self.assertTrue(host.panels, result)
        self.addCleanup(thread.join, 1)
        return result, done

    def finish(self, result, done):
        self.assertTrue(done.wait(5), "Plugin did not finish")
        if "error" in result:
            raise result["error"]
        return result["value"]

    def test_adb_stream_drains_stderr_and_preserves_unicode(self):
        module, _sdk, job, capture, _frames = self.module("adb-logcat")
        module.plugin.configuration = {"adb_path": "path with spaces/adb", "serial": "default"}
        fake, invocations, processes = self.fake_adb(module)
        with fake:
            result = module.start(job, {"serial": "chosen", "buffer": "all", "filters": "Tag:D *:S"})
        self.assertEqual(result["records"], 2)
        self.assertIn("שלום", capture.records[0])
        self.assertEqual(invocations[0], ["path with spaces/adb", "-s", "chosen", "logcat",
                                        "-v", "threadtime", "-b", "all", "Tag:D", "*:S"])
        self.assertTrue(all(process.poll() is not None for process in processes))

    def test_adb_devices_include_unauthorized_state(self):
        module, _sdk, job, _capture, frames = self.module("adb-logcat")
        fake, _invocations, _processes = self.fake_adb(module)
        with fake:
            result = module.devices(job, {})
        self.assertEqual(result["devices"][1], ["locked", "unauthorized", ""])
        self.assertTrue(any(frame.get("method") == "panel/update" for frame in frames))

    def test_adb_idle_cancellation_terminates_process(self):
        module, sdk, job, _capture, _frames = self.module("adb-logcat")
        fake, _invocations, processes = self.fake_adb(module)
        timer = threading.Timer(0.3, job.cancelled.set)
        timer.start()
        try:
            with fake, self.assertRaises(sdk.Cancelled):
                module.start(job, {"serial": "idle"})
        finally:
            timer.cancel()
            timer.join()
        self.assertTrue(all(process.poll() is not None for process in processes))

    def test_adb_tab_close_terminates_idle_process(self):
        module, sdk, job, capture, _frames = self.module("adb-logcat")
        fake, _invocations, processes = self.fake_adb(module)
        timer = threading.Timer(0.3, capture.closed.set)
        timer.start()
        try:
            with fake, self.assertRaises(sdk.Cancelled):
                module.start(job, {"serial": "idle"})
        finally:
            timer.cancel()
            timer.join()
        self.assertTrue(all(process.poll() is not None for process in processes))

    def test_adb_failure_keeps_diagnostic(self):
        module, _sdk, job, _capture, _frames = self.module("adb-logcat")
        fake, _invocations, _processes = self.fake_adb(module)
        with fake, self.assertRaisesRegex(RuntimeError, "unauthorized"):
            module.start(job, {"serial": "failure"})

    def test_adb_large_line_stays_within_frame_limit(self):
        module, _sdk, job, capture, _frames = self.module("adb-logcat")
        fake, _invocations, _processes = self.fake_adb(module)
        with fake:
            module.start(job, {"serial": "huge"})
        self.assertEqual("".join(capture.records), "x" * 100000)
        self.assertLessEqual(max(map(len, capture.records)), 32768)

    def test_adb_invalid_filter_does_not_start_process(self):
        module, _sdk, job, _capture, _frames = self.module("adb-logcat")
        with patch.object(module.subprocess, "Popen") as launch:
            with self.assertRaises(ValueError):
                module.start(job, {"filters": "--clear"})
            launch.assert_not_called()

    def test_adb_sdk_environment_discovery(self):
        module, _sdk, _job, _capture, _frames = self.module("adb-logcat")
        sdk_root = self.root / "android-sdk"
        (sdk_root / "platform-tools").mkdir(parents=True)
        binary = sdk_root / "platform-tools" / ("adb.exe" if sys.platform == "win32" else "adb")
        binary.touch()
        with patch.dict(module.os.environ, {"ANDROID_HOME": str(sdk_root)}):
            self.assertEqual(module.executable(), str(binary))

    def test_adb_missing_installation_has_useful_error(self):
        module, _sdk, _job, _capture, _frames = self.module("adb-logcat")
        with patch.object(module.pathlib.Path, "is_file", return_value=False), \
                patch.object(module.shutil, "which", return_value=None):
            with self.assertRaisesRegex(RuntimeError, "platform-tools"):
                module.executable()

    def test_adb_unicode_survives_read_boundary(self):
        module, _sdk, job, capture, _frames = self.module("adb-logcat")
        script = self.root / "unicode_adb.py"
        script.write_text('import sys\nsys.stdout.buffer.write(("x" * 32767 + "שלום\\n").encode("utf-8"))\n',
                          encoding="utf-8")
        original = subprocess.Popen
        with patch.object(module.subprocess, "Popen", lambda _argv, **kw:
                          original([sys.executable, str(script)], **kw)):
            self.assertEqual("".join(module.output(job, ["fake-adb"])), "x" * 32767 + "שלום")

    def test_syslog_tab_close_releases_listener(self):
        module, sdk, job, capture, _frames = self.module("syslog-server")
        port = self.available_port()
        timer = threading.Timer(0.3, capture.closed.set)
        timer.start()
        try:
            with self.assertRaises(sdk.Cancelled):
                module.start(job, {"bind": "127.0.0.1", "port": port})
        finally:
            timer.cancel()
            timer.join()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(("127.0.0.1", port))

    def test_syslog_modern_and_legacy_headers(self):
        module, _sdk, _job, _capture, _frames = self.module("syslog-server")
        modern = module.metadata('<165>1 2003-10-11T22:14:15.003Z host app 123 ID47 [meta x="y"] text')
        self.assertEqual(modern["severity"], "NOTICE")
        self.assertEqual(modern["facility"], "local4")
        self.assertEqual(modern["app"], "app")
        legacy = module.metadata("<34>Oct 11 22:14:15 host app: message")
        self.assertEqual(legacy["format"], "rfc3164")
        self.assertEqual(legacy["severity"], "CRIT")
        self.assertEqual(module.metadata("<999>bad")["severity"], "unknown")

    def test_syslog_protocol_capture_preserves_structured_data(self):
        host = self.host("syslog-server")
        port = self.available_port()
        result, done = self.async_server(host, port, count=2)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
            sender.sendto(b'<34>Oct 11 22:14:15 host app: legacy', ("127.0.0.1", port))
            sender.sendto('<166>1 2026-10-13T00:00:00Z host app 1 ID [meta k="v"] שלום'.encode(),
                          ("127.0.0.1", port))
        self.assertEqual(self.finish(result, done)["messages"], 2)
        self.assertIn("severity=CRIT", host.records[0])
        self.assertIn("sender=127.0.0.1:", host.records[0])
        self.assertIn('[meta k="v"] שלום', host.records[1])

    def test_syslog_filters_and_raw_encoding(self):
        host = self.host("syslog-server")
        host.send("configure", {"configuration": {"max_severity": 4, "output": "raw",
                  "encoding": "latin-1", "allowed_senders": "127.0.0.0/8"}})
        port = self.available_port()
        result, done = self.async_server(host, port)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
            sender.sendto(b"<191>debug filtered", ("127.0.0.1", port))
            sender.sendto(b"<132>caf\xe9\nsecond\x00\r\n", ("127.0.0.1", port))
        value = self.finish(result, done)
        self.assertEqual(value["filtered"], 1)
        self.assertEqual(host.records, ["<132>café\\x0asecond"])

    def test_syslog_sender_filter_and_idle_cancel_release_port(self):
        host = self.host("syslog-server")
        host.send("configure", {"configuration": {"allowed_senders": "192.0.2.0/24"}})
        port = self.available_port()
        result, done = self.async_server(host, port, count=0)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
            sender.sendto(b"<13>rejected", ("127.0.0.1", port))
        time.sleep(0.1)
        host.send("cancel", {"job_id": "job"})
        self.assertTrue(done.wait(3))
        self.assertIsInstance(result.get("error"), RuntimeError)
        self.assertFalse(host.records)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(("127.0.0.1", port))

    def test_syslog_conflicting_listener_reports_error(self):
        host = self.host("syslog-server")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as occupied:
            occupied.bind(("127.0.0.1", 0))
            with self.assertRaisesRegex(RuntimeError, "Cannot listen"):
                host.invoke("start", {"bind": "127.0.0.1", "port": occupied.getsockname()[1]})

    def test_syslog_largest_udp_record_and_malformed_payload(self):
        host = self.host("syslog-server")
        port = self.available_port()
        result, done = self.async_server(host, port, count=2)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sender:
            sender.sendto(b"x" * 65000, ("127.0.0.1", port))
            sender.sendto(b"<999>bad\xff\nmore", ("127.0.0.1", port))
        self.assertEqual(self.finish(result, done)["messages"], 2)
        self.assertTrue(host.records[0].endswith("x" * 65000))
        self.assertIn("severity=unknown", host.records[1])
        self.assertIn("bad\ufffd\\x0amore", host.records[1])

    def test_syslog_ipv6_loopback(self):
        if not socket.has_ipv6:
            self.skipTest("IPv6 unavailable")
        try:
            port = self.available_port(socket.AF_INET6)
        except OSError:
            self.skipTest("IPv6 loopback unavailable")
        host = self.host("syslog-server")
        result, done = self.async_server(host, port, bind="::1")
        with socket.socket(socket.AF_INET6, socket.SOCK_DGRAM) as sender:
            sender.sendto(b"<13>ipv6", ("::1", port))
        self.assertEqual(self.finish(result, done)["messages"], 1)
        self.assertIn("sender=[::1]:", host.records[0])


if __name__ == "__main__":
    unittest.main()
