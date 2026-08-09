#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""Drive the FP-2 stable-canvas maximize transition with a real Wayland
toplevel. The QWindow, stable applet measurements, layer-shell placement,
per-view reservation contribution, and maximum-depth group reservation must
stay fixed while only the internal qreal presentation progress changes.

Ported from tests/e2e/071-maximized-window-length.sh to latte_harness.recipe
and latte_harness.matrix (BP-3, the bash-to-python migration's window-touch
recipe batch R6). dockSystemData carries the whole stable-canvas, gap-policy
and screen-edge surface (dozens of fields no typed model models), so the
snapshot is read as raw JSON via recipe.read_json at the same boundary the
bash python one-liners used; a refused reply raises the pollable
DbusUnavailableError, the same empty-command-substitution channel every bash
poller swallowed (the dock-edit-retarget-cancel precedent). The
stable-contract comparison is
byte-for-byte the bash json.dumps(sort_keys=True). The coarse
setViewVisibilityMode action stays a busctl call that fails loudly on a D-Bus
error, matching the bash `e2e_call ... || e2e_fail`.
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
from contextlib import redirect_stderr, redirect_stdout, suppress
from io import StringIO
from typing import Any

from latte_harness import matrix, recipe

_TITLE = "LATTE FP2 STABLE CANVAS"
_KONSOLE_MATCH = f"|org.kde.konsole|{_TITLE}"


class _State:
    def __init__(self) -> None:
        self.view = 0
        self.configured = False
        self.konsole: subprocess.Popen[bytes] | None = None
        self.screen_edge_pointer: subprocess.Popen[bytes] | None = None
        self.base_stable_snapshot: str | None = None
        self.base_revisions: str | None = None
        self.base_popup_primary_x: int | None = None
        self.base_popup_primary_width: int | None = None
        # The first-fixture output identity assert_konsole_work_area validates
        # against (the bash globals read once from the panel fixture).
        self.screen = ""
        self.screen_x = 0
        self.screen_y = 0
        self.screen_w = 0
        self.screen_h = 0
        self.edge = ""
        self.stable_reservation_depth = 0


_S = _State()


# ---- transport helpers -----------------------------------------------------


def _fp(*args: str) -> int:
    return subprocess.run([os.environ["E2E_FAKEPOINTER"], *args], check=False).returncode


def _fp_or_fail(fail_message: str, *args: str) -> None:
    if _fp(*args) != 0:
        recipe.fail(fail_message)


def _kwrite(fail_message: str, *args: str) -> None:
    if subprocess.run(["kwriteconfig6", *args], check=False).returncode != 0:
        recipe.fail(fail_message)


def _kwin_tagged_last(body: str, collection_delay: float = 0.5) -> str:
    lines = recipe.kwin_js(body, collection_delay).splitlines()
    return lines[-1] if lines else ""


def _dock_record() -> dict[str, Any]:
    """dock_field's context: the single dockSystemData record for the view.

    The exactly-one guard mirrors the bash; a refused/failed reply raises
    recipe.read_json's pollable DbusUnavailableError (the bash empty command
    substitution).
    """
    snapshot = recipe.read_json("dockSystemData")
    matches = [r for r in snapshot["views"] if r["persistentDockId"] == _S.view]
    if len(matches) != 1:
        raise recipe.RecipeError(
            f"expected exactly one dockSystemData record for containment {_S.view}"
        )
    return matches[0]


def _view_field() -> recipe.View:
    """e2e_view_field's context: the typed viewsData record for the view.

    W3 (widen the readback models): visibilityMode rides the typed recipe.View, so
    this reads recipe.views() instead of raw JSON."""
    match = [v for v in recipe.views() if v.containment_id == _S.view]
    if not match:
        raise recipe.RecipeError(f"no view with containmentId {_S.view}")
    return match[0]


def _lower(value: bool) -> str:
    return "true" if value else "false"


# ---- kwin fixture helpers --------------------------------------------------


def _set_konsole_maximized(enabled: bool) -> str:
    return recipe.kwin_js(
        f"""for (const w of workspace.windowList()) {{
        if (w.resourceClass === 'org.kde.konsole' && w.caption.includes('{_TITLE}')) {{
            workspace.activeWindow = w;
            w.setMaximize({_lower(enabled)}, {_lower(enabled)});
            print('@TAG@|' + w.internalId);
        }}
    }}""",
        0.01,
    )


def _normalize_konsole_away_from_dock() -> str:
    return recipe.kwin_js(
        f"""for (const w of workspace.windowList()) {{
        if (w.resourceClass === 'org.kde.konsole' && w.caption.includes('{_TITLE}')) {{
            w.setMaximize(false, false);
            const geometry = Object.assign({{}}, w.frameGeometry);
            geometry.x = 180;
            geometry.y = 100;
            geometry.width = 900;
            geometry.height = 540;
            w.frameGeometry = geometry;
            workspace.activeWindow = w;
            print('@TAG@|' + w.internalId);
        }}
    }}""",
        0.05,
    )


