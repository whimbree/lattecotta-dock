# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# The edit-mode settings audit HARNESS (docs/tracking/edit-mode-settings-audit-plan.md,
# cluster CL-0). This is the Tier B / Tier C / Tier D driver every per-control
# cluster (CL-1 length, CL-2 appearance, CL-3 behavior, CL-4 effects, CL-5
# tasks, CL-6 chrome) calls. It is a SUPERSET of the e2e matrix harness, NOT a
# copy: it sources tests/e2e/matrix/matrix-lib.sh and reuses the nested vehicle,
# matrix_stage, setViewEditMode and viewsData wholesale, adding only what the
# semantic audit needs on top - the config-value snapshot/diff and the
# settings-window drive helpers.
#
# ============================ HARNESS API ==================================
# Call order in a cluster recipe:
#   source tests/e2e/lib.sh
#   source tests/e2e/matrix/matrix-lib.sh
#   source tests/e2e/audit/audit-lib.sh
#
# SNAPSHOTS (the config VALUES a control writes, read from the in-process map
# via the viewConfigData / appletConfigData readbacks - NEVER the on-disk file,
# so the KConfig default-deletion trap cannot corrupt the diff):
#   audit_config_snapshot <view> > file        # the containment "config" object
#   audit_view_snapshot   <view> > file        # the live C++ "view" object
#   audit_applet_config_snapshot <view> <appletId> > file   # a child applet's
#                                                            # config (tasks/CL-5)
# Each snapshot is sorted `key<TAB>json-value` lines: stable, diffable, and
# order-independent.
#
# TIER B / P2 - which keys a drive changed (the decisive right-key check):
#   audit_changed_keys <before> <after>        # sorted changed keys, one/line
#   audit_assert_applies    <before> <after> <key>          # P1: key changed
#   audit_assert_only_keys  <before> <after> <key...>       # P2: EXACT key set
# TIER C/D / P3 - a control reflects state / two surfaces agree:
#   audit_assert_reflects <snapshot> <key> <json-value>     # P3: value present
#   audit_assert_agrees   <snapA> <keyA> <snapB> <keyB>     # P4: two surfaces
#                                                           #     hold one value
# Every assert returns 0 PASS, 1 FAIL (loud on stderr), 2 REFUSED (bad input).
# HC3: audit_assert_only_keys FAILS on a stray/missing key, audit_assert_applies
# FAILS on a no-change - proven in tests/e2e/audit-harness-selftest.sh and in the
# C++ harness tests/settingswiringharnesstest.cpp. A harness that only passes the
# happy path cannot find the suspected-broken controls.
#
# SETTINGS-WINDOW DRIVE (the mapped config window setViewEditMode brings up):
#   audit_enter_editmode <view>                # edit mode ON + settle (rulers +
#                                              # the settings config window)
#   audit_exit_editmode  <view>                # edit mode OFF + settle
#   audit_settings_window_rect                 # "x y w h" of the config window
#   audit_settings_click <xfrac> <yfrac>       # click at a fractional position
#                                              # inside the config window
#   audit_settings_drag  <xfrac> <yfrac> <dx> <dy>   # press-move-release a
#                                                    # slider handle
# The fractional drivers are deliberate scaffolding: a cluster refines the
# fraction per control against the window dump, or reaches for the cheaper
# QML-handler wiring harness (tests/settingswiringharnesstest.cpp) where only
# the WIRING (not the pixel feel) is the question.
# ===========================================================================

_audit_refuse() { echo "audit: REFUSED: $*" >&2; return 2; }
_audit_fail()   { echo "audit: FAIL: $*" >&2; return 1; }

# --- snapshots --------------------------------------------------------------

# _audit_snapshot_object <method> <args...> | <object-key>: fetch a JSON readback
# and print the named sub-object as sorted `key<TAB>json-value` lines. A missing
# object or a call failure surfaces loudly (never a silent empty snapshot a
# later broken read would falsely match).
_audit_snapshot_object() {
    local object="$1"; shift
    { echo "$object"; e2e_json "$@"; } | python3 -c '
import json, sys
object_key = sys.stdin.readline().strip()
try:
    payload = json.load(sys.stdin)
except json.JSONDecodeError as e:
    sys.exit("audit: snapshot payload is not JSON: %s" % e)
obj = payload.get(object_key)
if not isinstance(obj, dict):
    sys.exit("audit: snapshot payload has no %r object" % object_key)
for key in sorted(obj):
    print("%s\t%s" % (key, json.dumps(obj[key], sort_keys=True)))
'
}

# audit_config_snapshot <view>: the containment config VALUES.
audit_config_snapshot() {
    local view="$1"
    [[ -n "$view" ]] || { _audit_refuse "audit_config_snapshot needs a view id"; return 2; }
    _audit_snapshot_object config viewConfigData u "$view"
}

