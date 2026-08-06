# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
"""The multi-output discover-and-pin layer: the port of
tests/e2e/matrix/multi-output-lib.sh (BP-3c).

Sourced by the multi-output self-test recipe inside a TWO-output nested vehicle
(E2E_OUTPUT_COUNT=2, via scripts/run-multi-output-e2e.sh). It answers open question
O7 - "which Latte screen id / connector maps to which physical virtual output" -
by READING the running dock's own ScreenPool over D-Bus (the screensData
readback), never by scraping a log line or hardcoding a connector name the
compositor is free to change between runs. Then it PINS the secondary output to a
fixed, documented ScreenPool id so a per-screen fixture lands the view
deterministically, and it VERIFIES the pin held by the same queryable surface.

Migration shape (the BP-2c/BP-3a fresh-module precedent): a fresh module, not a
bridge. This module owns the semantics outright: the bash lib and its contract
test were deleted by cleanup BW-3 (the multi-output bash closed loop - bash whose
only consumer was a bash test of itself), and the transaction drivers'
fake-doctor contracts are pinned behaviorally in
harness/tests/test_multi_output_transactions.py. The discovery/topology/pin math
(_discover_from_screens,
_project_output_state, _classify_output_priorities, _compare_output_state_semantically,
mo_classify_rectangles, _read_rectangles_from, _placement_target, _assert_pin_resolved)
is pure so it is unit-testable without a dual-output vehicle.

Two readback boundaries, two validation styles, deliberately:

- The dock's own D-Bus surfaces (screensData, viewsData) are pydantic-validated
  into typed records (Screen, _MoView), the harness's normal boundary contract.
- kscreen-doctor's -j output is the EXTERNAL tool's opaque JSON. It is parsed at
  the boundary with pydantic's JsonValue (so a non-JSON reply fails loudly) but
  kept as the raw recursive structure, because the semantic cleanup verifier's
  whole job is to detect ANY field drift across a restore - an extra="ignore"
  model would silently drop exactly the fields it must compare. The explicit
  isinstance narrowing mirrors the bash's defensive field checks one for one.
"""

from __future__ import annotations

import os
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import TypeIs

from pydantic import BaseModel, ConfigDict, Field, JsonValue, TypeAdapter, ValidationError

from latte_harness import recipe
from latte_harness.recipe import Rect

# The id every 2out fixture pins its secondary view's lastScreen to. ScreenPool
# reads the [ScreenConnectors] group before it enumerates live outputs, so this
# id resolves to the secondary connector and the primary is assigned the next free
# id. A FIXED id keeps the pin queryable and stable across dock restarts
# regardless of the compositor's output-enumeration order - the exact O7
# non-determinism this retires.
E2E_MO_SECONDARY_ID = 10

# kscreen-doctor rotation codes -> setter tokens (the bash rotation_tokens map).
_ROTATION_TOKENS: dict[int, str] = {
    1: "none",
    2: "left",
    4: "inverted",
    8: "right",
    16: "flipped",
    32: "flipped90",
    64: "flipped180",
    128: "flipped270",
}

_TOPOLOGY_CLASSES = ("full-touching", "partial-touching", "disconnected")

_JSON_VALUE = TypeAdapter[JsonValue](JsonValue)


class MultiOutputError(Exception):
    """A multi-output step could not proceed (the vehicle is not dual-output, a
    KScreen state is malformed or not restorable, a view is on the wrong output).
    The diagnostic is carried in the message; the recipe surfaces it loudly, never
    a silent skip that would "pass" against one output (the never-swallow rule).
    """


class _CompareError(ValueError):
    """A KScreen semantic-comparison input was malformed (the bash ValueError path
    that maps to comparison status 2)."""


# ---- readback models (the dock's own D-Bus surfaces) -----------------------


class Screen(BaseModel):
    """One screensData entry: the queryable O7 screen<->output mapping."""

    model_config = ConfigDict(extra="ignore", populate_by_name=True, frozen=True)

    id: int
    name: str
    geometry: Rect
    is_active: bool = Field(alias="isActive")
    is_primary: bool = Field(alias="isPrimary")


class _MoView(BaseModel):
    """One viewsData entry with the fields the placement checks read: the identity,
    the connector NAME it sits on, and the clone flag."""

    model_config = ConfigDict(extra="ignore", populate_by_name=True, frozen=True)

    containment_id: int = Field(alias="containmentId")
    screen: str
    is_cloned: bool = Field(alias="isCloned")


