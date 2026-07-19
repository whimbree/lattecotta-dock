# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# The applet-REORDER driver (P6 / C-I7, docs/tracking/e2e-interaction-test-plan.md
# sections 5, 7 A2, 9 G2). The reusable surface the F3 committed-reorder and
# the A2 abort chunks (C-S6/C-S7/C-A2/C-A2b) drive through - it does for
# containment applets in rearrange mode what 050-drag-reorder-launchers does
# for tasks inside the tasks applet, with the abort variants A2 needs.
#
# Sourced after tests/e2e/lib.sh (a scenario recipe also sources matrix-lib.sh
# for the verb hookup at the bottom). Runs inside the nested vehicle.
#
# ============================ WHY A DRIVING SURFACE =========================
# The ConfigOverlay drag machinery is only live when a view is BOTH in edit
# mode AND the global inConfigureAppletsMode sub-mode is set (main.qml:
# `inConfigureAppletsMode: editMode && universalSettings.inConfigureAppletsMode`).
# That sub-mode is transient - never persisted, deleted on config load
# (universalsettings.cpp) - so no config seed can reach it. The
# setViewConfiguringApplets D-Bus action (C-I7) is the driving surface: it flips
# the same global flag the settings-header rearrange button does, refusing
# loudly if the target view is not already in edit mode. Parabolic zoom is OFF
# in this sub-mode (ParabolicEffect.qml `isEnabled: ... && !inConfigureAppletsMode`),
# so the per-applet geometry viewAppletsData reports is stable and drag aiming
# needs no pixel calibration - the icon-shift trap 050 fights does not apply here.
#
# ============================ DRIVER API ===================================
#   applet_reorder_enter <view>          enter edit + rearrange, settle
#   applet_reorder_exit  <view>          leave rearrange + edit, settle
#   applet_reorder_order <view>          the reorderable applet-id order (space-sep)
#   applet_reorder_z     <view> <id>     one applet delegate's stacking z (G2)
#   applet_reorder_glide <view> <mode> <from> <to>
#         Pointer choreography for one attempt, rearrange ASSUMED already
#         entered (so the caller can compose it into a larger scenario).
#         <from>/<to> are VISUAL indices into applet_reorder_order. Modes:
#           commit   cross applet[to] and release there (a real reorder)
#           origin   glide onto applet[to] then back, release at the START
#                    center (release-at-origin abort: motion, net no move)
#           noop     nudge within applet[from]'s own slot, never crossing a
#                    neighbour midpoint (the HC3 REFUSED reorder: motion, but
#                    the order cannot change)
#           jitter   out->back->out reverse-jitter (DR-2), release past applet[to]
#           escape   dragkey Escape mid-drag before release (DR-6): taps Escape
#                    while the button is held - the escape-in-held-drag path
#           occupied release squarely on applet[to]'s center (T1a occupied-drop)
#           overflow release BEYOND applet[to]'s far edge (T1c past-the-last-slot)
#   applet_reorder_glide_to <view> <from> <rx> <ry> [via_x via_y]
#         Arbitrary-target press-glide-release (justify splitter seam T1b, a
#         foreign window, off-screen coords) - the caller supplies the point.
#         The edit-exit-mid-drag strand (T4c) needs a button held ACROSS a
#         D-Bus setViewEditMode call, which is C-I1's DR-4 mid-drag hook, not an
#         atomic-drag primitive here; it composes DR-4 with this driver in C-A2b.
#   applet_reorder_attempt <view> <mode> <from> <to>
#         enter -> glide(<mode>) -> exit+settle, the whole self-contained cycle
#         the committed/abort scenarios use. Returns:
#           0  the attempt COMMITTED a reorder (order changed)
#           3  the attempt was a NO-OP / REFUSED reorder (order UNCHANGED) -
#              a first-class outcome, NOT a failure: a noop/origin/escape abort
#              MUST land here, and this is the value HC3 asserts on
#           1  a real driver error (readback failed, rearrange stuck)
# ===========================================================================

