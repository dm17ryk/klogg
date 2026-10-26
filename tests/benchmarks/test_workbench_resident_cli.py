"""Black-box opt-in RSS reporting and gate contracts; no product API changes."""

import json
import importlib
import os
import sys
import unittest
from types import SimpleNamespace
from unittest.mock import patch

import test_workbench_gui as fixtures

sys.path.insert(0, str(fixtures.ROOT / "scripts/benchmarks"))


class ResidentCliTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.NativeBenchmarkTests(
            "test_native_measurements_have_exact_boundaries_and_test_double_identity"
        )
        self.addCleanup(self.fixture.doCleanups)
        self.fixture.setUp()

    def run_inprocess(self, mode="normal", *extra):
        benchmark = importlib.import_module("workbench_benchmark")
        output = self.fixture.root / "inprocess-run"
        args = [
            "--scenario",
            "ui-service",
            "--executable",
            str(fixtures.FAKE),
            "--test-executable",
            str(fixtures.FAKE),
            "--corpus",
            str(self.fixture.corpus),
            "--output",
            str(output),
            "--repetitions",
            "1",
            *extra,
        ]
        with patch.dict(os.environ, FAKE_NATIVE_MODE=mode):
            with patch.object(benchmark.logging, "basicConfig"):
                code = benchmark.main(args)
        return (
            code,
            json.loads((output / "result.json").read_text(encoding="utf-8")),
            output,
        )

    @staticmethod
    def controlled_sampler(_owner, host_pid):
        def result(status="sampled"):
            return {
                "status": status,
                "hostPid": host_pid,
                "backend": "controlled_fixture",
                "samples": 3,
                "measurements": {
                    "processTreeSampledRssPeakBytes": 100,
                    "hostSampledRssPeakBytes": 80,
                    "workersSampledRssPeakBytes": 20,
                },
            }

        return SimpleNamespace(
            sample=lambda: None,
            wait=lambda process, timeout: process.wait(timeout=timeout),
            result=result,
        )

    def test_default_does_not_sample_or_install_a_resident_budget(self):
        with patch(
            "workbench_gui.ResidentMemorySampler",
            side_effect=AssertionError("Default constructed RSS sampler"),
        ) as sampler:
            with patch(
                "workbench_resident._WindowsWorkingSet",
                side_effect=AssertionError("Default constructed RSS backend"),
            ) as backend:
                code, report, _ = self.run_inprocess()
                self.assertEqual(code, 0, report)
                sampler.assert_not_called()
                backend.assert_not_called()
        run = report["runs"][0]
        self.assertIn("residentMemory", run)
        self.assertEqual(run["residentMemory"]["status"], "disabled")
        self.assertNotIn("processTreeSampledRssPeakBytes", report["budgets"])
        self.assertNotIn("processTreeSampledRssPeakBytes", run["gates"])

    def test_initialization_failure_retains_failed_sampling_diagnostics(self):
        resident = importlib.import_module("workbench_resident")
        with patch(
            "workbench_gui.ResidentMemorySampler",
            side_effect=resident.ResidentError("resident_backend_unavailable"),
        ):
            code, report, _ = self.run_inprocess("normal", "--sample-resident-memory")
        self.assertEqual(code, 1, report)
        self.assertEqual(report["reason"], "resident_backend_unavailable")
        self.assertIn("residentMemoryFailure", report)
        failure = report["residentMemoryFailure"]
        self.assertEqual(failure["status"], "failed")
        self.assertEqual(failure["stage"], "initialization")
        self.assertEqual(failure["samples"], 0)
        self.assertEqual(failure["measurements"], {})
        self.assertGreater(failure["hostPid"], 0)

    def test_native_validation_failure_keeps_completed_resident_observation(self):
        with patch(
            "workbench_gui.ResidentMemorySampler", side_effect=self.controlled_sampler
        ):
            code, report, _ = self.run_inprocess(
                "wrong_source", "--sample-resident-memory"
            )
        self.assertEqual(code, 1, report)
        self.assertEqual(report["reason"], "native_source_mismatch")
        self.assertIn("residentMemoryObservations", report)
        observation = report["residentMemoryObservations"][0]
        self.assertEqual(observation["repetition"], 0)
        self.assertEqual(
            observation["measurements"]["processTreeSampledRssPeakBytes"], 100
        )

    def test_commit_query_failure_keeps_completed_resident_observation(self):
        with patch(
            "workbench_gui.ResidentMemorySampler", side_effect=self.controlled_sampler
        ):
            with patch(
                "workbench_gui.ProcessOwner.peak_tree_commit_bytes",
                side_effect=OSError("controlled query failure"),
            ):
                code, report, _ = self.run_inprocess(
                    "normal", "--sample-resident-memory"
                )
        self.assertEqual(code, 1, report)
        self.assertEqual(report["reason"], "OSError")
        self.assertIn("residentMemoryObservations", report)
        self.assertEqual(
            report["residentMemoryObservations"][0]["measurements"][
                "processTreeSampledRssPeakBytes"
            ],
            100,
        )

    @unittest.skipUnless(os.name == "nt", "Windows owned working sets")
    def test_opt_in_reports_sampled_boundaries_and_keeps_exact_peak_unmeasured(self):
        result, report, _ = self.fixture.run_fixture(
            "normal", "--sample-resident-memory"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        run = report["runs"][0]
        resident = run["residentMemory"]
        self.assertEqual(resident["status"], "sampled")
        self.assertEqual(resident["backend"], "windows_job_members_working_set")
        self.assertGreater(resident["samples"], 0)
        self.assertFalse(resident["includesExitedDescendants"])
        self.assertFalse(resident["snapshotIsAtomic"])
        self.assertGreater(run["measurements"]["hostSampledRssPeakBytes"], 0)
        self.assertTrue(run["gates"]["processTreeSampledRssPeakBytes"])
        self.assertIn("workerPeakResidentMemory", report["unmeasured"])
        self.assertEqual(report["releaseGateStatus"], "pending")

    @unittest.skipUnless(os.name == "nt", "Windows owned working sets")
    def test_one_byte_resident_budget_fails_with_retained_measurements(self):
        budgets = self.fixture.root / "resident-budget.json"
        budgets.write_text(
            json.dumps({"processTreeSampledRssPeakBytes": 1}), encoding="utf-8"
        )
        result, report, _ = self.fixture.run_fixture(
            "normal", "--sample-resident-memory", "--budgets", str(budgets)
        )
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(report["reason"], "budget_exceeded")
        run = report["runs"][0]
        self.assertFalse(run["gates"]["processTreeSampledRssPeakBytes"])
        self.assertGreater(run["measurements"]["processTreeSampledRssPeakBytes"], 1)

    def test_explicit_resident_budget_requires_the_opt_in(self):
        budgets = self.fixture.root / "resident-budget.json"
        budgets.write_text(
            json.dumps({"processTreeSampledRssPeakBytes": 1}), encoding="utf-8"
        )
        result, report, _ = self.fixture.run_fixture(
            "normal", "--budgets", str(budgets)
        )
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(report["reason"], "invalid_budgets")

    def test_sampling_is_rejected_for_an_unrelated_scenario(self):
        result, report, _ = self.fixture.run_fixture(
            "normal", "--scenario", "analysis", "--sample-resident-memory"
        )
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(report["reason"], "resident_sampling_requires_ui_service")

    @unittest.skipUnless(os.name == "nt", "Windows owned working sets")
    def test_invalid_resident_budgets_are_invalid_setup(self):
        root = self.fixture.root
        for index, value in enumerate(
            (True, 0, -1, 1.5, 16 * 1024**3 + 1, float("nan"))
        ):
            with self.subTest(value=value):
                self.fixture.root = root / str(index)
                self.fixture.root.mkdir()
                budgets = self.fixture.root / "resident-budget.json"
                budgets.write_text(
                    json.dumps({"processTreeSampledRssPeakBytes": value}),
                    encoding="utf-8",
                )
                result, report, _ = self.fixture.run_fixture(
                    "normal", "--sample-resident-memory", "--budgets", str(budgets)
                )
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertEqual(report["reason"], "invalid_budgets")

    @unittest.skipUnless(os.name == "nt", "Windows owned working sets")
    def test_sampling_does_not_extend_the_native_timeout(self):
        budgets = self.fixture.root / "timeout.json"
        budgets.write_text(json.dumps({"nativeSuiteSeconds": 0.1}), encoding="utf-8")
        result, report, _ = self.fixture.run_fixture(
            "stall", "--sample-resident-memory", "--budgets", str(budgets)
        )
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(report["reason"], "native_timeout")
        self.assertEqual(report["residentMemoryFailure"]["status"], "incomplete")
        self.assertGreater(report["residentMemoryFailure"]["samples"], 0)

    @unittest.skipIf(os.name == "nt", "Portable unsupported-platform contract")
    def test_unavailable_backend_does_not_claim_a_resident_gate(self):
        result, report, _ = self.fixture.run_fixture(
            "normal", "--sample-resident-memory"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        run = report["runs"][0]
        self.assertEqual(run["residentMemory"]["status"], "unsupported")
        self.assertNotIn("processTreeSampledRssPeakBytes", run["gates"])
        self.assertIn("processTreeSampledRssPeakBytes", report["unmeasured"])


if __name__ == "__main__":
    unittest.main()
