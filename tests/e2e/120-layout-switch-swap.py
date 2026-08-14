#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""Single-mode switchToLayout swaps the loaded layout and its views.

The contract: with two single-mode layouts staged (SwitchA: bottom dock,
containment 1; SwitchB: left dock, containment 2 - every discriminator
inverted, see tests/e2e/fixtures/layout-switch/), a `switchToLayout s SwitchB`
D-Bus call replaces the loaded central layout wholesale. Pull-asserted via the
typed layoutsData and viewsData readbacks, before and after the switch:
layoutsData reports memoryUsage "single" and exactly ONE record whose
name/isActive flip to the target, and the view set swaps from SwitchA's
(containment 1, bottom) to SwitchB's (containment 2, left) with the record's
viewsCount agreeing with the typed views list.

The switch is asynchronous (initSingleMode's 350 ms singleShot unloads the old
central layout and loads the new file, app/layouts/synchronizer.cpp), so every
post-switch assert sits behind wait_for_single_active_layout polling plus a
views settle - never a sleep.

First dedicated e2e coverage of the switchToLayout surface (previously zero
harness callers).
"""

import os
from pathlib import Path

from latte_harness import layout_switch, proc, recipe
from latte_harness.config_restore import ConfigHomeSnapshot


def _fixture_paths() -> tuple[Path, Path]:
    fixtures = Path(os.environ["E2E_REPO"]) / "tests" / "e2e" / "fixtures" / "layout-switch"
    switch_a = fixtures / "SwitchA.layout.latte"
    switch_b = fixtures / "SwitchB.layout.latte"
    for fixture in (switch_a, switch_b):
        if not fixture.is_file():
            recipe.fail(f"layout-switch fixture is missing: {fixture}")
    return switch_a, switch_b


def _seed_and_start_on_switch_a() -> None:
    """Stage SwitchA+SwitchB with SwitchA active and bring the dock up on them.

    Stop FIRST (the 030/110/032 ordering rule): the running dock's SIGTERM
    flush must land before the layout set is replaced, not on top of it.
    """
    config_home = Path(os.environ["E2E_CONFIG_HOME"])
    switch_a, switch_b = _fixture_paths()
    if not recipe.dock_stop():
        recipe.fail("could not stop the vehicle dock before staging the switch layouts")
    layout_switch.stage_single_mode_layouts(config_home, [switch_a, switch_b], "SwitchA")
    if not recipe.dock_start(90):
        recipe.fail("dock never settled on the seeded SwitchA layout")


def _assert_loaded_layout_shape(name: str, containment_id: int, edge: str, label: str) -> None:
    """Assert ``name`` is THE loaded single-mode layout and its view set matches.

    Waits for the layout record first (the switch is asynchronous), then for
    the views to settle, then asserts the whole shape: exactly one view with
    the layout's containment id and edge, and a layoutsData record whose
    viewsCount agrees with the typed views list.
    """
    layout_switch.wait_for_single_active_layout(name, 60)
    if not recipe.wait_settled(60):
        recipe.fail(f"{label}: views never settled on layout {name}")
    views = recipe.views()
    observed = [(view.containment_id, view.edge) for view in views]
    if observed != [(containment_id, edge)]:
        recipe.fail(
            f"{label}: expected exactly one view ({containment_id}, {edge!r}) "
            f"for layout {name}, observed {observed}"
        )
    record = layout_switch.wait_for_single_active_layout(name, 10)
    if record.views_count != len(views):
        recipe.fail(
            f"{label}: layoutsData viewsCount={record.views_count} disagrees with "
            f"the {len(views)} typed viewsData records for layout {name}"
        )


def _body() -> None:
    _seed_and_start_on_switch_a()
    _assert_loaded_layout_shape("SwitchA", 1, "bottom", "seeded baseline")
    print("BASELINE ok: SwitchA is the single active layout (view 1, bottom)")

    recipe.call_or_fail("switchToLayout SwitchB was refused", "switchToLayout", "s", "SwitchB")
    _assert_loaded_layout_shape("SwitchB", 2, "left", "after the switch")
    print("SWITCH ok: SwitchB is the single active layout (view 2, left)")

    print("PASS: single-mode switchToLayout swapped the loaded layout and its views")


def main() -> None:
    # Cleanup runs on every exit path: the recipe replaces the SHARED throwaway
    # config home's layout set and lattedockrc selection, so an un-restored
    # mutation would strand the switch fixtures into the next recipe. Stop the
    # dock before restoring (its SIGTERM flush must not overwrite the restored
    # files); a failed stop or a non-byte-identical restore worsens a would-be
    # success (the 022 cleanup-status contract).
    proc.install_conventional_signal_exits()
    config_home = Path(os.environ["E2E_CONFIG_HOME"])
    snapshot = ConfigHomeSnapshot()
    snapshot.snapshot_file(config_home / "lattedockrc")
    snapshot.snapshot_dir(config_home / "latte")

    def cleanup(status: int) -> int:
        dock_stopped = layout_switch.stop_dock_for_cleanup()
        restored = snapshot.restore()
        return recipe.worsen_status_on_cleanup_failure(status, not (dock_stopped and restored))

    recipe.run_with_cleanup(_body, cleanup, install_signal_exits=False)


if __name__ == "__main__":
    main()
