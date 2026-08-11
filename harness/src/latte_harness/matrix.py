# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
"""The typed interaction MATRIX HARNESS: the port of tests/e2e/matrix/matrix-lib.sh
(BP-3a).

This is the reusable driver every scenario chunk calls: a scenario is composed
as (fixture x interaction x expected-readback), never a bespoke script. It runs
inside the nested vehicle (a .py recipe driven through run-matrix/run-e2e), so
recipe.py's plain busctl/screenshot helpers reach the vehicle dock.

It mirrors matrix-lib.sh one for one, preserving the observable contract:

- ``scenario_commit(cell, verb, expected)`` stages the cell, drives the verb to
  COMMIT, reads the verb's probe, asserts it equals expected.
    return 0 PASS   1 FAIL (readback != expected)   2 REFUSED
- ``scenario_abort(cell, verb)`` stages, snapshots the residue surfaces, drives
  to ABORT, and asserts every surface is byte-identical afterwards.
    return 0 PASS (no residue)   1 FAIL (residue)   2 REFUSED

The residue SURFACES (``surface_list``) span every place an aborted interaction
has been observed to strand state: the view record, the applet visual order, the
whole layout config, lattedockrc [UniversalSettings] and [ScreenConnectors], the
verb's own probe, and one named applet config subtree per
MATRIX_APPLET_CONFIG_GROUPS entry. A visual-only ghost residue is caught by a
baseline FRAME comparison (when MATRIX_CAPTURE_FRAME=1) delegated to the golden
bridge (latte_harness.matrix_golden).

A VERB is a driver/probe pair a scenario registers with ``register_verb`` (the
typed twin of defining matrix_verb_<name>_drive / _probe). The harness dispatches
by name and REFUSES an unknown verb. The built-in ``editmode`` verb uses only the
existing setViewEditMode action so the harness self-tests real interaction with
no dependency on the later drivers.

Migration shape (BP-2c precedent): this is a fresh module, not a bridge. The
fixture generator is called as a library (latte_harness.matrix_fixture), never a
per-call subprocess; every D-Bus readback is validated with pydantic at the
boundary; the recipe API's models and transport are reused where they overlap
(recipe.view_applets for the applet lookup, recipe.json_payload transport,
recipe.dock_start/stop lifecycle, recipe.wait_settled settling). matrix-lib.sh
itself stays in place for the bash matrix recipes until the BP-3 batches port and
delete them, so this module and the bash lib coexist.

Verdict semantics preserved exactly (the never-swallow rule): a refusal prints
``matrix: REFUSED: <msg>`` and yields code 2; a failure prints
``matrix: FAIL: <msg>`` and yields code 1; a probe that cannot snapshot is an
UNASSERTABLE surface, never silently a clean pass.
"""

from __future__ import annotations

import difflib
import io
import json
import os
import re
import shutil
import sys
import time
from collections.abc import Callable
from contextlib import redirect_stderr, suppress
from dataclasses import dataclass
from pathlib import Path
from typing import NoReturn

from pydantic import Field, TypeAdapter

from latte_harness import matrix_fixture, matrix_golden, recipe
from latte_harness.matrix_fixture import (
    ALIGNMENTS,
    DISPLAYS,
    EDGES,
    VIEW_TYPES,
    Alignment,
    Display,
    Edge,
    ViewType,
)
from latte_harness.recipe import Rect

# The default matrix_stage settle budget (bash MATRIX_STAGE_TIMEOUT default).
_DEFAULT_STAGE_TIMEOUT = 90

# Keys whose values legitimately breathe across a restart / an interaction
# without being residue; stripped from every KConfig snapshot. A scenario adds
# more through MATRIX_VOLATILE_EXTRA without redefining the probe.
_VOLATILE_BASE = [
    "DialogHeight",
    "DialogWidth",
    "PreloadWeight",
    "lastScreen",
    "configurationSticker",
    "timerShow",
    "timerHide",
]


# ---- verdict / probe control flow ------------------------------------------


class _Stop(Exception):  # a control-flow verdict, not an error condition
    """A matrix verdict: the exit code plus the exact stderr line to print.

    Mirrors the bash matrix_refuse (code 2) / matrix_fail (code 1) that print and
    return. ``_to_status`` prints ``line`` once at the boundary and yields
    ``code``; an empty ``line`` means the diagnostic was already printed at the
    raise site (the ``view="$(...)" || return 2`` shape), so nothing is reprinted.
    """

    def __init__(self, code: int, line: str = "") -> None:
        super().__init__(line)
        self.code = code
        self.line = line


