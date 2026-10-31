"""Client-observed first-result timing, exact provenance and deadline oracles."""

import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/benchmarks"))
import workbench_benchmark as runner
import workbench_first_results as first
from workbench_corpus import generate_corpus
from workbench_runtime import RunError

TOKEN = "G02I_USEFUL"
REVISION = "9007199254740993"
SOURCE = dict(sourceId="source", generation="1", revision="7", indexedBytes="123")


class Clock:
    now = 0.0

    def __call__(self):
        return self.now

    def sleep(self, duration):
        self.now += duration


class Host:
    def __init__(self, clock):
        self.clock = clock
        self.jobs = set()
        self.calls = []
        self.pages = 0
        self.delay = 0.001
        self.page_delay = 0
        self.completion_delay = 0
        self.change = lambda page: None
        self.empty = False
        self.early_completion = False
        self.final_state = "completed"
        self.limited = False

    def status(self):
        done = self.early_completion or self.pages >= 2
        return dict(
            jobId="job",
            requestRevision=REVISION,
            source=copy.deepcopy(SOURCE),
            state=self.final_state if done else "running",
            complete=done and not self.limited,
            reason="resource_limit" if self.limited else "",
            scannedLines=21 if done else 0,
            resultRecords=1,
            captureBackend="Qt QRegularExpression",
        )

    def command(self, action, arguments=None, timeout=15, allow_error=False):
        self.clock.now += self.delay
        self.calls.append((action, arguments, timeout))
        if action == "regex_evaluate":
            return dict(jobId="job")
        if action == "get_analysis_job":
            if self.pages >= 2:
                self.clock.now += self.completion_delay
            return self.status()
        if action != "get_analysis_results":
            raise AssertionError(action)
        self.clock.now += self.page_delay
        self.pages += 1
        row = dict(
            jobId="job",
            requestRevision=REVISION,
            source=copy.deepcopy(SOURCE),
            lineNumber=1,
            text=TOKEN,
            matched=True,
            offsetConvention="zero-based UTF-16",
            subpatterns=[
                dict(
                    index=0,
                    matched=True,
                    matches=[
                        dict(
                            startUtf16=0,
                            lengthUtf16=len(TOKEN),
                            groups=[
                                dict(
                                    number=0,
                                    name="",
                                    matched=True,
                                    value=TOKEN,
                                    startUtf16=0,
                                    lengthUtf16=len(TOKEN),
                                )
                            ],
                        )
                    ],
                )
            ],
        )
        page = dict(
            jobId="job",
            requestRevision=REVISION,
            source=copy.deepcopy(SOURCE),
            rows=[] if self.empty or self.pages == 1 else [row],
            nextCursor="",
            state=self.status()["state"],
        )
        self.change(page)
        return page


