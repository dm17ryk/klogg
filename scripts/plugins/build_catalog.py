"""Build CILogg plugin packages and the plugins.json catalog of a plugin repository.

Each package folder (with plugin.json at its root) becomes a deterministic ZIP named
``<id>-<version>.zip`` under ``<catalog>/packages`` and an entry per platform in
``<catalog>/plugins.json`` (schema 1). Package addresses are either below a public HTTPS base
address (``--base-url``) or Azure DevOps Items API addresses of the catalog branch
(``--organization``, ``--project``, ``--repository``, ``--branch``).

A published version never changes: an existing ZIP with different bytes stops the build (raise the
version in plugin.json), and an identical rebuild changes nothing. See "Plugins: packing,
repositories and updates" in DOCUMENTATION.md and docs/plugin-repositories.md.

Examples:
  python build_catalog.py --package my-plugin --catalog out --base-url https://example.com/plugins
  python build_catalog.py --sources sources.json --checkouts "$(Build.SourcesDirectory)" \\
      --catalog catalog --organization org --project proj --repository repo --branch catalog
"""
from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import os
import pathlib
import re
import sys
import tempfile
import urllib.parse
import zipfile
from typing import Any, Dict, Iterable, List, NamedTuple, Set, Tuple

SCHEMA_VERSION = 1
PROTOCOL_VERSION = 1
MAX_PACKAGE_BYTES = 128 * 1024 * 1024
MAX_ENTRIES = 1000
TEXT_SUFFIXES = {".py", ".json", ".md", ".txt", ".lua", ".conf", ".yml", ".yaml"}
ALWAYS_EXCLUDED = ("__pycache__", "__pycache__/**", "**/__pycache__", "**/__pycache__/**",
                   "*.pyc", "**/*.pyc", ".git", ".git/**")
ID_PATTERN = re.compile(r"[a-z0-9][a-z0-9._-]{0,127}")
REPOSITORY_PATTERN = re.compile(r"[A-Za-z0-9._-]{1,128}")
# Semantic Versioning 2.0.0, as CILogg validates it.
SEMVER_PATTERN = re.compile(
    r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
    r"(?:-((?:0|[1-9][0-9]*|[0-9]*[a-zA-Z-][0-9a-zA-Z-]*)"
    r"(?:\.(?:0|[1-9][0-9]*|[0-9]*[a-zA-Z-][0-9a-zA-Z-]*))*))?"
    r"(?:\+([0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?")
PLATFORMS = {"windows", "linux", "macos", "any"}
# Keys of a configuration pack's `content` that the catalog's `contents` lists.
CONTENT_TYPES = ("previews", "actions", "highlights", "filters", "lua", "python_decoders")
# Optional plugin.json keys copied into each catalog entry.
COPIED_KEYS = ("min_host_version", "released", "homepage", "license")


class CatalogError(RuntimeError):
    """The build cannot continue; nothing is half written."""


class PackageSource(NamedTuple):
    """A package folder and the platforms its catalog entries name."""
    folder: pathlib.Path
    os: Tuple[str, ...] = ("any",)
    arch: Tuple[str, ...] = ("any",)
    exclude: Tuple[str, ...] = ()


class BuiltPackage(NamedTuple):
    manifest: Dict[str, Any]
    archive: pathlib.Path
    sha256: str
    source: PackageSource
    created: bool = False


def _excluded(relative: str, patterns: Iterable[str]) -> bool:
    return any(fnmatch.fnmatchcase(relative, pattern) for pattern in patterns)


