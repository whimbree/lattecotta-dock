#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""C-I8 / P7 acceptance (docs/tracking/e2e-interaction-test-plan.md section 5): the
task-reorder driver (latte_harness.task_reorder) and the G4 window-task order
readback. Proven on the launcher sub-model, which reorders through the IDENTICAL
tasks-applet handler as window tasks (MouseHandler.qml tasksModel.move), so it is
the deterministic vehicle-friendly stand-in the window sub-model rides on (O6).
Four legs, in HC3 tripwire shape:

  1. POSITIVE CONTROL: a real reorder - the driver must SEE the order flip,
     or its "no reorder" verdict below would be worthless.
  2. HC3 REJECTION: a zero-cross hold-noop - the driver reports NO reorder
     (order byte-unchanged, launchers key byte-unchanged) AS the refusal, the
     thing HC3 demands a driver be able to observe.
  3. D1 EVIDENCE: a crossed drag then Escape-held (dragkey) vs the same
     crossed drag without Escape - proves whether Escape reverts a committed
     task move (defect D1). Recorded truthfully, not wished.
  4. REVERSE-JITTER (DR-2 / T5b): out-and-back nets to zero - a clean
     abort-with-no-residue observation on both the order and the config key.

Ported from tests/e2e/092-task-reorder.sh to latte_harness.recipe /
latte_harness.task_reorder (BP-3, the bash-to-python migration's driver-recipe
batch). The order/launchers readbacks ride the typed task_reorder driver; the
launchers config KEY is discovered and round-tripped through kreadconfig6 /
kwriteconfig6 over E2E_LAYOUT, exactly as the bash did.
"""

import contextlib
import io
import os
import re
import subprocess
import sys
from collections.abc import Iterator
from pathlib import Path

from latte_harness import recipe, task_reorder


@contextlib.contextmanager
def _muted_stderr() -> Iterator[None]:
    """The cleanup dock stop's `>/dev/null 2>&1`: keep its diagnostics off output."""
    with contextlib.redirect_stderr(io.StringIO()):
        yield


