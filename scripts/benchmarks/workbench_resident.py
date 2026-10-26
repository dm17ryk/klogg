"""Opt-in, bounded working-set polls of an already-owned Windows process job."""

import ctypes
import logging
import math
import os
import subprocess
from time import perf_counter as clock

from workbench_runtime import RunError

LOG = logging.getLogger("workbench.resident")
MAX_PROCESSES = 256
INTERVAL_SECONDS = 0.01
RESIDENT_BUDGET = {"processTreeSampledRssPeakBytes": 512 * 1024 * 1024}


class ResidentError(RunError):
    pass


class ResidentSamples:
    """Keep bounded maxima of poll totals; never sum individual process peaks."""

    def __init__(self, host_pid):
        self.host_pid = host_pid
        self.pids = set()
        self.samples = self.exit_races = self.empty_polls = self.host_samples = 0
        self.tree_peak = self.host_peak = self.workers_peak = 0
        self.last_started = None
        self.maximum_gap = self.maximum_duration = 0.0

    def record(self, snapshot, started, finished):
        values = snapshot.get("rssByPid") if isinstance(snapshot, dict) else None
        races = snapshot.get("exitRaces") if isinstance(snapshot, dict) else None
        if (
            not isinstance(values, dict)
            or type(races) is not int
            or not 0 <= races <= MAX_PROCESSES
            or any(
                type(pid) is not int
                or not 0 < pid <= 0xFFFFFFFF
                or type(value) is not int
                or not 0 < value <= 0xFFFFFFFFFFFFFFFF
                for pid, value in values.items()
            )
            or any(
                isinstance(value, bool)
                or not isinstance(value, (int, float))
                or not math.isfinite(value)
                for value in (started, finished)
            )
            or not 0 <= started <= finished
            or (self.last_started is not None and started < self.last_started)
        ):
            LOG.error("resident_snapshot_rejected reason=invalid-counter-or-clock")
            raise ResidentError("invalid_resident_snapshot")
        if (
            len(values) > MAX_PROCESSES
            or len(self.pids | values.keys()) > MAX_PROCESSES
        ):
            LOG.error(
                "resident_snapshot_rejected reason=process-limit limit=%d",
                MAX_PROCESSES,
            )
            raise ResidentError("resident_process_limit")
        self.pids.update(values)
        self.samples += 1
        self.exit_races += races
        self.empty_polls += not values
        host = values.get(self.host_pid, 0)
        self.host_samples += host > 0
        self.tree_peak = max(self.tree_peak, sum(values.values()))
        self.host_peak = max(self.host_peak, host)
        self.workers_peak = max(self.workers_peak, sum(values.values()) - host)
        if self.last_started is not None:
            self.maximum_gap = max(self.maximum_gap, started - self.last_started)
        self.last_started = started
        self.maximum_duration = max(self.maximum_duration, finished - started)
        LOG.debug(
            "resident_poll samples=%d host_pid=%d members=%d exit_races=%d tree_bytes=%d",
            self.samples,
            self.host_pid,
            len(values),
            races,
            sum(values.values()),
        )

    def result(self, status="sampled"):
        if status == "sampled" and not self.host_samples:
            LOG.error(
                "resident_result_rejected host_pid=%d reason=host-not-observed",
                self.host_pid,
            )
            raise ResidentError("resident_host_not_observed")
        workers = self.pids - {self.host_pid}
        measurements = {}
        if self.host_samples:
            measurements.update(
                processTreeSampledRssPeakBytes=self.tree_peak,
                hostSampledRssPeakBytes=self.host_peak,
            )
        if workers:
            measurements["workersSampledRssPeakBytes"] = self.workers_peak
        return {
            "status": status,
            "kind": "sampled_working_set_bytes",
            "backend": "windows_job_members_working_set",
            "scope": "native_test_host_and_owned_descendants",
            "workerRole": "all_owned_non_host_members",
            "start": "first_poll_after_job_assignment_before_start_handshake",
            "end": "last_poll_before_native_test_host_exit",
            "hostPid": self.host_pid,
            "samples": self.samples,
            "hostSamples": self.host_samples,
            "emptyPolls": self.empty_polls,
            "exitRaces": self.exit_races,
            "observedWorkerPids": len(workers),
            "workerObserved": bool(workers),
            "intervalRequestedMs": INTERVAL_SECONDS * 1000,
            "maximumSampleGapMs": self.maximum_gap * 1000,
            "maximumSampleDurationMs": self.maximum_duration * 1000,
            "maximumTrackedPids": MAX_PROCESSES,
            "includesExitedDescendants": False,
            "snapshotIsAtomic": False,
            "countsSharedPagesPerProcess": True,
            "includesHarness": False,
            "measurements": measurements,
        }


