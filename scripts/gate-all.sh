#!/usr/bin/env bash
# The single canonical gate runner. Runs EVERY merge gate and stamps the
# exact HEAD it validated; scripts/git-hooks/pre-push refuses to push any
# code commit without a matching stamp.
#
# THE EXIT CODE IS THE VERDICT. Do not scrape logs for success, do not
# wrap this in `... || { echo failed; }` compounds (that masks the exit
# code - the 2026-07-16 broken-master push happened exactly that way, a
# failure branch that exited 0 and a tail -1 read in the same breath as
# the push). Run it, read $?, done. On success the LAST line is
# "GATES: ALL OK @ <sha>"; any other ending is a failure.
#
# Usage: scripts/gate-all.sh          (run after the final commit, before push)
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"

# same pinned-toolchain guard as build-check: a bare system cmake poisons
# builds, so re-exec into the devshell unless the pinned one is in PATH
if ! command -v cmake >/dev/null 2>&1 || [[ "$(command -v cmake)" != /nix/store/* ]]; then
    exec nix develop "$repo" -c "$0" "$@"
fi

"$repo/scripts/build-check.sh"        # both WITH_X11 variants + full ctest + coverage ratchet
"$repo/tests/coverage/qmllint-gate.sh"       # baseline only shrinks
"$repo/scripts/sceneprobe-gate.sh"    # real-pixel scene gate incl. self-test

sha="$(git -C "$repo" rev-parse HEAD)"
mkdir -p "$repo/build"
printf '%s\n' "$sha" > "$repo/build/_gates-passed"
echo "GATES: ALL OK @ $sha"
