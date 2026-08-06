#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
"""Self-test for D150 (hovered applet row escaped its resting background), on the
typed presentation-coverage oracle.

The controlled bad payload is the live regression shape: applets reach both
beyond a background capped to the resting maximum, while the complete row still
fits the output. This drives the payload-level oracle
(recipe._assert_presentation_coverage, the twin of lib.sh's
_e2e_assert_presentation_payloads) with crafted dockSystemData / viewAppletsData
snapshots, so the failure detector is exercised without a live dock - exactly
what made it independently self-testable in the bash.

This is the BP-3 R11 port of presentation-coverage-selftest.sh over
latte_harness.recipe: same fixtures, same acceptance of the complete-background
shape, same rejection of the D150 escaped-row shape, byte-identical PASS line
and negative-control diagnostics.
"""

from __future__ import annotations

from latte_harness import recipe
from latte_harness.recipe import Applet, DockSystemData

# The two live surfaces the oracle joins, as dockSystemData reports them: the
# valid fixture caps the background to the resting maximum with the applets
# inside it, the bad fixture keeps the same applets but shrinks the painted
# background so the row escapes both ends (the D150 shape).
_VIEWS_GOOD: dict[str, object] = {
    "persistentDockId": 14,
    "orientation": "horizontal",
    "isHidden": False,
    "effectsRect": [20, 220, 2520, 148],
    "canvasGeometry": [1440, 1643, 2560, 222],
}
_VIEWS_BAD: dict[str, object] = {
    "persistentDockId": 14,
    "orientation": "horizontal",
    "isHidden": False,
    "effectsRect": [225, 220, 2110, 148],
    "canvasGeometry": [1440, 1643, 2560, 222],
}
# The applet rectangles as viewAppletsData reports them. The oracle reads only
# inScheduledDestruction and geometry; id/plugin are synthesized to satisfy the
# typed Applet model (the bash raw-dict fixtures omitted them, and the assertion
# is identical either way - the oracle never looks at them).
_APPLET_GEOMETRIES = [
    [54, 241, 165, 106],
    [235, 220, 1906, 164],
    [2392, 241, 107, 106],
]


def _snapshot(view: dict[str, object]) -> DockSystemData:
    return DockSystemData.model_validate({"views": [view]})


def _applets() -> list[Applet]:
    return [
        Applet.model_validate(
            {"id": i, "plugin": "p", "geometry": geometry, "inScheduledDestruction": False}
        )
        for i, geometry in enumerate(_APPLET_GEOMETRIES)
    ]


def main() -> None:
    applets = _applets()

    # The valid complete-background fixture must be accepted (the oracle returns
    # the coverage line); a rejection here means the detector is over-eager.
    try:
        line = recipe._assert_presentation_coverage(_snapshot(_VIEWS_GOOD), applets, 14, 2)
    except recipe.RecipeError:
        recipe.fail("the valid complete-background fixture was rejected")
    print(line)

    # The D150 fixture must be rejected; capture the diagnostic the oracle raised
    # and assert it names both escaped ends, exactly as the bash grepped its log.
    try:
        recipe._assert_presentation_coverage(_snapshot(_VIEWS_BAD), applets, 14, 2)
    except recipe.RecipeError as err:
        message = str(err)
    else:
        recipe.fail("the D150 background/content separation fixture was not rejected")

    if "content starts at 54, before background 225" not in message:
        recipe.fail("the negative control failed for an unexpected reason")
    if "content ends at 2499, after background 2335" not in message:
        recipe.fail("the negative control did not observe the escaped tail")

    print("PASS: presentation coverage accepts complete chrome and rejects the D150 escaped-row shape")


if __name__ == "__main__":
    recipe.run(main)