_SCREENS = TypeAdapter(list[Screen])
_MO_VIEWS = TypeAdapter(list[_MoView])


@dataclass(frozen=True, slots=True)
class Discovery:
    """The discovered dual-output mapping: the two connectors, the secondary's Latte
    rect string, and the fixed ScreenPool id 2out fixtures pin to."""

    primary: str
    secondary: str
    secondary_geom: str
    secondary_id: int


@dataclass(frozen=True, slots=True)
class OutputState:
    """The restorable KScreen fields for one output (the bash projection row)."""

    name: str
    enabled: bool
    rotation: str
    scale: float
    x: int
    y: int
    priority: int


# ---- environment and transport ---------------------------------------------


def _require_env(name: str) -> str:
    """The bash ``${VAR:?}``: return the value, or refuse loudly naming the var."""
    value = os.environ.get(name)
    if not value:
        raise MultiOutputError(f"multi_output: required environment variable {name} is unset")
    return value


def _require_nested(helper: str) -> None:
    """_e2e_require_nested: a nested-only helper refuses loudly outside nested mode."""
    mode = os.environ.get("E2E_MODE")
    if mode != "nested":
        raise MultiOutputError(
            f"multi_output: {helper} is nested-only (it reads/mutates the vehicle "
            f"topology); refusing in mode '{mode or 'unset'}'"
        )


