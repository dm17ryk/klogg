"""Black-box checks of deterministic corpus and benchmark exit/report contracts."""

import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "scripts/benchmarks/workbench-benchmark.py"


class WorkbenchBenchmarkTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cilogg-benchmark-tests-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)

    def invoke(self, *arguments, environment=None):
        return subprocess.run(
            [sys.executable, str(RUNNER), *map(str, arguments)],
            capture_output=True,
            text=True,
            timeout=45,
            env=environment or os.environ.copy(),
        )

    def corpus(self, name="corpus", seed=42, rows=20):
        directory = self.directory / name
        result = self.invoke(
            "--generate-corpus", directory, "--seed", seed, "--rows", rows
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        return directory, json.loads(
            (directory / "manifest.json").read_text(encoding="utf-8")
        )

    def test_generator_is_byte_reproducible_and_records_hashes(self):
        first, manifest = self.corpus("first")
        second, again = self.corpus("second")
        self.assertEqual(manifest, again)
        self.assertEqual(
            (first / "manifest.json").read_bytes(),
            (second / "manifest.json").read_bytes(),
        )
        self.assertEqual(manifest["schemaVersion"], 1)
        self.assertGreaterEqual(len(manifest["files"]), 18)
        for entry in manifest["files"]:
            payload = (first / entry["path"]).read_bytes()
            self.assertEqual(hashlib.sha256(payload).hexdigest(), entry["sha256"])
            self.assertEqual(len(payload), entry["bytes"])
            self.assertEqual(payload, (second / entry["path"]).read_bytes())

    def test_reference_oracles_cover_domains_encodings_and_partial_records(self):
        directory, manifest = self.corpus(rows=21)
        entries = {entry["path"]: entry for entry in manifest["files"]}
        ascii_lines = (directory / "ascii.log").read_text(encoding="ascii").splitlines()
        self.assertEqual(len(ascii_lines), 21)
        self.assertEqual(sum(" ERROR " in line for line in ascii_lines), 3)
        self.assertEqual(entries["ascii.log"]["expected"]["errorLines"], 3)
        self.assertIn("😀", (directory / "unicode.log").read_text(encoding="utf-8"))
        self.assertTrue(
            (directory / "utf16le.log").read_bytes().startswith(b"\xff\xfe")
        )
        self.assertIn(b"\r\x00\n\x00", (directory / "utf16le.log").read_bytes())
        self.assertTrue(
            (directory / "utf8bom.log").read_bytes().startswith(b"\xef\xbb\xbf")
        )
        self.assertEqual(
            (directory / "utf8bom.log").read_bytes().count(b"\xef\xbb\xbf"), 1
        )
        self.assertFalse(
            (directory / "partial-final-line.log").read_bytes().endswith(b"\n")
        )
        self.assertIn(b"\x00\xff", (directory / "protocol.rx.bin").read_bytes())
        structured = json.loads(
            (directory / "structured.jsonl").read_text(encoding="utf-8").splitlines()[0]
        )
        self.assertEqual(structured["exactInteger"], 9007199254740993)
        for name in (
            "web.log",
            "build-tests.log",
            "etl.log",
            "frames.log",
            "stacktraces.log",
            "clock-resets.jsonl",
            "malformed.jsonl",
            "high-cardinality.log",
            "quoted.csv",
        ):
            self.assertIn(name, entries)

    def test_committed_reference_corpus_is_byte_exact(self):
        result = self.invoke("--verify-corpus", ROOT / "test_data/workbench")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(json.loads(result.stdout)["valid"])

    def test_seed_changes_content_and_generator_refuses_overwrite(self):
        directory, first = self.corpus()
        _, second = self.corpus("different-seed", seed=43)
        self.assertNotEqual(first["files"], second["files"])
        before = (directory / "manifest.json").read_bytes()
        result = self.invoke("--generate-corpus", directory)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(before, (directory / "manifest.json").read_bytes())

    def test_missing_runtime_is_invalid_setup_with_a_structured_report(self):
        corpus, _ = self.corpus()
        output = self.directory / "run"
        result = self.invoke(
            "--executable",
            self.directory / "absent.exe",
            "--corpus",
            corpus,
            "--scenario",
            "analysis",
            "--output",
            output,
        )
        self.assertEqual(result.returncode, 2, result.stderr)
        report = json.loads((output / "result.json").read_text(encoding="utf-8"))
        self.assertEqual(report["state"], "invalid_setup")
        self.assertFalse(report["passed"])

    def test_tampered_corpus_is_rejected_before_starting_a_process(self):
        corpus, _ = self.corpus()
        with (corpus / "ascii.log").open("ab") as stream:
            stream.write(b"changed")
        result = self.invoke(
            "--executable",
            sys.executable,
            "--corpus",
            corpus,
            "--scenario",
            "analysis",
            "--output",
            self.directory / "run",
        )
        self.assertEqual(result.returncode, 2, result.stderr)
        report = json.loads(
            (self.directory / "run/result.json").read_text(encoding="utf-8")
        )
        self.assertEqual(report["reason"], "corpus_hash_mismatch")

    def fake_run(self, name="run", environment=None, budgets=None):
        corpus, _ = self.corpus(name + "-corpus")
        output = self.directory / name
        arguments = [
            "--executable",
            ROOT / "tests/benchmarks/fake_cilogg.py",
            "--corpus",
            corpus,
            "--scenario",
            "analysis",
            "--repetitions",
            "2",
            "--output",
            output,
        ]
        if budgets is not None:
            path = self.directory / "budgets.json"
            path.write_text(json.dumps(budgets), encoding="utf-8")
            arguments.extend(["--budgets", path])
        result = self.invoke(*arguments, environment=environment)
        self.assertTrue(
            (output / "result.json").is_file(),
            f"Missing benchmark report; exit={result.returncode}\n"
            f"stdout={result.stdout}\nstderr={result.stderr}",
        )
        return result, json.loads((output / "result.json").read_text(encoding="utf-8"))

    def test_runtime_report_labels_cache_scope_test_double_and_open_release_gates(self):
        result, report = self.fake_run()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(report["state"], "passed")
        self.assertEqual(report["evidenceKind"], "test_double")
        self.assertEqual(report["releaseGateStatus"], "pending")
        self.assertEqual(report["cachePolicy"], "os_cache_uncontrolled")
        self.assertEqual(len(report["runs"]), 2)
        self.assertEqual(report["runs"][0]["processState"], "cold_process")
        self.assertEqual(report["runs"][1]["processState"], "warm_process")
        self.assertIn("guiEventLatency", report["unmeasured"])
        self.assertIn("serviceCancellationLatency", report["unmeasured"])
        self.assertEqual(report["budgets"].get("publicCancellationSeconds"), 10)
        self.assertEqual(report["runs"][0]["scannedLines"], 20)
        self.assertEqual(report["runs"][0]["captureBackend"], "benchmark-test-double")

    def test_fast_scan_survives_coarse_monotonic_clock(self):
        shim = self.directory / "clock-shim"
        shim.mkdir()
        (shim / "sitecustomize.py").write_text(
            "import time\ntime.monotonic = lambda: 1.0\n", encoding="utf-8"
        )
        result, report = self.fake_run(
            environment=dict(os.environ, PYTHONPATH=str(shim))
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(report["state"], "passed")
        self.assertEqual(report["runs"][0]["scannedLines"], 20)

    def test_unresolved_scan_duration_does_not_invent_throughput(self):
        shim = self.directory / "clock-shim"
        shim.mkdir()
        (shim / "sitecustomize.py").write_text(
            "import time\ntime.perf_counter = lambda: 1.0\n", encoding="utf-8"
        )
        result, report = self.fake_run(
            environment=dict(os.environ, PYTHONPATH=str(shim))
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        for run in report["runs"]:
            self.assertIsNone(run["scanMiBPerSecondIncludingProtocol"])
            self.assertEqual(run["scanTimingStatus"], "below_clock_resolution")

    def test_slow_fixture_fails_the_execution_gate_with_exit_one(self):
        environment = dict(os.environ, CILOGG_BENCHMARK_FAKE_DELAY_MS="300")
        result, report = self.fake_run(
            environment=environment, budgets={"jobSeconds": 0.05}
        )
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(report["state"], "failed")
        self.assertFalse(report["passed"])
        self.assertIn(report["reason"], ("job_timeout", "transport_timeout"))

    def test_malformed_protocol_is_a_runtime_failure(self):
        environment = dict(os.environ, CILOGG_BENCHMARK_FAKE_INVALID_PROTOCOL="1")
        result, report = self.fake_run(environment=environment)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(report["state"], "failed")

    def test_valid_json_with_invalid_protocol_shapes_is_structured_failure(self):
        for index, shape in enumerate(("[]", "null", '{"result":null}')):
            with self.subTest(shape=shape):
                environment = dict(os.environ, CILOGG_BENCHMARK_FAKE_SHAPE=shape)
                result, report = self.fake_run(
                    name=f"shape-{index}", environment=environment
                )
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(report["reason"], "protocol_failure")

    def test_nonreading_peer_times_out_and_reaps_owned_processes(self):
        started = time.monotonic()
        result, report = self.fake_run(
            environment=dict(os.environ, CILOGG_BENCHMARK_FAKE_STOP_READING="1")
        )
        self.assertLess(time.monotonic() - started, 30)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(report["reason"], "transport_timeout")
        self.assertIn("failures=0", result.stderr)

    def test_opening_is_included_in_timing_and_append_requires_active_scan(self):
        result, report = self.fake_run(
            environment=dict(os.environ, CILOGG_BENCHMARK_FAKE_OPEN_DELAY="0.25")
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertGreaterEqual(report["runs"][0]["indexAndSubmissionSeconds"], 0.25)
        self.assertTrue(report["runs"][0]["appendWhileActive"])

    def test_already_completed_scan_cannot_pass_append_coexistence_gate(self):
        result, report = self.fake_run(
            environment=dict(os.environ, CILOGG_BENCHMARK_FAKE_ALREADY_COMPLETE="1")
        )
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(report["reason"], "append_workload_already_terminal")

    def test_unknown_nonfinite_and_negative_budgets_are_invalid_setup(self):
        for budget in (
            {"invented": 1},
            {"jobSeconds": -1},
            {"jobSeconds": float("nan")},
            {"jobSeconds": 10**400},
        ):
            with self.subTest(budget=budget):
                directory = self.directory / str(len(list(self.directory.iterdir())))
                directory.mkdir()
                corpus, _ = self.corpus(directory.name + "-corpus")
                path = directory / "budget.json"
                path.write_text(json.dumps(budget), encoding="utf-8")
                result = self.invoke(
                    "--executable",
                    sys.executable,
                    "--corpus",
                    corpus,
                    "--output",
                    directory / "run",
                    "--budgets",
                    path,
                )
                self.assertEqual(result.returncode, 2, result.stderr)

    def test_nonfinite_supplied_metadata_is_invalid_setup_with_report(self):
        corpus, _ = self.corpus()
        metadata = self.directory / "metadata.json"
        metadata.write_text('{"compiler": NaN}', encoding="utf-8")
        result = self.invoke(
            "--executable",
            sys.executable,
            "--corpus",
            corpus,
            "--metadata",
            metadata,
            "--output",
            self.directory / "run",
        )
        self.assertEqual(result.returncode, 2, result.stderr)
        report = json.loads(
            (self.directory / "run/result.json").read_text(encoding="utf-8")
        )
        self.assertEqual(report["state"], "invalid_setup")

    def test_manifest_cannot_omit_the_file_used_for_measurement(self):
        corpus, manifest = self.corpus()
        manifest["files"] = [
            entry for entry in manifest["files"] if entry["path"] != "ascii.log"
        ]
        (corpus / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        result = self.invoke(
            "--executable",
            ROOT / "tests/benchmarks/fake_cilogg.py",
            "--corpus",
            corpus,
            "--output",
            self.directory / "run",
            "--repetitions",
            "1",
        )
        self.assertEqual(result.returncode, 2, result.stderr)

    def test_manifest_paths_must_be_relative_and_seed_rows_must_be_integers(self):
        for change in ("absolute", "boolean_rows", "boolean_seed"):
            with self.subTest(change=change):
                corpus, manifest = self.corpus(change)
                if change == "absolute":
                    manifest["files"][0]["path"] = str(corpus / "ascii.log")
                elif change == "boolean_rows":
                    manifest["rows"] = True
                else:
                    manifest["seed"] = True
                (corpus / "manifest.json").write_text(
                    json.dumps(manifest), encoding="utf-8"
                )
                result = self.invoke(
                    "--executable",
                    ROOT / "tests/benchmarks/fake_cilogg.py",
                    "--corpus",
                    corpus,
                    "--output",
                    self.directory / (change + "-output"),
                    "--repetitions",
                    "1",
                )
                self.assertEqual(result.returncode, 2, result.stderr)

    @unittest.skipUnless(
        shutil.which("pwsh"), "PowerShell generator bridge requires pwsh"
    )
    def test_powershell_bridge_preserves_legacy_manifest_and_adds_opt_in_corpus(self):
        script = ROOT / "scripts/benchmarks/generate-log-analysis-corpus.ps1"
        for name, extra in (
            ("legacy", []),
            ("extended", ["-IncludeWorkbench", "-PythonExecutable", sys.executable]),
        ):
            result = subprocess.run(
                [
                    shutil.which("pwsh"),
                    "-NoProfile",
                    "-File",
                    str(script),
                    "-OutputDirectory",
                    str(self.directory / name),
                    "-Lines",
                    "3",
                    "-LongLineKiB",
                    "1",
                    "-FileCount",
                    "1",
                    *extra,
                ],
                capture_output=True,
                text=True,
                timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            (self.directory / "legacy/manifest.json").read_bytes(),
            (self.directory / "extended/manifest.json").read_bytes(),
        )
        self.assertTrue((self.directory / "extended/workbench/manifest.json").is_file())


if __name__ == "__main__":
    unittest.main()
