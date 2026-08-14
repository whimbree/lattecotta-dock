# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The typed recipe-side API: the port of tests/e2e/lib.sh (BP-2c).

This is the harness brain the e2e recipes talk to. It mirrors every helper in
tests/e2e/lib.sh one-for-one, keeping the SAME busctl invocations (identical
service/object/interface/method argv), the SAME bounded wait loops and their
messages, the SAME dock lifecycle over the E2E_* environment the runner
exports, and the SAME geometry assertions and failure wording - so a recipe
ported from bash behaves identically.

Where the bash returned raw JSON text for a recipe to pipe through python, this
module VALIDATES the readback at the boundary with pydantic (the migration's
core promise: every D-Bus readback is typed where it enters the harness). The
models carry the fields the ported recipes actually assert on - widened by W3
(widen the readback models) to the surface the recipes read, so the parallel
per-recipe viewsData/appletsData/tasksData twins fold onto these; a dock-side
field addition is tolerated (extra keys are ignored), never a break. busctl
stays the transport, exactly as the bash used it - no python D-Bus binding.

lib.sh itself stays in place: the ~47 bash recipes keep sourcing it until the
BP-3 batches port and delete them, so this module and lib.sh coexist. It exists
so the .py recipes have a typed API to import; the pilot tests/e2e/000-smoke.py
is the first consumer and the template for the batches.

Failure discipline (matching lib.sh and the failures-and-root-cause rule):

- ``fail(msg)`` is e2e_fail: print ``FAIL: <msg>`` to stderr and exit 1.
- The wait/assert predicates return a bool and print their loud diagnostic to
  stderr on the failure path, exactly like the bash helpers' return status and
  ``>&2`` message, so a recipe composes ``if not r.wait_running(): r.fail(...)``.
- The resolve/coverage helpers that cannot proceed raise ``RecipeError`` with
  the bash message verbatim; ``run()`` turns an escaped RecipeError into that
  message on stderr and a nonzero exit (no traceback), matching the bash
  ``sys.exit(msg)`` those helpers used.
- A read the dock refuses or fails raises ``DbusUnavailableError`` - the ONE
  refusal channel (``read_json`` and the typed readbacks route through it), a
  RecipeError subclass so pollers treat it as "no answer yet" and strict sites
  fail loudly through ``run()``.
- A malformed readback surfaces as a pydantic ``ValidationError`` naming the
  offending field - loud, at the boundary, never a silently wrong value.
"""

from __future__ import annotations

import io
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from collections.abc import Callable, Sequence
from contextlib import redirect_stderr, suppress
from dataclasses import dataclass
from pathlib import Path
from typing import Any, NoReturn

from pydantic import BaseModel, ConfigDict, Field, TypeAdapter

from latte_harness import proc

# The addressing triple every lattedock read/action uses (the bash e2e_call).
_LATTE_OBJECT = ("org.kde.lattedock", "/Latte", "org.kde.LatteDock")

# busctl renders a string return as `s "..."`; lifecycleState compares field 2
# INCLUDING the quotes, exactly as the bash `awk '{print $2}'` kept them.
_LIFECYCLE_RUNNING = '"running"'

# The empty-views reply as busctl prints it (the wait_settled "still starting"
# sentinel), and the escaped inStartup flag as it appears in the raw reply
# (busctl escapes the JSON quote to \", so the needle carries that backslash -
# the same literal the bash `grep 'inStartup\\":true'` matched).
_EMPTY_VIEWS_REPLY = 's "[]"'
_IN_STARTUP_TRUE = 'inStartup\\":true'

# The tasks applet plugin id, grepped in the raw viewAppletsData reply to find
# the tasks-carrying view (kept as a raw-text scan, matching e2e_tasks_view).
_TASKS_PLUGIN = '"org.kde.latte.plasmoid"'


class RecipeError(RuntimeError):
    """A helper cannot proceed; carries the bash message verbatim.

    ``run()`` prints the message to stderr and exits 1, reproducing the bash
    ``sys.exit(msg)`` those resolve/coverage helpers used (loud, nonzero, no
    traceback).
    """


class DbusUnavailableError(RecipeError):
    """A lattedock read surface gave no answer that can be parsed.

    The ONE refusal channel (harness audit A4: the same dock-side event
    previously surfaced four different ways). Two arms, one meaning -
    "nothing can be read right now":

    - the busctl call itself failed (dock not up, mid-restart, bus name
      unowned);
    - the reply carries no JSON: dbusreports refuses a WHOLE reply while its
      data is not trustworthy - e.g. viewsData while any view lacks an
      accepted placement (app/dbusreports.cpp, its qCritical boundary), a
      transient state during startup, edit-mode enters and view duplication -
      and that refusal arrives as an empty payload.

    Subclasses RecipeError deliberately: a poller that treats "no answer yet"
    as a non-match catches it exactly where it caught RecipeError and keeps
    polling; a strict site lets it escape to ``run()``, which reports it
    loudly. Content that PARSED but has the wrong shape is never this error -
    it reaches the typed validators (pydantic) or the caller's own shape
    checks, the loud layer for garbage.
    """


# ---- readback models (pydantic validates every reply at the boundary) -------
#
# Rect is the [x,y,w,h] quad every geometry field is; typing it as a 4-tuple
# asserts that shape at the boundary (a short or long array fails loudly here,
# not three subsystems away where the bash `x,y,w,h = rect` unpack would).
Rect = tuple[int, int, int, int]


class _Readback(BaseModel):
    """Base for every D-Bus readback model.

    ``extra="ignore"`` is the explicit tolerance: a field the dock adds later is
    dropped, never a validation break, so recipes keep working across dock-side
    additions. ``frozen`` makes each reply an immutable value; ``populate_by_name``
    lets tests build a model by field name as well as by JSON alias.
    """

    model_config = ConfigDict(extra="ignore", populate_by_name=True, frozen=True)


class View(_Readback):
    """One entry of viewsData - the always-emitted per-view surface, typed.

    Every field here is written for every view by dbusreports' serializeViewRecord
    (app/dbusreports.h), so a reply that OMITS one is a real breakage, not a
    tolerated absence - the required fields make that loud at the boundary, exactly
    as the geometry quads already do. The enum-like fields (edge, alignment,
    visibility_mode, screen, and MatrixView's view_type) are the dock's own string
    names (edgeName / alignmentName / visibilityModeName / the connector name), kept
    as ``str`` because recipes compare them against those exact strings and the dock
    owns the set - a Python enum here would only add a second spelling to keep in
    sync (the same call the existing ``edge: str`` field already made).

    W3 (widen the readback models) grew this from the seven fields the first ported
    lib.sh helpers needed to the surface the recipes actually read. MatrixView now
    extends it, and the retired _MoView / _GoldenView / _ReorderFlags twins folded
    onto it, instead of re-declaring the same viewsData fields five times over.
    """

    containment_id: int = Field(alias="containmentId")
    is_cloned: bool = Field(alias="isCloned")
    #! the clone's original containment id; -1 for an original (dbusreports maps
    #! isCloned ? groupId() : -1), so an == -1 test reads "this is not a clone".
    is_cloned_from: int = Field(alias="isClonedFrom")
    edge: str
    alignment: str
    screen: str  # the connector NAME (e.g. "Virtual-0"), not a numeric id
    visibility_mode: str = Field(alias="visibilityMode")
    is_hidden: bool = Field(alias="isHidden")
    in_startup: bool = Field(alias="inStartup")
    edit_mode: bool = Field(alias="editMode")
    in_configure_applets_mode: bool = Field(alias="inConfigureAppletsMode")
    #! the keyboard-focus session trio the 112/113/114 focus recipes assert on.
    keyboard_navigation: bool = Field(alias="keyboardNavigation")
    containment_accepts_input: bool = Field(alias="containmentAcceptsInput")
    owns_panel_focus_session: bool = Field(alias="ownsPanelFocusSession")
    absolute_geometry: Rect = Field(alias="absoluteGeometry")
    local_geometry: Rect = Field(alias="localGeometry")
    screen_geometry: Rect = Field(alias="screenGeometry")
    view_type: str = Field(alias="type")
    #! the logical input band and the region actually handed to QWindow::setMask
    #! (each an array of rects, empty when the mask is cleared; a single rect
    #! today, dbusreports.h documents the array-for-future-multi-rect shape).
    #! They differ only while a length-shrink union-hold awaits its settle
    #! collapse; the 070 maximize-length mask recipe asserts their sustained
    #! agreement at every settled detent (D274's tripwire).
    input_region_rects: list[Rect] = Field(alias="inputRegionRects")
    applied_input_region_rects: list[Rect] = Field(alias="appliedInputRegionRects")


class Applet(_Readback):
    """One entry of viewAppletsData - the presentation/pointer-math fields plus
    the stacking and colorizer readbacks.

    ``z`` is the AppletItem delegate's stacking order (lifted to ~900 over the edit
    chrome during an applet-reorder drag, the G2 readback); colorizer_active /
    colorizer_reason are the per-applet colorizer decision (D21/D28). All are
    written for every applet by serializeAppletRecord, so all are required - a
    reply missing one is malformed, never a silent default.
    """

    id: int
    plugin: str
    geometry: Rect
    in_scheduled_destruction: bool = Field(alias="inScheduledDestruction")
    z: float
    colorizer_active: bool = Field(alias="colorizerActive")
    colorizer_reason: str = Field(alias="colorizerReason")


class Task(_Readback):
    """One entry of viewTasksData - the stable per-row identity and kind.

    ``app_id`` is the per-window identity a reorder permutes; ``launcher_url``
    persists to the tasks-applet ``launchers`` config key (empty for a window task,
    kept in place so the row order still matches the bar); is_launcher / is_grouped
    / child_count / is_active are the row's kind, grouping, and activation. All are
    written for every row by serializeTaskRecord, so all are required.
    """

    app_id: str = Field(alias="appId")
    launcher_url: str = Field(alias="launcherUrl")
    is_launcher: bool = Field(alias="isLauncher")
    is_grouped: bool = Field(alias="isGrouped")
    child_count: int = Field(alias="childCount")
    is_active: bool = Field(alias="isActive")


class DockView(_Readback):
    """One dockSystemData view - the presentation-coverage oracle's inputs."""

    persistent_dock_id: int = Field(alias="persistentDockId")
    is_hidden: bool = Field(alias="isHidden")
    orientation: str
    effects_rect: Rect = Field(alias="effectsRect")
    canvas_geometry: Rect = Field(alias="canvasGeometry")


