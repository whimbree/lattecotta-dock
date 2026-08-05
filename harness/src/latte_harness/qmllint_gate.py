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

Two defect dispositions ride in this port (docs/tracking/known-defects.md):

- D270 (--write-baseline emitted locale-dependent ordering) is RETIRED here: the
  baseline is serialized in codepoint (C-collation) order by construction
  (Python's default str sort), independent of the ambient locale, so a
  regeneration on an unchanged tree reproduces the committed file byte-for-byte.
- D269 (the curated count drifts under byte-identical inputs) stays OPEN as a
  family, but this port ships the diagnostic the registry asked for: every run
  persists a per-warning fingerprint artifact (file, category, line, a stable
  message digest) beside the stage, and on a count divergence the failure output
  names the per-file per-category breakdown and, when a prior fingerprint
  exists, the exact warnings that appeared or vanished since the last run. The
  committed baseline format does NOT change (counts stay the contract); the
  fingerprint is diagnostic only.

The staging and import assembly go through latte_harness.qmlenv (the BP-1a typed
module), not the bash bridge.
"""

from __future__ import annotations

import argparse
import difflib
import hashlib
import os
import re
import shutil
from collections import defaultdict
from collections.abc import Iterable, Iterator, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from pydantic import BaseModel, ConfigDict, ValidationError

from latte_harness.log import fail, info, warn
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

# The fingerprint artifact lives beside the stage but OUTSIDE it: the staging
# rsync runs with --delete on the stage dir, so a file under stage/ would be
# wiped before the next run could read it. build/_qmllint_fingerprint.json
# survives staging and lets one run diff against the previous run's warnings.
_FINGERPRINT_NAME = "_qmllint_fingerprint.json"


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


# --- the per-warning fingerprint (D269 diagnostic) ---------------------------


class FingerprintRecord(BaseModel):
    """One curated warning, identified stably across byte-identical runs."""

    file: str
    category: str
    line: int
    digest: str
    message: str

    @property
    def identity(self) -> tuple[str, str, int, str]:
        """The tuple that decides whether two runs saw the same warning."""
        return (self.file, self.category, self.line, self.digest)


class FingerprintArtifact(BaseModel):
    """The persisted set of curated warnings from one run."""

    warnings: list[FingerprintRecord] = []


def message_digest(message: str) -> str:
    """A short, stable digest of a warning's message text."""
    return hashlib.sha256(message.encode("utf-8")).hexdigest()[:12]


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
    """Per-file curated warning totals, files with no findings omitted.

    This is the verdict path: counts derive straight from the report, not from
    the D269 fingerprint layer, so the ratchet does not depend on the diagnostic.
    """
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


# --- the divergence diagnostics (D269) ---------------------------------------


def collect_fingerprints(report: QmllintReport, stage_prefix: str) -> list[FingerprintRecord]:
    """Every curated warning as a stable fingerprint, deterministically ordered.

    Sorted by (file, line, category, digest) so the persisted artifact is itself
    stable and diffable across byte-identical runs.
    """
    records = [
        FingerprintRecord(
            file=rel,
            category=w.id,
            line=w.line,
            digest=message_digest(w.message),
            message=w.message,
        )
        for rel, w in _iter_curated(report, stage_prefix)
    ]
    records.sort(key=lambda r: (r.file, r.line, r.category, r.digest))
    return records


def per_category_counts(report: QmllintReport, stage_prefix: str) -> dict[str, dict[str, int]]:
    """Per-file, per-category curated warning counts (the divergence breakdown)."""
    table: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))
    for rel, w in _iter_curated(report, stage_prefix):
        table[rel][w.id] += 1
    return {file: dict(cats) for file, cats in table.items()}


@dataclass(frozen=True, slots=True)
class FingerprintDelta:
    appeared: list[FingerprintRecord]
    vanished: list[FingerprintRecord]

    @property
    def empty(self) -> bool:
        return not self.appeared and not self.vanished


def fingerprint_delta(
    previous: Sequence[FingerprintRecord], current: Sequence[FingerprintRecord]
) -> FingerprintDelta:
    """The warnings that appeared or vanished between two runs, by identity."""
    prev_ids = {r.identity for r in previous}
    curr_ids = {r.identity for r in current}
    appeared = sorted(
        (r for r in current if r.identity not in prev_ids),
        key=lambda r: (r.file, r.line, r.category),
    )
    vanished = sorted(
        (r for r in previous if r.identity not in curr_ids),
        key=lambda r: (r.file, r.line, r.category),
    )
    return FingerprintDelta(appeared=appeared, vanished=vanished)


