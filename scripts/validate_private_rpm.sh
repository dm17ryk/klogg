#!/usr/bin/env bash
set -euo pipefail
for package in "$@"; do
  echo "Validating private RPM dependency metadata: $package"
  provides="$(rpm -qp --provides "$package")"
  requires="$(rpm -qp --requires "$package")"
  files="$(rpm -qpl "$package")"
  bundled=""
  while IFS= read -r file; do
    case "$file" in
      */cilogg/lib/*.so*|*/cilogg/qt-plugins/*.so*) bundled+="${file##*/}"$'\n' ;;
    esac
  done <<<"$files"
  [[ -n "$bundled" ]] || { echo "Missing private library payload" >&2; exit 1; }
  printf 'Provides:\n%s\nRequires:\n%s\n' "$provides" "$requires"
  if grep -Eq '^lib(Qt6|icu|tbb)' <<<"$provides"; then
    echo "Private runtime capabilities must not be advertised globally: $package" >&2
    exit 1
  fi
  while IFS= read -r requirement; do
    soname="${requirement%%(*}"
    if grep -Fxq "$soname" <<<"$bundled"; then
      echo "Bundled runtime capability remains an external requirement: $requirement" >&2
      exit 1
    fi
  done <<<"$requires"
  grep -Eq '^libc[.]so' <<<"$requires" || { echo "Missing system libc dependency" >&2; exit 1; }
  grep -Eq '^libstdc[+][+][.]so' <<<"$requires" || { echo "Missing system C++ runtime dependency" >&2; exit 1; }
done
