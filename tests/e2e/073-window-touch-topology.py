#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""FP-4B multi-output and separated-span acceptance. Three independent partial
floating panels retain per-view window-touch, presentation and input ownership
while the physical outputs move through full-touching, partial-touching and
disconnected arrangements. Output adjacency never changes the two
output-identity-plus-edge reservation groups.

Ported from tests/e2e/073-window-touch-topology.sh to latte_harness.multi_output
+ latte_harness.matrix + latte_harness.recipe (BP-3, the bash-to-python
migration's deferred dual-output topology recipe). Every assertion, poll bound,
iteration count, expected status, marker and failure message is byte-identical,
save the D284 axis-change fix (the phase-2 windowGeometry == surfaceGeometry
check, which raced the settle, was dropped bash-first and then ported); the
drive is NOT trimmed. The typed rectangle oracle
(tests/e2e/fixtures/fp4b/oracle.py) is still driven as a subprocess (identical
``python3 <oracle> <subcommand>`` argv and stdin/stdout as the bash), so the
oracle half of the contract is untouched. dockSystemData / screensData carry
fields the typed models do not, so the recipe reads them as raw JSON at the same
boundary the bash python one-liners used - recipe.read_json where the recipe
parses (a refusal raises the pollable DbusUnavailableError, read as a non-match
exactly like the bash predicate exiting non-zero), recipe.json_payload where the
delivered text itself is the artifact (oracle stdin, failure snapshots).

The cleanup safety net (cleanup runs on every exit path, preserves the body's
failure status, never masks it with a cleanup success, and enforces the teardown
ordering - restore the captured output topology, stop the dock, replace the
config, and restore the config BEFORE restarting a dock proven gone) is factored
into the pure, importable ``perform_topology_cleanup`` decision core so it can be
driven in-process with mocks. The behavioral proof lives in
harness/tests/test_topology_cleanup.py (the redesign of the bash cleanup-EVAL
sourceguard test, which eval-executed the shell function body in a mock harness
and has no direct Python analog); the transactional structure is still pinned by
sourceguardtest's matchesWindowTouchTopologyE2eContract, retargeted to this
recipe.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import time
from collections.abc import Iterator
from contextlib import suppress
from dataclasses import dataclass
from typing import Any, cast

from latte_harness import matrix, multi_output, recipe
from latte_harness.proc import install_conventional_signal_exits
from latte_harness.topology_cleanup import (
    TopologyCleanupDeps,
    perform_topology_cleanup,
)

E2E_REPO = os.environ["E2E_REPO"]
ORACLE = f"{E2E_REPO}/tests/e2e/fixtures/fp4b/oracle.py"
WINDOW_QML = f"{E2E_REPO}/tests/e2e/fixtures/fp4b/window.qml"
CLIENT_TITLE = "LATTE FP4B TOPOLOGY WINDOW"


def _warn(message: str) -> None:
    print(message, file=sys.stderr, flush=True)


@dataclass
class _State:
    """The recipe's mutable transaction state (the bash top-of-file globals)."""

    layout: str = ""
    view_a: int = 0
    view_b: int = 0
    view_c: int = 0
    view_ids_csv: str = ""
    client: subprocess.Popen[bytes] | None = None
    fixture_transaction_active: bool = False
    topology_captured: bool = False
    original_topology: str = ""
    baseline_stable: str = ""
    previous_anchor_revisions: str = ""


_S = _State()


# ---- D-Bus / config / oracle adapters --------------------------------------


def _snapshot() -> str:
    """dockSystemData as plain JSON text (the bash ``snapshot``).

    Deliberately raw: the text feeds the oracle's stdin and the failure
    artifacts, where the delivered bytes ARE the evidence; a refusal flows on
    as empty input, the established non-answer the oracle already refuses.
    Sites that parse in-recipe use recipe.read_json instead.
    """
    return recipe.json_payload("dockSystemData")


def _screens_json() -> str:
    """screensData as plain JSON text (the bash ``mo_screens_json``); raw for
    the same oracle-stdin reason as _snapshot."""
    return recipe.json_payload("screensData")


def _oracle(
    subcommand: str, *args: str, stdin: str = "", quiet: bool = False
) -> subprocess.CompletedProcess[str]:
    """Drive oracle.py as a subprocess (the bash ``python3 "$ORACLE"``).

    Identical argv, stdin and stdout to the shell; ``quiet`` is the bash
    ``2>/dev/null`` the polling probes used to hide the oracle's not-yet verdicts.
    """
    result = subprocess.run(
        ["python3", ORACLE, subcommand, *args],
        input=stdin,
        capture_output=True,
        text=True,
        check=False,
    )
    if not quiet and result.stderr:
        sys.stderr.write(result.stderr)
    return result


def _call(fail_message: str, *args: str) -> None:
    """``e2e_call ... >/dev/null || e2e_fail``: run a lattedock action, forward
    busctl stderr, and fail loudly on a D-Bus error."""
    result = subprocess.run(
        [
            "busctl",
            "--user",
            "call",
            "org.kde.lattedock",
            "/Latte",
            "org.kde.LatteDock",
            *args,
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        if result.stderr:
            sys.stderr.write(result.stderr)
        recipe.fail(fail_message)


def _kwriteconfig(*args: str) -> bool:
    return subprocess.run(["kwriteconfig6", *args], check=False).returncode == 0


# ---- view identity and duplication -----------------------------------------


def _view_ids() -> str:
    """view_ids: the space-joined persistentDockId of every view."""
    views = recipe.read_json("dockSystemData")["views"]
    return " ".join(str(view["persistentDockId"]) for view in views)


def _created_view_id(before: str) -> str:
    """created_view_id: the single independent view not present in ``before``, or
    "" when zero or many appeared (or the snapshot was refused/malformed)."""
    try:
        before_ids = {int(value) for value in before.split()}
        created = [
            view
            for view in recipe.read_json("dockSystemData")["views"]
            if view["persistentDockId"] not in before_ids and view["relationship"] == "independent"
        ]
    except recipe.DbusUnavailableError, KeyError, ValueError:
        return ""
    return str(created[0]["persistentDockId"]) if len(created) == 1 else ""


def _duplicate_independently(source: int, boundary: str) -> int:
    """duplicate_independently: duplicate ``source`` and resolve the one new
    independent dock, polling 80x0.25s for it to appear."""
    before = _view_ids()
    _call(f"{boundary} duplicateView call failed", "duplicateView", "u", str(source))
    candidate = ""
    for _ in range(80):
        candidate = _created_view_id(before)
        if candidate:
            break
        time.sleep(0.25)
    if not candidate:
        recipe.fail(f"{boundary} did not create exactly one independent dock")
    return int(candidate)


def _configure_panel(view: int, icon_size: int, length: int) -> None:
    """configure_panel: pin the per-view window-touch panel geometry via kwriteconfig6."""
    group = [
        "--file",
        _S.layout,
        "--group",
        "Containments",
        "--group",
        str(view),
        "--group",
        "General",
    ]

    def write(key: str, value: str, fail_message: str) -> None:
        if not _kwriteconfig(*group, "--key", key, value):
            recipe.fail(fail_message)

    write(
        "iconSize",
        str(icon_size),
        f"could not set icon size {icon_size} for panel {view}",
    )
    write(
        "minLength",
        str(length),
        f"could not set minimum length {length} for panel {view}",
    )
    write(
        "maxLength",
        str(length),
        f"could not set maximum length {length} for panel {view}",
    )
    write(
        "maximizeWhenMaximized",
        "false",
        f"could not disable maximize-driven length for panel {view}",
    )
    write(
        "hideFloatingGapForMaximized",
        "true",
        f"could not enable window-touch attachment for panel {view}",
    )
    write(
        "floatingGapHidingWaitsMouse",
        "false",
        f"could not disable pointer deferral for panel {view}",
    )
    write("screenEdgeMargin", "18", f"could not set the floating gap for panel {view}")
    write(
        "floatingInternalGapIsForced",
        "false",
        f"could not retain panel-owned floating gap for panel {view}",
    )
    write("zoomLevel", "0", f"could not keep panel {view} at resting scale")
    write(
        "useThemePanel",
        "true",
        f"could not retain theme panel behavior for panel {view}",
    )
    write(
        "panelSize",
        "100",
        f"could not retain full panel background thickness for panel {view}",
    )


def _resolve_screen_id(name: str, fail_message: str) -> int:
    """screen_id_for_name: the ScreenPool id of the single active screen named
    ``name`` (the bash inner diagnostic, then the caller's loud refusal)."""
    matches = [
        screen
        for screen in recipe.read_json("screensData")
        if screen["isActive"] and screen["name"] == name
    ]
    if len(matches) != 1:
        _warn(f"expected one active screen named {name}, got {len(matches)}")
        recipe.fail(fail_message)
    return int(matches[0]["id"])


# ---- placement and publication waiters -------------------------------------


def _views_by_dock_id() -> dict[int, dict[str, Any]]:
    """The live dockSystemData views keyed by persistentDockId (read_json, so a
    refusal raises the pollable DbusUnavailableError the callers below catch)."""
    return {view["persistentDockId"]: view for view in recipe.read_json("dockSystemData")["views"]}


def _fixture_placement_settled(primary_id: int, secondary_id: int) -> bool:
    try:
        ids = [int(value) for value in _S.view_ids_csv.split(",")]
        views = _views_by_dock_id()
        expected = {
            ids[0]: (primary_id, "bottom", "left"),
            ids[1]: (primary_id, "bottom", "right"),
            ids[2]: (secondary_id, "left", "center"),
        }
        if set(views) != set(ids):
            return False
        for dock_id, placement in expected.items():
            view = views[dock_id]
            actual = (view["screenId"], view["edge"], view["alignment"])
            if actual != placement or not view["geometrySettled"]:
                return False
    except recipe.DbusUnavailableError, KeyError, ValueError, IndexError:
        return False
    return True


def _wait_for_fixture_placement(primary_id: int, secondary_id: int) -> None:
    for _ in range(120):
        if _fixture_placement_settled(primary_id, secondary_id):
            return
        time.sleep(0.25)
    recipe.fail(
        "the three independent panels did not settle at their requested output-edge placements"
    )


def _publication_revision(dock_id: int, fail_message: str) -> int:
    """before_axis_revision: the view's current surfaceGeometryPublicationRevision."""
    view = _views_by_dock_id().get(dock_id)
    if not view:
        recipe.fail(fail_message)
    return int(view["surfaceGeometryPublicationRevision"])


def _axis_first_publication_reached(
    dock_id: int, expected: tuple[int, str, str], before_revision: int
) -> bool:
    try:
        view = _views_by_dock_id().get(dock_id)
        if not view:
            return False
        actual = (view["screenId"], view["edge"], view["alignment"])
        if (
            actual != expected
            or view["relocationGeneration"] != view["appliedRelocationGeneration"]
            or int(view["surfaceGeometryPublicationRevision"]) <= before_revision
            or view["windowGeometry"] != view["surfaceGeometry"]
        ):
            return False
    except recipe.DbusUnavailableError, KeyError, ValueError:
        return False
    return True


def _axis_extra_publication_diagnostic(
    dock_id: int, expected: tuple[int, str, str], before_revision: int
) -> str | None:
    """None when the placement published exactly once (before_revision + 1); the
    bash SystemExit diagnostic string when the validator republished or drifted.

    D284: the bash recipe also asserted windowGeometry == surfaceGeometry at this
    fixed 800 ms instant, but that convergence check races the axis-change settle
    (which runs past the 800 ms coalescer deadline in the nested vehicle) and is
    already polled to settlement by phase 3 (_axis_settled). It is dropped so
    phase 2 keeps only its unique contract - no extra publication after the
    coalescer deadline, and no placement drift. Fixed bash-first, then ported.
    """
    view = _views_by_dock_id().get(dock_id)
    if not view:
        return "axis-changing panel disappeared"
    actual = (view["screenId"], view["edge"], view["alignment"])
    if actual != expected:
        return f"axis-changing placement drifted: {actual!r}"
    if int(view["surfaceGeometryPublicationRevision"]) != before_revision + 1:
        return "geometry validator republished a completed placement"
    return None


def _axis_settled(dock_id: int, expected: tuple[int, str, str]) -> bool:
    try:
        view = _views_by_dock_id().get(dock_id)
        if not view:
            return False
        actual = (view["screenId"], view["edge"], view["alignment"])
        if (
            actual != expected
            or view["relocationGeneration"] != view["appliedRelocationGeneration"]
            or view["windowGeometry"] != view["surfaceGeometry"]
        ):
            return False
    except recipe.DbusUnavailableError, KeyError, ValueError:
        return False
    return view["geometrySettled"]


def _assert_axis_change_publishes_once(
    view: int, screen_id: int, edge: str, alignment: str, before_revision: int
) -> None:
    expected = (screen_id, edge, alignment)
    artifacts = os.environ["E2E_ARTIFACTS"]

    published = False
    for _ in range(150):
        if _axis_first_publication_reached(view, expected, before_revision):
            published = True
            break
        time.sleep(0.02)
    if not published:
        recipe.fail("axis-changing placement never reached its first complete publication")

    # Old coalescers could fire at 150 ms directly or at 650 ms after the
    # validator. Compare with the pre-mutation revision after both deadlines;
    # even a missed intermediate sample cannot hide an extra publication.
    time.sleep(0.8)
    diagnostic = _axis_extra_publication_diagnostic(view, expected, before_revision)
    if diagnostic is not None:
        _warn(diagnostic)
        with open(
            f"{artifacts}/fp4b-axis-change-extra-publication.json",
            "w",
            encoding="utf-8",
        ) as stream:
            stream.write(_snapshot())
        with open(
            f"{artifacts}/fp4b-axis-change-before-revision.txt", "w", encoding="utf-8"
        ) as stream:
            stream.write(f"{before_revision}\n")
        recipe.fail("axis-changing placement scheduled a redundant geometry publication")

    # Settlement also includes longer-lived presentation bookkeeping and may
    # include a later content-driven publication. Wait for convergence without
    # conflating it with the validator deadline checked above.
    for _ in range(120):
        if _axis_settled(view, expected):
            return
        time.sleep(0.25)
    with open(f"{artifacts}/fp4b-axis-change-unsettled.json", "w", encoding="utf-8") as stream:
        stream.write(_snapshot())
    recipe.fail("axis-changing placement did not settle")


# ---- compositor-window structure and stable-projection oracles -------------


_DOCK_WINDOWS_JS = """for (const window of workspace.windowList()) {
        if (String(window.resourceClass) === 'latte-dock' && window.layer === 3) {
            print('@TAG@|' + JSON.stringify({
                id: String(window.internalId),
                geometry: [
                    Math.round(window.frameGeometry.x),
                    Math.round(window.frameGeometry.y),
                    Math.round(window.frameGeometry.width),
                    Math.round(window.frameGeometry.height)
                ],
                output: window.output ? window.output.name : null
            }));
        }
    }"""


def _dock_windows_json() -> str | None:
    """dock_windows_json: the compositor-owned layer-3 latte-dock windows, packed
    into one compact JSON array; None on a malformed row (the bash return 1)."""
    rows = [line for line in recipe.kwin_js(_DOCK_WINDOWS_JS).splitlines() if line]
    try:
        return json.dumps([json.loads(row) for row in rows], separators=(",", ":"))
    except json.JSONDecodeError:
        return None


def _assert_structure() -> tuple[int, str]:
    """assert_structure: dump the layer-3 windows, then the oracle assert-structure.
    Returns (returncode, combined stdout+stderr) for the ``$(... 2>&1)`` capture."""
    windows_file = f"{os.environ['E2E_ARTIFACTS']}/fp4b-dock-windows.json"
    windows = _dock_windows_json()
    if windows is None:
        return 1, "could not capture compositor-owned dock windows"
    with open(windows_file, "w", encoding="utf-8") as stream:
        stream.write(windows)
    result = _oracle(
        "assert-structure",
        "--ids",
        _S.view_ids_csv,
        "--windows",
        windows_file,
        stdin=_snapshot(),
        quiet=True,
    )
    return result.returncode, result.stdout + result.stderr


def _stable_projection(*, quiet: bool = False) -> str | None:
    """stable_projection: the oracle's stable projection, or None when it refuses."""
    result = _oracle("stable-projection", "--ids", _S.view_ids_csv, stdin=_snapshot(), quiet=quiet)
    return result.stdout if result.returncode == 0 else None


def _stable_matches_client_baseline() -> bool:
    current = _stable_projection(quiet=True)
    return current is not None and current == _S.baseline_stable


def _persistent_projection(fail_message: str) -> str:
    result = _oracle("persistent-projection", "--ids", _S.view_ids_csv, stdin=_snapshot())
    if result.returncode != 0:
        recipe.fail(fail_message)
    return result.stdout


def _wait_for_stable_topology(expected: str) -> None:
    try:
        multi_output.mo_assert_output_topology(expected)
    except multi_output.MultiOutputError:
        recipe.fail(f"output helper did not observe {expected}")
    if _oracle("assert-topology", expected, stdin=_screens_json()).returncode != 0:
        recipe.fail(f"typed rectangle oracle did not observe {expected}")

    previous = ""
    structure_error = ""
    for _ in range(120):
        current = _stable_projection(quiet=True) or ""
        if current and current == previous:
            returncode, structure_error = _assert_structure()
            if returncode == 0:
                verified = _stable_projection(quiet=True) or ""
                if verified == current:
                    _S.baseline_stable = verified
                    anchors = _oracle(
                        "anchor-revisions", "--ids", _S.view_ids_csv, stdin=_snapshot()
                    )
                    if anchors.returncode != 0:
                        recipe.fail("could not capture popup anchor revisions")
                    _S.previous_anchor_revisions = anchors.stdout
                    return
        previous = current
        time.sleep(0.25)
    if structure_error:
        _warn(f"last unsettled structure: {structure_error}")
    recipe.fail(f"{expected} output mutation did not converge to two identical stable snapshots")


def _print_stable_diff(baseline_json: str, current_json: str) -> None:
    baseline = json.loads(baseline_json)
    current = json.loads(current_json)

    def differences(left: Any, right: Any, path: str = "$") -> Iterator[str]:
        if type(left) is not type(right):
            yield f"{path}: type {type(left).__name__} -> {type(right).__name__}"
        elif isinstance(left, dict):
            left_dict = cast("dict[str, Any]", left)
            right_dict = cast("dict[str, Any]", right)
            for key in sorted(set(left_dict) | set(right_dict)):
                if key not in left_dict:
                    yield f"{path}.{key}: added {right_dict[key]!r}"
                elif key not in right_dict:
                    yield f"{path}.{key}: removed {left_dict[key]!r}"
                else:
                    yield from differences(left_dict[key], right_dict[key], f"{path}.{key}")
        elif isinstance(left, list):
            left_list = cast("list[Any]", left)
            right_list = cast("list[Any]", right)
            if len(left_list) != len(right_list):
                yield f"{path}: length {len(left_list)} -> {len(right_list)}"
            # strict=False deliberately: the length mismatch is reported just
            # above and the common prefix is still compared (not an error here).
            for index, (before, after) in enumerate(zip(left_list, right_list, strict=False)):
                yield from differences(before, after, f"{path}[{index}]")
        elif left != right:
            yield f"{path}: {left!r} -> {right!r}"

    for line in list(differences(baseline, current))[:30]:
        _warn(line)


def _assert_anchor_revisions_monotonic(previous: str, current: str, fail_message: str) -> None:
    previous_values = [int(value) for value in previous.split()]
    current_values = [int(value) for value in current.split()]
    if len(previous_values) != 3 or len(current_values) != 3:
        _warn("expected three popup-anchor revisions")
        recipe.fail(fail_message)
    if any(after < before for before, after in zip(previous_values, current_values, strict=True)):
        _warn("popup-anchor revision moved backwards")
        recipe.fail(fail_message)


def _assert_stable_after_client_change(boundary: str) -> None:
    current = _stable_projection()
    if current is None:
        recipe.fail(f"{boundary} could not read the stable projection")
    if current != _S.baseline_stable:
        _print_stable_diff(_S.baseline_stable, current)
        recipe.fail(
            f"{boundary} changed stable surface, reservation, trigger, sizing, or authority state"
        )
    anchors = _oracle("anchor-revisions", "--ids", _S.view_ids_csv, stdin=_snapshot())
    if anchors.returncode != 0:
        recipe.fail(f"{boundary} could not read popup anchor revisions")
    _assert_anchor_revisions_monotonic(
        _S.previous_anchor_revisions,
        anchors.stdout,
        f"{boundary} violated monotonic popup-anchor revisions",
    )
    _S.previous_anchor_revisions = anchors.stdout


# ---- QML client window driving ---------------------------------------------


def _client_rows() -> str:
    """client_rows: the tagged rows for every window titled CLIENT_TITLE."""
    return recipe.kwin_js(
        f"""for (const window of workspace.windowList()) {{
        if (window.caption === '{CLIENT_TITLE}') {{
            print('@TAG@|' + String(window.internalId)
                + ' ' + Math.round(window.frameGeometry.x)
                + ' ' + Math.round(window.frameGeometry.y)
                + ' ' + Math.round(window.frameGeometry.width)
                + ' ' + Math.round(window.frameGeometry.height)
                + ' ' + String(window.minimized));
        }}
    }}""",
        0.05,
    )


def _wait_for_one_client() -> None:
    for _ in range(80):
        count = len([line for line in _client_rows().splitlines() if line])
        if count == 1:
            return
        time.sleep(0.1)
    recipe.fail("the topology fixture did not map exactly one tagged QML Window")


def _place_client(x: str, y: str, width: str, height: str, minimized: str) -> str:
    """place_client: move the tagged client to the requested frame and poll 80x0.05s
    for KWin to apply it exactly; returns "x y w h" of the applied frame."""
    result = recipe.kwin_js(
        f"""for (const window of workspace.windowList()) {{
        if (window.caption === '{CLIENT_TITLE}') {{
            window.minimized = false;
            const geometry = Object.assign({{}}, window.frameGeometry);
            geometry.x = {x};
            geometry.y = {y};
            geometry.width = {width};
            geometry.height = {height};
            window.frameGeometry = geometry;
            workspace.activeWindow = window;
            window.minimized = {minimized};
            print('@TAG@|' + String(window.internalId));
        }}
    }}""",
        0.05,
    )
    if not result or "\n" in result:
        recipe.fail("KWin targeted an invalid number of topology fixture windows")
    actual_id = actual_x = actual_y = actual_width = actual_height = actual_minimized = ""
    for _ in range(80):
        lines = _client_rows().splitlines()
        parts = lines[0].split() if lines else []
        if len(parts) == 6:
            (
                actual_id,
                actual_x,
                actual_y,
                actual_width,
                actual_height,
                actual_minimized,
            ) = parts
        if (
            actual_id == result
            and actual_x == x
            and actual_y == y
            and actual_width == width
            and actual_height == height
            and actual_minimized == minimized
        ):
            return f"{actual_x} {actual_y} {actual_width} {actual_height}"
        time.sleep(0.05)
    recipe.fail(
        f"KWin constrained the requested client frame {x},{y} {width}x{height} "
        f"(actual={actual_x},{actual_y} {actual_width}x{actual_height} "
        f"minimized={actual_minimized})"
    )


def _wait_for_client_policy(frame: str, expected: str, minimized: str, boundary: str) -> None:
    frame_args = frame.split()
    extra = ["--minimized"] if minimized == "true" else []
    for _ in range(120):
        result = _oracle(
            "assert-client",
            "--ids",
            _S.view_ids_csv,
            "--frame",
            *frame_args,
            "--expected",
            expected,
            *extra,
            stdin=_snapshot(),
            quiet=True,
        )
        if result.returncode == 0 and _stable_matches_client_baseline():
            _assert_stable_after_client_change(boundary)
            return
        time.sleep(0.05)
    result = _oracle(
        "assert-client",
        "--ids",
        _S.view_ids_csv,
        "--frame",
        *frame_args,
        "--expected",
        expected,
        *extra,
        stdin=_snapshot(),
    )
    if result.returncode != 0:
        recipe.fail(f"{boundary} did not settle at the expected per-view touch policy")
    _assert_stable_after_client_change(boundary)


def _drive_client_case(case: str, expected: str, minimized: str = "false") -> None:
    plan = _oracle("client-plan", "--ids", _S.view_ids_csv, "--case", case, stdin=_snapshot())
    if plan.returncode != 0:
        recipe.fail(f"could not plan the {case} client geometry from live triggers")
    fields = plan.stdout.split()
    frame = _place_client(fields[0], fields[1], fields[2], fields[3], minimized)
    _wait_for_client_policy(frame, expected, minimized, case)


def _wait_for_no_client() -> None:
    for _ in range(80):
        result = _oracle(
            "assert-no-client", "--ids", _S.view_ids_csv, stdin=_snapshot(), quiet=True
        )
        if result.returncode == 0 and _stable_matches_client_baseline():
            _assert_stable_after_client_change("client teardown")
            return
        time.sleep(0.1)
    if _oracle("assert-no-client", "--ids", _S.view_ids_csv, stdin=_snapshot()).returncode != 0:
        recipe.fail("destroyed client remained in the per-view touch policy")
    _assert_stable_after_client_change("client teardown")


def _drive_topology_cases(topology: str) -> None:
    _wait_for_stable_topology(topology)
    if _client_rows():
        recipe.fail(f"a tagged QML Window existed before the {topology} client run")
    _S.client = subprocess.Popen(
        ["qml", WINDOW_QML], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
    )
    _wait_for_one_client()

    _drive_client_case("parked", "none")
    _drive_client_case("a-only", str(_S.view_a))
    _drive_client_case("gap-only", "none")
    _drive_client_case("full-primary", f"{_S.view_a},{_S.view_b}")
    _drive_client_case("c-only", str(_S.view_c))
    _drive_client_case("spanning", f"{_S.view_b},{_S.view_c}")
    _drive_client_case("minimized", f"{_S.view_b},{_S.view_c}", "true")

    if _S.client.poll() is not None:
        recipe.fail(f"could not destroy the {topology} QML Window")
    _S.client.terminate()
    with suppress(Exception):
        _S.client.wait()
    _S.client = None
    for _ in range(40):
        if not _client_rows():
            break
        time.sleep(0.1)
    if _client_rows():
        recipe.fail(f"{topology} QML Window remained mapped after destruction")
    _wait_for_no_client()


# ---- config restore + cleanup (the safety net) -----------------------------


def _restore_output_topology() -> bool:
    """mo_restore_output_topology the captured KScreen topology; True on success."""
    try:
        multi_output.mo_restore_output_topology(_S.original_topology)
    except multi_output.MultiOutputError:
        return False
    return True


def _restore_config() -> bool:
    """The bash rm -rf "${E2E_CONFIG_HOME:?}" + cp -r "$MATRIX_PRISTINE" ...: replace
    the config home with the pristine seed. True on a clean copy."""
    config_home = os.environ["E2E_CONFIG_HOME"]
    shutil.rmtree(config_home, ignore_errors=True)
    try:
        _ = shutil.copytree(matrix.pristine_seed_dir(), config_home)
    except OSError:
        return False
    return True


def _pid_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def _running_dock_pid() -> int | None:
    """The recorded pid iff it is still alive (the bash e2e_dock_pid + kill -0)."""
    pid = recipe.dock_pid()
    if pid is not None and _pid_alive(pid):
        return pid
    return None


def _live_cleanup_deps() -> TopologyCleanupDeps:
    """Wire the pure cleanup core to the real output/dock/config side effects."""
    return TopologyCleanupDeps(
        restore_output_topology=_restore_output_topology,
        stop_dock=recipe.dock_stop,
        restore_config=_restore_config,
        running_dock_pid=_running_dock_pid,
        start_dock=lambda: recipe.dock_start(90),
        warn=_warn,
    )


def _cleanup(original_status: int) -> int:
    if _S.client is not None:
        with suppress(ProcessLookupError):
            _S.client.terminate()
        with suppress(Exception):
            _S.client.wait()
        _S.client = None
    return perform_topology_cleanup(
        _live_cleanup_deps(),
        topology_captured=_S.topology_captured,
        fixture_transaction_active=_S.fixture_transaction_active,
        original_status=original_status,
    )


# ---- the acceptance body ---------------------------------------------------


def _body() -> None:
    if os.environ.get("E2E_OUTPUT_COUNT", "1") != "2":
        recipe.fail("FP-4B topology acceptance requires exactly two nested outputs")

    if subprocess.run(["python3", ORACLE, "negative-probes"], check=False).returncode != 0:
        recipe.fail("controlled geometry and ownership negatives did not reject")

    if matrix.init() != 0:
        recipe.fail("could not capture the pristine nested configuration")
    _S.fixture_transaction_active = True
    try:
        _ = multi_output.mo_discover_outputs()
    except multi_output.MultiOutputError:
        recipe.fail("could not discover the two nested output identities")
    try:
        _S.original_topology = multi_output.mo_capture_output_topology()
    except multi_output.MultiOutputError:
        recipe.fail("could not capture the original nested output topology")
    _S.topology_captured = True

    if matrix.stage("panel-bottom-justify-1out") != 0:
        recipe.fail("could not stage the FP-4B panel seed")
    try:
        _S.view_a = matrix.view_id()
    except matrix.MatrixProbeError:
        recipe.fail("could not resolve the FP-4B seed panel")
    _S.view_b = _duplicate_independently(_S.view_a, "first independent duplicate")
    _S.view_c = _duplicate_independently(_S.view_a, "second independent duplicate")
    _S.view_ids_csv = f"{_S.view_a},{_S.view_b},{_S.view_c}"
    _S.layout = os.environ["E2E_LAYOUT"]

    if not recipe.dock_stop():
        recipe.fail("dock did not stop before the three-panel fixture configuration")
    _configure_panel(_S.view_a, 32, 28)
    _configure_panel(_S.view_b, 48, 28)
    _configure_panel(_S.view_c, 64, 45)
    if not recipe.dock_start(90):
        recipe.fail("dock did not restart with the three-panel fixture")

    primary_id = _resolve_screen_id(
        os.environ["E2E_MO_PRIMARY"], "could not resolve the primary Latte output id"
    )
    secondary_id = _resolve_screen_id(
        os.environ["E2E_MO_SECONDARY"],
        "could not resolve the secondary Latte output id",
    )
    _call(
        "could not place A at primary bottom start",
        "setViewPlacement",
        "uiii",
        str(_S.view_a),
        str(primary_id),
        "4",
        "1",
    )
    _call(
        "could not place B at primary bottom end",
        "setViewPlacement",
        "uiii",
        str(_S.view_b),
        str(primary_id),
        "4",
        "2",
    )
    _call(
        "could not place C at secondary left center",
        "setViewPlacement",
        "uiii",
        str(_S.view_c),
        str(secondary_id),
        "5",
        "0",
    )
    for view in (_S.view_a, _S.view_b, _S.view_c):
        _call(
            f"could not set panel {view} to Always Visible",
            "setViewVisibilityMode",
            "us",
            str(view),
            "alwaysVisible",
        )
    _wait_for_fixture_placement(primary_id, secondary_id)

    before_axis_revision = _publication_revision(
        _S.view_c, "could not capture C publication revision before its axis change"
    )
    _call(
        "could not exercise C across vertical-to-horizontal placement",
        "setViewPlacement",
        "uiii",
        str(_S.view_c),
        str(secondary_id),
        "3",
        "0",
    )
    _assert_axis_change_publishes_once(
        _S.view_c, secondary_id, "top", "center", before_axis_revision
    )
    _call(
        "could not restore C to secondary left center",
        "setViewPlacement",
        "uiii",
        str(_S.view_c),
        str(secondary_id),
        "5",
        "0",
    )
    _wait_for_fixture_placement(primary_id, secondary_id)

    try:
        _ = multi_output.mo_place_secondary_for_topology("full-touching")
    except multi_output.MultiOutputError:
        recipe.fail("could not realize the full-touching output topology")
    _drive_topology_cases("full-touching")

    try:
        _ = multi_output.mo_place_secondary_for_topology("partial-touching")
    except multi_output.MultiOutputError:
        recipe.fail("could not realize the partial-touching output topology")
    _drive_topology_cases("partial-touching")

    try:
        _ = multi_output.mo_place_secondary_for_topology("disconnected")
    except multi_output.MultiOutputError:
        recipe.fail("could not realize the disconnected output topology")
    _drive_topology_cases("disconnected")

    before_restart = _persistent_projection("could not capture pre-restart persistent topology")
    if not recipe.dock_stop():
        recipe.fail("dock did not stop for disconnected-topology persistence reload")
    if not recipe.dock_start(90):
        recipe.fail("dock did not restart for disconnected-topology persistence reload")
    _wait_for_stable_topology("disconnected")
    after_restart = _persistent_projection("could not capture post-restart persistent topology")
    if after_restart != before_restart:
        recipe.fail(
            "restart changed persistent identities, placement, stable spans, depths,"
            " or reservation groups"
        )
    if _oracle("assert-no-client", "--ids", _S.view_ids_csv, stdin=_snapshot()).returncode != 0:
        recipe.fail("restart created an unexpected window-touch participant")

    print(
        "FP-4B topology acceptance passed three output arrangements, exact separated-span"
        " activation, spanning-window fanout, maximum-depth reservations, restart persistence,"
        " and controlled negative oracles",
        flush=True,
    )


def main() -> int:
    install_conventional_signal_exits()
    status = 0
    try:
        try:
            _body()
        except SystemExit as exc:
            status = exc.code if isinstance(exc.code, int) else 1
        except recipe.RecipeError as exc:
            print(str(exc), file=sys.stderr, flush=True)
            status = 1
    finally:
        status = _cleanup(status)
    return status


if __name__ == "__main__":
    sys.exit(main())