class MatrixProbeError(Exception):
    """A residue probe could not produce a snapshot (view gone, D-Bus error,
    missing config file). The surface is UNASSERTABLE - never silently a clean
    pass (the never-swallow rule). The diagnostic is printed at the raise site.
    """


class MatrixDriveError(Exception):
    """A verb driver's D-Bus action failed. Raised by ``drive_action`` so a failed
    interaction is never silently ignored; the scenario translates it to a refusal.
    """


class MatrixError(Exception):
    """A resolution helper cannot proceed (e.g. an applet config group that is not
    present exactly once). The diagnostic is printed at the raise site.
    """


def _refuse(msg: str) -> NoReturn:
    """matrix_refuse: raise the code-2 verdict carrying the REFUSED line."""
    raise _Stop(2, f"matrix: REFUSED: {msg}")


def _fail(msg: str) -> NoReturn:
    """matrix_fail: raise the code-1 verdict carrying the FAIL line."""
    raise _Stop(1, f"matrix: FAIL: {msg}")


def _to_status(body: Callable[[], None]) -> int:
    """Run a harness body; translate its verdict to the bash return code.

    A clean return is 0; a ``_Stop`` prints its line (unless already reported) and
    yields its code. This is the single boundary where a verdict becomes a status,
    so a message prints exactly once, exactly as the bash printed it at the
    matrix_refuse / matrix_fail site and propagated the code up untouched.
    """
    try:
        body()
    except _Stop as stop:
        if stop.line:
            print(stop.line, file=sys.stderr, flush=True)
        return stop.code
    return 0


# ---- environment and workspace paths ---------------------------------------


def _require_env(name: str) -> str:
    """The bash ``${VAR:?}``: return the value, or refuse loudly naming the var."""
    value = os.environ.get(name)
    if not value:
        _refuse(f"required environment variable {name} is unset (run through run-matrix/run-e2e)")
    return value


def _ensure_nested(helper: str) -> None:
    """A nested-only helper refuses loudly outside nested mode (a safety boundary:
    matrix_stage rm -rf's E2E_CONFIG_HOME, which must never touch a live session).
    """
    mode = os.environ.get("E2E_MODE")
    if mode != "nested":
        _refuse(
            f"{helper} is nested-only (it manages the vehicle dock / nested kwin); "
            f"refusing in mode '{mode or 'unset'}'"
        )


def _matrix_work() -> Path:
    return Path(_require_env("E2E_ARTIFACTS")) / "matrix"


def _matrix_pristine() -> Path:
    # The loaded seed belongs to one nested vehicle; keeping it in the per-run
    # runtime (not persistent artifacts) stops a later run reusing this layout.
    return Path(_require_env("E2E_RT")) / "matrix-pristine-seed"


def pristine_seed_dir() -> Path:
    """The pristine seed matrix_init captured: the untouched base a scenario recipe
    restores E2E_CONFIG_HOME to when staging has mutated it in place.
    """
    return _matrix_pristine()


def _matrix_baseline_dir() -> Path:
    return _matrix_work() / "_baseline"


def _stage_timeout() -> int:
    return int(os.environ.get("MATRIX_STAGE_TIMEOUT") or _DEFAULT_STAGE_TIMEOUT)


# ---- the view readback (pydantic at the boundary) --------------------------
#
# The residue snapshot needs a few residue-GEOMETRY fields (struts/mask/input
# region, onPrimary, isOffScreen) that no other consumer reads, so MatrixView
# extends recipe.View (the shared viewsData surface) with exactly those; it
# validates the whole viewsData reply at the boundary (extra="ignore", inherited,
# so a dock-side field addition never breaks a recipe) and serializes exactly the
# residue fields for byte-comparison.

# The residue-relevant view fields, in the same set the bash matrix_probe_view
# snapshotted. json.dumps(..., sort_keys=True) makes the serialization
# order-stable and byte-identical to the bash formula.
_VIEW_RESIDUE_KEYS = (
    "type",
    "edge",
    "alignment",
    "screen",
    "onPrimary",
    "editMode",
    "inConfigureAppletsMode",
    "isHidden",
    "isOffScreen",
    "strutsThickness",
    "publishedStruts",
    "maskRect",
    "inputRegionRects",
    "absoluteGeometry",
    "localGeometry",
    "screenGeometry",
)


