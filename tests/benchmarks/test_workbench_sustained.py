"""Sustained-memory observations must reject incomplete or misleading evidence."""

import copy
import hashlib
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

HERE = Path(__file__).resolve().parents[2] / "scripts/benchmarks"
sys.path.insert(0, str(HERE))


class SustainedTests(unittest.TestCase):
    def module(self):
        path = HERE / "workbench_sustained.py"
        self.assertTrue(path.is_file(), "sustained-memory runner is missing")
        spec = importlib.util.spec_from_file_location("sustained_tested", path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def points(self):
        return [
            {"seconds": i * 12.0, "hostRssBytes": 1000000 + i * 12000}
            for i in range(11)
        ]

    def test_actual_clock_signed_slope_and_growth(self):
        module = self.module()
        summary = module.summarize(self.points(), 120)
        self.assertEqual(summary["durationSeconds"], 120)
        self.assertAlmostEqual(summary["hostSlopeBytesPerMinute"], 60000)
        self.assertEqual(summary["hostGrowthBytes"], 120000)
        points = self.points()
        for row in points:
            row["hostRssBytes"] = 2000000 - row["hostRssBytes"]
        self.assertLess(module.summarize(points, 120)["hostSlopeBytesPerMinute"], 0)

    def test_short_sparse_nonfinite_and_nonmonotonic_series_fail(self):
        module = self.module()
        for edit in (
            lambda p: p.pop(),
            lambda p: p.__delitem__(slice(2, 10)),
            lambda p: p[2].update(seconds=p[1]["seconds"]),
            lambda p: p[2].update(seconds=float("nan")),
            lambda p: p[2].update(hostRssBytes=True),
            lambda p: p[2].update(hostRssBytes=10**400),
        ):
            points = self.points()
            edit(points)
            with self.assertRaises(module.RunError):
                module.summarize(points, 120)

    def test_memory_gates_are_independent_and_reject_missing_measurements(self):
        module = self.module()
        limits = {"treeRssBytes": 512, "hostGrowthBytes": 32}
        self.assertTrue(all(row["passed"] for row in module.gates(400, 20, limits)))
        rows = module.gates(400, 20, {**limits, "treeRssBytes": 1})
        self.assertEqual([row["passed"] for row in rows], [False, True])
        self.assertEqual(
            [row["passed"] for row in module.gates(400, 40, limits)], [True, False]
        )
        for value in (None, False, float("nan"), 10**400):
            with self.assertRaises(module.RunError):
                module.gates(value, 20, limits)

    def test_exact_count_row_and_source_epoch_required(self):
        module = self.module()
        status = {
            "state": "completed",
            "complete": True,
            "scannedLines": "1016",
            "source": {"indexedBytes": "2048", "sourceId": "source", "generation": "2"},
        }
        identity = {"sourceId": "source", "generation": "2"}
        module.verify_job(status, 2048, 1016, identity)
        for edit in (
            lambda s: s.update(complete=False),
            lambda s: s.update(state="failed"),
            lambda s: s.update(scannedLines="1015"),
            lambda s: s["source"].update(indexedBytes="2047"),
            lambda s: s["source"].update(generation="3"),
        ):
            broken = copy.deepcopy(status)
            edit(broken)
            with self.assertRaises(module.RunError):
                module.verify_job(broken, 2048, 1016, identity)
        page = {"rows": [{"value": "G02F_TOKEN", "count": "16"}]}
        module.verify_count(page, 16)
        for broken in (
            {"rows": [{"value": "G02F_TOKEN", "count": 16}]},
            {"rows": [{"value": "wrong", "count": "16"}]},
            {**page, "nextCursor": "more"},
            {"rows": []},
        ):
            with self.assertRaises(module.RunError):
                module.verify_count(broken, 16)

    def test_snapshot_requires_owned_host_and_bounded_counters(self):
        module = self.module()
        sampler = mock.Mock()
        sampler.backend.snapshot.return_value = {
            "rssByPid": {7: 100, 8: 50},
            "exitRaces": 0,
        }
        trace = module.MemoryTrace(sampler, 7)
        trace.sample("released", released=True)
        self.assertEqual(trace.points[-1]["hostRssBytes"], 100)
        self.assertEqual(trace.tree_peak, 150)
        sampler.backend.snapshot.return_value = {"rssByPid": {8: 50}, "exitRaces": 0}
        with self.assertRaises(module.RunError):
            trace.sample("released", released=True)

    def test_published_count_metadata_is_preserved_without_weakening_typed_oracle(self):
        module = self.module()
        page = {
            "rows": [
                {
                    "value": "G02F_TOKEN",
                    "count": "16",
                    "missing": False,
                    "rowIndex": 0,
                    "percentage": 100.0,
                }
            ]
        }
        module.verify_count(page, 16)

    def test_results_must_belong_to_the_completed_job_and_source(self):
        module = self.module()
        source = {"sourceId": "source", "generation": "2", "indexedBytes": "100"}
        status = {"source": source, "requestRevision": "1"}
        page = {"jobId": "job", "source": source, "requestRevision": "1"}
        self.assertTrue(
            callable(getattr(module, "verify_page", None)),
            "page provenance check missing",
        )
        module.verify_page(page, "job", status)
        for edit in (
            lambda p: p.update(jobId="other"),
            lambda p: p.update(source={}),
            lambda p: p.update(requestRevision="2"),
        ):
            broken = copy.deepcopy(page)
            edit(broken)
            with self.assertRaises(module.RunError):
                module.verify_page(broken, "job", status)

    def test_deadline_and_release_verification_cannot_silently_succeed(self):
        module = self.module()
        host = mock.Mock()
        host.command.return_value = {"reason": "still_present"}
        with self.assertRaises(module.RunError):
            module.release_checked(host, "job")
        host.command.return_value = {"reason": "job_expired"}
        module.release_checked(host, "job")
        with (
            mock.patch.object(module, "clock", return_value=5),
            self.assertRaises(module.RunError),
        ):
            module.remaining(4)

    def test_runtime_failure_retains_partial_report_and_pending_release(self):
        module = self.module()
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            binary = root / "fake.py"
            binary.write_text("pass\n")
            output = root / "output"
            with (
                mock.patch.object(module, "supported", return_value=True),
                mock.patch.object(
                    module, "validate_corpus", return_value=({"rows": 1000}, "a" * 64)
                ),
                mock.patch.object(
                    module, "run", side_effect=module.RunError("worker_crash")
                ),
            ):
                code = module.main(
                    [
                        "--executable",
                        str(binary),
                        "--corpus",
                        str(root),
                        "--output",
                        str(output),
                    ]
                )
            import json

            report = json.loads((output / "result.json").read_text())
            self.assertEqual(code, 1)
            self.assertFalse(report["passed"])
            self.assertEqual(report["releaseGateStatus"], "pending")
            self.assertEqual(report["evidenceKind"], "test_double")
            self.assertEqual(report["reason"], "worker_crash")

    def test_initial_index_publication_must_precede_appending(self):
        module = self.module()
        self.assertTrue(
            callable(getattr(module, "wait_initial_source", None)),
            "initial readiness gate missing",
        )
        host = mock.Mock()
        source = Path("source.log").resolve()
        host.command.side_effect = [
            {"ciloggState": {"activeFile": str(source), "loadingInProgress": True}},
            {"ciloggState": {"activeFile": str(source), "loadingInProgress": False}},
        ]
        with mock.patch.object(module.time, "sleep"):
            module.wait_initial_source(host, source, module.clock() + 10)
        self.assertEqual(host.command.call_count, 2)

    def test_failed_probe_records_owned_process_cleanup(self):
        module = self.module()
        from workbench_process import ProcessOwner

        owner = ProcessOwner()
        self.addCleanup(owner.close)
        child = owner.spawn([sys.executable, "-c", "import time; time.sleep(30)"])
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            corpus, output = root / "corpus", root / "output"
            corpus.mkdir()
            output.mkdir()
            data = b"line\n"
            (corpus / "ascii.log").write_bytes(data)
            options = SimpleNamespace(
                corpus=corpus,
                output=output,
                executable=Path(sys.executable),
                duration_seconds=10,
            )
            host = mock.MagicMock()
            host.__enter__.return_value = host
            host.__exit__.side_effect = lambda *args: owner.close()
            host.owner = owner
            host.host = child
            host.binary_hash = "a" * 64
            host.version = "test double"
            report = {"repetition": 1}
            with (
                mock.patch.object(module, "DeadlineHost", return_value=host),
                mock.patch.object(
                    module,
                    "ResidentMemorySampler",
                    side_effect=module.RunError("sampler_failed"),
                ),
                self.assertRaisesRegex(module.RunError, "sampler_failed"),
            ):
                module.run(
                    options,
                    {
                        "rows": 1,
                        "files": [
                            {
                                "path": "ascii.log",
                                "sha256": hashlib.sha256(data).hexdigest(),
                            }
                        ],
                    },
                    report,
                )
            self.assertIsNotNone(child.poll())
            self.assertIs(report.get("ownedProcessesReaped"), True)

    def test_unsupported_platform_and_bad_configuration_are_setup_failures(self):
        module = self.module()
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for index, (available, extra) in enumerate(
                (
                    (False, []),
                    (True, ["--duration-seconds", "0"]),
                    (True, ["--repetitions", "4"]),
                    (True, ["--max-tree-rss-bytes", "0"]),
                )
            ):
                output = root / str(index)
                with (
                    mock.patch.object(module, "supported", return_value=available),
                    mock.patch.object(module, "run") as run,
                ):
                    code = module.main(
                        [
                            "--executable",
                            sys.executable,
                            "--corpus",
                            str(root),
                            "--output",
                            str(output),
                            *extra,
                        ]
                    )
                self.assertEqual(code, 2)
                self.assertTrue((output / "result.json").is_file())
                run.assert_not_called()

    def test_oversized_or_nonfinite_partial_report_remains_bounded_failure(self):
        module = self.module()
        import json

        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for index, bad in enumerate((float("nan"), "x" * (4 * 1024**2))):
                output = root / str(index)

                def fail(options, manifest, partial, value=bad):
                    partial["observation"] = value
                    raise module.RunError("worker_failed")

                with (
                    mock.patch.object(module, "supported", return_value=True),
                    mock.patch.object(
                        module,
                        "validate_corpus",
                        return_value=({"rows": 1000}, "a" * 64),
                    ),
                    mock.patch.object(module, "run", side_effect=fail),
                ):
                    code = module.main(
                        [
                            "--executable",
                            sys.executable,
                            "--corpus",
                            str(root),
                            "--output",
                            str(output),
                        ]
                    )
                data = (output / "result.json").read_bytes()
                self.assertEqual(code, 1)
                self.assertLess(len(data), 4 * 1024**2)
                self.assertFalse(json.loads(data)["passed"])

    def test_malformed_source_types_and_identity_are_structured_failures(self):
        module = self.module()
        valid = {
            "state": "completed",
            "complete": True,
            "scannedLines": 1,
            "source": {"sourceId": "source", "generation": "1", "indexedBytes": "5"},
        }
        for source in (
            None,
            [],
            True,
            {"sourceId": True, "generation": True, "indexedBytes": "5"},
            {"sourceId": "source", "generation": "", "indexedBytes": "5"},
        ):
            status = {**valid, "source": source}
            identity = (
                {key: source.get(key) for key in ("sourceId", "generation")}
                if isinstance(source, dict)
                else {"sourceId": "source", "generation": "1"}
            )
            with self.assertRaises(module.RunError):
                module.verify_job(status, 5, 1, identity)
        with self.assertRaises(module.RunError):
            module.verify_page(
                {"jobId": "job", "source": valid["source"], "requestRevision": ""},
                "job",
                {"source": valid["source"], "requestRevision": ""},
            )

    def test_unexpected_nested_attribute_error_cannot_leave_running_report(self):
        module = self.module()
        import json

        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            output = root / "output"
            with (
                mock.patch.object(module, "supported", return_value=True),
                mock.patch.object(
                    module, "validate_corpus", return_value=({"rows": 1000}, "a" * 64)
                ),
                mock.patch.object(
                    module, "run", side_effect=AttributeError("malformed backend")
                ),
            ):
                code = module.main(
                    [
                        "--executable",
                        sys.executable,
                        "--corpus",
                        str(root),
                        "--output",
                        str(output),
                    ]
                )
            report = json.loads((output / "result.json").read_text())
            self.assertEqual(code, 1)
            self.assertEqual(report["state"], "failed")
            self.assertTrue(report["reason"])

    def test_boolean_source_identity_cannot_substitute_for_published_strings(self):
        module = self.module()
        status = {
            "state": "completed",
            "complete": True,
            "scannedLines": 1,
            "source": {"sourceId": True, "generation": True, "indexedBytes": "5"},
        }
        with self.assertRaises(module.RunError):
            module.verify_job(status, 5, 1, {"sourceId": True, "generation": True})

    def test_empty_request_revision_cannot_bind_result_pages(self):
        module = self.module()
        source = {"sourceId": "source", "generation": "1", "indexedBytes": "5"}
        with self.assertRaises(module.RunError):
            module.verify_page(
                {"jobId": "job", "source": source, "requestRevision": ""},
                "job",
                {"source": source, "requestRevision": ""},
            )

    def test_valid_slow_regex_has_a_fresh_count_readiness_deadline(self):
        module = self.module()
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder) / "source.log"
            source.write_bytes(b"line\n")
            now = [0.0]
            host = mock.Mock()
            host.jobs = set()
            identity = {"sourceId": "source", "generation": "1"}

            def status(job, *args):
                return {
                    "state": "completed",
                    "complete": True,
                    "scannedLines": 17,
                    "requestRevision": "1",
                    "source": {**identity, "indexedBytes": str(source.stat().st_size)},
                }

            def submit(request, action):
                host.jobs.add(action)
                return {"jobId": action}

            def finish(job, *args):
                if job == "regex_evaluate":
                    now[0] += 16
                return status(job), []

            def command(action, arguments, **kwargs):
                if action in ("regex_evaluate", "value_counts"):
                    return submit(arguments["analysis_json"], action)
                job = arguments["job_id"]
                if action == "release_analysis_job":
                    host.jobs.discard(job)
                    return {"released": True}
                if action == "get_analysis_job":
                    return {"reason": "job_expired"}
                return {
                    "jobId": job,
                    "source": status(job)["source"],
                    "requestRevision": "1",
                    "rows": []
                    if job == "regex_evaluate"
                    else [{"value": "G02F_TOKEN", "count": "16"}],
                }

            host.start_job.side_effect = submit
            host.status.side_effect = status
            host.finish.side_effect = finish
            host.command.side_effect = command
            host.release.side_effect = host.jobs.discard
            with mock.patch.object(module, "clock", side_effect=lambda: now[0]):
                result = module.cycle(host, mock.Mock(), source, 1, 0, None, 100)
            self.assertEqual(result, (17, 16, identity))

    def test_submission_transport_uses_remaining_readiness_budget(self):
        module = self.module()
        self.assertTrue(
            callable(getattr(module, "submit_job", None)),
            "bounded submission helper missing",
        )
        host = mock.Mock()
        host.command.return_value = {"jobId": "job"}
        host.jobs = set()
        with mock.patch.object(module, "clock", return_value=10):
            module.submit_job(host, {}, "regex_evaluate", 10.25)
        self.assertLessEqual(host.command.call_args.kwargs["timeout"], 0.25)
        self.assertEqual(host.jobs, {"job"})


if __name__ == "__main__":
    unittest.main()
