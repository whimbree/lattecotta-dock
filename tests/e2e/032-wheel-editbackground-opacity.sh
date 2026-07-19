#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# AU-6a control 2 (docs/edit-mode-settings-audit-plan.md, cluster CL-6): the LIVE
# driven proof of the edit-background-wheel fork-rewire trap. In edit mode the
# wheel over the canvas grid adjusts editBackgroundOpacity - the edit-mode grid
# overlay's opacity, a Double in [0,1]. BOTH reference forks rewired this handler
# to write panelTransparency instead (latte-dock-qt6 CanvasConfiguration.qml:153,
# latte-dock-ng :98) - an Int in [0,100] that is the dock's REAL runtime
# background opacity, a persistent user setting. Scrolling the edit overlay must
# never silently move the running dock's transparency. This port keeps the
# Qt5-faithful editBackgroundOpacity (CanvasConfiguration.qml:154-160).
#
# The decisive check is P2 (right key, no stray write): snapshot the whole
# containment config through viewConfigData (the in-process map, so the KConfig
# default-deletion trap cannot corrupt the diff), scroll the wheel, snapshot
# again, and assert the EXACT changed-key set is {editBackgroundOpacity} - a
# panelTransparency write would appear here as a stray key and FAIL. The step is
# then confirmed to be one 0.1 detent in the driven direction.
#
# The wheel MouseArea (editBackMouseArea) fills the canvas grid; it sits BELOW
# the max-length ruler (the canvas' outermost rows) and away from the left-end
# rearrange toggle, so the wheel is delivered at the canvas' vertical middle,
# screen-centre horizontally. Wheel delivery over the canvas is flaky (the
# 030-wheel-ruler-maxlength calibration); same single-invocation scroll + park +
# poll + retry rhythm here.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"
source "$E2E_REPO/tests/e2e/matrix/matrix-lib.sh"
source "$E2E_REPO/tests/e2e/audit/audit-lib.sh"

view="$(e2e_tasks_view)" || e2e_fail "no tasks view"

# read editBackgroundOpacity from the in-process config map (never the on-disk
# file, which the KConfig default-deletion trap would corrupt)
cfg_op() { audit_config_snapshot "$view" | awk -F'\t' '$1=="editBackgroundOpacity"{print $2}'; }
cfg_pt() { audit_config_snapshot "$view" | awk -F'\t' '$1=="panelTransparency"{print $2}'; }

orig="$(cfg_op)"
restore() {
    e2e_dock_stop >/dev/null 2>&1 || true
    kwriteconfig6 --file "$E2E_LAYOUT" --group Containments --group "$view" --group General \
        --key editBackgroundOpacity "${orig:-0.2}"
}
trap restore EXIT

audit_enter_editmode "$view" || e2e_fail "edit mode never turned on"

#! the canvas: the latte edit-mode window just mapped, screen-wide and thin
screen="$(e2e_view_field "$view" '"%d %d %d %d" % tuple(v["screenGeometry"])')"
read -r scx scy scw sch <<< "$screen"
canvas="$(e2e_dumpwins | awk -F'|' -v scw="$scw" '
    { split($4,g," "); split(g[1],p,","); split(g[2],s,"x");
      if (s[1]==scw && s[2]<300){printf "%d %d %d %d\n",p[1],p[2],s[1],s[2];exit} }')"
[[ -n "$canvas" ]] || e2e_fail "no canvas window mapped for edit mode"
read -r cx cy cw ch <<< "$canvas"

#! screen-centre horizontally (clear of the left-end rearrange toggle), canvas
#! vertical middle (below the ~13px ruler at the canvas top)
wx=$(( scx + scw / 2 ))
wy=$(( cy + ch / 2 ))

start_op="$(cfg_op)"
[[ -n "$start_op" ]] || e2e_fail "could not read editBackgroundOpacity"
start_pt="$(cfg_pt)"
echo "start: editBackgroundOpacity=$start_op panelTransparency=$start_pt"

#! pick a direction with headroom: below 0.85 scroll up (+0.1), else down (-0.1)
detent="$(python3 -c "print(1 if float('$start_op') < 0.85 else -1)")"

before="$(mktemp)"; after="$(mktemp)"
trap 'rm -f "$before" "$after"; restore' EXIT
audit_config_snapshot "$view" > "$before"

#! deliver one detent on the edit-background grid and wait for the opacity to
#! land (single-invocation scroll, then park off so no tooltip dwell)
landed=""
for attempt in 1 2 3 4 5 6 7 8; do
    "$E2E_FAKEPOINTER" scroll "$wx" "$wy" "$detent" 100
    "$E2E_FAKEPOINTER" move "$wx" "$scy"
    for i in $(seq 1 6); do
        cur="$(cfg_op)"
        if [[ "$cur" != "$start_op" ]]; then
            landed="$cur"; break
        fi
        sleep 0.5
    done
    [[ -n "$landed" ]] && break
    echo "  (edit-background detent not delivered on attempt $attempt, retrying)"
done
[[ -n "$landed" ]] || e2e_fail "the edit-background wheel never moved editBackgroundOpacity after 8 attempts"

audit_config_snapshot "$view" > "$after"

#! THE decisive check (P2): the wheel changed editBackgroundOpacity and ONLY
#! editBackgroundOpacity. A panelTransparency write (the fork-rewire trap) would
#! show here as a second changed key and fail this assertion.
audit_assert_applies   "$before" "$after" editBackgroundOpacity || e2e_fail "P1: the wheel did not move editBackgroundOpacity"
audit_assert_only_keys "$before" "$after" editBackgroundOpacity \
    || e2e_fail "P2 fork-rewire trap: the edit-background wheel changed a key other than editBackgroundOpacity (panelTransparency rewire?)"

#! spell the trap out loud: panelTransparency is untouched
end_pt="$(cfg_pt)"
[[ "$end_pt" == "$start_pt" ]] || e2e_fail "panelTransparency moved ($start_pt -> $end_pt): the wheel rewired to the dock background opacity"

#! and the move is exactly one 0.1 detent in the driven direction, clamped to [0,1]
python3 - "$start_op" "$landed" "$detent" <<'PY' || e2e_fail "editBackgroundOpacity did not move by one 0.1 detent"
import sys
start, landed, detent = float(sys.argv[1]), float(sys.argv[2]), int(sys.argv[3])
expected = min(1.0, start + 0.1) if detent > 0 else max(0.0, start - 0.1)
if abs(landed - expected) > 0.001:
    sys.exit("editBackgroundOpacity %s -> %s, expected %s" % (start, landed, expected))
PY

echo "edit-background wheel moved editBackgroundOpacity $start_op -> $landed (only key changed; panelTransparency held at $start_pt)"

audit_exit_editmode "$view"
echo "AU-6a control 2: the edit-background wheel writes editBackgroundOpacity, NOT panelTransparency (Qt5-faithful, fork-rewire trap avoided)"
