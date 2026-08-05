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

valid_recipe_expectation() {
    local expectation="$1" status
    case "$expectation" in
        ""|fail) return 0;;
    esac
    if [[ "$expectation" =~ ^status\ ([1-9][0-9]{0,2})$ ]]; then
        status="${BASH_REMATCH[1]}"
        (( status <= 255 ))
        return
    fi
    return 1
}

extract_recipe_expectation() {
    local recipe="$1" declaration value
    local -a declarations=()
    mapfile -t declarations < <(sed -n '/^[[:space:]]*#[[:space:]]*e2e-expect/p' "$recipe")
    recipe_expectation=""
    recipe_expectation_error=""
    if (( ${#declarations[@]} == 0 )); then
        return 0
    fi
    if (( ${#declarations[@]} != 1 )); then
        recipe_expectation_error="found ${#declarations[@]} e2e-expect declarations; use exactly one nonempty marker"
        return 1
    fi
    declaration="${declarations[0]}"
    if [[ "$declaration" != "# e2e-expect:"* ]]; then
        recipe_expectation_error="malformed e2e-expect declaration '$declaration'; expected '# e2e-expect: fail' or '# e2e-expect: status N'"
        return 1
    fi
    value="${declaration#\# e2e-expect:}"
    value="${value#"${value%%[![:space:]]*}"}"
    if [[ -z "$value" ]]; then
        recipe_expectation_error="blank e2e-expect declaration; remove it for unmarked behavior"
        return 1
    fi
    if ! valid_recipe_expectation "$value"; then
        recipe_expectation_error="invalid e2e-expect value '$value'; allowed values are fail or status 1..255"
        return 1
    fi
    recipe_expectation="$value"
}

classify_recipe_result() {
    local expectation="$1" status="$2" name="$3" expected_status
    if [[ -z "$expectation" ]]; then
        if (( status == 0 )); then
            recipe_result=PASS
            recipe_result_message="run-e2e: PASS $name"
        else
            recipe_result=FAIL
            recipe_result_message="run-e2e: FAIL $name"
        fi
    elif [[ "$expectation" == fail ]]; then
        if (( status == 0 )); then
            recipe_result=XPASS
            recipe_result_message="run-e2e: XPASS $name (expected to fail but passed - remove '# e2e-expect: fail', the guarded condition is fixed)"
        else
            recipe_result=XFAIL
            recipe_result_message="run-e2e: XFAIL $name (expected failure of a known-open bug, not counted)"
        fi
    else
        expected_status="${expectation#status }"
        if (( status == 0 )); then
            recipe_result=XPASS
            recipe_result_message="run-e2e: XPASS $name (expected reserved status $expected_status but passed - the guarded condition is fixed)"
        elif (( status == expected_status )); then
            recipe_result=XFAIL
            recipe_result_message="run-e2e: XFAIL $name (matched reserved status $expected_status for the known-open bug)"
        else
            recipe_result=FAIL
            recipe_result_message="run-e2e: FAIL $name (expected reserved status $expected_status, got $status; failure is outside the known signature)"
        fi
    fi
}

capture_recipe_status() {
    if "$@"; then
        recipe_status=0
    else
        recipe_status=$?
    fi
}

record_recipe_result() {
    case "$recipe_result" in
        PASS) passed=$((passed+1));;
        XFAIL) xfailed=$((xfailed+1));;
        FAIL|XPASS) failed=$((failed+1));;
    esac
}

run_expectation_selftest() {
    local failures=0 passed=0 failed=0 xfailed=0 probe
    probe="$(mktemp)" || return 1
    check_result() {
        local label="$1" expectation="$2" status="$3" expected="$4"
        classify_recipe_result "$expectation" "$status" selftest
        if [[ "$recipe_result" == "$expected" ]]; then
            echo "  ok   $label -> $expected"
        else
            echo "  FAIL $label -> $recipe_result, expected $expected" >&2
            failures=$((failures + 1))
        fi
        record_recipe_result
    }
    check_marker() {
        local label="$1" text="$2" expected="$3" error_fragment="$4"
        printf '%s' "$text" > "$probe"
        if extract_recipe_expectation "$probe"; then
            if [[ -n "$error_fragment" || "$recipe_expectation" != "$expected" ]]; then
                echo "  FAIL $label marker accepted as '$recipe_expectation'" >&2
                failures=$((failures + 1))
            fi
        elif [[ -z "$error_fragment" || "$recipe_expectation_error" != *"$error_fragment"* ]]; then
            echo "  FAIL $label marker error: $recipe_expectation_error" >&2
            failures=$((failures + 1))
        fi
    }

    check_result pass "" 0 PASS
    check_result fail "" 1 FAIL
    check_result legacy-xfail fail 1 XFAIL
    check_result legacy-xpass fail 0 XPASS
    check_result exact-xfail "status 42" 42 XFAIL
    check_result exact-xpass "status 42" 0 XPASS
    check_result status-mismatch "status 42" 1 FAIL
    [[ "$passed $failed $xfailed" == "1 4 2" ]] || failures=$((failures + 1))

    check_marker no-marker $'#!/usr/bin/env bash\n' "" ""
    check_marker legacy $'# e2e-expect: fail\n' fail ""
    check_marker exact $'# e2e-expect: status 42\n' "status 42" ""
    check_marker blank $'# e2e-expect:   \n' "" blank
    check_marker duplicate $'# e2e-expect: fail\n# e2e-expect: fail\n' "" '2 e2e-expect declarations'
    check_marker conflict $'# e2e-expect: fail\n# e2e-expect: status 42\n' "" '2 e2e-expect declarations'
    check_marker malformed $'# e2e-expect status 42\n' "" malformed
    check_marker unknown $'# e2e-expect: unknown\n' "" invalid
    check_marker zero $'# e2e-expect: status 0\n' "" invalid
    check_marker range $'# e2e-expect: status 256\n' "" invalid

    capture_recipe_status bash -c 'exit 42'
    [[ "$recipe_status" == 42 ]] || failures=$((failures + 1))
    capture_recipe_status bash -c 'exit 0'
    [[ "$recipe_status" == 0 ]] || failures=$((failures + 1))
    rm -f "$probe"
    (( failures == 0 )) || return 1
    echo "run-e2e: PASS expectation classifier self-test"
}

if [[ "${1:-}" == "--self-test-expectations" ]]; then
    run_expectation_selftest
    exit $?
fi
if ! run_expectation_selftest >/dev/null; then
    echo "run-e2e: FAIL expectation classifier self-test"
    exit 2
fi

MODE=nested
if [[ "${1:-}" == "--live" ]]; then
    MODE=live; shift
fi

# both modes want the devshell (pinned kwin_wayland for the vehicle;
# python3/magick for recipe helpers). uv joined the recipe toolchain when the
# matrix fixture generator moved into the harness package (BP-1b, the
# bash-to-python migration's fixture chunk): matrix-lib and the FP-4C recipe
# invoke `uv run`, and recipes inherit THIS process's PATH - so the guard must
# check uv too. kwin_wayland alone is a stale proxy: a shell entered before uv
# joined the devShell carries a store kwin but no uv (the exact gap gate-all's
# cmake+uv guard already closes), and the vehicle then fails every staging call.
if ! command -v kwin_wayland >/dev/null 2>&1 || ! command -v uv >/dev/null 2>&1 \
   || [[ -z "${LATTE_QML_MODULE_PATH:-}" ]]; then
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
# A known-open bug recipe carries "# e2e-expect: fail" for the legacy any-failure
# contract or "# e2e-expect: status N" when only one reserved status proves the
# known signature. Status 0 remains XPASS until the marker is removed.

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
    if ! extract_recipe_expectation "$recipe"; then
        echo "run-e2e: FAIL $recipe_expectation_error in $name"
        failed=$((failed+1)); continue
    fi
    expect="$recipe_expectation"
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
    capture_recipe_status "$recipe"
    status="$recipe_status"
    classify_recipe_result "$expect" "$status" "$name"
    echo "$recipe_result_message"
    record_recipe_result
    if (( status != 0 )); then
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
