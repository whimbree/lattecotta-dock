#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""D77 (dock duplication retains clone lineage and edit ownership) focused
runtime acceptance. The shared edit chrome starts on A, receives an enter for B,
then receives B's exit before the 400 ms retarget expires. B must never enter
edit mode after that exit. A later ordinary B enter/exit proves the cancellation
did not disable the target.

Ported from tests/e2e/dock-edit-retarget-cancel.sh to latte_harness.recipe
(BP-3, the bash-to-python migration's recipe batch). editMode / isCloned /
isClonedFrom now ride the widened typed View model (W3, widen the readback
models), so viewsData is read through recipe.views() instead of raw JSON - a
transient dbusreports refusal still raises the pollable DbusUnavailableError the
polling callers here catch. The coarse duplicateView / setViewEditMode actions
stay busctl calls that fail loudly on a D-Bus error, matching the bash
`e2e_call ... || e2e_fail`.
"""

import time

from latte_harness import recipe


def _view_edit_mode(view: int) -> str:
    """'true'/'false' for a view's editMode, or a loud disappearance (the bash
    view_edit_mode, whose sys.exit maps to a RecipeError here).

    A refused viewsData reply raises the pollable DbusUnavailableError from
    recipe.views(), the RecipeError subclass the polling callers here catch."""
    record = next((v for v in recipe.views() if v.containment_id == view), None)
    if record is None:
        raise recipe.RecipeError(f"view {view} disappeared")
    return "true" if record.edit_mode else "false"


def _wait_for_edit_mode(view: int, expected: str) -> bool:
    """Poll editMode up to 50 times at 0.1s; True on match, False on timeout.

    A transient disappearance is treated as a non-match and polled through,
    exactly as the bash `[[ "$(view_edit_mode)" == "$expected" ]]` swallowed the
    empty output of a view_edit_mode that exited.
    """
    for _ in range(50):
        try:
            if _view_edit_mode(view) == expected:
                return True
        except recipe.RecipeError:
            pass
        time.sleep(0.1)
    return False


def main() -> None:
    before = recipe.views()
    originals = [v for v in before if not v.is_cloned]
    if len(originals) != 1:
        raise recipe.RecipeError(f"expected one original view, saw {len(originals)}")
    view_a = originals[0].containment_id

    recipe.call_or_fail(
        f"duplicateView failed for original containment {view_a}", "duplicateView", "u", str(view_a)
    )

    before_ids = {v.containment_id for v in before}
    view_b = None
    for _ in range(100):
        try:
            candidates = recipe.views()
        except recipe.RecipeError:
            candidates = []
        created = [v for v in candidates if v.containment_id not in before_ids]
        if len(created) == 1 and not created[0].is_cloned and created[0].is_cloned_from == -1:
            view_b = created[0].containment_id
            break
        time.sleep(0.2)
    if view_b is None:
        recipe.fail("independent duplicate did not reach viewsData")

    recipe.call_or_fail(
        f"could not enter edit mode on containment {view_a}",
        "setViewEditMode",
        "ub",
        str(view_a),
        "true",
    )
    if not _wait_for_edit_mode(view_a, "true"):
        recipe.fail(f"containment {view_a} never entered edit mode")

    start_ns = time.monotonic_ns()
    recipe.call_or_fail(
        f"could not request edit mode on containment {view_b}",
        "setViewEditMode",
        "ub",
        str(view_b),
        "true",
    )
    recipe.call_or_fail(
        f"could not cancel pending edit mode on containment {view_b}",
        "setViewEditMode",
        "ub",
        str(view_b),
        "false",
    )
    end_ns = time.monotonic_ns()
    elapsed_ms = (end_ns - start_ns) // 1_000_000
    if elapsed_ms >= 400:
        recipe.fail(f"enter/exit driver took {elapsed_ms}ms and missed the retarget window")

    for _ in range(20):
        if _view_edit_mode(view_a) != "false":
            recipe.fail(f"old containment {view_a} re-entered edit mode after B cancellation")
        if _view_edit_mode(view_b) != "false":
            recipe.fail(f"pending containment {view_b} entered edit mode after its exit")
        time.sleep(0.1)

    recipe.call_or_fail(
        f"ordinary edit enter failed for containment {view_b}",
        "setViewEditMode",
        "ub",
        str(view_b),
        "true",
    )
    if not _wait_for_edit_mode(view_b, "true"):
        recipe.fail(f"containment {view_b} never entered edit mode after cancellation")
    recipe.call_or_fail(
        f"ordinary edit exit failed for containment {view_b}",
        "setViewEditMode",
        "ub",
        str(view_b),
        "false",
    )
    if not _wait_for_edit_mode(view_b, "false"):
        recipe.fail(f"containment {view_b} stayed in edit mode after ordinary exit")

    print(
        f"dock edit retarget: B enter/exit canceled in {elapsed_ms}ms before timeout; "
        "later B round-trip passed"
    )


if __name__ == "__main__":
    recipe.run(main)
