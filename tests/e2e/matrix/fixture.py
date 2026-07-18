# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Parametrized fixture generator for the e2e interaction matrix (P0 /
# docs/e2e-interaction-test-plan.md section 5). Given a cell descriptor
# (viewType, edge, alignment, display) it produces a hermetic Latte config
# directory that seeds exactly one view in that configuration.
#
# It EXTENDS a seeded default config (scripts/lib-e2e-seed.sh already produces
# the "My Layout" default the vehicle can load) rather than synthesising a
# layout from nothing: it copies the proven-loadable seed and rewrites the
# single Latte view containment's edge/alignment/view-type/screen keys. That
# keeps the fixture faithful to a real first-run config (KConfig upgrade flags,
# lattedockrc, applet groups all intact) and confines the parametrization to
# the handful of keys the matrix varies.
#
# This is an API BOUNDARY (CLAUDE.md: qCritical-and-refuse at boundaries that
# receive input from outside). A malformed descriptor - a bad token, a seed
# with zero or several Latte containments - is REFUSED loudly with a non-zero
# exit and NO output written, never clamped into a plausible-but-wrong config.
# The harness surfaces that refusal (HC3: the generator must be able to reject
# a malformed cell, not silently emit a green fixture).
#
# The dock-vs-panel knob is not a single stored flag: viewsData.type is the
# QML-derived viewType (containment/plugin/units/backgroundstate.h
# resolveViewTypeInQuestion), a Panel only when parabolic zoom is off AND the
# layout cannot slide (Justify alignment or a static min==max length) AND the
# theme background is at least as thick as the icon row. So a panel fixture
# turns zoom off, pins the length, and thickens the background; a dock fixture
# keeps zoom on (which forces Dock regardless of the other knobs). The harness
# still ASSERTS the realized type by readback - if the derivation does not
# hold, that is caught as a staging refusal, never a silent wrong cell.

import argparse
import json
import os
import shutil
import sys
from collections import OrderedDict

# --- enum values, pinned from the source of truth --------------------------
# Plasma::Types::Location (libplasma plasma.h): the containment "location" key.
LOCATION = {"top": 3, "bottom": 4, "left": 5, "right": 6}
# Plasma::Types::FormFactor: Horizontal=2 for top/bottom, Vertical=3 for left/right.
FORMFACTOR = {"top": 2, "bottom": 2, "left": 3, "right": 3}
HORIZONTAL_EDGES = {"top", "bottom"}

# Latte::Types::Alignment (declarativeimports/coretypes.h.in). The alignment
# axis rotates with the edge: on a horizontal edge the three non-justify
# positions are Left/Center/Right; on a vertical edge they are Top/Center/
# Bottom. The matrix labels every cell {left,center,right,justify}; for a
# vertical edge "left" means the begin end (Top) and "right" the far end
# (Bottom). Justify is orientation-independent.
ALIGN_ENUM_HORIZONTAL = {"left": 1, "center": 0, "right": 2, "justify": 10}
ALIGN_ENUM_VERTICAL = {"left": 3, "center": 0, "right": 4, "justify": 10}
# The string viewsData reports back for each enum value (app/dbusreports.h
# alignmentToString): this is what the harness's realization check compares to.
ALIGN_READBACK_HORIZONTAL = {"left": "left", "center": "center", "right": "right", "justify": "justify"}
ALIGN_READBACK_VERTICAL = {"left": "top", "center": "center", "right": "bottom", "justify": "justify"}

VIEW_TYPES = ("dock", "panel")
EDGES = ("top", "bottom", "left", "right")
ALIGNMENTS = ("left", "center", "right", "justify")
DISPLAYS = ("1out", "2out")

LATTE_CONTAINMENT_PLUGIN = "org.kde.latte.containment"


def die(msg):
    """Refuse loudly: name the boundary, print the offending input, exit 2."""
    print("fixture.py: REFUSED: " + msg, file=sys.stderr)
    sys.exit(2)


# --- KConfig INI parse/serialize -------------------------------------------
# KConfig groups are line-based: a "[Group][Sub]" header followed by key=value
# lines, blank-line separated. Nested groups ([Containments][1] vs
# [Containments][1][General]) are DISTINCT top-level entries keyed by their
# full header. This parser preserves group order and key order so a patched
# file diffs minimally against its seed, and only the keys we set change.

def parse_kconfig(text):
    groups = OrderedDict()  # header string -> OrderedDict(key -> value)
    preamble = []           # lines before the first header (rare; kept verbatim)
    current = None
    for raw in text.splitlines():
        line = raw.rstrip("\n")
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            current = stripped
            groups.setdefault(current, OrderedDict())
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
                die("unparseable config line (no '='): %r" % line)
            groups[current][key.strip()] = value
    return preamble, groups