def diverging_files(baseline: Mapping[str, int], current: Mapping[str, int]) -> list[str]:
    """Every rel whose count differs (present-in-one or count-mismatch), sorted."""
    rels = set(baseline) | set(current)
    return sorted(rel for rel in rels if baseline.get(rel, 0) != current.get(rel, 0))


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
    """Order paths deterministically: full-path codepoint (C-collation) sort.

    qmllint's unresolved-type resolution is sensitive to the order files are
    presented in (the D269 drift family: the shell gate fed files in
    coreutils ``sort`` order, which follows the ambient LC_COLLATE, so the
    curated count changed with the invoking shell's locale - TaskItem.qml
    measured 210 under this machine's UTF-8 collation but 211 under C
    collation, one unresolved-type firing only in the latter order). The
    input order is now pinned to codepoint sort, locale-independent by
    construction, and the committed baseline was regenerated under this
    order in the same commit. "Simplifying" this back to a locale-aware
    sort reintroduces a verdict that changes with the invoking shell.
    """
    return sorted(paths, key=str)


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


# --- fingerprint artifact I/O ------------------------------------------------


def load_previous_fingerprint(path: Path) -> list[FingerprintRecord] | None:
    """The prior run's fingerprints, or None when absent or unreadable.

    A corrupt or old-format artifact is diagnostic-only, never the verdict: warn
    and proceed without the cross-run diff rather than failing the gate on it.
    """
    if not path.is_file():
        return None
    try:
        return FingerprintArtifact.model_validate_json(path.read_text()).warnings
    except (OSError, ValidationError) as exc:
        warn(TOOL, f"ignoring unreadable fingerprint artifact {path}: {exc}")
        return None


def write_fingerprint(path: Path, records: Sequence[FingerprintRecord]) -> None:
    """Persist this run's fingerprints for the next run to diff against."""
    path.write_text(FingerprintArtifact(warnings=list(records)).model_dump_json(indent=2) + "\n")


# --- verdict reporting -------------------------------------------------------


def _report_divergence(
    baseline: Mapping[str, int],
    current: Mapping[str, int],
    per_category: Mapping[str, dict[str, int]],
    delta: FingerprintDelta | None,
) -> None:
    """Print the FAIL diagnostics: the count diff, per-category breakdown, delta."""
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

    print(f"{TOOL}: per-file curated category breakdown for the diverging files:", flush=True)
    for rel in diverging_files(baseline, current):
        want = baseline.get(rel, 0)
        have = current.get(rel, 0)
        cats = per_category.get(rel, {})
        breakdown = ", ".join(f"{cat}={cats[cat]}" for cat in sorted(cats)) or "(none)"
        line = f"  {rel}: baseline {want} -> current {have} ({have - want:+d}); {breakdown}"
        print(line, flush=True)

    if delta is None:
        print(
            f"{TOOL}: no prior fingerprint to diff; the artifact from this run is "
            "written for the next one to name the exact warning that moves",
            flush=True,
        )
    elif delta.empty:
        print(f"{TOOL}: no per-warning change since the last run's fingerprint", flush=True)
    else:
        if delta.appeared:
            print(f"{TOOL}: curated warnings that appeared since the last run:", flush=True)
            for r in delta.appeared:
                print(f"  {r.file}:{r.line} [{r.category}] {r.message}", flush=True)
        if delta.vanished:
            print(f"{TOOL}: curated warnings that vanished since the last run:", flush=True)
            for r in delta.vanished:
                print(f"  {r.file}:{r.line} [{r.category}] {r.message}", flush=True)

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

    # D269 diagnostic: read the prior fingerprint BEFORE overwriting it, so a
    # divergence can name the warning that moved. The artifact lives outside the
    # stage (the staging --delete would otherwise wipe it before this read).
    fingerprints = collect_fingerprints(report, stage_prefix)
    fingerprint_path = ctx.build / _FINGERPRINT_NAME
    previous = load_previous_fingerprint(fingerprint_path)
    write_fingerprint(fingerprint_path, fingerprints)

    if counts != baseline:
        delta = fingerprint_delta(previous, fingerprints) if previous is not None else None
        _report_divergence(baseline, counts, per_category_counts(report, stage_prefix), delta)
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
