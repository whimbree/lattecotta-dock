#!/usr/bin/env bash
# THE DESK LIFECYCLE ENTRY POINT: safely kill any existing instance, then
# launch the staged dock detached from this terminal. Environment
# construction itself lives in run-staged.sh (the foreground env core this
# script delegates to) - kept as a separate file so harnesses can use the
# env core against nested compositors without this script's kill sequence
# ever reaching a live dock. Daily-driver shorthand: start-dock.sh.
#
# Instances whose launching terminal died can end up SIGSTOPped (state T).
# A stopped process never runs its SIGTERM handler, so a plain pkill leaves
# it alive forever and the next launch silently stacks a second instance -
# three docks fighting over layer surfaces was observed on 2026-07-10.
# TERM first, then CONT so the pending TERM is delivered, escalate to KILL,
# and refuse to start while any instance survives.
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"

if pgrep -x latte-dock >/dev/null; then
    pkill -TERM -x latte-dock || true
    pkill -CONT -x latte-dock || true
    for _ in $(seq 1 50); do
        pgrep -x latte-dock >/dev/null || break
        sleep 0.2
    done
fi

if pgrep -x latte-dock >/dev/null; then
    echo "latte-dock survived SIGTERM, sending SIGKILL:" >&2
    pgrep -ax latte-dock >&2
    pkill -KILL -x latte-dock
    sleep 1
fi

if pgrep -x latte-dock >/dev/null; then
    echo "latte-dock still alive after SIGKILL, refusing to stack another instance" >&2
    exit 1
fi

# nix develop re-evaluates the flake on every restart (~3.0s measured
# 2026-07-13, the largest single chunk of the ~4.1s pre-exec launcher
# overhead). print-dev-env emits the same environment as a sourceable
# script, so snapshot it once and refresh only when the flake inputs
# change. Delete build/_devshell.env to force a refresh by hand.
envcache="$repo/build/_devshell.env"
if [[ ! -s "$envcache" || "$repo/flake.lock" -nt "$envcache" || "$repo/flake.nix" -nt "$envcache" ]]; then
    echo "refreshing dev-shell env snapshot (nix print-dev-env)..."
    mkdir -p "$repo/build"
    nix print-dev-env "$repo" > "$envcache.tmp"
    mv "$envcache.tmp" "$envcache"
fi

# setsid + closed stdin: no controlling terminal, so the dock can never be
# stopped or hung up by its launching terminal going away.
exec setsid bash -c 'source "$1"; shift; exec "$@"' _ "$envcache" "$repo/scripts/run-staged.sh" "$@" </dev/null