class MatrixView(recipe.View):
    """A viewsData record widened with the residue-only fields, built on recipe.View.

    W3 (widen the readback models): the identity, placement, and mode fields the
    residue snapshot shares with the rest of the harness (containmentId, type/edge/
    alignment/screen, editMode, isHidden, ...) now live on the shared recipe.View,
    which this extends rather than re-declares. Only the residue-GEOMETRY fields no
    recipe outside the abort-residue check reads (the struts/mask/input-region quads,
    onPrimary, isOffScreen) are declared here. A malformed reply (a short geometry
    array, a non-bool flag) still fails loudly at the boundary, and extra="ignore"
    (inherited from recipe.View) still tolerates a dock-side field addition.
    """

    #! view_type and input_region_rects moved to the shared recipe.View when the
    #! 070 maximize-length mask recipe's port started reading them (the same W3
    #! promotion this class's docstring describes)
    on_primary: bool = Field(alias="onPrimary")
    is_off_screen: bool = Field(alias="isOffScreen")
    struts_thickness: int = Field(alias="strutsThickness")
    published_struts: Rect = Field(alias="publishedStruts")
    mask_rect: Rect = Field(alias="maskRect")

    def residue_snapshot(self) -> str:
        """The deterministic residue snapshot for byte-comparison across an abort.

        Byte-identical to the bash ``json.dumps({k: v.get(k) for k in fields},
        sort_keys=True)``: the same 16 keys, the QRect quads as [x,y,w,h] arrays,
        the input region as an array of rects, keys sorted.
        """
        fields: dict[str, object] = {
            "type": self.view_type,
            "edge": self.edge,
            "alignment": self.alignment,
            "screen": self.screen,
            "onPrimary": self.on_primary,
            "editMode": self.edit_mode,
            "inConfigureAppletsMode": self.in_configure_applets_mode,
            "isHidden": self.is_hidden,
            "isOffScreen": self.is_off_screen,
            "strutsThickness": self.struts_thickness,
            "publishedStruts": list(self.published_struts),
            "maskRect": list(self.mask_rect),
            "inputRegionRects": [list(r) for r in self.input_region_rects],
            "absoluteGeometry": list(self.absolute_geometry),
            "localGeometry": list(self.local_geometry),
            "screenGeometry": list(self.screen_geometry),
        }
        return json.dumps(fields, sort_keys=True)


_MATRIX_VIEWS = TypeAdapter(list[MatrixView])


def _matrix_views() -> list[MatrixView]:
    """viewsData, validated into typed MatrixView records.

    Routes through recipe.read_json so a refused reply raises the pollable
    DbusUnavailableError (the one W2 refusal channel) rather than a misleading
    ValidationError about "" - a delivered-but-misshapen reply still fails pydantic
    validation loudly, naming the offending field.
    """
    return _MATRIX_VIEWS.validate_python(recipe.read_json("viewsData"))


def _find_view(view: int) -> MatrixView | None:
    return next((v for v in _matrix_views() if v.containment_id == view), None)


# ---- cell parsing (the harness-side malformed-fixture guard) ---------------


@dataclass(frozen=True, slots=True)
class ParsedCell:
    """A validated matrix cell: every token is one of the closed fixture sets."""

    view_type: ViewType
    edge: Edge
    alignment: Alignment
    display: Display


def parse_cell(cell: str) -> ParsedCell:
    """matrix_cell_parse: split ``<vt>-<edge>-<align>-<display>`` and validate
    every token. A malformed cell is REFUSED here (code 2), the harness-side half
    of the malformed-fixture proof (the generator refuses the other half).
    """
    parts = cell.split("-")
    view_type = parts[0] if len(parts) > 0 else ""
    edge = parts[1] if len(parts) > 1 else ""
    alignment = parts[2] if len(parts) > 2 else ""
    display = parts[3] if len(parts) > 3 else ""
    extra = "-".join(parts[4:]) if len(parts) > 4 else ""
    if not view_type or not edge or not alignment or not display or extra:
        _refuse(f"cell '{cell}' is not <viewType>-<edge>-<alignment>-<display>")
    if view_type not in VIEW_TYPES:
        _refuse(f"cell '{cell}': bad viewType '{view_type}'")
    if edge not in EDGES:
        _refuse(f"cell '{cell}': bad edge '{edge}'")
    if alignment not in ALIGNMENTS:
        _refuse(f"cell '{cell}': bad alignment '{alignment}'")
    if display not in DISPLAYS:
        _refuse(f"cell '{cell}': bad display '{display}'")
    # the `not in <closed tuple>` guards above narrow each token to its Literal.
    return ParsedCell(view_type=view_type, edge=edge, alignment=alignment, display=display)


