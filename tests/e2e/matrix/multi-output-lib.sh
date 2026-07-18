# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# The MULTI-OUTPUT discover-and-pin layer for the e2e matrix (C-I2 / P1,
# docs/e2e-interaction-test-plan.md section 5, open question O7). Sourced by the
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
