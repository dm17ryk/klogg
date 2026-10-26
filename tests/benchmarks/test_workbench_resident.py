"""Resident-memory contracts: owned members, sampled aggregates, bounded failures."""

import ctypes
import importlib
import importlib.util
import os
import queue
import subprocess
import sys
import threading
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts/benchmarks"))
from workbench_process import ProcessOwner


class ResidentSamplerTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(
            importlib.util.find_spec("workbench_resident"),
            "The bounded resident-memory backend is not implemented",
        )
        self.resident = importlib.import_module("workbench_resident")

    def test_peak_is_maximum_poll_total_not_sum_of_individual_peaks(self):
        samples = self.resident.ResidentSamples(1)
        samples.record({"rssByPid": {1: 10, 2: 50}, "exitRaces": 0}, 1.0, 1.002)
        samples.record({"rssByPid": {1: 50, 2: 10}, "exitRaces": 1}, 1.02, 1.021)
        report = samples.result()
        self.assertEqual(report["measurements"]["processTreeSampledRssPeakBytes"], 60)
        self.assertEqual(report["measurements"]["hostSampledRssPeakBytes"], 50)
        self.assertEqual(report["measurements"]["workersSampledRssPeakBytes"], 50)
        self.assertEqual(report["samples"], 2)
        self.assertEqual(report["observedWorkerPids"], 1)
        self.assertEqual(report["exitRaces"], 1)
        self.assertAlmostEqual(report["maximumSampleGapMs"], 20.0)
        self.assertAlmostEqual(report["maximumSampleDurationMs"], 2.0)
        self.assertFalse(report["includesExitedDescendants"])
        self.assertFalse(report["snapshotIsAtomic"])
        self.assertTrue(report["countsSharedPagesPerProcess"])
        self.assertFalse(report["includesHarness"])

    def test_unobserved_workers_are_not_reported_as_zero_usage(self):
        samples = self.resident.ResidentSamples(1)
        samples.record({"rssByPid": {1: 10}, "exitRaces": 0}, 1.0, 1.001)
        samples.record({"rssByPid": {}, "exitRaces": 1}, 1.02, 1.021)
        report = samples.result()
        self.assertNotIn("workersSampledRssPeakBytes", report["measurements"])
        self.assertEqual(report["emptyPolls"], 1)
        self.assertFalse(report["workerObserved"])
        self.assertEqual(report["measurements"]["processTreeSampledRssPeakBytes"], 10)

    def test_missing_host_observation_cannot_become_a_passing_zero(self):
        samples = self.resident.ResidentSamples(1)
        samples.record({"rssByPid": {2: 10}, "exitRaces": 0}, 1.0, 1.001)
        with self.assertRaisesRegex(RuntimeError, "resident_host_not_observed"):
            samples.result()

    def test_invalid_counters_and_unbounded_process_history_fail(self):
        for snapshot in (
            {"rssByPid": {1: True}, "exitRaces": 0},
            {"rssByPid": {1: 0}, "exitRaces": 0},
            {"rssByPid": {True: 10}, "exitRaces": 0},
            {"rssByPid": {1: 10}, "exitRaces": -1},
        ):
            with self.subTest(snapshot=snapshot):
                with self.assertRaisesRegex(RuntimeError, "invalid_resident_snapshot"):
                    self.resident.ResidentSamples(1).record(snapshot, 1.0, 1.001)
        samples = self.resident.ResidentSamples(1)
        for pid in range(1, self.resident.MAX_PROCESSES + 1):
            samples.record({"rssByPid": {pid: 10}, "exitRaces": 0}, pid, pid + 0.001)
        with self.assertRaisesRegex(RuntimeError, "resident_process_limit"):
            samples.record({"rssByPid": {9999: 10}, "exitRaces": 0}, 9999, 9999.001)

    def hold_worker(self):
        owner = ProcessOwner()
        self.addCleanup(owner.close)
        child = (
            "import sys; allocation=bytearray(64*1024*1024); "
            "allocation[::4096]=b'x'*(len(allocation)//4096); "
            "print('ready',flush=True); sys.stdin.readline()"
        )
        parent = (
            "import subprocess,sys; sys.stdin.readline(); "
            f"child=subprocess.Popen([sys.executable,'-c',{child!r}],"
            "stdin=subprocess.PIPE,stdout=subprocess.PIPE); "
            "assert child.stdout.readline().strip()==b'ready'; "
            "print(child.pid,flush=True); sys.stdin.readline(); "
            "child.communicate(b'stop\\n',timeout=10)"
        )
        process = owner.spawn(
            [sys.executable, "-c", parent],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
        )
        replies = queue.Queue(maxsize=1)
        reader = threading.Thread(
            target=lambda: replies.put(process.stdout.readline()), daemon=True
        )
        reader.start()

        def cleanup():
            owner.close()
            reader.join(timeout=5)
            self.assertFalse(reader.is_alive(), "Owned readiness reader did not stop")

        self.addCleanup(cleanup)
        process.stdin.write(b"start\n")
        process.stdin.flush()
        child_pid = int(replies.get(timeout=20))
        reader.join(timeout=5)
        self.assertFalse(reader.is_alive())
        return owner, process, child_pid

    @unittest.skipUnless(os.name == "nt", "Windows owned working sets")
    def test_real_resident_snapshot_includes_held_worker_and_excludes_harness(self):
        owner, host, child_pid = self.hold_worker()
        snapshot = self.resident._WindowsWorkingSet(owner).snapshot()
        self.assertEqual(set(snapshot["rssByPid"]), {host.pid, child_pid})
        self.assertNotIn(os.getpid(), snapshot["rssByPid"])
        self.assertGreater(snapshot["rssByPid"][child_pid], 32 * 1024 * 1024)
        sampler = self.resident.ResidentMemorySampler(owner, host.pid)
        sampler.sample()
        self.assertTrue(sampler.result()["workerObserved"])
        host.stdin.write(b"stop\n")
        host.stdin.flush()
        self.assertEqual(host.wait(timeout=20), 0)

    @unittest.skipUnless(os.name == "nt", "Windows owned working sets")
    def test_closed_owner_cannot_query_the_callers_current_job(self):
        owner = ProcessOwner()
        backend = self.resident._WindowsWorkingSet(owner)
        owner.close()
        with patch.object(owner.kernel, "QueryInformationJobObject") as query:
            with self.assertRaisesRegex(RuntimeError, "owned_job_closed"):
                backend.snapshot()
            query.assert_not_called()

    @unittest.skipUnless(os.name == "nt", "Windows owned working sets")
    def test_query_error_and_oversized_member_list_fail_closed(self):
        owner = ProcessOwner()
        self.addCleanup(owner.close)
        backend = self.resident._WindowsWorkingSet(owner)
        with patch.object(owner.kernel, "QueryInformationJobObject", return_value=0):
            with self.assertRaisesRegex(RuntimeError, "resident_memory_query_failed"):
                backend.snapshot()

        def oversized(_job, _kind, buffer, _size, _returned):
            header = ctypes.cast(buffer, ctypes.POINTER(ctypes.c_uint32))
            header[0] = self.resident.MAX_PROCESSES + 1
            header[1] = self.resident.MAX_PROCESSES
            ctypes.set_last_error(234)
            return 0

        with patch.object(
            owner.kernel, "QueryInformationJobObject", side_effect=oversized
        ):
            with self.assertRaisesRegex(RuntimeError, "resident_process_limit"):
                backend.snapshot()

    @unittest.skipUnless(os.name == "nt", "Windows owned working sets")
    def test_zero_counter_and_member_mismatch_fail_and_close_query_handle(self):
        owner, _host, _child_pid = self.hold_worker()
        backend = self.resident._WindowsWorkingSet(owner)
        with patch.object(backend.psapi, "GetProcessMemoryInfo", return_value=1):
            with patch.object(
                owner.kernel, "CloseHandle", wraps=owner.kernel.CloseHandle
            ) as close:
                with self.assertRaisesRegex(
                    RuntimeError, "resident_counter_unavailable"
                ):
                    backend.snapshot()
                self.assertEqual(close.call_count, 1)
        with patch.object(owner.kernel, "IsProcessInJob", return_value=1):
            with self.assertRaisesRegex(RuntimeError, "resident_member_mismatch"):
                backend.snapshot()

    @unittest.skipUnless(os.name == "nt", "Windows owned working sets")
    def test_sampling_wait_retains_the_original_absolute_deadline(self):
        owner, host, _child_pid = self.hold_worker()
        sampler = self.resident.ResidentMemorySampler(owner, host.pid)
        sampler.sample()
        with self.assertRaises(subprocess.TimeoutExpired):
            sampler.wait(host, timeout=0.03)
        self.assertGreater(sampler.result()["samples"], 0)
        self.assertIsNone(host.poll())

    @unittest.skipUnless(os.name == "nt", "Windows owned working sets")
    def test_exit_races_are_counted_but_access_denial_fails_closed(self):
        owner, _host, _child_pid = self.hold_worker()
        backend = self.resident._WindowsWorkingSet(owner)

        def cannot_open(error):
            def fail(*_args):
                ctypes.set_last_error(error)
                return None

            return fail

        with patch.object(owner.kernel, "OpenProcess", side_effect=cannot_open(87)):
            snapshot = backend.snapshot()
            self.assertEqual(snapshot["rssByPid"], {})
            self.assertEqual(snapshot["exitRaces"], 2)
        with patch.object(owner.kernel, "OpenProcess", side_effect=cannot_open(5)):
            with self.assertRaisesRegex(RuntimeError, "resident_memory_query_failed"):
                backend.snapshot()

    @unittest.skipUnless(os.name == "nt", "Windows owned working sets")
    def test_query_diagnostics_preserve_error_before_checking_for_exit(self):
        owner, _host, _child_pid = self.hold_worker()
        backend = self.resident._WindowsWorkingSet(owner)
        original_exited = backend.exited

        def failed_counter(*_args):
            ctypes.set_last_error(5)
            return 0

        def wait_overwrites_error(handle):
            result = original_exited(handle)
            ctypes.set_last_error(87)
            return result

        with patch.object(
            backend.psapi, "GetProcessMemoryInfo", side_effect=failed_counter
        ):
            with patch.object(backend, "exited", side_effect=wait_overwrites_error):
                with self.assertLogs("workbench.resident", level="ERROR") as logs:
                    with self.assertRaisesRegex(
                        RuntimeError, "resident_memory_query_failed"
                    ):
                        backend.snapshot()
        self.assertTrue(
            any(
                "operation=working-set" in line and "winerror=5" in line
                for line in logs.output
            ),
            logs.output,
        )

    @unittest.skipIf(os.name == "nt", "Portable unsupported-platform contract")
    def test_unsupported_platform_has_no_measurement(self):
        owner = ProcessOwner()
        self.addCleanup(owner.close)
        report = self.resident.ResidentMemorySampler(owner, 1).result()
        self.assertEqual(report["status"], "unsupported")
        self.assertEqual(report["measurements"], {})

    @unittest.skipUnless(os.name == "nt", "Windows owned working sets")
    def test_backend_initialization_failure_is_not_unsupported(self):
        with patch.object(
            self.resident, "_WindowsWorkingSet", side_effect=OSError("unavailable")
        ):
            with self.assertRaises(Exception) as caught:
                self.resident.ResidentMemorySampler(object(), 1)
        self.assertIsInstance(caught.exception, self.resident.ResidentError)
        self.assertEqual(str(caught.exception), "resident_backend_unavailable")


if __name__ == "__main__":
    unittest.main()
