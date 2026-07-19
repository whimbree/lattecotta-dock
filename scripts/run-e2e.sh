#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# End-to-end recipe driver (docs/reference/TESTING.md, the e2e tier). Runs the
# tests/e2e/*.sh recipes against the staged dock and reports pass/fail per
# recipe. Two modes:
#
#   scripts/run-e2e.sh [recipes...]         # nested (default): desk-independent
#   scripts/run-e2e.sh --live [recipes...]  # against the real Wayland session
#
# NESTED is the maintained mode (the P4 vehicle, proven 2026-07-16 and
# promoted 2026-07-17): a throwaway kwin_wayland --virtual (1600x1000) in a
# private XDG_RUNTIME_DIR, ONE private dbus-run-session shared by kwin, the
# dock and every probe (separate buses make KWin-scripting mutations
# silently no-op), a throwaway copy of the staged config, fakepointer
# injection without the desktop-file allowlist
# (KWIN_WAYLAND_NO_PERMISSION_CHECKS) and ScreenShot2 without the caller
# allowlist (KWIN_SCREENSHOT_NO_PERMISSION_CHECKS). Nothing touches the
# desk session. Recipes run with the vehicle as their AMBIENT environment,
# so plain busctl/fakepointer inside them hit the vehicle.
#
# --live keeps the original behavior for recipes that genuinely need the
# real session (real kglobalaccel registration, real multi-screen, audio):
# it restarts the desk dock into the throwaway config and restores a
# displaced --user-config dock afterwards.
#
# Recipes declare mode constraints with a marker line; the driver skips
# non-matching recipes (they count as skipped, not failed):
#   # e2e-mode: nested-only     (mutates compositor state: desktops, config)
#   # e2e-mode: live-only       (needs the real session)
#
# Vehicle notes, each one a trap that cost real time:
# - WAYLAND_DISPLAY is preseeded into the session env BEFORE kwin exists so
#   dbus-activated services get it in their activation environment.
#   Without it kactivitymanagerd aborts on activation (no display), the
#   activities consumer never reaches Running, and Corona::load() waits
#   forever - the dock sits in lifecycleState "startup" with zero views.
# - QT_FORCE_STDERR_LOGGING=1: NixOS Qt logs straight to journald when
#   stderr is not a tty; without the override the vehicle kwin's log file
#   stays empty and every KWin-script readback (e2e_kwin_js) comes back
#   blank while the output lands in the DESK journal.
# - The vehicle kwin gets a private XDG_CONFIG_HOME and XDG_CACHE_HOME so
#   recipe mutations (virtual desktops, kwin options) can never write into
#   the real kwinrc/ksycoca.
set -uo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"

MODE=nested
if [[ "${1:-}" == "--live" ]]; then
    MODE=live; shift
fi

# both modes want the devshell (pinned kwin_wayland for the vehicle;
# python3/magick for recipe helpers)
if ! command -v kwin_wayland >/dev/null 2>&1 || [[ -z "${LATTE_QML_MODULE_PATH:-}" ]]; then
    exec nix develop "$repo" -c "$0" $([[ "$MODE" == live ]] && echo --live) "$@"
fi

export E2E_REPO="$repo"
export E2E_BUILD="${BUILD:-$repo/build}"
export E2E_MODE="$MODE"
export E2E_FAKEPOINTER="${E2E_FAKEPOINTER:-$HOME/.local/bin/fakepointer}"
export E2E_ARTIFACTS="${E2E_ARTIFACTS:-$E2E_BUILD/_e2e-artifacts}"
mkdir -p "$E2E_ARTIFACTS"

if [[ ! -x "$E2E_FAKEPOINTER" ]]; then
    echo "run-e2e: FAIL fakepointer not found at $E2E_FAKEPOINTER (build recipe: scripts/tools/fakepointer.c header)"; exit 2
fi
if [[ ! -x "$E2E_BUILD/bin/latte-dock" ]]; then
    echo "run-e2e: FAIL no built binary at $E2E_BUILD/bin/latte-dock (build first)"; exit 2
fi

source "$repo/tests/e2e/lib.sh"

# ---- recipe selection ------------------------------------------------------

