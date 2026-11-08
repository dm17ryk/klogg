#!/usr/bin/env python3
"""Qualify the experimental serial owner, then rebuild the default packaging mode."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts" / "benchmarks"))
from workbench_process import ProcessOwner


def cache_value(cache, key):
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(key + ":"):
            return line.partition("=")[2]
    return None


def run_phase(command, cwd, log, timeout):
    owner = ProcessOwner()
    try:
        with log.open("wb") as output:
            child = owner.spawn(command, cwd=cwd, stdout=output, stderr=subprocess.STDOUT)
            try:
                return child.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                return 124
    finally:
        owner.close()


def qualify(build, executable, backend, *, config="Release", report=None, root=ROOT,
            runner=run_phase, sdk_versions=()):
    root, build, executable = root.resolve(), build.resolve(), executable.resolve()
    build.relative_to(root / "build_root")
    executable.relative_to(build)
    cache = build / "CMakeCache.txt"
    home = cache_value(cache, "CMAKE_HOME_DIRECTORY")
    if not home or Path(home).resolve() != root:
        raise ValueError("build_source_mismatch")
    if cache_value(cache, "KLOGG_SHARED_SERIAL_WRITER") != "OFF":
        raise ValueError("qualification_requires_initial_off")
    if cache_value(cache, "KLOGG_BUILD_TESTS") != "ON":
        raise ValueError("qualification_requires_tests")
    sdk_profiles = []
    for version in sdk_versions:
        if version not in ("1.30.0", "2.3.0"):
            raise ValueError("unsupported_sdk_profile")
        sdk = build / ("workbench-sdk-" + version)
        python = sdk / ("Scripts/python.exe" if sys.platform == "win32" else "bin/python")
        if not python.is_file():
            raise ValueError("sdk_profile_missing:" + version)
        sdk_profiles.append((version, python))
    report = report or build / "reports" / "shared-serial-writer"
    report = report.resolve()
    report.relative_to(build)
    report.mkdir(parents=True, exist_ok=True)
    result = {"initialFlag": "OFF", "phases": [], "ok": False, "restored": False}

    def phase(name, command, timeout):
        log = report / (name + ".log")
        started = time.monotonic()
        code = runner(command, root, log, timeout)
        entry = {"name": name, "exitCode": code, "seconds": time.monotonic() - started,
                 "command": [str(part) for part in command], "log": str(log)}
        result["phases"].append(entry)
        print(json.dumps(entry), flush=True)
        if code:
            raise RuntimeError(name + "_failed")

    configure = ["cmake", "-S", str(root), "-B", str(build)]
    compile_command = ["cmake", "--build", str(build), "--config", config,
                       "--target", "ci_build", "--parallel", "2"]
    try:
        phase("enable", configure + ["-DKLOGG_SHARED_SERIAL_WRITER=ON"], 600)
        if cache_value(cache, "KLOGG_SHARED_SERIAL_WRITER") != "ON":
            raise RuntimeError("enable_not_confirmed")
        phase("enabled-build", compile_command, 2400)
        if sys.platform == "win32":
            phase("enabled-deploy", ["windeployqt", str(executable)], 120)
        phase("enabled-ctest", ["ctest", "--test-dir", str(build), "-C", config,
                                "--output-on-failure"], 900)
        phase("enabled-parity", [sys.executable, str(root / "tests/mcp/workbench_ci_gate.py"),
                                 "--executable", str(executable), "--cache", str(cache),
                                 "--backend", backend, "--artifacts", str(report / "native")], 600)
        for version, python in sdk_profiles:
            phase("enabled-sdk-" + version,
                  [str(python), str(root / "tests/mcp/workbench_ci_gate.py"),
                   "--executable", str(executable), "--cache", str(cache), "--backend", backend,
                   "--sdk-version", version, "--artifacts", str(report / ("native-sdk-" + version))],
                  900)
        result["enabledBinarySha256"] = hashlib.sha256(executable.read_bytes()).hexdigest()
        result["ok"] = True
    except (OSError, RuntimeError) as error:
        result["error"] = str(error)
    finally:
        # This gate runs before packaging: restore/rebuild even when the opt-in proof failed.
        try:
            phase("restore-off", configure + ["-DKLOGG_SHARED_SERIAL_WRITER=OFF"], 600)
            if cache_value(cache, "KLOGG_SHARED_SERIAL_WRITER") != "OFF":
                raise RuntimeError("restore_not_confirmed")
            phase("restored-build", compile_command, 2400)
            if cache_value(cache, "KLOGG_SHARED_SERIAL_WRITER") != "OFF":
                raise RuntimeError("restore_build_changed_mode")
            result["restored"] = True
        except (OSError, RuntimeError) as error:
            result["restoreError"] = str(error)
            result["ok"] = False
        result["finalFlag"] = cache_value(cache, "KLOGG_SHARED_SERIAL_WRITER")
        (report / "qualification.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--backend", choices=("qt", "accelerated"), required=True)
    parser.add_argument("--config", default="Release")
    parser.add_argument("--sdk-version", action="append", default=[], choices=("1.30.0", "2.3.0"))
    args = parser.parse_args()
    try:
        result = qualify(args.build_dir, args.executable, args.backend, config=args.config,
                         sdk_versions=args.sdk_version)
    except (OSError, ValueError) as error:
        print("Shared serial qualification rejected:", error, file=sys.stderr)
        return 1
    return 0 if result["ok"] and result["restored"] and result["finalFlag"] == "OFF" else 1


if __name__ == "__main__":
    sys.exit(main())
