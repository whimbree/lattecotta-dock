#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""CL-4 live P3 leg (docs/tracking/edit-mode-settings-audit-plan.md, controls 75-90):
the Effects page controls REFLECT the dock's live state on open. The wiring (which
key/property each control writes) is pinned deterministically in
tests/effectshandleraudittest.cpp; this recipe proves the other direction on a
REAL running dock - that the CL-0 readback surfaces the live effects state the
settings window would show:

  AU-4a Shadows   - config.appletShadowsEnabled / shadowSize / shadowOpacity /
                    shadowColorType reflect the seeded General values.
  AU-4b Animations - config.animationsEnabled / durationTime reflect. durationTime
                    is seeded to 1 (the x3 "faster" button's stored value, the S-c
                    pairing) and read back as the raw integer, the sole contract.
  AU-4c Indicators - view.indicatorEnabled / indicatorType / indicatorPresent
                    reflect the seeded [Indicator] sub-group.

Method (the 110-colorizer seed+restart shape): stop the dock, write known
NON-DEFAULT effects values into the containment config (General + Indicator),
restart, and assert every value via the CL-0 snapshot helpers. Non-default on
purpose - a readback that only ever showed the schema default would pass on a
dock that reads nothing. A full backup of the layout file is restored on exit.

Ported from tests/e2e/032-effects-config-readback.sh to latte_harness.audit /
.recipe (BP-3, the bash-to-python migration's recipe batch). The kwriteconfig6
seed writes and the whole-file backup/restore stay subprocess/shutil calls; the
snapshots and P3 asserts ride the typed audit API.
"""

from __future__ import annotations

import io
import os
import re
import shutil
import sys
import tempfile
from collections.abc import Callable
from contextlib import redirect_stderr, suppress
from pathlib import Path

from pydantic import ValidationError

from latte_harness import audit, recipe


def _quiet_dock_stop() -> None:
    """e2e_dock_stop >/dev/null 2>&1 || true: best-effort, chatter suppressed."""
    with suppress(recipe.RecipeError), redirect_stderr(io.StringIO()):
        _ = recipe.dock_stop()


def _kwrite(layout: str, group: str, key: str, value: str, view: int) -> None:
    """kwriteconfig6 into [Containments][view][<group>]: the gen()/ind() writers."""
    _ = recipe.kwriteconfig(
        "--file",
        layout,
        "--group",
        "Containments",
        "--group",
        str(view),
        "--group",
        group,
        "--key",
        key,
        value,
    )


def _snapshot_or_fail(produce: Callable[[], str], fail_message: str) -> str:
    """audit_*_snapshot > file || e2e_fail: a readback that cannot validate fails
    loudly with the bash message (a malformed reply raises pydantic here where the
    bash python one-liner sys.exit'd on the non-JSON / missing object)."""
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
    print(f"CL-4: effects readback view is containment {view}")

    layout = os.environ["E2E_LAYOUT"]
    fd, backup_name = tempfile.mkstemp(suffix=".latte")
    os.close(fd)
    backup = Path(backup_name)
    shutil.copy(layout, backup)
    try:
        # ---- seed non-default effects state while the dock is stopped ----------
        # stop first: a clean SIGTERM flushes the CURRENT config, so the write must
        # land after the flush, exactly as 030/110 order it.
        _quiet_dock_stop()

        _kwrite(layout, "General", "appletShadowsEnabled", "false", view)  # default true
        _kwrite(layout, "General", "shadowSize", "55", view)  # default 30
        _kwrite(layout, "General", "shadowOpacity", "40", view)  # default 70
        _kwrite(layout, "General", "shadowColorType", "2", view)  # default 0; 2 = User
        _kwrite(layout, "General", "animationsEnabled", "false", view)  # default true
        _kwrite(layout, "General", "durationTime", "1", view)  # default 2; 1 = "faster"
        _kwrite(layout, "Indicator", "enabled", "false", view)  # default true
        _kwrite(layout, "Indicator", "type", "org.kde.latte.plasma", view)  # default .default

        if not recipe.dock_start(90):
            recipe.fail("dock never settled after seeding the effects config")

        # ---- assert the CL-0 readback reflects every seeded value --------------
        with tempfile.TemporaryDirectory() as work_str:
            work = Path(work_str)
            cfg = work / "config"
            cfg.write_text(
                _snapshot_or_fail(
                    lambda: audit.config_snapshot(view), "viewConfigData config snapshot failed"
                )
            )
            vw = work / "view"
            vw.write_text(
                _snapshot_or_fail(
                    lambda: audit.view_snapshot(view), "viewConfigData view snapshot failed"
                )
            )
            _print_matching(
                "--- config snapshot (effects keys) ---",
                cfg.read_text(),
                r"^(appletShadowsEnabled|shadowSize|shadowOpacity|shadowColorType"
                r"|animationsEnabled|durationTime)\b",
            )
            _print_matching("--- view snapshot (indicator) ---", vw.read_text(), r"^indicator")

            # AU-4a shadows (config half), AU-4b animations (config half),
            # AU-4c indicators (view half - the CL-0 indicator readback)
            checks: list[tuple[Path, str, str]] = [
                (cfg, "appletShadowsEnabled", "false"),
                (cfg, "shadowSize", "55"),
                (cfg, "shadowOpacity", "40"),
                (cfg, "shadowColorType", "2"),
                (cfg, "animationsEnabled", "false"),
                (cfg, "durationTime", "1"),
                (vw, "indicatorPresent", "true"),
                (vw, "indicatorEnabled", "false"),
                (vw, "indicatorType", '"org.kde.latte.plasma"'),
            ]
            rc = 0
            for snap, key, want in checks:
                if audit.assert_reflects(snap, key, want) != 0:
                    rc = 1
            if rc != 0:
                recipe.fail(
                    "an effects value did not reflect through the CL-0 readback "
                    "(see the snapshots above)"
                )

        print("PASS: CL-4 effects config/indicator readback reflects live state (AU-4a/b/c P3)")
    finally:
        _quiet_dock_stop()
        shutil.copy(backup, layout)
        backup.unlink(missing_ok=True)


if __name__ == "__main__":
    recipe.run(main)