def _set_konsole_fullscreen(enabled: bool) -> str:
    return recipe.kwin_js(
        f"""for (const w of workspace.windowList()) {{
        if (w.resourceClass === 'org.kde.konsole' && w.caption.includes('{_TITLE}')) {{
            workspace.activeWindow = w;
            w.fullScreen = {_lower(enabled)};
            print('@TAG@|' + w.internalId);
        }}
    }}""",
        0.01,
    )


def _active_window_id() -> str:
    return _kwin_tagged_last(
        'print("@TAG@|" + (workspace.activeWindow ? workspace.activeWindow.internalId : "none"));'
    )


def _cursor_position() -> str:
    return _kwin_tagged_last(
        'print("@TAG@|" + Math.round(workspace.cursorPos.x) + " " '
        "+ Math.round(workspace.cursorPos.y));",
        0.01,
    )


def _wait_for_cursor_position(expected_x: int, expected_y: int, phase: str) -> None:
    actual_x = actual_y = "unread"
    for _ in range(40):
        fields = _cursor_position().split()
        if len(fields) == 2:
            actual_x, actual_y = fields
        if actual_x == str(expected_x) and actual_y == str(expected_y):
            return
        time.sleep(0.025)
    recipe.fail(
        f"{phase} left the nested KWin cursor at {actual_x},{actual_y}; "
        f"expected {expected_x},{expected_y}"
    )


# ---- snapshot / contract probes --------------------------------------------

_STABLE_KEYS = (
    "windowGeometry",
    "absoluteGeometry",
    "surfaceGeometry",
    "canvasGeometry",
    "stableCanvasGeometry",
    "attachedPresentationGeometry",
    "floatedPresentationGeometry",
    "stableTriggerGeometry",
    "stableAppletMeasurementBounds",
    "stablePrimaryAxisStart",
    "stablePrimaryAxisLength",
    "availablePrimaryLength",
    "configuredIconSize",
    "effectiveIconSize",
    "maximumLengthRatio",
    "alignment",
    "geometrySettled",
    "stableLayerShellMargin",
    "requestedReservationDepth",
    "reservationContributionDepth",
    "reservationPublishedDepth",
    "reservationOutputId",
    "reservationEdge",
    "reservationGroupGeneration",
    "reservationContributorDockIds",
    "reservationGeometry",
    "layerShellMargins",
    "layerShellAnchors",
    "layerShellExclusiveEdge",
    "layerShellExclusiveZone",
    "publishedStruts",
)


def _stable_snapshot() -> str:
    v = _dock_record()
    obj = v["objects"]
    return json.dumps(
        {
            "stable": {key: v[key] for key in _STABLE_KEYS},
            "objects": {
                "transitionController": obj["transitionController"],
                "reservationPublisher": obj["reservationPublisher"],
            },
        },
        sort_keys=True,
        separators=(",", ":"),
    )


def _revision_snapshot() -> str:
    v = _dock_record()
    return (
        f"{v['transitionGeometryRevision']} "
        f"{v['surfaceGeometryPublicationRevision']} "
        f"{v['layerShellConfigureRequestRevision']}"
    )


def _popup_anchor_probe() -> tuple[int, int, int, int, int, int]:
    v = _dock_record()
    x, y, width, height = v["appletsLayoutGeometry"]
    paint = v["computedPaintMaskGeometry"]
    paint_y = math.floor(paint[1])
    paint_height = math.ceil(paint[1] + paint[3]) - math.floor(paint[1])
    return x, y, width, height, paint_y, paint_height


def _assert_popup_anchor_contract(phase: str) -> None:
    try:
        x, y, width, height, paint_y, paint_height = _popup_anchor_probe()
    except recipe.RecipeError:
        recipe.fail(f"{phase} could not read popup-anchor geometry")
    if (x, width) != (_S.base_popup_primary_x, _S.base_popup_primary_width):
        recipe.fail(
            f"{phase} changed the popup anchor primary span: "
            f"base={_S.base_popup_primary_x}/{_S.base_popup_primary_width} current={x}/{width}"
        )
    if (y, height) != (paint_y, paint_height):
        recipe.fail(
            f"{phase} popup anchor did not follow the outward-aligned visible mask: "
            f"anchor={y}/{height} paint={paint_y}/{paint_height}"
        )


def _assert_stable_contract(phase: str) -> None:
    try:
        current = _stable_snapshot()
    except recipe.RecipeError, KeyError, IndexError:
        recipe.fail(f"{phase} could not read the stable geometry snapshot")
    if current != _S.base_stable_snapshot:
        recipe.fail(
            f"{phase} changed the stable panel contract: "
            f"base={_S.base_stable_snapshot} current={current}"
        )
    try:
        revisions = _revision_snapshot()
    except recipe.RecipeError:
        recipe.fail(f"{phase} could not read stable-controller and physical-geometry revisions")
    if revisions != _S.base_revisions:
        recipe.fail(
            f"{phase} reconfigured stable geometry or published physical geometry "
            f"during progress: base={_S.base_revisions} current={revisions}"
        )
    _assert_popup_anchor_contract(phase)


