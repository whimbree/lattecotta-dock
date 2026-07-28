#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later

"""Typed geometry and ownership oracle for recipe 073."""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass
from typing import Any, Callable, Iterable, NoReturn


STACKING_REASON = (
    "Inward same-edge stacking is unsupported; stable-span overlap is not yet rejected."
)


@dataclass(frozen=True)
class Rect:
    x: float
    y: float
    width: float
    height: float

    @classmethod
    def from_json(cls, value: list[float]) -> "Rect":
        if len(value) != 4:
            fail(f"rectangle needs four coordinates, got {value}")
        rect = cls(*value)
        if not all(math.isfinite(component) for component in value):
            fail(f"rectangle is not finite: {value}")
        if rect.width <= 0 or rect.height <= 0:
            fail(f"rectangle is empty: {value}")
        return rect

    @property
    def right(self) -> float:
        return self.x + self.width

    @property
    def bottom(self) -> float:
        return self.y + self.height

    def intersects(self, other: "Rect") -> bool:
        return (
            max(self.x, other.x) < min(self.right, other.right)
            and max(self.y, other.y) < min(self.bottom, other.bottom)
        )

    def outward_aligned(self) -> list[int]:
        left = math.floor(self.x)
        top = math.floor(self.y)
        return [
            left,
            top,
            math.ceil(self.right) - left,
            math.ceil(self.bottom) - top,
        ]

    def translated(self, dx: float, dy: float) -> "Rect":
        return Rect(self.x + dx, self.y + dy, self.width, self.height)


def fail(message: str) -> NoReturn:
    raise SystemExit(message)


def read_json() -> Any:
    return json.load(sys.stdin)


def parse_ids(raw: str) -> tuple[int, int, int]:
    values = tuple(int(value) for value in raw.split(","))
    if len(values) != 3 or len(set(values)) != 3:
        fail(f"expected three distinct dock ids, got {raw}")
    return values


def select_views(
    snapshot: dict[str, Any], ids: tuple[int, int, int]
) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
    if snapshot.get("schemaVersion") != 9:
        fail(f"expected dockSystemData schema 9, got {snapshot.get('schemaVersion')}")
    records = {view["persistentDockId"]: view for view in snapshot["views"]}
    assert_exact_view_set(set(records), set(ids))
    return records[ids[0]], records[ids[1]], records[ids[2]]


def assert_exact_view_set(actual: set[int], expected: set[int]) -> None:
    if actual != expected:
        fail(f"expected exactly views {sorted(expected)}, got {sorted(actual)}")


def classify_rectangles(first: Rect, second: Rect) -> str:
    horizontal_seam = first.right == second.x or second.right == first.x
    vertical_seam = first.bottom == second.y or second.bottom == first.y
    if horizontal_seam:
        overlap = max(
            0.0, min(first.bottom, second.bottom) - max(first.y, second.y)
        )
        shorter = min(first.height, second.height)
    elif vertical_seam:
        overlap = max(0.0, min(first.right, second.right) - max(first.x, second.x))
        shorter = min(first.width, second.width)
    else:
        return "disconnected"
    if math.isclose(overlap, shorter) and overlap > 0:
        return "full-touching"
    if 0 < overlap < shorter:
        return "partial-touching"
    return "disconnected"


def assert_topology_classification(actual: str, expected: str) -> None:
    if actual != expected:
        fail(f"expected {expected} output topology, got {actual}")


def primary_and_secondary(snapshot: list[dict[str, Any]]) -> tuple[Rect, Rect]:
    active = [screen for screen in snapshot if screen["isActive"]]
    if len(active) != 2:
        fail(f"expected two active outputs, got {len(active)}")
    primary = [screen for screen in active if screen["isPrimary"]]
    if len(primary) != 1:
        fail(f"expected one primary output, got {len(primary)}")
    secondary = [screen for screen in active if not screen["isPrimary"]]
    return (
        Rect.from_json(primary[0]["geometry"]),
        Rect.from_json(secondary[0]["geometry"]),
    )


