"""Execute the CMake staging hook and check its RPM spec escaping contract."""
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


@unittest.skipUnless(shutil.which("cmake"), "CMake is required for the RPM staging hook")
class PrivateRpmHookTests(unittest.TestCase):
    def test_spec_filters_exact_private_capabilities_and_preserves_definitions(self):
        capabilities = ["libQt6Core.so.6()(64bit)",
                        "libQt6Core.so.6(Qt_6.12)(64bit)", "libtest+.so.1()(64bit)"]
        with tempfile.TemporaryDirectory(prefix="rpm hook ") as temporary:
            root = Path(temporary)
            libraries = root / "stage/usr/lib/cilogg/lib"
            libraries.mkdir(parents=True)
            (libraries / "libQt6Core.so.6").write_bytes(b"fixture")
            windows = sys.platform == "win32"
            rpmdeps = root / ("rpmdeps.cmd" if windows else "rpmdeps")
            lines = ["@echo off"] if windows else ["#!/bin/sh"]
            lines += [f"echo {capability}" if windows else f"echo '{capability}'"
                      for capability in capabilities]
            rpmdeps.write_text("\n".join(lines) + "\n", encoding="utf-8")
            rpmdeps.chmod(0o755)
            source = Path(__file__).resolve().parents[2] / "cmake/PrivateRuntimeRpm.cmake.in"
            hook = root / "hook.cmake"
            hook.write_text(source.read_text(encoding="utf-8").replace(
                "@KLOGG_PRIVATE_DIR@", "lib/cilogg"), encoding="utf-8")
            output = root / "spec.txt"
            driver = root / "driver.cmake"
            driver.write_text(f'''
set(CPACK_GENERATOR RPM)
set(CPACK_TEMPORARY_DIRECTORY "{(root / 'stage').as_posix()}")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(_rpmdeps "{rpmdeps.as_posix()}")
set(CPACK_RPM_SPEC_MORE_DEFINE "%global existing_definition preserved")
include("{hook.as_posix()}")
file(WRITE "{output.as_posix()}" "${{CPACK_RPM_SPEC_MORE_DEFINE}}")
''', encoding="utf-8")
            result = subprocess.run(["cmake", "-P", str(driver)], capture_output=True,
                                    text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            spec = output.read_text(encoding="utf-8")
            self.assertIn("%global existing_definition preserved", spec)
            self.assertIn("%global __provides_exclude_from .*/lib/cilogg/.*", spec)
            pattern = next(line.removeprefix("%global __requires_exclude ")
                           for line in spec.splitlines()
                           if line.startswith("%global __requires_exclude "))
            self.assertIn(r"\\.", pattern)  # RPM spec parsing consumes one escaping layer.
            regex = re.compile(pattern.replace("\\\\", "\\"))
            for capability in capabilities:
                self.assertIsNotNone(regex.fullmatch(capability), capability)
            for external in ("libc.so.6()(64bit)", "libstdc++.so.6()(64bit)",
                             "libQt6CoreXsoX6()(64bit)"):
                self.assertIsNone(regex.fullmatch(external), external)


if __name__ == "__main__":
    unittest.main()
