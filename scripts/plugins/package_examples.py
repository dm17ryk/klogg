"""Package portable examples and optionally catalog already-published release assets.

The ZIP format is shared with build_catalog.py, which builds the catalog of any plugin
repository (see "Plugins: packing, repositories and updates" in DOCUMENTATION.md).
"""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import pathlib
import shutil
import tempfile
import urllib.parse
import urllib.request
from typing import Any

ROOT = pathlib.Path(__file__).resolve().parents[2]


def _build_catalog_module():
    spec = importlib.util.spec_from_file_location(
        "cilogg_build_catalog", pathlib.Path(__file__).resolve().parent / "build_catalog.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def package_examples(output: pathlib.Path) -> list[tuple[dict[str, Any], pathlib.Path]]:
    builder = _build_catalog_module()
    output.mkdir(parents=True, exist_ok=True)
    packages = []
    for source in sorted((ROOT / "plugins/examples").iterdir()):
        if not source.is_dir():
            continue
        manifest = json.loads((source / "plugin.json").read_text(encoding="utf-8"))
        archive_path = output / f'{manifest["id"]}-{manifest["version"]}.zip'
        with tempfile.TemporaryDirectory() as temporary:
            package = pathlib.Path(temporary) / "package"
            shutil.copytree(source, package, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
            shutil.copy2(ROOT / "plugins/sdk/python/cilogg_plugin.py", package)
            files = builder.package_files(package, ())
            builder.write_zip(files, archive_path, builder.executable_entries(manifest, files))
        packages.append((manifest, archive_path))
    return packages


def catalog(packages: list[tuple[dict[str, Any], pathlib.Path]], base_url: str) -> dict[str, Any]:
    if urllib.parse.urlparse(base_url).scheme != "https":
        raise ValueError("Release asset base URL must use HTTPS")
    entries = []
    for manifest, archive in packages:
        url = base_url.rstrip("/") + "/" + urllib.parse.quote(archive.name)
        # Never advertise unpublished URLs or checksums of different asset bytes.
        with urllib.request.urlopen(url, timeout=30) as response:
            if urllib.parse.urlparse(response.geturl()).scheme != "https":
                raise ValueError("Published asset redirected away from HTTPS")
            digest = hashlib.sha256()
            size = 0
            while chunk := response.read(65536):
                size += len(chunk)
                if size > 128 * 1024 * 1024:
                    raise ValueError("Published package exceeds 128 MiB")
                digest.update(chunk)
        local_digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        if digest.hexdigest() != local_digest:
            raise ValueError(f"Published asset bytes differ: {url}")
        for operating_system in ("windows", "linux", "macos"):
            for architecture in ("x86_64", "arm64"):
                entries.append({
                    "id": manifest["id"], "name": manifest["name"],
                    "description": manifest.get("description", ""),
                    "version": manifest["version"], "protocol_version": 1,
                    "os": operating_system, "arch": architecture,
                    "url": url, "sha256": local_digest,
                })
    return {"schema_version": 1, "plugins": entries}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--published-base-url", help="Verify published assets and write plugins.json")
    arguments = parser.parse_args()
    packages = package_examples(arguments.output)
    if arguments.published_base_url:
        result = catalog(packages, arguments.published_base_url)
        (arguments.output / "plugins.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    for manifest, archive in packages:
        print(f'{manifest["id"]}: {archive}')


if __name__ == "__main__":
    main()