# --- readbacks --------------------------------------------------------------

# applet_reorder_order <view>: the reorderable applet-instance-id order, the
# same splitter-free order viewAppletsData draws, space-separated on one line.
# A D-Bus failure surfaces loudly (never a plausible-but-empty order that would
# read "unchanged" on both sides of an abort - the never-swallow rule).
applet_reorder_order() {
    local view="$1" raw
    if ! raw="$(e2e_call viewAppletsOrder u "$view")"; then
        echo "applet_reorder_order: viewAppletsOrder call FAILED for view $view" >&2
        return 1
    fi
    case "$raw" in
        "as "*) awk '{for (i = 3; i <= NF; i++) printf "%s%s", $i, (i < NF ? " " : "\n")}' <<<"$raw";;
        *) echo "applet_reorder_order: reply is not an 'as' array: $raw" >&2; return 1;;
    esac
}

# applet_reorder_z <view> <applet-id>: the stacking z of one applet's delegate
# (the G2 readback). Used to prove an abort left no applet stranded over chrome.
applet_reorder_z() {
    local view="$1" id="$2"
    { echo "$id"; e2e_json viewAppletsData u "$view"; } | python3 -c '
import json, sys
want = int(sys.stdin.readline())
applets = json.load(sys.stdin)
a = next((a for a in applets if a["id"] == want), None)
if a is None:
    sys.exit("applet_reorder_z: applet %d not present in view" % want)
print(a["z"])
'
}

# --- rearrange lifecycle ----------------------------------------------------

# _applet_reorder_flag <view> <field>: read one bool view field (editMode /
# inConfigureAppletsMode) as true/false.
_applet_reorder_flag() {
    local view="$1" field="$2"
    { echo "$view $field"; e2e_json viewsData; } | python3 -c '
import json, sys
view, field = sys.stdin.readline().split()
views = json.load(sys.stdin)
v = next((v for v in views if v["containmentId"] == int(view)), None)
if v is None:
    sys.exit("view %s gone" % view)
print("true" if v[field] else "false")
'
}

# applet_reorder_enter <view>: open the view's edit session, then the rearrange
# sub-mode, polling each flip in the readback (both come up async), then settle
# (the dock grows to editThickness; geometry must stop moving before aiming).
applet_reorder_enter() {
    local view="$1" i
    e2e_call setViewEditMode ub "$view" true >/dev/null || return 1
    for ((i = 0; i < 40; i++)); do
        [[ "$(_applet_reorder_flag "$view" editMode)" == true ]] && break
        sleep 0.2
    done
    [[ "$(_applet_reorder_flag "$view" editMode)" == true ]] \
        || { echo "applet_reorder_enter: view $view never entered edit mode" >&2; return 1; }

    e2e_call setViewConfiguringApplets ub "$view" true >/dev/null || return 1
    for ((i = 0; i < 40; i++)); do
        [[ "$(_applet_reorder_flag "$view" inConfigureAppletsMode)" == true ]] && break
        sleep 0.2
    done
    [[ "$(_applet_reorder_flag "$view" inConfigureAppletsMode)" == true ]] \
        || { echo "applet_reorder_enter: view $view never entered rearrange mode" >&2; return 1; }

    e2e_wait_settled 15 || true
    return 0
}

# applet_reorder_exit <view>: leave rearrange, then edit, and settle back to the
# non-edit baseline geometry. Best-effort on each flag (main.qml also resets
# inConfigureAppletsMode on edit exit), but edit mode must actually clear.
applet_reorder_exit() {
    local view="$1" i
    e2e_call setViewConfiguringApplets ub "$view" false >/dev/null 2>&1 || true
    e2e_call setViewEditMode ub "$view" false >/dev/null 2>&1 || true
    for ((i = 0; i < 40; i++)); do
        [[ "$(_applet_reorder_flag "$view" editMode)" == false ]] && break
        sleep 0.2
    done
    e2e_wait_settled 15 || true
    [[ "$(_applet_reorder_flag "$view" editMode)" == false ]] \
        || { echo "applet_reorder_exit: view $view stuck in edit mode" >&2; return 1; }
    return 0
}

