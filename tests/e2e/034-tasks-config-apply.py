#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""CL-5 (the tasks-page settings audit) live leg, settling D10 (the Tasks config
page renders but did not apply its settings - the inherited half-finished upstream
feature; docs/tracking/known-defects.md D10). It proves the Tasks page's writes
reach the RUNNING tasks plasmoid on a real dock, the "applies at all" question
the audit plan's AU-5a demands answered first.

THE D10 PROOF: TasksConfig.qml writes tasks.plasmoid.configuration.<key>, and
tasks.plasmoid IS the Plasma::Applet whose .configuration IS the ONE
KConfigPropertyMap the running plasmoid reads AND appletConfigData() reports.
This recipe seeds NON-DEFAULT values for every tasks-page control while the dock
is stopped, restarts, and asserts appletConfigData reflects each one. Non-default
on purpose (the 032-effects shape): a readback that only ever showed the schema
default would pass on a plasmoid that reads nothing.

The RIGHT-KEY half is pinned in tests/taskshandleraudittest.cpp; this recipe is
the third leg: the live map carries what the page would write, AND one write
(launchersGroup Unique->Global, empty in this layout) visibly changes the running
bar - the plasmoid consuming the config end to end.

Ported from tests/e2e/034-tasks-config-apply.sh to latte_harness.audit / .recipe
(BP-3, the bash-to-python migration's recipe batch). The 30-value seed is one
(key, value) list, seeded via kwriteconfig6 and asserted via the typed audit
snapshot, so the seed and assert sets cannot drift apart. A full backup of the
layout file is restored on exit.
"""

from __future__ import annotations

import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections.abc import Callable
from contextlib import redirect_stderr, suppress
from pathlib import Path

from pydantic import ValidationError

from latte_harness import audit, recipe

# Every tasks-page control seeded to a NON-DEFAULT value (the comment is its
# main.xml default). The seed writes each and the readback asserts each, from one
# list so the two sets stay identical (the 30 values the D10 proof depends on).
SEEDS: list[tuple[str, str]] = [
    # AU-5b Badges (91-95)
    ("showInfoBadge", "false"),  # default true
    ("showProgressBadge", "false"),  # default true
    ("showAudioBadge", "false"),  # default true
    ("audioBadgeActionsEnabled", "false"),  # default true
    ("infoBadgeProminentColorEnabled", "true"),  # default false
    # AU-5b Interaction (96-99)
    ("isPreferredForDroppedLaunchers", "false"),  # default true
    ("showWindowActions", "true"),  # default false
    ("previewWindowAsPopup", "true"),  # default false
    # AU-5b Filters (100-105)
    ("showOnlyCurrentScreen", "true"),  # default false
    ("showOnlyCurrentDesktop", "true"),  # default false
    ("showOnlyCurrentActivity", "false"),  # default true
    ("showWindowsOnlyFromLaunchers", "true"),  # default false
    ("hideAllTasks", "true"),  # default false
    ("groupTasksByDefault", "false"),  # default true
    # AU-5c Animations (106-110)
    ("animationLauncherBouncing", "false"),  # default true
    ("animationWindowInAttention", "false"),  # default true
    ("animationNewWindowSliding", "false"),  # default true
    ("animationWindowAddedInGroup", "false"),  # default true
    ("animationWindowRemovedFromGroup", "false"),  # default true
    # AU-5c Launchers (111-113): 2 = Global (default 0 = Unique)
    ("launchersGroup", "2"),
    # AU-5c Scrolling (114-116)
    ("scrollTasksEnabled", "true"),  # default false
    ("autoScrollTasksEnabled", "false"),  # default true
    ("manualScrollTasksType", "2"),  # default 1
    # AU-5c Actions (117-121): each seeded to a different offered enum value
    ("leftClickAction", "7"),  # default 6 (PresentWindows) -> 7 (Preview)
    ("middleClickAction", "1"),  # default 2 (NewInstance) -> 1 (Close)
    ("hoverAction", "2"),  # default 9 (Preview+Highlight) -> 2 (Highlight)
    ("taskScrollAction", "2"),  # default 1 (ScrollTasks) -> 2 (ScrollToggleMin)
    ("modifierClickAction", "1"),  # default 0 (None) -> 1 (Close)
    ("modifier", "3"),  # default 1 (Ctrl) -> 3 (Meta)
    ("modifierClick", "2"),  # default 0 (LeftClick) -> 2 (RightClick)
]


def _quiet_dock_stop() -> None:
    """e2e_dock_stop >/dev/null 2>&1 || true: best-effort, chatter suppressed."""
    with suppress(recipe.RecipeError), redirect_stderr(io.StringIO()):
        _ = recipe.dock_stop()


def _tcfg(layout: str, view: int, applet: int, key: str, value: str) -> None:
    """tcfg(): write into the tasks plasmoid's own config subgroup
    ([Containments][view][Applets][applet][Configuration][General]) - the on-disk
    home of the tasks.plasmoid.configuration.<key> map."""
    subprocess.run(
        [
            "kwriteconfig6",
            "--file",
            layout,
            "--group",
            "Containments",
            "--group",
            str(view),
            "--group",
            "Applets",
            "--group",
            str(applet),
            "--group",
            "Configuration",
            "--group",
            "General",
            "--key",
            key,
            value,
        ],
        check=False,
    )


def _tasks_launcher_count(view: int) -> int:
    """tasks_launcher_count: the launcher rows the tasks plasmoid currently shows.

    W3 (widen the readback models): isLauncher rides the typed recipe.Task, so this
    counts the typed rows instead of grepping ``"isLauncher":true`` over the raw
    payload. That also fixes a silent swallow the bash carried: the raw grep over a
    REFUSED (empty) reply counted 0, reading a refusal as "no launchers"; the typed
    read raises the loud DbusUnavailableError instead (the never-swallow rule)."""
    return sum(1 for t in recipe.view_tasks(view) if t.is_launcher)


def _snapshot_or_fail(produce: Callable[[], str], fail_message: str) -> str:
    """audit_applet_config_snapshot > file || e2e_fail: a readback that cannot
    validate fails loudly with the bash message."""
    try:
        return produce()
    except ValidationError as err:
        print(str(err), file=sys.stderr, flush=True)
        recipe.fail(fail_message)


def _print_matching(header: str, text: str, pattern: str) -> None:
    """echo <header>; grep -E <pattern> || true: the diagnostic snapshot echo."""
    print(header)
    compiled = re.compile(pattern)
    for line in text.splitlines():
        if compiled.match(line):
            print(line)


def main() -> None:
    try:
        view = recipe.tasks_view()
    except recipe.RecipeError:
        recipe.fail("no tasks view")
    try:
        applet = audit.tasks_applet_id(view)
    except audit.AuditError:
        recipe.fail(f"could not resolve the single tasks plasmoid under view {view}")
    print(f"CL-5: tasks-config-apply view={view} tasks-applet={applet}")

    # baseline: the default layout ships the tasks bar with its Unique-group
    # launchers, captured BEFORE the seed so the post-seed change is a real
    # before/after, not an assumed absolute.
    baseline_launchers = _tasks_launcher_count(view)
    if baseline_launchers < 1:
        recipe.fail(
            f"the default tasks bar shows no launchers ({baseline_launchers}) "
            "- the vehicle cannot show a launchersGroup change"
        )
    print(
        f"baseline: tasks bar shows {baseline_launchers} launcher row(s) under the default config"
    )

    layout = os.environ["E2E_LAYOUT"]
    fd, backup_name = tempfile.mkstemp(suffix=".latte")
    os.close(fd)
    backup = Path(backup_name)
    shutil.copy(layout, backup)
    try:
        # ---- seed non-default tasks config while the dock is stopped -----------
        # stop first: a clean SIGTERM flushes the CURRENT config, so the seed must
        # land after the flush (030/110/032 order).
        _quiet_dock_stop()
        for key, value in SEEDS:
            _tcfg(layout, view, applet, key, value)

        if not recipe.dock_start(90):
            recipe.fail("dock never settled after seeding the tasks config")

        # ---- assert the applet's LIVE config map reflects every seeded value ---
        with tempfile.TemporaryDirectory() as work_str:
            snap = Path(work_str) / "snap"
            snap.write_text(
                _snapshot_or_fail(
                    lambda: audit.applet_config_snapshot(view, applet),
                    f"appletConfigData snapshot failed for tasks applet {applet}",
                )
            )
            _print_matching(
                "--- tasks applet config snapshot (seeded keys) ---",
                snap.read_text(),
                r"^(show|hide|group|animation|launchersGroup|scroll|auto|manual|leftClick"
                r"|middleClick|hover|taskScroll|modifier|isPreferred|previewWindow"
                r"|audioBadge|infoBadge)",
            )
            rc = 0
            for key, want in SEEDS:
                if audit.assert_reflects(snap, key, want) != 0:
                    rc = 1
            if rc != 0:
                recipe.fail(
                    "a tasks config value did not reflect through the applet's live map "
                    "(see the snapshot above)"
                )

        # ---- behavioural P1: a tasks-page write visibly changed the running bar -
        # The seed switched launchersGroup Unique (populated) -> Global (empty in
        # this layout); the delegate observes plasmoid.configuration.launchersGroup
        # live, so a changed launcher count is the plasmoid consuming the write end
        # to end - the D10 "applies at all" question answered behaviourally.
        seeded_launchers = _tasks_launcher_count(view)
        if not seeded_launchers < baseline_launchers:
            recipe.fail(
                f"launchersGroup=Global did not change the visible launcher set "
                f"({baseline_launchers} -> {seeded_launchers}) - a tasks-page write did not "
                "reach the running plasmoid"
            )
        print(
            f"behavioural apply: launchersGroup Unique->Global changed the bar "
            f"({baseline_launchers} -> {seeded_launchers} launcher rows)"
        )

        print(
            "PASS: CL-5 D10 (Tasks config page applies its settings) - the page's writes reach the"
        )
        print(
            "      running plasmoid: appletConfigData reflects all 30 seeded tasks-page values "
            "through"
        )
        print(
            "      the applet's live map, and launchersGroup visibly changed the bar (AU-5a/5b/5c)"
        )
    finally:
        _quiet_dock_stop()
        shutil.copy(backup, layout)
        backup.unlink(missing_ok=True)


if __name__ == "__main__":
    recipe.run(main)
