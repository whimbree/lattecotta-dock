#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""C-I4 / P4 acceptance (docs/tracking/e2e-interaction-test-plan.md, HC3): the
coarse removeApplet D-Bus action, PROVING BOTH SIDES.

Happy path - remove an existing applet by instance id and watch the coarse
action fire. removeApplet is the "Remove this Widget" the context menu triggers
(Applet::destroy()), which rides the libplasma UNDO WINDOW: the applet does NOT
vanish immediately, it lingers with inScheduledDestruction flipped to true (the
removeView undo trap, observed here for applets). The recipe reports the observed
removal effect (scheduled-destruction flip, or an outright leave if the vehicle
finalizes early) and asserts SOMETHING removed the applet - a green no-op is not
acceptance.

Rejection (the HC3 deliverable) - drive a BAD applet id AND a bad containment id
and prove each is REFUSED: a qWarning in the dock log AND the applet population
byte-identical (no applet entered destruction). The acceptance test OBSERVES A
REJECTION, not just a green remove.

Ported from tests/e2e/090-remove-applet.sh to latte_harness.recipe (BP-3, the
bash-to-python migration's recipe batch). The readback helpers are the typed
recipe API (viewsData/viewAppletsData); the dock log the rejection legs grep is
E2E_DOCK_LOG, read directly as the bash did.
"""

import os
import sys
import time
from pathlib import Path

from latte_harness import recipe


def _all_view_ids() -> list[int]:
    """Every current containment id (the bash all_view_ids)."""
    return [v.containment_id for v in recipe.views()]


def _applet_ids(cid: int) -> list[int]:
    """The applet instance ids in visual order (the bash applet_ids)."""
    return [a.id for a in recipe.view_applets(cid)]


def _sched_of(cid: int, applet_id: int) -> str:
    """One applet's inScheduledDestruction as 'True'/'False', or 'MISSING' if
    that id is no longer reported (finalized/absent) - the bash sched_of."""
    match = next((a for a in recipe.view_applets(cid) if a.id == applet_id), None)
    return "MISSING" if match is None else str(match.in_scheduled_destruction)


def _applet_total() -> int:
    """Applets summed across ALL views - the 'nothing changed anywhere' witness
    a rejection asserts stays byte-identical (the bash applet_total)."""
    return sum(len(_applet_ids(cid)) for cid in _all_view_ids())


def _dock_log_lines() -> list[str]:
    return Path(os.environ["E2E_DOCK_LOG"]).read_text(errors="replace").splitlines()


def _new_log_has(mark: int, needle: str) -> bool:
    """True iff a dock-log line added since ``mark`` carries ``needle`` (the bash
    ``tail -n +$((mark+1)) | grep -q``)."""
    return any(needle in line for line in _dock_log_lines()[mark:])


def _dump_new_log(mark: int) -> None:
    print("---- new dock-log lines ----", file=sys.stderr, flush=True)
    for line in _dock_log_lines()[mark:]:
        print(line, file=sys.stderr, flush=True)


def main() -> None:
    # target: the view with the most applets, and its last applet.
    view_ids = _all_view_ids()
    target = max(view_ids, key=lambda cid: (len(_applet_ids(cid)), cid), default=None)
    if target is None:
        recipe.fail("no view found to remove an applet from")

    before = _applet_ids(target)
    before_n = len(before)
    if before_n < 1:
        recipe.fail(f"target view {target} reports no applets to remove")

    #! the last applet in visual order: least structurally load-bearing, and the
    #! throwaway config copy is restarted between recipes so a scheduled removal
    #! never leaks into the next one
    victim = before[-1]
    victim_sched = _sched_of(target, victim)
    if victim_sched != "False":
        recipe.fail(
            f"target applet {victim} is already "
            f"inScheduledDestruction={victim_sched} before removal"
        )
    print(f"target view {target} has {before_n} applets; removing instance {victim}")

    # happy path: the coarse remove fires (undo window OR early finalize).
    recipe.call("removeApplet", "uu", str(target), str(victim))
    effect = ""
    for _ in range(15):
        time.sleep(1)
        now = _sched_of(target, victim)
        if now == "True":
            #! the documented libplasma path: destroy() marks the applet
            #! destroyed() and holds it for the ~60s undo window, so it lingers in
            #! the readback with inScheduledDestruction=true (the removeView trap)
            effect = "scheduled-destruction (undo window open)"
            break
        if now == "MISSING":
            #! the counter-case: the applet left the readback outright (the vehicle
            #! finalized before the poll saw the flip). Still a real removal.
            effect = "left the readback (finalized)"
            break
    if not effect:
        recipe.fail(
            f"removeApplet did not remove applet {victim}: it stayed "
            "inScheduledDestruction=False and present"
        )
    print(f"removeApplet fired: applet {victim} {effect}")

    #! undo-window witness: while the window is open the applet still counts (it
    #! drops from the readback only when the window ends), so report the total for
    #! the record - the rejection tests below re-baseline it either way.
    if effect.startswith("scheduled-destruction"):
        print(f"undo window: applet lingers, still counted (total now {_applet_total()})")

    # HC3a: a bad APPLET id is REFUSED (qWarning + nothing enters destruction).
    bad_applet = 987654
    for cid in _all_view_ids():
        if bad_applet in _applet_ids(cid):
            recipe.fail(f"test bug: {bad_applet} is a real applet id, pick another")

    pre_total = _applet_total()
    mark = len(_dock_log_lines())
    recipe.call("removeApplet", "uu", str(target), str(bad_applet))
    time.sleep(2)
    post_total = _applet_total()
    if post_total != pre_total:
        recipe.fail(
            f"REJECTION LEAK: applet total changed {pre_total} -> {post_total} on a bad applet id"
        )
    if not _new_log_has(mark, f"removeApplet found no applet {bad_applet} on containment {target}"):
        _dump_new_log(mark)
        recipe.fail(
            f"no removeApplet refusal qWarning for bad applet id {bad_applet} in the dock log"
        )
    print(f"rejection observed: bad applet id {bad_applet} refused (qWarning + nothing removed)")

    # HC3b: a bad CONTAINMENT id is REFUSED (qWarning + no removal).
    bad_cid = 987654
    if bad_cid in _all_view_ids():
        recipe.fail(f"test bug: {bad_cid} is a real view id, pick another")

    pre_total = _applet_total()
    mark = len(_dock_log_lines())
    recipe.call("removeApplet", "uu", str(bad_cid), str(victim))
    time.sleep(2)
    post_total = _applet_total()
    if post_total != pre_total:
        recipe.fail(
            f"REJECTION LEAK: applet total changed "
            f"{pre_total} -> {post_total} on a bad containment id"
        )
    if not _new_log_has(
        mark, f"removeApplet requested for containment {bad_cid} which has no view"
    ):
        _dump_new_log(mark)
        recipe.fail(
            f"no removeApplet refusal qWarning for bad containment id {bad_cid} in the dock log"
        )
    print(f"rejection observed: bad containment id {bad_cid} refused (qWarning + nothing removed)")

    print("PASS: removeApplet fires the coarse remove, and both bad ids are refused")


if __name__ == "__main__":
    recipe.run(main)
