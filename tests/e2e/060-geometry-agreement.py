#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""E2E: the dock renders where it says it does. Every locatable horizontal
view's layer-shell surface must sit at the origin viewsData reports - a
state-vs-render AGREEMENT check, the one bug class the rest of the suite's
D-Bus assertions are blind to by construction (viewsData stays
self-consistent even when the compositor draws the surface elsewhere).

Standing guard since the Phase 8 bottom-dock surface drift was fixed: a
masked dock's window spans the full screen length, so its layer surface now
anchors both length edges instead of leaning on the compositor to centre it
inside the region other docks' exclusive zones leave free (root cause and
fix: app/wm/waylandlayershell.cpp anchorsFor, ledger in
docs/agent-logs/2026-07-17-phase8-surface-drift.md). Any future state/render
divergence reds the suite here.
"""

from latte_harness import recipe


def main() -> None:
    if not recipe.wait_settled(45):
        recipe.fail("vehicle dock never settled")

    # 2px tolerance absorbs sub-pixel rounding; the Phase 8 drift is an order of
    # magnitude larger, so this is not a clamp hiding the bug - it is the width
    # of honest rounding noise and no wider.
    if not recipe.assert_geometry_agrees(2):
        recipe.fail("a view renders off its reported geometry (state/render divergence)")

    print("PASS: geometry-agreement")


if __name__ == "__main__":
    recipe.run(main)
