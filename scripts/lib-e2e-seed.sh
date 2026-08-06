# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Shared default-layout config seeder for the nested e2e harnesses (sourced by
# ci/build-and-gate.sh, scripts/asan-e2e-gate.sh, scripts/run-matrix.sh and
# scripts/run-multi-output-e2e.sh). Single source of truth so the container
# release gate and the NixOS sanitized gate can never drift on how a hermetic
# seed config is produced.
#
# THIN BRIDGE (BP-2a): the seeding logic (throwaway compositor bring-up via the
# vehicle module, staged dock run until the default layout self-writes, bounded
# teardown) lives in the typed latte_harness.seed module now; the full rationale
# (why a NORMAL dock, the WAYLAND_DISPLAY preseed, the loud-not-empty failure
# contract) is documented there. This file keeps the SAME sourced interface the
# consumers rely on so they need no change.

# e2e_seed_default_config <repo> <build> <seeddir>: seed a default-layout config
# at <seeddir> by driving the staged dock from <build> once. Returns non-zero
# (loudly, with the seed dock's log tail) if the dock never self-initializes a
# layout - never a silent empty seed.
e2e_seed_default_config() {
    local repo="$1" build="$2" seeddir="$3"
    uv run --locked --project "$repo/harness" python -m latte_harness.seed default-config \
        "$repo" "$build" "$seeddir"
}

# _e2e_seed_stop_dock_process_group <repo> <group> [term attempts/delay]
# [kill attempts/delay]: bounded, zombie-aware teardown for the setsid seed
# dock, via the vehicle module's stop_process_group (the typed twin of the
# package gate's transaction the bash reused). KCrash can leave the leader
# STOPPED, so a leader-only SIGTERM+wait can never finish; the group transaction
# escalates to SIGKILL on a bound.
#
# lib-installed-package-gate.sh is still sourced here so
# latte_package_gate_process_group_has_live_members stays defined in the
# caller's shell: the e2e-seed-cleanup selftest verifies teardown with it
# right after calling this. That liveness poll is the lib's ONE remaining
# function (BP-4b shed the rest with the selftest port); it retires when
# that selftest is ported.
_e2e_seed_stop_dock_process_group() {
    local repo="$1" process_group="$2"
    local term_attempts="${3:-25}" term_delay="${4:-0.2}"
    local kill_attempts="${5:-25}" kill_delay="${6:-0.2}"

    [[ "$process_group" =~ ^[1-9][0-9]*$ ]] || {
        echo "e2e_seed_default_config: FAIL invalid seed dock process group '$process_group'" >&2
        return 2
    }
    source "$repo/scripts/lib-installed-package-gate.sh" || return 2
    uv run --locked --project "$repo/harness" python -m latte_harness.vehicle stop-group \
        "$process_group" \
        --term-attempts "$term_attempts" --term-delay "$term_delay" \
        --kill-attempts "$kill_attempts" --kill-delay "$kill_delay" \
        --label "nested seed dock process group"
}
