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
    return
  fi

  QT_SQL_DRIVER_BACKUP_DIR="$(mktemp -d)"

  local driver
  for driver in "${QT_SQL_DRIVER_FILES[@]}"; do
    if [ -f "${sql_driver_dir}/${driver}" ]; then
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
      mv "${QT_SQL_DRIVER_BACKUP_DIR}/${driver}" "${sql_driver_dir}/${driver}"
    fi
  done

  rmdir "${QT_SQL_DRIVER_BACKUP_DIR}" 2>/dev/null || true
  QT_SQL_DRIVER_BACKUP_DIR=""
}

trap restore_optional_sql_drivers EXIT

DESTDIR=$(readlink -f appdir) ninja install
hide_optional_sql_drivers

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

    if VERSION=$CILOGG_VERSION "./${appimage}" "${args[@]}"; then
      return 0
    fi

    echo "linuxdeployqt (${arch}) failed, trying fallback if available..." >&2
  done

  return 1
}

if ! run_linuxdeployqt appdir/usr/share/applications/*.desktop \
  -bundle-non-qt-libs \
  "-exclude-libs=${QT_SQL_DRIVER_EXCLUDES}"; then
  if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
    echo "WARNING: linuxdeployqt failed on ARM64, skipping AppImage generation." >&2
    mkdir -p ./packages
    echo "AppImage generation skipped on ARM64 due linuxdeployqt incompatibility." \
      > "./packages/cilogg-${CILOGG_VERSION}-appimage-arm64-skip.txt"
    exit 0
  fi
  exit 1
fi

mkdir -p appdir/usr/lib
cp "${LIB_DIR}"/libssl* appdir/usr/lib

if ! run_linuxdeployqt appdir/usr/share/applications/*.desktop \
  -appimage \
  "-exclude-libs=${QT_SQL_DRIVER_EXCLUDES}"; then
  if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
    echo "WARNING: linuxdeployqt failed on ARM64 during AppImage stage, skipping." >&2
    mkdir -p ./packages
    echo "AppImage generation skipped on ARM64 due linuxdeployqt incompatibility." \
      > "./packages/cilogg-${CILOGG_VERSION}-appimage-arm64-skip.txt"
    exit 0
  fi
  exit 1
fi

mkdir -p ./packages
APPIMAGE_FILE="$(ls ./cilogg-$CILOGG_VERSION-*.AppImage | head -n 1)"
cp "${APPIMAGE_FILE}" "./packages/$(basename "${APPIMAGE_FILE}")"
