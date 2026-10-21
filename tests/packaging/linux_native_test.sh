#!/usr/bin/env bash
# Exercise release validation against legacy, private and escaping package layouts.
set -euo pipefail
repo_root="$(realpath "$(dirname "${BASH_SOURCE[0]}")/../..")"
mkdir -p "$repo_root/build_root"
test_root="$(mktemp -d "$repo_root/build_root/native-package-test.XXXXXX")"
trap 'rm -rf -- "$test_root"' EXIT
mkdir -p "$test_root/bin" "$test_root/packages-fixture"
cat > "$test_root/extract" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
cp -a "$TEST_FIXTURE/." "$1"
# NTFS/MSYS cp can rebase a relative symlink to the original fixture. Recreate
# the literal package entry so extraction has the same semantics on both hosts.
if [[ "$TEST_LAYOUT" != legacy ]]; then
  rm "$1/usr/bin/cilogg"
  if [[ "$TEST_LAYOUT" == escape ]]; then
    ln -s "$TEST_OUTSIDE" "$1/usr/bin/cilogg"
  else
    ln -s ../lib/cilogg/bin/cilogg "$1/usr/bin/cilogg"
  fi
fi
EOF
export TEST_EXTRACT="$test_root/extract"
export TEST_OUTSIDE="$test_root/outside"
cat > "$test_root/bin/dpkg-deb" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
case "$1" in
  -c)
    if [[ "$TEST_LAYOUT" == legacy ]]; then
      echo '-rwxr-xr-x root/root 20 2026-01-01 ./usr/bin/cilogg'
    else
      echo 'lrwxrwxrwx root/root 0 2026-01-01 ./usr/bin/cilogg -> ../lib/cilogg/bin/cilogg'
    fi ;;
  -I) echo ' Package: cilogg' ;;
  -x) bash "$TEST_EXTRACT" "$3" ;;
esac
EOF
cat > "$test_root/bin/rpm" <<'EOF'
#!/usr/bin/env bash
if [[ "$*" == *--provides* ]]; then
  echo 'cilogg = 26.04.2'
  [[ "${TEST_RPM_BAD:-}" != provides ]] || echo 'libQt6Core.so.6()(64bit)'
else
  if [[ "${TEST_RPM_BAD:-}" != system ]]; then
    echo 'libc.so.6()(64bit)'
    echo 'libstdc++.so.6()(64bit)'
  fi
  [[ "${TEST_RPM_BAD:-}" != requires ]] || echo 'libQt6Core.so.6()(64bit)'
fi
exit 0
EOF
cat > "$test_root/bin/rpm2cpio" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
cat > "$test_root/bin/cpio" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
cat >/dev/null
bash "$TEST_EXTRACT" .
EOF
chmod +x "$test_root/bin/"*
export PATH="$test_root/bin:$PATH"
export TEST_FIXTURE="$test_root/fixture"

for kind in deb rpm; do
  for layout in legacy private escape missing-plugin; do
    export TEST_LAYOUT="$layout"
    rm -rf -- "$TEST_FIXTURE"
    mkdir -p "$TEST_FIXTURE/usr/bin"
    bindir="$TEST_FIXTURE/usr/bin"
    if [[ "$layout" != legacy ]]; then
      bindir="$TEST_FIXTURE/usr/lib/cilogg/bin"
      mkdir -p "$bindir"
      ln -s ../lib/cilogg/bin/cilogg "$TEST_FIXTURE/usr/bin/cilogg"
    fi
    printf '#!/bin/sh\nexit 0\n' > "$bindir/cilogg"
    chmod +x "$bindir/cilogg"
    mkdir -p "$bindir/plugins"
    for plugin in adb-logcat syslog-server; do
      cp -a "$repo_root/plugins/examples/$plugin" "$bindir/plugins/$plugin"
      cp "$repo_root/plugins/sdk/python/cilogg_plugin.py" "$bindir/plugins/$plugin/"
    done
    if [[ "$layout" == escape ]]; then
      rm "$TEST_FIXTURE/usr/bin/cilogg"
      ln -s "$test_root/outside" "$TEST_FIXTURE/usr/bin/cilogg"
      printf '#!/bin/sh\nexit 0\n' > "$test_root/outside"
      chmod +x "$test_root/outside"
    elif [[ "$layout" == missing-plugin ]]; then
      rm "$bindir/plugins/adb-logcat/cilogg_plugin.py"
    fi
    package="$test_root/packages-fixture/cilogg.$kind"
    touch "$package"
    if (cd "$test_root"; bash "$repo_root/scripts/validate_release_linux.sh") > "$test_root/$kind-$layout.log" 2>&1; then
      [[ "$layout" == legacy || "$layout" == private ]] || { cat "$test_root/$kind-$layout.log"; exit 1; }
    else
      [[ "$layout" == escape || "$layout" == missing-plugin ]] || { cat "$test_root/$kind-$layout.log"; exit 1; }
    fi
    rm "$package"
    echo "PASS: $kind $layout"
  done
done

for metadata in clean provides requires system; do
  export TEST_RPM_BAD="$metadata"
  if bash "$repo_root/scripts/validate_private_rpm.sh" fixture.rpm > "$test_root/metadata-$metadata.log" 2>&1; then
    [[ "$metadata" == clean ]] || { cat "$test_root/metadata-$metadata.log"; exit 1; }
  else
    [[ "$metadata" != clean ]] || { cat "$test_root/metadata-$metadata.log"; exit 1; }
  fi
  echo "PASS: RPM metadata $metadata"
done