def read_manifest(folder: pathlib.Path) -> dict[str, Any]:
    path = folder / "plugin.json"
    if not path.is_file():
        raise CatalogError(f"{folder}: no plugin.json")
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise CatalogError(f"{path}: {error}") from error
    if not isinstance(manifest, dict):
        raise CatalogError(f"{path}: plugin.json must be an object")
    # The JSON types are checked as CILogg reads them: a number where text belongs would pass a
    # str() conversion here but make CILogg leave the entry out.
    identifier = manifest.get("id")
    if not isinstance(identifier, str) or not ID_PATTERN.fullmatch(identifier):
        raise CatalogError(f"{path}: id must use lowercase letters, digits, '.', '_' and '-'")
    name = manifest.get("name")
    if not isinstance(name, str) or not name.strip():
        raise CatalogError(f"{path}: name is required (text)")
    version = manifest.get("version")
    if not isinstance(version, str) or not SEMVER_PATTERN.fullmatch(version):
        raise CatalogError(f"{path}: version must be a Semantic Versioning 2.0.0 version")
    protocol = manifest.get("protocol_version")
    if type(protocol) is not int or protocol != PROTOCOL_VERSION:  # True is not 1 here
        raise CatalogError(f"{path}: protocol_version must be {PROTOCOL_VERSION}")
    for key in ("description",) + COPIED_KEYS:
        if key in manifest and not isinstance(manifest[key], str):
            raise CatalogError(f"{path}: {key} must be text")
    kind = manifest.get("kind", "process")
    if not isinstance(kind, str) or kind not in ("process", "config-pack"):
        raise CatalogError(f"{path}: unknown kind {kind!r}")
    if kind == "config-pack" and not isinstance(manifest.get("content"), dict):
        raise CatalogError(f"{path}: a configuration pack lists its files under content")
    return manifest


def package_files(folder: pathlib.Path, exclude: Iterable[str]) -> list[tuple[str, pathlib.Path]]:
    """Files of a package as (ZIP name, path), sorted; links and escapes are refused."""
    patterns = tuple(ALWAYS_EXCLUDED) + tuple(exclude)
    root = folder.resolve()
    files = []
    for path in sorted(folder.rglob("*")):
        relative = path.relative_to(folder).as_posix()
        if _excluded(relative, patterns):
            continue
        if path.is_symlink():
            raise CatalogError(f"{path}: packages cannot contain links")
        if path.is_dir():
            continue
        if root not in path.resolve().parents:
            raise CatalogError(f"{path}: outside the package folder")
        files.append((relative, path))
    if "plugin.json" not in {name for name, _ in files}:
        raise CatalogError(f"{folder}: plugin.json is excluded")
    return files


def executable_entries(manifest: Dict[str, Any],
                       files: List[Tuple[str, pathlib.Path]]) -> Set[str]:
    """ZIP names that get mode 0755: the declared executable and, on POSIX, files with an
    execute bit (a helper the executable starts). CILogg restores the execute permission only
    from these bits, so a native program packaged without them could not start."""
    names = set()
    declared = manifest.get("executable")
    if isinstance(declared, str) and declared and declared != "python":
        names.update({declared, declared + ".exe"})
    if os.name != "nt":  # Windows reports execute bits from file extensions only
        names.update(name for name, path in files if path.stat().st_mode & 0o111)
    return {name for name, _ in files if name in names}


def write_zip(files: List[Tuple[str, pathlib.Path]], archive: pathlib.Path,
              executables: Iterable[str] = ()) -> None:
    """Deterministic ZIP: sorted stored entries, fixed timestamps, mode 0644 or (for
    `executables`) 0755, LF line endings in text files."""
    total = 0
    executable = set(executables)
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_STORED) as output:
        for name, path in sorted(files):
            info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = (0o100755 if name in executable else 0o100644) << 16
            data = path.read_bytes()
            if path.suffix.lower() in TEXT_SUFFIXES:
                data = data.replace(b"\r\n", b"\n")
            total += len(data)
            if total > MAX_PACKAGE_BYTES:
                raise CatalogError(f"{archive.name}: package exceeds 128 MiB")
            # Stored entries have deterministic bytes across zlib and Python versions.
            output.writestr(info, data, compress_type=zipfile.ZIP_STORED)


def build_package(source: PackageSource, packages_dir: pathlib.Path) -> BuiltPackage:
    manifest = read_manifest(source.folder)
    name = f'{manifest["id"]}-{manifest["version"]}.zip'
    target = packages_dir / name
    packages_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=packages_dir) as temporary:
        staged = pathlib.Path(temporary) / name
        files = package_files(source.folder, source.exclude)
        write_zip(files, staged, executable_entries(manifest, files))
        data = staged.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        created = False
        if target.exists():
            # A published version never changes.
            if hashlib.sha256(target.read_bytes()).hexdigest() != digest:
                raise CatalogError(
                    f"{target.name} already exists with different bytes: raise the version in "
                    f"{source.folder / 'plugin.json'}")
        else:
            staged.replace(target)
            created = True
    return BuiltPackage(manifest, target, digest, source, created)