def assert_topology(expected: str) -> None:
    payload = read_json()
    if not isinstance(payload, list):
        fail("screensData must be a JSON list")
    primary, secondary = primary_and_secondary(payload)
    actual = classify_rectangles(primary, secondary)
    assert_topology_classification(actual, expected)
    if primary.width <= primary.height:
        fail(f"primary output is not landscape: {primary}")
    if secondary.height <= secondary.width:
        fail(f"secondary output is not portrait: {secondary}")
    print(actual)


def global_rect(view: dict[str, Any], local_key: str) -> Rect:
    canvas = Rect.from_json(view["stableCanvasGeometry"])
    local = Rect.from_json(view[local_key])
    return local.translated(canvas.x, canvas.y)


def assert_independent_authorities(
    views: Iterable[dict[str, Any]],
) -> None:
    all_tokens: list[str] = []
    for view in views:
        dock_id = view["persistentDockId"]
        if (
            view["relationship"] != "independent"
            or view["logicalDockId"] != dock_id
            or view["originalDockId"] is not None
            or view["linkedDockIds"] != []
        ):
            fail(f"view {dock_id} retained linked identity")
        objects = view["objects"]
        required = (
            "view",
            "configuration",
            "geometryController",
            "transitionController",
            "windowTouchTracker",
        )
        tokens = [objects[name] for name in required]
        if any(not isinstance(token, str) or not token for token in tokens):
            fail(f"view {dock_id} has an absent required authority: {objects}")
        if len(set(tokens)) != len(tokens):
            fail(f"view {dock_id} aliases independent authorities: {objects}")
        all_tokens.extend(
            objects[name]
            for name in (
                "view",
                "configuration",
                "geometryController",
                "transitionController",
                "windowTouchTracker",
            )
        )
    if len(set(all_tokens)) != len(all_tokens):
        fail("per-view runtime, geometry, transition, or touch authority is shared")


def assert_endpoint(view: dict[str, Any], expected_count: int) -> None:
    dock_id = view["persistentDockId"]
    attached = expected_count > 0
    expected_target = "attached" if attached else "floated"
    expected_progress = 0.0 if attached else 1.0
    equation = (
        view["floatingPanelEligible"]
        and view["attachOnWindowTouchConfigured"]
        and not view["attachmentDeferredByPointer"]
        and view["touchingWindowCount"] > 0
    )
    if equation != attached:
        fail(f"view {dock_id} violates the attachment policy equation")
    if view["touchingWindowCount"] != expected_count:
        fail(
            f"view {dock_id} touching count is {view['touchingWindowCount']}, "
            f"expected {expected_count}"
        )
    if (
        view["transitionTarget"] != expected_target
        or view["transitionPhase"] != "resting"
        or view["transitionRunning"]
        or not math.isclose(view["transitionProgress"], expected_progress)
    ):
        fail(
            f"view {dock_id} did not settle at {expected_target}/"
            f"{expected_progress}: target={view['transitionTarget']} "
            f"phase={view['transitionPhase']} running={view['transitionRunning']} "
            f"progress={view['transitionProgress']}"
        )


def primary_start_and_length(rect: Rect, orientation: str) -> tuple[float, float]:
    if orientation == "horizontal":
        return rect.x, rect.width
    if orientation == "vertical":
        return rect.y, rect.height
    fail(f"unsupported panel orientation {orientation}")


def assert_popup_primary_geometry(
    anchor: Rect,
    stable_paint: Rect,
    stable_measurement: Rect,
    available_primary_length: int,
    orientation: str,
) -> None:
    paint_start, paint_length = primary_start_and_length(stable_paint, orientation)
    _, measurement_length = primary_start_and_length(
        stable_measurement, orientation
    )
    anchor_start, anchor_length = primary_start_and_length(anchor, orientation)
    if paint_length != measurement_length:
        fail(
            "stable paint and applet-measurement primary lengths disagree: "
            f"{paint_length} != {measurement_length}"
        )
    layout_clearance = measurement_length - available_primary_length
    if layout_clearance < 0:
        fail(
            f"available primary length {available_primary_length} exceeds "
            f"stable measurement length {measurement_length}"
        )
    if anchor_length != available_primary_length:
        fail(
            f"popup primary span {anchor_length} does not match stable "
            f"available length {available_primary_length}"
        )
    if (
        anchor_start < paint_start
        or anchor_start + anchor_length > paint_start + paint_length
    ):
        fail(
            "popup primary geometry escapes the stable paint span: "
            f"anchor=({anchor_start},{anchor_length}) "
            f"paint=({paint_start},{paint_length})"
        )


