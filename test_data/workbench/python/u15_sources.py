"""U15 parity fixture: the Regex Lab and Value Counts docks through the public klogg SDK.

Run with run_global_script. The arguments file names the output file, the dock definitions and
the sources (tab:<id>, file:<path>, focused, all); the fixture writes, per dock and source, the
rows of every job the Run started, keyed by its tab.
"""

import json
import time

from klogg import KloggError, app

TERMINAL = {"completed", "failed", "cancelled"}
DROPPED_KEYS = ("jobId", "requestRevision", "source")


def job_rows(job_id):
    rows, cursor, seen = [], "", set()
    while True:
        if cursor in seen:
            raise RuntimeError("pagination_stalled")
        seen.add(cursor)
        page = app.command("get_analysis_results", job_id=job_id, page_size=200, cursor=cursor)
        rows.extend({k: v for k, v in row.items() if k not in DROPPED_KEYS} for row in page["rows"])
        cursor = page.get("nextCursor", "")
        if not cursor:
            return rows


def run_dock(setter, getter, definition, source):
    setter(dict(definition, source=source, operation="run"), window_index=0)
    deadline = time.monotonic() + 60
    while True:
        state = app.command(getter, window_index=0)
        runs = state.get("runs", [])
        if runs and all(run["state"] in TERMINAL for run in runs):
            break
        if time.monotonic() > deadline:
            raise RuntimeError(f"dock_run_timeout: {getter} {source}")
        time.sleep(0.05)
    result = {run["tabId"]: job_rows(run["jobId"]) for run in runs}
    setter({"operation": "release"}, window_index=0)
    return result


def main():
    arguments = app.script_args()
    report = {"regexLab": {}, "valueCounts": {}, "errors": []}
    try:
        for source in arguments["sources"]:
            report["regexLab"][source] = run_dock(
                app.set_regex_lab, "get_regex_lab", arguments["regex"], source)
            report["valueCounts"][source] = run_dock(
                app.set_value_counts, "get_value_counts", arguments["counts"], source)
    except (KloggError, RuntimeError, KeyError) as error:
        report["errors"].append(str(error))
    with open(arguments["output"], "w", encoding="utf-8") as output:
        json.dump(report, output, ensure_ascii=False)


main()
