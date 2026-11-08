"""The experimental consumer must never remain enabled for package creation."""
import importlib.util
import os
import sys
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "serial_writer_gate", ROOT / "scripts/codex/check-shared-serial-writer.py")
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)


class SerialWriterGateTests(unittest.TestCase):
    def run_gate(self, failure=None, suppress_flag=False, leak_after_restore=False, missing_home=False,
                 sdk_versions=(), missing_sdk=False):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build = root / "build_root"
            build.mkdir()
            cache = build / "CMakeCache.txt"
            executable = build / "cilogg"
            executable.write_bytes(b"fake qualification executable")
            if not missing_sdk:
                for version in sdk_versions:
                    sdk = build / ("workbench-sdk-" + version)
                    python = sdk / ("Scripts/python.exe" if sys.platform == "win32" else "bin/python")
                    python.parent.mkdir(parents=True, exist_ok=True)
                    python.write_bytes(b"fake SDK executable")
            flag = "OFF"

            def write_cache():
                home = "" if missing_home else "CMAKE_HOME_DIRECTORY:INTERNAL=" + str(root) + "\n"
                cache.write_text(home + "KLOGG_BUILD_TESTS:BOOL=ON\n"
                                 "KLOGG_SHARED_SERIAL_WRITER:BOOL=" + flag + "\n", encoding="utf-8")

            write_cache()
            phases = []

            def runner(command, cwd, log, timeout):
                nonlocal flag
                phases.append(log.stem)
                for argument in command:
                    if argument.startswith("-DKLOGG_SHARED_SERIAL_WRITER=") and not suppress_flag:
                        flag = argument.partition("=")[2]
                        write_cache()
                if leak_after_restore and log.stem == "restored-build":
                    flag = "ON"
                    write_cache()
                return 1 if log.stem == failure else 0

            previous = Path.cwd()
            try:
                if missing_home:
                    os.chdir(root)
                result = GATE.qualify(build, executable, "qt", root=root, runner=runner,
                                      sdk_versions=sdk_versions)
            finally:
                os.chdir(previous)
            return result, phases

    def test_success_rebuilds_default_mode_after_native_proof(self):
        result, phases = self.run_gate()
        self.assertTrue(result["ok"])
        self.assertTrue(result["restored"])
        self.assertEqual(result["finalFlag"], "OFF")
        self.assertLess(phases.index("enabled-parity"), phases.index("restore-off"))
        self.assertEqual(phases[-1], "restored-build")

    def test_opt_in_failures_remain_failures_and_still_restore(self):
        for phase in ("enable", "enabled-build", "enabled-ctest", "enabled-parity"):
            with self.subTest(phase=phase):
                result, phases = self.run_gate(failure=phase)
                self.assertFalse(result["ok"])
                self.assertTrue(result["restored"])
                self.assertEqual(result["finalFlag"], "OFF")
                self.assertEqual(phases[-1], "restored-build")

    def test_failed_restoration_blocks_packaging(self):
        result, _ = self.run_gate(failure="restored-build")
        self.assertFalse(result["ok"])
        self.assertFalse(result["restored"])

    def test_successful_command_without_enabled_cache_is_not_proof(self):
        result, phases = self.run_gate(suppress_flag=True)
        self.assertFalse(result["ok"])
        self.assertNotIn("enabled-ctest", phases)
        self.assertTrue(result["restored"])

    def test_rebuild_cannot_leave_the_packaging_cache_enabled(self):
        result, _ = self.run_gate(leak_after_restore=True)
        self.assertFalse(result["ok"])
        self.assertFalse(result["restored"])

    def test_missing_source_binding_is_rejected_even_in_the_source_directory(self):
        with self.assertRaisesRegex(ValueError, "build_source_mismatch"):
            self.run_gate(missing_home=True)

    def test_both_pinned_sdk_profiles_run_before_restoration(self):
        result, phases = self.run_gate(sdk_versions=("1.30.0", "2.3.0"))
        self.assertTrue(result["ok"])
        for version in ("1.30.0", "2.3.0"):
            name = "enabled-sdk-" + version
            self.assertIn(name, phases)
            self.assertLess(phases.index(name), phases.index("restore-off"))

    def test_missing_requested_sdk_is_rejected_before_switching_modes(self):
        with self.assertRaisesRegex(ValueError, "sdk_profile_missing"):
            self.run_gate(sdk_versions=("2.3.0",), missing_sdk=True)


if __name__ == "__main__":
    unittest.main()