def assert_presentation(view: dict[str, Any]) -> None:
    dock_id = view["persistentDockId"]
    if view["floatingDamageMaskPending"]:
        fail(f"view {dock_id} retained a pending damage mask")
    if view["appliedInputMask"] != view["inputMask"]:
        fail(f"view {dock_id} did not converge its applied input mask")
    paint = Rect.from_json(view["computedPaintMaskGeometry"]).outward_aligned()
    bridge = Rect.from_json(view["computedInputBridgeGeometry"]).outward_aligned()
    if view["maskRect"] != paint or view["effectsRect"] != paint:
        fail(f"view {dock_id} paint, mask, and effects shapes diverged")
    if view["inputMask"] != bridge:
        fail(f"view {dock_id} input mask diverged from its exact bridge")
    expected_popup_preference = view["transitionTarget"] == "floated"
    if view["floatingAppletPopupsPreferred"] != expected_popup_preference:
        fail(
            f"view {dock_id} popup preference does not follow its transition "
            f"target {view['transitionTarget']}"
        )

    anchor = Rect.from_json(view["appletsLayoutGeometry"])
    paint_rect = Rect.from_json(paint)
    stable_paint = Rect.from_json(view["floatedPresentationGeometry"])
    stable_measurement = Rect.from_json(view["stableAppletMeasurementBounds"])
    available_primary_length = view["availablePrimaryLength"]
    if not isinstance(available_primary_length, int):
        fail(f"view {dock_id} has no stable available primary length")
    assert_popup_primary_geometry(
        anchor,
        stable_paint,
        stable_measurement,
        available_primary_length,
        view["orientation"],
    )
    if view["orientation"] == "horizontal":
        secondary_matches = (
            anchor.y == paint_rect.y
            and anchor.height == paint_rect.height
        )
    else:
        secondary_matches = (
            anchor.x == paint_rect.x
            and anchor.width == paint_rect.width
        )
    if not secondary_matches:
        fail(
            f"view {dock_id} popup anchor does not preserve the primary span "
            "while following the visible secondary span"
        )


def assert_static_view_contract(view: dict[str, Any]) -> None:
    dock_id = view["persistentDockId"]
    expected_true = (
        "floatingGapConfigured",
        "floatingPanelConfigured",
        "floatingPanelEligible",
        "attachOnWindowTouchConfigured",
        "transitionGeometryPresent",
        "geometrySettled",
        "layerShellPresent",
        "reservationSurfacePresent",
    )
    for key in expected_true:
        if view[key] is not True:
            fail(f"view {dock_id} requires {key}=true")
    if (
        view["type"] != "panel"
        or view["visibilityMode"] != "alwaysVisible"
        or view["attachmentWaitsForPointerExitConfigured"]
        or view["pointerInsideView"]
        or view["attachmentDeferredByPointer"]
        or view["stableLayerShellMargin"] != 0
        or view["layerShellExclusiveZone"] != -1
        or view["layerShellExclusiveEdge"] != "none"
    ):
        policy = {
            key: view[key]
            for key in (
                "type",
                "visibilityMode",
                "attachmentWaitsForPointerExitConfigured",
                "pointerInsideView",
                "attachmentDeferredByPointer",
                "stableLayerShellMargin",
                "layerShellExclusiveZone",
                "layerShellExclusiveEdge",
            )
        }
        fail(f"view {dock_id} violates the stable floating-panel policy: {policy}")
    if view["windowTouchGeometryRoleType"] not in ("", "QRect"):
        fail(
            f"view {dock_id} reports unsupported window geometry role "
            f"{view['windowTouchGeometryRoleType']}"
        )
    assert_presentation(view)


