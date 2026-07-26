#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# FP-4C deterministic linked-dock operation storm. The shell is only the
# private-session transaction and D-Bus adapter. The typed plan, resolved
# identities and every semantic state verdict live in operation_model.py.
# e2e-mode: nested-only
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-multi-output-e2e.sh}/tests/e2e/lib.sh"

readonly MODEL="$E2E_REPO/tests/e2e/fixtures/fp4c/operation_model.py"
readonly FIXTURE_GENERATOR="$E2E_REPO/tests/e2e/matrix/fixture.py"
readonly requested_seed="${LATTE_LINKED_STRESS_SEED:-127934575}"
readonly supplied_plan="${LATTE_LINKED_STRESS_PLAN:-}"

acceptance_completed=false
backup_ready=false
transaction_started=false
cleanup_failed=0
transaction_dir=""
backup_dir=""
fixture_dir=""
artifact_dir=""
plan_file=""
operations_file=""
replay_file=""
bindings_file=""
outputs_file=""
baseline_snapshot_file=""
baseline_projection_file=""
candidate_plan=""

snapshot() {
    e2e_json dockSystemData
}

assert_no_pending_view_move() {
    local destination="$1"
    e2e_json viewMoveTransactionsData >"$destination" \
        || return 1
    python3 - "$destination" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    state = json.load(stream)
if set(state) != {"schemaVersion", "transactions"}:
    raise SystemExit("durable move readback has missing or surplus fields")
if state["schemaVersion"] != 1:
    raise SystemExit("durable move readback schema changed")
if state["transactions"] != []:
    raise SystemExit(
        "operation checkpoint retained a pending durable move: "
        + json.dumps(state["transactions"], sort_keys=True)
    )
PY
}

capture_snapshot() {
    local destination="$1"
    local candidate="${destination}.next"
    if snapshot >"$candidate" && [[ -s "$candidate" ]]; then
        mv -- "$candidate" "$destination"
        return 0
    fi
    rm -f -- "$candidate"
    return 1
}