# audit_view_snapshot <view>: the live C++-property half of viewConfigData.
audit_view_snapshot() {
    local view="$1"
    [[ -n "$view" ]] || { _audit_refuse "audit_view_snapshot needs a view id"; return 2; }
    _audit_snapshot_object view viewConfigData u "$view"
}

# audit_applet_config_snapshot <view> <appletId>: a child applet's config VALUES
# (the tasks plasmoid for CL-5). The applet id comes from viewAppletsData.
audit_applet_config_snapshot() {
    local view="$1" applet="$2"
    [[ -n "$view" && -n "$applet" ]] || { _audit_refuse "audit_applet_config_snapshot needs <view> <appletId>"; return 2; }
    _audit_snapshot_object config appletConfigData uu "$view" "$applet"
}

# audit_tasks_applet_id <view>: the applet id of the single latte tasks plasmoid
# under <view>, resolved from viewAppletsData (what CL-5 feeds
# audit_applet_config_snapshot). Refuses if not present exactly once.
audit_tasks_applet_id() {
    local view="$1"
    { echo "$view"; e2e_json viewAppletsData u "$view"; } | python3 -c '
import json, sys
view = sys.stdin.readline().strip()
applets = json.load(sys.stdin)
m = [a for a in applets if a["plugin"] == "org.kde.latte.plasmoid"]
if len(m) != 1:
    sys.exit("audit_tasks_applet_id: expected exactly one tasks plasmoid under view %s, saw %d" % (view, len(m)))
print(m[0]["id"])
'
}

# --- diff / assertions ------------------------------------------------------

# audit_changed_keys <before-file> <after-file>: the sorted set of keys whose
# value differs between two snapshots, one per line. SAFE DIRECTION (mirrors
# tests/units/configsnapshotdiff.h changedConfigKeys): a key present on exactly
# one side counts as changed - a loud false-FAIL, never a silent false-PASS.
audit_changed_keys() {
    local before="$1" after="$2"
    if [[ ! -f "$before" || ! -f "$after" ]]; then
        echo "audit: changed_keys: a snapshot file is missing ($before / $after)" >&2
        return 2
    fi
    python3 - "$before" "$after" <<'PY'
import sys
def load(path):
    d = {}
    with open(path) as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line:
                continue
            key, _, value = line.partition("\t")
            d[key] = value
    return d
before, after = load(sys.argv[1]), load(sys.argv[2])
for key in sorted(set(before) | set(after)):
    if before.get(key) != after.get(key):
        print(key)
PY
}

# audit_assert_applies <before> <after> <key>: P1 - driving the control changed
# the key it is labelled for. A no-change is a FAIL (the D10 dead-control class).
audit_assert_applies() {
    local before="$1" after="$2" key="$3" changed
    [[ -n "$key" ]] || { _audit_refuse "audit_assert_applies needs a key"; return 2; }
    changed="$(audit_changed_keys "$before" "$after")" || return 2
    if grep -qxF "$key" <<<"$changed"; then
        return 0
    fi
    _audit_fail "P1 applies: key '$key' did not change (a control that writes nothing readable)"
}

# audit_assert_only_keys <before> <after> <key...>: P2 - the EXACT set of keys
# that changed is the expected set. A stray side-effect key (the D15 coupling)
# or a missing expected key is a FAIL. This is the decisive right-key check.
audit_assert_only_keys() {
    local before="$1" after="$2"; shift 2
    local changed expected
    changed="$(audit_changed_keys "$before" "$after")" || return 2
    expected="$(printf '%s\n' "$@" | sort -u | sed '/^$/d')"
    if [[ "$changed" == "$expected" ]]; then
        return 0
    fi
    {
        echo "audit: FAIL: P2 right-key-only: changed key set != expected"
        diff <(printf '%s\n' "$expected") <(printf '%s\n' "$changed") | sed 's/^/audit:   (-expected +changed) /' || true
    } >&2
    return 1
}

# audit_assert_reflects <snapshot> <key> <json-value>: P3 - the snapshot carries
# the key at the expected value. The value is a JSON literal (90, true, "x"), the
# same form audit_config_snapshot stores.
audit_assert_reflects() {
    local snap="$1" key="$2" want="$3" have
    [[ -f "$snap" ]] || { _audit_refuse "audit_assert_reflects: snapshot missing: $snap"; return 2; }
    have="$(awk -F'\t' -v k="$key" '$1 == k {print $2; found=1} END {exit found ? 0 : 3}' "$snap")" \
        || { _audit_fail "P3 reflects: key '$key' absent from snapshot"; return 1; }
    if [[ "$have" == "$want" ]]; then
        return 0
    fi
    _audit_fail "P3 reflects: key '$key' is '$have', expected '$want'"
}