def assert_regions_remain_separated(first: Rect, second: Rect, label: str) -> None:
    if first.intersects(second):
        fail(f"{label} overlap")
    if first.right >= second.x:
        fail(f"{label} were widened into a continuous strip")


def assert_separated_bottom_spans(
    first: dict[str, Any], second: dict[str, Any]
) -> None:
    first_trigger = Rect.from_json(first["stableTriggerGeometry"])
    second_trigger = Rect.from_json(second["stableTriggerGeometry"])
    if (
        first["edge"] != "bottom"
        or second["edge"] != "bottom"
        or first["screenId"] != second["screenId"]
        or first["alignment"] != "left"
        or second["alignment"] != "right"
    ):
        fail("A and B are not start/end partial panels on one primary bottom edge")
    assert_regions_remain_separated(
        first_trigger, second_trigger, "A and B stable triggers"
    )
    for view in (first, second):
        canvas = Rect.from_json(view["stableCanvasGeometry"])
        screen = Rect.from_json(view["screenGeometry"])
        if canvas.bottom != screen.bottom:
            fail(
                f"view {view['persistentDockId']} canvas left the physical bottom edge"
            )
    first_input = global_rect(first, "inputMask")
    second_input = global_rect(second, "inputMask")
    assert_regions_remain_separated(
        first_input, second_input, "A and B exact input regions"
    )


def assert_fixture_placements(
    first: dict[str, Any], second: dict[str, Any], third: dict[str, Any]
) -> None:
    if (
        first["screenId"] != second["screenId"]
        or first["edge"] != "bottom"
        or first["alignment"] != "left"
        or second["edge"] != "bottom"
        or second["alignment"] != "right"
        or first["screenId"] == third["screenId"]
        or third["edge"] != "left"
        or third["alignment"] != "center"
    ):
        fail("A, B, and C are not on their exact output-edge placements")


def assert_maximum_depth_group(
    group: dict[str, Any], contributor_ids: list[int], depths: list[int]
) -> None:
    if (
        group["contributorDockIds"] != sorted(contributor_ids)
        or group["memberCount"] != len(contributor_ids)
        or group["publishedDepth"] != max(depths)
        or (len(depths) > 1 and group["publishedDepth"] == sum(depths))
    ):
        fail("reservation group is not canonical maximum-depth ownership")


def assert_group_mirror(view: dict[str, Any], group: dict[str, Any]) -> None:
    mirrored = {
        "reservationOutputId": "outputId",
        "reservationEdge": "edge",
        "reservationPublishedDepth": "publishedDepth",
        "reservationGroupMemberCount": "memberCount",
        "reservationGroupGeneration": "generation",
        "reservationContributorDockIds": "contributorDockIds",
        "reservationGeometry": "geometry",
        "reservationWindowGeometry": "windowGeometry",
        "reservationLayerShellAnchors": "layerShellAnchors",
        "reservationLayerShellMargins": "layerShellMargins",
        "reservationLayerShellExclusiveEdge": "layerShellExclusiveEdge",
        "reservationLayerShellExclusiveZone": "layerShellExclusiveZone",
    }
    mismatches = {
        view_key: (view[view_key], group[group_key])
        for view_key, group_key in mirrored.items()
        if view[view_key] != group[group_key]
    }
    if (
        not view["reservationSurfacePresent"]
        or not group["layerShellPresent"]
        or view["objects"]["reservationPublisher"] != group["publisher"]
    ):
        mismatches["publisher"] = (
            view["reservationSurfacePresent"],
            group["layerShellPresent"],
            view["objects"]["reservationPublisher"],
            group["publisher"],
        )
    if mismatches:
        fail(
            f"view {view['persistentDockId']} reservation mirror diverged "
            f"from its group: {mismatches}"
        )


