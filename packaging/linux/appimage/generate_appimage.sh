#!/bin/bash
set -euo pipefail
export LC_ALL=C

ARCH="$(uname -m)"
LINUXDEPLOYQT_ARCH="x86_64"
LIB_DIR="/lib/x86_64-linux-gnu"
ELF_MACHINE="Advanced Micro Devices X86-64"
DEPLOY_OPTIONS=()

if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
  LINUXDEPLOYQT_ARCH="aarch64"
  LIB_DIR="/lib/aarch64-linux-gnu"
  ELF_MACHINE="AArch64"
  # The ARM Qt binaries already require Ubuntu 24.04 (glibc 2.39). Do not
  # advertise the older baseline enforced by linuxdeployqt's default guard.
  DEPLOY_OPTIONS=(-unsupported-allow-new-glibc)
elif [ "$ARCH" != "x86_64" ]; then
  echo "ERROR: unsupported AppImage architecture: $ARCH" >&2
  exit 1
fi

: "${CILOGG_VERSION:?CILOGG_VERSION must be set}"
if [[ ! "$CILOGG_VERSION" =~ ^[A-Za-z0-9][A-Za-z0-9._+-]*$ ]]; then
  echo "ERROR: invalid AppImage version: $CILOGG_VERSION" >&2
  exit 1
fi
echo "AppImage packaging: architecture=$ARCH tool=$LINUXDEPLOYQT_ARCH machine=$ELF_MACHINE version=$CILOGG_VERSION"
# Avoid FUSE mounts for both linuxdeployqt and its nested appimagetool.
export APPIMAGE_EXTRACT_AND_RUN=1

QT_SQL_DRIVER_EXCLUDES="libqsqlibase.so,libqsqlmimer.so,libqsqlmysql.so,libqsqloci.so,libqsqlodbc.so,libqsqlpsql.so"
QT_SQL_DRIVER_FILES=(libqsqlibase.so libqsqlmimer.so libqsqlmysql.so libqsqloci.so libqsqlodbc.so libqsqlpsql.so)
QT_PLUGIN_DIR=""
QT_SQL_DRIVER_BACKUP_DIR=""

for qtpaths_bin in qtpaths qtpaths6; do
  if command -v "${qtpaths_bin}" >/dev/null 2>&1; then
    QT_PLUGIN_DIR="$(${qtpaths_bin} --plugin-dir 2>/dev/null || true)"
    if [ -n "${QT_PLUGIN_DIR}" ]; then
      break
    fi
  fi
done

if [ -z "${QT_PLUGIN_DIR}" ] && [ -d "/opt/Qt/6.10.1/current/plugins" ]; then
  QT_PLUGIN_DIR="/opt/Qt/6.10.1/current/plugins"
fi

hide_optional_sql_drivers() {
  local sql_driver_dir
  sql_driver_dir="${QT_PLUGIN_DIR}/sqldrivers"

  if [ ! -d "${sql_driver_dir}" ]; then
    echo "No optional SQL driver directory to hide: $sql_driver_dir"
    return
  fi

  QT_SQL_DRIVER_BACKUP_DIR="$(mktemp -d)"

  local driver
  for driver in "${QT_SQL_DRIVER_FILES[@]}"; do
    if [ -f "${sql_driver_dir}/${driver}" ]; then
      echo "Temporarily excluding optional SQL driver: $driver"
      mv "${sql_driver_dir}/${driver}" "${QT_SQL_DRIVER_BACKUP_DIR}/${driver}"
    fi
  done
}

restore_optional_sql_drivers() {
  local sql_driver_dir
  sql_driver_dir="${QT_PLUGIN_DIR}/sqldrivers"

  if [ -z "${QT_SQL_DRIVER_BACKUP_DIR}" ] || [ ! -d "${QT_SQL_DRIVER_BACKUP_DIR}" ]; then
    return
  fi

  mkdir -p "${sql_driver_dir}"

  local driver
  for driver in "${QT_SQL_DRIVER_FILES[@]}"; do
    if [ -f "${QT_SQL_DRIVER_BACKUP_DIR}/${driver}" ]; then
      echo "Restoring optional SQL driver: $driver"
      mv "${QT_SQL_DRIVER_BACKUP_DIR}/${driver}" "${sql_driver_dir}/${driver}"
    fi
  done

  rmdir "${QT_SQL_DRIVER_BACKUP_DIR}" 2>/dev/null || true
  QT_SQL_DRIVER_BACKUP_DIR=""
}

