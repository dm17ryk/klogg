"""Run registered GUI/CLI/native-MCP contracts against a private CILogg host."""

import argparse
import hashlib
import json
import logging
import subprocess
import sys
import time
import uuid
from pathlib import Path

from parity_process import ProcessOwner
from workbench_contracts import ParityError, compare_json, require_operations

LOG = logging.getLogger("workbench.parity")
REGISTRY = (
    Path(__file__).resolve().parents[2]
    / "test_data/workbench/command-cases/registry.json"
)


def select_cases(task, registry=REGISTRY):
    data = json.loads(registry.read_text(encoding="utf-8"))
    if data.get("schemaVersion") != 1:
        raise ParityError("unsupported_registry_schema")
    if task not in data["tasks"]:
        raise ParityError(f"unknown_task: {task}")
    selection = data["tasks"][task]
    if not selection:
        raise ParityError(f"empty_selection: {task}")
    cases = {case["id"]: case for case in data["cases"]}
    if len(cases) != len(data["cases"]) or len(set(selection)) != len(selection):
        raise ParityError("duplicate_case")
    for name in selection:
        if name not in cases:
            raise ParityError(f"missing_case: {name}")
    return [cases[name] for name in selection]


def supervise(arguments, artifacts, timeout, handshake=False):
    """Bound the suite, capturing diagnostics and reaping only its process tree."""
    owner = ProcessOwner()
    try:
        with (
            (artifacts / "stdout.log").open("w", encoding="utf-8") as stdout,
            (artifacts / "stderr.log").open("w", encoding="utf-8") as stderr,
        ):
            child = owner.spawn(
                arguments,
                stdin=subprocess.PIPE if handshake else subprocess.DEVNULL,
                stdout=stdout,
                stderr=stderr,
                text=True,
                encoding="utf-8",
            )
            if handshake:
                # Assignment to the Windows job precedes any application descendants.
                child.stdin.write("start\n")
                child.stdin.flush()
                child.stdin.close()
                child.stdin = None
            LOG.info("suite_started pid=%d timeout_seconds=%s", child.pid, timeout)
            try:
                code = child.wait(timeout=timeout)
            except subprocess.TimeoutExpired as error:
                raise ParityError(f"child_timeout: {timeout}s") from error
            if code:
                raise ParityError(f"child_exit: {code}; inspect stderr.log")
    finally:
        owner.close()


def exercise(cases, cli, command, finish, rows, normalized, catalog, artifacts):
    results = []
    for case in cases:
        name = case["id"]
        LOG.info("case_started case=%s action=%s", name, case["action"])
        require_operations(catalog, case["operations"])
        jobs = []
        try:
            request = case["request"]
            path = artifacts / f"{name}-request.json"
            path.write_text(json.dumps(request, ensure_ascii=False), encoding="utf-8")
            jobs.append(cli(case["action"], "--json-file", str(path))["jobId"])
            jobs.append(command(case["action"], {"analysis_json": request})["jobId"])
            for index, job in enumerate(jobs):
                status = finish(job, bool(index))
                if not status.get("complete"):
                    raise ParityError(f"incomplete_job: {name} {status.get('reason')}")
                compare_json(
                    request["requestRevision"],
                    status["requestRevision"],
                    "$.requestRevision",
                )
            first = normalized(rows(jobs[0], False))
            compare_json(first, normalized(rows(jobs[1], True)))
            if case["action"] == "value_counts":
                actual = [[row["value"], row["count"]] for row in first]
            else:
                actual = [row["lineNumber"] for row in first]
            compare_json(case["expected"], actual, "$.expected")
            gui = case.get("gui")
            presence = None
            if gui:
                command(gui["show"], {"window_index": 0})
                definition = {
                    key: value
                    for key, value in request.items()
                    if key != "requestRevision"
                }
                command(gui["set"], {"window_index": 0, "definition": definition})
                objects = command("get_ui", {"window_index": 0})["objects"]
                found = [obj for obj in objects if obj.get("objectName") == gui["run"]]
                if len(found) != 1 or not found[0].get("accessibleName"):
                    raise ParityError(f"gui_control_missing: {gui['run']}")
                presence = {
                    "objectName": gui["run"],
                    "accessibleName": found[0]["accessibleName"],
                }
                previous = command(gui["get"], {"window_index": 0})["jobId"]
                command("activate_ui", {"window_index": 0, "object_name": gui["run"]})
                deadline = time.perf_counter() + 30
                while True:
                    job = command(gui["get"], {"window_index": 0})["jobId"]
                    if job and job != previous:
                        break
                    if time.perf_counter() >= deadline:
                        raise ParityError(f"gui_run_timeout: {name}")
                    time.sleep(0.02)
                jobs.append(job)
                if not finish(job, True).get("complete"):
                    raise ParityError(f"gui_job_incomplete: {name}")
                compare_json(first, normalized(rows(job, True)))
                command(
                    gui["set"],
                    {"window_index": 0, "definition": {"operation": "release"}},
                )
                jobs.remove(job)  # The dock release already released its retained job.
            results.append(
                {"id": name, "passed": True, "rows": first, "guiControl": presence}
            )
            LOG.info(
                "case_completed case=%s rows=%d gui=%s", name, len(first), bool(gui)
            )
            (artifacts / "case-results.json").write_text(
                json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8"
            )
        finally:
            for job in jobs:
                command("release_analysis_job", {"job_id": job})
    (artifacts / "case-results.json").write_text(
        json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8"
    )


