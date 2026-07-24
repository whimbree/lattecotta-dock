#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Observe real-hardware presentation transitions on the live desktop.
#
# The watcher joins each view's painted background/output geometry with its
# live applet geometry, records every distinct composition, and stops at the
# first escaped applet. A failure preserves both D-Bus payloads and a desktop
# screenshot so the first bad transition can be inspected as state and pixels.
#
# Usage:
#   scripts/tools/watch-dock-presentation.sh [seconds] [sample-interval] [dock-id]
#
# Exit 0: at least one geometry transition was observed and every state fit.
# Exit 1: a composition invariant or live query failed.
# Exit 2: no geometry transition was exercised; refusing a vacuous pass.
set -uo pipefail

repo="$(cd "$(dirname "$0")/../.." && pwd)"
if ! command -v python3 >/dev/null 2>&1; then
    exec nix develop "$repo" -c "$0" "$@"
fi
source "$repo/tests/e2e/lib.sh"

duration_seconds="${1:-30}"
sample_interval="${2:-0.05}"
target_view_id="${3:-}"
case "$duration_seconds" in
    *[!0-9]*|"")
        echo "duration must be a positive integer, got '$duration_seconds'" >&2
        exit 1
        ;;
esac
(( duration_seconds > 0 )) || { echo "duration must be greater than zero" >&2; exit 1; }
if [[ ! "$sample_interval" =~ ^(0|[0-9]+)(\.[0-9]+)?$ ]] \
        || [[ "$sample_interval" =~ ^0(\.0+)?$ ]]; then
    echo "sample interval must be a positive number, got '$sample_interval'" >&2
    exit 1
fi
case "$target_view_id" in
    *[!0-9]*)
        echo "dock id must be an unsigned integer, got '$target_view_id'" >&2
        exit 1
        ;;
esac
if [[ -n "$target_view_id" ]] && (( target_view_id == 0 )); then
    echo "dock id must be greater than zero" >&2
    exit 1
fi

artifact_root="$repo/build/_live-observations"
run_stamp="$(date +%Y%m%d-%H%M%S)"
artifacts="$artifact_root/presentation-$run_stamp"
mkdir -p "$artifacts"
trace="$artifacts/trace.log"

if ! busctl --user call org.kde.lattedock /Latte org.kde.LatteDock lifecycleState \
        2>/dev/null | grep -q '"running"'; then
    echo "the live dock is not running" >&2
    exit 1
fi

declare -A previous_state=()
deadline=$((SECONDS + duration_seconds))
samples=0
transitions=0

preserve_failure() {
    local view_id="$1" views="$2" applets="$3" diagnostic="$4"
    printf '%s\n' "$views" >"$artifacts/dockSystemData.json"
    printf '%s\n' "$applets" >"$artifacts/view-${view_id}-applets.json"
    printf '%s\n' "$diagnostic" >"$artifacts/failure.txt"
    spectacle --background --nonotify --fullscreen \
        --output "$artifacts/workspace.png" >/dev/null 2>&1 \
        || printf '%s\n' "screenshot capture failed" >"$artifacts/screenshot-error.txt"
    echo "presentation watcher: FAIL view $view_id: $diagnostic" >&2
    echo "presentation watcher: artifacts: $artifacts" >&2
}

while (( SECONDS < deadline )); do
    views="$(e2e_json dockSystemData)" || {
        echo "dockSystemData query failed" >&2
        exit 1
    }

    mapfile -t visible_views < <(
        VIEWS_JSON="$views" TARGET_VIEW_ID="$target_view_id" python3 -c '
import json
import os

target = os.environ["TARGET_VIEW_ID"]
for view in json.loads(os.environ["VIEWS_JSON"])["views"]:
    if not view["isHidden"] and (not target or str(view["persistentDockId"]) == target):
        print(view["persistentDockId"])
'
    )

    for view_id in "${visible_views[@]}"; do
        applets="$(e2e_json viewAppletsData u "$view_id")" || {
            preserve_failure "$view_id" "$views" "[]" "viewAppletsData query failed"
            exit 1
        }

        if ! state="$(_e2e_assert_presentation_payloads "$views" "$applets" "$view_id" 2 2>&1)"; then
            preserve_failure "$view_id" "$views" "$applets" "$state"
            exit 1
        fi

        if [[ -n "${previous_state[$view_id]+set}" \
                && "${previous_state[$view_id]}" != "$state" ]]; then
            transitions=$((transitions + 1))
        fi
        if [[ "${previous_state[$view_id]:-}" != "$state" ]]; then
            printf '%(%FT%T%z)T %s\n' -1 "$state" | tee -a "$trace"
            previous_state[$view_id]="$state"
        fi
    done

    samples=$((samples + 1))
    sleep "$sample_interval"
done

if (( transitions == 0 )); then
    echo "presentation watcher: no geometry transition observed in ${duration_seconds}s" \
        | tee -a "$trace" >&2
    echo "presentation watcher: exercise hover zoom or another geometry change; artifacts: $artifacts" >&2
    exit 2
fi

echo "presentation watcher: PASS $samples samples, $transitions transitions; trace: $trace"