class _WindowsWorkingSet:
    """Query job members only; validate membership again on each opened handle."""

    def __init__(self, owner):
        from ctypes import wintypes

        self.owner = owner
        self.kernel = owner.kernel

        class Members(ctypes.Structure):
            _fields_ = [
                ("assigned", wintypes.DWORD),
                ("count", wintypes.DWORD),
                ("pids", ctypes.c_size_t * MAX_PROCESSES),
            ]

        class Counters(ctypes.Structure):
            _fields_ = [("size", wintypes.DWORD), ("faults", wintypes.DWORD)] + [
                (name, ctypes.c_size_t)
                for name in (
                    "peak",
                    "working",
                    "peakPaged",
                    "paged",
                    "peakNonPaged",
                    "nonPaged",
                    "pagefile",
                    "peakPagefile",
                )
            ]

        self.members_type = Members
        self.counters_type = Counters
        self.kernel.OpenProcess.argtypes = (
            wintypes.DWORD,
            wintypes.BOOL,
            wintypes.DWORD,
        )
        self.kernel.OpenProcess.restype = wintypes.HANDLE
        self.kernel.IsProcessInJob.argtypes = (
            wintypes.HANDLE,
            wintypes.HANDLE,
            ctypes.POINTER(wintypes.BOOL),
        )
        self.kernel.IsProcessInJob.restype = wintypes.BOOL
        self.kernel.WaitForSingleObject.argtypes = (wintypes.HANDLE, wintypes.DWORD)
        self.kernel.WaitForSingleObject.restype = wintypes.DWORD
        self.psapi = ctypes.WinDLL("psapi", use_last_error=True)
        self.psapi.GetProcessMemoryInfo.argtypes = (
            wintypes.HANDLE,
            ctypes.POINTER(Counters),
            wintypes.DWORD,
        )
        self.psapi.GetProcessMemoryInfo.restype = wintypes.BOOL

    def query_failed(self, operation, pid=None, error=None):
        if error is None:
            error = ctypes.get_last_error()
        LOG.error(
            "resident_query_failed operation=%s pid=%s winerror=%d",
            operation,
            pid,
            error,
        )
        raise ResidentError("resident_memory_query_failed") from ctypes.WinError(error)

    def exited(self, handle):
        state = self.kernel.WaitForSingleObject(handle, 0)
        if state == 0:
            return True
        if state != 258:  # WAIT_TIMEOUT; a process handle cannot be abandoned.
            LOG.error("resident_wait_rejected state=%d", state)
            self.query_failed("wait")
        return False

    def snapshot(self):
        from ctypes import wintypes

        job = self.owner.job
        if not job:
            LOG.error("resident_query_rejected reason=owned-job-closed")
            raise ResidentError("owned_job_closed")
        for attempt in range(3):
            members = self.members_type()
            success = self.kernel.QueryInformationJobObject(
                job, 3, ctypes.byref(members), ctypes.sizeof(members), None
            )
            if members.assigned > MAX_PROCESSES:
                LOG.error(
                    "resident_members_rejected count=%d limit=%d",
                    members.assigned,
                    MAX_PROCESSES,
                )
                raise ResidentError("resident_process_limit")
            if success:
                break
            if ctypes.get_last_error() != 234:  # ERROR_MORE_DATA
                self.query_failed("members")
            LOG.debug(
                "resident_members_retry attempt=%d assigned=%d",
                attempt,
                members.assigned,
            )
        else:
            LOG.error("resident_members_rejected reason=unstable-list attempts=3")
            raise ResidentError("resident_member_list_unstable")
        pids = list(members.pids[: members.count])
        if (
            members.count != members.assigned
            or members.count > MAX_PROCESSES
            or len(set(pids)) != len(pids)
            or any(not 0 < pid <= 0xFFFFFFFF for pid in pids)
        ):
            LOG.error(
                "resident_members_rejected assigned=%d returned=%d",
                members.assigned,
                members.count,
            )
            raise ResidentError("invalid_resident_members")
        values = {}
        races = 0
        for pid in pids:
            # PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, never VM write access.
            handle = self.kernel.OpenProcess(0x1000 | 0x100000, False, pid)
            if not handle:
                if (
                    ctypes.get_last_error() == 87
                ):  # PID exited between enumeration/open.
                    LOG.debug("resident_member_exited pid=%d phase=open", pid)
                    races += 1
                    continue
                self.query_failed("open", pid)
            try:
                if self.exited(handle):
                    LOG.debug("resident_member_exited pid=%d phase=before-query", pid)
                    races += 1
                    continue
                member = wintypes.BOOL()
                success = self.kernel.IsProcessInJob(handle, job, ctypes.byref(member))
                error = ctypes.get_last_error() if not success else None
                if not success or not member.value:
                    if self.exited(handle):
                        LOG.debug("resident_member_exited pid=%d phase=membership", pid)
                        races += 1
                        continue
                    if not success:
                        self.query_failed("membership", pid, error)
                    LOG.error(
                        "resident_member_rejected pid=%d reason=membership-mismatch",
                        pid,
                    )
                    raise ResidentError("resident_member_mismatch")
                counters = self.counters_type()
                counters.size = ctypes.sizeof(counters)
                success = self.psapi.GetProcessMemoryInfo(
                    handle, ctypes.byref(counters), counters.size
                )
                error = ctypes.get_last_error() if not success else None
                if not success or not counters.working:
                    if self.exited(handle):
                        LOG.debug(
                            "resident_member_exited pid=%d phase=working-set", pid
                        )
                        races += 1
                        continue
                    if not success:
                        self.query_failed("working-set", pid, error)
                    LOG.error(
                        "resident_counter_rejected pid=%d reason=zero-working-set", pid
                    )
                    raise ResidentError("resident_counter_unavailable")
                values[pid] = counters.working
                LOG.debug(
                    "resident_member_measured pid=%d working_set_bytes=%d",
                    pid,
                    counters.working,
                )
            finally:
                if not self.kernel.CloseHandle(handle):
                    self.query_failed("close", pid)
        return {"rssByPid": values, "exitRaces": races}


