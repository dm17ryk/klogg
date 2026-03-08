#!/bin/bash
set -e

ARCH="$(uname -m)"
LINUXDEPLOYQT_ARCH="x86_64"
LIB_DIR="/lib/x86_64-linux-gnu"
LINUXDEPLOYQT_FALLBACK_ARCH="aarch64"

if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
  LINUXDEPLOYQT_ARCH="aarch64"
  LINUXDEPLOYQT_FALLBACK_ARCH="x86_64"
  LIB_DIR="/lib/aarch64-linux-gnu"
fi

DESTDIR=$(readlink -f appdir) ninja install

run_linuxdeployqt() {
  local args=("$@")
  local arch

  for arch in "$LINUXDEPLOYQT_ARCH" "$LINUXDEPLOYQT_FALLBACK_ARCH"; do
    local appimage="linuxdeployqt-continuous-${arch}.AppImage"
    wget -q -O "${appimage}" "https://github.com/probonopd/linuxdeployqt/releases/download/continuous/${appimage}"
    chmod a+x "${appimage}"

    if ! readelf -h "${appimage}" >/dev/null 2>&1; then
      echo "linuxdeployqt (${arch}) is not a valid ELF binary, trying fallback..." >&2
      continue
    fi

    if VERSION=$KLOGG_VERSION "./${appimage}" "${args[@]}"; then
      return 0
    fi

    echo "linuxdeployqt (${arch}) failed, trying fallback if available..." >&2
  done

  return 1
}

if ! run_linuxdeployqt appdir/usr/share/applications/*.desktop -bundle-non-qt-libs; then
  if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
    echo "WARNING: linuxdeployqt failed on ARM64, skipping AppImage generation." >&2
    mkdir -p ./packages
    echo "AppImage generation skipped on ARM64 due linuxdeployqt incompatibility." \
      > "./packages/klogg-${KLOGG_VERSION}-appimage-arm64-skip.txt"
    exit 0
  fi
  exit 1
fi

mkdir -p appdir/usr/lib
cp "${LIB_DIR}"/libssl* appdir/usr/lib

if ! run_linuxdeployqt appdir/usr/share/applications/*.desktop -appimage; then
  if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
    echo "WARNING: linuxdeployqt failed on ARM64 during AppImage stage, skipping." >&2
    mkdir -p ./packages
    echo "AppImage generation skipped on ARM64 due linuxdeployqt incompatibility." \
      > "./packages/klogg-${KLOGG_VERSION}-appimage-arm64-skip.txt"
    exit 0
  fi
  exit 1
fi

mkdir -p ./packages
APPIMAGE_FILE="$(ls ./klogg-$KLOGG_VERSION-*.AppImage | head -n 1)"
cp "${APPIMAGE_FILE}" "./packages/$(basename "${APPIMAGE_FILE}")"
