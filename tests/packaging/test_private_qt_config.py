"""Exercise the installed Qt paths for each native package directory layout."""
import configparser
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(shutil.which("cmake"), "CMake is required for the install script")
class PrivateQtConfigTests(unittest.TestCase):
    def test_staged_config_resolves_plugins_and_rejects_missing_platform(self):
        source = Path(__file__).resolve().parents[2] / "cmake/InstallPrivateQtConfig.cmake.in"
        for libdir in ("lib", "lib64", "lib/aarch64-linux-gnu"):
            for missing in (False, True):
                with self.subTest(libdir=libdir, missing=missing), tempfile.TemporaryDirectory(
                        prefix="private Qt config ") as temporary:
                    root = Path(temporary)
                    prefix = root / "stage/usr"
                    private = prefix / libdir / "cilogg"
                    (private / "bin").mkdir(parents=True)
                    for plugin in ("platforms/libqxcb.so", "sqldrivers/libqsqlite.so",
                                   "platforms/libqoffscreen.so"):
                        if missing and "offscreen" in plugin:
                            continue
                        path = private / "qt-plugins" / plugin
                        path.parent.mkdir(parents=True, exist_ok=True)
                        path.write_bytes(b"fixture")
                    script = root / "install.cmake"
                    script.write_text(source.read_text(encoding="utf-8").replace(
                        "@KLOGG_PRIVATE_DIR@", f"{libdir}/cilogg"), encoding="utf-8")
                    environment = os.environ.copy()
                    environment.pop("DESTDIR", None)
                    result = subprocess.run(["cmake", f"-DCMAKE_INSTALL_PREFIX={prefix.as_posix()}",
                                             "-P", str(script)], env=environment,
                                            capture_output=True, text=True, timeout=30)
                    config_path = private / "bin/qt.conf"
                    if missing:
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn("libqoffscreen.so", result.stdout + result.stderr)
                        self.assertFalse(config_path.exists())
                        continue
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    config = configparser.ConfigParser()
                    config.read(config_path, encoding="utf-8")
                    paths = config["Paths"]
                    qt_prefix = (config_path.parent / paths["Prefix"]).resolve()
                    self.assertEqual(qt_prefix, private.resolve())
                    self.assertTrue((qt_prefix / paths["Plugins"] /
                                     "platforms/libqoffscreen.so").is_file())


if __name__ == "__main__":
    unittest.main()
