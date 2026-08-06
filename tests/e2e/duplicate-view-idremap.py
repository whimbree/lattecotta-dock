#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""E2E: dbus duplicateView produces a collision-free containment whose
appletOrder references exactly its own new applet ids (the EX-07
StorageIdRemapper path end to end), then removes the duplicate and
waits out the libplasma undo window before finishing.

Ported from tests/e2e/duplicate-view-idremap.sh to latte_harness.recipe (BP-3,
the bash-to-python migration's recipe batch). The on-disk layout file is parsed
directly, as the bash grep/awk/comm did; the coarse duplicateView / removeView
actions stay busctl calls, exactly as the bash e2e_call did (unchecked, matching
`set -uo pipefail` without -e).
"""

import os
import re
import time
from pathlib import Path

from latte_harness import recipe

_CONTAINMENT_RE = re.compile(r"^\[Containments\]\[(\d+)\]$", re.MULTILINE)


def _read_layout(layout: str) -> str:
    return Path(layout).read_text(errors="replace")


def _containment_ids(text: str) -> list[int]:
    """before_ids/after_ids: the [Containments][<id>] group ids, sorted unique
    (the bash grep | grep -oE | sort -n | uniq)."""
    return sorted({int(match) for match in _CONTAINMENT_RE.findall(text)})


def _applet_order_tokens(text: str, cid: int) -> list[str]:
    """The appletOrder tokens in [Containments][cid][General].

    Mirrors the awk section scan (set inside the General subsection, reset on any
    group header) then the bash `${order//;/ }` word-splitting (on ; and
    whitespace, empty fields dropped)."""
    header = f"[Containments][{cid}][General]"
    inside = False
    collected: list[str] = []
    for line in text.splitlines():
        if line == header:
            inside = True
            continue
        if line.startswith("["):
            inside = False
        if inside and line.startswith("appletOrder="):
            collected.append(line[len("appletOrder=") :])
    return "\n".join(collected).replace(";", " ").split()


def _applet_group_ids(text: str, cid: int) -> set[str]:
    """The applet instance ids under [Containments][cid][Applets][<n>], as strings
    (the bash grep -oE then grep -qx compared them line-for-line)."""
    pattern = re.compile(rf"^\[Containments\]\[{cid}\]\[Applets\]\[(\d+)\]$", re.MULTILINE)
    return set(pattern.findall(text))


def _group_present(text: str, cid: int) -> bool:
    """True iff the [Containments][cid] group line survives (bash grep -q ^...$)."""
    needle = f"[Containments][{cid}]"
    return any(line == needle for line in text.splitlines())


def main() -> None:
    layout = os.environ["E2E_LAYOUT"]
    if not Path(layout).is_file():
        recipe.fail(f"throwaway layout not found at {layout}")

    before_ids = _containment_ids(_read_layout(layout))
    src_id = before_ids[0]

    recipe.call("duplicateView", "u", str(src_id))

    #! the new view appears on the bus first; the layout file follows on the
    #! next config flush (on-disk config is a LAZY witness - the CaptSilver
    #! adoption's finding - so poll it instead of trusting one sleep)
    new_id: int | None = None
    for _ in range(30):
        time.sleep(2)
        after_ids = _containment_ids(_read_layout(layout))
        created = sorted(set(after_ids) - set(before_ids))
        if created:
            new_id = created[0]
            break

    if new_id is None:
        live = recipe.json_payload("viewsData").count('"containmentId"')
        recipe.fail(
            f"no new containment reached the layout file after 60s (viewsData reports {live} views)"
        )

    #! collision-free by construction of the set difference; check the applet references
    text = _read_layout(layout)
    order = _applet_order_tokens(text, new_id)
    applets = _applet_group_ids(text, new_id)

    ok = True
    for token in order:
        if token not in applets:
            print(f"FAIL: appletOrder token {token} has no applet group")
            ok = False

    #! cleanup: remove the duplicate and wait out the undo window
    recipe.call("removeView", "u", str(new_id))
    for _ in range(24):
        if not _group_present(_read_layout(layout), new_id):
            break
        time.sleep(5)
    if _group_present(_read_layout(layout), new_id):
        recipe.fail(f"duplicate {new_id} still in layout after undo window")

    if ok:
        print(
            f"duplicate {src_id} -> {new_id}: "
            "ids collision-free, appletOrder consistent, cleaned up"
        )
    if not ok:
        raise SystemExit(1)


if __name__ == "__main__":
    recipe.run(main)
