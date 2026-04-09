#!/usr/bin/env bash
set -euo pipefail

assert_path() {
  local path="$1"
  local message="$2"
  if [[ ! -e "$path" ]]; then
    echo "$message" >&2
    exit 1
  fi
}

validate_app_bundle() {
  local app="$1"
  assert_path "$app/Contents/MacOS/klogg" "Missing app executable in $app"
  assert_path "$app/Contents/Frameworks/QtSql.framework" "Missing QtSql.framework in $app"
  assert_path "$app/Contents/Frameworks/QtSerialPort.framework" "Missing QtSerialPort.framework in $app"
  assert_path "$app/Contents/PlugIns/platforms/libqcocoa.dylib" "Missing qcocoa plugin in $app"
}

validate_dmg() {
  local dmg="$1"
  assert_path "$dmg" "Missing DMG file $dmg"
  hdiutil imageinfo "$dmg" >/dev/null
}

for dir in ./packages-macos-*; do
  [[ -d "$dir" ]] || continue

  app="$(find "$dir" -maxdepth 1 -name '*.app' -print -quit)"
  dmg="$(find "$dir" -maxdepth 1 -name '*.dmg' -print -quit)"

  [[ -n "$app" ]] || { echo "No .app found in $dir" >&2; exit 1; }
  [[ -n "$dmg" ]] || { echo "No .dmg found in $dir" >&2; exit 1; }

  validate_app_bundle "$app"
  validate_dmg "$dmg"
done

echo "macOS release validation passed."
