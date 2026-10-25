"""Failure contracts for the reusable parity runner; no Qt or MCP SDK needed."""

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch


class ParityHarnessTests(unittest.TestCase):
    def setUp(self):
        path = Path(__file__).with_name("workbench_parity.py")
        self.assertTrue(path.is_file(), "G03 parity runner must exist")
        spec = importlib.util.spec_from_file_location("workbench_parity", path)
        self.runner = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.runner)
        self.temp = tempfile.TemporaryDirectory(prefix="cilogg-parity-tests-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_unknown_task_is_failure(self):
        with self.assertRaisesRegex(self.runner.ParityError, "unknown_task"):
            self.runner.select_cases("V99")

    def test_empty_selection_is_failure(self):
        registry = self.root / "empty.json"
        registry.write_text(
            json.dumps({"schemaVersion": 1, "tasks": {"G03": []}, "cases": []})
        )
        with self.assertRaisesRegex(self.runner.ParityError, "empty_selection"):
            self.runner.select_cases("G03", registry)

    def test_missing_selected_case_is_failure(self):
        registry = self.root / "missing.json"
        registry.write_text(
            json.dumps({"schemaVersion": 1, "tasks": {"G03": ["missing"]}, "cases": []})
        )
        with self.assertRaisesRegex(self.runner.ParityError, "missing_case"):
            self.runner.select_cases("G03", registry)

    def test_missing_operation_is_failure(self):
        with self.assertRaisesRegex(
            self.runner.ParityError, "missing_operation.*regex_evaluate"
        ):
            self.runner.require_operations(
                {"commander": {"commands": {}}}, ["regex_evaluate"]
            )

    def test_large_decimal_strings_are_preserved(self):
        exact = {"requestRevision": "9007199254740993", "count": "18446744073709551615"}
        self.runner.compare_json(exact, json.loads(json.dumps(exact)))
        damaged = dict(exact, requestRevision=9007199254740992.0)
        with self.assertRaisesRegex(
            self.runner.ParityError, "result_mismatch.*requestRevision"
        ):
            self.runner.compare_json(exact, damaged)

    def test_boolean_is_not_integer(self):
        with self.assertRaisesRegex(self.runner.ParityError, "result_mismatch"):
            self.runner.compare_json({"complete": True}, {"complete": 1})

    def test_sdk_error_flags_support_both_sdk_generations(self):
        import workbench_contracts

        for flag in (False, True):
            for field in ("isError", "is_error"):
                result = SimpleNamespace(**{field: flag})
                self.assertIs(workbench_contracts.sdk_tool_failed(result), flag)

    def test_sdk_missing_error_flag_cannot_pass(self):
        import workbench_contracts

        with self.assertRaisesRegex(self.runner.ParityError, "invalid_sdk_error_flag"):
            workbench_contracts.sdk_tool_failed(SimpleNamespace())

    def test_nested_capture_identity_is_not_removed(self):
        with self.assertRaisesRegex(self.runner.ParityError, "result_mismatch.*jobId"):
            self.runner.compare_json(
                {"captures": {"jobId": "a"}}, {"captures": {"jobId": "b"}}
            )

    def test_nonzero_child_exit_retains_diagnostics(self):
        with self.assertRaisesRegex(self.runner.ParityError, "child_exit.*7"):
            self.runner.supervise(
                [
                    sys.executable,
                    "-c",
                    "import sys; print('diagnostic', file=sys.stderr); sys.exit(7)",
                ],
                self.root,
                10,
            )
        self.assertIn("diagnostic", (self.root / "stderr.log").read_text())

    def test_timeout_retains_diagnostics_and_reaps(self):
        with self.assertRaisesRegex(self.runner.ParityError, "child_timeout"):
            self.runner.supervise(
                [
                    sys.executable,
                    "-c",
                    "import time; print('started', flush=True); time.sleep(30)",
                ],
                self.root,
                1,
            )
        self.assertIn("started", (self.root / "stdout.log").read_text())

    def test_unknown_task_cli_writes_failure(self):
        result = self.runner.main(
            [
                "--executable",
                sys.executable,
                "--task",
                "V99",
                "--artifacts",
                str(self.root / "run"),
            ]
        )
        self.assertNotEqual(result, 0)
        report = json.loads((self.root / "run" / "result.json").read_text())
        self.assertFalse(report["passed"])
        self.assertIn("unknown_task", report["reason"])

    def test_gui_job_is_released_once(self):
        case = self.runner.select_cases("P04")[0]
        active = {"cli", "mcp", "gui"}
        activated = False

        def cli(*args):
            return {"jobId": "cli"}

        def command(action, arguments):
            nonlocal activated
            if action == "regex_evaluate":
                return {"jobId": "mcp"}
            if action == "get_ui":
                return {
                    "objects": [
                        {"objectName": "regexLabRun", "accessibleName": "Run analysis"}
                    ]
                }
            if action == "get_regex_lab":
                return {"jobId": "gui" if activated else ""}
            if action == "activate_ui":
                activated = True
            if action == "release_analysis_job":
                active.remove(arguments["job_id"])
            if (
                action == "set_regex_lab"
                and arguments["definition"].get("operation") == "release"
            ):
                active.remove("gui")
            return {}

        self.runner.exercise(
            [case],
            cli,
            command,
            lambda *args: {"complete": True, "requestRevision": "9007199254740993"},
            lambda *args: [{"lineNumber": line} for line in [1, 2, 3]],
            lambda values: values,
            {"commander": {"commands": case["operations"]}},
            self.root,
        )
        self.assertFalse(active)


    def sdk_child_result(self, arguments, root, timeout, handshake=False):
        self.assertIn("--sdk-smoke", arguments)
        results = [{"id": case["id"], "passed": True} for case in self.runner.select_cases("G03")]
        (root / "native" / "case-results.json").write_text(json.dumps(results))
        sdk = root / "native" / "sdk"
        sdk.mkdir()
        (sdk / "result.json").write_text(json.dumps({"passed": True, "checks": ["smoke"]}))
        (sdk / "runtime-result.json").write_text(json.dumps({"passed": True, "sdkVersion": "2.3.0", "checks": ["jobs"]}))

    def test_sdk_request_reaches_owned_child(self):
        with patch.object(self.runner, "supervise", side_effect=self.sdk_child_result):
            code = self.runner.main([
                "--executable", sys.executable, "--task", "G03", "--sdk-smoke",
                "--artifacts", str(self.root / "sdk-run"),
            ])
        self.assertEqual(code, 0)

    def test_requested_sdk_cannot_pass_without_its_reports(self):
        def no_sdk(arguments, root, timeout, handshake=False):
            results = [{"id": case["id"], "passed": True} for case in self.runner.select_cases("G03")]
            (root / "native" / "case-results.json").write_text(json.dumps(results))
        with patch.object(self.runner, "supervise", side_effect=no_sdk):
            code = self.runner.main([
                "--executable", sys.executable, "--task", "G03", "--sdk-smoke",
                "--artifacts", str(self.root / "missing-sdk"),
            ])
        self.assertNotEqual(code, 0)

    def test_sdk_nonfinite_extension_never_passes(self):
        def nonfinite(arguments, root, timeout, handshake=False):
            self.sdk_child_result(arguments, root, timeout, handshake)
            path = root / "native/sdk/result.json"
            path.write_text(json.dumps({"passed": True, "extra": float("nan")}))
        with patch.object(self.runner, "supervise", side_effect=nonfinite):
            code = self.runner.main([
                "--executable", sys.executable, "--task", "G03", "--sdk-smoke",
                "--artifacts", str(self.root / "nan-sdk"),
            ])
        self.assertNotEqual(code, 0)


if __name__ == "__main__":
    unittest.main()
