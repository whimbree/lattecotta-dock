# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
"""The applet-reorder driver: the port of
tests/e2e/matrix/applet-reorder-driver.sh (BP-3c).

The reusable surface the F3 committed-reorder and the A2 abort chunks drive
through - it does for containment applets in rearrange mode what
050-drag-reorder-launchers does for tasks inside the tasks applet, with the abort
variants A2 needs.

The ConfigOverlay drag machinery is only live when a view is BOTH in edit mode AND
the global inConfigureAppletsMode sub-mode is set. That sub-mode is transient
(never persisted, deleted on config load), so no config seed can reach it. The
setViewConfiguringApplets D-Bus action is the driving surface: it flips the same
global flag the settings-header rearrange button does, refusing loudly if the
target view is not already in edit mode. Parabolic zoom is OFF in this sub-mode, so
the per-applet geometry viewAppletsData reports is stable and drag aiming needs no
pixel calibration - the icon-shift trap 050 fights does not apply here.

Migration shape (the BP-2c/BP-3a fresh-module precedent): a fresh module, not a
bridge. applet-reorder-driver.sh was retired in the R12 batch when
create-linked-dock (its last bash consumer) was ported onto this module; the 100
and 022 recipe ports had already replaced the earlier consumers. The coordinate
math (_compute_points) is pure so it is
unit-testable without a compositor; the order/flag/z readbacks go through
recipe.py's typed viewsData/viewAppletsData boundary (a local model for the
rearrange flags and the stacking z, which recipe.View/recipe.Applet do not
surface), and viewAppletsOrder stays a status-aware busctl call (recipe does not
expose it). The appletreorder verb registers into the same matrix registry the
bash matrix_verb_appletreorder_* hooked.
"""

from __future__ import annotations

import os
import subprocess
import sys
import time
from collections.abc import Callable
from contextlib import suppress
from dataclasses import dataclass
from typing import NoReturn

from pydantic import BaseModel, ConfigDict, Field, TypeAdapter

from latte_harness import matrix, recipe
from latte_harness.matrix import MatrixDriveError, MatrixProbeError
from latte_harness.recipe import Rect

# The lattedock addressing triple (mirrors recipe._LATTE_OBJECT / matrix). The
# viewAppletsOrder reply and the edit/rearrange actions need the exit code
# recipe.call swallows, so their raw busctl calls live here (the same way matrix
# carries its own status-aware call).
_LATTE_OBJECT = ("org.kde.lattedock", "/Latte", "org.kde.LatteDock")

# The default visual pair the appletreorder matrix verb swaps (bash
# APPLET_REORDER_FROM / APPLET_REORDER_TO, defaulting to the leading pair).
_DEFAULT_FROM = "0"
_DEFAULT_TO = "1"


class AppletReorderError(Exception):
    """An applet-reorder driver step could not proceed (order readback failed,
    rearrange never armed, points out of range, an unknown glide mode). The
    diagnostic is printed at the raise site (matching the bash ``echo ... >&2;
    return 1``); ``applet_reorder_attempt`` translates it to its driver-error code
    and the appletreorder verb translates it to a matrix refusal, so a failed
    interaction is never silently a clean pass (the never-swallow rule).
    """


def _raise(message: str) -> NoReturn:
    """Print the diagnostic loudly then raise (the bash ``echo >&2; return 1``)."""
    print(message, file=sys.stderr, flush=True)
    raise AppletReorderError(message)


# ---- low-level transport ---------------------------------------------------


