"""Synthetic native-measurement producer; never evidence of GUI performance."""

import hashlib
import json
import os
import sys
import time
from pathlib import Path

if "--version" in sys.argv:
    print("CILogg native benchmark protocol double")
    sys.exit(0)

if sys.stdin.readline() != "start\n":
    sys.exit(8)

mode = os.environ.get("FAKE_NATIVE_MODE", "normal")
if mode == "stall":
    time.sleep(30)
if mode == "crash":
    print("native fixture failure", file=sys.stderr)
    sys.exit(7)
if mode == "missing":
    sys.exit(0)

source = Path(os.environ["CILOGG_G02_SOURCE"])
count = int(os.environ["CILOGG_G02_SAMPLES"])
dispatch = 200.0 if mode == "slow" else 1.0
report = {
    "schemaVersion": 1,
    "scenario": "ui-service",
    "source": {
        "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "bytes": str(source.stat().st_size),
        "physicalLines": os.environ["CILOGG_G02_ROWS"],
    },
    "qt": {"compile": "test_double", "runtime": "test_double", "platform": "offscreen"},
    "contexts": [
        {"name": name, "samplesMs": [[dispatch, dispatch + 1.0]] * count}
        for name in ("idle", "analysis_active")
    ],
    "workerActiveDuringSamples": True,
    "cancellation": {"ackMs": 1.0, "terminalMs": 10.0, "state": "cancelled"},
    "recoveryComplete": True,
}
if mode == "wrong_source":
    report["source"]["sha256"] = "0" * 64
if mode == "short":
    report["contexts"][0]["samplesMs"].pop()
if mode == "nonfinite":
    report["contexts"][0]["samplesMs"][0] = [float("nan"), 1.0]
if mode == "inactive":
    report["workerActiveDuringSamples"] = False
if mode == "invalid_schema":
    report["schemaVersion"] = True
if mode == "huge_timing":
    report["contexts"][0]["samplesMs"][0] = [10**400, 2.0]
if mode == "extra_nonfinite":
    report["extra"] = float("nan")
if mode in ("tail_five", "tail_six"):
    tail = 5 if mode == "tail_five" else 6
    report["contexts"][0]["samplesMs"] = [[1.0, 2.0]] * (count - tail) + [[50.0, 51.0]] * tail
Path(os.environ["CILOGG_G02_REPORT"]).write_text(json.dumps(report), encoding="utf-8")
