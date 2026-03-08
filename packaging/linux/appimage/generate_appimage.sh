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
    wget -c -q "https://github.com/probonopd/linuxdeployqt/releases/download/continuous/${appimage}"
    chmod a+x "${appimage}"

    if VERSION=$KLOGG_VERSION "./${appimage}" "${args[@]}"; then
      return 0
    fi

    echo "linuxdeployqt (${arch}) failed, trying fallback if available..." >&2
  done

  return 1
}

run_linuxdeployqt appdir/usr/share/applications/*.desktop -bundle-non-qt-libs

mkdir -p appdir/usr/lib
cp "${LIB_DIR}"/libssl* appdir/usr/lib

run_linuxdeployqt appdir/usr/share/applications/*.desktop -appimage

mkdir -p ./packages
APPIMAGE_FILE="$(ls ./klogg-$KLOGG_VERSION-*.AppImage | head -n 1)"
cp "${APPIMAGE_FILE}" "./packages/$(basename "${APPIMAGE_FILE}")"
