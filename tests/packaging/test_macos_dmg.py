"""Exercise DMG recovery without requiring macOS tools or mounting disks."""
import plistlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, call, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import package_macos_dmg as dmg


class DmgTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="dmg build with spaces ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.app = self.root / "output/cilogg.app"
        (self.app / "Contents/MacOS").mkdir(parents=True)
        (self.app / "Contents/MacOS/cilogg").write_bytes(b"app")
        self.calls = []
        self.images = []
        self.cpack_ok = False
        self.busy = 0
        self.stuck = False
        self.verify_ok = True
        self.sources = []
        self.patch_run = patch.object(dmg.subprocess, "run", self.run_tool)
        self.patch_run.start()
        self.addCleanup(self.patch_run.stop)
        self.patch_cpack = patch.object(dmg, "_run_cpack", lambda root: self.run_tool(
            ["cpack", "--verbose", "-G", "DragNDrop"], check=False), create=True)
        self.patch_cpack.start()
        self.addCleanup(self.patch_cpack.stop)
        self.sleep = patch.object(dmg.time, "sleep").start()
        self.addCleanup(patch.stopall)

    def run_tool(self, args, **kwargs):
        args = [str(arg) for arg in args]
        self.calls.append(args)
        code, output = 0, b""
        if args[0] == "cpack":
            code = 0 if self.cpack_ok else 1
            if self.cpack_ok:
                target = self.root / "packages/cilogg-test-OSX.dmg"
                target.parent.mkdir(exist_ok=True)
                target.write_bytes(b"cpack image")
        elif args[:2] == ["hdiutil", "info"]:
            output = plistlib.dumps({"images": self.images})
        elif args[:2] == ["hdiutil", "detach"]:
            if self.stuck:
                code = 1
            else:
                self.images = [image for image in self.images if not any(
                    entity.get("dev-entry") == args[2]
                    for entity in image.get("system-entities", []))]
        elif args[0] == "ditto":
            shutil.copytree(args[1], args[2], symlinks=True)
        elif args[0] == "ln":
            # Windows does not require symlink privileges for this fixture.
            Path(args[-1]).write_text("/Applications", encoding="utf-8")
        elif args[:2] == ["hdiutil", "create"]:
            source = Path(args[args.index("-srcfolder") + 1])
            self.sources.append(sorted(path.name for path in source.iterdir()))
            self.assertTrue((source / "cilogg.app/Contents/MacOS/cilogg").is_file())
            Path(args[-1]).write_bytes(b"partial" if self.busy else b"fallback image")
            if self.busy:
                self.busy -= 1
                code = 1
        elif args[:2] == ["hdiutil", "verify"]:
            code = 0 if self.verify_ok else 1
        else:
            self.fail(f"Unexpected native command: {args}")
        result = subprocess.CompletedProcess(args, code, stdout=output)
        if code and kwargs.get("check"):
            raise subprocess.CalledProcessError(code, args, output=output)
        return result

    def owned_image(self, device="/dev/disk9"):
        return {"image-path": str(self.root / "_CPack_Packages/OSX/DragNDrop/temp.dmg"),
                "system-entities": [{"dev-entry": device},
                                    {"dev-entry": device + "s1", "mount-point": "/Volumes/CILogg 1"}]}

    def test_cpack_success_is_verified_without_fallback(self):
        self.cpack_ok = True
        result = dmg.package(self.root, "test")
        self.assertEqual(result.read_bytes(), b"cpack image")
        self.assertEqual([call[:2] for call in self.calls],
                         [["cpack", "--verbose"], ["hdiutil", "verify"]])

    def test_headless_packaging_never_starts_finder_or_cpack(self):
        self.cpack_ok = True
        result = dmg.package(self.root, "test", headless=True)
        self.assertEqual(result.read_bytes(), b"fallback image")
        self.assertEqual(self.sources, [["Applications", "cilogg.app"]])
        self.assertFalse(any(call[0] in ("cpack", "osascript") for call in self.calls))
        self.assertTrue(any(call[:2] == ["hdiutil", "verify"] for call in self.calls))

    def test_headless_packaging_keeps_bounded_create_retries(self):
        self.busy = 2
        result = dmg.package(self.root, "test", headless=True)
        self.assertEqual(result.read_bytes(), b"fallback image")
        outputs = [call[-1] for call in self.calls if call[:2] == ["hdiutil", "create"]]
        self.assertEqual(len(set(outputs)), 3)
        self.assertFalse(any(call[0] in ("cpack", "osascript") for call in self.calls))

    def test_finder_failure_cleans_owned_device_and_retries_busy_fallback(self):
        unrelated = {"image-path": str(self.root / "unrelated.dmg"),
                     "system-entities": [{"dev-entry": "/dev/disk2"}]}
        self.images = [self.owned_image(), unrelated]
        self.busy = 1
        result = dmg.package(self.root, "test")
        self.assertEqual(result.read_bytes(), b"fallback image")
        self.assertEqual(self.sources, [["Applications", "cilogg.app"]] * 2)
        self.assertEqual(self.images, [unrelated])
        self.assertIn(["hdiutil", "detach", "/dev/disk9", "-force"], self.calls)
        self.assertFalse(any("/dev/disk2" in call for call in self.calls))
        outputs = [call[-1] for call in self.calls if call[:2] == ["hdiutil", "create"]]
        self.assertEqual(len(set(outputs)), 2)

    def test_cpack_timeout_detaches_owned_image_and_publishes_verified_fallback(self):
        unrelated = {"image-path": str(self.root / "unrelated.dmg"),
                     "system-entities": [{"dev-entry": "/dev/disk2"}]}
        self.images = [self.owned_image(), unrelated]
        timeout = subprocess.TimeoutExpired(["cpack"], 600)
        with patch.object(dmg, "_run_cpack", side_effect=timeout) as run_cpack:
            result = dmg.package(self.root, "test")
        run_cpack.assert_called_once_with(self.root.resolve())
        self.assertEqual(result.read_bytes(), b"fallback image")
        self.assertEqual(self.images, [unrelated])
        self.assertTrue(any(call[:2] == ["hdiutil", "verify"] for call in self.calls))

    def test_interruption_does_not_start_image_recovery(self):
        with patch.object(dmg, "_run_cpack", side_effect=KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt):
                dmg.package(self.root, "test")
        self.assertFalse(self.calls)

    def test_stuck_mount_fails_without_starting_fallback(self):
        self.images, self.stuck = [self.owned_image()], True
        with self.assertRaisesRegex(RuntimeError, "still mounted"):
            dmg.package(self.root, "test")
        self.assertFalse(self.sources)
        self.assertEqual(sum(call[:2] == ["hdiutil", "detach"] for call in self.calls), 5)

    def test_bad_device_is_never_detached(self):
        self.images = [self.owned_image("/dev/not-a-disk")]
        with self.assertRaisesRegex(RuntimeError, "device"):
            dmg.package(self.root, "test")
        self.assertFalse(any(call[:2] == ["hdiutil", "detach"] for call in self.calls))

    def test_failed_verification_never_publishes_fallback(self):
        self.verify_ok = False
        with self.assertRaises(subprocess.CalledProcessError):
            dmg.package(self.root, "test")
        self.assertFalse((self.root / "packages/cilogg-test-OSX.dmg").exists())

    def test_exhausted_create_retries_fail(self):
        self.busy = 10
        with self.assertRaisesRegex(RuntimeError, "creation failed"):
            dmg.package(self.root, "test")
        self.assertEqual(len(self.sources), 3)
        self.assertFalse((self.root / "packages/cilogg-test-OSX.dmg").exists())

    def test_invalid_version_is_rejected_before_native_commands(self):
        with self.assertRaises(ValueError):
            dmg.package(self.root, "../other")
        self.assertFalse(self.calls)