class DockSystemData(_Readback):
    """The dockSystemData snapshot - only the view array the oracle joins."""

    views: list[DockView]


class LayoutRecord(_Readback):
    """One entry of layoutsData's layouts[] - a LOADED (central) layout.

    collectLayoutsData (app/dbusreports.cpp) serializes only the synchronizer's
    central layouts, so in single mode the settled reply carries exactly one
    record - the layout switching recipes assert that shape, never the whole
    storage list. All four fields are written for every record by
    serializeLayoutRecord (app/dbusreports.h), so all are required.
    """

    name: str
    is_active: bool = Field(alias="isActive")
    activities: list[str]
    views_count: int = Field(alias="viewsCount")


class LayoutsData(_Readback):
    """The layoutsData reply: the memory mode plus the loaded-layout records.

    ``memory_usage`` is the dock's own mode name from memoryUsageName
    (app/dbusreports.h): "single" or "multiple" ("current" is a query sentinel
    the manager never reports). Kept as ``str`` for the same reason View's
    enum-like fields are: the dock owns the name set.
    """

    memory_usage: str = Field(alias="memoryUsage")
    layouts: list[LayoutRecord]


_VIEWS = TypeAdapter(list[View])
_APPLETS = TypeAdapter(list[Applet])
_TASKS = TypeAdapter(list[Task])


# ---- busctl transport (identical argv to the bash) -------------------------


def _run_busctl(args: Sequence[str], *, forward_stderr: bool) -> subprocess.CompletedProcess[str]:
    """`busctl --user call <args>`; forward busctl's stderr like the bash terminal.

    The polling probes pass ``forward_stderr=False`` for the bash ``2>/dev/null``
    that hides the not-up-yet errors; everything else forwards it.
    """
    result = subprocess.run(
        ["busctl", "--user", "call", *args], capture_output=True, text=True, check=False
    )
    if forward_stderr and result.stderr:
        sys.stderr.write(result.stderr)
    return result


def call(*args: str) -> str:
    """e2e_call: a lattedock method's raw busctl stdout (trailing newline kept).

    The low-level escape hatch; recipes want ``json_payload`` or the typed
    readbacks. stderr is forwarded, matching the bash e2e_call | ... terminal.
    """
    return _run_busctl([*_LATTE_OBJECT, *args], forward_stderr=True).stdout


def _call_quiet(*args: str) -> str:
    """e2e_call with busctl stderr suppressed (the polling probes' `2>/dev/null`)."""
    return _run_busctl([*_LATTE_OBJECT, *args], forward_stderr=False).stdout


