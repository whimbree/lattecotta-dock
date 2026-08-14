# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Single-mode layout-switching helpers for the multi-layout e2e recipes.

Mechanism, verified in the tree: Corona::switchToLayout (app/lattecorona.cpp)
with a plain layout name reaches Synchronizer::switchToLayoutInSingleMode ->
initSingleMode (app/layouts/synchronizer.cpp), which swaps the loaded layout
ASYNCHRONOUSLY: a 350 ms singleShot unloads the old central layout, loads the
new file via Corona::loadLayout (file-verbatim containment and applet ids, not
an id-remapping import), then persists the new name through
UniversalSettings::setSingleModeLayoutName. The D-Bus call returns before any
of that happens, so a recipe never asserts right after the call:
``wait_for_single_active_layout`` polls the typed layoutsData readback until
the target layout is THE one loaded record, and the recipe then waits for its
views to settle (recipe.wait_settled) before asserting shapes.

Seeding rides the stop-first ordering rule (the 030/110/032 lesson recipe 111
carries): the dock's clean SIGTERM flush writes the config home, so layouts
staged under a running dock would be overwritten. ``stage_single_mode_layouts``
refuses a live dock loudly instead of racing it.
"""

from __future__ import annotations

import configparser
import contextlib
import io
import os
import sys
import time
from pathlib import Path
from typing import TYPE_CHECKING

from latte_harness import applet_reorder, recipe

if TYPE_CHECKING:
    from collections.abc import Sequence

_POLL_INTERVAL_SECONDS = 0.5


def _layout_name(fixture: Path) -> str:
    """The layout name a staged fixture carries: its filename stem.

    Synchronizer::layoutPath resolves a layout name to
    ``<config>/latte/<name>.layout.latte``, so the stem IS the switchable name.
    """
    return fixture.name.removesuffix(".layout.latte")


def write_single_mode_selection(lattedockrc: Path, active_name: str) -> None:
    """Point lattedockrc at ``active_name`` in single mode (memoryUsage=0).

    The exact seeding write recipe 111 established: RawConfigParser with case
    kept (KConfig keys are case-sensitive), rewriting only the two
    [UniversalSettings] keys the dock reads at startup to pick its layout.
    """
    parser = configparser.RawConfigParser()
    parser.optionxform = str  # pyright: ignore[reportAttributeAccessIssue] - keep KConfig key case
    parser.read(lattedockrc)
    if not parser.has_section("UniversalSettings"):
        parser.add_section("UniversalSettings")
    parser.set("UniversalSettings", "singleModeLayoutName", active_name)
    parser.set("UniversalSettings", "memoryUsage", "0")
    with lattedockrc.open("w") as output:
        parser.write(output, space_around_delimiters=False)


def read_single_mode_selection(lattedockrc: Path) -> str:
    """The persisted single-mode layout name, or "" when the key is absent.

    The persistence readback recipe 121 asserts on: after a clean stop the
    dock's config flush must have written the switched-to name here.
    """
    parser = configparser.RawConfigParser()
    parser.optionxform = str  # pyright: ignore[reportAttributeAccessIssue] - keep KConfig key case
    parser.read(lattedockrc)
    if not parser.has_section("UniversalSettings"):
        return ""
    return parser.get("UniversalSettings", "singleModeLayoutName", fallback="")


def stage_single_mode_layouts(
    config_home: Path, fixtures: Sequence[Path], active_name: str
) -> None:
    """Replace the config home's layout set with ``fixtures``, ``active_name`` active.

    Deletes every stale ``*.layout.latte`` first so the staged set is exactly
    the fixture set, then copies the fixtures in and points lattedockrc at
    ``active_name``. Refuses loudly when the vehicle dock is still running
    (its SIGTERM flush would overwrite the staged files) or when
    ``active_name`` names no staged fixture - both are recipe bugs, never
    states to proceed from.
    """
    pid = recipe.dock_pid()
    if pid is not None and recipe.pid_alive(pid):
        raise recipe.RecipeError(
            f"stage_single_mode_layouts: dock (pid {pid}) is still running; "
            "stop it first so its config flush cannot overwrite the staged layouts"
        )
    staged_names = [_layout_name(fixture) for fixture in fixtures]
    if active_name not in staged_names:
        raise recipe.RecipeError(
            f"stage_single_mode_layouts: active layout {active_name!r} "
            f"is not among the staged fixtures {staged_names}"
        )
    layouts_dir = config_home / "latte"
    layouts_dir.mkdir(parents=True, exist_ok=True)
    for stale in layouts_dir.glob("*.layout.latte"):
        stale.unlink()
    for fixture in fixtures:
        (layouts_dir / fixture.name).write_bytes(fixture.read_bytes())
    write_single_mode_selection(config_home / "lattedockrc", active_name)


def wait_for_single_active_layout(name: str, timeout: float = 60.0) -> recipe.LayoutRecord:
    """Poll layoutsData until ``name`` is THE single-mode loaded layout; return it.

    Success is the settled single-mode shape: memoryUsage "single" and exactly
    one record, named ``name`` and active. Anything else - the old layout still
    loaded, the mid-switch window with zero records, a refused read
    (DbusUnavailableError) - is "not yet". A timeout names the last observed
    reply so the failure says what the dock was actually reporting.
    """
    deadline = time.monotonic() + timeout
    last_observed = "no layoutsData reply"
    while time.monotonic() < deadline:
        try:
            data = recipe.layouts_data()
        except recipe.DbusUnavailableError as err:
            last_observed = str(err)
        else:
            record = _single_active_record(data, name)
            if record is not None:
                return record
            last_observed = (
                f"memoryUsage={data.memory_usage!r} "
                f"layouts={[(r.name, r.is_active) for r in data.layouts]}"
            )
        time.sleep(_POLL_INTERVAL_SECONDS)
    raise recipe.RecipeError(
        f"layout {name!r} never became the single active layout "
        f"within {timeout:g}s (last observed: {last_observed})"
    )


def _single_active_record(data: recipe.LayoutsData, name: str) -> recipe.LayoutRecord | None:
    """The one active record named ``name`` in a settled single-mode reply, else None."""
    if data.memory_usage != "single" or len(data.layouts) != 1:
        return None
    record = data.layouts[0]
    if record.name != name or not record.is_active:
        return None
    return record


def applet_plugin_sequence(containment_id: int) -> list[str]:
    """The view's applet PLUGIN ids in reorderable order (the layout-file order).

    Joins the viewAppletsOrder instance-id order with the typed viewAppletsData
    plugin mapping, so a recipe can assert the human-readable order two seeded
    layouts disagree on. An ordered id missing from the applets readback is a
    real disagreement between the two surfaces and surfaces loudly.

    applet_reorder_order deliberately preserves busctl's QUOTED array items
    ('"5" "4"') - its contract is an opaque before/after comparison string -
    so the join strips the quotes here, where bare instance ids are needed
    (caught on the first 122 drive: the quoted tokens matched no applet id).
    """
    order = [
        token.strip('"') for token in applet_reorder.applet_reorder_order(containment_id).split()
    ]
    plugin_by_id = {str(applet.id): applet.plugin for applet in recipe.view_applets(containment_id)}
    missing = [applet_id for applet_id in order if applet_id not in plugin_by_id]
    if missing:
        raise recipe.RecipeError(
            f"applet_plugin_sequence: ordered applet ids {missing} are missing from "
            f"viewAppletsData for containment {containment_id} (ids {sorted(plugin_by_id)})"
        )
    return [plugin_by_id[applet_id] for applet_id in order]


def stop_dock_for_cleanup() -> bool:
    """Stop the reused vehicle dock before a config restore; True when it is down.

    Recipe 111's cleanup helper, shared by the layout-switch recipes: cleanup
    stops the dock BEFORE restoring the config so the dock's SIGTERM config
    flush lands first, not on top of the restored files (the 022/034
    stop-then-restore order). Only the dock_stop() call itself is muted (its
    "already gone" chatter); a dock that SURVIVES SIGTERM is reported loudly
    and fails the cleanup - dock_stop deliberately never escalates to SIGKILL,
    and a surviving dock's eventual config flush would overwrite the restored
    files, resurrecting the leak under a PASS.
    """
    pid = recipe.dock_pid()
    if pid is None:
        return True
    try:
        os.kill(pid, 0)
    except OSError:
        return True
    with contextlib.redirect_stderr(io.StringIO()):
        stopped = recipe.dock_stop()
    if not stopped:
        print(f"FAIL: cleanup could not stop dock pid {pid}", file=sys.stderr, flush=True)
    return stopped
