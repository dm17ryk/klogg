"""Synthetic reports test the harness; they never establish native GUI usability."""

import base64
import hashlib
import json
import os
import sys
import time
from pathlib import Path

if sys.stdin.readline() != "start\n":
    sys.exit(8)
mode = os.environ.get("CILOGG_FAKE_USABILITY", "normal")
if mode == "stall":
    time.sleep(30)
if mode == "crash":
    print("fixture crash", file=sys.stderr)
    sys.exit(7)
path = Path(os.environ["CILOGG_G03_UI_REPORT"])
png = base64.b64decode(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a7WQAAAAASUVORK5CYII="
)
cases = []
mainwindow = "[workbench-mainwindow]" in sys.argv
for name in ("regex-lab", "value-counts"):
    views = {}
    for state in (
        ("docked", "floating", "restored") if mainwindow else ("docked", "floating")
    ):
        filename = f"{name}-{state}.png"
        (path.parent / filename).write_bytes(png)
        views[state] = dict(
            visible=True,
            exposed=True,
            paints=1,
            sampledColors=2,
            screenshot=filename,
            sha256=hashlib.sha256(png).hexdigest(),
            width=1,
            height=1,
        )
    cases.append(
        dict(
            id=name,
            views=views,
            patternKeys=True,
            textInputMethod=True,
            runClicked=True,
            resultClicked=True,
            applyClicked=True,
            draftInvalidatesApply=True,
            complete=True,
            rows=["1", "2", "3"] if name == "regex-lab" else [["1", "2"], ["2", "1"]],
        )
    )
    if mainwindow:
        cases[-1].update(
            menuClicked=True,
            toolbarClicked=True,
            navigationVerified=True,
            layoutRestored=True,
            draftsRuntimeOnly=True,
            navigationLine=2 if name == "value-counts" else 3,
            source=dict(
                type="file",
                sourceId="fixture-source",
                generation="1",
                revision="1",
                indexedBytes=str(len("שלום 😀1\nsecond2\nthird1".encode("utf-8"))),
            ),
        )
        cases[-1]["expectedSource"] = dict(cases[-1]["source"])
        cases[-1]["requestRevision"] = "7"
        if name == "regex-lab":
            cases[-1]["unicodeMatch"] = dict(
                startUtf16=7,
                lengthUtf16=1,
                groups=[
                    dict(
                        number=i,
                        name="value" if i else "",
                        matched=True,
                        value="1",
                        startUtf16=7,
                        lengthUtf16=1,
                    )
                    for i in (0, 1)
                ],
            )
report = dict(
    schemaVersion=1,
    platform="windows",
    qt=dict(compile="fixture", runtime="fixture"),
    workerBinarySha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    sourceFixtureSha256=hashlib.sha256(
        "שלום 😀1\nsecond2\nthird1".encode()
    ).hexdigest(),
    ownedJobsReleased=True,
    cases=cases,
)
first = cases[0]
if mainwindow:
    report["probeKind"] = "mainwindow"
if mode == "component_report":
    report.pop("probeKind", None)
for fault, key in (
    ("no_menu", "menuClicked"),
    ("no_toolbar", "toolbarClicked"),
    ("no_navigation", "navigationVerified"),
    ("no_layout_restore", "layoutRestored"),
    ("draft_persisted", "draftsRuntimeOnly"),
):
    if mode == fault:
        first[key] = False
if mode == "no_source_id":
    first["source"].pop("sourceId")
if mode == "wrong_watermark":
    first["source"]["indexedBytes"] = "0"
if mode == "wrong_navigation_line":
    first["navigationLine"] = 2
if mode == "missing_restored_png":
    (path.parent / "regex-lab-restored.png").unlink()
if mode == "wrong_utf16":
    first["unicodeMatch"]["startUtf16"] = 13
if mode == "wrong_snapshot":
    first["expectedSource"]["sourceId"] = "different-source"
if mode == "missing_revision":
    first.pop("requestRevision")
if mode == "offscreen":
    report["platform"] = "offscreen"
if mode == "hidden":
    first["views"]["docked"]["visible"] = False
if mode == "unpainted":
    first["views"]["docked"]["paints"] = 0
if mode == "blank":
    first["views"]["docked"]["sampledColors"] = 1
if mode == "wrong_results":
    first["rows"] = ["1"]
if mode == "not_clicked":
    first["runClicked"] = False
if mode == "missing_case":
    report["cases"] = cases[:1]
if mode == "duplicate_case":
    report["cases"] = [first, first]
if mode == "worker_mismatch":
    report["workerBinarySha256"] = "0" * 64
if mode == "unreleased":
    report["ownedJobsReleased"] = False
if mode == "missing_png":
    (path.parent / "regex-lab-docked.png").unlink()
if mode == "hash_mismatch":
    first["views"]["docked"]["sha256"] = "0" * 64
if mode == "unsafe_path":
    first["views"]["docked"]["screenshot"] = "../outside.png"
if mode == "wrong_dimensions":
    first["views"]["docked"]["width"] = 2
if mode == "nonfinite":
    report["extension"] = float("nan")
if mode == "invalid_schema":
    report["schemaVersion"] = True
if mode == "huge_report":
    report["extension"] = "x" * (1024 * 1024)
if mode != "missing_report":
    path.write_text(json.dumps(report), encoding="utf-8")
