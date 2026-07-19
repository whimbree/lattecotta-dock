#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# The applet-REORDER driver's acceptance test (P6 / C-I7,
# docs/tracking/e2e-interaction-test-plan.md). HC3: a driver whose acceptance only shows
# a green happy path is untrustworthy - it cannot be relied on to assert "the
# abort left no residue" if it would report success regardless. So this proves,
# on BOTH the horizontal (bottom/top, x-drag) AND the VERTICAL (left/right,
# y-drag - the historically-buggy path) axes, that the driver:
#   1. CAN commit a real cross-neighbour reorder (order changes) - so the
#      refusals below are genuine, not a driver that never reorders;
#   2. observes a REFUSED reorder AS a refusal - a drag that never crosses a
#      neighbour is reported as "no reorder" (the driver's own rc), NOT success;
#   3. observes an ABORTED reorder (release-at-origin: motion, a neighbour
#      crossed, then released back) as leaving the order UNCHANGED;
#   4. and drives the DR-6 escape-in-held-drag path, OBSERVING Escape's REAL
#      effect (the ConfigOverlay MouseArea has no Keys handler, so Escape is NOT
#      a given cancel - see the plan's ESCAPE-vs-RELEASE finding), never
#      assuming it cancels.
# The G2 stacking readback carries the "stuck over chrome" residue check: every
# applet sits at the layout default z (0) at rest, and no drag may leave an
# applet stranded at the lift z (>= 900) - the 480ae30e3 class made queryable.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"
source "${E2E_REPO}/tests/e2e/matrix/applet-reorder-driver.sh"

APPLET_LIFT_Z=900   # ConfigOverlay parks a dragged applet's delegate here

