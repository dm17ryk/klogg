#!/usr/bin/env bash
# Record a published release in the legacy latest.json and push it to a branch
# of origin (#178).
#
# Usage: publish_legacy_latest_json.sh <ci|stable> <version> <branch>
#
# Run from a checkout of <branch>. The branch can move while "Make CI Release"
# runs; a rejected push is retried after rebasing the one-file commit onto the
# branch's new tip. A concurrent change to latest.json itself stops the script
# instead of overwriting it.
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

if ! git ls-remote --exit-code --heads origin "$branch" > /dev/null; then
  echo "::error::$branch is not a branch on origin; latest.json is only kept on branches."
  exit 1
fi

CHANNEL="$channel" VERSION="$version" TAG="$tag" python3 - <<'PY'
import json
import os
from pathlib import Path

path = Path("latest.json")
data = json.loads(path.read_text(encoding="utf-8"))
channel = os.environ["CHANNEL"]
data[channel] = os.environ["VERSION"]
data[channel + "_url"] = "https://github.com/dm17ryk/klogg/releases/tag/" + os.environ["TAG"]
path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
PY

git add latest.json
if git diff --cached --quiet; then
  echo "latest.json already names $channel $version."
  exit 0
fi
git commit -q -m "chore: update legacy release metadata [skip ci]"

for ((attempt = 1; ; attempt++)); do
  if git push origin "HEAD:refs/heads/$branch"; then
    echo "latest.json on $branch now names $channel $version."
    exit 0
  fi
  if ((attempt >= attempts)); then
    echo "::error::Could not push latest.json to $branch after $attempts attempts."
    exit 1
  fi
  echo "::warning::Push of latest.json to $branch was rejected (attempt $attempt of $attempts); rebasing onto its new tip."
  sleep $((delay * attempt))
  git fetch --no-tags origin "$branch"
  if ! git rebase --autostash FETCH_HEAD; then
    git rebase --abort || true
    echo "::error::latest.json changed on $branch since this release started; record $channel $version in it by hand."
    exit 1
  fi
done
