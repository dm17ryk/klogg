"""A fake CILogg host for testing process plugins without CILogg. Python standard library only.

    from cilogg_plugin_testing import FakeHost, log_selection

    with FakeHost.start_package("path/to/my-plugin") as host:   # or FakeHost.in_process(plugin)
        host.initialize()                                       # a current host; see initialize_older_host()
        assert host.invoke("hello")["greeting"] == "hi"
        host.open_view("main")
        tree = host.wait_for("view/update")["root"]
        assert host.resolve_tooltip("main", "devices", row_id="r1", column_id="state") is not None
        assert host.populate_menu("main", "recent")["items"]       # a dynamic submenu opens
        host.send_command("lines", "explain", "log", selection=log_selection([(10, 12)]))
        assert host.complete_field("main", "board", "Bo")["items"]  # typing paused in a field
        host.change_field("main", "label", "x y", valid=False)      # a field changed
        host.check_rows("main", "devices", ["r1", "r2"], True)       # rich table events
        host.move_rows("main", "devices", ["r2"], before="r1")
        host.send_element_command("rows", "forget", "main", "devices", row_id="r1",
                                  selected_ids=["r1"])               # an element menu entry
        assert host.request_children("main", "benches", "bench1")["nodes"]  # a lazy node expands
        host.move_nodes("main", "benches", ["board2"], parent="bench1", before="board1")
        host.send_node_command("nodes", "reset", "main", "benches", node_id="board2")
        host.select_files("main", "files", ["C:/fw/a.bin"])       # a file tree's selection

The host records every frame in both directions (`frames`), answers stream/open and
stream/data like CILogg, and checks the plugin's JSON-RPC envelopes (`errors`). See
docs/plugin-sdk.md.
"""
from __future__ import annotations

import itertools
import json
import os
import pathlib
import queue
import subprocess
import sys
import threading
import time
import uuid
from typing import (
    Any, Callable, Dict, List, Mapping, NamedTuple, Optional, Protocol, Sequence, Set, Union,
)

JsonObject = Dict[str, Any]
Predicate = Callable[[JsonObject], bool]
PathLike = Union[str, "os.PathLike[str]"]

# What the current CILogg announces in initialize (docs/plugin-sdk-advanced.md, section 4.1).
HOST_FEATURES = ("views", "view_edit", "queries", "tooltips", "icons", "menus", "shortcuts",
                 "choices", "toggles", "field_kinds", "validation", "rich_tables",
                 "element_menus", "tree", "file_tree")
HOST_FEATURE_VERSIONS: Mapping[str, int] = {
    "views": 2, "view_edit": 1, "queries": 1, "tooltips": 1, "icons": 1, "menus": 1,
    "shortcuts": 1, "choices": 1, "toggles": 1, "field_kinds": 1, "validation": 1,
    "rich_tables": 1, "element_menus": 1, "tree": 1, "file_tree": 1}
HOST_LIMITS: Mapping[str, int] = {
    "max_elements": 512, "max_table_cells": 50000, "max_text_chars": 4096,
    "max_message_bytes": 1048576}
HOST_VERSION = "0.0.0-fakehost"
# What CILogg announced with E17d: rich tables and element menus, no trees or file trees.
E17D_HOST_FEATURES = ("views", "view_edit", "queries", "tooltips", "icons", "menus", "shortcuts",
                      "choices", "toggles", "field_kinds", "validation", "rich_tables",
                      "element_menus")
E17D_HOST_FEATURE_VERSIONS: Mapping[str, int] = {
    "views": 2, "view_edit": 1, "queries": 1, "tooltips": 1, "icons": 1, "menus": 1,
    "shortcuts": 1, "choices": 1, "toggles": 1, "field_kinds": 1, "validation": 1,
    "rich_tables": 1, "element_menus": 1}
# What CILogg announced with E17c: choices, toggles, field kinds and validation, no rich tables
# or element menus.
E17C_HOST_FEATURES = ("views", "view_edit", "queries", "tooltips", "icons", "menus", "shortcuts",
                      "choices", "toggles", "field_kinds", "validation")
E17C_HOST_FEATURE_VERSIONS: Mapping[str, int] = {
    "views": 2, "view_edit": 1, "queries": 1, "tooltips": 1, "icons": 1, "menus": 1,
    "shortcuts": 1, "choices": 1, "toggles": 1, "field_kinds": 1, "validation": 1}
# What CILogg announced with E17b: menus and shortcuts, no choices, toggles, field kinds or
# validation.
E17B_HOST_FEATURES = ("views", "view_edit", "queries", "tooltips", "icons", "menus", "shortcuts")
E17B_HOST_FEATURE_VERSIONS: Mapping[str, int] = {
    "views": 2, "view_edit": 1, "queries": 1, "tooltips": 1, "icons": 1, "menus": 1,
    "shortcuts": 1}
# What CILogg announced with E17a: queries, tooltips and icons, no menus or shortcuts.
E17A_HOST_FEATURES = ("views", "view_edit", "queries", "tooltips", "icons")
E17A_HOST_FEATURE_VERSIONS: Mapping[str, int] = {
    "views": 2, "view_edit": 1, "queries": 1, "tooltips": 1, "icons": 1}
# What CILogg announced before E17 (E13/E14): only host_features.
E14_HOST_FEATURES = ("views", "view_edit")
QUERY_DEFAULT_TIMEOUT_MS: Mapping[str, int] = {
    "tooltip/resolve": 1500, "menu/populate": 1000, "tree/children": 10000, "field/complete": 500}
MAX_MESSAGE = 1024 * 1024
MAX_PENDING = 8 * MAX_MESSAGE
MAX_TABLE_IDS = 1000  # selected and checked ids in one event or menu context
MAX_TREE_IDS = 1000  # node ids or paths in one tree event or menu context
TO_PLUGIN = "to_plugin"
FROM_PLUGIN = "from_plugin"


