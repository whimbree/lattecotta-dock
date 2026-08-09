# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The typed edit-mode settings AUDIT harness: the port of tests/e2e/audit/audit-lib.sh
(BP-3b).

This is the Tier B / Tier C / Tier D driver every per-control audit cluster
(CL-1 length, CL-2 appearance, CL-3 behavior, CL-4 effects, CL-5 tasks, CL-6
chrome) calls. It is a SUPERSET of the matrix harness, not a copy: it composes
over latte_harness.matrix (the editmode probe, the shared vehicle) and
latte_harness.recipe (the busctl transport, viewAppletsData readback, the window
dump, wait_settled), adding only what the semantic audit needs on top - the
config-value snapshot/diff and the settings-window drive helpers.

Migration shape (the BP-2c/BP-3a fresh-module precedent): this is a fresh module,
not a bridge. Every readback is validated with pydantic at the boundary; the
recipe/matrix transport and probes are reused where they overlap. The bash lib
this ports was deleted with its last consumer when the BP-3 audit-recipe batch
ported the six audit recipes to .py; this module is the only audit harness.

The observable contract is preserved exactly (the never-swallow rule):

- SNAPSHOTS read the in-process config map through the viewConfigData /
  appletConfigData readbacks (never the on-disk file, so the KConfig
  default-deletion trap cannot corrupt the diff). Each snapshot is sorted
  ``key<TAB>json-value`` lines, byte-identical to the bash formula
  (json.dumps(value, sort_keys=True)).
- The ASSERT family returns 0 PASS, 1 FAIL (loud on stderr), 2 REFUSED (bad
  input), with byte-identical FAIL/REFUSED wording. HC3: assert_only_keys FAILS
  on a stray/missing key and assert_applies FAILS on a no-change, so a harness
  that only passes the happy path cannot hide the suspected-broken controls.
- changed_keys is the SAFE DIRECTION (mirrors changedConfigKeys): a key present
  on exactly one side counts as changed - a loud false-FAIL, never a silent
  false-PASS (the KConfig default-deletion trap).
- The editmode enter/exit reuse the matrix editmode probe's flip-poll plus a
  geometry settle so a snapshot never races the animation; the settings-window
  drive keeps the fractional pointer math verbatim.
"""

from __future__ import annotations

import difflib
import json
import subprocess
import sys
import time
from collections.abc import Callable
from contextlib import suppress
from pathlib import Path
from typing import NoReturn

from pydantic import BaseModel, ConfigDict, JsonValue

from latte_harness import matrix, recipe

# The window-rect quad the settings-window heuristic returns (x, y, w, h).
WindowRect = tuple[int, int, int, int]


class AuditError(RuntimeError):
    """A helper cannot proceed (a required env var is unset, or the tasks
    plasmoid is not present exactly once). The diagnostic is carried on the
    exception; the pilot catches it exactly where the bash caught the ``sys.exit``
    (``2>/dev/null``).
    """


def _require_env(name: str) -> str:
    """This module's env accessor: recipe.require_env with the audit prefix/error.

    Only the settings-window drive reaches for E2E_FAKEPOINTER; the bash used it
    unguarded, so an unset var is surfaced here (AuditError) instead of a bare
    command-not-found three lines later.
    """
    return recipe.require_env(name, prefix="audit", error=AuditError)


# ---- the verdict boundary (assert 0 PASS / 1 FAIL / 2 REFUSED) --------------
#
# Mirrors matrix.py's _Stop / _refuse / _fail / _to_status with the "audit:"
# prefix: _refuse (code 2) prints "audit: REFUSED: <msg>", _fail (code 1) prints
# "audit: FAIL: <msg>", and _to_status is the single boundary where a verdict
# becomes a return code and its line prints exactly once (an empty line means the
# diagnostic was already printed at the raise site, so nothing is reprinted).


class _Stop(Exception):  # a control-flow verdict, not an error condition
    """An audit verdict: the exit code plus the exact stderr line to print.

    Private on purpose: only the _to_status wrappers catch it. External callers
    use the assert_* wrappers, which translate it to the 0/1/2 status contract;
    a helper that can raise it (changed_keys on a missing snapshot) is not part
    of the direct-call surface.
    """

    def __init__(self, code: int, line: str = "") -> None:
        super().__init__(line)
        self.code = code
        self.line = line


def _refuse(msg: str) -> NoReturn:
    """_audit_refuse: raise the code-2 verdict carrying the REFUSED line."""
    raise _Stop(2, f"audit: REFUSED: {msg}")


def _fail(msg: str) -> NoReturn:
    """_audit_fail: raise the code-1 verdict carrying the FAIL line."""
    raise _Stop(1, f"audit: FAIL: {msg}")


def _to_status(body: Callable[[], None]) -> int:
    """Run an assert body; translate its verdict to the bash return code.

    A clean return is 0; a ``_Stop`` prints its line (unless already reported) and
    yields its code. A message prints exactly once, exactly as the bash printed it
    at the _audit_refuse / _audit_fail site and returned the code untouched.
    """
    try:
        body()
    except _Stop as stop:
        if stop.line:
            print(stop.line, file=sys.stderr, flush=True)
        return stop.code
    return 0


# ---- snapshots (pydantic at the readback boundary) -------------------------
#
# viewConfigData carries the containment "config" map and the live C++ "view"
# map; appletConfigData carries a child applet's "config" map. The values are a
# dynamic key -> JSON-value map, so JsonValue types them losslessly at the
# boundary (int stays int, float stays float, bool/null/array/object preserved);
# a payload missing an object surfaces as a loud pydantic ValidationError naming
# the field, never a silent empty snapshot a later broken read would match.


class _ViewConfigData(BaseModel):
    """The viewConfigData reply: the containment config map and the C++ view map."""

    model_config = ConfigDict(extra="ignore", frozen=True)

    config: dict[str, JsonValue]
    view: dict[str, JsonValue]


class _AppletConfigData(BaseModel):
    """The appletConfigData reply: a child applet's config map (tasks/CL-5)."""

    model_config = ConfigDict(extra="ignore", frozen=True)

    config: dict[str, JsonValue]