# --- coordinate model -------------------------------------------------------

# _applet_reorder_points <view> <from> <to>: resolve the screen-pixel
# choreography for one attempt from viewAppletsData geometry + the view window
# origin (absoluteGeometry - localGeometry; render agrees with reported since
# the Phase 8 drift fix, guarded by 060). Emits, on one line, integers:
#   axis  sx sy  apx apy  mx my  crossx crossy  ctrx ctry  overx overy \
#     ox oy  retx rety  nx ny  fromid toid
# axis is h (top/bottom, x-drag) or v (left/right, y-drag). Points:
#   s      applet[from] center (press + arm point)
#   ap     just OUTSIDE applet[from] on the thickness axis (the off-item hover
#          that arms currentApplet before the press)
#   m      midpoint between applet[from] and applet[to] centers
#   cross  PAST applet[to] center into its far half (a committed cross-neighbour)
#   ctr    applet[to] EXACT center (occupied-drop, the midpoint branch)
#   over   BEYOND applet[to] far edge (overflow / past-the-last-slot)
#   o      origin = applet[from] center (release-at-origin)
#   ret    PAST origin into applet[from] slot far half: the release point for a
#          cross-then-return abort. Releasing exactly at o lands on the reflowed
#          neighbour center (it slid into the origin slot when the drag crossed)
#          = the ambiguous midpoint boundary that STAYS crossed; ret overshoots
#          into that neighbour near half so the placeHolder re-crosses back to
#          the origin slot and the reorder nets to nothing
#   n      a nudge INSIDE applet[from] own slot, never reaching a neighbour
#          midpoint (the refused reorder: motion, order cannot change)
_applet_reorder_points() {
    local view="$1" from="$2" to="$3"
    { echo "$view $from $to"; e2e_json viewsData; e2e_json viewAppletsData u "$view"; } | python3 -c '
import json, sys
view, frm, to = (int(x) for x in sys.stdin.readline().split())
#! two compact JSON docs, one per line: json.loads(line), not json.load(stdin)
#! (which would read both lines as one document = "Extra data")
views = json.loads(sys.stdin.readline())
applets = json.loads(sys.stdin.readline())
v = next((v for v in views if v["containmentId"] == view), None)
if v is None:
    sys.exit("view %d gone" % view)
if not (0 <= frm < len(applets)) or not (0 <= to < len(applets)):
    sys.exit("from/to out of range (have %d applets)" % len(applets))
ax, ay = v["absoluteGeometry"][:2]
lx, ly = v["localGeometry"][:2]
ox0, oy0 = ax - lx, ay - ly
horizontal = v["edge"] in ("top", "bottom")

def center(a):
    px, py, pw, ph = a["geometry"]
    return (ox0 + px + pw / 2.0, oy0 + py + ph / 2.0, pw, ph)

fx, fy, fw, fh = center(applets[frm])
tx, ty, tw, th = center(applets[to])

if horizontal:
    axis, size_from, size_to, span = "h", fw, tw, tx - fx
else:
    axis, size_from, size_to, span = "v", fh, th, ty - fy
sgn = 1 if span >= 0 else -1
nudge = sgn * min(abs(span) * 0.30, size_from * 0.25) if span else size_from * 0.10

if horizontal:
    s = (fx, fy)
    ap = (fx, fy - fh)                        # from above the item
    m = ((fx + tx) / 2.0, fy)
    cross = (tx + sgn * 0.35 * tw, ty)
    ctr = (tx, ty)
    over = (tx + sgn * 0.90 * tw, ty)
    ret = (fx - sgn * 0.30 * fw, fy)          # overshoot origin, far half
    n = (fx + nudge, fy)
else:
    s = (fx, fy)
    ap = (fx - fw, fy)                        # from left of the item
    m = (fx, (fy + ty) / 2.0)
    cross = (tx, ty + sgn * 0.35 * th)
    ctr = (tx, ty)
    over = (tx, ty + sgn * 0.90 * th)
    ret = (fx, fy - sgn * 0.30 * fh)          # overshoot origin, far half
    n = (fx, fy + nudge)

pts = [s, ap, m, cross, ctr, over, s, ret, n]
out = [axis]
for p in pts:
    out += [str(int(round(p[0]))), str(int(round(p[1])))]
out += [str(applets[frm]["id"]), str(applets[to]["id"])]
print(" ".join(out))
'
}

