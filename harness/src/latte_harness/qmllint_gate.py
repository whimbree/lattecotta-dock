# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The qmllint ratchet gate (BP-1d), the typed port of tests/coverage/qmllint-gate.sh.

Runs the PINNED Qt's qmllint over the same package QML the shell version scanned
(the shells/containment/plasmoid trees, the indicators, and the staged
org.kde.latte declarativeimports), counts five curated warning categories per
file, and compares the counts against tests/coverage/qmllint-baseline EXACTLY:
an increase is un-mergeable, an improvement lands with the baseline shrink in the
same commit (--write-baseline regenerates it). The curated categories are:

    unqualified                 unqualified access
    missing-type                untyped function signatures/parameters
    unresolved-type             unresolvable types
    deprecated                  deprecated syntax
    signal-handler-parameters   injected signal-handler parameters

Full-strict QML is the asymptotic state the extraction converges to
(strict-on-touch: files a cutover touches leave at zero), not a mandate on
inherited files. Files that cannot reach zero for a structural reason are named
with their reason in docs/tracking/QML_EXTRACTION_PLAN.md (section D); this file
is regenerated wholesale, so the durable record lives there.

D270 (--write-baseline emitted locale-dependent ordering) is RETIRED here: the
baseline is serialized in codepoint (C-collation) order by construction (Python's
default str sort), independent of the ambient locale, so a regeneration on an
unchanged tree reproduces the committed file byte-for-byte.

The staging and import assembly go through latte_harness.qmlenv (the BP-1a typed
module), not the bash bridge.
"""

from __future__ import annotations

import argparse
import difflib
import locale
import os
import re
import shutil
from collections import defaultdict
from collections.abc import Iterable, Iterator, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from pydantic import BaseModel, ConfigDict

from latte_harness.log import fail, info
from latte_harness.paths import RepoPaths
from latte_harness.proc import run
from latte_harness.qmlenv import (
    MissingModulePathError,
    assemble_imports,
    resolve_install_qmldir,
    stage_qml_modules,
    strip_packaged_latte_dock,
)

TOOL = "qmllint-gate"

# The five ratchet categories, by qmllint's stable --json id. A category is
# stable across the human-readable text format's rewordings, so counting cannot
# silently drift with a message-text change (the reason --json is the interface).
CURATED_CATEGORIES: frozenset[str] = frozenset(
    {"unqualified", "missing-type", "unresolved-type", "deprecated", "signal-handler-parameters"}
)

# The staged trees qml-compile-gate.sh and this gate both enumerate, relative to
# the stage prefix. lib/qml is the nixpkgs KDE_INSTALL_QMLDIR; the shell version
# hardcodes it here too (the gate refuses a qmllint outside /nix/store, so the
# stage is always nixpkgs-shaped and qmldir is always lib/qml on this path).
_SCAN_ROOTS: tuple[str, ...] = (
    "share/plasma/shells/org.kde.latte.shell",
    "share/plasma/plasmoids/org.kde.latte.containment",
    "share/plasma/plasmoids/org.kde.latte.plasmoid",
    "share/latte/indicators",
    "lib/qml/org/kde/latte",
)

# Skip classes, identical to qml-compile-gate.sh: org.kde.latte.private.app only
# registers inside the latte-dock binary, so its types can never resolve for a
# standalone tool; the superseded *.5.2[0-5].qml version-ladder rungs are dead on
# Plasma 6.
_APP_MODULE_MARKER = "org.kde.latte.private.app"
_VERSION_LADDER_RE = re.compile(r"\.5\.2[0-5]\.qml$")

# The nixpkgs Qt6 runtime seed vars the wrapped qmllint reads, re-exported with
# the packaged latte-dock leaf stripped (the D8/D271 import-path doctrine: an
# ambient foreign latte-dock leaf could otherwise resolve org.kde.latte types
# and move the counts). This pair mirrors qmlenv's _NIXPKGS_SEED_VARS;
# test_qmllint_gate locks it to qmlenv.build_setup_script so the two cannot
# drift.
SEED_VARS: tuple[str, ...] = ("NIXPKGS_QT6_QML_IMPORT_PATH", "NIXPKGS_QML_SEARCH_PATHS")

# The committed baseline's header, kept byte-identical so a --write-baseline on an
# unchanged tree reproduces the file exactly. The compare path strips comment and
# blank lines, so the text never affects a verdict; it is the human preamble.
BASELINE_HEADER = (
    "# Per-file curated qmllint warning counts (tests/coverage/qmllint-gate.sh).\n"
    "# Ratchet law: this file only ever shrinks. Regenerate with\n"
    "#   tests/coverage/qmllint-gate.sh --write-baseline\n"
    "# and commit the shrink together with the change that earned it.\n"
    "# Files that cannot reach zero for a structural reason are NAMED\n"
    "# with their reason in docs/tracking/QML_EXTRACTION_PLAN.md (section D,\n"
    "# step-2.5 additions) - this file is regenerated wholesale, so\n"
    "# the durable record lives there.\n"
)


# --- the qmllint --json boundary, validated with pydantic --------------------


class QmllintWarning(BaseModel):
    """One qmllint diagnostic. Extra fields (column, type, suggestions) ignored."""

    model_config = ConfigDict(extra="ignore")

    id: str
    message: str
    line: int


class QmllintFile(BaseModel):
    model_config = ConfigDict(extra="ignore")

    filename: str
    warnings: list[QmllintWarning] = []


class QmllintReport(BaseModel):
    model_config = ConfigDict(extra="ignore")

    files: list[QmllintFile] = []


# --- pure logic: parse, filter, count, serialize -----------------------------


def parse_report(json_text: str) -> QmllintReport:
    """Validate qmllint's --json output at the boundary (loud on a schema break)."""
    return QmllintReport.model_validate_json(json_text)


