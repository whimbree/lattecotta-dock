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

# Pin-vs-system lockstep guard (the 2026-07-17 incident: a system rebuild
# under a stale dev pin SIGSEGVs every NEW nested kwin_wayland at
# QApplication init, taking the sceneprobe gate down for a whole night of
# bisection). The dev flake's match-the-desktop doctrine is only real if
# it is checked, so check it here and fail fast: the system generation's
# short nixpkgs rev is the last dotted component of the store name
# /run/current-system resolves to.
if [[ -e /run/current-system ]]; then
    sysrev="$(basename "$(readlink /run/current-system)")"
    sysrev="${sysrev##*.}"
    pinrev="$(jq -r '.nodes.nixpkgs.locked.rev' "$repo/flake.lock")"
    if [[ -z "$sysrev" || "$pinrev" != "$sysrev"* ]]; then
        echo "gate-all: FAIL pin/system lockstep: flake.lock pins nixpkgs $pinrev"
        echo "  but /run/current-system runs $sysrev (system rebuilt under the pin?)."
        echo "  Re-pin first: set flake.nix's nixpkgs to the root nixpkgs rev of"
        echo "  /persist/etc/nixos/flake.lock, nix flake update nixpkgs, full"
        echo "  rebuild, expect a sceneprobe golden re-bless. See flake.nix's"
        echo "  pin comment and the 2026-07-17 handoff incident entry."
        exit 4
    fi
else
    # not NixOS (e.g. a future hosted CI runner): there is no system pin
    # to be in lockstep with, so the guard does not apply - say so loudly
    # rather than silently skipping.
    echo "gate-all: lockstep guard skipped: no /run/current-system on this host"
fi

"$repo/scripts/build-check.sh"        # full build + full ctest + coverage ratchet
"$repo/tests/coverage/qmllint-gate.sh"       # baseline only shrinks
"$repo/scripts/sceneprobe-gate.sh"    # real-pixel scene gate incl. self-test
"$repo/scripts/asan-e2e-gate.sh"      # driven sanitized dock: UB in integration paths aborts
"$repo/scripts/matrix-fixture-check.sh"   # hermetic e2e-matrix fixture generator + refusals
                                          # (fast tier-1; the nested-vehicle harness
                                          # acceptance is scripts/run-matrix.sh, periodic)

sha="$(git -C "$repo" rev-parse HEAD)"
mkdir -p "$repo/build"
printf '%s\n' "$sha" > "$repo/build/_gates-passed"
echo "GATES: ALL OK @ $sha"
