"""CILogg process-plugin SDK, protocol 1. Python standard library only."""
from __future__ import annotations

import json
import pathlib
import queue
import sys
import threading
import time
import uuid
from typing import Any, Callable, Iterable, Optional, Sequence

JsonObject = dict[str, Any]
Action = Callable[["Job", JsonObject], Optional[JsonObject]]
ViewCallback = Callable[[JsonObject], None]

MAX_MESSAGE = 1024 * 1024
MAX_PENDING = 8 * MAX_MESSAGE
CANCEL_FLUSH_TIMEOUT = 1.0
VIEW_QUEUE_LIMIT = 1024
MESSAGE_BUTTONS = ("ok", "cancel", "yes", "no", "retry", "abort", "ignore", "close")


# Plugin view element builders (docs/plugin-views.md). Each returns a plain dict; arguments
# that are None are omitted. Element ids must be unique within one view.
def _element(element_type: str, element_id: Optional[str], /, **props: Any) -> JsonObject:
    element: JsonObject = {"type": element_type}
    if element_id is not None:
        element["id"] = element_id
    element.update({key: value for key, value in props.items() if value is not None})
    return element


def column(*children: JsonObject, id: Optional[str] = None, **props: Any) -> JsonObject:
    return _element("column", id, children=list(children), **props)


def row(*children: JsonObject, id: Optional[str] = None, **props: Any) -> JsonObject:
    return _element("row", id, children=list(children), **props)


def group(title: str, *children: JsonObject, id: Optional[str] = None,
          collapsible: Optional[bool] = None, collapsed: Optional[bool] = None,
          **props: Any) -> JsonObject:
    return _element("group", id, title=title, collapsible=collapsible, collapsed=collapsed,
                    children=list(children), **props)


def label(id: str, text: str, tone: Optional[str] = None, bold: Optional[bool] = None,
          **props: Any) -> JsonObject:
    return _element("label", id, text=text, tone=tone, bold=bold, **props)


def badge(id: str, text: str, tone: Optional[str] = None, **props: Any) -> JsonObject:
    return _element("badge", id, text=text, tone=tone, **props)


def field(id: str, kind: str = "text", label: Optional[str] = None, value: Any = None,
          choices: Optional[Sequence[Any]] = None, filter: Optional[str] = None,
          placeholder: Optional[str] = None, **props: Any) -> JsonObject:
    return _element("field", id, kind=kind, label=label, value=value,
                    choices=list(choices) if choices is not None else None,
                    filter=filter, placeholder=placeholder, **props)


def button(id: str, label: str, action: Optional[str] = None,
           arguments: Optional[JsonObject] = None, event: Optional[bool] = None,
           tone: Optional[str] = None, confirm: Optional[JsonObject] = None,
           save_dialog: Optional[JsonObject] = None, **props: Any) -> JsonObject:
    return _element("button", id, label=label, action=action, arguments=arguments,
                    event=event, tone=tone, confirm=confirm, save_dialog=save_dialog, **props)


def progress(id: str, value: Optional[float] = None, busy: Optional[bool] = None,
             text: Optional[str] = None, **props: Any) -> JsonObject:
    return _element("progress", id, value=value, busy=busy, text=text, **props)


def table_column(id: str, title: Optional[str] = None, width: Optional[int] = None,
                 editable: Optional[bool] = None) -> JsonObject:
    column_object: JsonObject = {"id": id, "title": title if title is not None else id}
    if width is not None:
        column_object["width"] = width
    if editable is not None:
        column_object["editable"] = editable
    return column_object


def table_row(id: str, cells: Sequence[Any], tone: Optional[str] = None,
              tooltip: Optional[str] = None, editors: Optional[JsonObject] = None) -> JsonObject:
    row_object: JsonObject = {"id": id, "cells": [str(cell) for cell in cells]}
    for key, value in (("tone", tone), ("tooltip", tooltip), ("editors", editors)):
        if value is not None:
            row_object[key] = value
    return row_object