path_is_within() {
    local child parent
    child="$(realpath -m -- "$1")" || return 1
    parent="$(realpath -m -- "$2")" || return 1
    [[ "$child" == "$parent"/* ]]
}

require_private_nested_session() {
    local runtime_real config_real bus_file
    _e2e_require_nested linked-dock-operation-stress \
        || e2e_fail "FP-4C operation stress is nested-only"
    [[ "${E2E_OUTPUT_COUNT:-0}" -eq 2 ]] \
        || e2e_fail "FP-4C operation stress requires exactly two nested outputs"
    [[ -n "${E2E_RT:-}" && -d "$E2E_RT" ]] \
        || e2e_fail "FP-4C has no private nested runtime directory"
    runtime_real="$(realpath -e -- "$E2E_RT")" \
        || e2e_fail "FP-4C could not resolve its nested runtime directory"
    [[ "$(realpath -e -- "${XDG_RUNTIME_DIR:-/missing}")" == "$runtime_real" ]] \
        || e2e_fail "FP-4C XDG_RUNTIME_DIR is not the nested runtime"
    config_real="$(realpath -e -- "${E2E_CONFIG_HOME:-/missing}")" \
        || e2e_fail "FP-4C could not resolve its configuration home"
    [[ "$config_real" == "$runtime_real"/* ]] \
        || e2e_fail "FP-4C configuration home is not private to the nested runtime"
    path_is_within "${E2E_LAYOUT:?}" "$E2E_CONFIG_HOME" \
        || e2e_fail "FP-4C layout is outside the private configuration home"
    path_is_within "${E2E_DOCK_PIDFILE:?}" "$E2E_RT" \
        || e2e_fail "FP-4C dock pid file is outside the nested runtime"
    path_is_within "${E2E_DOCK_LOG:?}" "$E2E_RT" \
        || e2e_fail "FP-4C dock log is outside the nested runtime"
    [[ -S "$E2E_RT/${WAYLAND_DISPLAY:?}" ]] \
        || e2e_fail "FP-4C Wayland socket is not in the nested runtime"
    bus_file="$E2E_RT/bus-address"
    [[ -f "$bus_file"
       && "$(cat "$bus_file")" == "${DBUS_SESSION_BUS_ADDRESS:-}" ]] \
        || e2e_fail "FP-4C D-Bus address is not the nested session bus"
    [[ -f "$MODEL" && -f "$FIXTURE_GENERATOR" ]] \
        || e2e_fail "FP-4C model or fixture generator is missing"
}

dock_is_running() {
    local pid
    pid="$(e2e_dock_pid)" || return 1
    [[ "$pid" =~ ^[1-9][0-9]*$ ]] || return 1
    kill -0 "$pid" 2>/dev/null
}

stop_dock_if_running() {
    if dock_is_running; then
        e2e_dock_stop
    else
        return 0
    fi
}

restore_config_exactly() {
    [[ "$backup_ready" == true ]] || return 1
    path_is_within "$E2E_CONFIG_HOME" "$E2E_RT" || return 1
    path_is_within "$backup_dir" "$E2E_RT" || return 1
    rm -rf -- "$E2E_CONFIG_HOME" || return 1
    mkdir -p -- "$E2E_CONFIG_HOME" || return 1
    cp -a -- "$backup_dir/." "$E2E_CONFIG_HOME/" || return 1
    diff -qr --no-dereference "$backup_dir" "$E2E_CONFIG_HOME"
}

cleanup() {
    local original_status=$? pid=""
    local dock_stopped=false config_safe_to_start=false
    trap - EXIT INT TERM

    if [[ "$transaction_started" == true ]]; then
        if [[ -n "$artifact_dir" && -f "$E2E_DOCK_LOG" ]] \
            && ! cp -- "$E2E_DOCK_LOG" "$artifact_dir/fixture-dock.log"; then
            echo "FAIL: FP-4C cleanup could not preserve the fixture dock log" >&2
            cleanup_failed=1
        fi
        if ! stop_dock_if_running; then
            echo "FAIL: FP-4C cleanup could not stop the fixture dock" >&2
            cleanup_failed=1
        fi
        pid="$(e2e_dock_pid 2>/dev/null)" || pid=""
        if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
            echo "FAIL: FP-4C cleanup left fixture dock pid $pid running" >&2
            cleanup_failed=1
        else
            dock_stopped=true
        fi

        if [[ "$backup_ready" == true && "$dock_stopped" == true ]]; then
            if ! restore_config_exactly; then
                echo "FAIL: FP-4C cleanup could not recursively restore the pristine configuration" >&2
                cleanup_failed=1
            else
                config_safe_to_start=true
            fi
        elif [[ "$backup_ready" == true ]]; then
            echo "FAIL: FP-4C cleanup refused to replace configuration under a live fixture dock" >&2
            cleanup_failed=1
        elif [[ "$dock_stopped" == true ]]; then
            # No fixture mutation occurs before the exact backup is complete.
            config_safe_to_start=true
        fi

        if [[ "$dock_stopped" == true
           && "$config_safe_to_start" == true ]] \
            && ! dock_is_running; then
            if ! e2e_dock_start 90; then
                echo "FAIL: FP-4C cleanup could not restart the pristine nested dock" >&2
                cleanup_failed=1
            fi
        fi
        if [[ "$backup_ready" == true
           && "$config_safe_to_start" == true ]] \
            && dock_is_running; then
            if ! snapshot >"$artifact_dir/cleanup-baseline.json"; then
                echo "FAIL: FP-4C cleanup could not capture the restored runtime baseline" >&2
                cleanup_failed=1
            elif ! python3 "$MODEL" assert-baseline \
                <"$artifact_dir/cleanup-baseline.json" >/dev/null; then
                echo "FAIL: FP-4C cleanup did not restore the pristine runtime baseline" >&2
                cleanup_failed=1
            elif ! python3 "$MODEL" durable-projection \
                <"$artifact_dir/cleanup-baseline.json" \
                >"$artifact_dir/cleanup-baseline.projection.json"; then
                echo "FAIL: FP-4C cleanup could not project the restored runtime baseline" >&2
                cleanup_failed=1
            elif ! cmp -s "$baseline_projection_file" \
                "$artifact_dir/cleanup-baseline.projection.json"; then
                echo "FAIL: FP-4C cleanup runtime baseline differs after exact config restore" >&2
                diff -u "$baseline_projection_file" \
                    "$artifact_dir/cleanup-baseline.projection.json" >&2 || true
                cleanup_failed=1
            fi
        fi
    fi

    if [[ "$acceptance_completed" != true && $original_status -eq 0 ]]; then
        echo "FAIL: FP-4C recipe exited before completing its acceptance" >&2
        original_status=1
    fi
    if (( cleanup_failed != 0 )); then
        if (( original_status != 0 )); then
            echo "FAIL: FP-4C cleanup also failed after original recipe status $original_status" >&2
            exit "$original_status"
        fi
        exit 1
    fi
    exit "$original_status"
}

write_json_field() {
    local source_file="$1" field="$2" destination="$3"
    python3 - "$source_file" "$field" >"$destination" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    value = json.load(stream)
for component in sys.argv[2].split("."):
    value = value[component]
print(json.dumps(value, sort_keys=True, separators=(",", ":")))
PY
}

build_resolve_input() {
    local step_file="$1"
    python3 - "$step_file" "$bindings_file" "$outputs_file" <<'PY'
import json
import sys

def load(path):
    with open(path, encoding="utf-8") as stream:
        return json.load(stream)

print(json.dumps({
    "step": load(sys.argv[1]),
    "bindings": load(sys.argv[2]),
    "outputs": load(sys.argv[3]),
}, sort_keys=True, separators=(",", ":")))
PY
}

build_result_input() {
    local step_file="$1" before_file="$2" after_file="$3"
    python3 - "$step_file" "$bindings_file" "$before_file" "$after_file" <<'PY'
import json
import sys

def load(path):
    with open(path, encoding="utf-8") as stream:
        return json.load(stream)

print(json.dumps({
    "step": load(sys.argv[1]),
    "bindings": load(sys.argv[2]),
    "before": load(sys.argv[3]),
    "after": load(sys.argv[4]),
}, sort_keys=True, separators=(",", ":")))
PY
}

build_replay_header_input() {
    python3 - "$plan_file" "$bindings_file" "$outputs_file" <<'PY'
import json
import sys

def load(path):
    with open(path, encoding="utf-8") as stream:
        return json.load(stream)

print(json.dumps({
    "plan": load(sys.argv[1]),
    "bindings": load(sys.argv[2]),
    "outputs": load(sys.argv[3]),
}, sort_keys=True, separators=(",", ":")))
PY
}

build_checkpoint_input() {
    local through="$1" snapshot_file="$2"
    python3 - "$plan_file" "$bindings_file" "$outputs_file" \
        "$snapshot_file" "$through" <<'PY'
import json
import sys

def load(path):
    with open(path, encoding="utf-8") as stream:
        return json.load(stream)

print(json.dumps({
    "plan": load(sys.argv[1]),
    "through": int(sys.argv[5]),
    "bindings": load(sys.argv[2]),
    "outputs": load(sys.argv[3]),
    "snapshot": load(sys.argv[4]),
}, sort_keys=True, separators=(",", ":")))
PY
}

append_record_field() {
    local source_file="$1" field="$2"
    python3 - "$source_file" "$field" >>"$replay_file" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    value = json.load(stream)
for component in sys.argv[2].split("."):
    value = value[component]
print(json.dumps(value, sort_keys=True, separators=(",", ":")))
PY
}

wait_for_checkpoint() {
    local through="$1" current_file="$2"
    local attempts="${3:-240}" i
    for ((i = 0; i < attempts; ++i)); do
        if ! dock_is_running; then
            echo "FAIL: fixture dock exited before FP-4C checkpoint $through" >&2
            return 1
        fi
        capture_snapshot "$current_file" || true
        if [[ -s "$current_file" ]] \
            && build_checkpoint_input "$through" "$current_file" 2>/dev/null \
                | python3 "$MODEL" assert-checkpoint >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.25
    done
    build_checkpoint_input "$through" "$current_file" \
        | python3 "$MODEL" assert-checkpoint
}

wait_for_quiescent_projection() {
    local output_file="$1" snapshot_file="$2"
    local previous="" current="" repeats=0 i
    for ((i = 0; i < 240; ++i)); do
        if ! dock_is_running; then
            echo "FAIL: fixture dock exited before its state quiesced" >&2
            return 1
        fi
        capture_snapshot "$snapshot_file" || true
        current="$(
            python3 "$MODEL" quiescent-projection <"$snapshot_file" 2>/dev/null
        )" || current=""
        if [[ -n "$current" && "$current" == "$previous" ]]; then
            repeats=$((repeats + 1))
            if (( repeats >= 2 )); then
                printf '%s\n' "$current" >"$output_file"
                return 0
            fi
        else
            repeats=0
        fi
        previous="$current"
        sleep 0.25
    done
    python3 "$MODEL" quiescent-projection <"$snapshot_file"
}

json_scalar() {
    local source_file="$1" path="$2"
    python3 - "$source_file" "$path" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    value = json.load(stream)
for component in sys.argv[2].split("."):
    value = value[component]
if isinstance(value, bool):
    print("true" if value else "false")
elif value is None:
    print("")
elif isinstance(value, (str, int, float)):
    print(value)
else:
    raise SystemExit("%s is not a scalar" % sys.argv[2])
PY
}

json_action_arguments() {
    local resolved_file="$1"
    python3 - "$resolved_file" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    action = json.load(stream)["action"]
for value in action.get("args", []):
    if isinstance(value, bool):
        print("true" if value else "false")
    elif isinstance(value, (str, int)):
        print(value)
    else:
        raise SystemExit("D-Bus argument is not a shell scalar: %r" % (value,))
PY
}

wait_for_bound_result() {
    local step_file="$1" before_file="$2" after_file="$3" result_file="$4"
    local i
    for ((i = 0; i < 240; ++i)); do
        if ! dock_is_running; then
            echo "FAIL: fixture dock exited before an operation result bound" >&2
            return 1
        fi
        capture_snapshot "$after_file" || true
        if [[ -s "$after_file" ]] \
            && build_result_input \
                "$step_file" "$before_file" "$after_file" 2>/dev/null \
                | python3 "$MODEL" bind-result >"$result_file" 2>/dev/null; then
            write_json_field "$result_file" bindings "$bindings_file" \
                || return 1
            append_record_field "$result_file" record
            return 0
        fi
        sleep 0.25
    done
    build_result_input "$step_file" "$before_file" "$after_file" \
        | python3 "$MODEL" bind-result
}

build_edit_input() {
    local snapshot_file="$1" target="$2" editing="$3" configuring="$4"
    python3 - "$snapshot_file" "$bindings_file" "$target" "$editing" "$configuring" <<'PY'
import json
import sys

def load(path):
    with open(path, encoding="utf-8") as stream:
        return json.load(stream)

print(json.dumps({
    "snapshot": load(sys.argv[1]),
    "bindings": load(sys.argv[2]),
    "target": sys.argv[3],
    "editing": sys.argv[4] == "true",
    "configuring": sys.argv[5] == "true",
}, sort_keys=True, separators=(",", ":")))
PY
}

wait_for_edit_outcome() {
    local target="$1" editing="$2" configuring="$3" current_file="$4" i
    for ((i = 0; i < 240; ++i)); do
        if ! dock_is_running; then
            echo "FAIL: fixture dock exited during an edit transition" >&2
            return 1
        fi
        capture_snapshot "$current_file" || true
        if [[ -s "$current_file" ]] \
            && build_edit_input \
                "$current_file" "$target" "$editing" "$configuring" 2>/dev/null \
                | python3 "$MODEL" assert-edit >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.25
    done
    build_edit_input "$current_file" "$target" "$editing" "$configuring" \
        | python3 "$MODEL" assert-edit
}

build_reload_input() {
    local before_file="$1" after_file="$2" resolved_file="$3"
    python3 - "$before_file" "$after_file" "$bindings_file" "$resolved_file" <<'PY'
import json
import sys

def load(path):
    with open(path, encoding="utf-8") as stream:
        return json.load(stream)

before = load(sys.argv[1])
after = load(sys.argv[2])
bindings = load(sys.argv[3])
affected = load(sys.argv[4])["action"]["affected"]
print(json.dumps({
    "before": before,
    "after": after,
    "bindings": bindings,
    "affected": affected,
}, sort_keys=True, separators=(",", ":")))
PY
}

wait_for_runtime_reload() {
    local before_file="$1" after_file="$2" resolved_file="$3" i
    for ((i = 0; i < 240; ++i)); do
        if ! dock_is_running; then
            echo "FAIL: fixture dock exited during runtime reload" >&2
            return 1
        fi
        capture_snapshot "$after_file" || true
        if [[ -s "$after_file" ]] \
            && build_reload_input \
                "$before_file" "$after_file" "$resolved_file" 2>/dev/null \
                | python3 "$MODEL" assert-runtime-reload >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.25
    done
    build_reload_input "$before_file" "$after_file" "$resolved_file" \
        | python3 "$MODEL" assert-runtime-reload
}

assert_tombstone_on_disk() {
    local persistent_id="$1"
    python3 - "$E2E_LAYOUT" "$persistent_id" <<'PY'
import sys

path = sys.argv[1]
dock_id = int(sys.argv[2])
prefix = f"[Containments][{dock_id}]"
with open(path, encoding="utf-8") as stream:
    groups = [
        line.strip()
        for line in stream
        if line.lstrip().startswith("[") and line.rstrip().endswith("]")
    ]
survivors = [
    group for group in groups
    if group == prefix or group.startswith(prefix + "[")
]
if survivors:
    raise SystemExit(
        "removed containment subtree survived the immediate tombstone: "
        + ", ".join(survivors[:8])
    )
PY
}

capture_layer3_latte_windows() {
    local rows
    rows="$(e2e_kwin_js "for (const window of workspace.windowList()) {
        if (String(window.resourceClass) === 'latte-dock' && window.layer === 3) {
            print('@TAG@|' + JSON.stringify({
                id: String(window.internalId),
                caption: String(window.caption),
                geometry: [
                    Math.round(window.frameGeometry.x),
                    Math.round(window.frameGeometry.y),
                    Math.round(window.frameGeometry.width),
                    Math.round(window.frameGeometry.height)
                ],
                output: window.output ? window.output.name : null
            }));
        }
    }" 0.05)" || return 1
    python3 -c '
import json
import sys

rows = [line for line in sys.stdin.read().splitlines() if line]
print(json.dumps([json.loads(row) for row in rows],
                 sort_keys=True, separators=(",", ":")))
' <<<"$rows"
}

assert_visual_window_ownership() {
    local snapshot_file="$1" windows_file="$2" output_file="$3"
    python3 - "$snapshot_file" "$windows_file" "$output_file" <<'PY' \
        | python3 "$MODEL" assert-visual-window-ownership >/dev/null
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    snapshot = json.load(stream)
with open(sys.argv[2], encoding="utf-8") as stream:
    windows = json.load(stream)
with open(sys.argv[3], encoding="utf-8") as stream:
    outputs = json.load(stream)
print(json.dumps(
    {"snapshot": snapshot, "outputs": outputs, "windows": windows},
    sort_keys=True,
    separators=(",", ":"),
))
PY
}

assert_latest_intent_probe() {
    local before_file="$1" after_file="$2"
    python3 - "$plan_file" "$bindings_file" \
        "$before_file" "$after_file" <<'PY'
import json
import sys

def load(path):
    with open(path, encoding="utf-8") as stream:
        return json.load(stream)

plan, bindings, before, after = map(load, sys.argv[1:])
target = bindings[plan["latestIntentProbe"]["target"]]
before_view = next(
    view for view in before["views"]
    if view["persistentDockId"] == target
)
after_view = next(
    view for view in after["views"]
    if view["persistentDockId"] == target
)
stable_placement = (
    "screenId",
    "edge",
    "alignment",
    "onPrimary",
)
if any(before_view[key] != after_view[key] for key in stable_placement):
    raise SystemExit("latest-intent probe did not return to its exact origin")
before_generation = int(before_view["relocationGeneration"])
after_generation = int(after_view["relocationGeneration"])
if after_generation != before_generation + 2:
    raise SystemExit(
        "latest-intent probe did not claim exactly two generations: "
        f"{before_generation} -> {after_generation}"
    )
if (
    not after_view["geometrySettled"]
    or after_view["relocationGeneration"]
       != after_view["appliedRelocationGeneration"]
):
    raise SystemExit("latest-intent probe did not settle its newest generation")
PY
}

wait_for_visual_window_ownership() {
    local through="$1" snapshot_file="$2" windows_file="$3" i
    for ((i = 0; i < 120; ++i)); do
        if ! dock_is_running; then
            echo "FAIL: fixture dock exited before visual ownership settled" >&2
            return 1
        fi
        capture_snapshot "$snapshot_file" || true
        if [[ -s "$snapshot_file" ]] \
            && build_checkpoint_input "$through" "$snapshot_file" 2>/dev/null \
                | python3 "$MODEL" assert-checkpoint >/dev/null 2>&1 \
            && capture_layer3_latte_windows >"$windows_file" 2>/dev/null \
            && assert_visual_window_ownership \
                "$snapshot_file" "$windows_file" \
                "$outputs_file" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.25
    done
    build_checkpoint_input "$through" "$snapshot_file" \
        | python3 "$MODEL" assert-checkpoint >/dev/null
    capture_layer3_latte_windows >"$windows_file" \
        || return 1
    assert_visual_window_ownership \
        "$snapshot_file" "$windows_file" "$outputs_file"
}

require_private_nested_session

candidate_plan="$(mktemp "$E2E_RT/fp4c-operation-plan.XXXXXX")" \
    || e2e_fail "could not allocate the read-only FP-4C plan staging file"
if [[ -n "$supplied_plan" ]]; then
    plan_source="$(realpath -e -- "$supplied_plan")" \
        || e2e_fail "LATTE_LINKED_STRESS_PLAN does not resolve to a readable plan"
    [[ -f "$plan_source" && -r "$plan_source" ]] \
        || e2e_fail "LATTE_LINKED_STRESS_PLAN is not a readable regular file"
    python3 "$MODEL" validate-plan <"$plan_source" >/dev/null \
        || e2e_fail "the supplied FP-4C operation plan is invalid"
    cp -- "$plan_source" "$candidate_plan" \
        || e2e_fail "could not copy the supplied FP-4C plan into private staging"
    python3 "$MODEL" validate-plan <"$candidate_plan" >/dev/null \
        || e2e_fail "the private copy of the supplied FP-4C plan is invalid"
else
    python3 "$MODEL" generate-plan --seed "$requested_seed" >"$candidate_plan" \
        || e2e_fail "could not generate the typed FP-4C operation plan"
    python3 "$MODEL" validate-plan <"$candidate_plan" >/dev/null \
        || e2e_fail "the generated FP-4C operation plan failed its own validator"
fi
stress_seed="$(
    python3 - "$candidate_plan" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    print(json.load(stream)["seed"])
PY
)" || e2e_fail "could not read the validated FP-4C plan seed"
[[ "$stress_seed" =~ ^[0-9]+$ ]] \
    || e2e_fail "the validated FP-4C plan seed is not an unsigned integer"
readonly stress_seed

artifact_dir="$(mktemp -d \
    "$E2E_ARTIFACTS/linked-dock-operation-stress.seed-${stress_seed}.run-XXXXXX")" \
    || e2e_fail "could not allocate the FP-4C artifact directory"
transaction_dir="$(mktemp -d "$E2E_RT/fp4c-operation-stress.XXXXXX")" \
    || e2e_fail "could not allocate the FP-4C nested transaction directory"
path_is_within "$transaction_dir" "$E2E_RT" \
    || e2e_fail "FP-4C transaction directory escaped the nested runtime"
backup_dir="$transaction_dir/pristine-config"
fixture_dir="$transaction_dir/panel-fixture"
plan_file="$artifact_dir/plan.json"
operations_file="$artifact_dir/operations.jsonl"
replay_file="$artifact_dir/replay.jsonl"
bindings_file="$transaction_dir/bindings.json"
outputs_file="$transaction_dir/outputs.json"
baseline_snapshot_file="$artifact_dir/pristine-baseline.json"
baseline_projection_file="$artifact_dir/pristine-baseline.projection.json"
cp -- "$candidate_plan" "$plan_file" \
    || e2e_fail "could not preserve the validated FP-4C plan in its artifacts"
cmp -s "$candidate_plan" "$plan_file" \
    || e2e_fail "the artifact FP-4C plan differs from its validated input"
readonly artifact_dir transaction_dir backup_dir fixture_dir plan_file
readonly operations_file replay_file bindings_file outputs_file
readonly baseline_snapshot_file baseline_projection_file

latest_intent_final_seq="$(
    python3 - "$plan_file" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    print(json.load(stream)["latestIntentProbe"]["finalSeq"])
PY
)" || e2e_fail "could not read the validated latest-intent probe sequence"
[[ "$latest_intent_final_seq" =~ ^[1-9][0-9]*$ ]] \
    || e2e_fail "the latest-intent probe sequence is malformed"
readonly latest_intent_final_seq

# No command that can stop the pristine dock or replace its configuration runs
# outside this trap.
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
transaction_started=true

snapshot >"$baseline_snapshot_file" \
    || e2e_fail "could not capture the pristine FP-4C baseline"
python3 "$MODEL" assert-baseline <"$baseline_snapshot_file" >/dev/null \
    || e2e_fail "the starting nested configuration is not the pristine one-view baseline"
python3 "$MODEL" durable-projection <"$baseline_snapshot_file" \
    >"$baseline_projection_file" \
    || e2e_fail "could not project the pristine FP-4C cleanup baseline"

stop_dock_if_running \
    || e2e_fail "could not stop the pristine dock before its configuration backup"
mkdir -p -- "$backup_dir" \
    || e2e_fail "could not allocate the pristine configuration backup"
cp -a -- "$E2E_CONFIG_HOME/." "$backup_dir/" \
    || e2e_fail "could not back up the whole nested configuration"
diff -qr --no-dereference "$E2E_CONFIG_HOME" "$backup_dir" \
    || e2e_fail "the pristine whole-configuration backup differs after copy"
backup_ready=true

python3 "$FIXTURE_GENERATOR" \
    --seed-dir "$backup_dir" \
    --out-dir "$fixture_dir" \
    --view-type panel \
    --edge bottom \
    --alignment justify \
    --display 1out \
    --cell fp4c-partial-floating-panel >/dev/null \
    || e2e_fail "could not generate the FP-4C panel fixture from the fresh backup"

rm -rf -- "$E2E_CONFIG_HOME" \
    || e2e_fail "could not remove the pristine config before FP-4C staging"
mkdir -p -- "$E2E_CONFIG_HOME" \
    || e2e_fail "could not recreate the nested FP-4C config home"
cp -a -- "$fixture_dir/." "$E2E_CONFIG_HOME/" \
    || e2e_fail "could not stage the generated FP-4C panel fixture"
e2e_dock_start 90 \
    || e2e_fail "the generated FP-4C panel fixture did not start"

initial_snapshot="$artifact_dir/generated-panel.json"
snapshot >"$initial_snapshot" \
    || e2e_fail "could not capture the generated FP-4C panel"
python3 "$MODEL" assert-baseline <"$initial_snapshot" >/dev/null \
    || e2e_fail "the generated FP-4C fixture is not exactly one independent view"
root_id="$(
    python3 - "$initial_snapshot" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    views = json.load(stream)["views"]
print(views[0]["persistentDockId"])
PY
)" || e2e_fail "could not resolve the validated FP-4C root identity"
[[ "$root_id" =~ ^[1-9][0-9]*$ ]] \
    || e2e_fail "the validated FP-4C root identity is malformed"

e2e_dock_stop \
    || e2e_fail "could not stop the generated panel before deterministic configuration"
declare -a panel_group=(
    --file "$E2E_LAYOUT"
    --group Containments
    --group "$root_id"
    --group General
)
kwriteconfig6 "${panel_group[@]}" --key minLength 45 \
    || e2e_fail "could not set the FP-4C panel minimum length"
kwriteconfig6 "${panel_group[@]}" --key maxLength 45 \
    || e2e_fail "could not set the FP-4C panel maximum length"
kwriteconfig6 "${panel_group[@]}" --key maximizeWhenMaximized false \
    || e2e_fail "could not pin the FP-4C panel length"
kwriteconfig6 "${panel_group[@]}" --key hideFloatingGapForMaximized true \
    || e2e_fail "could not enable FP-4C window-touch attachment"
kwriteconfig6 "${panel_group[@]}" --key floatingGapHidingWaitsMouse false \
    || e2e_fail "could not disable FP-4C pointer deferral"
kwriteconfig6 "${panel_group[@]}" --key screenEdgeMargin 18 \
    || e2e_fail "could not set the positive FP-4C floating gap"
kwriteconfig6 "${panel_group[@]}" --key floatingInternalGapIsForced false \
    || e2e_fail "could not retain the panel-owned FP-4C gap"
kwriteconfig6 "${panel_group[@]}" --key zoomLevel 0 \
    || e2e_fail "could not disable FP-4C parabolic zoom"
kwriteconfig6 "${panel_group[@]}" --key useThemePanel true \
    || e2e_fail "could not retain FP-4C theme-panel behavior"
kwriteconfig6 "${panel_group[@]}" --key panelSize 100 \
    || e2e_fail "could not retain the FP-4C panel background thickness"
e2e_dock_start 90 \
    || e2e_fail "the configured partial floating FP-4C panel did not start"
e2e_call setViewVisibilityMode us "$root_id" alwaysVisible >/dev/null \
    || e2e_fail "could not set the FP-4C root to Always Visible"

screens_file="$artifact_dir/screens.json"
e2e_json screensData >"$screens_file" \
    || e2e_fail "could not read the two nested FP-4C outputs"
python3 - "$screens_file" "$outputs_file" <<'PY' \
    || e2e_fail "could not resolve exactly two active FP-4C output identities"
import json
import sys

with open(sys.argv[1], encoding="utf-8") as source:
    screens = [screen for screen in json.load(source) if screen["isActive"]]
primary = [screen for screen in screens if screen["isPrimary"]]
secondary = [screen for screen in screens if not screen["isPrimary"]]
if len(screens) != 2 or len(primary) != 1 or len(secondary) != 1:
    raise SystemExit(
        "expected two active screens with one primary, got %r" % screens
    )

def output_record(screen):
    return {
        "id": screen["id"],
        "name": screen["name"],
        "geometry": screen["geometry"],
    }

with open(sys.argv[2], "w", encoding="utf-8") as stream:
    json.dump({
        "primary": output_record(primary[0]),
        "secondary": output_record(secondary[0]),
    }, stream, sort_keys=True, separators=(",", ":"))
    stream.write("\n")
PY
printf '{"root":%s}\n' "$root_id" >"$bindings_file"

python3 "$MODEL" validate-plan <"$plan_file" >/dev/null \
    || e2e_fail "the artifact FP-4C operation plan failed revalidation"
python3 "$MODEL" emit-operations <"$plan_file" >"$operations_file" \
    || e2e_fail "could not emit the typed FP-4C operations"
build_replay_header_input | python3 "$MODEL" replay-header >"$replay_file" \
    || e2e_fail "could not record the resolved FP-4C replay header"

step_dir="$artifact_dir/steps"
mkdir -p -- "$step_dir" \
    || e2e_fail "could not allocate the resolved FP-4C step artifacts"
declare -a pending_steps=()
declare -a pending_resolved=()
pending_before_file=""
pending_tombstone_id=""
removal_started_ns=0

while IFS= read -r operation_row; do
    [[ -n "$operation_row" ]] || continue
    step_number="$(
        python3 -c 'import json,sys; print(json.loads(sys.argv[1])["seq"])' \
            "$operation_row"
    )" || e2e_fail "an emitted FP-4C operation has no sequence"
    last_step_number="$step_number"
    [[ "$step_number" =~ ^[1-9][0-9]*$ ]] \
        || e2e_fail "an emitted FP-4C operation has a malformed sequence"
    step_tag="$(printf '%03d' "$step_number")"
    step_file="$step_dir/$step_tag.operation.json"
    resolved_file="$step_dir/$step_tag.resolved.json"
    before_file="$step_dir/$step_tag.before.json"
    after_file="$step_dir/$step_tag.after.json"
    result_file="$step_dir/$step_tag.result.json"
    printf '%s\n' "$operation_row" >"$step_file"

    build_resolve_input "$step_file" \
        | python3 "$MODEL" resolve-operation >"$resolved_file" \
        || e2e_fail "could not resolve FP-4C operation $step_number"
    action_kind="$(json_scalar "$resolved_file" action.kind)" \
        || e2e_fail "resolved FP-4C operation $step_number has no action kind"
    operation_kind="$(json_scalar "$step_file" operation.kind)" \
        || e2e_fail "FP-4C operation $step_number has no typed operation kind"
    checkpoint="$(json_scalar "$step_file" checkpoint)" \
        || e2e_fail "FP-4C operation $step_number has no checkpoint policy"

    if [[ -z "$pending_before_file" ]]; then
        snapshot >"$before_file" \
            || e2e_fail "could not capture state before FP-4C operation $step_number"
        pending_before_file="$before_file"
    fi

    removed_this_step=""
    reload_this_step=false
    restart_this_step=false
    if [[ "$action_kind" == dbus ]]; then
        method="$(json_scalar "$resolved_file" action.method)" \
            || e2e_fail "FP-4C operation $step_number has no D-Bus method"
        signature="$(json_scalar "$resolved_file" action.signature)" \
            || e2e_fail "FP-4C operation $step_number has no D-Bus signature"
        action_args_text="$(json_action_arguments "$resolved_file")" \
            || e2e_fail "FP-4C operation $step_number has malformed D-Bus arguments"
        mapfile -t action_args <<<"$action_args_text"
        if [[ "$method" == removeView ]]; then
            removal_started_ns="$(date +%s%N)"
        fi
        if [[ "$method" == reloadView ]]; then
            before_reload_quiescent="$step_dir/$step_tag.before-reload.quiescent.json"
            before_reload_projection="$step_dir/$step_tag.before-reload.projection.json"
            after_reload_quiescent="$step_dir/$step_tag.after-reload.quiescent.json"
            after_reload_projection="$step_dir/$step_tag.after-reload.projection.json"
            wait_for_quiescent_projection \
                "$before_reload_quiescent" "$before_file" \
                || e2e_fail "state before reload operation $step_number did not quiesce"
            python3 "$MODEL" durable-projection <"$before_file" \
                >"$before_reload_projection" \
                || e2e_fail "could not project durable state before reload operation $step_number"
        fi
        e2e_call "$method" "$signature" "${action_args[@]}" >/dev/null \
            || e2e_fail "D-Bus transport failed for FP-4C operation $step_number ($method)"

        case "$method" in
            removeView)
                removed_this_step="${action_args[0]:-}"
                [[ "$removed_this_step" =~ ^[1-9][0-9]*$ ]] \
                    || e2e_fail "removeView operation $step_number has no persistent identity"
                ;;
            reloadView)
                reload_this_step=true
                ;;
            setViewEditMode|setViewConfiguringApplets)
                [[ "$checkpoint" == true ]] \
                    || e2e_fail "edit operation $step_number cannot be an unchecked burst member"
                edit_target="$(json_scalar "$step_file" operation.target)" \
                    || e2e_fail "edit operation $step_number has no symbolic target"
                case "$operation_kind" in
                    beginEdit)
                        expected_editing=true
                        expected_configuring=false
                        ;;
                    configureAppletsOn)
                        expected_editing=true
                        expected_configuring=true
                        ;;
                    configureAppletsOff)
                        expected_editing=true
                        expected_configuring=false
                        ;;
                    endEdit)
                        expected_editing=false
                        expected_configuring=false
                        ;;
                    *)
                        e2e_fail "operation $step_number maps an invalid edit kind '$operation_kind'"
                        ;;
                esac
                wait_for_edit_outcome "$edit_target" \
                    "$expected_editing" "$expected_configuring" "$after_file" \
                    || e2e_fail "edit ownership did not match FP-4C operation $step_number"
                ;;
        esac
    elif [[ "$action_kind" == restart ]]; then
        restart_this_step=true
        [[ "$checkpoint" == true && "${#pending_steps[@]}" -eq 0 ]] \
            || e2e_fail "restart operation $step_number is not an isolated checkpoint"
        before_restart_projection="$step_dir/$step_tag.before-restart.projection.json"
        after_restart_projection="$step_dir/$step_tag.after-restart.projection.json"
        before_restart_quiescent="$step_dir/$step_tag.before-restart.quiescent.json"
        after_restart_quiescent="$step_dir/$step_tag.after-restart.quiescent.json"
        wait_for_quiescent_projection "$before_restart_quiescent" "$before_file" \
            || e2e_fail "state before restart operation $step_number did not quiesce"
        python3 "$MODEL" durable-projection <"$before_file" \
            >"$before_restart_projection" \
            || e2e_fail "could not project durable state before restart operation $step_number"
        e2e_dock_stop \
            || e2e_fail "dock did not stop for FP-4C restart operation $step_number"
        if [[ -n "$pending_tombstone_id" ]]; then
            assert_tombstone_on_disk "$pending_tombstone_id" \
                || e2e_fail "the stopped layout resurrected removed view $pending_tombstone_id"
        fi
        e2e_dock_start 90 \
            || e2e_fail "dock did not start for FP-4C restart operation $step_number"
        if [[ -n "$pending_tombstone_id" ]]; then
            removal_elapsed_ms=$((($(date +%s%N) - removal_started_ns) / 1000000))
            (( removal_elapsed_ms < 60000 )) \
                || e2e_fail "removal restart missed the 60-second Undo interval (${removal_elapsed_ms}ms)"
        fi
    else
        e2e_fail "FP-4C operation $step_number resolved unsupported action '$action_kind'"
    fi

    pending_steps+=("$step_file")
    pending_resolved+=("$resolved_file")
    if [[ "$checkpoint" == false ]]; then
        [[ "$action_kind" == dbus ]] \
            || e2e_fail "only D-Bus actions may defer an FP-4C checkpoint"
        if [[ "$operation_kind" != move ]]; then
            case "$operation_kind" in
                createLinked|duplicateIndependent) ;;
                *)
                    e2e_fail "operation $step_number cannot defer its semantic checkpoint"
                    ;;
            esac
            append_record_field "$resolved_file" record
            wait_for_bound_result \
                "$step_file" "$pending_before_file" "$after_file" "$result_file" \
                || e2e_fail "snapshot did not bind FP-4C operation $step_number"
            pending_steps=()
            pending_resolved=()
            pending_before_file=""
        fi
        continue
    fi
    [[ "$checkpoint" == true ]] \
        || e2e_fail "FP-4C operation $step_number has invalid checkpoint '$checkpoint'"

    if [[ "$reload_this_step" == true ]]; then
        wait_for_runtime_reload \
            "$pending_before_file" "$after_file" "$resolved_file" \
            || e2e_fail "linked-root runtime reload did not rotate exactly its affected views"
    fi

    for pending_index in "${!pending_steps[@]}"; do
        pending_step="${pending_steps[$pending_index]}"
        pending_resolve="${pending_resolved[$pending_index]}"
        pending_sequence="$(json_scalar "$pending_step" seq)" \
            || e2e_fail "could not read a pending FP-4C sequence"
        pending_after="$step_dir/$(printf '%03d' "$pending_sequence").after.json"
        pending_result="$step_dir/$(printf '%03d' "$pending_sequence").result.json"
        append_record_field "$pending_resolve" record
        wait_for_bound_result \
            "$pending_step" "$pending_before_file" "$pending_after" "$pending_result" \
            || e2e_fail "snapshot did not prove the result of FP-4C operation $pending_sequence"
    done

    last_checkpoint_file="$step_dir/$step_tag.checkpoint.json"
    checkpoint_attempts=240
    if [[ -n "$removed_this_step" ]]; then
        # A reversible removal is a suspension transaction, not a request to
        # wait for Plasma's 60-second Undo expiry.
        checkpoint_attempts=20
    fi
    wait_for_checkpoint \
        "$step_number" "$last_checkpoint_file" "$checkpoint_attempts" \
        || e2e_fail "FP-4C operation checkpoint $step_number did not converge"
    assert_no_pending_view_move \
        "$step_dir/$step_tag.view-move-transactions.json" \
        || e2e_fail "FP-4C operation checkpoint $step_number retained a durable move transaction"
    if [[ "$step_number" == "$latest_intent_final_seq" ]]; then
        assert_latest_intent_probe \
            "$pending_before_file" "$last_checkpoint_file" \
            || e2e_fail "rapid return-to-origin did not preserve the newest complete placement intent"
    fi
    if [[ "$reload_this_step" == true
       || "$restart_this_step" == true
       || -n "$removed_this_step" ]]; then
        wait_for_visual_window_ownership \
            "$step_number" \
            "$step_dir/$step_tag.visual-snapshot.json" \
            "$step_dir/$step_tag.layer3-windows.json" \
            || e2e_fail "FP-4C checkpoint $step_number has leaked or duplicate visual QWindows"
    fi

    if [[ "$reload_this_step" == true ]]; then
        wait_for_quiescent_projection \
            "$after_reload_quiescent" "$last_checkpoint_file" \
            || e2e_fail "state after reload operation $step_number did not quiesce"
        python3 "$MODEL" durable-projection <"$last_checkpoint_file" \
            >"$after_reload_projection" \
            || e2e_fail "could not project durable state after reload operation $step_number"
        cmp -s "$before_reload_projection" "$after_reload_projection" \
            || {
                diff -u "$before_reload_projection" \
                    "$after_reload_projection" >&2 || true
                e2e_fail "reload operation $step_number changed the exact durable projection"
            }
    fi

    if [[ -n "$removed_this_step" ]]; then
        # This is deliberately the first persistence read after the runtime
        # removal verdict. It proves the synchronous tombstone, not expiry of
        # the 60-second Plasma Undo timer.
        assert_tombstone_on_disk "$removed_this_step" \
            || e2e_fail "removed view $removed_this_step was not tombstoned immediately"
        pending_tombstone_id="$removed_this_step"
    elif [[ -n "$pending_tombstone_id" && "$restart_this_step" != true ]]; then
        e2e_fail "FP-4C operation $step_number intervened before the removal restart"
    fi

    if [[ "$restart_this_step" == true ]]; then
        wait_for_quiescent_projection "$after_restart_quiescent" "$last_checkpoint_file" \
            || e2e_fail "state after restart operation $step_number did not quiesce"
        python3 "$MODEL" durable-projection <"$last_checkpoint_file" \
            >"$after_restart_projection" \
            || e2e_fail "could not project durable state after restart operation $step_number"
        cmp -s "$before_restart_projection" "$after_restart_projection" \
            || {
                diff -u "$before_restart_projection" "$after_restart_projection" >&2 || true
                e2e_fail "restart operation $step_number changed the exact durable projection"
            }
        pending_tombstone_id=""
    fi

    pending_steps=()
    pending_resolved=()
    pending_before_file=""
done <"$operations_file"

[[ "${#pending_steps[@]}" -eq 0 ]] \
    || e2e_fail "the typed FP-4C plan ended inside an unchecked placement burst"

final_snapshot="$artifact_dir/final.json"
final_quiescent="$artifact_dir/final.quiescent.json"
wait_for_quiescent_projection "$final_quiescent" "$final_snapshot" \
    || e2e_fail "the final FP-4C operation state did not remain quiescent"
build_checkpoint_input "${last_step_number:-0}" "$final_snapshot" \
    | python3 "$MODEL" assert-checkpoint >/dev/null \
    || e2e_fail "the final quiescent FP-4C state diverged from the typed plan"
assert_no_pending_view_move \
    "$artifact_dir/final.view-move-transactions.json" \
    || e2e_fail "the final FP-4C state retained a durable move transaction"
wait_for_visual_window_ownership \
    "${last_step_number:-0}" \
    "$artifact_dir/final.visual-snapshot.json" \
    "$artifact_dir/final.layer3-windows.json" \
    || e2e_fail "the final FP-4C state has leaked or duplicate visual QWindows"
python3 "$MODEL" validate-replay \
    --plan "$plan_file" --replay "$replay_file" >/dev/null \
    || e2e_fail "the resolved FP-4C replay is incomplete or inconsistent"

acceptance_completed=true
echo "FP-4C operation stress passed seed $stress_seed; resolved replay and snapshots: $artifact_dir"
