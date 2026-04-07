from __future__ import annotations

import os
import time
from contextlib import contextmanager
from copy import deepcopy
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Any, Callable, Dict, List, Optional

from .client import TabRef, _get_client
from .exceptions import KloggError


class ScenarioFailure(AssertionError):
    pass


class ScenarioSkip(Exception):
    pass


def _utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _client_notify(method: str, params: Dict[str, Any]) -> None:
    try:
        _get_client()._send_notification(method, params)
    except Exception:
        pass


@dataclass
class _StepContext:
    name: str
    started_at: str = field(default_factory=_utc_now)
    finished_at: str = ""
    status: str = "running"
    error: str = ""
    traceback: str = ""
    artifacts: List[Dict[str, Any]] = field(default_factory=list)
    metrics: List[Dict[str, Any]] = field(default_factory=list)

    def to_dict(self) -> Dict[str, Any]:
        return {
            "name": self.name,
            "startedAt": self.started_at,
            "finishedAt": self.finished_at,
            "status": self.status,
            "error": self.error,
            "traceback": self.traceback,
            "artifacts": deepcopy(self.artifacts),
            "metrics": deepcopy(self.metrics),
        }


@dataclass
class _ScenarioContext:
    name: str
    file_path: str
    args_json_file: str = ""
    tags: List[str] = field(default_factory=list)
    started_at: str = field(default_factory=_utc_now)
    finished_at: str = ""
    status: str = "running"
    error: str = ""
    traceback: str = ""
    artifacts: List[Dict[str, Any]] = field(default_factory=list)
    metrics: List[Dict[str, Any]] = field(default_factory=list)
    steps: List[Dict[str, Any]] = field(default_factory=list)
    active_steps: List[_StepContext] = field(default_factory=list)

    def current_step(self) -> Optional[_StepContext]:
        return self.active_steps[-1] if self.active_steps else None

    def to_dict(self) -> Dict[str, Any]:
        return {
            "name": self.name,
            "file": self.file_path,
            "argsJsonFile": self.args_json_file,
            "tags": list(self.tags),
            "startedAt": self.started_at,
            "finishedAt": self.finished_at,
            "status": self.status,
            "error": self.error,
            "traceback": self.traceback,
            "artifacts": deepcopy(self.artifacts),
            "metrics": deepcopy(self.metrics),
            "steps": deepcopy(self.steps),
        }


_CURRENT: Optional[_ScenarioContext] = None
_DEVICE_BINDINGS: Optional[Dict[str, Dict[str, Any]]] = None


def _load_device_bindings() -> Dict[str, Dict[str, Any]]:
    global _DEVICE_BINDINGS
    if _DEVICE_BINDINGS is not None:
        return _DEVICE_BINDINGS

    raw = os.environ.get("KLOGG_SCENARIO_DEVICE_BINDINGS_JSON", "").strip()
    if not raw:
        _DEVICE_BINDINGS = {}
        return _DEVICE_BINDINGS

    try:
        import json

        payload = json.loads(raw)
    except Exception:
        payload = {}

    _DEVICE_BINDINGS = payload if isinstance(payload, dict) else {}
    return _DEVICE_BINDINGS


def _ensure_context() -> _ScenarioContext:
    global _CURRENT
    if _CURRENT is None:
        default_name = os.path.splitext(os.path.basename(__file__))[0]
        _CURRENT = _ScenarioContext(name=default_name, file_path=default_name)
    return _CURRENT


def _update_status(*, current_step_name: str = "") -> None:
    context = _ensure_context()
    _client_notify(
        "report_scenario_status",
        {
            "currentScenarioName": context.name,
            "currentScenarioFile": context.file_path,
            "currentStepName": current_step_name,
        },
    )


def _begin_scenario_run(default_name: str, file_path: str, args_json_file: str = "") -> None:
    global _CURRENT
    _CURRENT = _ScenarioContext(name=default_name, file_path=file_path, args_json_file=args_json_file)
    _update_status(current_step_name="")


def _finalize_success() -> Dict[str, Any]:
    context = _ensure_context()
    context.finished_at = _utc_now()
    context.status = "passed"
    _update_status(current_step_name="")
    return context.to_dict()