def _snapshot_lines(obj: dict[str, JsonValue]) -> str:
    """Sorted ``key<TAB>json-value`` lines, one per key, each newline-terminated.

    Byte-identical to the bash ``for key in sorted(obj): print("%s\\t%s" % (key,
    json.dumps(obj[key], sort_keys=True)))`` piped through a redirect: sorted keys,
    each value serialized with sorted object keys, a trailing newline per line, and
    the empty string for an empty map (never a stray blank line).
    """
    return "".join(f"{key}\t{json.dumps(obj[key], sort_keys=True)}\n" for key in sorted(obj))


def _view_config_data(view: int) -> _ViewConfigData:
    """The validated viewConfigData reply for a view (both the config and view maps)."""
    return _ViewConfigData.model_validate_json(
        recipe.json_payload("viewConfigData", "u", str(view))
    )


def config_snapshot(view: int) -> str:
    """audit_config_snapshot: the containment config VALUES for a view."""
    return _snapshot_lines(_view_config_data(view).config)


def view_snapshot(view: int) -> str:
    """audit_view_snapshot: the live C++-property half of viewConfigData."""
    return _snapshot_lines(_view_config_data(view).view)


def applet_config_snapshot(view: int, applet: int) -> str:
    """audit_applet_config_snapshot: a child applet's config VALUES (tasks/CL-5)."""
    data = _AppletConfigData.model_validate_json(
        recipe.json_payload("appletConfigData", "uu", str(view), str(applet))
    )
    return _snapshot_lines(data.config)


def tasks_applet_id(view: int) -> int:
    """audit_tasks_applet_id: the id of the single latte tasks plasmoid under a view.

    Resolved from the typed viewAppletsData readback (recipe.view_applets). Refuses
    loudly if the plasmoid is not present exactly once - a degenerate count is a
    symptom to surface, never papered into a wrong applet id (the pilot catches this
    the way the bash caught the ``sys.exit`` with ``2>/dev/null``).
    """
    matched = [a for a in recipe.view_applets(view) if a.plugin == "org.kde.latte.plasmoid"]
    if len(matched) != 1:
        raise AuditError(
            f"audit_tasks_applet_id: expected exactly one tasks plasmoid under view {view}, "
            f"saw {len(matched)}"
        )
    return matched[0].id


# ---- diff / assertions (the pure core plus the file boundary) --------------


def _parse_snapshot_text(text: str) -> dict[str, str]:
    """Parse ``key<TAB>value`` snapshot lines into a map (the bash ``load``).

    Splits on newline only (matching the bash ``line.rstrip("\\n")``), skips empty
    lines, and partitions each on the first TAB. A value never carries a literal
    newline or tab (json.dumps escapes both), so this round-trips a snapshot exactly.
    """
    parsed: dict[str, str] = {}
    for line in text.split("\n"):
        if not line:
            continue
        key, _, value = line.partition("\t")
        parsed[key] = value
    return parsed


def _diff_changed_keys(before: dict[str, str], after: dict[str, str]) -> list[str]:
    """The sorted set of keys whose value differs between two parsed snapshots.

    SAFE DIRECTION (mirrors tests/units/configsnapshotdiff.h changedConfigKeys): a
    key present on exactly one side counts as changed (``before.get(k) !=
    after.get(k)`` over the key union), a loud false-FAIL, never a silent
    false-PASS. Pure over the parsed maps so the diff classification is unit-testable
    without files.
    """
    keys = sorted(before.keys() | after.keys())
    return [key for key in keys if before.get(key) != after.get(key)]


