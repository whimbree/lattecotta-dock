# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""End-to-end recipe driver: the typed port of scripts/run-e2e.sh (BP-2b).

Discovers the e2e recipes, parses their ``# e2e-mode`` / ``# e2e-expect``
markers, brings up the nested vehicle (or the live session), runs each recipe
as an OPAQUE EXECUTABLE against the documented environment, classifies the
result (PASS / FAIL / XFAIL / XPASS / SKIP), captures failure artifacts, prints
the summary line, and returns the exit-code verdict. It owns the driver logic
only; the nested compositor lifecycle lives in latte_harness.vehicle and the
per-recipe dock lifecycle stays in tests/e2e/lib.sh (BP-2c ports that), which
this driver shares by sourcing it in a bash subprocess exactly as the bash
run-e2e.sh sourced it - the same single implementation recipes use.

Recipes are OPAQUE EXECUTABLES given a documented environment, which is what
lets .sh and .py recipes run side by side during the BP-3 transition: the
driver never reads a recipe's body except for its two marker lines.

DISCOVERY DEVIATION (deliberate, recorded). The bash run-e2e.sh discovered
recipes with a RECURSIVE ``find "$repo/tests/e2e" -name '*.sh' ! -name
'lib.sh'``, which descended into subdirectories and ran non-recipe files as
recipes: the driver libraries and drivers under tests/e2e/matrix/ (matrix-lib,
dnd-lib, golden-bridge, task-reorder-lib, multi-output-lib,
applet-reorder-driver), tests/e2e/audit/audit-lib.sh, and the launched-app
fixtures tests/e2e/fixtures/sc-w1/{launcher,rate-launcher}.sh - nine files that
are sourced or exec'd by real recipes, never recipes themselves. The BP-2a A/B
measurement of the recursive discovery (33/52 in the plan; the recursive find
matches 62 ``*.sh`` files on the current tree, 9 of them those non-recipes)
recorded those as guaranteed failures: the sourced-only libs error or the
non-executable ones trip the "missing or non-executable recipe" refusal, so a
green suite was impossible while they sat in the run.

