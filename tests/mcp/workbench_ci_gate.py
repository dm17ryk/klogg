"""Run current-build native parity and record the configured backend provenance."""

import argparse
import hashlib
import json
import logging
import subprocess
from pathlib import Path

from workbench_contracts import ParityError
from workbench_parity import main as run_parity

ROOT = Path(__file__).resolve().parents[2]
LOG = logging.getLogger("workbench.ci")


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def verify_build(executable, cache, expected_backend):
    executable = executable.resolve(strict=True)
    cache = cache.resolve(strict=True)
    if cache.stat().st_size > 2 * 1024 * 1024:
        raise ParityError("cache_size_limit")
    values = {}
    for line in cache.read_text(encoding="utf-8").splitlines():
        if line.startswith(("#", "//")) or "=" not in line or ":" not in line.split("=", 1)[0]:
            continue
        key, value = line.split("=", 1)
        values[key.split(":", 1)[0]] = value
    home = values.get("CMAKE_HOME_DIRECTORY")
    if home is None or Path(home).resolve() != ROOT:
        raise ParityError("source_mismatch")
    if cache.parent not in executable.parents:
        raise ParityError("executable_outside_build")
    if executable.stat().st_size > 512 * 1024 * 1024:
        raise ParityError("executable_size_limit")

    def enabled(key):
        value = values.get(key, "").upper()
        if value not in ("ON", "OFF", "TRUE", "FALSE", "1", "0"):
            raise ParityError("invalid_backend_configuration: " + key)
        return value in ("ON", "TRUE", "1")

    hs = enabled("KLOGG_USE_HYPERSCAN")
    vs = enabled("KLOGG_USE_VECTORSCAN")
    if hs and vs:
        raise ParityError("ambiguous_backend")
    configured = "accelerated" if hs or vs else "qt"
    if configured != expected_backend:
        raise ParityError("backend_mismatch")
    LOG.info("build_verified backend=%s executable=%s", configured, executable)
    return {
        "configuredBackend": configured,
        "searchEngine": "hyperscan" if hs else "vectorscan" if vs else "qt",
        "binarySha256": digest(executable), "cacheSha256": digest(cache),
        "compiler": values.get("CMAKE_CXX_COMPILER", "unreported"),
        "configuration": values.get("CMAKE_BUILD_TYPE", "multi-config"),
        "qtPackage": values.get("Qt6_DIR", values.get("Qt5_DIR", "unreported")),
    }


def git_value(*arguments):
    return subprocess.check_output(
        ["git", "-c", "safe.directory=" + str(ROOT), "-C", str(ROOT), *arguments],
        text=True, timeout=20,
    ).strip()


def main(arguments=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--cache", required=True, type=Path)
    parser.add_argument("--backend", required=True, choices=("qt", "accelerated"))
    parser.add_argument("--artifacts", required=True, type=Path)
    parser.add_argument("--sdk-version", help="Require native smoke with this installed MCP SDK version")
    parser.add_argument("--timeout", type=float, help="Suite seconds (default: SDK 600, raw 300)")
    args = parser.parse_args(arguments)
    logging.basicConfig(level=logging.INFO)
    root = args.artifacts.resolve()
    if root.exists():
        LOG.error("artifacts_exist path=%s", root)
        return 1
    root.mkdir(parents=True)
    report = {"schemaVersion": 1, "passed": False, "guiMode": "offscreen functional checks"}
    try:
        report["build"] = verify_build(args.executable, args.cache, args.backend)
        report["sourceRevision"] = git_value("rev-parse", "HEAD")
        report["productSourceTree"] = git_value("rev-parse", "HEAD:src")
        report["sourceDirty"] = bool(git_value("status", "--porcelain", "--untracked-files=no"))
        parity = root / "parity"
        command = ["--executable", str(args.executable.resolve()), "--task", "G03",
                   "--backend", args.backend, "--timeout",
                   str(args.timeout if args.timeout is not None else (600 if args.sdk_version else 300)),
                   "--artifacts", str(parity)]
        if args.sdk_version:
            command.append("--sdk-smoke")
        code = run_parity(command)
        if code:
            raise ParityError("native_suite_failed: " + str(code))
        path = parity / "result.json"
        if path.is_symlink() or path.stat().st_size > 2 * 1024 * 1024:
            raise ParityError("invalid_parity_report")
        result = json.loads(path.read_text(encoding="utf-8"))
        json.dumps(result, allow_nan=False)
        if not isinstance(result, dict) or result.get("passed") is not True:
            raise ParityError("native_report_not_passed")
        if args.sdk_version:
            sdk = result.get("sdk")
            if not isinstance(sdk, dict):
                raise ParityError("invalid_sdk_metadata")
            if sdk.get("version") != args.sdk_version:
                raise ParityError("sdk_version_mismatch")
        report.update(passed=True, parity=result)
        LOG.info("gate_passed backend=%s sdk=%s", args.backend, args.sdk_version)
    except (OSError, RuntimeError, ValueError, TypeError, KeyError, subprocess.SubprocessError) as error:
        report["reason"] = str(error)
        LOG.error("gate_failed reason=%s", error)
    (root / "result.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False), encoding="utf-8"
    )
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
