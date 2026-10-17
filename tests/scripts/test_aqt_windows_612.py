"""Installer compatibility checks; run in the pinned aqtinstall environment."""

import importlib.util
import unittest
from pathlib import Path
from unittest.mock import patch

from aqt.archives import QtArchives
from semantic_version import Version

ADAPTER_PATH = Path(__file__).resolve().parents[2] / "scripts/codex/aqt-windows-612.py"
SPEC = importlib.util.spec_from_file_location("aqt_windows_612", ADAPTER_PATH)
adapter = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(adapter)


class WindowsMetadataTests(unittest.TestCase):
    def test_only_requested_windows_kits_change_metadata_folder(self):
        cases = [
            ("windows", "desktop", "6.12.0", "win64_msvc2022_64", "_msvc2022_64"),
            (
                "windows", "desktop", "6.12.0", "win64_msvc2022_arm64_cross_compiled",
                "_msvc2022_arm64_cross_compiled",
            ),
            ("windows", "desktop", "6.10.1", "win64_msvc2022_64", None),
            ("windows", "desktop", "6.13.0", "win64_msvc2022_64", None),
            ("windows", "desktop", "6.12.0", "win64_mingw", None),
            ("mac", "desktop", "6.10.1", "clang_64", None),
            ("mac", "desktop", "6.12.0", "clang_64", None),
            ("linux", "desktop", "6.12.0", "linux_gcc_64", None),
        ]
        for host, target, qt_version, arch, expected in cases:
            with self.subTest(host=host, version=qt_version, arch=arch):
                archive = QtArchives.__new__(QtArchives)
                archive.os_name = host
                archive.target = target
                archive.version = Version(qt_version)
                archive.arch = arch
                original = adapter._original_arch_ext(archive)
                self.assertEqual(
                    adapter.windows_arch_ext(archive), original if expected is None else expected
                )

    def test_unknown_installer_version_fails_before_patching(self):
        original = QtArchives._arch_ext
        with patch.object(adapter, "version", return_value="3.4.0"):
            with self.assertRaisesRegex(RuntimeError, "aqtinstall==3.3.0"):
                adapter.main()
        self.assertIs(QtArchives._arch_ext, original)


if __name__ == "__main__":
    unittest.main()
