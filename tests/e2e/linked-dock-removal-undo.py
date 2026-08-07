#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""Executable linked-member removal Undo. A test notification service receives
libplasma's real removal notification and emits its advertised Undo action;
no production state is forged and no log text is used as a verdict.

Ported from tests/e2e/linked-dock-removal-undo.sh to latte_harness.recipe
(BP-3, the bash-to-python migration's R10 dock-lifecycle recipe batch).
dockSystemData / viewAppletsData carry fields the typed models do not
(relationship, originalDockId, linkedDockIds, runtimeViewId, inDelete, applet
z), so they are read as raw JSON via recipe.read_json - the same boundary the
bash python one-liners used; a polling waiter reads a refused reply (the
pollable DbusUnavailableError) as a non-match, exactly like the bash predicate
exiting non-zero. The notification test service
is a real subprocess owning org.freedesktop.Notifications on the nested session
bus; its DeliveryCount / LastActions / InvokeUndo methods are driven through
gdbus, identical argv to the bash. The coarse
createLinkedView / addApplet / removeApplet / removeView actions stay busctl
calls that fail loudly on a D-Bus error, matching the bash `e2e_call ... ||
e2e_fail`.
"""

import contextlib
import json
import os
import re
import subprocess
import sys
import time
from collections.abc import Callable, Iterator
from pathlib import Path
from typing import Any

from latte_harness import recipe

_ADDED_PLUGIN = "org.kde.plasma.minimizeall"


@contextlib.contextmanager
def _notification_service(helper_path: str, log_path: str) -> Iterator[subprocess.Popen[bytes]]:
    """Launch the test notification service, yield it, and always stop it (the
    bash cleanup_notifications trap: kill then wait, errors ignored)."""
    with open(log_path, "w") as log_handle:
        helper = subprocess.Popen([helper_path], stdout=log_handle, stderr=subprocess.STDOUT)
        try:
            yield helper
        finally:
            with contextlib.suppress(OSError):
                helper.terminate()
            with contextlib.suppress(subprocess.TimeoutExpired):
                helper.wait(timeout=10)


_NOTIFICATIONS = (
    "--session",
    "--dest",
    "org.freedesktop.Notifications",
    "--object-path",
    "/org/freedesktop/Notifications",
    "--method",
)


def _latte_call(fail_message: str, *args: str) -> None:
    """`e2e_call ... >/dev/null || e2e_fail "<fail_message>"`: run a lattedock
    action, forward busctl stderr, and fail loudly on a D-Bus error."""
    result = subprocess.run(
        ["busctl", "--user", "call", "org.kde.lattedock", "/Latte", "org.kde.LatteDock", *args],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        if result.stderr:
            sys.stderr.write(result.stderr)
        recipe.fail(fail_message)


def _snapshot() -> dict[str, Any]:
    """dockSystemData as a raw JSON dict (the bash snapshot)."""
    return recipe.read_json("dockSystemData")


def _applets(view: int) -> list[dict[str, Any]]:
    """viewAppletsData for a view as raw JSON (carries the applet z field)."""
    return recipe.read_json("viewAppletsData", "u", str(view))


def _wait_for_state(predicate: Callable[[dict[str, Any]], bool], label: str) -> dict[str, Any]:
    """Poll dockSystemData 160x0.25s until ``predicate`` holds; return the
    matching snapshot. A refused reply (DbusUnavailableError) or a predicate
    lookup miss counts as a non-match, exactly like the bash python predicate
    exiting non-zero. On timeout, print the last attempt and fail with ``label``."""
    last_reply = "<no reply>"
    for _ in range(160):
        try:
            state = _snapshot()
        except recipe.DbusUnavailableError as exc:
            last_reply = f"<{exc}>"
            time.sleep(0.25)
            continue
        last_reply = json.dumps(state)
        with contextlib.suppress(KeyError, StopIteration, TypeError):
            if predicate(state):
                return state
        time.sleep(0.25)
    print(f"last dockSystemData: {last_reply}", file=sys.stderr, flush=True)
    recipe.fail(label)


def _delivery_count() -> int:
    """org.freedesktop.Notifications.DeliveryCount (the bash grep -o | tail -1)."""
    out = subprocess.run(
        ["gdbus", "call", *_NOTIFICATIONS, "org.freedesktop.Notifications.DeliveryCount"],
        capture_output=True,
        text=True,
        check=False,
    ).stdout
    numbers = re.findall(r"[0-9]+", out)
    return int(numbers[-1]) if numbers else 0


def _last_actions() -> str:
    """org.freedesktop.Notifications.LastActions raw text (errors -> empty)."""
    return subprocess.run(
        ["gdbus", "call", *_NOTIFICATIONS, "org.freedesktop.Notifications.LastActions"],
        capture_output=True,
        text=True,
        check=False,
    ).stdout


def _wait_for_undo_after(previous_count: int) -> bool:
    """Poll 80x0.1s for a new delivery whose LastActions advertise Undo (the bash
    wait_for_undo_after); print the latest actions and return False on timeout."""
    actions = ""
    for _ in range(80):
        count = _delivery_count()
        actions = _last_actions()
        if count > previous_count and "undo" in actions.lower():
            return True
        time.sleep(0.1)
    print(f"latest notification actions: {actions}", file=sys.stderr, flush=True)
    return False


def _invoke_notification_undo() -> bool:
    """Invoke the test service's advertised Undo action; False on a gdbus error
    or a non-true reply (the bash invoke_notification_undo)."""
    result = subprocess.run(
        ["gdbus", "call", *_NOTIFICATIONS, "org.freedesktop.Notifications.InvokeUndo"],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return False
    return "true" in result.stdout


def _notifications_name_owned() -> bool:
    """The test service owns org.freedesktop.Notifications on the session bus
    without triggering D-Bus activation (the bash busctl --list | grep)."""
    out = subprocess.run(
        ["busctl", "--user", "--list", "--no-legend"], capture_output=True, text=True, check=False
    ).stdout
    return any(line.startswith("org.freedesktop.Notifications ") for line in out.splitlines())


def _applet_of(view: int, plugin: str) -> dict[str, Any] | None:
    return next((a for a in _applets(view) if a["plugin"] == plugin), None)


def _applet_ready(view: int, plugin: str) -> bool:
    """Exactly one live applet of ``plugin`` with a settled visual geometry."""
    matches = [a for a in _applets(view) if a["plugin"] == plugin]
    return (
        len(matches) == 1
        and not matches[0]["inScheduledDestruction"]
        and matches[0]["z"] is not None
        and matches[0]["geometry"][2] > 0
        and matches[0]["geometry"][3] > 0
    )


def _applet_scheduled(view: int, plugin: str) -> bool:
    applet = _applet_of(view, plugin)
    return applet is not None and applet["inScheduledDestruction"]


def _applet_absent(view: int, plugin: str) -> bool:
    return not any(a["plugin"] == plugin for a in _applets(view))


def _applet_restored(view: int, plugin: str) -> bool:
    applet = _applet_of(view, plugin)
    return applet is not None and not applet["inScheduledDestruction"]


def _kread(layout: str, *args: str) -> str:
    return subprocess.run(
        ["kreadconfig6", "--file", layout, *args], capture_output=True, text=True, check=False
    ).stdout.strip()


def main() -> None:
    if int(os.environ.get("E2E_OUTPUT_COUNT", "1")) != 2:
        recipe.fail("linked-dock-removal-undo needs the dual-output vehicle")

    layout = os.environ["E2E_LAYOUT"]
    notification_helper = str(
        Path(os.environ["E2E_BUILD"]) / "bin" / "latte-test-notification-service"
    )
    notification_log = str(
        Path(os.environ["E2E_ARTIFACTS"]) / "linked-dock-removal-undo.notifications.log"
    )
    if not os.access(notification_helper, os.X_OK):
        recipe.fail(f"notification test service is missing at {notification_helper}")

    with _notification_service(notification_helper, notification_log) as helper:
        ready = False
        for _ in range(40):
            # Query the bus owner list without invoking D-Bus activation. Calling the
            # service name before this helper owns it can race an installed daemon.
            if _notifications_name_owned():
                ready = True
                break
            if helper.poll() is not None:
                recipe.fail(f"notification test service exited; see {notification_log}")
            time.sleep(0.1)
        if not ready:
            recipe.fail(
                f"notification test service did not acquire its D-Bus name; see {notification_log}"
            )

        state = _snapshot()
        screens = recipe.read_json("screensData")
        views = state["views"]
        secondary = next((s for s in screens if s["isActive"] and not s["isPrimary"]), None)
        if len(views) != 1 or views[0]["relationship"] != "independent" or secondary is None:
            recipe.fail("could not resolve the seed root and secondary output")
        root_id = views[0]["persistentDockId"]
        secondary_id = secondary["id"]

        _latte_call(
            "could not create the linked member for Undo",
            "createLinkedView",
            "uii",
            str(root_id),
            str(secondary_id),
            "5",
        )

        def member_settled(s: dict[str, Any]) -> bool:
            members = [v for v in s["views"] if v["relationship"] == "linkedMember"]
            return (
                len(s["views"]) == 2
                and len(members) == 1
                and members[0]["originalDockId"] == root_id
            )

        created = _wait_for_state(member_settled, "linked member did not settle before removal")
        member_id = next(
            v["persistentDockId"] for v in created["views"] if v["relationship"] == "linkedMember"
        )
        member_runtime_id = next(
            v["runtimeViewId"] for v in created["views"] if v["persistentDockId"] == member_id
        )

        # Root removal is intentionally refused until the complete relationship can
        # participate in one reversible Plasma transaction. This is narrower than the
        # legacy all-screens lifetime: only persistent explicit members block it.
        member_persisted = False
        for _ in range(80):
            if _kread(
                layout,
                "--group",
                "Containments",
                "--group",
                str(member_id),
                "--key",
                "isClonedFrom",
                "--default",
                "-1",
            ) == str(root_id):
                member_persisted = True
                break
            time.sleep(0.1)
        if not member_persisted:
            recipe.fail("linked member did not reach persistence before root-removal refusal")
        before_refused_root_notification = _delivery_count()
        _latte_call("linked-root removal refusal call failed", "removeView", "u", str(root_id))
        time.sleep(0.5)
        after_refusal = _snapshot()["views"]
        root = next((v for v in after_refusal if v["persistentDockId"] == root_id), None)
        member = next((v for v in after_refusal if v["persistentDockId"] == member_id), None)
        if not (
            len(after_refusal) == 2
            and root
            and member
            and root["linkedDockIds"] == [member_id]
            and member["originalDockId"] == root_id
            and not root["inDelete"]
            and not member["inDelete"]
        ):
            recipe.fail("linked-root removal was not refused atomically")
        if _delivery_count() != before_refused_root_notification:
            recipe.fail("refused linked-root removal created an Undo transaction")
        if _kread(
            layout,
            "--group",
            "Containments",
            "--group",
            str(member_id),
            "--key",
            "isClonedFrom",
            "--default",
            "-1",
        ) != str(root_id):
            recipe.fail("refused linked-root removal changed persistence")

        # Structural applet add/remove starts from the linked member. The direct root
        # owns the transaction, while both containments keep distinct applet ids and
        # mirror the real libplasma Undo state.
        _latte_call(
            "member-originated applet addition failed",
            "addApplet",
            "us",
            str(member_id),
            _ADDED_PLUGIN,
        )
        applet_addition_visible = False
        for _ in range(80):
            if _applet_ready(root_id, _ADDED_PLUGIN) and _applet_ready(member_id, _ADDED_PLUGIN):
                applet_addition_visible = True
                break
            time.sleep(0.1)
        if not applet_addition_visible:
            recipe.fail("member-originated applet addition did not reach every linked containment")

        member_applet = _applet_of(member_id, _ADDED_PLUGIN)
        if member_applet is None:
            recipe.fail("could not resolve the member-local applet id")
        member_applet_id = member_applet["id"]

        before_applet_notification = _delivery_count()
        _latte_call(
            "member-originated applet removal failed",
            "removeApplet",
            "uu",
            str(member_id),
            str(member_applet_id),
        )
        applet_removal_visible = False
        for _ in range(80):
            if _applet_scheduled(root_id, _ADDED_PLUGIN) and _applet_absent(
                member_id, _ADDED_PLUGIN
            ):
                applet_removal_visible = True
                break
            time.sleep(0.1)
        if not applet_removal_visible:
            root_applets = recipe.json_payload("viewAppletsData", "u", str(root_id))
            member_applets = recipe.json_payload("viewAppletsData", "u", str(member_id))
            print(
                f"root applets after removal: {root_applets}\n"
                f"member applets after removal: {member_applets}",
                file=sys.stderr,
                flush=True,
            )
            recipe.fail("linked applet removal Undo state was not visible in every containment")
        if not _wait_for_undo_after(before_applet_notification):
            recipe.fail("libplasma did not advertise applet-removal Undo")
        if not _invoke_notification_undo():
            recipe.fail("could not invoke applet-removal Undo")

        applet_undo_restored = False
        for _ in range(80):
            if _applet_restored(root_id, _ADDED_PLUGIN) and _applet_restored(
                member_id, _ADDED_PLUGIN
            ):
                applet_undo_restored = True
                break
            time.sleep(0.1)
        if not applet_undo_restored:
            recipe.fail("applet-removal Undo did not restore every linked applet instance")

        before_dock_notification = _delivery_count()
        _latte_call("linked-member removal request failed", "removeView", "u", str(member_id))

        def member_removed_from_root(s: dict[str, Any]) -> bool:
            root_view = next(v for v in s["views"] if v["persistentDockId"] == root_id)
            return len(s["views"]) == 1 and member_id not in root_view["linkedDockIds"]

        _wait_for_state(
            member_removed_from_root, "linked member remained active during its Undo window"
        )

        if (
            _kread(
                layout,
                "--group",
                "Containments",
                "--group",
                str(member_id),
                "--key",
                "plugin",
                "--default",
                "absent",
            )
            != "absent"
        ):
            recipe.fail("removal tombstone was not persisted before Undo")

        if not _wait_for_undo_after(before_dock_notification):
            recipe.fail("libplasma did not advertise dock-removal Undo")
        if not _invoke_notification_undo():
            recipe.fail("could not invoke the real dock-removal Undo action")

        def restored_relationship(s: dict[str, Any]) -> bool:
            root_view = next((v for v in s["views"] if v["persistentDockId"] == root_id), None)
            member_view = next((v for v in s["views"] if v["persistentDockId"] == member_id), None)
            return (
                len(s["views"]) == 2
                and root_view is not None
                and member_view is not None
                and member_view["runtimeViewId"] == member_runtime_id
                and member_view["relationship"] == "linkedMember"
                and member_view["originalDockId"] == root_id
                and root_view["linkedDockIds"] == [member_id]
            )

        _wait_for_state(
            restored_relationship,
            "Undo did not restore the same linked runtime instance and relationship",
        )

        if _kread(
            layout,
            "--group",
            "Containments",
            "--group",
            str(member_id),
            "--key",
            "isClonedFrom",
            "--default",
            "-1",
        ) != str(root_id):
            recipe.fail("Undo did not restore the linked root in persistence")
        if (
            _kread(
                layout,
                "--group",
                "Containments",
                "--group",
                str(member_id),
                "--key",
                "linkPlacement",
                "--default",
                "-1",
            )
            != "1"
        ):
            recipe.fail("Undo did not restore explicit placement ownership")

        if not recipe.dock_stop():
            recipe.fail("could not stop after linked-member Undo")
        if not recipe.dock_start():
            recipe.fail("could not restart after linked-member Undo")

        def relationship_reloaded(s: dict[str, Any]) -> bool:
            root_view = next((v for v in s["views"] if v["persistentDockId"] == root_id), None)
            member_view = next((v for v in s["views"] if v["persistentDockId"] == member_id), None)
            return (
                len(s["views"]) == 2
                and root_view is not None
                and member_view is not None
                and member_view["originalDockId"] == root_id
                and root_view["linkedDockIds"] == [member_id]
            )

        _wait_for_state(
            relationship_reloaded,
            "reloading after Undo did not reproduce the restored relationship",
        )

        # A shutdown during the applet Undo window must preserve the removal, including
        # member-local instances whose scheduled state is mirrored by the coordinator.
        member_applet = _applet_of(member_id, _ADDED_PLUGIN)
        if member_applet is None:
            recipe.fail("could not resolve the restored member applet")
        member_applet_id = member_applet["id"]
        _latte_call(
            "second member-originated applet removal failed",
            "removeApplet",
            "uu",
            str(member_id),
            str(member_applet_id),
        )

        root_scheduled = False
        member_removed = False
        for _ in range(80):
            root_scheduled = _applet_scheduled(root_id, _ADDED_PLUGIN)
            member_removed = _applet_absent(member_id, _ADDED_PLUGIN)
            if root_scheduled and member_removed:
                break
            time.sleep(0.1)
        if not (root_scheduled and member_removed):
            recipe.fail("second linked applet removal did not enter the Undo window")

        if not recipe.dock_stop():
            recipe.fail("could not stop inside the linked applet Undo window")
        if not recipe.dock_start():
            recipe.fail("could not restart after the linked applet tombstone")

        def relationship_survived(s: dict[str, Any]) -> bool:
            root_view = next((v for v in s["views"] if v["persistentDockId"] == root_id), None)
            member_view = next((v for v in s["views"] if v["persistentDockId"] == member_id), None)
            return (
                len(s["views"]) == 2
                and root_view is not None
                and member_view is not None
                and member_view["originalDockId"] == root_id
            )

        _wait_for_state(
            relationship_survived, "relationship did not survive the applet-removal restart"
        )

        for view in (root_id, member_id):
            if any(a["plugin"] == _ADDED_PLUGIN for a in _applets(view)):
                recipe.fail(f"restart resurrected the removed applet in view {view}")
        if f"plugin={_ADDED_PLUGIN}" in Path(layout).read_text(errors="replace"):
            recipe.fail("restart retained the removed applet in persistence")

        print(
            f"linked applet and dock Undo preserved direct root {root_id}, member {member_id}, "
            "and restart tombstones"
        )


if __name__ == "__main__":
    recipe.run(main)
