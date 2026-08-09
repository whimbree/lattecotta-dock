#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""Open the imported Global Menu through its real QMenu path, activate a real
com.canonical.dbusmenu action, and require focus to return to the exact
application that owned it before the panel's transient menu took focus. The
stock QMenu path remains outside Latte's containment focus session and lets
Qt restore its transient-parent focus directly.

Ported from tests/e2e/114-global-menu-focus-restoration.sh to
latte_harness.recipe (BP-3, the bash-to-python migration's focus-restoration
recipe wave R9). containmentAcceptsInput / ownsPanelFocusSession ride the widened
typed View model (W3, widen the readback models), so viewsData is read through
recipe.views(); the config backup/restore keeps the byte-for-byte
cp -a / diff -qr contract (subprocess), and the WAYLAND_DEBUG=client and
dbus-monitor logs are grepped exactly as the bash did.
"""

import configparser
import os
import re
import subprocess
import sys
import tempfile
import time
from contextlib import suppress
from pathlib import Path
from typing import IO

from latte_harness import proc, recipe


def _grep(log: Path, needle: str) -> bool:
    try:
        return needle in log.read_text(errors="replace")
    except OSError:
        return False


def _kill(procs: dict[str, subprocess.Popen[bytes] | None], name: str) -> None:
    handle = procs.get(name)
    if handle is not None:
        with suppress(ProcessLookupError):
            handle.kill()
        with suppress(Exception):
            handle.wait()
        procs[name] = None


def _fp(*args: object) -> bool:
    return (
        subprocess.run(
            [os.environ["E2E_FAKEPOINTER"], *(str(a) for a in args)], check=False
        ).returncode
        == 0
    )


def _views() -> list[recipe.View]:
    """viewsData as typed View records, or [] on a refused/failed reply (the wait
    loops' non-match sentinel).

    W3 (widen the readback models): containmentAcceptsInput / ownsPanelFocusSession
    ride the typed recipe.View, so this reads recipe.views()."""
    try:
        return recipe.views()
    except recipe.DbusUnavailableError:
        return []


def _kwin_last(js: str) -> str:
    lines = [ln for ln in recipe.kwin_js(js, 0.05).splitlines() if ln]
    return lines[-1] if lines else ""


def _active_caption() -> str:
    return _kwin_last(
        "const active = workspace.activeWindow;\n"
        '        print("@TAG@|" + (active ? active.caption : ""));'
    )


def _active_window_id() -> str:
    return _kwin_last(
        "const active = workspace.activeWindow;\n"
        '        print("@TAG@|" + (active ? active.internalId : ""));'
    )


def _activate_client() -> str:
    return _kwin_last(
        "for (const window of workspace.windowList()) {\n"
        '        if (window.caption === "LATTE PANEL FOCUS MENU CLIENT") {\n'
        "            workspace.activeWindow = window;\n"
        '            print("@TAG@|" + window.internalId);\n'
        "        }\n"
        "    }"
    )


def _panel_focus_state(cid: int) -> str:
    for view in _views():
        if view.containment_id == cid:
            accepts = str(view.containment_accepts_input).lower()
            owns = str(view.owns_panel_focus_session).lower()
            return f"{accepts} {owns}"
    return ""


def _keyboard_event_count(log: Path, event: str) -> int:
    """The bash ``grep -cE "wl_keyboard#[0-9]+\\.<event>" client.log || true``."""
    pattern = re.compile(rf"wl_keyboard#[0-9]+\.{event}")
    try:
        text = log.read_text(errors="replace")
    except OSError:
        return 0
    return sum(1 for line in text.splitlines() if pattern.search(line))


def _reduce_layout_to_appmenu(layout: Path) -> None:
    config = configparser.RawConfigParser()
    config.optionxform = str  # type: ignore[assignment,method-assign]
    config.read(layout)
    applet = "Containments][1][Applets][5"
    if not config.has_section(applet):
        config.add_section(applet)
    config.set(applet, "immutability", "1")
    config.set(applet, "plugin", "org.kde.plasma.appmenu")
    for section in list(config.sections()):
        if section.startswith("Containments][1][Applets][") and section != applet:
            config.remove_section(section)
    config.set("Containments][1][General", "appletOrder", "5")
    with layout.open("w", encoding="utf-8") as output:
        config.write(output, space_around_delimiters=False)


def _select_fixture_layout(lattedockrc: Path) -> None:
    config = configparser.RawConfigParser()
    config.optionxform = str  # type: ignore[assignment,method-assign]
    config.read(lattedockrc)
    if not config.has_section("UniversalSettings"):
        config.add_section("UniversalSettings")
    config.set("UniversalSettings", "singleModeLayoutName", "D21")
    config.set("UniversalSettings", "memoryUsage", "0")
    with lattedockrc.open("w", encoding="utf-8") as output:
        config.write(output, space_around_delimiters=False)


def main() -> None:
    proc.install_conventional_signal_exits()
    repo = Path(os.environ["E2E_REPO"])
    build = Path(os.environ["E2E_BUILD"])
    artifacts = Path(os.environ["E2E_ARTIFACTS"])
    config_home = Path(os.environ["E2E_CONFIG_HOME"])
    fixture = repo / "tests" / "e2e" / "fixtures" / "d21"
    client = build / "bin" / "latte-test-panel-focus-menu-client"
    platform_theme = os.environ.get("LATTE_PLASMA_PLATFORM_THEME_PLUGIN")
    if not platform_theme:
        recipe.fail("run inside the pinned development shell")
    assert platform_theme is not None
    scratch = Path(tempfile.mkdtemp(prefix="latte-global-menu-focus.", dir="/tmp"))
    backup = Path(tempfile.mkdtemp(prefix="latte-global-menu-backup.", dir="/tmp"))
    procs: dict[str, subprocess.Popen[bytes] | None] = {"app": None, "kded": None, "monitor": None}
    logs: list[IO[bytes]] = []
    state = {"backup_ready": False, "recipe_finalized": False, "body_ok": False}

    def _cp_a(source: str, destination: str) -> int:
        return subprocess.run(["cp", "-a", source, destination], check=False).returncode

    try:
        if not os.access(client, os.X_OK):
            recipe.fail("built Global Menu focus client is missing")
        if not Path(platform_theme).is_file():
            recipe.fail("pinned Plasma platform-theme plugin is missing")
        if not ((fixture / "D21.layout.latte").is_file() and (fixture / "kdeglobals").is_file()):
            recipe.fail("D21 Global Menu fixture is incomplete")

        (backup / "config").mkdir(parents=True)
        if _cp_a(f"{config_home}/.", f"{backup}/config/") != 0:
            recipe.fail("could not back up the nested config")
        state["backup_ready"] = True

        if not recipe.dock_stop():
            recipe.fail("could not stop the dock before staging Global Menu")
        for stale in (config_home / "latte").glob("*.layout.latte"):
            stale.unlink()
        if _cp_a(f"{fixture}/D21.layout.latte", f"{config_home}/latte/D21.layout.latte") != 0:
            recipe.fail("could not stage the Global Menu layout")
        if _cp_a(f"{fixture}/kdeglobals", f"{config_home}/kdeglobals") != 0:
            recipe.fail("could not stage the Global Menu color scheme")

        _select_fixture_layout(config_home / "lattedockrc")
        _reduce_layout_to_appmenu(config_home / "latte" / "D21.layout.latte")

        if not recipe.dock_start(90):
            recipe.fail("dock did not settle with the Global Menu fixture")

        kded_log = (scratch / "kded.log").open("wb")
        logs.append(kded_log)
        procs["kded"] = subprocess.Popen(["kded6"], stdout=kded_log, stderr=subprocess.STDOUT)

        def _registrar_present() -> bool:
            listing = subprocess.run(
                ["busctl", "--user", "list", "--no-pager"],
                capture_output=True,
                text=True,
                check=False,
            ).stdout
            return any(
                line.startswith("com.canonical.AppMenu.Registrar") for line in listing.splitlines()
            )

        for _ in range(80):
            if _registrar_present():
                break
            time.sleep(0.05)
        if not _registrar_present():
            recipe.fail("nested KDED did not register the Global Menu registrar")

        (scratch / "qt-plugins" / "platformthemes").mkdir(parents=True)
        (scratch / "qt-plugins" / "platformthemes" / "KDEPlasmaPlatformTheme6.so").symlink_to(
            platform_theme
        )

        client_log = (scratch / "client.log").open("wb")
        logs.append(client_log)
        client_env = dict(os.environ)
        client_env["KDE_FULL_SESSION"] = "true"
        client_env["XDG_CURRENT_DESKTOP"] = "KDE"
        client_env["QT_QPA_PLATFORMTHEME"] = "kde"
        client_env["QT_PLUGIN_PATH"] = str(scratch / "qt-plugins")
        client_env["WAYLAND_DEBUG"] = "client"
        procs["app"] = subprocess.Popen(
            [str(client)], stdout=client_log, stderr=subprocess.STDOUT, env=client_env
        )
        client_log_path = scratch / "client.log"

        for _ in range(80):
            if _active_caption() == "LATTE PANEL FOCUS MENU CLIENT":
                break
            _activate_client()
            time.sleep(0.05)
        if _active_caption() != "LATTE PANEL FOCUS MENU CLIENT":
            recipe.fail("controlled Global Menu application never became active")
        client_window_id = _active_window_id()
        if not client_window_id:
            recipe.fail("controlled Global Menu application has no KWin identity")

        published_line = ""
        set_address = re.compile(r"org_kde_kwin_appmenu#[0-9]+\.set_address")
        for _ in range(80):
            with suppress(OSError):
                for line in client_log_path.read_text(errors="replace").splitlines():
                    if set_address.search(line):
                        published_line = line
                        break
            if published_line:
                break
            time.sleep(0.05)
        if not published_line:
            recipe.fail("Qt did not publish the controlled menu on its Wayland surface")

        cid = next((v.containment_id for v in _views() if v.edge in ("top", "bottom")), None)
        if cid is None:
            recipe.fail("Global Menu fixture has no horizontal panel")
        assert cid is not None

        view = next(v for v in recipe.views() if v.containment_id == cid)
        applet = next(
            (a for a in recipe.view_applets(cid) if a.plugin == "org.kde.plasma.appmenu"), None
        )
        if applet is None:
            recipe.fail("could not resolve the Global Menu click point")
        assert applet is not None
        ax, ay, aw, ah = applet.geometry
        if not (aw > 0 and ah > 0):
            recipe.fail("could not resolve the Global Menu click point")
        click_x = view.absolute_geometry[0] - view.local_geometry[0] + ax + min(20, aw // 2)
        click_y = view.absolute_geometry[1] - view.local_geometry[1] + ay + ah // 2

        menu_bus_log = (scratch / "menu-bus.log").open("wb")
        logs.append(menu_bus_log)
        procs["monitor"] = subprocess.Popen(
            ["dbus-monitor", "--session", "interface='com.canonical.dbusmenu'"],
            stdout=menu_bus_log,
            stderr=subprocess.STDOUT,
        )
        time.sleep(0.1)
        keyboard_leaves_before = _keyboard_event_count(client_log_path, "leave")
        keyboard_enters_before = _keyboard_event_count(client_log_path, "enter")
        if not _fp("click", click_x, click_y):
            recipe.fail("could not open the real Global Menu QMenu")
        for _ in range(80):
            if _keyboard_event_count(client_log_path, "leave") > keyboard_leaves_before:
                break
            time.sleep(0.05)
        if not _keyboard_event_count(client_log_path, "leave") > keyboard_leaves_before:
            recipe.fail("Global Menu QMenu never took keyboard focus from the application")
        if _panel_focus_state(cid) != "false false":
            recipe.fail("stock Global Menu QMenu unexpectedly entered Latte's panel focus session")

        # The top-panel fixture opens the imported QMenu directly below its File
        # delegate. Click the only child action through that real popup instead of
        # relying on virtual-keyboard navigation inside a transient surface.
        action_x = click_x + 35
        action_y = click_y + 40
        if not _fp("click", action_x, action_y):
            recipe.fail("could not activate the imported Global Menu action")
        for _ in range(80):
            if _grep(client_log_path, "PANEL_FOCUS_ACTION_TRIGGERED"):
                break
            time.sleep(0.05)
        if not _grep(client_log_path, "PANEL_FOCUS_ACTION_TRIGGERED"):
            recipe.fail("the real Global Menu action did not fire in its application")

        time.sleep(0.2)
        _kill(procs, "monitor")
        if not _grep(scratch / "menu-bus.log", "member=Event"):
            recipe.fail("the action did not cross com.canonical.dbusmenu.Event")

        for _ in range(80):
            if _keyboard_event_count(client_log_path, "enter") > keyboard_enters_before:
                break
            time.sleep(0.05)
        if not _keyboard_event_count(client_log_path, "enter") > keyboard_enters_before:
            recipe.fail("Global Menu close did not return keyboard focus to the application")
        if _active_window_id() != client_window_id:
            recipe.fail("Global Menu close did not restore the exact application")
        if _panel_focus_state(cid) != "false false":
            recipe.fail("Global Menu close left Latte's panel focus session active")

        if not _fp("key", "F12"):
            recipe.fail("could not inject the post-menu focus probe")
        for _ in range(80):
            if _grep(client_log_path, "PANEL_FOCUS_KEY_DELIVERED"):
                break
            time.sleep(0.05)
        if not _grep(client_log_path, "PANEL_FOCUS_KEY_DELIVERED"):
            recipe.fail("the restored application did not receive keyboard input")

        if not recipe.dock_stop():
            recipe.fail("Global Menu fixture dock did not stop cleanly")
        if subprocess.run(["rm", "-rf", str(config_home)], check=False).returncode != 0:
            recipe.fail("could not clear the fixture config")
        if subprocess.run(["mkdir", "-p", str(config_home)], check=False).returncode != 0:
            recipe.fail("could not recreate the config directory")
        if _cp_a(f"{backup}/config/.", f"{config_home}/") != 0:
            recipe.fail("could not restore the nested config")
        if (
            subprocess.run(
                ["diff", "-qr", f"{backup}/config", str(config_home)],
                stdout=subprocess.DEVNULL,
                check=False,
            ).returncode
            != 0
        ):
            recipe.fail("restored nested config differs from its backup")
        state["recipe_finalized"] = True
        state["body_ok"] = True

        print("PASS: Global Menu action restored exact application focus")
    finally:
        cleanup_failed = 0
        _kill(procs, "monitor")
        _kill(procs, "app")
        _kill(procs, "kded")
        for log in logs:
            with suppress(OSError):
                log.close()
        if not state["body_ok"]:
            for src, dst in (
                ("client.log", "114-global-menu-client.log"),
                ("menu-bus.log", "114-global-menu-bus.log"),
                ("kded.log", "114-global-menu-kded.log"),
            ):
                with suppress(OSError):
                    subprocess.run(
                        ["cp", str(scratch / src), str(artifacts / dst)],
                        stderr=subprocess.DEVNULL,
                        check=False,
                    )
        if not state["recipe_finalized"] and state["backup_ready"]:
            pid = recipe.dock_pid()
            if pid is not None:
                alive = True
                try:
                    os.kill(pid, 0)
                except OSError:
                    alive = False
                if alive and not recipe.dock_stop():
                    cleanup_failed = 1
            for step in (
                ["rm", "-rf", str(config_home)],
                ["mkdir", "-p", str(config_home)],
            ):
                if subprocess.run(step, check=False).returncode != 0:
                    cleanup_failed = 1
            if _cp_a(f"{backup}/config/.", f"{config_home}/") != 0:
                cleanup_failed = 1
            if (
                subprocess.run(
                    ["diff", "-qr", f"{backup}/config", str(config_home)],
                    stdout=subprocess.DEVNULL,
                    check=False,
                ).returncode
                != 0
            ):
                cleanup_failed = 1
        for temp in (scratch, backup):
            if subprocess.run(["rm", "-rf", str(temp)], check=False).returncode != 0:
                cleanup_failed = 1
        if cleanup_failed:
            print("FAIL: Global Menu focus fixture cleanup left residue", file=sys.stderr)
            raise SystemExit(1)


if __name__ == "__main__":
    recipe.run(main)