# --- pointer choreography ---------------------------------------------------

# applet_reorder_glide <view> <mode> <from> <to>: one attempt. Rearrange mode is
# ASSUMED entered. Arms currentApplet with an off-item hover, presses, and
# finishes per <mode> (see the header). Does not enter/exit/settle.
applet_reorder_glide() {
    local view="$1" mode="$2" from="$3" to="$4" pts
    pts="$(_applet_reorder_points "$view" "$from" "$to")" \
        || { echo "applet_reorder_glide: could not resolve points" >&2; return 1; }
    local axis sx sy apx apy mx my crx cry ctrx ctry ovx ovy ox oy rtx rty nx ny fromid toid
    read -r axis sx sy apx apy mx my crx cry ctrx ctry ovx ovy ox oy rtx rty nx ny fromid toid <<<"$pts"

    #! arm currentApplet: ConfigOverlay.onPressed returns early unless a hover
    #! already set dragOverlay.currentApplet, so approach from off the item and
    #! glide onto its center first (an unpressed motion stream sets it).
    "$E2E_FAKEPOINTER" move "$apx" "$apy"; sleep 0.25
    "$E2E_FAKEPOINTER" glide "$apx" "$apy" "$sx" "$sy"; sleep 0.35

    case "$mode" in
        commit)
            "$E2E_FAKEPOINTER" drag "$sx" "$sy" "$mx" "$my" "$crx" "$cry";;
        occupied)
            #! release squarely on applet[to] center (T1a): the midpoint /
            #! degenerate-hovered branch, no deliberate far-half overshoot
            "$E2E_FAKEPOINTER" drag "$sx" "$sy" "$mx" "$my" "$ctrx" "$ctry";;
        overflow)
            #! release BEYOND applet[to] far edge (T1c past-the-last-slot when
            #! to is the final applet): the distance-fallback / index>=count arm
            "$E2E_FAKEPOINTER" drag "$sx" "$sy" "$mx" "$my" "$ovx" "$ovy";;
        origin)
            #! CROSS applet[to], then return and release in the origin slot far
            #! half (ret, not the exact origin center - see the model header):
            #! the placeHolder re-crosses back, so a reorder that DID engage nets
            #! to no change (the release-at-origin abort)
            "$E2E_FAKEPOINTER" drag "$sx" "$sy" "$mx" "$my" "$crx" "$cry" "$mx" "$my" "$rtx" "$rty";;
        noop)
            #! nudge within applet[from] own slot; never crosses a neighbour
            "$E2E_FAKEPOINTER" drag "$sx" "$sy" "$nx" "$ny" "$ox" "$oy";;
        jitter)
            #! out->back->out reverse-jitter (DR-2), then release past applet[to]
            "$E2E_FAKEPOINTER" drag "$sx" "$sy" "$crx" "$cry" "$sx" "$sy" "$crx" "$cry";;
        escape)
            #! DR-6: tap Escape WHILE the button is held (dragkey), then release.
            #! Escape's real effect on the ConfigOverlay reorder is observed by
            #! the caller (the MouseArea has no Keys handler - see the plan's
            #! ESCAPE-vs-RELEASE finding), never assumed to cancel.
            "$E2E_FAKEPOINTER" dragkey Escape "$sx" "$sy" "$mx" "$my" "$crx" "$cry";;
        *)
            echo "applet_reorder_glide: unknown mode '$mode'" >&2; return 2;;
    esac
    sleep 1.2
    return 0
}