class RpcError(Exception):
    """The plugin answered a request with a JSON-RPC error."""

    def __init__(self, code: int, message: str, data: Any = None) -> None:
        super().__init__(f"{message} ({code})")
        self.code = code
        self.message = message
        self.data = data


class QueryTimeout(TimeoutError):
    """A host query got no answer in time; the host sent query/cancel."""

    def __init__(self, identifier: str, method: str) -> None:
        super().__init__(f"No answer to {method} {identifier}")
        self.id = identifier
        self.method = method


class PluginExited(RuntimeError):
    """The plugin closed its output while the host was waiting."""


class Frame(NamedTuple):
    """One line on the wire. `message` is empty when the line was not a JSON object."""
    direction: str  # TO_PLUGIN or FROM_PLUGIN
    message: JsonObject
    raw: bytes
    time: float  # time.monotonic()

    @property
    def method(self) -> Optional[str]:
        method = self.message.get("method")
        return method if isinstance(method, str) else None


class ServablePlugin(Protocol):
    """What FakeHost.in_process() needs: cilogg_plugin.Plugin (SDK 2.0.0 or newer)."""

    def serve(self, stdin: Any = None, stdout: Any = None) -> None: ...


class _Transport(Protocol):
    def write(self, data: bytes) -> None: ...

    def readline(self) -> bytes: ...

    def close_input(self) -> None: ...

    def wait(self, timeout: float) -> Optional[int]: ...

    def kill(self) -> None: ...

    def close(self) -> None: ...

    def stderr(self) -> str: ...


