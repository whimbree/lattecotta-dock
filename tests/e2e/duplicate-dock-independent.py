#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""D77 (dock duplication retains clone lineage and edit ownership) dual-output
acceptance. Duplicate Dock must create exactly one independent snapshot from
either member of an existing linked replica relationship. The copied dock
receives fresh containment and applet identities, no clone graph entry, and
remains independent after persistence reload - which since the D283 fix (the
legacy AllScreensGroup clone path drops its persisted replica on reload) also
pins that the persisted replica is ADOPTED at reload with its containment
identity intact, not stripped and regenerated.

Ported from tests/e2e/duplicate-dock-independent.sh to latte_harness.recipe
(the bash-to-python migration's last defect-blocked lifecycle holdout, freed by
the D283 fix). The on-disk layout file is parsed directly for the persistence
checks, as the ported duplicate-view-idremap does for its id checks; the coarse
duplicateView / setViewVisibilityMode actions stay busctl calls that fail
loudly, matching the bash `e2e_call ... || e2e_fail`. One deliberate hardening
over the bash: every viewsData read that races a fresh duplicate's placement
publication polls through recipe.DbusUnavailableError (viewsData refuses
records while a placement is still unaccepted; the bash read it once and could
fail on empty JSON).
"""

import os
import time
from pathlib import Path

from latte_harness import recipe

_POLL_INTERVAL = 0.25


def _views_settled() -> list[recipe.View]:
    """viewsData, polled through transient placement-publication refusals.

    dbusreports refuses the whole viewsData reply while any view lacks an
    accepted placement (the placement-atomics contract), so a read racing a
    fresh duplicate can transiently fail; that is a wait, not a verdict.
    """
    last_error = "<no reply>"
    for _ in range(80):
        try:
            return recipe.views()
        except recipe.DbusUnavailableError as exc:
            last_error = str(exc)
            time.sleep(_POLL_INTERVAL)
    recipe.fail(f"viewsData never settled: {last_error}")


def _applet_ids(view_id: int) -> set[int]:
    return {applet.id for applet in recipe.view_applets(view_id)}


def _wait_for_linked_pair() -> tuple[int, int]:
    """One original plus one replica cloned from it (80 x 0.25s), or fail."""
    for _ in range(80):
        try:
            views = recipe.views()
        except recipe.DbusUnavailableError:
            views = []
        originals = [v for v in views if not v.is_cloned]
        clones = [v for v in views if v.is_cloned]
        if (
            len(originals) == 1
            and len(clones) == 1
            and clones[0].is_cloned_from == originals[0].containment_id
        ):
            return originals[0].containment_id, clones[0].containment_id
        time.sleep(_POLL_INTERVAL)
    recipe.fail("AllScreensGroup did not create one original and one linked replica")


def _duplicate_once(source: int, label: str, before_ids: set[int]) -> int:
    """duplicateView(source) must add exactly one independent containment."""
    recipe.call_or_fail(
        f"{label} duplicateView call failed for containment {source}",
        "duplicateView",
        "u",
        str(source),
    )

    candidate: int | None = None
    for _ in range(100):
        try:
            current = recipe.views()
        except recipe.DbusUnavailableError:
            current = []
        created = [v for v in current if v.containment_id not in before_ids]
        if len(created) == 1 and not created[0].is_cloned and created[0].is_cloned_from == -1:
            candidate = created[0].containment_id
            break
        time.sleep(0.2)
    if candidate is None:
        recipe.fail(f"{label} did not create one independent containment")

    #! Allow the old copied-AllScreensGroup policy enough time to spawn its
    #! second-output clone. A transient one-view state must not pass this check.
    time.sleep(3)
    final = _views_settled()
    created = [v for v in final if v.containment_id not in before_ids]
    if len(created) != 1:
        recipe.fail(
            f"{label} created a linked ensemble instead of one independent dock: "
            f"expected exactly one new view, got {len(created)}: "
            f"{[v.containment_id for v in created]}"
        )
    if created[0].is_cloned or created[0].is_cloned_from != -1:
        recipe.fail(f"{label} new view retained a clone graph entry: {created[0]}")

    return candidate


def _containment_entry(layout_text: str, cid: int, key: str) -> str | None:
    """The ``key=`` value directly inside [Containments][cid] (None when absent).

    Mirrors the bash kreadconfig6 --group Containments --group <cid> reads: only
    the top-level containment group, never its subsections.
    """
    header = f"[Containments][{cid}]"
    inside = False
    for line in layout_text.splitlines():
        if line == header:
            inside = True
            continue
        if line.startswith("["):
            inside = False
        if inside and line.startswith(f"{key}="):
            return line[len(key) + 1 :]
    return None


def main() -> None:
    if int(os.environ.get("E2E_OUTPUT_COUNT", "1")) != 2:
        recipe.fail("duplicate-dock-independent needs the dual-output vehicle")
    layout = recipe.require_env("E2E_LAYOUT")

    initial = [v for v in _views_settled() if not v.is_cloned]
    if len(initial) != 1:
        recipe.fail(f"expected one initial original, saw {len(initial)}")
    source_id = initial[0].containment_id

    #! Turn the seed dock into an existing linked relationship without changing
    #! its containment identity. Existing linked layout migration must preserve
    #! this pair; only newly duplicated docks are normalized.
    if not recipe.dock_stop():
        recipe.fail("could not stop the seed dock before linking outputs")
    recipe.kwriteconfig_or_fail(
        f"could not write screensGroup for containment {source_id}",
        "--file",
        layout,
        "--group",
        "Containments",
        "--group",
        str(source_id),
        "--key",
        "screensGroup",
        "1",
    )
    if not recipe.dock_start():
        recipe.fail("linked seed dock did not restart")
    original_id, replica_id = _wait_for_linked_pair()

    baseline = _views_settled()
    baseline_ids = {v.containment_id for v in baseline}
    baseline_applets: set[int] = set()
    for view in baseline:
        baseline_applets |= _applet_ids(view.containment_id)

    from_original = _duplicate_once(original_id, "original-source", baseline_ids)
    after_original_ids = {v.containment_id for v in _views_settled()}
    from_replica = _duplicate_once(replica_id, "replica-source", after_original_ids)

    if from_original == from_replica:
        recipe.fail(f"both duplicate calls returned containment {from_original}")

    duplicate_applets = [
        *sorted(_applet_ids(from_original)),
        *sorted(_applet_ids(from_replica)),
    ]
    if len(duplicate_applets) != len(set(duplicate_applets)):
        recipe.fail(f"duplicate applet ids overlap each other: {duplicate_applets}")
    overlap = baseline_applets.intersection(duplicate_applets)
    if overlap:
        recipe.fail(f"duplicate applet ids overlap the linked source: {sorted(overlap)}")

    #! Drive a real original-to-replica property synchronization after both
    #! duplicates exist. ClonedView connects the original VisibilityManager's
    #! mode signal to every relationship member. Neither independent snapshot
    #! may receive that change.
    old_mode = recipe.view(original_id).visibility_mode
    new_mode = "dodgeActive" if old_mode == "alwaysVisible" else "alwaysVisible"
    recipe.call_or_fail(
        "could not drive source relationship visibility synchronization",
        "setViewVisibilityMode",
        "us",
        str(original_id),
        new_mode,
    )

    sync_observed = False
    for _ in range(80):
        try:
            modes = {v.containment_id: v.visibility_mode for v in recipe.views()}
        except recipe.DbusUnavailableError:
            modes = {}
        if modes.get(original_id) == new_mode and modes.get(replica_id) == new_mode:
            sync_observed = True
            break
        time.sleep(_POLL_INTERVAL)
    if not sync_observed:
        recipe.fail("existing linked relationship did not synchronize visibility mode")
    for duplicate in (from_original, from_replica):
        if recipe.view(duplicate).visibility_mode != old_mode:
            recipe.fail(f"duplicate {duplicate} retained source visibility synchronization")

    #! A clean stop flushes the lazy layout file. Both duplicates must persist
    #! as single-screen originals. The pre-existing linked pair must remain
    #! linked - including the replica's containment record itself (D283).
    if not recipe.dock_stop():
        recipe.fail("could not stop the dock for persistence checks")
    layout_text = Path(layout).read_text(errors="replace")
    for duplicate in (from_original, from_replica):
        if _containment_entry(layout_text, duplicate, "isClonedFrom") != "-1":
            recipe.fail(f"duplicate {duplicate} persisted a clone source")
        if _containment_entry(layout_text, duplicate, "screensGroup") != "0":
            recipe.fail(f"duplicate {duplicate} persisted a multi-output replica policy")
    if _containment_entry(layout_text, original_id, "screensGroup") != "1":
        recipe.fail(
            f"existing linked original {original_id} lost AllScreensGroup during duplication"
        )
    if _containment_entry(layout_text, replica_id, "isClonedFrom") != str(original_id):
        recipe.fail(f"existing replica {replica_id} lost source {original_id} during duplication")

    if not recipe.dock_start():
        recipe.fail("dock did not restart for duplicate persistence proof")
    reloaded = _views_settled()
    expected = {original_id, replica_id, from_original, from_replica}
    actual = {v.containment_id for v in reloaded}
    if actual != expected:
        recipe.fail(
            "independent duplicates did not survive reload: reload changed view "
            f"membership: expected {sorted(expected)}, got {sorted(actual)}"
        )
    by_id = {v.containment_id: v for v in reloaded}
    for duplicate in (from_original, from_replica):
        if by_id[duplicate].is_cloned or by_id[duplicate].is_cloned_from != -1:
            recipe.fail(f"duplicate {duplicate} rejoined a clone graph after reload")

    print(
        f"duplicate dock: original {original_id} and replica {replica_id} each produced "
        f"one fresh independent dock ({from_original}, {from_replica}); source sync "
        "bypassed both and reload preserved all identities"
    )


if __name__ == "__main__":
    recipe.run(main)