def editor(kind: str, choices: Optional[Sequence[Any]] = None, min: Optional[float] = None,
           max: Optional[float] = None, max_length: Optional[int] = None,
           placeholder: Optional[str] = None, value: Any = None) -> JsonObject:
    """A table cell editor: kind text, number, choice, multichoice or boolean (host feature view_edit).

    Pass it in table_row(editors={column_id: editor(...)}). value is the optional starting value
    (a list of checked values for multichoice)."""
    spec: JsonObject = {"kind": kind}
    for key, item in (("choices", list(choices) if choices is not None else None), ("min", min),
                      ("max", max), ("max_length", max_length), ("placeholder", placeholder),
                      ("value", value)):
        if item is not None:
            spec[key] = item
    return spec


def table(id: str, columns: Sequence[JsonObject], rows: Optional[Sequence[JsonObject]] = None,
          sortable: Optional[bool] = None, filter_box: Optional[bool] = None,
          selection: Optional[str] = None, **props: Any) -> JsonObject:
    return _element("table", id, columns=list(columns),
                    rows=list(rows) if rows is not None else None, sortable=sortable,
                    filter_box=filter_box, selection=selection, **props)


# Patch entries for Plugin.patch().
def set_props(element_id: str, **props: Any) -> JsonObject:
    return {"id": element_id, "set": props}


def replace_rows(table_id: str, rows: Sequence[JsonObject]) -> JsonObject:
    return {"id": table_id, "replace_rows": list(rows)}


def update_rows(table_id: str, rows: Sequence[JsonObject]) -> JsonObject:
    return {"id": table_id, "update_rows": list(rows)}


def insert_rows(table_id: str, rows: Sequence[JsonObject],
                before: Optional[str] = None) -> JsonObject:
    entry: JsonObject = {"id": table_id, "insert_rows": list(rows)}
    if before is not None:
        entry["before"] = before
    return entry


def remove_rows(table_id: str, row_ids: Sequence[str]) -> JsonObject:
    return {"id": table_id, "remove_rows": list(row_ids)}


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

    def write(self, records: Iterable[str], *, finish_on_cancel: bool = False) -> None:
        """Write a bounded batch; optionally finish it within one second of cancellation."""
        cancellation_deadline = None

        def check() -> None:
            nonlocal cancellation_deadline
            if finish_on_cancel and self.job.cancelled.is_set() and not self.plugin.stopping.is_set():
                if cancellation_deadline is None:
                    cancellation_deadline = time.monotonic() + CANCEL_FLUSH_TIMEOUT
                if time.monotonic() >= cancellation_deadline:
                    raise Cancelled("Pending batch flush timed out")
            else:
                self.job.check_cancelled()
            if self.closed.is_set():
                raise Cancelled("Stream stopped")

        check()
        params = {"stream_id": self.id, "sequence": self.sequence, "records": list(records)}
        size = len(self.plugin.encode({"jsonrpc": "2.0", "method": "stream/data", "params": params}))
        with self.plugin.credit:
            while self.plugin.pending_bytes + size > MAX_PENDING:
                check()
                self.plugin.credit.wait(0.1)
            self.plugin.pending_bytes += size
        try:
            self.ack.clear()
            check()
            self.plugin.notify("stream/data", params)
            while not self.ack.wait(0.1):
                check()
            check()
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


class Message:
    """An open host message box; the result arrives as ui/message_result."""

    def __init__(self, plugin: Plugin, identifier: str,
                 callback: Optional[ViewCallback]) -> None:
        self.plugin = plugin
        self.id = identifier
        self.callback = callback
        self.result: Optional[JsonObject] = None
        self.done = threading.Event()

    def wait(self, timeout: Optional[float] = None) -> Optional[JsonObject]:
        """Block for the result: {message_id, button, timed_out}; None on timeout or shutdown."""
        deadline = None if timeout is None else time.monotonic() + timeout
        while not self.done.wait(0.1):
            if self.plugin.stopping.is_set():
                return None
            if deadline is not None and time.monotonic() >= deadline:
                return None
        return self.result

    def close(self) -> None:
        """Close the box; the result reports button "closed"."""
        self.plugin.notify("ui/close_message", {"message_id": self.id})


