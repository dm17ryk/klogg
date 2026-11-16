#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

fail() {
  echo "$1" >&2
  exit 1
}

resolve_package_executable() {
  local executable launcher_target
  executable="$(realpath -e "$1")"
  # Inspect only the bounded launcher header; never execute extracted packages.
  launcher_target=$(head -c 4096 "$executable" | sed -n 's/^# CILOGG_PRIVATE_EXECUTABLE=//p')
  if [[ -n "$launcher_target" ]]; then
    [[ "$launcher_target" != /* && "$launcher_target" != *$'\n'* ]] || fail "Invalid private launcher target: $launcher_target"
    executable="$(realpath -e "$(dirname "$executable")/$launcher_target")"
  fi
  printf '%s\n' "$executable"
}

# Script Runner and scenarios load this package from <executable dir>/python_runtime.
validate_python_runtime() {
  local runtime="$1"
  local file
  for file in worker_bootstrap.py scenario_bootstrap.py klogg/__init__.py; do
    [[ -f "$runtime/$file" ]] || fail "Package is missing python_runtime/$file next to the executable: $runtime"
  done
}

validate_deb() (
  local pkg="$1"
  local contents
  local metadata
  local workdir

  echo "Validating DEB: $pkg"
  contents="$(dpkg-deb -c "$pkg")"
  metadata="$(dpkg-deb -I "$pkg")"

  grep -Eq './usr/bin/cilogg( -> [^[:space:]]+)?$' <<<"$contents" || fail "DEB missing /usr/bin/cilogg: $pkg"
  grep -q '^ Package: ' <<<"$metadata" || fail "DEB metadata unreadable: $pkg"
  workdir="$(mktemp -d)"
  trap 'rm -rf "$workdir"' EXIT
  dpkg-deb -x "$pkg" "$workdir"
  local executable
  executable="$(resolve_package_executable "$workdir/usr/bin/cilogg")"
  [[ "$executable" == "$workdir/"* && -x "$executable" ]] || fail "DEB executable escapes package or is not executable: $pkg"
  echo "DEB executable resolved within package: ${executable#"$workdir/"}"
  python3 "$SCRIPT_DIR/plugins/validate_preinstalled.py" "$(dirname "$executable")/plugins"
  validate_python_runtime "$(dirname "$executable")/python_runtime"
)

validate_rpm() (
  local pkg="$1"
  local requires
  local pkg_abs
  local workdir
  local executable

  echo "Validating RPM: $pkg"
  requires="$(rpm -qp --requires "$pkg")"
  [[ -n "$requires" ]] || fail "RPM requires list is empty: $pkg"
  pkg_abs="$(realpath "$pkg")"
  workdir="$(mktemp -d)"
  trap 'rm -rf "$workdir"' EXIT
  (cd "$workdir"; rpm2cpio "$pkg_abs" | cpio -idm --quiet)
  executable="$(resolve_package_executable "$workdir/usr/bin/cilogg")"
  [[ "$executable" == "$workdir/"* && -x "$executable" ]] || fail "RPM executable escapes package or is not executable: $pkg"
  echo "RPM executable resolved within package: ${executable#"$workdir/"}"
  python3 "$SCRIPT_DIR/plugins/validate_preinstalled.py" "$(dirname "$executable")/plugins"
  validate_python_runtime "$(dirname "$executable")/python_runtime"
)

validate_appimage() (
  local pkg="$1"
  local pkg_abs
  local workdir

  echo "Validating AppImage: $pkg"
  pkg_abs="$(realpath "$pkg")"
  workdir="$(mktemp -d)"
  trap 'rm -rf "$workdir"' EXIT

  chmod +x "$pkg_abs"
  (
    cd "$workdir"
    "$pkg_abs" --appimage-extract >/dev/null
  )

  test -f "$workdir/squashfs-root/usr/bin/cilogg" || fail "AppImage missing cilogg binary: $pkg"
  find "$workdir/squashfs-root" -name 'libQt6Sql*.so*' | grep -q . || fail "AppImage missing Qt SQL runtime: $pkg"
  find "$workdir/squashfs-root" -path '*/sqldrivers/*qsqlite*' | grep -q . || fail "AppImage missing qsqlite plugin: $pkg"
  python3 "$SCRIPT_DIR/plugins/validate_preinstalled.py" "$workdir/squashfs-root/usr/bin/plugins"
  validate_python_runtime "$workdir/squashfs-root/usr/bin/python_runtime"
)

shopt -s nullglob

for pkg in ./packages-*/*.deb; do
  validate_deb "$pkg"
done

for pkg in ./packages-*/*.rpm; do
  validate_rpm "$pkg"
done

for pkg in ./packages-appimage/*.AppImage; do
  validate_appimage "$pkg"
done

echo "Linux release validation passed."
