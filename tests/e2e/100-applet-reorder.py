#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""The applet-REORDER driver's acceptance test (P6 / C-I7,
docs/tracking/e2e-interaction-test-plan.md). HC3: a driver whose acceptance only shows
a green happy path is untrustworthy - it cannot be relied on to assert "the
abort left no residue" if it would report success regardless. So this proves,
on BOTH the horizontal (bottom/top, x-drag) AND the VERTICAL (left/right,
y-drag - the historically-buggy path) axes, that the driver:
  1. CAN commit a real cross-neighbour reorder (order changes) - so the
     refusals below are genuine, not a driver that never reorders;
  2. observes a REFUSED reorder AS a refusal - a drag that never crosses a
     neighbour is reported as "no reorder" (the driver's own rc), NOT success;
  3. observes an ABORTED reorder (release-at-origin: motion, a neighbour
     crossed, then released back) as leaving the order UNCHANGED;
  4. drives the DR-6 escape-in-held-drag path, OBSERVING Escape's REAL effect
     (the ConfigOverlay MouseArea has no Keys handler, so Escape is NOT a given
     cancel - see the plan's ESCAPE-vs-RELEASE finding), never assuming it
     cancels, and now HARD-ASSERTS no applet is stranded over the chrome after
     the escape edit-exit (the D2 edit-exit-mid-drag stranding, closed by the
     same onCanceled restore the D285 fix adds);
  5. and drives the D285 right-click-during-drag path (a right-button click
     chorded while the left drag button is held), asserting the grab-lost drag
     leaves no stranded applet and restores the order.
The G2 stacking readback carries the "stuck over chrome" residue check: every
applet sits at the layout default z (0) at rest, and no drag may leave an
applet stranded at the lift z (>= 900) - the 480ae30e3 class made queryable.

Ported from tests/e2e/100-applet-reorder.sh to latte_harness.recipe /
latte_harness.applet_reorder (BP-3, the bash-to-python migration's driver-recipe
batch). The reorder lifecycle/order/attempt readbacks ride the typed
applet_reorder driver; the axis/pair discovery and the G2 z-residue scans read
viewsData / viewAppletsData ride the widened typed models (W3, widen the readback
models): recipe.Applet now carries z (the G2 stacking readback) and recipe.View
carries isCloned, so both read through the typed recipe.view_applets() /
recipe.views() readers, and a refused reply raises the pollable
DbusUnavailableError instead of the old json.loads crash on "".
"""

import contextlib

from latte_harness import applet_reorder, recipe

APPLET_LIFT_Z = 900  # ConfigOverlay parks a dragged applet's delegate here

# the wide/container plugins whose reorder geometry is not a simple single slot
_WIDE = {
    "org.kde.latte.plasmoid",
    "org.kde.plasma.icontasks",
    "org.kde.plasma.systemtray",
    "org.nomad.systemtray",
}


def discover_axis_view(orient: str) -> int | None:
    """The widest/tallest non-hidden, non-cloned view on that orientation carrying
    at least two applets. orientation is "horizontal" (bottom/top) or "vertical"
    (left/right)."""
    edges = ("bottom", "top") if orient == "horizontal" else ("left", "right")
    views = [v for v in recipe.views() if v.edge in edges and not v.is_hidden and not v.is_cloned]
    # widest (horizontal) / tallest (vertical) first, deterministic
    axis = 2 if orient == "horizontal" else 3
    views.sort(key=lambda v: -v.absolute_geometry[axis])
    for v in views:
        if len(recipe.view_applets(v.containment_id)) >= 2:
            return v.containment_id
    return None


def simple_adjacent_pair(view: int) -> tuple[int, int] | None:
    """The first visual index i such that applet[i] and applet[i+1] are BOTH
    ordinary single-slot widgets (not the wide tasks plasmoid or a systemtray
    container), returned as (i, j). Keeps the drag geometry simple and the reorder
    unambiguous. None if the view carries no such pair."""
    applets = recipe.view_applets(view)
    for i in range(len(applets) - 1):
        if applets[i].plugin not in _WIDE and applets[i + 1].plugin not in _WIDE:
            return i, i + 1
    return None


def assert_no_lifted_applet(view: int, where: str) -> None:
    """The G2 residue check - no applet is stranded at the lift z (>= APPLET_LIFT_Z)
    over the edit chrome. The normal post-drag delegate z is 0 (never dragged) or 1
    (ConfigOverlay's onReleased resets the dropped applet to 1), both far below the
    lift; a strand would read ~900, which THIS readback surfaces instead of a golden.
    """
    stuck = [(a.id, a.z) for a in recipe.view_applets(view) if a.z >= APPLET_LIFT_Z]
    bad = ";".join(f"{i}@z{z}" for i, z in stuck)
    if bad:
        recipe.fail(f"G2: applet(s) stranded over chrome at {where}: {bad}")


def assert_z_all_zero(view: int, where: str) -> None:
    """The clean at-rest baseline (before any drag) reports every applet at the
    layout default z 0."""
    nz = [(a.id, a.z) for a in recipe.view_applets(view) if a.z != 0]
    bad = ";".join(f"{i}@z{z}" for i, z in nz)
    if bad:
        recipe.fail(f"G2: applet(s) not at rest z 0 at {where}: {bad}")


def _order(view: int, label: str) -> str:
    """The applet-id order, or a loud fail (the bash ``|| e2e_fail`` on the readback;
    an unguarded raw ``$(...)`` that returned empty would have false-passed the
    order-changed assertions, so the readback failure is made loud here)."""
    try:
        return applet_reorder.applet_reorder_order(view)
    except applet_reorder.AppletReorderError:
        recipe.fail(f"{label}: order readback failed")


def run_axis_checks(view: int, label: str) -> None:
    """The full HC3 proof on one view."""
    pair = simple_adjacent_pair(view)
    if pair is None:
        recipe.fail(f"{label}: no adjacent pair of ordinary applets to reorder")
    frm, to = pair
    print(f"== {label} (view {view}): reorder visual applets {frm} <-> {to} ==")

    # G2 clean baseline: every applet at the layout default z 0
    assert_z_all_zero(view, f"{label} rest")
    print("  G2 baseline clean: all applets at rest z 0")

    # (1) the driver CAN commit a real reorder (proves the refusals are real)
    before = _order(view, label)
    ok = False
    for try_ in (1, 2, 3):
        rc = applet_reorder.applet_reorder_attempt(view, "commit", frm, to)
        if rc == 1:
            recipe.fail(f"{label}: driver error committing reorder")
        if rc == 0:
            ok = True
            break
        print(f"  (commit attempt {try_} did not cross the neighbour - retrying)")
    if not ok:
        recipe.fail(f"{label}: commit never reordered in 3 tries (calibration)")
    after = _order(view, label)
    if after == before:
        recipe.fail(f"{label}: driver reported commit (rc 0) but order is unchanged")
    assert_no_lifted_applet(view, f"{label} after commit")
    print(f"  committed reorder: [{before}] -> [{after}]")

    # swap back so the refusal checks start from a known order (a second commit
    # of the same pair is deterministic)
    applet_reorder.applet_reorder_attempt(view, "commit", frm, to)
    base2 = _order(view, label)

    # (2) HC3 CORE: a drag that does NOT cross a neighbour is REFUSED - reported
    # as "no reorder" (rc 3), never as success, and the order cannot change
    rc = applet_reorder.applet_reorder_attempt(view, "noop", frm, to)
    if rc != 3:
        recipe.fail(
            f"{label}: no-op reorder reported rc={rc}, expected 3 (REFUSED). A drag that never "
            "crosses a neighbour MUST be observed AS a refusal, not a success"
        )
    after = _order(view, label)
    if after != base2:
        recipe.fail(f"{label}: no-op drag changed the order ([{base2}] -> [{after}])")
    assert_no_lifted_applet(view, f"{label} after no-op")
    print("  HC3: refused no-op observed AS a refusal (rc 3, order unchanged)")

    # (3) HC3 ABORT: release-at-origin (motion + a neighbour crossed, released
    # back) leaves the order UNCHANGED and no applet stranded over chrome
    rc = applet_reorder.applet_reorder_attempt(view, "origin", frm, to)
    if rc != 3:
        recipe.fail(
            f"{label}: release-at-origin abort reported rc={rc}, expected 3 (order restored)"
        )
    after = _order(view, label)
    if after != base2:
        recipe.fail(f"{label}: release-at-origin abort left residue ([{base2}] -> [{after}])")
    assert_no_lifted_applet(view, f"{label} after release-at-origin abort")
    print("  HC3: aborted reorder (release-at-origin) restored baseline, no strand")

    # (4) DR-6: drive the escape-in-held-drag and OBSERVE Escape's real effect
    # (never assumed to cancel) - Escape here exits edit mode, hiding the
    # ConfigOverlay MouseArea mid-press. Report order + z + editMode diagnostically,
    # THEN hard-assert no applet is stranded over the chrome. Escape's edit-exit used
    # to leave the dragged applet at the lift z (900) parented to root (D2, the
    # edit-exit-mid-drag stranding) - hiding the grabbing MouseArea fires its
    # onCanceled, which the D285 fix now handles, so the applet un-strands on this
    # path too. This is the invariant that would have caught the stranding class
    # months ago; it is now enforced, not merely observed.
    pre = _order(view, label)
    try:
        applet_reorder.applet_reorder_enter(view)
    except applet_reorder.AppletReorderError:
        recipe.fail(f"{label}: could not enter rearrange for the escape observation")
    with contextlib.suppress(applet_reorder.AppletReorderError):
        applet_reorder.applet_reorder_glide(view, "escape", frm, to)
    with contextlib.suppress(applet_reorder.AppletReorderError):
        applet_reorder.applet_reorder_exit(view)
    if not recipe.wait_running(15):
        recipe.fail(f"{label}: dock did not survive the DR-6 escape-in-held-drag")
    epost = _order(view, label)
    emode = "true" if applet_reorder.applet_reorder_edit_mode(view) else "false"
    applets = recipe.view_applets(view)
    if any(a.z >= APPLET_LIFT_Z for a in applets):
        ez = "STRANDED " + ",".join(f"{a.id}@z{a.z}" for a in applets if a.z >= APPLET_LIFT_Z)
    else:
        ez = "no-strand"
    print(
        f"  DR-6 escape observed: order [{pre}] -> [{epost}], "
        f"editMode={emode}, z-residue={ez} (dock alive)"
    )
    assert_no_lifted_applet(view, f"{label} after DR-6 escape edit-exit")
    print("  DR-6: no applet stranded over chrome after the escape edit-exit")

    # (5) D285 (the drag-cancel stranding): RIGHT-CLICK during a held drag. The
    # right-click opens the containment context menu, which steals the pointer grab,
    # so Qt fires the ConfigOverlay MouseArea's onCanceled instead of onReleased.
    # Before the onCanceled restore landed, the dragged applet stayed parented to root
    # at the lift z (900), stranded outside the dock over the edit chrome. The chord
    # nudges INSIDE the origin slot (never crossing a neighbour), so a correct restore
    # returns the applet to its origin: the order is UNCHANGED and nothing is stranded.
    # This leg fails against the pre-fix QML (the strand survives) and passes after it.
    base3 = _order(view, label)
    try:
        applet_reorder.applet_reorder_enter(view)
    except applet_reorder.AppletReorderError:
        recipe.fail(f"{label}: could not enter rearrange for the right-click-during-drag leg")
    with contextlib.suppress(applet_reorder.AppletReorderError):
        applet_reorder.applet_reorder_glide(view, "rightclick", frm, to)
    with contextlib.suppress(applet_reorder.AppletReorderError):
        applet_reorder.applet_reorder_exit(view)
    if not recipe.wait_running(15):
        recipe.fail(f"{label}: dock did not survive the D285 right-click-during-drag")
    assert_no_lifted_applet(view, f"{label} after D285 right-click-during-drag")
    rc_after = _order(view, label)
    if rc_after != base3:
        recipe.fail(
            f"{label}: right-click-during-drag changed the order ([{base3}] -> [{rc_after}]); "
            "a grab-lost drag that never crossed a neighbour must restore the origin"
        )
    print("  D285: right-click-during-drag left no strand and restored the order")


def main() -> None:
    hview = discover_axis_view("horizontal")
    if hview is None:
        recipe.fail("no horizontal view with >=2 applets to reorder")
    vview = discover_axis_view("vertical")
    if vview is None:
        recipe.fail("no VERTICAL view with >=2 applets to reorder (the buggy path must be covered)")

    run_axis_checks(hview, "HORIZONTAL")
    run_axis_checks(vview, "VERTICAL")

    print(
        "applet-reorder driver: commit + refused-no-op + release-at-origin abort + DR-6 escape "
        "proven on both axes"
    )


if __name__ == "__main__":
    recipe.run(main)
