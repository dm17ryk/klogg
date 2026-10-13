"""Behavioral regressions for the live-source plugin review findings."""
from __future__ import annotations

import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import MagicMock, patch

import test_log_sources as log_sources
from test_python_plugins import ROOT


class ReviewRegressions(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = pathlib.Path(temporary.name)

    def module(self, example):
        return log_sources.LogSources.module(self, example)

    def test_syslog_defaults_to_loopback_and_allows_explicit_network_bind(self):
        manifest = json.loads((ROOT / "plugins/examples/syslog-server/plugin.json").read_text())
        bind = next(field for field in manifest["actions"][0]["fields"] if field["id"] == "bind")
        self.assertEqual(bind["default"], "127.0.0.1")
        for arguments, expected in (({}, "127.0.0.1"), ({"bind": "0.0.0.0"}, "0.0.0.0")):
            with self.subTest(bind=expected):
                module, _sdk, job, _capture, _frames = self.module("syslog-server")
                sock = MagicMock()
                sock.__enter__.return_value = sock
                sock.recvfrom.return_value = (b"<13>local", ("127.0.0.1", 1234))
                with patch.object(module.socket, "socket", return_value=sock), \
                        patch.object(module.select, "select", return_value=([sock], [], [])):
                    module.start(job, dict(arguments, port=5514, count=1))
                sock.bind.assert_called_once_with((expected, 5514))

    def test_syslog_cancel_flushes_records_already_received(self):
        module, sdk, job, capture, _frames = self.module("syslog-server")
        sock = MagicMock()
        sock.__enter__.return_value = sock

        def receive(_size):
            job.cancelled.set()
            return b"<13>received before cancellation", ("127.0.0.1", 1234)

        sock.recvfrom.side_effect = receive
        with patch.object(module.socket, "socket", return_value=sock), \
                patch.object(module.select, "select", return_value=([sock], [], [])), \
                patch.object(capture, "write", wraps=capture.write) as write:
            with self.assertRaises(sdk.Cancelled):
                module.start(job, {"port": 5514})
        self.assertEqual(len(capture.records), 1)
        self.assertIn("received before cancellation", capture.records[0])
        self.assertTrue(write.call_args.kwargs["finish_on_cancel"])
        sock.__exit__.assert_called_once()

    def test_syslog_escapes_c0_del_and_all_c1_controls(self):
        module, _sdk, _job, _capture, _frames = self.module("syslog-server")
        controls = "\x00\n\x7f" + "".join(chr(value) for value in range(128, 160))
        expected = "\\x00\\x0a\\x7f" + "".join(f"\\x{value:02x}" for value in range(128, 160))
        self.assertEqual(module.printable("שלום" + controls), "שלום" + expected)

    def test_adb_rejects_unknown_priorities_before_launch(self):
        module, _sdk, job, _capture, _frames = self.module("adb-logcat")
        for priority in ("A", "Q", "Z"):
            with self.subTest(priority=priority), patch.object(module.subprocess, "Popen") as launch:
                with self.assertRaises(ValueError):
                    module.start(job, {"filters": f"MyTag:{priority}"})
                launch.assert_not_called()

    def test_adb_keeps_android_default_priority(self):
        module, _sdk, job, _capture, _frames = self.module("adb-logcat")
        fake, invocations, _processes = log_sources.LogSources.fake_adb(self, module)
        with fake:
            module.start(job, {"filters": "MyTag:* *:S"})
        self.assertEqual(invocations[0][-2:], ["MyTag:*", "*:S"])

    def test_device_panels_retain_all_rows_within_element_limit(self):
        for count in (62, 63, 120):
            with self.subTest(count=count):
                module, _sdk, job, _capture, frames = self.module("adb-logcat")
                lines = [f"serial-{index} device model:Test" for index in range(count)]
                with patch.object(module, "output", return_value=iter(lines)), \
                        patch.object(module, "executable", return_value="adb"):
                    result = module.devices(job, {})
                panel = next(frame["params"]["panel"] for frame in frames
                             if frame.get("method") == "panel/update")
                self.assertLessEqual(len(panel["elements"]), 64)
                self.assertEqual(len(panel["elements"][0]["rows"]), count)
                self.assertEqual(len(result["devices"]), count)
                self.assertEqual(panel["elements"][-1]["action"], "devices")

    def test_logcat_batches_high_rate_records_and_final_partial_batch(self):
        module, _sdk, job, capture, _frames = self.module("adb-logcat")
        records = [f"record-{index}" for index in range(200)]
        with patch.object(module, "command", return_value=["adb"]), \
                patch.object(module, "output", return_value=(record for record in records)), \
                patch.object(capture, "write", wraps=capture.write) as write:
            result = module.start(job, {})
        self.assertEqual(capture.records, records)
        self.assertEqual(result["records"], 200)
        self.assertEqual(write.call_count, 4)
        self.assertTrue(all(len(call.args[0]) <= 64 for call in write.call_args_list))

    def test_logcat_flushes_while_idle_and_on_cancellation(self):
        module, sdk, job, capture, _frames = self.module("adb-logcat")

        def records():
            yield "first"
            yield None
            self.assertEqual(capture.records, ["first"])
            yield "last"
            job.cancelled.set()
            raise sdk.Cancelled("Job cancelled")

        with patch.object(module, "command", return_value=["adb"]), \
                patch.object(module, "output", return_value=records()):
            with self.assertRaises(sdk.Cancelled):
                module.start(job, {})
        self.assertEqual(capture.records, ["first", "last"])

    def test_logcat_bounds_serialized_batches_for_escaped_records(self):
        module, _sdk, job, capture, _frames = self.module("adb-logcat")
        records = ["\x00" * 32768 for _ in range(12)]
        with patch.object(module, "command", return_value=["adb"]), \
                patch.object(module, "output", return_value=(record for record in records)), \
                patch.object(capture, "write", wraps=capture.write) as write:
            module.start(job, {})
        self.assertEqual(capture.records, records)
        for call in write.call_args_list:
            frame = {"jsonrpc": "2.0", "method": "stream/data", "params": {
                "stream_id": "test", "sequence": 0, "records": call.args[0]}}
            self.assertLess(len(module.plugin.encode(frame)), 1024 * 1024)

    def test_adb_passes_metacharacters_as_literal_arguments_without_shell(self):
        module, _sdk, job, _capture, _frames = self.module("adb-logcat")
        fake, invocations, _processes = log_sources.LogSources.fake_adb(self, module)
        serial = "device with spaces & $(echo injected); literal"
        with fake, patch.object(module.subprocess, "Popen", wraps=module.subprocess.Popen) as launch:
            module.start(job, {"serial": serial})
        self.assertEqual(invocations[0][2], serial)
        self.assertIs(launch.call_args.kwargs["shell"], False)

    @unittest.skipUnless(sys.platform == "win32", "Windows batch launch restriction")
    def test_adb_rejects_windows_batch_executables(self):
        module, _sdk, job, _capture, _frames = self.module("adb-logcat")
        for extension in (".bat", ".cmd"):
            with self.subTest(extension=extension), patch.object(module.subprocess, "Popen") as launch:
                with self.assertRaisesRegex(ValueError, "native ADB executable"):
                    list(module.output(job, ["adb" + extension]))
                launch.assert_not_called()

    def stream(self, acknowledge=True):
        module, sdk, job, _capture, _frames = self.module("syslog-server")
        received = []

        def notify(method, params):
            stream = module.plugin.streams.get(params.get("stream_id"))
            if method == "stream/open":
                stream.opened.set()
            elif method == "stream/data":
                received.extend(params["records"])
                if acknowledge:
                    stream.ack.set()

        module.plugin.notify = notify
        stream = sdk.Stream(job)
        self.addCleanup(stream.close)
        return sdk, job, stream, received

    def test_sdk_preserves_default_immediate_cancellation(self):
        sdk, job, stream, received = self.stream()
        job.cancelled.set()
        with self.assertRaises(sdk.Cancelled):
            stream.write(["pending"])
        self.assertFalse(received)
        self.assertEqual(stream.plugin.pending_bytes, 0)

    def test_sdk_opt_in_flush_after_cancel_waits_for_ack(self):
        _sdk, job, stream, received = self.stream()
        job.cancelled.set()
        stream.write(["pending"], finish_on_cancel=True)
        self.assertEqual(received, ["pending"])
        self.assertEqual(stream.sequence, 1)
        self.assertEqual(stream.plugin.pending_bytes, 0)

    def test_sdk_cancel_flush_timeout_releases_credit(self):
        sdk, job, stream, received = self.stream(acknowledge=False)
        job.cancelled.set()
        started = time.monotonic()
        with patch.object(sdk, "CANCEL_FLUSH_TIMEOUT", 0.05), \
                self.assertRaisesRegex(sdk.Cancelled, "flush timed out"):
            stream.write(["pending"], finish_on_cancel=True)
        self.assertLess(time.monotonic() - started, 3)
        self.assertEqual(received, ["pending"])
        self.assertEqual(stream.plugin.pending_bytes, 0)

    def test_sdk_cancel_flush_still_honors_stop_and_shutdown(self):
        for event in ("closed", "stopping"):
            with self.subTest(event=event):
                sdk, job, stream, received = self.stream()
                job.cancelled.set()
                (stream.closed if event == "closed" else stream.plugin.stopping).set()
                with self.assertRaises(sdk.Cancelled):
                    stream.write(["pending"], finish_on_cancel=True)
                self.assertFalse(received)

    def test_sdk_flush_handles_cancellation_while_waiting_for_ack(self):
        _sdk, job, stream, received = self.stream(acknowledge=False)
        timers = [threading.Timer(0.05, job.cancelled.set), threading.Timer(0.2, stream.ack.set)]
        for timer in timers:
            timer.start()
        try:
            stream.write(["in flight"], finish_on_cancel=True)
        finally:
            for timer in timers:
                timer.cancel()
                timer.join()
        self.assertEqual(received, ["in flight"])
        self.assertEqual(stream.sequence, 1)

    def test_sdk_cancel_flush_bounds_credit_wait_without_emitting_data(self):
        sdk, job, stream, received = self.stream()
        stream.plugin.pending_bytes = sdk.MAX_PENDING
        job.cancelled.set()
        with patch.object(sdk, "CANCEL_FLUSH_TIMEOUT", 0.05), \
                self.assertRaisesRegex(sdk.Cancelled, "flush timed out"):
            stream.write(["pending"], finish_on_cancel=True)
        self.assertFalse(received)
        self.assertEqual(stream.plugin.pending_bytes, sdk.MAX_PENDING)


@unittest.skipUnless(sys.platform == "win32" and shutil.which("pwsh"), "Windows release validator")
class WindowsPayloadValidation(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = pathlib.Path(temporary.name)
        for name in ("cilogg.exe", "Qt6Sql.dll", "sqldrivers/qsqlite.dll",
                     "python_runtime/klogg/__init__.py", "platforms/qwindows.dll"):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        for folder in ("adb-logcat", "syslog-server"):
            destination = self.root / "plugins" / folder
            shutil.copytree(ROOT / "plugins/examples" / folder, destination)
            shutil.copy2(ROOT / "plugins/sdk/python/cilogg_plugin.py", destination)

    def validate(self):
        # Execute the real validation functions without invoking the installer loop.
        quote = lambda path: str(path).replace("'", "''")
        script = self.root / "validate.ps1"
        script.write_text(f"""$ErrorActionPreference = 'Stop'
$preinstalledValidator = '{quote(ROOT / 'scripts/plugins/validate_preinstalled.py')}'
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    '{quote(ROOT / 'scripts/validate_release_windows.ps1')}', [ref]$null, [ref]$null)
$functions = $ast.FindAll({{
    param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst]
}}, $false)
foreach ($function in $functions) {{ Invoke-Expression $function.Extent.Text }}
Assert-KloggDeployment -Root '{quote(self.root)}' -Context 'test package'
""", encoding="utf-8")
        return subprocess.run(["pwsh", "-NoProfile", "-File", str(script)],
                              capture_output=True, text=True, encoding="utf-8", errors="replace")

    def test_complete_payload_passes(self):
        result = self.validate()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_missing_sdk_fails(self):
        (self.root / "plugins/syslog-server/cilogg_plugin.py").unlink()
        result = self.validate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid preinstalled plugin payload", result.stderr)

    def test_disabled_default_fails(self):
        manifest = self.root / "plugins/adb-logcat/plugin.json"
        value = json.loads(manifest.read_text())
        value["enabled_by_default"] = False
        manifest.write_text(json.dumps(value))
        self.assertNotEqual(self.validate().returncode, 0)


if __name__ == "__main__":
    unittest.main()
