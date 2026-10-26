"""Copy a test-owned runtime, never the source installation's portable settings."""

import logging
import plistlib
import shutil
import subprocess
import sys
import tempfile
import uuid
from contextlib import contextmanager
from pathlib import Path

LOG = logging.getLogger("workbench.payload")
RUNTIME_DIRECTORIES = (
    "platforms", "styles", "imageformats", "iconengines", "tls",
    "networkinformation", "translations", "resources", "sqldrivers",
)


@contextmanager
def private_ipc_environment(environment):
    if sys.platform != "darwin":
        yield environment
        return
    # macOS's usual /var/folders TMPDIR plus the unique application name can
    # exceed sockaddr_un.sun_path. Share this short lease with every client.
    with tempfile.TemporaryDirectory(prefix="cg-", dir="/tmp") as directory:
        LOG.info("private_ipc_directory path=%s", directory)
        yield dict(environment, TMPDIR=directory)
    LOG.info("private_ipc_directory_released")


def copy_runtime(source, destination):
    for path in source.iterdir():
        name = path.name.lower()
        if path.is_file() and (
            name.endswith((".dll", ".qm", ".rcc", ".dylib", ".so"))
            or ".so." in name
            or name in ("cilogg_grep", "cilogg_grep.exe")
        ):
            shutil.copy2(path, destination / path.name)
    for name in RUNTIME_DIRECTORIES:
        path = source / name
        if path.is_dir():
            shutil.copytree(path, destination / name, symlinks=True)


def prepare_payload(executable: Path, artifacts: Path):
    executable = executable.resolve(strict=True)
    identity = uuid.uuid4().hex
    root = artifacts / ("payload-" + identity)
    root.mkdir()
    contents = executable.parent.parent
    bundle = contents.parent
    mac_bundle = (
        executable.parent.name == "MacOS"
        and contents.name == "Contents"
        and bundle.suffix == ".app"
    )
    payload = root
    if mac_bundle:
        private_contents = root / "cilogg-private.app" / "Contents"
        payload = private_contents / "MacOS"
        payload.mkdir(parents=True)
        for name in ("Frameworks", "PlugIns", "Resources"):
            if (contents / name).is_dir():
                # Qt plugins are required; CILogg's installed plugin catalog is
                # user-extensible and must not enter an isolated test runtime.
                ignore = shutil.ignore_patterns("cilogg") if name == "PlugIns" else None
                shutil.copytree(
                    contents / name, private_contents / name, symlinks=True, ignore=ignore
                )
    private = payload / ("cilogg-analysis-" + identity + executable.suffix)
    shutil.copy2(executable, private)
    copy_runtime(executable.parent, payload)
    (payload / "cilogg.conf").write_text(
        "[General]\nversion=1\nversionchecker.enabled=false\n", encoding="utf-8"
    )
    if mac_bundle:
        with (contents / "Info.plist").open("rb") as stream:
            info = plistlib.load(stream)
        info["CFBundleExecutable"] = private.name
        info["CFBundleIdentifier"] = "org.cilogg.test." + identity
        with (payload.parent / "Info.plist").open("wb") as stream:
            plistlib.dump(info, stream)
        if sys.platform == "darwin":
            # Only the newly created private bundle is re-signed for local execution.
            subprocess.run(
                ["codesign", "--force", "--deep", "--sign", "-", str(payload.parent.parent)],
                check=True, timeout=60,
            )
    LOG.info("runtime_copied bundle=%s private=%s", mac_bundle, private)
    return payload, private