# applet_reorder_glide_to <view> <from> <rx> <ry> [via_x via_y]: the arbitrary-
# target primitive for the adversarial drops whose release point is NOT an
# applet center - a justify ZONE-BOUNDARY / splitter seam (T1b), a foreign
# window, off-screen coords. Arms applet[from], presses, glides through the
# optional via waypoint to (rx,ry), releases. The caller computes the seam
# coordinate (it knows the justify layout the driver's applet-center model does
# not expose splitters for). Rearrange ASSUMED entered.
applet_reorder_glide_to() {
    local view="$1" from="$2" rx="$3" ry="$4" viax="${5:-}" viay="${6:-}" pts
    pts="$(_applet_reorder_points "$view" "$from" "$from")" \
        || { echo "applet_reorder_glide_to: could not resolve points" >&2; return 1; }
    local axis sx sy apx apy _rest
    read -r axis sx sy apx apy _rest <<<"$pts"
    "$E2E_FAKEPOINTER" move "$apx" "$apy"; sleep 0.25
    "$E2E_FAKEPOINTER" glide "$apx" "$apy" "$sx" "$sy"; sleep 0.35
    if [[ -n "$viax" && -n "$viay" ]]; then
        "$E2E_FAKEPOINTER" drag "$sx" "$sy" "$viax" "$viay" "$rx" "$ry"
    else
        "$E2E_FAKEPOINTER" drag "$sx" "$sy" "$rx" "$ry"
    fi
    sleep 1.2
    return 0
}

# applet_reorder_attempt <view> <mode> <from> <to>: the whole self-contained
# cycle - enter rearrange, drive one attempt, leave rearrange, settle - and
# classify the outcome by whether the applet-id order actually changed. A
# committed reorder returns 0; a NO-OP / REFUSED reorder returns 3 (a
# first-class outcome the abort/noop paths REQUIRE, not an error); a real driver
# failure returns 1.
applet_reorder_attempt() {
    local view="$1" mode="$2" from="$3" to="$4" before after
    before="$(applet_reorder_order "$view")" || return 1

    applet_reorder_enter "$view" || return 1
    applet_reorder_glide "$view" "$mode" "$from" "$to" || { applet_reorder_exit "$view" || true; return 1; }
    applet_reorder_exit "$view" || return 1

    after="$(applet_reorder_order "$view")" || return 1
    if [[ "$after" == "$before" ]]; then
        return 3
    fi
    return 0
}

# --- matrix verb hookup -----------------------------------------------------
# A scenario chunk sources matrix-lib.sh then this file. APPLET_REORDER_FROM /
# APPLET_REORDER_TO pick the visual indices to swap (default the leading pair).
# commit = a real cross-neighbour reorder; abort = release-at-origin (order
# restored). The probe echoes the applet-id order the harness asserts on.

matrix_verb_appletreorder_drive() {
    local view="$1" outcome="$2"
    local from="${APPLET_REORDER_FROM:-0}" to="${APPLET_REORDER_TO:-1}"
    case "$outcome" in
        commit) applet_reorder_attempt "$view" commit "$from" "$to"; [[ $? -eq 0 ]] || return 1;;
        abort)  applet_reorder_attempt "$view" origin "$from" "$to"; [[ $? -eq 3 || $? -eq 0 ]] || return 1;;
        *) echo "matrix_verb_appletreorder_drive: unknown outcome '$outcome'" >&2; return 2;;
    esac
    return 0
}

matrix_verb_appletreorder_probe() {
    applet_reorder_order "$1"
}
