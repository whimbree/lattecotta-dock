#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""D28 (obsolete whole-applet colorfulness veto): palette propagation changes
inherited Kirigami.Theme roles. It does not recolor fixed image, SVG, or
Rectangle pixels, so a colorful fixed region must not veto palette response
elsewhere in the same applet.

Four deterministic applets isolate the policy:
- responsive-only draws Kirigami.Theme.textColor;
- fixed-only draws the literal #d62976;
- mixed draws both controls side by side;
- inline-mixed draws the same pair from an inline full representation.

The same applets are captured first with PlasmaThemeColors disengaged, then
with LightThemeColors applied. Per-control raw RGBA crops must show responsive
pixels changing to the treatment palette while every fixed crop stays
byte-identical across states. Literal-color checks remain as independent
non-vacuity evidence. Sustained treatment sampling spans the retired probe's
retry interval, so restoring the old asynchronous veto cannot pass during its
initial unknown state.

Ported from tests/e2e/111-colorizer-content-policy.sh to latte_harness.recipe
(BP-3, the bash-to-python migration's focus-restoration recipe wave R9). The
colorizerData resolved palette and the per-applet colorizerActive/
colorizerReason are read as raw JSON, the same boundary the bash python
one-liners used; every magick crop spec, raw-RGBA serialization, byte-exact
pixel comparison, and cross-state cmp is preserved.
"""

import configparser
import contextlib
import io
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

from latte_harness import proc, recipe
from latte_harness.config_restore import ConfigHomeSnapshot

_PLUGINS = [
    "org.kde.latte.d28-responsive",
    "org.kde.latte.d28-fixed",
    "org.kde.latte.d28-mixed",
    "org.kde.latte.d28-inline-mixed",
]

_PIXEL_RE = re.compile(r"\s#([0-9a-fA-F]{6})([0-9a-fA-F]{2})?\s")


def _fail_raw(message: str) -> None:
    """A sub-python sys.exit(msg): print the message to stderr (no FAIL: prefix),
    exit 1. The outer callers that had ``|| e2e_fail`` add their own FAIL line."""
    print(message, file=sys.stderr, flush=True)
    raise SystemExit(1)


def _views_raw() -> list[dict[str, object]]:
    return json.loads(recipe.json_payload("viewsData"))


def _applets_by_plugin(cid: int) -> dict[str, dict[str, object]]:
    applets = json.loads(recipe.json_payload("viewAppletsData", "u", str(cid)))
    return {a["plugin"]: a for a in applets}


def _horizontal_view_id() -> int | None:
    for view in _views_raw():
        if view["edge"] in ("top", "bottom"):
            cid = view["containmentId"]
            assert isinstance(cid, int)
            return cid
    return None


def _body() -> None:
    repo = Path(os.environ["E2E_REPO"])
    rt = Path(os.environ["E2E_RT"])
    config_home = Path(os.environ["E2E_CONFIG_HOME"])
    artifacts = Path(os.environ["E2E_ARTIFACTS"])
    fixture = repo / "tests" / "e2e" / "fixtures" / "d28"
    theme = repo / "tests" / "e2e" / "fixtures" / "d21" / "kdeglobals"

    if not ((fixture / "D28.layout.latte").is_file() and theme.is_file()):
        recipe.fail("D28 layout or hermetic color scheme fixture is missing")
    for plugin in _PLUGINS:
        if not (
            (fixture / "plasmoids" / plugin / "metadata.json").is_file()
            and (fixture / "plasmoids" / plugin / "contents" / "ui" / "main.qml").is_file()
        ):
            recipe.fail(f"D28 applet fixture is incomplete: {plugin}")

    # Install test-only packages into the nested process's private data home.
    if not recipe.dock_stop():
        recipe.fail("could not stop the vehicle dock before staging D28")
    data_home = rt / "d28-data"
    os.environ["XDG_DATA_HOME"] = str(data_home)
    subprocess.run(["rm", "-rf", str(data_home)], check=True)
    (data_home / "plasma" / "plasmoids").mkdir(parents=True)
    subprocess.run(
        ["cp", "-r", f"{fixture}/plasmoids/.", f"{data_home}/plasma/plasmoids/"], check=True
    )
    subprocess.run(["cp", str(theme), str(config_home / "kdeglobals")], check=True)

    lattedockrc = config_home / "lattedockrc"
    config = configparser.RawConfigParser()
    config.optionxform = str  # type: ignore[assignment,method-assign]
    config.read(lattedockrc)
    if not config.has_section("UniversalSettings"):
        config.add_section("UniversalSettings")
    config.set("UniversalSettings", "singleModeLayoutName", "D28")
    config.set("UniversalSettings", "memoryUsage", "0")
    with lattedockrc.open("w") as output:
        config.write(output, space_around_delimiters=False)

    def stage_fixture_layout(palette: str) -> None:
        destination = config_home / "latte" / "D28.layout.latte"
        for stale in (config_home / "latte").glob("*.layout.latte"):
            stale.unlink()
        subprocess.run(["cp", str(fixture / "D28.layout.latte"), str(destination)], check=True)
        text = destination.read_text()
        source = "themeColors=LightThemeColors"
        if text.count(source) != 1:
            _fail_raw("D28 fixture must contain exactly one LightThemeColors source line")
        destination.write_text(text.replace(source, "themeColors=" + palette))

    def resolved_palette(cid: int, shown: str, mode: str, field: str) -> str | None:
        colorizer = json.loads(recipe.json_payload("colorizerData", "u", str(cid)))
        expected_shown = shown == "true"
        if colorizer.get("mustBeShown") is not expected_shown:
            print(
                f"D28 mustBeShown={colorizer.get('mustBeShown')!r}, expected {expected_shown!r}",
                file=sys.stderr,
                flush=True,
            )
            return None
        if colorizer.get("themeColorsMode") != mode:
            print(
                f"D28 themeColorsMode={colorizer.get('themeColorsMode')!r}, expected {mode!r}",
                file=sys.stderr,
                flush=True,
            )
            return None
        color = colorizer.get(field, "")
        if not isinstance(color, str) or len(color) != 7 or not color.startswith("#"):
            print(f"D28 colorizer has no resolved {field}", file=sys.stderr, flush=True)
            return None
        return color

    def assert_fixture_state(cid: int, active: str, reason: str, samples: int) -> None:
        expected_active = active == "true"
        for sample in range(1, samples + 1):
            applets = _applets_by_plugin(cid)
            missing = [plugin for plugin in _PLUGINS if plugin not in applets]
            bad = [
                (
                    plugin,
                    applets[plugin].get("colorizerActive"),
                    applets[plugin].get("colorizerReason"),
                )
                for plugin in _PLUGINS
                if plugin in applets
                and not (
                    applets[plugin].get("colorizerActive") is expected_active
                    and applets[plugin].get("colorizerReason") == reason
                )
            ]
            if missing or bad:
                print(
                    f"D28 state failure: missing={missing} bad={bad}", file=sys.stderr, flush=True
                )
                recipe.fail(
                    f"fixture applet state diverged from active={active} reason={reason} "
                    f"(sample {sample})"
                )
            if sample < samples:
                time.sleep(1)

    def crop_path(state: str, label: str) -> Path:
        return artifacts / f"d28-{state}-{label}.png"

    def raw_crop_path(state: str, label: str) -> Path:
        return artifacts / f"d28-{state}-{label}.rgba"

    def capture_controls(state: str, cid: int) -> None:
        if not recipe.assert_geometry_agrees(2):
            recipe.fail(f"D28 {state} control crops cannot trust view geometry")
        shot = artifacts / f"d28-{state}-content-policy.png"
        try:
            recipe.screenshot(str(shot), "include-cursor", "b", "false")
        except recipe.RecipeError:
            recipe.fail(f"D28 {state} screenshot failed")
        applets = {a.plugin: a for a in recipe.view_applets(cid)}
        view = next(v for v in recipe.views() if v.containment_id == cid)
        origin_x = view.absolute_geometry[0] - view.local_geometry[0]
        origin_y = view.absolute_geometry[1] - view.local_geometry[1]

        def emit(label: str, plugin: str, offset: int = 0) -> tuple[str, str]:
            x, y, width, height = applets[plugin].geometry
            center_x = origin_x + x + width // 2 + offset
            center_y = origin_y + y + height // 2
            return label, f"12x12+{center_x - 6}+{center_y - 6}"

        crop_specs = [
            emit("responsive", "org.kde.latte.d28-responsive"),
            emit("fixed", "org.kde.latte.d28-fixed"),
            emit("mixed-responsive", "org.kde.latte.d28-mixed", -18),
            emit("mixed-fixed", "org.kde.latte.d28-mixed", 18),
            emit("inline-responsive", "org.kde.latte.d28-inline-mixed", -18),
            emit("inline-fixed", "org.kde.latte.d28-inline-mixed", 18),
        ]
        for label, rect in crop_specs:
            image = crop_path(state, label)
            raw = raw_crop_path(state, label)
            if (
                subprocess.run(
                    ["magick", str(shot), "-crop", rect, "+repage", str(image)], check=False
                ).returncode
                != 0
            ):
                recipe.fail(f"could not crop D28 {state} {label} control at {rect}")
            if (
                subprocess.run(
                    ["magick", str(image), "-depth", "8", f"rgba:{raw}"], check=False
                ).returncode
                != 0
            ):
                recipe.fail(f"could not serialize D28 {state} {label} RGBA bytes")
            print(f"D28 {state} crop {label}: {rect}")

    def assert_solid_rgba(state: str, label: str, expected_hex: str) -> bool:
        image = crop_path(state, label)
        result = subprocess.run(
            ["magick", str(image), "-depth", "8", "txt:-"],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            recipe.fail(f"could not read D28 {state} {label} crop pixels")
        expected = (*bytes.fromhex(expected_hex.removeprefix("#")), 255)
        pixels: list[tuple[int, ...]] = []
        for line in result.stdout.splitlines():
            match = _PIXEL_RE.search(line)
            if match:
                alpha = int(match.group(2), 16) if match.group(2) else 255
                rgba = (*bytes.fromhex(match.group(1)), alpha)
                pixels.append(rgba)
        # These two legs print the detail and return False so the solid()
        # caller adds its own FAIL line - the bash sub-python sys.exit(msg)
        # followed by the caller's `|| e2e_fail "<desc>"`, reproduced.
        if len(pixels) != 144:
            print(
                f"D28 {state}-{label} crop yielded {len(pixels)} pixels, expected 144",
                file=sys.stderr,
                flush=True,
            )
            return False
        mismatches = [pixel for pixel in pixels if pixel != expected]
        if mismatches:
            observed = sorted(set(mismatches))[:8]
            print(
                f"D28 {state}-{label} pixels differ from {expected}: "
                f"{len(mismatches)}/144 mismatches, observed {observed}",
                file=sys.stderr,
                flush=True,
            )
            return False
        print(f"D28 RENDER ok: {state}-{label} is byte-exact {expected_hex}")
        return True

    def assert_crops_equal(first_state: str, second_state: str, label: str) -> None:
        if (
            subprocess.run(
                [
                    "cmp",
                    str(raw_crop_path(first_state, label)),
                    str(raw_crop_path(second_state, label)),
                ],
                stdout=subprocess.DEVNULL,
                check=False,
            ).returncode
            != 0
        ):
            recipe.fail(f"D28 {label} bytes changed between {first_state} and {second_state}")
        print(f"D28 CROSS-STATE ok: {label} bytes are identical")

    def assert_crops_differ(first_state: str, second_state: str, label: str) -> None:
        if (
            subprocess.run(
                [
                    "cmp",
                    str(raw_crop_path(first_state, label)),
                    str(raw_crop_path(second_state, label)),
                ],
                stdout=subprocess.DEVNULL,
                check=False,
            ).returncode
            == 0
        ):
            recipe.fail(
                f"D28 {label} bytes did not change between {first_state} and {second_state}"
            )
        print(f"D28 CROSS-STATE ok: {label} bytes changed")

    # CONTROL: the Plasma palette is inherited normally and the Latte colorizer
    # is genuinely disengaged. These pixels are the before-image for the treatment.
    stage_fixture_layout("PlasmaThemeColors")
    if not recipe.dock_start(90):
        recipe.fail("dock never settled with the D28 control fixture")
    control_cid = _horizontal_view_id()
    if control_cid is None:
        recipe.fail("no horizontal D28 control view came up")
    assert control_cid is not None
    control_color = resolved_palette(control_cid, "false", "plasma", "textColor")
    if control_color is None:
        recipe.fail("could not resolve the disengaged D28 control palette")
    assert control_color is not None
    assert_fixture_state(control_cid, "false", "notEngaged", 1)
    print("D28 CONTROL state: fixtures are inactive with reason=notEngaged")
    capture_controls("control", control_cid)

    def solid(state: str, label: str, expected_hex: str, fail_message: str) -> None:
        if not assert_solid_rgba(state, label, expected_hex):
            recipe.fail(fail_message)

    solid(
        "control",
        "responsive",
        control_color,
        "control responsive-only content does not match its inherited palette",
    )
    solid(
        "control",
        "mixed-responsive",
        control_color,
        "control mixed responsive content does not match its inherited palette",
    )
    solid(
        "control", "fixed", "#d62976", "control fixed-only content differs from its literal color"
    )
    solid(
        "control",
        "mixed-fixed",
        "#d62976",
        "control mixed fixed content differs from its literal color",
    )
    solid(
        "control",
        "inline-responsive",
        control_color,
        "control inline responsive content does not match its inherited palette",
    )
    solid(
        "control",
        "inline-fixed",
        "#d62976",
        "control inline fixed content differs from its literal color",
    )
    if not recipe.dock_stop():
        recipe.fail("could not stop the D28 control dock")

    # TREATMENT: LightThemeColors engages Latte's palette push. The removed probe
    # retried every two seconds, so six one-second applied samples ensure restoring
    # the old veto fails after its initial unknown state.
    stage_fixture_layout("LightThemeColors")
    if not recipe.dock_start(90):
        recipe.fail("dock never settled with the D28 treatment fixture")
    treatment_cid = _horizontal_view_id()
    if treatment_cid is None:
        recipe.fail("no horizontal D28 treatment view came up")
    assert treatment_cid is not None
    treatment_color = resolved_palette(treatment_cid, "true", "light", "applyColor")
    if treatment_color is None:
        recipe.fail("could not resolve the applied D28 treatment palette")
    assert treatment_color is not None
    if control_color == treatment_color:
        recipe.fail(f"D28 control and treatment palettes are identical ({control_color})")
    assert_fixture_state(treatment_cid, "true", "applied", 6)
    print("D28 TREATMENT state: fixtures stayed active with reason=applied")
    capture_controls("treatment", treatment_cid)
    solid(
        "treatment",
        "responsive",
        treatment_color,
        "treatment responsive-only content did not follow the panel palette",
    )
    solid(
        "treatment",
        "mixed-responsive",
        treatment_color,
        "treatment mixed responsive content did not follow the panel palette",
    )
    solid(
        "treatment",
        "fixed",
        "#d62976",
        "treatment fixed-only content differs from its literal color",
    )
    solid(
        "treatment",
        "mixed-fixed",
        "#d62976",
        "treatment mixed fixed content differs from its literal color",
    )
    solid(
        "treatment",
        "inline-responsive",
        treatment_color,
        "treatment inline responsive content did not follow the panel palette",
    )
    solid(
        "treatment",
        "inline-fixed",
        "#d62976",
        "treatment inline fixed content differs from its literal color",
    )

    assert_crops_differ("control", "treatment", "responsive")
    assert_crops_equal("control", "treatment", "fixed")
    assert_crops_differ("control", "treatment", "mixed-responsive")
    assert_crops_equal("control", "treatment", "mixed-fixed")
    print("D28 MIXED ok: responsive bytes changed while fixed bytes stayed identical")
    assert_crops_differ("control", "treatment", "inline-responsive")
    assert_crops_equal("control", "treatment", "inline-fixed")
    print("D28 INLINE ok: full-representation roles changed while fixed bytes stayed identical")

    print("PASS: D28 control/treatment palette response and fixed-pixel stability")


def _stop_dock_quietly() -> None:
    """Stop the reused vehicle dock if it is up, its diagnostics muted.

    Cleanup stops the dock BEFORE restoring the config so the dock's SIGTERM
    config flush lands first, not on top of the restored files (the 022/034
    stop-then-restore order). A dock already gone is fine; dock_stop's own
    "already gone" chatter is muted like 022's cleanup stop.
    """
    with contextlib.suppress(recipe.RecipeError), contextlib.redirect_stderr(io.StringIO()):
        pid = recipe.dock_pid()
        if pid is None:
            return
        try:
            os.kill(pid, 0)
        except OSError:
            return
        recipe.dock_stop()


def main() -> int:
    # The cleanup sits in a finally so it runs on EVERY exit path: the caught
    # verdict (recipe.fail's SystemExit), an unexpected exception after the
    # config home is already mutated, and the conventional signal exits installed
    # below. 111 overwrites the SHARED throwaway config home's kdeglobals, edits
    # its lattedockrc, and swaps its latte/ layout set (stage_fixture_layout),
    # and populates an XDG_DATA_HOME scratch tree at E2E_RT/d28-data with the D28
    # test plasmoids. The runner reuses that one config home (and the vehicle
    # dock) across every recipe of an invocation, so an un-restored mutation
    # strands the D28 dark theme and fixture layout into the next recipe - recipe
    # order becomes a hidden coupling. The deleted bash original leaked exactly
    # this way (no trap cleanup); the snapshot-and-restore closes it. The
    # d28-data tree was absent before the recipe, so restore removes it; the
    # in-process XDG_DATA_HOME export is discarded when this recipe process exits
    # and the runner restarts the next recipe's dock from its own ambient env.
    proc.install_conventional_signal_exits()
    config_home = Path(os.environ["E2E_CONFIG_HOME"])
    rt = Path(os.environ["E2E_RT"])
    snapshot = ConfigHomeSnapshot()
    snapshot.snapshot_file(config_home / "kdeglobals")
    snapshot.snapshot_file(config_home / "lattedockrc")
    snapshot.snapshot_dir(config_home / "latte")
    snapshot.snapshot_dir(rt / "d28-data")
    status = 0
    try:
        try:
            _body()
        except SystemExit as exc:
            status = exc.code if isinstance(exc.code, int) else 1
        except recipe.RecipeError as exc:
            print(str(exc), file=sys.stderr, flush=True)
            status = 1
    finally:
        _stop_dock_quietly()
        if snapshot.restore() and status == 0:
            status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