def _build_descriptor(cell: str, parsed: ParsedCell) -> matrix_fixture.CellDescriptor:
    """The fixture descriptor for a cell, with the P1 2out secondary-output pin.

    A 2out cell pins to the SECONDARY output the multi-output vehicle discovered
    (E2E_MO_SECONDARY*). Absent that discovery no pin is passed, so the fixture
    falls back to its sentinel screen id and the dock REFUSES the 2out view rather
    than silently placing it on the primary (the degenerate case fails loud).
    """
    screen = ""
    screen_id = matrix_fixture.SECONDARY_ABSENT_SCREEN_ID
    screen_geometry = ""
    secondary = os.environ.get("E2E_MO_SECONDARY")
    if parsed.display == "2out" and secondary:
        screen = secondary
        secondary_id = os.environ.get("E2E_MO_SECONDARY_ID")
        if not secondary_id:
            _refuse(
                "multi_output.mo_discover_outputs must set E2E_MO_SECONDARY_ID"
                " alongside E2E_MO_SECONDARY"
            )
        screen_id = int(secondary_id)
        screen_geometry = os.environ.get("E2E_MO_SECONDARY_GEOM", "")
    return matrix_fixture.CellDescriptor(
        view_type=parsed.view_type,
        edge=parsed.edge,
        alignment=parsed.alignment,
        display=parsed.display,
        screen=screen,
        screen_id=screen_id,
        screen_geometry=screen_geometry,
        cell=cell,
    )


# ---- fixture generation (the fixture module as a library) ------------------


def gen(cell: str, out_dir: str | Path) -> matrix_fixture.Manifest:
    """matrix_gen: generate a cell fixture into ``out_dir``, return its manifest.

    Calls latte_harness.matrix_fixture as a LIBRARY (never a per-call subprocess);
    a bad cell is refused by parse_cell first, a bad seed by the generator's
    RefusedError (rendered as the same ``fixture.py: REFUSED`` line and code 2 the
    subprocess produced). The matrix-cell.json write mirrors the fixture CLI so a
    staged cell dir is byte-identical whether generated through the subprocess or
    this library.
    """
    parsed = parse_cell(cell)
    descriptor = _build_descriptor(cell, parsed)
    out_path = Path(out_dir)
    try:
        manifest = matrix_fixture.generate(str(_matrix_pristine()), str(out_path), descriptor)
    except matrix_fixture.RefusedError as refusal:
        raise _Stop(2, f"{matrix_fixture.TOOL}: REFUSED: {refusal}") from refusal
    payload: dict[str, object] = manifest.model_dump(by_alias=True)
    with (out_path / "matrix-cell.json").open("w") as handle:
        json.dump(payload, handle, indent=2)
        _ = handle.write("\n")
    return manifest


# ---- init and staging ------------------------------------------------------


def _init() -> None:
    _ensure_nested("matrix_init")
    _matrix_work().mkdir(parents=True, exist_ok=True)
    pristine = _matrix_pristine()
    if not pristine.is_dir():
        if pristine.exists():
            pristine.unlink()
        _ = shutil.copytree(Path(_require_env("E2E_CONFIG_HOME")), pristine)
        for bak in (pristine / "latte").glob("*.bak"):
            with suppress(OSError):
                bak.unlink()


def init() -> int:
    """matrix_init: snapshot the pristine seed the fixtures derive from (once)."""
    return _to_status(_init)


def stop_dock(timeout: int = 25) -> bool:
    """SIGTERM the staged dock, best-effort and quiet (the matrix contract).

    recipe.dock_stop reaps a recipe-started child since the D275 fix (a
    recipe-started dock stayed a zombie its parent never reaped), so the
    local reaping loop this function carried as a workaround is gone; what
    remains is the matrix-specific contract the bash matrix_stage had with
    ``e2e_dock_stop >/dev/null 2>&1 || true``: suppress the stop's chatter
    and report the outcome without failing the scenario.
    """
    sink = io.StringIO()
    with redirect_stderr(sink):
        return recipe.dock_stop(timeout=timeout)


def _print_dock_log_tail(lines: int = 20) -> None:
    log = os.environ.get("E2E_DOCK_LOG")
    if not log or not Path(log).is_file():
        return
    for line in Path(log).read_text(errors="replace").splitlines()[-lines:]:
        print(line, file=sys.stderr, flush=True)


def _stage(cell: str) -> None:
    _ensure_nested("matrix_stage")
    celldir = _matrix_work() / cell
    manifest = gen(cell, celldir)

    # swap the dock onto the cell config: same config-home PATH (so E2E_LAYOUT
    # stays valid), fresh contents.
    _ = stop_dock()
    config_home = Path(_require_env("E2E_CONFIG_HOME"))
    if config_home.exists():
        shutil.rmtree(config_home)
    _ = shutil.copytree(celldir, config_home)
    if not recipe.dock_start(_stage_timeout()):
        print(
            f"matrix: dock did not settle on cell '{cell}'; dock log tail:",
            file=sys.stderr,
            flush=True,
        )
        _print_dock_log_tail()
        _refuse(f"cell '{cell}' did not bring up a settled view (dock refused the fixture)")
    _assert_realized_body(cell, manifest.expect)


