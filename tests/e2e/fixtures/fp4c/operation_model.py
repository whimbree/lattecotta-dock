#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later

"""Typed deterministic plan and schema-10 oracle for FP-4C."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import sys
from dataclasses import dataclass, replace
from enum import Enum, IntEnum
from pathlib import Path
from typing import Any, Iterable, NoReturn


SCHEMA_VERSION = 11
PLAN_FORMAT = "lattecotta.fp4c.operation-plan"
REPLAY_FORMAT = "lattecotta.fp4c.operation-replay"
FORMAT_VERSION = 1
DEFAULT_SEED = 127_934_575
GENERATOR = "splitmix64-v1"
VIEW_MOVE_LIFECYCLE_SCHEMA_VERSION = 2
VIEW_MOVE_LIFECYCLE_GENERATIONS = (
    "journalCreatedGeneration",
    "commitDecisionGeneration",
    "journalRetiredGeneration",
)
FLOAT32_ABSOLUTE_TOLERANCE = 1e-6
STACKING_REASON = (
    "Inward same-edge stacking is unsupported; stable-span overlap is not yet rejected."
)


def fail(message: str) -> NoReturn:
    raise SystemExit(f"operation_model: {message}")


def compact(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def read_json() -> Any:
    try:
        return json.load(sys.stdin)
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        fail(f"invalid JSON input: {error}")


def require_object(value: Any, label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        fail(f"{label} must be an object")
    return value


def require_keys(value: dict[str, Any], keys: Iterable[str], label: str) -> None:
    missing = sorted(set(keys) - set(value))
    if missing:
        fail(f"{label} is missing required keys {missing}")


def require_int(value: Any, label: str, minimum: int | None = None) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        fail(f"{label} must be an integer")
    if minimum is not None and value < minimum:
        fail(f"{label} must be at least {minimum}")
    return value


def require_string(value: Any, label: str, *, nonempty: bool = True) -> str:
    if not isinstance(value, str) or (nonempty and not value):
        fail(f"{label} must be {'a nonempty ' if nonempty else 'a '}string")
    return value


def require_decimal(value: Any, label: str) -> int:
    text = require_string(value, label)
    if not text.isdecimal():
        fail(f"{label} must be a decimal string")
    return int(text)


def require_number(value: Any, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        fail(f"{label} must be a number")
    result = float(value)
    if not math.isfinite(result):
        fail(f"{label} must be finite")
    return result


def require_bool(value: Any, label: str) -> bool:
    if not isinstance(value, bool):
        fail(f"{label} must be boolean")
    return value


def require_array(value: Any, label: str, length: int | None = None) -> list[Any]:
    if not isinstance(value, list) or (length is not None and len(value) != length):
        suffix = "" if length is None else f" with {length} elements"
        fail(f"{label} must be an array{suffix}")
    return value


def require_number_array(value: Any, label: str, length: int) -> list[Any]:
    result = require_array(value, label, length)
    for index, component in enumerate(result):
        require_number(component, f"{label}[{index}]")
    return result


def require_string_array(value: Any, label: str) -> list[str]:
    result = require_array(value, label)
    for index, component in enumerate(result):
        require_string(component, f"{label}[{index}]")
    return result


def require_id_array(value: Any, label: str) -> list[int]:
    result = require_array(value, label)
    for index, component in enumerate(result):
        require_int(component, f"{label}[{index}]", 1)
    return result


class OutputRole(str, Enum):
    PRIMARY = "primary"
    SECONDARY = "secondary"


class LayoutRole(str, Enum):
    ORIGIN = "origin"
    DESTINATION = "destination"


@dataclass(frozen=True, slots=True)
class OutputSnapshot:
    identity: int
    name: str
    geometry: tuple[int, int, int, int]


class Edge(IntEnum):
    TOP = 3
    BOTTOM = 4
    LEFT = 5
    RIGHT = 6

    @property
    def label(self) -> str:
        return self.name.lower()

    @property
    def orientation(self) -> str:
        return "horizontal" if self in (Edge.TOP, Edge.BOTTOM) else "vertical"


class Alignment(IntEnum):
    CENTER = 0
    START = 1
    END = 2
    JUSTIFY = 10

    def label_for(self, edge: Edge) -> str:
        if self is Alignment.CENTER:
            return "center"
        if self is Alignment.JUSTIFY:
            return "justify"
        if edge.orientation == "horizontal":
            return "left" if self is Alignment.START else "right"
        return "top" if self is Alignment.START else "bottom"

    @property
    def interval(self) -> tuple[float, float]:
        return {
            Alignment.START: (0.0, 0.45),
            Alignment.CENTER: (0.275, 0.725),
            Alignment.END: (0.55, 1.0),
            Alignment.JUSTIFY: (0.0, 1.0),
        }[self]


class OperationKind(str, Enum):
    MOVE = "move"
    MOVE_LAYOUT = "moveLayout"
    CREATE_LINKED = "createLinked"
    DUPLICATE = "duplicateIndependent"
    BEGIN_EDIT = "beginEdit"
    CONFIGURE_ON = "configureAppletsOn"
    CONFIGURE_OFF = "configureAppletsOff"
    END_EDIT = "endEdit"
    REMOVE = "remove"
    RESTART = "restartProcess"
    RELOAD = "reloadLinkedRoot"


@dataclass(frozen=True, slots=True)
class SplitMix64:
    state: int

    def next(self) -> tuple["SplitMix64", int]:
        state = (self.state + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
        value = state
        value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & 0xFFFFFFFFFFFFFFFF
        value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & 0xFFFFFFFFFFFFFFFF
        return SplitMix64(state), (value ^ (value >> 31)) & 0xFFFFFFFFFFFFFFFF

    def bounded(self, upper: int) -> tuple["SplitMix64", int]:
        if upper <= 0:
            fail(f"SplitMix64 bound must be positive, got {upper}")
        generator, value = self.next()
        return generator, value % upper


@dataclass(frozen=True, slots=True)
class Placement:
    output: OutputRole
    edge: Edge
    alignment: Alignment

    def to_json(self) -> dict[str, Any]:
        return {
            "output": self.output.value,
            "edge": self.edge.label,
            "edgeValue": int(self.edge),
            "alignment": self.alignment.name.lower(),
            "alignmentValue": int(self.alignment),
        }


@dataclass(frozen=True, slots=True)
class Operation:
    seq: int
    kind: OperationKind
    target: str | None = None
    source: str | None = None
    result: str | None = None
    placement: Placement | None = None
    layout: LayoutRole | None = None
    checkpoint: bool = True
    affected: tuple[str, ...] = ()

    def to_json(self) -> dict[str, Any]:
        payload: dict[str, Any] = {"kind": self.kind.value}
        for key, value in (
            ("target", self.target),
            ("source", self.source),
            ("result", self.result),
        ):
            if value is not None:
                payload[key] = value
        if self.placement is not None:
            payload["placement"] = self.placement.to_json()
        if self.layout is not None:
            payload["layout"] = self.layout.value
        if self.affected:
            payload["affected"] = list(self.affected)
        return {"seq": self.seq, "checkpoint": self.checkpoint, "operation": payload}


@dataclass(frozen=True, slots=True)
class ExpectedView:
    handle: str
    relationship: str
    root: str | None
    placement: Placement | None
    layout: LayoutRole = LayoutRole.ORIGIN
    follows_primary: bool | None = None


@dataclass(frozen=True, slots=True)
class ModelState:
    views: tuple[ExpectedView, ...]
    editing: str | None = None
    configuring: bool = False
    config_owner: str | None = None
    destroyed: tuple[str, ...] = ()

    def by_handle(self) -> dict[str, ExpectedView]:
        return {view.handle: view for view in self.views}


def placements_overlap(left: Placement, right: Placement) -> bool:
    if left.output is not right.output or left.edge is not right.edge:
        return False
    left_start, left_end = left.alignment.interval
    right_start, right_end = right.alignment.interval
    return max(left_start, right_start) < min(left_end, right_end)


def safe_placement(
    placements: dict[str, Placement], target: str, candidate: Placement
) -> bool:
    return all(
        handle == target or not placements_overlap(candidate, placement)
        for handle, placement in placements.items()
    )


def choose_safe_placement(
    generator: SplitMix64,
    placements: dict[str, Placement],
    target: str,
) -> tuple[SplitMix64, Placement]:
    candidates = [
        Placement(output, edge, alignment)
        for output in OutputRole
        for edge in Edge
        for alignment in Alignment
        if safe_placement(placements, target, Placement(output, edge, alignment))
        and placements.get(target) != Placement(output, edge, alignment)
    ]
    if not candidates:
        fail(f"no non-overlapping placement remains for {target}")
    generator, index = generator.bounded(len(candidates))
    return generator, candidates[index]


def operation_payload_digest(plan: dict[str, Any]) -> str:
    payload = dict(plan)
    payload.pop("planSha256", None)
    return hashlib.sha256(compact(payload).encode()).hexdigest()


def generate_plan(seed: int, *, validate: bool = True) -> dict[str, Any]:
    if seed < 0 or seed > 0xFFFFFFFFFFFFFFFF:
        fail(f"seed is outside uint64: {seed}")
    operations: list[Operation] = []
    generator = SplitMix64(seed)
    placements: dict[str, Placement] = {
        "root": Placement(OutputRole.PRIMARY, Edge.BOTTOM, Alignment.JUSTIFY)
    }
    live = ["root"]

    def append(
        kind: OperationKind,
        *,
        target: str | None = None,
        source: str | None = None,
        result: str | None = None,
        placement: Placement | None = None,
        layout: LayoutRole | None = None,
        checkpoint: bool = True,
        affected: tuple[str, ...] = (),
    ) -> None:
        operations.append(
            Operation(
                len(operations) + 1,
                kind,
                target,
                source,
                result,
                placement,
                layout,
                checkpoint,
                affected,
            )
        )

    def move(target: str, placement: Placement, checkpoint: bool = True) -> None:
        if not safe_placement(placements, target, placement):
            fail(f"generated overlapping placement for {target}: {placement}")
        placements[target] = placement
        append(
            OperationKind.MOVE,
            target=target,
            placement=placement,
            checkpoint=checkpoint,
        )

    def edit_cycle(target: str) -> None:
        append(OperationKind.BEGIN_EDIT, target=target)
        append(OperationKind.CONFIGURE_ON, target=target)
        append(OperationKind.CONFIGURE_OFF, target=target)
        append(OperationKind.END_EDIT, target=target)

    move("root", Placement(OutputRole.PRIMARY, Edge.BOTTOM, Alignment.START))
    append(
        OperationKind.MOVE_LAYOUT,
        target="root",
        layout=LayoutRole.DESTINATION,
    )
    append(
        OperationKind.MOVE_LAYOUT,
        target="root",
        layout=LayoutRole.ORIGIN,
    )
    append(
        OperationKind.CREATE_LINKED,
        source="root",
        result="member-a",
        placement=Placement(OutputRole.PRIMARY, Edge.TOP, Alignment.START),
        checkpoint=False,
    )
    live.append("member-a")
    placements["member-a"] = Placement(OutputRole.PRIMARY, Edge.TOP, Alignment.START)
    move("member-a", Placement(OutputRole.PRIMARY, Edge.TOP, Alignment.END))
    append(
        OperationKind.CREATE_LINKED,
        source="member-a",
        result="member-b",
        placement=Placement(OutputRole.SECONDARY, Edge.LEFT, Alignment.START),
        checkpoint=False,
    )
    live.append("member-b")
    placements["member-b"] = Placement(OutputRole.SECONDARY, Edge.LEFT, Alignment.START)
    move("member-b", Placement(OutputRole.SECONDARY, Edge.LEFT, Alignment.CENTER))
    append(
        OperationKind.DUPLICATE,
        source="member-a",
        result="duplicate-a",
        checkpoint=False,
    )
    live.append("duplicate-a")
    move("duplicate-a", Placement(OutputRole.PRIMARY, Edge.RIGHT, Alignment.CENTER))

    coverage = (
        ("root", Placement(OutputRole.PRIMARY, Edge.TOP, Alignment.START)),
        ("member-a", Placement(OutputRole.PRIMARY, Edge.TOP, Alignment.END)),
        ("member-b", Placement(OutputRole.SECONDARY, Edge.LEFT, Alignment.CENTER)),
        ("duplicate-a", Placement(OutputRole.SECONDARY, Edge.RIGHT, Alignment.JUSTIFY)),
        ("root", Placement(OutputRole.PRIMARY, Edge.BOTTOM, Alignment.CENTER)),
        ("member-a", Placement(OutputRole.SECONDARY, Edge.TOP, Alignment.START)),
        ("member-b", Placement(OutputRole.PRIMARY, Edge.LEFT, Alignment.END)),
        ("duplicate-a", Placement(OutputRole.PRIMARY, Edge.RIGHT, Alignment.CENTER)),
    )
    for index, (target, placement) in enumerate(coverage):
        move(target, placement)
        if index in (0, 2, 4, 6):
            edit_cycle(target)
    for _ in range(6):
        generator, target_index = generator.bounded(len(live))
        target = live[target_index]
        generator, placement = choose_safe_placement(generator, placements, target)
        move(target, placement)

    append(
        OperationKind.DUPLICATE,
        source="member-b",
        result="duplicate-b",
        checkpoint=False,
    )
    live.append("duplicate-b")
    generator, placement = choose_safe_placement(generator, placements, "duplicate-b")
    move("duplicate-b", placement)

    append(OperationKind.REMOVE, target="member-a")
    live.remove("member-a")
    placements.pop("member-a")
    append(OperationKind.RESTART, affected=tuple(sorted(live)))

    append(
        OperationKind.CREATE_LINKED,
        source="member-b",
        result="member-c",
        placement=Placement(OutputRole.PRIMARY, Edge.TOP, Alignment.START),
        checkpoint=False,
    )
    live.append("member-c")
    generator, placement = choose_safe_placement(generator, placements, "member-c")
    move("member-c", placement)

    for index in range(14):
        generator, target_index = generator.bounded(len(live))
        target = live[target_index]
        generator, placement = choose_safe_placement(generator, placements, target)
        move(target, placement)
        if index in (0, 5, 10):
            edit_cycle(target)

    latest_intent_target = "root"
    latest_intent_origin = placements[latest_intent_target]
    generator, latest_intent_away = choose_safe_placement(
        generator, placements, latest_intent_target
    )
    latest_intent_first_seq = len(operations) + 1
    move(
        latest_intent_target,
        latest_intent_away,
        checkpoint=False,
    )
    move(
        latest_intent_target,
        latest_intent_origin,
        checkpoint=True,
    )
    latest_intent_final_seq = len(operations)

    burst_target = "root"
    for index in range(3):
        generator, placement = choose_safe_placement(
            generator, placements, burst_target
        )
        move(burst_target, placement, checkpoint=index == 2)

    linked_group = ("root", "member-b", "member-c")
    append(
        OperationKind.RELOAD,
        target="root",
        affected=linked_group,
    )
    append(OperationKind.RESTART, affected=tuple(sorted(live)))

    plan: dict[str, Any] = {
        "format": PLAN_FORMAT,
        "version": FORMAT_VERSION,
        "dockSystemSchema": SCHEMA_VERSION,
        "generator": GENERATOR,
        "seed": str(seed),
        "initial": {
            "handle": "root",
            "placement": Placement(
                OutputRole.PRIMARY, Edge.BOTTOM, Alignment.JUSTIFY
            ).to_json(),
        },
        "latestIntentProbe": {
            "target": latest_intent_target,
            "firstSeq": latest_intent_first_seq,
            "finalSeq": latest_intent_final_seq,
        },
        "operations": [operation.to_json() for operation in operations],
    }
    plan["planSha256"] = operation_payload_digest(plan)
    if validate:
        validate_plan(plan, verify_generator=False)
    return plan


def parse_placement(value: Any, label: str) -> Placement:
    payload = require_object(value, label)
    require_keys(
        payload,
        ("output", "edge", "edgeValue", "alignment", "alignmentValue"),
        label,
    )
    try:
        output = OutputRole(require_string(payload["output"], f"{label}.output"))
        edge = Edge(require_int(payload["edgeValue"], f"{label}.edgeValue"))
        alignment = Alignment(
            require_int(payload["alignmentValue"], f"{label}.alignmentValue")
        )
    except ValueError as error:
        fail(f"{label} has an unsupported enum value: {error}")
    if payload["edge"] != edge.label:
        fail(f"{label} edge label disagrees with its value")
    if payload["alignment"] != alignment.name.lower():
        fail(f"{label} alignment label disagrees with its value")
    return Placement(output, edge, alignment)


def parse_operation(value: Any, expected_seq: int) -> Operation:
    step = require_object(value, f"operation {expected_seq}")
    require_keys(step, ("seq", "checkpoint", "operation"), f"operation {expected_seq}")
    if require_int(step["seq"], "operation.seq", 1) != expected_seq:
        fail(f"operation sequence is not contiguous at {expected_seq}")
    if not isinstance(step["checkpoint"], bool):
        fail(f"operation {expected_seq} checkpoint must be boolean")
    payload = require_object(step["operation"], f"operation {expected_seq}.operation")
    require_keys(payload, ("kind",), f"operation {expected_seq}.operation")
    try:
        kind = OperationKind(require_string(payload["kind"], "operation.kind"))
    except ValueError:
        fail(f"operation {expected_seq} has unknown kind {payload['kind']!r}")
    target = payload.get("target")
    source = payload.get("source")
    result = payload.get("result")
    for label, item in (("target", target), ("source", source), ("result", result)):
        if item is not None:
            require_string(item, f"operation {expected_seq}.{label}")
    placement = (
        parse_placement(payload["placement"], f"operation {expected_seq}.placement")
        if "placement" in payload
        else None
    )
    layout = None
    if "layout" in payload:
        try:
            layout = LayoutRole(
                require_string(
                    payload["layout"],
                    f"operation {expected_seq}.layout",
                )
            )
        except ValueError:
            fail(f"operation {expected_seq}.layout is not a known layout role")
    affected_raw = payload.get("affected", [])
    if not isinstance(affected_raw, list) or any(
        not isinstance(item, str) or not item for item in affected_raw
    ):
        fail(f"operation {expected_seq}.affected must be a string array")
    operation = Operation(
        expected_seq,
        kind,
        target,
        source,
        result,
        placement,
        layout,
        step["checkpoint"],
        tuple(affected_raw),
    )
    requirements = {
        OperationKind.MOVE: (target is not None and placement is not None),
        OperationKind.MOVE_LAYOUT: (target is not None and layout is not None),
        OperationKind.CREATE_LINKED: (
            source is not None and result is not None and placement is not None
        ),
        OperationKind.DUPLICATE: (source is not None and result is not None),
        OperationKind.BEGIN_EDIT: target is not None,
        OperationKind.CONFIGURE_ON: target is not None,
        OperationKind.CONFIGURE_OFF: target is not None,
        OperationKind.END_EDIT: target is not None,
        OperationKind.REMOVE: target is not None,
        OperationKind.RESTART: bool(operation.affected),
        OperationKind.RELOAD: target is not None and bool(operation.affected),
    }
    if not requirements[kind]:
        fail(f"operation {expected_seq} is missing fields required by {kind.value}")
    return operation


def apply_operation(state: ModelState, operation: Operation) -> ModelState:
    views = state.by_handle()

    def require_live(handle: str | None, role: str) -> ExpectedView:
        if handle is None or handle not in views:
            fail(f"operation {operation.seq} references non-live {role} {handle!r}")
        return views[handle]

    if operation.kind is OperationKind.MOVE:
        current = require_live(operation.target, "target")
        assert operation.placement is not None
        if any(
            other.handle != current.handle
            and other.placement is not None
            and placements_overlap(operation.placement, other.placement)
            for other in views.values()
        ):
            fail(f"operation {operation.seq} introduces a stable-span overlap")
        views[current.handle] = replace(
            current,
            placement=operation.placement,
            follows_primary=False,
        )
    elif operation.kind is OperationKind.MOVE_LAYOUT:
        current = require_live(operation.target, "target")
        if current.relationship != "independent":
            fail(f"operation {operation.seq} moves a linked relationship across layouts")
        if operation.layout is current.layout:
            fail(f"operation {operation.seq} does not change layout ownership")
        views[current.handle] = replace(current, layout=operation.layout)
    elif operation.kind is OperationKind.CREATE_LINKED:
        source = require_live(operation.source, "source")
        if operation.result in views or operation.result in state.destroyed:
            fail(f"operation {operation.seq} reuses handle {operation.result}")
        root = source.root or source.handle
        views[operation.result or ""] = ExpectedView(
            operation.result or "",
            "linkedMember",
            root,
            None,
            source.layout,
            False,
        )
        root_view = require_live(root, "relationship root")
        views[root] = replace(root_view, relationship="linkedRoot")
    elif operation.kind is OperationKind.DUPLICATE:
        source = require_live(operation.source, "source")
        if operation.result in views or operation.result in state.destroyed:
            fail(f"operation {operation.seq} reuses handle {operation.result}")
        views[operation.result or ""] = ExpectedView(
            operation.result or "",
            "independent",
            None,
            None,
            source.layout,
            source.follows_primary,
        )
    elif operation.kind is OperationKind.BEGIN_EDIT:
        require_live(operation.target, "target")
        if state.editing is not None:
            fail(f"operation {operation.seq} enters edit while {state.editing} owns it")
        return replace(
            state,
            editing=operation.target,
            config_owner=operation.target,
        )
    elif operation.kind is OperationKind.CONFIGURE_ON:
        require_live(operation.target, "target")
        if state.editing != operation.target or state.configuring:
            fail(f"operation {operation.seq} cannot enter rearrange mode")
        return replace(state, configuring=True)
    elif operation.kind is OperationKind.CONFIGURE_OFF:
        require_live(operation.target, "target")
        if state.editing != operation.target or not state.configuring:
            fail(f"operation {operation.seq} cannot leave rearrange mode")
        return replace(state, configuring=False)
    elif operation.kind is OperationKind.END_EDIT:
        require_live(operation.target, "target")
        if state.editing != operation.target or state.configuring:
            fail(f"operation {operation.seq} cannot leave edit mode")
        return replace(state, editing=None)
    elif operation.kind is OperationKind.REMOVE:
        removed = require_live(operation.target, "target")
        if removed.relationship != "linkedMember":
            fail(f"operation {operation.seq} removes a non-member")
        del views[removed.handle]
        destroyed = tuple(sorted((*state.destroyed, removed.handle)))
        return replace(
            state,
            views=tuple(sorted(views.values(), key=lambda item: item.handle)),
            config_owner=(
                None
                if state.config_owner == removed.handle
                else state.config_owner
            ),
            destroyed=destroyed,
        )
    elif operation.kind is OperationKind.RELOAD:
        target = require_live(operation.target, "target")
        root = target.root or target.handle
        expected = tuple(
            sorted(
                view.handle
                for view in views.values()
                if view.handle == root or view.root == root
            )
        )
        if tuple(sorted(operation.affected)) != expected:
            fail(f"operation {operation.seq} reload affected set is not its linked group")
        if state.config_owner in operation.affected:
            return replace(state, config_owner=None)
    elif operation.kind is OperationKind.RESTART:
        if tuple(sorted(operation.affected)) != tuple(sorted(views)):
            fail(f"operation {operation.seq} restart affected set is not every live view")
        return replace(state, config_owner=None)

    return replace(
        state,
        views=tuple(sorted(views.values(), key=lambda item: item.handle)),
    )


def state_through(plan: dict[str, Any], through: int) -> ModelState:
    validate_plan(plan, recurse=False, verify_generator=False)
    if through < 0 or through > len(plan["operations"]):
        fail(
            f"checkpoint sequence {through} is outside "
            f"0..{len(plan['operations'])}"
        )
    initial = parse_placement(plan["initial"]["placement"], "initial.placement")
    state = ModelState(
        (
            ExpectedView(
                "root",
                "independent",
                None,
                initial,
                LayoutRole.ORIGIN,
                True,
            ),
        )
    )
    for expected_seq, raw in enumerate(plan["operations"], 1):
        if expected_seq > through:
            break
        state = apply_operation(state, parse_operation(raw, expected_seq))
    return state


def validate_plan(
    plan_value: Any,
    *,
    recurse: bool = True,
    verify_generator: bool = True,
) -> dict[str, Any]:
    plan = require_object(plan_value, "plan")
    require_keys(
        plan,
        (
            "format",
            "version",
            "dockSystemSchema",
            "generator",
            "seed",
            "initial",
            "latestIntentProbe",
            "operations",
            "planSha256",
        ),
        "plan",
    )
    if (
        plan["format"] != PLAN_FORMAT
        or plan["version"] != FORMAT_VERSION
        or plan["dockSystemSchema"] != SCHEMA_VERSION
        or plan["generator"] != GENERATOR
    ):
        fail("plan format, version, schema, or generator is unsupported")
    seed = require_decimal(plan["seed"], "plan.seed")
    if seed > 0xFFFFFFFFFFFFFFFF:
        fail("plan.seed is outside uint64")
    initial = require_object(plan["initial"], "plan.initial")
    require_keys(initial, ("handle", "placement"), "plan.initial")
    if initial["handle"] != "root":
        fail("plan initial handle must be root")
    parse_placement(initial["placement"], "plan.initial.placement")
    latest_intent_probe = require_object(
        plan["latestIntentProbe"],
        "plan.latestIntentProbe",
    )
    require_keys(
        latest_intent_probe,
        ("target", "firstSeq", "finalSeq"),
        "plan.latestIntentProbe",
    )
    require_string(
        latest_intent_probe["target"],
        "plan.latestIntentProbe.target",
    )
    first_probe_seq = require_int(
        latest_intent_probe["firstSeq"],
        "plan.latestIntentProbe.firstSeq",
        1,
    )
    final_probe_seq = require_int(
        latest_intent_probe["finalSeq"],
        "plan.latestIntentProbe.finalSeq",
        1,
    )
    if final_probe_seq != first_probe_seq + 1:
        fail("latest-intent probe must contain exactly two consecutive requests")
    operations = plan["operations"]
    if not isinstance(operations, list) or not operations:
        fail("plan operations must be a nonempty array")
    if operation_payload_digest(plan) != plan["planSha256"]:
        fail("planSha256 does not match the canonical plan")
    if recurse:
        final_state = state_through(plan, len(operations))
        kinds = [parse_operation(raw, index).kind for index, raw in enumerate(operations, 1)]
        placements = [
            parse_operation(raw, index).placement
            for index, raw in enumerate(operations, 1)
            if parse_operation(raw, index).kind is OperationKind.MOVE
        ]
        edit_rounds = kinds.count(OperationKind.BEGIN_EDIT)
        layout_moves = [
            parse_operation(raw, index).layout
            for index, raw in enumerate(operations, 1)
            if parse_operation(raw, index).kind is OperationKind.MOVE_LAYOUT
        ]
        if (
            kinds.count(OperationKind.CREATE_LINKED) != 3
            or kinds.count(OperationKind.DUPLICATE) != 2
            or kinds.count(OperationKind.REMOVE) != 1
            or kinds.count(OperationKind.RELOAD) != 1
            or kinds.count(OperationKind.RESTART) != 2
            or layout_moves
            != [LayoutRole.DESTINATION, LayoutRole.ORIGIN]
            or edit_rounds != 7
            or kinds.count(OperationKind.END_EDIT) != edit_rounds
            or kinds.count(OperationKind.CONFIGURE_ON) != edit_rounds
            or kinds.count(OperationKind.CONFIGURE_OFF) != edit_rounds
        ):
            fail("plan does not structurally cover the FP-4C lifecycle")
        assert all(placement is not None for placement in placements)
        if (
            {placement.output for placement in placements if placement}
            != set(OutputRole)
            or {placement.edge for placement in placements if placement} != set(Edge)
            or {placement.alignment for placement in placements if placement}
            != set(Alignment)
        ):
            fail("plan does not cover both outputs, all edges, and all alignments")
        if final_state.editing is not None or final_state.configuring:
            fail("plan ends inside edit mode")
    if verify_generator and plan != generate_plan(seed, validate=False):
        fail("plan does not match the canonical splitmix64-v1 output for its seed")
    return plan


VIEW_REQUIRED_KEYS = (
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
    "screenEdgeMargin",
    "presentedScreenEdgeGap",
    "screenEdgeBackend",
    "screenEdgeArmed",
    "screenEdgeRegistered",
    "compositorScreenEdgeSupported",
    "visibilityContainsMouse",
    "windowGeometry",
    "absoluteGeometry",
    "localGeometry",
    "screenGeometry",
    "surfaceGeometry",
    "canvasGeometry",
    "effectsRect",
    "appletsLayoutGeometry",
    "maskRect",
    "inputMask",
    "appliedInputMask",
    "floatingDamageMaskPending",
    "floatingDamageMaskGeneration",
    "enabledBorders",
    "shadowEnabledBorders",
    "shadowPaddingOffsets",
    "floatingAppletPopupsPreferred",
    "floatingAnchorRevision",
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
    "floatingGapConfigured",
    "floatingPanelConfigured",
    "floatingPanelEligible",
    "attachOnWindowTouchConfigured",
    "attachmentWaitsForPointerExitConfigured",
    "pointerInsideView",
    "attachmentDeferredByPointer",
    "dockGapHideRequested",
    "touchingWindowCount",
    "windowTouchGeometryRoleType",
    "transitionTarget",
    "transitionProgress",
    "transitionAnimationDuration",
    "transitionPhase",
    "transitionDirection",
    "transitionRunning",
    "transitionGeometryPresent",
    "transitionGeometryRevision",
    "stableCanvasGeometry",
    "attachedPresentationGeometry",
    "floatedPresentationGeometry",
    "currentVisibleGeometry",
    "computedPaintMaskGeometry",
    "computedInputBridgeGeometry",
    "contentTranslation",
    "stableTriggerGeometry",
    "stableAppletMeasurementBounds",
    "stablePrimaryAxisStart",
    "stablePrimaryAxisLength",
    "stableLayerShellMargin",
    "surfaceGeometryPublicationRevision",
    "layerShellConfigureRequestRevision",
    "requestedReservationDepth",
    "visibilityMode",
    "isHidden",
    "inStartup",
    "isOffScreen",
    "inRelocationAnimation",
    "inRelocationShowing",
    "geometrySettled",
    "relocationGeneration",
    "appliedRelocationGeneration",
    "inDelete",
    "inReadyState",
    "editMode",
    "effectiveConfigureAppletsMode",
    "settingsWindowShown",
    "objects",
)

GROUP_REQUIRED_KEYS = (
    "outputId",
    "edge",
    "generation",
    "publishedDepth",
    "contributorDockIds",
    "memberCount",
    "geometry",
    "windowGeometry",
    "layerShellPresent",
    "layerShellAnchors",
    "layerShellMargins",
    "layerShellExclusiveEdge",
    "layerShellExclusiveZone",
    "publisher",
)

VISUAL_WINDOW_REQUIRED_KEYS = (
    "caption",
    "geometry",
    "id",
    "output",
)

OWNED_OBJECTS = (
    "view",
    "containment",
    "configuration",
    "layoutController",
    "geometryController",
    "editController",
    "transitionController",
    "windowTouchTracker",
)

VIEW_STRING_KEYS = (
    "runtimeViewId",
    "relationship",
    "screensGroup",
    "layout",
    "screen",
    "type",
    "edge",
    "orientation",
    "alignment",
    "visibilityMode",
    "screenEdgeBackend",
    "transitionDirection",
    "transitionPhase",
    "transitionTarget",
    "windowTouchGeometryRoleType",
)

VIEW_OPTIONAL_STRING_KEYS = (
    "layerShellExclusiveEdge",
    "reservationEdge",
    "reservationLayerShellExclusiveEdge",
)

VIEW_DECIMAL_KEYS = (
    "runtimeViewId",
    "relocationGeneration",
    "appliedRelocationGeneration",
    "floatingDamageMaskGeneration",
    "floatingAnchorRevision",
    "transitionGeometryRevision",
    "surfaceGeometryPublicationRevision",
    "layerShellConfigureRequestRevision",
)

VIEW_BOOL_KEYS = (
    "onPrimary",
    "isHidden",
    "inStartup",
    "isOffScreen",
    "inRelocationAnimation",
    "inRelocationShowing",
    "geometrySettled",
    "inDelete",
    "inReadyState",
    "editMode",
    "effectiveConfigureAppletsMode",
    "settingsWindowShown",
    "layerShellPresent",
    "reservationSurfacePresent",
    "floatingDamageMaskPending",
    "floatingAppletPopupsPreferred",
    "attachOnWindowTouchConfigured",
    "attachmentDeferredByPointer",
    "attachmentWaitsForPointerExitConfigured",
    "dockGapHideRequested",
    "floatingGapConfigured",
    "floatingPanelConfigured",
    "floatingPanelEligible",
    "pointerInsideView",
    "screenEdgeArmed",
    "screenEdgeRegistered",
    "compositorScreenEdgeSupported",
    "visibilityContainsMouse",
    "transitionGeometryPresent",
    "transitionRunning",
)

VIEW_NUMBER_KEYS = (
    "screenId",
    "maximumLengthRatio",
    "offsetRatio",
    "normalThickness",
    "maximumNormalThickness",
    "screenEdgeMargin",
    "strutsThickness",
    "stableLayerShellMargin",
    "touchingWindowCount",
    "transitionProgress",
    "transitionAnimationDuration",
)

VIEW_OPTIONAL_NUMBER_KEYS = (
    "configuredIconSize",
    "effectiveIconSize",
    "availablePrimaryLength",
    "presentedScreenEdgeGap",
    "layerShellExclusiveZone",
    "reservationContributionDepth",
    "reservationGroupMemberCount",
    "reservationOutputId",
    "reservationPublishedDepth",
    "reservationLayerShellExclusiveZone",
    "requestedReservationDepth",
    "stablePrimaryAxisLength",
    "stablePrimaryAxisStart",
)

VIEW_RECT_KEYS = (
    "windowGeometry",
    "absoluteGeometry",
    "localGeometry",
    "screenGeometry",
    "surfaceGeometry",
    "canvasGeometry",
    "effectsRect",
    "appletsLayoutGeometry",
    "maskRect",
    "inputMask",
    "appliedInputMask",
    "publishedStruts",
)

VIEW_OPTIONAL_RECT_KEYS = (
    "reservationGeometry",
    "reservationWindowGeometry",
    "stableCanvasGeometry",
    "attachedPresentationGeometry",
    "floatedPresentationGeometry",
    "currentVisibleGeometry",
    "computedPaintMaskGeometry",
    "computedInputBridgeGeometry",
    "stableTriggerGeometry",
    "stableAppletMeasurementBounds",
)


def parse_snapshot(value: Any) -> dict[str, Any]:
    snapshot = require_object(value, "dockSystemData")
    require_keys(
        snapshot,
        (
            "schemaVersion",
            "snapshotSequence",
            "globalConfigureAppletsMode",
            "stacking",
            "reservationStateGeneration",
            "reservationGroups",
            "views",
        ),
        "dockSystemData",
    )
    if snapshot["schemaVersion"] != SCHEMA_VERSION:
        fail(f"expected dockSystemData schema {SCHEMA_VERSION}")
    if require_decimal(snapshot["snapshotSequence"], "snapshotSequence") == 0:
        fail("snapshotSequence must be positive")
    reservation_state_generation = require_decimal(
        snapshot["reservationStateGeneration"], "reservationStateGeneration"
    )
    require_bool(
        snapshot["globalConfigureAppletsMode"], "globalConfigureAppletsMode"
    )
    if snapshot["stacking"] != {
        "available": False,
        "reason": STACKING_REASON,
    }:
        fail("unsupported stacking contract changed")
    if not isinstance(snapshot["views"], list) or not isinstance(
        snapshot["reservationGroups"], list
    ):
        fail("views and reservationGroups must be arrays")
    view_ids: list[int] = []
    for index, view_value in enumerate(snapshot["views"]):
        view = require_object(view_value, f"views[{index}]")
        require_keys(view, VIEW_REQUIRED_KEYS, f"views[{index}]")
        view_ids.append(
            require_int(view["persistentDockId"], f"views[{index}].persistentDockId", 1)
        )
        require_int(view["logicalDockId"], f"views[{index}].logicalDockId", 1)
        if view["originalDockId"] is not None:
            require_int(
                view["originalDockId"], f"views[{index}].originalDockId", 1
            )
        if view["linkPlacement"] is not None:
            require_string(
                view["linkPlacement"], f"views[{index}].linkPlacement"
            )
        for key in VIEW_STRING_KEYS:
            require_string(
                view[key],
                f"views[{index}].{key}",
                nonempty=key != "windowTouchGeometryRoleType",
            )
        for key in VIEW_OPTIONAL_STRING_KEYS:
            if view[key] is not None:
                require_string(view[key], f"views[{index}].{key}")
        for key in VIEW_DECIMAL_KEYS:
            require_decimal(view[key], f"views[{index}].{key}")
        if view["reservationGroupGeneration"] is not None:
            group_generation = require_decimal(
                view["reservationGroupGeneration"],
                f"views[{index}].reservationGroupGeneration",
            )
            if group_generation == 0 or group_generation > reservation_state_generation:
                fail(
                    f"views[{index}].reservationGroupGeneration is outside "
                    "the committed reservation generation"
                )
        for key in VIEW_BOOL_KEYS:
            require_bool(view[key], f"views[{index}].{key}")
        compositor_backend = view["screenEdgeBackend"] == "kwinAutoHide"
        client_backend = view["screenEdgeBackend"] == "clientGhost"
        revealing_mode = view["visibilityMode"] in (
            "autoHide",
            "dodgeActive",
            "dodgeMaximized",
            "dodgeAllWindows",
        )
        client_mode = revealing_mode or view["visibilityMode"] == "windowsCanCover"
        if view["screenEdgeBackend"] not in (
            "none",
            "clientGhost",
            "kwinAutoHide",
        ):
            fail(f"views[{index}].screenEdgeBackend is invalid")
        if compositor_backend and (
            not view["compositorScreenEdgeSupported"] or not revealing_mode
        ):
            fail(f"views[{index}] compositor backend has no supported reveal mode")
        if client_backend and (
            not client_mode
            or (revealing_mode and view["compositorScreenEdgeSupported"])
        ):
            fail(f"views[{index}] client backend conflicts with edge ownership")
        if view["screenEdgeRegistered"] and not compositor_backend:
            fail(f"views[{index}] non-compositor backend owns a registration")
        if view["screenEdgeArmed"] and view["screenEdgeBackend"] == "none":
            fail(f"views[{index}] missing backend owns an armed edge")
        if compositor_backend and view["screenEdgeArmed"] and (
            not view["isHidden"] or view["visibilityContainsMouse"]
            or (view["inReadyState"] and not view["screenEdgeRegistered"])
        ):
            fail(f"views[{index}] armed screen edge has incompatible visibility state")
        if client_backend and revealing_mode and view["screenEdgeArmed"] and (
            not view["isHidden"] or view["visibilityContainsMouse"]
        ):
            fail(f"views[{index}] armed client edge has incompatible visibility state")
        for key in VIEW_NUMBER_KEYS:
            require_number(view[key], f"views[{index}].{key}")
        for key in VIEW_OPTIONAL_NUMBER_KEYS:
            if view[key] is not None:
                require_number(view[key], f"views[{index}].{key}")
        require_id_array(view["linkedDockIds"], f"views[{index}].linkedDockIds")
        require_id_array(
            view["reservationContributorDockIds"],
            f"views[{index}].reservationContributorDockIds",
        )
        for key in VIEW_RECT_KEYS:
            require_number_array(view[key], f"views[{index}].{key}", 4)
        for key in VIEW_OPTIONAL_RECT_KEYS:
            if view[key] is not None:
                require_number_array(view[key], f"views[{index}].{key}", 4)
        if view["contentTranslation"] is not None:
            require_number_array(
                view["contentTranslation"],
                f"views[{index}].contentTranslation",
                2,
            )
        require_string_array(
            view["enabledBorders"], f"views[{index}].enabledBorders"
        )
        if view["shadowEnabledBorders"] is not None:
            require_string_array(
                view["shadowEnabledBorders"],
                f"views[{index}].shadowEnabledBorders",
            )
        if view["shadowPaddingOffsets"] is not None:
            require_number_array(
                view["shadowPaddingOffsets"],
                f"views[{index}].shadowPaddingOffsets",
                4,
            )
        require_string_array(
            view["layerShellAnchors"], f"views[{index}].layerShellAnchors"
        )
        require_number_array(
            view["layerShellMargins"], f"views[{index}].layerShellMargins", 4
        )
        require_string_array(
            view["reservationLayerShellAnchors"],
            f"views[{index}].reservationLayerShellAnchors",
        )
        require_number_array(
            view["reservationLayerShellMargins"],
            f"views[{index}].reservationLayerShellMargins",
            4,
        )
        objects = require_object(view["objects"], f"views[{index}].objects")
        require_keys(
            objects,
            (*OWNED_OBJECTS, "layout", "configWindow", "reservationPublisher"),
            f"views[{index}].objects",
        )
        for key in (*OWNED_OBJECTS, "layout"):
            require_string(
                objects[key],
                f"views[{index}].objects.{key}",
                nonempty=False,
            )
        for key in ("configWindow", "reservationPublisher"):
            if objects[key] is not None:
                require_string(objects[key], f"views[{index}].objects.{key}")
    if len(view_ids) != len(set(view_ids)):
        fail("dockSystemData contains duplicate persistent dock ids")
    for index, group_value in enumerate(snapshot["reservationGroups"]):
        group = require_object(group_value, f"reservationGroups[{index}]")
        require_keys(group, GROUP_REQUIRED_KEYS, f"reservationGroups[{index}]")
        require_int(group["outputId"], f"reservationGroups[{index}].outputId", 0)
        require_string(group["edge"], f"reservationGroups[{index}].edge")
        generation = require_decimal(
            group["generation"], f"reservationGroups[{index}].generation"
        )
        if generation == 0 or generation > reservation_state_generation:
            fail(
                f"reservationGroups[{index}].generation is outside "
                "the committed reservation generation"
            )
        require_int(
            group["publishedDepth"],
            f"reservationGroups[{index}].publishedDepth",
            1,
        )
        require_id_array(
            group["contributorDockIds"],
            f"reservationGroups[{index}].contributorDockIds",
        )
        require_int(
            group["memberCount"], f"reservationGroups[{index}].memberCount", 1
        )
        require_number_array(
            group["geometry"], f"reservationGroups[{index}].geometry", 4
        )
        require_number_array(
            group["windowGeometry"],
            f"reservationGroups[{index}].windowGeometry",
            4,
        )
        require_bool(
            group["layerShellPresent"],
            f"reservationGroups[{index}].layerShellPresent",
        )
        require_string_array(
            group["layerShellAnchors"],
            f"reservationGroups[{index}].layerShellAnchors",
        )
        require_number_array(
            group["layerShellMargins"],
            f"reservationGroups[{index}].layerShellMargins",
            4,
        )
        require_string(
            group["layerShellExclusiveEdge"],
            f"reservationGroups[{index}].layerShellExclusiveEdge",
        )
        require_int(
            group["layerShellExclusiveZone"],
            f"reservationGroups[{index}].layerShellExclusiveZone",
            1,
        )
        require_string(
            group["publisher"], f"reservationGroups[{index}].publisher"
        )
    return snapshot


def parse_bindings(value: Any) -> dict[str, int]:
    bindings = require_object(value, "bindings")
    parsed: dict[str, int] = {}
    for handle, persistent_id in bindings.items():
        require_string(handle, "binding handle")
        parsed[handle] = require_int(persistent_id, f"binding {handle}", 1)
    if len(parsed.values()) != len(set(parsed.values())):
        fail("bindings contain duplicate persistent ids")
    return parsed


def parse_outputs(value: Any) -> dict[str, OutputSnapshot]:
    outputs = require_object(value, "outputs")
    if set(outputs) != {role.value for role in OutputRole}:
        fail("outputs must contain exactly primary and secondary")

    parsed: dict[str, OutputSnapshot] = {}
    for role in OutputRole:
        label = f"output {role.value}"
        record = require_object(outputs[role.value], label)
        if set(record) != {"id", "name", "geometry"}:
            fail(f"{label} must contain exactly id, name, and geometry")
        raw_geometry = require_number_array(
            record["geometry"], f"{label}.geometry", 4
        )
        if any(
            not isinstance(component, int) or isinstance(component, bool)
            for component in raw_geometry
        ):
            fail(f"{label}.geometry must contain integers")
        geometry = tuple(raw_geometry)
        if geometry[2] <= 0 or geometry[3] <= 0:
            fail(f"{label}.geometry must have positive dimensions")
        parsed[role.value] = OutputSnapshot(
            require_int(record["id"], f"{label}.id", 0),
            require_string(record["name"], f"{label}.name"),
            geometry,
        )

    if len({output.identity for output in parsed.values()}) != len(parsed):
        fail("primary and secondary outputs must have distinct identities")
    if len({output.name for output in parsed.values()}) != len(parsed):
        fail("primary and secondary outputs must have distinct names")
    return parsed


def parse_layouts(value: Any) -> dict[str, str]:
    layouts = require_object(value, "layouts")
    if set(layouts) != {role.value for role in LayoutRole}:
        fail("layouts must contain exactly origin and destination")
    parsed = {
        role.value: require_string(layouts[role.value], f"layout {role.value}")
        for role in LayoutRole
    }
    if len(set(parsed.values())) != len(parsed):
        fail("origin and destination layouts must have distinct names")
    return parsed


def parse_view_move_lifecycle(value: Any, label: str) -> dict[str, Any]:
    lifecycle = require_object(value, label)
    expected_keys = {
        "schemaVersion",
        "transactions",
        *VIEW_MOVE_LIFECYCLE_GENERATIONS,
    }
    if set(lifecycle) != expected_keys:
        fail(f"{label} has missing or surplus fields")
    if (
        require_int(lifecycle["schemaVersion"], f"{label}.schemaVersion")
        != VIEW_MOVE_LIFECYCLE_SCHEMA_VERSION
    ):
        fail(f"{label} schema changed")
    generations = {
        key: require_decimal(lifecycle[key], f"{label}.{key}")
        for key in VIEW_MOVE_LIFECYCLE_GENERATIONS
    }
    transactions = require_array(
        lifecycle["transactions"],
        f"{label}.transactions",
    )
    return {
        "schemaVersion": VIEW_MOVE_LIFECYCLE_SCHEMA_VERSION,
        **generations,
        "transactions": transactions,
    }


def assert_view_move_lifecycle(payload_value: Any) -> dict[str, Any]:
    payload = require_object(payload_value, "view move lifecycle input")
    require_keys(
        payload,
        ("step", "before", "after"),
        "view move lifecycle input",
    )
    step = require_object(payload["step"], "step")
    operation = parse_operation(
        step,
        require_int(step.get("seq"), "step.seq", 1),
    )
    before = parse_view_move_lifecycle(
        payload["before"],
        "view move lifecycle before",
    )
    after = parse_view_move_lifecycle(
        payload["after"],
        "view move lifecycle after",
    )
    if before["transactions"]:
        fail(
            f"operation {operation.seq} started with a pending durable move"
        )
    if after["transactions"]:
        fail(
            f"operation {operation.seq} retained a pending durable move"
        )

    if operation.kind is OperationKind.RESTART:
        if any(after[key] != 0 for key in VIEW_MOVE_LIFECYCLE_GENERATIONS):
            fail(
                f"operation {operation.seq} restarted with stale lifecycle generations"
            )
    else:
        expected_delta = (
            1
            if operation.kind is OperationKind.MOVE_LAYOUT
            else 0
        )
        for key in VIEW_MOVE_LIFECYCLE_GENERATIONS:
            observed_delta = after[key] - before[key]
            if observed_delta != expected_delta:
                fail(
                    f"operation {operation.seq} advanced {key} by "
                    f"{observed_delta}, expected {expected_delta}"
                )

    return {
        "ok": True,
        "seq": operation.seq,
        "kind": operation.kind.value,
        "generations": {
            key: after[key]
            for key in VIEW_MOVE_LIFECYCLE_GENERATIONS
        },
    }


def outputs_to_json(
    outputs: dict[str, OutputSnapshot],
) -> dict[str, dict[str, Any]]:
    return {
        role: {
            "id": output.identity,
            "name": output.name,
            "geometry": list(output.geometry),
        }
        for role, output in outputs.items()
    }


def edge_from_label(label: str) -> Edge:
    try:
        return Edge[label.upper()]
    except KeyError:
        fail(f"unknown edge {label}")


def output_by_identity(
    outputs: dict[str, OutputSnapshot],
) -> dict[int, OutputSnapshot]:
    return {output.identity: output for output in outputs.values()}


def expected_reservation_depths(
    views: dict[int, dict[str, Any]],
) -> dict[tuple[int, str], int]:
    depths: dict[tuple[int, str], int] = {}
    for persistent_id, view in views.items():
        depth = view["requestedReservationDepth"]
        if not isinstance(depth, int) or isinstance(depth, bool) or depth <= 0:
            fail(
                f"view {persistent_id} has no positive integral requested reservation depth"
            )
        key = (view["screenId"], view["edge"])
        depths[key] = max(depths.get(key, 0), depth)
    return depths


def expected_reservation_geometry(
    output: OutputSnapshot,
    edge: Edge,
    depth: int,
) -> list[int]:
    x, y, width, height = output.geometry
    return {
        Edge.TOP: [x, y, width, depth],
        Edge.BOTTOM: [x, y + height - depth, width, depth],
        Edge.LEFT: [x, y, depth, height],
        Edge.RIGHT: [x + width - depth, y, depth, height],
    }[edge]


def expected_view_strut_geometry(
    output: OutputSnapshot,
    edge: Edge,
    primary_start: int,
    primary_length: int,
    depth: int,
) -> list[int]:
    x, y, width, height = output.geometry
    return {
        Edge.TOP: [primary_start, y, primary_length, depth],
        Edge.BOTTOM: [
            primary_start,
            y + height - depth,
            primary_length,
            depth,
        ],
        Edge.LEFT: [x, primary_start, depth, primary_length],
        Edge.RIGHT: [
            x + width - depth,
            primary_start,
            depth,
            primary_length,
        ],
    }[edge]


def expected_reservation_window_geometry(
    output: OutputSnapshot,
    edge: Edge,
    depths: dict[tuple[int, str], int],
) -> list[int]:
    _, _, width, height = output.geometry
    if edge.orientation == "horizontal":
        return [0, 0, width, 1]
    top = depths.get((output.identity, Edge.TOP.label), 0)
    bottom = depths.get((output.identity, Edge.BOTTOM.label), 0)
    available_height = height - top - bottom
    if available_height <= 0:
        fail(
            f"output {output.identity} has no vertical reservation publisher span"
        )
    return [0, 0, 1, available_height]


def expected_reservation_anchors(edge: Edge) -> list[str]:
    return {
        Edge.TOP: ["top", "left", "right"],
        Edge.BOTTOM: ["bottom", "left", "right"],
        Edge.LEFT: ["top", "bottom", "left"],
        Edge.RIGHT: ["top", "bottom", "right"],
    }[edge]


def expected_global_reservation_window_geometry(
    output: OutputSnapshot,
    edge: Edge,
    depths: dict[tuple[int, str], int],
) -> list[int]:
    x, y, width, height = output.geometry
    if edge is Edge.TOP:
        return [x, y, width, 1]
    if edge is Edge.BOTTOM:
        return [x, y + height - 1, width, 1]
    top = depths.get((output.identity, Edge.TOP.label), 0)
    available_height = expected_reservation_window_geometry(
        output, edge, depths
    )[3]
    if edge is Edge.LEFT:
        return [x, y + top, 1, available_height]
    return [x + width - 1, y + top, 1, available_height]


def assert_visual_window_ownership(payload_value: Any) -> dict[str, Any]:
    payload = require_object(payload_value, "visual ownership input")
    require_keys(
        payload, ("snapshot", "outputs", "windows"), "visual ownership input"
    )
    snapshot = parse_snapshot(payload["snapshot"])
    outputs = parse_outputs(payload["outputs"])
    raw_windows = require_array(payload["windows"], "visual windows")

    windows: list[dict[str, Any]] = []
    window_ids: set[str] = set()
    for index, raw_window in enumerate(raw_windows):
        label = f"visual windows[{index}]"
        window = require_object(raw_window, label)
        if set(window) != set(VISUAL_WINDOW_REQUIRED_KEYS):
            fail(f"{label} has missing or surplus fields")
        caption = require_string(
            window["caption"], f"{label}.caption", nonempty=False
        )
        window_id = require_string(window["id"], f"{label}.id")
        output = require_string(window["output"], f"{label}.output")
        geometry = require_number_array(
            window["geometry"], f"{label}.geometry", 4
        )
        if any(
            not isinstance(component, int) or isinstance(component, bool)
            for component in geometry
        ):
            fail(f"{label}.geometry must contain integers")
        if geometry[2] <= 0 or geometry[3] <= 0:
            fail(f"{label}.geometry must have positive dimensions")
        if window_id in window_ids:
            fail(f"visual ownership contains duplicate window id {window_id}")
        windows.append({
            "caption": caption,
            "geometry": tuple(geometry),
            "id": window_id,
            "output": output,
        })
        window_ids.add(window_id)

    unmatched = list(windows)
    for view in snapshot["views"]:
        persistent_id = view["persistentDockId"]
        expected_geometry = tuple(
            int(round(component)) for component in view["stableCanvasGeometry"]
        )
        candidates = [
            window
            for window in unmatched
            if window["geometry"] == expected_geometry
            and window["output"] == view["screen"]
        ]
        if len(candidates) != 1:
            fail(
                f"view {persistent_id} expected one compositor canvas at "
                f"{expected_geometry} on {view['screen']}, got {len(candidates)}"
            )
        unmatched.remove(candidates[0])

    outputs_by_id = output_by_identity(outputs)
    views = view_map(snapshot)
    depths = expected_reservation_depths(views)
    for group in snapshot["reservationGroups"]:
        output_id = group["outputId"]
        edge = edge_from_label(group["edge"])
        output = outputs_by_id.get(output_id)
        if output is None:
            fail(
                f"reservation group {output_id}/{group['edge']} has no independent output"
            )
        expected_geometry = expected_global_reservation_window_geometry(
            output, edge, depths
        )
        candidates = [
            window
            for window in unmatched
            if list(window["geometry"]) == expected_geometry
            and window["output"] == output.name
        ]
        if len(candidates) != 1:
            fail(
                f"reservation group {output_id}/{group['edge']} compositor "
                f"publisher match count is {len(candidates)}"
            )
        unmatched.remove(candidates[0])

    if unmatched:
        fail(
            "layer-3 Latte QWindows do not equal the live view canvases "
            f"and reservation publishers: surplus={unmatched}"
        )
    return {"ok": True, "windowCount": len(raw_windows)}


def view_map(snapshot: dict[str, Any]) -> dict[int, dict[str, Any]]:
    return {view["persistentDockId"]: view for view in snapshot["views"]}


def assert_lineage(
    state: ModelState,
    bindings: dict[str, int],
    views: dict[int, dict[str, Any]],
) -> None:
    expected_ids = {bindings[view.handle] for view in state.views}
    if set(views) != expected_ids:
        fail(f"expected live ids {sorted(expected_ids)}, got {sorted(views)}")
    linked_by_root: dict[str, list[str]] = {}
    for expected in state.views:
        if expected.root is not None:
            linked_by_root.setdefault(expected.root, []).append(expected.handle)
    for expected in state.views:
        persistent_id = bindings[expected.handle]
        view = views[persistent_id]
        if expected.relationship == "linkedMember":
            assert expected.root is not None
            root_id = bindings[expected.root]
            wanted = (
                "linkedMember",
                root_id,
                root_id,
                "explicitTarget",
                "single",
                [],
            )
        else:
            relationship = (
                "linkedRoot" if linked_by_root.get(expected.handle) else "independent"
            )
            wanted = (
                relationship,
                persistent_id,
                None,
                None,
                "single",
                sorted(bindings[item] for item in linked_by_root.get(expected.handle, [])),
            )
        actual = (
            view["relationship"],
            view["logicalDockId"],
            view["originalDockId"],
            view["linkPlacement"],
            view["screensGroup"],
            view["linkedDockIds"],
        )
        if actual != wanted:
            fail(f"view {expected.handle} lineage is {actual}, expected {wanted}")


def assert_placement(
    state: ModelState,
    bindings: dict[str, int],
    outputs: dict[str, OutputSnapshot],
    views: dict[int, dict[str, Any]],
) -> None:
    for expected in state.views:
        if expected.placement is None:
            continue
        view = views[bindings[expected.handle]]
        placement = expected.placement
        output = outputs[placement.output.value]
        actual = (
            view["screenId"],
            view["screen"],
            tuple(view["screenGeometry"]),
            view["edge"],
            view["orientation"],
            view["alignment"],
        )
        wanted = (
            output.identity,
            output.name,
            output.geometry,
            placement.edge.label,
            placement.edge.orientation,
            placement.alignment.label_for(placement.edge),
        )
        if actual != wanted:
            fail(f"view {expected.handle} placement is {actual}, expected {wanted}")
        if (
            expected.follows_primary is not None
            and view["onPrimary"] != expected.follows_primary
        ):
            fail(f"view {expected.handle} primary-output flag is stale")


def assert_stable_spans(views: dict[int, dict[str, Any]]) -> None:
    intervals: dict[tuple[int, str], list[tuple[int, int, int]]] = {}
    for persistent_id, view in views.items():
        start = view["stablePrimaryAxisStart"]
        length = view["stablePrimaryAxisLength"]
        if not isinstance(start, int) or isinstance(start, bool):
            fail(f"view {persistent_id} has no integral stable primary start")
        if not isinstance(length, int) or isinstance(length, bool) or length <= 0:
            fail(f"view {persistent_id} has no positive integral stable primary length")
        screen = view["screenGeometry"]
        horizontal = view["orientation"] == "horizontal"
        screen_start = screen[0] if horizontal else screen[1]
        screen_length = screen[2] if horizontal else screen[3]
        if start < screen_start or start + length > screen_start + screen_length:
            fail(f"view {persistent_id} stable primary span escaped its output")
        trigger = view["stableTriggerGeometry"]
        trigger_start = trigger[0] if horizontal else trigger[1]
        trigger_length = trigger[2] if horizontal else trigger[3]
        if (trigger_start, trigger_length) != (start, length):
            fail(f"view {persistent_id} trigger does not own its stable primary span")
        intervals.setdefault((view["screenId"], view["edge"]), []).append(
            (start, start + length, persistent_id)
        )
    for key, members in intervals.items():
        ordered = sorted(members)
        for left, right in zip(ordered, ordered[1:]):
            if left[1] > right[0]:
                fail(
                    f"output-edge {key} has overlapping stable spans "
                    f"for views {left[2]} and {right[2]}"
                )


def assert_runtime_ownership(
    state: ModelState,
    bindings: dict[str, int],
    layouts: dict[str, str],
    views: dict[int, dict[str, Any]],
) -> None:
    runtime_ids = [view["runtimeViewId"] for view in views.values()]
    if len(runtime_ids) != len(set(runtime_ids)):
        fail("runtimeViewId is shared")
    all_owned: list[str] = []
    layout_objects: dict[str, set[str]] = {}
    for persistent_id, view in views.items():
        objects = view["objects"]
        tokens = [objects[key] for key in OWNED_OBJECTS]
        if any(not isinstance(token, str) or not token for token in tokens):
            fail(f"view {persistent_id} has an absent owned runtime authority")
        if len(tokens) != len(set(tokens)):
            fail(f"view {persistent_id} aliases owned runtime authorities")
        all_owned.extend(tokens)
        layout_objects.setdefault(view["layout"], set()).add(objects["layout"])
    if len(all_owned) != len(set(all_owned)):
        fail("a per-view runtime authority is shared across views")
    for expected in state.views:
        persistent_id = bindings[expected.handle]
        view = views[persistent_id]
        expected_layout = layouts[expected.layout.value]
        if view["layout"] != expected_layout:
            fail(
                f"view {expected.handle} has layout {view['layout']!r}, "
                f"expected {expected_layout!r}"
            )
    if any(
        len(tokens) != 1
        or any(not isinstance(token, str) or not token for token in tokens)
        for tokens in layout_objects.values()
    ):
        fail("views in one persistent layout do not share exactly one layout authority")
    flattened_layout_objects = {
        next(iter(tokens)) for tokens in layout_objects.values()
    }
    if len(flattened_layout_objects) != len(layout_objects):
        fail("distinct persistent layouts share one runtime layout authority")


def assert_applet_geometry(view: dict[str, Any]) -> None:
    persistent_id = view["persistentDockId"]
    horizontal = view["orientation"] == "horizontal"

    def primary(rectangle: list[Any]) -> tuple[float, float]:
        return (
            (rectangle[0], rectangle[2])
            if horizontal
            else (rectangle[1], rectangle[3])
        )

    def secondary(rectangle: list[Any]) -> tuple[float, float]:
        return (
            (rectangle[1], rectangle[3])
            if horizontal
            else (rectangle[0], rectangle[2])
        )

    paint = view["computedPaintMaskGeometry"]
    stable_paint = view["floatedPresentationGeometry"]
    measurement = view["stableAppletMeasurementBounds"]
    applets = view["appletsLayoutGeometry"]
    available = view["availablePrimaryLength"]
    if (
        not isinstance(available, int)
        or isinstance(available, bool)
        or available <= 0
    ):
        fail(f"view {persistent_id} has no positive integral applet budget")
    paint_start, paint_length = primary(stable_paint)
    measurement_start, measurement_length = primary(measurement)
    applet_start, applet_length = primary(applets)
    if (
        (paint_start, paint_length) != (measurement_start, measurement_length)
        or applet_length != available
        or applet_start < paint_start
        or applet_start + applet_length > paint_start + paint_length
        or secondary(applets) != secondary(paint)
    ):
        fail(
            f"view {persistent_id} applet and popup geometry escaped "
            "the stable resting layout"
        )


def assert_transition_and_lifecycle(view: dict[str, Any]) -> None:
    persistent_id = view["persistentDockId"]
    published_strut_depth = (
        view["publishedStruts"][3]
        if view["orientation"] == "horizontal"
        else view["publishedStruts"][2]
    )
    shadow_contract_matches = (
        view["shadowEnabledBorders"] is None
        and view["shadowPaddingOffsets"] is None
    ) or (
        view["shadowEnabledBorders"] == view["enabledBorders"]
        and view["shadowPaddingOffsets"] is not None
    )
    required_true_fields = (
        "floatingGapConfigured",
        "floatingPanelConfigured",
        "floatingPanelEligible",
        "attachOnWindowTouchConfigured",
        "transitionGeometryPresent",
        "geometrySettled",
        "inReadyState",
        "layerShellPresent",
        "reservationSurfacePresent",
    )
    missing_true_fields = [
        field for field in required_true_fields
        if view[field] is not True
    ]
    if missing_true_fields:
        fail(
            f"view {persistent_id} has false floating-panel authorities: "
            f"{missing_true_fields}"
        )
    if (
        view["type"] != "panel"
        or view["visibilityMode"] != "alwaysVisible"
        or view["isHidden"]
        or not math.isclose(
            view["maximumLengthRatio"],
            0.45,
            rel_tol=0.0,
            abs_tol=FLOAT32_ABSOLUTE_TOLERANCE,
        )
        or not math.isclose(
            view["offsetRatio"],
            0.0,
            rel_tol=0.0,
            abs_tol=FLOAT32_ABSOLUTE_TOLERANCE,
        )
        or view["attachmentWaitsForPointerExitConfigured"]
        or view["pointerInsideView"]
        or view["attachmentDeferredByPointer"]
        or view["dockGapHideRequested"]
        or view["touchingWindowCount"] != 0
        or view["windowTouchGeometryRoleType"] not in ("", "QRect")
        or view["transitionTarget"] != "floated"
        or not math.isclose(
            view["transitionProgress"],
            1.0,
            rel_tol=0.0,
            abs_tol=FLOAT32_ABSOLUTE_TOLERANCE,
        )
        or view["presentedScreenEdgeGap"] != view["screenEdgeMargin"]
        or view["transitionPhase"] != "resting"
        or view["transitionDirection"] != "none"
        or view["transitionRunning"]
        or view["currentVisibleGeometry"] != view["floatedPresentationGeometry"]
        or view["computedPaintMaskGeometry"] != view["currentVisibleGeometry"]
        or view["maskRect"] != view["computedPaintMaskGeometry"]
        or view["effectsRect"] != view["computedPaintMaskGeometry"]
        or view["inputMask"] != view["computedInputBridgeGeometry"]
        or view["stableLayerShellMargin"] != 0
        or view["layerShellExclusiveEdge"] != "none"
        or view["layerShellExclusiveZone"] != -1
        or view["strutsThickness"] != view["reservationContributionDepth"]
        or published_strut_depth != view["reservationContributionDepth"]
        or view["requestedReservationDepth"] != view["normalThickness"]
        or view["floatingDamageMaskPending"]
        or view["appliedInputMask"] != view["inputMask"]
        or view["inStartup"]
        or view["isOffScreen"]
        or view["inRelocationAnimation"]
        or view["inRelocationShowing"]
        or view["inDelete"]
        or view["relocationGeneration"] != view["appliedRelocationGeneration"]
        or view["configuredIconSize"] is None
        or view["effectiveIconSize"] is None
        or view["availablePrimaryLength"] is None
        or not view["floatingAppletPopupsPreferred"]
        or view["enabledBorders"] != ["top", "right", "bottom", "left"]
        or not shadow_contract_matches
        or not view["layerShellAnchors"]
    ):
        fail(f"view {persistent_id} did not converge to its stable floated state")
    for revision in (
        "floatingAnchorRevision",
        "transitionGeometryRevision",
        "surfaceGeometryPublicationRevision",
        "layerShellConfigureRequestRevision",
    ):
        if require_decimal(
            view[revision],
            f"view {persistent_id}.{revision}",
        ) == 0:
            fail(
                f"view {persistent_id} did not publish required {revision}"
            )
    for geometry in (
        "stableCanvasGeometry",
        "attachedPresentationGeometry",
        "floatedPresentationGeometry",
        "currentVisibleGeometry",
        "computedPaintMaskGeometry",
        "computedInputBridgeGeometry",
        "stableTriggerGeometry",
        "stableAppletMeasurementBounds",
    ):
        value = view[geometry]
        if (
            not isinstance(value, list)
            or len(value) != 4
            or any(not isinstance(component, (int, float)) for component in value)
            or value[2] <= 0
            or value[3] <= 0
        ):
            fail(f"view {persistent_id} has invalid {geometry}")
    assert_applet_geometry(view)


def assert_reservations(
    snapshot: dict[str, Any],
    views: dict[int, dict[str, Any]],
    outputs: dict[str, OutputSnapshot],
) -> None:
    group_keys = [
        (group["outputId"], group["edge"])
        for group in snapshot["reservationGroups"]
    ]
    if len(group_keys) != len(set(group_keys)):
        fail("reservation groups contain a duplicate output-edge key")
    groups = {
        (group["outputId"], group["edge"]): group
        for group in snapshot["reservationGroups"]
    }
    expected_keys = {(view["screenId"], view["edge"]) for view in views.values()}
    if set(groups) != expected_keys:
        fail(f"reservation groups {sorted(groups)} do not equal {sorted(expected_keys)}")
    outputs_by_id = output_by_identity(outputs)
    expected_depths = expected_reservation_depths(views)
    publisher_tokens: list[str] = []
    for key, group in groups.items():
        output = outputs_by_id.get(key[0])
        if output is None:
            fail(f"reservation group {key} has no independent output")
        edge = edge_from_label(key[1])
        members = sorted(
            view["persistentDockId"]
            for view in views.values()
            if (view["screenId"], view["edge"]) == key
        )
        contribution_depths = [
            views[persistent_id]["reservationContributionDepth"]
            for persistent_id in members
        ]
        requested_depths = [
            views[persistent_id]["requestedReservationDepth"]
            for persistent_id in members
        ]
        expected_depth = expected_depths[key]
        expected_geometry = expected_reservation_geometry(
            output, edge, expected_depth
        )
        expected_window_geometry = expected_reservation_window_geometry(
            output, edge, expected_depths
        )
        expected_anchors = expected_reservation_anchors(edge)
        if (
            group["contributorDockIds"] != members
            or group["memberCount"] != len(members)
            or any(
                not isinstance(depth, int)
                or isinstance(depth, bool)
                or depth <= 0
                for depth in contribution_depths
            )
            or contribution_depths != requested_depths
            or group["publishedDepth"] != expected_depth
            or not group["layerShellPresent"]
            or group["geometry"] != expected_geometry
            or group["windowGeometry"] != expected_window_geometry
            or group["layerShellAnchors"] != expected_anchors
            or group["layerShellMargins"] != [0, 0, 0, 0]
            or group["layerShellExclusiveEdge"] != group["edge"]
            or group["layerShellExclusiveZone"] != expected_depth
            or not isinstance(group["publisher"], str)
            or not group["publisher"]
        ):
            fail(f"reservation group {key} violates maximum-depth ownership")
        publisher_tokens.append(group["publisher"])
        mirror = {
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
        for persistent_id in members:
            view = views[persistent_id]
            depth = view["requestedReservationDepth"]
            expected_strut = expected_view_strut_geometry(
                output,
                edge,
                view["stablePrimaryAxisStart"],
                view["stablePrimaryAxisLength"],
                depth,
            )
            if (
                view["normalThickness"] != depth
                or view["strutsThickness"] != depth
                or view["reservationContributionDepth"] != depth
                or view["publishedStruts"] != expected_strut
                or any(
                    view[left] != group[right]
                    for left, right in mirror.items()
                )
                or view["objects"]["reservationPublisher"] != group["publisher"]
            ):
                fail(f"view {persistent_id} reservation mirror diverged")
    if len(publisher_tokens) != len(set(publisher_tokens)):
        fail("distinct reservation groups share one publisher")


def assert_edit(
    snapshot: dict[str, Any],
    state: ModelState,
    bindings: dict[str, int],
) -> None:
    expected_id = bindings[state.editing] if state.editing else None
    expected_config_owner = (
        bindings[state.config_owner] if state.config_owner else None
    )
    if snapshot["globalConfigureAppletsMode"] != state.configuring:
        fail("global rearrange ownership does not match the model")
    for view in snapshot["views"]:
        editing = view["persistentDockId"] == expected_id
        if (
            view["editMode"] != editing
            or view["settingsWindowShown"] != editing
            or view["effectiveConfigureAppletsMode"]
            != (editing and state.configuring)
        ):
            fail(f"edit presentation escaped to view {view['persistentDockId']}")
        if editing and not view["objects"]["configWindow"]:
            fail(f"editing view {view['persistentDockId']} has no config window owner")
        owns_config_window = (
            view["persistentDockId"] == expected_config_owner
        )
        if bool(view["objects"]["configWindow"]) != owns_config_window:
            fail(
                f"view {view['persistentDockId']} has the wrong config window owner state"
            )


def assert_snapshot(
    plan: dict[str, Any],
    through: int,
    bindings_value: Any,
    outputs_value: Any,
    snapshot_value: Any,
    layouts_value: Any | None = None,
) -> ModelState:
    snapshot = parse_snapshot(snapshot_value)
    bindings = parse_bindings(bindings_value)
    outputs = parse_outputs(outputs_value)
    layouts = parse_layouts(
        layouts_value
        if layouts_value is not None
        else {
            LayoutRole.ORIGIN.value: "My Layout",
            LayoutRole.DESTINATION.value: "Other Layout",
        }
    )
    state = state_through(plan, through)
    needed = {view.handle for view in state.views}
    missing = sorted(needed - set(bindings))
    surplus = sorted(set(bindings) - needed)
    if missing:
        fail(f"checkpoint lacks bindings for {missing}")
    if surplus:
        fail(f"checkpoint has surplus bindings for {surplus}")
    views = view_map(snapshot)
    assert_lineage(state, bindings, views)
    assert_placement(state, bindings, outputs, views)
    assert_runtime_ownership(state, bindings, layouts, views)
    assert_stable_spans(views)
    for view in views.values():
        assert_transition_and_lifecycle(view)
    assert_reservations(snapshot, views, outputs)
    assert_edit(snapshot, state, bindings)
    return state


def snapshot_view_ids(snapshot: dict[str, Any]) -> set[int]:
    return {view["persistentDockId"] for view in snapshot["views"]}


def resolve_operation(payload_value: Any) -> dict[str, Any]:
    payload = require_object(payload_value, "resolve input")
    require_keys(
        payload,
        ("step", "bindings", "outputs", "layouts"),
        "resolve input",
    )
    step = require_object(payload["step"], "step")
    operation = parse_operation(step, require_int(step.get("seq"), "step.seq", 1))
    bindings = parse_bindings(payload["bindings"])
    outputs = parse_outputs(payload["outputs"])
    layouts = parse_layouts(payload["layouts"])

    def bound(handle: str | None) -> int:
        if handle is None or handle not in bindings:
            fail(f"operation {operation.seq} has no binding for {handle!r}")
        return bindings[handle]

    action: dict[str, Any]
    resolved: dict[str, Any] = {}
    if operation.kind is OperationKind.MOVE:
        assert operation.placement is not None
        target = bound(operation.target)
        screen_id = outputs[operation.placement.output.value].identity
        action = {
            "kind": "dbus",
            "method": "setViewPlacement",
            "signature": "uiii",
            "args": [
                target,
                screen_id,
                int(operation.placement.edge),
                int(operation.placement.alignment),
            ],
        }
        resolved = {"targetPersistentDockId": target, "screenId": screen_id}
    elif operation.kind is OperationKind.MOVE_LAYOUT:
        assert operation.layout is not None
        target = bound(operation.target)
        layout_name = layouts[operation.layout.value]
        action = {
            "kind": "dbus",
            "method": "moveViewToLayout",
            "signature": "us",
            "args": [target, layout_name],
        }
        resolved = {
            "targetPersistentDockId": target,
            "layout": operation.layout.value,
            "layoutName": layout_name,
        }
    elif operation.kind is OperationKind.CREATE_LINKED:
        assert operation.placement is not None
        source = bound(operation.source)
        screen_id = outputs[operation.placement.output.value].identity
        action = {
            "kind": "dbus",
            "method": "createLinkedView",
            "signature": "uii",
            "args": [source, screen_id, int(operation.placement.edge)],
        }
        resolved = {"sourcePersistentDockId": source, "screenId": screen_id}
    elif operation.kind is OperationKind.DUPLICATE:
        source = bound(operation.source)
        action = {
            "kind": "dbus",
            "method": "duplicateView",
            "signature": "u",
            "args": [source],
        }
        resolved = {"sourcePersistentDockId": source}
    elif operation.kind in (
        OperationKind.BEGIN_EDIT,
        OperationKind.END_EDIT,
    ):
        target = bound(operation.target)
        enabled = operation.kind is OperationKind.BEGIN_EDIT
        action = {
            "kind": "dbus",
            "method": "setViewEditMode",
            "signature": "ub",
            "args": [target, enabled],
        }
        resolved = {"targetPersistentDockId": target}
    elif operation.kind in (
        OperationKind.CONFIGURE_ON,
        OperationKind.CONFIGURE_OFF,
    ):
        target = bound(operation.target)
        enabled = operation.kind is OperationKind.CONFIGURE_ON
        action = {
            "kind": "dbus",
            "method": "setViewConfiguringApplets",
            "signature": "ub",
            "args": [target, enabled],
        }
        resolved = {"targetPersistentDockId": target}
    elif operation.kind is OperationKind.REMOVE:
        target = bound(operation.target)
        action = {
            "kind": "dbus",
            "method": "removeView",
            "signature": "u",
            "args": [target],
        }
        resolved = {"targetPersistentDockId": target}
    elif operation.kind is OperationKind.RELOAD:
        target = bound(operation.target)
        action = {
            "kind": "dbus",
            "method": "reloadView",
            "signature": "u",
            "args": [target],
            "affected": list(operation.affected),
        }
        resolved = {"targetPersistentDockId": target}
    elif operation.kind is OperationKind.RESTART:
        action = {"kind": "restart", "affected": list(operation.affected)}
    else:
        fail(f"unhandled operation kind {operation.kind}")
    return {
        "action": action,
        "record": {
            "record": "operation",
            "seq": operation.seq,
            "operation": step["operation"],
            "resolved": resolved,
        },
    }


def bind_result(payload_value: Any) -> dict[str, Any]:
    payload = require_object(payload_value, "result input")
    require_keys(
        payload, ("step", "bindings", "before", "after"), "result input"
    )
    step = require_object(payload["step"], "step")
    operation = parse_operation(step, require_int(step.get("seq"), "step.seq", 1))
    bindings = parse_bindings(payload["bindings"])
    before = parse_snapshot(payload["before"])
    after = parse_snapshot(payload["after"])
    after_sequence = require_decimal(
        after["snapshotSequence"], "after.snapshotSequence"
    )
    before_sequence = require_decimal(
        before["snapshotSequence"], "before.snapshotSequence"
    )
    if (
        operation.kind is not OperationKind.RESTART
        and after_sequence <= before_sequence
    ):
        fail(f"operation {operation.seq} did not advance the snapshot sequence")
    before_ids = snapshot_view_ids(before)
    after_ids = snapshot_view_ids(after)
    created_id: int | None = None
    if operation.kind in (OperationKind.CREATE_LINKED, OperationKind.DUPLICATE):
        created = sorted(after_ids - before_ids)
        if len(created) != 1 or before_ids - after_ids:
            fail(f"operation {operation.seq} did not create exactly one dock")
        assert operation.result is not None
        if operation.result in bindings:
            fail(f"operation {operation.seq} result handle is already bound")
        created_id = created[0]
        if created_id in bindings.values():
            fail(f"operation {operation.seq} reused persistent dock id {created_id}")
        bindings[operation.result] = created_id
    elif operation.kind is OperationKind.REMOVE:
        assert operation.target is not None
        target_id = bindings[operation.target]
        if before_ids - after_ids != {target_id} or after_ids - before_ids:
            fail(f"operation {operation.seq} did not remove exactly its target")
        # A persistent containment identity is stable only for that
        # containment's lifetime. Plasma may assign its now-free numeric ID
        # to a later containment, so the live handle table must release it
        # after this exact removal result has been proved.
        del bindings[operation.target]
    elif before_ids != after_ids:
        fail(f"operation {operation.seq} unexpectedly changed the live dock set")
    record: dict[str, Any] = {
        "record": "result",
        "seq": operation.seq,
        "snapshotSequence": after["snapshotSequence"],
    }
    if created_id is not None:
        record["createdPersistentDockId"] = created_id
    return {"bindings": bindings, "record": record}


def quiescent_projection(snapshot_value: Any) -> dict[str, Any]:
    snapshot = copy.deepcopy(parse_snapshot(snapshot_value))
    snapshot.pop("snapshotSequence")
    return snapshot


DURABLE_VIEW_KEYS = (
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
    "screenEdgeMargin",
    "windowGeometry",
    "absoluteGeometry",
    "localGeometry",
    "screenGeometry",
    "surfaceGeometry",
    "canvasGeometry",
    "effectsRect",
    "appletsLayoutGeometry",
    "maskRect",
    "inputMask",
    "appliedInputMask",
    "floatingDamageMaskPending",
    "enabledBorders",
    "shadowEnabledBorders",
    "shadowPaddingOffsets",
    "floatingAppletPopupsPreferred",
    "strutsThickness",
    "publishedStruts",
    "layerShellPresent",
    "layerShellAnchors",
    "layerShellMargins",
    "layerShellExclusiveEdge",
    "layerShellExclusiveZone",
    "stableCanvasGeometry",
    "attachedPresentationGeometry",
    "floatedPresentationGeometry",
    "currentVisibleGeometry",
    "computedPaintMaskGeometry",
    "computedInputBridgeGeometry",
    "contentTranslation",
    "stableTriggerGeometry",
    "stableAppletMeasurementBounds",
    "stablePrimaryAxisStart",
    "stablePrimaryAxisLength",
    "stableLayerShellMargin",
    "requestedReservationDepth",
    "reservationOutputId",
    "reservationEdge",
    "reservationContributionDepth",
    "reservationPublishedDepth",
    "reservationGroupMemberCount",
    "reservationContributorDockIds",
    "reservationGeometry",
    "reservationWindowGeometry",
    "reservationSurfacePresent",
    "reservationLayerShellAnchors",
    "reservationLayerShellMargins",
    "reservationLayerShellExclusiveEdge",
    "reservationLayerShellExclusiveZone",
    "floatingGapConfigured",
    "floatingPanelConfigured",
    "floatingPanelEligible",
    "attachOnWindowTouchConfigured",
    "attachmentWaitsForPointerExitConfigured",
    "pointerInsideView",
    "attachmentDeferredByPointer",
    "dockGapHideRequested",
    "touchingWindowCount",
    "windowTouchGeometryRoleType",
    "transitionTarget",
    "transitionProgress",
    "transitionPhase",
    "transitionDirection",
    "transitionRunning",
    "visibilityMode",
    "isHidden",
    "geometrySettled",
    "inReadyState",
    "editMode",
    "effectiveConfigureAppletsMode",
    "settingsWindowShown",
)

DURABLE_GROUP_KEYS = (
    "outputId",
    "edge",
    "publishedDepth",
    "contributorDockIds",
    "memberCount",
    "geometry",
    "windowGeometry",
    "layerShellPresent",
    "layerShellAnchors",
    "layerShellMargins",
    "layerShellExclusiveEdge",
    "layerShellExclusiveZone",
)


def durable_projection(snapshot_value: Any) -> dict[str, Any]:
    snapshot = parse_snapshot(snapshot_value)
    return {
        "stacking": snapshot["stacking"],
        "reservationGroups": [
            {key: group[key] for key in DURABLE_GROUP_KEYS}
            for group in sorted(
                snapshot["reservationGroups"],
                key=lambda item: (item["outputId"], item["edge"]),
            )
        ],
        "views": [
            {key: view[key] for key in DURABLE_VIEW_KEYS}
            for view in sorted(
                snapshot["views"], key=lambda item: item["persistentDockId"]
            )
        ],
    }


def assert_runtime_reload(payload_value: Any) -> dict[str, bool]:
    payload = require_object(payload_value, "runtime reload input")
    require_keys(
        payload, ("before", "after", "bindings", "affected"), "runtime reload input"
    )
    before = parse_snapshot(payload["before"])
    after = parse_snapshot(payload["after"])
    bindings = parse_bindings(payload["bindings"])
    affected = payload["affected"]
    if not isinstance(affected, list) or any(
        handle not in bindings for handle in affected
    ):
        fail("runtime reload affected handles are invalid")
    before_views = view_map(before)
    after_views = view_map(after)
    if set(before_views) != set(after_views):
        fail("runtime reload changed persistent dock identities")
    affected_ids = {bindings[handle] for handle in affected}
    before_runtime_ids = {
        view["runtimeViewId"] for view in before_views.values()
    }
    after_runtime_ids = [
        view["runtimeViewId"] for view in after_views.values()
    ]
    if len(after_runtime_ids) != len(set(after_runtime_ids)):
        fail("runtime reload produced duplicate runtime identities")
    for persistent_id in before_views:
        changed = (
            before_views[persistent_id]["runtimeViewId"]
            != after_views[persistent_id]["runtimeViewId"]
        )
        if changed != (persistent_id in affected_ids):
            fail(
                f"runtime reload ownership is wrong for persistent dock {persistent_id}"
            )
        if (
            persistent_id in affected_ids
            and after_views[persistent_id]["runtimeViewId"] in before_runtime_ids
        ):
            fail(
                f"runtime reload reused a retired identity for "
                f"persistent dock {persistent_id}"
            )
    return {"ok": True}


def replay_header(payload_value: Any) -> dict[str, Any]:
    payload = require_object(payload_value, "replay header input")
    require_keys(
        payload,
        ("plan", "bindings", "outputs", "layouts"),
        "replay header input",
    )
    plan = validate_plan(payload["plan"])
    bindings = parse_bindings(payload["bindings"])
    outputs = parse_outputs(payload["outputs"])
    layouts = parse_layouts(payload["layouts"])
    if set(bindings) != {"root"}:
        fail("initial replay header must bind only root")
    return {
        "record": "header",
        "format": REPLAY_FORMAT,
        "version": FORMAT_VERSION,
        "dockSystemSchema": SCHEMA_VERSION,
        "generator": GENERATOR,
        "seed": plan["seed"],
        "planSha256": plan["planSha256"],
        "outputs": outputs_to_json(outputs),
        "layouts": layouts,
        "initialBindings": bindings,
    }


def validate_replay(path: str, plan_value: Any) -> dict[str, Any]:
    plan = validate_plan(plan_value)
    try:
        lines = Path(path).read_text(encoding="utf-8").splitlines()
    except OSError as error:
        fail(f"could not read replay: {error}")
    if not lines:
        fail("replay is empty")
    records = []
    for index, line in enumerate(lines, 1):
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError as error:
            fail(f"replay line {index} is invalid JSON: {error}")
    header = require_object(records[0], "replay header")
    require_keys(
        header,
        ("outputs", "layouts", "initialBindings"),
        "replay header",
    )
    outputs = parse_outputs(header["outputs"])
    layouts = parse_layouts(header["layouts"])
    bindings = parse_bindings(header["initialBindings"])
    expected_header = replay_header(
        {
            "plan": plan,
            "bindings": bindings,
            "outputs": outputs_to_json(outputs),
            "layouts": layouts,
        }
    )
    if header != expected_header:
        fail("replay header does not match the operation plan")
    expected_operations = plan["operations"]
    if len(records) != 1 + 2 * len(expected_operations):
        fail("replay is truncated or has surplus records")
    previous_snapshot_sequence = -1
    for index, step in enumerate(expected_operations, 1):
        operation = require_object(records[2 * index - 1], f"replay operation {index}")
        result = require_object(records[2 * index], f"replay result {index}")
        expected_operation = resolve_operation(
            {
                "step": step,
                "bindings": bindings,
                "outputs": outputs_to_json(outputs),
                "layouts": layouts,
            }
        )["record"]
        if operation != expected_operation:
            fail(f"replay operation/result pair {index} diverges from the plan")
        require_keys(
            result,
            ("record", "seq", "snapshotSequence"),
            f"replay result {index}",
        )
        snapshot_sequence = require_decimal(
            result["snapshotSequence"],
            f"replay result {index}.snapshotSequence",
        )
        operation_kind = parse_operation(step, index).kind
        if operation_kind is OperationKind.RESTART:
            previous_snapshot_sequence = -1
        if (
            result["record"] != "result"
            or result["seq"] != index
            or snapshot_sequence == 0
            or snapshot_sequence <= previous_snapshot_sequence
        ):
            fail(f"replay operation/result pair {index} diverges from the plan")
        previous_snapshot_sequence = snapshot_sequence
        if operation_kind in (
            OperationKind.CREATE_LINKED,
            OperationKind.DUPLICATE,
        ):
            created_id = require_int(
                result.get("createdPersistentDockId"),
                f"replay result {index}.createdPersistentDockId",
                1,
            )
            result_handle = step["operation"]["result"]
            if result_handle in bindings or created_id in bindings.values():
                fail(f"replay result {index} reuses an identity binding")
            bindings[result_handle] = created_id
            expected_result_keys = {
                "record",
                "seq",
                "snapshotSequence",
                "createdPersistentDockId",
            }
        elif operation_kind is OperationKind.REMOVE:
            operation_target = step["operation"]["target"]
            if operation_target not in bindings:
                fail(f"replay result {index} removes an unbound identity")
            del bindings[operation_target]
            expected_result_keys = {"record", "seq", "snapshotSequence"}
        else:
            expected_result_keys = {"record", "seq", "snapshotSequence"}
        if set(result) != expected_result_keys:
            fail(f"replay result {index} has missing or surplus fields")
    return {"ok": True, "operationCount": len(expected_operations)}


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser()
    subparsers = result.add_subparsers(dest="command", required=True)
    generate = subparsers.add_parser("generate-plan")
    generate.add_argument("--seed", type=int, default=DEFAULT_SEED)
    for name in (
        "validate-plan",
        "emit-operations",
        "resolve-operation",
        "bind-result",
        "replay-header",
        "assert-baseline",
        "assert-checkpoint",
        "assert-edit",
        "quiescent-projection",
        "durable-projection",
        "assert-runtime-reload",
        "assert-visual-window-ownership",
        "assert-view-move-lifecycle",
    ):
        subparsers.add_parser(name)
    replay = subparsers.add_parser("validate-replay")
    replay.add_argument("--plan", required=True)
    replay.add_argument("--replay", required=True)
    return result


def main() -> None:
    args = parser().parse_args()
    if args.command == "generate-plan":
        print(compact(generate_plan(args.seed)))
        return
    if args.command == "validate-replay":
        with open(args.plan, encoding="utf-8") as stream:
            plan = json.load(stream)
        print(compact(validate_replay(args.replay, plan)))
        return
    payload = read_json()
    if args.command == "validate-plan":
        output = validate_plan(payload)
    elif args.command == "emit-operations":
        output = validate_plan(payload)["operations"]
        for step in output:
            print(compact(step))
        return
    elif args.command == "resolve-operation":
        output = resolve_operation(payload)
    elif args.command == "bind-result":
        output = bind_result(payload)
    elif args.command == "replay-header":
        output = replay_header(payload)
    elif args.command == "assert-baseline":
        snapshot = parse_snapshot(payload)
        views = snapshot["views"]
        if (
            len(views) != 1
            or views[0]["relationship"] != "independent"
            or views[0]["logicalDockId"] != views[0]["persistentDockId"]
            or views[0]["originalDockId"] is not None
            or views[0]["linkedDockIds"]
        ):
            fail("baseline must contain exactly one independent dock")
        output = {"ok": True}
    elif args.command == "assert-checkpoint":
        request = require_object(payload, "checkpoint input")
        require_keys(
            request,
            ("plan", "through", "bindings", "outputs", "layouts", "snapshot"),
            "checkpoint input",
        )
        state = assert_snapshot(
            validate_plan(request["plan"]),
            require_int(request["through"], "checkpoint through", 0),
            request["bindings"],
            request["outputs"],
            request["snapshot"],
            request["layouts"],
        )
        output = {
            "ok": True,
            "modelState": {
                "live": [view.handle for view in state.views],
                "destroyed": list(state.destroyed),
                "editing": state.editing,
                "configuring": state.configuring,
                "configOwner": state.config_owner,
            },
        }
    elif args.command == "assert-edit":
        request = require_object(payload, "edit input")
        require_keys(
            request,
            ("snapshot", "bindings", "target", "editing", "configuring"),
            "edit input",
        )
        bindings = parse_bindings(request["bindings"])
        target = request["target"]
        if target not in bindings:
            fail("edit target has no binding")
        assert_edit(
            parse_snapshot(request["snapshot"]),
            ModelState(
                (),
                editing=target if request["editing"] else None,
                configuring=bool(request["configuring"]),
                config_owner=target,
            ),
            bindings,
        )
        output = {"ok": True}
    elif args.command == "quiescent-projection":
        output = quiescent_projection(payload)
    elif args.command == "durable-projection":
        output = durable_projection(payload)
    elif args.command == "assert-runtime-reload":
        output = assert_runtime_reload(payload)
    elif args.command == "assert-view-move-lifecycle":
        output = assert_view_move_lifecycle(payload)
    elif args.command == "assert-visual-window-ownership":
        output = assert_visual_window_ownership(payload)
    else:
        fail(f"unhandled command {args.command}")
    print(compact(output))


if __name__ == "__main__":
    main()
