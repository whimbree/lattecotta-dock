# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
"""Fast HERMETIC check of the matrix fixture generator (latte_harness.
matrix_fixture). This is the per-commit tier-1 hook for C-I1 (the interaction
matrix; O4: keep the merge gate fast, the full nested-vehicle suite periodic). It
needs no compositor and no built dock - it drives the generator (a pure config
transform) over a seed built from the shipped Default template, asserts the
parametrized keys land, and asserts every malformed descriptor is REFUSED with no
output. The full harness acceptance (fixture -> live dock -> readback, incl. the
HC3 reject-observing self-test) is scripts/run-matrix.sh, run periodically.

Ported from scripts/matrix-fixture-check.sh (BP-1b): the generator is driven as a
subprocess so this leg tests the REAL exit-code contract (0 ok, 2 refused) end to
end, exactly as the bash version did. THE EXIT CODE IS THE VERDICT (CLAUDE.md gate
discipline): run it, read the return, done.
"""

from __future__ import annotations

import re
import shutil
import sys
import tempfile
from pathlib import Path

from latte_harness.paths import RepoPaths
from latte_harness.proc import run

TOOL = "matrix-fixture-check"

TEMPLATE_REL = "shell/package/contents/templates/Default.layout.latte"

# The generator, driven as its real CLI. sys.executable is the venv interpreter
# under `uv run`, so the module imports cleanly in the subprocess.
GENERATOR_ARGV = [sys.executable, "-m", "latte_harness.matrix_fixture"]


class Checks:
    """Running tally of the landing/refusal assertions, mirroring the bash
    version's `  ok   [label]` / `  FAIL [label]` surface and its fail count."""

    def __init__(self) -> None:
        self.fails = 0

    def ok(self, label: str) -> None:
        print(f"  ok   [{label}]")

    def fail(self, label: str, detail: str) -> None:
        print(f"  FAIL [{label}]: {detail}", file=sys.stderr)
        self.fails += 1

    def key(self, label: str, layout: Path, pattern: str) -> None:
        """The layout must carry a line matching pattern (grep -qE '^...$')."""
        if re.search(pattern, layout.read_text(), re.MULTILINE):
            self.ok(label)
        else:
            self.fail(label, f"no line matching /{pattern}/ in {layout}")


def _gen(
    out: Path,
    view_type: str,
    edge: str,
    alignment: str,
    display: str = "1out",
    *extra: str,
    seed: Path,
) -> None:
    """Generate a cell into out; a nonzero exit here is a hard error (the bash
    version ran under set -e), so surface it and stop."""
    result = run(
        [
            *GENERATOR_ARGV,
            "--seed-dir",
            str(seed),
            "--out-dir",
            str(out),
            "--view-type",
            view_type,
            "--edge",
            edge,
            "--alignment",
            alignment,
            "--display",
            display,
            *extra,
        ],
        capture=True,
    )
    if result.returncode != 0:
        print(
            f"{TOOL}: FAIL generator exited {result.returncode} for a landing case:",
            file=sys.stderr,
        )
        print(result.stderr, file=sys.stderr)
        raise SystemExit(1)


def _layout_of(out: Path) -> Path:
    return out / "latte" / "My Layout.layout.latte"


def _assert_refused(checks: Checks, label: str, out: Path, seed: Path, args: list[str]) -> None:
    """The generator must exit 2 and leave no output dir for a malformed cell."""
    if out.exists():
        shutil.rmtree(out)
    result = run(
        [*GENERATOR_ARGV, "--seed-dir", str(seed), "--out-dir", str(out), *args],
        capture=True,
    )
    if result.returncode == 2 and not out.exists():
        print(f"  ok   [{label}] refused (exit 2, no output)")
    else:
        exists = "yes" if out.exists() else "no"
        print(f"  FAIL [{label}] exit={result.returncode} out-dir-exists={exists}", file=sys.stderr)
        checks.fails += 1


def _build_seed(seed: Path, template: Path) -> None:
    """A minimal seed from the shipped Default template (the same layout a
    first-run dock writes, so the generator patches a real, loadable base)."""
    (seed / "latte").mkdir(parents=True)
    _ = shutil.copy(template, seed / "latte" / "My Layout.layout.latte")
    _ = (seed / "lattedockrc").write_text(
        "[UniversalSettings]\nsingleModeLayoutName=My Layout\nmemoryUsage=0\n"
    )