def contents(manifest: dict[str, Any]) -> list[str]:
    content = manifest.get("content")
    if not isinstance(content, dict):
        return []
    return [kind for kind in CONTENT_TYPES if content.get(kind)]


def azure_items_url(organization: str, project: str, repository: str, branch: str,
                    path: str) -> str:
    """The Items API address that returns the raw file of a branch (docs section 3.5)."""
    quote = lambda value: urllib.parse.quote(value, safe="")  # noqa: E731
    query = urllib.parse.urlencode({
        "path": path,
        "versionDescriptor.versionType": "branch",
        "versionDescriptor.version": branch,
        "$format": "octetStream",
        "download": "true",
        "api-version": "7.1",
    }, safe="/$")
    return (f"https://dev.azure.com/{quote(organization)}/{quote(project)}/_apis/git/"
            f"repositories/{quote(repository)}/items?{query}")


def package_url(args: argparse.Namespace, archive_name: str) -> str:
    if args.base_url:
        return args.base_url.rstrip("/") + "/packages/" + urllib.parse.quote(archive_name)
    return azure_items_url(args.organization, args.project, args.repository, args.branch,
                           "/packages/" + archive_name)


def catalog_entries(built: list[BuiltPackage], url_for) -> list[dict[str, Any]]:
    entries = []
    for package in built:
        manifest = package.manifest
        common: dict[str, Any] = {
            "id": manifest["id"],
            "name": manifest["name"],
            "description": manifest.get("description", ""),
            "version": manifest["version"],
            "protocol_version": PROTOCOL_VERSION,
            "url": url_for(package.archive.name),
            "sha256": package.sha256,
        }
        if "kind" in manifest:
            common["kind"] = manifest["kind"]
        packed = contents(manifest)
        if packed:
            common["contents"] = packed
        for key in COPIED_KEYS:
            if key in manifest:
                common[key] = manifest[key]
        for operating_system in package.source.os:
            for architecture in package.source.arch:
                entries.append(dict(common, os=operating_system, arch=architecture))
    if len(entries) > MAX_ENTRIES:
        raise CatalogError(f"the catalog would have {len(entries)} entries (maximum {MAX_ENTRIES})")
    return entries


def _platforms(value: Any, where: str, allowed: set[str] | None) -> tuple[str, ...]:
    items = [value] if isinstance(value, str) else value
    if not isinstance(items, list) or not items or not all(isinstance(i, str) and i for i in items):
        raise CatalogError(f"{where}: give a list of names")
    if allowed is not None and not set(items) <= allowed:
        raise CatalogError(f"{where}: unknown platform {sorted(set(items) - allowed)}")
    return tuple(items)


