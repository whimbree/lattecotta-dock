# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# The e2e interaction MATRIX HARNESS (P0 / C-I1, docs/e2e-interaction-test-plan.md
# sections 3 and 5). This is the reusable driver every scenario chunk (C-S*,
# C-A*) calls: a scenario is a FUNCTION composed as
# (fixture x interaction x expected-readback), never a bespoke copy-paste
# script. It is sourced inside the nested vehicle (by an e2e recipe run through
# scripts/run-e2e.sh, or by scripts/run-matrix.sh), so plain busctl/fakepointer
# reach the vehicle dock.
#
# ============================ HARNESS API ==================================
# Call order in a scenario recipe:
#   source tests/e2e/lib.sh
#   source tests/e2e/matrix/matrix-lib.sh
#   matrix_init                       # snapshot the pristine seed (once)
#
# COMMITTED scenario (assert a state TRANSITION by readback):
#   matrix_scenario_commit <cell> <verb> <expected-readback>
#     stages the cell, drives <verb> to COMMIT, reads the verb's probe, and
#     asserts it equals <expected-readback>.
#       return 0  PASS      1  FAIL (readback != expected)   2  REFUSED
#
# ABORT scenario (assert RETURN-TO-BASELINE with no residue):
#   matrix_scenario_abort <cell> <verb>
#     stages the cell, captures the baseline, drives <verb> to ABORT, and
#     asserts every baseline field is byte-identical afterwards.
#       return 0  PASS (no residue)   1  FAIL (residue)   2  REFUSED
#
# A VERB is two shell functions a scenario chunk (or this file) defines:
#   matrix_verb_<name>_drive <view-id> <commit|abort>   # performs the interaction
#   matrix_verb_<name>_probe <view-id>                  # echoes the one queryable
#                                                       # fact the verb changes
# The harness dispatches by name and REFUSES an unknown verb (a boundary
# check, not a silent no-op). This file ships the `editmode` verb (uses only
# the existing setViewEditMode D-Bus action) so P0 self-tests real interaction
# end-to-end with no dependency on the P3-P9 drivers.
#
# ABORT BACKBONE (used by matrix_scenario_abort; also callable directly):
#   matrix_baseline_capture <view-id> <verb>           # snapshot residue fields
#   matrix_assert_baseline_restored <view-id> <verb>   # diff; 1 on residue
# The captured field set is the abort-residue surface the plan enumerates
# (section 3): the view record, the applet order, the layout config bytes
# (KConfig-default-aware), plus the verb's own probe. Where a residue is
# visual-only (a ghost overlay whose STATE says clean - PR #20), a baseline
# FRAME is captured too when MATRIX_CAPTURE_FRAME=1; the pixel comparison
# itself is delegated to the golden bridge (P2 / C-I6) via a hook this file
# stubs.
#
# STAGING:
#   matrix_stage <cell>              # generate + load the cell fixture, then
#                                    # assert the view REALIZED as declared.
#                                    #   return 0 ok   2 refused/mismatch
#   matrix_view_id                   # containment id of the view under test
# ===========================================================================

# --- configuration ----------------------------------------------------------
MATRIX_DIR="${E2E_REPO:?matrix-lib must run through run-e2e/run-matrix}/tests/e2e/matrix"
MATRIX_FIXTURE="$MATRIX_DIR/fixture.py"

# The render-golden bridge (P2 / C-I6): e2e_golden_compare (tier-aware pixel
# compare) backs the abort backbone's visual-residue hook below, and
# e2e_assert_golden / e2e_screenshot_crop serve the committed-golden scenarios.
source "$MATRIX_DIR/golden-bridge.sh"
MATRIX_WORK="${E2E_ARTIFACTS:?}/matrix"
MATRIX_PRISTINE="$MATRIX_WORK/_pristine-seed"
MATRIX_BASELINE="$MATRIX_WORK/_baseline"
MATRIX_STAGE_TIMEOUT="${MATRIX_STAGE_TIMEOUT:-90}"

# last-parsed cell, set by matrix_cell_parse
MATRIX_VT="" MATRIX_EDGE="" MATRIX_ALIGN="" MATRIX_DISPLAY=""

