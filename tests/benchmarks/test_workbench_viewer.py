"""Viewer baseline oracles, phase bounds and shared entry-point contracts."""

import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from unittest.mock import Mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/benchmarks"))
from workbench_corpus import generate_corpus
from workbench_runtime import RunError
import workbench_benchmark as runner
import workbench_viewer as viewer


class Clock:
    now = 0.0

    def __call__(self):
        return self.now


class Host:
    def __init__(self, source, clock):
        self.source, self.clock = source, clock
        self.deadline = 100.0
        self.state = dict(
            activeFile=str(source),
            activeTabIndex=0,
            loadingInProgress=False,
            searchInProgress=False,
            searchText="",
            matchCount=0,
            lastErrorText="",
        )
        self.calls = []
        self.bad_count = False
        self.delay = 0.001

    def command(self, action, arguments=None, timeout=15):
        self.calls.append((action, arguments, timeout))
        self.clock.now += self.delay
        if action == "search":
            if arguments.get("tab_index") != self.state["activeTabIndex"]:
                raise RunError("viewer_selector_missing")
            self.state.update(
                searchText=arguments["text"],
                matchCount=2 if arguments["text"] == " ERROR " else 18,
            )
            if self.bad_count:
                self.state["matchCount"] += 1
        if action == "dump_state":
            return {"ciloggState": dict(self.state)}
        return {}


