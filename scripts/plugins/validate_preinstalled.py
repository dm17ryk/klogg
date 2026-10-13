"""Validate the preinstalled log-source payload in an extracted release package."""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def validate(root: Path) -> None:
    for folder in ("adb-logcat", "syslog-server"):
        directory = root / folder
        for filename in ("plugin.json", "main.py", "cilogg_plugin.py"):
            path = directory / filename
            if not path.is_file() or path.stat().st_size == 0:
                raise ValueError(f"Missing or empty preinstalled plugin file: {path}")
        manifest = json.loads((directory / "plugin.json").read_text(encoding="utf-8"))
        if manifest.get("id") != f"org.cilogg.{folder}":
            raise ValueError(f"Unexpected preinstalled plugin ID: {directory}")
        if manifest.get("enabled_by_default") is not True:
            raise ValueError(f"Preinstalled plugin is not enabled by default: {directory}")
        print(f"Validated preinstalled plugin: {directory}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path, help="Package directory containing the plugin folders")
    options = parser.parse_args()
    try:
        validate(options.root)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Preinstalled plugin validation failed: {error}\n")