def _call_status(method: str, *args: str, quiet: bool = False) -> tuple[int, str]:
    """``busctl --user call`` for a lattedock method, returning (exit code, stdout).

    Keeps the exit code (unlike recipe.call, which swallows it) so a D-Bus failure
    is distinguishable from an empty reply - the never-swallow contract the order
    readback and the edit/rearrange drivers depend on. busctl's own stderr is
    forwarded unless ``quiet`` (the exit's best-effort ``2>&1`` suppression).
    """
    result = subprocess.run(
        ["busctl", "--user", "call", *_LATTE_OBJECT, method, *args],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.stderr and not quiet:
        _ = sys.stderr.write(result.stderr)
    return result.returncode, result.stdout


def _fakepointer(*args: str) -> None:
    """Fire one fakepointer invocation (the reorder choreography does not gate on
    its status, matching the bash, which runs move/glide/drag unconditionally)."""
    _ = subprocess.run([_require_env("E2E_FAKEPOINTER"), *args], check=False)


def _require_env(name: str) -> str:
    """The bash ``${VAR:?}``: return the value, or refuse loudly naming the var."""
    value = os.environ.get(name)
    if not value:
        raise AppletReorderError(f"applet_reorder: required environment variable {name} is unset")
    return value


def _flatten(*points: tuple[int, int]) -> list[str]:
    """Flatten (x, y) waypoints into the fakepointer string argument list."""
    out: list[str] = []
    for x, y in points:
        out += [str(x), str(y)]
    return out


def _drag(*points: tuple[int, int]) -> None:
    """fakepointer ``drag`` across the given waypoints."""
    _fakepointer("drag", *_flatten(*points))


def _dragkey(key: str, *points: tuple[int, int]) -> None:
    """fakepointer ``dragkey <key>`` across the given waypoints (a key tap held
    mid-drag)."""
    _fakepointer("dragkey", key, *_flatten(*points))


def _dragbutton(button: str, *points: tuple[int, int]) -> None:
    """fakepointer ``dragbutton <button>`` across the given waypoints: a LEFT press
    held through the glide, then a click of ``button`` (right/middle/left) at the last
    waypoint WHILE left is still held - the mid-drag button chord. The whole chord
    rides one fakepointer connection so the left grab spans the second button; a plain
    ``drag`` then ``rightclick`` cannot reproduce it (``drag`` releases left first)."""
    _fakepointer("dragbutton", button, *_flatten(*points))


# ---- readback models (a local twin for the fields recipe does not surface) --


class _AppletZ(BaseModel):
    """One viewAppletsData entry with the G2 stacking readback.

    recipe.Applet carries id/plugin/geometry but not z (the delegate's stacking
    order, lifted to ~900 over the edit chrome during a drag). extra="ignore"
    tolerates a dock-side field addition, like every readback model.
    """

    model_config = ConfigDict(extra="ignore", populate_by_name=True, frozen=True)

    id: int
    z: float


class _ReorderFlags(BaseModel):
    """One viewsData entry with the rearrange-lifecycle flags.

    recipe.View does not carry editMode / inConfigureAppletsMode; a local model
    reads exactly those two plus the identity, validated at the boundary.
    """

    model_config = ConfigDict(extra="ignore", populate_by_name=True, frozen=True)

    containment_id: int = Field(alias="containmentId")
    edit_mode: bool = Field(alias="editMode")
    in_configure_applets_mode: bool = Field(alias="inConfigureAppletsMode")


_APPLET_ZS = TypeAdapter(list[_AppletZ])
_REORDER_FLAGS = TypeAdapter(list[_ReorderFlags])


# ---- readbacks -------------------------------------------------------------


def _parse_applets_order(reply: str) -> str:
    """The bash ``awk '{for (i = 3; i <= NF; i++) printf ...}'``: the order items
    only, space-joined.

    busctl prints an ``as`` array as ``as <count> "a" "b" "c"``; dropping the first
    two fields (the ``as`` signature and the count) and joining the rest yields the
    quoted-item order the bash before/after comparison uses. Pure so the parse is
    unit-testable.
    """
    return " ".join(reply.split()[2:])


def applet_reorder_order(view: int) -> str:
    """applet_reorder_order: the reorderable applet-instance-id order (viewAppletsOrder),
    space-separated on one line.

    A D-Bus failure surfaces loudly (never a plausible-but-empty order that would
    read "unchanged" on both sides of an abort - the never-swallow rule).
    """
    code, stdout = _call_status("viewAppletsOrder", "u", str(view))
    if code != 0:
        _raise(f"applet_reorder_order: viewAppletsOrder call FAILED for view {view}")
    reply = stdout.rstrip("\n")
    if not reply.startswith("as "):
        _raise(f"applet_reorder_order: reply is not an 'as' array: {reply}")
    return _parse_applets_order(reply)


def applet_reorder_z(view: int, applet_id: int) -> float:
    """applet_reorder_z: the stacking z of one applet's delegate (the G2 readback).

    Used to prove an abort left no applet stranded over chrome. An applet absent
    from the view is a symptom to surface, never papered over with a plausible 0.
    """
    applets = _APPLET_ZS.validate_json(recipe.json_payload("viewAppletsData", "u", str(view)))
    found = next((a for a in applets if a.id == applet_id), None)
    if found is None:
        _raise(f"applet_reorder_z: applet {applet_id} not present in view {view}")
    return found.z


# ---- rearrange lifecycle ---------------------------------------------------


def _reorder_flags(view: int) -> _ReorderFlags:
    """The view's editMode / inConfigureAppletsMode flags, or a loud refusal."""
    flags = _REORDER_FLAGS.validate_json(recipe.json_payload("viewsData"))
    found = next((v for v in flags if v.containment_id == view), None)
    if found is None:
        _raise(f"applet_reorder: view {view} gone")
    return found


def applet_reorder_edit_mode(view: int) -> bool:
    """The view's editMode flag (the bash ``_applet_reorder_flag view editMode``)."""
    return _reorder_flags(view).edit_mode


def applet_reorder_configuring(view: int) -> bool:
    """The view's inConfigureAppletsMode flag (the rearrange sub-mode)."""
    return _reorder_flags(view).in_configure_applets_mode


def _drive_action(method: str, *args: str) -> None:
    """Fire a lattedock action, refusing to swallow a failure (the enter path's
    ``e2e_call ... || return 1``)."""
    code, _ = _call_status(method, *args)
    if code != 0:
        _raise(f"applet_reorder: D-Bus action {method} failed for the reorder driver")


def _poll_until(read: Callable[[], bool], want: bool, tries: int = 40, delay: float = 0.2) -> bool:
    """Poll a flag reader until it equals ``want`` (both flips come up async).

    A read error mid-startup (the view briefly gone) counts as not-yet-the-target,
    exactly like the bash empty ``$(...)`` result compared unequal, so it keeps
    polling rather than aborting; the caller decides the final verdict.
    """
    for _ in range(tries):
        with suppress(AppletReorderError):
            if read() == want:
                return True
        time.sleep(delay)
    with suppress(AppletReorderError):
        return read() == want
    return False


def applet_reorder_enter(view: int) -> None:
    """applet_reorder_enter: open the view's edit session, then the rearrange
    sub-mode, polling each flip in the readback, then settle (the dock grows to
    editThickness; geometry must stop moving before aiming). Refuses loudly if
    either flag never comes up."""
    _drive_action("setViewEditMode", "ub", str(view), "true")
    if not _poll_until(lambda: applet_reorder_edit_mode(view), True):
        _raise(f"applet_reorder_enter: view {view} never entered edit mode")

    _drive_action("setViewConfiguringApplets", "ub", str(view), "true")
    if not _poll_until(lambda: applet_reorder_configuring(view), True):
        _raise(f"applet_reorder_enter: view {view} never entered rearrange mode")

    _ = recipe.wait_settled(15)


def applet_reorder_exit(view: int) -> None:
    """applet_reorder_exit: leave rearrange, then edit, and settle back to the
    non-edit baseline geometry. Best-effort on each flag flip (main.qml also resets
    inConfigureAppletsMode on edit exit), but edit mode must actually clear."""
    _ = _call_status("setViewConfiguringApplets", "ub", str(view), "false", quiet=True)
    _ = _call_status("setViewEditMode", "ub", str(view), "false", quiet=True)
    _ = _poll_until(lambda: applet_reorder_edit_mode(view), False)
    _ = recipe.wait_settled(15)
    if applet_reorder_edit_mode(view):
        _raise(f"applet_reorder_exit: view {view} stuck in edit mode")


# ---- coordinate model ------------------------------------------------------


@dataclass(frozen=True, slots=True)
class ReorderPoints:
    """The screen-pixel choreography for one attempt (the bash _applet_reorder_points
    fields as named waypoints; ``o`` is the origin, equal to ``s``)."""

    axis: str
    s: tuple[int, int]
    ap: tuple[int, int]
    m: tuple[int, int]
    cross: tuple[int, int]
    ctr: tuple[int, int]
    over: tuple[int, int]
    o: tuple[int, int]
    ret: tuple[int, int]
    n: tuple[int, int]
    from_id: int
    to_id: int


def _compute_points(
    edge: str,
    abs_geom: Rect,
    local_geom: Rect,
    from_geom: Rect,
    from_id: int,
    to_geom: Rect,
    to_id: int,
) -> ReorderPoints:
    """Resolve one attempt's waypoints from applet geometry + the view window origin.

    Pure (no D-Bus) so the whole coordinate model is unit-testable. The window
    origin is absoluteGeometry - localGeometry (render agrees with reported since
    the Phase 8 drift fix, guarded by 060). Points, per the bash model header:
      s     applet[from] center (press + arm point)
      ap    just OUTSIDE applet[from] on the thickness axis (the off-item hover)
      m     midpoint between applet[from] and applet[to] centers
      cross PAST applet[to] center into its far half (a committed cross-neighbour)
      ctr   applet[to] EXACT center (occupied-drop)
      over  BEYOND applet[to] far edge (overflow / past-the-last-slot)
      o     origin = applet[from] center (release-at-origin)
      ret   PAST origin into applet[from] far half (the cross-then-return abort)
      n     a nudge INSIDE applet[from] own slot, never reaching a neighbour midpoint
    """
    ax, ay = abs_geom[0], abs_geom[1]
    lx, ly = local_geom[0], local_geom[1]
    ox0, oy0 = ax - lx, ay - ly
    horizontal = edge in ("top", "bottom")

    def center(geom: Rect) -> tuple[float, float, int, int]:
        px, py, pw, ph = geom
        return (ox0 + px + pw / 2.0, oy0 + py + ph / 2.0, pw, ph)

    fx, fy, fw, fh = center(from_geom)
    tx, ty, tw, th = center(to_geom)

    if horizontal:
        axis, size_from, span = "h", float(fw), tx - fx
    else:
        axis, size_from, span = "v", float(fh), ty - fy
    sgn = 1 if span >= 0 else -1
    nudge = sgn * min(abs(span) * 0.30, size_from * 0.25) if span else size_from * 0.10

    if horizontal:
        s = (fx, fy)
        ap = (fx, fy - fh)
        m = ((fx + tx) / 2.0, fy)
        cross = (tx + sgn * 0.35 * tw, ty)
        ctr = (tx, ty)
        over = (tx + sgn * 0.90 * tw, ty)
        ret = (fx - sgn * 0.30 * fw, fy)
        n = (fx + nudge, fy)
    else:
        s = (fx, fy)
        ap = (fx - fw, fy)
        m = (fx, (fy + ty) / 2.0)
        cross = (tx, ty + sgn * 0.35 * th)
        ctr = (tx, ty)
        over = (tx, ty + sgn * 0.90 * th)
        ret = (fx, fy - sgn * 0.30 * fh)
        n = (fx, fy + nudge)

    def rnd(point: tuple[float, float]) -> tuple[int, int]:
        # round() over a float returns int and applies round-half-to-even, the
        # SAME rounding the bash-invoked python3 int(round(...)) did, so the port
        # stays byte-identical.
        return (round(point[0]), round(point[1]))

    return ReorderPoints(
        axis=axis,
        s=rnd(s),
        ap=rnd(ap),
        m=rnd(m),
        cross=rnd(cross),
        ctr=rnd(ctr),
        over=rnd(over),
        o=rnd(s),
        ret=rnd(ret),
        n=rnd(n),
        from_id=from_id,
        to_id=to_id,
    )


def _applet_reorder_points(view: int, frm: int, to: int) -> ReorderPoints:
    """Fetch the view + applet geometry and resolve the attempt's waypoints."""
    try:
        target = recipe.view(view)
        applets = recipe.view_applets(view)
    except recipe.RecipeError as err:
        _raise(str(err))
    if not (0 <= frm < len(applets)) or not (0 <= to < len(applets)):
        _raise(f"applet_reorder: from/to out of range (have {len(applets)} applets)")
    return _compute_points(
        target.edge,
        target.absolute_geometry,
        target.local_geometry,
        applets[frm].geometry,
        applets[frm].id,
        applets[to].geometry,
        applets[to].id,
    )


# ---- pointer choreography --------------------------------------------------


def applet_reorder_glide(view: int, mode: str, frm: int, to: int) -> None:
    """applet_reorder_glide: one attempt. Rearrange mode is ASSUMED entered. Arms
    currentApplet with an off-item hover, presses, and finishes per ``mode`` (see
    the bash model header). Does not enter/exit/settle.

    ConfigOverlay.onPressed returns early unless a hover already set
    dragOverlay.currentApplet, so approach from off the item and glide onto its
    centre first (an unpressed motion stream arms it).
    """
    pts = _applet_reorder_points(view, frm, to)

    _fakepointer("move", str(pts.ap[0]), str(pts.ap[1]))
    time.sleep(0.25)
    _fakepointer("glide", str(pts.ap[0]), str(pts.ap[1]), str(pts.s[0]), str(pts.s[1]))
    time.sleep(0.35)

    match mode:
        case "commit":
            _drag(pts.s, pts.m, pts.cross)
        case "occupied":
            # release squarely on applet[to] centre (T1a): the midpoint /
            # degenerate-hovered branch, no deliberate far-half overshoot.
            _drag(pts.s, pts.m, pts.ctr)
        case "overflow":
            # release BEYOND applet[to] far edge (T1c past-the-last-slot): the
            # distance-fallback / index>=count arm.
            _drag(pts.s, pts.m, pts.over)
        case "origin":
            # CROSS applet[to], then return and release in the origin slot far half
            # (ret, not the exact origin centre): the placeHolder re-crosses back, so
            # a reorder that DID engage nets to no change (the release-at-origin abort).
            _drag(pts.s, pts.m, pts.cross, pts.m, pts.ret)
        case "noop":
            # nudge within applet[from] own slot; never crosses a neighbour.
            _drag(pts.s, pts.n, pts.o)
        case "jitter":
            # out->back->out reverse-jitter (DR-2), then release past applet[to].
            _drag(pts.s, pts.cross, pts.s, pts.cross)
        case "escape":
            # DR-6: tap Escape WHILE the button is held (dragkey), then release.
            # Escape's real effect on the ConfigOverlay reorder is observed by the
            # caller (the MouseArea has no Keys handler), never assumed to cancel.
            _dragkey("Escape", pts.s, pts.m, pts.cross)
        case "rightclick":
            # D285 (the drag-cancel stranding): RIGHT-CLICK while the left button is
            # held mid-drag (dragbutton). The right-click steals the pointer grab (the
            # containment context menu opens), so Qt fires the ConfigOverlay
            # MouseArea's onCanceled, not onReleased. The applet is LIFTED to the strand
            # z (900) on press regardless of motion, so the nudge stays INSIDE the origin
            # slot (pts.n, never crossing a neighbour): the placeHolder never moves, so
            # the onCanceled restore returns the applet to its origin and the order is
            # RESTORED - letting the leg assert both "no strand" AND "order unchanged".
            # Without the onCanceled fix the applet stays stranded at z 900.
            _dragbutton("right", pts.s, pts.n)
        case _:
            _raise(f"applet_reorder_glide: unknown mode '{mode}'")

    time.sleep(1.2)


def applet_reorder_glide_to(
    view: int, frm: int, rx: int, ry: int, viax: int | None = None, viay: int | None = None
) -> None:
    """applet_reorder_glide_to: the arbitrary-target primitive for adversarial drops
    whose release point is NOT an applet centre - a justify zone-boundary / splitter
    seam (T1b), a foreign window, off-screen coords. Arms applet[from], presses,
    glides through the optional via waypoint to (rx, ry), releases. Rearrange
    ASSUMED entered."""
    pts = _applet_reorder_points(view, frm, frm)
    _fakepointer("move", str(pts.ap[0]), str(pts.ap[1]))
    time.sleep(0.25)
    _fakepointer("glide", str(pts.ap[0]), str(pts.ap[1]), str(pts.s[0]), str(pts.s[1]))
    time.sleep(0.35)
    if viax is not None and viay is not None:
        _drag(pts.s, (viax, viay), (rx, ry))
    else:
        _drag(pts.s, (rx, ry))
    time.sleep(1.2)


# ---- the whole self-contained attempt --------------------------------------


def applet_reorder_attempt(view: int, mode: str, frm: int, to: int) -> int:
    """applet_reorder_attempt: the whole self-contained cycle - enter rearrange,
    drive one attempt, leave rearrange, settle - and classify the outcome by whether
    the applet-id order actually changed. Returns:
      0  the attempt COMMITTED a reorder (order changed)
      3  the attempt was a NO-OP / REFUSED reorder (order UNCHANGED) - a first-class
         outcome, NOT a failure: a noop/origin/escape abort MUST land here
      1  a real driver error (readback failed, rearrange stuck, points out of range)
    """
    try:
        before = applet_reorder_order(view)
        applet_reorder_enter(view)
    except AppletReorderError:
        return 1
    try:
        applet_reorder_glide(view, mode, frm, to)
    except AppletReorderError:
        with suppress(AppletReorderError):
            applet_reorder_exit(view)
        return 1
    try:
        applet_reorder_exit(view)
        after = applet_reorder_order(view)
    except AppletReorderError:
        return 1
    return 3 if after == before else 0


# ---- matrix verb hookup ----------------------------------------------------
# APPLET_REORDER_FROM / APPLET_REORDER_TO pick the visual indices to swap (default
# the leading pair). commit = a real cross-neighbour reorder; abort =
# release-at-origin (order restored). The probe echoes the applet-id order the
# harness asserts on.


def verb_appletreorder_drive(view: int, outcome: str) -> None:
    """The appletreorder verb driver. A commit that did not reorder, an abort that
    was a driver error, or an unknown outcome is surfaced as a MatrixDriveError,
    which the matrix backbone translates to a scenario refusal."""
    frm = int(os.environ.get("APPLET_REORDER_FROM") or _DEFAULT_FROM)
    to = int(os.environ.get("APPLET_REORDER_TO") or _DEFAULT_TO)
    if outcome == "commit":
        if applet_reorder_attempt(view, "commit", frm, to) != 0:
            raise MatrixDriveError("appletreorder commit did not change the order")
    elif outcome == "abort":
        if applet_reorder_attempt(view, "origin", frm, to) not in (0, 3):
            raise MatrixDriveError("appletreorder abort was a driver error")
    else:
        raise MatrixDriveError(f"appletreorder: unknown outcome '{outcome}'")


def verb_appletreorder_probe(view: int) -> str:
    """The appletreorder verb probe: the applet-id order. An order-readback failure
    is an UNASSERTABLE residue surface (MatrixProbeError), never silently a clean
    pass."""
    try:
        return applet_reorder_order(view)
    except AppletReorderError as err:
        raise MatrixProbeError from err


matrix.register_verb("appletreorder", verb_appletreorder_drive, verb_appletreorder_probe)
