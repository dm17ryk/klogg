"""Protocol test double only. Never represents application performance evidence."""

import json
import os
import sys
import time
from pathlib import Path

if "--version" in sys.argv:
    print("cilogg benchmark test double; not a product build")
    raise SystemExit(0)
if "--multi" in sys.argv:
    print("Klogg instance benchmark-test-double", file=sys.stderr, flush=True)
    while True:
        time.sleep(0.1)

jobs = {}
source = None
for line in sys.stdin:
    request = json.loads(line)
    if "id" not in request:
        continue
    if os.environ.get("CILOGG_BENCHMARK_FAKE_SHAPE"):
        shape = json.loads(os.environ["CILOGG_BENCHMARK_FAKE_SHAPE"])
        if isinstance(shape, dict):
            shape["id"] = request["id"]
        print(json.dumps(shape), flush=True)
        continue
    if os.environ.get("CILOGG_BENCHMARK_FAKE_INVALID_PROTOCOL"):
        print("invalid JSON", flush=True)
        continue
    if request["method"] == "initialize":
        result = {
            "protocolVersion": "2025-11-25",
            "capabilities": {"tools": {}},
            "serverInfo": {"name": "benchmark-test-double", "version": "1"},
        }
    else:
        parameters = request["params"]
        arguments = parameters.get("arguments", {})
        if parameters["name"] == "cilogg_commands":
            result = {
                "structuredContent": {
                    "commander": {
                        "commands": dict.fromkeys(
                            [
                                "regex_evaluate",
                                "value_counts",
                                "get_analysis_job",
                                "get_analysis_results",
                                "cancel_analysis_job",
                                "release_analysis_job",
                                "open_file",
                                "get_info",
                            ]
                        )
                    }
                }
            }
        else:
            action = arguments["action"]
            values = arguments.get("arguments", {})
            payload = {}
            if action == "open_file":
                time.sleep(
                    float(os.environ.get("CILOGG_BENCHMARK_FAKE_OPEN_DELAY", "0"))
                )
                source = Path(values["file"])
            elif action in ("regex_evaluate", "value_counts"):
                analysis = values["analysis_json"]
                inline = analysis.get("scope") == "text"
                size = source.stat().st_size if source else 0
                text = source.read_text(encoding="ascii") if source else ""
                job = str(len(jobs) + 1)
                jobs[job] = {
                    "jobId": job,
                    "state": "completed"
                    if os.environ.get("CILOGG_BENCHMARK_FAKE_ALREADY_COMPLETE")
                    else "running",
                    "complete": not inline,
                    "scannedLines": len(text.splitlines()),
                    "source": {"indexedBytes": str(size)},
                    "newerDataAvailable": False,
                    "captureBackend": "benchmark-test-double",
                    "metrics": {"compileUs": 10, "matchUs": 20},
                    "timing": {},
                    "count": str(sum(" ERROR " in row for row in text.splitlines())),
                }
                payload = {"jobId": job}
            elif action == "get_analysis_job":
                time.sleep(
                    float(os.environ.get("CILOGG_BENCHMARK_FAKE_DELAY_MS", "0")) / 1000
                )
                stored = jobs[values["job_id"]]
                if stored["state"] == "running" and stored["complete"]:
                    stored["observations"] = stored.get("observations", 0) + 1
                    if stored["observations"] >= 2:
                        stored["state"] = "completed"
                payload = dict(stored)
                payload["newerDataAvailable"] = bool(
                    source
                    and source.stat().st_size > int(payload["source"]["indexedBytes"])
                )
            elif action == "get_analysis_results":
                payload = {
                    "rows": [{"count": jobs[values["job_id"]]["count"]}],
                    "nextCursor": "",
                }
            elif action == "cancel_analysis_job":
                jobs[values["job_id"]].update(state="cancelled", complete=False)
            elif action == "close_cilogg":
                payload = {"closing": True}
            result = {"structuredContent": {"payload": payload}, "isError": False}
    print(
        json.dumps({"jsonrpc": "2.0", "id": request["id"], "result": result}),
        flush=True,
    )
    if (
        os.environ.get("CILOGG_BENCHMARK_FAKE_STOP_READING")
        and request["method"] == "tools/call"
        and request["params"].get("arguments", {}).get("action")
        == "release_analysis_job"
        and request["params"]["arguments"]["arguments"].get("job_id") == "2"
    ):
        while True:
            time.sleep(1)
