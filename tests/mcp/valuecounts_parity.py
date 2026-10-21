"""Value Counts acceptance checks using the isolated analysis-parity host."""

import csv
import json
import time


def exercise_counts(cli, command, finish, rows, normalized, artifacts):
    jobs = []
    request = {"scope": "text", "pattern": r"(?<v>a\.a|b\+b|שלום)",
               "text": "a.a b+b a.a\r\nשלום", "counts": {"capture": "v"}}
    try:
        first = cli("value_counts", "--analysis-json", json.dumps(request))["jobId"]
        second = command("value_counts", {"analysis_json": request})["jobId"]
        jobs.extend((first, second))
        for job in jobs:
            status = finish(job, True)
            assert status["complete"] and status["kind"] == "value_counts", status
            assert status["aggregation"]["countedOccurrences"] == "4", status
        expected = rows(first, False)
        assert normalized(expected) == normalized(rows(second, True))
        assert [(row["value"], row["count"]) for row in expected] == [
            ("a.a", "2"), ("b+b", "1"), ("שלום", "1")]
        assert expected[0]["percentage"] == 50 and expected[0]["filterPattern"] == r"a\.a"
        invalid = command("value_counts", {"analysis_json": dict(request, counts={"maxValues": 0})}, success=False)
        assert invalid["reason"] == "invalid_count_options", invalid
        partial = command("value_counts", {"analysis_json": dict(request, counts={"maxValues": 1})})["jobId"]
        jobs.append(partial)
        status = finish(partial, True)
        assert not status["complete"] and status["reason"] == "unique_value_limit", status
        export_path = artifacts / "partial-counts.csv"
        args = {"jobId": partial, "filePath": str(export_path)}
        assert command("export_value_counts", {"analysis_json": args}, success=False)["reason"] == "incomplete_job"
        command("export_value_counts", {"analysis_json": dict(args, allowPartial=True)})
        with export_path.open(encoding="utf-8", newline="") as source:
            exported = list(csv.DictReader(source))
        assert len(exported) == 1 and exported[0]["complete"] == "false", exported
        # An accepted numeric-looking capture name remains a string through the dock.
        named_zero = dict(request, pattern="(a)", text="a", counts={"capture": "0", "missing": "include"})
        command("show_value_counts", {"window_index": 0})
        command("set_value_counts", {"window_index": 0, "definition": named_zero})
        command("activate_ui", {"window_index": 0, "object_name": "valueCountsRun"})
        deadline = time.monotonic() + 30
        while True:
            gui = cli("get_value_counts", "--window-index", "0")
            if gui["jobId"]:
                break
            assert time.monotonic() < deadline, "GUI counts Run did not start"
            time.sleep(0.02)
        assert finish(gui["jobId"], True)["complete"]
        assert rows(gui["jobId"], True)[0]["missing"]
        previous = gui["jobId"]
        command("set_value_counts", {"window_index": 0, "definition": request})
        command("activate_ui", {"window_index": 0, "object_name": "valueCountsRun"})
        gui = cli("get_value_counts", "--window-index", "0")
        assert gui["jobId"] and gui["jobId"] != previous, gui
        assert finish(gui["jobId"], True)["complete"]
        assert normalized(rows(gui["jobId"], True)) == normalized(expected)
        command("set_value_counts", {"window_index": 0, "definition": {"pattern": "changed"}})
        assert command("set_value_counts", {"window_index": 0, "definition": {
            "operation": "apply", "rowIndex": 0}}, success=False)["reason"] == "draft_changed"
        command("set_value_counts", {"window_index": 0, "definition": {"operation": "release"}})
        # Stable tab IDs must reach the owning window even when it is not first.
        windows = cli("get_info")["windows"]
        if len(windows) > 1:
            initial = command("get_value_counts", {"window_index": 0})
            target = windows[-1]["tabs"][0]["tabId"]
            command("show_value_counts", {"tab_id": target})
            state = command("set_value_counts", {"tab_id": target, "definition": dict(request, operation="run")})
            assert state.get("jobId"), state
            assert finish(state["jobId"], True)["complete"]
            assert command("get_value_counts", {"tab_id": target})["jobId"] == state["jobId"]
            independent = command("get_value_counts", {"window_index": len(windows) - 1})
            assert independent["jobId"] == state["jobId"] and independent["sourceTabId"] == target, independent
            assert command("get_value_counts", {"window_index": 0})["jobId"] == initial["jobId"]
            command("set_value_counts", {"tab_id": target, "definition": {"operation": "release"}})
        # Round-trip CSV quoting and Unicode. These are data, including formula-looking strings.
        quoted = dict(request, pattern=r"[^\r\n]+", text='a,"b"\n=1+1\nשלום', counts={})
        quote_job = command("value_counts", {"analysis_json": quoted})["jobId"]
        jobs.append(quote_job)
        assert finish(quote_job, True)["complete"]
        path = artifacts / "quoted-counts.csv"
        command("export_value_counts", {"analysis_json": {"jobId": quote_job, "filePath": str(path)}})
        with path.open(encoding="utf-8", newline="") as source:
            exported = list(csv.DictReader(source))
        assert {row["value"] for row in exported} == {'a,"b"', '=1+1', 'שלום'}, exported
        assert len(exported) == 3 and all(row["count"] == "1" for row in exported), exported
        assert all(row["complete"] == "true" for row in exported)
        (artifacts / "value-counts-result.json").write_text(
            json.dumps({"passed": True, "rows": normalized(expected)}, ensure_ascii=False, indent=2), encoding="utf-8")
        print("PASS: Value Counts CLI/native-MCP/GUI parity, stable tab routing, exact limits, paging and CSV")
    finally:
        for job in jobs:
            command("release_analysis_job", {"job_id": job})
