#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""E2E: the dock under test is the build tree we think it is, not a shadow.

Reads the vehicle dock's own /proc/<pid>/maps and asserts that the executable
AND the containment plugin actually mapped into the running process resolve
under $E2E_BUILD - never a packaged /nix/store latte-dock copy.

Two reasons this guard exists:
 1. The PR #23 shadow (caught 2026-07-18 via /proc/<dock>/maps): the desktop
    session leaked NIXPKGS_QT6_QML_IMPORT_PATH carrying the SYSTEM-INSTALLED
    packaged latte-dock, whose org.kde.latte.private.containment plugin then
    shadowed the staged one - so every containment/plugin change "landed but
    never ran". latte_harness.qmlenv now strips that leaf; this recipe is the
    standing guard that it stays stripped, in any nested run.
 2. The sanitized gate (docs/tracking/ub-catching-plan.md A3): a driven ASan/UBSan gate
    that ran a shadowed, NON-instrumented binary would catch no UB and pass
    green - the worst kind of false confidence. When the caller sets
    E2E_EXPECT_ASAN=1 (scripts/run-asan-dock.sh does), this recipe additionally
    asserts libasan is mapped into the dock, proving the process really is the
    sanitized build and not an uninstrumented shadow.

The containment plugin is dlopened at view creation, so this waits for the dock
to settle before reading the map - an empty map would be "not loaded yet", not
"no shadow".

BP-3 R11 port of 070-asan-binary-shadow.sh over latte_harness.recipe: the same
settle wait, the same /proc/<pid>/maps parse (executable, containment plugin,
libasan), the same prefix assertions and byte-identical failure messages.
"""

from __future__ import annotations

import os
from pathlib import Path

from latte_harness import recipe


def _last_field_paths(maps: str, needle: str) -> list[str]:
    """awk '/<needle>/ {print $NF}' | sort -u: the mapped paths matching needle."""
    return sorted({line.split()[-1] for line in maps.splitlines() if needle in line})


def main() -> None:
    # realpath -m "$E2E_BUILD": canonicalize the build tree under test. The env is
    # set by the runner for every nested recipe; an unset value is a broken harness
    # (the bash ${E2E_BUILD:?E2E_BUILD unset}), so a loud KeyError is correct here.
    build = os.path.realpath(os.environ["E2E_BUILD"])

    if not recipe.wait_settled(45):
        recipe.fail("vehicle dock never settled (containment plugin not loaded yet)")

    pid = recipe.dock_pid()
    if pid is None:
        recipe.fail("no dock pid recorded")
    if not recipe.pid_alive(pid):
        recipe.fail(f"dock (pid {pid}) is not alive")
    maps_path = f"/proc/{pid}/maps"
    try:
        maps = Path(maps_path).read_text()
    except OSError:
        recipe.fail(f"cannot read {maps_path}")

    # 1. the executable itself. run-staged.sh execs "$build/bin/latte-dock", so this
    #    is guaranteed by construction - assert it anyway, cheaply, so a future
    #    launcher change that reintroduces a PATH-resolved dock is caught here.
    exe = os.path.realpath(f"/proc/{pid}/exe")
    if not exe.startswith(f"{build}/"):
        recipe.fail(
            f"dock exe resolves to {exe}, not under the build tree under test ({build}) "
            "- a shadow binary is running"
        )
    print(f"  exe: {exe}")

    # 2. the containment plugin actually mapped into THIS process. A packaged copy
    #    would map from /nix/store/*-latte-dock-*/...; the staged one maps from
    #    $build/_qmlstage/... - both absolute, so a prefix check on the resolved
    #    path separates them cleanly.
    plugpaths = _last_field_paths(maps, "liblattecontainmentplugin.so")
    if not plugpaths:
        recipe.fail(
            f"liblattecontainmentplugin.so is not mapped into the dock (pid {pid}) "
            "- the containment plugin never loaded, so the shadow check has nothing to verify"
        )
    for path in plugpaths:
        resolved = os.path.realpath(path)
        if not resolved.startswith(f"{build}/"):
            recipe.fail(
                f"containment plugin mapped from {resolved}, not under the build tree under "
                f"test ({build}) - a shadow copy is running "
                "(PR #23 class: a leaked NIXPKGS_QT6_QML_IMPORT_PATH?)"
            )
        print(f"  containment plugin: {resolved}")

    # 3. when the caller declares the dock should be sanitized, prove it: libasan
    #    must be mapped. GCC's shared ASan runtime is pulled by the instrumented
    #    executable and the dlopened plugins resolve __asan_* from it, so its
    #    absence means the running binary is not the sanitized build - exactly the
    #    silent no-op the driven UB gate must never accept.
    if os.environ.get("E2E_EXPECT_ASAN", "0") == "1":
        if "libasan.so" not in maps:
            recipe.fail(
                f"E2E_EXPECT_ASAN=1 but libasan is not mapped into the dock (pid {pid}) "
                "- the sanitized binary was shadowed by an uninstrumented one; "
                "the UB gate would catch nothing"
            )
        asan = next((line.split()[-1] for line in maps.splitlines() if "libasan.so" in line), "")
        print(f"  sanitized: libasan mapped ({asan})")
    else:
        print("  (E2E_EXPECT_ASAN unset: skipping the libasan check - normal non-sanitized run)")

    print("PASS: asan-binary-shadow")


if __name__ == "__main__":
    recipe.run(main)
