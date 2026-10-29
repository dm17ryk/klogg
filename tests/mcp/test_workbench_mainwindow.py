"""A component report must never masquerade as native MainWindow evidence."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
RUNNER = HERE / "workbench_usability.py"
FIXTURE = HERE / "fixtures/fake_usability.py"


class MainWindowTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def invoke(self, mode="normal"):
        # Different executable bytes catch selecting the component worker hash.
        probe = self.root / "probe.py"
        probe.write_bytes(FIXTURE.read_bytes() + b"\n# MainWindow worker identity\n")
        output = self.root / "artifacts"
        result = subprocess.run(
            [
                sys.executable,
                str(RUNNER),
                "--executable",
                str(FIXTURE),
                "--test-executable",
                str(probe),
                "--probe",
                "mainwindow",
                "--artifacts",
                str(output),
            ],
            env=dict(os.environ, CILOGG_FAKE_USABILITY=mode),
            capture_output=True,
            text=True,
            timeout=30,
        )
        path = output / "result.json"
        self.assertTrue(path.is_file(), result.stderr)
        return result, json.loads(path.read_text(encoding="utf-8"))

    def test_mainwindow_mode_has_distinct_worker_and_retained_evidence(self):
        result, report = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(report["probeKind"], "mainwindow")
        self.assertEqual(report["evidenceKind"], "test_double")
        self.assertEqual(report["releaseGateStatus"], "pending")
        self.assertNotEqual(report["binarySha256"], report["testBinarySha256"])
        self.assertEqual(
            report["native"]["workerBinarySha256"], report["testBinarySha256"]
        )
        self.assertEqual(len(report["native"]["cases"]), 2)
        self.assertEqual(
            hashlib.sha256(FIXTURE.read_bytes()).hexdigest(), report["binarySha256"]
        )

    def test_component_report_cannot_pass_for_a_mainwindow(self):
        result, report = self.invoke("component_report")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(report["reason"], "probe_kind_mismatch")

    def test_missing_mainwindow_interaction_is_rejected(self):
        for mode in (
            "no_menu",
            "no_toolbar",
            "no_navigation",
            "no_layout_restore",
            "draft_persisted",
        ):
            with self.subTest(mode=mode):
                self.root = Path(self.temp.name) / mode
                self.root.mkdir()
                result, report = self.invoke(mode)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertFalse(report["passed"])

    def test_wrong_file_provenance_or_physical_navigation_is_rejected(self):
        for mode in ("no_source_id", "wrong_watermark", "wrong_navigation_line"):
            with self.subTest(mode=mode):
                self.root = Path(self.temp.name) / mode
                self.root.mkdir()
                result, report = self.invoke(mode)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertFalse(report["passed"])

    def test_restored_window_capture_is_required(self):
        result, report = self.invoke("missing_restored_png")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(report["reason"], "screenshot_missing_or_oversized")

    def test_unicode_byte_offsets_cannot_pass_as_utf16(self):
        result, report = self.invoke("wrong_utf16")
        self.assertEqual(result.returncode, 1)
        self.assertFalse(report["passed"])

    def test_consistently_wrong_job_provenance_is_rejected_against_snapshot(self):
        result, report = self.invoke("wrong_snapshot")
        self.assertEqual(result.returncode, 1)
        self.assertFalse(report["passed"])

    def test_missing_request_revision_never_compares_as_equal_missing_values(self):
        result, report = self.invoke("missing_revision")
        self.assertEqual(result.returncode, 1)
        self.assertFalse(report["passed"])


if __name__ == "__main__":
    unittest.main()