def stage(cell: str) -> int:
    """matrix_stage: generate the cell, restart the dock onto it, and assert the
    view realized AS DECLARED. A fixture that stages but does not realize is a
    refusal (code 2), never trusted as a silent wrong cell.
    """
    return _to_status(lambda: _stage(cell))


# ---- view identity and realization -----------------------------------------


def view_id() -> int:
    """matrix_view_id: the containment id of the single non-cloned view under test.

    A degenerate count is a symptom to surface (raises MatrixProbeError), never
    papered over - the fixture seeds exactly one non-cloned Latte view.
    """
    non_cloned = [v for v in _matrix_views() if not v.is_cloned]
    if len(non_cloned) != 1:
        print(
            f"matrix_view_id: expected exactly one non-cloned view, saw {len(non_cloned)}",
            file=sys.stderr,
            flush=True,
        )
        raise MatrixProbeError
    return non_cloned[0].containment_id


def _view_id_or_refuse() -> int:
    """view_id for a scenario: a missing view is the ``|| return 2`` refuse (the
    view_id message is already on stderr, so the verdict is silent)."""
    try:
        return view_id()
    except MatrixProbeError as err:
        raise _Stop(2) from err


def _assert_realized_body(cell: str, expect: matrix_fixture.ExpectedRealization) -> None:
    try:
        view = view_id()
    except MatrixProbeError as err:
        raise _Stop(2, f"matrix: REFUSED: cell '{cell}': no view under test realized") from err
    found = _find_view(view)
    if found is None:
        _refuse(f"cell '{cell}': realization read failed: no view {view}")
    want = {"type": expect.type, "edge": expect.edge, "alignment": expect.alignment}
    got = {"type": found.view_type, "edge": found.edge, "alignment": found.alignment}
    if want != got:
        _refuse(
            f"cell '{cell}' did not realize as declared: "
            f"MISMATCH want={json.dumps(want)} got={json.dumps(got)}"
        )


def assert_realized(cell: str, expect: matrix_fixture.ExpectedRealization) -> int:
    """matrix_assert_realized: the staged view must report the type/edge/alignment
    the manifest declared. Mismatch is a refusal (code 2).
    """
    return _to_status(lambda: _assert_realized_body(cell, expect))


# ---- residue probes --------------------------------------------------------


def probe_view(view: int) -> str:
    """matrix_probe_view: the residue-relevant view fields, normalized snapshot."""
    found = _find_view(view)
    if found is None:
        print(f"matrix_probe_view: view {view} gone", file=sys.stderr, flush=True)
        raise MatrixProbeError
    return found.residue_snapshot()


def probe_applets_order(view: int) -> str:
    """matrix_probe_applets_order: the applet visual order (the raw ``as`` array).

    A D-Bus call failure is NOT swallowed into a plausible-but-empty answer (which
    would read as the same empty order on both sides = a false PASS); the reply is
    validated as an ``as`` array and any error surfaces loudly (MatrixProbeError),
    so the backbone treats it as unassertable, never clean.
    """
    code, stdout = recipe.call_status("viewAppletsOrder", "u", str(view))
    if code != 0:
        print(
            f"matrix: viewAppletsOrder call FAILED for view {view} "
            "(D-Bus error, not an empty order)",
            file=sys.stderr,
            flush=True,
        )
        raise MatrixProbeError
    reply = stdout.rstrip("\n")
    if not reply.startswith("as "):
        print(
            f"matrix: viewAppletsOrder reply is not an 'as' array: {reply}",
            file=sys.stderr,
            flush=True,
        )
        raise MatrixProbeError
    return reply