def call_status(*args: str, quiet: bool = False) -> tuple[int, str]:
    """A lattedock method's ``(exit status, raw busctl stdout)`` - the status ``call`` drops.

    ``call`` forwards busctl's stderr and returns only stdout, swallowing the exit
    code. A caller that must tell a real D-Bus FAILURE (dock down, unknown method,
    bad args) from a method that legitimately returned an empty reply needs that
    code, so this is the one missing primitive the module and recipe copies each
    reimplemented (their comments all said exactly "recipe.call swallows the
    status"). The status contract is the shell's: ``0`` is success, nonzero is a
    transport/method failure, and returning the pair lets the caller branch -
    validate the reply, or raise its own domain error - instead of guessing from
    an empty string (the never-swallow rule). ``quiet`` suppresses busctl's own
    stderr for the ``2>/dev/null`` sites (a best-effort cleanup, or a not-up-yet
    poll); the default forwards it exactly as ``call`` does, so a real error still
    reaches the terminal. Reuses the single ``_run_busctl``/``_LATTE_OBJECT``
    transport - there is no second busctl path.
    """
    result = _run_busctl([*_LATTE_OBJECT, *args], forward_stderr=not quiet)
    return result.returncode, result.stdout


def call_or_fail(fail_message: str, *args: str) -> None:
    """Fire a lattedock action (a void mutating method), failing LOUDLY on a D-Bus error.

    The fail-loud twin of a coarse action call and the shared form of the ~13
    per-recipe ``_latte_call``/``_call`` helpers: run the method, and if the busctl
    call returns nonzero print ``FAIL: <fail_message>`` and exit 1 (``fail``).
    busctl's stderr - its error diagnostic - is forwarded first by ``call_status``;
    busctl is silent on stderr for a successful call, so nothing prints on the
    success path and this matches the copies' forward-on-failure exactly. Returns
    nothing: this is for void actions whose only interesting outcome is
    success-or-fail; a caller that needs the reply text, or wants to branch on the
    code itself, uses ``call_status``.
    """
    code, _ = call_status(*args)
    if code != 0:
        fail(fail_message)


def _unescape_busctl_json(busctl_stdout: str) -> str:
    """The e2e_json sed: strip the `s "` wrapper and unescape `\\"` to `"`.

    busctl prints a returned JSON string as one line `s "<escaped json>"`; the
    bash `sed 's/^s "//; s/"$//; s/\\"/"/g'` unwraps it, and command substitution
    strips the trailing newline. This reproduces all of that, byte for byte.
    """
    line = busctl_stdout.rstrip("\n")
    if line.startswith('s "'):
        line = line[len('s "') :]
    if line.endswith('"'):
        line = line[:-1]
    return line.replace('\\"', '"')


def json_payload(method: str, *args: str) -> str:
    """e2e_json: a read surface's payload as plain JSON text (byte-identical).

    The raw-text escape hatch: a failed or refused call collapses to "".
    Recipes want ``read_json`` (the one loud refusal channel) or the typed
    readbacks; this stays for the sites that need the payload TEXT itself -
    artifact writers preserving the delivered bytes, raw diagnostic dumps, and
    oracle stdin feeds whose empty input is the established non-answer the
    oracle already refuses.
    """
    return _unescape_busctl_json(call(method, *args))


def try_json_payload(method: str, *args: str) -> str | None:
    """The delivered payload text, or None when no answer arrived.

    The optional-style twin of ``read_json`` for callers that must preserve
    the DELIVERED BYTES (the presentation watcher writes the failing payload
    as an artifact, so a re-serialization would not be evidence). None covers
    both no-answer arms read_json raises DbusUnavailableError for - a failed
    busctl call and the refused, empty reply - so a refusal can never reach a
    JSON parse or a validator as "". Delivered text is unescaped exactly as
    json_payload does, malformed content and all; the parse stays the loud
    layer for garbage.
    """
    result = _run_busctl([*_LATTE_OBJECT, method, *args], forward_stderr=True)
    if result.returncode != 0:
        return None
    payload = _unescape_busctl_json(result.stdout)
    return payload or None


def read_json(method: str, *args: str) -> Any:
    """Read a lattedock JSON surface into parsed Python data.

    The recipe-facing read: raises DbusUnavailableError (the one refusal
    channel - its docstring carries the dbusreports mechanism, documented
    once) when busctl fails or the reply carries no parseable JSON; returns
    the ``json.loads`` result otherwise. Returns ``Any`` on purpose: this is
    the raw boundary for fields the typed readbacks do not model, and callers
    index the result exactly as they would a json.loads result. Prefer the
    typed readbacks where the fields exist.
    """
    result = _run_busctl([*_LATTE_OBJECT, method, *args], forward_stderr=True)
    if result.returncode != 0:
        raise DbusUnavailableError(f"{method}: busctl call failed (dock unreachable?)")
    payload = _unescape_busctl_json(result.stdout)
    try:
        return json.loads(payload)
    except json.JSONDecodeError:
        # A refusal prints nothing, so this arm is normally the empty reply; a
        # NON-empty unparseable reply is named in the message rather than
        # silently classified (never swallow what actually arrived).
        detail = f" (unparseable reply: {payload!r})" if payload else ""
        raise DbusUnavailableError(f"{method} refused or returned no JSON{detail}") from None


# ---- typed readbacks (the pydantic boundary) -------------------------------


def views() -> list[View]:
    """viewsData, validated into typed View records.

    Every typed readback routes through ``read_json``: a refused or failed
    read raises DbusUnavailableError - never a misleading ValidationError
    about "" - while a delivered-but-misshapen reply still fails pydantic
    validation loudly, naming the offending field.
    """
    return _VIEWS.validate_python(read_json("viewsData"))


def view_applets(containment_id: int) -> list[Applet]:
    """viewAppletsData for a view, validated into typed Applet records."""
    return _APPLETS.validate_python(read_json("viewAppletsData", "u", str(containment_id)))


def view_tasks(containment_id: int) -> list[Task]:
    """viewTasksData for a view, validated into typed Task records."""
    return _TASKS.validate_python(read_json("viewTasksData", "u", str(containment_id)))


def dock_system_data() -> DockSystemData:
    """dockSystemData, validated into the typed snapshot."""
    return DockSystemData.model_validate(read_json("dockSystemData"))


def layouts_data() -> LayoutsData:
    """layoutsData, validated into the typed memory-mode-plus-layouts snapshot."""
    return LayoutsData.model_validate(read_json("layoutsData"))


def _find_view(containment_id: int) -> View | None:
    return next((v for v in views() if v.containment_id == containment_id), None)


def view(containment_id: int) -> View:
    """e2e_view_field's lookup as typed access: the View, or a loud refusal.

    e2e_view_field evaluated a python expression over the view dict; the typed
    replacement is this record, whose fields a recipe reads directly. Absence is
    the bash ``sys.exit('no view with containmentId <id>')``.
    """
    found = _find_view(containment_id)
    if found is None:
        raise RecipeError(f"no view with containmentId {containment_id}")
    return found


# ---- loud failure and the recipe entry wrapper -----------------------------


def fail(message: str) -> NoReturn:
    """e2e_fail: `FAIL: <message>` to stderr, exit 1."""
    print(f"FAIL: {message}", file=sys.stderr, flush=True)
    raise SystemExit(1)


