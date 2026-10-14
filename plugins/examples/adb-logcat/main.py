"""Android logcat source. Only JSON-RPC is written to stdout."""
from __future__ import annotations

import queue
import re
import codecs
import json
import os
import pathlib
import shutil
import subprocess
import threading
import time
from collections import deque

from cilogg_plugin import Cancelled, Job, JsonObject, Plugin

plugin = Plugin()
FORMATS = {"threadtime", "time", "brief", "long", "process", "raw", "tag", "thread"}
BUFFERS = {"default", "main", "system", "radio", "events", "crash", "all"}


def debug(message: str) -> None:
    plugin.notify("log", {"message": "ADB: " + message})


def executable() -> str:
    configured = str(plugin.configuration.get("adb_path", "")).strip()
    if configured:
        debug("Using configured ADB executable")
        return configured
    filename = "adb.exe" if os.name == "nt" else "adb"
    roots = [os.environ.get("ANDROID_HOME"), os.environ.get("ANDROID_SDK_ROOT")]
    home = pathlib.Path.home()
    roots.extend([str(home / "AppData/Local/Android/Sdk"),
                  str(home / "Library/Android/sdk"), str(home / "Android/Sdk")])
    for index, root in enumerate(roots):
        if root:
            candidate = pathlib.Path(root) / "platform-tools" / filename
            if candidate.is_file():
                debug(f"Found ADB in SDK location index={index}")
                return str(candidate)
            debug(f"ADB missing from SDK location index={index}")
    candidate = shutil.which("adb")
    if candidate:
        debug("Found ADB on PATH")
        return candidate
    debug("Automatic ADB discovery failed")
    raise RuntimeError("ADB not found; install Android platform-tools or configure adb_path")


def command(arguments: JsonObject) -> list[str]:
    result = [executable()]
    serial = str(arguments.get("serial", "") or plugin.configuration.get("serial", "")).strip()
    if serial:
        result.extend(["-s", serial])
        debug("Using an explicitly selected device")
    else:
        debug("Using ADB's default device selection")
    return result


