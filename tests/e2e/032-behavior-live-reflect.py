#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""CL-3 behavior audit, the LIVE half (docs/tracking/edit-mode-settings-audit-plan.md).
The Behavior page mixes four writer classes; the cfg controls and the S-a
TypeSelection dead-key finding are pinned in tests/behaviorwiringaudittest.cpp.
The pos/vis/view controls write NO config key - Location/Alignment/Screen call
positioner.setNextLocation(...), Visibility/Delay write visibility.* and the
Environment checkboxes write latteView.byPassWM / isPreferredForShortcuts - so
their audit is a READBACK question: does the live view expose edge/alignment/
screen/visibilityMode and the visibility-timer / byPassWM / shortcut state, and
do those readbacks REFLECT the running view (P3, "two views of one value never
disagree")?

This recipe CONSUMES the CL-0 readbacks (it adds none): viewsData for edge /
alignment / screen / visibilityMode, viewConfigData for config.alignment (int)
and the "view" half. It proves: the readbacks ANSWER for a real view; the
alignment reported two ways AGREES (viewsData string vs config int - the P3/P4
cross-view identity); and the drive -> readback loop closes (edit mode flips
viewsData.editMode and the view half still answers in edit mode).

Ported from tests/e2e/032-behavior-live-reflect.sh to latte_harness.audit /
.recipe (BP-3, the bash-to-python migration's recipe batch). viewsData carries
alignment/visibilityMode/screen the typed View model does not, so it is read as
raw JSON (the same boundary the bash python one-liners used); a refused reply on
the editMode re-read maps to the empty-non-answer the bash swallowed.
"""

from __future__ import annotations

import json
import re
import sys
from collections.abc import Callable
from contextlib import suppress
from typing import Any

from latte_harness import audit, recipe
from pydantic import ValidationError

# LatteCore.Types Alignment (declarativeimports/coretypes.h.in).
_ALIGNMENT_NAMES = {
    -1: "none", 0: "center", 1: "left", 2: "right", 3: "top", 4: "bottom", 10: "justify",
}


def _parse_snapshot(text: str) -> dict[str, str]:
    """key<TAB>value snapshot lines into a map (the awk cfg_get/view_get reads)."""
    parsed: dict[str, str] = {}
    for line in text.split("\n"):
        if not line:
            continue
        key, _, value = line.partition("\t")
        parsed[key] = value
    return parsed


def _snapshot_or_fail(produce: Callable[[], str], fail_message: str) -> str:
    """audit_*_snapshot > file || e2e_fail: a readback that cannot validate fails
    loudly with the bash message."""
    try:
        return produce()
    except ValidationError as err:
        print(str(err), file=sys.stderr, flush=True)
        recipe.fail(fail_message)


def _or_absent(value: str) -> str:
    """${v:-<absent>}: the placeholder the bash printed for an empty readback."""
    return value if value else "<absent>"


def _view_record_or_fail(view: int) -> dict[str, Any]:
    """The viewsData record for `view`, or the bash e2e_fail on absence / a
    refused reply (the terminal `|| e2e_fail "could not read viewsData ..."`)."""
    with suppress(json.JSONDecodeError, KeyError, TypeError):
        records: list[dict[str, Any]] = json.loads(recipe.json_payload("viewsData"))
        for record in records:
            if record["containmentId"] == view:
                return record
    recipe.fail(f"could not read viewsData for view {view}")


def _edit_mode_now(view: int) -> str:
    """viewsData.editMode for `view` as 'true'/'false', '' when the reply is
    refused or the view is absent (the bash lenient one-liner: a missing view is
    an empty dict, str('').lower() == '')."""
    try:
        records: list[dict[str, Any]] = json.loads(recipe.json_payload("viewsData"))
    except json.JSONDecodeError:
        return ""
    record = next((r for r in records if r.get("containmentId") == view), None)
    if record is None:
        return ""
    return str(record.get("editMode", "")).lower()


def main() -> None:
    try:
        view = recipe.tasks_view()
    except recipe.RecipeError:
        recipe.fail("no tasks view to audit")
    print(f"auditing behavior readbacks for view {view}")

    # --- viewsData readback: the Location/Alignment/Screen/Visibility surface ---
    record = _view_record_or_fail(view)
    vd_edge = str(record.get("edge", ""))
    vd_alignment = str(record.get("alignment", ""))
    vd_vismode = str(record.get("visibilityMode", ""))
    vd_screen = str(record.get("screen", ""))

    if not vd_edge or vd_edge == "None":
        recipe.fail(f"viewsData carries no edge for view {view}")
    if not vd_alignment:
        recipe.fail(f"viewsData carries no alignment for view {view}")
    if not vd_vismode:
        recipe.fail(f"viewsData carries no visibilityMode for view {view}")
    print(
        f"  viewsData: edge={vd_edge} alignment={vd_alignment} "
        f"screen='{vd_screen}' visibilityMode={vd_vismode}"
    )

    # --- viewConfigData readback: the config int + the C++ "view" half ----------
    cfg = _parse_snapshot(
        _snapshot_or_fail(
            lambda: audit.config_snapshot(view), "viewConfigData config snapshot failed"
        )
    )
    view_map = _parse_snapshot(
        _snapshot_or_fail(lambda: audit.view_snapshot(view), "viewConfigData view snapshot failed")
    )

    # --- AU-3a P3/P4: the alignment reported two ways must AGREE -----------------
    cfg_alignment = cfg.get("alignment", "")
    if not cfg_alignment:
        recipe.fail("viewConfigData config carries no alignment key")
    try:
        expected_alignment_name = _ALIGNMENT_NAMES.get(int(cfg_alignment), "?")
    except ValueError:
        # the bash one-liner's int() traceback left an empty command substitution
        # and fell through to the terminal disagree FAIL; keep that path loud
        expected_alignment_name = ""
    if expected_alignment_name == vd_alignment:
        print(
            f"  P4 agree: alignment reads '{vd_alignment}' (viewsData) "
            f"== config int {cfg_alignment}"
        )
    else:
        recipe.fail(
            f"alignment disagrees across surfaces: viewsData='{vd_alignment}' but config int "
            f"{cfg_alignment} maps to '{expected_alignment_name}'"
        )

    # --- AU-3b/3c: the "view" half answers every C++-property field, sanely ------
    for boolfield in (
        "byPassWM", "isPreferredForShortcuts", "visibilityEnableKWinEdges",
        "visibilityRaiseOnDesktop", "visibilityRaiseOnActivity",
    ):
        value = view_map.get(boolfield, "")
        if value not in ("true", "false"):
            recipe.fail(
                f"view-half '{boolfield}' is not a boolean readback (got '{_or_absent(value)}')"
            )
    print(
        f"  view half booleans answer: byPassWM={view_map.get('byPassWM', '')} "
        f"isPreferredForShortcuts={view_map.get('isPreferredForShortcuts', '')} "
        f"enableKWinEdges={view_map.get('visibilityEnableKWinEdges', '')} "
        f"raiseOnDesktop={view_map.get('visibilityRaiseOnDesktop', '')} "
        f"raiseOnActivity={view_map.get('visibilityRaiseOnActivity', '')}"
    )

    for timer in ("visibilityTimerShow", "visibilityTimerHide"):
        value = view_map.get(timer, "")
        if not re.fullmatch(r"[0-9]+", value):
            recipe.fail(
                f"view-half '{timer}' is not a non-negative integer readback "
                f"(got '{_or_absent(value)}')"
            )
    print(
        f"  view half timers answer: show={view_map.get('visibilityTimerShow', '')}ms "
        f"hide={view_map.get('visibilityTimerHide', '')}ms"
    )

    if not view_map.get("indicatorType", ""):
        recipe.fail("view-half indicatorType absent")

    # --- drive -> readback loop closes: edit mode flips a readback ----------------
    if not audit.enter_editmode(view):
        recipe.fail("edit mode never turned on")
    edit_now = _edit_mode_now(view)
    if edit_now != "true":
        recipe.fail(f"viewsData.editMode did not reflect the edit-mode drive (got '{edit_now}')")

    view_edit = _parse_snapshot(
        _snapshot_or_fail(lambda: audit.view_snapshot(view), "view snapshot failed in edit mode")
    )
    if "byPassWM" not in view_edit:
        recipe.fail("view-half stopped answering in edit mode")
    print("  drive->readback: edit mode flipped viewsData.editMode=true; view half still answers")

    _ = audit.exit_editmode(view)  # bash `audit_exit_editmode "$view" || true`

    print(
        "behavior live reflect: viewsData + viewConfigData readbacks answer and agree "
        "for the pos/vis/view controls"
    )


if __name__ == "__main__":
    recipe.run(main)
