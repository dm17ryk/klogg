"""CILogg process-plugin SDK, protocol 1. Python standard library only."""
from __future__ import annotations

import json
import pathlib
import sys
import threading
import time
import uuid
from typing import Any, Callable, Iterable

JsonObject = dict[str, Any]
Action = Callable[["Job", JsonObject], JsonObject | None]

MAX_MESSAGE = 1024 * 1024
MAX_PENDING = 8 * MAX_MESSAGE


class Cancelled(Exception):
    """The host cancelled this job or stopped its stream."""


class Job:
    def __init__(self, plugin: Plugin, identifier: str, context: JsonObject) -> None:
        self.plugin = plugin
        self.id = identifier
        self.context = context
        self.cancelled = threading.Event()

    def check_cancelled(self) -> None:
        if self.cancelled.is_set() or self.plugin.stopping.is_set():
            raise Cancelled("Job cancelled")

    def progress(self, value: float) -> None:
        self.check_cancelled()
        self.plugin.notify("progress", {"job_id": self.id, "value": value})

    def panel(self, identifier: str, title: str, elements: list[JsonObject]) -> None:
        self.check_cancelled()
        self.plugin.notify("panel/update", {
            "job_id": self.id,
            "panel": {"id": identifier, "title": title, "elements": elements},
        })

    def stream(self) -> Stream:
        self.check_cancelled()
        return Stream(self)


class Stream:
    def __init__(self, job: Job) -> None:
        self.plugin = job.plugin
        self.job = job
        self.id = str(uuid.uuid4())
        self.sequence = 0
        self.closed = threading.Event()
        self.opened = threading.Event()
        self.ack = threading.Event()
        with self.plugin.lock:
            if len(self.plugin.streams) >= 128:
                raise RuntimeError("Too many streams")
            self.plugin.streams[self.id] = self
        try:
            self.plugin.notify("stream/open", {"job_id": job.id, "stream_id": self.id})
            self._wait(self.opened)
        except BaseException:
            self.close()
            raise

    def _wait(self, event: threading.Event) -> None:
        self.job.check_cancelled()
        while not event.wait(0.1):
            self.job.check_cancelled()
            if self.closed.is_set():
                raise Cancelled("Stream stopped")
        self.job.check_cancelled()
        if self.closed.is_set():
            raise Cancelled("Stream stopped")

    def write(self, records: Iterable[str]) -> None:
        self.job.check_cancelled()
        if self.closed.is_set():
            raise Cancelled("Stream stopped")
        params = {"stream_id": self.id, "sequence": self.sequence, "records": list(records)}
        size = len(self.plugin.encode({"jsonrpc": "2.0", "method": "stream/data", "params": params}))
        with self.plugin.credit:
            while self.plugin.pending_bytes + size > MAX_PENDING:
                self.job.check_cancelled()
                self.plugin.credit.wait(0.1)
            self.plugin.pending_bytes += size
        try:
            self.ack.clear()
            self.plugin.notify("stream/data", params)
            self._wait(self.ack)
            self.sequence += 1
        finally:
            with self.plugin.credit:
                self.plugin.pending_bytes -= size
                self.plugin.credit.notify_all()

    def close(self) -> None:
        try:
            if not self.closed.is_set():
                self.closed.set()
                self.plugin.notify("stream/close", {"stream_id": self.id})
        finally:
            with self.plugin.lock:
                self.plugin.streams.pop(self.id, None)

    def __enter__(self) -> Stream:
        return self

    def __exit__(self, *_args: Any) -> None:
        self.close()