def assert_reservations(
    snapshot: dict[str, Any],
    first: dict[str, Any],
    second: dict[str, Any],
    third: dict[str, Any],
) -> None:
    groups = snapshot["reservationGroups"]
    if len(groups) != 2:
        fail(f"expected exactly two output-edge reservation groups, got {len(groups)}")
    ab_ids = sorted([first["persistentDockId"], second["persistentDockId"]])
    ab_group = next(
        (
            group
            for group in groups
            if group["outputId"] == first["screenId"] and group["edge"] == "bottom"
        ),
        None,
    )
    c_group = next(
        (
            group
            for group in groups
            if group["outputId"] == third["screenId"] and group["edge"] == "left"
        ),
        None,
    )
    if ab_group is None or c_group is None:
        fail("output identity plus edge did not produce the two expected groups")
    ab_depths = [
        first["reservationContributionDepth"],
        second["reservationContributionDepth"],
    ]
    assert_maximum_depth_group(ab_group, ab_ids, ab_depths)
    assert_group_mirror(first, ab_group)
    assert_group_mirror(second, ab_group)
    assert_group_mirror(third, c_group)
    if (
        first["objects"]["reservationPublisher"]
        != second["objects"]["reservationPublisher"]
        or first["objects"]["reservationPublisher"] != ab_group["publisher"]
    ):
        fail("A and B do not share their primary-bottom reservation publisher")
    if (
        c_group["contributorDockIds"] != [third["persistentDockId"]]
        or c_group["memberCount"] != 1
        or c_group["publishedDepth"] != third["reservationContributionDepth"]
        or third["objects"]["reservationPublisher"] != c_group["publisher"]
        or c_group["publisher"] == ab_group["publisher"]
    ):
        fail("C does not own one isolated secondary-left reservation publisher")
    depths = {
        first["reservationContributionDepth"],
        second["reservationContributionDepth"],
        third["reservationContributionDepth"],
    }
    if len(depths) != 3 or any(depth <= 0 for depth in depths):
        fail(f"fixture depths are not three distinct positive values: {depths}")


def assert_structure(ids: tuple[int, int, int], windows_path: str | None) -> None:
    snapshot = read_json()
    first, second, third = select_views(snapshot, ids)
    assert_independent_authorities((first, second, third))
    for view in (first, second, third):
        assert_static_view_contract(view)
    assert_fixture_placements(first, second, third)
    assert_separated_bottom_spans(first, second)
    assert_reservations(snapshot, first, second, third)
    stacking = snapshot["stacking"]
    if stacking != {"available": False, "reason": STACKING_REASON}:
        fail(f"unsupported stacking contract changed: {stacking}")
    if windows_path is not None:
        with open(windows_path, encoding="utf-8") as handle:
            windows = json.load(handle)
        rect_counts: dict[tuple[int, int, int, int], int] = {}
        for window in windows:
            rect = tuple(window["geometry"])
            rect_counts[rect] = rect_counts.get(rect, 0) + 1
        for view in (first, second, third):
            expected = tuple(view["stableCanvasGeometry"])
            if rect_counts.get(expected, 0) != 1:
                fail(
                    f"view {view['persistentDockId']} has no exact unique QWindow "
                    f"at {expected}; layer-3 windows={windows}"
                )
    print("ok")


