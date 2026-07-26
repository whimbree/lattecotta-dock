# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Shared default-layout config seeder for the nested e2e harnesses (sourced by
# ci/build-and-gate.sh and scripts/asan-e2e-gate.sh). Single source of truth so
# the container release gate and the NixOS sanitized gate can never drift on how
# a hermetic seed config is produced.
#
# run-e2e.sh needs a pre-existing $base/latte to copy its throwaway from and
# refuses loudly without one, but a fresh dock only writes its default layout on
# first run. So this brings up a throwaway nested compositor, runs the staged
# dock against an EMPTY config home until it self-inits the default layout, then
# tears the compositor down, leaving a seeded config tree the vehicle can copy.
#
# Seed with a NORMAL (non-sanitized) dock: the seed is plain config DATA (the
# "My Layout" default, written synchronously at first run by app/layouts/
# manager.cpp:86 newLayout -> QFile::copy), so it gains nothing from being
# produced under ASan and only pays the sanitizer's startup overhead. The
# sanitized gate seeds with build/ and reserves build-asan for the driven runs.

# _e2e_seed_has_layout <confighome>: true iff a *.layout.latte exists under
# <confighome>/latte. Deliberately NOT `compgen -G`: the nix devShell's bash
# (5.3.9) is built without programmable completion, so `compgen` exits 127
# ("command not found") and, with its output swallowed, every check reads as
# "no layout" - which is exactly what made an already-seeded config look empty
# for the whole seeding debug (the file lands synchronously at first-run start,
# it was the check that lied). This loop is nullglob- and nounset-safe: with no
# match the pattern stays literal and `-e` is false; it needs no bash extras.
_e2e_seed_has_layout() {
    local f
    for f in "$1"/latte/*.layout.latte; do
        [[ -e "$f" ]] && return 0
    done
    return 1
}

# _e2e_seed_stop_dock_process_group <repo> <group> [term attempts/delay]
# [kill attempts/delay]: bounded teardown for the setsid seed dock. KCrash can
# leave the leader stopped, so a leader-only SIGTERM followed by wait can never
# finish. Reuse the package gate's tested live-member and zombie-aware process
# group transaction instead of maintaining a second cleanup implementation.
_e2e_seed_stop_dock_process_group() {
    local repo="$1" process_group="$2"
    local term_attempts="${3:-25}" term_delay="${4:-0.2}"
    local kill_attempts="${5:-25}" kill_delay="${6:-0.2}"

    [[ "$process_group" =~ ^[1-9][0-9]*$ ]] || {
        echo "e2e_seed_default_config: FAIL invalid seed dock process group '$process_group'" >&2
        return 2
    }
    source "$repo/scripts/lib-installed-package-gate.sh" || return 2
    latte_package_gate_stop_process_group \
        "$process_group" "nested seed dock process group" \
        "$term_attempts" "$term_delay" "$kill_attempts" "$kill_delay"
}

# e2e_seed_default_config <repo> <build> <seeddir>: seed a default-layout config
# at <seeddir> by driving the staged dock from <build> once. Returns non-zero
# (loudly) if the dock never self-initializes a layout - a real seeding failure,
# never a silent empty seed.
e2e_seed_default_config() {
    local repo="$1" build="$2" seeddir="$3"
    rm -rf "$seeddir"
    mkdir -p "$seeddir"
    (
        # lib-nested-kwin.sh is nounset-safe but NOT errexit-safe: its cleanup
        # ends with `wait $NESTED_KWIN_PID`, which returns the kwin's SIGTERM
        # status (143) since teardown just killed it. Under an inherited `set -e`
        # that 143 fires from the EXIT trap and takes the whole caller down
        # (caught in-container: the gate died 143 right after a clean seed).
        # Every library caller runs without -e (run-e2e.sh uses `set -uo
        # pipefail`); match that contract here. Explicit checks below still catch
        # a real seeding failure loudly.
        set +e
        source "$repo/scripts/lib-nested-kwin.sh"
        nested_kwin_prepare
        trap 'nested_kwin_cleanup' EXIT
        mkdir -p "$NESTED_RT/kwin-config" "$NESTED_RT/kwin-cache"
        # WAYLAND_DISPLAY is preseeded into the session env BEFORE kwin exists so
        # dbus-activated kactivitymanagerd gets a display in its activation
        # environment; without it the activities consumer never reaches Running
        # and the dock hangs in startup with zero views (the run-e2e trap).
        nested_kwin_env+=(
            WAYLAND_DISPLAY=latte-seed-wl
            XDG_CONFIG_HOME="$NESTED_RT/kwin-config"
            XDG_CACHE_HOME="$NESTED_RT/kwin-cache"
            QT_FORCE_STDERR_LOGGING=1
        )
        nested_kwin_start 1600 1000 latte-seed-wl || exit 2

        export XDG_RUNTIME_DIR="$NESTED_RT"
        export WAYLAND_DISPLAY="$NESTED_SOCK"
        export DBUS_SESSION_BUS_ADDRESS="$NESTED_BUS"
        unset DISPLAY XAUTHORITY

        local seedlog="$build/_seed-dock.log"
        setsid env LATTE_CONFIG_HOME="$seeddir" BUILD="$build" \
            "$repo/scripts/run-staged.sh" -d >"$seedlog" 2>&1 &
        local dockpid=$! i state settled=0
        for ((i = 0; i < 90; i++)); do
            state="$(busctl --user call org.kde.lattedock /Latte org.kde.LatteDock lifecycleState 2>/dev/null | awk '{print $2}' || true)"
            if [[ "$state" == '"running"' ]] && _e2e_seed_has_layout "$seeddir"; then
                settled=1; break
            fi
            kill -0 "$dockpid" 2>/dev/null || break
            sleep 1
        done
        if ! _e2e_seed_stop_dock_process_group "$repo" "$dockpid"; then
            echo "e2e_seed_default_config: FAIL could not stop the nested seed dock process group" >&2
            exit 2
        fi
        if [[ "$settled" != 1 ]]; then
            echo "e2e_seed_default_config: FAIL the dock never self-initialized a default layout while seeding (last state='${state:-none}'); seed dock log tail:" >&2
            tail -30 "$seedlog" >&2 || true
            exit 2
        fi
    )
}
