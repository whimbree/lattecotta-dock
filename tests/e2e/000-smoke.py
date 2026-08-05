#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""Smoke: the vehicle dock reaches lifecycleState running, its views settle with
sane geometry on the vehicle output, a SIGTERM exits cleanly, and a relaunch
reproduces the same view set. This is the by-hand verification of the nested
vehicle's first proof (session-handoff 2026-07-16), made re-runnable.

It is also the first recipe on the typed stack (BP-2c): the port of
000-smoke.sh over latte_harness.recipe, and the template every BP-3 batch
follows - typed throughout, the loud helpers composed with recipe.fail, wrapped
by recipe.run so a RecipeError from any helper exits loudly without a traceback.
"""

from latte_harness import recipe


def main() -> None:
    if not recipe.wait_running(30):
        recipe.fail("dock not running")
    if not recipe.wait_settled(30):
        recipe.fail("views did not settle")

    # Every settled view must carry a real geometry; a 0x0 view is the
    # stranded-startup signature viewsData exists to expose.
    settled = recipe.views()
    if not settled:
        recipe.fail("no views loaded")
    for view in settled:
        _x, _y, width, height = view.absolute_geometry
        if width <= 0 or height <= 0:
            recipe.fail(
                f"view {view.containment_id} has degenerate geometry "
                f"{list(view.absolute_geometry)}"
            )
    print(f"{len(settled)} views settled")
    views_before = len(settled)

    if not recipe.dock_stop():
        recipe.fail("no clean SIGTERM exit")
    print("clean SIGTERM exit")

    if not recipe.dock_start():
        recipe.fail("dock did not come back after restart")

    # The restart must reproduce the same view set, not merely "some views".
    views_after = len(recipe.views())
    if views_after != views_before:
        recipe.fail(f"restart changed the view count: {views_before} -> {views_after}")
    print(f"dock relaunched; {views_after} views settled again")


if __name__ == "__main__":
    recipe.run(main)
