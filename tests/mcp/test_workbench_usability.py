"""Black-box failure gates for native dock interaction evidence."""

import json
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import contextmanager
from pathlib import Path
from unittest.mock import Mock, patch

HERE = Path(__file__).resolve().parent
RUNNER = HERE / "workbench_usability.py"
FIXTURE = HERE / "fixtures" / "fake_usability.py"


class UsabilityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="cilogg-usability-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def invoke(self, mode="normal", *extra):
        output = self.root / "artifacts"
        result = subprocess.run(
            [
                sys.executable,
                str(RUNNER),
                "--executable",
                str(FIXTURE),
                "--test-executable",
                str(FIXTURE),
                "--artifacts",
                str(output),
                *extra,
            ],
            env=dict(os.environ, CILOGG_FAKE_USABILITY=mode),
            capture_output=True,
            text=True,
            timeout=30,
        )
        report = output / "result.json"
        self.assertTrue(report.is_file(), result.stderr)
        return result, json.loads(report.read_text(encoding="utf-8")), output

    def test_complete_evidence_is_bounded_and_labels_test_doubles(self):
        result, report, _ = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(report["passed"])
        self.assertEqual(report["evidenceKind"], "test_double")
        self.assertEqual(report["releaseGateStatus"], "pending")
        self.assertEqual(len(report["native"]["cases"]), 2)

    def test_headless_or_incomplete_interaction_never_passes(self):
        for mode in (
            "offscreen",
            "hidden",
            "unpainted",
            "blank",
            "wrong_results",
            "not_clicked",
            "missing_case",
            "duplicate_case",
            "worker_mismatch",
            "unreleased",
        ):
            with self.subTest(mode=mode):
                self.root = Path(self.temp.name) / mode
                self.root.mkdir()
                result, report, _ = self.invoke(mode)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertFalse(report["passed"])

    def test_missing_corrupt_or_unsafe_screenshot_never_passes(self):
        for mode in ("missing_png", "hash_mismatch", "unsafe_path", "wrong_dimensions"):
            with self.subTest(mode=mode):
                self.root = Path(self.temp.name) / mode
                self.root.mkdir()
                result, report, _ = self.invoke(mode)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertFalse(report["passed"])

    def test_report_must_be_finite_bounded_and_present(self):
        for mode in ("missing_report", "nonfinite", "huge_report", "invalid_schema"):
            with self.subTest(mode=mode):
                self.root = Path(self.temp.name) / mode
                self.root.mkdir()
                result, report, _ = self.invoke(mode)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertFalse(report["passed"])

    def test_worker_failure_retains_diagnostics(self):
        result, report, output = self.invoke("crash")
        self.assertEqual(result.returncode, 1)
        self.assertFalse(report["passed"])
        self.assertIn("fixture crash", (output / "stderr.log").read_text())

    def test_timeout_is_failed_and_owned_children_are_reaped(self):
        result, report, _ = self.invoke("stall", "--timeout", "1")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(report["reason"], "native_timeout")
        self.assertIn("failures=0", result.stderr)

    def test_processes_stop_before_private_ipc_lease_is_released(self):
        import workbench_usability as runner

        events = []

        @contextmanager
        def lease(environment):
            try:
                yield environment
            finally:
                events.append("lease_released")

        owner = Mock()
        owner.spawn.return_value.pid = 1234
        owner.close.side_effect = lambda: events.append("processes_stopped")
        owner.spawn.return_value.wait.side_effect = subprocess.TimeoutExpired(
            "probe", 1
        )
        with (
            patch.object(runner, "private_ipc_environment", lease),
            patch.object(runner, "ProcessOwner", return_value=owner),
        ):
            code = runner.main(
                [
                    "--executable",
                    str(FIXTURE),
                    "--test-executable",
                    str(FIXTURE),
                    "--artifacts",
                    str(self.root / "lease-check"),
                ]
            )
        self.assertEqual(code, 1)
        self.assertLess(
            events.index("processes_stopped"), events.index("lease_released")
        )

    def test_invalid_deadline_is_setup_failure_without_starting_a_probe(self):
        for value in ("0", "nan", "601"):
            with self.subTest(value=value):
                self.root = Path(self.temp.name) / value
                self.root.mkdir()
                result, report, _ = self.invoke("normal", "--timeout", value)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual(report["state"], "invalid_setup")


if __name__ == "__main__":
    unittest.main()
