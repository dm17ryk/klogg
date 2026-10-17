"""Run pinned aqtinstall with Qt 6.12's Windows architecture metadata layout.

Qt 6.12 publishes desktop metadata under qt6_6120_msvc2022_64 (or
msvc2022_arm64_cross_compiled); aqtinstall 3.3.0 looks under qt6_6120.
Only that folder suffix is adapted. aqt still handles package selection,
checksum verification, extraction, relocation and host-tool installation.
Remove this adapter when the pinned installer supports that layout directly.
"""

from __future__ import annotations

import logging
import sys
from importlib.metadata import version

from aqt.archives import QtArchives
from aqt.installer import Cli
from semantic_version import Version

_original_arch_ext = QtArchives._arch_ext
_windows_suffixes = {
    "win64_msvc2022_64": "_msvc2022_64",
    "win64_msvc2022_arm64_cross_compiled": "_msvc2022_arm64_cross_compiled",
}


def windows_arch_ext(archives: QtArchives) -> str:
    if (
        archives.os_name == "windows"
        and archives.target == "desktop"
        and archives.version == Version("6.12.0")
        and archives.arch in _windows_suffixes
    ):
        suffix = _windows_suffixes[archives.arch]
        logging.getLogger("aqt").info("Using Qt 6.12 Windows metadata suffix %s", suffix)
        return suffix
    logging.getLogger("aqt").debug(
        "Retaining aqt metadata route: host=%s target=%s version=%s arch=%s",
        archives.os_name, archives.target, archives.version, archives.arch,
    )
    return _original_arch_ext(archives)


def main() -> int:
    if version("aqtinstall") != "3.3.0":
        raise RuntimeError("This Qt 6.12 adapter requires aqtinstall==3.3.0")
    QtArchives._arch_ext = windows_arch_ext
    return Cli().run(sys.argv[1:])


if __name__ == "__main__":
    sys.exit(main())