class Plugin:
    def __init__(self, manifest_path: str = "plugin.json") -> None:
        self.manifest = json.loads(pathlib.Path(manifest_path).read_text(encoding="utf-8"))
        self.actions: dict[str, Action] = {}
        self.jobs: dict[str, Job] = {}
        self.streams: dict[str, Stream] = {}
        self.workers: dict[str, threading.Thread] = {}
        self.configuration: JsonObject = {}
        self.config_directory = None
        self.lock = threading.RLock()
        self.output_lock = threading.Lock()
        self.credit = threading.Condition()
        self.pending_bytes = 0
        self.stopping = threading.Event()
        self.initialized = False

    def action(self, identifier: str) -> Callable[[Action], Action]:
        def register(callback: Action) -> Action:
            self.actions[identifier] = callback
            return callback
        return register

    @staticmethod
    def encode(message: JsonObject) -> bytes:
        encoded = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        if len(encoded) > MAX_MESSAGE:
            raise ValueError("Plugin message exceeds 1 MiB; split it into smaller batches")
        return encoded + b"\n"

    def send(self, message: JsonObject) -> None:
        encoded = self.encode(message)
        with self.output_lock:
            sys.stdout.buffer.write(encoded)
            sys.stdout.buffer.flush()

    def notify(self, method: str, params: JsonObject) -> None:
        self.send({"jsonrpc": "2.0", "method": method, "params": params})

    def _invoke(self, request_id: str, params: JsonObject) -> None:
        identifier = params["job_id"]
        job = Job(self, identifier, params.get("context", {}))
        with self.lock:
            if len(self.jobs) >= 128:
                self.send({"jsonrpc": "2.0", "id": request_id,
                           "error": {"code": -32000, "message": "Too many jobs"}})
                return
            self.jobs[identifier] = job

        def run() -> None:
            try:
                job.check_cancelled()
                result = self.actions[params["action"]](job, params.get("arguments", {}))
                job.check_cancelled()
                self.send({"jsonrpc": "2.0", "id": request_id, "result": result or {}})
            except Exception as error:
                print(f"Plugin job {identifier}: {error}", file=sys.stderr, flush=True)
                if not self.stopping.is_set():
                    self.send({"jsonrpc": "2.0", "id": request_id,
                               "error": {"code": -32000, "message": str(error)}})
            finally:
                with self.lock:
                    self.jobs.pop(identifier, None)
                    self.workers.pop(identifier, None)

        worker = threading.Thread(target=run, name=f"plugin-job-{identifier}", daemon=True)
        with self.lock:
            self.workers[identifier] = worker
        worker.start()

    def _dispatch(self, message: JsonObject) -> None:
        method = message.get("method")
        params = message.get("params", {})
        if method == "initialize":
            if self.initialized or params.get("protocol_version") != 1 or params.get("plugin_id") != self.manifest["id"]:
                raise ValueError("Incompatible initialization")
            self.configuration = params.get("configuration", {})
            self.config_directory = params.get("config_directory")
            self.initialized = True
            self.send({"jsonrpc": "2.0", "id": message["id"], "result": {
                "plugin_id": self.manifest["id"], "protocol_version": 1,
                "capabilities": self.manifest["capabilities"],
                "actions": self.manifest.get("actions", []),
                "configuration": self.manifest.get("configuration", []),
            }})
        elif method == "shutdown":
            self.stopping.set()
        elif not self.initialized:
            raise ValueError("Plugin has not initialized")
        elif method == "invoke":
            self._invoke(message["id"], params)
        elif method == "configure":
            self.configuration = params["configuration"]
        elif method == "cancel":
            with self.lock:
                job = self.jobs.get(params["job_id"])
                if job:
                    job.cancelled.set()
        elif method.startswith("stream/"):
            with self.lock:
                stream = self.streams.get(params["stream_id"])
                if stream:
                    if method == "stream/opened":
                        stream.opened.set()
                    elif method == "stream/ack" and params.get("sequence") == stream.sequence:
                        stream.ack.set()
                    elif method == "stream/stop":
                        stream.closed.set()
        else:
            raise ValueError(f"Unknown host method: {method}")

    def serve(self) -> None:
        try:
            while not self.stopping.is_set():
                line = sys.stdin.buffer.readline(MAX_MESSAGE + 2)
                if not line:
                    break
                if len(line.rstrip(b"\r\n")) > MAX_MESSAGE or not line.endswith(b"\n"):
                    raise ValueError("Oversized or truncated host message")
                message = json.loads(line)
                if not isinstance(message, dict) or message.get("jsonrpc") != "2.0":
                    raise ValueError("Malformed host JSON-RPC")
                self._dispatch(message)
        except Exception as error:
            print(f"Plugin protocol failed: {error}", file=sys.stderr, flush=True)
        finally:
            self.stopping.set()
            with self.lock:
                for job in self.jobs.values():
                    job.cancelled.set()
                for stream in self.streams.values():
                    stream.closed.set()
                workers = list(self.workers.values())
            with self.credit:
                self.credit.notify_all()
            deadline = time.monotonic() + 2.8
            for worker in workers:
                worker.join(max(0.0, deadline - time.monotonic()))
