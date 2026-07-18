#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# C-I4 / P4, the G3 readback (docs/e2e-interaction-test-plan.md): the live
# drop-marker readback viewDropMarkerIndex - the direct insert(-1)
# observability an add/reorder abort leans on.
#
# This recipe proves the readback is WIRED end-to-end and reports the CLEAN
# sentinel (-1) for a real view AT REST - which is exactly the post-abort
# baseline (no drag in flight => the dndSpacer is parked off the layouts =>
# no live marker). A bad containment id is refused (-1 + qWarning).
#
# The LIVE side (a mid-drag marker reads >=0, index 0 being a live leading
# marker) is proven two ways: the value contract is pinned deterministically
# by tests/units/dbusreportstest.cpp (dropMarkerIsLiveSeparatesLiveFromClean),
# and the full mid-drag end-to-end drive lands with the widget-explorer DND
# driver (C-I9/P8) that the A1 abort scenario (C-A1) uses - this readback is
# the surface that scenario asserts on. Driving a real Wayland drag needs a
# drag SOURCE surface, which is exactly what C-I9 builds; there is no coarse
# action that makes the spacer live without it, so it is not re-implemented
# here.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"

# all_view_ids: every current containment id, one per line.
all_view_ids() {
    e2e_json viewsData | python3 -c 'import json,sys
print("\n".join(str(v["containmentId"]) for v in json.load(sys.stdin)))'
}

# drop_marker <cid>: the viewDropMarkerIndex int (busctl prints "i <n>").
drop_marker() {
    e2e_call viewDropMarkerIndex u "$1" | awk '{print $NF}'
}

# ---- clean at rest: every real view reports -1 (no live marker) -------------

any=""
for cid in $(all_view_ids); do
    any=1
    idx="$(drop_marker "$cid")"
    [[ "$idx" == "-1" ]] \
        || e2e_fail "view $cid reports drop-marker index $idx at rest; expected -1 (no live marker / no orphan spacer)"
    echo "view $cid: drop marker clean at rest (index -1)"
done
[[ -n "$any" ]] || e2e_fail "no views found to query the drop marker on"

# ---- a bad containment id is refused (-1 + qWarning) ------------------------

bad_cid=987654
all_view_ids | grep -qx "$bad_cid" && e2e_fail "test bug: $bad_cid is a real view id, pick another"

logmark="$(wc -l < "$E2E_DOCK_LOG")"
idx="$(drop_marker "$bad_cid")"
[[ "$idx" == "-1" ]] \
    || e2e_fail "viewDropMarkerIndex on a bad containment id returned $idx; expected the -1 sentinel"
if ! tail -n +$((logmark + 1)) "$E2E_DOCK_LOG" \
        | grep -q "viewDropMarkerIndex queried for containment $bad_cid which has no view"; then
    echo "---- new dock-log lines ----" >&2
    tail -n +$((logmark + 1)) "$E2E_DOCK_LOG" >&2
    e2e_fail "no viewDropMarkerIndex refusal qWarning for bad containment id $bad_cid in the dock log"
fi
echo "rejection observed: bad containment id $bad_cid returns -1 + qWarning"

echo "PASS: viewDropMarkerIndex reads clean (-1) at rest and refuses a bad id"