def read_sources(path: pathlib.Path, checkouts: pathlib.Path | None) -> list[PackageSource]:
    """sources.json: packages of this repository ("self") or of other checked-out ones."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise CatalogError(f"{path}: {error}") from error
    packages = data.get("packages") if isinstance(data, dict) else None
    if not isinstance(packages, list) or not packages:
        raise CatalogError(f"{path}: expected {{\"packages\": [...]}}")
    sources = []
    for index, item in enumerate(packages):
        where = f"{path} packages[{index}]"
        if not isinstance(item, dict) or not isinstance(item.get("path"), str):
            raise CatalogError(f"{where}: needs a path")
        repository = item.get("repository", "self")
        if repository == "self":
            base = path.resolve().parent
        else:
            # A repository is one folder name below --checkouts, never a path out of it.
            if (not isinstance(repository, str) or not REPOSITORY_PATTERN.fullmatch(repository)
                    or repository in (".", "..")):
                raise CatalogError(f"{where}: repository must be a folder name below --checkouts")
            if checkouts is None:
                raise CatalogError(f"{where}: --checkouts is required for repository {repository}")
            root = checkouts.resolve()
            base = (checkouts / repository).resolve()
            if base.parent != root:
                raise CatalogError(f"{where}: repository {repository} is not below --checkouts")
        folder = (base / item["path"]).resolve()
        if base.resolve() not in folder.parents and folder != base.resolve():
            raise CatalogError(f"{where}: path leaves the repository")
        exclude = item.get("exclude", [])
        if not isinstance(exclude, list) or not all(isinstance(e, str) for e in exclude):
            raise CatalogError(f"{where}: exclude must be a list of patterns")
        sources.append(PackageSource(folder,
                                     _platforms(item.get("os", ["any"]), where + " os", PLATFORMS),
                                     _platforms(item.get("arch", ["any"]), where + " arch", None),
                                     tuple(exclude)))
    return sources


def build_catalog(sources: list[PackageSource], catalog: pathlib.Path, url_for) -> dict[str, Any]:
    """Builds every package, then writes plugins.json last, so it never names a missing ZIP."""
    ids = {}
    built = []
    for source in sources:
        package = build_package(source, catalog / "packages")
        key = package.manifest["id"]
        if key in ids and ids[key] != package.manifest["version"]:
            raise CatalogError(f"{key} is listed twice with different versions")
        ids[key] = package.manifest["version"]
        built.append(package)
    document = {"schema_version": SCHEMA_VERSION, "plugins": catalog_entries(built, url_for)}
    text = json.dumps(document, indent=2, ensure_ascii=False) + "\n"
    target = catalog / "plugins.json"
    if not target.exists() or target.read_text(encoding="utf-8") != text:
        target.write_text(text, encoding="utf-8", newline="\n")
    return document


def parse_arguments(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--sources", type=pathlib.Path, help="sources.json listing the packages")
    inputs.add_argument("--package", type=pathlib.Path, action="append",
                        help="a package folder with plugin.json (repeatable)")
    parser.add_argument("--checkouts", type=pathlib.Path,
                        help="folder holding the checkouts of other repositories")
    parser.add_argument("--os", action="append", help="platform of --package entries (default any)")
    parser.add_argument("--arch", action="append",
                        help="architecture of --package entries, such as x86_64 (default any)")
    parser.add_argument("--exclude", action="append", default=[],
                        help="pattern of files to leave out of --package folders")
    parser.add_argument("--catalog", type=pathlib.Path, required=True,
                        help="output folder: plugins.json and packages/")
    parser.add_argument("--base-url", help="public HTTPS address of the catalog folder")
    parser.add_argument("--organization", help="Azure DevOps organization")
    parser.add_argument("--project", help="Azure DevOps project")
    parser.add_argument("--repository", help="Azure DevOps repository holding the catalog branch")
    parser.add_argument("--branch", default="catalog", help="catalog branch (default catalog)")
    args = parser.parse_args(argv)
    azure = [args.organization, args.project, args.repository]
    if args.base_url:
        if any(azure):
            parser.error("give either --base-url or the Azure DevOps options, not both")
        parsed = urllib.parse.urlparse(args.base_url)
        if parsed.scheme != "https" or not parsed.hostname or parsed.username or parsed.password:
            parser.error("--base-url must be an https:// address without user information")
    elif not all(azure):
        parser.error("give --base-url, or --organization, --project and --repository")
    return args


def main(argv: list[str] | None = None) -> int:
    args = parse_arguments(argv)
    try:
        if args.sources:
            sources = read_sources(args.sources, args.checkouts)
        else:
            os_names = _platforms(args.os or ["any"], "--os", PLATFORMS)
            architectures = _platforms(args.arch or ["any"], "--arch", None)
            sources = [PackageSource(folder, os_names, architectures, tuple(args.exclude))
                       for folder in args.package]
        document = build_catalog(sources, args.catalog, lambda name: package_url(args, name))
    except CatalogError as error:
        print(f"build_catalog: {error}", file=sys.stderr)
        return 1
    for entry in document["plugins"]:
        print(f'{entry["id"]} {entry["version"]} {entry["os"]}/{entry["arch"]}: {entry["url"]}')
    return 0


if __name__ == "__main__":
    sys.exit(main())