def _transition_probe() -> tuple[str, str, str, float, str, str, str]:
    v = _dock_record()
    return (
        v["transitionTarget"],
        v["transitionPhase"],
        _lower(v["transitionRunning"]),
        v["transitionProgress"],
        str(v["transitionGeometryRevision"]),
        str(v["surfaceGeometryPublicationRevision"]),
        str(v["layerShellConfigureRequestRevision"]),
    )


def _wait_for_resting_target(expected_target: str, expected_progress: float) -> None:
    target = phase = running = "unread"
    progress = -1.0
    for _ in range(80):
        try:
            target, phase, running, progress, _, _, _ = _transition_probe()
        except recipe.RecipeError:
            time.sleep(0.05)
            continue
        if (
            target == expected_target
            and phase == "resting"
            and running == "false"
            and abs(progress - expected_progress) < 0.000001
        ):
            return
        time.sleep(0.05)
    recipe.fail(
        f"transition did not settle at {expected_target}/{expected_progress} "
        f"(target={target} phase={phase} running={running} progress={progress})"
    )


def _capture_progress_only_transition(expected_target: str, expected_phase: str) -> None:
    target = phase = running = "unread"
    progress = -1.0
    for _ in range(100):
        try:
            (
                target,
                phase,
                running,
                progress,
                geometry_revision,
                surface_revision,
                layer_revision,
            ) = _transition_probe()
        except recipe.RecipeError:
            time.sleep(0.01)
            continue
        if (
            target == expected_target
            and phase == expected_phase
            and running == "true"
            and 0.0 < progress < 1.0
        ):
            if f"{geometry_revision} {surface_revision} {layer_revision}" != _S.base_revisions:
                recipe.fail(
                    f"{expected_phase} transition changed stable-controller or "
                    f"physical-geometry revisions at progress {progress}"
                )
            _assert_stable_contract(f"{expected_phase} midpoint")
            return
        time.sleep(0.01)
    recipe.fail(
        f"no qreal midpoint observed for {expected_phase} transition "
        f"(target={target} phase={phase} running={running} progress={progress})"
    )


def _wait_for_in_flight_target(expected_target: str, expected_phase: str) -> None:
    target = phase = running = "unread"
    progress = -1.0
    for _ in range(80):
        try:
            (
                target,
                phase,
                running,
                progress,
                geometry_revision,
                surface_revision,
                layer_revision,
            ) = _transition_probe()
        except recipe.RecipeError:
            time.sleep(0.01)
            continue
        if (
            target == expected_target
            and phase == expected_phase
            and running == "true"
            and 0.0 < progress < 1.0
        ):
            if f"{geometry_revision} {surface_revision} {layer_revision}" != _S.base_revisions:
                recipe.fail(
                    f"rapid reversal to {expected_target} changed stable-controller or "
                    f"physical-geometry revisions at progress {progress}"
                )
            return
        time.sleep(0.01)
    recipe.fail(
        f"rapid reversal never entered {expected_phase} for target {expected_target} "
        f"(target={target} phase={phase} running={running} progress={progress})"
    )


def _tracker_maximized_probe() -> tuple[str, str]:
    tracker = recipe.read_json("trackerData", "u", str(_S.view))
    return _lower(tracker["activeWindowMaximized"]), _lower(tracker["existsWindowMaximized"])


def _wait_for_tracker_and_target(
    expected_maximized: str, expected_target: str, expected_progress: float
) -> None:
    active_maximized = exists_maximized = "unread"
    target = phase = running = "unread"
    progress = -1.0
    for _ in range(80):
        try:
            active_maximized, exists_maximized = _tracker_maximized_probe()
            target, phase, running, progress, _, _, _ = _transition_probe()
        except recipe.RecipeError:
            time.sleep(0.05)
            continue
        if (
            active_maximized == expected_maximized
            and exists_maximized == expected_maximized
            and target == expected_target
            and phase == "resting"
            and running == "false"
            and abs(progress - expected_progress) < 0.000001
        ):
            return
        time.sleep(0.05)
    recipe.fail(
        f"tracker/controller did not settle together (active={active_maximized} "
        f"exists={exists_maximized} target={target} phase={phase} progress={progress})"
    )


def _wait_for_zero_gap_floated_snapshot() -> None:
    configured_panel = eligible_panel = view_type = visibility_mode = "unread"
    target = phase = running = "unread"
    progress = -1.0
    for _ in range(80):
        try:
            v = _dock_record()
            configured_panel = _lower(v["floatingPanelConfigured"])
            eligible_panel = _lower(v["floatingPanelEligible"])
            view_type = v["type"]
            visibility_mode = v["visibilityMode"]
            target = v["transitionTarget"]
            phase = v["transitionPhase"]
            running = _lower(v["transitionRunning"])
            progress = v["transitionProgress"]
        except recipe.RecipeError:
            time.sleep(0.05)
            continue
        if (
            view_type == "panel"
            and visibility_mode == "alwaysVisible"
            and configured_panel == "false"
            and eligible_panel == "false"
            and target == "floated"
            and phase == "resting"
            and running == "false"
            and abs(progress - 1.0) < 0.000001
        ):
            return
        time.sleep(0.05)
    recipe.fail(
        f"zero-gap panel never exposed one consistent floated endpoint snapshot "
        f"(type={view_type} visibility={visibility_mode} configured={configured_panel} "
        f"eligible={eligible_panel} target={target} phase={phase} running={running} "
        f"progress={progress})"
    )


