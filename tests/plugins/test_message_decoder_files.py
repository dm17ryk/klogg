"""Real filesystem regressions for immutable proof inputs and exclusive reports."""

import hashlib
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/plugins"))


class DecoderFileTests(unittest.TestCase):
    def setUp(self):
        utility = ROOT / "scripts/plugins/message_decoder_files.py"
        self.assertTrue(
            utility.is_file(), "Immutable decoder input/report handling is missing"
        )
        spec = importlib.util.spec_from_file_location("decoder_files", utility)
        self.files = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.files)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.manifest = self.root / "manifest.json"
        self.payload = self.root / "body.hex"
        self.payload.write_bytes(b"0102")
        self.manifest.write_text(
            json.dumps(
                {
                    "cases": [
                        {
                            "id": "safe",
                            "dissector": "proof",
                            "hex": "body.hex",
                            "sha256": "a12871fee210fb8619291eaea194581cbd2531e4b23759d225f6806923f63222",
                            "expect": {},
                        }
                    ]
                }
            ),
            encoding="utf-8",
        )

    def test_snapshot_uses_one_manifest_epoch_and_rejects_later_replacement(self):
        inputs = self.files.OriginInputs(self.manifest)
        self.assertEqual(inputs.cases[0]["payload"], b"\x01\x02")
        self.assertEqual(
            inputs.cases[0]["manifestSha256"],
            hashlib.sha256(self.manifest.read_bytes()).hexdigest(),
        )
        self.manifest.write_text('{"cases":[]}', encoding="utf-8")
        destination = self.root / "result.json"
        destination.write_bytes(b"keep")
        with self.assertRaisesRegex(self.files.ProofError, "input_changed"):
            self.files.publish_report(destination, b"replacement", inputs, [])
        self.assertEqual(destination.read_bytes(), b"keep")
        self.assertEqual(inputs.cases[0]["payload"], b"\x01\x02")

    def test_symlink_retarget_after_snapshot_preserves_new_target(self):
        link = self.root / "linked.hex"
        try:
            link.symlink_to(self.payload)
        except OSError as error:
            self.skipTest(str(error))
        manifest = json.loads(self.manifest.read_text(encoding="utf-8"))
        manifest["cases"][0]["hex"] = "linked.hex"
        self.manifest.write_text(json.dumps(manifest), encoding="utf-8")
        inputs = self.files.OriginInputs(self.manifest)
        destination = self.root / "result.json"
        destination.write_bytes(b"0102")
        link.unlink()
        link.symlink_to(destination)
        with self.assertRaisesRegex(
            self.files.ProofError, "input_changed|invalid_origin_destination"
        ):
            self.files.publish_report(destination, b"replacement", inputs, [])
        self.assertEqual(destination.read_bytes(), b"0102")

    def test_hardlink_report_and_lock_paths_cannot_alias_fixture(self):
        destination = self.root / "result.json"
        os.link(self.payload, destination)
        lease = self.root / ".decoder-origins.lock"
        os.link(self.payload, lease)
        inputs = self.files.OriginInputs(self.manifest)
        with self.assertRaisesRegex(
            self.files.ProofError, "invalid_origin_destination"
        ):
            self.files.publish_report(destination, b"replacement", inputs, [])
        self.assertEqual(self.payload.read_bytes(), b"0102")
        with (
            self.assertRaisesRegex(self.files.ProofError, "invalid_origin_destination"),
            self.files.OutputLease(self.root, inputs, []),
        ):
            self.fail("fixture cannot become a lease")
        self.assertEqual(self.payload.read_bytes(), b"0102")

    def test_atomic_report_failure_keeps_existing_report_and_removes_owned_temp(self):
        inputs = self.files.OriginInputs(self.manifest)
        destination = self.root / "result.json"
        destination.write_bytes(b"previous")
        with (
            patch.object(
                self.files.os,
                "replace",
                side_effect=OSError("injected publication failure"),
            ),
            self.assertRaises(OSError),
        ):
            self.files.publish_report(destination, b"new", inputs, [])
        self.assertEqual(destination.read_bytes(), b"previous")
        self.assertEqual(list(self.root.glob(".origin-report-*.tmp")), [])

    def test_same_output_is_exclusive_and_reusable_after_child_exit(self):
        inputs = self.files.OriginInputs(self.manifest)
        # Hold the actual platform lock in another process; no timing sleeps.
        code = (
            "import sys; from pathlib import Path; "
            "sys.path.insert(0,sys.argv[1]); "
            "from message_decoder_files import OriginInputs,OutputLease; "
            "inputs=OriginInputs(Path(sys.argv[2])); "
            "lease=OutputLease(Path(sys.argv[3]),inputs,[]); lease.__enter__(); "
            "print('READY',flush=True); sys.stdin.readline()"
        )
        child = subprocess.Popen(
            [
                sys.executable,
                "-u",
                "-c",
                code,
                str(ROOT / "scripts/plugins"),
                str(self.manifest),
                str(self.root),
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            # Bounded receipt of an observable ownership marker.
            import concurrent.futures

            with concurrent.futures.ThreadPoolExecutor() as pool:
                ready = pool.submit(child.stdout.readline)
                try:
                    self.assertEqual(ready.result(timeout=10).strip(), b"READY")
                except BaseException:
                    child.kill()
                    child.wait(timeout=10)
                    raise
            with (
                self.assertRaisesRegex(self.files.ProofError, "output_busy"),
                self.files.OutputLease(self.root, inputs, []),
            ):
                self.fail("concurrent lease admitted")
            import message_decoder_origins

            destination = self.root / "result.json"
            destination.write_bytes(b"owned by first invocation")
            result = message_decoder_origins.main(
                [
                    "--tshark",
                    sys.executable,
                    "--text2pcap",
                    sys.executable,
                    "--manifest",
                    str(self.manifest),
                    "--output",
                    str(self.root),
                ]
            )
            self.assertEqual(result, 1)
            self.assertEqual(destination.read_bytes(), b"owned by first invocation")
        finally:
            if child.poll() is None:
                child.kill()
            child.communicate(timeout=10)
        with self.files.OutputLease(self.root, inputs, []):
            self.files.publish_report(destination, b"new invocation", inputs, [])
        self.assertEqual(destination.read_bytes(), b"new invocation")

    @unittest.skipUnless(
        hasattr(os, "mkfifo"), "FIFO admission is exercised on Unix CI"
    )
    def test_fifo_manifest_and_fixture_are_rejected_without_a_writer(self):
        fifo = self.root / "fifo"
        os.mkfifo(fifo)
        code = (
            "import sys; from pathlib import Path; sys.path.insert(0,sys.argv[1]); "
            "from message_decoder_files import OriginInputs,ProofError; "
            "\ntry: OriginInputs(Path(sys.argv[2]))"
            "\nexcept ProofError as error: print(str(error)); sys.exit(2)"
        )
        for source in [fifo, self.manifest]:
            if source == self.manifest:
                document = json.loads(self.manifest.read_text(encoding="utf-8"))
                document["cases"][0]["hex"] = "fifo"
                self.manifest.write_text(json.dumps(document), encoding="utf-8")
            result = subprocess.run(
                [
                    sys.executable,
                    "-c",
                    code,
                    str(ROOT / "scripts/plugins"),
                    str(source),
                ],
                capture_output=True,
                check=False,
                timeout=5,
            )
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertIn(
                result.stdout.strip(), [b"input_not_regular", b"input_missing"]
            )

    @unittest.skipUnless(os.name == "nt", "Windows special device")
    def test_windows_device_manifest_is_not_treated_as_regular_input(self):
        with self.assertRaisesRegex(
            self.files.ProofError, "input_not_regular|input_missing"
        ):
            self.files.OriginInputs(Path("NUL"))


if __name__ == "__main__":
    unittest.main()