def serialize_kconfig(preamble, groups):
    out = []
    out.extend(preamble)
    if preamble:
        out.append("")
    for header, kv in groups.items():
        out.append(header)
        for key, value in kv.items():
            out.append("%s=%s" % (key, value))
        out.append("")
    return "\n".join(out).rstrip("\n") + "\n"


def find_latte_containment(groups):
    """The single top-level containment group whose plugin is the Latte
    containment. Zero or several is a malformed seed - refuse."""
    matches = []
    for header, kv in groups.items():
        # top-level containment header is exactly [Containments][<id>]
        if header.startswith("[Containments][") and header.count("[") == 2:
            if kv.get("plugin") == LATTE_CONTAINMENT_PLUGIN:
                matches.append(header)
    if not matches:
        die("seed layout has no %s containment to parametrize" % LATTE_CONTAINMENT_PLUGIN)
    if len(matches) > 1:
        die("seed layout has %d Latte containments (%s); the matrix fixture needs exactly one"
            % (len(matches), ", ".join(matches)))
    return matches[0]


def general_group_header(containment_header):
    # [Containments][1] -> [Containments][1][General]
    return containment_header + "[General]"


LATTE_TASKS_PLUGIN = "org.kde.latte.plasmoid"


def find_tasks_applet(groups, containment_header):
    """The Latte tasks applet's own group under the containment, or None.
    An applet's own group is [Containments][<c>][Applets][<a>] (four brackets);
    its config sub-groups have more. Returns the header so the caller can reach
    [...][Configuration][General]."""
    prefix = containment_header + "[Applets]["
    for header, kv in groups.items():
        if header.startswith(prefix) and header.count("[") == 4:
            if kv.get("plugin") == LATTE_TASKS_PLUGIN:
                return header
    return None


def set_key(groups, header, key, value):
    groups.setdefault(header, OrderedDict())[key] = str(value)


# --- the parametrization ----------------------------------------------------

def patch_layout(text, view_type, edge, alignment, display, screen):
    preamble, groups = parse_kconfig(text)
    cont = find_latte_containment(groups)
    gen = general_group_header(cont)
    horizontal = edge in HORIZONTAL_EDGES

    # edge: Plasma location + form factor on the containment group
    set_key(groups, cont, "location", LOCATION[edge])
    set_key(groups, cont, "formfactor", FORMFACTOR[edge])

    # alignment: the axis-correct Latte enum in [General]
    align_map = ALIGN_ENUM_HORIZONTAL if horizontal else ALIGN_ENUM_VERTICAL
    set_key(groups, gen, "alignment", align_map[alignment])
    # the alignmentUpgraded flag must stay set or the dock re-derives alignment
    # from the deprecated panelPosition and overwrites ours on load
    set_key(groups, gen, "alignmentUpgraded", "true")

    # view type: viewsData.type is the QML-derived viewType. Drive the
    # derivation deterministically (see the module header).
    if view_type == "panel":
        set_key(groups, gen, "zoomLevel", 0)              # zero parabolic zoom...
        # ...but zoomLevel=0 alone is not enough: factor.maxZoom is the MAX of
        # the containment zoom and every applet's requested zoomFactor
        # (containment ParabolicEffect.qml). The Latte tasks applet requests
        # 1.65 whenever a high-thickness animation is on (plasmoid main.qml:893
        # / hasHighThicknessAnimation), and all three of those animations
        # default true - so maxZoom stays 1.65 and resolveViewTypeInQuestion
        # keeps returning Dock. A fixed-thickness panel cannot host those
        # bounce animations anyway, so turning them off is the Qt5-faithful
        # panel config, and it drops the requirement to 1.0 -> maxZoom == 1.0.
        tasks = find_tasks_applet(groups, cont)
        if tasks is not None:
            tgen = tasks + "[Configuration][General]"
            set_key(groups, tgen, "animationLauncherBouncing", "false")
            set_key(groups, tgen, "animationWindowInAttention", "false")
            set_key(groups, tgen, "animationWindowAddedInGroup", "false")
        set_key(groups, gen, "useThemePanel", "true")     # background is a theme panel
        set_key(groups, gen, "panelSize", 100)            # thick: visualThickness >= iconSize
        if alignment != "justify":
            # a non-justify panel needs a static length (min==max) to stop
            # sliding; a full-span panel pins both to 100%
            set_key(groups, gen, "minLength", 100)
            set_key(groups, gen, "maxLength", 100)
        # justify already stops the slide; leave its length free
        set_key(groups, cont, "viewType", 1)              # informational; keeps config self-consistent
    else:  # dock
        set_key(groups, gen, "zoomLevel", 16)             # zoom on forces Dock regardless of length
        set_key(groups, cont, "viewType", 0)

    # screen assignment
    if display == "1out":
        set_key(groups, cont, "onPrimary", "true")
        set_key(groups, cont, "screensGroup", 0)          # SingleScreenGroup
        set_key(groups, cont, "lastScreen", -1)           # primary / any
    else:  # 2out - secondary output. The name<->connector mapping under a
        # headless multi-output vehicle is P1's job (C-I2); P0 emits the
        # per-screen pin the settings combo writes so a dual cell is fully
        # declared, but the actual placement is only exercised once the
        # multi-output vehicle lands. Single-output runs must not trust it.
        set_key(groups, cont, "onPrimary", "false")
        set_key(groups, cont, "screensGroup", 0)
        if screen:
            # explicitScreen is the connector name the view pins to; lastScreen
            # is the numeric id ScreenPool resolves it to at runtime (P1).
            set_key(groups, cont, "explicitScreen", screen)

    # the readback strings the harness will assert the realized view against
    align_readback = (ALIGN_READBACK_HORIZONTAL if horizontal else ALIGN_READBACK_VERTICAL)[alignment]
    expect = {"type": view_type, "edge": edge, "alignment": align_readback}
    return serialize_kconfig(preamble, groups), expect


