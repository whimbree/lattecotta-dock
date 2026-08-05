# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
"""Parametrized fixture generator for the e2e interaction matrix (P0 /
docs/tracking/e2e-interaction-test-plan.md section 5). Given a cell descriptor
(viewType, edge, alignment, display) it produces a hermetic Latte config
directory that seeds exactly one view in that configuration.

Promoted from tests/e2e/matrix/fixture.py (BP-1b): same algorithm, now typed
under basedpyright strict with the KConfig document and the cell/manifest
structures as pydantic models so the token space is a closed set of Literals
and the malformed-descriptor states are unrepresentable past the boundary.

It EXTENDS a seeded default config (latte_harness.seed / lib-e2e-seed.sh already
produces the "My Layout" default the vehicle can load) rather than synthesising
a layout from nothing: it copies the proven-loadable seed and rewrites the
single Latte view containment's edge/alignment/view-type/screen keys. That keeps
the fixture faithful to a real first-run config (KConfig upgrade flags,
lattedockrc, applet groups all intact) and confines the parametrization to the
handful of keys the matrix varies.

This is an API BOUNDARY (CLAUDE.md: qCritical-and-refuse at boundaries that
receive input from outside). A malformed descriptor - a bad token, a seed with
zero or several Latte containments - is REFUSED loudly with exit 2 and NO output
written, never clamped into a plausible-but-wrong config. The harness surfaces
that refusal (HC3: the generator must be able to reject a malformed cell, not
silently emit a green fixture). Refusal is raised as RefusedError so callers can
unit-test every refusal path; main() renders it as the exact "fixture.py:
REFUSED: <msg>" stderr line and exit 2 the matrix harness controls assert on.

The dock-vs-panel knob is not a single stored flag: viewsData.type is the
QML-derived viewType (containment/plugin/units/backgroundstate.h
resolveViewTypeInQuestion), a Panel only when parabolic zoom is off AND the
layout cannot slide (Justify alignment or a static min==max length) AND the
theme background is at least as thick as the icon row. So a panel fixture turns
zoom off, pins the length, and thickens the background; a dock fixture keeps zoom
on (which forces Dock regardless of the other knobs). The harness still ASSERTS
the realized type by readback - if the derivation does not hold, that is caught
as a staging refusal, never a silent wrong cell.
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
from pathlib import Path
from typing import Literal, NoReturn, cast

from pydantic import BaseModel, ConfigDict, Field

# --- token spaces, pinned from the source of truth --------------------------

ViewType = Literal["dock", "panel"]
Edge = Literal["top", "bottom", "left", "right"]
Alignment = Literal["left", "center", "right", "justify"]
Display = Literal["1out", "2out"]
# The readback strings viewsData reports; the alignment axis rotates with the
# edge, so a vertical "left"/"right" reads back as "top"/"bottom".
AlignmentReadback = Literal["left", "center", "right", "top", "bottom", "justify"]

VIEW_TYPES: tuple[ViewType, ...] = ("dock", "panel")
EDGES: tuple[Edge, ...] = ("top", "bottom", "left", "right")
ALIGNMENTS: tuple[Alignment, ...] = ("left", "center", "right", "justify")
DISPLAYS: tuple[Display, ...] = ("1out", "2out")

# Plasma::Types::Location (libplasma plasma.h): the containment "location" key.
LOCATION: dict[Edge, int] = {"top": 3, "bottom": 4, "left": 5, "right": 6}
# Plasma::Types::FormFactor: Horizontal=2 for top/bottom, Vertical=3 for left/right.
FORMFACTOR: dict[Edge, int] = {"top": 2, "bottom": 2, "left": 3, "right": 3}
HORIZONTAL_EDGES: frozenset[Edge] = frozenset({"top", "bottom"})

# Latte::Types::Alignment (declarativeimports/coretypes.h.in). The alignment axis
# rotates with the edge: on a horizontal edge the three non-justify positions are
# Left/Center/Right; on a vertical edge they are Top/Center/Bottom. The matrix
# labels every cell {left,center,right,justify}; for a vertical edge "left" means
# the begin end (Top) and "right" the far end (Bottom). Justify is
# orientation-independent.
ALIGN_ENUM_HORIZONTAL: dict[Alignment, int] = {"left": 1, "center": 0, "right": 2, "justify": 10}
ALIGN_ENUM_VERTICAL: dict[Alignment, int] = {"left": 3, "center": 0, "right": 4, "justify": 10}
# The string viewsData reports back for each enum value (app/dbusreports.h
# alignmentToString): this is what the harness's realization check compares to.
ALIGN_READBACK_HORIZONTAL: dict[Alignment, AlignmentReadback] = {
    "left": "left",
    "center": "center",
    "right": "right",
    "justify": "justify",
}
ALIGN_READBACK_VERTICAL: dict[Alignment, AlignmentReadback] = {
    "left": "top",
    "center": "center",
    "right": "bottom",
    "justify": "justify",
}

LATTE_CONTAINMENT_PLUGIN = "org.kde.latte.containment"
LATTE_TASKS_PLUGIN = "org.kde.latte.plasmoid"

# A 2out view pins to a SECONDARY output by the pair Latte actually reads:
# onPrimary=false plus lastScreen=<numeric ScreenPool id> (the dead
# `explicitScreen` key earlier fixtures wrote is not consulted anywhere in the
# app - grep app/ for it: only local variables and a computed list, never a
# config read). ScreenPool resolves that id to a connector via the
# [ScreenConnectors] group in lattedockrc, so a fixture that pins lastScreen must
# ALSO seed that mapping or the id resolves to nothing.
#
# When the multi-output vehicle (C-I2/P1) has discovered the real secondary
# connector it passes --screen/--screen-id/--screen-geometry and this fixture
# seeds a valid mapping. When NO secondary was discovered (a single-output run
# asked for a dual cell) the id defaults to this sentinel, which no connector
# claims, so the dock REJECTS the view ("Adding View: ... Rejected because Screen
# is not available", genericlayout.cpp) instead of silently placing it on the
# primary. A silent wrong-output placement is exactly the class the whole e2e
# suite exists to catch, so the degenerate case fails loud.
SECONDARY_ABSENT_SCREEN_ID = 999
LATTE_SCREEN_CONNECTORS_GROUP = "[ScreenConnectors]"
# Data::Screen::serialize() (app/data/screendata.cpp): "<name>:::<x,y wxh>".
SCREEN_SERIALIZE_SPLITTER = ":::"

TOOL = "fixture.py"


class RefusedError(Exception):
    """A malformed descriptor or seed at the fixture boundary.

    Carried as an exception (not an immediate exit) so every refusal path is
    unit-testable; main() renders it as the exact stderr line and exit code 2
    the matrix-fixture check and matrix-lib controls assert on.
    """


def _refuse(msg: str) -> NoReturn:
    """Refuse loudly: name the boundary, name the offending input, exit 2."""
    raise RefusedError(msg)


# --- KConfig INI parse/serialize -------------------------------------------
# KConfig groups are line-based: a "[Group][Sub]" header followed by key=value
# lines, blank-line separated. Nested groups ([Containments][1] vs
# [Containments][1][General]) are DISTINCT top-level entries keyed by their full
# header. This is NOT standard INI - values are not stripped, headers nest, and
# duplicate headers re-select an existing group - so it stays a hand-rolled
# parser. Group order and key order are preserved (a plain dict is
# insertion-ordered) so a patched file diffs minimally against its seed and only
# the keys the matrix sets change.


class KConfigDocument(BaseModel):
    """A parsed KConfig file: the lines before the first header, then the ordered
    groups, each an ordered header -> (key -> value) map."""

    model_config = ConfigDict(strict=True)

    preamble: list[str] = Field(default_factory=list)
    groups: dict[str, dict[str, str]] = Field(default_factory=dict)

    @classmethod
    def parse(cls, text: str) -> KConfigDocument:
        groups: dict[str, dict[str, str]] = {}
        preamble: list[str] = []
        current: str | None = None
        for line in text.splitlines():
            stripped = line.strip()
            if stripped.startswith("[") and stripped.endswith("]"):
                current = stripped
                groups.setdefault(current, {})
            elif current is None:
                if stripped:
                    preamble.append(line)
            elif not stripped:
                continue
            else:
                key, sep, value = line.partition("=")
                if not sep:
                    # a header-less non-empty line inside a group is malformed
                    # KConfig; refuse rather than silently drop it
                    _refuse(f"unparseable config line (no '='): {line!r}")
                groups[current][key.strip()] = value
        return cls(preamble=preamble, groups=groups)

    def serialize(self) -> str:
        out: list[str] = []
        out.extend(self.preamble)
        if self.preamble:
            out.append("")
        for header, kv in self.groups.items():
            out.append(header)
            for key, value in kv.items():
                out.append(f"{key}={value}")
            out.append("")
        return "\n".join(out).rstrip("\n") + "\n"

    def set_key(self, header: str, key: str, value: object) -> None:
        self.groups.setdefault(header, {})[key] = str(value)

    def find_latte_containment(self) -> str:
        """The single top-level containment group whose plugin is the Latte
        containment. Zero or several is a malformed seed - refuse."""
        matches: list[str] = []
        for header, kv in self.groups.items():
            # top-level containment header is exactly [Containments][<id>]
            if (
                header.startswith("[Containments][")
                and header.count("[") == 2
                and kv.get("plugin") == LATTE_CONTAINMENT_PLUGIN
            ):
                matches.append(header)
        if not matches:
            _refuse(f"seed layout has no {LATTE_CONTAINMENT_PLUGIN} containment to parametrize")
        if len(matches) > 1:
            _refuse(
                f"seed layout has {len(matches)} Latte containments "
                f"({', '.join(matches)}); the matrix fixture needs exactly one"
            )
        return matches[0]

    def find_tasks_applet(self, containment_header: str) -> str | None:
        """The Latte tasks applet's own group under the containment, or None.
        An applet's own group is [Containments][<c>][Applets][<a>] (four
        brackets); its config sub-groups have more. Returns the header so the
        caller can reach [...][Configuration][General]."""
        prefix = containment_header + "[Applets]["
        for header, kv in self.groups.items():
            if (
                header.startswith(prefix)
                and header.count("[") == 4
                and kv.get("plugin") == LATTE_TASKS_PLUGIN
            ):
                return header
        return None


def general_group_header(containment_header: str) -> str:
    # [Containments][1] -> [Containments][1][General]
    return containment_header + "[General]"


# --- the parametrization ----------------------------------------------------


class ExpectedRealization(BaseModel):
    """What the staged view must report back for the harness realization check."""

    model_config = ConfigDict(strict=True)

    type: ViewType
    edge: Edge
    alignment: AlignmentReadback


class CellDescriptor(BaseModel):
    """A validated matrix cell: every token is a closed Literal set, so a
    malformed descriptor cannot exist as a CellDescriptor - it is refused at
    construction time in main()."""

    model_config = ConfigDict(strict=True, frozen=True)

    view_type: ViewType
    edge: Edge
    alignment: Alignment
    display: Display
    screen: str
    screen_id: int
    screen_geometry: str
    cell: str


def patch_layout(doc: KConfigDocument, descriptor: CellDescriptor) -> ExpectedRealization:
    """Rewrite the single Latte containment in-place for the requested cell and
    return the readback the harness will assert the realized view against."""
    edge = descriptor.edge
    alignment = descriptor.alignment
    cont = doc.find_latte_containment()
    gen = general_group_header(cont)
    horizontal = edge in HORIZONTAL_EDGES

    # edge: Plasma location + form factor on the containment group
    doc.set_key(cont, "location", LOCATION[edge])
    doc.set_key(cont, "formfactor", FORMFACTOR[edge])

    # alignment: the axis-correct Latte enum in [General]
    align_map = ALIGN_ENUM_HORIZONTAL if horizontal else ALIGN_ENUM_VERTICAL
    doc.set_key(gen, "alignment", align_map[alignment])
    # the alignmentUpgraded flag must stay set or the dock re-derives alignment
    # from the deprecated panelPosition and overwrites ours on load
    doc.set_key(gen, "alignmentUpgraded", "true")

    # view type: viewsData.type is the QML-derived viewType. Drive the
    # derivation deterministically (see the module header).
    if descriptor.view_type == "panel":
        doc.set_key(gen, "zoomLevel", 0)  # zero parabolic zoom...
        # ...but zoomLevel=0 alone is not enough: factor.maxZoom is the MAX of
        # the containment zoom and every applet's requested zoomFactor
        # (containment ParabolicEffect.qml). The Latte tasks applet requests
        # 1.65 whenever a high-thickness animation is on (plasmoid main.qml:893 /
        # hasHighThicknessAnimation), and all three of those animations default
        # true - so maxZoom stays 1.65 and resolveViewTypeInQuestion keeps
        # returning Dock. A fixed-thickness panel cannot host those bounce
        # animations anyway, so turning them off is the Qt5-faithful panel
        # config, and it drops the requirement to 1.0 -> maxZoom == 1.0.
        tasks = doc.find_tasks_applet(cont)
        if tasks is not None:
            tgen = tasks + "[Configuration][General]"
            doc.set_key(tgen, "animationLauncherBouncing", "false")
            doc.set_key(tgen, "animationWindowInAttention", "false")
            doc.set_key(tgen, "animationWindowAddedInGroup", "false")
        doc.set_key(gen, "useThemePanel", "true")  # background is a theme panel
        doc.set_key(gen, "panelSize", 100)  # thick: visualThickness >= iconSize
        if alignment != "justify":
            # a non-justify panel needs a static length (min==max) to stop
            # sliding; a full-span panel pins both to 100%
            doc.set_key(gen, "minLength", 100)
            doc.set_key(gen, "maxLength", 100)
        # justify already stops the slide; leave its length free
        doc.set_key(cont, "viewType", 1)  # informational; keeps config self-consistent
    else:  # dock
        doc.set_key(gen, "zoomLevel", 16)  # zoom on forces Dock regardless of length
        doc.set_key(cont, "viewType", 0)

    # screen assignment
    if descriptor.display == "1out":
        doc.set_key(cont, "onPrimary", "true")
        doc.set_key(cont, "screensGroup", 0)  # SingleScreenGroup
        doc.set_key(cont, "lastScreen", -1)  # primary / any
    else:  # 2out - pin to the SECONDARY output by the keys the app reads:
        # onPrimary=false + lastScreen=<ScreenPool id>. The id resolves to a
        # connector through the [ScreenConnectors] group in lattedockrc, seeded
        # by patch_lattedockrc below (C-I2/P1: the multi-output vehicle discovers
        # the real secondary; without one, screen_id is the
        # SECONDARY_ABSENT_SCREEN_ID sentinel so the view is rejected, never
        # mis-placed on the primary).
        doc.set_key(cont, "onPrimary", "false")
        doc.set_key(cont, "screensGroup", 0)  # SingleScreenGroup
        doc.set_key(cont, "lastScreen", descriptor.screen_id)

    # the readback strings the harness will assert the realized view against
    align_readback = (ALIGN_READBACK_HORIZONTAL if horizontal else ALIGN_READBACK_VERTICAL)[
        alignment
    ]
    return ExpectedRealization(type=descriptor.view_type, edge=edge, alignment=align_readback)


def patch_lattedockrc(text: str, screen_id: int, screen_name: str, screen_geometry: str) -> str:
    """Seed the [ScreenConnectors] mapping so a 2out view's lastScreen=<screen_id>
    resolves to <screen_name>. The value mirrors Data::Screen::serialize()
    (app/data/screendata.cpp): "<name>:::<x,y wxh>". The geometry half is optional
    (ScreenPool refreshes it from the live output on load), but a real one avoids
    the transient default rect ever being observed."""
    doc = KConfigDocument.parse(text)
    value = screen_name
    if screen_geometry:
        value = screen_name + SCREEN_SERIALIZE_SPLITTER + screen_geometry
    doc.set_key(LATTE_SCREEN_CONNECTORS_GROUP, str(screen_id), value)
    return doc.serialize()


def find_seed_layout(seed_dir: str) -> Path:
    """The single loadable *.layout.latte under <seed_dir>/latte, or the one the
    lattedockrc names when several are present. Zero or ambiguous is refused."""
    latte_dir = Path(seed_dir) / "latte"
    if not latte_dir.is_dir():
        _refuse(f"seed dir {seed_dir!r} has no latte/ subdir (not a seeded config home)")
    layouts = [
        f.name
        for f in sorted(latte_dir.iterdir())
        if f.name.endswith(".layout.latte") and not f.name.endswith(".bak")
    ]
    if not layouts:
        _refuse(f"seed dir {seed_dir!r} carries no *.layout.latte")
    if len(layouts) > 1:
        # single-layout mode expects one; pick the one lattedockrc names, else refuse
        named: str | None = None
        rc = Path(seed_dir) / "lattedockrc"
        if rc.is_file():
            for line in rc.read_text().splitlines():
                if line.startswith("singleModeLayoutName="):
                    named = line.split("=", 1)[1].strip() + ".layout.latte"
                    break
        if named is not None and named in layouts:
            return latte_dir / named
        _refuse(
            f"seed dir {seed_dir!r} has {len(layouts)} layouts and lattedockrc names none of them"
        )
    return latte_dir / layouts[0]


def _validate_token(name: str, value: str, allowed: tuple[str, ...]) -> None:
    if value not in allowed:
        _refuse(f"{name} {value!r} is not one of {'|'.join(allowed)}")


class Manifest(BaseModel):
    """The matrix-cell.json boundary the harness (matrix-lib.sh) reads back: the
    cell parameters plus the realization the staged view must report."""

    model_config = ConfigDict(strict=True, populate_by_name=True)

    cell: str
    view_type: ViewType = Field(serialization_alias="viewType")
    edge: Edge
    alignment: Alignment
    display: Display
    screen: str
    screen_id: int = Field(serialization_alias="screenId")
    screen_geometry: str = Field(serialization_alias="screenGeometry")
    layout: str
    expect: ExpectedRealization


def _str_attr(namespace: argparse.Namespace, name: str) -> str:
    value: object = getattr(namespace, name)
    if not isinstance(value, str):
        raise TypeError(f"argparse produced a non-str for --{name}: {value!r}")
    return value


def _int_attr(namespace: argparse.Namespace, name: str) -> int:
    value: object = getattr(namespace, name)
    if not isinstance(value, int):
        raise TypeError(f"argparse produced a non-int for --{name}: {value!r}")
    return value


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        prog=TOOL,
        description="Generate a parametrized Latte matrix fixture config.",
    )
    _ = ap.add_argument(
        "--seed-dir",
        required=True,
        help="a seeded config home to derive from (has latte/ + lattedockrc)",
    )
    _ = ap.add_argument("--out-dir", required=True, help="destination config home (overwritten)")
    _ = ap.add_argument("--view-type", required=True)
    _ = ap.add_argument("--edge", required=True)
    _ = ap.add_argument("--alignment", required=True)
    _ = ap.add_argument("--display", default="1out")
    _ = ap.add_argument(
        "--screen",
        default="",
        help="connector name for a 2out secondary pin (P1); "
        "when given, its [ScreenConnectors] mapping is seeded so lastScreen resolves",
    )
    _ = ap.add_argument(
        "--screen-id",
        type=int,
        default=SECONDARY_ABSENT_SCREEN_ID,
        help="numeric ScreenPool id a 2out view pins lastScreen to (P1). Defaults to a "
        "sentinel no connector claims so an undiscovered secondary is refused, not mis-placed",
    )
    _ = ap.add_argument(
        "--screen-geometry",
        default="",
        help="the secondary output geometry as 'x,y WxH' "
        "(Latte's rect string) for the seeded [ScreenConnectors] entry",
    )
    _ = ap.add_argument("--cell", default="", help="cell id for the manifest (default: derived)")
    return ap


def descriptor_from_args(namespace: argparse.Namespace) -> CellDescriptor:
    """Validate the raw CLI tokens with the exact boundary messages, then lift
    them into the closed-Literal CellDescriptor. Bad tokens refuse (exit 2)
    before any output directory is touched."""
    view_type = _str_attr(namespace, "view_type")
    edge = _str_attr(namespace, "edge")
    alignment = _str_attr(namespace, "alignment")
    display = _str_attr(namespace, "display")
    _validate_token("view-type", view_type, VIEW_TYPES)
    _validate_token("edge", edge, EDGES)
    _validate_token("alignment", alignment, ALIGNMENTS)
    _validate_token("display", display, DISPLAYS)

    cell = _str_attr(namespace, "cell") or f"{view_type}-{edge}-{alignment}-{display}"
    return CellDescriptor(
        view_type=cast(ViewType, view_type),
        edge=cast(Edge, edge),
        alignment=cast(Alignment, alignment),
        display=cast(Display, display),
        screen=_str_attr(namespace, "screen"),
        screen_id=_int_attr(namespace, "screen_id"),
        screen_geometry=_str_attr(namespace, "screen_geometry"),
        cell=cell,
    )


def generate(seed_dir: str, out_dir: str, descriptor: CellDescriptor) -> Manifest:
    """Stage a hermetic config for the cell into out_dir and return the manifest.

    Refuses (RefusedError, exit 2) before writing anything when the seed is
    malformed, so a refused fixture leaves no half-written output behind. The one
    intentional exception is a 2out pin to a NAMED secondary whose seed carries no
    lattedockrc: that refuses after the seed is staged, exactly as the original.
    """
    if not Path(seed_dir).is_dir():
        _refuse(f"seed dir {seed_dir!r} does not exist")

    seed_layout = find_seed_layout(seed_dir)
    text = seed_layout.read_text()
    # patch first (may refuse) BEFORE writing anything, so a refused fixture
    # leaves no half-written output dir behind
    doc = KConfigDocument.parse(text)
    expect = patch_layout(doc, descriptor)
    patched = doc.serialize()

    # stage the whole seed dir, then overwrite the one layout we patched
    out_path = Path(out_dir)
    if out_path.exists():
        shutil.rmtree(out_path)
    _ = shutil.copytree(seed_dir, out_dir, ignore=shutil.ignore_patterns("*.bak"))
    out_layout = out_path / "latte" / seed_layout.name
    _ = out_layout.write_text(patched)

    # a 2out pin to a NAMED secondary also needs the [ScreenConnectors] mapping so
    # ScreenPool resolves lastScreen to that connector. The sentinel case (no
    # --screen) writes no mapping on purpose: the id then resolves to nothing and
    # the dock refuses the view (the "no such output" negative).
    if descriptor.display == "2out" and descriptor.screen:
        rc_path = out_path / "lattedockrc"
        if not rc_path.is_file():
            _refuse(
                f"seed dir {seed_dir!r} has no lattedockrc to seed [ScreenConnectors] "
                "into (a 2out pin needs it)"
            )
        rc_text = rc_path.read_text()
        _ = rc_path.write_text(
            patch_lattedockrc(
                rc_text, descriptor.screen_id, descriptor.screen, descriptor.screen_geometry
            )
        )

    return Manifest(
        cell=descriptor.cell,
        view_type=descriptor.view_type,
        edge=descriptor.edge,
        alignment=descriptor.alignment,
        display=descriptor.display,
        screen=descriptor.screen,
        screen_id=descriptor.screen_id,
        screen_geometry=descriptor.screen_geometry,
        layout=seed_layout.name,
        expect=expect,
    )


def _manifest_dict(manifest: Manifest) -> dict[str, object]:
    """The manifest as the exact camelCase JSON dict the old generator emitted,
    field order preserved for byte-identical output."""
    return manifest.model_dump(by_alias=True)


def run(argv: list[str] | None = None) -> int:
    """Parse, generate, write the manifest, echo it. Returns the process exit
    code (0 ok, 2 refused). Refusals are rendered as the exact boundary line."""
    ap = build_parser()
    namespace = ap.parse_args(argv)
    seed_dir = _str_attr(namespace, "seed_dir")
    out_dir = _str_attr(namespace, "out_dir")
    try:
        descriptor = descriptor_from_args(namespace)
        manifest = generate(seed_dir, out_dir, descriptor)
    except RefusedError as refusal:
        print(f"{TOOL}: REFUSED: {refusal}", file=sys.stderr)
        return 2

    payload = _manifest_dict(manifest)
    manifest_path = Path(out_dir) / "matrix-cell.json"
    with manifest_path.open("w") as handle:
        json.dump(payload, handle, indent=2)
        _ = handle.write("\n")

    # stdout is the manifest so the shell harness can read expect.* without a
    # second file read
    print(json.dumps(payload))
    return 0


def main() -> NoReturn:
    sys.exit(run())


if __name__ == "__main__":
    main()
