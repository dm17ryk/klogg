#!/usr/bin/env bash
set -euo pipefail
for package in "$@"; do
  echo "Validating private RPM dependency metadata: $package"
  provides="$(rpm -qp --provides "$package")"
  requires="$(rpm -qp --requires "$package")"
  printf 'Provides:\n%s\nRequires:\n%s\n' "$provides" "$requires"
  if grep -Eq '^lib(Qt6|icu|tbb)' <<<"$provides"; then
    echo "Private runtime capabilities must not be advertised globally: $package" >&2
    exit 1
  fi
  if grep -Eq '^lib(Qt6|icu|tbb)' <<<"$requires"; then
    echo "Bundled runtime capabilities must not remain external requirements: $package" >&2
    exit 1
  fi
  grep -Eq '^libc[.]so' <<<"$requires" || { echo "Missing system libc dependency" >&2; exit 1; }
  grep -Eq '^libstdc[+][+][.]so' <<<"$requires" || { echo "Missing system C++ runtime dependency" >&2; exit 1; }
done
