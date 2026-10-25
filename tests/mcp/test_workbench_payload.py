"""Portable runtime isolation, including the native grep and SQLite dependencies."""

import importlib
import plistlib
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


class PayloadTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source"
        self.source.mkdir()
        self.artifacts = self.root / "artifacts"
        self.artifacts.mkdir()
        self.module = importlib.import_module("parity_payload")

    def put(self, path, value=b"runtime"):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(value)
        return path

    def test_flat_runtime_contains_grep_shared_libraries_and_sqlite(self):
        executable = self.put(self.source / "cilogg.exe")
        for name in ("cilogg_grep.exe", "Qt6Core.dll", "libtbb.so.12", "libpython.dylib",
                     "sqldrivers/qsqlite.dll", "platforms/qoffscreen.dll"):
            self.put(self.source / name)
        self.put(self.source / "cilogg.conf", b"user-settings")
        self.put(self.source / "cilogg_session.conf", b"user-captures")
        payload, private = self.module.prepare_payload(executable, self.artifacts)
        self.assertNotEqual(private.name, executable.name)
        self.assertEqual(private.parent, payload)
        for name in ("cilogg_grep.exe", "Qt6Core.dll", "libtbb.so.12", "libpython.dylib",
                     "sqldrivers/qsqlite.dll", "platforms/qoffscreen.dll"):
            self.assertTrue((payload / name).is_file(), name)
        self.assertFalse((payload / "cilogg_session.conf").exists())
        self.assertEqual((self.source / "cilogg.conf").read_bytes(), b"user-settings")

    def test_macos_bundle_retains_resources_and_updates_its_executable_identity(self):
        contents = self.source / "cilogg.app" / "Contents"
        executable = self.put(contents / "MacOS/cilogg")
        for name in ("MacOS/cilogg_grep", "Frameworks/QtCore.framework/runtime",
                     "PlugIns/platforms/libqoffscreen.dylib", "Resources/icon.icns"):
            self.put(contents / name)
        self.put(contents / "MacOS/cilogg_session.conf", b"user-captures")
        self.put(contents / "MacOS/plugins/user-plugin", b"user-plugin")
        self.put(contents / "PlugIns/cilogg/catalog/user-plugin", b"user-plugin")
        with (contents / "Info.plist").open("wb") as stream:
            plistlib.dump({"CFBundleExecutable": "cilogg", "CFBundleIdentifier": "org.cilogg"}, stream)
        def signing(arguments, **options):
            marker = Path(arguments[-1]) / "Contents/MacOS/cilogg.conf"
            self.assertIn("versionchecker.enabled=false", marker.read_text())
        with patch("parity_payload.sys.platform", "darwin"), patch(
            "parity_payload.subprocess.run", side_effect=signing
        ):
            payload, private = self.module.prepare_payload(executable, self.artifacts)
        with (payload.parent / "Info.plist").open("rb") as stream:
            info = plistlib.load(stream)
        self.assertEqual(info["CFBundleExecutable"], private.name)
        self.assertNotEqual(info["CFBundleIdentifier"], "org.cilogg")
        self.assertTrue((payload.parent / "Resources/icon.icns").is_file())
        self.assertTrue((payload / "cilogg_grep").is_file())
        self.assertFalse((payload / "cilogg_session.conf").exists())
        self.assertFalse((payload / "plugins").exists())
        self.assertFalse((payload.parent / "PlugIns/cilogg").exists())
        self.assertTrue((payload.parent / "PlugIns/platforms/libqoffscreen.dylib").is_file())
        self.assertTrue(executable.is_file())

    def test_missing_executable_does_not_create_a_payload(self):
        with self.assertRaises(FileNotFoundError):
            self.module.prepare_payload(self.source / "missing", self.artifacts)
        self.assertEqual(list(self.artifacts.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
