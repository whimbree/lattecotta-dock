#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""AU-6b (docs/tracking/edit-mode-settings-audit-plan.md, cluster CL-6): the
settings-window chrome audit's live leg. Two things it proves against a real dock:

  1. The universalSettings readback CL-6 added to viewConfigData's "view" object
     answers live and with sane values: inAdvancedModeForEditSettings (control 7,
     the advanced switch) and settingsWindowScaleWidth/Height (control 8, the
     drag-corner per-screen window scales). These live on UniversalSettings, not
     the containment config, so before this readback controls 7 and 8 had nothing
     to read.

  2. The Advanced switch (control 7) DRIVEN through the real settings window flips
     inAdvancedModeForEditSettings, and the readback re-tracks it (P1 applies + P3
     reflects, end to end). The advanced label's MouseArea sits right-aligned in
     the header, so the click sweeps a small right-of-header grid and the readback
     confirms which hit landed.

Non-destructive: it toggles advanced mode and restores it, never adds or removes
a view.

Ported from tests/e2e/033-chrome-advanced-readback.sh to latte_harness.audit /
.recipe (BP-3, the bash-to-python migration's recipe batch). The view_field /
config-field reads re-snapshot the typed audit surface each call (the bash
`audit_view_snapshot | awk` re-read), staying lenient - a snapshot that cannot
validate reads as the empty non-answer the bash awk pipe produced.
"""

from __future__ import annotations

import time

from latte_harness import audit, recipe
from pydantic import ValidationError


def _view_field(view: int, key: str) -> str:
    """view_field(): the value for `key` in a fresh view snapshot, '' when absent
    or the readback cannot validate (the bash `audit_view_snapshot | awk` pipe,
    whose empty stdin yields empty output)."""
    try:
        text = audit.view_snapshot(view)
    except ValidationError:
        return ""
    return _snap_value(text, key)


def _config_field(view: int, key: str) -> str:
    """The value for `key` in a fresh config snapshot, '' when absent / unreadable."""
    try:
        text = audit.config_snapshot(view)
    except ValidationError:
        return ""
    return _snap_value(text, key)


def _snap_value(snapshot: str, key: str) -> str:
    """The json-value stored for `key` in a key<TAB>value snapshot, '' when absent."""
    for line in snapshot.split("\n"):
        if not line:
            continue
        candidate, _, value = line.partition("\t")
        if candidate == key:
            return value
    return ""


def _restore_advanced(view: int, adv0: str) -> None:
    """Drive the Advanced switch back to its baseline through the same header grid
    (the bash restore loop; a match returns, emulating the bash `break 2`)."""
    for yf in (0.05, 0.07, 0.09, 0.11):
        for xf in (0.86, 0.91, 0.80, 0.95):
            _ = audit.settings_click(xf, yf)
            if _view_field(view, "inAdvancedModeForEditSettings") == adv0:
                return
            time.sleep(0.3)


def main() -> None:
    try:
        view = recipe.tasks_view()
    except recipe.RecipeError:
        recipe.fail("no tasks view")

    if not audit.enter_editmode(view):
        recipe.fail("edit mode never turned on")

    # leg 1: the new readback answers live with well-formed values
    adv0 = _view_field(view, "inAdvancedModeForEditSettings")
    sw = _view_field(view, "settingsWindowScaleWidth")
    sh = _view_field(view, "settingsWindowScaleHeight")
    if adv0 not in ("true", "false"):
        recipe.fail(f"view.inAdvancedModeForEditSettings is not a bool: '{adv0}'")
    try:
        width, height = float(sw), float(sh)
        scales_sane = 0.3 <= width <= 3.0 and 0.3 <= height <= 3.0
    except ValueError:
        scales_sane = False
    if not scales_sane:
        recipe.fail(
            f"view.settingsWindowScaleWidth/Height out of the sane 0.3..3.0 range ({sw} / {sh})"
        )
    stick = _config_field(view, "configurationSticker")
    if stick not in ("true", "false"):
        recipe.fail(f"config.configurationSticker (pin state) is not a bool: '{stick}'")
    print(
        f"readback answers live: inAdvancedModeForEditSettings={adv0} scales={sw}/{sh} "
        f"configurationSticker={stick}"
    )

    # leg 2: drive the Advanced switch and confirm the readback flips
    want = "false" if adv0 == "true" else "true"
    flipped = ""
    for yf in (0.05, 0.07, 0.09, 0.11):
        for xf in (0.86, 0.91, 0.80, 0.95):
            _ = audit.settings_click(xf, yf)
            for _ in range(4):
                now = _view_field(view, "inAdvancedModeForEditSettings")
                if now == want:
                    flipped = now
                    break
                time.sleep(0.4)
            if flipped:
                break
        if flipped:
            break
    if not flipped:
        recipe.fail(
            f"the Advanced switch drive never flipped inAdvancedModeForEditSettings from {adv0}"
        )
    print(
        f"Advanced switch driven: inAdvancedModeForEditSettings {adv0} -> {flipped} "
        "(control 7 applies; readback re-tracks)"
    )

    # restore advanced mode to its baseline through the same control, then leave edit mode
    if flipped != adv0:
        _restore_advanced(view, adv0)

    _ = audit.exit_editmode(view)
    print(
        "AU-6b: the CL-6 universalSettings readback answers live "
        "and the Advanced switch drives it"
    )


if __name__ == "__main__":
    recipe.run(main)
