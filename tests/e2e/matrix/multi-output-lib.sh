# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# The MULTI-OUTPUT discover-and-pin layer for the e2e matrix (C-I2 / P1,
# docs/tracking/e2e-interaction-test-plan.md section 5, open question O7). Sourced by the
# multi-output self-test recipe inside a TWO-output nested vehicle
# (E2E_OUTPUT_COUNT=2, via scripts/run-multi-output-e2e.sh). Depends on
# tests/e2e/lib.sh and tests/e2e/matrix/matrix-lib.sh being sourced first.
#
# It answers O7 - "which Latte screen id / connector maps to which physical
# virtual output" - by READING the running dock's own ScreenPool over D-Bus
# (the screensData readback), never by scraping a log line or hardcoding a
# connector name the compositor is free to change between runs. Then it PINS the
# secondary output to a fixed, documented ScreenPool id so a per-screen fixture
# lands the view deterministically, and it VERIFIES the pin held by the same
# queryable surface.

# The id every 2out fixture pins its secondary view's lastScreen to. ScreenPool
# reads the [ScreenConnectors] group (fixture.py seeds "<id>=<secondary-name>")
# BEFORE it enumerates live outputs, so this id resolves to the secondary
# connector and the primary is assigned the next free id. A FIXED id keeps the
# pin queryable and stable across dock restarts regardless of the compositor's
# output-enumeration order - the exact O7 non-determinism this retires.
E2E_MO_SECONDARY_ID=10

# mo_screens_json: the ScreenPool topology as JSON (the queryable O7 mapping):
# a list of {id, name, geometry:[x,y,w,h], isActive, isPrimary}.
mo_screens_json() { e2e_json screensData; }

