"""Readiness retries stay inside the existing host-startup deadline."""

import subprocess
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

import analysis_parity


class StartupTests(unittest.TestCase):
    def check(self, outcomes, budget=40, exited=False):
        self.assertTrue(callable(getattr(analysis_parity, "wait_for_commander", None)))
        now = [0.0]
        budgets = []
        self.observed_time = now
        self.observed_budgets = budgets
        host = Mock()
        host.poll.return_value = 1 if exited else None
        pending = iter(outcomes)

        def probe(arguments, **kwargs):
            self.assertIn("--require-running", arguments)
            budgets.append(kwargs["timeout"])
            result = next(pending)
            if result == "timeout":
                now[0] += kwargs["timeout"]
                raise subprocess.TimeoutExpired(arguments, kwargs["timeout"])
            return subprocess.CompletedProcess(arguments, result, b"{}", b"not ready")

        with (
            patch.object(analysis_parity.subprocess, "run", side_effect=probe),
            patch.object(analysis_parity.time, "monotonic", side_effect=lambda: now[0]),
            patch.object(
                analysis_parity.time,
                "sleep",
                side_effect=lambda delay: now.__setitem__(0, now[0] + delay),
            ),
        ):
            analysis_parity.wait_for_commander(Path("fixture.exe"), {}, host, budget)
        return budgets, now[0]

    def test_timed_out_probe_can_recover_without_restarting_the_host(self):
        budgets, elapsed = self.check(["timeout", 0])
        self.assertEqual(budgets, [10, 10])
        self.assertEqual(elapsed, 10)

    def test_probe_retries_cannot_extend_the_existing_startup_deadline(self):
        with self.assertRaisesRegex(AssertionError, "startup deadline"):
            self.check(["timeout", "timeout", "timeout"], budget=25)
        self.assertEqual(self.observed_budgets, [10, 10, 5])
        self.assertEqual(self.observed_time, [25])

    def test_exited_host_fails_before_starting_another_probe(self):
        with self.assertRaisesRegex(AssertionError, "host exited"):
            self.check([], exited=True)


if __name__ == "__main__":
    unittest.main()
