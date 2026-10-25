"""A native CI gate needs build provenance and a successful selected suite."""

import importlib
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


class CiGateTests(unittest.TestCase):
    def setUp(self):
        self.module = importlib.import_module("workbench_ci_gate")
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        self.build.mkdir()
        self.exe = self.build / "output/cilogg"
        self.exe.parent.mkdir()
        self.exe.write_bytes(b"fixture")
        self.cache = self.build / "CMakeCache.txt"
        self.write_cache()

    def write_cache(self, hs="OFF", vs="OFF", source=None):
        self.cache.write_text(
            f"CMAKE_HOME_DIRECTORY:INTERNAL={source or self.module.ROOT}\n"
            f"KLOGG_USE_HYPERSCAN:BOOL={hs}\nKLOGG_USE_VECTORSCAN:BOOL={vs}\n"
            "CMAKE_BUILD_TYPE:STRING=Release\nCMAKE_CXX_COMPILER:FILEPATH=compiler\n"
            "Qt6_DIR:PATH=Qt6/lib/cmake/Qt6\n"
        )

    def test_configured_backend_cannot_be_relabelled(self):
        with self.assertRaisesRegex(RuntimeError, "backend_mismatch"):
            self.module.verify_build(self.exe, self.cache, "accelerated")

    def test_foreign_source_cache_is_rejected(self):
        self.write_cache(source=self.root)
        with self.assertRaisesRegex(RuntimeError, "source_mismatch"):
            self.module.verify_build(self.exe, self.cache, "qt")

    def test_installed_executable_is_not_current_build_evidence(self):
        other = self.root / "installed"
        other.write_bytes(b"old")
        with self.assertRaisesRegex(RuntimeError, "outside_build"):
            self.module.verify_build(other, self.cache, "qt")

    def test_ambiguous_backend_is_rejected(self):
        self.write_cache(hs="ON", vs="ON")
        with self.assertRaisesRegex(RuntimeError, "ambiguous_backend"):
            self.module.verify_build(self.exe, self.cache, "accelerated")

    def run_gate(self, report, child_code=0, sdk=None):
        output = self.root / "reports"

        def child(arguments):
            self.child_arguments = arguments
            directory = Path(arguments[arguments.index("--artifacts") + 1])
            directory.mkdir()
            (directory / "result.json").write_text(json.dumps(report))
            return child_code

        arguments = ["--executable", str(self.exe), "--cache", str(self.cache),
                     "--backend", "qt", "--artifacts", str(output)]
        if sdk:
            arguments += ["--sdk-version", sdk]
        with patch.object(self.module, "run_parity", side_effect=child):
            code = self.module.main(arguments)
        return code, json.loads((output / "result.json").read_text())

    def test_child_failure_overrides_a_passed_artifact(self):
        code, report = self.run_gate({"passed": True}, child_code=7)
        self.assertNotEqual(code, 0)
        self.assertFalse(report["passed"])

    def test_wrong_sdk_cannot_pass_requested_profile(self):
        code, report = self.run_gate({"passed": True, "sdk": {"version": "2.3.0"}}, sdk="1.30.0")
        self.assertNotEqual(code, 0)
        self.assertIn("sdk_version_mismatch", report["reason"])
        self.assertEqual(self.child_arguments[self.child_arguments.index("--timeout") + 1], "600")

    def test_success_records_verified_configuration_and_binary_hash(self):
        code, report = self.run_gate({"passed": True})
        self.assertEqual(code, 0)
        self.assertTrue(report["passed"])
        self.assertEqual(report["build"]["configuredBackend"], "qt")
        self.assertEqual(len(report["build"]["binarySha256"]), 64)
        self.assertEqual(len(report["build"]["cacheSha256"]), 64)
        self.assertEqual(self.child_arguments[self.child_arguments.index("--timeout") + 1], "300")

    def test_nonfinite_extension_does_not_prevent_a_failure_report(self):
        code, report = self.run_gate({"passed": True, "extra": float("nan")})
        self.assertNotEqual(code, 0)
        self.assertFalse(report["passed"])

    def test_null_sdk_metadata_still_writes_failure_report(self):
        code, report = self.run_gate({"passed": True, "sdk": None}, sdk="2.3.0")
        self.assertNotEqual(code, 0)
        self.assertFalse(report["passed"])
        self.assertIn("invalid_sdk_metadata", report["reason"])

    def test_array_sdk_metadata_still_writes_failure_report(self):
        code, report = self.run_gate({"passed": True, "sdk": []}, sdk="2.3.0")
        self.assertNotEqual(code, 0)
        self.assertFalse(report["passed"])
        self.assertIn("invalid_sdk_metadata", report["reason"])


if __name__ == "__main__":
    unittest.main()