This port discovers RECIPES ONLY: a file is a recipe when it sits DIRECTLY
under tests/e2e/ (not a subdirectory) and is not lib.sh; tests/e2e/*.py files
are discovered the same way (the BP-3 transition property, .sh and .py side by
side). The nine subdirectory files leave the denominator entirely because they
were never recipes. Explicit positional invocation still resolves a bare name
(with or without a .sh/.py extension) to tests/e2e/<name>.sh then
tests/e2e/<name>.py - the front doors and the asan gate pass explicit names.

The classification matrix and its self-test port BIT-IDENTICALLY from the bash
(the ``# e2e-expect`` contract in docs/reference/TESTING.md): the
``--self-test-expectations`` entry the bash exposed runs the same self-test,
and every cell is pinned as a unit test.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from collections.abc import Sequence
from contextlib import suppress
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import assert_never

from latte_harness import vehicle
from latte_harness.paths import RepoPaths
from latte_harness.proc import install_conventional_signal_exits

TOOL = "run-e2e"

# Both the startup self-test guard and the precondition refusals exit 2, the
# bash run-e2e's distinguished "cannot even start" code (a recipe failure is 1).
EXIT_PRECONDITION = 2

# The vehicle geometry the bash start_vehicle hard-coded: one 1600x1000 virtual
# output on this socket (E2E_OUTPUT_COUNT>1 asks the multi-output front door for
# more; the single-output default keeps every existing recipe unchanged).
VEHICLE_W = 1600
VEHICLE_H = 1000
VEHICLE_SOCKET = "latte-e2e-wl"

# The dock lifecycle helpers still live in tests/e2e/lib.sh (BP-2c); the bash
# run-e2e sourced it, so the timeouts it passed are preserved here.
DOCK_START_TIMEOUT = 90
LOG_TAIL_LINES = 30


# ---- expectation model -----------------------------------------------------
#
# The bash carried the expectation as a bare string ("" | "fail" | "status N").
# Modelling it as a closed union makes the invalid state unrepresentable and
# lets classify() dispatch with a compiler-checked match (the step-2.5 law).


@dataclass(frozen=True, slots=True)
class Unmarked:
    """No ``# e2e-expect`` marker: a nonzero exit is a real failure."""


@dataclass(frozen=True, slots=True)
class ExpectFail:
    """``# e2e-expect: fail``: any nonzero exit is the known-open bug (XFAIL)."""


@dataclass(frozen=True, slots=True)
class ExpectStatus:
    """``# e2e-expect: status N``: only exit N proves the known signature."""

    status: int


Expectation = Unmarked | ExpectFail | ExpectStatus


@dataclass(frozen=True, slots=True)
class MarkerError:
    """A malformed/blank/duplicate/out-of-range marker: refuse before running."""

    message: str


class RecipeResult(Enum):
    PASS = "PASS"
    FAIL = "FAIL"
    XFAIL = "XFAIL"
    XPASS = "XPASS"
    SKIP = "SKIP"


# ---- marker parsing (bit-identical to the bash, unit-tested directly) -------

# The value-form of `# e2e-expect: status N`: N is 1..999 by shape, then bounded
# to <= 255 (a Unix exit code) below - the bash `^status ([1-9][0-9]{0,2})$`.
_STATUS_RE = re.compile(r"^status ([1-9][0-9]{0,2})$")

# sed `^[[:space:]]*#[[:space:]]*e2e-expect`: a comment marker with optional
# leading and post-hash whitespace. Kept to the ASCII space class the bash used
# (no \n inside a splitlines() line).
_WS = r"[ \t\r\f\v]"
_EXPECT_MARKER_RE = re.compile(_WS + r"*#" + _WS + r"*e2e-expect")

# The strict prefix the bash required with a literal glob check; a marker that
# the loose scan matched but that does not start with exactly this is malformed.
_EXPECT_PREFIX = "# e2e-expect:"

# sed `s/^# e2e-mode: *//p`: an exact "# e2e-mode:" prefix then optional spaces.
_MODE_PREFIX = "# e2e-mode:"


def parse_expectation_value(value: str) -> Expectation | None:
    """The bash valid_recipe_expectation, returning the parsed form or None.

    None means invalid. The empty string maps to Unmarked to keep this a total
    function (valid_recipe_expectation accepted ""), though extract handles the
    no-marker and blank-marker cases before ever calling this with an empty
    value.
    """
    if value == "":
        return Unmarked()
    if value == "fail":
        return ExpectFail()
    match = _STATUS_RE.match(value)
    if match is not None:
        status = int(match.group(1))
        if status <= 255:
            return ExpectStatus(status)
    return None


def extract_recipe_expectation(text: str) -> Expectation | MarkerError:
    """The bash extract_recipe_expectation over a recipe's full text.

    Refuses (returns MarkerError) on a duplicate/conflicting, malformed, blank,
    or out-of-range marker BEFORE the recipe would run - the strictness the
    docs/reference/TESTING.md contract records. No marker returns Unmarked.
    """
    declarations = [line for line in text.splitlines() if _EXPECT_MARKER_RE.match(line)]
    if not declarations:
        return Unmarked()
    if len(declarations) != 1:
        return MarkerError(
            f"found {len(declarations)} e2e-expect declarations; use exactly one nonempty marker"
        )
    declaration = declarations[0]
    if not declaration.startswith(_EXPECT_PREFIX):
        return MarkerError(
            f"malformed e2e-expect declaration '{declaration}'; "
            "expected '# e2e-expect: fail' or '# e2e-expect: status N'"
        )
    value = declaration[len(_EXPECT_PREFIX) :].lstrip(" \t\r\f\v")
    if value == "":
        return MarkerError("blank e2e-expect declaration; remove it for unmarked behavior")
    parsed = parse_expectation_value(value)
    if parsed is None:
        return MarkerError(
            f"invalid e2e-expect value '{value}'; allowed values are fail or status 1..255"
        )
    return parsed


def recipe_mode(text: str) -> str:
    """The ``# e2e-mode`` constraint, or '' when unmarked (the bash sed | head -1).

    Only spaces are stripped after the colon, matching the sed ` *`; the first
    matching line wins.
    """
    for line in text.splitlines():
        if line.startswith(_MODE_PREFIX):
            return line[len(_MODE_PREFIX) :].lstrip(" ")
    return ""


def classify_recipe_result(exp: Expectation, status: int, name: str) -> tuple[RecipeResult, str]:
    """The bash classify_recipe_result: (result, the exact message it printed)."""
    match exp:
        case Unmarked():
            if status == 0:
                return RecipeResult.PASS, f"{TOOL}: PASS {name}"
            return RecipeResult.FAIL, f"{TOOL}: FAIL {name}"
        case ExpectFail():
            if status == 0:
                return RecipeResult.XPASS, (
                    f"{TOOL}: XPASS {name} (expected to fail but passed - "
                    "remove '# e2e-expect: fail', the guarded condition is fixed)"
                )
            return RecipeResult.XFAIL, (
                f"{TOOL}: XFAIL {name} (expected failure of a known-open bug, not counted)"
            )
        case ExpectStatus(status=expected):
            if status == 0:
                return RecipeResult.XPASS, (
                    f"{TOOL}: XPASS {name} (expected reserved status {expected} but passed - "
                    "the guarded condition is fixed)"
                )
            if status == expected:
                return RecipeResult.XFAIL, (
                    f"{TOOL}: XFAIL {name} (matched reserved status {expected} "
                    "for the known-open bug)"
                )
            return RecipeResult.FAIL, (
                f"{TOOL}: FAIL {name} (expected reserved status {expected}, got {status}; "
                "failure is outside the known signature)"
            )
        case _:
            assert_never(exp)


def _expectation_text(exp: Expectation) -> str:
    """The bash recipe_expectation string, used only in self-test diagnostics."""
    match exp:
        case Unmarked():
            return ""
        case ExpectFail():
            return "fail"
        case ExpectStatus(status=status):
            return f"status {status}"
        case _:
            assert_never(exp)


# ---- run counters ----------------------------------------------------------


@dataclass(slots=True)
class Counters:
    failed: int = 0
    skipped: int = 0
    ran: int = 0
    passed: int = 0
    xfailed: int = 0

    def record(self, result: RecipeResult) -> None:
        """The bash record_recipe_result tally (SKIP is counted at its site)."""
        match result:
            case RecipeResult.PASS:
                self.passed += 1
            case RecipeResult.XFAIL:
                self.xfailed += 1
            case RecipeResult.FAIL | RecipeResult.XPASS:
                self.failed += 1
            case RecipeResult.SKIP:
                self.skipped += 1


# ---- discovery -------------------------------------------------------------


def discover_recipes(tests_e2e: Path, names: Sequence[str]) -> list[Path]:
    """Explicit names, else every top-level recipe (the DISCOVERY DEVIATION).

    Top-level ``*.sh`` (never lib.sh) plus top-level ``*.py``, sorted; NEVER a
    subdirectory (that recursion ran non-recipe libs). Explicit names resolve
    through resolve_recipe.
    """
    if names:
        return [resolve_recipe(tests_e2e, name) for name in names]
    recipes = [p for p in tests_e2e.glob("*.sh") if p.name != "lib.sh"]
    recipes += list(tests_e2e.glob("*.py"))
    return sorted(recipes)


def resolve_recipe(tests_e2e: Path, name: str) -> Path:
    """A bare or extensioned name to its recipe path, resolving .sh then .py.

    The bash resolved ``${name%.sh}.sh``; this also accepts a .py name and
    prefers an existing .sh over .py. A name matching neither resolves to the
    .sh path so the loop's "missing or non-executable recipe" refusal fires with
    a sensible name (the bash behavior).
    """
    stem = name
    for ext in (".sh", ".py"):
        if stem.endswith(ext):
            stem = stem[: -len(ext)]
            break
    sh = tests_e2e / f"{stem}.sh"
    if sh.exists():
        return sh
    py = tests_e2e / f"{stem}.py"
    if py.exists():
        return py
    return sh


# ---- the classifier self-test (the bash run_expectation_selftest) ----------


def run_expectation_selftest(quiet: bool = False) -> int:
    """Drive every classification and marker cell; 0 on success, 1 on any miss.

    A faithful port of the bash run_expectation_selftest: the same cells, the
    same accumulator check (1 pass / 4 fail / 2 xfail), and the same
    capture-status probes. ``quiet`` hides the stdout progress like the bash
    ``run_expectation_selftest >/dev/null`` startup guard; failures still print
    to stderr. Unit tests pin every cell independently; this is the runtime
    ``--self-test-expectations`` surface the bash exposed.
    """
    failures = 0
    counters = Counters()

    def out(msg: str) -> None:
        if not quiet:
            print(msg, flush=True)

    def note_fail(msg: str) -> None:
        nonlocal failures
        print(msg, file=sys.stderr, flush=True)
        failures += 1

    def check_result(label: str, exp: Expectation, status: int, expected: RecipeResult) -> None:
        result, _ = classify_recipe_result(exp, status, "selftest")
        if result is expected:
            out(f"  ok   {label} -> {expected.value}")
        else:
            note_fail(f"  FAIL {label} -> {result.value}, expected {expected.value}")
        counters.record(result)

    def check_marker(
        label: str, text: str, expected: Expectation | None, error_fragment: str
    ) -> None:
        parsed = extract_recipe_expectation(text)
        if not isinstance(parsed, MarkerError):
            if error_fragment or parsed != expected:
                note_fail(f"  FAIL {label} marker accepted as '{_expectation_text(parsed)}'")
        elif not error_fragment or error_fragment not in parsed.message:
            note_fail(f"  FAIL {label} marker error: {parsed.message}")

    check_result("pass", Unmarked(), 0, RecipeResult.PASS)
    check_result("fail", Unmarked(), 1, RecipeResult.FAIL)
    check_result("legacy-xfail", ExpectFail(), 1, RecipeResult.XFAIL)
    check_result("legacy-xpass", ExpectFail(), 0, RecipeResult.XPASS)
    check_result("exact-xfail", ExpectStatus(42), 42, RecipeResult.XFAIL)
    check_result("exact-xpass", ExpectStatus(42), 0, RecipeResult.XPASS)
    check_result("status-mismatch", ExpectStatus(42), 1, RecipeResult.FAIL)
    if (counters.passed, counters.failed, counters.xfailed) != (1, 4, 2):
        failures += 1

    dup = "2 e2e-expect declarations"
    check_marker("no-marker", "#!/usr/bin/env bash\n", Unmarked(), "")
    check_marker("legacy", "# e2e-expect: fail\n", ExpectFail(), "")
    check_marker("exact", "# e2e-expect: status 42\n", ExpectStatus(42), "")
    check_marker("blank", "# e2e-expect:   \n", None, "blank")
    check_marker("duplicate", "# e2e-expect: fail\n# e2e-expect: fail\n", None, dup)
    check_marker("conflict", "# e2e-expect: fail\n# e2e-expect: status 42\n", None, dup)
    check_marker("malformed", "# e2e-expect status 42\n", None, "malformed")
    check_marker("unknown", "# e2e-expect: unknown\n", None, "invalid")
    check_marker("zero", "# e2e-expect: status 0\n", None, "invalid")
    check_marker("range", "# e2e-expect: status 256\n", None, "invalid")

    if subprocess.run(["bash", "-c", "exit 42"], check=False).returncode != 42:
        failures += 1
    if subprocess.run(["bash", "-c", "exit 0"], check=False).returncode != 0:
        failures += 1

    if failures != 0:
        return 1
    out(f"{TOOL}: PASS expectation classifier self-test")
    return 0


# ---- environment contract --------------------------------------------------


def _env_default(key: str, fallback: str) -> str:
    """The bash ``${VAR:-fallback}``: an unset OR empty value takes the fallback."""
    return os.environ.get(key) or fallback


def _export_common_env(repo: Path, mode: str) -> None:
    """Set E2E_REPO/BUILD/MODE/FAKEPOINTER/ARTIFACTS, byte-for-byte (both modes)."""
    os.environ["E2E_REPO"] = str(repo)
    build = _env_default("BUILD", str(repo / "build"))
    os.environ["E2E_BUILD"] = build
    os.environ["E2E_MODE"] = mode
    os.environ["E2E_FAKEPOINTER"] = _env_default(
        "E2E_FAKEPOINTER", str(Path(os.environ["HOME"]) / ".local" / "bin" / "fakepointer")
    )
    artifacts = _env_default("E2E_ARTIFACTS", f"{build}/_e2e-artifacts")
    os.environ["E2E_ARTIFACTS"] = artifacts
    Path(artifacts).mkdir(parents=True, exist_ok=True)


def _check_preconditions() -> int | None:
    """The bash fakepointer/binary refusals; return EXIT_PRECONDITION or None."""
    fakepointer = Path(os.environ["E2E_FAKEPOINTER"])
    if not os.access(fakepointer, os.X_OK):
        print(
            f"{TOOL}: FAIL fakepointer not found at {fakepointer} "
            "(build recipe: scripts/tools/fakepointer.c header)",
            flush=True,
        )
        return EXIT_PRECONDITION
    binary = Path(os.environ["E2E_BUILD"]) / "bin" / "latte-dock"
    if not os.access(binary, os.X_OK):
        print(f"{TOOL}: FAIL no built binary at {binary} (build first)", flush=True)
        return EXIT_PRECONDITION
    return None


# ---- dock lifecycle (shared with recipes via tests/e2e/lib.sh) -------------


def _lib_sh_call(func: str, *args: str) -> int:
    """Run a tests/e2e/lib.sh dock-lifecycle function in a bash subprocess.

    The bash run-e2e sourced lib.sh into its own shell; until BP-2c ports lib.sh
    the runner shares that ONE implementation the same way, so e2e_dock_start's
    setsid-detach and the settle waits behave identically. The recipe reads the
    exported E2E_* contract from the inherited environment. The detached dock
    survives this helper's exit (setsid), exactly as it survived the bash shell.
    """
    script = f'source "$E2E_REPO/tests/e2e/lib.sh"; {func} "$@"'
    completed = subprocess.run(["bash", "-c", script, "bash", *args], env=os.environ, check=False)
    return completed.returncode


def _dock_pid() -> int | None:
    """The recorded vehicle-dock pid (the bash e2e_dock_pid = cat pidfile)."""
    pidfile = os.environ.get("E2E_DOCK_PIDFILE")
    if not pidfile:
        return None
    try:
        text = Path(pidfile).read_text().strip()
    except OSError:
        return None
    return int(text) if text.isdigit() else None


def _pid_alive(pid: int) -> bool:
    """The bash ``kill -0``: alive iff a signal could be delivered."""
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def _print_log_tail(log: Path, lines: int) -> None:
    """The bash ``tail -N`` (to stdout) of a log that may be absent."""
    if not log.is_file():
        return
    for line in log.read_text(errors="replace").splitlines()[-lines:]:
        print(line, flush=True)


# ---- nested vehicle driver -------------------------------------------------


def _export_nested_session_env(runtime_dir: Path, session: vehicle.VehicleSession) -> None:
    """Move the driver (and every child) onto the vehicle's ambient environment.

    The bash start_vehicle exports these after nested_kwin_start: the private
    runtime dir, socket and bus become the ambient ones so plain busctl /
    fakepointer in recipes hit the vehicle, and the real X server is unset.
    """
    os.environ["E2E_RT"] = str(runtime_dir)
    os.environ["E2E_KWIN_LOG"] = str(session.log)
    os.environ["E2E_DOCK_LOG"] = str(runtime_dir / "dock.log")
    os.environ["E2E_DOCK_PIDFILE"] = str(runtime_dir / "dock.pid")
    os.environ["E2E_SCREEN_W"] = str(VEHICLE_W)
    os.environ["E2E_SCREEN_H"] = str(VEHICLE_H)
    os.environ["XDG_RUNTIME_DIR"] = str(runtime_dir)
    os.environ["WAYLAND_DISPLAY"] = session.socket
    os.environ["DBUS_SESSION_BUS_ADDRESS"] = session.bus
    os.environ.pop("DISPLAY", None)
    os.environ.pop("XAUTHORITY", None)


def _prepare_throwaway_config(repo: Path) -> str | None:
    """Copy a fresh throwaway config and pick E2E_LAYOUT; a FAIL message or None.

    A fresh copy per run (never the base itself), the .bak files dropped, and
    the layout chosen the bash way: the sole layout, or the singleModeLayoutName
    one when several exist. A base dir without a latte/ tree refuses loudly.
    """
    build = Path(os.environ["E2E_BUILD"])
    base = Path(_env_default("E2E_CONFIG_BASE", str(build / "_runconfig")))
    if not (base / "latte").is_dir():
        return (
            f"{TOOL}: FAIL no staged config at {base} "
            "(start the staged dock once, or set E2E_CONFIG_BASE)"
        )
    config_home = Path(os.environ["E2E_RT"]) / "latte-config"
    os.environ["E2E_CONFIG_HOME"] = str(config_home)
    shutil.copytree(base, config_home)
    for bak in (config_home / "latte").glob("*.bak"):
        bak.unlink()

    layouts = sorted((config_home / "latte").glob("*.layout.latte"))
    if not layouts:
        # E2E_LAYOUT drives every config-mutating recipe (kwriteconfig6 --file);
        # a base whose latte/ carries no layout is a broken seed, not something
        # to paper over with a bogus path (the failures-and-root-cause rule).
        return f"{TOOL}: FAIL no layout in {config_home}/latte (broken seed config base {base})"
    layout = layouts[0]
    if len(layouts) > 1:
        name = _single_mode_layout_name(config_home / "lattedockrc")
        candidate = config_home / "latte" / f"{name}.layout.latte"
        if name and candidate.is_file():
            layout = candidate
    os.environ["E2E_LAYOUT"] = str(layout)
    return None


def _single_mode_layout_name(lattedockrc: Path) -> str:
    """The bash ``sed -n 's/^singleModeLayoutName=//p' | head -1``."""
    try:
        for line in lattedockrc.read_text().splitlines():
            if line.startswith("singleModeLayoutName="):
                return line[len("singleModeLayoutName=") :]
    except OSError:
        return ""
    return ""


def _drive_nested(repo: Path, recipes: list[Path]) -> int:
    """Bring up the vehicle, run the recipes, tear down; the bash nested path.

    The running_compositor context manager is the bash ``trap
    nested_kwin_cleanup EXIT INT TERM``: the compositor and its runtime dir are
    reclaimed on every exit path (normal, precondition refusal, a signal turned
    into SystemExit). The summary prints before that teardown, matching the bash
    order (echo, then the EXIT trap fires).
    """
    runtime_dir = vehicle.prepare_runtime_dir()
    (runtime_dir / "kwin-config").mkdir(parents=True, exist_ok=True)
    (runtime_dir / "kwin-cache").mkdir(parents=True, exist_ok=True)
    outputs = int(_env_default("E2E_OUTPUT_COUNT", "1"))
    os.environ["E2E_OUTPUT_COUNT"] = str(outputs)
    extra_env = [
        f"WAYLAND_DISPLAY={VEHICLE_SOCKET}",
        f"XDG_CONFIG_HOME={runtime_dir / 'kwin-config'}",
        f"XDG_CACHE_HOME={runtime_dir / 'kwin-cache'}",
        "KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1",
        "QT_FORCE_STDERR_LOGGING=1",
    ]
    try:
        with vehicle.running_compositor(
            runtime_dir, VEHICLE_W, VEHICLE_H, VEHICLE_SOCKET, outputs, extra_env, os.environ
        ) as session:
            _export_nested_session_env(runtime_dir, session)
            config_error = _prepare_throwaway_config(repo)
            if config_error is not None:
                print(config_error, flush=True)
                return EXIT_PRECONDITION
            print(f"{TOOL}: vehicle up (rt {runtime_dir}), starting the staged dock...", flush=True)
            if _lib_sh_call("e2e_dock_start", str(DOCK_START_TIMEOUT)) != 0:
                print(
                    f"{TOOL}: FAIL the dock never settled in the vehicle; dock log tail:",
                    flush=True,
                )
                _print_log_tail(Path(os.environ["E2E_DOCK_LOG"]), LOG_TAIL_LINES)
                return EXIT_PRECONDITION
            counters = _execute_recipes(recipes, "nested")
            _teardown_nested_dock(counters)
            _print_summary(counters)
            return 0 if counters.failed == 0 else 1
    except vehicle.CompositorStartError as err:
        # running_compositor already tore the compositor down in its finally;
        # report the socket timeout the way the bash nested_kwin_start did (its
        # log to stderr) and exit 2 (bash `nested_kwin_start || exit 2`).
        print(
            f"{vehicle.TOOL}: nested kwin_wayland never brought up its socket; its log:",
            file=sys.stderr,
            flush=True,
        )
        sys.stderr.write(err.log_text)
        sys.stderr.flush()
        return EXIT_PRECONDITION


def _teardown_nested_dock(counters: Counters) -> None:
    """The final e2e_dock_stop, doubling as a clean-shutdown check (bash teardown)."""
    pid = _dock_pid()
    if pid is not None and _pid_alive(pid) and _lib_sh_call("e2e_dock_stop") != 0:
        print(
            f"{TOOL}: FAIL the vehicle dock did not exit cleanly on SIGTERM at teardown",
            flush=True,
        )
        counters.failed += 1


# ---- live session driver ---------------------------------------------------


def _latte_dock_running() -> bool:
    return (
        subprocess.run(["pgrep", "-x", "latte-dock"], capture_output=True, check=False).returncode
        == 0
    )


def _live_user_config_running() -> bool:
    """True when a running latte-dock uses the real ~/.config (bash restore probe)."""
    result = subprocess.run(
        ["pgrep", "-x", "latte-dock"], capture_output=True, text=True, check=False
    )
    pids = result.stdout.split()
    if result.returncode != 0 or not pids:
        return False
    try:
        environ = Path(f"/proc/{pids[0]}/environ").read_bytes()
    except OSError:
        return False
    target = f"XDG_CONFIG_HOME={os.environ['HOME']}/.config".encode()
    return target in environ


def _spawn_live_dock(repo: Path, script_args: Sequence[str], logfile: str) -> None:
    """Launch restart-staged.sh detached, logging to a file (the bash ``... &``)."""
    with open(logfile, "w") as handle:
        subprocess.Popen(
            [str(repo / "scripts" / "restart-staged.sh"), *script_args],
            stdout=handle,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )


def _drive_live(repo: Path, recipes: list[Path]) -> int:
    """The bash live path: restart the desk dock into the throwaway config, run,
    then restore a displaced user-config dock. Ported by inspection - never
    driven from here (the discipline: never touch the real session in a harness
    worktree); the nested path is the maintained, tested one.
    """
    if not os.environ.get("WAYLAND_DISPLAY"):
        print(f"{TOOL}: FAIL no wayland session (WAYLAND_DISPLAY unset)", flush=True)
        return EXIT_PRECONDITION
    config_home = Path(os.environ["E2E_BUILD"]) / "_runconfig"
    os.environ["E2E_CONFIG_HOME"] = str(config_home)
    layouts = sorted((config_home / "latte").glob("*.layout.latte"))
    os.environ["E2E_LAYOUT"] = str(layouts[0]) if layouts else ""

    restore_user_config = _live_user_config_running()
    print(f"{TOOL}: starting the throwaway dock on the live session...", flush=True)
    _spawn_live_dock(repo, ["-d"], "/tmp/latte-e2e.log")
    time.sleep(16)
    if not _latte_dock_running():
        print(
            f"{TOOL}: FAIL the throwaway dock did not come up (see /tmp/latte-e2e.log)",
            flush=True,
        )
        return EXIT_PRECONDITION

    counters = _execute_recipes(recipes, "live")

    if restore_user_config:
        print(f"{TOOL}: restoring the user-config dock...", flush=True)
        _spawn_live_dock(repo, ["--user-config", "-d"], "/tmp/latte-e2e-restore.log")
        time.sleep(8)
    _print_summary(counters)
    return 0 if counters.failed == 0 else 1


# ---- the recipe loop -------------------------------------------------------


def _execute_recipes(recipes: list[Path], mode: str) -> Counters:
    """Run each recipe as an opaque executable; classify, tally, capture on fail.

    The bash per-recipe order preserved exactly: executability, then the
    e2e-mode constraint, then the e2e-expect marker (a malformed marker refuses
    even a would-be-skipped recipe), then the mode-skip decision, then (nested)
    a dock health check, then the run and classification.
    """
    counters = Counters()
    for recipe in recipes:
        name = recipe.name
        if not os.access(recipe, os.X_OK):
            print(f"{TOOL}: FAIL missing or non-executable recipe: {name}", flush=True)
            counters.failed += 1
            continue
        text = recipe.read_text(errors="replace")
        constraint = recipe_mode(text)
        if constraint not in ("", "nested-only", "live-only"):
            print(
                f"{TOOL}: FAIL unknown e2e-mode marker '{constraint}' in {name} "
                "(allowed: nested-only, live-only, or none)",
                flush=True,
            )
            counters.failed += 1
            continue
        parsed = extract_recipe_expectation(text)
        if isinstance(parsed, MarkerError):
            print(f"{TOOL}: FAIL {parsed.message} in {name}", flush=True)
            counters.failed += 1
            continue
        expectation = parsed
        if (constraint == "nested-only" and mode != "nested") or (
            constraint == "live-only" and mode != "live"
        ):
            print(f"{TOOL}: SKIP {name} ({constraint})", flush=True)
            counters.skipped += 1
            continue

        if mode == "nested" and not _ensure_vehicle_dock(name, counters):
            continue

        print(f"{TOOL}: ---- {name}", flush=True)
        counters.ran += 1
        status = subprocess.run([str(recipe)], env=os.environ, check=False).returncode
        result, message = classify_recipe_result(expectation, status, name)
        print(message, flush=True)
        counters.record(result)
        if status != 0 and mode == "nested":
            _capture_failure_artifacts(name)
    return counters


def _ensure_vehicle_dock(name: str, counters: Counters) -> bool:
    """Give the recipe a running dock; restart it if a prior recipe left it down.

    Recipes may leave the dock stopped (the config-restore contract), so the
    bash re-checked and restarted before each. A restart failure FAILs this
    recipe (failed++, skip it) and returns False.
    """
    pid = _dock_pid()
    if pid is not None and _pid_alive(pid):
        return True
    print(f"{TOOL}: (re)starting the vehicle dock for {name}...", flush=True)
    if _lib_sh_call("e2e_dock_start", str(DOCK_START_TIMEOUT)) != 0:
        print(f"{TOOL}: FAIL could not restart the vehicle dock; dock log tail:", flush=True)
        _print_log_tail(Path(os.environ["E2E_DOCK_LOG"]), LOG_TAIL_LINES)
        counters.failed += 1
        return False
    return True


def _capture_failure_artifacts(name: str) -> None:
    """Copy the dock and kwin logs into E2E_ARTIFACTS on a nested failure (bash cp)."""
    artifacts = Path(os.environ["E2E_ARTIFACTS"])
    for env_key, suffix in (("E2E_DOCK_LOG", "dock.log"), ("E2E_KWIN_LOG", "kwin.log")):
        source = os.environ.get(env_key)
        if not source:
            continue
        with suppress(OSError):
            shutil.copyfile(source, artifacts / f"{name}.{suffix}")


def _print_summary(counters: Counters) -> None:
    print(
        f"{TOOL}: {counters.passed}/{counters.ran} recipes passed "
        f"({counters.skipped} skipped for mode, {counters.xfailed} xfail)",
        flush=True,
    )


# ---- entry point -----------------------------------------------------------


def _parse_args(argv: Sequence[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="latte_harness.e2e_runner", description=__doc__)
    parser.add_argument(
        "--self-test-expectations",
        action="store_true",
        help="run the classification self-test and exit with its verdict",
    )
    parser.add_argument(
        "--live",
        action="store_true",
        help="drive recipes against the real Wayland session instead of the nested vehicle",
    )
    parser.add_argument(
        "recipes",
        nargs="*",
        help="explicit recipe names (bare or .sh/.py); default: discover every top-level recipe",
    )
    return parser.parse_args(argv)


def run(argv: Sequence[str] | None = None) -> int:
    """Drive the e2e suite; the return value is the exit-code verdict."""
    install_conventional_signal_exits()
    args = _parse_args(argv)
    if args.self_test_expectations:
        return run_expectation_selftest()
    if run_expectation_selftest(quiet=True) != 0:
        print(f"{TOOL}: FAIL expectation classifier self-test", file=sys.stderr, flush=True)
        return EXIT_PRECONDITION

    mode = "live" if args.live else "nested"
    paths = RepoPaths.discover()
    _export_common_env(paths.root, mode)
    precondition = _check_preconditions()
    if precondition is not None:
        return precondition

    recipes = discover_recipes(paths.tests_e2e, args.recipes)
    if mode == "nested":
        return _drive_nested(paths.root, recipes)
    return _drive_live(paths.root, recipes)


def main(argv: Sequence[str] | None = None) -> None:
    raise SystemExit(run(argv))


if __name__ == "__main__":
    main()
