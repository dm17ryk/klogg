from __future__ import annotations

import json
import os
import runpy
import sys
import traceback
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List

from klogg.client import _get_client, _set_script_args
from klogg.test import (
    ScenarioSkip,
    _begin_scenario_run,
    _finalize_failure,
    _finalize_skip,
    _finalize_success,
)


def _utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def _load_json_file(path: str) -> Any:
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def _load_device_bindings() -> Dict[str, Any]:
    raw = os.environ.get("KLOGG_SCENARIO_DEVICE_BINDINGS_JSON", "").strip()
    if not raw:
        return {}
    try:
        payload = json.loads(raw)
    except Exception:
        return {}
    return payload if isinstance(payload, dict) else {}


def _resolve_relative(base_dir: Path, value: str) -> str:
    path = Path(value)
    if not path.is_absolute():
        path = base_dir / path
    return str(path.resolve())


def _notify_status(**kwargs: Any) -> None:
    _get_client()._send_notification("report_scenario_status", kwargs)


def _notify_report(payload: Dict[str, Any]) -> None:
    _get_client()._send_notification("report_scenario_report", payload)


def _scenario_entry(file_path: str, args_json_file: str = "", enabled: bool = True) -> Dict[str, Any]:
    return {
        "scenarioFile": file_path,
        "argsJsonFile": args_json_file,
        "enabled": enabled,
    }


def _run_scenario_file(file_path: str, args_json_file: str = "") -> Dict[str, Any]:
    scenario_path = Path(file_path).resolve()
    scenario_name = scenario_path.stem

    if args_json_file:
        os.environ["KLOGG_SCRIPT_ARGS_JSON_FILE"] = args_json_file
        _set_script_args(_load_json_file(args_json_file))
    else:
        os.environ.pop("KLOGG_SCRIPT_ARGS_JSON_FILE", None)
        _set_script_args(None)

    _begin_scenario_run(scenario_name, str(scenario_path), args_json_file)

    try:
        runpy.run_path(str(scenario_path), run_name="__main__")
        return _finalize_success()
    except ScenarioSkip as exc:
        return _finalize_skip(str(exc))
    except SystemExit as exc:
        if isinstance(exc.code, int) and exc.code == 0:
            return _finalize_success()
        return _finalize_failure(f"Scenario exited with code {exc.code!r}.")
    except BaseException as exc:
        return _finalize_failure(str(exc), traceback.format_exc())


def _disabled_result(file_path: str, args_json_file: str = "") -> Dict[str, Any]:
    scenario_path = Path(file_path).resolve()
    _begin_scenario_run(scenario_path.stem, str(scenario_path), args_json_file)
    return _finalize_skip("Disabled in suite")


def _summarize_report(suite_name: str, suite_id: str, scenarios: List[Dict[str, Any]]) -> Dict[str, Any]:
    counts = {
        "total": len(scenarios),
        "passed": sum(1 for item in scenarios if item.get("status") == "passed"),
        "failed": sum(1 for item in scenarios if item.get("status") == "failed"),
        "skipped": sum(1 for item in scenarios if item.get("status") == "skipped"),
        "cancelled": sum(1 for item in scenarios if item.get("status") == "cancelled"),
    }
    status = "passed"
    if counts["failed"] > 0:
        status = "failed"
    elif counts["passed"] == 0 and counts["skipped"] > 0:
        status = "skipped"

    return {
        "suiteId": suite_id,
        "suiteName": suite_name,
        "status": status,
        "startedAt": scenarios[0]["startedAt"] if scenarios else _utc_now(),
        "finishedAt": scenarios[-1]["finishedAt"] if scenarios else _utc_now(),
        "counts": counts,
        "scenarios": scenarios,
        "deviceBindings": _load_device_bindings(),
    }


def _write_json_report(path: str, report: Dict[str, Any]) -> None:
    if not path:
        return
    Path(path).write_text(json.dumps(report, indent=2), encoding="utf-8")


