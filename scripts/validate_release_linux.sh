#!/usr/bin/env bash
set -euo pipefail

validate_deb() {
  local pkg="$1"
  local contents
  local metadata
  contents="$(dpkg-deb -c "$pkg")"
  metadata="$(dpkg-deb -I "$pkg")"
  grep -q './usr/bin/klogg$' <<<"$contents"
  grep -q 'libqt6sql6' <<<"$metadata"
  grep -q 'libqt6serialport6' <<<"$metadata"
}

validate_rpm() {
  local pkg="$1"
  rpm -qp --requires "$pkg" | grep -q 'libQt6Sql.so.6'
  rpm -qp --requires "$pkg" | grep -q 'libQt6SerialPort.so.6'
}

validate_appimage() {
  local pkg="$1"
  local workdir
  workdir="$(mktemp -d)"

  chmod +x "$pkg"
  (
    cd "$workdir"
    "$pkg" --appimage-extract >/dev/null
  )

  test -f "$workdir/squashfs-root/usr/bin/klogg"
  find "$workdir/squashfs-root" -name 'libQt6Sql*.so*' | grep -q .
  find "$workdir/squashfs-root" -path '*/sqldrivers/*qsqlite*' | grep -q .

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