def changed_keys(before: Path, after: Path) -> list[str]:
    """audit_changed_keys: the sorted changed-key set between two snapshot files.

    A missing snapshot file is a code-2 refusal with the bash message verbatim (not
    the REFUSED-prefixed wording - the bash printed this one directly), so a caller
    inside an assert body propagates it as return 2.
    """
    if not before.is_file() or not after.is_file():
        raise _Stop(2, f"audit: changed_keys: a snapshot file is missing ({before} / {after})")
    return _diff_changed_keys(
        _parse_snapshot_text(before.read_text()), _parse_snapshot_text(after.read_text())
    )


def _assert_applies(before: Path, after: Path, key: str) -> None:
    if not key:
        _refuse("audit_assert_applies needs a key")
    if key in changed_keys(before, after):
        return
    _fail(f"P1 applies: key '{key}' did not change (a control that writes nothing readable)")


def assert_applies(before: Path, after: Path, key: str) -> int:
    """audit_assert_applies: P1 - driving the control changed the key it is labelled
    for. A no-change is a FAIL (the D10 dead-control class).
      return 0 PASS   1 FAIL (no change)   2 REFUSED (no key / missing snapshot)
    """
    return _to_status(lambda: _assert_applies(before, after, key))


def _assert_only_keys(before: Path, after: Path, keys: tuple[str, ...]) -> None:
    changed = changed_keys(before, after)
    # sorted() is code-point order on BOTH sides, a deliberate cleanup of the
    # bash, which compared code-point-sorted `changed` against locale-collated
    # `sort -u` output for `expected` (unobservable for the camelCase config
    # keys, but the D269 lesson is that locale collation is not a verdict input).
    expected = sorted({key for key in keys if key})
    if changed == expected:
        return
    # The bash printed the FAIL line plus a ``diff`` of expected-vs-changed; the
    # verdict line is byte-identical and the diff detail is diagnostic (difflib is
    # the established idiom here, matrix._print_surface_diff).
    print(
        "audit: FAIL: P2 right-key-only: changed key set != expected", file=sys.stderr, flush=True
    )
    diff = difflib.unified_diff(
        expected, changed, fromfile="expected", tofile="changed", lineterm=""
    )
    for line in diff:
        print(f"audit:   (-expected +changed) {line}", file=sys.stderr, flush=True)
    raise _Stop(1)  # already reported


def assert_only_keys(before: Path, after: Path, *keys: str) -> int:
    """audit_assert_only_keys: P2 - the EXACT set of keys that changed is the
    expected set. A stray side-effect key (the D15 coupling) or a missing expected
    key is a FAIL. The decisive right-key check.
      return 0 PASS   1 FAIL (wrong key set)   2 REFUSED (missing snapshot)
    """
    return _to_status(lambda: _assert_only_keys(before, after, keys))


def _snapshot_value(snap: Path, key: str) -> str | None:
    """The json-value stored for ``key`` in a snapshot file, or None if absent."""
    return _parse_snapshot_text(snap.read_text()).get(key)


def _assert_reflects(snap: Path, key: str, want: str) -> None:
    if not snap.is_file():
        _refuse(f"audit_assert_reflects: snapshot missing: {snap}")
    have = _snapshot_value(snap, key)
    if have is None:
        _fail(f"P3 reflects: key '{key}' absent from snapshot")
    if have == want:
        return
    _fail(f"P3 reflects: key '{key}' is '{have}', expected '{want}'")


def assert_reflects(snap: Path, key: str, want: str) -> int:
    """audit_assert_reflects: P3 - the snapshot carries the key at the expected
    value. ``want`` is a JSON literal (90, true, "x"), the form the snapshot stores.
      return 0 PASS   1 FAIL (absent or wrong value)   2 REFUSED (missing snapshot)
    """
    return _to_status(lambda: _assert_reflects(snap, key, want))


def _assert_agrees(snap_a: Path, key_a: str, snap_b: Path, key_b: str) -> None:
    if not snap_a.is_file() or not snap_b.is_file():
        _refuse("audit_assert_agrees: a snapshot is missing")
    value_a = _snapshot_value(snap_a, key_a)
    if value_a is None:
        _fail(f"P4 agrees: key '{key_a}' absent from first snapshot")
    value_b = _snapshot_value(snap_b, key_b)
    if value_b is None:
        _fail(f"P4 agrees: key '{key_b}' absent from second snapshot")
    if value_a == value_b:
        return
    _fail(
        f"P4 agrees: '{key_a}'='{value_a}' but '{key_b}'='{value_b}' "
        "- two views of one value disagree"
    )