def _write_junit_report(path: str, report: Dict[str, Any]) -> None:
    if not path:
        return

    testsuite = ET.Element(
        "testsuite",
        {
            "name": report.get("suiteName", ""),
            "tests": str(report.get("counts", {}).get("total", 0)),
            "failures": str(report.get("counts", {}).get("failed", 0)),
            "skipped": str(report.get("counts", {}).get("skipped", 0)),
        },
    )

    for scenario in report.get("scenarios", []):
        testcase = ET.SubElement(
            testsuite,
            "testcase",
            {
                "name": scenario.get("name", ""),
                "classname": report.get("suiteName", ""),
                "file": scenario.get("file", ""),
            },
        )
        if scenario.get("status") == "failed":
            failure = ET.SubElement(
                testcase,
                "failure",
                {"message": scenario.get("error", "Scenario failed")},
            )
            failure.text = scenario.get("traceback", "") or scenario.get("error", "")
        elif scenario.get("status") == "skipped":
            skipped = ET.SubElement(
                testcase,
                "skipped",
                {"message": scenario.get("error", "Scenario skipped")},
            )
            skipped.text = scenario.get("error", "")

    tree = ET.ElementTree(testsuite)
    tree.write(path, encoding="utf-8", xml_declaration=True)


def _run_single_scenario() -> Dict[str, Any]:
    scenario_file = os.environ.get("KLOGG_SCENARIO_FILE", "")
    if not scenario_file:
        raise RuntimeError("Missing KLOGG_SCENARIO_FILE")
    args_json_file = os.environ.get("KLOGG_SCENARIO_ARGS_JSON_FILE", "")
    result = _run_scenario_file(scenario_file, args_json_file)
    return _summarize_report(Path(scenario_file).stem, "single-scenario", [result])


def _run_suite() -> Dict[str, Any]:
    suite_file = os.environ.get("KLOGG_SUITE_FILE", "")
    if not suite_file:
        raise RuntimeError("Missing KLOGG_SUITE_FILE")

    suite_path = Path(suite_file).resolve()
    suite_document = _load_json_file(str(suite_path))
    suite_name = suite_document.get("name") or suite_path.stem
    suite_id = suite_document.get("suiteId") or suite_path.stem
    entries = suite_document.get("scenarios", [])
    base_dir = suite_path.parent
    scenarios: List[Dict[str, Any]] = []

    total = len(entries)
    _notify_status(suiteName=suite_name, suiteId=suite_id, totalScenarios=total)

    for index, entry in enumerate(entries, start=1):
        scenario_file = _resolve_relative(base_dir, entry.get("scenarioFile", ""))
        args_json_file = entry.get("argsJsonFile", "")
        if args_json_file:
            args_json_file = _resolve_relative(base_dir, args_json_file)

        _notify_status(
            suiteName=suite_name,
            suiteId=suite_id,
            totalScenarios=total,
            completedScenarios=len(scenarios),
            currentScenarioFile=scenario_file,
            currentScenarioName=Path(scenario_file).stem,
        )

        if not entry.get("enabled", True):
            result = _disabled_result(scenario_file, args_json_file)
        else:
            result = _run_scenario_file(scenario_file, args_json_file)
        scenarios.append(result)

        counts = _summarize_report(suite_name, suite_id, scenarios)["counts"]
        _notify_status(
            suiteName=suite_name,
            suiteId=suite_id,
            totalScenarios=total,
            completedScenarios=len(scenarios),
            passedCount=counts["passed"],
            failedCount=counts["failed"],
            skippedCount=counts["skipped"],
            currentScenarioFile="" if index == total else "",
            currentScenarioName="" if index == total else "",
            currentStepName="",
        )

    return _summarize_report(suite_name, suite_id, scenarios)


def main() -> int:
    report_json = os.environ.get("KLOGG_SCENARIO_REPORT_JSON", "")
    report_junit = os.environ.get("KLOGG_SCENARIO_REPORT_JUNIT", "")

    try:
        report = _run_suite() if os.environ.get("KLOGG_SUITE_FILE", "") else _run_single_scenario()
        report["reportJsonFile"] = report_json
        report["reportJunitFile"] = report_junit
        _write_json_report(report_json, report)
        _write_junit_report(report_junit, report)
        _notify_report(report)
        return 0 if report.get("status") != "failed" else 1
    except BaseException as exc:
        traceback_text = traceback.format_exc()
        report = {
            "suiteId": "scenario-run",
            "suiteName": "Scenario Run",
            "status": "failed",
            "startedAt": _utc_now(),
            "finishedAt": _utc_now(),
            "counts": {"total": 0, "passed": 0, "failed": 1, "skipped": 0, "cancelled": 0},
            "scenarios": [],
            "error": str(exc),
            "traceback": traceback_text,
            "deviceBindings": _load_device_bindings(),
            "reportJsonFile": report_json,
            "reportJunitFile": report_junit,
        }
        _write_json_report(report_json, report)
        _write_junit_report(report_junit, report)
        _notify_report(report)
        print(traceback_text, file=sys.stderr, flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
