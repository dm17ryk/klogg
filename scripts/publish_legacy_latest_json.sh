#!/usr/bin/env bash
# Record a published release in the legacy latest.json and push it to a branch
# of origin (#178).
#
# Usage: publish_legacy_latest_json.sh <ci|stable> <version> <branch>
#
# Run from a checkout of <branch>. The branch can move while "Make CI Release"
# runs, so a rejected push is retried: the one-key edit is made again on the
# branch's new tip (a rebase that cannot conflict) and pushed. Other changes to
# latest.json are kept, and a newer version already recorded for the channel,
# for example by an overlapping release, is never replaced by an older one.
set -euo pipefail

channel="$1"
version="$2"
branch="$3"
attempts="${CILOGG_PUSH_ATTEMPTS:-5}"
delay="${CILOGG_PUSH_RETRY_DELAY:-10}"

case "$channel" in
  ci) tag="continuous-$version" ;;
  stable) tag="v$version" ;;
  *) echo "Unknown release channel: $channel" >&2; exit 2 ;;
esac

# git ls-remote --exit-code exits with 2 when the branch does not exist; any other
# failure (for example the network) is retried, so it is not reported as a missing branch.
for ((check = 1; ; check++)); do
  remote_status=0
  git ls-remote --exit-code --heads origin "$branch" > /dev/null || remote_status=$?
  if ((remote_status == 0)); then
    break
  fi
  if ((remote_status == 2)); then
    echo "::error::$branch is not a branch on origin; latest.json is only kept on branches."
    exit 1
  fi
  if ((check >= attempts)); then
    echo "::error::Could not reach origin to check $branch (git ls-remote exited with $remote_status)."
    exit 1
  fi
  echo "::warning::Could not reach origin to check $branch (attempt $check of $attempts); trying again."
  sleep $((delay * check))
done

# Records the release in latest.json and commits it. Returns 0 after committing
# and 1 when latest.json needs no change; exits the script on errors (set -e
# does not apply inside a function called from `if`).
commit_release() {
  local status=0
  CHANNEL="$channel" VERSION="$version" TAG="$tag" python3 - <<'PY' || status=$?
import json
import os
from pathlib import Path


def numbers(text):
    try:
        return tuple(int(part) for part in text.split("."))
    except (AttributeError, ValueError):
        return None


path = Path("latest.json")
data = json.loads(path.read_text(encoding="utf-8"))
channel = os.environ["CHANNEL"]
version = os.environ["VERSION"]
current = data.get(channel)
if numbers(current) and numbers(version) and numbers(current) > numbers(version):
    print(f"latest.json already names {channel} {current}, newer than {version}; leaving it.")
    raise SystemExit(0)
data[channel] = version
data[channel + "_url"] = "https://github.com/dm17ryk/klogg/releases/tag/" + os.environ["TAG"]
path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
PY
  if ((status != 0)); then
    echo "::error::Could not update latest.json (python exited with $status)."
    exit 1
  fi
  git add latest.json || exit 1
  if git diff --cached --quiet; then
    return 1
  fi
  git commit -q -m "chore: update legacy release metadata [skip ci]" || exit 1
}

if ! commit_release; then
  echo "latest.json needs no change for $channel $version."
  exit 0
fi

for ((attempt = 1; ; attempt++)); do
  if git push origin "HEAD:refs/heads/$branch"; then
    echo "latest.json on $branch now names $channel $version."
    exit 0
  fi
  if ((attempt >= attempts)); then
    echo "::error::Could not push latest.json to $branch after $attempts attempts."
    exit 1
  fi
  echo "::warning::Push of latest.json to $branch was rejected (attempt $attempt of $attempts); redoing it on the new tip."
  sleep $((delay * attempt))
  if ! git fetch --no-tags origin "$branch"; then
    echo "::warning::Could not fetch $branch (attempt $attempt of $attempts); trying again."
    continue
  fi
  # Only latest.json is tracked-and-changed here; the release files are untracked.
  git reset -q --hard FETCH_HEAD
  if ! commit_release; then
    echo "latest.json on $branch needs no change for $channel $version."
    exit 0
  fi
done