def assert_agrees(snap_a: Path, key_a: str, snap_b: Path, key_b: str) -> int:
    """audit_assert_agrees: P4 - two surfaces report the same value for one logical
    setting. The keys may differ (each surface names the value differently); the
    VALUES must match.
      return 0 PASS   1 FAIL (absent or disagree)   2 REFUSED (missing snapshot)
    """
    return _to_status(lambda: _assert_agrees(snap_a, key_a, snap_b, key_b))


# ---- settings-window drive (edit mode + the mapped config window) ----------


def _drive_editmode(view: int, *, on: bool) -> bool:
    """Fire setViewEditMode through the matrix action primitive; True on success.

    Reuses matrix.drive_action (status-aware busctl, forwards busctl's stderr) so a
    D-Bus failure is not swallowed; the bash ``e2e_call ... || return 1`` becomes a
    caught MatrixDriveError yielding False.
    """
    try:
        matrix.drive_action("setViewEditMode", "ub", str(view), "true" if on else "false")
    except matrix.MatrixDriveError:
        return False
    return True


def _poll_editmode(view: int, want: str) -> None:
    """Poll the matrix editmode probe until it reads ``want`` (30 x 0.2s).

    A probe that cannot read yet (view still coming up) counts as not-the-target,
    exactly as the bash empty command-substitution ``!= want`` did.
    """
    for _ in range(30):
        with suppress(matrix.MatrixProbeError):
            if matrix.verb_editmode_probe(view) == want:
                return
        time.sleep(0.2)


def _editmode_is(view: int, want: str) -> bool:
    """Whether the view's editMode reads ``want`` right now (a gone view is not it)."""
    try:
        return matrix.verb_editmode_probe(view) == want
    except matrix.MatrixProbeError:
        return False


def enter_editmode(view: int) -> bool:
    """audit_enter_editmode: turn edit mode ON (canvas rulers plus the settings
    config window) and settle, reusing the matrix editmode probe's flip-poll and a
    geometry settle so a later snapshot never races the animation. False (loud) if
    the drive fails or edit mode never turns on.
    """
    if not _drive_editmode(view, on=True):
        return False
    _poll_editmode(view, "true")
    _ = recipe.wait_settled(15)
    if _editmode_is(view, "true"):
        return True
    print(f"audit: FAIL: edit mode never turned on for view {view}", file=sys.stderr, flush=True)
    return False


def exit_editmode(view: int) -> bool:
    """audit_exit_editmode: turn edit mode OFF and settle. False if the drive fails."""
    if not _drive_editmode(view, on=False):
        return False
    _poll_editmode(view, "false")
    _ = recipe.wait_settled(15)
    return True


def settings_window_rect() -> WindowRect | None:
    """audit_settings_window_rect: the mapped settings config window as (x, y, w, h).

    Located from the compositor dump (recipe.windows) by its shape - a tall, wide
    latte-dock window, not a dock strip - the same heuristic settings-window-onscreen
    uses. None (loud) if no config window is mapped, so a caller cannot silently
    drive nothing.
    """
    for window in recipe.windows():
        if (
            "latte-dock" in window.resource_class
            and window.height > 400
            and 300 < window.width < 2000
        ):
            return (window.x, window.y, window.width, window.height)
    print("audit: no settings config window mapped", file=sys.stderr, flush=True)
    return None


def _fractional_point(rect: WindowRect, xfrac: float, yfrac: float) -> tuple[int, int]:
    """The pixel point at a fractional position inside a rect (0,0 top-left,
    1,1 bottom-right). ``int()`` truncates toward zero, matching the bash
    ``int($x + $xf * $w)``. Pure so the pointer math is unit-testable.
    """
    x, y, width, height = rect
    return int(x + xfrac * width), int(y + yfrac * height)


def _fakepointer(*args: str) -> None:
    """Run the fakepointer binary (E2E_FAKEPOINTER) for effect, fire-and-forget as
    the bash did (the settle poll that follows is the observable, not this call).
    """
    subprocess.run([_require_env("E2E_FAKEPOINTER"), *args], check=False)


def settings_click(xfrac: float, yfrac: float) -> bool:
    """audit_settings_click: click at a fractional position inside the settings
    config window. False if no config window is mapped.
    """
    rect = settings_window_rect()
    if rect is None:
        return False
    px, py = _fractional_point(rect, xfrac, yfrac)
    _fakepointer("click", str(px), str(py))
    _ = recipe.wait_settled(10)
    return True


def settings_drag(xfrac: float, yfrac: float, dx: int, dy: int) -> bool:
    """audit_settings_drag: press at a fractional position inside the config window,
    move by (dx, dy) pixels, release - the slider-drag primitive. False if no config
    window is mapped.
    """
    rect = settings_window_rect()
    if rect is None:
        return False
    px, py = _fractional_point(rect, xfrac, yfrac)
    _fakepointer("drag", str(px), str(py), str(px + dx), str(py + dy))
    _ = recipe.wait_settled(10)
    return True