class ResidentMemorySampler:
    def __init__(self, owner, host_pid):
        self.samples = ResidentSamples(host_pid)
        try:
            self.backend = _WindowsWorkingSet(owner) if os.name == "nt" else None
        except (OSError, AttributeError) as error:
            LOG.exception("resident_backend_failed host_pid=%d", host_pid)
            raise ResidentError("resident_backend_unavailable") from error
        LOG.debug(
            "resident_sampler_created host_pid=%d supported=%s",
            host_pid,
            self.backend is not None,
        )

    def sample(self):
        if self.backend is not None:
            started = clock()
            snapshot = self.backend.snapshot()
            self.samples.record(snapshot, started, clock())

    def wait(self, process, timeout):
        if self.backend is None:
            return process.wait(timeout=timeout)
        deadline = clock() + timeout
        while process.poll() is None:
            if clock() >= deadline:
                LOG.error(
                    "resident_wait_expired pid=%d samples=%d",
                    process.pid,
                    self.samples.samples,
                )
                raise subprocess.TimeoutExpired(process.args, timeout)
            self.sample()
            remaining = deadline - clock()
            if remaining <= 0:
                raise subprocess.TimeoutExpired(process.args, timeout)
            try:
                return process.wait(timeout=min(INTERVAL_SECONDS, remaining))
            except subprocess.TimeoutExpired:
                pass
        return process.returncode

    def result(self, status="sampled"):
        if self.backend is None:
            return {
                "status": "unsupported",
                "backend": "unavailable",
                "reason": "windows_job_members_required",
                "measurements": {},
            }
        return self.samples.result(status)
