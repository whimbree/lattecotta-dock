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

# The dock's comm depends on the build shape: the dev/staged binary runs as
# latte-dock, but the nixpkgs-packaged one (programs.latte-dock module,
# autostarted at login) is a wrapQtAppsHook shell wrapper that execs
# .latte-dock-wrapped - truncated by the kernel's 15-char comm limit to
# ".latte-dock-wra". A sweep that only matches latte-dock leaves the
# packaged instance alive holding the KDBusService unique name, and the
# staged dock then exits silently at startup (caught live 2026-07-17, the
# first login with the module autostart enabled). The wrapper pattern's
# leading dot is deliberately unescaped: at 15 characters it stays under
# procps' pattern-length warning, and regex-any vs literal dot cannot
# collide with any real comm here.
comm_pats=('latte-dock' '.latte-dock-wra')

dock_pgrep() {
    local found=1 pat
    for pat in "${comm_pats[@]}"; do
        pgrep "$@" -x "$pat" && found=0
    done
    return "$found"
}

dock_alive() { dock_pgrep >/dev/null; }

dock_pkill() {
    local pat
    for pat in "${comm_pats[@]}"; do
        pkill "$@" -x "$pat" || true
    done
}

if dock_alive; then
    dock_pkill -TERM
    dock_pkill -CONT
    for _ in $(seq 1 50); do
        dock_alive || break
        sleep 0.2
    done
fi

if dock_alive; then
    echo "latte-dock survived SIGTERM, sending SIGKILL:" >&2
    dock_pgrep -a >&2
    dock_pkill -KILL
    sleep 1
fi

if dock_alive; then
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
