"""Keep distribution-specific packages out of generic update candidates."""
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import generate_update_manifest as manifest


class ManifestTests(unittest.TestCase):
    def test_native_platform_variants(self):
        variants = {
            "focal.deb": "focal", "jammy.deb": "jammy", "noble.deb": "noble",
            "debian12.deb": "bookworm", "fedora42.rpm": "fedora42",
            "rhel9.rpm": "rhel9.6", "rhel10.rpm": "rhel10.0",
            "rhel9-compatible.rpm": "rhel9.6", "rhel10-compatible.rpm": "rhel10.0",
            "opensuse16.rpm": "opensuse-leap16.0",
        }
        with tempfile.TemporaryDirectory() as temporary:
            for suffix, variant in variants.items():
                with self.subTest(suffix=suffix):
                    package = Path(temporary) / ("cilogg-26.04.2.1234-" + suffix)
                    package.write_bytes(b"package")
                    asset = manifest.classify(package)
                    self.assertIsNotNone(asset)
                    self.assertEqual(asset["variant"], variant)
                    self.assertEqual(asset["architecture"], "x64")

    def test_modern_dmg_is_manual_install_only(self):
        with tempfile.TemporaryDirectory() as temporary:
            for arch in ("x64", "arm64"):
                dmg = Path(temporary) / f"cilogg-26.04.2-mac-{arch}-qt612.dmg"
                dmg.write_bytes(b"image")
                self.assertIsNone(manifest.classify(dmg))
                update = Path(temporary) / f"cilogg-26.04.2-mac-{arch}-update.zip"
                update.write_bytes(b"bundle")
                asset = manifest.classify(update)
                self.assertEqual(asset["kind"], "macos-bundle")
                self.assertEqual(asset["architecture"], arch)

    def test_manifest_contains_distinct_native_variants(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            packages = root / "packages-fixture"
            packages.mkdir()
            for suffix in ("noble.deb", "debian12.deb", "rhel9.rpm", "rhel10.rpm", "opensuse16.rpm"):
                (packages / ("cilogg-26.04.2-" + suffix)).write_bytes(b"package")
            output = root / "manifest.json"
            with patch.object(sys, "argv", ["manifest", "--root", str(root), "--output", str(output)]):
                self.assertEqual(manifest.main(), 0)
            assets = json.loads(output.read_text(encoding="utf-8"))["assets"]
            self.assertEqual({asset["variant"] for asset in assets},
                             {"noble", "bookworm", "rhel9.6", "rhel10.0", "opensuse-leap16.0"})


if __name__ == "__main__":
    unittest.main()