def _relativize(filename: str, stage_prefix: str) -> str:
    """Strip the stage prefix from a filename (jq's ``ltrimstr($stage)``)."""
    return filename[len(stage_prefix) :] if filename.startswith(stage_prefix) else filename


def _iter_curated(report: QmllintReport, stage_prefix: str) -> Iterator[tuple[str, QmllintWarning]]:
    """Every curated warning as (rel, warning), the stage prefix stripped."""
    for file in report.files:
        rel = _relativize(file.filename, stage_prefix)
        for w in file.warnings:
            if w.id in CURATED_CATEGORIES:
                yield rel, w


def curated_counts(report: QmllintReport, stage_prefix: str) -> dict[str, int]:
    """Per-file curated warning totals, files with no findings omitted."""
    counts: dict[str, int] = defaultdict(int)
    for rel, _w in _iter_curated(report, stage_prefix):
        counts[rel] += 1
    return dict(counts)


def _count_lines(counts: Mapping[str, int]) -> list[str]:
    """The ``<n>\\t<rel>`` lines, codepoint-sorted by rel (the D270 fix).

    Python's default str sort is codepoint order == C collation, independent of
    the ambient locale, which is exactly what the committed baseline uses. The
    shell version's ``sort -k2`` followed the ambient locale on write, so a
    regeneration reordered unchanged lines; sorting here by construction removes
    that source of dirty diffs.
    """
    return [f"{counts[rel]}\t{rel}" for rel in sorted(counts)]


def format_current(counts: Mapping[str, int]) -> str:
    """The newline-terminated body of curated counts (the shell's ``current``)."""
    lines = _count_lines(counts)
    return "".join(f"{line}\n" for line in lines)


def render_baseline(counts: Mapping[str, int]) -> str:
    """The full baseline file text: the fixed header plus the C-sorted body."""
    return BASELINE_HEADER + format_current(counts)


def parse_baseline(text: str) -> dict[str, int]:
    """The committed baseline's per-file counts, comments and blanks dropped.

    Mirrors ``grep -v '^#' | grep -v '^$'`` then the ``<n>\\t<rel>`` split.
    A malformed data line is a corrupt baseline, refused loudly by the caller.
    """
    counts: dict[str, int] = {}
    for raw in text.splitlines():
        if not raw or raw.startswith("#"):
            continue
        count_str, tab, rel = raw.partition("\t")
        if not tab or not rel or not count_str.isdigit():
            raise ValueError(f"malformed baseline line: {raw!r}")
        counts[rel] = int(count_str)
    return counts


# --- the gate context: staging, imports, and the child env -------------------


@dataclass(frozen=True, slots=True)
class GateContext:
    build: Path
    stage: Path
    qmldir: str
    imports: list[str]
    child_env: dict[str, str]