class CpackSupervisorTests(unittest.TestCase):
    def test_completed_exit_status_is_preserved(self):
        for status in (0, 1):
            with self.subTest(status=status):
                process = Mock()
                process.wait.return_value = status
                with patch.object(dmg.subprocess, "Popen", return_value=process):
                    self.assertEqual(dmg._run_cpack(Path.cwd()).returncode, status)

    def test_timeout_kills_process_group_and_reaps_before_propagating(self):
        root = Path.cwd()
        process = Mock(pid=4321)
        timeout = subprocess.TimeoutExpired(["cpack"], 3)
        process.wait.side_effect = [timeout, -9]
        with patch.object(dmg.os, "name", "posix"), \
                patch.object(dmg.signal, "SIGKILL", 9, create=True), \
                patch.object(dmg.os, "killpg", create=True) as kill_group, \
                patch.object(dmg.subprocess, "Popen", return_value=process) as popen:
            events = Mock()
            events.attach_mock(process, "process")
            events.attach_mock(kill_group, "kill_group")
            with self.assertRaises(subprocess.TimeoutExpired):
                dmg._run_cpack(root, timeout=3)
        self.assertTrue(popen.call_args.kwargs["start_new_session"])
        self.assertEqual(popen.call_args.kwargs["cwd"], root)
        self.assertEqual(events.mock_calls, [call.process.wait(timeout=3),
                                            call.kill_group(4321, 9),
                                            call.process.wait(timeout=30)])

    def test_unreaped_process_is_a_hard_failure(self):
        root = Path.cwd()
        process = Mock(pid=4321)
        process.wait.side_effect = [subprocess.TimeoutExpired(["cpack"], 3),
                                    subprocess.TimeoutExpired(["cpack"], 30)]
        with patch.object(dmg.os, "name", "posix"), \
                patch.object(dmg.signal, "SIGKILL", 9, create=True), \
                patch.object(dmg.os, "killpg", create=True), \
                patch.object(dmg.subprocess, "Popen", return_value=process):
            with self.assertRaisesRegex(RuntimeError, "refusing DMG recovery"):
                dmg._run_cpack(root, timeout=3)

    def test_interruption_terminates_and_reaps_before_propagating(self):
        root = Path.cwd()
        process = Mock(pid=4321)
        interrupted = KeyboardInterrupt()
        process.wait.side_effect = [interrupted, -9]
        with patch.object(dmg.os, "name", "posix"), \
                patch.object(dmg.signal, "SIGKILL", 9, create=True), \
                patch.object(dmg.os, "killpg", create=True) as kill_group, \
                patch.object(dmg.subprocess, "Popen", return_value=process):
            with self.assertRaises(KeyboardInterrupt) as caught:
                dmg._run_cpack(root)
        self.assertIs(caught.exception, interrupted)
        kill_group.assert_called_once_with(4321, 9)
        self.assertEqual(process.wait.call_count, 2)


