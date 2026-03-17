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

for dir in ./packages-macos-*; do
  [[ -d "$dir" ]] || continue

  app="$(find "$dir" -maxdepth 1 -name '*.app' -print -quit)"
  dmg="$(find "$dir" -maxdepth 1 -name '*.dmg' -print -quit)"

  [[ -n "$app" ]] || { echo "No .app found in $dir" >&2; exit 1; }
  [[ -n "$dmg" ]] || { echo "No .dmg found in $dir" >&2; exit 1; }

  validate_app_bundle "$app"

  mount_point="$(mktemp -d /tmp/klogg-dmg-XXXXXX)"
  hdiutil attach "$dmg" -mountpoint "$mount_point" -nobrowse -readonly >/dev/null
  mounted_app="$(find "$mount_point" -maxdepth 1 -name '*.app' -print -quit)"
  [[ -n "$mounted_app" ]] || { echo "No .app found inside DMG $dmg" >&2; hdiutil detach "$mount_point" -quiet || true; exit 1; }
  assert_path "$mounted_app/Contents/MacOS/klogg" "Mounted DMG app is missing executable for $dmg"
  hdiutil detach "$mount_point" -quiet
  rmdir "$mount_point"
done

echo "macOS release validation passed."
