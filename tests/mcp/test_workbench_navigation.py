"""MainWindow source-navigation contracts; test doubles never establish native UI."""

import copy
import importlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))


class NavigationTests(unittest.TestCase):
    def setUp(self):
        self.runner = importlib.import_module("workbench_navigation")
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.tabs = {}
        self.jobs = {}
        self.active = None
        self.line = 1
        self.lab = {"jobId": "", "sourceTabId": "", "floating": False}
        self.fault = None
        self.serial = 0
        self.now = 0.0

    def clock(self):
        self.now += 1
        return self.now

    def status(self, tab):
        return {
            "complete": True,
            "scope": "whole_file",
            "source": {
                "type": "file",
                "sourceId": "source-" + tab,
                "generation": "1",
                "revision": "1",
                "indexedBytes": str(len(self.runner.SOURCE.encode("utf-8"))),
            },
            "sourceDescription": {"tabId": tab, "filePath": self.tabs[tab]},
            "requestRevision": "9007199254740993",
        }

    def allocate(self, tab):
        self.serial += 1
        job = "job-" + str(self.serial)
        self.jobs[job] = self.status(tab)
        return job

    def command(self, action, arguments=None, success=True):
        args = arguments or {}
        if action == "open_file":
            tab = "tab-" + str(len(self.tabs))
            self.tabs[tab] = args["file"]
            if self.fault != "other_never_current" or len(self.tabs) == 1:
                self.active = tab
        elif action == "get_info":
            return {
                "windows": [
                    {
                        "windowIndex": 0,
                        "tabs": [
                            {"tabId": tab, "filePath": path}
                            for tab, path in self.tabs.items()
                        ],
                    }
                ]
            }
        elif action == "focus_tab":
            if self.fault != "focus_ignored":
                self.active = args["tab_id"]
            self.line = 1
        elif action == "close_tab":
            del self.tabs[args["tab_id"]]
        elif action == "dump_state":
            return {
                "ciloggState": {
                    "activeFile": self.tabs[self.active],
                    "cursorLine": self.line,
                    "loadingInProgress": False,
                }
            }
        elif action == "regex_evaluate":
            return {"jobId": self.allocate(args["tab_id"])}
        elif action == "show_regex_lab":
            pass
        elif action == "get_ui":
            return {
                "objects": [
                    {
                        "objectName": "regexLabRun",
                        "accessibleName": "Run",
                        "visible": True,
                        "enabled": True,
                    }
                ]
            }
        elif action == "get_regex_lab":
            return copy.deepcopy(self.lab)
        elif action == "activate_ui":
            if self.fault != "run_stalled":
                self.lab["jobId"] = self.allocate(self.lab["sourceTabId"])
        elif action == "set_regex_lab":
            definition = args["definition"]
            self.lab.update(
                {
                    k: v
                    for k, v in definition.items()
                    if k in ("sourceTabId", "floating")
                }
            )
            if definition.get("operation") == "release":
                self.jobs.pop(self.lab["jobId"])
                self.lab["jobId"] = ""
            if "activateResultIndex" in definition:
                source = self.lab["sourceTabId"]
                if source not in self.tabs:
                    if self.fault == "stale_allowed":
                        return {}
                    self.assertFalse(success)
                    return {"reason": "stale_location"}
                if self.fault != "wrong_tab":
                    self.active = source
                self.line = (
                    1
                    if self.fault == "wrong_line"
                    else definition["activateResultIndex"] + 1
                )
            return copy.deepcopy(self.lab)
        elif action == "release_analysis_job":
            self.jobs.pop(args["job_id"])
        elif action == "get_analysis_job":
            status = copy.deepcopy(self.jobs[args["job_id"]])
            if self.fault == "wrong_source":
                status["sourceDescription"]["tabId"] = "unrelated"
            return status
        else:
            self.fail("Unexpected operation " + action)
        return {}

    def cli(self, action, *values, success=True):
        options = dict(zip(values[::2], values[1::2]))
        args = {
            key.removeprefix("--").replace("-", "_"): value
            for key, value in options.items()
        }
        if "--json-file" in options:
            request = json.loads(
                Path(options["--json-file"]).read_text(encoding="utf-8")
            )
            args["definition" if action == "set_regex_lab" else "analysis_json"] = (
                request
            )
        return self.command(action, args, success=success)

    def rows(self, job, mcp):
        result = [
            {
                "lineNumber": i + 1,
                "text": text,
                "source": copy.deepcopy(self.jobs[job]["source"]),
                "jobId": job,
                "requestRevision": self.jobs[job]["requestRevision"],
            }
            for i, text in enumerate(self.runner.SOURCE.splitlines())
        ]
        if self.fault == "wrong_rows" and mcp:
            result[1]["lineNumber"] = 99
        if mcp:
            gui = job == self.lab["jobId"]
            if self.fault == ("wrong_gui_source" if gui else "wrong_mcp_source"):
                result[0]["source"]["sourceId"] = "unrelated"
            if self.fault == ("missing_gui_source" if gui else "missing_mcp_source"):
                result[0].pop("source")
            if self.fault == "retained_gui_source" and gui and len(self.tabs) == 1:
                result[0]["source"]["sourceId"] = "unrelated"
            if self.fault == "row_revision":
                result[0]["requestRevision"] = "wrong"
            if self.fault == "row_job":
                result[0]["jobId"] = "unrelated"
        return result

    def exercise(self):
        with (
            patch.object(self.runner.time, "monotonic", side_effect=self.clock),
            patch.object(self.runner.time, "sleep"),
        ):
            return self.runner.exercise_navigation(
                self.cli,
                self.command,
                lambda job, mcp: self.command("get_analysis_job", {"job_id": job}),
                self.rows,
                lambda rows: [
                    {
                        k: v
                        for k, v in row.items()
                        if k not in ("source", "jobId", "requestRevision")
                    }
                    for row in rows
                ],
                self.root,
            )

    def test_complete_case_routes_and_releases_owned_resources(self):
        result = self.exercise()
        self.assertTrue(result["passed"])
        self.assertTrue(result["closedSourceRejected"])
        self.assertTrue(result["retainedResultsAfterClose"])
        self.assertFalse(self.jobs)
        self.assertFalse(self.tabs)

    def test_wrong_active_tab_cannot_pass(self):
        self.fault = "wrong_tab"
        with self.assertRaisesRegex(RuntimeError, "navigation"):
            self.exercise()
        self.assertFalse(self.jobs)
        self.assertFalse(self.tabs)

    def test_wrong_physical_line_cannot_pass(self):
        self.fault = "wrong_line"
        with self.assertRaisesRegex(RuntimeError, "navigation"):
            self.exercise()

    def test_closure_must_invalidate_navigation(self):
        self.fault = "stale_allowed"
        with self.assertRaisesRegex(RuntimeError, "stale"):
            self.exercise()

    def test_gui_run_must_publish_new_job_within_deadline(self):
        self.fault = "run_stalled"
        with self.assertRaisesRegex(RuntimeError, "gui_run_timeout"):
            self.exercise()
        self.assertFalse(self.jobs)

    def test_source_provenance_cannot_be_ignored(self):
        self.fault = "wrong_source"
        with self.assertRaisesRegex(RuntimeError, "source"):
            self.exercise()

    def test_physical_rows_must_match_across_transports(self):
        self.fault = "wrong_rows"
        with self.assertRaisesRegex(RuntimeError, "result_mismatch"):
            self.exercise()

    def test_registry_exposes_navigation_in_g03_and_its_own_task(self):
        import workbench_parity

        self.assertIn(
            "file-navigation", [c["id"] for c in workbench_parity.select_cases("G03")]
        )
        self.assertEqual(
            ["file-navigation"],
            [c["id"] for c in workbench_parity.select_cases("G03d")],
        )

    def check_row_fault(self, fault, reason):
        self.fault = fault
        with self.assertRaisesRegex(RuntimeError, reason):
            self.exercise()
        self.assertFalse(self.jobs)
        self.assertFalse(self.tabs)

    def test_wrong_mcp_row_source_rejected(self):
        self.check_row_fault("wrong_mcp_source", "row.source")

    def test_missing_mcp_row_source_rejected(self):
        self.check_row_fault("missing_mcp_source", "row.source")

    def test_wrong_gui_row_source_rejected(self):
        self.check_row_fault("wrong_gui_source", "row.source")

    def test_missing_gui_row_source_rejected(self):
        self.check_row_fault("missing_gui_source", "row.source")

    def test_retained_gui_row_source_rejected(self):
        self.check_row_fault("retained_gui_source", "row.source")

    def test_row_request_revision_rejected_before_normalization(self):
        self.check_row_fault("row_revision", "row.requestRevision")

    def test_row_job_identity_rejected_before_normalization(self):
        self.check_row_fault("row_job", "row.jobId")

    def test_other_tab_must_be_current_before_analysis(self):
        self.check_row_fault("other_never_current", "other_tab")

    def test_other_tab_must_be_current_before_activation(self):
        self.check_row_fault("focus_ignored", "other_tab")

    def test_cleanup_failure_cannot_pass_and_other_resources_are_released(self):
        original = self.command

        def command(action, *args, **kwargs):
            if action == "release_analysis_job":
                raise RuntimeError("deliberate cleanup failure")
            return original(action, *args, **kwargs)

        self.command = command
        with self.assertRaisesRegex(RuntimeError, "navigation_cleanup_failed"):
            self.exercise()
        self.assertFalse(self.tabs)


if __name__ == "__main__":
    unittest.main()
