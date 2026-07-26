#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later

"""Adversarial pure tests for the FP-4C deterministic operation model."""

from __future__ import annotations

import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any, Callable


sys.path.insert(0, str(Path(__file__).resolve().parent))
import operation_model as model  # noqa: E402


SnapshotMutation = Callable[[dict[str, Any]], None]


class OperationModelTest(unittest.TestCase):
    maxDiff = None

    def setUp(self) -> None:
        self.plan = model.generate_plan(model.DEFAULT_SEED)
        self.final_seq = len(self.plan["operations"])
        self.final_state = model.state_through(self.plan, self.final_seq)
        self.historical_bindings = {
            handle: index + 1
            for index, handle in enumerate(
                sorted(
                    {
                        *(view.handle for view in self.final_state.views),
                        *self.final_state.destroyed,
                    }
                )
            )
        }
        self.bindings = {
            view.handle: self.historical_bindings[view.handle]
            for view in self.final_state.views
        }
        self.outputs = {"primary": 10, "secondary": 20}
        self.snapshot = self.make_snapshot(
            self.final_state, self.bindings, self.outputs
        )

    def assert_refused(
        self, action: Callable[[], object], message: str | None = None
    ) -> None:
        if message is None:
            with self.assertRaises(SystemExit):
                action()
        else:
            with self.assertRaisesRegex(SystemExit, message):
                action()

    @staticmethod
    def group_key(placement: model.Placement, outputs: dict[str, int]) -> tuple[int, str]:
        return outputs[placement.output.value], placement.edge.label

    @staticmethod
    def span_for(placement: model.Placement) -> tuple[int, int]:
        if placement.alignment is model.Alignment.START:
            return 0, 450
        if placement.alignment is model.Alignment.END:
            return 550, 450
        return 275, 450

    @classmethod
    def rect_for(
        cls, placement: model.Placement, outputs: dict[str, int]
    ) -> list[int]:
        output_offset = 0 if placement.output is model.OutputRole.PRIMARY else 2000
        primary_start, primary_length = cls.span_for(placement)
        gap = 18
        depth = 50
        if placement.edge in (model.Edge.TOP, model.Edge.BOTTOM):
            y = 0 if placement.edge is model.Edge.TOP else 1000 - depth - gap
            return [output_offset + primary_start, y, primary_length, depth + gap]
        x = output_offset if placement.edge is model.Edge.LEFT else output_offset + 1000 - depth - gap
        return [x, primary_start, depth + gap, primary_length]

    def make_view(
        self,
        expected: model.ExpectedView,
        state: model.ModelState,
        bindings: dict[str, int],
        outputs: dict[str, int],
        group: dict[str, Any],
    ) -> dict[str, Any]:
        persistent_id = bindings[expected.handle]
        placement = expected.placement
        self.assertIsNotNone(placement)
        assert placement is not None
        screen_id = outputs[placement.output.value]
        primary_start, primary_length = self.span_for(placement)
        output_offset = (
            0 if placement.output is model.OutputRole.PRIMARY else 2000
        )
        if placement.output is model.OutputRole.SECONDARY and (
            placement.edge.orientation == "horizontal"
        ):
            primary_start += 2000
        depth = 40 + persistent_id
        gap = 18
        if placement.edge in (model.Edge.TOP, model.Edge.BOTTOM):
            y = (
                0
                if placement.edge is model.Edge.TOP
                else 1000 - depth - gap
            )
            rect = [primary_start, y, primary_length, depth + gap]
        else:
            x = (
                output_offset
                if placement.edge is model.Edge.LEFT
                else output_offset + 1000 - depth - gap
            )
            rect = [x, primary_start, depth + gap, primary_length]
        local = [0, 0, rect[2], rect[3]]
        if placement.edge is model.Edge.TOP:
            attached = [0, 0, primary_length, depth]
            floated = [0, gap, primary_length, depth]
            bridge = [0, 0, primary_length, depth + gap]
            trigger = [rect[0], 0, primary_length, 1]
        elif placement.edge is model.Edge.BOTTOM:
            attached = [0, gap, primary_length, depth]
            floated = [0, 0, primary_length, depth]
            bridge = [0, 0, primary_length, depth + gap]
            trigger = [rect[0], 999, primary_length, 1]
        elif placement.edge is model.Edge.LEFT:
            attached = [0, 0, depth, primary_length]
            floated = [gap, 0, depth, primary_length]
            bridge = [0, 0, depth + gap, primary_length]
            trigger = [rect[0], primary_start, 1, primary_length]
        else:
            attached = [gap, 0, depth, primary_length]
            floated = [0, 0, depth, primary_length]
            bridge = [0, 0, depth + gap, primary_length]
            trigger = [rect[0] + rect[2] - 1, primary_start, 1, primary_length]
        applet_primary_start = 15
        available_primary_length = primary_length - 30
        applets = (
            [applet_primary_start, floated[1], available_primary_length, depth]
            if placement.edge.orientation == "horizontal"
            else [floated[0], applet_primary_start, depth, available_primary_length]
        )
        members = sorted(group["contributorDockIds"])
        editing = expected.handle == state.editing
        owns_config_window = expected.handle == state.config_owner
        member_handles = sorted(
            view.handle
            for view in state.views
            if view.root == expected.handle
        )
        if expected.relationship == "linkedMember":
            assert expected.root is not None
            logical_id = bindings[expected.root]
            original_id: int | None = logical_id
            relationship = "linkedMember"
            link_placement: str | None = "explicitTarget"
            linked_ids: list[int] = []
        else:
            logical_id = persistent_id
            original_id = None
            relationship = "linkedRoot" if member_handles else "independent"
            link_placement = None
            linked_ids = sorted(bindings[handle] for handle in member_handles)

        objects = {
            "view": f"view-{persistent_id}",
            "containment": f"containment-{persistent_id}",
            "configuration": f"configuration-{persistent_id}",
            "layout": "shared-layout",
            "layoutController": f"layout-controller-{persistent_id}",
            "geometryController": f"geometry-controller-{persistent_id}",
            "editController": f"edit-controller-{persistent_id}",
            "configWindow": (
                f"config-window-{persistent_id}"
                if owns_config_window
                else None
            ),
            "reservationPublisher": group["publisher"],
            "transitionController": f"transition-controller-{persistent_id}",
            "windowTouchTracker": f"window-touch-tracker-{persistent_id}",
        }
        reservation_geometry = group["geometry"]
        reservation_window = group["windowGeometry"]
        reservation_anchors = group["layerShellAnchors"]
        reservation_margins = group["layerShellMargins"]
        return {
            "runtimeViewId": str(100 + persistent_id),
            "persistentDockId": persistent_id,
            "logicalDockId": logical_id,
            "originalDockId": original_id,
            "relationship": relationship,
            "linkPlacement": link_placement,
            "screensGroup": "single",
            "linkedDockIds": linked_ids,
            "layout": "My Layout",
            "screenId": screen_id,
            "screen": placement.output.value,
            "onPrimary": (
                expected.follows_primary
                if expected.follows_primary is not None
                else placement.output is model.OutputRole.PRIMARY
            ),
            "type": "panel",
            "edge": placement.edge.label,
            "orientation": placement.edge.orientation,
            "alignment": placement.alignment.label_for(placement.edge),
            "maximumLengthRatio": 0.45,
            "offsetRatio": 0.0,
            "configuredIconSize": 48,
            "effectiveIconSize": 48,
            "availablePrimaryLength": available_primary_length,
            "normalThickness": depth,
            "maximumNormalThickness": depth + 8,
            "windowGeometry": rect,
            "absoluteGeometry": rect,
            "localGeometry": local,
            "screenGeometry": [
                0 if placement.output is model.OutputRole.PRIMARY else 2000,
                0,
                1000,
                1000,
            ],
            "surfaceGeometry": rect,
            "canvasGeometry": rect,
            "effectsRect": floated,
            "appletsLayoutGeometry": applets,
            "maskRect": floated,
            "inputMask": bridge,
            "appliedInputMask": bridge,
            "floatingDamageMaskPending": False,
            "floatingDamageMaskGeneration": "9",
            "enabledBorders": ["top", "right", "bottom", "left"],
            "shadowEnabledBorders": ["top", "right", "bottom", "left"],
            "shadowPaddingOffsets": [0, 0, 0, 0],
            "floatingAppletPopupsPreferred": True,
            "floatingAnchorRevision": "11",
            "strutsThickness": depth,
            "publishedStruts": (
                [rect[0], 0 if placement.edge is model.Edge.TOP else 1000 - depth,
                 primary_length, depth]
                if placement.edge.orientation == "horizontal"
                else [
                    rect[0] if placement.edge is model.Edge.LEFT else rect[0] + rect[2] - depth,
                    primary_start,
                    depth,
                    primary_length,
                ]
            ),
            "layerShellPresent": True,
            "layerShellAnchors": [placement.edge.label],
            "layerShellMargins": [0, 0, 0, 0],
            "layerShellExclusiveEdge": "none",
            "layerShellExclusiveZone": -1,
            "reservationSurfacePresent": True,
            "reservationOutputId": group["outputId"],
            "reservationEdge": group["edge"],
            "reservationContributionDepth": depth,
            "reservationPublishedDepth": group["publishedDepth"],
            "reservationGroupMemberCount": group["memberCount"],
            "reservationGroupGeneration": group["generation"],
            "reservationContributorDockIds": members,
            "reservationGeometry": reservation_geometry,
            "reservationWindowGeometry": reservation_window,
            "reservationLayerShellAnchors": reservation_anchors,
            "reservationLayerShellMargins": reservation_margins,
            "reservationLayerShellExclusiveEdge": group[
                "layerShellExclusiveEdge"
            ],
            "reservationLayerShellExclusiveZone": group[
                "layerShellExclusiveZone"
            ],
            "floatingGapConfigured": True,
            "floatingPanelConfigured": True,
            "floatingPanelEligible": True,
            "attachOnWindowTouchConfigured": True,
            "attachmentWaitsForPointerExitConfigured": False,
            "pointerInsideView": False,
            "attachmentDeferredByPointer": False,
            "dockGapHideRequested": False,
            "touchingWindowCount": 0,
            "windowTouchGeometryRoleType": "",
            "transitionTarget": "floated",
            "transitionProgress": 1.0,
            "transitionPhase": "resting",
            "transitionDirection": "none",
            "transitionRunning": False,
            "transitionGeometryPresent": True,
            "transitionGeometryRevision": "17",
            "stableCanvasGeometry": rect,
            "attachedPresentationGeometry": attached,
            "floatedPresentationGeometry": floated,
            "currentVisibleGeometry": floated,
            "computedPaintMaskGeometry": floated,
            "computedInputBridgeGeometry": bridge,
            "contentTranslation": [0.0, 0.0],
            "stableTriggerGeometry": trigger,
            "stableAppletMeasurementBounds": floated,
            "stablePrimaryAxisStart": primary_start,
            "stablePrimaryAxisLength": primary_length,
            "stableLayerShellMargin": 0,
            "surfaceGeometryPublicationRevision": "21",
            "layerShellConfigureRequestRevision": "22",
            "requestedReservationDepth": depth,
            "visibilityMode": "alwaysVisible",
            "isHidden": False,
            "inStartup": False,
            "isOffScreen": False,
            "inRelocationAnimation": False,
            "inRelocationShowing": False,
            "geometrySettled": True,
            "relocationGeneration": "31",
            "appliedRelocationGeneration": "31",
            "inDelete": False,
            "inReadyState": True,
            "editMode": editing,
            "effectiveConfigureAppletsMode": editing and state.configuring,
            "settingsWindowShown": editing,
            "objects": objects,
        }

    def make_snapshot(
        self,
        state: model.ModelState,
        bindings: dict[str, int],
        outputs: dict[str, int],
    ) -> dict[str, Any]:
        expected_groups: dict[tuple[int, str], list[model.ExpectedView]] = {}
        for expected in state.views:
            self.assertIsNotNone(expected.placement)
            assert expected.placement is not None
            expected_groups.setdefault(
                self.group_key(expected.placement, outputs), []
            ).append(expected)

        groups: list[dict[str, Any]] = []
        groups_by_key: dict[tuple[int, str], dict[str, Any]] = {}
        for generation, (key, contributors) in enumerate(
            sorted(expected_groups.items()), 1
        ):
            contributor_ids = sorted(bindings[view.handle] for view in contributors)
            depths = [40 + persistent_id for persistent_id in contributor_ids]
            edge = key[1]
            horizontal = edge in ("top", "bottom")
            geometry = [
                0 if key[0] == outputs["primary"] else 2000,
                0 if edge == "top" else 950 if edge == "bottom" else 0,
                1000 if horizontal else max(depths),
                max(depths) if horizontal else 1000,
            ]
            group = {
                "outputId": key[0],
                "edge": edge,
                "generation": str(generation),
                "publishedDepth": max(depths),
                "contributorDockIds": contributor_ids,
                "memberCount": len(contributor_ids),
                "geometry": geometry,
                "windowGeometry": [
                    0,
                    0,
                    geometry[2] if horizontal else 1,
                    1 if horizontal else geometry[3],
                ],
                "layerShellPresent": True,
                "layerShellAnchors": [edge],
                "layerShellMargins": [0, 0, 0, 0],
                "layerShellExclusiveEdge": edge,
                "layerShellExclusiveZone": max(depths),
                "publisher": f"reservation-{key[0]}-{edge}",
            }
            groups.append(group)
            groups_by_key[key] = group

        views = [
            self.make_view(
                expected,
                state,
                bindings,
                outputs,
                groups_by_key[self.group_key(expected.placement, outputs)],
            )
            for expected in state.views
            if expected.placement is not None
        ]
        return {
            "schemaVersion": 7,
            "snapshotSequence": "41",
            "globalConfigureAppletsMode": state.configuring,
            "stacking": {
                "available": False,
                "reason": model.STACKING_REASON,
            },
            "reservationStateGeneration": str(max(1, len(groups))),
            "reservationGroups": groups,
            "views": views,
        }

    @staticmethod
    def make_visual_windows(snapshot: dict[str, Any]) -> list[dict[str, Any]]:
        result = [
            {
                "id": f"canvas-{view['persistentDockId']}",
                "caption": f"#view#{view['persistentDockId']}",
                "geometry": list(view["stableCanvasGeometry"]),
                "output": view["screen"],
            }
            for view in snapshot["views"]
        ]
        output_views = {
            view["screenId"]: view for view in snapshot["views"]
        }
        for group in snapshot["reservationGroups"]:
            output_view = output_views[group["outputId"]]
            screen_x, screen_y, screen_width, screen_height = (
                output_view["screenGeometry"]
            )
            _, _, width, height = group["windowGeometry"]
            edge = model.Edge[group["edge"].upper()]
            x = (
                screen_x + screen_width - width
                if edge is model.Edge.RIGHT
                else screen_x
            )
            y = (
                screen_y + screen_height - height
                if edge is model.Edge.BOTTOM
                else screen_y
            )
            result.append(
                {
                    "id": (
                        f"reservation-{group['outputId']}-{group['edge']}"
                    ),
                    "caption": (
                        "#screen-space-reservation"
                        f"#output={group['outputId']}#edge={int(edge)}"
                    ),
                    "geometry": [x, y, width, height],
                    "output": output_view["screen"],
                }
            )
        return result

    def mutated(self, mutation: SnapshotMutation) -> dict[str, Any]:
        snapshot = copy.deepcopy(self.snapshot)
        mutation(snapshot)
        return snapshot

    def assert_checkpoint_refuses(
        self,
        mutation: SnapshotMutation,
        message: str | None = None,
    ) -> None:
        snapshot = self.mutated(mutation)
        self.assert_refused(
            lambda: model.assert_snapshot(
                self.plan,
                self.final_seq,
                self.bindings,
                self.outputs,
                snapshot,
            ),
            message,
        )

    def make_replay_records(
        self,
        identity_bindings: dict[str, int] | None = None,
    ) -> list[dict[str, Any]]:
        recorded_identities = (
            self.historical_bindings
            if identity_bindings is None
            else identity_bindings
        )
        records = [
            model.replay_header(
                {
                    "plan": self.plan,
                    "bindings": {"root": recorded_identities["root"]},
                    "outputs": self.outputs,
                }
            )
        ]
        bindings = {"root": recorded_identities["root"]}
        snapshot_sequence = 1
        for raw in self.plan["operations"]:
            operation = model.parse_operation(raw, raw["seq"])
            records.append(
                model.resolve_operation(
                    {
                        "step": raw,
                        "bindings": bindings,
                        "outputs": self.outputs,
                    }
                )["record"]
            )
            snapshot_sequence = (
                1
                if operation.kind is model.OperationKind.RESTART
                else snapshot_sequence + 1
            )
            result = {
                "record": "result",
                "seq": raw["seq"],
                "snapshotSequence": str(snapshot_sequence),
            }
            if operation.kind in (
                model.OperationKind.CREATE_LINKED,
                model.OperationKind.DUPLICATE,
            ):
                assert operation.result is not None
                created_id = recorded_identities[operation.result]
                result["createdPersistentDockId"] = created_id
                bindings[operation.result] = created_id
            elif operation.kind is model.OperationKind.REMOVE:
                assert operation.target is not None
                del bindings[operation.target]
            records.append(result)
        return records

    def validate_replay_records(
        self, records: list[dict[str, Any]]
    ) -> dict[str, Any]:
        with tempfile.TemporaryDirectory() as directory:
            replay_path = Path(directory) / "replay.jsonl"
            replay_path.write_text(
                "".join(f"{model.compact(record)}\n" for record in records),
                encoding="utf-8",
            )
            return model.validate_replay(str(replay_path), self.plan)

    def test_same_seed_has_stable_digest_and_valid_plan(self) -> None:
        same = model.generate_plan(model.DEFAULT_SEED)
        other = model.generate_plan(model.DEFAULT_SEED + 1)
        self.assertEqual(self.plan, same)
        self.assertEqual(
            self.plan["planSha256"], model.operation_payload_digest(self.plan)
        )
        self.assertNotEqual(self.plan["planSha256"], other["planSha256"])
        self.assertIs(model.validate_plan(self.plan), self.plan)

        state = model.ModelState(
            (
                model.ExpectedView(
                    "root",
                    "independent",
                    None,
                    model.Placement(
                        model.OutputRole.PRIMARY,
                        model.Edge.BOTTOM,
                        model.Alignment.JUSTIFY,
                    ),
                ),
            )
        )
        for index, raw in enumerate(self.plan["operations"], 1):
            state = model.apply_operation(state, model.parse_operation(raw, index))
            placements = [
                view.placement for view in state.views if view.placement is not None
            ]
            for left_index, left in enumerate(placements):
                for right in placements[left_index + 1 :]:
                    self.assertFalse(model.placements_overlap(left, right))

    def test_numeric_primary_move_pins_instead_of_following_primary(self) -> None:
        initial = model.state_through(self.plan, 0)
        root = initial.by_handle()["root"]
        self.assertTrue(root.follows_primary)
        assert root.placement is not None

        moved = model.apply_operation(
            initial,
            model.Operation(
                1,
                model.OperationKind.MOVE,
                target="root",
                placement=root.placement,
            ),
        )
        self.assertFalse(moved.by_handle()["root"].follows_primary)

        duplicated_while_following = model.apply_operation(
            initial,
            model.Operation(
                1,
                model.OperationKind.DUPLICATE,
                source="root",
                result="following-copy",
            ),
        )
        self.assertTrue(
            duplicated_while_following.by_handle()["following-copy"].follows_primary
        )

        linked = model.apply_operation(
            moved,
            model.Operation(
                2,
                model.OperationKind.CREATE_LINKED,
                source="root",
                result="member",
                placement=model.Placement(
                    model.OutputRole.PRIMARY,
                    model.Edge.LEFT,
                    model.Alignment.START,
                ),
            ),
        )
        self.assertFalse(linked.by_handle()["member"].follows_primary)

        duplicated_while_pinned = model.apply_operation(
            moved,
            model.Operation(
                2,
                model.OperationKind.DUPLICATE,
                source="root",
                result="pinned-copy",
            ),
        )
        self.assertFalse(
            duplicated_while_pinned.by_handle()["pinned-copy"].follows_primary
        )

    def test_plan_digest_use_after_destroy_and_overlap_are_refused(self) -> None:
        bad_digest = copy.deepcopy(self.plan)
        bad_digest["seed"] = str(model.DEFAULT_SEED + 2)
        self.assert_refused(lambda: model.validate_plan(bad_digest), "planSha256")

        remove_index = next(
            index
            for index, raw in enumerate(self.plan["operations"])
            if raw["operation"]["kind"] == model.OperationKind.REMOVE.value
        )
        use_after_destroy = copy.deepcopy(self.plan)
        use_after_destroy["operations"][remove_index + 1] = {
            "seq": remove_index + 2,
            "checkpoint": True,
            "operation": {
                "kind": "move",
                "target": "member-a",
                "placement": model.Placement(
                    model.OutputRole.PRIMARY,
                    model.Edge.TOP,
                    model.Alignment.CENTER,
                ).to_json(),
            },
        }
        use_after_destroy["planSha256"] = model.operation_payload_digest(
            use_after_destroy
        )
        self.assert_refused(
            lambda: model.validate_plan(use_after_destroy), "non-live"
        )

        state = model.ModelState(
            (
                model.ExpectedView(
                    "a",
                    "independent",
                    None,
                    model.Placement(
                        model.OutputRole.PRIMARY,
                        model.Edge.TOP,
                        model.Alignment.START,
                    ),
                ),
                model.ExpectedView(
                    "b",
                    "independent",
                    None,
                    model.Placement(
                        model.OutputRole.PRIMARY,
                        model.Edge.BOTTOM,
                        model.Alignment.START,
                    ),
                ),
            )
        )
        overlap = model.Operation(
            1,
            model.OperationKind.MOVE,
            target="b",
            placement=model.Placement(
                model.OutputRole.PRIMARY,
                model.Edge.TOP,
                model.Alignment.START,
            ),
        )
        self.assert_refused(
            lambda: model.apply_operation(state, overlap), "overlap"
        )

    def test_recomputed_digest_does_not_authorize_a_noncanonical_plan(self) -> None:
        mutated = copy.deepcopy(self.plan)
        mutated["operations"][0]["checkpoint"] = not mutated["operations"][0][
            "checkpoint"
        ]
        mutated["planSha256"] = model.operation_payload_digest(mutated)
        self.assertEqual(
            mutated["planSha256"], model.operation_payload_digest(mutated)
        )
        self.assert_refused(
            lambda: model.validate_plan(mutated),
            "canonical splitmix64-v1",
        )

    def test_schema_and_current_visible_geometry_are_strict(self) -> None:
        malformed = self.mutated(
            lambda snapshot: snapshot.__setitem__("schemaVersion", 6)
        )
        self.assert_refused(lambda: model.parse_snapshot(malformed), "schema 7")

        nullable_publisher_wire = self.mutated(
            lambda snapshot: snapshot["views"][0]["objects"].__setitem__(
                "reservationPublisher", None
            )
        )
        self.assertIs(
            model.parse_snapshot(nullable_publisher_wire),
            nullable_publisher_wire,
        )
        self.assert_refused(
            lambda: model.assert_snapshot(
                self.plan,
                self.final_seq,
                self.bindings,
                self.outputs,
                nullable_publisher_wire,
            ),
            "reservation mirror diverged",
        )

        missing = self.mutated(
            lambda snapshot: snapshot["views"][0].pop("currentVisibleGeometry")
        )
        self.assert_refused(
            lambda: model.parse_snapshot(missing), "currentVisibleGeometry"
        )

        renamed = self.mutated(
            lambda snapshot: (
                snapshot["views"][0].__setitem__(
                    "visibleGeometry",
                    snapshot["views"][0]["currentVisibleGeometry"],
                ),
                snapshot["views"][0].pop("currentVisibleGeometry"),
            )
        )
        self.assertIn("visibleGeometry", renamed["views"][0])
        self.assert_refused(
            lambda: model.parse_snapshot(renamed), "currentVisibleGeometry"
        )

    def test_visual_ownership_matches_global_output_edge_geometry(self) -> None:
        windows = self.make_visual_windows(self.snapshot)
        result = model.assert_visual_window_ownership(
            {"snapshot": self.snapshot, "windows": windows}
        )
        self.assertEqual(result["windowCount"], len(windows))

        bottom_or_right = next(
            window
            for window in windows
            if window["caption"].endswith(
                (f"edge={int(model.Edge.BOTTOM)}", f"edge={int(model.Edge.RIGHT)}")
            )
        )
        self.assertNotEqual(bottom_or_right["geometry"][:2], [0, 0])

        leaked = copy.deepcopy(windows)
        leaked.append(
            {
                "id": "stale-reservation",
                "caption": "#screen-space-reservation#output=99#edge=4",
                "geometry": [0, 999, 1000, 1],
                "output": "primary",
            }
        )
        self.assert_refused(
            lambda: model.assert_visual_window_ownership(
                {"snapshot": self.snapshot, "windows": leaked}
            ),
            "surplus",
        )

        wrong_edge = copy.deepcopy(windows)
        reservation = next(
            window
            for window in wrong_edge
            if window["caption"].startswith("#screen-space-reservation")
        )
        edge_number = int(reservation["caption"].rsplit("=", 1)[1])
        if edge_number == int(model.Edge.TOP):
            reservation["geometry"][1] += 1
        elif edge_number == int(model.Edge.BOTTOM):
            reservation["geometry"][1] -= 1
        elif edge_number == int(model.Edge.LEFT):
            reservation["geometry"][0] += 1
        else:
            reservation["geometry"][0] -= 1
        self.assert_refused(
            lambda: model.assert_visual_window_ownership(
                {"snapshot": self.snapshot, "windows": wrong_edge}
            ),
            "publisher match count is 0",
        )

    def test_valid_checkpoint_and_identity_authority_negatives(self) -> None:
        state = model.assert_snapshot(
            self.plan,
            self.final_seq,
            self.bindings,
            self.outputs,
            self.snapshot,
        )
        self.assertEqual(state, self.final_state)

        self.assert_checkpoint_refuses(
            lambda snapshot: snapshot["views"][1].__setitem__(
                "runtimeViewId", snapshot["views"][0]["runtimeViewId"]
            )
        )
        self.assert_checkpoint_refuses(
            lambda snapshot: snapshot["views"][1]["objects"].__setitem__(
                "transitionController",
                snapshot["views"][0]["objects"]["transitionController"],
            )
        )
        self.assert_refused(
            lambda: model.assert_snapshot(
                self.plan,
                self.final_seq,
                {**self.bindings, "ghost": 999},
                self.outputs,
                self.snapshot,
            ),
            "surplus bindings",
        )

    def test_float32_maximum_length_ratio_is_a_valid_checkpoint(self) -> None:
        snapshot = copy.deepcopy(self.snapshot)
        snapshot["views"][0]["maximumLengthRatio"] = 0.44999998807907104
        self.assertEqual(
            model.assert_snapshot(
                self.plan,
                self.final_seq,
                self.bindings,
                self.outputs,
                snapshot,
            ),
            self.final_state,
        )

    def test_stable_same_edge_spans_must_not_overlap(self) -> None:
        def overlap_stable_spans(snapshot: dict[str, Any]) -> None:
            by_edge: dict[tuple[int, str], list[dict[str, Any]]] = {}
            for view in snapshot["views"]:
                by_edge.setdefault((view["screenId"], view["edge"]), []).append(
                    view
                )
            left, right = next(
                views for views in by_edge.values() if len(views) >= 2
            )[:2]
            right["stablePrimaryAxisStart"] = left["stablePrimaryAxisStart"]
            right["stablePrimaryAxisLength"] = left["stablePrimaryAxisLength"]
            trigger = right["stableTriggerGeometry"]
            if right["orientation"] == "horizontal":
                trigger[0] = left["stablePrimaryAxisStart"]
                trigger[2] = left["stablePrimaryAxisLength"]
            else:
                trigger[1] = left["stablePrimaryAxisStart"]
                trigger[3] = left["stablePrimaryAxisLength"]

        self.assert_checkpoint_refuses(
            overlap_stable_spans,
            "overlapping stable spans",
        )

    def test_checkpoint_rejects_hardened_authority_drift(self) -> None:
        mutations: dict[str, tuple[SnapshotMutation, str]] = {
            "multi-screen lineage": (
                lambda snapshot: snapshot["views"][0].__setitem__(
                    "screensGroup", "all"
                ),
                "lineage",
            ),
            "duplicate reservation key": (
                lambda snapshot: snapshot["reservationGroups"].append(
                    copy.deepcopy(snapshot["reservationGroups"][0])
                ),
                "duplicate output-edge key",
            ),
            "stale config window": (
                lambda snapshot: snapshot["views"][0]["objects"].__setitem__(
                    "configWindow", "stale-config-window"
                ),
                "wrong config window owner state",
            ),
            "applet budget mismatch": (
                lambda snapshot: snapshot["views"][0].__setitem__(
                    "availablePrimaryLength",
                    snapshot["views"][0]["availablePrimaryLength"] + 1,
                ),
                "applet and popup geometry",
            ),
            "zero reservation width": (
                lambda snapshot: snapshot["reservationGroups"][0]["geometry"].__setitem__(
                    2, 0
                ),
                "maximum-depth ownership",
            ),
            "missing reservation anchor": (
                lambda snapshot: snapshot["reservationGroups"][0].__setitem__(
                    "layerShellAnchors", []
                ),
                "maximum-depth ownership",
            ),
        }
        for name, (mutation, message) in mutations.items():
            with self.subTest(name=name):
                self.assert_checkpoint_refuses(mutation, message)

    def test_primary_and_secondary_outputs_must_be_distinct(self) -> None:
        self.assert_refused(
            lambda: model.assert_snapshot(
                self.plan,
                self.final_seq,
                self.bindings,
                {"primary": 10, "secondary": 10},
                self.snapshot,
            ),
            "distinct identities",
        )

    def test_lineage_placement_orientation_and_destroyed_residue_are_refused(
        self,
    ) -> None:
        member_index = next(
            index
            for index, view in enumerate(self.snapshot["views"])
            if view["relationship"] == "linkedMember"
        )
        self.assert_checkpoint_refuses(
            lambda snapshot: snapshot["views"][member_index].__setitem__(
                "originalDockId", None
            )
        )
        self.assert_checkpoint_refuses(
            lambda snapshot: snapshot["views"][0].__setitem__(
                "screenId",
                self.outputs["secondary"]
                if snapshot["views"][0]["screenId"] == self.outputs["primary"]
                else self.outputs["primary"],
            )
        )
        self.assert_checkpoint_refuses(
            lambda snapshot: snapshot["views"][0].__setitem__(
                "orientation",
                "vertical"
                if snapshot["views"][0]["orientation"] == "horizontal"
                else "horizontal",
            )
        )
        destroyed_id = max(self.historical_bindings.values()) + 100
        root_index = next(
            index
            for index, view in enumerate(self.snapshot["views"])
            if view["relationship"] == "linkedRoot"
        )
        self.assert_checkpoint_refuses(
            lambda snapshot: snapshot["views"][root_index][
                "linkedDockIds"
            ].append(destroyed_id)
        )

    def test_reservation_max_orphan_and_residue_are_refused(self) -> None:
        self.assert_checkpoint_refuses(
            lambda snapshot: snapshot["reservationGroups"][0].__setitem__(
                "publishedDepth",
                snapshot["reservationGroups"][0]["publishedDepth"] + 1,
            )
        )

        def add_orphan(snapshot: dict[str, Any]) -> None:
            orphan = copy.deepcopy(snapshot["reservationGroups"][0])
            orphan["outputId"] = 999
            orphan["publisher"] = "orphan-publisher"
            snapshot["reservationGroups"].append(orphan)

        self.assert_checkpoint_refuses(add_orphan)

        destroyed_id = max(self.historical_bindings.values()) + 100

        def add_destroyed_residue(snapshot: dict[str, Any]) -> None:
            snapshot["reservationGroups"][0]["contributorDockIds"].append(
                destroyed_id
            )
            snapshot["reservationGroups"][0]["contributorDockIds"].sort()
            snapshot["reservationGroups"][0]["memberCount"] += 1

        self.assert_checkpoint_refuses(add_destroyed_residue)

    def test_edit_participants_are_exact(self) -> None:
        begin_index = next(
            index
            for index, raw in enumerate(self.plan["operations"], 1)
            if raw["operation"]["kind"] == model.OperationKind.BEGIN_EDIT.value
        )
        state = model.state_through(self.plan, begin_index)
        handles = {view.handle for view in state.views}
        bindings = {
            handle: self.historical_bindings[handle]
            for handle in handles
        }
        snapshot = self.make_snapshot(state, bindings, self.outputs)
        model.assert_snapshot(
            self.plan, begin_index, bindings, self.outputs, snapshot
        )

        unrelated = next(
            view
            for view in snapshot["views"]
            if view["persistentDockId"] != bindings[state.editing or ""]
        )
        unrelated["editMode"] = True
        unrelated["settingsWindowShown"] = True
        unrelated["objects"]["configWindow"] = "leaked-config-window"
        self.assert_refused(
            lambda: model.assert_snapshot(
                self.plan,
                begin_index,
                bindings,
                self.outputs,
                snapshot,
            ),
            "edit presentation",
        )

        end_index = next(
            index
            for index, raw in enumerate(self.plan["operations"], 1)
            if raw["operation"]["kind"] == model.OperationKind.END_EDIT.value
        )
        ended_state = model.state_through(self.plan, end_index)
        ended_handles = {view.handle for view in ended_state.views}
        ended_bindings = {
            handle: self.historical_bindings[handle]
            for handle in ended_handles
        }
        ended_snapshot = self.make_snapshot(
            ended_state,
            ended_bindings,
            self.outputs,
        )
        model.assert_snapshot(
            self.plan,
            end_index,
            ended_bindings,
            self.outputs,
            ended_snapshot,
        )
        config_owner_id = ended_bindings[ended_state.config_owner or ""]
        owner = next(
            view
            for view in ended_snapshot["views"]
            if view["persistentDockId"] == config_owner_id
        )
        unrelated = next(
            view
            for view in ended_snapshot["views"]
            if view["persistentDockId"] != config_owner_id
        )
        unrelated["objects"]["configWindow"] = owner["objects"]["configWindow"]
        owner["objects"]["configWindow"] = None
        self.assert_refused(
            lambda: model.assert_snapshot(
                self.plan,
                end_index,
                ended_bindings,
                self.outputs,
                ended_snapshot,
            ),
            "wrong config window owner state",
        )

    def test_transition_endpoint_and_running_state_are_refused(self) -> None:
        self.assert_checkpoint_refuses(
            lambda snapshot: snapshot["views"][0].__setitem__(
                "transitionProgress", 0.5
            )
        )

        def leave_running(snapshot: dict[str, Any]) -> None:
            snapshot["views"][0]["transitionRunning"] = True
            snapshot["views"][0]["transitionPhase"] = "floating"
            snapshot["views"][0]["transitionDirection"] = "towardFloated"

        self.assert_checkpoint_refuses(leave_running)
        for revision in (
            "transitionGeometryRevision",
            "surfaceGeometryPublicationRevision",
            "layerShellConfigureRequestRevision",
        ):
            with self.subTest(revision=revision):
                self.assert_checkpoint_refuses(
                    lambda snapshot, field=revision: snapshot["views"][
                        0
                    ].__setitem__(field, "0")
                )

    def test_quiescent_and_durable_projections_detect_the_right_drift(self) -> None:
        quiescent = model.quiescent_projection(self.snapshot)
        sequence_only = copy.deepcopy(self.snapshot)
        sequence_only["snapshotSequence"] = "42"
        self.assertEqual(
            quiescent, model.quiescent_projection(sequence_only)
        )

        runtime_drift = copy.deepcopy(self.snapshot)
        runtime_drift["views"][0]["runtimeViewId"] = "999"
        self.assertNotEqual(
            quiescent, model.quiescent_projection(runtime_drift)
        )
        self.assertEqual(
            model.durable_projection(self.snapshot),
            model.durable_projection(runtime_drift),
        )

        placement_drift = copy.deepcopy(self.snapshot)
        placement_drift["views"][0]["alignment"] = "center"
        self.assertNotEqual(
            model.durable_projection(self.snapshot),
            model.durable_projection(placement_drift),
        )

        geometry_drift = copy.deepcopy(self.snapshot)
        geometry_drift["views"][0]["currentVisibleGeometry"][0] += 1
        self.assertNotEqual(
            model.durable_projection(self.snapshot),
            model.durable_projection(geometry_drift),
        )

    def test_runtime_reload_requires_exact_affected_rotation(self) -> None:
        before = copy.deepcopy(self.snapshot)
        after = copy.deepcopy(self.snapshot)
        affected = ["root", "member-b", "member-c"]
        affected_ids = {self.bindings[handle] for handle in affected}
        for view in after["views"]:
            if view["persistentDockId"] in affected_ids:
                view["runtimeViewId"] = str(int(view["runtimeViewId"]) + 1000)
        request = {
            "before": before,
            "after": after,
            "bindings": self.bindings,
            "affected": affected,
        }
        self.assertEqual(model.assert_runtime_reload(request), {"ok": True})

        not_rotated = copy.deepcopy(after)
        target = next(
            view
            for view in not_rotated["views"]
            if view["persistentDockId"] in affected_ids
        )
        original = next(
            view
            for view in before["views"]
            if view["persistentDockId"] == target["persistentDockId"]
        )
        target["runtimeViewId"] = original["runtimeViewId"]
        bad_request = {**request, "after": not_rotated}
        self.assert_refused(
            lambda: model.assert_runtime_reload(bad_request),
            "runtime reload ownership",
        )

        rotated_unrelated = copy.deepcopy(after)
        unrelated = next(
            view
            for view in rotated_unrelated["views"]
            if view["persistentDockId"] not in affected_ids
        )
        unrelated["runtimeViewId"] = str(int(unrelated["runtimeViewId"]) + 1000)
        self.assert_refused(
            lambda: model.assert_runtime_reload(
                {**request, "after": rotated_unrelated}
            ),
            "runtime reload ownership",
        )

        swapped_retired = copy.deepcopy(after)
        affected_views = [
            view
            for view in swapped_retired["views"]
            if view["persistentDockId"] in affected_ids
        ]
        before_runtime = {
            view["persistentDockId"]: view["runtimeViewId"]
            for view in before["views"]
        }
        first, second = affected_views[:2]
        first["runtimeViewId"] = before_runtime[second["persistentDockId"]]
        second["runtimeViewId"] = before_runtime[first["persistentDockId"]]
        self.assert_refused(
            lambda: model.assert_runtime_reload(
                {**request, "after": swapped_retired}
            ),
            "reused a retired identity",
        )

    def test_replay_rejects_truncation_and_surplus(self) -> None:
        records = self.make_replay_records()
        self.assertEqual(
            self.validate_replay_records(records),
            {"ok": True, "operationCount": len(self.plan["operations"])},
        )
        self.assert_refused(
            lambda: self.validate_replay_records(records[:-1]),
            "truncated or has surplus",
        )
        self.assert_refused(
            lambda: self.validate_replay_records(
                [*records, {"record": "result", "seq": self.final_seq + 1}]
            ),
            "truncated or has surplus",
        )

    def test_replay_rejects_tampered_authoritative_fields(self) -> None:
        mutations: dict[str, tuple[Callable[[list[dict[str, Any]]], None], str]] = {
            "header outputs": (
                lambda records: records[0]["outputs"].__setitem__("primary", 11),
                "pair 1 diverges",
            ),
            "resolved operation": (
                lambda records: records[1]["resolved"].__setitem__(
                    "targetPersistentDockId",
                    records[1]["resolved"]["targetPersistentDockId"] + 100,
                ),
                "pair 1 diverges",
            ),
            "missing created id": (
                lambda records: next(
                    record
                    for record in records
                    if "createdPersistentDockId" in record
                ).pop("createdPersistentDockId"),
                "createdPersistentDockId",
            ),
            "zero snapshot sequence": (
                lambda records: records[2].__setitem__(
                    "snapshotSequence", "0"
                ),
                "pair 1 diverges",
            ),
        }
        for name, (mutation, message) in mutations.items():
            with self.subTest(name=name):
                records = self.make_replay_records()
                mutation(records)
                self.assert_refused(
                    lambda: self.validate_replay_records(records),
                    message,
                )

    def test_resolve_and_bind_preserve_symbolic_identity(self) -> None:
        create = next(
            raw
            for raw in self.plan["operations"]
            if raw["operation"]["kind"] == model.OperationKind.CREATE_LINKED.value
        )
        resolved = model.resolve_operation(
            {
                "step": create,
                "bindings": {"root": 1},
                "outputs": self.outputs,
            }
        )
        self.assertEqual(resolved["action"]["method"], "createLinkedView")
        self.assertEqual(resolved["record"]["operation"], create["operation"])

        initial_state = model.state_through(self.plan, create["seq"] - 1)
        before = self.make_snapshot(initial_state, {"root": 1}, self.outputs)
        next_state = model.state_through(self.plan, create["seq"] + 1)
        after_bindings = {"root": 1, create["operation"]["result"]: 2}
        after = self.make_snapshot(next_state, after_bindings, self.outputs)
        after["snapshotSequence"] = "42"
        result = model.bind_result(
            {
                "step": create,
                "bindings": {"root": 1},
                "before": before,
                "after": after,
            }
        )
        self.assertEqual(result["bindings"], after_bindings)
        self.assertEqual(result["record"]["createdPersistentDockId"], 2)

    def test_removed_persistent_identity_can_bind_a_later_generation(self) -> None:
        remove = next(
            raw
            for raw in self.plan["operations"]
            if raw["operation"]["kind"] == model.OperationKind.REMOVE.value
        )
        create = self.plan["operations"][remove["seq"] + 1]
        self.assertEqual(
            create["operation"]["kind"],
            model.OperationKind.CREATE_LINKED.value,
        )
        removed_handle = remove["operation"]["target"]
        created_handle = create["operation"]["result"]
        reused_id = self.historical_bindings[removed_handle]

        before_remove_state = model.state_through(self.plan, remove["seq"] - 1)
        before_remove_bindings = {
            view.handle: self.historical_bindings[view.handle]
            for view in before_remove_state.views
        }
        before_remove = self.make_snapshot(
            before_remove_state,
            before_remove_bindings,
            self.outputs,
        )
        after_remove_state = model.state_through(self.plan, remove["seq"])
        after_remove_bindings = {
            view.handle: self.historical_bindings[view.handle]
            for view in after_remove_state.views
        }
        after_remove = self.make_snapshot(
            after_remove_state,
            after_remove_bindings,
            self.outputs,
        )
        after_remove["snapshotSequence"] = "42"

        removal_result = model.bind_result(
            {
                "step": remove,
                "bindings": before_remove_bindings,
                "before": before_remove,
                "after": after_remove,
            }
        )
        self.assertNotIn(removed_handle, removal_result["bindings"])

        # CREATE_LINKED binds the runtime identity before the following MOVE
        # supplies the complete placement. A settled synthetic snapshot
        # therefore uses the next state, while bind_result checks only the
        # exact live-ID delta.
        after_create_state = model.state_through(self.plan, create["seq"] + 1)
        after_create_bindings = dict(removal_result["bindings"])
        after_create_bindings[created_handle] = reused_id
        before_create = self.make_snapshot(
            model.state_through(self.plan, create["seq"] - 1),
            removal_result["bindings"],
            self.outputs,
        )
        before_create["snapshotSequence"] = "42"
        after_create = self.make_snapshot(
            after_create_state,
            after_create_bindings,
            self.outputs,
        )
        after_create["snapshotSequence"] = "43"

        creation_result = model.bind_result(
            {
                "step": create,
                "bindings": removal_result["bindings"],
                "before": before_create,
                "after": after_create,
            }
        )
        self.assertEqual(
            creation_result["bindings"][created_handle],
            reused_id,
        )

        replay_identities = dict(self.historical_bindings)
        replay_identities[created_handle] = reused_id
        self.assertEqual(
            self.validate_replay_records(
                self.make_replay_records(replay_identities)
            ),
            {"ok": True, "operationCount": self.final_seq},
        )


if __name__ == "__main__":
    unittest.main()
