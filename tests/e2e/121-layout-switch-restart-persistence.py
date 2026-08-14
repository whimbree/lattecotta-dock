#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""A single-mode layout switch persists across a dock restart.

The contract: after `switchToLayout s SwitchB` completes, the switched-to
layout is the one the NEXT dock start comes back on. The persistence mechanism
is UniversalSettings::setSingleModeLayoutName, which initSingleMode's async
completion writes (app/layouts/synchronizer.cpp) and startup reads to pick the
single-mode layout. Asserted at both layers: after a clean SIGTERM stop (read
once the process is gone, the last possible flush point; saveConfig itself
already ends in syncSettings, so the rc write lands at switch time and the
post-exit read simply removes any ordering question), lattedockrc carries
singleModeLayoutName=SwitchB; after the
restart, the typed layoutsData readback reports SwitchB as the one active
record and the view set is SwitchB's (containment 2, left) - the switched-to
layout came back without any external reselection.

Fixtures and discriminators: tests/e2e/fixtures/layout-switch/ (SwitchA bottom
dock containment 1, SwitchB left dock containment 2).
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
    """Stage SwitchA+SwitchB with SwitchA active and bring the dock up on them."""
    config_home = Path(os.environ["E2E_CONFIG_HOME"])
    switch_a, switch_b = _fixture_paths()
    if not recipe.dock_stop():
        recipe.fail("could not stop the vehicle dock before staging the switch layouts")
    layout_switch.stage_single_mode_layouts(config_home, [switch_a, switch_b], "SwitchA")
    if not recipe.dock_start(90):
        recipe.fail("dock never settled on the seeded SwitchA layout")


def _body() -> None:
    config_home = Path(os.environ["E2E_CONFIG_HOME"])
    _seed_and_start_on_switch_a()
    layout_switch.wait_for_single_active_layout("SwitchA", 60)
    print("BASELINE ok: SwitchA is the single active layout")

    recipe.call_or_fail("switchToLayout SwitchB was refused", "switchToLayout", "s", "SwitchB")
    layout_switch.wait_for_single_active_layout("SwitchB", 60)
    if not recipe.wait_settled(60):
        recipe.fail("views never settled after the switch to SwitchB")
    print("SWITCH ok: SwitchB is the single active layout")

    # The restart, with the persistence readback at the flush point in between:
    # a dock surviving SIGTERM here is itself a shutdown defect this recipe
    # must surface, so the stop is asserted, not retried.
    if not recipe.dock_stop():
        recipe.fail("dock did not shut down cleanly after the layout switch")
    persisted = layout_switch.read_single_mode_selection(config_home / "lattedockrc")
    if persisted != "SwitchB":
        recipe.fail(
            f"singleModeLayoutName did not follow the switch: expected 'SwitchB', "
            f"lattedockrc carries {persisted!r}"
        )
    print("PERSISTENCE ok: lattedockrc singleModeLayoutName followed the switch to SwitchB")

    if not recipe.dock_start(90):
        recipe.fail("dock never settled after the post-switch restart")
    layout_switch.wait_for_single_active_layout("SwitchB", 60)
    views = recipe.views()
    observed = [(view.containment_id, view.edge) for view in views]
    if observed != [(2, "left")]:
        recipe.fail(
            f"the restarted dock did not come back on SwitchB's view set: "
            f"expected [(2, 'left')], observed {observed}"
        )

    print("PASS: the single-mode layout switch persisted across a dock restart")


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