def _wait_for_dock_gap_policy(
    expected_visibility: str,
    expected_maximized: str,
    expected_request: str,
    expected_target: str,
    expected_progress: int,
) -> None:
    active_maximized = exists_maximized = "unread"
    view_type = visibility_mode = "unread"
    floating_gap_configured = "unread"
    configured_panel = eligible_panel = "unread"
    configured_hide = dock_request = "unread"
    transition_geometry = panel_geometry_absent = "unread"
    floating_popups = "unread"
    target = phase = running = "unread"
    progress = -1.0
    transition_duration = -1
    configured_gap = -1
    presented_gap = -1
    for _ in range(80):
        try:
            active_maximized, exists_maximized = _tracker_maximized_probe()
            v = _dock_record()
            view_type = v["type"]
            visibility_mode = v["visibilityMode"]
            floating_gap_configured = _lower(v["floatingGapConfigured"])
            configured_panel = _lower(v["floatingPanelConfigured"])
            eligible_panel = _lower(v["floatingPanelEligible"])
            configured_hide = _lower(v["attachOnWindowTouchConfigured"])
            dock_request = _lower(v["dockGapHideRequested"])
            target = v["transitionTarget"]
            phase = v["transitionPhase"]
            running = _lower(v["transitionRunning"])
            progress = v["transitionProgress"]
            transition_duration = v["transitionAnimationDuration"]
            transition_geometry = _lower(v["transitionGeometryPresent"])
            panel_geometry_absent = _lower(
                all(
                    v[key] is None
                    for key in (
                        "stableCanvasGeometry",
                        "attachedPresentationGeometry",
                        "floatedPresentationGeometry",
                        "currentVisibleGeometry",
                        "computedPaintMaskGeometry",
                        "computedInputBridgeGeometry",
                    )
                )
            )
            floating_popups = _lower(v["floatingAppletPopupsPreferred"])
            configured_gap = v["screenEdgeMargin"]
            presented_gap = v["presentedScreenEdgeGap"]
        except recipe.RecipeError:
            time.sleep(0.05)
            continue
        if (
            active_maximized == expected_maximized
            and exists_maximized == expected_maximized
            and view_type == "dock"
            and visibility_mode == expected_visibility
            and floating_gap_configured == "true"
            and configured_panel == "false"
            and eligible_panel == "false"
            and configured_hide == "true"
            and dock_request == expected_request
            and target == expected_target
            and phase == "resting"
            and running == "false"
            and transition_duration == 200
            and transition_geometry == "false"
            and panel_geometry_absent == "true"
            and floating_popups == "false"
            and presented_gap == configured_gap * expected_progress
            and abs(progress - expected_progress) < 0.000001
        ):
            return
        time.sleep(0.05)
    recipe.fail(
        f"Dock maximized-gap policy did not settle (active={active_maximized} "
        f"exists={exists_maximized} type={view_type} visibility={visibility_mode} "
        f"floatingGapConfigured={floating_gap_configured} configuredPanel={configured_panel} "
        f"panelEligible={eligible_panel} configuredHide={configured_hide} "
        f"dockRequest={dock_request} target={target} phase={phase} running={running} "
        f"progress={progress} duration={transition_duration} configuredGap={configured_gap} "
        f"presentedGap={presented_gap} transitionGeometry={transition_geometry} "
        f"panelGeometryAbsent={panel_geometry_absent} floatingPopups={floating_popups})"
    )


def _wait_for_dock_window_touch_policy(
    expected_request: str, expected_target: str, expected_progress: int
) -> None:
    touching_count = -1
    dock_request = target = phase = running = "unread"
    progress = -1.0
    presented_gap = configured_gap = -1
    for _ in range(80):
        try:
            v = _dock_record()
            touching_count = v["touchingWindowCount"]
            dock_request = _lower(v["dockGapHideRequested"])
            target = v["transitionTarget"]
            phase = v["transitionPhase"]
            running = _lower(v["transitionRunning"])
            progress = v["transitionProgress"]
            configured_gap = v["screenEdgeMargin"]
            presented_gap = v["presentedScreenEdgeGap"]
        except recipe.RecipeError:
            time.sleep(0.05)
            continue
        touching_matches = (expected_request == "true" and touching_count > 0) or (
            expected_request == "false" and touching_count == 0
        )
        if (
            touching_matches
            and dock_request == expected_request
            and target == expected_target
            and phase == "resting"
            and running == "false"
            and presented_gap == configured_gap * expected_progress
            and abs(progress - expected_progress) < 0.000001
        ):
            return
        time.sleep(0.05)
    recipe.fail(
        f"Dock window-touch policy did not settle (touching={touching_count} "
        f"request={dock_request} target={target} phase={phase} running={running} "
        f"progress={progress} configuredGap={configured_gap} presentedGap={presented_gap})"
    )


