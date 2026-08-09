#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
# e2e-expect: status 57
#
# SC-CW1 (the D57 ConfigOverlay wheel-threshold reproduction): drive a real
# Latte-style applet through ConfigOverlay.onWheel and observe the independent
# applet geometry readback. The production handler divides angleDelta.y by 8,
# increases above +12, and should decrease below -12. A standard 120-unit
# detent is therefore 15 degrees; the exact strict boundary is 96 units.
#
# The current inherited decrease comparison is `angle < 12`, so delivered
# -96, +/-90, and horizontal events are expected to expose D57 by shrinking
# the applet. The recipe states the correct threshold expectations and remains
# XFAIL only when all of those effects occur. The separate axisstop control
# asks KWin for wl_pointer.axis_stop; Qt emits no QWheelEvent in this isolated
# sequence, so it does not claim ConfigOverlay accepts a delivered (0,0).
#
# Ported from tests/e2e/022-configoverlay-wheel-threshold.sh to
# latte_harness.recipe / latte_harness.applet_reorder (BP-3, the bash-to-python
# migration's driver-recipe batch). The exit-status contract (57 on the D57
# signature, 0 on the corrected signature) and the # e2e-expect: status 57
# marker are preserved byte-identically; the applet_reorder_enter/exit rearrange
# path is the typed driver, the fixture staging/backup/restore is a direct port
# of the bash cleanup discipline.
"""SC-CW1 ConfigOverlay wheel-threshold reproduction (the D57 status-57 recipe)."""

import os
import shutil
import subprocess
import sys
import time
from typing import NoReturn

from latte_harness import applet_reorder, proc, recipe

_PLUGIN = "org.kde.latte.separator"


def _fakepointer(*args: str) -> int:
    """Fire one fakepointer invocation, returning its exit status (the bash
    ``... || e2e_fail`` sites gate on this)."""
    return subprocess.run([os.environ["E2E_FAKEPOINTER"], *args], check=False).returncode


def _pid_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def _copytree_contents(src: str, dst: str) -> None:
    """cp -a src/. dst/: copy the CONTENTS of src into the existing dst."""
    for entry in os.listdir(src):
        s = os.path.join(src, entry)
        d = os.path.join(dst, entry)
        if os.path.isdir(s):
            shutil.copytree(s, d, symlinks=True)
        else:
            shutil.copy2(s, d, follow_symlinks=False)


def _diff_qr(a: str, b: str) -> bool:
    """diff -qr a b: True when the two trees are byte-identical."""
    return (
        subprocess.run(
            ["diff", "-qr", a, b], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False
        ).returncode
        == 0
    )


def _kwrite_file(dest: str, *args: str) -> int:
    return subprocess.run(["kwriteconfig6", "--file", dest, *args], check=False).returncode


# ---- readbacks (the widened typed models, W3 - widen the readback models) -----


def _fixture_view(axis: str, fail_msg: str) -> int:
    edges = ("top", "bottom") if axis == "horizontal" else ("left", "right")
    views = [v for v in recipe.views() if v.edge in edges]
    if len(views) != 1:
        recipe.fail(fail_msg)
    return views[0].containment_id


def _applet_length(view: int, axis: str, fail_msg: str) -> int:
    applets = [a for a in recipe.view_applets(view) if a.plugin == _PLUGIN]
    if len(applets) != 1:
        recipe.fail(fail_msg)
    return applets[0].geometry[2 if axis == "horizontal" else 3]