def _kconfig_snapshot(path: Path, prefix: str = "") -> str:
    """A KConfig-default-aware snapshot of ``path``, optionally limited to groups
    at or under ``prefix`` ([Foo] and [Foo][Bar], not [Foobar]).

    Groups and keys are sorted so a semantic change surfaces regardless of KConfig
    line order. Volatile keys (a base set plus MATRIX_VOLATILE_EXTRA) are stripped.
    A key that returned to its default (KConfig deletes it) surfaces as a removed
    line = a diff = a false-FAIL, the SAFE direction (never a false-PASS). A
    missing file surfaces loudly (MatrixProbeError), never an empty clean snapshot.
    """
    if not path.is_file():
        print(f"matrix: kconfig snapshot: file missing: {path}", file=sys.stderr, flush=True)
        raise MatrixProbeError
    extra = [
        a for a in re.split(r"[|\s]+", os.environ.get("MATRIX_VOLATILE_EXTRA", "").strip()) if a
    ]
    joined = "|".join(_VOLATILE_BASE + extra)
    volatile = re.compile(rf"^(?:{joined})=")

    def in_scope(group: str) -> bool:
        return (not prefix) or group == prefix or group.startswith(prefix + "[")

    groups: dict[str, list[str]] = {}
    order: list[str] = []
    group: str | None = None
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if line.startswith("[") and line.endswith("]"):
            group = line
            if in_scope(group) and group not in groups:
                groups[group] = []
                order.append(group)
        elif line and group is not None and in_scope(group) and not volatile.match(line):
            groups[group].append(line)
    out: list[str] = []
    for group_header in sorted(order):
        out.append(group_header)
        out.extend(sorted(groups[group_header]))
    return "\n".join(out)


def probe_config() -> str:
    """matrix_probe_config: the whole LAYOUT config (the broad residue net)."""
    return _kconfig_snapshot(Path(_require_env("E2E_LAYOUT")))


def probe_universal() -> str:
    """matrix_probe_universal: lattedockrc [UniversalSettings] persisted residue."""
    return _kconfig_snapshot(
        Path(_require_env("E2E_CONFIG_HOME")) / "lattedockrc", "[UniversalSettings]"
    )


def probe_screenpool() -> str:
    """matrix_probe_screenpool: lattedockrc [ScreenConnectors] (an A4 phantom
    connector left by a cross-screen move abort is the residue this catches).
    """
    return _kconfig_snapshot(
        Path(_require_env("E2E_CONFIG_HOME")) / "lattedockrc", "[ScreenConnectors]"
    )


def probe_applet_config(group: str) -> str:
    """matrix_probe_applet_config: one applet's config subtree in the layout (e.g.
    the tasks applet's group, where the ``launchers`` key strands a reorder abort).
    """
    return _kconfig_snapshot(Path(_require_env("E2E_LAYOUT")), group)


def applet_config_group(view: int, plugin: str) -> str:
    """matrix_applet_config_group: the [Containments][v][Applets][a] group prefix
    for the single applet of ``plugin`` under ``view``. Refuses if the applet is
    not present exactly once (a degenerate count is a symptom, not to paper over).
    """
    matched = [a for a in recipe.view_applets(view) if a.plugin == plugin]
    if len(matched) != 1:
        message = (
            f"matrix_applet_config_group: expected exactly one {plugin} applet "
            f"under view {view}, saw {len(matched)}"
        )
        print(message, file=sys.stderr, flush=True)
        raise MatrixError(message)
    return f"[Containments][{view}][Applets][{matched[0].id}]"


# ---- the baseline backbone -------------------------------------------------


@dataclass(frozen=True, slots=True)
class Baseline:
    """A residue baseline: one snapshot per surface, plus an optional clean frame.

    Passed by value from ``baseline_capture`` to ``assert_baseline_restored`` (the
    bash stored these under MATRIX_BASELINE files; the explicit value is the same
    contract without the filename-sanitization dance).
    """

    snapshots: dict[str, str]
    frame: Path | None


def surface_list() -> list[str]:
    """matrix_surface_list: the residue surfaces, fixed core plus one
    ``appletcfg:<group>`` per MATRIX_APPLET_CONFIG_GROUPS entry (the seam a scenario
    sets, read from the environment exactly as the bash recipes set it).
    """
    surfaces = ["view", "applets_order", "config", "universal", "screenpool", "verb"]
    surfaces += [
        f"appletcfg:{group}" for group in os.environ.get("MATRIX_APPLET_CONFIG_GROUPS", "").split()
    ]
    return surfaces


def capture_surface(surface: str, view: int, verb: str) -> str:
    """matrix_capture_surface: one surface's snapshot. A probe failure is NOT masked
    into an empty snapshot (never-swallow) - it propagates as MatrixProbeError.
    """
    if surface == "view":
        return probe_view(view)
    if surface == "applets_order":
        return probe_applets_order(view)
    if surface == "config":
        return probe_config()
    if surface == "universal":
        return probe_universal()
    if surface == "screenpool":
        return probe_screenpool()
    if surface == "verb":
        return verb_probe(verb, view)
    if surface.startswith("appletcfg:"):
        return probe_applet_config(surface[len("appletcfg:") :])
    print(f"matrix: unknown residue surface '{surface}'", file=sys.stderr, flush=True)
    raise MatrixProbeError


def _capture_frame_enabled() -> bool:
    return os.environ.get("MATRIX_CAPTURE_FRAME") == "1"


