# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""scripts/tools/watch-dock-presentation.sh ported (BP-5a, the dev-tools chunk).

Observe real-hardware presentation transitions on the live desktop. Each sample
joins a view's painted background/effects rectangle (dockSystemData) with its
live applet rectangles (viewAppletsData), records every distinct composition,
and stops at the first applet that escapes its background or the output canvas.
A failure preserves both D-Bus payloads and a fullscreen screenshot so the
first bad transition is inspectable as state and pixels.

Rebased onto the typed recipe API (BP-2c): the sample query is
recipe.try_json_payload, the composition oracle is
recipe._assert_presentation_coverage (the typed twin of lib.sh's
_e2e_assert_presentation_payloads, already proven byte-identical), and the
lifecycle guard is recipe.is_running.

Exit codes (the verdict, per the gate/exit-code contract):
  0  at least one geometry transition was observed and every state fit.
  1  a composition invariant or live query failed (or a bad argument).
  2  no geometry transition was exercised; refusing a vacuous pass.
"""

from __future__ import annotations

import re
import subprocess
import sys
import time
from collections.abc import Sequence
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path

from pydantic import TypeAdapter

from latte_harness import recipe
from latte_harness.paths import RepoPaths
from latte_harness.recipe import Applet, DockSystemData

TOOL = "presentation watcher"

EXIT_OK = 0  # a transition was observed and every state fit
EXIT_INVARIANT = 1  # a composition invariant or live query failed, or a bad arg
EXIT_NO_TRANSITION = 2  # no geometry transition exercised; vacuous pass refused

# The bash passed tolerance 2 explicitly to the coverage assert.
_TOLERANCE = 2

_APPLET_LIST: TypeAdapter[list[Applet]] = TypeAdapter(list[Applet])


class ArgError(Exception):
    """A bad CLI argument; the message is the exact bash stderr text."""


@dataclass(frozen=True, slots=True)
class WatchArgs:
    """The three validated positionals: [seconds] [sample-interval] [dock-id]."""

    duration: int
    duration_text: str  # the raw string, used in the summary like the bash `${1}`
    interval: float
    target: str  # "" -> every visible view, matching the bash empty-default


def parse_args(argv: Sequence[str]) -> WatchArgs:
    """Validate [seconds] [sample-interval] [dock-id] exactly as the bash did.

    Each refusal message, its stderr destination, and the exit-1 shape are
    byte-identical to the original so muscle memory and any doc quoting stay
    valid. Defaults mirror `${1:-30}` / `${2:-0.05}` / `${3:-}`: an unset or
    empty positional falls back the same way.
    """
    duration_text = (argv[0] if len(argv) >= 1 else "") or "30"
    if not duration_text.isdecimal():
        raise ArgError(f"duration must be a positive integer, got '{duration_text}'")
    duration = int(duration_text)
    if duration <= 0:
        raise ArgError("duration must be greater than zero")

    interval_text = (argv[1] if len(argv) >= 2 else "") or "0.05"
    if not re.fullmatch(r"(0|[0-9]+)(\.[0-9]+)?", interval_text) or re.fullmatch(
        r"0(\.0+)?", interval_text
    ):
        raise ArgError(f"sample interval must be a positive number, got '{interval_text}'")
    interval = float(interval_text)

    target = argv[2] if len(argv) >= 3 else ""
    if target and not target.isdecimal():
        raise ArgError(f"dock id must be an unsigned integer, got '{target}'")
    if target and int(target) == 0:
        raise ArgError("dock id must be greater than zero")

    return WatchArgs(
        duration=duration, duration_text=duration_text, interval=interval, target=target
    )


def select_visible_views(snapshot: DockSystemData, target: str) -> list[int]:
    """The bash inline filter: visible (not hidden) views, optionally one target.

    Iteration order is the dockSystemData view order so the trace and the
    per-view bookkeeping match the bash sample order. ``target`` compares against
    the string form of persistentDockId, exactly as the bash python did.
    """
    return [
        v.persistent_dock_id
        for v in snapshot.views
        if not v.is_hidden and (not target or str(v.persistent_dock_id) == target)
    ]


@dataclass(slots=True)
class TransitionTracker:
    """Per-view state bookkeeping: count transitions, flag first-seen/changed states.

    Mirrors the bash associative-array logic exactly. A transition is counted
    only when a view ALREADY had a recorded state and the new one differs (the
    `previous exists && previous != state` guard). A state is "new" - worth
    tracing and storing - when it differs from the recorded one, which includes
    the first observation of a view (the bash `${previous:-} != state` with an
    empty default against a non-empty coverage line).
    """

    transitions: int = 0
    _previous: dict[int, str] = field(default_factory=dict)

    def observe(self, view_id: int, state: str) -> bool:
        prior = self._previous.get(view_id)
        if prior is not None and prior != state:
            self.transitions += 1
        is_new = prior != state
        if is_new:
            self._previous[view_id] = state
        return is_new


def _now_stamp() -> str:
    """The bash printf '%(%FT%T%z)T' -1: local time, ISO-8601 with numeric tz."""
    return datetime.now().astimezone().strftime("%Y-%m-%dT%H:%M:%S%z")


def _run_stamp() -> str:
    """The bash `date +%Y%m%d-%H%M%S` run stamp for the artifact directory."""
    return datetime.now().strftime("%Y%m%d-%H%M%S")


def _append_trace(trace: Path, line: str) -> None:
    """`tee -a`: append the line (newline-terminated) to the trace file."""
    with trace.open("a") as handle:
        handle.write(line + "\n")


def _preserve_failure(
    artifacts: Path, view_id: int, views_json: str, applets_json: str, diagnostic: str
) -> None:
    """The bash preserve_failure: both payloads, the diagnostic, and a screenshot.

    The first bad transition is inspectable as state (the two JSON payloads plus
    failure.txt) and pixels (the fullscreen capture). The capture is best-effort:
    a failure leaves screenshot-error.txt instead, exactly like the bash `||`.
    """
    (artifacts / "dockSystemData.json").write_text(views_json + "\n")
    (artifacts / f"view-{view_id}-applets.json").write_text(applets_json + "\n")
    (artifacts / "failure.txt").write_text(diagnostic + "\n")
    capture = subprocess.run(
        [
            "spectacle",
            "--background",
            "--nonotify",
            "--fullscreen",
            "--output",
            str(artifacts / "workspace.png"),
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if capture.returncode != 0:
        (artifacts / "screenshot-error.txt").write_text("screenshot capture failed\n")
    print(f"{TOOL}: FAIL view {view_id}: {diagnostic}", file=sys.stderr, flush=True)
    print(f"{TOOL}: artifacts: {artifacts}", file=sys.stderr, flush=True)


def _sample_view(
    artifacts: Path, snapshot: DockSystemData, views_json: str, view_id: int
) -> str | None:
    """One view's coverage state, or None after preserving a failure (exit 1).

    Queries this view's applets, runs the composition oracle, and returns the
    coverage line. A failed or refused query (try_json_payload's None) or an
    escaped applet preserves the exact failing payloads and diagnostic (the
    bash's two per-view failure branches) and returns None to signal the
    caller to stop with EXIT_INVARIANT.
    """
    applets_json = recipe.try_json_payload("viewAppletsData", "u", str(view_id))
    if applets_json is None:
        _preserve_failure(artifacts, view_id, views_json, "[]", "viewAppletsData query failed")
        return None
    applets = _APPLET_LIST.validate_json(applets_json)
    try:
        return recipe._assert_presentation_coverage(  # pyright: ignore[reportPrivateUsage]
            snapshot, applets, view_id, _TOLERANCE
        )
    except recipe.RecipeError as exc:
        _preserve_failure(artifacts, view_id, views_json, applets_json, str(exc))
        return None


def watch(args: WatchArgs, repo: Path) -> int:
    """Sample the live dock for ``args.duration`` seconds; return the exit verdict.

    Reuses the typed recipe API for every live read: the lifecycle guard, the
    per-surface query (a failed or refused read is try_json_payload's None, so
    a dbusreports refusal exits as "query failed", never a ValidationError
    about an empty payload), and the composition oracle. The pure decision
    logic (view selection, transition
    counting) is delegated to the tested helpers above; this function is the I/O
    wiring the nested vehicle and the live session exercise.
    """
    artifacts = repo / "build" / "_live-observations" / f"presentation-{_run_stamp()}"
    artifacts.mkdir(parents=True, exist_ok=True)
    trace = artifacts / "trace.log"

    if not recipe.is_running():
        print("the live dock is not running", file=sys.stderr, flush=True)
        return EXIT_INVARIANT

    tracker = TransitionTracker()
    deadline = time.monotonic() + args.duration
    samples = 0

    while time.monotonic() < deadline:
        views_json = recipe.try_json_payload("dockSystemData")
        if views_json is None:
            print("dockSystemData query failed", file=sys.stderr, flush=True)
            return EXIT_INVARIANT
        snapshot = DockSystemData.model_validate_json(views_json)

        for view_id in select_visible_views(snapshot, args.target):
            state = _sample_view(artifacts, snapshot, views_json, view_id)
            if state is None:
                return EXIT_INVARIANT
            if tracker.observe(view_id, state):
                line = f"{_now_stamp()} {state}"
                print(line, flush=True)
                _append_trace(trace, line)

        samples += 1
        time.sleep(args.interval)

    if tracker.transitions == 0:
        message = f"{TOOL}: no geometry transition observed in {args.duration_text}s"
        _append_trace(trace, message)
        print(message, file=sys.stderr, flush=True)
        print(
            f"{TOOL}: exercise hover zoom or another geometry change; artifacts: {artifacts}",
            file=sys.stderr,
            flush=True,
        )
        return EXIT_NO_TRANSITION

    print(
        f"{TOOL}: PASS {samples} samples, {tracker.transitions} transitions; trace: {trace}",
        flush=True,
    )
    return EXIT_OK


def main(argv: Sequence[str] | None = None) -> int:
    args_list = list(sys.argv[1:] if argv is None else argv)
    try:
        args = parse_args(args_list)
    except ArgError as err:
        print(str(err), file=sys.stderr, flush=True)
        return EXIT_INVARIANT
    return watch(args, RepoPaths.discover().root)


if __name__ == "__main__":
    raise SystemExit(main())
