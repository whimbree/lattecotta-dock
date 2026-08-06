#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# BP-4 (the bash-to-python migration's package-gate chunks) ported this
# library to the typed harness: the audit half lives in
# harness/src/latte_harness/package_gate_audit.py (BP-4a, the gate
# engine), the process-group transaction in latte_harness.vehicle
# (BP-2a), and the selftest that fault-injected these functions by
# sourcing them is harness/tests/test_package_gate_selftest.py (BP-4b,
# the gate selftest). One function remains, for the one bash consumer
# left: tests/e2e-seed-cleanup-selftest.sh verifies the seed dock's
# teardown with it (sourced via scripts/lib-e2e-seed.sh). It retires
# with that selftest's port. Keep its diagnostics in lockstep with
# vehicle.group_live_status, its typed twin: the message text is a
# shared refusal taxonomy, matched verbatim on both sides.

latte_package_gate_process_group_has_live_members() {
    local process_group="$1" pgrep_output pgrep_status pid stat_line stat_tail state

    if pgrep_output="$(pgrep -g "$process_group" 2>&1)"; then
        [[ -n "$pgrep_output" ]] || {
            echo "installed-package-gate: FAIL: pgrep returned success without members for process group $process_group" >&2
            return 2
        }
    else
        pgrep_status=$?
        [[ "$pgrep_status" -eq 1 ]] && return 1
        echo "installed-package-gate: FAIL: pgrep failed while polling process group $process_group with status $pgrep_status${pgrep_output:+: $pgrep_output}" >&2
        return 2
    fi

    while IFS= read -r pid; do
        [[ "$pid" =~ ^[0-9]+$ ]] || {
            echo "installed-package-gate: FAIL: pgrep returned an invalid pid while polling process group $process_group: $pid" >&2
            return 2
        }
        if ! IFS= read -r stat_line <"/proc/$pid/stat"; then
            # A member can disappear between pgrep and the procfs read.
            [[ ! -d "/proc/$pid" ]] && continue
            echo "installed-package-gate: FAIL: cannot read state for process-group member $pid" >&2
            return 2
        fi
        stat_tail="${stat_line##*) }"
        state="${stat_tail%% *}"
        case "$state" in
            Z|X) ;;
            [A-Z]) return 0 ;;
            *)
                echo "installed-package-gate: FAIL: cannot parse state for process-group member $pid" >&2
                return 2
                ;;
        esac
    done <<<"$pgrep_output"
    return 1
}