trap restore_optional_sql_drivers EXIT

# Do not mistake an image left by a previous run for fresh output.
shopt -s nullglob
previous_images=(./cilogg-"$CILOGG_VERSION"-*.AppImage ./CILogg-"$CILOGG_VERSION"-*.AppImage)
shopt -u nullglob
if [ "${#previous_images[@]}" -ne 0 ]; then
  echo "ERROR: remove previous AppImage output for $CILOGG_VERSION before packaging." >&2
  exit 1
fi

DESTDIR=$(readlink -f appdir) ninja install
if [ "${#DEPLOY_OPTIONS[@]}" -ne 0 ]; then
  echo "ARM64 compatibility baseline: Ubuntu 24.04 / glibc 2.39; preserving libc license."
  mkdir -p appdir/usr/share/doc/libc6
  cp /usr/share/doc/libc6/copyright appdir/usr/share/doc/libc6/copyright
fi
hide_optional_sql_drivers

APPIMAGE_TOOL="linuxdeployqt-continuous-${LINUXDEPLOYQT_ARCH}.AppImage"
echo "Downloading architecture-matched deployment tool: $APPIMAGE_TOOL"
wget -q -O "$APPIMAGE_TOOL" "https://github.com/probonopd/linuxdeployqt/releases/download/continuous/$APPIMAGE_TOOL"
chmod a+x "$APPIMAGE_TOOL"
readelf -h "$APPIMAGE_TOOL" | grep -F "Machine:" | grep -F "$ELF_MACHINE" || {
  echo "ERROR: deployment tool does not match $ELF_MACHINE" >&2
  exit 1
}

run_linuxdeployqt() {
  echo "Running $APPIMAGE_TOOL: $*"
  VERSION="$CILOGG_VERSION" "./$APPIMAGE_TOOL" "$@" "${DEPLOY_OPTIONS[@]}"
}

if ! run_linuxdeployqt appdir/usr/share/applications/*.desktop \
  -bundle-non-qt-libs \
  -extra-plugins=platforms/libqoffscreen.so \
  "-exclude-libs=${QT_SQL_DRIVER_EXCLUDES}"; then
  echo "ERROR: linuxdeployqt dependency deployment failed for $ARCH." >&2
  exit 1
fi

mkdir -p appdir/usr/lib
cp "${LIB_DIR}"/libssl* appdir/usr/lib

if ! run_linuxdeployqt appdir/usr/share/applications/*.desktop \
  -appimage \
  -extra-plugins=platforms/libqoffscreen.so \
  "-exclude-libs=${QT_SQL_DRIVER_EXCLUDES}"; then
  echo "ERROR: linuxdeployqt AppImage creation failed for $ARCH." >&2
  exit 1
fi

mkdir -p ./packages
shopt -s nullglob
images=(./cilogg-"$CILOGG_VERSION"-*.AppImage ./CILogg-"$CILOGG_VERSION"-*.AppImage)
shopt -u nullglob
if [ "${#images[@]}" -ne 1 ]; then
  echo "ERROR: expected one AppImage for $CILOGG_VERSION, found ${#images[@]}." >&2
  ls -1 ./*.AppImage 2>/dev/null || true
  exit 1
fi

APPIMAGE_FILE="${images[0]}"
test -s "$APPIMAGE_FILE"
readelf -h "$APPIMAGE_FILE" | grep -F "Machine:" | grep -F "$ELF_MACHINE" || {
  echo "ERROR: generated AppImage does not match $ELF_MACHINE" >&2
  exit 1
}
echo "Publishing AppImage: $APPIMAGE_FILE ($(wc -c < "$APPIMAGE_FILE") bytes)"
cp "${APPIMAGE_FILE}" "./packages/$(basename "${APPIMAGE_FILE}")"