# _mo_require_topology_mutation <caller>: refuse unless every ambient-session
# identity proves this is the private two-output nested vehicle. The socket and
# bus-address checks are deliberately stricter than E2E_MODE alone: an exported
# marker in a desk shell must never authorize kscreen-doctor against host
# outputs.
_mo_require_topology_mutation() {
    local -r caller="$1"
    _e2e_require_nested "$caller" || return 2
    if [[ "${E2E_OUTPUT_COUNT:-}" != 2 ]]; then
        echo "$caller: runtime output mutation requires E2E_OUTPUT_COUNT=2, got '${E2E_OUTPUT_COUNT:-unset}'" >&2
        return 2
    fi
    if [[ -z "${E2E_RT:-}" || "${XDG_RUNTIME_DIR:-}" != "$E2E_RT" ]]; then
        echo "$caller: XDG_RUNTIME_DIR must equal the private E2E_RT (XDG_RUNTIME_DIR='${XDG_RUNTIME_DIR:-unset}', E2E_RT='${E2E_RT:-unset}')" >&2
        return 2
    fi
    if [[ -z "${WAYLAND_DISPLAY:-}" || "$WAYLAND_DISPLAY" == */* \
          || ! -S "$E2E_RT/$WAYLAND_DISPLAY" ]]; then
        echo "$caller: no active nested Wayland socket at '$E2E_RT/${WAYLAND_DISPLAY:-unset}'" >&2
        return 2
    fi
    if [[ ! -r "$E2E_RT/bus-address" ]]; then
        echo "$caller: private D-Bus address file is missing at '$E2E_RT/bus-address'" >&2
        return 2
    fi

    local private_bus
    private_bus="$(<"$E2E_RT/bus-address")"
    if [[ -z "$private_bus" || "${DBUS_SESSION_BUS_ADDRESS:-}" != "$private_bus" ]]; then
        echo "$caller: ambient D-Bus does not match the nested vehicle's private bus" >&2
        return 2
    fi
    if ! busctl --user --no-pager list >/dev/null; then
        echo "$caller: the nested vehicle's private D-Bus is not responding" >&2
        return 2
    fi
}

# mo_view_screen <view-id>: the connector NAME the view currently sits on, from
# viewsData.screen (positioner()->currentScreenName()).
mo_view_screen() {
    local id="$1"
    { echo "$id"; e2e_json viewsData; } | python3 -c '
import json, sys
view = int(sys.stdin.readline())
views = json.load(sys.stdin)
v = next((v for v in views if v["containmentId"] == view), None)
if v is None:
    sys.exit("mo_view_screen: view %d not present" % view)
print(v["screen"])
'
}

# mo_discover_outputs: read the running dock's ScreenPool, identify the primary
# and the single active SECONDARY output, and export the pin parameters:
#   E2E_MO_PRIMARY          primary connector name
#   E2E_MO_SECONDARY        secondary connector name (the 2out placement target)
#   E2E_MO_SECONDARY_GEOM   secondary geometry as Latte's "x,y WxH" rect string
#   E2E_MO_SECONDARY_ID     the id 2out fixtures pin to (E2E_MO_SECONDARY_ID)
# Refuses loudly (return 1) if the vehicle is not actually dual-output, or if
# ScreenPool's primary disagrees with where the default onPrimary view landed -
# a discovery failure to surface, never a guessed connector name.
mo_discover_outputs() {
    _e2e_require_nested mo_discover_outputs || return 2
    local screens primary_view parsed
    screens="$(mo_screens_json)" || { echo "mo_discover_outputs: screensData query failed" >&2; return 1; }
    #! cross-check anchor: where the default onPrimary view actually landed is
    #! the ground truth for "which output is primary". If ScreenPool's own
    #! primary flag disagrees, the discovery is untrustworthy - surfaced, not
    #! papered over.
    primary_view="$(e2e_json viewsData | python3 -c '
import json, sys
views = [v for v in json.load(sys.stdin) if not v.get("isCloned")]
print(views[0]["screen"] if views else "")
')"

    parsed="$({ echo "$screens"; } | python3 -c '
import json, sys
screens = json.load(sys.stdin)
primary_view_screen = sys.argv[1]

active = [s for s in screens if s["isActive"]]
if len(active) != 2:
    sys.exit("expected exactly 2 active outputs under the dual vehicle, saw %d: %s"
             % (len(active), [s["name"] for s in active]))

primaries = [s for s in active if s["isPrimary"]]
if len(primaries) != 1:
    sys.exit("expected exactly 1 primary among the active outputs, saw %d: %s"
             % (len(primaries), [s["name"] for s in primaries]))
primary = primaries[0]
secondary = next(s for s in active if not s["isPrimary"])

if primary_view_screen and primary_view_screen != primary["name"]:
    sys.exit("ScreenPool reports primary=%s but the onPrimary view is on %s (discovery inconsistent)"
             % (primary["name"], primary_view_screen))

g = secondary["geometry"]
print("%s\t%s\t%d,%d %dx%d" % (primary["name"], secondary["name"], g[0], g[1], g[2], g[3]))
' "$primary_view")" || { echo "mo_discover_outputs: $parsed" >&2; return 1; }

    IFS=$'\t' read -r E2E_MO_PRIMARY E2E_MO_SECONDARY E2E_MO_SECONDARY_GEOM <<<"$parsed"
    export E2E_MO_PRIMARY E2E_MO_SECONDARY E2E_MO_SECONDARY_GEOM
    export E2E_MO_SECONDARY_ID
    echo "mo_discover_outputs: primary=$E2E_MO_PRIMARY secondary=$E2E_MO_SECONDARY" \
         "(pinned to ScreenPool id $E2E_MO_SECONDARY_ID) geom='$E2E_MO_SECONDARY_GEOM'"
    return 0
}

# _mo_project_output_state <kscreen-json>: validate the two discovered outputs
# and print the fields this helper can restore as tab-separated
# name/enabled/rotation/scale/x/y/priority rows. This projection builds only
# documented kscreen-doctor setters. Full-state verification is a separate
# semantic comparison so fields without a documented setter cannot drift
# silently.
_mo_project_output_state() {
    local -r state="$1"
    KSCREEN_STATE="$state" python3 - "$E2E_MO_PRIMARY" "$E2E_MO_SECONDARY" <<'PY'
import json
import os
import sys

rotation_tokens = {
    1: "none",
    2: "left",
    4: "inverted",
    8: "right",
    16: "flipped",
    32: "flipped90",
    64: "flipped180",
    128: "flipped270",
}
names = sys.argv[1:3]
payload = json.loads(os.environ["KSCREEN_STATE"])
raw_outputs = payload.get("outputs")
if not isinstance(raw_outputs, list) or len(raw_outputs) != 2:
    raise SystemExit("expected exactly two KScreen outputs")
if not all(isinstance(output, dict) for output in raw_outputs):
    raise SystemExit("every KScreen output must be an object")
outputs = {output.get("name"): output for output in raw_outputs}
if len(outputs) != len(raw_outputs):
    raise SystemExit("KScreen output names are not unique")
rows = []
priorities = []
for name in names:
    output = outputs.get(name)
    if output is None:
        raise SystemExit(f"ScreenPool output {name!r} is absent from KScreen")
    rotation = rotation_tokens.get(output.get("rotation"))
    if rotation is None:
        raise SystemExit(f"output {name!r} has unsupported rotation")
    scale = output.get("scale")
    enabled = output.get("enabled")
    pos = output.get("pos")
    priority = output.get("priority")
    if (
        isinstance(scale, bool)
        or not isinstance(scale, (int, float))
        or scale <= 0
    ):
        raise SystemExit(f"output {name!r} has invalid scale {scale!r}")
    if not isinstance(enabled, bool):
        raise SystemExit(f"output {name!r} has invalid enabled state")
    if not isinstance(pos, dict) or not all(
        type(pos.get(axis)) is int for axis in ("x", "y")
    ):
        raise SystemExit(f"output {name!r} has invalid position")
    if type(priority) is not int or not 1 <= priority <= 100:
        raise SystemExit(f"output {name!r} has invalid priority {priority!r}")
    if any(character.isspace() for character in name):
        raise SystemExit(f"whitespace in output name {name!r} is unsupported")
    state = "enable" if enabled else "disable"
    priorities.append(priority)
    rows.append(
        f"{name}\t{state}\t{rotation}\t{scale:.15g}\t"
        f"{pos['x']}\t{pos['y']}\t{priority}"
    )
if sorted(priorities) != [1, 2]:
    raise SystemExit(
        f"the two active outputs need continuous unique priorities, got {priorities!r}"
    )
print("\n".join(rows))
PY
}

# _mo_classify_output_priorities <kscreen-json>: return 0 when the two active
# discovered outputs already have KScreen's canonical unique priorities 1 and
# 2, return 1 when the private vehicle needs baseline normalization, and return
# 2 for malformed state. KScreen::Config::adjustPriorities defines enabled
# priorities as continuous from 1; priority 0 is unassigned/disabled state.
_mo_classify_output_priorities() {
    local -r state="$1"
    KSCREEN_STATE="$state" python3 - "$E2E_MO_PRIMARY" "$E2E_MO_SECONDARY" <<'PY'
import json
import os
import sys

try:
    payload = json.loads(os.environ["KSCREEN_STATE"])
    raw_outputs = payload.get("outputs")
    if not isinstance(raw_outputs, list) or len(raw_outputs) != 2:
        raise ValueError("expected exactly two KScreen outputs")
    if not all(isinstance(output, dict) for output in raw_outputs):
        raise ValueError("every KScreen output must be an object")
    outputs = {output.get("name"): output for output in raw_outputs}
    if len(outputs) != len(raw_outputs):
        raise ValueError("KScreen output names are not unique")

    priorities = []
    for name in sys.argv[1:3]:
        output = outputs.get(name)
        if output is None:
            raise ValueError(f"ScreenPool output {name!r} is absent from KScreen")
        if output.get("enabled") is not True:
            raise ValueError(
                f"ScreenPool output {name!r} is not enabled before capture"
            )
        priority = output.get("priority")
        if type(priority) is not int or not 0 <= priority <= 100:
            raise ValueError(f"output {name!r} has invalid priority {priority!r}")
        priorities.append(priority)
except (KeyError, json.JSONDecodeError, ValueError) as error:
    print(f"_mo_classify_output_priorities: {error}", file=sys.stderr)
    raise SystemExit(2)

raise SystemExit(0 if sorted(priorities) == [1, 2] else 1)
PY
}

# _mo_compare_output_state_semantically <captured-json> <current-json>:
# compare the complete KScreen payload without relying on JSON byte order.
# Outputs are an identity-keyed collection, and each output's modes are an
# identity-keyed collection. No value or field is excluded. Fields the helper
# cannot restore, including mode, priority, capability, and future fields, must
# remain unchanged or cleanup fails loudly.
_mo_compare_output_state_semantically() {
    local -r captured="$1"
    local -r current="$2"
    KSCREEN_CAPTURED_STATE="$captured" KSCREEN_CURRENT_STATE="$current" \
        python3 - <<'PY'
import json
import os
import sys


def parse_state(variable, label):
    try:
        payload = json.loads(os.environ[variable])
    except (KeyError, json.JSONDecodeError) as error:
        raise ValueError(f"{label} KScreen state is not valid JSON: {error}") from error
    if not isinstance(payload, dict):
        raise ValueError(f"{label} KScreen state must be a JSON object")
    return payload


def sort_identity_collection(records, identity, path):
    if not isinstance(records, list):
        raise ValueError(f"{path} must be a JSON array")
    identities = []
    for index, record in enumerate(records):
        if not isinstance(record, dict):
            raise ValueError(f"{path}[{index}] must be a JSON object")
        value = record.get(identity)
        if isinstance(value, bool) or not isinstance(value, (str, int)):
            raise ValueError(
                f"{path}[{index}].{identity} must be a string or integer"
            )
        identities.append(value)
    if len(set((type(value).__name__, value) for value in identities)) != len(
        identities
    ):
        raise ValueError(f"{path} has duplicate {identity} values")
    return [
        record
        for _, record in sorted(
            zip(identities, records),
            key=lambda item: (type(item[0]).__name__, str(item[0])),
        )
    ]


def canonicalize(payload, label):
    outputs = sort_identity_collection(
        payload.get("outputs"), "name", f"{label}.outputs"
    )
    canonical = dict(payload)
    canonical_outputs = []
    for output in outputs:
        canonical_output = dict(output)
        if "modes" in canonical_output:
            canonical_output["modes"] = sort_identity_collection(
                canonical_output["modes"],
                "id",
                f"{label}.outputs[{output['name']!r}].modes",
            )
        canonical_outputs.append(canonical_output)
    canonical["outputs"] = canonical_outputs
    return canonical


def numbers_equal(left, right):
    return (
        not isinstance(left, bool)
        and not isinstance(right, bool)
        and isinstance(left, (int, float))
        and isinstance(right, (int, float))
        and left == right
    )


def first_difference(left, right, path="$"):
    if numbers_equal(left, right):
        return None
    if type(left) is not type(right):
        return (
            f"{path}: type changed from {type(left).__name__} "
            f"to {type(right).__name__}"
        )
    if isinstance(left, dict):
        left_keys = set(left)
        right_keys = set(right)
        if left_keys != right_keys:
            removed = sorted(left_keys - right_keys)
            added = sorted(right_keys - left_keys)
            return f"{path}: fields removed={removed!r} added={added!r}"
        for key in sorted(left):
            difference = first_difference(left[key], right[key], f"{path}.{key}")
            if difference is not None:
                return difference
        return None
    if isinstance(left, list):
        if len(left) != len(right):
            return f"{path}: list length changed from {len(left)} to {len(right)}"
        for index, (captured_value, current_value) in enumerate(zip(left, right)):
            difference = first_difference(
                captured_value, current_value, f"{path}[{index}]"
            )
            if difference is not None:
                return difference
        return None
    if left != right:
        return f"{path}: changed from {left!r} to {right!r}"
    return None


try:
    captured = canonicalize(
        parse_state("KSCREEN_CAPTURED_STATE", "captured"), "captured"
    )
    current = canonicalize(
        parse_state("KSCREEN_CURRENT_STATE", "current"), "current"
    )
except ValueError as error:
    print(f"_mo_compare_output_state_semantically: {error}", file=sys.stderr)
    raise SystemExit(2)

difference = first_difference(captured, current)
if difference is not None:
    print(
        "_mo_compare_output_state_semantically: "
        f"complete KScreen state drifted at {difference}",
        file=sys.stderr,
    )
    raise SystemExit(1)
PY
}

# mo_capture_output_topology: establish a valid priority baseline, then print
# complete current kscreen-doctor JSON after validating every field this helper
# can restore. The nested backend can begin with priority 0 on active virtual
# outputs, but the first topology write canonicalizes that state. Normalize
# before capture so priority belongs to the reversible transaction instead of
# becoming an impossible post-mutation restoration target.
mo_capture_output_topology() {
    _mo_require_topology_mutation mo_capture_output_topology || return 2
    if [[ -z "${E2E_MO_PRIMARY:-}" || -z "${E2E_MO_SECONDARY:-}" ]]; then
        mo_discover_outputs >&2 || return 1
    fi

    local snapshot
    if ! snapshot="$(kscreen-doctor -j)"; then
        echo "mo_capture_output_topology: kscreen-doctor could not read the nested topology" >&2
        return 1
    fi

    local priority_status=0
    if _mo_classify_output_priorities "$snapshot"; then
        priority_status=0
    else
        priority_status=$?
    fi
    if (( priority_status == 2 )); then
        echo "mo_capture_output_topology: KScreen returned malformed priority state" >&2
        return 1
    fi
    if (( priority_status == 1 )); then
        # kscreen-doctor 6.7.3 parses output.<name>.priority.<uint32>.
        # Setting primary then secondary yields the requested 1,2 ordering
        # after Config::adjustPriorities canonicalizes each parsed setter.
        if ! kscreen-doctor \
            "output.${E2E_MO_PRIMARY}.priority.1" \
            "output.${E2E_MO_SECONDARY}.priority.2" >/dev/null; then
            echo "mo_capture_output_topology: could not normalize output priorities" >&2
            return 1
        fi
        if ! snapshot="$(kscreen-doctor -j)"; then
            echo "mo_capture_output_topology: could not read normalized priorities" >&2
            return 1
        fi
    fi
    if ! _mo_project_output_state "$snapshot" >/dev/null; then
        echo "mo_capture_output_topology: KScreen returned an invalid restorable state" >&2
        return 1
    fi
    printf '%s\n' "$snapshot"
}

# _mo_wait_for_captured_output_topology <captured-json>: verify cleanup by
# polling until every restorable field matches and the complete KScreen payload
# is semantically equal. Unhandled state may settle asynchronously, but it is
# never excluded from the final verdict.
_mo_wait_for_captured_output_topology() {
    local -r captured="$1"
    local expected current current_projection semantic_drift=""
    local comparison_status=0
    expected="$(_mo_project_output_state "$captured")" || return 1
    local i
    for ((i = 0; i < 120; ++i)); do
        if ! current="$(kscreen-doctor -j)"; then
            echo "_mo_wait_for_captured_output_topology: kscreen-doctor read failed during restore" >&2
            return 1
        fi
        current_projection="$(_mo_project_output_state "$current")" || return 1
        if [[ "$current_projection" == "$expected" ]]; then
            if semantic_drift="$(
                _mo_compare_output_state_semantically "$captured" "$current" 2>&1
            )"; then
                return 0
            else
                comparison_status=$?
            fi
            if (( comparison_status == 2 )); then
                printf '%s\n' "$semantic_drift" >&2
                return 1
            fi
        else
            semantic_drift="restorable output fields have not settled"
        fi
        sleep 0.25
    done
    printf '%s\n' \
        "_mo_wait_for_captured_output_topology: KScreen did not restore the complete captured state: $semantic_drift" >&2
    printf '%s\n' "_mo_wait_for_captured_output_topology: last state:" >&2
    printf '%s\n' "$current" >&2
    return 1
}

# mo_restore_output_topology <captured-json>: atomically restore both outputs'
# captured enabled state, rotation, scale, position, and priority. These are
# every field this harness can mutate directly or through KScreen
# canonicalization. Then prove the complete captured KScreen state is
# semantically unchanged, including every field without a restore setter.
mo_restore_output_topology() {
    local -r captured="$1"
    _mo_require_topology_mutation mo_restore_output_topology || return 2
    if [[ -z "${E2E_MO_PRIMARY:-}" || -z "${E2E_MO_SECONDARY:-}" ]]; then
        echo "mo_restore_output_topology: output discovery is missing; refusing an unscoped restore" >&2
        return 2
    fi

    local parsed
    parsed="$(_mo_project_output_state "$captured")" || return 1

    local -a restore_args=()
    local name enabled rotation scale x y priority
    local restored_count=0
    while IFS=$'\t' read -r name enabled rotation scale x y priority; do
        if [[ -z "$name" || -z "$enabled" || -z "$rotation" || -z "$scale" \
              || -z "$x" || -z "$y" || -z "$priority" ]]; then
            echo "mo_restore_output_topology: incomplete parsed output state" >&2
            return 1
        fi
        restore_args+=(
            "output.${name}.${enabled}"
            "output.${name}.rotation.${rotation}"
            "output.${name}.scale.${scale}"
            "output.${name}.position.${x},${y}"
            "output.${name}.priority.${priority}"
        )
        restored_count=$((restored_count + 1))
    done <<<"$parsed"
    if (( restored_count != 2 )); then
        echo "mo_restore_output_topology: expected two parsed outputs, got $restored_count" >&2
        return 1
    fi
    if ! kscreen-doctor "${restore_args[@]}" >/dev/null; then
        echo "mo_restore_output_topology: kscreen-doctor rejected the captured state" >&2
        return 1
    fi
    _mo_wait_for_captured_output_topology "$captured"
}

# mo_classify_rectangles <ax> <ay> <aw> <ah> <bx> <by> <bw> <bh>: classify
# two non-overlapping output rectangles as full-touching (the entire shorter
# contacting edge overlaps), partial-touching (positive but incomplete edge
# overlap), or disconnected. Geometry classification never implies Latte
# reservation membership: output identity plus dock edge owns that authority.
# Overlapping output rectangles are outside this three-state contract and fail.
mo_classify_rectangles() {
    if (( $# != 8 )); then
        echo "mo_classify_rectangles: expected 8 rectangle fields, got $#" >&2
        return 2
    fi
    local value
    for value in "$@"; do
        if [[ ! "$value" =~ ^-?[0-9]+$ ]]; then
            echo "mo_classify_rectangles: '$value' is not an integer rectangle field" >&2
            return 2
        fi
    done

    local -r ax="$1" ay="$2" aw="$3" ah="$4"
    local -r bx="$5" by="$6" bw="$7" bh="$8"
    if (( aw <= 0 || ah <= 0 || bw <= 0 || bh <= 0 )); then
        echo "mo_classify_rectangles: rectangle sizes must be positive" >&2
        return 2
    fi
    local -r ar=$((ax + aw)) ab=$((ay + ah))
    local -r br=$((bx + bw)) bb=$((by + bh))
    local overlap_x overlap_y
    overlap_x=$(( (ar < br ? ar : br) - (ax > bx ? ax : bx) ))
    overlap_y=$(( (ab < bb ? ab : bb) - (ay > by ? ay : by) ))

    if (( overlap_x > 0 && overlap_y > 0 )); then
        echo "mo_classify_rectangles: output rectangles overlap; topology is outside the acceptance contract" >&2
        return 1
    fi

    local contact_overlap contact_span
    if (( ar == bx || br == ax )); then
        contact_overlap="$overlap_y"
        contact_span=$((ah < bh ? ah : bh))
    elif (( ab == by || bb == ay )); then
        contact_overlap="$overlap_x"
        contact_span=$((aw < bw ? aw : bw))
    else
        echo disconnected
        return 0
    fi

    if (( contact_overlap <= 0 )); then
        echo disconnected
    elif (( contact_overlap == contact_span )); then
        echo full-touching
    else
        echo partial-touching
    fi
}

# _mo_read_output_rectangles: print both discovered active output geometries as
# eight tab-separated integers: primary x/y/w/h then secondary x/y/w/h.
_mo_read_output_rectangles() {
    local screens
    if ! screens="$(mo_screens_json)"; then
        echo "_mo_read_output_rectangles: screensData query failed" >&2
        return 1
    fi
    SCREENS_JSON="$screens" python3 - "$E2E_MO_PRIMARY" "$E2E_MO_SECONDARY" <<'PY'
import json
import os
import sys

primary_name, secondary_name = sys.argv[1:3]
active = {
    screen["name"]: screen
    for screen in json.loads(os.environ["SCREENS_JSON"])
    if screen["isActive"]
}
primary = active.get(primary_name)
secondary = active.get(secondary_name)
if primary is None or secondary is None:
    raise SystemExit(
        "_mo_read_output_rectangles: both discovered outputs must be active"
    )
if not primary["isPrimary"] or secondary["isPrimary"]:
    raise SystemExit(
        "_mo_read_output_rectangles: ScreenPool primary identity changed"
    )
geometry = primary["geometry"] + secondary["geometry"]
if len(geometry) != 8 or not all(isinstance(value, int) for value in geometry):
    raise SystemExit("_mo_read_output_rectangles: malformed screen geometry")
print("\t".join(str(value) for value in geometry))
PY
}

# mo_classify_output_topology: classify the actual ScreenPool rectangles.
mo_classify_output_topology() {
    local fields
    fields="$(_mo_read_output_rectangles)" || return 1
    local -a geometry=()
    IFS=$'\t' read -r -a geometry <<<"$fields"
    mo_classify_rectangles "${geometry[@]}"
}

# mo_assert_output_topology <classification>: require the actual ScreenPool
# rectangles to have the requested classification.
mo_assert_output_topology() {
    local -r expected="$1"
    case "$expected" in
        full-touching|partial-touching|disconnected) ;;
        *)
            echo "mo_assert_output_topology: unsupported classification '$expected'" >&2
            return 2
            ;;
    esac
    local actual
    actual="$(mo_classify_output_topology)" || return 1
    if [[ "$actual" != "$expected" ]]; then
        echo "mo_assert_output_topology: actual topology is '$actual', expected '$expected'" >&2
        return 1
    fi
}

# _mo_wait_for_portrait_secondary: poll ScreenPool until the primary is
# landscape and the secondary is portrait, then print both exact rectangles.
_mo_wait_for_portrait_secondary() {
    local fields=""
    local i
    for ((i = 0; i < 120; ++i)); do
        fields="$(_mo_read_output_rectangles)" || return 1
        local -a geometry=()
        IFS=$'\t' read -r -a geometry <<<"$fields"
        if (( geometry[2] > geometry[3] && geometry[6] < geometry[7] )); then
            printf '%s\n' "$fields"
            return 0
        fi
        sleep 0.25
    done
    echo "_mo_wait_for_portrait_secondary: primary must be landscape and secondary portrait after rotation; last rectangles '$fields'" >&2
    return 1
}

# _mo_wait_for_secondary_geometry <x> <y> <w> <h>: poll screensData until the
# secondary reports the exact requested rectangle. A KScreen-normalized
# position never becomes a skip or approximate pass; it times out as failure.
_mo_wait_for_secondary_geometry() {
    local -r expected_x="$1" expected_y="$2" expected_w="$3" expected_h="$4"
    local fields=""
    local i
    for ((i = 0; i < 120; ++i)); do
        fields="$(_mo_read_output_rectangles)" || return 1
        local -a geometry=()
        IFS=$'\t' read -r -a geometry <<<"$fields"
        if (( geometry[4] == expected_x && geometry[5] == expected_y \
              && geometry[6] == expected_w && geometry[7] == expected_h )); then
            return 0
        fi
        sleep 0.25
    done
    echo "_mo_wait_for_secondary_geometry: KScreen did not preserve requested geometry ${expected_x},${expected_y},${expected_w},${expected_h}; last rectangles '$fields'" >&2
    return 1
}

# mo_place_secondary_for_topology <full-touching|partial-touching|disconnected>:
# rotate the discovered secondary left, dynamically derive its portrait size,
# position it relative to the actual primary rectangle, poll screensData for
# the exact requested geometry, and verify the resulting classification. The
# disconnected fixture uses both a derived horizontal gap and vertical offset.
# Prints the accepted secondary rectangle as x,y,w,h.
mo_place_secondary_for_topology() {
    local -r requested="$1"
    case "$requested" in
        full-touching|partial-touching|disconnected) ;;
        *)
            echo "mo_place_secondary_for_topology: unsupported classification '$requested'" >&2
            return 2
            ;;
    esac
    _mo_require_topology_mutation mo_place_secondary_for_topology || return 2
    if [[ -z "${E2E_MO_PRIMARY:-}" || -z "${E2E_MO_SECONDARY:-}" ]]; then
        mo_discover_outputs >&2 || return 1
    fi

    if ! kscreen-doctor "output.${E2E_MO_SECONDARY}.rotation.left" >/dev/null; then
        echo "mo_place_secondary_for_topology: could not rotate secondary '$E2E_MO_SECONDARY' left" >&2
        return 1
    fi

    local fields
    fields="$(_mo_wait_for_portrait_secondary)" || return 1
    local -a geometry=()
    IFS=$'\t' read -r -a geometry <<<"$fields"
    local -r px="${geometry[0]}" py="${geometry[1]}"
    local -r pw="${geometry[2]}" ph="${geometry[3]}"
    local -r sw="${geometry[6]}" sh="${geometry[7]}"
    local target_x target_y
    case "$requested" in
        full-touching)
            target_x=$((px + pw))
            target_y="$py"
            ;;
        partial-touching)
            local -r shorter_edge=$((ph < sh ? ph : sh))
            local -r requested_overlap=$((shorter_edge / 2))
            if (( requested_overlap <= 0 || requested_overlap >= shorter_edge )); then
                echo "mo_place_secondary_for_topology: output heights cannot form a partial contact (primary=$ph secondary=$sh)" >&2
                return 1
            fi
            target_x=$((px + pw))
            target_y=$((py + ph - requested_overlap))
            ;;
        disconnected)
            local -r dynamic_gap=$((pw / 4 > 0 ? pw / 4 : 1))
            local -r shorter_height=$((ph < sh ? ph : sh))
            local -r vertical_offset=$((shorter_height / 4 > 0 ? shorter_height / 4 : 1))
            target_x=$((px + pw + dynamic_gap))
            target_y=$((py + vertical_offset))
            ;;
    esac

    if ! kscreen-doctor \
        "output.${E2E_MO_SECONDARY}.rotation.left" \
        "output.${E2E_MO_SECONDARY}.position.${target_x},${target_y}" >/dev/null; then
        echo "mo_place_secondary_for_topology: KScreen rejected '$requested' placement for '$E2E_MO_SECONDARY'" >&2
        return 1
    fi
    _mo_wait_for_secondary_geometry "$target_x" "$target_y" "$sw" "$sh" || return 1
    mo_assert_output_topology "$requested" || return 1
    printf '%s,%s,%s,%s\n' "$target_x" "$target_y" "$sw" "$sh"
}

# mo_assert_pin_resolved: the pin is queryable - after staging a 2out cell,
# ScreenPool must report id E2E_MO_SECONDARY_ID as ACTIVE, NON-primary, and
# named E2E_MO_SECONDARY. This is the "PIN it (a queryable mapping the harness
# reads)" half of O7: the discover step chose the id, this proves the running
# dock resolved it to the intended output.
mo_assert_pin_resolved() {
    { mo_screens_json; } | python3 -c '
import json, sys
screens = json.load(sys.stdin)
want_id = int(sys.argv[1])
want_name = sys.argv[2]
s = next((s for s in screens if s["id"] == want_id), None)
if s is None:
    sys.exit("pin id %d is not in the ScreenPool mapping" % want_id)
if not s["isActive"]:
    sys.exit("pin id %d (%s) is not active" % (want_id, s["name"]))
if s["isPrimary"]:
    sys.exit("pin id %d (%s) resolved to the PRIMARY - it must be the secondary" % (want_id, s["name"]))
if s["name"] != want_name:
    sys.exit("pin id %d resolved to %s, expected the secondary %s" % (want_id, s["name"], want_name))
print("ok")
' "$E2E_MO_SECONDARY_ID" "$E2E_MO_SECONDARY" >/dev/null
}

# mo_assert_view_on <view-id> <expected-connector>: the placement check the HC3
# acceptance leans on. Returns 0 IFF the view reports sitting on <expected>.
# This is deliberately a check that can FAIL: fed the wrong expected connector
# it must go red (proven in the self-test), which is what makes it trustworthy
# for the cross-screen F5/A4 scenarios - a check that only ever passes cannot
# catch a view that went to the wrong output.
mo_assert_view_on() {
    local id="$1" expected="$2" got
    got="$(mo_view_screen "$id")" || { echo "mo_assert_view_on: view $id not readable" >&2; return 1; }
    if [[ "$got" == "$expected" ]]; then
        return 0
    fi
    echo "mo_assert_view_on: view $id is on '$got', expected '$expected'" >&2
    return 1
}
