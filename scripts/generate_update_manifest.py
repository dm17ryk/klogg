#!/usr/bin/env python3
"""Generate the CILogg updater manifest from release artifact directories."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def classify(path: Path) -> dict[str, object] | None:
    name = path.name
    lower = name.lower()
    architecture = "arm64" if "arm64" in lower or "aarch64" in lower else "x64"
    result: dict[str, object]

    if lower.endswith("-setup.exe"):
        result = {"platform": "windows", "architecture": architecture,
                  "kind": "windows-setup", "variant": ""}
    elif lower.endswith("-portable.zip"):
        result = {"platform": "windows", "architecture": architecture,
                  "kind": "windows-portable", "variant": ""}
    elif lower.endswith("-update.zip") and ("mac" in lower or "osx" in lower):
        result = {"platform": "macos", "architecture": architecture,
                  "kind": "macos-bundle", "variant": ""}
    elif lower.endswith(".appimage"):
        result = {"platform": "linux", "architecture": architecture,
                  "kind": "appimage", "variant": ""}
    elif lower.endswith(".deb"):
        variant = next((value for value in ("focal", "jammy", "noble") if value in lower), "")
        result = {"platform": "linux", "architecture": architecture,
                  "kind": "deb", "variant": variant}
    elif lower.endswith(".rpm"):
        match = re.search(r"fedora(?:-|_)?(\d+)", lower)
        variant = f"fedora{match.group(1)}" if match else ""
        result = {"platform": "linux", "architecture": architecture,
                  "kind": "rpm", "variant": variant}
    else:
        return None

    result.update({"filename": name, "size": path.stat().st_size, "sha256": sha256(path)})
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--output", default="cilogg-update-manifest.json")
    args = parser.parse_args()

    root = Path(args.root)
    assets = []
    seen: set[str] = set()
    for path in sorted(root.glob("packages-*/*")):
        if not path.is_file():
            continue
        asset = classify(path)
        if asset is None:
            continue
        filename = str(asset["filename"])
        if filename in seen:
            raise SystemExit(f"duplicate installable release asset: {filename}")
        seen.add(filename)
        assets.append(asset)

    if not assets:
        raise SystemExit("no installable release assets were found")

    output = Path(args.output)
    output.write_text(json.dumps({"schema": 1, "assets": assets}, indent=2) + "\n",
                      encoding="utf-8")
    print(f"wrote {output} with {len(assets)} installable assets")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
