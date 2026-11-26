#!/usr/bin/env python3
"""Check what "Make CI Release" is about to publish (#193).

``ci-run``: the CI Build run named by the ``ci-run-id`` input must be
successful. With ``--allow-missing-macos-arm64`` it may instead be a finished
master run whose only unsuccessful jobs are macOS arm64 jobs that never got a
runner. The package directories of exactly those jobs are printed, one per
line; the release may leave out only these.

``macos-packages``: the downloaded artifacts must contain every macOS package
directory except those named with ``--allow-missing``. The allowed ones that
are absent are printed, one per line, for the release notes.

See "Hosted macOS runner outages" in docs/qt-platforms.md.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

CI_WORKFLOW = ".github/workflows/ci-build.yml"
RELEASE_BRANCH = "master"
# ci-build.yml names Mac jobs "Mac (<arch>, <backend>, Qt <qt_version>)" and
# uploads each row's packages as "packages-<artifacts_id>".
MACOS_ARM64_JOB_PREFIX = "Mac (arm64,"
MACOS_PACKAGE_DIRS = ("packages-macos-intel-qt6", "packages-macos-intel-qt612")
MACOS_ARM64_PACKAGES = {
    "Mac (arm64, vectorscan, Qt 6.10.1)": "packages-macos-arm-qt6",
    "Mac (arm64, vectorscan, Qt 6.12.0)": "packages-macos-arm-qt612",
}
MACOS_ARM64_PACKAGE_DIRS = tuple(MACOS_ARM64_PACKAGES.values())


class ReleaseInputError(Exception):
    """The release must not go ahead with these inputs."""


def load_json_values(text: str) -> list:
    """Parse JSON values written one after another, as ``gh api --paginate --jq '.jobs[]'`` does."""
    decoder = json.JSONDecoder()
    values = []
    index = 0
    while True:
        while index < len(text) and text[index].isspace():
            index += 1
        if index == len(text):
            return values
        value, index = decoder.raw_decode(text, index)
        values.append(value)


def never_got_a_runner(job: dict) -> bool:
    """A job cancelled (or skipped) while still queued: no runner, no steps."""
    return (job.get("conclusion") in ("cancelled", "skipped")
            and not job.get("runner_name") and not job.get("steps"))


def check_ci_run(run: dict, jobs: list, allow_missing_macos_arm64: bool) -> list:
    """Return the package directories of the macOS arm64 jobs that never got a runner.

    Raises ReleaseInputError when the run must not be released.
    """
    run_id = run.get("id")
    if not str(run.get("path", "")).startswith(CI_WORKFLOW):
        raise ReleaseInputError(f"run {run_id} is not a CI Build run ({run.get('path')})")
    if not allow_missing_macos_arm64:
        if run.get("conclusion") != "success":
            raise ReleaseInputError(
                f"CI Build run {run_id} is {run.get('status')}/{run.get('conclusion')}, not successful. "
                "Set allow-missing-macos-arm64 only if its macOS arm64 jobs got no runner.")
        return []
    if run.get("status") != "completed":
        raise ReleaseInputError(
            f"CI Build run {run_id} is still {run.get('status')}. Cancel it once every job "
            "except the queued macOS arm64 ones has finished, then start the release again.")
    if run.get("head_branch") != RELEASE_BRANCH:
        raise ReleaseInputError(
            f"CI Build run {run_id} built {run.get('head_branch')}; an outage release comes from {RELEASE_BRANCH}")
    if not jobs:
        raise ReleaseInputError(f"CI Build run {run_id} has no jobs")
    left_out = []
    problems = []
    for job in jobs:
        name = job.get("name", "?")
        if job.get("conclusion") == "success":
            continue
        if name.startswith(MACOS_ARM64_JOB_PREFIX) and never_got_a_runner(job):
            if name in MACOS_ARM64_PACKAGES:
                left_out.append(MACOS_ARM64_PACKAGES[name])
            else:
                problems.append(f"{name}: unknown macOS arm64 job; add it to MACOS_ARM64_PACKAGES")
        else:
            problems.append(f"{name}: {job.get('status')}/{job.get('conclusion')}")
    if problems:
        raise ReleaseInputError(
            "only macOS arm64 jobs that never got a runner may be left out, but these jobs did not succeed: "
            + "; ".join(problems))
    return [directory for directory in MACOS_ARM64_PACKAGE_DIRS if directory in left_out]


def missing_macos_packages(root: Path, allowed_missing=()) -> list:
    """Return the directories in allowed_missing that are absent.

    Raises ReleaseInputError when any other macOS package directory is absent.
    """
    unknown = [name for name in allowed_missing if name not in MACOS_ARM64_PACKAGE_DIRS]
    if unknown:
        raise ReleaseInputError("only macOS arm64 packages may be left out, not: " + ", ".join(unknown))
    every = MACOS_PACKAGE_DIRS + MACOS_ARM64_PACKAGE_DIRS
    absent = [name for name in every if name not in allowed_missing and not (root / name).is_dir()]
    if absent:
        raise ReleaseInputError("missing macOS package directories: " + ", ".join(absent))
    return [name for name in MACOS_ARM64_PACKAGE_DIRS if name in allowed_missing and not (root / name).is_dir()]


def parse_args(argv: list) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    ci_run = commands.add_parser("ci-run", help="check the CI Build run a release is made from")
    ci_run.add_argument("--run", type=Path, required=True, help="GET /actions/runs/{id} response")
    ci_run.add_argument("--jobs", type=Path, required=True, help="the run's jobs, as JSON values")
    ci_run.add_argument("--allow-missing-macos-arm64", action="store_true",
                        help="accept macOS arm64 jobs that never got a runner")
    packages = commands.add_parser("macos-packages", help="check the downloaded macOS packages")
    packages.add_argument("--root", type=Path, required=True, help="directory holding packages-* artifacts")
    packages.add_argument("--allow-missing", action="append", default=[], metavar="DIR",
                          help="a macOS arm64 package directory that may be absent (repeatable)")
    return parser.parse_args(argv)


def main(argv: list) -> int:
    args = parse_args(argv)
    try:
        if args.command == "ci-run":
            run = json.loads(args.run.read_text(encoding="utf-8"))
            jobs = load_json_values(args.jobs.read_text(encoding="utf-8"))
            for directory in check_ci_run(run, jobs, args.allow_missing_macos_arm64):
                print(f"Left out, its job never got a runner: {directory}", file=sys.stderr)
                print(directory)
        else:
            for directory in missing_macos_packages(args.root, args.allow_missing):
                print(directory)
    except ReleaseInputError as error:
        print(f"Release check failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
