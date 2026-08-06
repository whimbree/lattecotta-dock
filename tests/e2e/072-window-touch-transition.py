#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""Drive FP-4A stable window-touch attachment with one real Wayland toplevel.
Direct frame placement establishes the negative control. KWin's persistent
interactive-move mode then crosses the stable trigger in both directions,
reverses both animation directions at fractional progress, and proves Escape
restores both client geometry and policy. A committed maximize supplies the
ordinary end-user path before destruction proves fail-closed count reset.

Ported from tests/e2e/072-window-touch-transition.sh to latte_harness.recipe
and latte_harness.matrix (BP-3, the bash-to-python migration's window-touch
recipe batch R6). dockSystemData carries the whole stable-canvas and transition
surface (dozens of fields no typed model models), so the snapshot is read as raw
JSON at the same boundary the bash python one-liners used; a refused reply raises
the pollable RecipeError, the same empty-command-substitution channel every bash
poller swallowed (the dock-edit-retarget-cancel precedent). The stable-contract
comparison is byte-for-byte the bash json.dumps(sort_keys=True). The coarse
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

_TITLE = "LATTE FP4 WINDOW TOUCH"
_KONSOLE_MATCH = f"|org.kde.konsole|{_TITLE}"

_GEOMETRY_KEYS = (
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


class _State:
    def __init__(self) -> None:
        self.view = 0
        self.configured = False
        self.konsole: subprocess.Popen[bytes] | None = None
        self.departure: subprocess.Popen[bytes] | None = None
        self.base_stable_snapshot: str | None = None
        self.base_revisions: str | None = None
        self.base_popup_primary_x: int | None = None
        self.base_popup_primary_width: int | None = None


_S = _State()


# ---- transport helpers -----------------------------------------------------


def _fp(*args: str) -> int:
    return subprocess.run([os.environ["E2E_FAKEPOINTER"], *args], check=False).returncode


def _fp_or_fail(fail_message: str, *args: str) -> None:
    if _fp(*args) != 0:
        recipe.fail(fail_message)


def _latte_call_or_fail(fail_message: str, *args: str) -> None:
    result = subprocess.run(
        ["busctl", "--user", "call", "org.kde.lattedock", "/Latte", "org.kde.LatteDock", *args],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        if result.stderr:
            sys.stderr.write(result.stderr)
        recipe.fail(fail_message)


def _kwrite(fail_message: str, *args: str) -> None:
    if subprocess.run(["kwriteconfig6", *args], check=False).returncode != 0:
        recipe.fail(fail_message)


def _kwin_tagged_last(body: str, collection_delay: float = 0.5) -> str:
    lines = recipe.kwin_js(body, collection_delay).splitlines()
    return lines[-1] if lines else ""


def _dock_record() -> dict[str, Any]:
    """dock_field's context: the single dockSystemData record for the view.

    schema-11 and exactly-one guards mirror the bash; a refused/empty reply
    raises the pollable RecipeError (the bash empty command substitution).
    """
    payload = recipe.json_payload("dockSystemData")
    try:
        snapshot = json.loads(payload)
    except json.JSONDecodeError:
        raise recipe.RecipeError("dockSystemData refused or returned no JSON") from None
    if snapshot["schemaVersion"] != 11:
        raise recipe.RecipeError("expected dockSystemData schema 11")
    matches = [r for r in snapshot["views"] if r["persistentDockId"] == _S.view]
    if len(matches) != 1:
        raise recipe.RecipeError(f"expected exactly one dockSystemData record for view {_S.view}")
    return matches[0]


def _view_field() -> dict[str, Any]:
    """e2e_view_field's context: the viewsData record for the view (raw JSON)."""
    views = json.loads(recipe.json_payload("viewsData"))
    match = [v for v in views if v["containmentId"] == _S.view]
    if not match:
        raise recipe.RecipeError(f"no view with containmentId {_S.view}")
    return match[0]


def _lower(value: bool) -> str:
    return "true" if value else "false"


# ---- snapshot / contract probes --------------------------------------------


def _stable_snapshot() -> str:
    payload = recipe.json_payload("dockSystemData")
    snapshot = json.loads(payload)
    matches = [r for r in snapshot["views"] if r["persistentDockId"] == _S.view]
    v = matches[0]
    obj = v["objects"]
    return json.dumps(
        {
            "reservationStateGeneration": snapshot["reservationStateGeneration"],
            "geometry": {key: v[key] for key in _GEOMETRY_KEYS},
            "objects": {
                "view": obj["view"],
                "geometryController": obj["geometryController"],
                "transitionController": obj["transitionController"],
                "windowTouchTracker": obj["windowTouchTracker"],
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


def _assert_popup_anchor_contract(boundary: str) -> None:
    try:
        x, y, width, height, paint_y, paint_height = _popup_anchor_probe()
    except recipe.RecipeError:
        recipe.fail(f"{boundary} could not read popup-anchor geometry")
    if (x, width) != (_S.base_popup_primary_x, _S.base_popup_primary_width):
        recipe.fail(
            f"{boundary} changed the popup anchor primary span: "
            f"base={_S.base_popup_primary_x}/{_S.base_popup_primary_width} current={x}/{width}"
        )
    if (y, height) != (paint_y, paint_height):
        recipe.fail(
            f"{boundary} popup anchor did not follow the outward-aligned visible mask: "
            f"anchor={y}/{height} paint={paint_y}/{paint_height}"
        )


def _assert_stable_contract(boundary: str) -> None:
    try:
        current = _stable_snapshot()
    except (recipe.RecipeError, json.JSONDecodeError, KeyError, IndexError):
        recipe.fail(f"{boundary} could not read the stable window-touch contract")
    if current != _S.base_stable_snapshot:
        recipe.fail(
            f"{boundary} changed stable QWindow, reservation, applet, trigger, or authority state: "
            f"base={_S.base_stable_snapshot} current={current}"
        )
    try:
        revisions = _revision_snapshot()
    except recipe.RecipeError:
        recipe.fail(f"{boundary} could not read physical publication revisions")
    if revisions != _S.base_revisions:
        recipe.fail(
            f"{boundary} changed physical publication revisions: "
            f"base={_S.base_revisions} current={revisions}"
        )
    _assert_popup_anchor_contract(boundary)


def _policy_probe() -> tuple[str, str, str, str, str, str, int, str, str, str, float]:
    v = _dock_record()
    return (
        _lower(v["floatingPanelEligible"]),
        _lower(v["attachOnWindowTouchConfigured"]),
        _lower(v["attachmentWaitsForPointerExitConfigured"]),
        _lower(v["pointerInsideView"]),
        _lower(v["attachmentDeferredByPointer"]),
        _lower(v["dockGapHideRequested"]),
        v["touchingWindowCount"],
        v["transitionTarget"],
        v["transitionPhase"],
        _lower(v["transitionRunning"]),
        v["transitionProgress"],
    )


def _wait_for_policy(
    expected_pointer_inside: str,
    expected_deferred: str,
    expected_count: int,
    expected_target: str,
    expected_progress: float,
    boundary: str,
) -> None:
    eligible = configured_touch = configured_wait = "unread"
    pointer_inside = deferred = dock_request = "unread"
    count = -1
    target = phase = running = "unread"
    progress = -1.0
    for _ in range(100):
        try:
            (
                eligible,
                configured_touch,
                configured_wait,
                pointer_inside,
                deferred,
                dock_request,
                count,
                target,
                phase,
                running,
                progress,
            ) = _policy_probe()
        except recipe.RecipeError:
            time.sleep(0.05)
            continue
        if (
            eligible == "true"
            and configured_touch == "true"
            and configured_wait == "true"
            and pointer_inside == expected_pointer_inside
            and deferred == expected_deferred
            and dock_request == "false"
            and count == expected_count
            and target == expected_target
            and phase == "resting"
            and running == "false"
            and abs(progress - expected_progress) < 0.000001
        ):
            if _S.base_stable_snapshot and _S.base_revisions:
                _assert_stable_contract(f"{boundary} settled")
            return
        time.sleep(0.05)
    recipe.fail(
        f"{boundary} did not settle at pointerInside={expected_pointer_inside} "
        f"deferred={expected_deferred} count={expected_count} "
        f"target={expected_target}/{expected_progress} (eligible={eligible} "
        f"configured={configured_touch} waits={configured_wait} pointerInside={pointer_inside} "
        f"deferred={deferred} dockRequest={dock_request} count={count} target={target} "
        f"phase={phase} running={running} progress={progress})"
    )


def _capture_fractional_policy(
    expected_pointer_inside: str,
    expected_deferred: str,
    expected_count: int,
    expected_target: str,
    expected_phase: str,
    boundary: str,
) -> None:
    eligible = configured_touch = configured_wait = "unread"
    pointer_inside = deferred = dock_request = "unread"
    count = -1
    target = phase = running = "unread"
    progress = -1.0
    for _ in range(100):
        try:
            (
                eligible,
                configured_touch,
                configured_wait,
                pointer_inside,
                deferred,
                dock_request,
                count,
                target,
                phase,
                running,
                progress,
            ) = _policy_probe()
        except recipe.RecipeError:
            time.sleep(0.01)
            continue
        if (
            eligible == "true"
            and configured_touch == "true"
            and configured_wait == "true"
            and pointer_inside == expected_pointer_inside
            and deferred == expected_deferred
            and dock_request == "false"
            and count == expected_count
            and target == expected_target
            and phase == expected_phase
            and running == "true"
            and 0.0 < progress < 1.0
        ):
            _assert_stable_contract(f"{boundary} fractional")
            return
        time.sleep(0.01)
    recipe.fail(
        f"{boundary} exposed no fractional {expected_phase} state (eligible={eligible} "
        f"configured={configured_touch} waits={configured_wait} pointerInside={pointer_inside} "
        f"deferred={deferred} dockRequest={dock_request} count={count} target={target} "
        f"phase={phase} running={running} progress={progress})"
    )


# ---- konsole fixture helpers -----------------------------------------------


def _konsole_count() -> int:
    return sum(1 for line in recipe.dumpwins().splitlines() if _KONSOLE_MATCH in line)


def _konsole_geometry() -> tuple[int, int, int, int, str] | None:
    rows = [line for line in recipe.dumpwins().splitlines() if _KONSOLE_MATCH in line]
    if not rows:
        return None
    window = recipe.parse_dumpwins(rows[-1])
    if not window:
        return None
    w = window[0]
    return w.x, w.y, w.width, w.height, w.output


def _move_konsole_for_setup(x: int, y: int, width: int, height: int) -> str:
    return _kwin_tagged_last(
        f"""for (const w of workspace.windowList()) {{
        if (w.resourceClass === 'org.kde.konsole'
                && w.caption.includes('{_TITLE}')) {{
            const geometry = Object.assign({{}}, w.frameGeometry);
            geometry.x = {x};
            geometry.y = {y};
            geometry.width = {width};
            geometry.height = {height};
            w.frameGeometry = geometry;
            workspace.activeWindow = w;
            print('@TAG@|' + w.internalId);
        }}
    }}"""
    )


def _wait_for_konsole_geometry(x: int, y: int, width: int, height: int) -> None:
    actual: tuple[int, int, int, int, str] | None = None
    for _ in range(60):
        actual = _konsole_geometry()
        if actual is not None and actual[0] == x and actual[1] == y and actual[2] == width and actual[3] == height:
            return
        time.sleep(0.05)
    got = actual if actual is not None else ("unread", "unread", "unread", "unread")
    recipe.fail(
        f"KWin did not place the single client at {x},{y} {width}x{height} "
        f"(actual={got[0]},{got[1]} {got[2]}x{got[3]})"
    )


def _active_window_id() -> str:
    return _kwin_tagged_last(
        'print("@TAG@|" + (workspace.activeWindow ? workspace.activeWindow.internalId : "none"));'
    )


def _invoke_window_move() -> int:
    return subprocess.run(
        [
            "busctl",
            "--user",
            "call",
            "org.kde.kglobalaccel",
            "/component/kwin",
            "org.kde.kglobalaccel.Component",
            "invokeShortcut",
            "s",
            "Window Move",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    ).returncode


def _nudge_vertical(key: str, count: int) -> None:
    for _ in range(count):
        _fp_or_fail(f"could not deliver {key} during KWin interactive move", "key", key)
        time.sleep(0.02)


def _set_konsole_maximized(enabled: bool) -> str:
    return _kwin_tagged_last(
        f"""for (const w of workspace.windowList()) {{
        if (w.resourceClass === 'org.kde.konsole'
                && w.caption.includes('{_TITLE}')) {{
            workspace.activeWindow = w;
            w.setMaximize({_lower(enabled)}, {_lower(enabled)});
            print('@TAG@|' + w.internalId);
        }}
    }}""",
        0.05,
    )


def _konsole_maximize_mode() -> str:
    return _kwin_tagged_last(
        f"""for (const w of workspace.windowList()) {{
        if (w.resourceClass === 'org.kde.konsole'
                && w.caption.includes('{_TITLE}')) {{
            print('@TAG@|' + w.maximizeMode);
        }}
    }}""",
        0.01,
    )


def _wait_for_maximize_mode(expected: str) -> None:
    actual = "unread"
    for _ in range(60):
        actual = _konsole_maximize_mode()
        if actual == expected:
            return
        time.sleep(0.05)
    recipe.fail(f"KWin maximize mode did not become {expected} (actual={actual})")


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
        recipe.fail("could not realize the stable window-touch fixture")
    try:
        _S.view = matrix.view_id()
    except matrix.MatrixProbeError:
        recipe.fail("could not resolve the stable window-touch fixture")
    view = _S.view
    layout = os.environ["E2E_LAYOUT"]
    group = ("--file", layout, "--group", "Containments", "--group", str(view), "--group", "General")

    if not recipe.dock_stop():
        recipe.fail("dock did not stop before window-touch fixture configuration")
    _kwrite("could not disable maximize-driven panel length", *group, "--key", "maximizeWhenMaximized", "false")
    _kwrite("could not configure a partial panel length", *group, "--key", "maxLength", "60")
    _kwrite("could not configure attachment on stable window touch", *group, "--key", "hideFloatingGapForMaximized", "true")
    _kwrite("could not configure pointer deferral", *group, "--key", "floatingGapHidingWaitsMouse", "true")
    _kwrite("could not configure the floating gap", *group, "--key", "screenEdgeMargin", "18")
    _kwrite("could not keep the floating gap under panel-surface ownership", *group, "--key", "floatingInternalGapIsForced", "false")
    _kwrite("could not configure Justify alignment", *group, "--key", "alignment", "10")
    _kwrite("could not mark Justify alignment as upgraded", *group, "--key", "alignmentUpgraded", "true")
    if not _muted_dock_start(90):
        recipe.fail("dock did not restart with the window-touch fixture")
    _latte_call_or_fail("could not set the fixture view to alwaysVisible", "setViewVisibilityMode", "us", str(view), "alwaysVisible")
    for _ in range(40):
        with suppress(recipe.RecipeError):
            if _view_field()["visibilityMode"] == "alwaysVisible":
                break
        time.sleep(0.25)
    try:
        settled = _view_field()["visibilityMode"] == "alwaysVisible"
    except recipe.RecipeError:
        settled = False
    if not settled:
        recipe.fail(f"view {view} did not enter alwaysVisible mode")

    _fp_or_fail("could not park the pointer outside the panel", "move", "20", "20")

    record = _dock_record()
    view_type = record["type"]
    floating_gap_configured = _lower(record["floatingGapConfigured"])
    configured_panel = _lower(record["floatingPanelConfigured"])
    eligible_panel = _lower(record["floatingPanelEligible"])
    geometry_present = _lower(record["transitionGeometryPresent"])
    alignment = record["alignment"]
    if not (
        view_type == "panel"
        and floating_gap_configured == "true"
        and configured_panel == "true"
        and eligible_panel == "true"
        and geometry_present == "true"
        and alignment == "justify"
    ):
        recipe.fail(
            f"fixture is not one eligible Justify panel with stable geometry "
            f"(type={view_type} floatingGapConfigured={floating_gap_configured} "
            f"configuredPanel={configured_panel} eligible={eligible_panel} "
            f"geometry={geometry_present} alignment={alignment})"
        )

    _wait_for_policy("false", "false", 0, "floated", 1, "initial negative control")

    record = _dock_record()
    trigger_x, trigger_y, trigger_width, trigger_height = record["stableTriggerGeometry"]
    screen_x, screen_y, screen_width, screen_height = record["screenGeometry"]
    if not (trigger_width > 0 and trigger_height > 0):
        recipe.fail(
            f"stable trigger is invalid: {trigger_x},{trigger_y} {trigger_width}x{trigger_height}"
        )

    try:
        _S.base_stable_snapshot = _stable_snapshot()
    except (json.JSONDecodeError, KeyError, IndexError):
        recipe.fail("could not capture the base stable window-touch contract")
    try:
        _S.base_revisions = _revision_snapshot()
    except recipe.RecipeError:
        recipe.fail("could not capture base physical publication revisions")
    try:
        anchor = _popup_anchor_probe()
    except recipe.RecipeError:
        recipe.fail("could not capture the base popup-anchor primary span")
    _S.base_popup_primary_x = anchor[0]
    _S.base_popup_primary_width = anchor[2]
    _assert_stable_contract("initial settled panel")
    transition_token = _dock_record()["objects"]["transitionController"]
    tracker_token = _dock_record()["objects"]["windowTouchTracker"]
    if not (transition_token and tracker_token and transition_token != tracker_token):
        recipe.fail("transition and window-touch authorities are absent or aliased")

    if _konsole_count() != 0:
        recipe.fail("a tagged Konsole already exists; this recipe owns one client")
    _S.konsole = subprocess.Popen(
        ["konsole", "-p", f"LocalTabTitleFormat={_TITLE}"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )
    for _ in range(30):
        if _konsole_count() == 1:
            break
        time.sleep(0.5)
    if _konsole_count() != 1:
        recipe.fail("the single Konsole window-touch client never mapped")

    geometry_role_type = ""
    for _ in range(40):
        with suppress(recipe.RecipeError):
            geometry_role_type = _dock_record()["windowTouchGeometryRoleType"]
        if geometry_role_type == "QRect":
            break
        time.sleep(0.05)
    if geometry_role_type != "QRect":
        recipe.fail(
            f"the live TasksModel Geometry role was not observed as QRect (type={geometry_role_type})"
        )

    fixture_id = _set_konsole_maximized(False)
    if not fixture_id or "\n" in fixture_id:
        recipe.fail("KWin found an invalid number of tagged clients")
    _wait_for_maximize_mode("0")

    geometry = _konsole_geometry()
    if geometry is None:
        recipe.fail("the single client reported invalid geometry")
    client_width, client_height = geometry[2], geometry[3]
    if not (client_width > 0 and client_height > 0):
        recipe.fail("the single client reported invalid geometry")

    step = 8
    touch_nudges = 3
    baseline_x = trigger_x + trigger_width // 2 - client_width // 2
    minimum_x = screen_x + 20
    maximum_x = screen_x + screen_width - client_width - 20
    if baseline_x < minimum_x:
        baseline_x = minimum_x
    if baseline_x > maximum_x:
        baseline_x = maximum_x
    baseline_y = trigger_y - client_height - (touch_nudges - 1) * step

    placement_id = _move_konsole_for_setup(baseline_x, baseline_y, client_width, client_height)
    if placement_id != fixture_id:
        recipe.fail(
            f"KWin setup placement targeted a different client "
            f"(fixture={fixture_id} placement={placement_id})"
        )
    _wait_for_konsole_geometry(baseline_x, baseline_y, client_width, client_height)
    _wait_for_policy("false", "false", 0, "floated", 1, "direct-placement negative control")

    _fp_or_fail(
        "could not activate the owned client",
        "click",
        str(baseline_x + client_width // 2),
        str(baseline_y + client_height // 2),
    )
    for _ in range(20):
        if _active_window_id() == fixture_id:
            break
        time.sleep(0.05)
    if _active_window_id() != fixture_id:
        recipe.fail("the owned client did not become KWin's active window")

    # One persistent interactive move crosses in, out, in, and out. The first
    # two reversals happen before either animation reaches an endpoint.
    if _invoke_window_move() != 0:
        recipe.fail("could not start KWin interactive move")
    time.sleep(0.2)
    _nudge_vertical("Down", touch_nudges)
    _capture_fractional_policy("false", "false", 1, "attached", "attaching", "interactive drag into stable trigger")
    _nudge_vertical("Up", touch_nudges)
    _capture_fractional_policy("false", "false", 0, "floated", "floating", "fractional attaching-to-floating reversal")
    _nudge_vertical("Down", touch_nudges)
    _capture_fractional_policy("false", "false", 1, "attached", "attaching", "fractional floating-to-attaching reversal")
    _nudge_vertical("Up", touch_nudges)
    _capture_fractional_policy("false", "false", 0, "floated", "floating", "interactive drag back out")
    _fp_or_fail("could not commit the interactive move outside the trigger", "key", "Return")
    _wait_for_konsole_geometry(baseline_x, baseline_y, client_width, client_height)
    _wait_for_policy("false", "false", 0, "floated", 1, "interactive out-of-trigger commit")

    # Escape must cancel a second in-flight move after it has crossed the trigger.
    if _invoke_window_move() != 0:
        recipe.fail("could not start the cancelable KWin interactive move")
    time.sleep(0.2)
    _nudge_vertical("Down", touch_nudges)
    _capture_fractional_policy("false", "false", 1, "attached", "attaching", "cancel trial trigger crossing")
    _fp_or_fail("could not cancel the in-flight move with Escape", "key", "Escape")
    _wait_for_konsole_geometry(baseline_x, baseline_y, client_width, client_height)
    _wait_for_policy("false", "false", 0, "floated", 1, "Escape geometry and policy restoration")

    # A committed full maximize is the normal non-scripted geometry path into
    # the reserved edge. MaximizeMode 3 proves KWin accepted both axes.
    if _set_konsole_maximized(True) != fixture_id:
        recipe.fail("KWin did not identify the owned client for committed maximize")
    # Sample the fractional frame FIRST: the D259 200 ms transition (cd74a9244)
    # starts while set_konsole_maximized's script applies, and one
    # wait_for_maximize_mode kwin_js round trip costs more than the whole
    # animation. MaximizeMode 3 is verified after the capture; the endpoint
    # policy still proves the attached target.
    _capture_fractional_policy("false", "false", 1, "attached", "attaching", "committed maximize attachment")
    _wait_for_maximize_mode("3")
    _wait_for_policy("false", "false", 1, "attached", 0, "committed maximize attachment")

    record = _dock_record()
    canvas_x = record["stableCanvasGeometry"][0]
    canvas_y = record["stableCanvasGeometry"][1]
    visible_x = record["currentVisibleGeometry"][0]
    visible_y = record["currentVisibleGeometry"][1]
    visible_width = record["currentVisibleGeometry"][2]
    visible_height = record["currentVisibleGeometry"][3]
    pointer_x = canvas_x + visible_x + visible_width // 2
    pointer_y = canvas_y + visible_y + visible_height // 2
    _fp_or_fail("could not move the pointer inside the attached panel", "glide", "20", "20", str(pointer_x), str(pointer_y))
    _wait_for_policy("true", "false", 1, "attached", 0, "pointer entry preserves the existing attachment")

    if _set_konsole_maximized(False) != fixture_id:
        recipe.fail("KWin did not identify the owned client for pointer-held touch loss")
    # Same sampling order as the committed maximize: fractional frame first,
    # MaximizeMode 0 verified after the capture.
    _capture_fractional_policy("true", "false", 0, "floated", "floating", "pointer-held touch loss")
    _wait_for_maximize_mode("0")
    _wait_for_policy("true", "false", 0, "floated", 1, "pointer-held touch loss")

    if _set_konsole_maximized(True) != fixture_id:
        recipe.fail("KWin did not identify the owned client for pointer-present attachment")
    _wait_for_maximize_mode("3")
    _wait_for_policy("true", "true", 1, "floated", 1, "pointer-present attachment deferral")

    # D259 (cd74a9244) gave the deferral-release attachment Plasma's 200 ms
    # Kirigami long duration, so the whole animation now completes inside the
    # departure glide's own tail: sampling after fp returns can only observe
    # the attached endpoint. Sample DURING the departure with the glide in the
    # background - the one-live-input-device pattern of the screen-edge round
    # trip in 071, which also keeps the device alive so the panel's pointer
    # leave cannot be lost to a device swap - then reap the gesture before
    # asserting the endpoint.
    _S.departure = subprocess.Popen(
        [os.environ["E2E_FAKEPOINTER"], "glide", str(pointer_x), str(pointer_y), "20", "20"]
    )
    _capture_fractional_policy("false", "false", 1, "attached", "attaching", "pointer deferral release")
    departure = _S.departure
    if departure.wait() != 0:
        recipe.fail("could not move the pointer out of the panel")
    _S.departure = None
    _wait_for_policy("false", "false", 1, "attached", 0, "pointer deferral release")

    konsole = _S.konsole
    if konsole is None:
        recipe.fail("could not destroy the single window-touch client")
    try:
        os.kill(konsole.pid, signal.SIGTERM)
    except ProcessLookupError:
        recipe.fail("could not destroy the single window-touch client")
    _capture_fractional_policy("false", "false", 0, "floated", "floating", "client destruction reset")
    with suppress(Exception):
        konsole.wait()
    _S.konsole = None
    for _ in range(40):
        if _konsole_count() == 0:
            break
        time.sleep(0.1)
    if _konsole_count() != 0:
        recipe.fail("the single window-touch client remained mapped after destruction")
    _wait_for_policy("false", "false", 0, "floated", 1, "client destruction reset")

    print(
        "FP-4A stable window touch passed interactive reversals, Escape restoration, "
        "existing-attachment preservation, pointer-present deferral, destruction reset, "
        "and zero stable-surface revision drift"
    )


def _cleanup() -> bool:
    cleanup_failed = False
    if _S.departure is not None:
        with suppress(ProcessLookupError):
            _S.departure.terminate()
        with suppress(Exception):
            _S.departure.wait()
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
        if pid is not None and _pid_alive(pid):
            cleanup_failed = True
        elif not _muted_dock_start(90):
            cleanup_failed = True
    if cleanup_failed:
        print(
            "FAIL: FP-4A window-touch fixture cleanup did not restore the dock configuration",
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