class FirstResultTests(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.host = Host(self.clock)
        self.record = {}
        self.budgets = dict(firstResultMs=1000, completionMs=2000, openIndexSeconds=30)

    def observe(self):
        self.assertTrue(
            callable(getattr(first, "observe_first_results", None)),
            "first-useful-result observer is not implemented",
        )
        with (
            patch.object(first, "clock", self.clock),
            patch.object(first.time, "sleep", self.clock.sleep),
        ):
            first.observe_first_results(
                self.host, 123, 21, self.budgets, self.record, 100
            )
        return self.record

    def test_first_page_and_completion_are_distinct_validated_observations(self):
        result = self.observe()
        self.assertTrue(result["complete"])
        self.assertLess(
            result["submissionAcknowledgedMs"], result["firstUsefulPageObservedMs"]
        )
        self.assertLess(
            result["firstUsefulPageObservedMs"], result["completedJobStatusObservedMs"]
        )
        self.assertEqual(result["emptyPages"], 1)
        self.assertEqual(result["firstUsefulPhysicalLine"], 1)
        self.assertEqual(result["requestRevision"], REVISION)
        self.assertEqual(result["source"], SOURCE)
        self.assertEqual(
            [
                a[1]["page_size"]
                for a in self.host.calls
                if a[0] == "get_analysis_results"
            ],
            [1, 1],
        )

    def test_completed_job_without_useful_row_fails(self):
        self.host.empty = True
        with self.assertRaisesRegex(RunError, "first_result_missing"):
            self.observe()
        self.assertFalse(self.record["complete"])

    def test_completion_may_be_observed_before_page_delivery(self):
        self.host.early_completion = True
        self.host.pages = 1
        result = self.observe()
        self.assertLess(
            result["completedJobStatusObservedMs"], result["firstUsefulPageObservedMs"]
        )

    def test_slow_completed_response_cannot_bypass_first_result_deadline(self):
        self.host.delay = 0.1
        self.budgets["firstResultMs"] = 50
        with self.assertRaisesRegex(RunError, "first_result_timeout"):
            self.observe()
        self.assertFalse(self.record["complete"])

    def test_completed_limited_job_is_rejected(self):
        self.host.limited = True
        with self.assertRaisesRegex(RunError, "first_result_incomplete"):
            self.observe()

    def test_late_page_delivery_is_not_accepted_after_its_deadline(self):
        self.host.page_delay = 0.1
        self.budgets["firstResultMs"] = 50
        with self.assertRaisesRegex(RunError, "first_result_timeout"):
            self.observe()
        self.assertFalse(self.record["complete"])
        self.assertNotIn("firstUsefulPageObservedMs", self.record)

    def test_completion_reply_uses_completion_budget_after_valid_first_delivery(self):
        self.host.completion_delay = 0.2
        self.budgets.update(firstResultMs=100, completionMs=1000)
        try:
            result = self.observe()
        except RunError as error:
            self.fail(
                f"Valid first delivery was rejected by the wrong phase budget: {error}"
            )
        self.assertLess(result["firstUsefulPageObservedMs"], 100)
        self.assertGreater(result["completedJobStatusObservedMs"], 100)
        self.assertTrue(result["complete"])

    def test_startup_info_read_uses_remaining_startup_budget_then_restores_normal_timeout(
        self,
    ):
        self.assertTrue(
            callable(getattr(first, "FirstResultsHost", None)),
            "bounded first-results startup host is not implemented",
        )
        host = object.__new__(first.FirstResultsHost)
        host.started, host.deadline = 0, 100
        self.clock.now = 8
        with (
            patch.object(first, "clock", self.clock),
            patch.object(
                first.ViewerHost, "start", lambda current: current.command("get_info")
            ),
            patch.object(first.ViewerHost, "command", return_value={}) as command,
        ):
            host.start()
            self.assertEqual(command.call_args.kwargs["timeout"], 22)
            host.command("get_info")
            self.assertEqual(command.call_args.kwargs["timeout"], 15)
            self.assertEqual(host.remaining(), 15)

    def test_completion_timeout_preserves_the_already_validated_first_page(self):
        self.host.final_state = "running"
        with self.assertRaisesRegex(RunError, "first_result_completion_timeout"):
            self.observe()
        self.assertIn("firstUsefulPageObservedMs", self.record)
        self.assertFalse(self.record["complete"])

    def test_failed_or_cancelled_worker_is_not_a_completed_measurement(self):
        for state in ("failed", "cancelled"):
            with self.subTest(state=state):
                self.host = Host(self.clock)
                self.host.final_state = state
                with self.assertRaisesRegex(RunError, "first_result_job_failed"):
                    self.observe()

    def test_wrong_job_source_or_revision_never_becomes_useful(self):
        for key, value in (
            ("jobId", "old-job"),
            ("requestRevision", "9007199254740992"),
            ("source", dict(SOURCE, generation="2")),
        ):
            with self.subTest(key=key):
                self.host = Host(self.clock)
                self.host.change = lambda p, k=key, v=value: p.update({k: v})
                with self.assertRaisesRegex(RunError, "first_result_page_provenance"):
                    self.observe()

    def test_false_match_wrong_line_text_or_span_is_rejected(self):
        def span(row):
            row["subpatterns"][0]["matches"][0]["startUtf16"] = 1

        for mutate in (
            lambda r: r.update(matched=False),
            lambda r: r.update(lineNumber=2),
            lambda r: r.update(text="other"),
            lambda r: r.update(jobId="old"),
            span,
        ):
            with self.subTest(mutate=mutate):
                self.host = Host(self.clock)
                self.host.change = lambda p, m=mutate: (
                    m(p["rows"][0]) if p["rows"] else None
                )
                with self.assertRaisesRegex(RunError, "first_result_row_oracle"):
                    self.observe()

    def test_submission_is_not_retried_and_timeouts_remain_bounded(self):
        self.observe()
        self.assertEqual(sum(c[0] == "regex_evaluate" for c in self.host.calls), 1)
        self.assertTrue(all(0 < c[2] <= 2 for c in self.host.calls))

    def test_boolean_match_outcomes_reject_numeric_ones(self):
        for target in ("row", "pattern", "group"):
            with self.subTest(target=target):
                self.host = Host(self.clock)
                self.record = {}

                def corrupt(page, location=target):
                    if not page["rows"]:
                        return
                    row = page["rows"][0]
                    value = row if location == "row" else row["subpatterns"][0]
                    if location == "group":
                        value = value["matches"][0]["groups"][0]
                    value["matched"] = 1

                self.host.change = corrupt
                with self.assertRaisesRegex(RunError, "first_result_row_oracle"):
                    self.observe()

    def test_wrong_watermark_or_final_count_fails(self):
        original = self.host.status
        self.host.status = lambda: dict(
            original(), source=dict(SOURCE, indexedBytes="122")
        )
        with self.assertRaisesRegex(RunError, "first_result_snapshot"):
            self.observe()

    def test_completed_job_with_wrong_final_counts_fails(self):
        original = self.host.status
        self.host.status = lambda: dict(original(), scannedLines=20)
        with self.assertRaisesRegex(RunError, "first_result_incomplete"):
            self.observe()

    def test_cleanup_failure_is_retained_without_masking_the_original_failure(self):
        host = Mock()
        host.remaining.return_value = 2
        host.command.side_effect = RunError("cleanup_transport_failure")
        record = dict(jobId="job")
        with self.assertRaisesRegex(RunError, "original_failure"):
            try:
                raise RunError("original_failure")
            finally:
                first.release_job(host, record)
        self.assertEqual(record["cleanupFailure"], "RunError")
        with self.assertRaisesRegex(RunError, "cleanup_transport_failure"):
            first.release_job(host, record)

    def test_common_runner_exposes_scenario_and_retains_failure(self):
        self.assertTrue(
            callable(getattr(runner, "run_first_results", None)),
            "first-results scenario is not registered",
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            generate_corpus(root / "corpus", rows=20)
            executable = root / "synthetic.py"
            executable.write_text("# never executed\n")
            with patch.object(
                runner,
                "run_first_results",
                side_effect=RunError("first_result_timeout"),
            ):
                result = runner.main(
                    [
                        "--scenario",
                        "first-results",
                        "--executable",
                        str(executable),
                        "--corpus",
                        str(root / "corpus"),
                        "--output",
                        str(root / "out"),
                    ]
                )
            report = json.loads((root / "out/result.json").read_text())
            self.assertEqual(result, 1)
            self.assertEqual(report["reason"], "first_result_timeout")
            self.assertEqual(report["evidenceKind"], "test_double")
            self.assertEqual(report["releaseGateStatus"], "pending")

    def test_first_results_has_its_own_budget_contract(self):
        self.assertIn("firstResultMs", runner.load_budgets(None, "first-results"))
        self.assertNotIn("appendSeconds", runner.load_budgets(None, "first-results"))


if __name__ == "__main__":
    unittest.main()
