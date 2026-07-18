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

# The asan-e2e-gate rebuilds a full sanitized build-asan tree (~25-35 min),
# far longer than a subagent's per-tool time budget (a 10-min cap), so a swarm
# branch gate that ran it inline could not finish in one call and stalled
# (decided 2026-07-18: split the gate). LATTE_GATE_FAST=1 skips it on a swarm
# BRANCH gate; the orchestrator then runs the full gate (asan included) at
# MERGE time on the rebased branch head, before merging, where a background
# poll across turns is available. Default (unset) runs the full gate, so
# master gates, merge gates, and any non-swarm gate stay complete. The skip is
# announced loudly - a fast stamp is deliberately partial and only valid as a
# branch push, never as the merge verdict.
if [[ "${LATTE_GATE_FAST:-0}" == "1" ]]; then
    echo "gate-all: LATTE_GATE_FAST=1 - asan-e2e-gate SKIPPED (runs at merge time)"
    printf '%s\n' "fast" > "$repo/build/_gates-fast" 2>/dev/null || true
else
    rm -f "$repo/build/_gates-fast" 2>/dev/null || true
    "$repo/scripts/asan-e2e-gate.sh"  # driven sanitized dock: UB in integration paths aborts
fi
"$repo/scripts/matrix-fixture-check.sh"   # hermetic e2e-matrix fixture generator + refusals
                                          # (fast tier-1; the nested-vehicle harness
                                          # acceptance is scripts/run-matrix.sh, periodic)

sha="$(git -C "$repo" rev-parse HEAD)"
mkdir -p "$repo/build"
printf '%s\n' "$sha" > "$repo/build/_gates-passed"
echo "GATES: ALL OK @ $sha"
