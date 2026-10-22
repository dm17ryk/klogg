#!/usr/bin/env bash
# Verify the real launcher executes a canonical private path and preserves argv.
set -euo pipefail
repo_root="$(realpath "$(dirname "${BASH_SOURCE[0]}")/../..")"
mkdir -p "$repo_root/build_root"
workdir=$(mktemp -d "$repo_root/build_root/launcher-test.XXXXXX")
trap 'rm -rf -- "$workdir"' EXIT
root="$workdir/install root"
mkdir -p "$root/usr/bin" "$root/usr/lib64/cilogg/bin"
for command_name in cilogg cilogg_grep; do
  private="$root/usr/lib64/cilogg/bin/$command_name"
  cat > "$private" <<'EOF'
#!/bin/sh
printf '%s\n' "$0" "$@"
EOF
  chmod +x "$private"
  sed -e 's|@_klogg_public_to_private@|../lib64/cilogg/bin|g' \
      -e "s|@_klogg_command@|$command_name|g" \
      "$repo_root/cmake/LinuxCommandLauncher.sh.in" > "$root/usr/bin/$command_name"
  chmod +x "$root/usr/bin/$command_name"
  expected=$(printf '%s\n' "$(realpath -e "$private")" 'argument with spaces' '--help')
  actual=$("$root/usr/bin/$command_name" 'argument with spaces' --help)
  [[ "$actual" == "$expected" ]]
  actual=$(cd "$workdir"; PATH="$root/usr/bin:$PATH" "$command_name" 'argument with spaces' --help)
  [[ "$actual" == "$expected" ]]
  ln -s "$root/usr/bin/$command_name" "$workdir/$command_name-link"
  actual=$("$workdir/$command_name-link" 'argument with spaces' --help)
  [[ "$actual" == "$expected" ]]
  echo "PASS: $command_name canonical private path and argv, absolute/PATH/symlink invocation"
done