def main(arguments=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--task", required=True)
    parser.add_argument("--artifacts", required=True, type=Path)
    parser.add_argument("--backend", choices=("accelerated", "qt"))
    parser.add_argument("--timeout", type=float, default=300)
    parser.add_argument("--sdk-smoke", action="store_true", help="Also run SDK and transport checks inside the private host")
    parser.add_argument(
        "--new-run",
        action="store_true",
        help="Create a unique run directory below --artifacts (CTest reruns)",
    )
    parser.add_argument("--_child", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args(arguments)
    logging.basicConfig(level=logging.INFO, format="%(name)s %(levelname)s %(message)s")
    if args._child:
        if sys.stdin.readline() != "start\n":
            raise ParityError("supervisor_handshake_missing")
        from analysis_parity import run

        cases = select_cases(args.task)
        run(
            args.executable.resolve(),
            args.artifacts.resolve(),
            regex_lab=any(case["id"] == "regex-lab" for case in cases),
            value_counts=any(case["id"] == "value-counts" for case in cases),
            required_operations=sorted(
                {op for case in cases for op in case["operations"]}
            ),
            verify=lambda *values: exercise(cases, *values),
            sdk_smoke=args.sdk_smoke,
        )
        return 0
    root = args.artifacts.resolve()
    if args.new_run:
        root /= "run-" + uuid.uuid4().hex
    LOG.info("suite_artifacts path=%s", root)
    # Existing output is never mistaken for current evidence or overwritten.
    root.mkdir(parents=True, exist_ok=True)
    report = {
        "schemaVersion": 1,
        "passed": False,
        "task": args.task,
        "backendRequested": args.backend,
        "backendVerified": False,
        "guiMode": "offscreen controls; interactive usability unmeasured",
    }
    if (root / "result.json").exists() or (root / "native").exists():
        print("artifacts_exist: choose a fresh artifacts directory", file=sys.stderr)
        return 1
    try:
        cases = select_cases(args.task)
        if not args.executable.is_file():
            raise ParityError("executable_missing")
        if not 0 < args.timeout <= 1800:
            raise ParityError("invalid_timeout")
        report["executable"] = str(args.executable.resolve())
        digest = hashlib.sha256()
        with args.executable.open("rb") as source:
            for block in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(block)
        report["executableSha256"] = digest.hexdigest()
        native = root / "native"
        native.mkdir()
        child = [
            sys.executable,
            str(Path(__file__).resolve()),
            "--_child",
            "--executable",
            str(args.executable.resolve()),
            "--task",
            args.task,
            "--artifacts",
            str(native),
        ]
        if args.sdk_smoke:
            child.append("--sdk-smoke")
        supervise(child, root, args.timeout, handshake=True)
        results = json.loads((native / "case-results.json").read_text(encoding="utf-8"))
        compare_json(
            [case["id"] for case in cases], [result["id"] for result in results]
        )
        if not results or not all(result.get("passed") is True for result in results):
            raise ParityError("cases_not_passed")
        if args.sdk_smoke:
            sdk_reports = []
            for name in ("result.json", "runtime-result.json"):
                path = native / "sdk" / name
                if path.is_symlink() or path.stat().st_size > 65536:
                    raise ParityError("invalid_sdk_report")
                data = json.loads(path.read_text(encoding="utf-8"))
                json.dumps(data, allow_nan=False)
                if not isinstance(data, dict) or data.get("passed") is not True:
                    raise ParityError("sdk_checks_not_passed")
                sdk_reports.append(data)
            version = sdk_reports[1].get("sdkVersion")
            if not isinstance(version, str) or not version:
                raise ParityError("sdk_version_missing")
            report["sdk"] = {"version": version, "reports": sdk_reports}
        report.update(passed=True, cases=results)
    except (RuntimeError, OSError, ValueError, KeyError, TypeError) as error:
        report["reason"] = str(error)
        LOG.error("suite_failed task=%s reason=%s", args.task, error)
    (root / "result.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