def run(body: Callable[[], None]) -> NoReturn:
    """Run a recipe body; translate an escaped RecipeError to a loud exit.

    A RecipeError becomes its message on stderr and exit 1 (the bash
    ``sys.exit(msg)`` shape); ``fail()``'s SystemExit passes through unchanged; a
    clean return exits 0. This is the template every .py recipe uses.
    """
    try:
        body()
    except RecipeError as err:
        print(str(err), file=sys.stderr, flush=True)
        raise SystemExit(1) from err
    raise SystemExit(0)


def worsen_status_on_cleanup_failure(status: int, cleanup_failed: bool) -> int:
    """The common teardown-status rule: a failed cleanup worsens a SUCCESS, never masks a failure.

    A recipe whose success status is 0 folds its cleanup outcome into the exit code
    with this: a body that succeeded (status 0) but left residue fails (1); a body
    that already failed (nonzero) keeps its own code - a cleanup success must never
    hide it, and a second failure must not relabel it. This is the never-swallow
    rule applied to teardown: a stranded fixture fails a passing recipe, and a real
    body failure keeps its own code. The pure storm/topology cleanup cores apply
    the identical rule internally (they also own an acceptance/ordering decision),
    so they are handed to ``run_with_cleanup`` directly rather than through this
    helper; a recipe whose success status is NONZERO (an xfail signature) also
    cannot use this and states its own policy in its cleanup callable.
    """
    if cleanup_failed and status == 0:
        return 1
    return status


def run_with_cleanup(
    body: Callable[[], int | None],
    cleanup: Callable[[int], int],
    *,
    install_signal_exits: bool = True,
) -> NoReturn:
    """Run a recipe ``body`` under the shared teardown lifecycle, run ``cleanup``, and exit.

    This owns the main()/cleanup boilerplate every teardown-carrying recipe copied
    by hand (harness audit B2: the exact install-signals / try-body / translate /
    finally-cleanup / worsen-status shape appeared in ~10 recipes). The contract:

    - the body's clean return is status 0, or the int it returns - an xfail recipe
      returns its signature code (e.g. 57);
    - ``fail()``'s SystemExit and the signal exits (SIGINT 130 / SIGTERM 143)
      become that exit code, so the distinguished codes survive an interrupt;
    - a ``RecipeError`` that escapes the body prints its message to stderr and
      becomes status 1 (the bash ``sys.exit(msg)`` shape, no traceback);
    - an UNEXPECTED exception is NOT swallowed: cleanup still runs (the finally),
      then the exception propagates and the process dies loudly nonzero, exactly
      as the hand-rolled mains let an unguarded decode escape after cleanup;
    - cleanup runs on EVERY path (the bash ``trap cleanup EXIT``) and OWNS the
      final status. It receives the body's resolved status and returns the exit
      code, so it wraps - never replaces - the recipe's own teardown ordering
      (stop-dock-before-restore) and status policy. Most recipes fold their
      outcome in via ``worsen_status_on_cleanup_failure``; the pure
      storm/topology cores return their own computed status. ``run_with_cleanup``
      does not second-guess that return.

    ``install_signal_exits`` defaults on; a recipe that runs strandable setup
    BEFORE the body (its temp fixtures need the same signal-driven teardown)
    installs the handlers itself first and passes ``False`` here.
    """
    if install_signal_exits:
        proc.install_conventional_signal_exits()
    status = 0
    try:
        try:
            result = body()
            if isinstance(result, int):
                status = result
        except SystemExit as exc:
            status = exc.code if isinstance(exc.code, int) else 1
        except RecipeError as exc:
            print(str(exc), file=sys.stderr, flush=True)
            status = 1
    finally:
        status = cleanup(status)
    raise SystemExit(status)


# ---- environment contract (the E2E_* the runner exports) -------------------


def require_env(name: str, *, prefix: str = "e2e", error: type[Exception] = RecipeError) -> str:
    """The bash ``${VAR:?}``: return the value, or refuse loudly naming the var.

    The one shared env accessor the recipe modules each re-declared (harness audit
    B3, ~8 copies each raising its own exception). ``prefix`` names the refusing
    module in the message and ``error`` is the exception it raises, so a caller
    keeps its exact wording and its own error type - a multi_output caller still
    gets a MultiOutputError its pollers catch. The default is this module's
    ``e2e:``/RecipeError. An empty value counts as unset, exactly as the bash
    ``:?`` treated the empty string.
    """
    value = os.environ.get(name)
    if not value:
        raise error(f"{prefix}: required environment variable {name} is unset")
    return value


def _require_nested(helper: str) -> None:
    """_e2e_require_nested: a nested-only helper refuses loudly outside nested mode.

    The bash returned 2; here a wrong-mode call is a programming error (a
    nested-only helper reached in live mode), so it raises RecipeError with the
    same message rather than silently touching the live session.
    """
    mode = os.environ.get("E2E_MODE")
    if mode != "nested":
        raise RecipeError(
            f"e2e: {helper} is nested-only (it manages the vehicle dock / nested kwin); "
            f"refusing in mode '{mode or 'unset'}'"
        )


def screen_dims() -> tuple[int, int]:
    """E2E_SCREEN_W/H as ints (the bash ``${E2E_SCREEN_W:?} ${E2E_SCREEN_H:?}`` staging math).

    The shared form of the per-module ``_screen_dims`` copies (harness audit B3);
    an unset dimension refuses loudly through ``require_env``.
    """
    return int(require_env("E2E_SCREEN_W")), int(require_env("E2E_SCREEN_H"))


# ---- shared recipe utilities (the micro-copy tier, harness audit B3) --------


def muted_stderr() -> redirect_stderr[io.StringIO]:
    """A `2>/dev/null` for stderr: swallow diagnostics from a best-effort call.

    The shared form of the ~8 per-recipe ``_muted_stderr`` context managers
    (harness audit B3): ``with recipe.muted_stderr():`` wraps a cleanup dock-stop
    (or any noisy best-effort step) so its "already gone" chatter stays off the
    recipe output. Returns the redirect context manager directly (stdout is
    untouched - only the wrapped call's stderr is redirected).
    """
    return redirect_stderr(io.StringIO())


def fakepointer(*args: object) -> int:
    """Fire one E2E_FAKEPOINTER pointer/keyboard injection, returning its exit status.

    The shared form of the ~13 per-recipe pointer wrappers (harness audit B3):
    resolve the fixture-injected tool, run it with the args stringified, and return
    the process status (``check=False`` - the ``... || fail`` sites branch on it).
    Callers that only fire ignore the return; a bool test is ``== 0``; a caller
    that asserts an exact status reads it directly - the three shapes the copies
    split into. E2E_FAKEPOINTER unset refuses loudly (require_env) rather than a
    bare command-not-found (the never-swallow rule).
    """
    return subprocess.run(
        [require_env("E2E_FAKEPOINTER"), *(str(a) for a in args)], check=False
    ).returncode


