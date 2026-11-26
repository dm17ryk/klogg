"""What "Make CI Release" may publish when hosted macOS arm64 runners are down (#193)."""
import io
import json
import re
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import check_release_inputs as release  # noqa: E402

ARM_610 = "Mac (arm64, vectorscan, Qt 6.10.1)"
ARM_612 = "Mac (arm64, vectorscan, Qt 6.12.0)"
DIR_610 = "packages-macos-arm-qt6"
DIR_612 = "packages-macos-arm-qt612"


def run(**overrides):
    value = {"id": 42, "path": ".github/workflows/ci-build.yml", "head_branch": "master",
             "status": "completed", "conclusion": "success"}
    value.update(overrides)
    return value


def job(name, conclusion="success", runner="GitHub Actions 7", steps=None):
    return {"name": name, "status": "completed", "conclusion": conclusion,
            "runner_name": runner, "steps": [{"name": "Set up job"}] if steps is None else steps}


def never_started(name):
    return job(name, conclusion="cancelled", runner=None, steps=[])


def green_jobs():
    return [job("SaveVersion"), job("Linux (ubuntu_noble, x64, hyperscan)"),
            job("Mac (x64, hyperscan, Qt 6.10.1)"), job("Windows (x64, hyperscan)")]


class CiRunTests(unittest.TestCase):
    def test_successful_run_releases_everything(self):
        jobs = green_jobs() + [job(ARM_610), job(ARM_612)]
        self.assertEqual(release.check_ci_run(run(), jobs, False), [])

    def test_unsuccessful_run_needs_the_outage_input(self):
        jobs = green_jobs() + [never_started(ARM_610)]
        with self.assertRaisesRegex(release.ReleaseInputError, "not successful"):
            release.check_ci_run(run(conclusion="cancelled"), jobs, False)

    def test_other_workflows_are_rejected(self):
        with self.assertRaisesRegex(release.ReleaseInputError, "not a CI Build run"):
            release.check_ci_run(run(path=".github/workflows/codeql-analysis.yml"), green_jobs(), False)

    def test_outage_names_the_packages_of_jobs_that_never_got_a_runner(self):
        jobs = green_jobs() + [never_started(ARM_612), never_started(ARM_610)]
        self.assertEqual(release.check_ci_run(run(conclusion="failure"), jobs, True), [DIR_610, DIR_612])

    def test_outage_input_keeps_the_package_of_an_arm64_job_that_ran(self):
        jobs = green_jobs() + [job(ARM_610), never_started(ARM_612)]
        self.assertEqual(release.check_ci_run(run(conclusion="cancelled"), jobs, True), [DIR_612])

    def test_arm64_job_that_ran_and_failed_blocks_the_release(self):
        jobs = green_jobs() + [job(ARM_610, conclusion="failure"), never_started(ARM_612)]
        with self.assertRaisesRegex(release.ReleaseInputError, "Qt 6.10.1"):
            release.check_ci_run(run(conclusion="failure"), jobs, True)

    def test_arm64_job_cancelled_on_a_runner_blocks_the_release(self):
        jobs = green_jobs() + [job(ARM_610, conclusion="cancelled"), never_started(ARM_612)]
        with self.assertRaisesRegex(release.ReleaseInputError, "Qt 6.10.1"):
            release.check_ci_run(run(conclusion="cancelled"), jobs, True)

    def test_unknown_arm64_job_blocks_the_release(self):
        jobs = green_jobs() + [never_started("Mac (arm64, vectorscan, Qt 6.13.0)")]
        with self.assertRaisesRegex(release.ReleaseInputError, "unknown macOS arm64 job"):
            release.check_ci_run(run(conclusion="cancelled"), jobs, True)

    def test_only_macos_arm64_jobs_may_be_missing(self):
        jobs = green_jobs()[:-1] + [never_started("Windows (x64, hyperscan)"), never_started(ARM_610)]
        with self.assertRaisesRegex(release.ReleaseInputError, "Windows"):
            release.check_ci_run(run(conclusion="cancelled"), jobs, True)

    def test_outage_release_waits_for_the_run_to_finish(self):
        with self.assertRaisesRegex(release.ReleaseInputError, "still in_progress"):
            release.check_ci_run(run(status="in_progress", conclusion=None), green_jobs(), True)

    def test_outage_release_comes_from_master(self):
        with self.assertRaisesRegex(release.ReleaseInputError, "master"):
            release.check_ci_run(run(head_branch="feat/x", conclusion="cancelled"), green_jobs(), True)

    def test_outage_release_needs_the_job_list(self):
        with self.assertRaisesRegex(release.ReleaseInputError, "no jobs"):
            release.check_ci_run(run(conclusion="cancelled"), [], True)


