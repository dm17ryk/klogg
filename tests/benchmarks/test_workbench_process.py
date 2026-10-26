"""Native ownership accounting includes descendants that have already exited."""

import os
import subprocess
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts/benchmarks"))
from workbench_process import ProcessOwner


class ProcessMemoryTests(unittest.TestCase):
    @unittest.skipUnless(os.name == "nt", "Windows job accounting")
    def test_peak_retains_short_lived_descendant_commit(self):
        owner = ProcessOwner()
        self.addCleanup(owner.close)
        # The parent waits until assigned to the job before creating a child.
        child = "allocation = bytearray(64 * 1024 * 1024)"
        parent = (
            "import subprocess, sys; sys.stdin.readline(); "
            f"subprocess.run([sys.executable, '-c', {child!r}], check=True)"
        )
        process = owner.spawn(
            [sys.executable, "-c", parent], stdin=subprocess.PIPE
        )
        process.communicate(b"start\n", timeout=20)
        self.assertEqual(process.returncode, 0)
        self.assertGreaterEqual(owner.peak_tree_commit_bytes(), 64 * 1024 * 1024)

    @unittest.skipUnless(os.name == "nt", "Windows job accounting")
    def test_query_failure_cannot_become_a_zero_memory_pass(self):
        owner = ProcessOwner()
        self.addCleanup(owner.close)
        with patch.object(owner.kernel, "QueryInformationJobObject", return_value=0):
            with self.assertRaises(OSError):
                owner.peak_tree_commit_bytes()

    @unittest.skipUnless(os.name == "nt", "Windows job accounting")
    def test_closed_owner_cannot_query_an_unrelated_current_job(self):
        owner = ProcessOwner()
        owner.close()
        with self.assertRaisesRegex(RuntimeError, "owned_job_closed"):
            owner.peak_tree_commit_bytes()

    @unittest.skipUnless(os.name == "nt", "Windows job accounting")
    def test_missing_counter_cannot_become_a_zero_memory_pass(self):
        owner = ProcessOwner()
        self.addCleanup(owner.close)
        with patch.object(owner.kernel, "QueryInformationJobObject", return_value=1):
            with self.assertRaisesRegex(RuntimeError, "owned_job_memory_unavailable"):
                owner.peak_tree_commit_bytes()

    @unittest.skipIf(os.name == "nt", "Portable unsupported-platform contract")
    def test_other_platforms_report_no_commit_measurement(self):
        owner = ProcessOwner()
        self.addCleanup(owner.close)
        self.assertIsNone(owner.peak_tree_commit_bytes())


if __name__ == "__main__":
    unittest.main()