def kwriteconfig(*args: str) -> int:
    """Run one ``kwriteconfig6`` write, returning its exit status.

    The shared core of the ~14 per-recipe kwriteconfig wrappers (harness audit B3).
    It writes exactly the args given (the caller supplies --file/--group/--key and
    the value), so it fits every wrapper shape: fire-and-forget (ignore the
    return), a bool ``== 0``, or the fail-loud twin below.
    """
    return subprocess.run(["kwriteconfig6", *args], check=False).returncode


def kwriteconfig_or_fail(fail_message: str, *args: str) -> None:
    """Write a kwriteconfig6 key, failing LOUDLY (``fail``) on a nonzero status.

    The fail-loud twin of ``kwriteconfig`` (the ``_kwrite(fail_message, *args)``
    copies): the coarse config write whose only interesting outcome is
    success-or-fail.
    """
    if kwriteconfig(*args) != 0:
        fail(fail_message)


def dock_log_lines() -> list[str]:
    """The vehicle dock's captured log (E2E_DOCK_LOG) as lines - the bash grep source.

    The shared form of the DnD recipes' ``_dock_log_lines`` copies (harness audit
    B3); an unset E2E_DOCK_LOG refuses loudly through ``require_env``.
    """
    return Path(require_env("E2E_DOCK_LOG")).read_text(errors="replace").splitlines()


def new_dock_log_has(mark: int, needle: str) -> bool:
    """True iff a dock-log line added since ``mark`` carries ``needle``.

    ``mark`` is a line count captured before the action (``len(dock_log_lines())``);
    this is the bash ``tail -n +$((mark+1)) | grep -q``.
    """
    return any(needle in line for line in dock_log_lines()[mark:])


def dump_new_dock_log(mark: int) -> None:
    """Print every dock-log line added since ``mark`` to stderr (the failure-path dump)."""
    print("---- new dock-log lines ----", file=sys.stderr, flush=True)
    for line in dock_log_lines()[mark:]:
        print(line, file=sys.stderr, flush=True)


# ---- bounded wait loops (pure cores + the live probes) ---------------------


def _wait_running_loop(
    probe: Callable[[], str], timeout: int, sleep: Callable[[float], None]
) -> tuple[bool, str]:
    """e2e_wait_running: poll lifecycleState ``timeout`` times at 1s, never blind.

    Pure over an injected probe and sleep so the bound (timeout iterations) and
    the timeout message are unit-testable without a live dock.
    """
    state = ""
    for _ in range(timeout):
        state = probe()
        if state == _LIFECYCLE_RUNNING:
            return True, ""
        sleep(1)
    return False, (
        f"dock never reached lifecycleState running in {timeout}s (last: {state or 'no reply'})"
    )


def _wait_settled_loop(
    probe_raw: Callable[[], str], timeout: int, sleep: Callable[[float], None]
) -> tuple[bool, str]:
    """e2e_wait_settled: views must EXIST, be out of inStartup, and STOP MOVING.

    The existence check is load-bearing (lifecycleState flips to running before
    any view is created), and the equal-to-previous check is the animation gate
    (startup-zoom coordinates sampled mid-animation misaim pointer math). Pure
    over an injected raw-reply probe and sleep.
    """
    previous = ""
    for _ in range(timeout):
        payload = probe_raw()
        if payload and payload != _EMPTY_VIEWS_REPLY and _IN_STARTUP_TRUE not in payload:
            if payload == previous:
                return True, ""
            previous = payload
        sleep(1)
    return False, f"views still absent, inStartup, or animating after {timeout}s"


def _probe_lifecycle_state() -> str:
    """Field 2 of the lifecycleState reply, or '' when the dock is unreachable."""
    fields = _call_quiet("lifecycleState").split()
    return fields[1] if len(fields) >= 2 else ""


def _probe_views_reply() -> str:
    """The raw viewsData reply, trailing newline stripped (command-sub semantics)."""
    return _call_quiet("viewsData").rstrip("\n")


def wait_running(timeout: int = 60) -> bool:
    """Poll lifecycleState until running; True on success, False (loud) on timeout."""
    ok, message = _wait_running_loop(_probe_lifecycle_state, timeout, time.sleep)
    if not ok:
        print(message, file=sys.stderr, flush=True)
    return ok


def is_running() -> bool:
    """One-shot lifecycleState probe: True iff the dock answers "running".

    wait_running polls up to a timeout; this is the single non-blocking check
    the live tools do to refuse fast when no dock is up (the presentation
    watcher's `busctl ... lifecycleState | grep -q '"running"'` guard, which
    exits at once rather than waiting).
    """
    return _probe_lifecycle_state() == _LIFECYCLE_RUNNING


def wait_settled(timeout: int = 60) -> bool:
    """Poll viewsData until views exist, leave inStartup, and stop animating."""
    ok, message = _wait_settled_loop(_probe_views_reply, timeout, time.sleep)
    if not ok:
        print(message, file=sys.stderr, flush=True)
    return ok


# ---- dock lifecycle (nested-only) ------------------------------------------


def dock_pid() -> int | None:
    """e2e_dock_pid: the recorded vehicle-dock pid, or None when unrecorded."""
    pidfile = require_env("E2E_DOCK_PIDFILE")
    try:
        text = Path(pidfile).read_text().strip()
    except OSError:
        return None
    return int(text) if text.isdigit() else None


def pid_alive(pid: int) -> bool:
    """The bash ``kill -0``: alive iff a signal could be delivered."""
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def _reap_if_child(pid: int) -> bool:
    """Reap ``pid`` when it is this process's exited child; True if reaped.

    D275 (a recipe-started dock stays a zombie its parent never reaps):
    bash auto-reaps background children, so ``kill -0`` there meant "still
    running"; Python does not, so a SIGTERM'd child dock answers kill(0)
    as a zombie forever and a liveness poll never sees it exit. The
    waitpid(WNOHANG) probe reaps an exited child and reports it; a pid
    that is not this process's child (the runner-started dock) raises
    ChildProcessError and the kill(0) probe stays authoritative.
    """
    try:
        reaped, _status = os.waitpid(pid, os.WNOHANG)
    except ChildProcessError:
        return False
    return reaped == pid