mapfile -t recipes < <(
    if [[ $# -gt 0 ]]; then
        for r in "$@"; do echo "$repo/tests/e2e/${r%.sh}.sh"; done
    else
        find "$repo/tests/e2e" -name '*.sh' ! -name 'lib.sh' | sort
    fi
)

recipe_mode() { sed -n 's/^# e2e-mode: *//p' "$1" | head -1; }
# A recipe that reproduces a known-open bug carries "# e2e-expect: fail": the
# driver then treats its failure as EXPECTED (XFAIL, not counted against the
# suite) and its unexpected PASS as a hard failure (XPASS - the guarded bug is
# fixed, so the marker must come off and the recipe becomes a real guard).
recipe_expect() { sed -n 's/^# e2e-expect: *//p' "$1" | head -1; }

# ---- nested vehicle --------------------------------------------------------

dock_launched=0

start_vehicle() {
    source "$repo/scripts/lib-nested-kwin.sh"
    nested_kwin_prepare
    trap 'nested_kwin_cleanup' EXIT
    trap 'nested_kwin_cleanup; exit 130' INT
    trap 'nested_kwin_cleanup; exit 143' TERM

    mkdir -p "$NESTED_RT/kwin-config" "$NESTED_RT/kwin-cache"
    nested_kwin_env+=(
        WAYLAND_DISPLAY=latte-e2e-wl
        XDG_CONFIG_HOME="$NESTED_RT/kwin-config"
        XDG_CACHE_HOME="$NESTED_RT/kwin-cache"
        KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1
        QT_FORCE_STDERR_LOGGING=1
    )
    #! E2E_OUTPUT_COUNT defaults to 1: the vehicle is single-output unless a
    #! multi-output front door (scripts/run-multi-output-e2e.sh) asks for more,
    #! so every existing recipe keeps its one 1600x1000 output unchanged. The
    #! dual-output vehicle (C-I2/P1) proves per-screen view placement.
    export E2E_OUTPUT_COUNT="${E2E_OUTPUT_COUNT:-1}"
    nested_kwin_start 1600 1000 latte-e2e-wl "$E2E_OUTPUT_COUNT" || exit 2

    export E2E_RT="$NESTED_RT"
    export E2E_KWIN_LOG="$NESTED_KWIN_LOG"
    export E2E_DOCK_LOG="$NESTED_RT/dock.log"
    export E2E_DOCK_PIDFILE="$NESTED_RT/dock.pid"
    export E2E_SCREEN_W=1600 E2E_SCREEN_H=1000

    #! the driver itself moves into the vehicle's ambient environment; every
    #! child (recipes, busctl, fakepointer, the dock) inherits it
    export XDG_RUNTIME_DIR="$NESTED_RT"
    export WAYLAND_DISPLAY="$NESTED_SOCK"
    export DBUS_SESSION_BUS_ADDRESS="$NESTED_BUS"
    unset DISPLAY XAUTHORITY

    #! throwaway config: a fresh copy per driver run, never the base itself
    local base="${E2E_CONFIG_BASE:-$E2E_BUILD/_runconfig}"
    if [[ ! -d "$base/latte" ]]; then
        echo "run-e2e: FAIL no staged config at $base (start the staged dock once, or set E2E_CONFIG_BASE)"; exit 2
    fi
    export E2E_CONFIG_HOME="$NESTED_RT/latte-config"
    cp -r "$base" "$E2E_CONFIG_HOME"
    rm -f "$E2E_CONFIG_HOME"/latte/*.bak

    local layouts=("$E2E_CONFIG_HOME"/latte/*.layout.latte)
    export E2E_LAYOUT="${layouts[0]}"
    if [[ "${#layouts[@]}" -gt 1 ]]; then
        local name
        name="$(sed -n 's/^singleModeLayoutName=//p' "$E2E_CONFIG_HOME/lattedockrc" | head -1)"
        [[ -n "$name" && -f "$E2E_CONFIG_HOME/latte/$name.layout.latte" ]] && export E2E_LAYOUT="$E2E_CONFIG_HOME/latte/$name.layout.latte"
    fi

    echo "run-e2e: vehicle up (rt $NESTED_RT), starting the staged dock..."
    if ! e2e_dock_start 90; then
        echo "run-e2e: FAIL the dock never settled in the vehicle; dock log tail:"
        tail -30 "$E2E_DOCK_LOG"
        exit 2
    fi
    dock_launched=1
}

# ---- live session ----------------------------------------------------------

restore_user_config=0

start_live() {
    if [[ -z "${WAYLAND_DISPLAY:-}" ]]; then
        echo "run-e2e: FAIL no wayland session (WAYLAND_DISPLAY unset)"; exit 2
    fi
    export E2E_CONFIG_HOME="$E2E_BUILD/_runconfig"
    local layouts=("$E2E_CONFIG_HOME"/latte/*.layout.latte)
    export E2E_LAYOUT="${layouts[0]:-}"

    #! remember whether a user-config dock was running, to restore it
    if pgrep -x latte-dock >/dev/null; then
        if tr '\0' '\n' < "/proc/$(pgrep -x latte-dock | head -1)/environ" | grep -q "XDG_CONFIG_HOME=$HOME/.config"; then
            restore_user_config=1
        fi
    fi

    echo "run-e2e: starting the throwaway dock on the live session..."
    "$repo/scripts/restart-staged.sh" -d > /tmp/latte-e2e.log 2>&1 &
    sleep 16
    if ! pgrep -x latte-dock >/dev/null; then
        echo "run-e2e: FAIL the throwaway dock did not come up (see /tmp/latte-e2e.log)"; exit 2
    fi
}

finish_live() {
    if [[ "$restore_user_config" == 1 ]]; then
        echo "run-e2e: restoring the user-config dock..."
        "$repo/scripts/restart-staged.sh" --user-config -d > /tmp/latte-e2e-restore.log 2>&1 &
        sleep 8
    fi
}

# ---- run --------------------------------------------------------------------

if [[ "$MODE" == nested ]]; then
    start_vehicle
else
    start_live
fi

failed=0 skipped=0 ran=0 passed=0 xfailed=0
for recipe in "${recipes[@]}"; do
    name="$(basename "$recipe")"
    if [[ ! -x "$recipe" ]]; then
        echo "run-e2e: FAIL missing or non-executable recipe: $name"; failed=$((failed+1)); continue
    fi
    constraint="$(recipe_mode "$recipe")"
    #! a typo here must fail loud, not silently run the recipe in both
    #! modes - the empty string (no marker) is the only mode-agnostic value
    case "$constraint" in
        ""|nested-only|live-only) ;;
        *) echo "run-e2e: FAIL unknown e2e-mode marker '$constraint' in $name (allowed: nested-only, live-only, or none)"; failed=$((failed+1)); continue;;
    esac
    expect="$(recipe_expect "$recipe")"
    case "$expect" in
        ""|fail) ;;
        *) echo "run-e2e: FAIL unknown e2e-expect marker '$expect' in $name (allowed: fail, or none)"; failed=$((failed+1)); continue;;
    esac
    if [[ ( "$constraint" == "nested-only" && "$MODE" != nested ) \
       || ( "$constraint" == "live-only"   && "$MODE" != live   ) ]]; then
        echo "run-e2e: SKIP $name ($constraint)"; skipped=$((skipped+1)); continue
    fi

    #! recipes may leave the dock stopped (config-restore contract); give
    #! every recipe a running, settled dock
    if [[ "$MODE" == nested ]]; then
        pid="$(e2e_dock_pid)"
        if [[ -z "$pid" ]] || ! kill -0 "$pid" 2>/dev/null; then
            echo "run-e2e: (re)starting the vehicle dock for $name..."
            if ! e2e_dock_start 90; then
                echo "run-e2e: FAIL could not restart the vehicle dock; dock log tail:"
                tail -30 "$E2E_DOCK_LOG"; failed=$((failed+1)); continue
            fi
        fi
    fi

    echo "run-e2e: ---- $name"
    ran=$((ran+1))
    if "$recipe"; then
        if [[ "$expect" == fail ]]; then
            #! the guarded bug is FIXED - this must go red until the marker is
            #! removed and the recipe is promoted to a real guard
            echo "run-e2e: XPASS $name (expected to fail but passed - remove '# e2e-expect: fail', the guarded condition is fixed)"
            failed=$((failed+1))
        else
            echo "run-e2e: PASS $name"
            passed=$((passed+1))
        fi
    else
        if [[ "$expect" == fail ]]; then
            echo "run-e2e: XFAIL $name (expected failure of a known-open bug, not counted)"
            xfailed=$((xfailed+1))
        else
            echo "run-e2e: FAIL $name"
            failed=$((failed+1))
        fi
        if [[ "$MODE" == nested ]]; then
            cp "$E2E_DOCK_LOG" "$E2E_ARTIFACTS/$name.dock.log" 2>/dev/null || true
            cp "$E2E_KWIN_LOG" "$E2E_ARTIFACTS/$name.kwin.log" 2>/dev/null || true
        fi
    fi
done

if [[ "$MODE" == nested ]]; then
    #! final teardown doubles as a shutdown check when a dock is still up
    if [[ "$dock_launched" == 1 ]]; then
        pid="$(e2e_dock_pid)"
        if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
            if ! e2e_dock_stop; then
                echo "run-e2e: FAIL the vehicle dock did not exit cleanly on SIGTERM at teardown"
                failed=$((failed+1))
            fi
        fi
    fi
else
    finish_live
fi

echo "run-e2e: $passed/$ran recipes passed (${skipped} skipped for mode, ${xfailed} xfail)"
[[ "$failed" == 0 ]]