class MacosPackageTests(unittest.TestCase):
    def make(self, *names):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        for name in names:
            (root / name).mkdir()
        return root

    def test_complete_set_has_nothing_missing(self):
        root = self.make(*release.MACOS_PACKAGE_DIRS, *release.MACOS_ARM64_PACKAGE_DIRS)
        self.assertEqual(release.missing_macos_packages(root), [])

    def test_arm64_packages_are_required_by_default(self):
        root = self.make(*release.MACOS_PACKAGE_DIRS, DIR_610)
        with self.assertRaisesRegex(release.ReleaseInputError, DIR_612):
            release.missing_macos_packages(root)

    def test_allowed_missing_packages_are_listed(self):
        root = self.make(*release.MACOS_PACKAGE_DIRS, DIR_610)
        self.assertEqual(release.missing_macos_packages(root, [DIR_612]), [DIR_612])

    def test_package_of_a_job_that_ran_is_still_required(self):
        # The Qt 6.10.1 arm64 job succeeded but its artifact is gone; only the
        # Qt 6.12 one never got a runner.
        root = self.make(*release.MACOS_PACKAGE_DIRS, DIR_612)
        with self.assertRaisesRegex(release.ReleaseInputError, DIR_610):
            release.missing_macos_packages(root, [DIR_612])

    def test_present_allowed_package_is_not_listed(self):
        root = self.make(*release.MACOS_PACKAGE_DIRS, DIR_610, DIR_612)
        self.assertEqual(release.missing_macos_packages(root, [DIR_612]), [])

    def test_intel_packages_cannot_be_left_out(self):
        root = self.make(*release.MACOS_ARM64_PACKAGE_DIRS)
        with self.assertRaisesRegex(release.ReleaseInputError, "only macOS arm64"):
            release.missing_macos_packages(root, ["packages-macos-intel-qt6"])


class CommandLineTests(unittest.TestCase):
    def call(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = release.main(list(argv))
        return code, out.getvalue(), err.getvalue()

    def test_ci_run_prints_the_left_out_directories(self):
        with tempfile.TemporaryDirectory() as temporary:
            run_file = Path(temporary) / "run.json"
            jobs_file = Path(temporary) / "jobs.json"
            run_file.write_text(json.dumps(run(conclusion="cancelled")), encoding="utf-8")
            # `gh api --paginate --jq '.jobs[]'` prints one JSON value after another.
            jobs = green_jobs() + [job(ARM_610), never_started(ARM_612)]
            jobs_file.write_text("\n".join(json.dumps(item, indent=2) for item in jobs), encoding="utf-8")
            code, out, err = self.call("ci-run", "--run", str(run_file), "--jobs", str(jobs_file),
                                       "--allow-missing-macos-arm64")
        self.assertEqual(code, 0, err)
        self.assertEqual(out.splitlines(), [DIR_612])
        self.assertIn(DIR_612, err)

    def test_macos_packages_prints_one_missing_directory_per_line(self):
        with tempfile.TemporaryDirectory() as temporary:
            for name in release.MACOS_PACKAGE_DIRS:
                (Path(temporary) / name).mkdir()
            code, out, _ = self.call("macos-packages", "--root", temporary,
                                     "--allow-missing", DIR_612, "--allow-missing", DIR_610)
        self.assertEqual(code, 0)
        self.assertEqual(out.splitlines(), [DIR_610, DIR_612])

    def test_errors_exit_non_zero_with_a_message(self):
        with tempfile.TemporaryDirectory() as temporary:
            code, out, err = self.call("macos-packages", "--root", temporary)
        self.assertEqual(code, 1)
        self.assertEqual(out, "")
        self.assertIn("packages-macos-intel-qt6", err)


class WorkflowContractTests(unittest.TestCase):
    """The checker knows the Mac jobs and artifacts by the names ci-build.yml gives them."""

    def mac_rows(self):
        workflow = (ROOT / ".github/workflows/ci-build.yml").read_text(encoding="utf-8")
        self.assertIn("name: Mac (${{ matrix.config.arch }}, ${{ matrix.config.backend }}, "
                      "Qt ${{ matrix.config.qt_version }})", workflow)
        start = workflow.index("\n  Mac:\n")
        section = workflow[start:workflow.index("\n    runs-on:", start)]  # the job's matrix only
        rows = []
        for chunk in section.split("- os: macos")[1:]:
            row = dict(re.findall(r"^\s+(arch|backend|qt_version|artifacts_id): *([^\s#]+)", chunk, re.MULTILINE))
            rows.append(row)
        return rows

    def test_mac_rows_match_the_checker(self):
        rows = self.mac_rows()
        arm64 = {f"Mac (arm64, {row['backend']}, Qt {row['qt_version']})": "packages-" + row["artifacts_id"]
                 for row in rows if row["arch"] == "arm64"}
        others = tuple("packages-" + row["artifacts_id"] for row in rows if row["arch"] != "arm64")
        self.assertEqual(arm64, release.MACOS_ARM64_PACKAGES)
        self.assertEqual(others, release.MACOS_PACKAGE_DIRS)
        for name in arm64:
            self.assertTrue(name.startswith(release.MACOS_ARM64_JOB_PREFIX))


if __name__ == "__main__":
    unittest.main()
