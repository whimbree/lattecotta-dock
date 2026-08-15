#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""Per-layout applet order survives a layout-switch round-trip.

The contract: each layout's applet order is its own - switching away from a
layout (which syncs its runtime state back to its file on unload) and back
(which reloads that file) reproduces the order exactly. The two fixtures seed
OPPOSITE plugin orders (SwitchA appletOrder=2;3 = plasmoid then analogclock;
SwitchB appletOrder=5;4 = analogclock then plasmoid), so the mid-trip
assertion on SwitchB is the non-vacuity control: a readback blind to the
seeded appletOrder cannot show the reversal, and a switch that leaked one
layout's order into the other would collapse the difference.

Pull-asserted via viewAppletsOrder (the stable applet-instance-id order) joined
with the typed viewAppletsData plugin mapping: on the return to SwitchA both
the instance-id order string and the plugin sequence must be identical to the
pre-trip reading (Corona::loadLayout loads layout files id-verbatim, so the
instance ids are part of the contract, not an implementation accident).

Fixtures and discriminators: tests/e2e/fixtures/layout-switch/ (SwitchA bottom
dock containment 1, SwitchB left dock containment 2).
"""

import os
from pathlib import Path

from latte_harness import applet_reorder, layout_switch, proc, recipe
from latte_harness.config_restore import ConfigHomeSnapshot

_SWITCH_A_PLUGINS = ["org.kde.latte.plasmoid", "org.kde.plasma.analogclock"]
_SWITCH_B_PLUGINS = ["org.kde.plasma.analogclock", "org.kde.latte.plasmoid"]


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


def _switch_and_settle(name: str) -> None:
    """Drive switchToLayout and wait until ``name`` is loaded with settled views."""
    recipe.call_or_fail(f"switchToLayout {name} was refused", "switchToLayout", "s", name)
    layout_switch.wait_for_single_active_layout(name, 60)
    if not recipe.wait_settled(60):
        recipe.fail(f"views never settled after the switch to {name}")


def _assert_plugin_sequence(containment_id: int, expected: list[str], label: str) -> None:
    observed = layout_switch.applet_plugin_sequence(containment_id)
    if observed != expected:
        recipe.fail(f"{label}: expected plugin order {expected}, observed {observed}")


def _body() -> None:
    _seed_and_start_on_switch_a()
    layout_switch.wait_for_single_active_layout("SwitchA", 60)
    if not recipe.wait_settled(60):
        recipe.fail("views never settled on the seeded SwitchA layout")

    order_before = applet_reorder.applet_reorder_order(1)
    _assert_plugin_sequence(1, _SWITCH_A_PLUGINS, "seeded SwitchA baseline")
    print(f"BASELINE ok: SwitchA applet order is '{order_before}' ({_SWITCH_A_PLUGINS})")

    _switch_and_settle("SwitchB")
    # The non-vacuity control: SwitchB's seeded order is the REVERSE plugin
    # sequence, so the readback provably reflects the per-layout appletOrder
    # and SwitchA's order did not leak across the switch.
    _assert_plugin_sequence(2, _SWITCH_B_PLUGINS, "SwitchB mid-trip control")
    print(f"CONTROL ok: SwitchB applet order is reversed ({_SWITCH_B_PLUGINS})")

    _switch_and_settle("SwitchA")
    order_after = applet_reorder.applet_reorder_order(1)
    if order_after != order_before:
        recipe.fail(
            f"SwitchA's applet-instance-id order did not survive the round-trip: "
            f"'{order_before}' before, '{order_after}' after"
        )
    _assert_plugin_sequence(1, _SWITCH_A_PLUGINS, "SwitchA after the round-trip")

    print("PASS: per-layout applet order survived the layout-switch round-trip")


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