def _resolve_points(view: int, axis: str) -> tuple[int, int, int, int]:
    """The live ConfigOverlay target center and the outside-the-band park point."""
    v = next((x for x in recipe.views() if x.containment_id == view), None)
    applet = next((a for a in recipe.view_applets(view) if a.plugin == _PLUGIN), None)
    if v is None or applet is None:
        recipe.fail(f"{axis}: could not resolve the live ConfigOverlay target")
    origin_x = v.absolute_geometry[0] - v.local_geometry[0]
    origin_y = v.absolute_geometry[1] - v.local_geometry[1]
    x, y, width, height = applet.geometry
    target_x = round(origin_x + x + width / 2)
    target_y = round(origin_y + y + height / 2)
    if axis == "horizontal":
        park_x, park_y = target_x, v.screen_geometry[1] + v.screen_geometry[3] // 2
    else:
        park_x, park_y = v.screen_geometry[0] + v.screen_geometry[2] // 2, target_y
    return target_x, target_y, park_x, park_y


def _arm_target(view: int, axis: str) -> tuple[int, int]:
    target_x, target_y, park_x, park_y = _resolve_points(view, axis)
    if _fakepointer("move", str(park_x), str(park_y)) != 0:
        recipe.fail(f"{axis}: pointer park move failed")
    time.sleep(0.2)
    if _fakepointer("glide", str(park_x), str(park_y), str(target_x), str(target_y)) != 0:
        recipe.fail(f"{axis}: pointer target glide failed")
    time.sleep(0.4)
    return target_x, target_y


