"""Build a DMG, recovering from CPack/Finder failures without reusing busy images."""
from __future__ import annotations

import argparse
import plistlib
import re
import shutil
import subprocess
import tempfile
import time
from pathlib import Path


def _run(args: list[str], *, check: bool = True, capture: bool = False,
         timeout: int = 600) -> subprocess.CompletedProcess:
    print(f"DMG command: {args!r}", flush=True)
    return subprocess.run(args, check=check, timeout=timeout,
                          stdout=subprocess.PIPE if capture else None)


def _within(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def _detach_owned_images(roots: list[Path]) -> None:
    """Match backing images, since CPack can mount outside /Volumes/CILogg."""
    for attempt in range(6):
        state = plistlib.loads(_run(["hdiutil", "info", "-plist"],
                                   capture=True, timeout=30).stdout)
        owned = []
        for image in state.get("images", []):
            image_path = image.get("image-path")
            if not image_path or not any(_within(Path(image_path).resolve(), root)
                                         for root in roots):
                continue
            devices = [entity.get("dev-entry", "")
                       for entity in image.get("system-entities", [])]
            device = next((value for value in devices
                           if re.fullmatch(r"/dev/disk[0-9]+", value)), None)
            if device is None:
                raise RuntimeError(f"Cannot identify owned image device: {image_path}")
            print(f"DMG owned mount: image={image_path!r} device={device} "
                  f"cleanup_attempt={attempt + 1}", flush=True)
            owned.append(device)
        if not owned:
            print("DMG cleanup: no owned images remain mounted", flush=True)
            return
        if attempt == 5:
            raise RuntimeError(f"Owned DMG images are still mounted: {owned}")
        for device in dict.fromkeys(owned):
            result = _run(["hdiutil", "detach", device, "-force"],
                          check=False, timeout=30)
            print(f"DMG detach: device={device} exit={result.returncode}", flush=True)
        time.sleep(2)


def package(build_root: Path, version: str) -> Path:
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+-]*", version):
        raise ValueError("Invalid package version")
    build_root = build_root.resolve()
    app = build_root / "output/cilogg.app"
    if not (app / "Contents/MacOS/cilogg").is_file():
        raise RuntimeError(f"Missing application bundle: {app}")
    packages = build_root / "packages"
    packages.mkdir(exist_ok=True)
    destination = packages / f"cilogg-{version}-OSX.dmg"
    result = _run(["cpack", "--verbose", "-G", "DragNDrop"], check=False)
    if result.returncode == 0:
        _run(["hdiutil", "verify", str(destination)])
        print(f"DMG CPack success: {destination}", flush=True)
        return destination

    print(f"DMG CPack failed: exit={result.returncode}; starting recovery", flush=True)
    cpack_root = (build_root / "_CPack_Packages").resolve()
    _detach_owned_images([cpack_root])
    temporary = Path(tempfile.mkdtemp(prefix="dmg-recovery-", dir=packages)).resolve()
    try:
        source = temporary / "source"
        source.mkdir()
        _run(["ditto", str(app), str(source / app.name)])
        _run(["ln", "-s", "/Applications", str(source / "Applications")])
        for attempt in range(1, 4):
            # A new backing file per attempt avoids reusing a busy partial image.
            image = temporary / f"attempt-{attempt}.dmg"
            print(f"DMG fallback: attempt={attempt}/3 source={source} image={image}",
                  flush=True)
            result = _run(["hdiutil", "create", "-fs", "HFS+", "-srcfolder", str(source),
                           "-volname", "CILogg", "-format", "UDZO", str(image)], check=False)
            if result.returncode == 0:
                _run(["hdiutil", "verify", str(image)])
                image.replace(destination)
                print(f"DMG verified and published: {destination}", flush=True)
                break
            print(f"DMG creation failed: attempt={attempt} exit={result.returncode}",
                  flush=True)
            _detach_owned_images([cpack_root, temporary])
            if attempt == 3:
                raise RuntimeError("DMG creation failed after 3 attempts")
            time.sleep(2)
    except BaseException:
        # Preserve diagnostics and backing files if an image cannot be detached.
        print(f"DMG recovery failed; retaining work directory: {temporary}", flush=True)
        raise
    shutil.rmtree(temporary)
    return destination


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    options = parser.parse_args()
    package(Path.cwd(), options.version)