@unittest.skipUnless(sys.platform == "darwin", "requires native macOS disk images")
class NativeDmgTests(unittest.TestCase):
    def test_headless_image_has_installable_layout_without_cpack(self):
        with tempfile.TemporaryDirectory(prefix="cilogg headless dmg ") as directory:
            root = Path(directory).resolve()
            app = root / "output/cilogg.app/Contents/MacOS"
            app.mkdir(parents=True)
            executable = app / "cilogg"
            executable.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            executable.chmod(0o755)
            mount = root / "mounted image"
            mount.mkdir()
            try:
                with patch.object(dmg, "_run_cpack", side_effect=AssertionError("CPack must not run")):
                    image = dmg.package(root, "native-headless", headless=True)
                dmg._run(["hdiutil", "attach", "-readonly", "-nobrowse", "-mountpoint",
                          str(mount), str(image)])
                installed = mount / "cilogg.app/Contents/MacOS/cilogg"
                self.assertEqual(installed.read_bytes(), executable.read_bytes())
                self.assertTrue(installed.stat().st_mode & 0o111)
                self.assertTrue((mount / "Applications").is_symlink())
                self.assertEqual((mount / "Applications").readlink(), Path("/Applications"))
            finally:
                if mount.is_mount():
                    dmg._run(["hdiutil", "detach", str(mount), "-force"], timeout=30)

    def test_real_fallback_recovers_own_mount_and_has_installable_layout(self):
        with tempfile.TemporaryDirectory(prefix="cilogg native dmg ") as directory:
            root = Path(directory).resolve()
            app = root / "output/cilogg.app/Contents/MacOS"
            app.mkdir(parents=True)
            executable = app / "cilogg"
            executable.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            executable.chmod(0o755)
            cpack = root / "_CPack_Packages"
            cpack.mkdir()
            stale_image = cpack / "temp.dmg"
            stale_mount, final_mount = root / "stale mount", root / "final mount"
            stale_mount.mkdir()
            final_mount.mkdir()
            real_run = dmg._run

            try:
                real_run(["hdiutil", "create", "-size", "16m", "-fs", "HFS+",
                          "-volname", "CILogg", str(stale_image)])
                real_run(["hdiutil", "attach", "-nobrowse", "-mountpoint",
                          str(stale_mount), str(stale_image)])
                with patch.object(dmg, "_run_cpack", side_effect=subprocess.TimeoutExpired(
                        ["cpack"], 600)):
                    image = dmg.package(root, "native-test")
                self.assertFalse(stale_mount.is_mount())
                real_run(["hdiutil", "attach", "-readonly", "-nobrowse", "-mountpoint",
                          str(final_mount), str(image)])
                installed = final_mount / "cilogg.app/Contents/MacOS/cilogg"
                self.assertEqual(installed.read_bytes(), executable.read_bytes())
                self.assertTrue(installed.stat().st_mode & 0o111)
                self.assertTrue((final_mount / "Applications").is_symlink())
                self.assertEqual((final_mount / "Applications").readlink(), Path("/Applications"))
            finally:
                for mount in (final_mount, stale_mount):
                    if mount.is_mount():
                        real_run(["hdiutil", "detach", str(mount), "-force"], timeout=30)


if __name__ == "__main__":
    unittest.main()