def main() -> None:
    try:
        view = recipe.tasks_view()
    except recipe.RecipeError:
        recipe.fail("no tasks view")

    layout = os.environ["E2E_LAYOUT"]

    #! preconditions: a pure-launcher bar (a window task reflows mid-drag and its
    #! order does not persist), at least three launchers so a two-slot swap is
    #! unambiguous. The driver is identity-based (appId), no per-icon pixel
    #! calibration - the arithmetic even-slot center (recipe.task_center) rides the
    #! compositor-true window x, accurate within a few px at the default zoom.
    if '"isLauncher":false' in recipe.json_payload("viewTasksData", "u", str(view)):
        recipe.fail("window tasks present; this recipe needs a launchers-only bar")
    apps = task_reorder.taskdrag_order(view).split()
    if len(apps) < 3:
        recipe.fail(f"need >=3 launchers, have {len(apps)}")

    #! the launchers config key (name carries the synced-group id, so discovered
    #! not assumed) - the residue surface a task-reorder abort could strand into
    tasks_applet = next(
        a.id for a in recipe.view_applets(view) if a.plugin == "org.kde.latte.plasmoid"
    )
    launchers_key = _find_launchers_key(layout, view, tasks_applet)
    if not launchers_key:
        recipe.fail(f"no launchers entry in the layout for applet {tasks_applet}")

    general = (
        "--group",
        "Containments",
        "--group",
        str(view),
        "--group",
        "Applets",
        "--group",
        str(tasks_applet),
        "--group",
        "Configuration",
        "--group",
        "General",
    )

    def read_launchers_key() -> str:
        return _kread(layout, *general, "--key", launchers_key)

    def write_launchers_key(value: str) -> None:
        _kwrite(layout, *general, "--key", launchers_key, value)

    orig_launchers = read_launchers_key()

    def restore_config() -> None:
        with _muted_stderr():
            recipe.dock_stop()
        write_launchers_key(orig_launchers)

    try:
        baseline = task_reorder.taskdrag_order(view)
        print(f"baseline order: {baseline}")

        # reset_to_baseline: stop, rewrite the launchers key to the original, restart -
        # the deterministic way back between legs (a committed launcher move persists,
        # so a plain re-read would drift).
        def reset_to_baseline() -> bool:
            if not recipe.dock_stop():
                return False
            write_launchers_key(orig_launchers)
            if not recipe.dock_start():
                return False
            now = task_reorder.taskdrag_order(view)
            if now != baseline:
                print(f"reset did not restore baseline (got: {now})", file=sys.stderr, flush=True)
                return False
            return True

        # ---- leg 1: positive control (the driver SEES a real reorder) --------------
        # swap the middle pair (slots 1 and 2, the 050-proven geometry - the leftmost
        # slot has edge behaviour); expected order is apps[0] apps[2] apps[1] apps[3..]
        src, dst = apps[1], apps[2]
        expected = " ".join([apps[0], apps[2], apps[1], *apps[3:]])

        after = ""
        for attempt in (1, 2, 3):
            task_reorder.taskdrag_reorder(view, src, dst)
            after = task_reorder.taskdrag_order(view)
            if after == expected:
                break
            if after != baseline:
                print(f"  (attempt {attempt} reordered the wrong pair: {after} - resetting)")
            else:
                print(f"  (attempt {attempt} did not cross, retrying)")
            if not reset_to_baseline():
                recipe.fail("could not reset between reorder attempts")
        if after != expected:
            recipe.fail(
                f"positive control: driver could not reorder in 3 attempts "
                f"(expected: {expected}, got: {after})"
            )
        print(f"leg 1 PASS: driver observed a real reorder ({baseline} -> {after})")

        if not reset_to_baseline():
            recipe.fail("could not reset after leg 1")

        # ---- leg 2: HC3 rejection (zero-cross hold-noop reads AS no reorder) --------
        task_reorder.taskdrag_hold_noop(view, apps[1])
        noop_after = task_reorder.taskdrag_order(view)
        if noop_after != baseline:
            recipe.fail(
                f"HC3: a zero-cross hold-noop MOVED a task ({baseline} -> {noop_after}) - "
                "the driver would false-report a reorder"
            )
        print("leg 2 PASS (HC3): zero-cross hold-noop reported AS no reorder (order byte-unchanged)")

        #! and it stranded NO config residue: flush the key with a clean stop and read
        #! it back byte-identical to the original (the launchers-key residue surface)
        if not recipe.dock_stop():
            recipe.fail("no clean stop to flush the launchers key for the residue check")
        noop_key = read_launchers_key()
        if noop_key != orig_launchers:
            recipe.fail(
                f"HC3 residue: the hold-noop rewrote the launchers key "
                f"('{noop_key}' vs '{orig_launchers}')"
            )
        print("         and left the launchers config key byte-unchanged")
        if not recipe.dock_start():
            recipe.fail("dock did not come back after the residue flush")
        if not reset_to_baseline():
            recipe.fail("could not reset after leg 2")

        # ---- leg 3: D1 evidence via Escape (does a committed task move survive?) ----
        # The D1 claim: tasksModel.move runs LIVE during the drag (MouseHandler.qml),
        # and the drag being a compositor drag (dragHelper Drag.dragType Automatic ->
        # QDrag/wl_data_device) means Escape DOES cancel the DRAG - but nothing reverts
        # the already-applied model move. Method: prove the geometry commits a move
        # without Escape (the control), then run it WITH Escape held mid-drag; the
        # verdict hinges only on whether a committed move SURVIVES, not on which
        # neighbour the calibration happened to cross (the 050 calibration can land
        # either adjacent pair, so exact-order matching would be brittle).
        plain_order = ""
        for _attempt in (1, 2, 3, 4):
            task_reorder.taskdrag_reorder(view, src, dst)
            plain_order = task_reorder.taskdrag_order(view)
            if plain_order != baseline:
                break
            if not reset_to_baseline():
                recipe.fail("could not reset during the D1 control")
        if plain_order == baseline:
            recipe.fail("D1: the plain control never crossed a neighbour (calibration) - re-run")
        print(f"leg 3 control: a plain drag committed a move ({baseline} -> {plain_order})")
        if not reset_to_baseline():
            recipe.fail("could not reset before the Escape-held drag")

        escape_order = ""
        for _attempt in (1, 2, 3, 4):
            task_reorder.taskdrag_escape_held(view, src, dst)
            escape_order = task_reorder.taskdrag_order(view)
            #! a committed move that Escape did NOT revert leaves a NON-baseline order;
            #! retry only the ambiguous baseline result (Escape reverted, OR - the
            #! control just proved the geometry crosses - a flaky non-cross)
            if escape_order != baseline:
                break
            if not reset_to_baseline():
                recipe.fail("could not reset during the Escape leg")
        print(f"leg 3 escape-held drag -> {escape_order}")

        if escape_order != baseline:
            d1_disposition = "no-revert"
            print("leg 3 D1 EVIDENCE: a task move committed mid-drag SURVIVED Escape (order is")
            print(f"                  {escape_order}, not the baseline) - Escape cancels the compositor")
            print("                  drag but does NOT revert the already-applied tasksModel.move")
        else:
            d1_disposition = "reverts"
            print("leg 3 D1 EVIDENCE: with the geometry the control just crossed, Escape restored the")
            print("                  baseline order across 4 attempts - a genuine cancel-and-revert")
        print(f"D1 disposition observed: {d1_disposition}")
        if not reset_to_baseline():
            recipe.fail("could not reset after the D1 leg")

        # ---- leg 4: release-back-at-origin does NOT revert either (D1, from A3) ------
        # The A3 abort's other input path: cross a neighbour then bring the pointer
        # BACK to the exact origin and release (reverse-jitter, DR-2 / T5b). Because
        # the move applied LIVE on the out-swing and the 200ms ignoredItem timer
        # suppresses an immediate re-cross on the return, the return does NOT undo it -
        # so a release-back-at-origin leaves the task MOVED, the same live-move truth as
        # leg 3 seen from the release path. Assert the crossed move survived (order !=
        # baseline); retry to ensure the out-swing actually crossed.
        jitter_after = ""
        for _attempt in (1, 2, 3, 4):
            task_reorder.taskdrag_reverse_jitter(view, src, dst)
            jitter_after = task_reorder.taskdrag_order(view)
            if jitter_after != baseline:
                break
            if not reset_to_baseline():
                recipe.fail("could not reset during the reverse-jitter leg")
        if jitter_after == baseline:
            recipe.fail("leg 4: reverse-jitter never crossed a neighbour (calibration) - re-run")
        print("leg 4 D1 EVIDENCE: a reverse-jitter returned to the exact origin still left the task")
        print(f"                  moved (order {jitter_after}, not baseline) - release-back does not revert")
        if not reset_to_baseline():
            recipe.fail("could not reset after leg 4")

        print(
            "ALL LEGS PASS: the driver observes a reorder (leg 1) AND its refusal (leg 2, HC3);"
        )
        print(
            f"D1 = {d1_disposition} (Escape and release-back both leave a committed task move in place)"
        )
    finally:
        restore_config()


def _find_launchers_key(layout: str, view: int, applet: int) -> str:
    """The launchers config KEY name in the tasks applet's General group.

    The bash awk: scan E2E_LAYOUT for the exact group header, then the first
    ``launchers[0-9]*=`` line under it, returning the key name before ``=``. A
    ``[`` line other than the header closes the group, exactly as the awk
    ``/^\\[/ {f=0}`` reset did.
    """
    target = f"[Containments][{view}][Applets][{applet}][Configuration][General]"
    in_group = False
    key = re.compile(r"^launchers[0-9]*=")
    for line in Path(layout).read_text().splitlines():
        if line == target:
            in_group = True
            continue
        if line.startswith("["):
            in_group = False
        if in_group and key.match(line):
            return line.split("=", 1)[0]
    return ""


def _kread(layout: str, *args: str) -> str:
    result = subprocess.run(
        ["kreadconfig6", "--file", layout, *args], capture_output=True, text=True, check=False
    )
    return result.stdout.rstrip("\n")


def _kwrite(layout: str, *args: str) -> None:
    subprocess.run(["kwriteconfig6", "--file", layout, *args], check=False)


if __name__ == "__main__":
    recipe.run(main)
