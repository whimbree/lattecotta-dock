#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""Prove that both floating Panels and floating Docks consume live KWin frame
geometry during one real button-held titlebar drag. Each case crosses the
per-view stable envelope and reverses before button release while the QWindow,
reservation, layer-shell publication, and tracker authority remain stable.

Ported from tests/e2e/074-live-titlebar-window-touch.sh to latte_harness.recipe
and latte_harness.matrix (BP-3, the bash-to-python migration's window-touch
recipe batch R6). dockSystemData carries the whole live-presentation surface
(dozens of fields no typed model models), so the snapshot is read as raw JSON
via recipe.read_json at the same boundary the bash python one-liners used; a
refused reply raises the pollable DbusUnavailableError, the same
empty-command-substitution channel every bash poller swallowed (the
dock-edit-retarget-cancel precedent). The stable-contract comparison is
byte-for-byte the bash json.dumps(sort_keys=True). The bash
compared %.9f-formatted progress endpoints as strings; the port keeps that exact
rounding semantics through the same formatting. The held-drag liveness check
maps the bash `kill -0 $drag_pid` onto Popen.poll().
"""

from __future__ import annotations

import json
import math
import os
import shutil
import signal
import subprocess
import sys
import time
from collections import Counter
from contextlib import redirect_stderr, redirect_stdout, suppress
from io import StringIO
from typing import Any

from latte_harness import matrix, recipe

_TITLE = "LATTE LIVE TITLEBAR TOUCH"
_KONSOLE_MATCH = f"|org.kde.konsole|{_TITLE}"

# The drag-choreography samplers must OUTLIVE the draghold's scheduled events
# (two 2 s dwells plus three glide legs, ~5 s total): the bash 100-iteration
# loops got that horizon implicitly from ~100 ms python-one-liner probes
# (~10 s wall), while this port's bare-busctl probes cost ~3 ms, so the same
# iteration count spans ~1.3 s and expires before the outward crossing even
# begins (measured; the port's first drive died exactly there). The sampling
# cadence stays the bash 0.01 s; the budget restores the bash wall-clock span.
_HELD_SAMPLER_BUDGET_SECONDS = 10.0


class _State:
    def __init__(self) -> None:
        self.view = 0
        self.configured = False
        self.konsole: subprocess.Popen[bytes] | None = None
        self.drag: subprocess.Popen[bytes] | None = None


_S = _State()


# ---- transport helpers -----------------------------------------------------


def _fp(*args: str) -> int:
    return recipe.fakepointer(*args)


def _fp_or_fail(fail_message: str, *args: str) -> None:
    if _fp(*args) != 0:
        recipe.fail(fail_message)


def _kwrite(fail_message: str, *args: str) -> None:
    recipe.kwriteconfig_or_fail(fail_message, *args)


def _dock_record() -> dict[str, Any]:
    """dock_field's context: the single dockSystemData record for the view.

    schema-11 and exactly-one guards mirror the bash; a refused/failed reply
    raises recipe.read_json's pollable DbusUnavailableError (the bash empty
    command substitution).
    """
    snapshot = recipe.read_json("dockSystemData")
    if snapshot["schemaVersion"] != 11:
        raise recipe.RecipeError("expected dockSystemData schema 11")
    matches = [r for r in snapshot["views"] if r["persistentDockId"] == _S.view]
    if len(matches) != 1:
        raise recipe.RecipeError(f"expected exactly one dockSystemData record for view {_S.view}")
    return matches[0]


def _dock_snapshot_and_record() -> tuple[dict[str, Any], dict[str, Any]]:
    """The whole dockSystemData snapshot plus the view record (for the fields the
    bash read off ``snapshot`` alongside ``v``)."""
    snapshot = recipe.read_json("dockSystemData")
    if snapshot["schemaVersion"] != 11:
        raise recipe.RecipeError("expected dockSystemData schema 11")
    matches = [r for r in snapshot["views"] if r["persistentDockId"] == _S.view]
    if len(matches) != 1:
        raise recipe.RecipeError(f"expected exactly one dockSystemData record for view {_S.view}")
    return snapshot, matches[0]


def _view_config() -> dict[str, Any]:
    """view_config_field's context: viewConfigData's config subtree (raw JSON)."""
    return recipe.read_json("viewConfigData", "u", str(_S.view))["config"]


def _lower(value: bool) -> str:
    return "true" if value else "false"


def _drag_held() -> bool:
    """The bash ``kill -0 $drag_pid``: the owned draghold is still running."""
    return _S.drag is not None and _S.drag.poll() is None


# ---- snapshot / contract probes --------------------------------------------


def _stable_physical_snapshot() -> str:
    snapshot, v = _dock_snapshot_and_record()
    return json.dumps(
        {
            "reservationStateGeneration": snapshot["reservationStateGeneration"],
            "windowGeometry": v["windowGeometry"],
            "absoluteGeometry": v["absoluteGeometry"],
            "localGeometry": v["localGeometry"],
            "surfaceGeometry": v["surfaceGeometry"],
            "canvasGeometry": v["canvasGeometry"],
            "stableTriggerGeometry": v["stableTriggerGeometry"],
            "screenEdgeMargin": v["screenEdgeMargin"],
            "normalThickness": v["normalThickness"],
            "maximumNormalThickness": v["maximumNormalThickness"],
            "reservationContributionDepth": v["reservationContributionDepth"],
            "reservationPublishedDepth": v["reservationPublishedDepth"],
            "reservationOutputId": v["reservationOutputId"],
            "reservationEdge": v["reservationEdge"],
            "reservationGroupGeneration": v["reservationGroupGeneration"],
            "reservationContributorDockIds": v["reservationContributorDockIds"],
            "reservationGeometry": v["reservationGeometry"],
            "layerShellMargins": v["layerShellMargins"],
            "layerShellAnchors": v["layerShellAnchors"],
            "layerShellExclusiveEdge": v["layerShellExclusiveEdge"],
            "layerShellExclusiveZone": v["layerShellExclusiveZone"],
            "publishedStruts": v["publishedStruts"],
            "surfaceGeometryPublicationRevision": v["surfaceGeometryPublicationRevision"],
            "layerShellConfigureRequestRevision": v["layerShellConfigureRequestRevision"],
            "windowTouchTracker": v["objects"]["windowTouchTracker"],
            "configuredIconSize": v["configuredIconSize"],
            "effectiveIconSize": v["effectiveIconSize"],
            "availablePrimaryLength": v["availablePrimaryLength"],
        },
        sort_keys=True,
        separators=(",", ":"),
    )


def _configured_length_independent_snapshot() -> str:
    v = _dock_record()
    return json.dumps(
        {
            "windowGeometry": v["windowGeometry"],
            "surfaceGeometry": v["surfaceGeometry"],
            "canvasGeometry": v["canvasGeometry"],
            "screenGeometry": v["screenGeometry"],
            "configuredIconSize": v["configuredIconSize"],
            "effectiveIconSize": v["effectiveIconSize"],
            "screenEdgeMargin": v["screenEdgeMargin"],
            "reservationContributionDepth": v["reservationContributionDepth"],
            "reservationPublishedDepth": v["reservationPublishedDepth"],
            "layerShellMargins": v["layerShellMargins"],
            "layerShellAnchors": v["layerShellAnchors"],
            "layerShellExclusiveEdge": v["layerShellExclusiveEdge"],
            "layerShellExclusiveZone": v["layerShellExclusiveZone"],
        },
        sort_keys=True,
        separators=(",", ":"),
    )


def _assert_stable_physical_snapshot(boundary: str, expected: str) -> None:
    try:
        actual = _stable_physical_snapshot()
    except recipe.RecipeError, KeyError, IndexError:
        recipe.fail(f"could not read the {boundary} physical contract")
    if actual != expected:
        recipe.fail(
            f"{boundary} changed stable physical state (expected={expected} actual={actual})"
        )


def _policy_probe() -> tuple[str, int, str, str, str, float, str, int, int]:
    v = _dock_record()
    return (
        v["type"],
        v["touchingWindowCount"],
        _lower(v["dockGapHideRequested"]),
        v["transitionTarget"],
        v["transitionPhase"],
        v["transitionProgress"],
        v["windowTouchGeometryRoleType"],
        v["screenEdgeMargin"],
        v["presentedScreenEdgeGap"],
    )


def _presented_gap_matches_progress(progress: float, configured: int, presented: int) -> bool:
    return presented == math.floor(configured * progress + 0.5)


def _dock_length_matches_progress(
    progress: float, configured_ratio: float, presented_length: int, output_length: int
) -> bool:
    expected = output_length * (configured_ratio + (1.0 - configured_ratio) * (1.0 - progress))
    return math.isclose(presented_length, expected, abs_tol=2.0)


def _presentation_probe() -> tuple[float, float, int, int, int, int, str]:
    v = _dock_record()
    return (
        v["transitionProgress"],
        v["maximumLengthRatio"],
        v["windowGeometry"][0] + v["effectsRect"][0],
        v["effectsRect"][2],
        v["screenGeometry"][0],
        v["screenGeometry"][2],
        ",".join(sorted(v["enabledBorders"])),
    )


def _fractional_presentation_probe() -> tuple[
    str, int, str, str, str, float, str, int, int, float, int, int
]:
    v = _dock_record()
    return (
        v["type"],
        v["touchingWindowCount"],
        _lower(v["dockGapHideRequested"]),
        v["transitionTarget"],
        v["transitionPhase"],
        v["transitionProgress"],
        v["windowTouchGeometryRoleType"],
        v["screenEdgeMargin"],
        v["presentedScreenEdgeGap"],
        v["maximumLengthRatio"],
        v["effectsRect"][2],
        v["screenGeometry"][2],
    )


def _wait_for_dock_attached_presentation_while_held(require_held: bool = True) -> None:
    progress = -1.0
    configured_ratio = -1.0
    presented_x = presented_length = output_x = output_length = -1
    borders = "unread"
    deadline = time.monotonic() + _HELD_SAMPLER_BUDGET_SECONDS
    while time.monotonic() < deadline:
        try:
            (
                progress,
                configured_ratio,
                presented_x,
                presented_length,
                output_x,
                output_length,
                borders,
            ) = _presentation_probe()
        except recipe.RecipeError:
            time.sleep(0.01)
            continue
        if (
            f"{progress:.9f}" == "0.000000000"
            and presented_x == output_x
            and presented_length == output_length
            and borders == "bottom"
        ):
            if require_held:
                if _S.drag is None:
                    recipe.fail("attached Dock presentation has no held drag")
                if not _drag_held():
                    recipe.fail("Dock reached full span only after button release")
            return
        time.sleep(0.01)
    recipe.fail(
        f"Dock did not reach the full attached span with only the inward border "
        f"(progress={progress} configuredRatio={configured_ratio} "
        f"presentation={presented_x}+{presented_length} output={output_x}+{output_length} "
        f"borders={borders})"
    )


def _wait_for_dock_floated_presentation(expected_x: int, expected_length: int) -> None:
    progress = -1.0
    configured_ratio = -1.0
    presented_x = presented_length = -1
    borders = "unread"
    deadline = time.monotonic() + _HELD_SAMPLER_BUDGET_SECONDS
    while time.monotonic() < deadline:
        try:
            (
                progress,
                configured_ratio,
                presented_x,
                presented_length,
                _output_x,
                _output_length,
                borders,
            ) = _presentation_probe()
        except recipe.RecipeError:
            time.sleep(0.01)
            continue
        if (
            f"{progress:.9f}" == "1.000000000"
            and presented_x == expected_x
            and presented_length == expected_length
            and borders == "bottom,left,right,top"
        ):
            return
        time.sleep(0.01)
    recipe.fail(
        f"Dock did not restore its configured floated span and corners "
        f"(progress={progress} configuredRatio={configured_ratio} "
        f"presentation={presented_x}+{presented_length} expected={expected_x}+{expected_length} "
        f"borders={borders})"
    )


def _wait_for_partial_panel_attached_presentation(expected_x: int, expected_length: int) -> None:
    progress = -1.0
    presented_x = presented_length = output_x = output_length = -1
    borders = "unread"
    deadline = time.monotonic() + _HELD_SAMPLER_BUDGET_SECONDS
    while time.monotonic() < deadline:
        try:
            (
                progress,
                _configured_ratio,
                presented_x,
                presented_length,
                output_x,
                output_length,
                borders,
            ) = _presentation_probe()
        except recipe.RecipeError:
            time.sleep(0.01)
            continue
        if (
            f"{progress:.9f}" == "0.000000000"
            and (presented_x, presented_length) == (expected_x, expected_length)
            and presented_length < output_length
            and borders == "bottom,left,right"
        ):
            if _S.drag is None:
                recipe.fail("attached partial Panel has no held drag")
            if not _drag_held():
                recipe.fail("partial Panel border endpoint appeared only after button release")
            return
        time.sleep(0.01)
    recipe.fail(
        f"partial Panel did not keep both primary-axis end borders at attachment "
        f"(progress={progress} presentation={presented_x}+{presented_length} "
        f"expected={expected_x}+{expected_length} output={output_x}+{output_length} "
        f"borders={borders})"
    )


def _assert_partial_dock_presentation(boundary: str, expected_x: int, expected_length: int) -> None:
    (
        progress,
        configured_ratio,
        presented_x,
        presented_length,
        output_x,
        output_length,
        borders,
    ) = _presentation_probe()
    if not (
        presented_x == expected_x
        and presented_length == expected_length
        and presented_length < output_length
        and borders == "bottom,left,right,top"
    ):
        recipe.fail(
            f"{boundary} changed a partial Dock's primary presentation "
            f"(progress={progress} configuredRatio={configured_ratio} "
            f"presentation={presented_x}+{presented_length} "
            f"expected={expected_x}+{expected_length} output={output_x}+{output_length} "
            f"borders={borders})"
        )


def _wait_for_policy_while_held(
    expected_type: str,
    expected_count: int,
    expected_request: str,
    expected_target: str,
    boundary: str,
    require_held: bool = True,
) -> None:
    actual_type = "unread"
    count = -1
    request = target = phase = "unread"
    progress = -1.0
    role = "unread"
    configured_gap = presented_gap = -1
    deadline = time.monotonic() + _HELD_SAMPLER_BUDGET_SECONDS
    while time.monotonic() < deadline:
        try:
            (
                actual_type,
                count,
                request,
                target,
                phase,
                progress,
                role,
                configured_gap,
                presented_gap,
            ) = _policy_probe()
        except recipe.RecipeError:
            time.sleep(0.01)
            continue
        if (
            actual_type == expected_type
            and count == expected_count
            and request == expected_request
            and target == expected_target
            and role == "QRect"
            and _presented_gap_matches_progress(progress, configured_gap, presented_gap)
        ):
            if require_held:
                if _S.drag is None:
                    recipe.fail(f"{boundary} has no owned held-drag process")
                if not _drag_held():
                    recipe.fail(f"{boundary} appeared only after button release")
            return
        time.sleep(0.01)
    recipe.fail(
        f"{boundary} did not appear during the held drag (type={actual_type} count={count} "
        f"request={request} target={target} phase={phase} progress={progress} "
        f"configuredGap={configured_gap} presentedGap={presented_gap} role={role})"
    )


def _wait_for_fractional_progress_while_held(
    expected_type: str, expected_phase: str, boundary: str, expect_dock_expansion: bool = False
) -> None:
    actual_type = "unread"
    count = -1
    request = target = phase = "unread"
    progress = -1.0
    role = "unread"
    configured_gap = presented_gap = -1
    deadline = time.monotonic() + _HELD_SAMPLER_BUDGET_SECONDS
    while time.monotonic() < deadline:
        try:
            (
                actual_type,
                count,
                request,
                target,
                phase,
                progress,
                role,
                configured_gap,
                presented_gap,
                configured_ratio,
                presented_length,
                output_length,
            ) = _fractional_presentation_probe()
        except recipe.RecipeError:
            time.sleep(0.01)
            continue
        if (
            actual_type == expected_type
            and phase == expected_phase
            and role == "QRect"
            and _presented_gap_matches_progress(progress, configured_gap, presented_gap)
            and 0.0 < progress < 1.0
        ):
            if (
                expected_type == "dock"
                and expect_dock_expansion
                and not _dock_length_matches_progress(
                    progress, configured_ratio, presented_length, output_length
                )
            ):
                # The bash `|| continue` skipped the sleep: resample the
                # short-lived expansion frame immediately.
                continue
            if _S.drag is None:
                recipe.fail(f"{boundary} has no owned held-drag process")
            if not _drag_held():
                recipe.fail(f"{boundary} appeared only after button release")
            return
        time.sleep(0.01)
    recipe.fail(
        f"{boundary} exposed no fractional transition frame (type={actual_type} count={count} "
        f"request={request} target={target} phase={phase} progress={progress} "
        f"configuredGap={configured_gap} presentedGap={presented_gap} role={role})"
    )


# ---- konsole fixture helpers -----------------------------------------------


def _konsole_count() -> int:
    return sum(1 for line in recipe.dumpwins().splitlines() if _KONSOLE_MATCH in line)


def _konsole_geometry() -> tuple[int, int, int, int] | None:
    rows = [line for line in recipe.dumpwins().splitlines() if _KONSOLE_MATCH in line]
    if not rows:
        return None
    window = recipe.parse_dumpwins(rows[-1])
    if not window:
        return None
    w = window[0]
    return w.x, w.y, w.width, w.height


def _kwin_tagged_last(body: str, collection_delay: float = 0.5) -> str:
    lines = recipe.kwin_js(body, collection_delay).splitlines()
    return lines[-1] if lines else ""


def _move_konsole(x: int, y: int, width: int, height: int) -> str:
    return _kwin_tagged_last(
        f"""for (const window of workspace.windowList()) {{
        if (window.resourceClass === 'org.kde.konsole'
                && window.caption.includes('{_TITLE}')) {{
            window.setMaximize(false, false);
            const geometry = Object.assign({{}}, window.frameGeometry);
            geometry.x = {x};
            geometry.y = {y};
            geometry.width = {width};
            geometry.height = {height};
            window.frameGeometry = geometry;
            workspace.activeWindow = window;
            print('@TAG@|' + window.internalId);
        }}
    }}"""
    )


def _set_konsole_maximized(enabled: bool) -> str:
    return _kwin_tagged_last(
        f"""for (const window of workspace.windowList()) {{
        if (window.resourceClass === 'org.kde.konsole'
                && window.caption.includes('{_TITLE}')) {{
            workspace.activeWindow = window;
            window.setMaximize({_lower(enabled)}, {_lower(enabled)});
            print('@TAG@|' + window.internalId);
        }}
    }}"""
    )


def _wait_for_konsole_geometry(
    expected_x: int, expected_y: int, expected_width: int, expected_height: int
) -> None:
    actual: tuple[int, int, int, int] | None = None
    for _ in range(60):
        actual = _konsole_geometry()
        if actual == (expected_x, expected_y, expected_width, expected_height):
            return
        time.sleep(0.05)
    got = actual if actual is not None else ("unread", "unread", "unread", "unread")
    recipe.fail(
        f"KWin did not place the titlebar client at {expected_x},{expected_y} "
        f"{expected_width}x{expected_height} (actual={got[0]},{got[1]} {got[2]}x{got[3]})"
    )


def _stop_owned_konsole() -> None:
    if _S.konsole is None:
        return
    with suppress(ProcessLookupError):
        os.kill(_S.konsole.pid, signal.SIGTERM)
    with suppress(Exception):
        _S.konsole.wait()
    _S.konsole = None
    for _ in range(40):
        if _konsole_count() == 0:
            return
        time.sleep(0.05)
    recipe.fail("the owned titlebar client remained mapped after destruction")


# ---- fixture configuration -------------------------------------------------


def _configure_case(cell: str) -> None:
    if matrix.stage(cell) != 0:
        recipe.fail(f"could not realize {cell}")
    try:
        _S.view = matrix.view_id()
    except matrix.MatrixProbeError:
        recipe.fail(f"could not resolve {cell}")
    layout = os.environ["E2E_LAYOUT"]
    group = (
        "--file",
        layout,
        "--group",
        "Containments",
        "--group",
        str(_S.view),
        "--group",
        "General",
    )

    if not recipe.dock_stop():
        recipe.fail(f"dock did not stop before configuring {cell}")
    _kwrite(
        f"could not set the partial primary span for {cell}", *group, "--key", "maxLength", "60"
    )
    _kwrite(
        f"could not keep the partial Panel span static for {cell}",
        *group,
        "--key",
        "minLength",
        "60",
    )
    if cell.startswith("dock-"):
        _kwrite(
            f"could not disable automatic sizing for {cell}",
            *group,
            "--key",
            "autoSizeEnabled",
            "false",
        )
        _kwrite(
            f"could not retain Dock presentation for {cell}",
            *group,
            "--key",
            "backgroundRadius",
            "50",
        )
        _kwrite(
            f"could not enable live maximize-length presentation for {cell}",
            *group,
            "--key",
            "maximizeWhenMaximized",
            "true",
        )
    else:
        _kwrite(
            f"could not keep the partial Panel span stable for {cell}",
            *group,
            "--key",
            "maximizeWhenMaximized",
            "false",
        )
    _kwrite(
        f"could not enable live attachment for {cell}",
        *group,
        "--key",
        "hideFloatingGapForMaximized",
        "true",
    )
    _kwrite(
        f"could not disable the independent pointer-deferral policy for {cell}",
        *group,
        "--key",
        "floatingGapHidingWaitsMouse",
        "false",
    )
    _kwrite(
        f"could not configure the floating gap for {cell}",
        *group,
        "--key",
        "screenEdgeMargin",
        "18",
    )
    _kwrite(
        f"could not retain one floating surface for {cell}",
        *group,
        "--key",
        "floatingInternalGapIsForced",
        "false",
    )
    if not recipe.dock_start(90):
        recipe.fail(f"dock did not restart for {cell}")
    recipe.call_or_fail(
        f"could not set {cell} to alwaysVisible",
        "setViewVisibilityMode",
        "us",
        str(_S.view),
        "alwaysVisible",
    )

    for _ in range(40):
        with suppress(recipe.RecipeError):
            if _dock_record()["visibilityMode"] == "alwaysVisible":
                return
        time.sleep(0.05)
    recipe.fail(f"{cell} did not enter alwaysVisible mode")


# ---- the held-drag exercise ------------------------------------------------


def _exercise_held_drag(
    expected_type: str,
    expected_panel: str,
    expected_request: str,
    expected_target: str,
    expect_dock_expansion: bool = False,
) -> None:
    v = _dock_record()
    view_type = v["type"]
    panel = _lower(v["floatingPanelConfigured"])
    geometry_present = _lower(v["transitionGeometryPresent"])
    edge = v["edge"]
    floating_gap = _lower(v["floatingGapConfigured"])
    gap = v["screenEdgeMargin"]
    normal = v["normalThickness"]
    maximum = v["maximumNormalThickness"]
    trigger_x, trigger_y, trigger_width, trigger_height = v["stableTriggerGeometry"]
    screen_x, screen_y, screen_width, screen_height = v["screenGeometry"]

    expected_envelope_depth = maximum
    expected_normal = maximum - gap
    if expected_panel == "true":
        expected_envelope_depth = normal + gap
        expected_normal = maximum

    if not (
        view_type == expected_type
        and panel == expected_panel
        and geometry_present == expected_panel
        and edge == "top"
        and floating_gap == "true"
        and gap == 18
        and trigger_width > 0
        and trigger_height == expected_envelope_depth
        and trigger_y == screen_y + 1
        and normal == expected_normal
    ):
        recipe.fail(
            f"invalid {expected_type} stable-envelope fixture (type={view_type} panel={panel} "
            f"geometry={geometry_present} edge={edge} floating={floating_gap} gap={gap} "
            f"normal={normal} maximum={maximum} trigger={trigger_x},{trigger_y} "
            f"{trigger_width}x{trigger_height} screen={screen_x},{screen_y} "
            f"{screen_width}x{screen_height})"
        )

    if _konsole_count() != 0:
        recipe.fail("a tagged titlebar client already exists")
    _S.konsole = subprocess.Popen(
        ["konsole", "-p", f"LocalTabTitleFormat={_TITLE}"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )
    for _ in range(40):
        if _konsole_count() == 1:
            break
        time.sleep(0.1)
    if _konsole_count() != 1:
        recipe.fail("the titlebar client never mapped")

    client_width = 500
    client_height = 400
    baseline_x = trigger_x + trigger_width // 2 - client_width // 2
    minimum_x = screen_x + 20
    maximum_x = screen_x + screen_width - client_width - 20
    if baseline_x < minimum_x:
        baseline_x = minimum_x
    if baseline_x > maximum_x:
        baseline_x = maximum_x
    baseline_y = trigger_y + trigger_height + 60

    if not _move_konsole(baseline_x, baseline_y, client_width, client_height):
        recipe.fail("KWin did not accept titlebar-client placement")
    _wait_for_konsole_geometry(baseline_x, baseline_y, client_width, client_height)
    _wait_for_policy_while_held(
        expected_type, 0, "false", "floated", f"{expected_type} initial negative control", False
    )

    try:
        base_snapshot = _stable_physical_snapshot()
    except recipe.RecipeError, KeyError, IndexError:
        recipe.fail(f"could not capture the stable {expected_type} surface")
    (
        _progress,
        configured_ratio,
        base_presented_x,
        base_presented_length,
        _output_x,
        output_length,
        borders,
    ) = _presentation_probe()
    if expected_type == "dock":
        if not math.isclose(configured_ratio, 0.6, abs_tol=1e-6):
            recipe.fail("Dock fixture did not retain its configured 60% resting length")
        if not base_presented_length < output_length:
            recipe.fail("Dock fixture is not partial before live attachment")
        if borders != "bottom,left,right,top":
            recipe.fail("floated Dock fixture did not begin with all corners")
    else:
        if not base_presented_length < output_length:
            recipe.fail("Panel fixture is not partial before live attachment")
        if borders != "bottom,left,right,top":
            recipe.fail("floated Panel fixture did not begin with all corners")

    titlebar_offset = 12
    start_x = baseline_x + client_width // 2
    start_y = baseline_y + titlebar_offset
    touching_y = trigger_y + trigger_height - 8 + titlebar_offset

    # Leave enough endpoint time for several D-Bus samples on a loaded test
    # host. The production transition is short, but the observer must still
    # prove its endpoint before the pointer reverses.
    _S.drag = subprocess.Popen(
        [
            os.environ["E2E_FAKEPOINTER"],
            "draghold",
            "2000",
            str(start_x),
            str(start_y),
            str(start_x),
            str(touching_y),
            str(start_x),
            str(start_y),
        ]
    )

    # Sample the short-lived fractional phase before its stable endpoint. The
    # endpoint policy remains observable for the rest of the held interval.
    _wait_for_fractional_progress_while_held(
        expected_type,
        "attaching",
        f"{expected_type} live inward presentation",
        expect_dock_expansion,
    )
    if expected_type == "dock" and expect_dock_expansion:
        #! The complete-span endpoint is the shortest-lived assertion in the
        #! held crossing. Observe it before the policy query spends another
        #! D-Bus round trip; the endpoint itself also proves the attached
        #! target and the still-owned button hold.
        _wait_for_dock_attached_presentation_while_held(True)
    elif expected_panel == "true":
        _wait_for_partial_panel_attached_presentation(base_presented_x, base_presented_length)
    _wait_for_policy_while_held(
        expected_type, 1, expected_request, expected_target, f"{expected_type} live inward crossing"
    )
    if expected_type == "dock" and not expect_dock_expansion:
        _assert_partial_dock_presentation(
            f"{expected_type} live attachment", base_presented_x, base_presented_length
        )
    _assert_stable_physical_snapshot(f"{expected_type} live attachment", base_snapshot)

    _wait_for_fractional_progress_while_held(
        expected_type,
        "floating",
        f"{expected_type} live outward presentation",
        expect_dock_expansion,
    )
    _wait_for_policy_while_held(
        expected_type, 0, "false", "floated", f"{expected_type} live outward reversal"
    )
    _assert_stable_physical_snapshot(f"{expected_type} live reversal", base_snapshot)

    drag = _S.drag
    if drag is None or drag.wait() != 0:
        recipe.fail(f"{expected_type} held titlebar drag failed")
    _S.drag = None
    _wait_for_konsole_geometry(baseline_x, baseline_y, client_width, client_height)
    if expected_type == "dock":
        _wait_for_dock_floated_presentation(base_presented_x, base_presented_length)
    _assert_stable_physical_snapshot(f"{expected_type} after release", base_snapshot)
    _stop_owned_konsole()


# ---- the attached configured-length mutation -------------------------------


def _latte_dock_window_rows() -> list[str]:
    return sorted(line for line in recipe.dumpwins().splitlines() if "|latte-dock|" in line)


def _find_new_edit_canvas(
    windows_before: list[str], screen_x: int, screen_width: int
) -> tuple[int, int, int, int] | None:
    """The bash ``comm -13`` multiset difference plus the awk canvas filter: the
    first new latte-dock row that spans the output at under 300px height."""
    before = Counter(windows_before)
    for row in _latte_dock_window_rows():
        if before[row] > 0:
            before[row] -= 1
            continue
        parsed = recipe.parse_dumpwins(row)
        if not parsed:
            continue
        w = parsed[0]
        if w.x == screen_x and w.width == screen_width and w.height < 300:
            return w.x, w.y, w.width, w.height
    return None


def _exercise_attached_maximum_length_change() -> None:
    try:
        auto_size_disabled = json.dumps(_view_config()["autoSizeEnabled"]) == "false"
    except recipe.RecipeError, KeyError:
        auto_size_disabled = False
    if not auto_size_disabled:
        recipe.fail("attached length-mutation fixture did not disable automatic sizing")
    try:
        initial_maximum = _view_config()["maxLength"]
    except recipe.RecipeError, KeyError:
        recipe.fail("could not read the initial configured maximum length")
    if str(initial_maximum) != "60":
        recipe.fail(f"attached length-mutation fixture began at {initial_maximum}% instead of 60%")
    initial_maximum = int(initial_maximum)

    v = _dock_record()
    screen_x, screen_y, screen_width, screen_height = v["screenGeometry"]
    initial_absolute_x = v["absoluteGeometry"][0]
    initial_absolute_width = v["absoluteGeometry"][2]
    initial_trigger_x = v["stableTriggerGeometry"][0]
    initial_trigger_width = v["stableTriggerGeometry"][2]
    expected_initial_width = screen_width * initial_maximum // 100
    expected_initial_x = screen_x + (screen_width - expected_initial_width) // 2
    if not (
        (initial_absolute_x, initial_absolute_width) == (expected_initial_x, expected_initial_width)
        and initial_trigger_width == expected_initial_width
        and expected_initial_x - 1 <= initial_trigger_x <= expected_initial_x + 1
    ):
        recipe.fail(
            f"initial configured authorities do not describe the centered 60% rest span "
            f"(absolute={initial_absolute_x}+{initial_absolute_width} "
            f"trigger={initial_trigger_x}+{initial_trigger_width} "
            f"expected={expected_initial_x}+{expected_initial_width})"
        )

    if _konsole_count() != 0:
        recipe.fail("a tagged titlebar client already exists before the attached length mutation")
    _S.konsole = subprocess.Popen(
        ["konsole", "-p", f"LocalTabTitleFormat={_TITLE}"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )
    for _ in range(40):
        if _konsole_count() == 1:
            break
        time.sleep(0.1)
    if _konsole_count() != 1:
        recipe.fail("the attached length-mutation client never mapped")
    if not _set_konsole_maximized(True):
        recipe.fail("KWin did not maximize the attached length-mutation client")
    _wait_for_dock_attached_presentation_while_held(False)

    windows_before = _latte_dock_window_rows()
    _fp_or_fail(
        "could not normalize the pointer for the attached length mutation",
        "move",
        str(screen_x + screen_width // 2),
        str(screen_y + screen_height // 2),
    )
    recipe.call_or_fail(
        "could not enter edit mode for the attached length mutation",
        "setViewEditMode",
        "ub",
        str(_S.view),
        "true",
    )
    time.sleep(3)
    _wait_for_dock_attached_presentation_while_held(False)

    canvas = _find_new_edit_canvas(windows_before, screen_x, screen_width)
    if canvas is None:
        recipe.fail("no edit canvas mapped for the attached length mutation")
    _cx, cy, _cw, ch = canvas

    try:
        stable_before = _configured_length_independent_snapshot()
    except recipe.RecipeError, KeyError, IndexError:
        recipe.fail("could not capture stable state before the attached length mutation")

    ruler_x = screen_x + screen_width // 2
    ruler_y = cy + ch - 7
    changed_maximum = initial_maximum
    for _attempt in range(5):
        _fp("scroll", str(ruler_x), str(ruler_y), "-1", "100")
        _fp("move", str(ruler_x), str(screen_y + screen_height // 2))
        changed = False
        for _ in range(8):
            time.sleep(0.5)
            try:
                changed_maximum = int(_view_config()["maxLength"])
            except recipe.RecipeError, KeyError, ValueError:
                continue
            if changed_maximum != initial_maximum:
                changed = True
                break
        if changed:
            break
    if changed_maximum != 54:
        recipe.fail(
            f"one attached ruler detent changed Maximum Length from {initial_maximum} "
            f"to {changed_maximum} instead of 54"
        )

    expected_width = screen_width * changed_maximum // 100
    expected_x = screen_x + (screen_width - expected_width) // 2
    progress = -1.0
    target = "unread"
    absolute_x = absolute_width = -1
    trigger_x = trigger_width = -1
    paint_x = paint_width = -1
    stable_after = "unread"

    def _converged() -> bool:
        return (
            f"{progress:.9f}" == "0.000000000"
            and target == "attached"
            and (absolute_x, absolute_width) == (expected_x, expected_width)
            and trigger_width == expected_width
            and expected_x - 1 <= trigger_x <= expected_x + 1
            and (paint_x, paint_width) == (screen_x, screen_width)
            and stable_after == stable_before
        )

    for _ in range(100):
        try:
            v = _dock_record()
            progress = v["transitionProgress"]
            target = v["transitionTarget"]
            absolute_x = v["absoluteGeometry"][0]
            absolute_width = v["absoluteGeometry"][2]
            trigger_x = v["stableTriggerGeometry"][0]
            trigger_width = v["stableTriggerGeometry"][2]
            paint_x = v["windowGeometry"][0] + v["effectsRect"][0]
            paint_width = v["effectsRect"][2]
        except recipe.RecipeError, KeyError, IndexError, TypeError:
            time.sleep(0.05)
            continue
        try:
            stable_after = _configured_length_independent_snapshot()
        except recipe.RecipeError, KeyError, IndexError:
            time.sleep(0.05)
            continue
        if _converged():
            break
        time.sleep(0.05)
    if not _converged():
        recipe.fail(
            f"attached configured-length authorities did not converge without presentation "
            f"feedback (progress={progress} target={target} "
            f"absolute={absolute_x}+{absolute_width} trigger={trigger_x}+{trigger_width} "
            f"paint={paint_x}+{paint_width} expectedRest={expected_x}+{expected_width} "
            f"expectedPaint={screen_x}+{screen_width} stableBefore={stable_before} "
            f"stableAfter={stable_after})"
        )

    recipe.call_or_fail(
        "could not leave edit mode after the attached length mutation",
        "setViewEditMode",
        "ub",
        str(_S.view),
        "false",
    )
    if not _set_konsole_maximized(False):
        recipe.fail("KWin did not restore the attached length-mutation client")
    _wait_for_dock_floated_presentation(expected_x, expected_width)
    _stop_owned_konsole()


# ---- the recipe body -------------------------------------------------------


def _body() -> None:
    if matrix.init() != 0:
        recipe.fail("could not capture the pristine nested configuration")
    _S.configured = True

    _configure_case("panel-top-center-1out")
    _exercise_held_drag("panel", "true", "false", "attached")

    _configure_case("dock-top-center-1out")
    _exercise_held_drag("dock", "false", "true", "attached")

    _configure_case("dock-top-justify-1out")
    _exercise_held_drag("dock", "false", "true", "attached", True)
    _exercise_attached_maximum_length_change()

    print(
        "Live titlebar window touch passed before button release for Panel, partial "
        "Center Dock, and expanding Justify Dock; attached Maximum Length mutation "
        "refreshed stable occupancy and touch authority without changing presentation "
        "or surface ownership"
    )


def _cleanup() -> bool:
    cleanup_failed = False
    if _S.drag is not None:
        with suppress(ProcessLookupError):
            _S.drag.terminate()
        with suppress(Exception):
            _S.drag.wait()
    if _S.konsole is not None:
        with suppress(ProcessLookupError):
            os.kill(_S.konsole.pid, signal.SIGTERM)
        with suppress(Exception):
            _S.konsole.wait()
    if _S.configured:
        if not matrix.stop_dock():
            cleanup_failed = True
        config_home = os.environ["E2E_CONFIG_HOME"]
        if not config_home:
            raise recipe.RecipeError("E2E_CONFIG_HOME is unset")
        shutil.rmtree(config_home, ignore_errors=True)
        try:
            _ = shutil.copytree(matrix.pristine_seed_dir(), config_home)
        except OSError:
            cleanup_failed = True
        pid = recipe.dock_pid()
        if (pid is not None and recipe.pid_alive(pid)) or not _muted_dock_start(90):
            cleanup_failed = True
    if cleanup_failed:
        print(
            "FAIL: live titlebar window-touch cleanup did not restore the nested dock",
            file=sys.stderr,
            flush=True,
        )
    return cleanup_failed


def _muted_dock_start(timeout: int) -> bool:
    with redirect_stdout(StringIO()), redirect_stderr(StringIO()):
        return recipe.dock_start(timeout)


def main() -> int:
    status = 0
    try:
        _body()
    except SystemExit as exc:
        status = exc.code if isinstance(exc.code, int) else 1
    except recipe.RecipeError as exc:
        print(str(exc), file=sys.stderr, flush=True)
        status = 1
    if _cleanup() and status == 0:
        status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