class View:
    """Convenience wrapper that binds a declared view id."""

    def __init__(self, plugin: Plugin, identifier: str) -> None:
        self.plugin = plugin
        self.id = identifier

    def update(self, root: JsonObject, window_id: Optional[str] = None) -> None:
        self.plugin.update(self.id, root, window_id)

    def patch(self, element_id: Optional[str] = None, window_id: Optional[str] = None,
              changes: Optional[Sequence[JsonObject]] = None, **props: Any) -> None:
        entries = list(changes or [])
        if element_id is not None:
            entries.append(set_props(element_id, **props))
        self.plugin.patch(self.id, entries, window_id)

    def show(self, window_id: Optional[str] = None, **overrides: Any) -> None:
        self.plugin.show_view(self.id, window_id, **overrides)

    def hide(self, window_id: Optional[str] = None) -> None:
        self.plugin.hide_view(self.id, window_id)


class Plugin:
    def __init__(self, manifest_path: str = "plugin.json") -> None:
        self.manifest = json.loads(pathlib.Path(manifest_path).read_text(encoding="utf-8"))
        self.actions: dict[str, Action] = {}
        self.jobs: dict[str, Job] = {}
        self.streams: dict[str, Stream] = {}
        self.workers: dict[str, threading.Thread] = {}
        self.configuration: JsonObject = {}
        self.config_directory = None
        self.host_features: list[str] = []
        self.lock = threading.RLock()
        self.output_lock = threading.Lock()
        self.credit = threading.Condition()
        self.pending_bytes = 0
        self.stopping = threading.Event()
        self.initialized = False
        self.view_callbacks: dict[str, list[ViewCallback]] = {
            "view/opened": [], "view/closed": [], "view/event": []}
        self.messages: dict[str, Message] = {}
        self.view_queue: queue.Queue = queue.Queue(maxsize=VIEW_QUEUE_LIMIT)
        self.view_worker: Optional[threading.Thread] = None

    def action(self, identifier: str) -> Callable[[Action], Action]:
        def register(callback: Action) -> Action:
            self.actions[identifier] = callback
            return callback
        return register

    # Plugin views and message boxes (docs/plugin-views.md).
    @property
    def supports_views(self) -> bool:
        return "views" in self.host_features

    @property
    def supports_view_edit(self) -> bool:
        return "view_edit" in self.host_features

    def on_view_opened(self, callback: ViewCallback) -> ViewCallback:
        """Register callback(params) for view/opened {view_id, window_id, context}."""
        self.view_callbacks["view/opened"].append(callback)
        return callback

    def on_view_closed(self, callback: ViewCallback) -> ViewCallback:
        """Register callback(params) for view/closed {view_id, window_id}."""
        self.view_callbacks["view/closed"].append(callback)
        return callback

    def on_view_event(self, callback: ViewCallback) -> ViewCallback:
        """Register callback(params) for view/event {view_id, window_id, element_id, type,
        value, inputs}. Callbacks run in order on one worker thread, never the reader."""
        self.view_callbacks["view/event"].append(callback)
        return callback

    def view(self, identifier: str) -> View:
        return View(self, identifier)

    def update(self, view_id: str, root: JsonObject, window_id: Optional[str] = None) -> None:
        """Replace a view's element tree (in every window without window_id)."""
        params: JsonObject = {"view_id": view_id, "root": root}
        if window_id:
            params["window_id"] = window_id
        self.notify("view/update", params)

    def patch(self, view_id: str, changes: Sequence[JsonObject],
              window_id: Optional[str] = None) -> None:
        """Apply set_props()/row operation entries to existing elements."""
        params: JsonObject = {"view_id": view_id, "patch": list(changes)}
        if window_id:
            params["window_id"] = window_id
        self.notify("view/update", params)

    def show_view(self, view_id: str, window_id: Optional[str] = None, *,
                  area: Optional[str] = None, floating: Optional[bool] = None,
                  title: Optional[str] = None, focus: Optional[bool] = None) -> None:
        """Open or raise a declared view (default: the most recently active window)."""
        params = _element("view", None, view_id=view_id, window_id=window_id or None,
                          area=area, floating=floating, title=title, focus=focus)
        del params["type"]
        self.notify("view/show", params)

    def hide_view(self, view_id: str, window_id: Optional[str] = None) -> None:
        params: JsonObject = {"view_id": view_id}
        if window_id:
            params["window_id"] = window_id
        self.notify("view/hide", params)

    def message(self, text: str, title: str = "", icon: str = "info",
                buttons: Sequence[str] = ("ok",), default: Optional[str] = None,
                timeout_ms: Optional[int] = None, modal: bool = True,
                window_id: Optional[str] = None, callback: Optional[ViewCallback] = None,
                message_id: Optional[str] = None) -> Message:
        """Show a host message box. Use the returned Message.wait() or a result callback."""
        unknown = [name for name in buttons if name not in MESSAGE_BUTTONS]
        if unknown or not buttons:
            raise ValueError(f"Unsupported message buttons: {unknown or 'none'}")
        identifier = message_id or str(uuid.uuid4())
        handle = Message(self, identifier, callback)
        with self.lock:
            self.messages[identifier] = handle
        params = _element("message", None, message_id=identifier, title=title, text=text[:4096],
                          icon=icon, buttons=list(buttons), default=default,
                          timeout_ms=timeout_ms, modal=modal, window_id=window_id or None)
        del params["type"]
        try:
            self.notify("ui/message", params)
        except BaseException:
            with self.lock:
                self.messages.pop(identifier, None)
            raise
        return handle

    def _queue_view_callback(self, callbacks: list[ViewCallback], params: JsonObject) -> None:
        if not callbacks:
            return
        with self.lock:
            if self.view_worker is None:
                self.view_worker = threading.Thread(target=self._run_view_callbacks,
                                                    name="plugin-view-callbacks", daemon=True)
                self.view_worker.start()
        try:
            self.view_queue.put_nowait((list(callbacks), params))
        except queue.Full:
            print("Plugin view callback queue is full; event dropped", file=sys.stderr, flush=True)

    def _run_view_callbacks(self) -> None:
        while True:
            item = self.view_queue.get()
            if item is None:
                return
            callbacks, params = item
            for callback in callbacks:
                try:
                    callback(params)
                except Exception as error:  # A callback failure never stops the protocol.
                    print(f"Plugin view callback failed: {error}", file=sys.stderr, flush=True)

    def _view_notification(self, method: str, params: JsonObject) -> None:
        if method == "ui/message_result":
            with self.lock:
                handle = self.messages.pop(str(params.get("message_id")), None)
            if handle:
                handle.result = params
                handle.done.set()
                if handle.callback:
                    self._queue_view_callback([handle.callback], params)
            return
        self._queue_view_callback(self.view_callbacks[method], params)

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
            self.host_features = [str(name) for name in params.get("host_features", [])]
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
        elif method in ("view/opened", "view/closed", "view/event", "ui/message_result"):
            self._view_notification(method, params)
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
                view_worker = self.view_worker
                for handle in self.messages.values():
                    handle.done.set()
                self.messages.clear()
            with self.credit:
                self.credit.notify_all()
            deadline = time.monotonic() + 2.8
            for worker in workers:
                worker.join(max(0.0, deadline - time.monotonic()))
            if view_worker is not None:
                try:
                    self.view_queue.put(None, timeout=max(0.0, deadline - time.monotonic()))
                    view_worker.join(max(0.0, deadline - time.monotonic()))
                except queue.Full:
                    pass