# audit_assert_agrees <snapA> <keyA> <snapB> <keyB>: P4 - two surfaces report the
# same value for one logical setting (e.g. the config value vs a second
# readback). The keys may differ because the two surfaces name the value
# differently; the VALUES must match.
audit_assert_agrees() {
    local snapA="$1" keyA="$2" snapB="$3" keyB="$4" a b
    [[ -f "$snapA" && -f "$snapB" ]] || { _audit_refuse "audit_assert_agrees: a snapshot is missing"; return 2; }
    a="$(awk -F'\t' -v k="$keyA" '$1 == k {print $2; found=1} END {exit found ? 0 : 3}' "$snapA")" \
        || { _audit_fail "P4 agrees: key '$keyA' absent from first snapshot"; return 1; }
    b="$(awk -F'\t' -v k="$keyB" '$1 == k {print $2; found=1} END {exit found ? 0 : 3}' "$snapB")" \
        || { _audit_fail "P4 agrees: key '$keyB' absent from second snapshot"; return 1; }
    if [[ "$a" == "$b" ]]; then
        return 0
    fi
    _audit_fail "P4 agrees: '$keyA'='$a' but '$keyB'='$b' - two views of one value disagree"
}

# --- settings-window drive ---------------------------------------------------

# audit_enter_editmode <view>: turn edit mode ON (the canvas rulers plus the
# settings config window) and settle, reusing the matrix editmode verb's proven
# flip-poll + geometry-settle so a snapshot never races the animation.
audit_enter_editmode() {
    local view="$1"
    e2e_call setViewEditMode ub "$view" true >/dev/null || return 1
    local i
    for ((i = 0; i < 30; i++)); do
        [[ "$(matrix_verb_editmode_probe "$view")" == true ]] && break
        sleep 0.2
    done
    e2e_wait_settled 15 || true
    [[ "$(matrix_verb_editmode_probe "$view")" == true ]] || { _audit_fail "edit mode never turned on for view $view"; return 1; }
}

# audit_exit_editmode <view>: turn edit mode OFF and settle.
audit_exit_editmode() {
    local view="$1"
    e2e_call setViewEditMode ub "$view" false >/dev/null || return 1
    local i
    for ((i = 0; i < 30; i++)); do
        [[ "$(matrix_verb_editmode_probe "$view")" == false ]] && break
        sleep 0.2
    done
    e2e_wait_settled 15 || true
}

# audit_settings_window_rect: the mapped settings config window as "x y w h",
# located from the compositor dump by its shape (tall and wide, not a dock
# strip - the same heuristic settings-window-onscreen.sh uses). Empty (return 1)
# if no config window is mapped, so a caller cannot silently drive nothing.
audit_settings_window_rect() {
    local rect
    rect="$(e2e_dumpwins | awk -F'|' '
        $2 ~ /latte-dock/ {
            split($4, g, " "); split(g[1], pos, ","); split(g[2], size, "x");
            if (size[2] > 400 && size[1] > 300 && size[1] < 2000) {
                printf "%d %d %d %d\n", pos[1], pos[2], size[1], size[2]; exit
            }
        }')"
    [[ -n "$rect" ]] || { echo "audit: no settings config window mapped" >&2; return 1; }
    echo "$rect"
}

# audit_settings_click <xfrac> <yfrac>: click at a fractional position inside the
# settings config window (0,0 = top-left, 1,1 = bottom-right). A cluster tunes
# the fraction per control against the window dump.
audit_settings_click() {
    local xf="$1" yf="$2" rect x y w h px py
    rect="$(audit_settings_window_rect)" || return 1
    read -r x y w h <<<"$rect"
    px="$(python3 -c "print(int($x + $xf * $w))")"
    py="$(python3 -c "print(int($y + $yf * $h))")"
    "$E2E_FAKEPOINTER" click "$px" "$py"
    e2e_wait_settled 10 || true
}

# audit_settings_drag <xfrac> <yfrac> <dx> <dy>: press at a fractional position
# inside the config window, move by (dx,dy) pixels, release - the slider-drag
# primitive (a cluster tunes the start fraction and delta per slider).
audit_settings_drag() {
    local xf="$1" yf="$2" dx="$3" dy="$4" rect x y w h px py
    rect="$(audit_settings_window_rect)" || return 1
    read -r x y w h <<<"$rect"
    px="$(python3 -c "print(int($x + $xf * $w))")"
    py="$(python3 -c "print(int($y + $yf * $h))")"
    "$E2E_FAKEPOINTER" drag "$px" "$py" "$((px + dx))" "$((py + dy))"
    e2e_wait_settled 10 || true
}
