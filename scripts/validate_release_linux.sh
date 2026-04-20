#!/usr/bin/env bash
set -euo pipefail

fail() {
  echo "$1" >&2
  exit 1
}

validate_deb() {
  local pkg="$1"
  local contents
  local metadata

  echo "Validating DEB: $pkg"
  contents="$(dpkg-deb -c "$pkg")"
  metadata="$(dpkg-deb -I "$pkg")"

  grep -q './usr/bin/cilogg$' <<<"$contents" || fail "DEB missing /usr/bin/cilogg: $pkg"
  grep -q '^ Package: ' <<<"$metadata" || fail "DEB metadata unreadable: $pkg"
}

validate_rpm() {
  local pkg="$1"
  local requires

  echo "Validating RPM: $pkg"
  requires="$(rpm -qp --requires "$pkg")"
  [[ -n "$requires" ]] || fail "RPM requires list is empty: $pkg"
}

validate_appimage() {
  local pkg="$1"
  local pkg_abs
  local workdir

  echo "Validating AppImage: $pkg"
  pkg_abs="$(realpath "$pkg")"
  workdir="$(mktemp -d)"

  chmod +x "$pkg_abs"
  (
    cd "$workdir"
    "$pkg_abs" --appimage-extract >/dev/null
  )

  test -f "$workdir/squashfs-root/usr/bin/cilogg" || fail "AppImage missing cilogg binary: $pkg"
  find "$workdir/squashfs-root" -name 'libQt6Sql*.so*' | grep -q . || fail "AppImage missing Qt SQL runtime: $pkg"
  find "$workdir/squashfs-root" -path '*/sqldrivers/*qsqlite*' | grep -q . || fail "AppImage missing qsqlite plugin: $pkg"

  rm -rf "$workdir"
}

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
