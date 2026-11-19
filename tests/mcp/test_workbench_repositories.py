"""Self-tests of the E15a repository parity case (no CILogg build needed)."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import workbench_parity  # noqa: E402
import workbench_repositories  # noqa: E402
from workbench_contracts import ParityError  # noqa: E402


class E15RegistryTests(unittest.TestCase):
    def test_e15_task_selects_the_repository_case(self):
        cases = workbench_parity.select_cases("E15")
        self.assertEqual([case["id"] for case in cases], ["e15a_repositories"])
        self.assertEqual(cases[0]["kind"], "e15a-repositories")
        for operation in ("plugin_manage", "get_plugin_job", "get_ui", "set_ui", "activate_ui",
                          "run_global_script"):
            self.assertIn(operation, cases[0]["operations"])

    def test_python_fixture_exists(self):
        fixture = Path(__file__).resolve().parents[2] / "test_data/workbench/python/e15a_repositories.py"
        self.assertTrue(fixture.is_file())


class E15HelperTests(unittest.TestCase):
    def test_listed_keeps_only_the_compared_fields(self):
        listed = workbench_repositories.listed([{
            "position": 1, "id": "team", "name": "Team", "url": "https://example.test/p.json",
            "signIn": "none", "enabled": True, "autoUpdate": False,
            "lastCheck": "2026-11-17T10:00:00Z", "lastError": "x", "pluginCount": 3}])
        self.assertEqual(listed, [{"position": 1, "id": "team", "name": "Team",
                                   "url": "https://example.test/p.json", "signIn": "none",
                                   "enabled": True, "autoUpdate": False}])

    def test_catalog_states_read_the_job_result(self):
        job = {"state": "failed", "result": {"repositories": [
            {"repository": "a", "state": "failed", "entries": 0, "error": "refused"}]}}
        self.assertEqual(workbench_repositories.catalog_states(job),
                         [{"repository": "a", "state": "failed", "entries": 0}])
        self.assertEqual(workbench_repositories.catalog_states({"state": "failed"}), [])

    def test_update_check_reads_state_updates_and_checked(self):
        job = {"state": "failed", "result": {"updates": [], "checked": ["a"], "notDue": []}}
        self.assertEqual(workbench_repositories.update_check(job),
                         {"state": "failed", "updates": [], "checked": ["a"]})
        self.assertEqual(workbench_repositories.update_check({"state": "cancelled"}),
                         {"state": "cancelled", "updates": None, "checked": None})

    def test_gui_rows_read_positions_names_and_check_states(self):
        table = {"objectName": "pluginRepositoriesTable",
                 "items": [["1", None, "Team", "https://a.test/p.json", "x", "Never"],
                           ["2", None, "Lab", "https://b.test/p.json", "x", "Never"]],
                 "checkStates": [[None, 2, None, None, None, None],
                                 [None, 0, None, None, None, None]]}
        command = lambda action, request: {"objects": [table]}  # noqa: E731
        self.assertEqual(workbench_repositories.gui_rows(command), [
            {"position": 1, "name": "Team", "url": "https://a.test/p.json", "enabled": True},
            {"position": 2, "name": "Lab", "url": "https://b.test/p.json", "enabled": False}])
        self.assertEqual(workbench_repositories.row_of(command, "Lab"), 1)
        with self.assertRaises(ParityError):
            workbench_repositories.row_of(command, "Missing")


if __name__ == "__main__":
    unittest.main()