def _wait_for_hidden_state(expected: str, phase: str) -> None:
    hidden = "unread"
    for _ in range(100):
        try:
            hidden = _lower(_dock_record()["isHidden"])
        except recipe.RecipeError:
            recipe.fail("could not read the Dodge Active hidden state")
        if hidden == expected:
            return
        time.sleep(0.05)
    recipe.fail(
        f"{phase} left the Dodge Active Dock hidden state at {hidden}; "
        f"expected {expected} (cursor={_cursor_position()})"
    )


def _wait_for_native_screen_edge_armed(phase: str) -> None:
    backend = armed = registered = "unread"
    supported = contains_mouse = "unread"
    unavailable_snapshots = 0
    for _ in range(80):
        try:
            state = recipe.read_json("dockSystemData")
        except recipe.DbusUnavailableError:
            unavailable_snapshots += 1
            time.sleep(0.05)
            continue
        try:
            matches = [v for v in state["views"] if v["persistentDockId"] == _S.view]
            if len(matches) != 1:
                time.sleep(0.05)
                continue
            v = matches[0]
            backend = v["screenEdgeBackend"]
            armed = _lower(v["screenEdgeArmed"])
            registered = _lower(v["screenEdgeRegistered"])
            supported = _lower(v["compositorScreenEdgeSupported"])
            contains_mouse = _lower(v["visibilityContainsMouse"])
        except KeyError:
            time.sleep(0.05)
            continue
        if (
            backend == "kwinAutoHide"
            and armed == "true"
            and registered == "true"
            and supported == "true"
            and contains_mouse == "false"
        ):
            return
        time.sleep(0.05)
    recipe.fail(
        f"{phase} did not establish compositor-owned edge reveal (backend={backend} "
        f"armed={armed} registered={registered} supported={supported} "
        f"containsMouse={contains_mouse} unavailableSnapshots={unavailable_snapshots})"
    )


def _start_kwin_screen_edge_round_trip(x: int, y: int, departure_x: int, departure_y: int) -> None:
    # Keep one input device alive for edge pressure, surface enter, and leave.
    # KWin's fake-input backend can retain the first device's surface focus if
    # a second short-lived client replaces it after the reveal. The repeated
    # endpoint also exceeds KWin's ElectricBorderDelay while Latte slides in.
    _S.screen_edge_pointer = subprocess.Popen(
        [
            os.environ["E2E_FAKEPOINTER"],
            "glide",
            str(x),
            str(y - 80),
            str(x),
            str(y),
            str(x),
            str(y),
            str(x),
            str(y),
            str(x),
            str(y),
            str(x),
            str(y),
            str(departure_x),
            str(departure_y),
        ]
    )


def _finish_kwin_screen_edge_round_trip() -> None:
    pointer = _S.screen_edge_pointer
    if pointer is None:
        recipe.fail("no Dodge Active screen-edge pointer gesture is running")
    if pointer.wait() != 0:
        recipe.fail("could not complete the Dodge Active screen-edge pointer gesture")
    _S.screen_edge_pointer = None


def _wait_for_revealed_attached_bottom_dock() -> None:
    hidden = target = phase = running = "unread"
    progress = -1.0
    presented_gap = screen_bottom = painted_bottom = -1
    screen_edge_border = "unread"
    for _ in range(100):
        try:
            v = _dock_record()
            hidden = _lower(v["isHidden"])
            target = v["transitionTarget"]
            phase = v["transitionPhase"]
            running = _lower(v["transitionRunning"])
            progress = v["transitionProgress"]
            presented_gap = v["presentedScreenEdgeGap"]
            screen_bottom = v["screenGeometry"][1] + v["screenGeometry"][3]
            painted_bottom = v["surfaceGeometry"][1] + v["effectsRect"][1] + v["effectsRect"][3]
            screen_edge_border = _lower("bottom" in v["enabledBorders"])
        except recipe.RecipeError:
            time.sleep(0.05)
            continue
        if (
            hidden == "false"
            and target == "attached"
            and phase == "resting"
            and running == "false"
            and presented_gap == 0
            and screen_bottom == painted_bottom
            and screen_edge_border == "false"
            and abs(progress) < 0.000001
        ):
            return
        time.sleep(0.05)
    with suppress(recipe.RecipeError):
        print(recipe.dumpwins(), file=sys.stderr, flush=True)
    with suppress(recipe.RecipeError):
        print(
            recipe.json_payload("viewConfigData", "u", str(_S.view)),
            file=sys.stderr,
            flush=True,
        )
    recipe.fail(
        f"revealed Dodge Active Dock did not become flush and square-edged "
        f"(hidden={hidden} target={target} phase={phase} running={running} "
        f"progress={progress} gap={presented_gap} screenBottom={screen_bottom} "
        f"paintedBottom={painted_bottom} screenEdgeBorder={screen_edge_border})"
    )


# ---- konsole fixture helpers -----------------------------------------------


def _konsole_row() -> str:
    rows = [line for line in recipe.dumpwins().splitlines() if _KONSOLE_MATCH in line]
    return rows[-1] if rows else ""