def run_checks(work: Path, template: Path) -> int:
    checks = Checks()

    seed = work / "seed"
    _build_seed(seed, template)

    print(f"{TOOL}: DOCK top/left (horizontal axis)")
    _gen(work / "dtl", "dock", "top", "left", seed=seed)
    layout = _layout_of(work / "dtl")
    checks.key("location=TopEdge(3)", layout, r"^location=3$")
    checks.key("formfactor=Horizontal(2)", layout, r"^formfactor=2$")
    checks.key("alignment=Left(1)", layout, r"^alignment=1$")
    checks.key("zoomLevel on (dock)", layout, r"^zoomLevel=16$")
    checks.key("onPrimary=true (1out)", layout, r"^onPrimary=true$")
    checks.key("lastScreen=-1 (1out)", layout, r"^lastScreen=-1$")

    print(f"{TOOL}: DOCK left/right (vertical axis rotates alignment)")
    _gen(work / "dlr", "dock", "left", "right", seed=seed)
    layout = _layout_of(work / "dlr")
    checks.key("location=LeftEdge(5)", layout, r"^location=5$")
    checks.key("formfactor=Vertical(3)", layout, r"^formfactor=3$")
    checks.key("alignment=Bottom(4) [right->far end]", layout, r"^alignment=4$")

    print(f"{TOOL}: PANEL bottom/justify (zoom off + justify + thick bg + no bounce)")
    _gen(work / "pbj", "panel", "bottom", "justify", seed=seed)
    layout = _layout_of(work / "pbj")
    checks.key("zoomLevel=0", layout, r"^zoomLevel=0$")
    checks.key("alignment=Justify(10)", layout, r"^alignment=10$")
    checks.key("panelSize=100 (thick)", layout, r"^panelSize=100$")
    checks.key("useThemePanel=true", layout, r"^useThemePanel=true$")
    checks.key("tasks bounce anim off", layout, r"^animationLauncherBouncing=false$")

    print(f"{TOOL}: PANEL top/center (full-span static length)")
    _gen(work / "ptc", "panel", "top", "center", seed=seed)
    layout = _layout_of(work / "ptc")
    checks.key("minLength=100", layout, r"^minLength=100$")
    checks.key("maxLength=100", layout, r"^maxLength=100$")

    print(
        f"{TOOL}: 2out per-screen pin (real keys: onPrimary=false + "
        "lastScreen=<id> + ScreenConnectors)"
    )
    # a NAMED secondary pins by the pair the app reads (lastScreen + the
    # ScreenConnectors mapping that resolves it), NOT the dead explicitScreen key
    _gen(
        work / "d2",
        "dock",
        "bottom",
        "center",
        "2out",
        "--screen",
        "HDMI-A-2",
        "--screen-id",
        "11",
        "--screen-geometry",
        "1600,0 1600x1000",
        seed=seed,
    )
    layout = _layout_of(work / "d2")
    checks.key("onPrimary=false (2out)", layout, r"^onPrimary=false$")
    checks.key("lastScreen=11 (2out pins the numeric id)", layout, r"^lastScreen=11$")
    if re.search(r"^explicitScreen=", layout.read_text(), re.MULTILINE):
        print(
            "  FAIL [no dead explicitScreen key]: fixture still writes the no-op explicitScreen",
            file=sys.stderr,
        )
        checks.fails += 1
    else:
        print("  ok   [no dead explicitScreen key]")
    checks.key(
        "ScreenConnectors 11=HDMI-A-2",
        work / "d2" / "lattedockrc",
        r"^11=HDMI-A-2:::1600,0 1600x1000$",
    )

    print(f"{TOOL}: 2out with NO discovered secondary pins a sentinel (rejected, never mis-placed)")
    _gen(work / "d2s", "dock", "bottom", "center", "2out", seed=seed)
    layout = _layout_of(work / "d2s")
    checks.key("onPrimary=false (2out sentinel)", layout, r"^onPrimary=false$")
    checks.key("lastScreen=999 sentinel (no such output)", layout, r"^lastScreen=999$")
    if re.search(
        r"^\[ScreenConnectors\]", (work / "d2s" / "lattedockrc").read_text(), re.MULTILINE
    ):
        print(
            "  FAIL [sentinel seeds no mapping]: a sentinel 2out must NOT seed ScreenConnectors",
            file=sys.stderr,
        )
        checks.fails += 1
    else:
        print("  ok   [sentinel seeds no mapping]")

    print(f"{TOOL}: REFUSALS (each must exit 2, leave no output dir)")
    _assert_refused(
        checks,
        "bad edge",
        work / "r1",
        seed,
        ["--view-type", "dock", "--edge", "diagonal", "--alignment", "left", "--display", "1out"],
    )
    _assert_refused(
        checks,
        "bad alignment",
        work / "r2",
        seed,
        ["--view-type", "dock", "--edge", "top", "--alignment", "skew", "--display", "1out"],
    )
    _assert_refused(
        checks,
        "bad view-type",
        work / "r3",
        seed,
        ["--view-type", "slab", "--edge", "top", "--alignment", "left", "--display", "1out"],
    )
    _assert_refused(
        checks,
        "bad display",
        work / "r4",
        seed,
        ["--view-type", "dock", "--edge", "top", "--alignment", "left", "--display", "3out"],
    )

    # a seed with no Latte containment must be refused (the promised view is absent)
    badseed = work / "badseed"
    (badseed / "latte").mkdir(parents=True)
    _ = (badseed / "latte" / "x.layout.latte").write_text(
        "[Containments][1]\nplugin=org.kde.plasma.folder\n"
    )
    _ = (badseed / "lattedockrc").write_text("[UniversalSettings]\nsingleModeLayoutName=x\n")
    result = run(
        [
            *GENERATOR_ARGV,
            "--seed-dir",
            str(badseed),
            "--out-dir",
            str(work / "rb"),
            "--view-type",
            "dock",
            "--edge",
            "top",
            "--alignment",
            "left",
            "--display",
            "1out",
        ],
        capture=True,
    )
    if result.returncode == 2:
        print("  ok   [no-latte-containment] refused")
    else:
        print(f"  FAIL [no-latte-containment] exit={result.returncode}", file=sys.stderr)
        checks.fails += 1

    if checks.fails == 0:
        print(f"{TOOL}: PASS")
        return 0
    print(f"{TOOL}: FAIL ({checks.fails} check(s))")
    return 1


def main() -> None:
    paths = RepoPaths.discover()
    template = paths.root / TEMPLATE_REL
    if not template.is_file():
        print(f"{TOOL}: FAIL no Default template at {template}", file=sys.stderr)
        raise SystemExit(2)
    with tempfile.TemporaryDirectory(prefix="matrix-fixture-check.") as tmp:
        raise SystemExit(run_checks(Path(tmp), template))


if __name__ == "__main__":
    main()
