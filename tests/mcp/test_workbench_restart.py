"""Restart evidence must reject retained runtime data and single-window probes."""

import copy
import hashlib
import importlib.util
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
RUNNER = HERE / "workbench_restart.py"


class RestartTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def runner(self):
        self.assertTrue(RUNNER.is_file(), "process-restart runner is not implemented")
        spec = importlib.util.spec_from_file_location("restart_under_test", RUNNER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def evidence(self, module):
        source = self.root / "source.log"
        source.write_bytes(b"first\nsecond\n")
        cases = []
        for name, case in module.CASES.items():
            text = module.pasted_text(case)
            before = {
                "visible": True,
                "floating": case["floating"],
                "pattern": module.pattern(case),
                "scope": "text",
                "textUnits": len(text.encode("utf-16-le")) // 2,
                "textSha256": hashlib.sha256(text.encode()).hexdigest(),
                "jobId": name + "-job",
                "jobComplete": True,
                "jobState": "completed",
                "revision": "2",
                "jobRevision": "2",
                "applyEnabled": False,
            }
            after = {
                "visible": True,
                "floating": case["floating"],
                "pattern": case["default"],
                "scope": "sample",
                "textUnits": 0,
                "textSha256": hashlib.sha256(b"").hexdigest(),
                "jobId": "",
                "jobComplete": False,
                "jobState": "",
                "revision": "1",
                "jobRevision": "",
                "applyEnabled": False,
            }
            cases.append(
                {
                    "id": name,
                    "before": before,
                    "after": after,
                    "oldJobReason": "job_expired",
                    "resultRows": case["rows"],
                }
            )
        saved = self.root / "saved-first"
        saved.mkdir()
        files = []
        for name in ("cilogg.conf", "cilogg_session.conf"):
            target = saved / name
            target.write_bytes(b"private layout bytes\n")
            files.append(
                {
                    "file": "saved-first/" + name,
                    "sha256": hashlib.sha256(target.read_bytes()).hexdigest(),
                }
            )
        executable = self.root / "runtime" / "cilogg"
        executable.parent.mkdir()
        executable.write_bytes(b"private post-signing executable bytes")
        return {
            "schemaVersion": 1,
            "probeKind": "process-restart",
            "binarySha256": "a" * 64,
            "executedBinary": "runtime/cilogg",
            "executedBinarySha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
            "sourcePath": str(source),
            "cases": cases,
            "savedSettings": files,
            "epochs": [
                {
                    "pid": 101,
                    "started": 1.0,
                    "exited": 2.0,
                    "exitCode": 0,
                    "serverExitCode": 0,
                    "windowId": "private-window",
                    "sourceFiles": [str(source)],
                },
                {
                    "pid": 102,
                    "started": 3.0,
                    "exited": 4.0,
                    "exitCode": 0,
                    "serverExitCode": 0,
                    "windowId": "private-window",
                    "sourceFiles": [str(source)],
                },
            ],
        }

    def test_exact_two_process_evidence_is_accepted(self):
        module = self.runner()
        report = self.evidence(module)
        module.validate_report(report, self.root, "a" * 64)

    def test_post_signing_bytes_have_a_separate_verified_identity(self):
        module = self.runner()
        report = self.evidence(module)
        self.assertNotEqual(report["binarySha256"], report["executedBinarySha256"])
        module.validate_report(report, self.root, "a" * 64)
        (self.root / report["executedBinary"]).write_bytes(b"tampered executable")
        with self.assertRaises(module.ParityError):
            module.validate_report(report, self.root, "a" * 64)

    def test_executed_identity_requires_a_bounded_confined_regular_file(self):
        module = self.runner()
        report = self.evidence(module)
        for relative in (
            "../outside",
            str(self.root / "runtime/cilogg"),
            "missing",
            ".",
        ):
            broken = copy.deepcopy(report)
            broken["executedBinary"] = relative
            with self.assertRaises(module.ParityError):
                module.validate_report(broken, self.root, "a" * 64)
        broken = copy.deepcopy(report)
        broken.pop("executedBinarySha256")
        with self.assertRaises(module.ParityError):
            module.validate_report(broken, self.root, "a" * 64)

    def test_close_recreate_or_overlapping_hosts_cannot_pass_as_restart(self):
        module = self.runner()
        report = self.evidence(module)
        for edit in (
            lambda r: r["epochs"].pop(),
            lambda r: r["epochs"][0].update(exitCode=None),
            lambda r: r["epochs"][0].update(serverExitCode=7),
            lambda r: r["epochs"][1].update(started=1.5),
            lambda r: r["epochs"][1].update(started=float("nan")),
        ):
            broken = copy.deepcopy(report)
            edit(broken)
            with self.assertRaises(module.ParityError):
                module.validate_report(broken, self.root, "a" * 64)

    def test_retained_draft_text_jobs_and_reset_layout_are_rejected(self):
        module = self.runner()
        report = self.evidence(module)
        for name, value in (
            ("pattern", "retained"),
            ("scope", "text"),
            ("jobId", "retained-job"),
            ("textUnits", 1),
            ("textSha256", "a" * 64),
            ("applyEnabled", True),
            ("floating", False),
            ("visible", False),
        ):
            with self.subTest(field=name):
                broken = copy.deepcopy(report)
                broken["cases"][0]["after"][name] = value
                with self.assertRaises(module.ParityError):
                    module.validate_report(broken, self.root, "a" * 64)

    def test_live_job_or_missing_revision_is_not_valid_completed_precondition(self):
        module = self.runner()
        report = self.evidence(module)
        for edit in (
            lambda r: r["cases"][0]["before"].update(jobComplete=False),
            lambda r: r["cases"][0]["before"].update(jobState="running"),
            lambda r: r["cases"][0]["before"].pop("revision"),
            lambda r: r["cases"][0]["before"].pop("jobRevision"),
            lambda r: r["cases"][0].update(resultRows=0),
            lambda r: r["cases"][0].update(oldJobReason="still_retained"),
        ):
            broken = copy.deepcopy(report)
            edit(broken)
            with self.assertRaises(module.ParityError):
                module.validate_report(broken, self.root, "a" * 64)

    def test_wrong_identity_missing_case_or_switched_profile_is_rejected(self):
        module = self.runner()
        report = self.evidence(module)
        for edit in (
            lambda r: r.update(binarySha256="b" * 64),
            lambda r: r.update(probeKind="mainwindow"),
            lambda r: r["cases"].pop(),
            lambda r: r["cases"].append(copy.deepcopy(r["cases"][0])),
            lambda r: r["epochs"][1].update(windowId="new-profile"),
            lambda r: r["epochs"][1].update(sourceFiles=[]),
        ):
            broken = copy.deepcopy(report)
            edit(broken)
            with self.assertRaises(module.ParityError):
                module.validate_report(broken, self.root, "a" * 64)

    def test_saved_files_are_bounded_and_must_match_without_runtime_sentinels(self):
        module = self.runner()
        report = self.evidence(module)
        target = self.root / report["savedSettings"][0]["file"]
        target.write_text(module.CASES["regex-lab"]["token"])
        report["savedSettings"][0]["sha256"] = hashlib.sha256(
            target.read_bytes()
        ).hexdigest()
        with self.assertRaises(module.ParityError):
            module.validate_report(report, self.root, "a" * 64)
        target.write_bytes(b"changed")
        with self.assertRaises(module.ParityError):
            module.validate_report(report, self.root, "a" * 64)
        target.write_bytes(b"x" * (2 * 1024 * 1024 + 1))
        with self.assertRaises(module.ParityError):
            module.validate_report(report, self.root, "a" * 64)

    def test_worker_failure_is_a_failure_report(self):
        module = self.runner()
        executable = self.root / "fake.py"
        executable.write_text("raise SystemExit(7)\n")
        output = self.root / "failed"
        with mock.patch.object(
            module, "exercise", side_effect=module.ParityError("child_exit: 7")
        ):
            code = module.main(
                ["--executable", str(executable), "--artifacts", str(output)]
            )
        result = json.loads((output / "result.json").read_text())
        self.assertEqual(code, 1)
        self.assertFalse(result["passed"])
        self.assertEqual(result["releaseGateStatus"], "pending")
        self.assertEqual(result["evidenceKind"], "test_double")

    def test_cli_uses_public_action_flags_and_plain_definition_payload(self):
        module = self.runner()
        self.assertTrue(
            callable(getattr(module, "cli_arguments", None)),
            "action-specific CLI argument mapping is missing",
        )
        request = self.root / "request.json"
        self.assertEqual(
            module.cli_arguments("open_file", {"file": "source.log"}, request),
            ["--file", "source.log"],
        )
        args = module.cli_arguments(
            "set_regex_lab",
            {"window_index": 0, "definition": {"pattern": "x"}},
            request,
        )
        self.assertEqual(args, ["--window-index", "0", "--json-file", str(request)])
        self.assertEqual(json.loads(request.read_text()), {"pattern": "x"})

    def test_payload_free_cli_success_is_supported_but_scalars_and_large_output_are_not(
        self,
    ):
        module = self.runner()
        self.assertTrue(
            callable(getattr(module, "load_cli_output", None)),
            "bounded CLI reply reader is missing",
        )
        output = self.root / "cli.json"
        output.write_bytes(b"")
        self.assertEqual(module.load_cli_output(output), {})
        output.write_text('{"jobId":"x"}')
        self.assertEqual(module.load_cli_output(output), {"jobId": "x"})
        for data in (b"true", b"[]", b"x" * (2 * 1024 * 1024 + 1)):
            output.write_bytes(data)
            with self.assertRaises(module.ParityError):
                module.load_cli_output(output)

    def test_malformed_large_or_nonfinite_observations_still_write_a_failure(self):
        module = self.runner()
        executable = self.root / "fake.py"
        executable.write_bytes(b"not executed")
        report = self.evidence(module)
        report["binarySha256"] = hashlib.sha256(executable.read_bytes()).hexdigest()
        for name, extra in (
            ("oversized", "x" * (2 * 1024 * 1024 + 1)),
            ("nonfinite", float("nan")),
        ):
            broken = copy.deepcopy(report)
            broken["unexpected"] = extra
            output = self.root / name

            def produce(options, artifacts, partial, outcome=broken):
                shutil.copytree(self.root / "saved-first", artifacts / "saved-first")
                shutil.copytree(self.root / "runtime", artifacts / "runtime")
                return outcome

            with mock.patch.object(module, "exercise", side_effect=produce):
                code = module.main(
                    ["--executable", str(executable), "--artifacts", str(output)]
                )
            self.assertEqual(code, 1)
            self.assertFalse(json.loads((output / "result.json").read_text())["passed"])
            self.assertLess((output / "result.json").stat().st_size, 2 * 1024 * 1024)

    def test_successful_synthetic_report_stays_labelled_as_test_double(self):
        module = self.runner()
        executable = self.root / "fake.py"
        executable.write_bytes(b"not executed")
        report = self.evidence(module)
        report["binarySha256"] = hashlib.sha256(executable.read_bytes()).hexdigest()

        def produce(options, artifacts, partial):
            shutil.copytree(self.root / "saved-first", artifacts / "saved-first")
            shutil.copytree(self.root / "runtime", artifacts / "runtime")
            return report

        output = self.root / "success"
        with mock.patch.object(module, "exercise", side_effect=produce):
            code = module.main(
                ["--executable", str(executable), "--artifacts", str(output)]
            )
        self.assertEqual(code, 0)
        result = json.loads((output / "result.json").read_text())
        self.assertTrue(result["passed"])
        self.assertEqual(result["evidenceKind"], "test_double")
        self.assertEqual(result["releaseGateStatus"], "pending")

    def test_source_readiness_waits_for_exact_file_and_completed_index(self):
        module = self.runner()
        self.assertTrue(
            callable(getattr(module, "wait_for_source", None)),
            "source readiness observation is missing",
        )
        source = self.root / "source.log"
        states = iter(
            [
                {"activeFile": "other.log", "loadingInProgress": False},
                {"activeFile": str(source), "loadingInProgress": True},
                {"activeFile": str(source), "loadingInProgress": False},
            ]
        )
        host = mock.Mock()
        host.remaining.return_value = 180
        host.command.side_effect = lambda *args, **kwargs: {"ciloggState": next(states)}
        module.wait_for_source(host, source)
        self.assertEqual(host.command.call_count, 3)

    def test_source_readiness_can_retry_only_failed_readiness_probes(self):
        module = self.runner()
        source = self.root / "source.log"
        host = mock.Mock()
        host.remaining.return_value = 180
        host.command.side_effect = [
            module.RunError("command_failed_dump_state"),
            {"ciloggState": {"activeFile": str(source), "loadingInProgress": False}},
        ]
        module.wait_for_source(host, source)
        self.assertEqual(host.command.call_count, 2)
        host.command.side_effect = module.RunError("protocol_failure")
        with self.assertRaises(module.RunError):
            module.wait_for_source(host, source)

    def test_readiness_retries_cannot_reset_the_fixed_phase_deadline(self):
        module = self.runner()
        now = [0.0]
        host = mock.Mock()
        host.remaining.return_value = 180

        def probe(*args, **kwargs):
            now[0] += min(5, kwargs["timeout"])
            raise module.RunError("command_failed_dump_state")

        host.command.side_effect = probe
        with (
            mock.patch.object(module.time, "perf_counter", side_effect=lambda: now[0]),
            mock.patch.object(
                module.time,
                "sleep",
                side_effect=lambda seconds: now.__setitem__(0, now[0] + seconds),
            ),
            self.assertRaises(module.ParityError),
        ):
            module.wait_for_source(host, self.root / "source.log")
        self.assertLessEqual(now[0], 31)

    def test_row_oracles_preserve_public_line_and_count_json_types(self):
        module = self.runner()
        self.assertTrue(
            callable(getattr(module, "verify_rows", None)),
            "typed result oracle is missing",
        )
        rows = self.regex_rows(module)
        module.verify_rows("regex-lab", rows)
        wrong_lines = copy.deepcopy(rows)
        wrong_lines[0]["lineNumber"] = "1"
        with self.assertRaises(module.ParityError):
            module.verify_rows("regex-lab", wrong_lines)
        value = module.CASES["value-counts"]["token"]
        module.verify_rows("value-counts", [{"value": value, "count": "2"}])
        with self.assertRaises(module.ParityError):
            module.verify_rows("value-counts", [{"value": value, "count": 2}])

    def regex_rows(self, module):
        token = module.CASES["regex-lab"]["token"]
        result = []
        for number, start in ((1, 8), (2, 0)):
            groups = [
                {
                    "number": index,
                    "name": "" if index == 0 else "v",
                    "matched": True,
                    "value": token,
                    "startUtf16": start,
                    "lengthUtf16": len(token),
                }
                for index in (0, 1)
            ]
            result.append(
                {
                    "lineNumber": number,
                    "text": ("שלום 😀 " if number == 1 else "") + token,
                    "matched": True,
                    "offsetConvention": "zero-based UTF-16",
                    "subpatterns": [
                        {
                            "index": 0,
                            "matched": True,
                            "matches": [
                                {
                                    "startUtf16": start,
                                    "lengthUtf16": len(token),
                                    "groups": groups,
                                }
                            ],
                        }
                    ],
                }
            )
        return result

    def test_same_line_numbers_cannot_hide_wrong_text_matches_or_captures(self):
        module = self.runner()
        for edit in (
            lambda r: r[0].update(text="wrong"),
            lambda r: r[0].update(matched=False),
            lambda r: r[0].update(subpatterns=[]),
            lambda r: r[0]["subpatterns"][0]["matches"][0].update(startUtf16=7),
            lambda r: r[0]["subpatterns"][0]["matches"][0]["groups"][1].update(
                name="wrong"
            ),
            lambda r: r[0]["subpatterns"][0]["matches"][0]["groups"][1].update(
                value="wrong"
            ),
        ):
            rows = self.regex_rows(module)
            edit(rows)
            with self.assertRaises(module.ParityError):
                module.verify_rows("regex-lab", rows)

    def test_uppercase_python_fixture_cannot_claim_application_evidence(self):
        module = self.runner()
        executable = self.root / "FAKE.PY"
        executable.write_bytes(b"not executed")
        report = self.evidence(module)
        report["binarySha256"] = hashlib.sha256(executable.read_bytes()).hexdigest()

        def produce(options, artifacts, partial):
            shutil.copytree(self.root / "saved-first", artifacts / "saved-first")
            shutil.copytree(self.root / "runtime", artifacts / "runtime")
            return report

        output = self.root / "uppercase"
        with mock.patch.object(module, "exercise", side_effect=produce):
            code = module.main(
                ["--executable", str(executable), "--artifacts", str(output)]
            )
        self.assertEqual(code, 0)
        self.assertEqual(
            json.loads((output / "result.json").read_text())["evidenceKind"],
            "test_double",
        )

    def test_boolean_counters_and_hidden_retained_job_metadata_are_rejected(self):
        module = self.runner()
        report = self.evidence(module)
        for edit in (
            lambda r: r.update(schemaVersion=True),
            lambda r: r["cases"][1].update(resultRows=True),
            lambda r: r["cases"][0]["after"].update(textUnits=False),
            lambda r: r["cases"][0]["after"].update(jobComplete=True),
            lambda r: r["cases"][0]["after"].update(jobState="completed"),
            lambda r: r["cases"][0]["after"].update(jobRevision="2"),
        ):
            broken = copy.deepcopy(report)
            edit(broken)
            with self.assertRaises(module.ParityError):
                module.validate_report(broken, self.root, "a" * 64)

    def test_real_outer_process_rejects_a_host_that_exits_before_readiness(self):
        self.runner()
        executable = self.root / "fake.py"
        executable.write_text("raise SystemExit(7)\n")
        output = self.root / "real-child-failure"
        result = subprocess.run(
            [
                sys.executable,
                str(RUNNER),
                "--executable",
                str(executable),
                "--artifacts",
                str(output),
                "--timeout",
                "20",
            ],
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertFalse(json.loads((output / "result.json").read_text())["passed"])


if __name__ == "__main__":
    unittest.main()