class _SubprocessTransport:
    def __init__(self, command: Sequence[str], cwd: PathLike,
                 env: Optional[Mapping[str, str]]) -> None:
        environment = dict(os.environ if env is None else env)
        environment.setdefault("PYTHONUNBUFFERED", "1")
        self.process = subprocess.Popen(
            list(command), cwd=os.fspath(cwd), env=environment,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self._stderr: List[bytes] = []
        self._stderr_size = 0
        self._stderr_reader = threading.Thread(target=self._read_stderr, daemon=True,
                                               name="fake-host-stderr")
        self._stderr_reader.start()

    def _read_stderr(self) -> None:
        assert self.process.stderr is not None
        for line in self.process.stderr:
            if self._stderr_size < MAX_MESSAGE:  # Keep the first MiB; drain the rest.
                self._stderr.append(line)
                self._stderr_size += len(line)

    def write(self, data: bytes) -> None:
        assert self.process.stdin is not None
        self.process.stdin.write(data)
        self.process.stdin.flush()

    def readline(self) -> bytes:
        assert self.process.stdout is not None
        return self.process.stdout.readline(MAX_MESSAGE + 2)

    def close_input(self) -> None:
        assert self.process.stdin is not None
        try:
            self.process.stdin.close()
        except OSError:
            pass

    def wait(self, timeout: float) -> Optional[int]:
        try:
            return self.process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            return None

    def kill(self) -> None:
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait(timeout=5)

    def close(self) -> None:
        self.kill()
        self._stderr_reader.join(timeout=2)
        for stream in (self.process.stdout, self.process.stderr):
            if stream is not None:
                stream.close()

    def stderr(self) -> str:
        return b"".join(self._stderr).decode("utf-8", errors="replace")


class _InProcessTransport:
    """Runs plugin.serve() on a thread, connected to the host by two OS pipes."""

    def __init__(self, plugin: ServablePlugin) -> None:
        host_read, plugin_write = os.pipe()
        plugin_read, host_write = os.pipe()
        self._host_reader = os.fdopen(host_read, "rb")
        self._host_writer = os.fdopen(host_write, "wb")
        self._plugin_reader = os.fdopen(plugin_read, "rb")
        self._plugin_writer = os.fdopen(plugin_write, "wb")
        self.failure: Optional[BaseException] = None
        self.thread = threading.Thread(target=self._serve, args=(plugin,), daemon=True,
                                       name="fake-host-plugin")
        self.thread.start()

    def _serve(self, plugin: ServablePlugin) -> None:
        try:
            plugin.serve(stdin=self._plugin_reader, stdout=self._plugin_writer)
        except BaseException as error:  # Reported through `failure`; the host sees end of output.
            self.failure = error
        finally:
            for stream in (self._plugin_writer, self._plugin_reader):
                try:
                    stream.close()
                except OSError:
                    pass

    def write(self, data: bytes) -> None:
        self._host_writer.write(data)
        self._host_writer.flush()

    def readline(self) -> bytes:
        return self._host_reader.readline(MAX_MESSAGE + 2)

    def close_input(self) -> None:
        try:
            self._host_writer.close()
        except OSError:
            pass

    def wait(self, timeout: float) -> Optional[int]:
        self.thread.join(timeout)
        if self.thread.is_alive():
            return None
        return 0 if self.failure is None else 1

    def kill(self) -> None:
        self.close_input()

    def close(self) -> None:
        self.close_input()
        self.thread.join(5)
        try:
            self._host_reader.close()
        except OSError:
            pass

    def stderr(self) -> str:
        return ""


def log_selection(ranges: Sequence[Sequence[int]], *, count: Optional[int] = None,
                  text: Optional[str] = None, truncated: Optional[bool] = None) -> JsonObject:
    """The `context.selection` of a log menu entry: 1-based [first, last] line ranges, the
    selected line count (default: the lines of `ranges`) and, for a picked entry, the selected
    text and whether the host cut it at 64 KiB."""
    pairs = [[int(first), int(last)] for first, last in ranges]
    selection: JsonObject = {
        "ranges": pairs,
        "count": count if count is not None else sum(last - first + 1 for first, last in pairs)}
    if text is not None:
        selection["text"] = text
        selection["truncated"] = bool(truncated)
    elif truncated is not None:
        selection["truncated"] = truncated
    return selection


def element_context(view_id: str, element_id: str, *, row_id: Optional[str] = None,
                    column_id: Optional[str] = None, selected_ids: Sequence[str] = (),
                    selected_count: Optional[int] = None, window_id: str = "w1") -> JsonObject:
    """The `context` of an element menu entry (location "element"): the view and element, the
    clicked row and column (None: the click was not on a cell), the selected row ids (CILogg
    sends at most 1000) and their count (default: the number of ids)."""
    if isinstance(selected_ids, str):
        raise ValueError("selected_ids is a list of row ids")
    ids = [str(row) for row in selected_ids]
    return {"window_id": window_id, "view_id": view_id, "element_id": element_id,
            "row_id": row_id, "column_id": column_id, "selected_ids": ids[:MAX_TABLE_IDS],
            "selected_count": len(ids) if selected_count is None else selected_count}


def node_context(view_id: str, element_id: str, *, node_id: Optional[str] = None,
                 selected_ids: Sequence[str] = (), selected_count: Optional[int] = None,
                 window_id: str = "w1") -> JsonObject:
    """The `context` of an element menu entry on a tree or a file tree: the view and element,
    the clicked node (a file tree's clicked path; None: the click was not on a node), the
    selected node ids or paths (CILogg sends at most 1000) and their count (default: the
    number of ids). It has node_id instead of a table's row_id and column_id."""
    if isinstance(selected_ids, str):
        raise ValueError("selected_ids is a list of node ids or paths")
    ids = [str(node) for node in selected_ids]
    return {"window_id": window_id, "view_id": view_id, "element_id": element_id,
            "node_id": node_id, "selected_ids": ids[:MAX_TREE_IDS],
            "selected_count": len(ids) if selected_count is None else selected_count}


def _row_ids(row_ids: Union[str, Sequence[str]]) -> List[str]:
    return [row_ids] if isinstance(row_ids, str) else [str(row) for row in row_ids]


def _paths(paths: Union[str, Sequence[str]]) -> List[str]:
    """Paths as CILogg sends them: with forward slashes."""
    return [path.replace("\\", "/") for path in _row_ids(paths)]


def _selected_extra(ids: List[str], selected_count: Optional[int]) -> JsonObject:
    """`selected_count` (default: the number of ids) and `truncated` when CILogg cut the ids."""
    extra: JsonObject = {"selected_count": len(ids) if selected_count is None else selected_count}
    if len(ids) > MAX_TREE_IDS or extra["selected_count"] > len(ids):
        extra["truncated"] = True
    return extra


def _envelope_error(message: Any) -> Optional[str]:
    """What CILogg would refuse in a plugin frame (PluginProcess::drain), or None."""
    if not isinstance(message, dict) or message.get("jsonrpc") != "2.0":
        return "Malformed plugin JSON-RPC message"
    if "method" not in message:
        if "id" not in message:
            return "Malformed plugin JSON-RPC message"
        error = message.get("error")
        if (not isinstance(message.get("id"), str) or ("result" in message) == ("error" in message)
                or ("error" in message and (
                    not isinstance(error, dict) or not isinstance(error.get("code"), int)
                    or not isinstance(error.get("message"), str)))):
            return "Malformed plugin JSON-RPC response"
        return None
    if not isinstance(message.get("method"), str) or not isinstance(message.get("params"), dict):
        return "Malformed plugin JSON-RPC notification"
    if "id" in message:
        return f"Plugin sent a request ({message.get('method')}); CILogg fails such plugins"
    return None


class FakeHost:
    """Drives one plugin like CILogg does. Create it with start(), start_package() or
    in_process(); close() (or a with block) stops the plugin."""

    def __init__(self, transport: _Transport, manifest: Optional[Mapping[str, Any]] = None,
                 timeout: float = 10.0) -> None:
        self.manifest: JsonObject = dict(manifest) if manifest is not None else {}
        self.timeout = timeout
        self.auto_streams = True
        self.frames: List[Frame] = []
        self.errors: List[str] = []
        self.records: List[str] = []
        self.panels: List[JsonObject] = []
        self.progress: List[JsonObject] = []
        self.initialization: Optional[JsonObject] = None
        self._transport = transport
        self._condition = threading.Condition()
        self._frames_lock = threading.Lock()
        # Frames go out through one writer thread, so neither a test nor the reader (which
        # answers stream traffic) ever blocks on a full pipe while the plugin waits for us.
        self._outgoing: queue.Queue[Optional[bytes]] = queue.Queue()
        self._inbox: List[JsonObject] = []
        self._responses: Dict[str, JsonObject] = {}
        self._abandoned: Set[str] = set()
        self._ids = itertools.count(1)
        self._eof = False
        self._closed = False
        self._reader = threading.Thread(target=self._read, daemon=True, name="fake-host-reader")
        self._writer = threading.Thread(target=self._write, daemon=True, name="fake-host-writer")
        self._reader.start()
        self._writer.start()

    # Construction.
    @classmethod
    def start(cls, command: Sequence[str], cwd: PathLike, env: Optional[Mapping[str, str]] = None,
              timeout: float = 10.0) -> FakeHost:
        """Start a plugin process: command and working directory, as CILogg would."""
        manifest_path = pathlib.Path(cwd) / "plugin.json"
        manifest = (json.loads(manifest_path.read_text(encoding="utf-8"))
                    if manifest_path.is_file() else None)
        return cls(_SubprocessTransport(command, cwd, env), manifest, timeout)

    @classmethod
    def start_package(cls, folder: PathLike, python: Optional[str] = None,
                      env: Optional[Mapping[str, str]] = None, timeout: float = 10.0) -> FakeHost:
        """Start the plugin in `folder` from its plugin.json (executable and arguments)."""
        root = pathlib.Path(folder)
        manifest = json.loads((root / "plugin.json").read_text(encoding="utf-8"))
        arguments = [str(argument) for argument in manifest.get("arguments", [])]
        executable = str(manifest["executable"])
        if executable == "python":
            command = [python or sys.executable, *arguments]
        else:
            path = root / executable
            if os.name == "nt" and not path.suffix:
                path = path.with_suffix(".exe")
            command = [str(path), *arguments]
        return cls(_SubprocessTransport(command, root, env), manifest, timeout)

    @classmethod
    def in_process(cls, plugin: ServablePlugin, timeout: float = 10.0) -> FakeHost:
        """Serve a cilogg_plugin.Plugin object on a thread of this process."""
        manifest = getattr(plugin, "manifest", None)
        return cls(_InProcessTransport(plugin),
                   manifest if isinstance(manifest, Mapping) else None, timeout)

    def __enter__(self) -> FakeHost:
        return self

    def __exit__(self, *_args: Any) -> None:
        self.close()

    # Sending.
    def send_frame(self, frame: Union[Mapping[str, Any], str, bytes]) -> None:
        """Send any frame: a message (one JSON line), or raw text or bytes exactly as given."""
        if isinstance(frame, bytes):
            data = frame
        elif isinstance(frame, str):
            data = frame.encode("utf-8")
        else:
            data = json.dumps(frame, ensure_ascii=False, separators=(",", ":")).encode("utf-8") + b"\n"
        try:
            parsed = json.loads(data)
        except ValueError:
            parsed = None
        with self._frames_lock:
            if self._closed:
                raise PluginExited("The host is closed")
            self.frames.append(Frame(TO_PLUGIN, parsed if isinstance(parsed, dict) else {}, data,
                                     time.monotonic()))
            self._outgoing.put(data)

    def notify(self, method: str, params: Optional[Mapping[str, Any]] = None) -> None:
        self.send_frame({"jsonrpc": "2.0", "method": method, "params": dict(params or {})})

    def send_request(self, method: str, params: Optional[Mapping[str, Any]] = None,
                     identifier: Optional[str] = None) -> str:
        """Send a request without waiting; returns its id (see wait_response())."""
        identifier = identifier if identifier is not None else f"r:{next(self._ids)}"
        self.send_frame({"jsonrpc": "2.0", "id": identifier, "method": method,
                         "params": dict(params or {})})
        return identifier

    def request(self, method: str, params: Optional[Mapping[str, Any]] = None,
                timeout: Optional[float] = None, identifier: Optional[str] = None) -> Any:
        """Send a request and return its result; raises RpcError or TimeoutError."""
        identifier = self.send_request(method, params, identifier)
        return self._result(self.wait_response(identifier, timeout))

    # The protocol.
    def initialize(self, configuration: Optional[Mapping[str, Any]] = None, *,
                   host_features: Optional[Sequence[str]] = HOST_FEATURES,
                   host_feature_versions: Optional[Mapping[str, int]] = HOST_FEATURE_VERSIONS,
                   host_version: Optional[str] = HOST_VERSION,
                   host_limits: Optional[Mapping[str, int]] = HOST_LIMITS,
                   plugin_id: Optional[str] = None, config_directory: Optional[str] = None,
                   extra_params: Optional[Mapping[str, Any]] = None,
                   timeout: Optional[float] = None) -> JsonObject:
        """Initialize like the current CILogg; pass None to leave a key out (an older host)."""
        identifier = plugin_id if plugin_id is not None else self.manifest.get("id")
        if not isinstance(identifier, str):
            raise ValueError("No plugin id: pass plugin_id or start the plugin from its folder")
        params: JsonObject = {
            "protocol_version": 1, "plugin_id": identifier,
            "configuration": dict(configuration or {}),
            "max_message_bytes": MAX_MESSAGE, "max_pending_bytes": MAX_PENDING,
        }
        if config_directory is not None:
            params["config_directory"] = config_directory
        for key, value in (("host_features", list(host_features) if host_features is not None else None),
                           ("host_feature_versions",
                            dict(host_feature_versions) if host_feature_versions is not None else None),
                           ("host_version", host_version),
                           ("host_limits", dict(host_limits) if host_limits is not None else None)):
            if value is not None:
                params[key] = value
        params.update(extra_params or {})
        result = self.request("initialize", params, timeout, identifier="initialize")
        if not isinstance(result, dict):
            raise RpcError(-32603, "Initialize result is not an object")
        self.initialization = result
        return result

    def initialize_older_host(self, configuration: Optional[Mapping[str, Any]] = None,
                              host_features: Optional[Sequence[str]] = E14_HOST_FEATURES,
                              timeout: Optional[float] = None) -> JsonObject:
        """Initialize like CILogg before E17: only host_features (None: not even that)."""
        return self.initialize(configuration, host_features=host_features,
                               host_feature_versions=None, host_version=None, host_limits=None,
                               timeout=timeout)

    def configure(self, configuration: Mapping[str, Any]) -> None:
        self.notify("configure", {"configuration": dict(configuration)})

    def start_invoke(self, action: str, arguments: Optional[Mapping[str, Any]] = None,
                     context: Optional[Mapping[str, Any]] = None,
                     job_id: Optional[str] = None) -> str:
        """Start a job without waiting; returns the job id (also the request id)."""
        job_id = job_id or str(uuid.uuid4())
        self.send_request("invoke", {"job_id": job_id, "action": action,
                                     "arguments": dict(arguments or {}),
                                     "context": dict(context or {})}, job_id)
        return job_id

    def invoke(self, action: str, arguments: Optional[Mapping[str, Any]] = None,
               context: Optional[Mapping[str, Any]] = None,
               timeout: Optional[float] = None) -> Any:
        """Run an action to completion and return its result; raises RpcError on failure."""
        return self._result(self.wait_response(self.start_invoke(action, arguments, context),
                                               timeout))

    def cancel(self, job_id: str) -> None:
        self.notify("cancel", {"job_id": job_id})

    def open_view(self, view_id: str, window_id: str = "w1",
                  context: Optional[Mapping[str, Any]] = None) -> None:
        self.notify("view/opened", {"view_id": view_id, "window_id": window_id,
                                    "context": dict(context or {})})

    def close_view(self, view_id: str, window_id: str = "w1") -> None:
        self.notify("view/closed", {"view_id": view_id, "window_id": window_id})

    def view_event(self, view_id: str, element_id: str, event_type: str, value: Any = None, *,
                   window_id: str = "w1", inputs: Optional[Mapping[str, Any]] = None,
                   **extra: Any) -> None:
        """Send view/event, e.g. view_event("main", "go", "clicked")."""
        self.notify("view/event", {"view_id": view_id, "window_id": window_id,
                                   "element_id": element_id, "type": event_type, "value": value,
                                   "inputs": dict(inputs or {}), **extra})

    def change_field(self, view_id: str, element_id: str, value: Any, *,
                     valid: Optional[bool] = None, window_id: str = "w1",
                     inputs: Optional[Mapping[str, Any]] = None) -> None:
        """Send a `changed` view/event, as when the user changes a field, a check box, a radio
        group or a check group. `valid` is the field's validity: a host with validation sends
        it only for fields that declare a validation key (required, pattern, min, max,
        min_length, max_length or a non-empty error), so leave it None for other elements.
        `inputs` gets the new value under element_id, like the host's."""
        all_inputs = dict(inputs or {})
        all_inputs[element_id] = value
        extra: JsonObject = {} if valid is None else {"valid": valid}
        self.view_event(view_id, element_id, "changed", value, window_id=window_id,
                        inputs=all_inputs, **extra)

    # Rich tables (host feature rich_tables).
    def _declares_handles(self) -> bool:
        handles = self.manifest.get("handles")
        return isinstance(handles, list) and bool(handles)

    def select_rows(self, view_id: str, element_id: str, row_ids: Union[str, Sequence[str]], *,
                    current: Optional[str] = None, selected_count: Optional[int] = None,
                    window_id: str = "w1") -> None:
        """Send a table's `selected` event, as when the user selects rows: the ids (at most
        1000 go out, with `truncated` when more were selected), `selected_count` (default: the
        number of ids) and, when the plugin's manifest declares `handles` (as CILogg checks),
        `current`, the row with the keyboard focus (None: no row)."""
        ids = _row_ids(row_ids)
        extra: JsonObject = {"selected_count": len(ids) if selected_count is None else selected_count}
        if len(ids) > MAX_TABLE_IDS or extra["selected_count"] > len(ids):
            extra["truncated"] = True
        if self._declares_handles():
            extra["current"] = current
        self.view_event(view_id, element_id, "selected", ids[:MAX_TABLE_IDS],
                        window_id=window_id, **extra)

    def activate_cell(self, view_id: str, element_id: str, row_id: str, column_id: str, *,
                      window_id: str = "w1") -> None:
        """Send a table's `activated` event, as on a double-click or Enter on a cell that is not
        editable. CILogg sends it only to plugins whose manifest declares `handles`."""
        self.view_event(view_id, element_id, "activated",
                        {"row_id": row_id, "column_id": column_id}, window_id=window_id)

    def check_rows(self, view_id: str, element_id: str, row_ids: Union[str, Sequence[str]],
                   checked: bool = True, *, row_id: Optional[str] = None,
                   window_id: str = "w1") -> None:
        """Send a table's `checked` event, as when the user changes a row's check box (or a
        Shift+click range): `row_ids` every row that changed, `row_id` the clicked row
        (default: the first of row_ids)."""
        ids = _row_ids(row_ids)
        if not ids:
            raise ValueError("check_rows needs at least one row id")
        self.view_event(view_id, element_id, "checked",
                        {"row_id": ids[0] if row_id is None else row_id, "row_ids": ids,
                         "checked": checked}, window_id=window_id)

    def move_rows(self, view_id: str, element_id: str, row_ids: Union[str, Sequence[str]],
                  before: Optional[str] = None, *, window_id: str = "w1") -> None:
        """Send a reorderable table's `moved` event, as when the user drops the rows (in shown
        order) before the row `before` (None: at the end)."""
        ids = _row_ids(row_ids)
        if not ids:
            raise ValueError("move_rows needs at least one row id")
        self.view_event(view_id, element_id, "moved", {"row_ids": ids, "before": before},
                        window_id=window_id)

    # Trees (host feature tree). CILogg sends every key of these events to every plugin.
    def select_nodes(self, view_id: str, element_id: str, node_ids: Union[str, Sequence[str]], *,
                     current: Optional[str] = None, selected_count: Optional[int] = None,
                     window_id: str = "w1") -> None:
        """Send a tree's `selected` event, as when the user selects nodes: the ids in tree
        order (at most 1000 go out, with `truncated` when more were selected),
        `selected_count` (default: the number of ids) and `current`, the node with the
        keyboard focus (None: no node)."""
        ids = _row_ids(node_ids)
        extra = _selected_extra(ids, selected_count)
        extra["current"] = current
        self.view_event(view_id, element_id, "selected", ids[:MAX_TREE_IDS],
                        window_id=window_id, **extra)

    def activate_node(self, view_id: str, element_id: str, node_id: str,
                      column_id: Optional[str] = None, *, window_id: str = "w1") -> None:
        """Send a tree's `activated` event, as on a double-click or Enter on a cell that is not
        editable and not a check box (`column_id` None: a tree without columns)."""
        self.view_event(view_id, element_id, "activated",
                        {"node_id": node_id, "column_id": column_id}, window_id=window_id)

    def check_nodes(self, view_id: str, element_id: str, node_ids: Union[str, Sequence[str]],
                    checked: bool = True, *, node_id: Optional[str] = None,
                    window_id: str = "w1") -> None:
        """Send a tree's `checked` event, as when the user changes a node's check box:
        `node_ids` every node set to `checked` (the clicked one first; with auto_tristate its
        loaded descendants too), `node_id` the clicked node (default: the first of node_ids)."""
        ids = _row_ids(node_ids)
        if not ids:
            raise ValueError("check_nodes needs at least one node id")
        self.view_event(view_id, element_id, "checked",
                        {"node_id": ids[0] if node_id is None else node_id, "checked": checked,
                         "node_ids": ids[:MAX_TREE_IDS]}, window_id=window_id)

    def edit_node(self, view_id: str, element_id: str, node_id: str, column_id: Optional[str],
                  value: Any, previous: str = "", *, window_id: str = "w1") -> None:
        """Send a tree's `edited` event, as when the user edits a cell of an `editable` column
        of a node with an editor: the new value, and `node_id`, `column_id` (None: a tree
        without columns) and the previous text."""
        self.view_event(view_id, element_id, "edited", value, window_id=window_id,
                        node_id=node_id, column_id=column_id, previous=previous)

    def move_nodes(self, view_id: str, element_id: str, node_ids: Union[str, Sequence[str]],
                   parent: Optional[str] = None, before: Optional[str] = None, *,
                   window_id: str = "w1") -> None:
        """Send a reorderable tree's `moved` event, as when the user drops nodes under `parent`
        (None: the top level) before the node `before` (None: at the end)."""
        ids = _row_ids(node_ids)
        if not ids:
            raise ValueError("move_nodes needs at least one node id")
        self.view_event(view_id, element_id, "moved",
                        {"node_ids": ids, "parent": parent, "before": before},
                        window_id=window_id)

    def expand_node(self, view_id: str, element_id: str, node_id: str, *,
                    window_id: str = "w1") -> None:
        """Send a tree's `expanded` event (a tree with expand_events). It does not ask
        tree/children: see request_children()."""
        self.view_event(view_id, element_id, "expanded", {"node_id": node_id},
                        window_id=window_id)

    def collapse_node(self, view_id: str, element_id: str, node_id: str, *,
                      window_id: str = "w1") -> None:
        """Send a tree's `collapsed` event (a tree with expand_events)."""
        self.view_event(view_id, element_id, "collapsed", {"node_id": node_id},
                        window_id=window_id)

    # File trees (host feature file_tree).
    def select_files(self, view_id: str, element_id: str, paths: Union[str, Sequence[str]], *,
                     selected_count: Optional[int] = None, window_id: str = "w1",
                     inputs: Optional[Mapping[str, Any]] = None) -> None:
        """Send a file tree's `selected` event, as when the user selects (or, with
        `checkboxes`, checks) files or folders: the paths with forward slashes (at most 1000
        go out, with `truncated` when more were selected) and `selected_count`; `inputs` gets
        the paths under element_id, like the host's."""
        sent = _paths(paths)
        all_inputs = dict(inputs or {})
        all_inputs[element_id] = sent[:MAX_TREE_IDS]
        self.view_event(view_id, element_id, "selected", sent[:MAX_TREE_IDS],
                        window_id=window_id, inputs=all_inputs,
                        **_selected_extra(sent, selected_count))

    def activate_file(self, view_id: str, element_id: str, path: str, *,
                      window_id: str = "w1") -> None:
        """Send a file tree's `activated` event, as on a double-click or Enter on a file."""
        self.view_event(view_id, element_id, "activated", {"path": _paths(path)[0]},
                        window_id=window_id)

    def message_result(self, message_id: str, button: str, timed_out: bool = False) -> None:
        self.notify("ui/message_result", {"message_id": message_id, "button": button,
                                          "timed_out": timed_out})

    # Host queries.
    def send_query(self, method: str, params: Optional[Mapping[str, Any]] = None, *,
                   timeout_ms: Optional[int] = None,
                   context: Optional[Mapping[str, Any]] = None) -> str:
        """Send a query without waiting; returns its id ("q:<n>")."""
        identifier = f"q:{next(self._ids)}"
        body = dict(params or {})
        body["timeout_ms"] = timeout_ms if timeout_ms is not None else QUERY_DEFAULT_TIMEOUT_MS.get(method, 1500)
        body["context"] = dict(context or {})
        self.send_request(method, body, identifier)
        return identifier

    def cancel_query(self, identifier: str) -> None:
        """Stop waiting for a query: the host forgets it and sends query/cancel."""
        with self._condition:
            self._abandoned.add(identifier)
            self._responses.pop(identifier, None)
        self.notify("query/cancel", {"id": identifier})

    def query(self, method: str, params: Optional[Mapping[str, Any]] = None, *,
              timeout_ms: Optional[int] = None, timeout: Optional[float] = None,
              context: Optional[Mapping[str, Any]] = None) -> Any:
        """Ask a query and return its result. Without an answer within `timeout` seconds
        (default: timeout_ms) the host sends query/cancel and raises QueryTimeout."""
        identifier = self.send_query(method, params, timeout_ms=timeout_ms, context=context)
        budget_ms = timeout_ms if timeout_ms is not None else QUERY_DEFAULT_TIMEOUT_MS.get(method, 1500)
        try:
            response = self.wait_response(identifier, timeout if timeout is not None
                                          else budget_ms / 1000.0)
        except TimeoutError:
            self.cancel_query(identifier)
            raise QueryTimeout(identifier, method) from None
        return self._result(response)

    def resolve_tooltip(self, view_id: str, element_id: str, *, window_id: str = "w1",
                        row_id: Optional[str] = None, column_id: Optional[str] = None,
                        node_id: Optional[str] = None, tooltip_key: Optional[str] = None,
                        timeout_ms: int = 1500, timeout: Optional[float] = None,
                        context: Optional[Mapping[str, Any]] = None) -> Optional[JsonObject]:
        """Ask tooltip/resolve; returns the tooltip object or None."""
        params: JsonObject = {"view_id": view_id, "window_id": window_id, "element_id": element_id}
        for key, value in (("row_id", row_id), ("column_id", column_id), ("node_id", node_id),
                           ("tooltip_key", tooltip_key)):
            if value is not None:
                params[key] = value
        result = self.query("tooltip/resolve", params, timeout_ms=timeout_ms, timeout=timeout,
                            context=context)
        if result is not None and not isinstance(result, dict):
            raise RpcError(-32603, "A tooltip answer is null or an object")
        return result

    def complete_field(self, view_id: str, element_id: str, text: str,
                       cursor: Optional[int] = None, *, window_id: str = "w1",
                       timeout_ms: int = 500, timeout: Optional[float] = None,
                       context: Optional[Mapping[str, Any]] = None) -> Optional[JsonObject]:
        """Ask field/complete, as when typing pauses in a field with `completion`; `cursor`
        defaults to the end of the text. Returns the answer ({items}) or None."""
        params: JsonObject = {"view_id": view_id, "window_id": window_id,
                              "element_id": element_id, "text": text,
                              "cursor": len(text) if cursor is None else cursor}
        result = self.query("field/complete", params, timeout_ms=timeout_ms, timeout=timeout,
                            context=context)
        if result is not None and not isinstance(result, dict):
            raise RpcError(-32603, "A field/complete answer is null or an object")
        return result

    def request_children(self, view_id: str, element_id: str, node_id: str, *,
                         window_id: str = "w1", timeout_ms: int = 10000,
                         timeout: Optional[float] = None,
                         context: Optional[Mapping[str, Any]] = None) -> Any:
        """Ask tree/children, as when a node with `has_children` and no children of a `lazy`
        tree expands. Returns the answer: {"nodes": [...]} (what the SDK's
        @plugin.tree_children sends), a list of nodes or None, which CILogg all accept."""
        params: JsonObject = {"view_id": view_id, "window_id": window_id,
                              "element_id": element_id, "node_id": node_id}
        result = self.query("tree/children", params, timeout_ms=timeout_ms, timeout=timeout,
                            context=context)
        if result is not None and not isinstance(result, (dict, list)):
            raise RpcError(-32603, "A tree/children answer is null, a list or an object")
        return result

    # Menus.
    def populate_menu(self, menu_id: str, item_id: str, location: str = "plugins",
                      context: Optional[Mapping[str, Any]] = None, timeout_ms: int = 1000, *,
                      window_id: str = "w1", timeout: Optional[float] = None
                      ) -> Optional[JsonObject]:
        """Ask menu/populate, as when the dynamic entry item_id of menu_id is about to show;
        returns the answer ({items, cache_ms}) or None. For a log menu, put
        log_selection(...) without text in context["selection"]."""
        params: JsonObject = {"menu_id": menu_id, "item_id": item_id, "location": location,
                              "window_id": window_id}
        result = self.query("menu/populate", params, timeout_ms=timeout_ms, timeout=timeout,
                            context=context)
        if result is not None and not isinstance(result, dict):
            raise RpcError(-32603, "A menu/populate answer is null or an object")
        return result

    def send_command(self, menu_id: str, item_id: str, location: str = "plugins", *,
                     origin: str = "menu", window_id: str = "w1", checked: Optional[bool] = None,
                     context: Optional[Mapping[str, Any]] = None,
                     selection: Optional[Mapping[str, Any]] = None) -> None:
        """Send ui/command, as when the user picks the `event` entry item_id of menu_id
        (origin "shortcut": presses its shortcut). `checked` is the new state of a checkable
        entry; `selection` (see log_selection()) goes into context["selection"]."""
        command_context = dict(context or {})
        if selection is not None:
            command_context["selection"] = dict(selection)
        params: JsonObject = {"origin": origin, "menu_id": menu_id, "item_id": item_id,
                              "location": location, "window_id": window_id}
        if checked is not None:
            params["checked"] = checked
        params["context"] = command_context
        self.notify("ui/command", params)

    def send_element_command(self, menu_id: str, item_id: str, view_id: str, element_id: str, *,
                             row_id: Optional[str] = None, column_id: Optional[str] = None,
                             selected_ids: Sequence[str] = (),
                             selected_count: Optional[int] = None,
                             checked: Optional[bool] = None, window_id: str = "w1") -> None:
        """Send ui/command for an `event` entry of an element menu (location "element"), as when
        the user picks it in the context menu of the table element_id: the context holds the
        clicked row and column and the selected rows (see element_context())."""
        self.send_command(menu_id, item_id, "element", window_id=window_id, checked=checked,
                          context=element_context(view_id, element_id, row_id=row_id,
                                                  column_id=column_id, selected_ids=selected_ids,
                                                  selected_count=selected_count,
                                                  window_id=window_id))

    def send_node_command(self, menu_id: str, item_id: str, view_id: str, element_id: str, *,
                          node_id: Optional[str] = None, selected_ids: Sequence[str] = (),
                          selected_count: Optional[int] = None, checked: Optional[bool] = None,
                          window_id: str = "w1") -> None:
        """Send ui/command for an `event` entry of an element menu picked in the context menu
        of the tree or file tree element_id: the context holds the clicked node (a file tree's
        path) and the selected nodes or paths (see node_context())."""
        self.send_command(menu_id, item_id, "element", window_id=window_id, checked=checked,
                          context=node_context(view_id, element_id, node_id=node_id,
                                               selected_ids=selected_ids,
                                               selected_count=selected_count,
                                               window_id=window_id))

    # Receiving.
    def wait_response(self, identifier: str, timeout: Optional[float] = None) -> JsonObject:
        """Wait for the response frame to a request; raises TimeoutError or PluginExited."""
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        with self._condition:
            while identifier not in self._responses:
                if self._eof:
                    raise PluginExited(f"The plugin exited before answering {identifier}")
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(f"No answer to {identifier}")
                self._condition.wait(remaining)
            return self._responses.pop(identifier)

    def wait_for(self, method: str, predicate: Optional[Predicate] = None,
                 timeout: Optional[float] = None) -> JsonObject:
        """Return the params of the oldest unconsumed plugin notification `method` that
        matches `predicate`, and consume it. Raises TimeoutError or PluginExited."""
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        with self._condition:
            while True:
                for index, message in enumerate(self._inbox):
                    params = message.get("params")
                    if (message.get("method") == method and isinstance(params, dict)
                            and (predicate is None or predicate(params))):
                        del self._inbox[index]
                        return params
                if self._eof:
                    raise PluginExited(f"The plugin exited without sending {method}")
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(f"No {method} notification")
                self._condition.wait(remaining)

    def notifications(self, method: Optional[str] = None) -> List[JsonObject]:
        """The params of every plugin notification so far (consumed or not), oldest first."""
        with self._frames_lock:
            frames = list(self.frames)
        return [frame.message.get("params", {}) for frame in frames
                if frame.direction == FROM_PLUGIN and "id" not in frame.message
                and frame.method is not None and (method is None or frame.method == method)]

    def answers(self, identifier: str) -> List[JsonObject]:
        """Every response frame the plugin sent for `identifier`, including late ones."""
        with self._frames_lock:
            frames = list(self.frames)
        return [frame.message for frame in frames
                if frame.direction == FROM_PLUGIN and frame.method is None
                and frame.message.get("id") == identifier]

    @property
    def stderr(self) -> str:
        """The plugin's stderr (subprocess plugins; an in-process plugin writes to sys.stderr)."""
        return self._transport.stderr()

    # Stopping.
    def shutdown(self, timeout: float = 3.0) -> Optional[int]:
        """Send shutdown and wait for the plugin to end; returns its exit code (None: still
        running, then it is killed)."""
        try:
            self.notify("shutdown", {})
        except PluginExited:
            pass
        code = self._transport.wait(timeout)
        self.close()
        return code

    def close(self) -> None:
        """End the plugin's input, wait up to 3 seconds, then kill it."""
        with self._frames_lock:
            if self._closed:
                return
            self._closed = True
            self._outgoing.put(None)  # The writer closes the plugin's input after the last frame.
        self._writer.join(timeout=5)
        if self._transport.wait(3.0) is None:
            self._transport.kill()
        self._reader.join(timeout=5)
        self._transport.close()

    def _write(self) -> None:
        failed = False
        while True:
            data = self._outgoing.get()
            if data is None:
                self._transport.close_input()
                return
            if failed:
                continue
            try:
                self._transport.write(data)
            except (OSError, ValueError) as error:  # The plugin closed its input or exited.
                failed = True
                with self._condition:
                    self.errors.append(f"Cannot write to the plugin: {error}")

    @staticmethod
    def _result(response: JsonObject) -> Any:
        if "error" in response:
            error = response.get("error") or {}
            raise RpcError(int(error.get("code", 0)), str(error.get("message", "")), error.get("data"))
        return response.get("result")

    def _read(self) -> None:
        try:
            while True:
                try:
                    line = self._transport.readline()
                except (OSError, ValueError):
                    break
                if not line:
                    break
                self._receive(line)
        finally:
            with self._condition:
                self._eof = True
                self._condition.notify_all()

    def _receive(self, line: bytes) -> None:
        try:
            message = json.loads(line)
        except ValueError:
            message = None
        problem = _envelope_error(message)
        with self._frames_lock:
            self.frames.append(Frame(FROM_PLUGIN, message if isinstance(message, dict) else {}, line,
                                     time.monotonic()))
        with self._condition:
            if problem is not None:
                self.errors.append(f"{problem}: {line[:200]!r}")
            if not isinstance(message, dict):
                return
            if "method" not in message:
                identifier = message.get("id")
                if isinstance(identifier, str) and identifier not in self._abandoned:
                    self._responses[identifier] = message
                self._condition.notify_all()
                return
        self._auto_answer(message)
        with self._condition:
            self._inbox.append(message)
            self._condition.notify_all()

    def _auto_answer(self, message: JsonObject) -> None:
        """Answer stream traffic and keep panels and progress like CILogg."""
        method = message.get("method")
        params = message.get("params") if isinstance(message.get("params"), dict) else {}
        assert isinstance(params, dict)
        if method == "stream/data":
            records = params.get("records")
            if isinstance(records, list):
                self.records.extend(str(record) for record in records)
        elif method == "panel/update" and isinstance(params.get("panel"), dict):
            self.panels.append(params["panel"])
        elif method == "progress":
            self.progress.append(params)
        if not self.auto_streams:
            return
        try:
            if method == "stream/open":
                self.notify("stream/opened", {"stream_id": params.get("stream_id")})
            elif method == "stream/data":
                self.notify("stream/ack", {"stream_id": params.get("stream_id"),
                                           "sequence": params.get("sequence")})
        except PluginExited:  # The host is closing.
            pass
