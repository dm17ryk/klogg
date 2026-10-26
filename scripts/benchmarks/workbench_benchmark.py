"""Versioned benchmark reports. A scenario pass is not a full release gate pass."""

import argparse
import json
import logging
import math
import os
import platform
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

from workbench_corpus import generate_corpus, json_bytes, validate_corpus
from workbench_gui import GUI_BUDGETS, run_gui
from workbench_resident import RESIDENT_BUDGET
from workbench_runtime import BenchmarkHost, RunError, file_hash, measure_analysis

LOG = logging.getLogger("workbench.benchmark")
DEFAULT_BUDGETS = {
    "jobSeconds": 120.0,
    "publicCancellationSeconds": 10.0,
    "appendSeconds": 5.0,
    "hostPeakBytes": 512 * 1024 * 1024,
}


def load_budgets(path, scenario="analysis", sample_resident_memory=False):
    result = dict(GUI_BUDGETS if scenario == "ui-service" else DEFAULT_BUDGETS)
    if scenario == "ui-service" and sample_resident_memory and os.name == "nt":
        result.update(RESIDENT_BUDGET)
    if path is not None:
        if not path.is_file() or path.stat().st_size > 8192:
            raise ValueError("invalid_budget_file")
        values = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(values, dict):
            raise ValueError("invalid_budgets")
        for key, value in values.items():
            if (
                key not in result
                or isinstance(value, bool)
                or not isinstance(value, (int, float))
            ):
                raise ValueError("invalid_budgets")
            maximum = (
                16 * 1024**3
                if key.endswith("Bytes")
                else (600000 if key.endswith("Ms") else 600)
            )
            if not 0 < value <= maximum or not math.isfinite(value):
                raise ValueError("invalid_budgets")
            if key.endswith("Bytes") and value != int(value):
                raise ValueError("invalid_budgets")
            result[key] = value
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--generate-corpus", type=Path)
    modes.add_argument("--verify-corpus", type=Path)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--rows", type=int, default=10000)
    parser.add_argument("--executable", type=Path)
    parser.add_argument("--corpus", type=Path)
    parser.add_argument(
        "--scenario", choices=("analysis", "ui-service"), default="analysis"
    )
    parser.add_argument("--test-executable", type=Path)
    parser.add_argument("--samples", type=int, default=100)
    parser.add_argument(
        "--platform", choices=("offscreen", "native"), default="offscreen"
    )
    parser.add_argument("--output", type=Path)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--budgets", type=Path)
    parser.add_argument(
        "--sample-resident-memory",
        action="store_true",
        help="Opt in to bounded Windows job-member working-set polls for ui-service",
    )
    parser.add_argument(
        "--metadata",
        type=Path,
        help="Build/SDK/backend provenance JSON; unknown fields remain explicit",
    )
    parser.add_argument("--verbose", action="store_true")
    options = parser.parse_args(argv)
    logging.basicConfig(
        level=logging.DEBUG if options.verbose else logging.INFO,
        format="%(name)s %(levelname)s %(message)s",
        stream=sys.stderr,
    )
    if options.sample_resident_memory and (
        options.generate_corpus is not None or options.verify_corpus is not None
    ):
        LOG.error("setup_rejected reason=resident_sampling_requires_ui_service")
        return 2
    if options.generate_corpus is not None:
        try:
            generate_corpus(options.generate_corpus, options.rows, options.seed)
            return 0
        except (OSError, ValueError) as error:
            LOG.error("generation_rejected reason=%s", error)
            return 2
    if options.verify_corpus is not None:
        try:
            manifest, digest = validate_corpus(options.verify_corpus)
            print(
                json.dumps(
                    {
                        "valid": True,
                        "files": len(manifest["files"]),
                        "manifestSha256": digest,
                    }
                )
            )
            return 0
        except (OSError, ValueError, KeyError, TypeError) as error:
            LOG.error("verification_rejected reason=%s", error)
            return 2
    if options.output is None:
        parser.error("--output is required for a benchmark run")
    report = {
        "schemaVersion": 1,
        "scenario": options.scenario,
        "sampleResidentMemory": options.sample_resident_memory,
        "passed": False,
        "state": "invalid_setup",
        "releaseGateStatus": "pending",
        "recordedAtUtc": datetime.now(timezone.utc).isoformat(),
        "harnessModules": {
            path.name: file_hash(path)
            for path in sorted(Path(__file__).parent.glob("workbench*.py"))
        },
        "machine": {
            "platform": platform.platform(),
            "architecture": platform.machine(),
            "processor": platform.processor(),
            "logicalProcessors": os.cpu_count(),
            "python": platform.python_version(),
        },
        "runs": [],
        "cachePolicy": "os_cache_uncontrolled",
        "unmeasured": [
            "guiEventLatency",
            "scrollLatency",
            "serviceCancellationLatency",
            "workerPeakResidentMemory",
            "processTreePeakCommitBytes",
            "processTreeSampledRssPeakBytes",
            "hostSampledRssPeakBytes",
            "workersSampledRssPeakBytes",
            "sustainedMemorySlope",
            "osCacheColdRuns",
            "richRendererOverhead",
        ],
    }
    try:
        if options.output.exists() or options.output.is_symlink():
            raise ValueError("output_destination_exists")
        options.output.mkdir(parents=True)
        options.output = options.output.resolve()
    except (OSError, ValueError) as error:
        LOG.error("output_rejected reason=%s", error)
        return 2
    try:
        if options.corpus is None:
            raise ValueError("missing_corpus")
        manifest, digest = validate_corpus(options.corpus)
        report["corpus"] = {
            "seed": manifest["seed"],
            "rows": manifest["rows"],
            "manifestSha256": digest,
        }
        if options.executable is None or not options.executable.is_file():
            raise ValueError("executable_not_found")
        if not 1 <= options.repetitions <= 10:
            raise ValueError("invalid_repetitions")
        options.corpus = options.corpus.resolve()
        if options.sample_resident_memory and options.scenario != "ui-service":
            raise ValueError("resident_sampling_requires_ui_service")
        report["budgets"] = load_budgets(
            options.budgets, options.scenario, options.sample_resident_memory
        )
        if options.scenario == "ui-service":
            if not 100 <= options.samples <= 1000:
                raise ValueError("invalid_samples")
            if options.test_executable is None or not options.test_executable.is_file():
                raise ValueError("test_executable_not_found")
            if (
                options.test_executable.resolve().parent
                != options.executable.resolve().parent
            ):
                raise ValueError("native_deployment_mismatch")
            if manifest["rows"] < 1000:
                raise ValueError("native_corpus_too_small")
            options.test_executable = options.test_executable.resolve()
            report["unmeasured"].append("scrollPresentationLatency")
        report["build"] = {
            "binarySha256": file_hash(options.executable),
            "sdk": "unreported",
            "backend": "unreported",
        }
        if options.metadata is not None:
            if options.metadata.stat().st_size > 65536:
                raise ValueError("build_metadata_limit")
            metadata = json.loads(options.metadata.read_text(encoding="utf-8"))
            if not isinstance(metadata, dict):
                raise ValueError("invalid_build_metadata")
            # Reject nonfinite JSON before it can poison the final report.
            json_bytes(metadata)
            report["build"]["suppliedMetadata"] = metadata
        report["evidenceKind"] = (
            "test_double"
            if options.executable.suffix.lower() == ".py"
            or (
                options.test_executable is not None
                and options.test_executable.suffix.lower() == ".py"
            )
            else "application"
        )
    except (OSError, ValueError, KeyError, TypeError) as error:
        report["reason"] = str(error)
        LOG.error("setup_rejected reason=%s", error)
        (options.output / "result.json").write_bytes(json_bytes(report))
        return 2
    try:
        if options.scenario == "ui-service":
            run_gui(options, manifest, report)
        else:
            with BenchmarkHost(options.executable, options.output) as host:
                report["build"]["binarySha256"] = host.binary_hash
                report["build"]["reportedVersion"] = host.version
                report["startupSeconds"] = host.startup_seconds
                for repetition in range(options.repetitions):
                    report["runs"].append(
                        measure_analysis(
                            host,
                            options.corpus,
                            manifest,
                            options.output,
                            report["budgets"],
                            repetition,
                        )
                    )
        report.update(state="passed", passed=True)
        exit_code = 0
    except (
        OSError,
        ValueError,
        KeyError,
        TypeError,
        RuntimeError,
        subprocess.SubprocessError,
    ) as error:
        report.update(
            state="failed",
            reason=str(error) if isinstance(error, RunError) else type(error).__name__,
        )
        if getattr(error, "operation", None):
            report["failedOperation"] = error.operation
        LOG.error("benchmark_failed reason=%s", report["reason"])
        exit_code = 1
    except KeyboardInterrupt:
        report.update(state="cancelled", reason="interrupted")
        exit_code = 1
    (options.output / "result.json").write_bytes(json_bytes(report))
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