def prepare_child_env(env: Mapping[str, str]) -> dict[str, str]:
    """The env qmllint runs under: QML2 import paths dropped, seed vars stripped.

    Mirrors the env mutations qmlenv.build_setup_script emits for the bash
    consumers: unconditionally drop QML2_IMPORT_PATH / QML_IMPORT_PATH (explicit
    -import lists only, the D8/D271 doctrine) and re-export each non-empty
    nixpkgs seed var with the packaged latte-dock leaf stripped.
    """
    child = dict(env)
    child.pop("QML2_IMPORT_PATH", None)
    child.pop("QML_IMPORT_PATH", None)
    for var in SEED_VARS:
        current = child.get(var)
        if current:
            child[var] = strip_packaged_latte_dock(current)
    return child


def resolve_context(repo: Path, env: Mapping[str, str]) -> GateContext:
    """Build/stage/qmldir/imports/child-env, mirroring qmlenv.build_setup_script.

    The build/stage overrides and the module-path refusal match the bridge line
    for line; qmldir and the import list come straight from qmlenv library calls.
    """
    module_path = env.get("LATTE_QML_MODULE_PATH")
    if not module_path:
        raise MissingModulePathError
    build = Path(env["BUILD"]) if env.get("BUILD") else repo / "build"
    stage = Path(env["STAGE"]) if env.get("STAGE") else build / "_qmlstage"
    qmldir = resolve_install_qmldir(build)
    imports = assemble_imports(module_path, build, stage, qmldir)
    return GateContext(
        build=build,
        stage=stage,
        qmldir=qmldir,
        imports=imports,
        child_env=prepare_child_env(env),
    )


def import_flags(stage: Path, imports: Sequence[str]) -> list[str]:
    """qmllint -I flags: the staged tree first, then every assembled import dir.

    The assembled list is ``-import <dir>`` pairs; qmllint wants ``-I <dir>``, so
    the dirs are the odd indices. The leading ``stage/lib/qml`` mirrors the shell
    version (redundant with the staged import the list already ends in, harmless
    as a duplicate -I).
    """
    flags = ["-I", str(stage / "lib/qml")]
    for directory in imports[1::2]:
        flags += ["-I", directory]
    return flags


def _resolve_pinned_qmllint() -> str:
    """The qmllint on PATH, refused unless it resolves inside /nix/store."""
    lint = shutil.which("qmllint")
    if lint is None:
        fail(TOOL, "qmllint not found on PATH (devShell provides the pinned copy)", 2)
    if not lint.startswith("/nix/store/"):
        fail(TOOL, f"qmllint resolves outside the pinned closure: {lint}", 2)
    return lint


def sorted_like_find(paths: Iterable[Path]) -> list[Path]:
    """Order paths as ``find ... | sort`` does: full-path ambient-locale collation.

    This is deliberately locale-dependent, and it is the crux of the OPEN D269
    family. qmllint's unresolved-type resolution is sensitive to the order the
    files are presented in, and the shell gate fed them in coreutils ``sort``
    order, which follows LC_COLLATE. The committed baseline was written under
    this machine's ambient UTF-8 locale; measured on the current tree,
    TaskItem.qml counts 210 curated warnings in that order but 211 under C
    collation (codepoint), where one type stops resolving and an unresolved-type
    fires. The port reproduces the shell's locale order to stay byte-equivalent
    against the untouched baseline. It does NOT substitute a deterministic order:
    that would kill the fragility but only against a baseline regenerated under a
    pinned locale, which is the deferred real fix for D269 and outside this port.
    Python starts in the C locale regardless of the environment, so LC_COLLATE
    must be adopted explicitly for strxfrm to match coreutils sort.
    """
    locale.setlocale(locale.LC_COLLATE, "")
    return sorted(paths, key=lambda p: locale.strxfrm(str(p)))