def _require_topology_mutation(caller: str) -> None:
    """Refuse unless every ambient-session identity proves this is the private
    two-output nested vehicle. The socket and bus-address checks are deliberately
    stricter than E2E_MODE alone: an exported marker in a desk shell must never
    authorize kscreen-doctor against host outputs."""
    _require_nested(caller)
    if os.environ.get("E2E_OUTPUT_COUNT") != "2":
        got = os.environ.get("E2E_OUTPUT_COUNT") or "unset"
        raise MultiOutputError(
            f"{caller}: runtime output mutation requires E2E_OUTPUT_COUNT=2, got '{got}'"
        )
    e2e_rt = os.environ.get("E2E_RT")
    if not e2e_rt or os.environ.get("XDG_RUNTIME_DIR") != e2e_rt:
        xdg = os.environ.get("XDG_RUNTIME_DIR") or "unset"
        raise MultiOutputError(
            f"{caller}: XDG_RUNTIME_DIR must equal the private E2E_RT "
            f"(XDG_RUNTIME_DIR='{xdg}', E2E_RT='{e2e_rt or 'unset'}')"
        )
    wayland = os.environ.get("WAYLAND_DISPLAY")
    if not wayland or "/" in wayland or not Path(e2e_rt, wayland).is_socket():
        raise MultiOutputError(
            f"{caller}: no active nested Wayland socket at '{e2e_rt}/{wayland or 'unset'}'"
        )
    bus_file = Path(e2e_rt, "bus-address")
    if not (bus_file.is_file() and os.access(bus_file, os.R_OK)):
        raise MultiOutputError(f"{caller}: private D-Bus address file is missing at '{bus_file}'")
    private_bus = bus_file.read_text().strip()
    if not private_bus or os.environ.get("DBUS_SESSION_BUS_ADDRESS") != private_bus:
        raise MultiOutputError(
            f"{caller}: ambient D-Bus does not match the nested vehicle's private bus"
        )
    if (
        subprocess.run(
            ["busctl", "--user", "--no-pager", "list"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        ).returncode
        != 0
    ):
        raise MultiOutputError(f"{caller}: the nested vehicle's private D-Bus is not responding")


def _screens() -> list[Screen]:
    """mo_screens_json: the ScreenPool topology, validated into typed records."""
    return _SCREENS.validate_json(recipe.json_payload("screensData"))


def _kscreen_read() -> str | None:
    """kscreen-doctor -j; the JSON text, or None on a nonzero exit."""
    result = subprocess.run(["kscreen-doctor", "-j"], capture_output=True, text=True, check=False)
    return result.stdout if result.returncode == 0 else None


def _kscreen_set(*args: str) -> bool:
    """kscreen-doctor <setters>; True on success."""
    return (
        subprocess.run(["kscreen-doctor", *args], stdout=subprocess.DEVNULL, check=False).returncode
        == 0
    )


# ---- view / screen readbacks -----------------------------------------------


def mo_view_screen(view: int) -> str:
    """mo_view_screen: the connector NAME the view currently sits on
    (viewsData.screen)."""
    views = _MO_VIEWS.validate_json(recipe.json_payload("viewsData"))
    found = next((v for v in views if v.containment_id == view), None)
    if found is None:
        raise MultiOutputError(f"mo_view_screen: view {view} not present")
    return found.screen


def _primary_view_screen() -> str:
    """The connector the first non-cloned view landed on - the ground-truth anchor
    for "which output is primary" (empty when no non-cloned view exists yet)."""
    views = _MO_VIEWS.validate_json(recipe.json_payload("viewsData"))
    non_cloned = [v for v in views if not v.is_cloned]
    return non_cloned[0].screen if non_cloned else ""


# ---- discovery (pure core + the env-exporting wrapper) ---------------------


def _discover_from_screens(screens: list[Screen], primary_view_screen: str) -> tuple[str, str, str]:
    """Identify the primary and the single active SECONDARY output, cross-checked
    against where the default onPrimary view actually landed. Pure so the discovery
    logic is unit-testable without a dual-output vehicle. Returns
    (primary name, secondary name, secondary Latte rect string)."""
    active = [s for s in screens if s.is_active]
    if len(active) != 2:
        raise MultiOutputError(
            f"expected exactly 2 active outputs under the dual vehicle, saw {len(active)}: "
            f"{[s.name for s in active]}"
        )
    primaries = [s for s in active if s.is_primary]
    if len(primaries) != 1:
        raise MultiOutputError(
            f"expected exactly 1 primary among the active outputs, saw {len(primaries)}: "
            f"{[s.name for s in primaries]}"
        )
    primary = primaries[0]
    secondary = next(s for s in active if not s.is_primary)
    if primary_view_screen and primary_view_screen != primary.name:
        raise MultiOutputError(
            f"ScreenPool reports primary={primary.name} but the onPrimary view is on "
            f"{primary_view_screen} (discovery inconsistent)"
        )
    g = secondary.geometry
    return primary.name, secondary.name, f"{g[0]},{g[1]} {g[2]}x{g[3]}"


def mo_discover_outputs() -> Discovery:
    """mo_discover_outputs: read the running dock's ScreenPool, identify primary and
    secondary, and export the pin parameters (E2E_MO_PRIMARY / E2E_MO_SECONDARY /
    E2E_MO_SECONDARY_GEOM / E2E_MO_SECONDARY_ID) so the fixture generator and the
    matrix backbone pick them up. Refuses loudly if the vehicle is not actually
    dual-output, or if ScreenPool's primary disagrees with where the default
    onPrimary view landed."""
    _require_nested("mo_discover_outputs")
    primary, secondary, secondary_geom = _discover_from_screens(_screens(), _primary_view_screen())
    os.environ["E2E_MO_PRIMARY"] = primary
    os.environ["E2E_MO_SECONDARY"] = secondary
    os.environ["E2E_MO_SECONDARY_GEOM"] = secondary_geom
    os.environ["E2E_MO_SECONDARY_ID"] = str(E2E_MO_SECONDARY_ID)
    print(
        f"mo_discover_outputs: primary={primary} secondary={secondary} "
        f"(pinned to ScreenPool id {E2E_MO_SECONDARY_ID}) geom='{secondary_geom}'",
        flush=True,
    )
    return Discovery(
        primary=primary,
        secondary=secondary,
        secondary_geom=secondary_geom,
        secondary_id=E2E_MO_SECONDARY_ID,
    )


# ---- KScreen projection and classification (pure) --------------------------


def _is_plain_int(value: JsonValue) -> TypeIs[int]:
    """A JSON integer that is not a bool (the bash ``type(x) is int``). A TypeIs so
    a positive check narrows the value to ``int`` for the range/assignment that
    follows, propagating through ``or`` in the field guards."""
    return isinstance(value, int) and not isinstance(value, bool)


def _parse_kscreen_object(state_json: str) -> dict[str, JsonValue]:
    """Parse a kscreen-doctor -j reply into its top object, raising _CompareError on
    a non-JSON or non-object reply (the boundary that must fail loud)."""
    try:
        payload = _JSON_VALUE.validate_json(state_json)
    except ValidationError as err:
        raise _CompareError(f"KScreen state is not valid JSON: {err}") from err
    if not isinstance(payload, dict):
        raise _CompareError("KScreen state must be a JSON object")
    return payload


def _two_named_outputs(payload: dict[str, JsonValue]) -> dict[str, dict[str, JsonValue]]:
    """The two KScreen outputs keyed by name, validating count, object shape,
    non-empty string names, and uniqueness (the bash shared front-half)."""
    raw_outputs = payload.get("outputs")
    if not isinstance(raw_outputs, list) or len(raw_outputs) != 2:
        raise _CompareError("expected exactly two KScreen outputs")
    by_name: dict[str, dict[str, JsonValue]] = {}
    for index, output in enumerate(raw_outputs):
        if not isinstance(output, dict):
            raise _CompareError("every KScreen output must be an object")
        name = output.get("name")
        if not isinstance(name, str) or not name:
            raise _CompareError(f"KScreen output {index} must have a nonempty string name")
        if name in by_name:
            raise _CompareError("KScreen output names are not unique")
        by_name[name] = output
    return by_name


def _project_output_state(
    state_json: str, primary_name: str, secondary_name: str
) -> list[OutputState]:
    """The restorable projection: validate every field this harness can restore for
    the two discovered outputs, then require continuous unique priorities 1 and 2.
    Pure so the projection is unit-testable. Raises MultiOutputError on any invalid
    or unrestorable field."""
    try:
        by_name = _two_named_outputs(_parse_kscreen_object(state_json))
    except _CompareError as err:
        raise MultiOutputError(str(err)) from err

    states: list[OutputState] = []
    for name in (primary_name, secondary_name):
        output = by_name.get(name)
        if output is None:
            raise MultiOutputError(f"ScreenPool output {name!r} is absent from KScreen")
        rotation_code = output.get("rotation")
        rotation = _ROTATION_TOKENS.get(rotation_code) if _is_plain_int(rotation_code) else None
        if rotation is None:
            raise MultiOutputError(f"output {name!r} has unsupported rotation")
        scale = output.get("scale")
        if isinstance(scale, bool) or not isinstance(scale, (int, float)) or scale <= 0:
            raise MultiOutputError(f"output {name!r} has invalid scale {scale!r}")
        enabled = output.get("enabled")
        if not isinstance(enabled, bool):
            raise MultiOutputError(f"output {name!r} has invalid enabled state")
        pos = output.get("pos")
        if not isinstance(pos, dict):
            raise MultiOutputError(f"output {name!r} has invalid position")
        x, y = pos.get("x"), pos.get("y")
        if not _is_plain_int(x) or not _is_plain_int(y):
            raise MultiOutputError(f"output {name!r} has invalid position")
        priority = output.get("priority")
        if not _is_plain_int(priority) or not 1 <= priority <= 100:
            raise MultiOutputError(f"output {name!r} has invalid priority {priority!r}")
        if any(character.isspace() for character in name):
            raise MultiOutputError(f"whitespace in output name {name!r} is unsupported")
        states.append(
            OutputState(
                name=name,
                enabled=enabled,
                rotation=rotation,
                scale=float(scale),
                x=x,
                y=y,
                priority=priority,
            )
        )

    if sorted(state.priority for state in states) != [1, 2]:
        raise MultiOutputError(
            f"the two active outputs need continuous unique priorities, "
            f"got {[state.priority for state in states]!r}"
        )
    return states


def _classify_output_priorities(state_json: str, primary_name: str, secondary_name: str) -> int:
    """Classify the two outputs' KScreen priorities: 0 when they already have the
    canonical unique 1 and 2, 1 when the private vehicle needs baseline
    normalization, 2 for malformed state (printed to stderr). Pure; returns the
    3-way status the bash returned."""
    try:
        by_name = _two_named_outputs(_parse_kscreen_object(state_json))
        priorities: list[int] = []
        for name in (primary_name, secondary_name):
            output = by_name.get(name)
            if output is None:
                raise _CompareError(f"ScreenPool output {name!r} is absent from KScreen")
            if output.get("enabled") is not True:
                raise _CompareError(f"ScreenPool output {name!r} is not enabled before capture")
            priority = output.get("priority")
            if not _is_plain_int(priority) or not 0 <= priority <= 100:
                raise _CompareError(f"output {name!r} has invalid priority {priority!r}")
            priorities.append(priority)
    except _CompareError as err:
        print(f"_mo_classify_output_priorities: {err}", file=sys.stderr, flush=True)
        return 2
    return 0 if sorted(priorities) == [1, 2] else 1


# ---- KScreen semantic comparison (pure, no field dropped) ------------------


def _numbers_equal(left: JsonValue, right: JsonValue) -> bool:
    """Two JSON numbers (neither a bool) comparing equal."""
    return (
        not isinstance(left, bool)
        and not isinstance(right, bool)
        and isinstance(left, (int, float))
        and isinstance(right, (int, float))
        and left == right
    )


def _sort_identity_collection(
    records: JsonValue, identity: str, path: str
) -> list[dict[str, JsonValue]]:
    """Sort an identity-keyed collection deterministically, so the comparison never
    depends on JSON array order. Every record must be an object carrying a unique
    string-or-integer ``identity``."""
    if not isinstance(records, list):
        raise _CompareError(f"{path} must be a JSON array")
    dicts: list[dict[str, JsonValue]] = []
    identities: list[str | int] = []
    for index, record in enumerate(records):
        if not isinstance(record, dict):
            raise _CompareError(f"{path}[{index}] must be a JSON object")
        value = record.get(identity)
        if isinstance(value, bool) or not isinstance(value, (str, int)):
            raise _CompareError(f"{path}[{index}].{identity} must be a string or integer")
        dicts.append(record)
        identities.append(value)
    if len({(type(v).__name__, v) for v in identities}) != len(identities):
        raise _CompareError(f"{path} has duplicate {identity} values")
    order = sorted(
        range(len(dicts)), key=lambda i: (type(identities[i]).__name__, str(identities[i]))
    )
    return [dicts[i] for i in order]


def _canonicalize(payload: dict[str, JsonValue], label: str) -> dict[str, JsonValue]:
    """Canonicalize a KScreen payload: outputs sorted by name, each output's modes
    sorted by id, so a byte-order difference is not mistaken for a semantic one."""
    outputs = _sort_identity_collection(payload.get("outputs"), "name", f"{label}.outputs")
    canonical = dict(payload)
    canonical_outputs: list[JsonValue] = []
    for output in outputs:
        canonical_output: dict[str, JsonValue] = dict(output)
        if "modes" in canonical_output:
            name = output.get("name")
            canonical_output["modes"] = list(
                _sort_identity_collection(
                    canonical_output["modes"], "id", f"{label}.outputs[{name!r}].modes"
                )
            )
        canonical_outputs.append(canonical_output)
    canonical["outputs"] = canonical_outputs
    return canonical


def _first_difference(left: JsonValue, right: JsonValue, path: str = "$") -> str | None:
    """The first semantic difference between two canonicalized payloads, or None.

    No value or field is excluded: a removed/added field, a type change, a list
    length change, or a changed scalar all surface. Integer-vs-float numeric
    equality is tolerated (1 == 1.0), matching the bash comparator."""
    if _numbers_equal(left, right):
        return None
    if type(left) is not type(right):
        return f"{path}: type changed from {type(left).__name__} to {type(right).__name__}"
    if isinstance(left, dict) and isinstance(right, dict):
        left_keys, right_keys = set(left), set(right)
        if left_keys != right_keys:
            removed = sorted(left_keys - right_keys)
            added = sorted(right_keys - left_keys)
            return f"{path}: fields removed={removed!r} added={added!r}"
        for key in sorted(left):
            difference = _first_difference(left[key], right[key], f"{path}.{key}")
            if difference is not None:
                return difference
        return None
    if isinstance(left, list) and isinstance(right, list):
        if len(left) != len(right):
            return f"{path}: list length changed from {len(left)} to {len(right)}"
        for index, (captured_value, current_value) in enumerate(zip(left, right, strict=True)):
            difference = _first_difference(captured_value, current_value, f"{path}[{index}]")
            if difference is not None:
                return difference
        return None
    if left != right:
        return f"{path}: changed from {left!r} to {right!r}"
    return None


def _compare_output_state_semantically(captured_json: str, current_json: str) -> tuple[int, str]:
    """Compare two complete KScreen payloads without relying on JSON byte order,
    dropping no field. Returns (status, message): 0 equal, 1 drift (message names
    the first difference), 2 malformed (message names the parse error)."""
    try:
        captured = _canonicalize(_parse_kscreen_object(captured_json), "captured")
        current = _canonicalize(_parse_kscreen_object(current_json), "current")
    except _CompareError as err:
        return 2, f"_mo_compare_output_state_semantically: {err}"
    difference = _first_difference(captured, current)
    if difference is not None:
        return (
            1,
            "_mo_compare_output_state_semantically: complete KScreen state "
            f"drifted at {difference}",
        )
    return 0, ""


# ---- capture and restore (runtime; needs the dual-output vehicle) ----------


def mo_capture_output_topology() -> str:
    """mo_capture_output_topology: establish a valid priority baseline, then return
    the complete kscreen-doctor JSON after validating every restorable field. The
    nested backend can begin with priority 0 on active virtual outputs; the first
    topology write canonicalizes that, so normalize before capture."""
    _require_topology_mutation("mo_capture_output_topology")
    if not os.environ.get("E2E_MO_PRIMARY") or not os.environ.get("E2E_MO_SECONDARY"):
        _ = mo_discover_outputs()
    primary, secondary = _require_env("E2E_MO_PRIMARY"), _require_env("E2E_MO_SECONDARY")

    snapshot = _kscreen_read()
    if snapshot is None:
        raise MultiOutputError(
            "mo_capture_output_topology: kscreen-doctor could not read the nested topology"
        )

    status = _classify_output_priorities(snapshot, primary, secondary)
    if status == 2:
        raise MultiOutputError(
            "mo_capture_output_topology: KScreen returned malformed priority state"
        )
    if status == 1:
        # kscreen-doctor 6.7.3 parses output.<name>.priority.<uint32>. Setting
        # primary then secondary yields the requested 1,2 ordering after
        # Config::adjustPriorities canonicalizes each parsed setter.
        if not _kscreen_set(f"output.{primary}.priority.1", f"output.{secondary}.priority.2"):
            raise MultiOutputError(
                "mo_capture_output_topology: could not normalize output priorities"
            )
        snapshot = _kscreen_read()
        if snapshot is None:
            raise MultiOutputError(
                "mo_capture_output_topology: could not read normalized priorities"
            )

    try:
        _ = _project_output_state(snapshot, primary, secondary)
    except MultiOutputError as err:
        raise MultiOutputError(
            "mo_capture_output_topology: KScreen returned an invalid restorable state"
        ) from err
    return snapshot


def _mo_wait_for_captured_output_topology(captured: str) -> None:
    """Verify cleanup by polling until every restorable field matches and the
    complete KScreen payload is semantically equal. Unhandled state may settle
    asynchronously, but it is never excluded from the final verdict."""
    primary, secondary = _require_env("E2E_MO_PRIMARY"), _require_env("E2E_MO_SECONDARY")
    expected = _project_output_state(captured, primary, secondary)
    drift = ""
    last = ""
    for _ in range(120):
        current = _kscreen_read()
        if current is None:
            raise MultiOutputError(
                "_mo_wait_for_captured_output_topology: kscreen-doctor read failed during restore"
            )
        last = current
        if _project_output_state(current, primary, secondary) == expected:
            status, message = _compare_output_state_semantically(captured, current)
            if status == 0:
                return
            if status == 2:
                raise MultiOutputError(message)
            drift = message
        else:
            drift = "restorable output fields have not settled"
        time.sleep(0.25)
    raise MultiOutputError(
        "_mo_wait_for_captured_output_topology: KScreen did not restore the complete captured "
        f"state: {drift}; last state: {last}"
    )


def mo_restore_output_topology(captured: str) -> None:
    """mo_restore_output_topology: atomically restore both outputs' captured enabled
    state, rotation, scale, position, and priority, then prove the complete captured
    KScreen state is semantically unchanged (including fields without a restore
    setter)."""
    _require_topology_mutation("mo_restore_output_topology")
    if not os.environ.get("E2E_MO_PRIMARY") or not os.environ.get("E2E_MO_SECONDARY"):
        raise MultiOutputError(
            "mo_restore_output_topology: output discovery is missing; refusing an unscoped restore"
        )
    states = _project_output_state(
        captured, _require_env("E2E_MO_PRIMARY"), _require_env("E2E_MO_SECONDARY")
    )
    if len(states) != 2:
        raise MultiOutputError(
            f"mo_restore_output_topology: expected two parsed outputs, got {len(states)}"
        )
    restore_args: list[str] = []
    for state in states:
        restore_args += [
            f"output.{state.name}.{'enable' if state.enabled else 'disable'}",
            f"output.{state.name}.rotation.{state.rotation}",
            f"output.{state.name}.scale.{state.scale:.15g}",
            f"output.{state.name}.position.{state.x},{state.y}",
            f"output.{state.name}.priority.{state.priority}",
        ]
    if not _kscreen_set(*restore_args):
        raise MultiOutputError(
            "mo_restore_output_topology: kscreen-doctor rejected the captured state"
        )
    _mo_wait_for_captured_output_topology(captured)


# ---- rectangle classification (pure) ---------------------------------------


def mo_classify_rectangles(a: Rect, b: Rect) -> str:
    """Classify two non-overlapping output rectangles as full-touching (the entire
    shorter contacting edge overlaps), partial-touching (positive but incomplete
    edge overlap), or disconnected. Overlapping rectangles are outside the
    three-state contract and are refused. Pure."""
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    if aw <= 0 or ah <= 0 or bw <= 0 or bh <= 0:
        raise MultiOutputError("mo_classify_rectangles: rectangle sizes must be positive")
    ar, ab = ax + aw, ay + ah
    br, bb = bx + bw, by + bh
    overlap_x = min(ar, br) - max(ax, bx)
    overlap_y = min(ab, bb) - max(ay, by)
    if overlap_x > 0 and overlap_y > 0:
        raise MultiOutputError(
            "mo_classify_rectangles: output rectangles overlap; topology is outside the "
            "acceptance contract"
        )
    if ar == bx or br == ax:
        contact_overlap, contact_span = overlap_y, min(ah, bh)
    elif ab == by or bb == ay:
        contact_overlap, contact_span = overlap_x, min(aw, bw)
    else:
        return "disconnected"
    if contact_overlap <= 0:
        return "disconnected"
    if contact_overlap == contact_span:
        return "full-touching"
    return "partial-touching"


def _read_rectangles_from(
    screens: list[Screen], primary_name: str, secondary_name: str
) -> tuple[Rect, Rect]:
    """The primary and secondary active output geometries, validating that both are
    active and the ScreenPool primary identity has not changed. Pure."""
    active = {screen.name: screen for screen in screens if screen.is_active}
    primary = active.get(primary_name)
    secondary = active.get(secondary_name)
    if primary is None or secondary is None:
        raise MultiOutputError("_mo_read_output_rectangles: both discovered outputs must be active")
    if not primary.is_primary or secondary.is_primary:
        raise MultiOutputError("_mo_read_output_rectangles: ScreenPool primary identity changed")
    return primary.geometry, secondary.geometry


def _mo_read_output_rectangles() -> tuple[Rect, Rect]:
    """The two discovered active output geometries (primary, secondary)."""
    return _read_rectangles_from(
        _screens(), _require_env("E2E_MO_PRIMARY"), _require_env("E2E_MO_SECONDARY")
    )


def mo_classify_output_topology() -> str:
    """mo_classify_output_topology: classify the actual ScreenPool rectangles."""
    primary_rect, secondary_rect = _mo_read_output_rectangles()
    return mo_classify_rectangles(primary_rect, secondary_rect)


def mo_assert_output_topology(expected: str) -> None:
    """mo_assert_output_topology: require the actual ScreenPool rectangles to have the
    requested classification. A check that can FAIL is what makes it a trustworthy
    tripwire."""
    if expected not in _TOPOLOGY_CLASSES:
        raise MultiOutputError(
            f"mo_assert_output_topology: unsupported classification '{expected}'"
        )
    actual = mo_classify_output_topology()
    if actual != expected:
        raise MultiOutputError(
            f"mo_assert_output_topology: actual topology is '{actual}', expected '{expected}'"
        )


# ---- secondary placement (pure target math + the runtime driver) -----------


def _placement_target(
    requested: str, primary: Rect, secondary_w: int, secondary_h: int
) -> tuple[int, int]:
    """The secondary's target top-left for the requested topology, derived from the
    actual primary rectangle and the rotated secondary size. Pure."""
    px, py, pw, ph = primary
    if requested == "full-touching":
        return px + pw, py
    if requested == "partial-touching":
        shorter_edge = min(ph, secondary_h)
        requested_overlap = shorter_edge // 2
        if requested_overlap <= 0 or requested_overlap >= shorter_edge:
            raise MultiOutputError(
                "mo_place_secondary_for_topology: output heights cannot form a partial contact "
                f"(primary={ph} secondary={secondary_h})"
            )
        return px + pw, py + ph - requested_overlap
    if requested == "disconnected":
        dynamic_gap = pw // 4 if pw // 4 > 0 else 1
        shorter_height = min(ph, secondary_h)
        vertical_offset = shorter_height // 4 if shorter_height // 4 > 0 else 1
        return px + pw + dynamic_gap, py + vertical_offset
    raise MultiOutputError(
        f"mo_place_secondary_for_topology: unsupported classification '{requested}'"
    )


def _mo_wait_for_portrait_secondary() -> tuple[Rect, Rect]:
    """Poll ScreenPool until the primary is landscape and the secondary portrait,
    then return both exact rectangles."""
    last = ""
    for _ in range(120):
        primary, secondary = _mo_read_output_rectangles()
        last = f"primary={primary} secondary={secondary}"
        if primary[2] > primary[3] and secondary[2] < secondary[3]:
            return primary, secondary
        time.sleep(0.25)
    raise MultiOutputError(
        "_mo_wait_for_portrait_secondary: primary must be landscape and secondary portrait after "
        f"rotation; last rectangles '{last}'"
    )


def _mo_wait_for_secondary_geometry(x: int, y: int, w: int, h: int) -> None:
    """Poll screensData until the secondary reports the exact requested rectangle. A
    KScreen-normalized position never becomes a skip or approximate pass; it times
    out as failure."""
    last = ""
    for _ in range(120):
        _primary, secondary = _mo_read_output_rectangles()
        last = str(secondary)
        if secondary == (x, y, w, h):
            return
        time.sleep(0.25)
    raise MultiOutputError(
        f"_mo_wait_for_secondary_geometry: KScreen did not preserve requested geometry "
        f"{x},{y},{w},{h}; last rectangles '{last}'"
    )


def mo_place_secondary_for_topology(requested: str) -> str:
    """mo_place_secondary_for_topology: rotate the discovered secondary left, derive
    its portrait size, position it relative to the actual primary rectangle, poll
    for the exact requested geometry, and verify the resulting classification.
    Returns the accepted secondary rectangle as ``x,y,w,h``."""
    if requested not in _TOPOLOGY_CLASSES:
        raise MultiOutputError(
            f"mo_place_secondary_for_topology: unsupported classification '{requested}'"
        )
    _require_topology_mutation("mo_place_secondary_for_topology")
    if not os.environ.get("E2E_MO_PRIMARY") or not os.environ.get("E2E_MO_SECONDARY"):
        _ = mo_discover_outputs()
    secondary = _require_env("E2E_MO_SECONDARY")

    if not _kscreen_set(f"output.{secondary}.rotation.left"):
        raise MultiOutputError(
            f"mo_place_secondary_for_topology: could not rotate secondary '{secondary}' left"
        )

    primary_rect, secondary_rect = _mo_wait_for_portrait_secondary()
    sw, sh = secondary_rect[2], secondary_rect[3]
    target_x, target_y = _placement_target(requested, primary_rect, sw, sh)

    if not _kscreen_set(
        f"output.{secondary}.rotation.left", f"output.{secondary}.position.{target_x},{target_y}"
    ):
        raise MultiOutputError(
            f"mo_place_secondary_for_topology: KScreen rejected '{requested}' placement "
            f"for '{secondary}'"
        )
    _mo_wait_for_secondary_geometry(target_x, target_y, sw, sh)
    mo_assert_output_topology(requested)
    return f"{target_x},{target_y},{sw},{sh}"


# ---- pin and placement assertions (pure core + runtime read) ---------------


def _assert_pin_resolved(screens: list[Screen], want_id: int, want_name: str) -> None:
    """The pin is queryable: ScreenPool must report ``want_id`` as ACTIVE,
    NON-primary, and named ``want_name``. Pure."""
    found = next((screen for screen in screens if screen.id == want_id), None)
    if found is None:
        raise MultiOutputError(f"pin id {want_id} is not in the ScreenPool mapping")
    if not found.is_active:
        raise MultiOutputError(f"pin id {want_id} ({found.name}) is not active")
    if found.is_primary:
        raise MultiOutputError(
            f"pin id {want_id} ({found.name}) resolved to the PRIMARY - it must be the secondary"
        )
    if found.name != want_name:
        raise MultiOutputError(
            f"pin id {want_id} resolved to {found.name}, expected the secondary {want_name}"
        )


def mo_assert_pin_resolved() -> None:
    """mo_assert_pin_resolved: after staging a 2out cell, ScreenPool must report id
    E2E_MO_SECONDARY_ID as active, non-primary, and named the discovered secondary."""
    _assert_pin_resolved(_screens(), E2E_MO_SECONDARY_ID, _require_env("E2E_MO_SECONDARY"))


def mo_assert_view_on(view: int, expected: str) -> None:
    """mo_assert_view_on: the placement check the HC3 acceptance leans on. Passes IFF
    the view reports sitting on ``expected``. Deliberately a check that can FAIL: fed
    the wrong expected connector it must go red, which is what makes it trustworthy
    for the cross-screen scenarios."""
    try:
        got = mo_view_screen(view)
    except MultiOutputError as err:
        raise MultiOutputError(f"mo_assert_view_on: view {view} not readable") from err
    if got != expected:
        raise MultiOutputError(
            f"mo_assert_view_on: view {view} is on '{got}', expected '{expected}'"
        )
