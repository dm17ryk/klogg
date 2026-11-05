"""Verify the offline terminal dependency has no unrecorded source changes."""
import hashlib
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
VENDOR = ROOT / "3rdparty/libvterm"


class VendorLockTests(unittest.TestCase):
    def test_exact_vendored_source_set_and_bytes(self):
        lock = json.loads((VENDOR / "SOURCE.json").read_text(encoding="utf-8"))
        self.assertEqual(lock["archiveSha256"], "09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0")
        actual = {p.relative_to(VENDOR).as_posix() for directory in ("src", "include")
                  for p in (VENDOR / directory).rglob("*") if p.is_file()}
        actual.add("LICENSE")
        self.assertEqual(actual, set(lock["vendoredSha256"]))
        for name, expected in lock["vendoredSha256"].items():
            with self.subTest(path=name):
                self.assertEqual(hashlib.sha256((VENDOR / name).read_bytes()).hexdigest(), expected)
        self.assertIn("Permission is hereby granted", (VENDOR / "LICENSE").read_text())


if __name__ == "__main__":
    unittest.main()