def find_seed_layout(seed_dir):
    latte_dir = os.path.join(seed_dir, "latte")
    if not os.path.isdir(latte_dir):
        die("seed dir %r has no latte/ subdir (not a seeded config home)" % seed_dir)
    layouts = [f for f in sorted(os.listdir(latte_dir))
               if f.endswith(".layout.latte") and not f.endswith(".bak")]
    if not layouts:
        die("seed dir %r carries no *.layout.latte" % seed_dir)
    if len(layouts) > 1:
        # single-layout mode expects one; pick the one lattedockrc names, else refuse
        named = None
        rc = os.path.join(seed_dir, "lattedockrc")
        if os.path.isfile(rc):
            with open(rc) as fh:
                for line in fh:
                    if line.startswith("singleModeLayoutName="):
                        named = line.split("=", 1)[1].strip() + ".layout.latte"
                        break
        if named and named in layouts:
            return os.path.join(latte_dir, named)
        die("seed dir %r has %d layouts and lattedockrc names none of them" % (seed_dir, len(layouts)))
    return os.path.join(latte_dir, layouts[0])


def validate_token(name, value, allowed):
    if value not in allowed:
        die("%s %r is not one of %s" % (name, value, "|".join(allowed)))


def main():
    ap = argparse.ArgumentParser(description="Generate a parametrized Latte matrix fixture config.")
    ap.add_argument("--seed-dir", required=True, help="a seeded config home to derive from (has latte/ + lattedockrc)")
    ap.add_argument("--out-dir", required=True, help="destination config home (overwritten)")
    ap.add_argument("--view-type", required=True)
    ap.add_argument("--edge", required=True)
    ap.add_argument("--alignment", required=True)
    ap.add_argument("--display", default="1out")
    ap.add_argument("--screen", default="", help="connector name for a 2out secondary pin (P1)")
    ap.add_argument("--cell", default="", help="cell id for the manifest (default: derived)")
    args = ap.parse_args()

    validate_token("view-type", args.view_type, VIEW_TYPES)
    validate_token("edge", args.edge, EDGES)
    validate_token("alignment", args.alignment, ALIGNMENTS)
    validate_token("display", args.display, DISPLAYS)

    cell = args.cell or "%s-%s-%s-%s" % (args.view_type, args.edge, args.alignment, args.display)

    if not os.path.isdir(args.seed_dir):
        die("seed dir %r does not exist" % args.seed_dir)

    seed_layout = find_seed_layout(args.seed_dir)
    with open(seed_layout) as fh:
        text = fh.read()
    # patch first (may refuse) BEFORE writing anything, so a refused fixture
    # leaves no half-written output dir behind
    patched, expect = patch_layout(text, args.view_type, args.edge, args.alignment, args.display, args.screen)

    # stage the whole seed dir, then overwrite the one layout we patched
    if os.path.exists(args.out_dir):
        shutil.rmtree(args.out_dir)
    shutil.copytree(args.seed_dir, args.out_dir,
                    ignore=shutil.ignore_patterns("*.bak"))
    out_layout = os.path.join(args.out_dir, "latte", os.path.basename(seed_layout))
    with open(out_layout, "w") as fh:
        fh.write(patched)

    manifest = {
        "cell": cell,
        "viewType": args.view_type,
        "edge": args.edge,
        "alignment": args.alignment,
        "display": args.display,
        "screen": args.screen,
        "layout": os.path.basename(seed_layout),
        "expect": expect,
    }
    with open(os.path.join(args.out_dir, "matrix-cell.json"), "w") as fh:
        json.dump(manifest, fh, indent=2)
        fh.write("\n")

    # stdout is the manifest so the shell harness can read expect.* without a
    # second file read
    print(json.dumps(manifest))


if __name__ == "__main__":
    main()