STABLE_VIEW_FIELDS = (
    "runtimeViewId",
    "persistentDockId",
    "logicalDockId",
    "originalDockId",
    "relationship",
    "linkPlacement",
    "screensGroup",
    "linkedDockIds",
    "layout",
    "screenId",
    "screen",
    "onPrimary",
    "type",
    "edge",
    "orientation",
    "alignment",
    "maximumLengthRatio",
    "offsetRatio",
    "configuredIconSize",
    "effectiveIconSize",
    "availablePrimaryLength",
    "normalThickness",
    "maximumNormalThickness",
    "floatingGapConfigured",
    "floatingPanelConfigured",
    "floatingPanelEligible",
    "attachOnWindowTouchConfigured",
    "attachmentWaitsForPointerExitConfigured",
    "transitionGeometryPresent",
    "transitionGeometryRevision",
    "stableCanvasGeometry",
    "attachedPresentationGeometry",
    "floatedPresentationGeometry",
    "stableTriggerGeometry",
    "stableAppletMeasurementBounds",
    "stablePrimaryAxisStart",
    "stablePrimaryAxisLength",
    "stableLayerShellMargin",
    "requestedReservationDepth",
    "surfaceGeometryPublicationRevision",
    "layerShellConfigureRequestRevision",
    "windowGeometry",
    "absoluteGeometry",
    "screenGeometry",
    "surfaceGeometry",
    "canvasGeometry",
    "strutsThickness",
    "publishedStruts",
    "layerShellPresent",
    "layerShellAnchors",
    "layerShellMargins",
    "layerShellExclusiveEdge",
    "layerShellExclusiveZone",
    "reservationSurfacePresent",
    "reservationOutputId",
    "reservationEdge",
    "reservationContributionDepth",
    "reservationPublishedDepth",
    "reservationGroupMemberCount",
    "reservationGroupGeneration",
    "reservationContributorDockIds",
    "reservationGeometry",
    "reservationWindowGeometry",
    "reservationLayerShellAnchors",
    "reservationLayerShellMargins",
    "reservationLayerShellExclusiveEdge",
    "reservationLayerShellExclusiveZone",
    "geometrySettled",
)


def stable_projection(ids: tuple[int, int, int]) -> None:
    snapshot = read_json()
    views = select_views(snapshot, ids)
    projection = {
        "reservationStateGeneration": snapshot["reservationStateGeneration"],
        "reservationGroups": snapshot["reservationGroups"],
        "views": [
            {
                **{key: view[key] for key in STABLE_VIEW_FIELDS},
                "popupAnchorPrimarySpan": (
                    [
                        view["appletsLayoutGeometry"][0],
                        view["appletsLayoutGeometry"][2],
                    ]
                    if view["orientation"] == "horizontal"
                    else [
                        view["appletsLayoutGeometry"][1],
                        view["appletsLayoutGeometry"][3],
                    ]
                ),
                "objects": {
                    key: view["objects"][key]
                    for key in (
                        "view",
                        "configuration",
                        "layout",
                        "layoutController",
                        "geometryController",
                        "transitionController",
                        "windowTouchTracker",
                        "reservationPublisher",
                    )
                },
            }
            for view in views
        ],
    }
    print(json.dumps(projection, sort_keys=True, separators=(",", ":")))


def persistent_projection(ids: tuple[int, int, int]) -> None:
    snapshot = read_json()
    views = select_views(snapshot, ids)
    fields = (
        "persistentDockId",
        "logicalDockId",
        "originalDockId",
        "relationship",
        "linkPlacement",
        "screensGroup",
        "linkedDockIds",
        "screenId",
        "screen",
        "onPrimary",
        "type",
        "edge",
        "orientation",
        "alignment",
        "maximumLengthRatio",
        "offsetRatio",
        "configuredIconSize",
        "effectiveIconSize",
        "availablePrimaryLength",
        "stableCanvasGeometry",
        "stableTriggerGeometry",
        "stableAppletMeasurementBounds",
        "stablePrimaryAxisStart",
        "stablePrimaryAxisLength",
        "stableLayerShellMargin",
        "requestedReservationDepth",
        "reservationContributionDepth",
        "reservationPublishedDepth",
        "reservationContributorDockIds",
        "reservationGeometry",
    )
    groups = [
        {
            key: group[key]
            for key in (
                "outputId",
                "edge",
                "publishedDepth",
                "contributorDockIds",
                "memberCount",
                "geometry",
            )
        }
        for group in snapshot["reservationGroups"]
    ]
    projection = {
        "groups": groups,
        "views": [{key: view[key] for key in fields} for view in views],
    }
    print(json.dumps(projection, sort_keys=True, separators=(",", ":")))


def anchor_revisions(ids: tuple[int, int, int]) -> None:
    views = select_views(read_json(), ids)
    print(*(view["floatingAnchorRevision"] for view in views))


