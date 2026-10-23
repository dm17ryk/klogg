"""Own benchmark process trees; never enumerate or stop a user's CILogg instance."""

import ctypes
import logging
import os
import signal
import subprocess
from pathlib import Path

LOG = logging.getLogger("workbench.process")


class ProcessOwner:
    def __init__(self):
        self.processes = []
        self.job = None
        if os.name == "nt":
            from ctypes import wintypes

            class Basic(ctypes.Structure):
                _fields_ = [
                    ("processTime", ctypes.c_longlong),
                    ("jobTime", ctypes.c_longlong),
                    ("flags", wintypes.DWORD),
                    ("minimum", ctypes.c_size_t),
                    ("maximum", ctypes.c_size_t),
                    ("active", wintypes.DWORD),
                    ("affinity", ctypes.c_size_t),
                    ("priority", wintypes.DWORD),
                    ("scheduling", wintypes.DWORD),
                ]

            class Extended(ctypes.Structure):
                _fields_ = [
                    ("basic", Basic),
                    ("io", ctypes.c_ulonglong * 6),
                    ("processMemory", ctypes.c_size_t),
                    ("jobMemory", ctypes.c_size_t),
                    ("peakProcessMemory", ctypes.c_size_t),
                    ("peakJobMemory", ctypes.c_size_t),
                ]

            self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            self.kernel.CreateJobObjectW.argtypes = (ctypes.c_void_p, wintypes.LPCWSTR)
            self.kernel.CreateJobObjectW.restype = wintypes.HANDLE
            self.kernel.SetInformationJobObject.argtypes = (
                wintypes.HANDLE,
                ctypes.c_int,
                ctypes.c_void_p,
                wintypes.DWORD,
            )
            self.kernel.AssignProcessToJobObject.argtypes = (
                wintypes.HANDLE,
                wintypes.HANDLE,
            )
            self.kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
            self.job = self.kernel.CreateJobObjectW(None, None)
            if not self.job:
                raise ctypes.WinError(ctypes.get_last_error())
            limits = Extended()
            limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
            if not self.kernel.SetInformationJobObject(
                self.job, 9, ctypes.byref(limits), ctypes.sizeof(limits)
            ):
                error = ctypes.WinError(ctypes.get_last_error())
                self.kernel.CloseHandle(self.job)
                self.job = None
                raise error

    def spawn(self, arguments, **kwargs):
        process = subprocess.Popen(
            arguments, start_new_session=(os.name != "nt"), **kwargs
        )
        self.processes.append(process)
        if self.job and not self.kernel.AssignProcessToJobObject(
            self.job, int(process._handle)
        ):
            # This occurs before any analysis commands can spawn workers.
            error = ctypes.WinError(ctypes.get_last_error())
            process.kill()
            process.wait(timeout=5)
            raise error
        LOG.debug("owned_process_started pid=%d", process.pid)
        return process

    def close(self):
        if self.job:
            self.kernel.CloseHandle(self.job)
            self.job = None
        else:
            for process in self.processes:
                try:
                    if os.name == "nt":
                        if process.poll() is None:
                            process.kill()
                    else:
                        os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
        failures = []
        for process in self.processes:
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                failures.append(process.pid)
            for stream in (process.stdin, process.stdout):
                if stream is not None:
                    try:
                        stream.close()
                    except OSError as error:
                        # Killing a nonreading peer can leave buffered stdin bytes.
                        # The process is already reaped; preserve the original timeout.
                        LOG.debug(
                            "owned_pipe_closed_after_exit pid=%d errno=%s",
                            process.pid,
                            error.errno,
                        )
        LOG.info(
            "owned_processes_reaped count=%d failures=%d",
            len(self.processes),
            len(failures),
        )
        if failures:
            raise RuntimeError("owned_process_reap_failed")


def peak_host_working_set(process):
    if os.name != "nt":
        status = Path(f"/proc/{process.pid}/status")
        if status.is_file():
            for line in status.read_text(encoding="ascii").splitlines():
                if line.startswith("VmHWM:"):
                    return int(line.split()[1]) * 1024
        return None
    from ctypes import wintypes

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

    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    psapi.GetProcessMemoryInfo.argtypes = (
        wintypes.HANDLE,
        ctypes.POINTER(Counters),
        wintypes.DWORD,
    )
    counters = Counters()
    counters.size = ctypes.sizeof(counters)
    if not psapi.GetProcessMemoryInfo(
        int(process._handle), ctypes.byref(counters), counters.size
    ):
        raise ctypes.WinError(ctypes.get_last_error())
    return counters.peak