def output(job: Job, argv: list[str], stream=None, timeout: float = 0):
    """Drain both pipes with bounded storage; keep cancellation responsive while idle."""
    debug("Starting ADB process without a shell")
    if os.name == "nt" and pathlib.Path(argv[0]).suffix.lower() in {".bat", ".cmd"}:
        raise ValueError("Configure a native ADB executable, not a Windows batch file")
    try:
        # Audited: the executable is user-configured or discovered locally; device data stays
        # in separate arguments. No shell is used, and Windows batch wrappers are rejected.
        # nosemgrep: python.lang.security.audit.dangerous-subprocess-use-audit
        process = subprocess.Popen(
            argv, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            shell=False,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
    except OSError as error:
        raise RuntimeError(f"Cannot start ADB; configure adb_path: {error}") from error
    lines: queue.Queue = queue.Queue(maxsize=64)
    stopped = threading.Event()
    errors = deque(maxlen=8)

    def read_stdout() -> None:
        try:
            while not stopped.is_set():
                # Also splits pathological newline-free output to stay below host frame limits.
                line = process.stdout.readline(32768)
                if not line:
                    break
                while not stopped.is_set():
                    try:
                        lines.put(line, timeout=0.1)
                        break
                    except queue.Full:
                        continue
        finally:
            while not stopped.is_set():
                try:
                    lines.put(None, timeout=0.1)
                    break
                except queue.Full:
                    continue

    def read_stderr() -> None:
        while not stopped.is_set():
            line = process.stderr.readline(512)
            if not line:
                break
            errors.append(line.decode("utf-8", errors="replace").strip())

    readers = [threading.Thread(target=read_stdout, daemon=True),
               threading.Thread(target=read_stderr, daemon=True)]
    for reader in readers:
        reader.start()
    deadline = time.monotonic() + timeout if timeout else None
    decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
    try:
        while True:
            job.check_cancelled()
            if stream is not None and stream.closed.is_set():
                raise Cancelled("ADB stream stopped")
            if deadline is not None and time.monotonic() >= deadline:
                debug("ADB command timed out")
                raise TimeoutError("ADB command timed out")
            try:
                line = lines.get(timeout=0.1)
            except queue.Empty:
                if stream is not None:
                    yield None  # Let a live capture flush its partial batch while idle.
                continue
            if line is None:
                tail = decoder.decode(b"", final=True)
                if tail:
                    yield tail
                break
            record = decoder.decode(line).rstrip("\r\n")
            if record or line.endswith(b"\n"):
                yield record
        while process.poll() is None:
            job.check_cancelled()
            if stream is not None and stream.closed.is_set():
                raise Cancelled("ADB stream stopped")
            if deadline is not None and time.monotonic() >= deadline:
                raise TimeoutError("ADB command timed out")
            stopped.wait(0.05)
        readers[1].join(timeout=0.2)
        if process.returncode:
            debug(f"ADB exited with status {process.returncode}")
            raise RuntimeError(f"ADB exited with status {process.returncode}: {'; '.join(errors)}")
        debug("ADB command completed successfully")
    finally:
        stopped.set()
        if process.poll() is None:
            debug("Terminating the job's ADB process")
            process.terminate()
            try:
                process.wait(timeout=0.5)
            except subprocess.TimeoutExpired:
                debug("ADB did not terminate; killing the job's process")
                process.kill()
                process.wait(timeout=0.5)
        for reader in readers:
            reader.join(timeout=0.2)
        process.stdout.close()
        process.stderr.close()


@plugin.action("devices")
def devices(job: Job, arguments: JsonObject) -> JsonObject:
    rows = []
    for line in output(job, [executable(), "devices", "-l"], timeout=15):
        parts = line.split(maxsplit=2)
        if len(parts) >= 2 and not line.startswith("List of devices"):
            rows.append([parts[0], parts[1], parts[2] if len(parts) > 2 else ""])
    debug(f"Device discovery completed count={len(rows)}")
    elements = [{"type": "table", "columns": ["Serial", "State", "Description"],
                 "rows": rows}]
    ready = [row for row in rows if row[1] == "device"]
    # Reserve table/refresh slots and, for large lists, a notice within the host's 64-element limit.
    button_limit = 62 if len(ready) <= 62 else 61
    for serial, _state, _description in ready[:button_limit]:
        elements.append({"type": "button", "label": f"Capture {serial}",
                         "action": "start", "arguments": {"serial": serial}})
    if len(ready) > button_limit:
        debug(f"Device panel buttons capped ready={len(ready)} buttons={button_limit}")
        elements.append({"type": "text", "text":
                         "All devices are listed above. Use Start ADB Logcat with a serial for additional devices."})
    if not rows:
        elements.append({"type": "text", "text": "No ADB devices found. Connect a device and enable USB debugging."})
    elements.append({"type": "button", "label": "Refresh devices", "action": "devices"})
    job.panel("adb-devices", "ADB Devices", elements)
    return {"devices": rows}


@plugin.action("start")
def start(job: Job, arguments: JsonObject) -> JsonObject:
    format_name = arguments.get("format", "threadtime")
    buffer = arguments.get("buffer", "default")
    if format_name not in FORMATS or buffer not in BUFFERS:
        raise ValueError("Unsupported logcat format or buffer")
    filters = str(arguments.get("filters", "")).split()
    # Android liblog accepts '*' as ANDROID_LOG_DEFAULT; 'A' is not a logcat priority.
    if any(not re.fullmatch(r"[^\s:]+:[VDIWEFS*]", item) or item.startswith("-")
           for item in filters):
        raise ValueError("Filters must be space-separated tag:priority values, e.g. MyTag:D *:S")
    argv = command(arguments) + ["logcat", "-v", format_name]
    if buffer != "default":
        argv.extend(["-b", buffer])
    argv.extend(filters)
    debug(f"Logcat configured format={format_name} buffer={buffer} filters={len(filters)}")
    count = 0
    with job.stream() as stream:
        batch = []
        batch_bytes = 0

        def flush() -> None:
            nonlocal batch, batch_bytes, count
            if not batch:
                return
            records = batch
            batch = []  # Do not retry a batch that was sent but whose acknowledgement failed.
            batch_bytes = 0
            stream.write(records, finish_on_cancel=True)
            count += len(records)
            debug(f"Logcat capture checkpoint records={count} batch={len(records)}")

        reader = output(job, argv, stream)
        try:
            for record in reader:
                if record is None:
                    flush()
                    continue
                size = len(json.dumps(record, ensure_ascii=False).encode("utf-8")) + 1
                if batch and batch_bytes + size > 65536:
                    flush()
                batch.append(record)
                batch_bytes += size
                if len(batch) >= 64 or batch_bytes >= 65536:
                    flush()
        finally:
            try:
                flush()
            finally:
                reader.close()
    return {"records": count}


if __name__ == "__main__":
    plugin.serve()