class ViewerBaselineTests(unittest.TestCase):
    def test_host_waits_for_semantic_startup_readiness_before_profile_commands(self):
        host = object.__new__(viewer.ViewerHost)
        host.deadline = 100
        host.started = 0
        host.command = Mock(
            side_effect=[
                RunError("command_failed_dump_state"),
                {"ciloggState": {"startupReady": False}},
                {"ciloggState": {"startupReady": True}},
            ]
        )
        with (
            patch.object(viewer.EpochHost, "start"),
            patch.object(viewer, "clock", return_value=0),
            patch.object(viewer.time, "sleep"),
        ):
            host.start()
        self.assertEqual(host.command.call_count, 3)

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="cilogg-viewer-tests-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "source.log"
        self.source.write_bytes(b"fixture")
        self.clock = Clock()
        self.host = Host(self.source, self.clock)
        self.expected = dict(rows=20, errorLines=2)

    def cycle(self, budgets=None):
        record = {}
        with patch.object(viewer, "clock", self.clock):
            viewer.baseline_cycle(
                self.host,
                self.source,
                self.expected,
                budgets or viewer.VIEWER_BUDGETS,
                record,
            )
        return record

    def test_cycle_requires_completed_source_and_exact_search_oracles(self):
        result = self.cycle()
        self.assertTrue(result["complete"])
        self.assertGreater(result["openIndexSecondsIncludingProtocol"], 0)
        self.assertEqual([search["matches"] for search in result["searches"]], [2, 18])
        self.assertEqual(
            [search["regex"] for search in result["searches"]], [False, True]
        )
        self.assertEqual([call[0] for call in self.host.calls].count("search"), 2)
        self.assertFalse(any("analysis" in call[0] for call in self.host.calls))

    def test_wrong_count_source_and_non_boolean_state_fail(self):
        self.host.bad_count = True
        with self.assertRaisesRegex(RunError, "viewer_count_mismatch"):
            self.cycle()
        self.host.bad_count = False
        for changes in (
            {"activeFile": str(self.root / "other.log")},
            {"loadingInProgress": 0},
            {"searchInProgress": None},
            {"matchCount": True},
            {"lastErrorText": "bad pattern"},
        ):
            with self.subTest(changes=changes):
                self.host.state.update(changes)
                with self.assertRaises(RunError):
                    self.cycle()
                self.host = Host(self.source, self.clock)

    def test_phase_wait_counts_command_time_and_preserves_partial_record(self):
        self.host.delay = 0.02
        record = {}
        with patch.object(viewer, "clock", self.clock):
            with self.assertRaisesRegex(RunError, "viewer_phase_timeout"):
                viewer.baseline_cycle(
                    self.host,
                    self.source,
                    self.expected,
                    {"openIndexSeconds": 0.01, "searchSeconds": 1},
                    record,
                )
        self.assertFalse(record["complete"])
        self.assertEqual(record["phase"], "open_index")
        self.assertEqual(self.host.calls[0][2], 0.01)

    def test_completed_operation_cannot_pass_after_total_deadline(self):
        self.host.deadline = 0.0001
        with self.assertRaisesRegex(RunError, "viewer_phase_timeout"):
            self.cycle()

    def test_fixture_uses_exact_generated_oracles_and_rejects_boolean_counts(self):
        corpus = self.root / "corpus"
        manifest = generate_corpus(corpus, rows=20)
        # Generator returns its manifest; hash validation remains in the shared runner.
        if manifest is None:
            manifest = json.loads((corpus / "manifest.json").read_text())
        self.assertEqual(viewer.fixture(manifest)["errorLines"], 2)
        entry = next(item for item in manifest["files"] if item["path"] == "ascii.log")
        for value in (True, 3, "2"):
            entry["expected"]["errorLines"] = value
            with self.assertRaisesRegex(ValueError, "viewer_fixture_oracle"):
                viewer.fixture(manifest)

    def test_paired_overhead_preserves_negative_deltas_and_missing_denominator(self):
        baseline = {
            "openIndexSecondsIncludingProtocol": 2,
            "searches": [
                {"secondsIncludingProtocol": 4},
                {"secondsIncludingProtocol": 0},
            ],
        }
        idle = {
            "openIndexSecondsIncludingProtocol": 1,
            "searches": [
                {"secondsIncludingProtocol": 6},
                {"secondsIncludingProtocol": 2},
            ],
        }
        result = viewer.paired_overhead(baseline, idle)
        self.assertEqual(result["openIndex"]["deltaSeconds"], -1)
        self.assertEqual(result["openIndex"]["ratio"], 0.5)
        self.assertEqual(result["searches"][0]["ratio"], 1.5)
        self.assertIsNone(result["searches"][1]["ratio"])

    def test_registered_scenario_runs_and_retains_runtime_failure(self):
        corpus = self.root / "corpus"
        generate_corpus(corpus, rows=20)
        output = self.root / "failed"
        executable = self.root / "synthetic.py"
        executable.write_text("# synthetic executable, never launched\n")
        with patch.object(
            runner, "run_viewer", side_effect=RunError("viewer_phase_timeout")
        ):
            code = runner.main(
                [
                    "--scenario",
                    "viewer",
                    "--executable",
                    str(executable),
                    "--corpus",
                    str(corpus),
                    "--output",
                    str(output),
                ]
            )
        report = json.loads((output / "result.json").read_text())
        self.assertEqual(code, 1)
        self.assertFalse(report["passed"])
        self.assertEqual(report["reason"], "viewer_phase_timeout")
        self.assertEqual(report["evidenceKind"], "test_double")
        self.assertEqual(report["releaseGateStatus"], "pending")

    def test_viewer_uses_separate_budgets_and_invalid_sampling_is_setup_error(self):
        self.assertEqual(runner.load_budgets(None, "viewer"), viewer.VIEWER_BUDGETS)
        self.assertNotIn("appendSeconds", runner.load_budgets(None, "viewer"))
        corpus = self.root / "corpus"
        generate_corpus(corpus, rows=20)
        executable = self.root / "synthetic.py"
        executable.write_text("# never launched\n")
        output = self.root / "invalid"
        code = runner.main(
            [
                "--scenario",
                "viewer",
                "--executable",
                str(executable),
                "--corpus",
                str(corpus),
                "--output",
                str(output),
                "--sample-resident-memory",
            ]
        )
        self.assertEqual(code, 2)
        self.assertEqual(
            json.loads((output / "result.json").read_text())["state"], "invalid_setup"
        )


if __name__ == "__main__":
    unittest.main()
