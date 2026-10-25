"""Black-box contracts for native GUI and direct-service timing reports."""

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "scripts/benchmarks/workbench-benchmark.py"
FAKE = Path(__file__).with_name("fake_native_benchmark.py")


class NativeBenchmarkTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="cilogg-native-benchmark-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.corpus = self.root / "corpus"
        generated = subprocess.run(
            [
                sys.executable,
                str(RUNNER),
                "--generate-corpus",
                str(self.corpus),
                "--rows",
                "1000",
            ],
            check=False,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(generated.returncode, 0, generated.stderr)

    def run_fixture(self, mode="normal", *extra):
        output = self.root / "run"
        env = dict(os.environ, FAKE_NATIVE_MODE=mode)
        result = subprocess.run(
            [
                sys.executable,
                str(RUNNER),
                "--scenario",
                "ui-service",
                "--executable",
                str(FAKE),
                "--test-executable",
                str(FAKE),
                "--corpus",
                str(self.corpus),
                "--output",
                str(output),
                "--repetitions",
                "1",
                *extra,
            ],
            check=False,
            env=env,
            capture_output=True,
            text=True,
            timeout=30,
        )
        path = output / "result.json"
        self.assertTrue(
            path.is_file(),
            f"Report missing; exit={result.returncode}; stderr={result.stderr}",
        )
        return result, json.loads(path.read_text(encoding="utf-8")), output

    def test_native_measurements_have_exact_boundaries_and_test_double_identity(self):
        result, report, _ = self.run_fixture()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(report["passed"])
        self.assertEqual(report["evidenceKind"], "test_double")
        self.assertEqual(report["runs"][0]["measurements"]["guiEventP95Ms"], 1.0)
        self.assertEqual(report["runs"][0]["measurements"]["scrollPaintP95Ms"], 2.0)
        self.assertEqual(
            report["runs"][0]["measurements"]["serviceCancellationMs"], 10.0
        )
        self.assertEqual(report["releaseGateStatus"], "pending")
        self.assertIn("scrollPresentationLatency", report["unmeasured"])

    def test_slow_gui_fails_gate_and_retains_measurements(self):
        result, report, _ = self.run_fixture("slow")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertFalse(report["passed"])
        self.assertEqual(report["reason"], "budget_exceeded")
        self.assertEqual(report["runs"][0]["measurements"]["guiEventP95Ms"], 200.0)

    def test_invalid_native_measurements_never_pass(self):
        for mode in ("wrong_source", "short", "nonfinite", "inactive", "invalid_schema"):
            with self.subTest(mode=mode):
                child = self.root / mode
                child.mkdir()
                previous = self.root
                self.root = child
                try:
                    result, report, _ = self.run_fixture(mode)
                finally:
                    self.root = previous
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertFalse(report["passed"])

    def test_native_failure_and_missing_report_retain_diagnostics(self):
        result, report, output = self.run_fixture("crash")
        self.assertEqual(result.returncode, 1)
        self.assertFalse(report["passed"])
        self.assertIn(
            "native fixture failure", (output / "native-0-stderr.log").read_text()
        )

    def test_timeout_is_a_failed_gate(self):
        budgets = self.root / "budgets.json"
        budgets.write_text(json.dumps({"nativeSuiteSeconds": 1.0}), encoding="utf-8")
        result, report, _ = self.run_fixture("stall", "--budgets", str(budgets))
        self.assertEqual(result.returncode, 1)
        self.assertEqual(report["reason"], "native_timeout")

    def test_too_few_samples_are_invalid_setup(self):
        result, report, _ = self.run_fixture("normal", "--samples", "3")
        self.assertEqual(result.returncode, 2)
        self.assertEqual(report["reason"], "invalid_samples")

    def test_p95_uses_nearest_rank_and_keeps_tail_samples(self):
        result, report, _ = self.run_fixture("tail_five")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(report["runs"][0]["measurements"]["guiEventP95Ms"], 1.0)
        self.root = self.root / "six"
        self.root.mkdir()
        result, report, _ = self.run_fixture("tail_six")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(report["runs"][0]["measurements"]["guiEventP95Ms"], 50.0)

    def test_missing_native_measurements_are_a_failure(self):
        result, report, _ = self.run_fixture("missing")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(report["reason"], "native_report_missing_or_oversized")

    def test_numeric_report_failures_still_write_structured_results(self):
        for mode in ("huge_timing", "extra_nonfinite"):
            with self.subTest(mode=mode):
                previous = self.root
                self.root = previous / mode
                self.root.mkdir()
                try:
                    result, report, _ = self.run_fixture(mode)
                finally:
                    self.root = previous
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(report["reason"], "invalid_native_report")


if __name__ == "__main__":
    unittest.main()