matrix_refuse() { echo "matrix: REFUSED: $*" >&2; return 2; }
matrix_fail()   { echo "matrix: FAIL: $*" >&2; return 1; }

# matrix_cell_parse <cell>: split "<vt>-<edge>-<align>-<display>" and validate
# every token. A malformed cell is refused here (return 2), the harness-side
# half of HC3's malformed-fixture proof (the generator refuses the other half).
matrix_cell_parse() {
    local cell="$1" IFS='-'
    read -r MATRIX_VT MATRIX_EDGE MATRIX_ALIGN MATRIX_DISPLAY extra <<<"$cell"
    if [[ -z "$MATRIX_VT" || -z "$MATRIX_EDGE" || -z "$MATRIX_ALIGN" || -z "$MATRIX_DISPLAY" || -n "${extra:-}" ]]; then
        matrix_refuse "cell '$cell' is not <viewType>-<edge>-<alignment>-<display>"; return 2
    fi
    case "$MATRIX_VT" in dock|panel) ;; *) matrix_refuse "cell '$cell': bad viewType '$MATRIX_VT'"; return 2;; esac
    case "$MATRIX_EDGE" in top|bottom|left|right) ;; *) matrix_refuse "cell '$cell': bad edge '$MATRIX_EDGE'"; return 2;; esac
    case "$MATRIX_ALIGN" in left|center|right|justify) ;; *) matrix_refuse "cell '$cell': bad alignment '$MATRIX_ALIGN'"; return 2;; esac
    case "$MATRIX_DISPLAY" in 1out|2out) ;; *) matrix_refuse "cell '$cell': bad display '$MATRIX_DISPLAY'"; return 2;; esac
    return 0
}

