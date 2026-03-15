from __future__ import annotations

import base64
import json
import os
import socket
import threading
import time
import traceback
from collections import deque
from dataclasses import dataclass
from typing import Any, Callable, Deque, Dict, List, Optional, Tuple

from .exceptions import KloggError


def _load_script_args() -> Any:
    path = os.environ.get("KLOGG_SCRIPT_ARGS_JSON_FILE", "")
    if not path:
        return None
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


@dataclass
class ReceiveEvent:
    tab_id: str
    window_index: int
    tab_index: int
    file_path: str
    port_name: str
    text: str
    raw_bytes: bytes
    hex_string: str
    timestamp: str
    display_name: str = ""
    window_id: str = ""


@dataclass
class ResponseEvent:
    tab_id: str
    window_index: int
    tab_index: int
    file_path: str
    port_name: str
    matched_text: str
    raw_bytes: bytes
    hex_string: str
    timestamp: str
    response_id: int
    response_name: str
    counter: int
    display_name: str = ""
    window_id: str = ""


class _RpcClient:
    MAX_EVENT_QUEUE = 1000

    def __init__(self) -> None:
        port = os.environ.get("KLOGG_SCRIPT_PORT", "")
        token = os.environ.get("KLOGG_SCRIPT_TOKEN", "")
        if not port or not token:
            raise RuntimeError("klogg scripting environment is not initialized")

        self._token = token
        self._sock = socket.create_connection(("127.0.0.1", int(port)))
        self._reader = self._sock.makefile("r", encoding="utf-8", newline="\n")
        self._next_id = 1
        self._script_args = _load_script_args()
        self._write_lock = threading.Lock()
        self._response_condition = threading.Condition()
        self._event_condition = threading.Condition()
        self._pending: Dict[int, Optional[Dict[str, Any]]] = {}
        self._event_queue: Deque[Dict[str, Any]] = deque()
        self._closed = False
        self._local_stop_requested = False
        self._dropped_events = 0
        self._receive_handlers: Dict[str, List[Callable[[ReceiveEvent], Any]]] = {}
        self._response_handlers: Dict[str, List[Tuple[Callable[[ResponseEvent], Any], Optional[int], str]]] = {}
        self._reader_thread = threading.Thread(target=self._reader_loop, name="klogg-rpc-reader", daemon=True)
        self._reader_thread.start()

    @property
    def script_args(self) -> Any:
        return self._script_args

    def _send_message(self, payload: Dict[str, Any]) -> None:
        raw = json.dumps(payload, separators=(",", ":"))
        with self._write_lock:
            self._sock.sendall(raw.encode("utf-8") + b"\n")

    def _send_notification(self, method: str, params: Optional[Dict[str, Any]] = None) -> None:
        try:
            self._send_message(
                {
                    "token": self._token,
                    "method": method,
                    "params": params or {},
                    "notification": True,
                }
            )
        except OSError:
            pass

    def _reader_loop(self) -> None:
        try:
            while True:
                line = self._reader.readline()
                if not line:
                    break

                payload = json.loads(line)
                if "id" in payload:
                    request_id = int(payload["id"])
                    with self._response_condition:
                        self._pending[request_id] = payload
                        self._response_condition.notify_all()
                    continue

                if payload.get("type") == "event":
                    self._queue_event(payload.get("event", {}))
        finally:
            self._closed = True
            with self._response_condition:
                self._response_condition.notify_all()
            with self._event_condition:
                self._event_condition.notify_all()

    def _queue_event(self, event: Dict[str, Any]) -> None:
        with self._event_condition:
            if len(self._event_queue) >= self.MAX_EVENT_QUEUE:
                self._event_queue.popleft()
                self._dropped_events += 1
                self._send_notification("report_event_stats", {"droppedEvents": self._dropped_events})
            self._event_queue.append(event)
            self._event_condition.notify_all()

    def request(self, method: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        request_id = self._next_id
        self._next_id += 1
        with self._response_condition:
            self._pending[request_id] = None

        self._send_message(
            {
                "id": request_id,
                "token": self._token,
                "method": method,
                "params": params or {},
            }
        )

        with self._response_condition:
            while self._pending.get(request_id) is None and not self._closed:
                self._response_condition.wait()
            response = self._pending.pop(request_id, None)

        if response is None:
            raise RuntimeError("klogg RPC connection closed")
        if not response.get("ok", False):
            raise RuntimeError(response.get("error", "unknown RPC error"))
        return response.get("result", {})

    def command(self, action: str, **request: Any) -> Dict[str, Any]:
        result = self.request("command", {"action": action, **request})
        code = result.get("code", "ExecutionFailed")
        if code != "Success":
            raise KloggError(code, result.get("message", ""), result.get("payload", {}))
        return result.get("payload", {})

    def is_stop_requested(self) -> bool:
        if self._closed:
            return True
        result = self.request("is_stop_requested")
        return bool(result.get("stopRequested", False))

    def subscribe_event(
        self,
        selector: Dict[str, Any],
        event_type: str,
        *,
        response_id: Optional[int] = None,
        response_name: Optional[str] = None,
    ) -> Dict[str, Any]:
        payload = dict(selector)
        payload["eventType"] = event_type
        if response_id is not None:
            payload["responseId"] = int(response_id)
        if response_name:
            payload["responseName"] = response_name
        return self.request("subscribe_event", payload)

    def clear_event_handlers(self, selector: Dict[str, Any]) -> None:
        self.request("clear_event_handlers", dict(selector))

    def register_receive_handler(self, tab_id: str, handler: Callable[[ReceiveEvent], Any]) -> None:
        self._receive_handlers.setdefault(tab_id, []).append(handler)

    def register_response_handler(
        self,
        tab_id: str,
        handler: Callable[[ResponseEvent], Any],
        *,
        response_id: Optional[int] = None,
        response_name: str = "",
    ) -> None:
        self._response_handlers.setdefault(tab_id, []).append((handler, response_id, response_name))

    def clear_local_handlers(self, tab_id: str) -> None:
        self._receive_handlers.pop(tab_id, None)
        self._response_handlers.pop(tab_id, None)

    def run(self) -> None:
        self._local_stop_requested = False
        self._send_notification("set_dispatch_state", {"state": "dispatching"})
        try:
            while not self._local_stop_requested:
                with self._event_condition:
                    while not self._event_queue and not self._closed and not self._local_stop_requested:
                        self._event_condition.wait(timeout=0.2)

                    if self._local_stop_requested:
                        break

                    if self._event_queue:
                        event = self._event_queue.popleft()
                    elif self._closed:
                        break
                    else:
                        continue

                self._dispatch_event(event)
        finally:
            self._send_notification("set_dispatch_state", {"state": "idle"})
            self._local_stop_requested = False

    def stop(self) -> None:
        self._local_stop_requested = True
        with self._event_condition:
            self._event_condition.notify_all()

    def _dispatch_event(self, payload: Dict[str, Any]) -> None:
        event_type = payload.get("eventType", "")
        tab_id = payload.get("tabId", "")
        if event_type == "receive":
            event = ReceiveEvent(
                tab_id=tab_id,
                window_index=int(payload.get("windowIndex", -1)),
                tab_index=int(payload.get("tabIndex", -1)),
                file_path=payload.get("filePath", ""),
                port_name=payload.get("portName", ""),
                text=payload.get("text", ""),
                raw_bytes=base64.b64decode(payload.get("rawBase64", "") or b""),
                hex_string=payload.get("hexString", ""),
                timestamp=payload.get("timestamp", ""),
                display_name=payload.get("displayName", ""),
                window_id=payload.get("windowId", ""),
            )
            handlers = list(self._receive_handlers.get(tab_id, []))
            for handler in handlers:
                self._invoke_handler(handler, event)
            return

        if event_type == "response":
            event = ResponseEvent(
                tab_id=tab_id,
                window_index=int(payload.get("windowIndex", -1)),
                tab_index=int(payload.get("tabIndex", -1)),
                file_path=payload.get("filePath", ""),
                port_name=payload.get("portName", ""),
                matched_text=payload.get("matchedText", ""),
                raw_bytes=base64.b64decode(payload.get("rawBase64", "") or b""),
                hex_string=payload.get("hexString", ""),
                timestamp=payload.get("timestamp", ""),
                response_id=int(payload.get("responseId", 0)),
                response_name=payload.get("responseName", ""),
                counter=int(payload.get("counter", 0)),
                display_name=payload.get("displayName", ""),
                window_id=payload.get("windowId", ""),
            )
            handlers = list(self._response_handlers.get(tab_id, []))
            for handler, expected_id, expected_name in handlers:
                if expected_id is not None and expected_id != event.response_id:
                    continue
                if expected_name and expected_name.lower() != event.response_name.lower():
                    continue
                self._invoke_handler(handler, event)

    def _invoke_handler(self, handler: Callable[[Any], Any], event: Any) -> None:
        try:
            handler(event)
        except BaseException:
            self._send_notification("report_callback_error", {"error": traceback.format_exc().strip()})
            raise


_CLIENT: Optional[_RpcClient] = None


def _get_client() -> _RpcClient:
    global _CLIENT
    if _CLIENT is None:
        _CLIENT = _RpcClient()
    return _CLIENT


class TabRef:
    def __init__(self, tab_id: Optional[str] = None, window_index: Optional[int] = None,
                 tab_index: Optional[int] = None) -> None:
        self._selector: Dict[str, Any] = {}
        if tab_id:
            self._selector["tabId"] = tab_id
        if window_index is not None:
            self._selector["windowIndex"] = int(window_index)
        if tab_index is not None:
            self._selector["tabIndex"] = int(tab_index)

    @classmethod
    def from_info(cls, tab_info: Dict[str, Any], window_index: int) -> "TabRef":
        return cls(tab_id=tab_info.get("tabId"),
                   window_index=window_index,
                   tab_index=tab_info.get("tabIndex"))

    def _update_selector(self, info: Dict[str, Any]) -> None:
        tab_id = info.get("tabId")
        if tab_id:
            self._selector["tabId"] = tab_id
        if info.get("windowIndex") is not None:
            self._selector["windowIndex"] = int(info["windowIndex"])
        if info.get("tabIndex") is not None:
            self._selector["tabIndex"] = int(info["tabIndex"])

    def _ensure_tab_id(self) -> str:
        tab_id = self._selector.get("tabId", "")
        if tab_id:
            return tab_id
        info = self.info()
        self._update_selector(info)
        tab_id = self._selector.get("tabId", "")
        if not tab_id:
            raise KloggError("NotFound", "Tab was not found", {})
        return str(tab_id)

    def info(self) -> Dict[str, Any]:
        info = _get_client().command("get_info")
        for window in info.get("windows", []):
            for tab in window.get("tabs", []):
                if self._selector.get("tabId") and tab.get("tabId") == self._selector["tabId"]:
                    return tab
                if (
                    self._selector.get("windowIndex") == window.get("windowIndex")
                    and self._selector.get("tabIndex") == tab.get("tabIndex")
                ):
                    return tab
        raise KloggError("NotFound", "Tab was not found", {})

    def _command(self, action: str, **extra: Any) -> Dict[str, Any]:
        payload = dict(self._selector)
        payload.update(extra)
        return _get_client().command(action, **payload)

    def send_action(self, action_id: int) -> Dict[str, Any]:
        return self._command("send_action", entityId=int(action_id))

    def wait_response(self, *, response_id: Optional[int] = None, name: Optional[str] = None,
                      timeout_ms: int = 1000) -> Dict[str, Any]:
        request: Dict[str, Any] = {"timeoutMs": int(timeout_ms)}
        if response_id is not None:
            request["entityId"] = int(response_id)
        if name:
            request["entityName"] = name
        return self._command("wait_response", **request)

    def start_comm(self) -> Dict[str, Any]:
        return self._command("start_comm")

    def stop_comm(self) -> Dict[str, Any]:
        return self._command("stop_comm")

    def get_comm_status(self) -> Dict[str, Any]:
        return self._command("get_comm_status")

    def start_logging(self) -> Dict[str, Any]:
        return self._command("start_logging")

    def stop_logging(self) -> Dict[str, Any]:
        return self._command("stop_logging")

    def add_comment(self, text: str, *, timestamp: bool = False) -> Dict[str, Any]:
        return self._command("add_comment", commentText=text, timestampComment=bool(timestamp))

    def get_response_counter(self, *, response_id: Optional[int] = None,
                             name: Optional[str] = None, all: bool = False) -> Dict[str, Any]:
        payload: Dict[str, Any] = {}
        if response_id is not None:
            payload["entityId"] = int(response_id)
        if name:
            payload["entityName"] = name
        if all:
            payload["allEntities"] = True
        return self._command("get_response_counter", **payload)

    def reset_response_counter(self, *, response_id: Optional[int] = None,
                               name: Optional[str] = None, all: bool = False) -> Dict[str, Any]:
        payload: Dict[str, Any] = {}
        if response_id is not None:
            payload["entityId"] = int(response_id)
        if name:
            payload["entityName"] = name
        if all:
            payload["allEntities"] = True
        return self._command("reset_response_counter", **payload)

    def get_filters(self, *, predefined: bool = False) -> Dict[str, Any]:
        return self._command("get_filters", predefinedFilters=bool(predefined))

    def set_filter(self, *, filter_id: Optional[str] = None, filter_index: Optional[int] = None,
                   filter_string: Optional[str] = None, predefined: bool = False,
                   search: bool = False, auto_refresh: bool = False) -> Dict[str, Any]:
        payload: Dict[str, Any] = {
            "predefinedFilters": bool(predefined),
            "runSearch": bool(search or auto_refresh),
            "rearmAutoRefresh": bool(auto_refresh),
        }
        if filter_id:
            payload["filterId"] = filter_id
        if filter_index is not None:
            payload["filterIndex"] = int(filter_index)
        if filter_string:
            payload["filterString"] = filter_string
        return self._command("set_filter", **payload)

    def clear_comm(self) -> Dict[str, Any]:
        return self._command("clear_comm")

    def on_receive(self, handler: Callable[[ReceiveEvent], Any]) -> None:
        resolved = _get_client().subscribe_event(self._selector, "receive")
        self._update_selector(resolved)
        _get_client().register_receive_handler(str(resolved.get("tabId", "")), handler)

    def on_response(self, handler: Callable[[ResponseEvent], Any], *,
                    response_id: Optional[int] = None, name: Optional[str] = None) -> None:
        resolved = _get_client().subscribe_event(
            self._selector, "response", response_id=response_id, response_name=name
        )
        self._update_selector(resolved)
        _get_client().register_response_handler(
            str(resolved.get("tabId", "")), handler, response_id=response_id, response_name=name or ""
        )

    def clear_event_handlers(self) -> None:
        _get_client().clear_event_handlers(self._selector)
        _get_client().clear_local_handlers(self._ensure_tab_id())


class Application:
    def get_info(self) -> Dict[str, Any]:
        return _get_client().command("get_info")

    def get_actions(self) -> List[Dict[str, Any]]:
        return _get_client().command("get_actions").get("actions", [])

    def get_responses(self) -> List[Dict[str, Any]]:
        return _get_client().command("get_responses").get("responses", [])

    def tabs(self) -> List[TabRef]:
        result: List[TabRef] = []
        info = self.get_info()
        for window in info.get("windows", []):
            window_index = window.get("windowIndex")
            for tab in window.get("tabs", []):
                result.append(TabRef.from_info(tab, window_index))
        return result

    def current_tab(self) -> TabRef:
        info = self.get_info()
        for window in info.get("windows", []):
            if not window.get("isActiveWindow", False):
                continue
            current_tab_id = window.get("currentTabId")
            current_tab_index = window.get("currentTabIndex")
            if current_tab_id:
                return TabRef(tab_id=current_tab_id)
            if current_tab_index is not None:
                return TabRef(window_index=window.get("windowIndex"), tab_index=current_tab_index)
        raise KloggError("NotFound", "No active tab was found", {})

    def tab(self, *, tab_id: Optional[str] = None,
            window_index: Optional[int] = None, tab_index: Optional[int] = None) -> TabRef:
        return TabRef(tab_id=tab_id, window_index=window_index, tab_index=tab_index)

    def create_action(self, definition: Dict[str, Any]) -> Dict[str, Any]:
        return _get_client().command("create_action", definitionPayload=definition)

    def update_action(self, action_id: int, definition: Dict[str, Any]) -> Dict[str, Any]:
        return _get_client().command("update_action", entityId=int(action_id),
                                     definitionPayload=definition)

    def delete_action(self, action_id: int) -> Dict[str, Any]:
        return _get_client().command("delete_action", entityId=int(action_id))

    def create_response(self, definition: Dict[str, Any]) -> Dict[str, Any]:
        return _get_client().command("create_response", definitionPayload=definition)

    def update_response(self, response_id: int, definition: Dict[str, Any]) -> Dict[str, Any]:
        return _get_client().command("update_response", entityId=int(response_id),
                                     definitionPayload=definition)

    def delete_response(self, response_id: int) -> Dict[str, Any]:
        return _get_client().command("delete_response", entityId=int(response_id))

    def run_action_crud(self, operation: str, **kwargs: Any) -> Dict[str, Any]:
        return _get_client().command(f"{operation}_action", **kwargs)

    def run_response_crud(self, operation: str, **kwargs: Any) -> Dict[str, Any]:
        return _get_client().command(f"{operation}_response", **kwargs)

    def script_args(self) -> Any:
        return _get_client().script_args


app = Application()


def sleep_ms(milliseconds: int) -> None:
    time.sleep(max(0, milliseconds) / 1000.0)


def is_stop_requested() -> bool:
    return _get_client().is_stop_requested()


def run() -> None:
    _get_client().run()


def stop() -> None:
    _get_client().stop()


def log(message: Any) -> None:
    print(f"[klogg] {message}", flush=True)
