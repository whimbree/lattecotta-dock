#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""C-I3 / P3 acceptance (docs/tracking/e2e-interaction-test-plan.md, HC3): the
coarse addApplet D-Bus action, PROVING BOTH SIDES.

Happy path - append an installed plasmoid and watch the applet-id order (the G1
readback) grow by exactly one NEW instance id; the two order readbacks
(viewAppletsData ids, viewAppletsOrder) agree.

Rejection (the HC3 deliverable) - drive a BAD containment id and prove the
action REFUSES it: a qWarning in the dock log AND zero applets created on ANY
view. The acceptance test OBSERVES A REJECTION, not just a green add.

Ported from tests/e2e/080-add-applet.sh to latte_harness.recipe (BP-3, the
bash-to-python migration's recipe batch). The readback helpers are the typed
recipe API; viewAppletsOrder is a busctl string-array reply, parsed for its
quoted ids exactly as the bash did, and the rejection leg greps E2E_DOCK_LOG
directly.
"""

import os
import re
import sys
import time
from pathlib import Path

from latte_harness import recipe


def _all_view_ids() -> list[int]:
    """Every current containment id (the bash all_view_ids)."""
    return [v.containment_id for v in recipe.views()]


def _applet_ids(cid: int) -> list[int]:
    """The applet instance ids in visual order, from viewAppletsData (the bash
    applet_ids)."""
    return [a.id for a in recipe.view_applets(cid)]


def _applet_order(cid: int) -> list[int]:
    """The same ids via the viewAppletsOrder method (busctl `as N "id" ...`); the
    G1 fix strips justify-splitter sentinels so this must equal _applet_ids on
    every alignment (the bash applet_order)."""
    return [int(m) for m in re.findall(r'"(-?\d+)"', recipe.call("viewAppletsOrder", "u", str(cid)))]


def _plugin_of(cid: int, applet_id: int) -> str:
    """The plugin string of one applet (the bash plugin_of)."""
    return next(a.plugin for a in recipe.view_applets(cid) if a.id == applet_id)


def _applet_total() -> int:
    """Applets summed across ALL views - the 'created nothing anywhere' witness
    the rejection asserts stays byte-identical (the bash applet_total)."""
    return sum(len(_applet_ids(cid)) for cid in _all_view_ids())


def _dock_log_lines() -> list[str]:
    return Path(os.environ["E2E_DOCK_LOG"]).read_text(errors="replace").splitlines()


def _new_log_has(mark: int, needle: str) -> bool:
    return any(needle in line for line in _dock_log_lines()[mark:])


def _dump_new_log(mark: int) -> None:
    print("---- new dock-log lines ----", file=sys.stderr, flush=True)
    for line in _dock_log_lines()[mark:]:
        print(line, file=sys.stderr, flush=True)


def main() -> None:
    # target: the view with the most applets, and a safe source plugin.
    view_ids = _all_view_ids()
    target = max(view_ids, key=lambda cid: (len(_applet_ids(cid)), cid), default=None)
    if target is None:
        recipe.fail("no view found to add an applet to")

    before = _applet_ids(target)
    before_n = len(before)
    if before_n < 1:
        recipe.fail(f"target view {target} reports no applets")

    #! a plugin the dock can actually RESOLVE in this vehicle: addApplet accepts
    #! only plugins under <datadir>/plasma/plasmoids/<id> (Importer::standardPaths
    #! = GenericDataLocation), which is Latte's staged plasmoids plus the system
    #! plasma set - NOT every widget the session renders (analogclock is a
    #! metapackage, no package dir, and addApplet rightly refuses it). minimizeall
    #! is a trivial system button; org.kde.latte.plasmoid is the staged guarantee
    #! (and, since the view already carries one, adds a SECOND same-plugin instance
    #! - the exact G1 disambiguation case). Try in order; a refusal is logged, so
    #! it is distinguishable from a slow async create.
    candidates = ("org.kde.plasma.minimizeall", "org.kde.latte.plasmoid")

    src_plugin = ""
    after: list[int] = before
    for cand in candidates:
        print(f"target view {target} has {before_n} applets; trying to add '{cand}'")
        mark = len(_dock_log_lines())
        recipe.call("addApplet", "us", str(target), cand)
        for _ in range(15):
            time.sleep(1)
            after = _applet_ids(target)
            if len(after) > before_n:
                break
            if _new_log_has(mark, "found no installed plasmoid named") and _new_log_has(mark, cand):
                break
        if len(after) > before_n:
            src_plugin = cand
            break
        print(f"  ('{cand}' not resolvable in this vehicle, trying the next candidate)")

    if not src_plugin:
        recipe.fail(f"no candidate plugin was addable in this vehicle: {' '.join(candidates)}")
    after_n = len(after)
    if after_n != before_n + 1:
        recipe.fail(
            f"addApplet did not add exactly one applet "
            f"(before {before_n}, after {after_n}: {' '.join(str(i) for i in after)})"
        )

    #! exactly one id is new, every old id survived (nothing dropped/reordered
    #! into oblivion), and the new one carries the SAME plugin - the same-plugin
    #! disambiguation the G1 id-order readback exists for
    new_ids = set(after) - set(before)
    if len(new_ids) != 1:
        recipe.fail(
            f"expected exactly one new applet id, got: {' '.join(str(i) for i in sorted(new_ids))}"
        )
    missing = set(before) - set(after)
    if missing:
        recipe.fail(f"add dropped pre-existing applet ids: {' '.join(str(i) for i in sorted(missing))}")
    new_id = next(iter(new_ids))
    new_plugin = _plugin_of(target, new_id)
    if new_plugin != src_plugin:
        recipe.fail(f"new applet {new_id} is '{new_plugin}', expected '{src_plugin}'")
    print(f"added one new instance id {new_id} (plugin {new_plugin}), distinct from {before_n} existing")

    #! G1 consistency: the cheap viewAppletsOrder method must report the SAME
    #! ordered ids as viewAppletsData (both splitter-free) - the fix that retires
    #! the justify -10 pollution
    order_method = _applet_order(target)
    if order_method != after:
        recipe.fail(
            f"viewAppletsOrder ('{' '.join(str(i) for i in order_method)}') disagrees with "
            f"viewAppletsData ids ('{' '.join(str(i) for i in after)}')"
        )
    print("viewAppletsOrder agrees with viewAppletsData id order (G1)")

    # HC3: a bad containment id is REFUSED (qWarning + zero applets).
    bad_cid = 987654
    if bad_cid in _all_view_ids():
        recipe.fail(f"test bug: {bad_cid} is a real view id, pick another")

    before_total = _applet_total()
    mark = len(_dock_log_lines())
    recipe.call("addApplet", "us", str(bad_cid), src_plugin)
    time.sleep(2)
    after_total = _applet_total()
    if after_total != before_total:
        recipe.fail(
            f"REJECTION LEAK: applet total changed {before_total} -> {after_total} "
            "on a bad containment id"
        )

    #! the refusal must be LOUD, not a silent no-op: the qWarning naming the bad
    #! id must appear in the dock log lines produced by THIS call
    if not _new_log_has(mark, f"addApplet requested for containment {bad_cid} which has no view"):
        _dump_new_log(mark)
        recipe.fail(f"no addApplet refusal qWarning for bad containment id {bad_cid} in the dock log")
    print(f"rejection observed: bad containment id {bad_cid} refused (qWarning + 0 applets created)")

    print("PASS: addApplet appends one, G1 order is consistent, and a bad id is refused")


if __name__ == "__main__":
    recipe.run(main)
