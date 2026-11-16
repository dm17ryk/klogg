"""U15: the public Python command route and the U15 parity case registration."""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(ROOT / "src/app/python_runtime"))

import workbench_parity  # noqa: E402
from klogg import app  # noqa: E402
from klogg.client import _commander_request  # noqa: E402


class PythonCommandRouteTests(unittest.TestCase):
    def test_cli_and_mcp_names_become_commander_fields(self):
        self.assertEqual(
            _commander_request("invoke_plugin", {
                "plugin_id": "p", "plugin_action": "a", "source": "all",
                "definition": {"x": 1}, "window_index": 0, "tab_id": None}),
            {"pluginId": "p", "pluginAction": "a", "source": "all",
             "pluginArguments": {"x": 1}, "windowIndex": 0})
        self.assertEqual(
            _commander_request("set_regex_lab", {"definition": {"source": "tab:t1"},
                                                 "window_index": 1}),
            {"definitionPayload": {"source": "tab:t1"}, "windowIndex": 1})
        self.assertEqual(
            _commander_request("preview_message", {"analysis_json": '{"line": 2}',
                                                   "source": "file:/a.log"}),
            {"analysisArguments": {"line": 2}, "source": "file:/a.log"})
        self.assertEqual(
            _commander_request("search", {"text": "ERROR", "source": "all", "wait": True,
                                          "timeout_ms": 500, "regex": True,
                                          "match_mode": "whole_word"}),
            {"searchText": "ERROR", "source": "all", "wait": True, "timeoutMs": 500,
             "searchUseRegex": True, "match_mode": "whole_word"})
        self.assertEqual(
            _commander_request("get_analysis_results", {"job_id": "j", "page_size": 5,
                                                        "cursor": ""}),
            {"analysisArguments": {"jobId": "j", "pageSize": 5, "cursor": ""}})
        self.assertEqual(
            _commander_request("run_script", {"script_file": "s.py", "source": "tab:t2"}),
            {"scriptFilePath": "s.py", "source": "tab:t2"})
        self.assertEqual(_commander_request("add_comment", {"text": "note"}),
                         {"commentText": "note"})
        # Commander field names pass through unchanged.
        self.assertEqual(_commander_request("open_plugin_view", {"viewId": "main"}),
                         {"viewId": "main"})
        with self.assertRaises(ValueError):
            _commander_request("set_value_counts", {"definition": [1]})

    def test_public_calls_exist(self):
        for name in ("command", "set_regex_lab", "set_value_counts"):
            self.assertTrue(callable(getattr(app, name)), name)

    def test_success_code_from_the_host_is_not_an_error(self):
        # The host sends commanderResultCodeToString() codes: "success", "invalid_request", ...
        from klogg.client import _RpcClient
        from klogg.exceptions import KloggError

        client = _RpcClient.__new__(_RpcClient)
        replies = iter([
            {"code": "success", "payload": {"windows": []}},
            {"code": "invalid_request", "message": "bad", "payload": {"reason": "invalid_source"}},
        ])
        client.request = lambda method, params=None: next(replies)
        self.assertEqual(client.command("get_info"), {"windows": []})
        with self.assertRaises(KloggError) as raised:
            client.command("search", source="window")
        self.assertEqual(raised.exception.payload["reason"], "invalid_source")
        self.assertEqual(raised.exception.code, "invalid_request")


class U15RegistryTests(unittest.TestCase):
    def test_u15_cases_cover_python_and_tools(self):
        cases = workbench_parity.select_cases("U15")
        self.assertEqual([case["id"] for case in cases],
                         ["u15_python_source_parity", "u15b_sources"])
        self.assertEqual({case["kind"] for case in cases}, {"u15-docks", "u15b-sources"})
        tools = cases[1]["operations"]
        for operation in ("search", "preview_message", "invoke_plugin", "open_plugin_view",
                          "run_script", "run_global_script", "get_ui"):
            self.assertIn(operation, tools)


if __name__ == "__main__":
    unittest.main()