def _scan_files(stage: Path) -> tuple[list[Path], int, int]:
    """Every scanned .qml under the stage roots, minus the two skip classes.

    Returns (kept, skipped_app, skipped_ladder). A total of zero .qml found is a
    broken/empty stage, refused by the caller.
    """
    found: list[Path] = []
    for root in _SCAN_ROOTS:
        base = stage / root
        if base.is_dir():
            found += base.rglob("*.qml")
    found = sorted_like_find(found)
    if not found:
        print(f"no staged QML found under {stage}", flush=True)
        raise SystemExit(2)

    kept: list[Path] = []
    skipped_app = 0
    skipped_ladder = 0
    for f in found:
        if _APP_MODULE_MARKER in f.read_text():
            skipped_app += 1
            continue
        if _VERSION_LADDER_RE.search(f.name):
            skipped_ladder += 1
            continue
        kept.append(f)
    return kept, skipped_app, skipped_ladder


def run_qmllint(ctx: GateContext, files: Sequence[Path]) -> QmllintReport:
    """Run the pinned qmllint over ``files`` and return the validated report.

    qmllint exits nonzero whenever any warning fires; the ratchet judges counts,
    so the exit code is ignored (the report file is the interface). Output goes to
    a file under the stage, matching the shell version's scratch location.
    """
    lint = _resolve_pinned_qmllint()
    out = ctx.stage / "_qmllint_gate.json"
    argv = [lint, *import_flags(ctx.stage, ctx.imports), "--json", str(out)]
    argv += [str(f) for f in files]
    run(argv, env=ctx.child_env, capture=True)  # exit code intentionally ignored
    return parse_report(out.read_text())


# --- verdict reporting -------------------------------------------------------


def _report_divergence(baseline: Mapping[str, int], current: Mapping[str, int]) -> None:
    """Print the FAIL diagnostics: the unified count diff and the ratchet hint."""
    print(
        f"{TOOL}: FAIL per-file curated warning counts diverge from "
        "tests/coverage/qmllint-baseline:",
        flush=True,
    )
    expected_lines = _count_lines(baseline)
    current_lines = _count_lines(current)
    for line in difflib.unified_diff(
        expected_lines, current_lines, fromfile="baseline", tofile="current", lineterm=""
    ):
        print(line, flush=True)

    print(f"{TOOL}: an increase is a regression to fix; an improvement lands", flush=True)
    print(f"{TOOL}: with the baseline shrink in the same commit (--write-baseline).", flush=True)


# --- the run --------------------------------------------------------------


def _gate(repo: Path, env: Mapping[str, str], *, write_baseline: bool) -> None:
    baseline_path = repo / "tests" / "coverage" / "qmllint-baseline"
    ctx = resolve_context(repo, env)
    stage_qml_modules(ctx.build, ctx.stage)

    files, skipped_app, skipped_ladder = _scan_files(ctx.stage)
    info(
        TOOL,
        f"{len(files)} files ({skipped_app} app-module-dependent + "
        f"{skipped_ladder} dead-version-ladder skipped)",
    )

    report = run_qmllint(ctx, files)
    stage_prefix = f"{ctx.stage}/"
    counts = curated_counts(report, stage_prefix)

    if write_baseline:
        baseline_path.write_text(render_baseline(counts))
        rel = baseline_path.relative_to(repo)
        info(TOOL, f"baseline written to {rel} ({len(counts)} files with findings)")
        return

    if not baseline_path.is_file():
        fail(
            TOOL,
            "no baseline at tests/coverage/qmllint-baseline (generate with --write-baseline)",
        )

    try:
        baseline = parse_baseline(baseline_path.read_text())
    except ValueError as exc:
        fail(TOOL, f"corrupt baseline at tests/coverage/qmllint-baseline: {exc}")

    if counts != baseline:
        _report_divergence(baseline, counts)
        raise SystemExit(1)

    total = sum(counts.values())
    info(
        TOOL,
        f"OK ({len(counts)} files with findings, {total} curated warnings, baseline matched)",
    )


def main(argv: Sequence[str] | None = None) -> None:
    parser = argparse.ArgumentParser(prog="latte_harness.qmllint_gate", description=__doc__)
    parser.add_argument(
        "--write-baseline",
        action="store_true",
        help="regenerate tests/coverage/qmllint-baseline from the current tree",
    )
    args = parser.parse_args(argv)
    write_baseline: bool = args.write_baseline

    repo = RepoPaths.discover().root
    try:
        _gate(repo, dict(os.environ), write_baseline=write_baseline)
    except MissingModulePathError:
        fail(
            TOOL,
            "LATTE_QML_MODULE_PATH is unset; run inside the flake devShell "
            "(nix develop provides it)",
        )


if __name__ == "__main__":
    main()
