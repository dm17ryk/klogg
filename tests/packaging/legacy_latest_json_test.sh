#!/usr/bin/env bash
# "Make CI Release" records the release in latest.json and pushes it while
# master may have moved on (#178).
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
script="$repo_root/scripts/publish_legacy_latest_json.sh"
workdir=$(mktemp -d)
trap 'rm -rf -- "$workdir"' EXIT
export CILOGG_PUSH_RETRY_DELAY=0

fail() { echo "FAIL: $*" >&2; exit 1; }

git_as_ci() { git -c user.name=ci -c user.email=ci@example.invalid "$@"; }

# A bare "origin" whose master holds latest.json, and a shallow clone like
# actions/checkout makes.
new_origin() {
  local name="$1"
  rm -rf "$workdir/$name" "$workdir/$name-seed" "$workdir/$name-job" "$workdir/$name-other"
  git init -q --bare -b master "$workdir/$name"
  git init -q -b master "$workdir/$name-seed"
  printf '{\n  "ci": "26.04.2.1",\n  "ci_url": "old"\n}\n' > "$workdir/$name-seed/latest.json"
  git -C "$workdir/$name-seed" add latest.json
  git_as_ci -C "$workdir/$name-seed" commit -q -m initial
  git -C "$workdir/$name-seed" push -q "$workdir/$name" master
  git clone -q --depth 1 "file://$workdir/$name" "$workdir/$name-job"
  git -C "$workdir/$name-job" config user.name github-actions
  git -C "$workdir/$name-job" config user.email github-actions@github.com
  git clone -q "file://$workdir/$name" "$workdir/$name-other"
}

# master moves on while the release job runs.
push_other_commit() {
  local name="$1" file="$2" content="$3"
  printf '%s' "$content" > "$workdir/$name-other/$file"
  git -C "$workdir/$name-other" add "$file"
  git_as_ci -C "$workdir/$name-other" commit -q -m "other change to $file"
  git -C "$workdir/$name-other" push -q origin master
}

remote_latest() { git -C "$workdir/$1" show master:latest.json; }

# 1. master moved: the push is rejected once, then rebased and pushed.
new_origin moved
push_other_commit moved feature.txt "merged while releasing"
(cd "$workdir/moved-job" && bash "$script" ci 26.04.2.1462 master) > "$workdir/moved.log" 2>&1 \
  || { cat "$workdir/moved.log"; fail "push after master moved"; }
remote_latest moved | grep -q '"ci": "26.04.2.1462"' || fail "latest.json not updated on master"
remote_latest moved | grep -q '"ci_url": "https://github.com/dm17ryk/klogg/releases/tag/continuous-26.04.2.1462"' \
  || fail "ci_url not updated"
git -C "$workdir/moved" show master:feature.txt | grep -q "merged while releasing" || fail "other commit lost"
[[ "$(git -C "$workdir/moved" log --format=%s master)" == $'chore: update legacy release metadata [skip ci]\nother change to feature.txt\ninitial' ]] \
  || fail "unexpected master history: $(git -C "$workdir/moved" log --oneline master)"
grep -q "rejected" "$workdir/moved.log" || fail "the first push was expected to be rejected"
echo "PASS: master moved during the release; latest.json rebased and pushed"

# 2. Nothing to record: no commit, no push.
before=$(git -C "$workdir/moved" rev-parse master)
(cd "$workdir/moved-job" && bash "$script" ci 26.04.2.1462 master) | grep -q "already names" \
  || fail "expected 'already names' for an unchanged latest.json"
[[ "$(git -C "$workdir/moved" rev-parse master)" == "$before" ]] || fail "unchanged latest.json was pushed"
echo "PASS: an unchanged latest.json is not committed"

# 3. Stable channel keeps the ci entry.
new_origin stable
(cd "$workdir/stable-job" && bash "$script" stable 26.04.3.0 master) > "$workdir/stable.log" 2>&1 \
  || { cat "$workdir/stable.log"; fail "stable release push"; }
remote_latest stable | grep -q '"stable_url": "https://github.com/dm17ryk/klogg/releases/tag/v26.04.3.0"' \
  || fail "stable_url not updated"
remote_latest stable | grep -q '"ci": "26.04.2.1"' || fail "ci entry changed by a stable release"
echo "PASS: stable release updates only the stable entries"

# 4. Someone else changed latest.json meanwhile: stop, leave no rebase behind.
new_origin conflict
push_other_commit conflict latest.json $'{\n  "ci": "26.04.2.2",\n  "ci_url": "other"\n}\n'
if (cd "$workdir/conflict-job" && bash "$script" ci 26.04.2.1462 master) > "$workdir/conflict.log" 2>&1; then
  fail "a conflicting latest.json was overwritten"
fi
grep -q "changed on master" "$workdir/conflict.log" || { cat "$workdir/conflict.log"; fail "unclear conflict message"; }
[[ ! -d "$(git -C "$workdir/conflict-job" rev-parse --git-path rebase-merge)" ]] || fail "rebase left in progress"
remote_latest conflict | grep -q '"ci": "26.04.2.2"' || fail "remote latest.json was changed"
echo "PASS: a concurrent latest.json change stops the push with a clear message"

# 5. A ref that is not a branch on origin is refused before any push.
new_origin tag
if (cd "$workdir/tag-job" && bash "$script" ci 26.04.2.1462 continuous-26.04.2.1462) > "$workdir/tag.log" 2>&1; then
  fail "pushed to a ref that is not a branch"
fi
grep -q "not a branch" "$workdir/tag.log" || { cat "$workdir/tag.log"; fail "unclear branch message"; }
[[ -z "$(git -C "$workdir/tag" branch --list 'continuous-*')" ]] || fail "created a branch"
echo "PASS: only an existing branch is updated"