def _finalize_skip(message: str) -> Dict[str, Any]:
    context = _ensure_context()
    context.finished_at = _utc_now()
    context.status = "skipped"
    context.error = message
    _update_status(current_step_name="")
    return context.to_dict()


def _finalize_failure(message: str, traceback_text: str = "") -> Dict[str, Any]:
    context = _ensure_context()
    context.finished_at = _utc_now()
    context.status = "failed"
    context.error = message
    context.traceback = traceback_text
    _update_status(current_step_name="")
    return context.to_dict()


def scenario(name: str, tags: List[str] | None = None) -> None:
    context = _ensure_context()
    context.name = name
    context.tags = list(tags or [])
    _update_status(current_step_name=context.current_step().name if context.current_step() else "")


@contextmanager
def step(name: str):
    context = _ensure_context()
    step_context = _StepContext(name=name)
    context.active_steps.append(step_context)
    _update_status(current_step_name=name)
    try:
        yield
    except ScenarioSkip as exc:
        step_context.status = "skipped"
        step_context.error = str(exc)
        step_context.finished_at = _utc_now()
        context.steps.append(step_context.to_dict())
        context.active_steps.pop()
        _update_status(current_step_name=context.current_step().name if context.current_step() else "")
        raise
    except BaseException as exc:
        step_context.status = "failed"
        step_context.error = str(exc)
        step_context.finished_at = _utc_now()
        context.steps.append(step_context.to_dict())
        context.active_steps.pop()
        _update_status(current_step_name=context.current_step().name if context.current_step() else "")
        raise
    else:
        step_context.status = "passed"
        step_context.finished_at = _utc_now()
        context.steps.append(step_context.to_dict())
        context.active_steps.pop()
        _update_status(current_step_name=context.current_step().name if context.current_step() else "")


def expect(condition: Any, message: str) -> None:
    if not condition:
        raise ScenarioFailure(message)


def assert_equal(actual: Any, expected: Any, message: str = "") -> None:
    if actual != expected:
        if not message:
            message = f"Expected {expected!r} but got {actual!r}"
        raise ScenarioFailure(message)


def fail(message: str) -> None:
    raise ScenarioFailure(message)


def skip(message: str) -> None:
    raise ScenarioSkip(message)


def wait_until(
    fn: Callable[[], Any],
    timeout_ms: int,
    poll_ms: int = 50,
    message: str = "",
) -> None:
    deadline = time.monotonic() + (max(0, int(timeout_ms)) / 1000.0)
    last_error = ""
    while time.monotonic() <= deadline:
        try:
            if fn():
                return
        except Exception as exc:
            last_error = str(exc)
        time.sleep(max(1, int(poll_ms)) / 1000.0)

    timeout_message = message or "Timed out waiting for condition."
    if last_error:
        timeout_message = f"{timeout_message} Last error: {last_error}"
    raise ScenarioFailure(timeout_message)


def artifact(name: str, value_or_path: Any) -> None:
    context = _ensure_context()
    entry: Dict[str, Any]
    if isinstance(value_or_path, str) and os.path.exists(value_or_path):
        entry = {"name": name, "path": value_or_path}
    else:
        entry = {"name": name, "value": value_or_path}

    current_step = context.current_step()
    if current_step is not None:
        current_step.artifacts.append(entry)
    else:
        context.artifacts.append(entry)


def record_metric(name: str, value: Any, unit: str = "") -> None:
    context = _ensure_context()
    metric = {"name": name, "value": value, "unit": unit}
    current_step = context.current_step()
    if current_step is not None:
        current_step.metrics.append(metric)
    else:
        context.metrics.append(metric)


def devices() -> Dict[str, Dict[str, Any]]:
    return deepcopy(_load_device_bindings())


def device(name: str) -> TabRef:
    bindings = _load_device_bindings()
    binding = bindings.get(name)
    if not isinstance(binding, dict):
        raise KloggError("NotFound", f"Logical device {name!r} was not found", {})

    tab_id = binding.get("tabId")
    if tab_id:
        return TabRef(tab_id=tab_id)

    window_index = binding.get("windowIndex")
    tab_index = binding.get("tabIndex")
    if window_index is not None and tab_index is not None:
        return TabRef(window_index=int(window_index), tab_index=int(tab_index))

    raise KloggError("NotFound", f"Logical device {name!r} does not have an active tab binding", {})
