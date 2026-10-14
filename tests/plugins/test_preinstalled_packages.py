"""Check release validation against complete and incomplete plugin payloads."""
from __future__ import annotations

import contextlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "validate_preinstalled", ROOT / "scripts/plugins/validate_preinstalled.py")
VALIDATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VALIDATOR)


class PreinstalledPackageTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def payload(self, relative: str = "bin/plugins") -> Path:
        plugins = self.root / relative
        for folder in ("adb-logcat", "syslog-server"):
            destination = plugins / folder
            shutil.copytree(ROOT / "plugins/examples" / folder, destination,
                            ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
            shutil.copy2(ROOT / "plugins/sdk/python/cilogg_plugin.py", destination)
        return plugins

    def validate(self, plugins: Path) -> None:
        with contextlib.redirect_stdout(io.StringIO()):
            VALIDATOR.validate(plugins)

    def test_platform_layouts(self) -> None:
        for layout in ("bin/plugins", "usr/bin/plugins",
                       "cilogg.app/Contents/PlugIns/cilogg"):
            with self.subTest(layout=layout):
                self.validate(self.payload(layout))

    def test_missing_runtime_rejected(self) -> None:
        plugins = self.payload()
        (plugins / "syslog-server/cilogg_plugin.py").unlink()
        with self.assertRaisesRegex(ValueError, "Missing or empty"):
            self.validate(plugins)

    def test_empty_entrypoint_rejected(self) -> None:
        plugins = self.payload()
        (plugins / "adb-logcat/main.py").write_bytes(b"")
        with self.assertRaisesRegex(ValueError, "Missing or empty"):
            self.validate(plugins)

    def test_disabled_default_rejected(self) -> None:
        plugins = self.payload()
        path = plugins / "adb-logcat/plugin.json"
        manifest = json.loads(path.read_text(encoding="utf-8"))
        manifest["enabled_by_default"] = False
        path.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "not enabled by default"):
            self.validate(plugins)

    def test_swapped_manifest_rejected(self) -> None:
        plugins = self.payload()
        shutil.copy2(plugins / "adb-logcat/plugin.json", plugins / "syslog-server/plugin.json")
        with self.assertRaisesRegex(ValueError, "Unexpected preinstalled plugin ID"):
            self.validate(plugins)


if __name__ == "__main__":
    unittest.main()