def client_plan(ids: tuple[int, int, int], case: str) -> None:
    snapshot = read_json()
    first, second, third = select_views(snapshot, ids)
    primary = Rect.from_json(first["screenGeometry"])
    triggers = [
        Rect.from_json(view["stableTriggerGeometry"])
        for view in (first, second, third)
    ]
    height = 100
    width = 120

    if case == "parked":
        rect = Rect(primary.x + primary.width / 2 - width / 2, primary.y + 20,
                    width, height)
    elif case in ("a-only", "gap-only", "full-primary"):
        bottom = triggers[0].y + 1
        if case == "a-only":
            rect = Rect(triggers[0].x + triggers[0].width / 2 - width / 2,
                        bottom - height, width, height)
        elif case == "gap-only":
            gap_start = triggers[0].right
            gap_end = triggers[1].x
            gap_width = gap_end - gap_start
            if gap_width <= 4:
                fail(f"separated panels have no usable gap: {gap_width}")
            width = min(width, gap_width - 2)
            rect = Rect(gap_start + (gap_width - width) / 2, bottom - height,
                        width, height)
        else:
            rect = Rect(primary.x, bottom - height, primary.width, height)
    elif case == "c-only":
        rect = Rect(
            triggers[2].x,
            triggers[2].y + triggers[2].height / 2 - height / 2,
            width,
            height,
        )
    elif case in ("spanning", "minimized"):
        selected = (triggers[1], triggers[2])
        left = min(trigger.x for trigger in selected)
        top = min(trigger.y for trigger in selected)
        right = max(trigger.right for trigger in selected)
        bottom = max(trigger.bottom for trigger in selected)
        rect = Rect(left, top, right - left, bottom - top)
    else:
        fail(f"unknown client case {case}")

    values = [round(rect.x), round(rect.y), round(rect.width), round(rect.height)]
    if values[2] <= 0 or values[3] <= 0:
        fail(f"planned client rectangle is empty: {values}")
    print(*values)


def assert_client(
    ids: tuple[int, int, int],
    frame: Rect,
    expected_raw: str,
    minimized: bool,
) -> None:
    snapshot = read_json()
    views = select_views(snapshot, ids)
    actual_touched = {
        view["persistentDockId"]
        for view in views
        if frame.intersects(Rect.from_json(view["stableTriggerGeometry"]))
    }
    expected = (
        set()
        if expected_raw == "none"
        else {int(value) for value in expected_raw.split(",")}
    )
    if actual_touched != expected:
        fail(
            f"actual KWin frame intersects {sorted(actual_touched)}, "
            f"expected {sorted(expected)}"
        )
    for view in views:
        expected_count = (
            0
            if minimized
            else int(view["persistentDockId"] in actual_touched)
        )
        assert_endpoint(view, expected_count)
        assert_presentation(view)
        if view["windowTouchGeometryRoleType"] != "QRect":
            fail(
                f"view {view['persistentDockId']} did not observe the live "
                "QWindow geometry role as QRect"
            )
    print("ok")


def assert_no_client(ids: tuple[int, int, int]) -> None:
    views = select_views(read_json(), ids)
    for view in views:
        assert_endpoint(view, 0)
        assert_presentation(view)
    print("ok")


