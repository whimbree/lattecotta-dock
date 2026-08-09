#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""Create Linked Dock dual-output acceptance. This drives the public D-Bus actions
and reads the atomic dock-system snapshot. It covers occupied-edge creation, a
separated portrait output, direct-root lineage, local placement, visibility and
edit ownership, applet synchronization, independent Duplicate, and persistence
reload.

Ported from tests/e2e/create-linked-dock.sh to latte_harness.recipe +
latte_harness.task_reorder + latte_harness.applet_reorder (BP-3, the bash-to-python
migration's R12 dual-output recipe batch); it is the last consumer of
task-reorder-lib.sh and applet-reorder-driver.sh, retired with this port.
dockSystemData / viewsData / viewAppletsData / appletConfigData carry many fields
the typed models do not (relationship, linkedDockIds, runtimeViewId, per-view
objects, applet config), so they are read as raw JSON via recipe.read_json - the
same boundary the bash python one-liners used; a polling waiter reads a refused
reply (the pollable DbusUnavailableError) or a predicate lookup miss as a
non-match, exactly like the bash predicate exiting non-zero. The coarse
createLinkedView / setViewPlacement / addApplet / duplicateView
/ reloadView actions stay busctl calls that fail loudly on a D-Bus error, matching
the bash `e2e_call ... || e2e_fail`. KWin owns the output topology, driven with raw
kscreen-doctor exactly as the bash did (this recipe never routed through the
multi-output mutation gate).
"""

import contextlib
import json
import os
import subprocess
import sys
import time
from collections.abc import Callable
from pathlib import Path
from typing import Any

from latte_harness import applet_reorder, recipe, task_reorder
from latte_harness.proc import install_conventional_signal_exits

_State = dict[str, Any]
_Views = list[dict[str, Any]]


# ---- coarse actions and config reads ---------------------------------------


def _kscreen(*args: str) -> bool:
    """kscreen-doctor <setters>; True on success (the bash `kscreen-doctor ... >/dev/null`)."""
    return (
        subprocess.run(["kscreen-doctor", *args], stdout=subprocess.DEVNULL, check=False).returncode
        == 0
    )


def _kread(*args: str) -> str:
    """kreadconfig6 stdout, stripped (the bash `$(kreadconfig6 ...)`)."""
    return subprocess.run(
        ["kreadconfig6", *args], capture_output=True, text=True, check=False
    ).stdout.strip()


# ---- polling waiters -------------------------------------------------------


def _wait_for_snapshot(predicate: Callable[[_State], bool], label: str) -> _State:
    """wait_for_snapshot: poll dockSystemData 120x0.25s until ``predicate`` holds;
    return the matching snapshot. A refused reply (DbusUnavailableError) or a
    lookup miss counts as a non-match, exactly like the bash predicate exiting
    non-zero. On timeout, print the last attempt and fail with ``label``."""
    last_reply = "<no reply>"
    for _ in range(120):
        try:
            snapshot = recipe.read_json("dockSystemData")
        except recipe.DbusUnavailableError as exc:
            last_reply = f"<{exc}>"
            time.sleep(0.25)
            continue
        last_reply = json.dumps(snapshot)
        with contextlib.suppress(KeyError, StopIteration, TypeError, ValueError):
            if predicate(snapshot):
                return snapshot
        time.sleep(0.25)
    print(f"last dockSystemData: {last_reply}", file=sys.stderr, flush=True)
    recipe.fail(label)


def _wait_for_views_data(predicate: Callable[[_Views], bool], label: str) -> _Views:
    """wait_for_views_data: poll viewsData 120x0.25s until ``predicate`` holds."""
    last_reply = "<no reply>"
    for _ in range(120):
        try:
            views = recipe.read_json("viewsData")
        except recipe.DbusUnavailableError as exc:
            last_reply = f"<{exc}>"
            time.sleep(0.25)
            continue
        last_reply = json.dumps(views)
        with contextlib.suppress(KeyError, StopIteration, TypeError, ValueError):
            if predicate(views):
                return views
        time.sleep(0.25)
    print(f"last viewsData: {last_reply}", file=sys.stderr, flush=True)
    recipe.fail(label)


def _wait_for_topology() -> _State | None:
    """wait_for_topology: a portrait secondary with a horizontal gap and vertical
    offset, separated from the primary. Returns the screensData match, or None on
    timeout (the bash return 1)."""
    for _ in range(120):
        with contextlib.suppress(
            recipe.DbusUnavailableError, KeyError, StopIteration, TypeError, ValueError
        ):
            screens = recipe.read_json("screensData")
            active = [s for s in screens if s["isActive"]]
            if len(active) == 2:
                primary = next(s for s in active if s["isPrimary"])
                secondary = next(s for s in active if not s["isPrimary"])
                px, py, pw, ph = primary["geometry"]
                sx, sy, sw, sh = secondary["geometry"]
                separated = sx > px + pw or px > sx + sw or sy > py + ph or py > sy + sh
                if sw < sh and separated and sy != py:
                    return screens
        time.sleep(0.25)
    return None


def _wait_for_active_output_count(expected: int) -> bool:
    """wait_for_active_output_count: poll screensData until ``expected`` outputs are
    active. Returns True on match, False on timeout (the bash return 1)."""
    for _ in range(120):
        with contextlib.suppress(recipe.DbusUnavailableError, KeyError, TypeError, ValueError):
            active = [s for s in recipe.read_json("screensData") if s["isActive"]]
            if len(active) == expected:
                return True
        time.sleep(0.25)
    return False


# ---- applet / task config readbacks ----------------------------------------


def _applets(view: int) -> list[dict[str, Any]]:
    return recipe.read_json("viewAppletsData", "u", str(view))


def _applet_config(view: int, applet: int) -> dict[str, Any]:
    return recipe.read_json("appletConfigData", "uu", str(view), str(applet))


def _view_plugins(view: int) -> str:
    """view_plugins: the sorted plugin multiset, space-joined."""
    return " ".join(sorted(a["plugin"] for a in _applets(view)))


def _view_applet_ids(view: int) -> str:
    """view_applet_ids: the applet instance ids, space-joined (model order)."""
    return " ".join(str(a["id"]) for a in _applets(view))


def _view_plugin_order(view: int) -> str:
    """view_plugin_order: the plugin sequence in model order, space-joined."""
    return " ".join(a["plugin"] for a in _applets(view))


def _tasks_applet_id(view: int) -> int:
    """tasks_applet_id: the single Latte Tasks applet id, or a loud refusal."""
    ids = [a["id"] for a in _applets(view) if a["plugin"] == "org.kde.latte.plasmoid"]
    if len(ids) != 1:
        raise recipe.RecipeError(f"expected one Latte Tasks applet, got {ids!r}")
    return ids[0]


def _tasks_launchers_config(view: int) -> str:
    """tasks_launchers_config: the tasks applet's launchers* config keys, canonical JSON."""
    applet = _tasks_applet_id(view)
    config = _applet_config(view, applet)["config"]
    values = {key: value for key, value in config.items() if key.startswith("launchers")}
    return json.dumps(values, sort_keys=True, separators=(",", ":"))


def _tasks_local_length(view: int) -> str:
    """tasks_local_length: the tasks applet's local length key (or "absent")."""
    applet = _tasks_applet_id(view)
    config = _applet_config(view, applet)["config"]
    return json.dumps(config.get("length", "absent"), separators=(",", ":"))


def _stable_tasks_local_length(view: int) -> str | None:
    """stable_tasks_local_length: the local length once four consecutive reads agree,
    or None on timeout (the bash return 1)."""
    previous: str | None = None
    current: str | None = None
    stable = 0
    for _ in range(80):
        try:
            current = _tasks_local_length(view)
        except recipe.RecipeError:
            time.sleep(0.25)
            continue
        if current == previous:
            stable += 1
        else:
            previous = current
            stable = 1
        if stable >= 4:
            return current
        time.sleep(0.25)
    return None


def _view_content_fingerprint(view: int) -> str:
    """view_content_fingerprint: one canonical JSON line per applet (plugin + config
    with the local length dropped), model order, newline-joined."""
    lines: list[str] = []
    for applet in _applets(view):
        config = _applet_config(view, applet["id"])
        stripped = dict(config["config"])
        stripped.pop("length", None)
        lines.append(
            json.dumps(
                {"plugin": applet["plugin"], "config": stripped},
                sort_keys=True,
                separators=(",", ":"),
            )
        )
    return "\n".join(lines)


# ---- the scenario ----------------------------------------------------------


def _scenario(layout: str) -> None:
    active = [s for s in recipe.read_json("screensData") if s["isActive"]]
    primary_list = [s for s in active if s["isPrimary"]]
    secondary_list = [s for s in active if not s["isPrimary"]]
    if len(primary_list) != 1 or len(secondary_list) != 1:
        recipe.fail("expected one primary and one secondary output")
    primary_id = primary_list[0]["id"]
    secondary_id = secondary_list[0]["id"]
    secondary_name = secondary_list[0]["name"]

    seed_views = recipe.read_json("dockSystemData")["views"]
    if len(seed_views) != 1 or seed_views[0]["relationship"] != "independent":
        recipe.fail("could not identify the independent seed dock")
    root_id = seed_views[0]["persistentDockId"]

    # KWin owns output topology. Drive a portrait secondary with a horizontal gap
    # and vertical offset, then require Latte to consume that exact QScreen geometry.
    if not _kscreen(
        f"output.{secondary_name}.rotation.left",
        f"output.{secondary_name}.position.2300,180",
    ):
        recipe.fail("could not configure the separated portrait output")

    if _wait_for_topology() is None:
        recipe.fail("Latte did not observe the separated portrait topology")

    secondary_screen = next(
        s for s in recipe.read_json("screensData") if s["id"] == secondary_id and s["isActive"]
    )
    secondary_geometry = ",".join(str(v) for v in secondary_screen["geometry"])

    # Create on the root's occupied primary/bottom edge. This is supported
    # stacking membership, not a free-edge fallback.
    recipe.call_or_fail(
        "same-edge createLinkedView call failed",
        "createLinkedView",
        "uii",
        str(root_id),
        str(primary_id),
        "4",
    )

    def same_edge_pred(state: _State) -> bool:
        views = state["views"]
        root = next((v for v in views if v["persistentDockId"] == root_id), None)
        members = [v for v in views if v["relationship"] == "linkedMember"]
        return bool(
            root
            and root["relationship"] == "linkedRoot"
            and len(members) == 1
            and members[0]["originalDockId"] == root_id
            and members[0]["linkPlacement"] == "explicitTarget"
            and members[0]["screenId"] == primary_id
            and members[0]["edge"] == "bottom"
        )

    same_edge_state = _wait_for_snapshot(
        same_edge_pred, "linked dock did not appear on the occupied primary/bottom edge"
    )
    same_edge_id = next(
        v["persistentDockId"]
        for v in same_edge_state["views"]
        if v["relationship"] == "linkedMember"
    )

    # Create from a member. The new relationship must still point directly to the
    # root, and its geometry must come from the selected non-adjacent output.
    recipe.call_or_fail(
        "cross-output createLinkedView call failed",
        "createLinkedView",
        "uii",
        str(same_edge_id),
        str(secondary_id),
        "5",
    )

    def remote_pred(state: _State) -> bool:
        members = [v for v in state["views"] if v["relationship"] == "linkedMember"]
        remote = [v for v in members if v["persistentDockId"] != same_edge_id]
        return (
            len(members) == 2
            and len(remote) == 1
            and remote[0]["originalDockId"] == root_id
            and remote[0]["logicalDockId"] == root_id
            and remote[0]["screenId"] == secondary_id
            and remote[0]["edge"] == "left"
            and ",".join(str(x) for x in remote[0]["screenGeometry"]) == secondary_geometry
        )

    remote_state = _wait_for_snapshot(
        remote_pred,
        "linked member did not retain direct-root lineage and target-output geometry",
    )
    remote_id = next(
        v["persistentDockId"]
        for v in remote_state["views"]
        if v["relationship"] == "linkedMember" and v["persistentDockId"] != same_edge_id
    )

    def metrics_pred(state: _State) -> bool:
        v = next(v for v in state["views"] if v["persistentDockId"] == remote_id)
        fields = (
            v["configuredIconSize"],
            v["effectiveIconSize"],
            v["availablePrimaryLength"],
        )
        return all(value is not None for value in fields)

    metrics_state = _wait_for_snapshot(
        metrics_pred, "linked member sizing authorities did not become live"
    )
    metrics_view = next(v for v in metrics_state["views"] if v["persistentDockId"] == remote_id)
    before_remote_metrics = " ".join(
        str(value)
        for value in (
            metrics_view["configuredIconSize"],
            metrics_view["effectiveIconSize"],
            metrics_view["availablePrimaryLength"],
        )
    )
    before_remote_local_length = _stable_tasks_local_length(remote_id)
    if before_remote_local_length is None:
        recipe.fail("remote member applet length did not settle before the alignment test")

    # Exact cross-dock sizing reproducer: changing the root bottom dock to start
    # alignment must not alter the separate vertical member's sizing inputs.
    recipe.call_or_fail(
        "root alignment change failed",
        "setViewPlacement",
        "uiii",
        str(root_id),
        str(primary_id),
        "4",
        "1",
    )

    def root_align_pred(state: _State) -> bool:
        views = {v["persistentDockId"]: v for v in state["views"]}
        root, remote = views[root_id], views[remote_id]
        metrics = (
            remote["configuredIconSize"],
            remote["effectiveIconSize"],
            remote["availablePrimaryLength"],
        )
        return (
            root["alignment"] == "left"
            and root["edge"] == "bottom"
            and remote["edge"] == "left"
            and " ".join(str(v) for v in metrics) == before_remote_metrics
        )

    _wait_for_snapshot(
        root_align_pred,
        "root alignment changed the remote linked member sizing or placement",
    )
    if _stable_tasks_local_length(remote_id) != before_remote_local_length:
        recipe.fail("root alignment copied applet-local length into the remote linked member")

    # Relocate the explicit member through both axes and outputs. The semantic end
    # alignment (2) normalizes to bottom on a vertical edge, while the root and the
    # other explicit member remain in place.
    recipe.call_or_fail(
        "explicit member relocation to primary/right failed",
        "setViewPlacement",
        "uiii",
        str(remote_id),
        str(primary_id),
        "6",
        "2",
    )

    def member_right_pred(state: _State) -> bool:
        views = {v["persistentDockId"]: v for v in state["views"]}
        remote = views[remote_id]
        return (
            remote["screenId"] == primary_id
            and remote["edge"] == "right"
            and remote["orientation"] == "vertical"
            and remote["alignment"] == "bottom"
            and views[root_id]["edge"] == "bottom"
            and views[same_edge_id]["edge"] == "bottom"
        )

    _wait_for_snapshot(member_right_pred, "explicit member relocation leaked into another dock")

    recipe.call_or_fail(
        "explicit member relocation back to secondary/left failed",
        "setViewPlacement",
        "uiii",
        str(remote_id),
        str(secondary_id),
        "5",
        "1",
    )

    def member_back_pred(state: _State) -> bool:
        v = next(v for v in state["views"] if v["persistentDockId"] == remote_id)
        return v["screenId"] == secondary_id and v["edge"] == "left" and v["alignment"] == "top"

    _wait_for_snapshot(member_back_pred, "explicit member did not return to the portrait output")

    # Visibility and edit presentation are local to explicit members.
    old_member_mode = next(
        v for v in recipe.read_json("dockSystemData")["views"] if v["persistentDockId"] == remote_id
    )["visibilityMode"]
    new_root_mode = "autoHide"
    recipe.call_or_fail(
        "root visibility change failed",
        "setViewVisibilityMode",
        "us",
        str(root_id),
        new_root_mode,
    )

    def root_visibility_pred(state: _State) -> bool:
        views = {v["persistentDockId"]: v for v in state["views"]}
        return (
            views[root_id]["visibilityMode"] == new_root_mode
            and views[remote_id]["visibilityMode"] == old_member_mode
            and views[same_edge_id]["visibilityMode"] == old_member_mode
        )

    _wait_for_snapshot(
        root_visibility_pred, "root visibility change leaked into an explicit member"
    )

    def root_hidden_pred(views: _Views) -> bool:
        root = next(v for v in views if v["containmentId"] == root_id)
        return root["isHidden"]

    _wait_for_views_data(root_hidden_pred, "Auto Hide root did not hide before peer editing")

    recipe.call_or_fail(
        "could not enter the linked member edit presentation",
        "setViewEditMode",
        "ub",
        str(remote_id),
        "true",
    )

    def edit_highlight_pred(views: _Views) -> bool:
        editing = {v["containmentId"] for v in views if v["editMode"]}
        highlighted = {v["containmentId"] for v in views if v["linkedEditHighlight"]}
        visible_highlights = all(not v["isHidden"] for v in views if v["linkedEditHighlight"])
        passive = all(not v["inConfigureAppletsMode"] for v in views if v["linkedEditHighlight"])
        keyboard_clear = all(not v["keyboardNavigation"] for v in views)
        return (
            editing == {remote_id}
            and highlighted == {root_id, same_edge_id}
            and visible_highlights
            and passive
            and keyboard_clear
        )

    _wait_for_views_data(
        edit_highlight_pred,
        "linked peers did not expose a visible passive edit highlight",
    )

    def edit_ownership_pred(state: _State) -> bool:
        views = {v["persistentDockId"]: v for v in state["views"]}
        active_view = views[remote_id]
        peers = [views[root_id], views[same_edge_id]]
        return (
            active_view["editMode"]
            and active_view["settingsWindowShown"]
            and active_view["objects"]["configWindow"] is not None
            and all(
                not peer["editMode"]
                and not peer["settingsWindowShown"]
                and peer["objects"]["configWindow"] is None
                for peer in peers
            )
        )

    _wait_for_snapshot(
        edit_ownership_pred,
        "passive linked peers acquired edit or configuration-window ownership",
    )
    recipe.call_or_fail(
        "could not leave the linked member edit presentation",
        "setViewEditMode",
        "ub",
        str(remote_id),
        "false",
    )

    def edit_closed_pred(views: _Views) -> bool:
        root = next(v for v in views if v["containmentId"] == root_id)
        return (
            not any(v["editMode"] or v["linkedEditHighlight"] for v in views) and root["isHidden"]
        )

    _wait_for_views_data(
        edit_closed_pred, "linked member edit mode or peer highlight did not close"
    )

    # Temporarily expose the occupied-edge member on the primary top edge while
    # pointer-driven mutations run. Same-edge overlap remains supported and is
    # restored below; a covered view is not a valid pointer target.
    recipe.call_or_fail(
        "could not expose the linked member for pointer-driven mutations",
        "setViewPlacement",
        "uiii",
        str(same_edge_id),
        str(primary_id),
        "3",
        "0",
    )

    def expose_pred(state: _State) -> bool:
        views = {v["persistentDockId"]: v for v in state["views"]}
        return (
            views[same_edge_id]["edge"] == "top"
            and views[same_edge_id]["alignment"] == "center"
            and views[root_id]["edge"] == "bottom"
        )

    _wait_for_snapshot(
        expose_pred, "linked member did not settle on its temporary pointer-test edge"
    )

    # A Tasks launcher reorder is an applet-CONFIG mutation, not a containment
    # applet reorder. Drive it from an editable linked member and require the live
    # launchers config and rendered launcher order to converge across the group.
    task_apps = task_reorder.taskdrag_order(same_edge_id).split()
    if len(task_apps) < 2:
        recipe.fail("linked-member config test needs at least two launchers")
    before_task_order = task_reorder.taskdrag_launcher_order(same_edge_id)
    try:
        before_launchers_config = _tasks_launchers_config(same_edge_id)
    except recipe.RecipeError:
        recipe.fail("could not read linked-member launcher config")
    after_task_order = before_task_order
    for _ in range(4):
        task_reorder.taskdrag_reorder(same_edge_id, task_apps[0], task_apps[1])
        after_task_order = task_reorder.taskdrag_launcher_order(same_edge_id)
        if after_task_order != before_task_order:
            break
    if after_task_order == before_task_order:
        recipe.fail("linked-member task reorder did not change launcher order")

    config_sync = False
    for _ in range(80):
        try:
            root_task_order = task_reorder.taskdrag_launcher_order(root_id)
            remote_task_order = task_reorder.taskdrag_launcher_order(remote_id)
            root_launchers_config = _tasks_launchers_config(root_id)
            member_launchers_config = _tasks_launchers_config(same_edge_id)
            remote_launchers_config = _tasks_launchers_config(remote_id)
        except recipe.RecipeError:
            time.sleep(0.25)
            continue
        if (
            root_task_order == after_task_order
            and remote_task_order == after_task_order
            and member_launchers_config != before_launchers_config
            and root_launchers_config == member_launchers_config
            and remote_launchers_config == member_launchers_config
        ):
            config_sync = True
            break
        time.sleep(0.25)
    if not config_sync:
        recipe.fail("member-originated applet config did not converge across the linked group")
    if _stable_tasks_local_length(remote_id) != before_remote_local_length:
        recipe.fail(
            "shared launcher configuration overwrote the remote member's local applet length"
        )

    # Applet content remains linked. Add one resolvable plugin from a MEMBER and wait
    # until every member has the same plugin multiset with disjoint instance ids.
    before_plugin_count = len(_view_plugins(root_id).split())
    recipe.call_or_fail(
        "could not add the installed plasmoid from a linked member",
        "addApplet",
        "us",
        str(remote_id),
        "org.kde.plasma.minimizeall",
    )
    applet_sync = False
    root_plugins = same_plugins = remote_plugins = ""
    for _ in range(120):
        root_plugins = _view_plugins(root_id)
        same_plugins = _view_plugins(same_edge_id)
        remote_plugins = _view_plugins(remote_id)
        if (
            len(root_plugins.split()) == before_plugin_count + 1
            and root_plugins == same_plugins
            and root_plugins == remote_plugins
        ):
            applet_sync = True
            break
        time.sleep(0.5)
    if not applet_sync:
        print(
            f"root plugins:   {root_plugins}\nsame plugins:   {same_plugins}\n"
            f"remote plugins: {remote_plugins}",
            file=sys.stderr,
            flush=True,
        )
        recipe.fail("applet addition did not synchronize across linked members")

    all_ids_text = " ".join(_view_applet_ids(cid) for cid in (root_id, same_edge_id, remote_id))
    ids = [int(value) for value in all_ids_text.split()]
    if len(ids) != len(set(ids)):
        recipe.fail("linked members reused mutable applet identities")

    # Reorder containment applets from a member. Instance ids differ by design, so
    # the relationship coordinator translates the member order to root ids and the
    # observable plugin sequence must then agree everywhere.
    before_applet_order = _view_plugin_order(same_edge_id)
    if applet_reorder.applet_reorder_attempt(same_edge_id, "commit", 0, 1) != 0:
        recipe.fail("linked-member applet reorder did not commit")
    after_applet_order = _view_plugin_order(same_edge_id)
    if after_applet_order == before_applet_order:
        recipe.fail("linked-member applet reorder left the plugin sequence unchanged")
    order_sync = False
    for _ in range(80):
        if (
            _view_plugin_order(root_id) == after_applet_order
            and _view_plugin_order(remote_id) == after_applet_order
        ):
            order_sync = True
            break
        time.sleep(0.25)
    if not order_sync:
        print(
            f"expected plugin order: {after_applet_order}\n"
            f"root plugin order: {_view_plugin_order(root_id)}\n"
            f"remote plugin order: {_view_plugin_order(remote_id)}",
            file=sys.stderr,
            flush=True,
        )
        recipe.fail("member-originated applet order did not converge across the linked group")

    # Disconnect the explicit member's output while the root stays live, mutate
    # the root, then reconnect. The persistent member must remain present on disk
    # and its fresh runtime must reconcile exact applet structure, order, and
    # configuration before it can publish a settled state.
    if not _kscreen(f"output.{secondary_name}.disable"):
        recipe.fail("could not disable the explicit member output")
    if not _wait_for_active_output_count(1):
        recipe.fail("Latte did not observe the explicit member output disconnect")

    def member_offline_pred(state: _State) -> bool:
        return {v["persistentDockId"] for v in state["views"]} == {
            root_id,
            same_edge_id,
        }

    _wait_for_snapshot(
        member_offline_pred, "the disconnected-output member retained a runtime view"
    )
    if _kread(
        "--file",
        layout,
        "--group",
        "Containments",
        "--group",
        str(remote_id),
        "--key",
        "isClonedFrom",
        "--default",
        "-1",
    ) != str(root_id):
        recipe.fail("member output disconnect removed the persistent relationship")

    recipe.call_or_fail(
        "could not mutate linked applets while one member output was disconnected",
        "addApplet",
        "us",
        str(root_id),
        "org.kde.plasma.minimizeall",
    )
    offline_sync = False
    for _ in range(120):
        root_plugins = _view_plugins(root_id)
        same_plugins = _view_plugins(same_edge_id)
        if (
            root_plugins.split().count("org.kde.plasma.minimizeall") == 2
            and root_plugins == same_plugins
        ):
            offline_sync = True
            break
        time.sleep(0.25)
    if not offline_sync:
        recipe.fail("live linked members did not converge during the output disconnect")
    offline_content_sync = False
    offline_expected_content = ""
    same_edge_offline_content = ""
    for _ in range(160):
        offline_expected_content = _view_content_fingerprint(root_id)
        same_edge_offline_content = _view_content_fingerprint(same_edge_id)
        if same_edge_offline_content == offline_expected_content:
            offline_content_sync = True
            break
        time.sleep(0.25)
    if not offline_content_sync:
        print(
            f"root content:\n{offline_expected_content}\n"
            f"live member content:\n{same_edge_offline_content}",
            file=sys.stderr,
            flush=True,
        )
        recipe.fail("live member content differed from the root during the output disconnect")

    if not _kscreen(
        f"output.{secondary_name}.enable",
        f"output.{secondary_name}.rotation.left",
        f"output.{secondary_name}.position.2300,180",
    ):
        recipe.fail("could not reconnect the explicit member output")
    if _wait_for_topology() is None:
        recipe.fail("Latte did not observe the reconnected portrait output")

    def member_return_pred(state: _State) -> bool:
        views = {v["persistentDockId"]: v for v in state["views"]}
        remote = views.get(remote_id)
        return (
            set(views) == {root_id, same_edge_id, remote_id}
            and remote is not None
            and remote["originalDockId"] == root_id
            and remote["screenId"] == secondary_id
            and remote["edge"] == "left"
        )

    _wait_for_snapshot(
        member_return_pred,
        "explicit member did not return after its output reconnected",
    )
    reconnect_sync = False
    for _ in range(160):
        if _view_content_fingerprint(remote_id) == offline_expected_content:
            reconnect_sync = True
            break
        time.sleep(0.25)
    if not reconnect_sync:
        recipe.fail("reconnected member did not reconcile exact root applet content")
    if _stable_tasks_local_length(remote_id) != before_remote_local_length:
        recipe.fail("output reconnection overwrote the remote member's local applet length")

    # Return the member to the already occupied edge. The relationship and content
    # operations above must not rewrite placement ownership.
    recipe.call_or_fail(
        "could not restore the linked member to the occupied edge",
        "setViewPlacement",
        "uiii",
        str(same_edge_id),
        str(primary_id),
        "4",
        "0",
    )

    def member_occupied_pred(state: _State) -> bool:
        views = {v["persistentDockId"]: v for v in state["views"]}
        return (
            views[same_edge_id]["edge"] == "bottom"
            and views[same_edge_id]["alignment"] == "center"
            and views[root_id]["edge"] == "bottom"
        )

    _wait_for_snapshot(member_occupied_pred, "linked member did not return to the occupied edge")

    # Duplicate the explicit member. The result must be independent and must not be
    # added to the root's linkedDockIds.
    before_ids = {v["persistentDockId"] for v in recipe.read_json("dockSystemData")["views"]}
    recipe.call_or_fail(
        "Duplicate Dock failed from an explicit linked member",
        "duplicateView",
        "u",
        str(remote_id),
    )

    def duplicate_pred(state: _State) -> bool:
        views = state["views"]
        created = [v for v in views if v["persistentDockId"] not in before_ids]
        root = next(v for v in views if v["persistentDockId"] == root_id)
        return (
            len(created) == 1
            and created[0]["relationship"] == "independent"
            and created[0]["originalDockId"] is None
            and created[0]["linkPlacement"] is None
            and created[0]["persistentDockId"] not in root["linkedDockIds"]
        )

    duplicate_state = _wait_for_snapshot(
        duplicate_pred, "Duplicate Dock retained the linked relationship"
    )
    duplicate_id = next(
        v["persistentDockId"]
        for v in duplicate_state["views"]
        if v["persistentDockId"] not in before_ids
    )

    # Keep the relationship in edit mode across root runtime recreation. The
    # independent duplicate has no durable relation to infer, so a cue or keyboard
    # mode there would expose relationship leakage before or after replacement.
    recipe.call_or_fail(
        "could not enter edit mode for runtime recreation",
        "setViewEditMode",
        "ub",
        str(remote_id),
        "true",
    )

    def pre_recreate_pred(views: _Views) -> bool:
        editing = {v["containmentId"] for v in views if v["editMode"]}
        highlighted = {v["containmentId"] for v in views if v["linkedEditHighlight"]}
        duplicate = next(v for v in views if v["containmentId"] == duplicate_id)
        return (
            editing == {remote_id}
            and highlighted == {root_id, same_edge_id}
            and not duplicate["linkedEditHighlight"]
            and not duplicate["editMode"]
            and all(not v["keyboardNavigation"] for v in views)
        )

    _wait_for_views_data(
        pre_recreate_pred,
        "pre-recreation edit state leaked into the independent duplicate or keyboard mode",
    )

    # Recreate the root runtime through the same path used when an installed
    # custom indicator changes. Every linked runtime must rotate to the new root
    # generation while preserving the active member and exact passive-peer set.
    # The independent duplicate must remain untouched.
    before_reload_runtime = {
        v["persistentDockId"]: v["runtimeViewId"]
        for v in recipe.read_json("dockSystemData")["views"]
    }
    before_reload_content = _view_content_fingerprint(root_id)
    recipe.call_or_fail(
        "could not request root runtime recreation", "reloadView", "u", str(root_id)
    )

    def recreated_pred(state: _State) -> bool:
        views = {v["persistentDockId"]: v for v in state["views"]}
        group = [root_id, same_edge_id, remote_id]
        editing = {identity for identity, view in views.items() if view["editMode"]}
        active_view = views.get(remote_id)
        passive = [views.get(root_id), views.get(same_edge_id), views.get(duplicate_id)]
        return (
            set(views) == {root_id, same_edge_id, remote_id, duplicate_id}
            and all(
                views[identity]["runtimeViewId"] != before_reload_runtime[identity]
                for identity in group
            )
            and views[duplicate_id]["runtimeViewId"] == before_reload_runtime[duplicate_id]
            and all(
                views[identity]["originalDockId"] == root_id
                for identity in [same_edge_id, remote_id]
            )
            and editing == {remote_id}
            and active_view is not None
            and active_view["settingsWindowShown"]
            and active_view["objects"]["configWindow"] is not None
            and all(
                view is not None
                and not view["settingsWindowShown"]
                and view["objects"]["configWindow"] is None
                for view in passive
            )
        )

    _wait_for_snapshot(
        recreated_pred,
        "root recreation did not replace and rebind the whole linked runtime group",
    )

    def post_recreate_pred(views: _Views) -> bool:
        editing = {v["containmentId"] for v in views if v["editMode"]}
        highlighted = {v["containmentId"] for v in views if v["linkedEditHighlight"]}
        visible_highlights = all(not v["isHidden"] for v in views if v["linkedEditHighlight"])
        return (
            editing == {remote_id}
            and highlighted == {root_id, same_edge_id}
            and visible_highlights
            and all(not v["keyboardNavigation"] for v in views)
        )

    _wait_for_views_data(
        post_recreate_pred,
        "replacement runtimes lost edit ownership, passive peers, or focus isolation",
    )
    recipe.call_or_fail(
        "could not leave edit mode after runtime recreation",
        "setViewEditMode",
        "ub",
        str(remote_id),
        "false",
    )

    def post_exit_pred(views: _Views) -> bool:
        root = next(v for v in views if v["containmentId"] == root_id)
        return (
            not any(
                v["editMode"] or v["linkedEditHighlight"] or v["keyboardNavigation"] for v in views
            )
            and root["isHidden"]
        )

    _wait_for_views_data(
        post_exit_pred,
        "post-recreation edit exit retained a cue, focus mode, or hiding blocker",
    )
    recreate_sync = False
    for _ in range(160):
        if (
            _view_content_fingerprint(root_id) == before_reload_content
            and _view_content_fingerprint(same_edge_id) == before_reload_content
            and _view_content_fingerprint(remote_id) == before_reload_content
        ):
            recreate_sync = True
            break
        time.sleep(0.25)
    if not recreate_sync:
        recipe.fail("linked content changed or failed to converge after root recreation")
    if _stable_tasks_local_length(remote_id) != before_remote_local_length:
        recipe.fail("root runtime recreation overwrote the remote member's local applet length")

    # Pin the unrelated duplicate to the primary output, move the root to the
    # secondary output, then disconnect the root output. All linked runtimes must
    # park together while every persistent containment survives. Reconnection
    # creates a fresh root generation first and rebinds both explicit members.
    recipe.call_or_fail(
        "could not pin the independent duplicate to the primary output",
        "setViewPlacement",
        "uiii",
        str(duplicate_id),
        str(primary_id),
        "3",
        "0",
    )
    recipe.call_or_fail(
        "could not move the remote member off the root test output",
        "setViewPlacement",
        "uiii",
        str(remote_id),
        str(primary_id),
        "6",
        "2",
    )
    recipe.call_or_fail(
        "could not pin the linked root to the secondary output",
        "setViewPlacement",
        "uiii",
        str(root_id),
        str(secondary_id),
        "4",
        "1",
    )

    def root_disconnect_ready_pred(state: _State) -> bool:
        views = {v["persistentDockId"]: v for v in state["views"]}
        return (
            views[root_id]["screenId"] == secondary_id
            and views[remote_id]["screenId"] == primary_id
            and views[same_edge_id]["screenId"] == primary_id
            and views[duplicate_id]["screenId"] == primary_id
        )

    root_disconnect_ready = _wait_for_snapshot(
        root_disconnect_ready_pred,
        "views did not settle before the root-output disconnect",
    )
    before_root_disconnect_runtime = {
        v["persistentDockId"]: v["runtimeViewId"] for v in root_disconnect_ready["views"]
    }

    if not _kscreen(f"output.{secondary_name}.disable"):
        recipe.fail("could not disable the linked root output")
    if not _wait_for_active_output_count(1):
        recipe.fail("Latte did not observe the linked root output disconnect")

    def root_offline_pred(state: _State) -> bool:
        views = state["views"]
        return len(views) == 1 and views[0]["persistentDockId"] == duplicate_id

    _wait_for_snapshot(
        root_offline_pred, "linked member runtime survived without its root coordinator"
    )
    layout_lines = Path(layout).read_text(errors="replace").splitlines()
    for cid in (root_id, same_edge_id, remote_id):
        if f"[Containments][{cid}]" not in layout_lines:
            recipe.fail(f"root-output disconnect removed persistent containment {cid}")

    if not _kscreen(
        f"output.{secondary_name}.enable",
        f"output.{secondary_name}.rotation.left",
        f"output.{secondary_name}.position.2300,180",
    ):
        recipe.fail("could not reconnect the linked root output")
    if _wait_for_topology() is None:
        recipe.fail("Latte did not observe the reconnected root output")

    def root_reconnect_pred(state: _State) -> bool:
        views = {v["persistentDockId"]: v for v in state["views"]}
        group = [root_id, same_edge_id, remote_id]
        return (
            set(views) == {root_id, same_edge_id, remote_id, duplicate_id}
            and all(
                views[identity]["runtimeViewId"] != before_root_disconnect_runtime[identity]
                for identity in group
            )
            and views[duplicate_id]["runtimeViewId"] == before_root_disconnect_runtime[duplicate_id]
            and all(
                views[identity]["originalDockId"] == root_id
                for identity in [same_edge_id, remote_id]
            )
        )

    _wait_for_snapshot(
        root_reconnect_pred,
        "root-output reconnect did not create and rebind a fresh relationship generation",
    )
    root_reconnect_sync = False
    for _ in range(160):
        if (
            _view_content_fingerprint(root_id) == before_reload_content
            and _view_content_fingerprint(same_edge_id) == before_reload_content
            and _view_content_fingerprint(remote_id) == before_reload_content
        ):
            root_reconnect_sync = True
            break
        time.sleep(0.25)
    if not root_reconnect_sync:
        recipe.fail("linked content failed to converge after the root output reconnected")

    # Restore the intended final placement before the process-reload assertion.
    recipe.call_or_fail(
        "could not restore the root to the primary output",
        "setViewPlacement",
        "uiii",
        str(root_id),
        str(primary_id),
        "4",
        "1",
    )
    recipe.call_or_fail(
        "could not restore the remote member to the portrait output",
        "setViewPlacement",
        "uiii",
        str(remote_id),
        str(secondary_id),
        "5",
        "1",
    )

    def final_placement_pred(state: _State) -> bool:
        views = {v["persistentDockId"]: v for v in state["views"]}
        return (
            views[root_id]["screenId"] == primary_id
            and views[root_id]["edge"] == "bottom"
            and views[remote_id]["screenId"] == secondary_id
            and views[remote_id]["edge"] == "left"
        )

    _wait_for_snapshot(
        final_placement_pred,
        "final linked placement did not settle after output lifecycle tests",
    )

    expected_ids = {root_id, same_edge_id, remote_id, duplicate_id}
    if not recipe.dock_stop():
        recipe.fail("could not stop the dock for linked relationship persistence")
    for cid in (same_edge_id, remote_id):
        if _kread(
            "--file",
            layout,
            "--group",
            "Containments",
            "--group",
            str(cid),
            "--key",
            "isClonedFrom",
            "--default",
            "-1",
        ) != str(root_id):
            recipe.fail(f"linked member {cid} lost root {root_id} on disk")
        if (
            _kread(
                "--file",
                layout,
                "--group",
                "Containments",
                "--group",
                str(cid),
                "--key",
                "linkPlacement",
                "--default",
                "-1",
            )
            != "1"
        ):
            recipe.fail(f"linked member {cid} lost explicit placement ownership on disk")
    if (
        _kread(
            "--file",
            layout,
            "--group",
            "Containments",
            "--group",
            str(duplicate_id),
            "--key",
            "isClonedFrom",
            "--default",
            "-999",
        )
        != "-1"
    ):
        recipe.fail("independent duplicate persisted a relationship root")

    if not recipe.dock_start():
        recipe.fail("dock did not restart after linked relationship persistence")

    def reloaded_pred(state: _State) -> bool:
        views = state["views"]
        actual = {v["persistentDockId"] for v in views}
        root = next((v for v in views if v["persistentDockId"] == root_id), None)
        members = [v for v in views if v["relationship"] == "linkedMember"]
        independent = next((v for v in views if v["persistentDockId"] == duplicate_id), None)
        return bool(
            actual == expected_ids
            and root
            and root["linkedDockIds"] == sorted([same_edge_id, remote_id])
            and len(members) == 2
            and all(
                v["originalDockId"] == root_id and v["linkPlacement"] == "explicitTarget"
                for v in members
            )
            and next(v for v in members if v["persistentDockId"] == same_edge_id)["screenId"]
            == primary_id
            and next(v for v in members if v["persistentDockId"] == same_edge_id)["edge"]
            == "bottom"
            and next(v for v in members if v["persistentDockId"] == remote_id)["screenId"]
            == secondary_id
            and next(v for v in members if v["persistentDockId"] == remote_id)["edge"] == "left"
            and independent
            and independent["relationship"] == "independent"
            and not any(v["editMode"] for v in views)
        )

    reloaded = _wait_for_snapshot(
        reloaded_pred,
        "persistence reload changed dock membership, lineage, or edit ownership",
    )

    views = reloaded["views"]
    runtime_ids = [v["runtimeViewId"] for v in views]
    containments = [v["persistentDockId"] for v in views]
    if len(runtime_ids) != len(set(runtime_ids)) or len(containments) != len(set(containments)):
        recipe.fail("runtime ownership was shared after reload")
    for key in (
        "view",
        "containment",
        "configuration",
        "layoutController",
        "geometryController",
        "editController",
    ):
        values = [v["objects"][key] for v in views]
        if any(value is None for value in values) or len(values) != len(set(values)):
            recipe.fail("runtime ownership was shared after reload")

    print(
        f"create linked dock: root {root_id}, occupied-edge member {same_edge_id}, "
        f"portrait-output member {remote_id}, independent duplicate {duplicate_id}; "
        "placement, sizing, edit mode, applets, and reload passed"
    )


def _restore_original_layout(layout: str, original_layout: str) -> None:
    """restore_original_layout (the bash EXIT trap): stop the dock, restore the
    pre-scenario layout, restart. Best-effort - the trap's return never changed the
    script's exit status."""
    with contextlib.suppress(Exception):
        _ = recipe.dock_stop()
    try:
        _ = Path(layout).write_bytes(Path(original_layout).read_bytes())
    except OSError:
        return
    with contextlib.suppress(Exception):
        _ = recipe.dock_start()


def main() -> None:
    install_conventional_signal_exits()
    if int(os.environ.get("E2E_OUTPUT_COUNT", "1")) != 2:
        recipe.fail("create-linked-dock needs the dual-output vehicle")

    layout = os.environ["E2E_LAYOUT"]
    original_layout = str(
        Path(os.environ["E2E_ARTIFACTS"]) / "create-linked-dock.original.layout.latte"
    )
    try:
        _ = Path(original_layout).write_bytes(Path(layout).read_bytes())
    except OSError:
        recipe.fail("could not preserve the pre-scenario layout")

    def cleanup(status: int) -> int:
        # The layout restore runs on every exit path but does not fold into the
        # status (the bash raised SystemExit(body_exit) unchanged); the body's
        # code stands.
        _restore_original_layout(layout, original_layout)
        return status

    # run_with_cleanup owns the install-signals / try-body / finally-restore shape
    # this recipe hand-rolled (the signal exits are armed above, before the layout
    # backup, so install_signal_exits=False here).
    recipe.run_with_cleanup(lambda: _scenario(layout), cleanup, install_signal_exits=False)


if __name__ == "__main__":
    main()
