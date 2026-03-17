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

  grep -q './usr/bin/klogg$' <<<"$contents" || fail "DEB missing /usr/bin/klogg: $pkg"

  if ! grep -Eq 'libqt6sql6|libQt6Sql\.so\.6' <<<"$metadata"; then
    fail "DEB missing Qt SQL dependency metadata: $pkg"
  fi

  if ! grep -Eq 'libqt6serialport6|libQt6SerialPort\.so\.6' <<<"$metadata"; then
    fail "DEB missing Qt SerialPort dependency metadata: $pkg"
  fi
}

validate_rpm() {
  local pkg="$1"
  local requires

  echo "Validating RPM: $pkg"
  requires="$(rpm -qp --requires "$pkg")"

  if ! grep -Eq 'libQt6Sql\.so\.6|qt6-qtbase' <<<"$requires"; then
    fail "RPM missing Qt SQL dependency metadata: $pkg"
  fi

  if ! grep -Eq 'libQt6SerialPort\.so\.6|qt6-qtserialport' <<<"$requires"; then
    fail "RPM missing Qt SerialPort dependency metadata: $pkg"
  fi
}

validate_appimage() {
  local pkg="$1"
  local workdir

  echo "Validating AppImage: $pkg"
  workdir="$(mktemp -d)"

  chmod +x "$pkg"
  (
    cd "$workdir"
    "$pkg" --appimage-extract >/dev/null
  )

  test -f "$workdir/squashfs-root/usr/bin/klogg" || fail "AppImage missing klogg binary: $pkg"
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
