#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

repo="${REPO_ROOT:-$(cd "$(dirname "$0")/.." && pwd -P)}"
source "$repo/scripts/lib-e2e-seed.sh"

for required_command in bash kill pgrep setsid sleep; do
    command -v "$required_command" >/dev/null 2>&1 || {
        echo "e2e-seed-cleanup-selftest: FAIL missing $required_command" >&2
        exit 1
    }
done
unset required_command

group_pid=""
cleanup() {
    if [[ "$group_pid" =~ ^[1-9][0-9]*$ ]]; then
        kill -KILL -- "-$group_pid" 2>/dev/null \
            || kill -KILL "$group_pid" 2>/dev/null \
            || true
        wait "$group_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT

setsid bash -c '
    trap "" TERM
    kill -STOP "$$"
    while :; do
        sleep 60
    done
' &
group_pid=$!

stopped=false
for ((attempt = 0; attempt < 100; ++attempt)); do
    if [[ -r "/proc/$group_pid/stat" ]]; then
        stat_tail="$(<"/proc/$group_pid/stat")"
        stat_tail="${stat_tail##*) }"
        [[ "${stat_tail%% *}" == T ]] && {
            stopped=true
            break
        }
    fi
    sleep 0.01
done
[[ "$stopped" == true ]] || {
    echo "e2e-seed-cleanup-selftest: FAIL fixture did not enter stopped state" >&2
    exit 1
}

_e2e_seed_stop_dock_process_group \
    "$repo" "$group_pid" 1 0.01 50 0.01
if latte_package_gate_process_group_has_live_members "$group_pid"; then
    echo "e2e-seed-cleanup-selftest: FAIL stopped seed group survived cleanup" >&2
    exit 1
else
    poll_status=$?
    [[ "$poll_status" -eq 1 ]] || exit "$poll_status"
fi

group_pid=""
echo "e2e-seed-cleanup-selftest: PASS stopped seed group reached bounded SIGKILL cleanup"
