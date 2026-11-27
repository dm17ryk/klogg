#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

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
  assert_path "$app/Contents/MacOS/cilogg" "Missing app executable in $app"
  assert_path "$app/Contents/Frameworks/QtSql.framework" "Missing QtSql.framework in $app"
  assert_path "$app/Contents/Frameworks/QtSerialPort.framework" "Missing QtSerialPort.framework in $app"
  assert_path "$app/Contents/PlugIns/platforms/libqcocoa.dylib" "Missing qcocoa plugin in $app"
  python3 "$SCRIPT_DIR/plugins/validate_preinstalled.py" "$app/Contents/PlugIns/cilogg"
  # Script Runner and scenarios load the Python runtime from Contents/Resources.
  local runtime_file
  for runtime_file in worker_bootstrap.py scenario_bootstrap.py klogg/__init__.py; do
    assert_path "$app/Contents/Resources/python_runtime/$runtime_file" \
      "Missing python_runtime/$runtime_file in $app/Contents/Resources"
  done
}

validate_dmg() (
  local dmg="$1"
  local mountpoint
  local app
  assert_path "$dmg" "Missing DMG file $dmg"
  hdiutil imageinfo "$dmg" >/dev/null
  mountpoint="$(mktemp -d)"
  trap 'rmdir "$mountpoint"' EXIT
  hdiutil attach "$dmg" -readonly -nobrowse -mountpoint "$mountpoint" >/dev/null
  trap 'hdiutil detach "$mountpoint" >/dev/null; rmdir "$mountpoint"' EXIT
  app="$(find "$mountpoint" -maxdepth 2 -name '*.app' -print -quit)"
  [[ -n "$app" ]] || { echo "No .app found inside $dmg" >&2; exit 1; }
  validate_app_bundle "$app"
)

# Every macOS package must be present. During a hosted Apple Silicon runner outage
# "Make CI Release" passes the arm64 package directories whose CI Build jobs never
# got a runner; only those may be missing (docs/qt-platforms.md).
package_check=(macos-packages --root .)
for directory in ${CILOGG_ALLOWED_MISSING_MACOS_PACKAGES:-}; do
  package_check+=(--allow-missing "$directory")
done
missing="$(python3 "$SCRIPT_DIR/check_release_inputs.py" "${package_check[@]}")"
if [[ -n "$missing" ]]; then
  echo "Left out of this release: ${missing//$'\n'/, }"
fi

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
