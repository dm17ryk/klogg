"""Black-box opt-in RSS reporting and gate contracts; no product API changes."""

import json
import os
import unittest

import test_workbench_gui as fixtures


class ResidentCliTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.NativeBenchmarkTests(
            "test_native_measurements_have_exact_boundaries_and_test_double_identity"
        )
        self.addCleanup(self.fixture.doCleanups)
        self.fixture.setUp()

    def test_default_does_not_sample_or_install_a_resident_budget(self):
        result, report, _ = self.fixture.run_fixture()
        self.assertEqual(result.returncode, 0, result.stderr)
        run = report["runs"][0]
        self.assertIn("residentMemory", run)
        self.assertEqual(run["residentMemory"]["status"], "disabled")
        self.assertNotIn("processTreeSampledRssPeakBytes", report["budgets"])
        self.assertNotIn("processTreeSampledRssPeakBytes", run["gates"])

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
