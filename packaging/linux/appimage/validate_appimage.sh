#!/usr/bin/env bash
# Run inside the target-architecture build container, without FUSE.
set -euo pipefail
export LC_ALL=C QT_QPA_PLATFORM=offscreen
image="$(realpath "${1:?AppImage path is required}")"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
case "$(uname -m)" in
  aarch64|arm64) machine=AArch64 ;;
  x86_64) machine='Advanced Micro Devices X86-64' ;;
  *) echo 'ERROR: unsupported validation architecture' >&2; exit 1 ;;
esac
echo "Validating AppImage: $image; expected machine=$machine"
test -s "$image"
readelf -h "$image" | grep -F 'Machine:' | grep -F "$machine"
workdir="$(mktemp -d)"
trap 'rm -rf -- "$workdir"' EXIT
chmod +x "$image"
(
  cd "$workdir"
  timeout 120 "$image" --appimage-extract > extraction.log
)
appdir="$workdir/squashfs-root"
test -x "$appdir/AppRun"
test -x "$appdir/usr/bin/cilogg"
readelf -h "$appdir/usr/bin/cilogg" | grep -F 'Machine:' | grep -F "$machine"
find "$appdir" -name 'libQt6Sql*.so*' -print | grep .
find "$appdir" -path '*/sqldrivers/*qsqlite*' -print | grep .
find "$appdir" -path '*/platforms/*qoffscreen*' -print | grep .
python3 "$repo_root/scripts/plugins/validate_preinstalled.py" "$appdir/usr/bin/plugins"
echo 'Checking packaged application version and native MCP discovery'
timeout 60 "$appdir/AppRun" --version
timeout 60 "$appdir/AppRun" mcp --help
echo "AppImage validation passed: $image"
