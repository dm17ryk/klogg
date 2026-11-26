"""scripts/plugins/build_catalog.py: packages and plugins.json of a plugin repository (E15a)."""
import hashlib
import importlib.util
import json
import os
import pathlib
import re
import tempfile
import unittest
import urllib.parse
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("build_catalog", ROOT / "scripts/plugins/build_catalog.py")
build_catalog = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build_catalog)

SHA = re.compile(r"^[0-9a-f]{64}$")


def write(path: pathlib.Path, text: str, newline: str = "\n") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.replace("\n", newline).encode("utf-8"))


def plugin(folder: pathlib.Path, identifier="org.example.tool", version="1.0.0", newline="\n",
           extra=None) -> pathlib.Path:
    manifest = {"id": identifier, "name": "Example tool", "description": "Counts lines",
                "version": version, "protocol_version": 1, "executable": "python",
                "arguments": ["main.py"], "capabilities": ["tool"]}
    manifest.update(extra or {})
    write(folder / "plugin.json", json.dumps(manifest, indent=2), newline)
    write(folder / "main.py", "print('tool')\n", newline)
    write(folder / "tests/test_main.py", "assert True\n", newline)
    write(folder / "__pycache__/main.cpython-312.pyc", "compiled")
    return folder


class BuildCatalogTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def run_script(self, *arguments):
        return build_catalog.main([str(argument) for argument in arguments])

    def catalog(self, folder="catalog"):
        return json.loads((self.root / folder / "plugins.json").read_text(encoding="utf-8"))

    def test_package_folder_gives_entries_the_host_accepts(self):
        source = plugin(self.root / "tool")
        code = self.run_script("--package", source, "--os", "windows", "--os", "linux",
                               "--arch", "x86_64", "--exclude", "tests/**",
                               "--catalog", self.root / "catalog",
                               "--base-url", "https://example.com/repo/")
        self.assertEqual(code, 0)
        document = self.catalog()
        self.assertEqual(document["schema_version"], 1)
        self.assertEqual([(e["os"], e["arch"]) for e in document["plugins"]],
                         [("windows", "x86_64"), ("linux", "x86_64")])
        archive = self.root / "catalog/packages/org.example.tool-1.0.0.zip"
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        for entry in document["plugins"]:
            # The rules CILogg applies to an entry (src/plugins/src/plugincatalog.cpp).
            self.assertEqual(entry["protocol_version"], 1)
            self.assertRegex(entry["id"], r"^[a-z0-9][a-z0-9._-]{0,127}$")
            self.assertRegex(entry["sha256"], SHA)
            self.assertEqual(entry["sha256"], digest)
            self.assertEqual(entry["url"],
                             "https://example.com/repo/packages/org.example.tool-1.0.0.zip")
            self.assertEqual(entry["name"], "Example tool")
            self.assertNotIn("kind", entry)
        with zipfile.ZipFile(archive) as package:
            self.assertEqual(sorted(package.namelist()), ["main.py", "plugin.json"])
            self.assertTrue(all(info.compress_type == zipfile.ZIP_STORED
                                and info.date_time == (1980, 1, 1, 0, 0, 0)
                                for info in package.infolist()))

    def test_rebuild_is_identical_and_line_endings_do_not_matter(self):
        lf = plugin(self.root / "lf")
        crlf = plugin(self.root / "crlf", newline="\r\n")
        for name, folder in (("first", lf), ("second", crlf)):
            self.assertEqual(self.run_script("--package", folder, "--exclude", "tests/**",
                                             "--catalog", self.root / name,
                                             "--base-url", "https://example.com"), 0)
        first = (self.root / "first/packages/org.example.tool-1.0.0.zip").read_bytes()
        self.assertEqual(first, (self.root / "second/packages/org.example.tool-1.0.0.zip").read_bytes())
        before = (self.root / "first/plugins.json").stat().st_mtime_ns
        self.assertEqual(self.run_script("--package", lf, "--exclude", "tests/**",
                                         "--catalog", self.root / "first",
                                         "--base-url", "https://example.com"), 0)
        self.assertEqual((self.root / "first/plugins.json").stat().st_mtime_ns, before)
        self.assertEqual((self.root / "first/packages/org.example.tool-1.0.0.zip").read_bytes(), first)

    def test_a_published_version_never_changes(self):
        source = plugin(self.root / "tool")
        arguments = ("--package", source, "--catalog", self.root / "catalog",
                     "--base-url", "https://example.com")
        self.assertEqual(self.run_script(*arguments), 0)
        published = (self.root / "catalog/plugins.json").read_text(encoding="utf-8")
        write(source / "main.py", "print('changed')\n")
        self.assertEqual(self.run_script(*arguments), 1)
        self.assertEqual((self.root / "catalog/plugins.json").read_text(encoding="utf-8"), published)
        plugin(source, version="1.0.1")
        write(source / "main.py", "print('changed')\n")
        self.assertEqual(self.run_script(*arguments), 0)
        self.assertEqual([e["version"] for e in self.catalog()["plugins"]], ["1.0.1"])
        self.assertTrue((self.root / "catalog/packages/org.example.tool-1.0.0.zip").exists())

    def test_sources_json_with_checkouts_and_azure_items_addresses(self):
        repository = self.root / "checkouts/CatalogRepo"
        plugin(repository / "packs/tool")
        plugin(self.root / "checkouts/Tools/projTools/other", identifier="org.example.other",
               version="2.1.0-beta.1")
        sources = repository / "sources.json"
        write(sources, json.dumps({"packages": [
            {"repository": "self", "path": "packs/tool", "os": ["any"], "arch": ["any"]},
            {"repository": "Tools", "ref": "refs/heads/main", "path": "projTools/other",
             "os": ["windows"], "arch": ["x86_64", "arm64"], "exclude": ["tests/**"]},
        ]}))
        code = self.run_script("--sources", sources, "--checkouts", self.root / "checkouts",
                               "--catalog", self.root / "catalog", "--organization", "contoso",
                               "--project", "Lab Tools", "--repository", "CatalogRepo",
                               "--branch", "catalog")
        self.assertEqual(code, 0)
        entries = self.catalog()["plugins"]
        self.assertEqual([(e["id"], e["os"], e["arch"]) for e in entries], [
            ("org.example.tool", "any", "any"),
            ("org.example.other", "windows", "x86_64"),
            ("org.example.other", "windows", "arm64"),
        ])
        url = urllib.parse.urlparse(entries[1]["url"])
        self.assertEqual((url.scheme, url.netloc), ("https", "dev.azure.com"))
        self.assertEqual(url.path, "/contoso/Lab%20Tools/_apis/git/repositories/CatalogRepo/items")
        self.assertEqual(urllib.parse.parse_qs(url.query), {
            "path": ["/packages/org.example.other-2.1.0-beta.1.zip"],
            "versionDescriptor.versionType": ["branch"],
            "versionDescriptor.version": ["catalog"],
            "$format": ["octetStream"],
            "download": ["true"],
            "api-version": ["7.1"],
        })
        with zipfile.ZipFile(self.root / "catalog/packages/org.example.other-2.1.0-beta.1.zip") as package:
            self.assertNotIn("tests/test_main.py", package.namelist())

    def test_configuration_pack_lists_its_contents(self):
        folder = self.root / "pack"
        write(folder / "plugin.json", json.dumps({
            "id": "org.example.config", "name": "Config", "version": "1.2.0",
            "protocol_version": 1, "kind": "config-pack", "pack_format": 1,
            "min_host_version": "26.12", "license": "Proprietary",
            "content": {"previews": "previews.json", "filters": "filters.conf",
                        "lua": {"folder": "lua", "load": ["a.lua"]}}}))
        write(folder / "previews.json", "{}")
        write(folder / "filters.conf", "")
        write(folder / "lua/a.lua", "-- a\n")
        self.assertEqual(self.run_script("--package", folder, "--catalog", self.root / "catalog",
                                         "--base-url", "https://example.com"), 0)
        entry = self.catalog()["plugins"][0]
        self.assertEqual(entry["kind"], "config-pack")
        self.assertEqual(entry["contents"], ["previews", "filters", "lua"])
        self.assertEqual((entry["os"], entry["arch"]), ("any", "any"))
        self.assertEqual(entry["min_host_version"], "26.12")
        self.assertEqual(entry["license"], "Proprietary")

    def test_python_decoder_pack_lists_its_code(self):
        # E16b: the GUI asks about a pack's code from the entry's `contents` before an install,
        # so a pack with Python decoders must list them (the public example pack included).
        folder = self.root / "python-pack"
        write(folder / "plugin.json", json.dumps({
            "id": "org.example.python", "name": "Python", "version": "1.0.0",
            "protocol_version": 1, "kind": "config-pack", "pack_format": 1,
            "content": {"previews": "previews.json",
                        "python_decoders": [{"id": "record", "module": "python/record.py"}]}}))
        write(folder / "previews.json", "{}")
        write(folder / "python/record.py", "def decode(payload, context):\n    return None\n")
        example = ROOT / "plugins/examples/python-decoder-pack"
        self.assertEqual(self.run_script("--package", folder, "--package", example,
                                         "--catalog", self.root / "catalog",
                                         "--base-url", "https://example.com"), 0)
        entries = {entry["id"]: entry for entry in self.catalog()["plugins"]}
        self.assertEqual(entries["org.example.python"]["contents"],
                         ["previews", "python_decoders"])
        self.assertEqual(entries["org.cilogg.example.python-decoder-pack"]["contents"],
                         ["previews", "python_decoders"])

    def test_invalid_input_is_refused(self):
        bad = plugin(self.root / "bad", version="1.02.0")
        self.assertEqual(self.run_script("--package", bad, "--catalog", self.root / "c1",
                                         "--base-url", "https://example.com"), 1)
        self.assertFalse((self.root / "c1/plugins.json").exists())
        good = plugin(self.root / "good")
        with self.assertRaises(SystemExit):
            self.run_script("--package", good, "--catalog", self.root / "c2",
                            "--base-url", "http://example.com")
        with self.assertRaises(SystemExit):
            self.run_script("--package", good, "--catalog", self.root / "c2",
                            "--base-url", "https://user:secret@example.com")
        with self.assertRaises(SystemExit):
            self.run_script("--package", good, "--catalog", self.root / "c2")
        escape = self.root / "repo/sources.json"
        write(escape, json.dumps({"packages": [{"repository": "self", "path": "../good"}]}))
        self.assertEqual(self.run_script("--sources", escape, "--catalog", self.root / "c3",
                                         "--base-url", "https://example.com"), 1)

    def test_semantic_version_pattern_matches_the_host(self):
        for version in ("1.0.0", "0.0.1", "1.0.0-alpha.2", "1.0.0-alpha.10", "1.0.0+build.5",
                        "1.0.0-rc.1+build", "10.20.30"):
            self.assertTrue(build_catalog.SEMVER_PATTERN.fullmatch(version), version)
        for version in ("1.0", "01.0.0", "1.0.0-", "1.0.0-01", "1.0.0+", "v1.0.0", "1.0.0.0",
                        "1.0.0\n"):
            self.assertIsNone(build_catalog.SEMVER_PATTERN.fullmatch(version), version)

    def test_manifest_fields_must_have_their_json_types(self):
        for extra in ({"id": 123}, {"name": {"en": "Tool"}}, {"version": 1},
                      {"protocol_version": True}, {"protocol_version": "1"}, {"kind": 5},
                      {"license": ["MIT"]}):
            folder = plugin(self.root / f"bad{len(list(self.root.iterdir()))}", extra=extra)
            with self.subTest(extra=extra):
                self.assertEqual(self.run_script("--package", folder, "--catalog", self.root / "out",
                                                 "--base-url", "https://example.com"), 1)
        self.assertFalse((self.root / "out/plugins.json").exists())

    def test_repositories_stay_below_the_checkouts_folder(self):
        plugin(self.root / "outside")
        repository = self.root / "checkouts/CatalogRepo"
        repository.mkdir(parents=True)
        for name in ("..", "../outside", str(self.root / "outside"), "a/b", "", 7):
            sources = repository / "sources.json"
            write(sources, json.dumps({"packages": [{"repository": name, "path": "."}]}))
            with self.subTest(repository=name):
                self.assertEqual(self.run_script("--sources", sources,
                                                 "--checkouts", self.root / "checkouts",
                                                 "--catalog", self.root / "out",
                                                 "--base-url", "https://example.com"), 1)
        self.assertFalse((self.root / "out/plugins.json").exists())

    def test_the_declared_executable_keeps_its_execute_permission(self):
        folder = plugin(self.root / "native", extra={"executable": "bin/tool", "arguments": []})
        write(folder / "bin/tool", "#!/bin/sh\necho tool\n")
        write(folder / "bin/tool.exe", "MZ")
        self.assertEqual(self.run_script("--package", folder, "--exclude", "tests/**",
                                         "--catalog", self.root / "out",
                                         "--base-url", "https://example.com"), 0)
        with zipfile.ZipFile(self.root / "out/packages/org.example.tool-1.0.0.zip") as package:
            modes = {info.filename: info.external_attr >> 16 for info in package.infolist()}
        self.assertEqual(modes["bin/tool"], 0o100755)
        self.assertEqual(modes["bin/tool.exe"], 0o100755)
        self.assertEqual(modes["plugin.json"], 0o100644)
        if os.name != "nt":
            os.chmod(folder / "main.py", 0o755)
            self.assertEqual(self.run_script("--package", folder, "--exclude", "tests/**",
                                             "--catalog", self.root / "posix",
                                             "--base-url", "https://example.com"), 0)
            with zipfile.ZipFile(self.root / "posix/packages/org.example.tool-1.0.0.zip") as package:
                self.assertEqual(package.getinfo("main.py").external_attr >> 16, 0o100755)


if __name__ == "__main__":
    unittest.main()