def _konsole_frame_geometry() -> tuple[int, int, int, int, str] | None:
    row = _konsole_row()
    if not row:
        return None
    window = recipe.parse_dumpwins(row)
    if not window:
        return None
    w = window[0]
    if not (w.width > 0 and w.height > 0 and w.output):
        return None
    return w.x, w.y, w.width, w.height, w.output


def _assert_konsole_work_area(phase: str) -> None:
    geometry = _konsole_frame_geometry()
    if geometry is None:
        recipe.fail(f"{phase} maximize has no valid Konsole frame geometry")
    kx, ky, kw, kh, output = geometry
    if output != _S.screen:
        recipe.fail(
            f"{phase} maximize placed the Konsole fixture on output '{output}'; "
            f"expected '{_S.screen}'"
        )
    expected_x = _S.screen_x
    expected_w = _S.screen_w
    expected_h = _S.screen_h - _S.stable_reservation_depth
    expected_y = _S.screen_y + _S.stable_reservation_depth if _S.edge == "top" else _S.screen_y
    if not (kx == expected_x and ky == expected_y and kw == expected_w and kh == expected_h):
        recipe.fail(
            f"{phase} maximize has frame {kx},{ky} {kw}x{kh}; expected exact {_S.edge} "
            f"work area {expected_x},{expected_y} {expected_w}x{expected_h} from the "
            f"stable {_S.stable_reservation_depth}px group reservation"
        )


def _pid_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def _muted_dock_start(timeout: int) -> bool:
    with redirect_stdout(StringIO()), redirect_stderr(StringIO()):
        return recipe.dock_start(timeout)


# ---- the recipe body -------------------------------------------------------


