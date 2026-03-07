#!/bin/bash
set -e

ARCH="$(uname -m)"
LINUXDEPLOYQT_ARCH="x86_64"
LIB_DIR="/lib/x86_64-linux-gnu"

if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
  LINUXDEPLOYQT_ARCH="aarch64"
  LIB_DIR="/lib/aarch64-linux-gnu"
fi

DESTDIR=$(readlink -f appdir) ninja install
wget -c -q "https://github.com/probonopd/linuxdeployqt/releases/download/continuous/linuxdeployqt-continuous-${LINUXDEPLOYQT_ARCH}.AppImage"
chmod a+x "linuxdeployqt-continuous-${LINUXDEPLOYQT_ARCH}.AppImage"

VERSION=$KLOGG_VERSION "./linuxdeployqt-continuous-${LINUXDEPLOYQT_ARCH}.AppImage" appdir/usr/share/applications/*.desktop -bundle-non-qt-libs

mkdir -p appdir/usr/lib
cp "${LIB_DIR}"/libssl* appdir/usr/lib

VERSION=$KLOGG_VERSION "./linuxdeployqt-continuous-${LINUXDEPLOYQT_ARCH}.AppImage" appdir/usr/share/applications/*.desktop -appimage

mkdir -p ./packages
APPIMAGE_FILE="$(ls ./klogg-$KLOGG_VERSION-*.AppImage | head -n 1)"
cp "${APPIMAGE_FILE}" "./packages/$(basename "${APPIMAGE_FILE}")"
