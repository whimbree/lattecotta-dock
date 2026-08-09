#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""D20 guard: the dock right-click context menu must expose the FULL
always-shown action set in NORMAL (non-edit) mode.

menu.cpp:288 shows each normal-mode action iff
m_actionsAlwaysShown.contains(action) || configuring, where
m_actionsAlwaysShown is contextMenuData index 3 (the ;;-joined always-shown
list from UniversalSettings::contextMenuActionsAlwaysShown). So when that
list is empty, normal mode (configuring==false) hides every Latte action
except the section header and Edit Dock - the D20 collapse. EDIT mode masks
the fault entirely (|| configuring shows everything), which is why the
port's edit-mode-only menu verification (PORTING_PLAN menu check) never
caught it. This is the missing normal-mode assertion.

The assertion is on the DATA menu.cpp gates on (contextMenuData index 3),
not on rendered menu pixels: index 3 is precisely the input to the
normal-mode visibility decision, and it is pull-queryable.

Ported from tests/e2e/110-context-menu-normal-mode.sh to latte_harness.recipe
(BP-3, the bash-to-python migration's recipe batch). contextMenuData is a
plain array reply, not the single JSON string e2e_json unwraps, so it is read
through busctl --json=short exactly as the bash recipe read it directly.
"""

import json
import subprocess
import sys

from latte_harness import recipe

# The always-shown ids a default/rich config exposes
# (app/data/contextmenudata.h ACTIONSALWAYSVISIBLE). Sorted for
# order-independent comparison; the D-Bus feed carries config order.
EXPECTED_ALWAYS = "_add_latte_widgets _add_view _layouts _preferences _quit_latte _separator1"


def _feed_index3(cid: int) -> str:
    """The raw ;;-joined always-shown feed (contextMenuData index 3) for a
    containment, exactly the string menu.cpp splits and gates on."""
    result = subprocess.run(
        [
            "busctl",
            "--user",
            "--json=short",
            "call",
            "org.kde.lattedock",
            "/Latte",
            "org.kde.LatteDock",
            "contextMenuData",
            "u",
            str(cid),
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    d = json.loads(result.stdout)["data"][0]
    return d[3] if len(d) > 3 else ""


def _sorted_ids(feed: str) -> str:
    """The feed as a sorted, space-joined id set (empty feed -> empty output),
    so the guard compares sets, not serialisation order."""
    return " ".join(sorted(p for p in feed.split(";;") if p))


def _assert_full_always_set(feed: str, label: str, *, quiet: bool = False) -> bool:
    """The exact D20 guard. Passes iff the feed's id set is the full expected
    always-shown set; fails loud (unless quiet) otherwise. This is the SINGLE
    assertion driven live below AND exercised by the negative control, so a
    proven-rejecting negative control proves the live pass is real."""
    got = _sorted_ids(feed)
    expected = _sorted_ids(EXPECTED_ALWAYS.replace(" ", ";;"))
    if got != expected:
        if not quiet:
            print(
                f"assert_full_always_set: {label} always-shown set is "
                f"[{got}], expected [{expected}]",
                file=sys.stderr,
                flush=True,
            )
        return False
    return True


def main() -> None:
    if not recipe.wait_running(30):
        recipe.fail("dock not running")
    if not recipe.wait_settled(30):
        recipe.fail("views did not settle")

    # The guard: every view, in normal mode, exposes the full always-shown set.
    # W3 (widen the readback models): containmentId / editMode ride the typed
    # recipe.views().
    checked = 0
    for view in recipe.views():
        cid = view.containment_id
        #! normal mode is the whole point: edit mode would mask an emptied list
        editmode = view.edit_mode
        if editmode is not False:
            recipe.fail(
                f"view {cid} reports editMode={editmode}; "
                "the normal-mode guard needs configuring==false"
            )
        if not _assert_full_always_set(_feed_index3(cid), f"view {cid} (normal mode)"):
            recipe.fail(
                f"view {cid} right-click menu is collapsed in normal mode (D20): "
                "the always-shown feed does not carry the full action set"
            )
        checked += 1
    if checked == 0:
        recipe.fail("no views were available to check")
    print(f"normal-mode context menu: full always-shown set present on {checked} view(s)")

    # The negative control: the SAME assertion must REJECT the D20 states, or the
    # guard above is vacuous. An emptied key (the D20 collapse) and any partial
    # feed must both fail. Proven live too: seeding contextMenuActionsAlwaysShown=
    # and restarting drives contextMenuData index 3 to '' (recorded in the D20
    # entry); here the assertion is shown to reject that exact feed shape.
    for bad in ("", "_layouts", "_layouts;;_preferences;;_quit_latte"):
        if _assert_full_always_set(bad, f"negative-control [{bad}]", quiet=True):
            recipe.fail(
                f"negative control: the guard ACCEPTED a collapsed/partial feed [{bad}] - "
                "it would not catch D20"
            )
    print("negative control: the guard rejects the emptied (D20) and partial always-shown feeds")


if __name__ == "__main__":
    recipe.run(main)
