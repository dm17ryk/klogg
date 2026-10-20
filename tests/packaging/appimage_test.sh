#!/usr/bin/env bash
# Exercise the packaging orchestration without downloading tools or requiring Qt.
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
mkdir -p "$repo_root/build_root"
test_root="$(mktemp -d "$repo_root/build_root/appimage-test.XXXXXX")"
cleanup() {
  case "$test_root" in "$repo_root"/build_root/appimage-test.*) rm -rf -- "$test_root" ;; *) exit 1 ;; esac
}
trap cleanup EXIT
mkdir -p "$test_root/bin"
cat > "$test_root/bin/mock" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
case "$(basename "$0")" in
  uname) echo "${TEST_ARCH:-aarch64}" ;;
  qtpaths) echo "$PWD/qt/plugins" ;;
  ninja)
    mkdir -p "$DESTDIR/usr/share/applications" "$DESTDIR/usr/bin"
    touch "$DESTDIR/usr/share/applications/cilogg.desktop"
    ;;
  readelf)
    if [[ "${TEST_FAILURE:-}" == bad-tool && "$*" == *linuxdeployqt* ]]; then
      echo 'Machine: Advanced Micro Devices X86-64'
    elif [[ "${TEST_FAILURE:-}" == bad-output && "$*" == *cilogg-* ]]; then
      echo 'Machine: Advanced Micro Devices X86-64'
    elif [[ "${TEST_ARCH:-aarch64}" == x86_64 ]]; then
      echo 'Machine: Advanced Micro Devices X86-64'
    else
      echo 'Machine: AArch64'
    fi
    ;;
  wget)
    printf '%s\n' "$*" >> "$PWD/downloads.log"
    [[ "${TEST_FAILURE:-}" != download ]] || exit 1
    # The tool is a script fixture; readelf above supplies the ELF metadata.
    cp "$TEST_TOOL" "$3"
    ;;
  cp)
    if [[ "$1" == /usr/share/doc/libc6/copyright ]]; then
      printf 'libc license fixture\n' > "$2"
    elif [[ "$1" == /lib/*/libssl* ]]; then
      touch "${@: -1}/libssl.so.3"
    else
      "$TEST_REAL_CP" "$@"
    fi
    ;;
  python3)
    [[ "${TEST_FAILURE:-}" != plugins ]]
    ;;
esac
EOF
chmod +x "$test_root/bin/mock"
for tool in uname qtpaths ninja readelf wget cp python3; do
  cp "$test_root/bin/mock" "$test_root/bin/$tool"
done
cat > "$test_root/deploy-tool" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf '%s\n' "$* extract=${APPIMAGE_EXTRACT_AND_RUN:-}" >> tool.log
if [[ " $* " == *' -appimage '* ]]; then
  [[ "${TEST_FAILURE:-}" != package ]] || exit 19
  [[ "${TEST_FAILURE:-}" != missing-output ]] || exit 0
  if [[ "${TEST_FAILURE:-}" == empty-output ]]; then
    touch "cilogg-$CILOGG_VERSION-${TEST_ARCH:-aarch64}.AppImage"
  else
    printf 'image fixture\n' > "cilogg-$CILOGG_VERSION-${TEST_ARCH:-aarch64}.AppImage"
    if [[ "${TEST_FAILURE:-}" == ambiguous ]]; then
      cp "cilogg-$CILOGG_VERSION-${TEST_ARCH:-aarch64}.AppImage" "CILogg-$CILOGG_VERSION-${TEST_ARCH:-aarch64}.AppImage"
    fi
  fi
else
  [[ "${TEST_FAILURE:-}" != deploy ]] || exit 18
fi
EOF
chmod +x "$test_root/deploy-tool"
export TEST_REAL_CP="$(command -v cp)"
export PATH="$test_root/bin:$PATH" TEST_TOOL="$test_root/deploy-tool"
export CILOGG_VERSION=26.test.1

run_case() {
  local name="$1" arch="$2" failure="$3" expected="$4"
  local dir="$test_root/$name" status=0
  mkdir -p "$dir/qt/plugins/sqldrivers"
  touch "$dir/qt/plugins/sqldrivers/libqsqlodbc.so"
  if [[ "$failure" == stale-output ]]; then
    touch "$dir/cilogg-$CILOGG_VERSION-$arch.AppImage"
  fi
  (
    cd "$dir"
    export TEST_ARCH="$arch" TEST_FAILURE="$failure"
    if [[ "$failure" == version ]]; then export CILOGG_VERSION=../invalid; fi
    bash "$repo_root/packaging/linux/appimage/generate_appimage.sh"
  ) > "$dir/output.log" 2>&1 || status=$?
  if [[ "$expected" == success && "$status" != 0 ]] || [[ "$expected" == failure && "$status" == 0 ]]; then
    cat "$dir/output.log"
    echo "FAIL: $name returned $status, expected $expected" >&2
    exit 1
  fi
  test -f "$dir/qt/plugins/sqldrivers/libqsqlodbc.so"
  if compgen -G "$dir/packages/*skip*" > /dev/null; then
    echo "FAIL: $name published a skip marker" >&2
    exit 1
  fi
  if [[ "$expected" == failure ]] && compgen -G "$dir/packages/*.AppImage" > /dev/null; then
    echo "FAIL: $name published a failed image" >&2
    exit 1
  fi
  if [[ "$expected" == success ]]; then
    test -s "$dir/packages/cilogg-$CILOGG_VERSION-$arch.AppImage"
    test "$(wc -l < "$dir/downloads.log")" -eq 1
    grep -q "linuxdeployqt-continuous-$arch.AppImage" "$dir/downloads.log"
    grep -q 'extract=1' "$dir/tool.log"
    grep -q -- '-extra-plugins=platforms/libqoffscreen.so' "$dir/tool.log"
    if [[ "$arch" == aarch64 ]]; then
      grep -q -- '-unsupported-allow-new-glibc' "$dir/tool.log"
      test -s "$dir/appdir/usr/share/doc/libc6/copyright"
    else
      ! grep -q -- '-unsupported-allow-new-glibc' "$dir/tool.log"
    fi
  fi
  echo "PASS: $name"
}

run_case arm64 aarch64 '' success
run_case amd64 x86_64 '' success
run_case deployment-failure aarch64 deploy failure
run_case packaging-failure aarch64 package failure
run_case missing-artifact aarch64 missing-output failure
run_case empty-artifact aarch64 empty-output failure
run_case wrong-tool aarch64 bad-tool failure
run_case wrong-artifact aarch64 bad-output failure
run_case stale-artifact aarch64 stale-output failure
run_case ambiguous-artifact aarch64 ambiguous failure
run_case download-failure aarch64 download failure
run_case invalid-version aarch64 version failure
run_case unsupported-arch riscv64 '' failure

cat > "$test_root/image-fixture" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
[[ "$1" == --appimage-extract ]]
[[ "${TEST_FAILURE:-}" != extract ]] || exit 1
mkdir -p squashfs-root/usr/bin/plugins squashfs-root/usr/lib/sqldrivers squashfs-root/usr/plugins/platforms
cat > squashfs-root/AppRun <<'RUN'
#!/usr/bin/env bash
set -euo pipefail
[[ "${TEST_FAILURE:-}" != startup ]] || exit 1
echo "$*" >> "$TEST_LOG"
RUN
chmod +x squashfs-root/AppRun
cp squashfs-root/AppRun squashfs-root/usr/bin/cilogg
touch squashfs-root/usr/lib/libQt6Sql.so.6
touch squashfs-root/usr/plugins/platforms/libqoffscreen.so
if [[ "${TEST_FAILURE:-}" != sqlite ]]; then
  touch squashfs-root/usr/lib/sqldrivers/libqsqlite.so
fi
EOF
chmod +x "$test_root/image-fixture"

validate_case() {
  local name="$1" failure="$2" expected="$3" status=0
  local dir="$test_root/$name"
  mkdir -p "$dir"
  cp "$test_root/image-fixture" "$dir/cilogg-test.AppImage"
  (
    export TEST_ARCH=aarch64 TEST_FAILURE="$failure" TEST_LOG="$dir/startup.log" TMPDIR="$dir"
    bash "$repo_root/packaging/linux/appimage/validate_appimage.sh" "$dir/cilogg-test.AppImage"
  ) > "$dir/output.log" 2>&1 || status=$?
  if [[ "$expected" == success && "$status" != 0 ]] || [[ "$expected" == failure && "$status" == 0 ]]; then
    cat "$dir/output.log"
    echo "FAIL: $name returned $status, expected $expected" >&2
    exit 1
  fi
  if [[ "$expected" == success ]]; then
    grep -q -- '--version' "$dir/startup.log"
    grep -q -- 'mcp --help' "$dir/startup.log"
  fi
  echo "PASS: $name"
}
validate_case validate-bundle '' success
validate_case validate-machine bad-output failure
validate_case validate-extraction extract failure
validate_case validate-sqlite sqlite failure
validate_case validate-plugins plugins failure
validate_case validate-startup startup failure