def _capture_baseline_frame() -> Path | None:
    """The clean baseline frame (cursor excluded). Best-effort: a shot failure
    leaves the visual-residue check off, exactly as the bash ``|| true``."""
    baseline_dir = _matrix_baseline_dir()
    baseline_dir.mkdir(parents=True, exist_ok=True)
    frame = baseline_dir / "frame.png"
    try:
        recipe.screenshot(str(frame), "include-cursor", "b", "false")
    except recipe.RecipeError:
        return None
    return frame


def baseline_capture(view: int, verb: str) -> Baseline:
    """matrix_baseline_capture: snapshot every residue surface. A probe that fails
    here is a BROKEN baseline, not a clean one: it re-raises MatrixProbeError (after
    naming the surface) rather than recording an empty snapshot a later broken probe
    would falsely match.
    """
    snapshots: dict[str, str] = {}
    for surface in surface_list():
        try:
            snapshots[surface] = capture_surface(surface, view, verb)
        except MatrixProbeError:
            print(
                f"matrix: baseline capture FAILED for surface '{surface}' "
                "(probe error) - baseline invalid",
                file=sys.stderr,
                flush=True,
            )
            raise
    frame = _capture_baseline_frame() if _capture_frame_enabled() else None
    return Baseline(snapshots=snapshots, frame=frame)


def _print_surface_diff(base: str, now: str) -> None:
    for line in difflib.unified_diff(
        base.splitlines(), now.splitlines(), fromfile="baseline", tofile="after", lineterm=""
    ):
        print(line, file=sys.stderr, flush=True)


def _frame_equals_baseline(baseline_frame: Path) -> bool:
    """The visual-residue comparison: re-shoot and route through the golden bridge's
    tier-aware compare. A re-shoot failure skips the check (no residue asserted),
    matching the bash ``if e2e_screenshot ...; then`` guard.
    """
    after = _matrix_baseline_dir() / "frame.after.png"
    try:
        recipe.screenshot(str(after), "include-cursor", "b", "false")
    except recipe.RecipeError:
        return True
    return matrix_golden.golden_compare(str(after), str(baseline_frame)) == 0


def assert_baseline_restored(view: int, verb: str, baseline: Baseline) -> int:
    """matrix_assert_baseline_restored: re-read every residue surface and assert
    byte-identical to baseline. Any diff is residue (return 1), naming the surface
    so the failure localizes. A probe that fails on re-read is surfaced as an
    unassertable surface, never swallowed into a silent pass.
    """
    bad = False
    for surface in surface_list():
        base = baseline.snapshots.get(surface)
        if base is None:
            print(
                f"matrix: no baseline snapshot for surface '{surface}'", file=sys.stderr, flush=True
            )
            bad = True
            continue
        try:
            now = capture_surface(surface, view, verb)
        except MatrixProbeError:
            print(
                f"matrix: probe for surface '{surface}' FAILED on re-read "
                "- unassertable, not a clean pass",
                file=sys.stderr,
                flush=True,
            )
            bad = True
            continue
        if now != base:
            print(
                f"matrix: RESIDUE in surface '{surface}' after abort:", file=sys.stderr, flush=True
            )
            _print_surface_diff(base, now)
            bad = True
    if baseline.frame is not None and not _frame_equals_baseline(baseline.frame):
        print(
            "matrix: VISUAL RESIDUE: post-abort frame differs from the clean baseline",
            file=sys.stderr,
            flush=True,
        )
        bad = True
    return 1 if bad else 0


# ---- verb dispatch ---------------------------------------------------------


@dataclass(frozen=True, slots=True)
class _Verb:
    drive: Callable[[int, str], None]
    probe: Callable[[int], str]


_VERBS: dict[str, _Verb] = {}


def register_verb(
    name: str, drive: Callable[[int, str], None], probe: Callable[[int], str]
) -> None:
    """Register a verb's driver/probe pair (the typed twin of defining
    matrix_verb_<name>_drive / _probe). The harness dispatches by name and refuses
    an unknown verb - a boundary check, not a silent no-op.
    """
    _VERBS[name] = _Verb(drive=drive, probe=probe)


def drive_action(method: str, *args: str) -> None:
    """Fire a lattedock D-Bus action for a verb driver, refusing to swallow a
    failure: a nonzero call raises MatrixDriveError, which the scenario translates
    to a refusal. Verb drivers use this instead of a status-blind call.
    """
    code, _ = recipe.call_status(method, *args)
    if code != 0:
        raise MatrixDriveError(f"D-Bus action {method} failed")