# matrix_init: snapshot the pristine seed the fixtures derive from. Runs once;
# the pristine is E2E_CONFIG_HOME as the vehicle first loaded it (a
# proven-loadable seed), captured before any stage mutates it.
matrix_init() {
    _e2e_require_nested matrix_init || return 2
    mkdir -p "$MATRIX_WORK"
    if [[ ! -d "$MATRIX_PRISTINE" ]]; then
        rm -rf "$MATRIX_PRISTINE"
        cp -r "${E2E_CONFIG_HOME:?}" "$MATRIX_PRISTINE"
        rm -f "$MATRIX_PRISTINE"/latte/*.bak
    fi
    return 0
}

# matrix_gen <cell> <out-dir>: run the fixture generator for a cell. Echoes the
# manifest JSON; returns 2 (refusal) if the generator rejects the descriptor.
matrix_gen() {
    local cell="$1" out="$2"
    matrix_cell_parse "$cell" || return 2
    #! P1 (C-I2): a 2out cell pins its view to the SECONDARY output discovered
    #! by the multi-output vehicle (tests/e2e/matrix/multi-output-lib.sh
    #! mo_discover_outputs exports E2E_MO_SECONDARY*). Absent that discovery (a
    #! single-output run, or any recipe that never sourced multi-output-lib),
    #! no --screen is passed and the fixture falls back to its sentinel id, so
    #! the dock REFUSES the 2out view rather than silently placing it on the
    #! primary - the degenerate case fails loud, per the observes-a-rejection
    #! rule. This is the only 2out-aware line; 1out generation is unchanged.
    local pin_args=()
    if [[ "$MATRIX_DISPLAY" == 2out && -n "${E2E_MO_SECONDARY:-}" ]]; then
        pin_args=(--screen "$E2E_MO_SECONDARY"
                  --screen-id "${E2E_MO_SECONDARY_ID:?multi-output-lib sets this alongside E2E_MO_SECONDARY}"
                  --screen-geometry "${E2E_MO_SECONDARY_GEOM:-}")
    fi
    python3 "$MATRIX_FIXTURE" \
        --seed-dir "$MATRIX_PRISTINE" --out-dir "$out" \
        --view-type "$MATRIX_VT" --edge "$MATRIX_EDGE" \
        --alignment "$MATRIX_ALIGN" --display "$MATRIX_DISPLAY" \
        --cell "$cell" "${pin_args[@]}" || return 2
}

# matrix_stage <cell>: generate the cell fixture, restart the vehicle dock onto
# it, and assert the view realized AS DECLARED (edge/type/alignment match the
# manifest). A fixture that stages but does not realize as declared is a
# refusal (return 2), never trusted as a silent wrong cell - this is the guard
# every scenario leans on.
matrix_stage() {
    _e2e_require_nested matrix_stage || return 2
    local cell="$1"
    local celldir="$MATRIX_WORK/$cell"
    local manifest
    manifest="$(matrix_gen "$cell" "$celldir")" || return 2

    # swap the dock onto the cell config: same config-home PATH (so E2E_LAYOUT
    # stays valid), fresh contents.
    e2e_dock_stop >/dev/null 2>&1 || true
    rm -rf "${E2E_CONFIG_HOME:?}"
    cp -r "$celldir" "$E2E_CONFIG_HOME"
    if ! e2e_dock_start "$MATRIX_STAGE_TIMEOUT"; then
        echo "matrix: dock did not settle on cell '$cell'; dock log tail:" >&2
        tail -20 "$E2E_DOCK_LOG" >&2 2>/dev/null || true
        matrix_refuse "cell '$cell' did not bring up a settled view (dock refused the fixture)"; return 2
    fi

    matrix_assert_realized "$cell" "$manifest" || return 2
    return 0
}

# matrix_assert_realized <cell> <manifest-json>: the single view under test must
# report the type/edge/alignment the manifest declared. Mismatch is a refusal.
matrix_assert_realized() {
    local cell="$1" manifest="$2" view got
    view="$(matrix_view_id)" || { matrix_refuse "cell '$cell': no view under test realized"; return 2; }
    got="$({ echo "$manifest"; e2e_json viewsData; } | python3 -c '
import json, sys
manifest = json.loads(sys.stdin.readline())
views = json.load(sys.stdin)
want = manifest["expect"]
v = next((v for v in views if v["containmentId"] == '"$view"'), None)
if v is None:
    sys.exit("no view "+str('"$view"'))
got = {"type": v["type"], "edge": v["edge"], "alignment": v["alignment"]}
if got != want:
    print("MISMATCH want=%s got=%s" % (json.dumps(want), json.dumps(got)))
else:
    print("OK")
')" || { matrix_refuse "cell '$cell': realization read failed: $got"; return 2; }
    if [[ "$got" != OK ]]; then
        matrix_refuse "cell '$cell' did not realize as declared: $got"; return 2
    fi
    return 0
}

# matrix_view_id: the containment id of the view under test. The fixture seeds
# exactly one non-cloned Latte view; refuse loudly if that is not what is up
# (a degenerate count is a symptom to surface, not to paper over).
matrix_view_id() {
    e2e_json viewsData | python3 -c '
import json, sys
views = [v for v in json.load(sys.stdin) if not v.get("isCloned")]
if len(views) != 1:
    sys.exit("matrix_view_id: expected exactly one non-cloned view, saw %d" % len(views))
print(views[0]["containmentId"])
'
}

# --- residue probes (the abort backbone reads these) ------------------------

# matrix_probe_view <view>: the residue-relevant view fields, normalized and
# deterministic. This is the core "did anything about the view change" surface.
matrix_probe_view() {
    local view="$1"
    { echo "$view"; e2e_json viewsData; } | python3 -c '
import json, sys
view = int(sys.stdin.readline())
views = json.load(sys.stdin)
v = next((v for v in views if v["containmentId"] == view), None)
if v is None:
    sys.exit("matrix_probe_view: view %d gone" % view)
fields = ["type", "edge", "alignment", "screen", "onPrimary", "editMode",
          "inConfigureAppletsMode", "isHidden", "isOffScreen",
          "strutsThickness", "publishedStruts", "maskRect", "inputRegionRects",
          "absoluteGeometry", "localGeometry", "screenGeometry"]
print(json.dumps({k: v.get(k) for k in fields}, sort_keys=True))
'
}

# matrix_probe_applets_order <view>: the applet visual order (as: cheap, stable).
matrix_probe_applets_order() {
    local view="$1"
    e2e_call viewAppletsOrder u "$view" 2>/dev/null | sed 's/^as [0-9]* //'
}

# matrix_probe_config: the layout config, KConfig-default-aware. Volatile keys
# that change without being residue (config-dialog geometry, preload weights,
# the runtime-resolved screen id) are stripped; everything else is sorted so a
# semantic change surfaces as a diff regardless of KConfig line ordering.
# NOTE (per-verb refinement): abort assertions that must reason about a key
# returning to its default (KConfig deletes it - the plan's default-key trap)
# extend this denylist in their scenario chunk; the backbone ships the common set.
matrix_probe_config() {
    local layout="${E2E_LAYOUT:?}"
    python3 - "$layout" <<'PY'
import sys, re
path = sys.argv[1]
# keys whose value legitimately breathes across a restart / an interaction
# without being abort residue
VOLATILE = re.compile(r'^(DialogHeight|DialogWidth|PreloadWeight|lastScreen|'
                      r'configurationSticker|timerShow|timerHide)=')
lines = []
group = None
groups = {}
order = []
with open(path) as fh:
    for raw in fh:
        line = raw.rstrip("\n")
        s = line.strip()
        if s.startswith("[") and s.endswith("]"):
            group = s
            if group not in groups:
                groups[group] = []
                order.append(group)
        elif s and group is not None and not VOLATILE.match(s):
            groups[group].append(s)
for g in sorted(order):
    print(g)
    for k in sorted(groups[g]):
        print(k)
PY
}

# --- baseline backbone ------------------------------------------------------

# matrix_baseline_capture <view> <verb>: snapshot every residue field to
# $MATRIX_BASELINE. Includes the verb's own probe so the check is verb-aware.
matrix_baseline_capture() {
    local view="$1" verb="$2"
    rm -rf "$MATRIX_BASELINE"; mkdir -p "$MATRIX_BASELINE"
    matrix_probe_view "$view"          > "$MATRIX_BASELINE/view"
    matrix_probe_applets_order "$view" > "$MATRIX_BASELINE/applets_order"
    matrix_probe_config                > "$MATRIX_BASELINE/config"
    matrix_verb_probe "$verb" "$view"  > "$MATRIX_BASELINE/verb"
    if [[ "${MATRIX_CAPTURE_FRAME:-0}" == 1 ]]; then
        # visual-only residue (PR #20 ghost overlay): the clean baseline frame.
        # Cursor excluded so a pointer moved between capture and re-shoot cannot
        # masquerade as residue. The pixel comparison is the golden bridge's job
        # (matrix_frame_equals_baseline below); this only preserves the frame.
        e2e_screenshot "$MATRIX_BASELINE/frame.png" include-cursor b false 2>/dev/null || true
    fi
}

# matrix_assert_baseline_restored <view> <verb>: re-read every field and assert
# byte-identical to the captured baseline. Any diff is residue -> return 1,
# naming the field so the failure localizes.
matrix_assert_baseline_restored() {
    local view="$1" verb="$2" bad=0 field now
    for field in view applets_order config verb; do
        case "$field" in
            view)          now="$(matrix_probe_view "$view")";;
            applets_order) now="$(matrix_probe_applets_order "$view")";;
            config)        now="$(matrix_probe_config)";;
            verb)          now="$(matrix_verb_probe "$verb" "$view")";;
        esac
        if [[ "$now" != "$(cat "$MATRIX_BASELINE/$field")" ]]; then
            echo "matrix: RESIDUE in '$field' after abort:" >&2
            diff <(cat "$MATRIX_BASELINE/$field") <(printf '%s\n' "$now") >&2 || true
            bad=1
        fi
    done
    if [[ "${MATRIX_CAPTURE_FRAME:-0}" == 1 && -f "$MATRIX_BASELINE/frame.png" ]]; then
        local nowframe="$MATRIX_BASELINE/frame.after.png"
        if e2e_screenshot "$nowframe" include-cursor b false 2>/dev/null; then
            if ! matrix_frame_equals_baseline "$MATRIX_BASELINE/frame.png" "$nowframe"; then
                echo "matrix: VISUAL RESIDUE: post-abort frame differs from the clean baseline" >&2
                bad=1
            fi
        fi
    fi
    (( bad == 0 )) && return 0
    return 1
}

# matrix_frame_equals_baseline <baseline.png> <after.png>: the visual-residue
# comparison for the abort backbone (P2 / C-I6 hook, now LIVE). Routes through
# the render-golden bridge's tier-aware latte-imgdiff compare instead of the
# old raw byte floor, so live-compositor noise is tolerated at the interaction
# Tolerance tier while a real ghost-overlay residue (the PR #20 class) still
# shows as a diff. The clean baseline frame captured before the abort IS the
# expected image, so no NEW golden is blessed here: residue is a diff against a
# known-clean frame, not against a committed reference.
matrix_frame_equals_baseline() {
    local baseline="$1" after="$2"
    e2e_golden_compare "$after" "$baseline"
}

# --- verb dispatch ----------------------------------------------------------

# matrix_verb_drive <verb> <view> <outcome>: dispatch to the verb's driver,
# refusing an unknown verb (boundary check, not a silent no-op).
matrix_verb_drive() {
    local verb="$1" view="$2" outcome="$3"
    if ! declare -F "matrix_verb_${verb}_drive" >/dev/null; then
        matrix_refuse "unknown verb '$verb' (no matrix_verb_${verb}_drive defined)"; return 2
    fi
    "matrix_verb_${verb}_drive" "$view" "$outcome"
}

# matrix_verb_probe <verb> <view>: dispatch to the verb's probe.
matrix_verb_probe() {
    local verb="$1" view="$2"
    if ! declare -F "matrix_verb_${verb}_probe" >/dev/null; then
        matrix_refuse "unknown verb '$verb' (no matrix_verb_${verb}_probe defined)"; return 2
    fi
    "matrix_verb_${verb}_probe" "$view"
}

# --- scenario composition ---------------------------------------------------

matrix_scenario_commit() {
    local cell="$1" verb="$2" expected="$3" view actual
    matrix_stage "$cell" || return 2
    view="$(matrix_view_id)" || return 2
    matrix_verb_drive "$verb" "$view" commit || return 2
    actual="$(matrix_verb_probe "$verb" "$view")" || return 2
    if [[ "$actual" == "$expected" ]]; then
        echo "matrix: PASS commit $cell/$verb -> $actual"
        return 0
    fi
    matrix_fail "commit $cell/$verb: expected '$expected' got '$actual'"
}

matrix_scenario_abort() {
    local cell="$1" verb="$2" view
    matrix_stage "$cell" || return 2
    view="$(matrix_view_id)" || return 2
    matrix_baseline_capture "$view" "$verb"
    matrix_verb_drive "$verb" "$view" abort || return 2
    if matrix_assert_baseline_restored "$view" "$verb"; then
        echo "matrix: PASS abort $cell/$verb -> baseline restored (no residue)"
        return 0
    fi
    matrix_fail "abort $cell/$verb: residue against baseline"
}

# --- built-in verb: editmode ------------------------------------------------
# Uses ONLY the existing setViewEditMode D-Bus action, so the harness self-tests
# a real interaction with no dependency on the P3-P9 drivers. Commit = enter
# edit mode; abort = enter then exit (the no-op edit session, the A5 shape).

matrix_verb_editmode_drive() {
    local view="$1" outcome="$2"
    e2e_call setViewEditMode ub "$view" true >/dev/null || return 1
    # wait for the flip to land in the readback (edit chrome comes up async)
    local i
    for ((i = 0; i < 30; i++)); do
        [[ "$(matrix_verb_editmode_probe "$view")" == true ]] && break
        sleep 0.2
    done
    if [[ "$outcome" == abort ]]; then
        e2e_call setViewEditMode ub "$view" false >/dev/null || return 1
        for ((i = 0; i < 30; i++)); do
            [[ "$(matrix_verb_editmode_probe "$view")" == false ]] && break
            sleep 0.2
        done
    fi
    return 0
}

matrix_verb_editmode_probe() {
    local view="$1"
    { echo "$view"; e2e_json viewsData; } | python3 -c '
import json, sys
view = int(sys.stdin.readline())
views = json.load(sys.stdin)
v = next((v for v in views if v["containmentId"] == view), None)
if v is None:
    sys.exit("editmode probe: view %d gone" % view)
print("true" if v["editMode"] else "false")
'
}