def _body() -> None:
    if matrix.init() != 0:
        recipe.fail("could not capture the pristine nested configuration")
    _S.configured = True
    if matrix.stage("panel-bottom-justify-1out") != 0:
        recipe.fail("could not realize the floating-panel fixture")
    try:
        _S.view = matrix.view_id()
    except matrix.MatrixProbeError:
        recipe.fail("could not resolve the floating-panel fixture")
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
        recipe.fail("dock did not stop before fixture configuration")
    _kwrite(
        "could not disable maximize-driven panel length",
        *group,
        "--key",
        "maximizeWhenMaximized",
        "false",
    )
    _kwrite("could not configure a partial panel length", *group, "--key", "maxLength", "60")
    _kwrite(
        "could not configure floating-gap attachment",
        *group,
        "--key",
        "hideFloatingGapForMaximized",
        "true",
    )
    _kwrite("could not configure the floating gap", *group, "--key", "screenEdgeMargin", "18")
    _kwrite(
        "could not keep the floating gap under panel-surface ownership",
        *group,
        "--key",
        "floatingInternalGapIsForced",
        "false",
    )
    _kwrite("could not configure Justify alignment", *group, "--key", "alignment", "10")
    _kwrite(
        "could not mark the Justify alignment as upgraded",
        *group,
        "--key",
        "alignmentUpgraded",
        "true",
    )
    if not recipe.dock_start(90):
        recipe.fail("dock did not restart with the stable-canvas fixture")
    recipe.call_or_fail(
        "could not set the fixture view to alwaysVisible",
        "setViewVisibilityMode",
        "us",
        str(_S.view),
        "alwaysVisible",
    )
    for _ in range(40):
        with suppress(recipe.RecipeError):
            if _view_field().visibility_mode == "alwaysVisible":
                break
        time.sleep(0.25)
    try:
        settled = _view_field().visibility_mode == "alwaysVisible"
    except recipe.RecipeError:
        settled = False
    if not settled:
        recipe.fail(f"view {_S.view} did not enter alwaysVisible mode")

    record = _dock_record()
    configured_panel = _lower(record["floatingPanelConfigured"])
    eligible_panel = _lower(record["floatingPanelEligible"])
    geometry_present = _lower(record["transitionGeometryPresent"])
    alignment = record["alignment"]
    if not (configured_panel == "true" and eligible_panel == "true" and geometry_present == "true"):
        recipe.fail(
            f"view {_S.view} did not expose an eligible configured floating-panel controller"
        )
    if alignment != "justify":
        recipe.fail(f"view {_S.view} did not retain Justify alignment")
    _wait_for_resting_target("floated", 1)

    record = _dock_record()
    base_window_width = record["windowGeometry"][2]
    _S.screen_x, _S.screen_y, _S.screen_w, _S.screen_h = record["screenGeometry"]
    _S.edge = record["edge"]
    _S.screen = record["screen"]
    _S.stable_reservation_depth = record["reservationPublishedDepth"]
    contribution_depth = record["reservationContributionDepth"]
    requested_depth = record["requestedReservationDepth"]
    if not (_S.screen_w > 0 and _S.screen_h > 0):
        recipe.fail(
            f"view {_S.view} reported invalid output dimensions {_S.screen_w}x{_S.screen_h}"
        )
    if not _S.screen:
        recipe.fail(f"view {_S.view} did not report its output name")
    if not base_window_width * 100 < _S.screen_w * 90:
        recipe.fail(
            f"fixture view {_S.view} is not partial ({base_window_width} of {_S.screen_w}px)"
        )
    if requested_depth != contribution_depth:
        recipe.fail(
            f"requested depth {requested_depth} differs "
            f"from the view contribution {contribution_depth}"
        )
    if not (_S.stable_reservation_depth >= contribution_depth and contribution_depth > 0):
        recipe.fail(
            f"maximum-depth reservation {_S.stable_reservation_depth} does not cover "
            f"contribution {contribution_depth}"
        )

    try:
        _S.base_stable_snapshot = _stable_snapshot()
    except recipe.RecipeError, KeyError, IndexError:
        recipe.fail("could not capture the base stable geometry contract")
    try:
        _S.base_revisions = _revision_snapshot()
    except recipe.RecipeError:
        recipe.fail("could not capture base stable-controller and physical-geometry revisions")
    try:
        anchor = _popup_anchor_probe()
    except recipe.RecipeError:
        recipe.fail("could not capture the base popup-anchor primary span")
    _S.base_popup_primary_x = anchor[0]
    _S.base_popup_primary_width = anchor[2]

    _S.konsole = subprocess.Popen(
        ["konsole", "-p", f"LocalTabTitleFormat={_TITLE}"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )
    for _ in range(30):
        if _konsole_row():
            break
        time.sleep(0.5)
    if not _konsole_row():
        recipe.fail("Konsole stable-canvas fixture never mapped")

    fixture_id = _normalize_konsole_away_from_dock()
    if not fixture_id:
        recipe.fail("KWin did not normalize the Konsole fixture away from the dock")
    if "\n" in fixture_id:
        recipe.fail("KWin found multiple tagged Konsole fixtures")
    _wait_for_tracker_and_target("false", "floated", 1)
    _assert_stable_contract("normalized floated state")

    if _set_konsole_maximized(True) != fixture_id:
        recipe.fail("KWin did not maximize the tagged Konsole fixture")
    _capture_progress_only_transition("attached", "attaching")
    _wait_for_tracker_and_target("true", "attached", 0)
    _assert_stable_contract("attached resting state")
    if _active_window_id() != fixture_id:
        recipe.fail("tagged Konsole was not active after attachment")
    _assert_konsole_work_area("attached")

    if _set_konsole_maximized(False) != fixture_id:
        recipe.fail("KWin did not restore the tagged Konsole fixture")
    _capture_progress_only_transition("floated", "floating")
    _wait_for_tracker_and_target("false", "floated", 1)
    _assert_stable_contract("floated resting state")

    for maximized in (True, False, True, False, True, False, True, False):
        expected_target = "attached" if maximized else "floated"
        expected_phase = "attaching" if maximized else "floating"
        if _set_konsole_maximized(maximized) != fixture_id:
            recipe.fail(f"KWin did not drive the {expected_target} storm target")
        _wait_for_in_flight_target(expected_target, expected_phase)

    if _set_konsole_maximized(True) != fixture_id:
        recipe.fail("KWin did not settle the storm at attached")
    _wait_for_tracker_and_target("true", "attached", 0)
    _assert_stable_contract("rapid reversal storm")
    _assert_konsole_work_area("post-storm attached")

    if not recipe.dock_stop():
        recipe.fail("dock did not stop before the zero-gap boundary check")
    _kwrite(
        "could not configure the legal zero-pixel floating gap",
        *group,
        "--key",
        "screenEdgeMargin",
        "0",
    )
    if not recipe.dock_start(90):
        recipe.fail("dock did not restart for the zero-gap boundary check")
    _wait_for_zero_gap_floated_snapshot()

    if matrix.stage("dock-bottom-center-1out") != 0:
        recipe.fail("could not realize the legacy floating-Dock fixture")
    try:
        _S.view = matrix.view_id()
    except matrix.MatrixProbeError:
        recipe.fail("could not resolve the legacy floating-Dock fixture")
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
        recipe.fail("dock did not stop before legacy Dock policy configuration")
    _kwrite(
        "could not configure legacy Dock maximized-gap hiding",
        *group,
        "--key",
        "hideFloatingGapForMaximized",
        "true",
    )
    _kwrite(
        "could not configure the legacy Dock floating gap",
        *group,
        "--key",
        "screenEdgeMargin",
        "18",
    )
    _kwrite(
        "could not keep the legacy Dock gap under transition ownership",
        *group,
        "--key",
        "floatingInternalGapIsForced",
        "false",
    )
    _kwrite(
        "could not configure immediate Dock attachment under the pointer",
        *group,
        "--key",
        "floatingGapHidingWaitsMouse",
        "false",
    )
    _kwrite("could not configure normal animation speed", *group, "--key", "durationTime", "2")
    if not recipe.dock_start(90):
        recipe.fail("dock did not restart with the legacy floating-Dock fixture")
    recipe.call_or_fail(
        "could not set the legacy Dock fixture to alwaysVisible",
        "setViewVisibilityMode",
        "us",
        str(_S.view),
        "alwaysVisible",
    )

    if _set_konsole_maximized(False) != fixture_id:
        recipe.fail("KWin did not normalize the client for the legacy Dock check")
    _wait_for_dock_gap_policy("alwaysVisible", "false", "false", "floated", 1)
    if _set_konsole_maximized(True) != fixture_id:
        recipe.fail("KWin did not maximize the client for the legacy Dock check")
    _wait_for_dock_gap_policy("alwaysVisible", "true", "true", "attached", 0)
    if _set_konsole_maximized(False) != fixture_id:
        recipe.fail("KWin did not restore the client for the legacy Dock check")
    _wait_for_dock_gap_policy("alwaysVisible", "false", "false", "floated", 1)

    recipe.call_or_fail(
        "could not set the legacy Dock fixture to windowsGoBelow",
        "setViewVisibilityMode",
        "us",
        str(_S.view),
        "windowsGoBelow",
    )
    _wait_for_dock_gap_policy("windowsGoBelow", "false", "false", "floated", 1)
    if _set_konsole_maximized(True) != fixture_id:
        recipe.fail("KWin did not maximize the client for the WindowsGoBelow Dock check")
    _wait_for_dock_gap_policy("windowsGoBelow", "true", "true", "attached", 0)
    if _set_konsole_maximized(False) != fixture_id:
        recipe.fail("KWin did not restore the client for the WindowsGoBelow Dock check")
    _wait_for_dock_gap_policy("windowsGoBelow", "false", "false", "floated", 1)

    _fp_or_fail(
        "could not normalize the pointer away from the Dock edge",
        "move",
        str(_S.screen_x + _S.screen_w // 2),
        str(_S.screen_y + _S.screen_h // 2),
    )
    _wait_for_cursor_position(
        _S.screen_x + _S.screen_w // 2,
        _S.screen_y + _S.screen_h // 2,
        "Dodge Active pointer normalization",
    )
    recipe.call_or_fail(
        "could not set the floating Dock fixture to Dodge Active",
        "setViewVisibilityMode",
        "us",
        str(_S.view),
        "dodgeActive",
    )
    _wait_for_dock_gap_policy("dodgeActive", "false", "false", "floated", 1)
    _wait_for_hidden_state("false", "initial Dodge Active state")
    if _set_konsole_maximized(True) != fixture_id:
        recipe.fail("KWin did not maximize the client for the Dodge Active Dock check")
    _wait_for_dock_gap_policy("dodgeActive", "true", "true", "attached", 0)
    _wait_for_hidden_state("true", "maximized-window concealment")
    _wait_for_native_screen_edge_armed("maximized-window concealment")

    record = _dock_record()
    reveal_x = record["absoluteGeometry"][0] + record["absoluteGeometry"][2] // 2
    reveal_y = record["screenGeometry"][1] + record["screenGeometry"][3] - 1
    _start_kwin_screen_edge_round_trip(
        reveal_x,
        reveal_y,
        _S.screen_x + _S.screen_w // 2,
        _S.screen_y + _S.screen_h // 2,
    )
    _wait_for_revealed_attached_bottom_dock()
    _finish_kwin_screen_edge_round_trip()
    _wait_for_cursor_position(
        _S.screen_x + _S.screen_w // 2,
        _S.screen_y + _S.screen_h // 2,
        "post-reveal pointer departure",
    )
    _wait_for_hidden_state("true", "post-reveal pointer departure")
    _wait_for_native_screen_edge_armed("post-reveal pointer departure")

    if _set_konsole_maximized(False) != fixture_id:
        recipe.fail("KWin did not restore the client after the Dodge Active Dock check")
    _wait_for_dock_gap_policy("dodgeActive", "false", "false", "floated", 1)

    if _set_konsole_fullscreen(True) != fixture_id:
        recipe.fail("KWin did not fullscreen the client for the Dodge Active Dock check")
    _wait_for_dock_window_touch_policy("true", "attached", 0)
    _wait_for_revealed_attached_bottom_dock()
    _wait_for_hidden_state("true", "fullscreen-window concealment")
    _wait_for_native_screen_edge_armed("fullscreen-window concealment")

    if _set_konsole_fullscreen(False) != fixture_id:
        recipe.fail("KWin did not restore the fullscreen client after the Dodge Active Dock check")
    _wait_for_dock_window_touch_policy("false", "floated", 1)
    _wait_for_hidden_state("false", "post-fullscreen reveal")

    print(
        "FP-2/FP-4A stable canvas held its maximum-depth reservation across qreal "
        "reversals; Always Visible, Windows Go Below, and Dodge Active Docks reached "
        "the correct maximized and fullscreen window-touch endpoints, with compositor "
        "edge reveal covered outside true fullscreen"
    )


def _cleanup() -> bool:
    cleanup_failed = False
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
        if (pid is not None and _pid_alive(pid)) or not _muted_dock_start(90):
            cleanup_failed = True
    if cleanup_failed:
        print(
            "FAIL: FP-2 stable-canvas fixture cleanup did not restore the dock configuration",
            file=sys.stderr,
            flush=True,
        )
    return cleanup_failed


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