# discover_axis_view <orientation>: the widest/tallest non-hidden, non-cloned
# view on that orientation carrying at least two applets. orientation is
# "horizontal" (bottom/top) or "vertical" (left/right).
discover_axis_view() {
    local orient="$1" id cnt
    #! the python emits candidate ids one per line; the for-loop runs in THIS
    #! shell (not a pipe subshell) so return actually leaves the function
    for id in $({ echo "$orient"; e2e_json viewsData; } | python3 -c '
import json, sys
orient = sys.stdin.readline().strip()
edges = ("bottom", "top") if orient == "horizontal" else ("left", "right")
views = [v for v in json.load(sys.stdin)
         if v["edge"] in edges and not v["isHidden"] and not v.get("isCloned")]
# widest (horizontal) / tallest (vertical) first, deterministic
axis = 2 if orient == "horizontal" else 3
views.sort(key=lambda v: -v["absoluteGeometry"][axis])
for v in views:
    print(v["containmentId"])
'); do
        cnt="$(e2e_json viewAppletsData u "$id" | python3 -c 'import json,sys; print(len(json.load(sys.stdin)))')"
        if (( cnt >= 2 )); then echo "$id"; return 0; fi
    done
    return 1
}

# simple_adjacent_pair <view>: the first visual index i such that applet[i] and
# applet[i+1] are BOTH ordinary single-slot widgets (not the wide tasks plasmoid
# or a systemtray container), echoed as "i j". Keeps the drag geometry simple
# and the reorder unambiguous. Refuses loudly if the view carries no such pair.
simple_adjacent_pair() {
    local view="$1"
    e2e_json viewAppletsData u "$view" | python3 -c '
import json, sys
wide = {"org.kde.latte.plasmoid", "org.kde.plasma.icontasks",
        "org.kde.plasma.systemtray", "org.nomad.systemtray"}
applets = json.load(sys.stdin)
for i in range(len(applets) - 1):
    if applets[i]["plugin"] not in wide and applets[i+1]["plugin"] not in wide:
        print(i, i + 1); sys.exit(0)
sys.exit("no adjacent pair of ordinary applets to reorder")
'
}

# assert_no_lifted_applet <view> <where>: the G2 residue check - no applet is
# stranded at the lift z (>= APPLET_LIFT_Z) over the edit chrome. The normal
# post-drag delegate z is 0 (never dragged) or 1 (ConfigOverlay's onReleased
# resets the dropped applet to 1), both far below the lift; a strand would read
# ~900, which THIS readback surfaces instead of a golden.
assert_no_lifted_applet() {
    local view="$1" where="$2" bad
    bad="$({ echo "$view $APPLET_LIFT_Z"; e2e_json viewAppletsData u "$view"; } | python3 -c '
import json, sys
view, lift = (int(x) for x in sys.stdin.readline().split())
applets = json.load(sys.stdin)
stuck = [(a["id"], a["z"]) for a in applets if a["z"] >= lift]
print(";".join("%d@z%s" % (i, z) for i, z in stuck))
')"
    [[ -z "$bad" ]] || e2e_fail "G2: applet(s) stranded over chrome at $where: $bad"
}

# assert_z_all_zero <view> <where>: the clean at-rest baseline (before any drag)
# reports every applet at the layout default z 0.
assert_z_all_zero() {
    local view="$1" where="$2" bad
    bad="$({ echo "$view"; e2e_json viewAppletsData u "$view"; } | python3 -c '
import json, sys
sys.stdin.readline()
applets = json.load(sys.stdin)
nz = [(a["id"], a["z"]) for a in applets if a["z"] != 0]
print(";".join("%d@z%s" % (i, z) for i, z in nz))
')"
    [[ -z "$bad" ]] || e2e_fail "G2: applet(s) not at rest z 0 at $where: $bad"
}

# run_axis_checks <view> <label>: the full HC3 proof on one view.
run_axis_checks() {
    local view="$1" label="$2" pair from to rc before after
    pair="$(simple_adjacent_pair "$view")" || e2e_fail "$label: no adjacent pair of ordinary applets to reorder"
    read -r from to <<<"$pair"
    echo "== $label (view $view): reorder visual applets $from <-> $to =="

    # G2 clean baseline: every applet at the layout default z 0
    assert_z_all_zero "$view" "$label rest"
    echo "  G2 baseline clean: all applets at rest z 0"

    # (1) the driver CAN commit a real reorder (proves the refusals are real)
    before="$(applet_reorder_order "$view")" || e2e_fail "$label: order readback failed"
    local ok=0 try
    for try in 1 2 3; do
        applet_reorder_attempt "$view" commit "$from" "$to"; rc=$?
        (( rc == 1 )) && e2e_fail "$label: driver error committing reorder"
        if (( rc == 0 )); then ok=1; break; fi
        echo "  (commit attempt $try did not cross the neighbour - retrying)"
    done
    (( ok == 1 )) || e2e_fail "$label: commit never reordered in 3 tries (calibration)"
    after="$(applet_reorder_order "$view")"
    [[ "$after" != "$before" ]] || e2e_fail "$label: driver reported commit (rc 0) but order is unchanged"
    assert_no_lifted_applet "$view" "$label after commit"
    echo "  committed reorder: [$before] -> [$after]"

    # swap back so the refusal checks start from a known order (a second commit
    # of the same pair is deterministic)
    applet_reorder_attempt "$view" commit "$from" "$to" >/dev/null 2>&1 || true
    local base2
    base2="$(applet_reorder_order "$view")"

    # (2) HC3 CORE: a drag that does NOT cross a neighbour is REFUSED - reported
    # as "no reorder" (rc 3), never as success, and the order cannot change
    applet_reorder_attempt "$view" noop "$from" "$to"; rc=$?
    (( rc == 3 )) || e2e_fail "$label: no-op reorder reported rc=$rc, expected 3 (REFUSED). A drag that never crosses a neighbour MUST be observed AS a refusal, not a success"
    after="$(applet_reorder_order "$view")"
    [[ "$after" == "$base2" ]] || e2e_fail "$label: no-op drag changed the order ([$base2] -> [$after])"
    assert_no_lifted_applet "$view" "$label after no-op"
    echo "  HC3: refused no-op observed AS a refusal (rc 3, order unchanged)"

    # (3) HC3 ABORT: release-at-origin (motion + a neighbour crossed, released
    # back) leaves the order UNCHANGED and no applet stranded over chrome
    applet_reorder_attempt "$view" origin "$from" "$to"; rc=$?
    (( rc == 3 )) || e2e_fail "$label: release-at-origin abort reported rc=$rc, expected 3 (order restored)"
    after="$(applet_reorder_order "$view")"
    [[ "$after" == "$base2" ]] || e2e_fail "$label: release-at-origin abort left residue ([$base2] -> [$after])"
    assert_no_lifted_applet "$view" "$label after release-at-origin abort"
    echo "  HC3: aborted reorder (release-at-origin) restored baseline, no strand"

    # (4) DR-6: drive the escape-in-held-drag and OBSERVE Escape's real effect
    # (never assumed to cancel). Report order + z + editMode; the only hard
    # assertion is that the dock survives it. If Escape stranded an applet over
    # chrome, the G2 readback below SEES it - a live demonstration of the readback.
    local pre epost ez emode
    pre="$(applet_reorder_order "$view")"
    applet_reorder_enter "$view" || e2e_fail "$label: could not enter rearrange for the escape observation"
    applet_reorder_glide "$view" escape "$from" "$to" || true
    applet_reorder_exit "$view" || true
    e2e_wait_running 15 || e2e_fail "$label: dock did not survive the DR-6 escape-in-held-drag"
    epost="$(applet_reorder_order "$view")"
    emode="$(_applet_reorder_flag "$view" editMode)"
    ez="$({ echo "$view $APPLET_LIFT_Z"; e2e_json viewAppletsData u "$view"; } | python3 -c '
import json, sys
view, lift = (int(x) for x in sys.stdin.readline().split())
applets = json.load(sys.stdin)
print("STRANDED " + ",".join("%d@z%s" % (a["id"], a["z"]) for a in applets if a["z"] >= lift) if any(a["z"] >= lift for a in applets) else "no-strand")')"
    echo "  DR-6 escape observed: order [$pre] -> [$epost], editMode=$emode, z-residue=$ez (dock alive)"
}

hview="$(discover_axis_view horizontal)" || e2e_fail "no horizontal view with >=2 applets to reorder"
vview="$(discover_axis_view vertical)"   || e2e_fail "no VERTICAL view with >=2 applets to reorder (the buggy path must be covered)"

run_axis_checks "$hview" "HORIZONTAL"
run_axis_checks "$vview" "VERTICAL"

echo "applet-reorder driver: commit + refused-no-op + release-at-origin abort + DR-6 escape proven on both axes"
