"""Exercise the release workflow's legacy metadata update against local Git remotes."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import textwrap
import unittest


WORKFLOW = Path(__file__).resolve().parents[2] / ".github/workflows/ci-release.yml"
STEP = WORKFLOW.read_text(encoding="utf-8").split(
    "    - name: Maintain legacy latest.json\n", 1
)[1].split("    - name: Discord notification\n", 1)[0].split("      run: |\n", 1)[1]
SCRIPT = textwrap.dedent(STEP)


class ReleaseMetadataTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.remote = self.root / "remote.git"
        self.seed = self.root / "seed"
        self.checkout = self.root / "checkout"
        self.runner_temp = self.root / "runner-temp"
        self.runner_temp.mkdir()
        self.git(self.root, "init", "--bare", "--initial-branch=master", str(self.remote))
        self.git(self.root, "init", "--initial-branch=master", str(self.seed))
        self.git(self.seed, "config", "user.name", "Test")
        self.git(self.seed, "config", "user.email", "test@example.invalid")
        self.write_metadata({"ci": "old", "stable": "old", "other": "original"})
        self.git(self.seed, "add", "latest.json")
        self.git(self.seed, "commit", "-m", "initial")
        self.git(self.seed, "remote", "add", "origin", str(self.remote))
        self.git(self.seed, "push", "-u", "origin", "master")
        self.git(self.root, "clone", "--depth=1", self.remote.as_uri(), str(self.checkout))

    def git(self, directory, *args):
        repository = ["--git-dir", str(directory)] if directory == self.remote else ["-C", str(directory)]
        return subprocess.run(
            ["git", *repository, *args], check=True,
            text=True, capture_output=True,
        ).stdout.strip()

    def write_metadata(self, data):
        (self.seed / "latest.json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")

    def run_step(self, channel="ci", ref="refs/heads/master"):
        environment = os.environ.copy()
        environment.update(
            GITHUB_REF=ref, RUNNER_TEMP=str(self.runner_temp),
            RELEASE_CHANNEL=channel, CILOGG_VERSION="26.04.2.1248",
        )
        return subprocess.run(
            ["bash", "--noprofile", "--norc", "-eo", "pipefail", "-c", SCRIPT],
            cwd=self.checkout, env=environment, text=True, capture_output=True,
        )

    def remote_metadata(self):
        return json.loads(self.git(self.remote, "show", "refs/heads/master:latest.json"))

    def test_stale_checkout_preserves_current_metadata(self):
        self.write_metadata({"ci": "old", "stable": "newer", "other": "changed"})
        self.git(self.seed, "commit", "-am", "advance branch")
        self.git(self.seed, "push")
        result = self.run_step()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        data = self.remote_metadata()
        self.assertEqual(data["ci"], "26.04.2.1248")
        self.assertEqual(data["ci_url"], "https://github.com/dm17ryk/klogg/releases/tag/continuous-26.04.2.1248")
        self.assertEqual(data["stable"], "newer")
        self.assertEqual(data["other"], "changed")

    def test_retry_reapplies_update_after_concurrent_push(self):
        replacement = self.root / "replacement.json"
        replacement.write_text(json.dumps({"ci": "old", "stable": "concurrent", "other": "changed"}))
        hook = self.checkout / ".git/hooks/pre-push"
        hook.write_text(
            "#!/bin/sh\n"
            "rm -- \"$0\"\n"
            "unset GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_PREFIX\n"
            f"cp {shlex.quote(str(replacement))} {shlex.quote(str(self.seed / 'latest.json'))}\n"
            f"git -C {shlex.quote(str(self.seed))} commit -am concurrent\n"
            f"git -C {shlex.quote(str(self.seed))} push\n",
            encoding="utf-8",
        )
        hook.chmod(0o755)
        result = self.run_step("stable")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        data = self.remote_metadata()
        self.assertEqual(data["stable"], "26.04.2.1248")
        self.assertEqual(data["stable_url"], "https://github.com/dm17ryk/klogg/releases/tag/v26.04.2.1248")
        self.assertEqual(data["other"], "changed")
        self.assertEqual(data["ci"], "old")

    def test_unchanged_metadata_does_not_push(self):
        self.write_metadata({
            "ci": "26.04.2.1248",
            "ci_url": "https://github.com/dm17ryk/klogg/releases/tag/continuous-26.04.2.1248",
        })
        self.git(self.seed, "commit", "-am", "already updated")
        self.git(self.seed, "push")
        before = self.git(self.remote, "rev-parse", "refs/heads/master")
        hook = self.checkout / ".git/hooks/pre-push"
        hook.write_text("#!/bin/sh\nexit 1\n", encoding="utf-8")
        hook.chmod(0o755)
        result = self.run_step()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.git(self.remote, "rev-parse", "refs/heads/master"), before)

    def test_non_branch_ref_is_rejected(self):
        result = self.run_step(ref="refs/tags/example")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Release ref is not a branch", result.stdout)

    def test_push_failures_are_bounded(self):
        hook = self.checkout / ".git/hooks/pre-push"
        hook.write_text("#!/bin/sh\nexit 1\n", encoding="utf-8")
        hook.chmod(0o755)
        result = self.run_step()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("after 3 attempts", result.stdout)
        self.assertEqual(result.stdout.count("Push attempt"), 3)
        self.assertEqual(self.remote_metadata()["ci"], "old")


if __name__ == "__main__":
    unittest.main()
