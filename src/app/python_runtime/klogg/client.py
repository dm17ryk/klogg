from __future__ import annotations

import json
import os
import socket
import time
from typing import Any, Dict, List, Optional

from .exceptions import KloggError


def _load_script_args() -> Any:
    path = os.environ.get("KLOGG_SCRIPT_ARGS_JSON_FILE", "")
    if not path:
        return None
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


class _RpcClient:
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

    @property
    def script_args(self) -> Any:
        return self._script_args

    def request(self, method: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        request_id = self._next_id
        self._next_id += 1
        payload = {
            "id": request_id,
            "token": self._token,
            "method": method,
            "params": params or {},
        }
        raw = json.dumps(payload, separators=(",", ":"))
        self._sock.sendall(raw.encode("utf-8") + b"\n")
        line = self._reader.readline()
        if not line:
            raise RuntimeError("klogg RPC connection closed")
        response = json.loads(line)
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
        result = self.request("is_stop_requested")
        return bool(result.get("stopRequested", False))


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


def log(message: Any) -> None:
    print(f"[klogg] {message}", flush=True)