def negative_probes() -> None:
    def expect_rejected(label: str, action: Callable[[], None]) -> None:
        try:
            action()
        except SystemExit:
            return
        fail(f"controlled negative accepted {label}")

    primary = Rect(0, 0, 1000, 600)
    portrait = Rect(1000, 300, 400, 500)
    actual = classify_rectangles(primary, portrait)
    if actual != "partial-touching":
        fail("negative topology classifier fixture is invalid")
    expect_rejected(
        "wrong topology classification",
        lambda: assert_topology_classification(actual, "full-touching"),
    )
    expect_rejected(
        "wrong expected view set",
        lambda: assert_exact_view_set({1, 2, 3}, {1, 2, 4}),
    )

    first = {"screenId": 1, "edge": "bottom", "alignment": "left"}
    second = {"screenId": 1, "edge": "bottom", "alignment": "right"}
    wrong_third = {"screenId": 1, "edge": "left", "alignment": "center"}
    expect_rejected(
        "wrong C output placement",
        lambda: assert_fixture_placements(first, second, wrong_third),
    )

    contributions = [41, 67]
    sum_group = {
        "contributorDockIds": [1, 2],
        "memberCount": 2,
        "publishedDepth": sum(contributions),
    }
    expect_rejected(
        "sum-depth reservation policy",
        lambda: assert_maximum_depth_group(sum_group, [1, 2], contributions),
    )

    first_region = Rect(0, 599, 300, 1)
    second_region = Rect(700, 599, 300, 1)
    strip = Rect(0, 599, 1000, 1)
    if first_region.intersects(second_region) or not (
        strip.intersects(first_region) and strip.intersects(second_region)
    ):
        fail("continuous-strip negative fixture is invalid")
    expect_rejected(
        "continuous A-B activation strip",
        lambda: assert_regions_remain_separated(
            strip, second_region, "A and B exact input regions"
        ),
    )

    stable_paint = Rect(0, 18, 448, 64)
    stable_measurement = Rect(0, 0, 448, 64)
    correct_popup = Rect(6, 18, 436, 64)
    assert_popup_primary_geometry(
        correct_popup,
        stable_paint,
        stable_measurement,
        436,
        "horizontal",
    )
    wrong_popup_span = Rect(6, 18, 435, 64)
    expect_rejected(
        "wrong popup primary span",
        lambda: assert_popup_primary_geometry(
            wrong_popup_span,
            stable_paint,
            stable_measurement,
            436,
            "horizontal",
        ),
    )
    outside_popup = Rect(20, 18, 436, 64)
    expect_rejected(
        "popup primary origin outside stable paint",
        lambda: assert_popup_primary_geometry(
            outside_popup,
            stable_paint,
            stable_measurement,
            436,
            "horizontal",
        ),
    )
    print("ok")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser()
    subparsers = result.add_subparsers(dest="command", required=True)

    topology = subparsers.add_parser("assert-topology")
    topology.add_argument("expected")

    structure = subparsers.add_parser("assert-structure")
    structure.add_argument("--ids", required=True)
    structure.add_argument("--windows")

    stable = subparsers.add_parser("stable-projection")
    stable.add_argument("--ids", required=True)

    persistent = subparsers.add_parser("persistent-projection")
    persistent.add_argument("--ids", required=True)

    anchors = subparsers.add_parser("anchor-revisions")
    anchors.add_argument("--ids", required=True)

    plan = subparsers.add_parser("client-plan")
    plan.add_argument("--ids", required=True)
    plan.add_argument("--case", required=True)

    client = subparsers.add_parser("assert-client")
    client.add_argument("--ids", required=True)
    client.add_argument("--frame", nargs=4, type=float, required=True)
    client.add_argument("--expected", required=True)
    client.add_argument("--minimized", action="store_true")

    no_client = subparsers.add_parser("assert-no-client")
    no_client.add_argument("--ids", required=True)

    subparsers.add_parser("negative-probes")
    return result


def main() -> None:
    args = parser().parse_args()
    if args.command == "assert-topology":
        assert_topology(args.expected)
    elif args.command == "assert-structure":
        assert_structure(parse_ids(args.ids), args.windows)
    elif args.command == "stable-projection":
        stable_projection(parse_ids(args.ids))
    elif args.command == "persistent-projection":
        persistent_projection(parse_ids(args.ids))
    elif args.command == "anchor-revisions":
        anchor_revisions(parse_ids(args.ids))
    elif args.command == "client-plan":
        client_plan(parse_ids(args.ids), args.case)
    elif args.command == "assert-client":
        assert_client(
            parse_ids(args.ids),
            Rect(*args.frame),
            args.expected,
            args.minimized,
        )
    elif args.command == "assert-no-client":
        assert_no_client(parse_ids(args.ids))
    elif args.command == "negative-probes":
        negative_probes()
    else:
        fail(f"unhandled command {args.command}")


if __name__ == "__main__":
    main()
