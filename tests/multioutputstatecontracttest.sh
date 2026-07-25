#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Pure contract test for the multi-output transaction's complete semantic
# KScreen-state comparison. No compositor, D-Bus, output, or config is read.
set -uo pipefail

repo="${REPO_ROOT:?CMake must provide REPO_ROOT}"
source "$repo/tests/e2e/matrix/multi-output-lib.sh"
E2E_MO_PRIMARY=Virtual-1
E2E_MO_SECONDARY=Virtual-2

captured='{
  "outputs": [
    {
      "name": "Virtual-1",
      "enabled": true,
      "rotation": 1,
      "scale": 1,
      "pos": {"x": 0, "y": 0},
      "currentModeId": "1",
      "priority": 1,
      "modes": [
        {"id": "1", "size": {"width": 1600, "height": 1000}, "refreshRate": 60},
        {"id": "2", "size": {"width": 1280, "height": 800}, "refreshRate": 60}
      ],
      "capabilities": {"hdr": false, "wideColorGamut": false},
      "futureState": {"calibration": "native"}
    },
    {
      "name": "Virtual-2",
      "enabled": true,
      "rotation": 8,
      "scale": 1.25,
      "pos": {"x": 1600, "y": 120},
      "currentModeId": "3",
      "priority": 2,
      "modes": [
        {"id": "3", "size": {"width": 1000, "height": 1600}, "refreshRate": 60}
      ],
      "capabilities": {"hdr": false, "wideColorGamut": false},
      "futureState": {"calibration": "portrait"}
    }
  ],
  "screen": {"currentSize": {"width": 2600, "height": 1720}},
  "metadata": {"backend": "KWayland", "featureGeneration": 7}
}'

mutate_state() {
    local -r mutation="$1"
    KSCREEN_STATE="$captured" python3 - "$mutation" <<'PY'
import json
import os
import sys

payload = json.loads(os.environ["KSCREEN_STATE"])
mutation = sys.argv[1]
virtual_1 = next(output for output in payload["outputs"]
                 if output["name"] == "Virtual-1")

if mutation == "equivalent-order":
    payload["outputs"].reverse()
    for output in payload["outputs"]:
        output["modes"].reverse()
        reordered = {key: output[key] for key in reversed(output)}
        output.clear()
        output.update(reordered)
    payload = {key: payload[key] for key in reversed(payload)}
elif mutation == "current-mode":
    virtual_1["currentModeId"] = "2"
elif mutation == "priority":
    virtual_1["priority"] = 9
elif mutation == "unassigned-priority":
    virtual_1["priority"] = 0
elif mutation == "reversed-priorities":
    virtual_1["priority"] = 2
    next(output for output in payload["outputs"]
         if output["name"] == "Virtual-2")["priority"] = 1
elif mutation == "mode-refresh":
    virtual_1["modes"][0]["refreshRate"] = 59.94
elif mutation == "capability":
    virtual_1["capabilities"]["hdr"] = True
elif mutation == "future-output-state":
    virtual_1["futureState"]["calibration"] = "changed"
elif mutation == "top-level-state":
    payload["metadata"]["backend"] = "changed"
elif mutation == "added-field":
    virtual_1["newOutputState"] = {"enabled": True}
elif mutation == "restorable-position":
    virtual_1["pos"]["x"] = 4
else:
    raise SystemExit(f"unknown mutation {mutation!r}")

print(json.dumps(payload, separators=(",", ":")))
PY
}

fail() {
    echo "multioutputstatecontracttest: FAIL: $*" >&2
    exit 1
}

equivalent="$(mutate_state equivalent-order)" \
    || fail "could not construct the reordered equivalent state"
if ! _mo_compare_output_state_semantically "$captured" "$equivalent"; then
    fail "semantic key, output, and mode reordering was rejected"
fi

expected_projection=$'Virtual-1\tenable\tnone\t1\t0\t0\t1\nVirtual-2\tenable\tright\t1.25\t1600\t120\t2'
projection="$(_mo_project_output_state "$captured")" \
    || fail "the canonical captured state did not produce a restore projection"
if [[ "$projection" != "$expected_projection" ]]; then
    fail "restore projection omitted or changed captured priority: $projection"
fi

if ! _mo_classify_output_priorities "$captured"; then
    fail "canonical captured priorities were not preserved"
fi
reversed_priorities="$(mutate_state reversed-priorities)" \
    || fail "could not construct reversed valid priorities"
if ! _mo_classify_output_priorities "$reversed_priorities"; then
    fail "valid unique reversed priorities were not preserved"
fi
unassigned_priority="$(mutate_state unassigned-priority)" \
    || fail "could not construct an unassigned priority"
priority_status=0
if _mo_classify_output_priorities "$unassigned_priority"; then
    priority_status=0
else
    priority_status=$?
fi
if (( priority_status != 1 )); then
    fail "active priority 0 returned $priority_status instead of requesting normalization"
fi

# Pure command-emission proof: replace the nested guard and kscreen-doctor with
# local fakes, drive the real restore function, and require both captured
# priorities in the one atomic setter call. The -j fake returns the capture so
# the complete semantic postcondition also executes.
_mo_require_topology_mutation() {
    return 0
}
declare -a recorded_doctor_args=()
kscreen-doctor() {
    if (( $# == 1 )) && [[ "$1" == -j ]]; then
        printf '%s\n' "$captured"
        return 0
    fi
    recorded_doctor_args=("$@")
}
if ! mo_restore_output_topology "$captured"; then
    fail "the pure restore driver rejected the canonical captured state"
fi
priority_setters=0
for argument in "${recorded_doctor_args[@]}"; do
    case "$argument" in
        output.Virtual-1.priority.1|output.Virtual-2.priority.2)
            priority_setters=$((priority_setters + 1))
            ;;
        *.priority.*)
            fail "restore emitted an unexpected priority setter: $argument"
            ;;
    esac
done
if (( priority_setters != 2 )); then
    fail "restore emitted $priority_setters captured priority setters instead of 2"
fi

expect_rejected() {
    local -r mutation="$1"
    local -r expected_fragment="$2"
    local current diagnostic
    local status=0
    current="$(mutate_state "$mutation")" \
        || fail "could not construct the $mutation controlled mutation"
    if diagnostic="$(
        _mo_compare_output_state_semantically "$captured" "$current" 2>&1
    )"; then
        status=0
    else
        status=$?
    fi
    if (( status != 1 )); then
        fail "$mutation returned $status instead of semantic-drift status 1"
    fi
    if [[ "$diagnostic" != *"$expected_fragment"* ]]; then
        fail "$mutation was rejected for an unexpected reason: $diagnostic"
    fi
    echo "  rejected $mutation at $expected_fragment"
}

# currentModeId and priority are deliberately not assigned through guessed
# kscreen-doctor syntax. They and all other unhandled state must remain equal.
expect_rejected current-mode currentModeId
expect_rejected priority priority
expect_rejected mode-refresh refreshRate
expect_rejected capability hdr
expect_rejected future-output-state calibration
expect_rejected top-level-state backend
expect_rejected added-field newOutputState

# The full comparison also retains the documented writable-field check.
expect_rejected restorable-position pos

echo "PASS: complete KScreen state accepts semantic reordering and rejects every controlled drift"