def dock_start(timeout: int = 60) -> bool:
    """e2e_dock_start: launch the staged dock into the vehicle, detached, and wait.

    run-staged.sh execs the binary, so the launcher pid IS the dock pid. The
    child is its own session (setsid), so it survives this call - the recipe
    process exiting reparents it to init, exactly as the bash ``setsid ... &``
    left it running. QT_FORCE_STDERR_LOGGING keeps the dock's qCDebug/qWarning
    in E2E_DOCK_LOG (NixOS Qt otherwise routes to journald off a tty).
    """
    _require_nested("e2e_dock_start")
    repo = require_env("E2E_REPO")
    log = require_env("E2E_DOCK_LOG")
    pidfile = require_env("E2E_DOCK_PIDFILE")
    env = dict(os.environ)
    env["LATTE_CONFIG_HOME"] = require_env("E2E_CONFIG_HOME")
    env["BUILD"] = require_env("E2E_BUILD")
    env["LATTE_DEBUG_DBUS"] = "1"
    env["QT_FORCE_STDERR_LOGGING"] = "1"
    with open(log, "a") as handle:
        proc = subprocess.Popen(
            [str(Path(repo) / "scripts" / "run-staged.sh"), "-d"],
            env=env,
            stdout=handle,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
    Path(pidfile).write_text(f"{proc.pid}\n")
    return wait_running(timeout) and wait_settled(timeout)


def dock_stop(timeout: int = 25) -> bool:
    """e2e_dock_stop: SIGTERM the dock and wait for a CLEAN exit (no SIGKILL).

    A dock that survives SIGTERM is a shutdown defect the caller must see, so
    there is deliberately no escalation - the bash contract carried over.
    """
    _require_nested("e2e_dock_stop")
    pid = dock_pid()
    if pid is None:
        print("e2e_dock_stop: no dock pid recorded", file=sys.stderr, flush=True)
        return False
    if not pid_alive(pid):
        print(f"e2e_dock_stop: dock (pid {pid}) already gone", file=sys.stderr, flush=True)
        return False
    with suppress(ProcessLookupError):
        os.kill(pid, 15)
    for _ in range(timeout * 5):
        if _reap_if_child(pid) or not pid_alive(pid):
            return True
        time.sleep(0.2)
    print(f"dock (pid {pid}) survived SIGTERM for {timeout}s", file=sys.stderr, flush=True)
    return False


# ---- kwin scripting and window dumps ---------------------------------------


class KwinScriptError(RecipeError):
    """The transient-KWin-script transport failed; no script output exists.

    Raised when loadScript or the script run call is refused (harness audit
    A2: this failure previously collapsed into "", the same value as "script
    ran and printed nothing", and empty-check consumers passed vacuously -
    a missing-fixture precondition or a gone-after-teardown wait would
    "succeed" with the compositor unreachable). "" from kwin_js stays the
    legitimate ran-and-printed-nothing result; this error means the script
    never executed, so nothing about compositor state can be concluded.
    Subclasses RecipeError so run() reports it loudly at strict sites and
    an existing poller's broad catch keeps polling (the old ""-as-non-match
    shape) rather than gaining a new crash channel.
    """


_DUMPWINS_JS = (
    "for (const w of workspace.windowList()) {\n"
    '        print("@TAG@|DUMPWIN|" + w.resourceClass + "|" + w.caption + "|" '
    '+ w.frameGeometry.x + "," + w.frameGeometry.y + " " + w.frameGeometry.width '
    '+ "x" + w.frameGeometry.height + "|" + (w.output ? w.output.name : "?") '
    '+ "|layer=" + w.layer);\n'
    "    }"
)


def _busctl_bg(args: Sequence[str]) -> None:
    """Run a best-effort busctl call for effect (the stop/unload cleanups).

    Deliberately quiet and unchecked: these run after collection, so a failure
    here cannot fake or lose script output; the checked calls (loadScript, run)
    have their own raising helpers above.
    """
    subprocess.run(
        ["busctl", "--user", "call", *args],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )


def _kwin_load_script(js: str, tag: str) -> str:
    """loadScript, returning the script number (busctl reply field 2).

    Raises KwinScriptError when the call fails or the reply carries no script
    number, so an unloaded script can never read as an empty capture.
    """
    result = subprocess.run(
        [
            "busctl",
            "--user",
            "call",
            "org.kde.KWin",
            "/Scripting",
            "org.kde.kwin.Scripting",
            "loadScript",
            "ss",
            js,
            tag,
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.stderr:
        sys.stderr.write(result.stderr)
    fields = result.stdout.split()
    if result.returncode != 0 or len(fields) < 2:
        raise KwinScriptError("e2e_kwin_js: loadScript failed")
    return fields[1]


def _kwin_run_script(number: str) -> None:
    """The script's run call; raises KwinScriptError on refusal (a script that
    never ran must not read as ran-and-printed-nothing). stderr is forwarded,
    as the old best-effort call inherited it."""
    result = subprocess.run(
        [
            "busctl",
            "--user",
            "call",
            "org.kde.KWin",
            f"/Scripting/Script{number}",
            "org.kde.kwin.Script",
            "run",
        ],
        stdout=subprocess.DEVNULL,
        check=False,
    )
    if result.returncode != 0:
        raise KwinScriptError("e2e_kwin_js: script run call failed")


def _tag_lines(text: str, tag: str) -> str:
    """grep the tagged lines and strip through the LAST `<tag>|` (the sed greedy)."""
    needle = f"{tag}|"
    out: list[str] = []
    for line in text.splitlines():
        if needle in line:
            out.append(line[line.rfind(needle) + len(needle) :])
    return "\n".join(out)


def _journal_tag_lines(since_epoch: float, tag: str) -> str:
    """The live-session log read: journalctl since ``mark``, tagged lines only.

    The live counterpart of the nested E2E_KWIN_LOG grep. Ported by inspection;
    the maintained, tested path is the nested one (the vehicle), so this is never
    driven from a harness worktree (the same discipline as the runner's live leg).
    """
    result = subprocess.run(
        [
            "journalctl",
            "--user",
            "-u",
            "plasma-kwin_wayland",
            "--since",
            f"@{since_epoch}",
            "--no-pager",
            "-o",
            "cat",
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    return _tag_lines(result.stdout, tag)


def kwin_js(body: str, collection_delay: float = 0.5) -> str:
    """e2e_kwin_js: run a transient KWin script and return its tagged print output.

    @TAG@ in ``body`` is replaced with a unique run tag so a previous/concurrent
    run cannot bleed into the result; the 0.5s default delay lets the script
    flush before it is stopped. Nested reads the vehicle kwin's captured log;
    live reads the session journal (the mode branch the bash carried).

    A loadScript/run transport failure raises KwinScriptError instead of
    returning "" (harness audit A2): "" is reserved for the legitimate
    ran-and-printed-nothing result, so an empty-check consumer can trust it
    means "the script ran and matched no window", never "the compositor
    scripting surface was unreachable".
    """
    mode = os.environ.get("E2E_MODE", "")
    tag = f"E2EJS-{os.getpid()}-{time.time_ns()}"
    with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
        handle.write(body.replace("@TAG@", tag) + "\n")
        js = handle.name
    mark = time.time()
    try:
        num = _kwin_load_script(js, tag)
        try:
            _kwin_run_script(num)
            time.sleep(collection_delay)
        finally:
            # Stop/unload stay best-effort AND run even when the run call
            # raised: a loaded-but-refused script must not leak into kwin.
            _busctl_bg(["org.kde.KWin", f"/Scripting/Script{num}", "org.kde.kwin.Script", "stop"])
            _busctl_bg(
                ["org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "unloadScript", "s", tag]
            )
    finally:
        with suppress(OSError):
            os.unlink(js)
    if mode == "nested":
        return _tag_lines(Path(require_env("E2E_KWIN_LOG")).read_text(errors="replace"), tag)
    return _journal_tag_lines(mark, tag)


@dataclass(frozen=True, slots=True)
class Window:
    """One parsed e2e_dumpwins record."""

    resource_class: str
    caption: str
    geometry_field: str
    x: int
    y: int
    width: int
    height: int
    output: str
    layer: int


def parse_dumpwins(text: str) -> list[Window]:
    """Parse DUMPWIN lines into typed records (the awk field layout, exactly).

    Split on '|' at the fixed positions the e2e_view_window_x awk used ($2 class,
    $4 geometry, $6 layer), so a typed consumer and the ported view_window_x pick
    the same fields. A caption is assumed free of '|' (the dock's is), the same
    assumption the awk made; a line that does not parse to that shape is skipped.
    """
    windows: list[Window] = []
    for line in text.splitlines():
        parts = line.split("|")
        if len(parts) < 6 or parts[0] != "DUMPWIN":
            continue
        position, _, size = parts[3].partition(" ")
        x_text, _, y_text = position.partition(",")
        w_text, _, h_text = size.partition("x")
        layer_text = parts[5].removeprefix("layer=")
        if not (x_text and y_text and w_text and h_text and layer_text):
            continue
        try:
            windows.append(
                Window(
                    resource_class=parts[1],
                    caption=parts[2],
                    geometry_field=parts[3],
                    x=int(x_text),
                    y=int(y_text),
                    width=int(w_text),
                    height=int(h_text),
                    output=parts[4],
                    layer=int(layer_text),
                )
            )
        except ValueError:
            continue
    return windows


def dumpwins() -> str:
    """e2e_dumpwins: all windows as DUMPWIN|class|caption|x,y WxH|output|layer=N."""
    return kwin_js(_DUMPWINS_JS)


def windows() -> list[Window]:
    """The parsed window dump (typed twin of e2e_dumpwins for recipe use)."""
    return parse_dumpwins(dumpwins())


# ---- geometry helpers ------------------------------------------------------


def _select_view_window_x(
    dump: Sequence[Window], edge: str, screen_w: int, screen_h: int
) -> int | None:
    """The e2e_view_window_x awk predicate: the latte-dock layer-3 window's x.

    The screen-width layer-3 latte-dock window whose anchored edge touches the
    screen (bottom edge at screen_h, or top edge at 0); its x, or None. Pure so
    the selection logic is unit-testable without a compositor.
    """
    for w in dump:
        if "latte-dock" not in w.resource_class or w.layer != 3:
            continue
        if w.width != screen_w:
            continue
        if edge == "bottom" and w.y + w.height == screen_h:
            return w.x
        if edge == "top" and w.y == 0:
            return w.x
    return None


def view_window_x(containment_id: int) -> int | None:
    """e2e_view_window_x: the TRUE screen x of a horizontal view's window.

    Reads what the compositor actually shows (the window dump) rather than
    trusting viewsData's reported origin, so a future state/render divergence
    cannot silently misaim a recipe. None for a view whose window it cannot
    locate (non-horizontal, non-screen-width, or still settling).
    """
    found = _find_view(containment_id)
    if found is None:
        return None
    return _select_view_window_x(
        windows(), found.edge, found.screen_geometry[2], found.screen_geometry[3]
    )


def geometry_drift(containment_id: int) -> int | None:
    """e2e_geometry_drift: rendered_x - (absoluteGeometry.x - localGeometry.x).

    Zero means the compositor draws the dock exactly where viewsData claims;
    nonzero is the state/render divergence the D-Bus assertions are blind to.
    None for a view whose window cannot be located.
    """
    winx = view_window_x(containment_id)
    if winx is None:
        return None
    found = _find_view(containment_id)
    if found is None:
        return None
    reported_x = found.absolute_geometry[0] - found.local_geometry[0]
    return winx - reported_x


def assert_geometry_agrees(tolerance: int = 2) -> bool:
    """e2e_assert_geometry_agrees: every locatable view renders within tolerance.

    Names each divergent view on stderr; a run where NOTHING was measurable also
    fails, so a broken dump can never look like agreement. Returns True on
    agreement, matching the bash exit status the recipes compose with `|| fail`.
    """
    bad = False
    measured = 0
    for record in views():
        drift = geometry_drift(record.containment_id)
        if drift is None:
            continue
        measured += 1
        if drift < -tolerance or drift > tolerance:
            print(
                f"e2e_assert_geometry_agrees: view {record.containment_id} renders {drift}px "
                f"off its reported origin (tolerance {tolerance}px)",
                file=sys.stderr,
                flush=True,
            )
            bad = True
    if measured == 0:
        print(
            "e2e_assert_geometry_agrees: no view geometry was measurable - "
            "refusing to report agreement",
            file=sys.stderr,
            flush=True,
        )
        return False
    return not bad


# ---- task pointer math -----------------------------------------------------


def tasks_view() -> int:
    """e2e_tasks_view: the widest horizontal view that carries a tasks applet.

    The canonical target for task-interaction recipes, independent of any
    config's ids: bottom/top, not hidden, widest first, bottom beating top on
    ties. Raises when no horizontal view carries the tasks applet.
    """
    candidates = [v for v in views() if v.edge in ("bottom", "top") and not v.is_hidden]
    candidates.sort(key=lambda v: (-v.absolute_geometry[2], v.edge != "bottom"))
    for candidate in candidates:
        if _TASKS_PLUGIN in json_payload("viewAppletsData", "u", str(candidate.containment_id)):
            return candidate.containment_id
    raise RecipeError("e2e_tasks_view: no horizontal view carries a tasks applet")


def task_center(containment_id: int, app_id: str) -> tuple[int, int]:
    """e2e_task_center: the SCREEN center of a task icon, computed arithmetically.

    The tasks applet geometry is view-local; viewsData's absolute/local pair
    gives the window origin (x from the compositor's true window position when
    locatable, so the parabolic zoom cannot distort it), and icons split the
    applet evenly at rest. Callers must approach the returned point from OUTSIDE
    the dock (published task geometries are unusable mid-zoom).
    """
    target = view(containment_id)
    ax, ay, _aw, _ah = target.absolute_geometry
    lx, ly = target.local_geometry[0], target.local_geometry[1]
    winx = view_window_x(containment_id)
    origin_x = winx if winx is not None else ax - lx
    origin_y = ay - ly

    applet = next(
        (a for a in view_applets(containment_id) if a.plugin == "org.kde.latte.plasmoid"), None
    )
    if applet is None:
        raise RecipeError(f"e2e_task_center: view {containment_id} carries no tasks applet")
    px, py, pw, ph = applet.geometry

    tasks = view_tasks(containment_id)
    index = next((i for i, t in enumerate(tasks) if t.app_id == app_id), None)
    if index is None:
        raise RecipeError(f"e2e_task_center: no task with appId {app_id} in view {containment_id}")
    count = len(tasks)

    if target.edge in ("bottom", "top"):
        center_x = origin_x + px + (index + 0.5) * pw / count
        center_y = origin_y + py + ph / 2
    else:
        center_x = (ax - lx) + px + pw / 2
        center_y = origin_y + py + (index + 0.5) * ph / count
    return int(center_x), int(center_y)


# ---- screenshots (nested-only) ---------------------------------------------


def _reply_uint(reply: str, key: str) -> int | None:
    """The `"<key>" u <n>` uint out of a busctl vardict reply (the bash grep -oE)."""
    match = re.search(rf'"{key}" u ([0-9]+)', reply)
    return int(match.group(1)) if match is not None else None


def screenshot(out: str, *options: str) -> None:
    """e2e_screenshot: capture the vehicle workspace via KWin ScreenShot2.

    The image arrives RAW over a passed fd (the reply vardict carries
    width/height/stride/format); native-resolution is always on, and extra
    ``key type value`` triples forward verbatim into CaptureWorkspace's option
    dict (the golden bridge passes ``include-cursor b false``). Refuses loudly on
    a non-(A)RGB32 or padded-stride layout rather than guessing the converter.
    """
    _require_nested("e2e_screenshot")
    opts = ["native-resolution", "b", "true", *options]
    if len(opts) % 3 != 0:
        given = " ".join(options)
        raise RecipeError(
            f"e2e_screenshot: option args must be (key type value) triples, got: {given}"
        )
    count = len(opts) // 3
    raw_fd, raw = tempfile.mkstemp(suffix=".raw")
    os.close(raw_fd)
    try:
        with open(raw, "wb") as sink:
            fdnum = sink.fileno()
            reply = subprocess.run(
                [
                    "busctl",
                    "--user",
                    "call",
                    "org.kde.KWin",
                    "/org/kde/KWin/ScreenShot2",
                    "org.kde.KWin.ScreenShot2",
                    "CaptureWorkspace",
                    "a{sv}h",
                    str(count),
                    *opts,
                    str(fdnum),
                ],
                stdout=subprocess.PIPE,
                text=True,
                pass_fds=(fdnum,),
                check=False,
            )
        if reply.returncode != 0:
            raise RecipeError("e2e_screenshot: CaptureWorkspace failed")
        width = _reply_uint(reply.stdout, "width")
        height = _reply_uint(reply.stdout, "height")
        stride = _reply_uint(reply.stdout, "stride")
        image_format = _reply_uint(reply.stdout, "format")
        # QImage formats 5/6 are (A)RGB32: BGRA byte order on little-endian.
        # Anything else, or a padded stride, needs new handling, not guessing.
        if (
            width is None
            or height is None
            or stride is None
            or image_format not in (5, 6)
            or stride != width * 4
        ):
            raise RecipeError(
                f"e2e_screenshot: unexpected raw layout "
                f"(format={image_format} stride={stride} width={width}) - extend the converter"
            )
        convert = subprocess.run(
            ["magick", "-size", f"{width}x{height}", "-depth", "8", f"bgra:{raw}", out], check=False
        )
        if convert.returncode != 0:
            raise RecipeError(f"e2e_screenshot: magick conversion failed ({convert.returncode})")
    finally:
        with suppress(OSError):
            os.unlink(raw)


# ---- presentation-coverage oracle ------------------------------------------


def _assert_presentation_coverage(
    snapshot: DockSystemData, applets: Sequence[Applet], containment_id: int, tolerance: int
) -> str:
    """Join dockSystemData's painted rectangle with the QML item rectangles.

    dockSystemData owns the painted background/effects rectangle; viewAppletsData
    owns the applet rectangles. This catches a coherent internal geometry whose
    rendered applets escape either the dock background or the output-owned canvas
    (the D150 shape - a hovered applet row escaping its resting background).
    Returns the coverage line on success; raises RecipeError on a violation, with
    the bash message verbatim.
    """
    matches = [v for v in snapshot.views if v.persistent_dock_id == containment_id]
    if len(matches) != 1:
        raise RecipeError(
            f"presentation coverage: expected one view {containment_id}, got {len(matches)}"
        )
    target = matches[0]
    if target.is_hidden:
        raise RecipeError(
            f"presentation coverage: view {containment_id} is hidden; no painted background exists"
        )

    live = [
        a
        for a in applets
        if not a.in_scheduled_destruction and a.geometry[2] > 0 and a.geometry[3] > 0
    ]
    if not live:
        raise RecipeError(
            f"presentation coverage: view {containment_id} reports no live applet geometry"
        )

    horizontal = target.orientation == "horizontal"
    origin_index = 0 if horizontal else 1
    length_index = 2 if horizontal else 3

    def interval(rect: Rect) -> tuple[int, int]:
        start = rect[origin_index]
        return start, start + rect[length_index]

    background_start, background_end = interval(target.effects_rect)
    content = [interval(a.geometry) for a in live]
    content_start = min(start for start, _ in content)
    content_end = max(end for _, end in content)
    canvas_end = target.canvas_geometry[length_index]

    failures: list[str] = []
    if content_start < background_start - tolerance:
        failures.append(f"content starts at {content_start}, before background {background_start}")
    if content_end > background_end + tolerance:
        failures.append(f"content ends at {content_end}, after background {background_end}")
    if content_start < -tolerance:
        failures.append(f"content starts at {content_start}, before canvas 0")
    if content_end > canvas_end + tolerance:
        failures.append(f"content ends at {content_end}, after canvas {canvas_end}")

    tail = (
        f"content=[{content_start},{content_end}] "
        f"background=[{background_start},{background_end}] canvas=[0,{canvas_end}]"
    )
    if failures:
        raise RecipeError(
            f"presentation coverage: view {containment_id} {'; '.join(failures)}; {tail}"
        )
    return f"presentation coverage: view {containment_id} {tail}"


def assert_applets_covered_by_background(containment_id: int, tolerance: int = 2) -> str:
    """e2e_assert_applets_covered_by_background: the full-dock composition oracle.

    Call it at rest and after every driven parabolic transition; on failure the
    caller should preserve a screenshot. Prints and returns the coverage line on
    success; raises RecipeError on a violation.
    """
    snapshot = dock_system_data()
    applets = view_applets(containment_id)
    line = _assert_presentation_coverage(snapshot, applets, containment_id, tolerance)
    print(line, flush=True)
    return line
