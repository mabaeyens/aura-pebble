#!/usr/bin/env bash
# Sync one monorepo watchface subdirectory to its own CloudPebble deploy branch.
#
# Why: CloudPebble's GitHub integration builds the Pebble project at the ROOT of
# the repo, but this repo is a monorepo (aura-analog / aura-digital /
# aura-essential each live in a subdirectory, with no project at the root). A
# direct GitHub sync therefore just builds the first subdir it finds
# (aura-analog, alphabetically). This script splits one face's subdirectory into
# a branch whose root IS that project, so CloudPebble pointed at the branch
# builds the face you want.
#
# Usage:
#   scripts/sync-cloudpebble.sh essential        # or: aura-essential
#   scripts/sync-cloudpebble.sh analog
#
# The deploy branch is also pinned to Emery (Pebble Time 2) ONLY: CloudPebble
# compiles every platform in targetPlatforms, and for fast single-device
# iteration we don't want all five. This is applied as one extra commit on top of
# the split, so it survives every re-sync. main keeps all five platforms for the
# distributable .pbw.
#
# Then in CloudPebble set the GitHub branch to cloudpebble-<face> and pull.
# Re-run this after committing changes to the face on main; it force-pushes the
# regenerated split, which is expected — the branch is a mirror, not shared work.
set -euo pipefail

face="${1:-}"
if [[ -z "$face" ]]; then
  echo "usage: $0 <face>    e.g. $0 essential   (accepts 'essential' or 'aura-essential')" >&2
  exit 2
fi

# Accept either the short name ("essential") or the full dir name ("aura-essential").
dir="$face"
[[ "$dir" == aura-* ]] || dir="aura-$dir"
short="${dir#aura-}"
branch="cloudpebble-$short"

cd "$(git rev-parse --show-toplevel)"

if [[ ! -f "$dir/package.json" ]]; then
  echo "error: '$dir/package.json' not found — is '$face' a real face directory?" >&2
  exit 1
fi

# The split only captures COMMITTED content — warn on a dirty subdir.
if ! git diff --quiet -- "$dir" || ! git diff --cached --quiet -- "$dir"; then
  echo "warning: '$dir' has uncommitted changes; commit them first or the split will miss them." >&2
fi

ref="$(git rev-parse --abbrev-ref HEAD)"
echo "splitting $dir/ (from $ref) -> $branch"
git branch -D "$branch" 2>/dev/null || true
git subtree split --prefix="$dir" -b "$branch"

# Pin the deploy branch to Emery only, as one commit on top of the split.
# Uses a detached worktree so the main working tree is never touched, then
# fast-forwards the branch ref to the new tip.
echo "pinning $branch to targetPlatforms=[emery]"
wt="$(mktemp -d)"
git worktree add -q --detach "$wt" "$branch"
python3 - "$wt/package.json" <<'PY'
import json, sys
path = sys.argv[1]
with open(path) as f:
    manifest = json.load(f)
manifest["pebble"]["targetPlatforms"] = ["emery"]
with open(path, "w") as f:
    json.dump(manifest, f, indent=2)
    f.write("\n")
PY
git -C "$wt" add package.json
GIT_COMMITTER_NAME="Miguel A. Baeyens" \
GIT_COMMITTER_EMAIL="8981792+mabaeyens@users.noreply.github.com" \
  git -C "$wt" commit -q \
    --author="Miguel A. Baeyens <8981792+mabaeyens@users.noreply.github.com>" \
    -m "cloudpebble: build Emery (Pebble Time 2) only on this deploy branch"
newtip="$(git -C "$wt" rev-parse HEAD)"
git worktree remove --force "$wt"
git branch -f "$branch" "$newtip"

git push -f origin "$branch"

echo "done: pushed '$branch' (Emery only). In CloudPebble, set the GitHub branch to '$branch' and pull."
