"""Opt-in native dock interaction probe; no user's CILogg instance is attached."""

import argparse
import hashlib
import json
import logging
import os
import shutil
import struct
import subprocess
import sys
import uuid
from contextlib import ExitStack
from pathlib import Path

from parity_payload import prepare_payload, private_ipc_environment
from parity_process import ProcessOwner
from workbench_contracts import ParityError, compare_json

LOG = logging.getLogger("workbench.usability")
SOURCE = "שלום 😀1\nsecond2\nthird1"
ORACLES = {"regex-lab": ["1", "2", "3"], "value-counts": [["1", "2"], ["2", "1"]]}
NATIVE_PLATFORMS = {"windows", "cocoa", "xcb", "wayland", "wayland-egl"}


def file_hash(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_report(path, artifacts, worker_hash, synthetic, probe_kind="docks"):
    if path.is_symlink() or not path.is_file() or path.stat().st_size > 1024 * 1024:
        raise ParityError("native_report_missing_or_oversized")
    data = json.loads(path.read_text(encoding="utf-8"))
    # Reject nonfinite extension fields before retaining the complete report.
    json.dumps(data, allow_nan=False)
    if not isinstance(data, dict) or type(data.get("schemaVersion")) is not int:
        raise ParityError("invalid_native_schema")
    if data["schemaVersion"] != 1 or data.get("platform") not in NATIVE_PLATFORMS:
        raise ParityError("native_platform_required")
    if data.get("probeKind", "docks") != probe_kind:
        raise ParityError("probe_kind_mismatch")
    if data.get("workerBinarySha256") != worker_hash:
        raise ParityError("worker_binary_mismatch")
    if data.get("sourceFixtureSha256") != hashlib.sha256(SOURCE.encode()).hexdigest():
        raise ParityError("source_fixture_mismatch")
    if data.get("ownedJobsReleased") is not True:
        raise ParityError("jobs_not_released")
    qt = data.get("qt")
    if not isinstance(qt, dict) or any(
        not isinstance(qt.get(key), str) or not 1 <= len(qt[key]) <= 256
        for key in ("compile", "runtime")
    ):
        raise ParityError("qt_identity_missing")
    cases = data.get("cases")
    if not isinstance(cases, list) or len(cases) != 2:
        raise ParityError("native_case_missing")
    if any(not isinstance(case, dict) for case in cases):
        raise ParityError("invalid_native_case")
    if sorted(case.get("id", "") for case in cases) != sorted(ORACLES):
        raise ParityError("native_case_missing_or_duplicate")
    for case in cases:
        name = case["id"]
        for key in (
            "patternKeys",
            "textInputMethod",
            "runClicked",
            "resultClicked",
            "applyClicked",
            "draftInvalidatesApply",
            "complete",
        ):
            if case.get(key) is not True:
                raise ParityError(f"interaction_incomplete: {name}/{key}")
        compare_json(ORACLES[name], case.get("rows"), "$.rows")
        if probe_kind == "mainwindow":
            for key in (
                "menuClicked",
                "toolbarClicked",
                "navigationVerified",
                "layoutRestored",
                "draftsRuntimeOnly",
            ):
                if case.get(key) is not True:
                    raise ParityError(f"mainwindow_incomplete: {name}/{key}")
            source = case.get("source")
            if (
                not isinstance(source, dict)
                or source.get("type") != "file"
                or not isinstance(source.get("sourceId"), str)
                or not 1 <= len(source["sourceId"]) <= 256
                or source.get("indexedBytes") != str(len(SOURCE.encode("utf-8")))
                or any(
                    not isinstance(source.get(key), str) or not source[key].isdigit()
                    for key in ("generation", "revision")
                )
            ):
                raise ParityError("mainwindow_source_mismatch")
            keys = {"type", "sourceId", "generation", "revision", "indexedBytes"}
            expected_source = case.get("expectedSource")
            if not isinstance(expected_source, dict) or set(expected_source) != keys:
                raise ParityError("mainwindow_snapshot_missing")
            compare_json(
                expected_source, {key: source[key] for key in keys}, "$.sourceSnapshot"
            )
            revision = case.get("requestRevision")
            if (
                not isinstance(revision, str)
                or not 1 <= len(revision) <= 20
                or not revision.isascii()
                or not revision.isdigit()
            ):
                raise ParityError("mainwindow_revision_missing")
            if name == "regex-lab":
                compare_json(
                    {
                        "startUtf16": 7,
                        "lengthUtf16": 1,
                        "groups": [
                            {
                                "number": number,
                                "name": "value" if number else "",
                                "matched": True,
                                "value": "1",
                                "startUtf16": 7,
                                "lengthUtf16": 1,
                            }
                            for number in (0, 1)
                        ],
                    },
                    case.get("unicodeMatch"),
                    "$.unicodeMatch",
                )
            if type(case.get("navigationLine")) is not int or case[
                "navigationLine"
            ] != (2 if name == "value-counts" else 3):
                raise ParityError("mainwindow_navigation_mismatch")
        views = case.get("views")
        expected_views = {"docked", "floating"}
        if probe_kind == "mainwindow":
            expected_views.add("restored")
        if not isinstance(views, dict) or set(views) != expected_views:
            raise ParityError("native_view_missing")
        for state, view in views.items():
            if not isinstance(view, dict):
                raise ParityError("invalid_native_view")
            if view.get("visible") is not True or view.get("exposed") is not True:
                raise ParityError("native_view_hidden")
            if (
                type(view.get("paints")) is not int
                or not 1 <= view["paints"] <= 1000000
            ):
                raise ParityError("native_view_unpainted")
            if (
                type(view.get("sampledColors")) is not int
                or not 2 <= view["sampledColors"] <= 4096
            ):
                raise ParityError("native_capture_blank")
            filename = f"{name}-{state}.png"
            if view.get("screenshot") != filename:
                raise ParityError("unsafe_screenshot_path")
            screenshot = artifacts / filename
            if (
                screenshot.is_symlink()
                or not screenshot.is_file()
                or screenshot.resolve().parent != artifacts.resolve()
                or not 24 <= screenshot.stat().st_size <= 8 * 1024 * 1024
            ):
                raise ParityError("screenshot_missing_or_oversized")
            png = screenshot.read_bytes()
            if png[:8] != b"\x89PNG\r\n\x1a\n" or png[12:16] != b"IHDR":
                raise ParityError("invalid_screenshot")
            dimensions = struct.unpack(">II", png[16:24])
            for key, actual in zip(("width", "height"), dimensions):
                if (
                    type(view.get(key)) is not int
                    or view[key] != actual
                    or not (1 if synthetic else 200) <= actual <= 8192
                ):
                    raise ParityError("invalid_screenshot_dimensions")
            if hashlib.sha256(png).hexdigest() != view.get("sha256"):
                raise ParityError("screenshot_hash_mismatch")
            LOG.info(
                "view_verified case=%s state=%s paints=%d size=%s",
                name,
                state,
                view["paints"],
                dimensions,
            )
    return data


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--test-executable", required=True, type=Path)
    parser.add_argument("--artifacts", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--probe", choices=("docks", "mainwindow"), default="docks")
    options = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(name)s %(levelname)s %(message)s")
    if options.artifacts.exists() or options.artifacts.is_symlink():
        LOG.error("output_destination_exists")
        return 2
    options.artifacts.mkdir(parents=True)
    artifacts = options.artifacts.resolve()
    report = dict(
        schemaVersion=1,
        passed=False,
        state="invalid_setup",
        releaseGateStatus="pending",
        probeKind=options.probe,
        qtScaleFactorRequested=os.environ.get("QT_SCALE_FACTOR", "default"),
    )
    try:
        if not 1 <= options.timeout <= 600:
            raise ValueError("invalid_timeout")
        app = options.executable.resolve(strict=True)
        probe = options.test_executable.resolve(strict=True)
        if not app.is_file() or not probe.is_file():
            raise ValueError("invalid_executable")
        report.update(
            evidenceKind="test_double"
            if ".py" in (app.suffix, probe.suffix)
            else (
                "native_mainwindow_probe"
                if options.probe == "mainwindow"
                else "native_dock_probe"
            ),
            binarySha256=file_hash(app),
            testBinarySha256=file_hash(probe),
            limitations=[
                "Qt event injection is not physical input or a complete product usability pass.",
                (
                    "Real MainWindow in the integration executable; packaged launcher/installer conformance unmeasured."
                    if options.probe == "mainwindow"
                    else "The probe hosts real docks with Commander callbacks, not a packaged MainWindow."
                ),
            ],
        )
    except (OSError, ValueError) as error:
        report["reason"] = str(error)
        (artifacts / "result.json").write_text(
            json.dumps(report, indent=2), encoding="utf-8"
        )
        return 2
    code = 1
    try:
        payload, _ = prepare_payload(app, artifacts)
        private_probe = payload / ("cilogg-ui-" + uuid.uuid4().hex + probe.suffix)
        shutil.copy2(probe, private_probe)
        settings = artifacts / "settings"
        settings.mkdir()
        environment = dict(
            os.environ,
            CILOGG_AUTOMATION="1",
            PYTHONUTF8="1",
            CILOGG_G03_UI_REPORT=str(artifacts / "native.json"),
            APPDATA=str(settings),
            LOCALAPPDATA=str(settings),
            XDG_CONFIG_HOME=str(settings),
            XDG_DATA_HOME=str(settings),
        )
        environment.pop("QT_QPA_PLATFORM", None)
        arguments = (
            [sys.executable, str(private_probe)]
            if probe.suffix == ".py"
            else [str(private_probe)]
        ) + [
            "[workbench-mainwindow]"
            if options.probe == "mainwindow"
            else "[workbench-usability]"
        ]
        with ExitStack() as stack:
            environment = stack.enter_context(private_ipc_environment(environment))
            owner = ProcessOwner()
            # Stop readers/workers before releasing the IPC directory lease.
            stack.callback(owner.close)
            with (
                (artifacts / "stdout.log").open("wb") as stdout,
                (artifacts / "stderr.log").open("wb") as stderr,
            ):
                child = owner.spawn(
                    arguments,
                    env=environment,
                    cwd=payload,
                    stdin=subprocess.PIPE,
                    stdout=stdout,
                    stderr=stderr,
                )
                LOG.info(
                    "native_started pid=%d timeout_seconds=%s",
                    child.pid,
                    options.timeout,
                )
                child.stdin.write(b"start\n")
                child.stdin.flush()
                child.stdin.close()
                child.stdin = None
                try:
                    exit_code = child.wait(timeout=options.timeout)
                except subprocess.TimeoutExpired as error:
                    raise ParityError("native_timeout") from error
                if exit_code:
                    raise ParityError("native_process_failed")
            report["native"] = validate_report(
                artifacts / "native.json",
                artifacts,
                report["testBinarySha256"]
                if options.probe == "mainwindow"
                else report["binarySha256"],
                report["evidenceKind"] == "test_double",
                options.probe,
            )
        report.update(passed=True, state="passed")
        code = 0
    except (
        OSError,
        ValueError,
        TypeError,
        KeyError,
        RuntimeError,
        subprocess.SubprocessError,
    ) as error:
        report.update(
            state="failed",
            reason=str(error)
            if isinstance(error, ParityError)
            else type(error).__name__,
        )
        LOG.error("native_failed reason=%s", report["reason"])
    finally:
        (artifacts / "result.json").write_text(
            json.dumps(report, indent=2, allow_nan=False), encoding="utf-8"
        )
    return code


if __name__ == "__main__":
    raise SystemExit(main())