def verb_probe(verb: str, view: int) -> str:
    """matrix_verb_probe: dispatch to the verb's probe, refusing an unknown verb."""
    driver = _VERBS.get(verb)
    if driver is None:
        _refuse(f"unknown verb '{verb}' (no matrix_verb_{verb}_probe defined)")
    return driver.probe(view)


def _drive_verb(verb: str, view: int, outcome: str) -> None:
    driver = _VERBS.get(verb)
    if driver is None:
        _refuse(f"unknown verb '{verb}' (no matrix_verb_{verb}_drive defined)")
    try:
        driver.drive(view, outcome)
    except MatrixDriveError as err:
        raise _Stop(2) from err


def verb_drive(verb: str, view: int, outcome: str) -> int:
    """matrix_verb_drive: dispatch to the verb's driver, refusing an unknown verb
    (code 2) and translating a driver's D-Bus failure to a refusal.
    """
    return _to_status(lambda: _drive_verb(verb, view, outcome))


# ---- the built-in editmode verb --------------------------------------------
# Uses ONLY the existing setViewEditMode action, so the harness self-tests a real
# interaction with no dependency on the later drivers. Commit = enter edit mode;
# abort = enter then exit (the no-op edit session).


def verb_editmode_probe(view: int) -> str:
    """The editmode verb's one queryable fact: the view's editMode flag."""
    found = _find_view(view)
    if found is None:
        print(f"editmode probe: view {view} gone", file=sys.stderr, flush=True)
        raise MatrixProbeError
    return "true" if found.edit_mode else "false"


def _wait_editmode(view: int, want: str) -> None:
    """Poll the editMode flip into the readback (edit chrome comes up async). A
    probe that cannot read yet counts as not-the-target (the bash empty != want)."""
    for _ in range(30):
        with suppress(MatrixProbeError):
            if verb_editmode_probe(view) == want:
                return
        time.sleep(0.2)


def verb_editmode_drive(view: int, outcome: str) -> None:
    """The editmode verb driver. Enters edit mode; on abort, exits again."""
    drive_action("setViewEditMode", "ub", str(view), "true")
    _wait_editmode(view, "true")
    if outcome == "abort":
        drive_action("setViewEditMode", "ub", str(view), "false")
        _wait_editmode(view, "false")
    # SETTLING CONTRACT (see matrix-lib.sh header): the residue diff is byte-exact
    # on geometry/struts/mask, so do not return mid-animation. Edit mode grows the
    # dock to editThickness and shrinks it back on exit; wait for the geometry to
    # stop moving before the caller snapshots. Best-effort: a settle timeout is left
    # to surface as a geometry diff (the safe false-FAIL direction), not hidden here.
    _ = recipe.wait_settled(15)


register_verb("editmode", verb_editmode_drive, verb_editmode_probe)


# ---- scenario composition --------------------------------------------------


def _scenario_commit(cell: str, verb: str, expected: str) -> None:
    _stage(cell)
    view = _view_id_or_refuse()
    _drive_verb(verb, view, "commit")
    try:
        actual = verb_probe(verb, view)
    except MatrixProbeError as err:
        raise _Stop(2) from err
    if actual == expected:
        print(f"matrix: PASS commit {cell}/{verb} -> {actual}", flush=True)
        return
    _fail(f"commit {cell}/{verb}: expected '{expected}' got '{actual}'")


def scenario_commit(cell: str, verb: str, expected: str) -> int:
    """A COMMITTED scenario: stage the cell, drive the verb to commit, read its
    probe, and assert it equals ``expected``.
      return 0 PASS   1 FAIL (readback != expected)   2 REFUSED
    """
    return _to_status(lambda: _scenario_commit(cell, verb, expected))


def _scenario_abort(cell: str, verb: str) -> None:
    _stage(cell)
    view = _view_id_or_refuse()
    try:
        baseline = baseline_capture(view, verb)
    except MatrixProbeError as err:
        raise _Stop(2, f"matrix: REFUSED: abort {cell}/{verb}: baseline capture failed") from err
    _drive_verb(verb, view, "abort")
    if assert_baseline_restored(view, verb, baseline) == 0:
        print(f"matrix: PASS abort {cell}/{verb} -> baseline restored (no residue)", flush=True)
        return
    _fail(f"abort {cell}/{verb}: residue against baseline")


def scenario_abort(cell: str, verb: str) -> int:
    """An ABORT scenario: stage, capture the baseline, drive the verb to abort, and
    assert every residue surface is byte-identical afterwards.
      return 0 PASS (no residue)   1 FAIL (residue)   2 REFUSED
    """
    return _to_status(lambda: _scenario_abort(cell, verb))