def _expect_invalid_wheel(value: str) -> None:
    result = subprocess.run(
        [os.environ["E2E_FAKEPOINTER"], "wheel", "0", "0", value],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    output = result.stdout
    status = result.returncode
    if status != 2:
        recipe.fail(
            f"fakepointer accepted invalid wheel delta '{value}' or returned wrong status "
            f"{status}: {output}"
        )
    if "invalid angle-delta" not in output:
        recipe.fail(f"fakepointer rejected '{value}' without the angle-delta diagnostic: {output}")


def _select_fixture_layout(path: str) -> None:
    """The bash configparser heredoc: point lattedockrc at the SC-CW1 layout and
    force single-layout memory. Key case is preserved (the ``optionxform = str``)."""
    import configparser

    class _CaseParser(configparser.RawConfigParser):
        def optionxform(self, optionstr: str) -> str:
            return optionstr

    try:
        config = _CaseParser()
        _ = config.read(path)
        if not config.has_section("UniversalSettings"):
            config.add_section("UniversalSettings")
        config.set("UniversalSettings", "singleModeLayoutName", "SC-CW1")
        config.set("UniversalSettings", "memoryUsage", "0")
        with open(path, "w") as output:
            config.write(output, space_around_delimiters=False)
    except OSError, configparser.Error:
        recipe.fail("could not select the SC-CW1 fixture layout")


def _stage_axis(axis: str, fixture: str, config_home: str) -> None:
    destination = f"{config_home}/latte/SC-CW1.layout.latte"
    latte_dir = f"{config_home}/latte"
    try:
        for stale in os.listdir(latte_dir):
            if stale.endswith(".layout.latte"):
                os.remove(os.path.join(latte_dir, stale))
    except OSError:
        recipe.fail(f"{axis}: could not clear the prior layout")
    try:
        shutil.copy(f"{fixture}/SC-CW1.layout.latte", destination)
    except OSError:
        recipe.fail(f"{axis}: could not stage the fixture layout")
    if axis == "horizontal":
        formfactor, location = "2", "3"
    else:
        formfactor, location = "3", "5"
    if (
        _kwrite_file(
            destination,
            "--group",
            "Containments",
            "--group",
            "1",
            "--key",
            "formfactor",
            formfactor,
        )
        != 0
    ):
        recipe.fail(f"{axis}: could not set the fixture form factor")
    if (
        _kwrite_file(
            destination, "--group", "Containments", "--group", "1", "--key", "location", location
        )
        != 0
    ):
        recipe.fail(f"{axis}: could not set the fixture edge")


def _record_case(
    view: int,
    view_axis: str,
    label: str,
    delta: str,
    wheel_axis: str,
    spec_delta: int,
    inherited_delta: int,
    counters: dict[str, int],
) -> None:
    before = _applet_length(
        view, view_axis, f"{view_axis} {label}: initial applet-length query failed"
    )
    observed = 0
    after = before
    for attempt in (1, 2, 3):
        target_x, target_y = _arm_target(view, view_axis)
        if wheel_axis == "horizontal":
            if _fakepointer("wheel", str(target_x), str(target_y), delta, "horizontal") != 0:
                recipe.fail(f"{view_axis} {label}: horizontal wheel injection failed")
        elif wheel_axis == "axisstop":
            if _fakepointer("axisstop", str(target_x), str(target_y)) != 0:
                recipe.fail(f"{view_axis} {label}: axis-stop injection failed")
        else:
            if _fakepointer("wheel", str(target_x), str(target_y), delta) != 0:
                recipe.fail(f"{view_axis} {label}: vertical wheel injection failed")
        for _poll in range(6):
            time.sleep(0.2)
            after = _applet_length(
                view, view_axis, f"{view_axis} {label}: applet-length poll failed"
            )
            observed = after - before
            if observed != 0:
                break
        if observed != 0 or inherited_delta == 0:
            break
        print(f"  ({view_axis} {label} was not delivered on attempt {attempt}, retrying)")
    after = _applet_length(
        view, view_axis, f"{view_axis} {label}: final applet-length query failed"
    )
    observed = after - before
    print(
        f"OBS|view={view_axis}|event={label}|angleDelta={delta}|length={before}->{after}|"
        f"delta={observed:+d}|spec={spec_delta:+d}|inherited={inherited_delta:+d}"
    )
    if observed != inherited_delta:
        print(
            f"UNEXPECTED: {view_axis} {label} produced {observed}, expected inherited-path effect "
            f"{inherited_delta}",
            file=sys.stderr,
            flush=True,
        )
        counters["unexpected_effects"] += 1
    if observed != spec_delta:
        counters["spec_failures"] += 1


def _run_axis(axis: str, fixture: str, config_home: str, counters: dict[str, int]) -> None:
    _stage_axis(axis, fixture, config_home)
    if not recipe.dock_start(90):
        recipe.fail(f"{axis}: dock did not settle with the SC-CW1 fixture")
    view = _fixture_view(axis, f"{axis}: fixture view discovery failed")

    # Negative control: without rearrange mode ConfigOverlay is not visible, so
    # the fixture's own no-handler surface must leave its geometry unchanged.
    before = _applet_length(view, axis, f"{axis}: normal-mode initial applet-length query failed")
    target_x, target_y = _arm_target(view, axis)
    if _fakepointer("wheel", str(target_x), str(target_y), "120") != 0:
        recipe.fail(f"{axis}: normal-mode wheel injection failed")
    time.sleep(0.8)
    after = _applet_length(view, axis, f"{axis}: normal-mode final applet-length query failed")
    print(f"CONTROL|view={axis}|mode=normal|angleDelta=(0,120)|length={before}->{after}")
    if after != before:
        recipe.fail(
            f"{axis}: normal-mode wheel changed fixture length before ConfigOverlay was active"
        )

    try:
        applet_reorder.applet_reorder_enter(view)
    except applet_reorder.AppletReorderError:
        recipe.fail(f"{axis}: could not enter the ConfigOverlay rearrange path")

    _record_case(view, axis, "vertical-positive", "120", "vertical", 8, 8, counters)
    _record_case(view, axis, "vertical-negative", "-120", "vertical", -8, -8, counters)
    _record_case(view, axis, "vertical-positive-boundary", "96", "vertical", 0, 0, counters)
    _record_case(view, axis, "vertical-negative-boundary", "-96", "vertical", 0, -8, counters)
    _record_case(view, axis, "vertical-positive-subthreshold", "90", "vertical", 0, -8, counters)
    _record_case(view, axis, "vertical-negative-subthreshold", "-90", "vertical", 0, -8, counters)
    _record_case(view, axis, "horizontal-positive", "120", "horizontal", 0, -8, counters)
    _record_case(view, axis, "horizontal-negative", "-120", "horizontal", 0, -8, counters)
    _record_case(view, axis, "vertical-axis-stop", "stop", "axisstop", 0, 0, counters)
    _record_case(view, axis, "post-zero-positive-control", "120", "vertical", 8, 8, counters)

    try:
        applet_reorder.applet_reorder_exit(view)
    except applet_reorder.AppletReorderError:
        recipe.fail(f"{axis}: ConfigOverlay did not cleanly exit")
    if not recipe.dock_stop():
        recipe.fail(f"{axis}: dock did not stop cleanly after the matrix")


def _finalize_recipe(
    config_home: str, fixture_data: str, backup: str, state: dict[str, bool]
) -> None:
    try:
        pid = recipe.dock_pid()
    except recipe.RecipeError:
        recipe.fail("finalization could not read the dock pid")
    if pid is None:
        recipe.fail("finalization found no recorded dock pid")
    if _pid_alive(pid):
        recipe.fail(f"finalization found dock pid {pid} still running")
    try:
        shutil.rmtree(config_home, ignore_errors=False)
    except OSError:
        recipe.fail("finalization could not clear the fixture config")
    try:
        os.makedirs(config_home, exist_ok=True)
    except OSError:
        recipe.fail("finalization could not recreate the config directory")
    try:
        _copytree_contents(f"{backup}/config", config_home)
    except OSError:
        recipe.fail("finalization could not restore the original config")
    if not _diff_qr(f"{backup}/config", config_home):
        recipe.fail("finalization restored different config bytes")
    shutil.rmtree(fixture_data, ignore_errors=True)
    if os.path.exists(fixture_data):
        recipe.fail(f"finalization left fixture data at {fixture_data}")
    state["recipe_finalized"] = True


def _cleanup(config_home: str, fixture_data: str, backup: str, state: dict[str, bool]) -> bool:
    """The bash trap cleanup: restore the original config and remove all fixture
    residue unless the recipe already finalized. Returns True if it left residue."""
    cleanup_failed = False
    if not state["recipe_finalized"] and state["backup_ready"]:
        pid: int | None = None
        try:
            pid = recipe.dock_pid()
        except recipe.RecipeError:
            pid = None
        if pid is not None and _pid_alive(pid):
            try:
                stopped = recipe.dock_stop()
            except recipe.RecipeError:
                stopped = False
            if not stopped:
                print(f"FAIL: cleanup could not stop dock pid {pid}", file=sys.stderr, flush=True)
                cleanup_failed = True
        try:
            shutil.rmtree(config_home, ignore_errors=True)
            os.makedirs(config_home, exist_ok=True)
            _copytree_contents(f"{backup}/config", config_home)
            restored = _diff_qr(f"{backup}/config", config_home)
        except OSError:
            restored = False
        if not restored:
            print(
                "FAIL: cleanup could not restore the original config byte-for-byte",
                file=sys.stderr,
                flush=True,
            )
            cleanup_failed = True
        shutil.rmtree(fixture_data, ignore_errors=True)
        if os.path.exists(fixture_data):
            print(
                f"FAIL: cleanup could not remove fixture data {fixture_data}",
                file=sys.stderr,
                flush=True,
            )
            cleanup_failed = True
    shutil.rmtree(backup, ignore_errors=True)
    if os.path.exists(backup):
        print(f"FAIL: cleanup could not remove backup {backup}", file=sys.stderr, flush=True)
        cleanup_failed = True
    if cleanup_failed:
        print("FAIL: SC-CW1 recipe cleanup left residue", file=sys.stderr, flush=True)
    return cleanup_failed


def _body(
    fixture: str, config_home: str, fixture_data: str, backup: str, state: dict[str, bool]
) -> int:
    if not (
        os.path.isfile(f"{fixture}/SC-CW1.layout.latte")
        and os.path.isfile(f"{fixture}/plasmoids/{_PLUGIN}/metadata.json")
        and os.path.isfile(f"{fixture}/plasmoids/{_PLUGIN}/contents/ui/main.qml")
    ):
        recipe.fail("SC-CW1 ConfigOverlay fixture is incomplete")

    try:
        os.makedirs(f"{backup}/config", exist_ok=True)
    except OSError:
        recipe.fail("could not create the SC-CW1 config backup directory")
    try:
        _copytree_contents(config_home, f"{backup}/config")
    except OSError:
        recipe.fail("could not back up the original config")
    state["backup_ready"] = True

    for invalid_delta in ("0", "1.5", "nan", "100663297", "2147483648"):
        _expect_invalid_wheel(invalid_delta)

    if not recipe.dock_stop():
        recipe.fail("could not stop the vehicle dock before staging SC-CW1")
    os.environ["XDG_DATA_HOME"] = fixture_data
    try:
        os.makedirs(f"{fixture_data}/plasma/plasmoids", exist_ok=True)
    except OSError:
        recipe.fail("could not create the fixture data tree")
    try:
        _copytree_contents(f"{fixture}/plasmoids", f"{fixture_data}/plasma/plasmoids")
    except OSError:
        recipe.fail("could not stage the SC-CW1 applet fixture")

    _select_fixture_layout(f"{config_home}/lattedockrc")

    counters = {"spec_failures": 0, "unexpected_effects": 0}

    _run_axis("horizontal", fixture, config_home, counters)
    _run_axis("vertical", fixture, config_home, counters)

    if counters["unexpected_effects"] == 0 and counters["spec_failures"] == 10:
        _finalize_recipe(config_home, fixture_data, backup, state)
        print(
            "D57 reproduced: -96, +/-90, and horizontal wheel events decreased the Latte-style "
            "applet on both view axes"
        )
        return 57
    if counters["spec_failures"] == 0:
        _finalize_recipe(config_home, fixture_data, backup, state)
        print(
            "D57 corrected signature observed on both view axes; "
            "promote SC-CW1 to a regression guard"
        )
        return 0
    recipe.fail(
        f"SC-CW1 observed a partial or unrelated signature "
        f"(inherited mismatches={counters['unexpected_effects']}, "
        f"spec violations={counters['spec_failures']})"
    )


def run() -> int:
    fixture = f"{os.environ['E2E_REPO']}/tests/e2e/fixtures/sc-cw1"
    config_home = os.environ["E2E_CONFIG_HOME"]
    fixture_data = f"{os.environ['E2E_RT']}/sc-cw1-data"
    try:
        backup = _mkdtemp()
    except OSError:
        recipe.fail("could not allocate the SC-CW1 config backup")
    state: dict[str, bool] = {"backup_ready": False, "recipe_finalized": False}

    # The cleanup sits in a finally so it runs on EVERY exit path, like the
    # bash `trap cleanup EXIT`: not just the caught fail/verdict exits but
    # also an unexpected exception (a malformed busctl reply mid-matrix, an
    # OSError) and the conventional signal exits main() installs. Without
    # it, an unintended exit strands the vehicle dock on the SC-CW1 fixture
    # config and the runner's dock-reuse poisons every following recipe.
    body_status = 0
    try:
        try:
            body_status = _body(fixture, config_home, fixture_data, backup, state)
        except SystemExit as exc:
            body_status = exc.code if isinstance(exc.code, int) else 1
        except recipe.RecipeError as exc:
            print(str(exc), file=sys.stderr, flush=True)
            body_status = 1
    finally:
        if _cleanup(config_home, fixture_data, backup, state):
            body_status = 1
    return body_status


def _mkdtemp() -> str:
    import tempfile

    return tempfile.mkdtemp()


def main() -> NoReturn:
    # SIGINT/SIGTERM become SystemExit(130/143), so they route through the
    # cleanup finally and preserve the distinguished exit codes the
    # equivalence contract names (the bash trap fired on those too).
    proc.install_conventional_signal_exits()
    raise SystemExit(run())


if __name__ == "__main__":
    main()
