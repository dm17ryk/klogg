"""File-backed MainWindow parity through existing Commander operations."""

import json
import logging
import time
from pathlib import Path

from workbench_contracts import ParityError, compare_json

LOG = logging.getLogger("workbench.navigation")
SOURCE = "alpha10\nשלום 😀20\nomega30"
REVISION = "9007199254740993"


def wait_value(read, accept, reason):
    deadline = time.monotonic() + 15
    while True:
        value = read()
        if accept(value):
            return value
        if time.monotonic() >= deadline:
            LOG.error("navigation_wait_failed reason=%s", reason)
            raise ParityError(reason)
        time.sleep(0.02)


def exercise_navigation(cli, command, finish, rows, normalized, artifacts):
    """Use only files, jobs and tabs created inside this private parity host."""
    fixture = artifacts / "navigation-source.log"
    other = artifacts / "navigation-other.log"
    # Physical fixture bytes must be identical on Windows and POSIX.
    fixture.write_bytes(SOURCE.encode("utf-8"))
    other.write_bytes(b"other100\n")
    jobs, tabs, cleanup_errors = [], [], []
    dock_job = None
    result = None

    def state():
        return command("dump_state", {"window_index": 0})["ciloggState"]

    def other_current():
        wait_value(
            state,
            lambda value: Path(value["activeFile"]).resolve() == other.resolve(),
            "navigation_other_tab_not_current",
        )

    def checked_rows(job, through_mcp, status):
        values = rows(job, through_mcp)
        for row in values:
            compare_json(status["source"], row.get("source"), "$.row.source")
            compare_json(job, row.get("jobId"), "$.row.jobId")
            compare_json(
                status["requestRevision"],
                row.get("requestRevision"),
                "$.row.requestRevision",
            )
        LOG.debug(
            "navigation_rows_verified job=%s mcp=%s rows=%d",
            job,
            through_mcp,
            len(values),
        )
        return values

    def tab_for(path):
        info = command("get_info")
        found = [
            (w["windowIndex"], t["tabId"])
            for w in info["windows"]
            for t in w["tabs"]
            if Path(t["filePath"]).resolve() == path.resolve()
        ]
        if len(found) != 1 or found[0][0] != 0:
            raise ParityError("source_tab_missing_or_ambiguous")
        return found[0][1]

    def assert_source(status, tab):
        source = status.get("source", {})
        description = status.get("sourceDescription", {})
        if (
            not status.get("complete")
            or status.get("scope") != "whole_file"
            or source.get("type") != "file"
            or not source.get("sourceId")
            or source.get("indexedBytes") != str(fixture.stat().st_size)
            or description.get("tabId") != tab
            or Path(description.get("filePath", "")).resolve() != fixture.resolve()
        ):
            raise ParityError("file_source_provenance_mismatch")

    def cleanup(action, arguments):
        try:
            command(action, arguments)
        except (RuntimeError, AssertionError, OSError) as error:
            LOG.error("navigation_cleanup_failed action=%s reason=%s", action, error)
            cleanup_errors.append(action)

    try:
        cli("open_file", "--file", str(fixture))
        source_tab = tab_for(fixture)
        tabs.append(source_tab)
        wait_value(
            state,
            lambda value: (
                value.get("loadingInProgress") is False
                and Path(value["activeFile"]).resolve() == fixture.resolve()
            ),
            "source_indexing_timeout",
        )
        command("open_file", {"file": str(other)})
        other_tab = tab_for(other)
        tabs.append(other_tab)
        other_current()
        LOG.info(
            "navigation_sources_ready source_tab=%s other_tab=%s", source_tab, other_tab
        )
        request = {
            "scope": "whole_file",
            "pattern": r"(?<value>\d+)",
            "requestRevision": REVISION,
        }
        request_path = artifacts / "navigation-request.json"
        request_path.write_text(json.dumps(request), encoding="utf-8")
        first = cli(
            "regex_evaluate", "--tab-id", source_tab, "--json-file", str(request_path)
        )["jobId"]
        jobs.append(first)
        other_current()
        second = command(
            "regex_evaluate", {"tab_id": source_tab, "analysis_json": request}
        )["jobId"]
        jobs.append(second)
        statuses = [finish(first, False), finish(second, True)]
        for status in statuses:
            assert_source(status, source_tab)
            compare_json(REVISION, status["requestRevision"], "$.requestRevision")
        compare_json(statuses[0]["source"], statuses[1]["source"], "$.source")
        expected = checked_rows(first, False, statuses[0])
        (artifacts / "navigation-snapshot.json").write_text(
            json.dumps(
                {"statuses": statuses, "rows": expected}, ensure_ascii=False, indent=2
            ),
            encoding="utf-8",
        )
        compare_json(
            normalized(expected), normalized(checked_rows(second, True, statuses[1]))
        )
        compare_json([1, 2, 3], [row["lineNumber"] for row in expected])
        compare_json(SOURCE.splitlines(), [row["text"] for row in expected])
        command("show_regex_lab", {"window_index": 0})
        definition = {k: v for k, v in request.items() if k != "requestRevision"}
        definition["sourceTabId"] = source_tab
        command("set_regex_lab", {"window_index": 0, "definition": definition})
        previous = command("get_regex_lab", {"window_index": 0})["jobId"]
        objects = command("get_ui", {"window_index": 0})["objects"]
        controls = [obj for obj in objects if obj.get("objectName") == "regexLabRun"]
        if (
            len(controls) != 1
            or not controls[0].get("accessibleName")
            or not controls[0].get("visible")
            or not controls[0].get("enabled")
        ):
            raise ParityError("navigation_gui_control_missing")
        other_current()
        command("activate_ui", {"window_index": 0, "object_name": "regexLabRun"})
        gui = wait_value(
            lambda: command("get_regex_lab", {"window_index": 0}),
            lambda value: bool(value["jobId"]) and value["jobId"] != previous,
            "gui_run_timeout",
        )
        dock_job = gui["jobId"]
        gui_status = finish(dock_job, True)
        assert_source(gui_status, source_tab)
        compare_json(statuses[0]["source"], gui_status["source"], "$.gui.source")
        compare_json(
            normalized(expected), normalized(checked_rows(dock_job, True, gui_status))
        )
        LOG.info("navigation_job_parity cli=%s mcp=%s gui=%s", first, second, dock_job)
        for floating in (True, False):
            command(
                "set_regex_lab",
                {"window_index": 0, "definition": {"floating": floating}},
            )
            compare_json(
                floating, cli("get_regex_lab", "--window-index", "0")["floating"]
            )
        for index, through_mcp in ((2, True), (1, False)):
            command("focus_tab", {"tab_id": other_tab})
            other_current()
            activation = {"activateResultIndex": index}
            if through_mcp:
                command("set_regex_lab", {"window_index": 0, "definition": activation})
            else:
                activation_path = artifacts / "navigation-activate.json"
                activation_path.write_text(json.dumps(activation), encoding="utf-8")
                cli(
                    "set_regex_lab",
                    "--window-index",
                    "0",
                    "--json-file",
                    str(activation_path),
                )
            wait_value(
                state,
                lambda value: (
                    Path(value["activeFile"]).resolve() == fixture.resolve()
                    and value["cursorLine"] == index + 1
                ),
                "navigation_target_mismatch",
            )
            LOG.info(
                "navigation_target_verified tab=%s physical_line=%d via=%s",
                source_tab,
                index + 1,
                "mcp" if through_mcp else "cli",
            )
        command("focus_tab", {"tab_id": other_tab})
        other_current()
        before = state()
        command("close_tab", {"tab_id": source_tab})
        tabs.remove(source_tab)
        stale = command(
            "set_regex_lab",
            {"window_index": 0, "definition": {"activateResultIndex": 1}},
            success=False,
        )
        compare_json("stale_location", stale.get("reason"), "$.stale.reason")
        after = state()
        compare_json(
            [before["activeFile"], before["cursorLine"]],
            [after["activeFile"], after["cursorLine"]],
            "$.stale.viewer",
        )
        compare_json(
            expected, checked_rows(first, False, statuses[0]), "$.retained.cli"
        )
        compare_json(
            normalized(expected),
            normalized(checked_rows(dock_job, True, gui_status)),
            "$.retained.gui",
        )
        result = {
            "id": "file-navigation",
            "passed": True,
            "source": statuses[0]["source"],
            "rows": normalized(expected),
            "closedSourceRejected": True,
            "retainedResultsAfterClose": True,
            "navigationLines": [3, 2],
            "guiMode": "real MainWindow semantic controls; offscreen, physical UI unmeasured",
        }
        LOG.info(
            "navigation_closed_source_verified tab=%s jobs_retained=%d", source_tab, 3
        )
    finally:
        if dock_job:
            cleanup(
                "set_regex_lab",
                {"window_index": 0, "definition": {"operation": "release"}},
            )
        for job in jobs:
            cleanup("release_analysis_job", {"job_id": job})
        for tab in reversed(tabs):
            cleanup("close_tab", {"tab_id": tab})
    if cleanup_errors:
        raise ParityError("navigation_cleanup_failed: " + ",".join(cleanup_errors))
    (artifacts / "navigation-result.json").write_text(
        json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    return result
