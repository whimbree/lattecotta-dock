# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The sceneprobe gate's pure logic: tier default, import-path flattening, the
Vulkan device dispatch (with its refusal messages), the probe env assembly, and
the pass/fail and self-test verdict classifiers.

The device-dispatch cases drive REAL files (a temp ICD manifest, a temp layer
dir) rather than mocks, so the exists()/is_file() checks the bash `[ -f ]` /
`[ -d ]` guards mirror are exercised for real. The self-test message templates
are pinned against the run_in_kwin.sh / sceneprobe-gate.sh wording verbatim so a
reworded verdict cannot drift silently.
"""

from pathlib import Path

import pytest

from latte_harness.sceneprobe_gate import (
    SELF_TESTS,
    DeviceConfig,
    DeviceDispatchError,
    build_probe_env,
    flatten_import_dirs,
    resolve_device_config,
    resolve_tier,
    scene_passed,
    selftest_disagrees,
)

# ---- resolve_tier ----------------------------------------------------------


@pytest.mark.parametrize("env", [{}, {"SCENEPROBE_TIER": ""}])
def test_resolve_tier_defaults_bitexact_when_unset_or_empty(env: dict[str, str]) -> None:
    # Mirrors the shell `${SCENEPROBE_TIER:-bitexact}`: unset AND empty both fall
    # back, so the merge gate stays bit-exact when nothing sets it.
    assert resolve_tier(env) == "bitexact"


def test_resolve_tier_passes_a_set_value_through() -> None:
    # A set value is never second-guessed here; the probe validates it and
    # refuses an unknown tier loudly on its own.
    assert resolve_tier({"SCENEPROBE_TIER": "tolerance"}) == "tolerance"


# ---- flatten_import_dirs ---------------------------------------------------


def test_flatten_import_dirs_takes_the_directories_from_flag_pairs() -> None:
    imports = ["-import", "/a", "-import", "/b", "-import", "/stage/lib/qml"]
    assert flatten_import_dirs(imports) == "/a:/b:/stage/lib/qml"


def test_flatten_import_dirs_empty_list_is_empty_string() -> None:
    assert flatten_import_dirs([]) == ""


# ---- resolve_device_config -------------------------------------------------


@pytest.fixture
def layer_dir(tmp_path: Path) -> str:
    layers = tmp_path / "explicit_layer.d"
    layers.mkdir()
    return str(layers)


@pytest.fixture
def icd_file(tmp_path: Path) -> str:
    icd = tmp_path / "lvp_icd.x86_64.json"
    icd.write_text("{}")
    return str(icd)


def test_resolve_device_lavapipe_default_forces_the_pinned_icd(
    icd_file: str, layer_dir: str
) -> None:
    cfg = resolve_device_config(
        {"LATTE_VULKAN_LAVAPIPE_ICD": icd_file, "LATTE_VK_LAYER_PATH": layer_dir}
    )
    assert cfg == DeviceConfig("lavapipe", icd_file, layer_dir)
    assert cfg.is_lavapipe


def test_resolve_device_explicit_lavapipe_matches_default(icd_file: str, layer_dir: str) -> None:
    cfg = resolve_device_config(
        {
            "SCENEPROBE_DEVICE": "lavapipe",
            "LATTE_VULKAN_LAVAPIPE_ICD": icd_file,
            "LATTE_VK_LAYER_PATH": layer_dir,
        }
    )
    assert cfg.name == "lavapipe"
    assert cfg.icd == icd_file


def test_resolve_device_dgpu_forces_no_icd(layer_dir: str) -> None:
    # dgpu leaves the loader to enumerate the host's drivers, so no ICD is
    # forced and LP_NUM_THREADS never applies.
    cfg = resolve_device_config({"SCENEPROBE_DEVICE": "dgpu", "LATTE_VK_LAYER_PATH": layer_dir})
    assert cfg == DeviceConfig("dgpu", "", layer_dir)
    assert not cfg.is_lavapipe


def test_resolve_device_missing_lavapipe_icd_refuses_with_bash_wording(layer_dir: str) -> None:
    with pytest.raises(DeviceDispatchError) as excinfo:
        resolve_device_config({"LATTE_VK_LAYER_PATH": layer_dir})
    assert str(excinfo.value) == (
        "lavapipe ICD not found (LATTE_VULKAN_LAVAPIPE_ICD unset or missing; "
        "run inside the flake devShell)"
    )


def test_resolve_device_icd_path_that_does_not_exist_refuses(
    tmp_path: Path, layer_dir: str
) -> None:
    missing = str(tmp_path / "gone.json")
    with pytest.raises(DeviceDispatchError):
        resolve_device_config(
            {"LATTE_VULKAN_LAVAPIPE_ICD": missing, "LATTE_VK_LAYER_PATH": layer_dir}
        )


def test_resolve_device_unknown_device_refuses_with_bash_wording(
    icd_file: str, layer_dir: str
) -> None:
    with pytest.raises(DeviceDispatchError) as excinfo:
        resolve_device_config(
            {
                "SCENEPROBE_DEVICE": "nvidia",
                "LATTE_VULKAN_LAVAPIPE_ICD": icd_file,
                "LATTE_VK_LAYER_PATH": layer_dir,
            }
        )
    assert str(excinfo.value) == (
        "unsupported SCENEPROBE_DEVICE 'nvidia': lavapipe (default, CI tier) "
        "or dgpu (opt-in host GPU)"
    )


def test_resolve_device_missing_layers_refuses_even_with_a_good_icd(icd_file: str) -> None:
    with pytest.raises(DeviceDispatchError) as excinfo:
        resolve_device_config({"LATTE_VULKAN_LAVAPIPE_ICD": icd_file})
    assert str(excinfo.value) == (
        "validation layer manifests not found (LATTE_VK_LAYER_PATH unset or missing; "
        "run inside the flake devShell)"
    )


def test_resolve_device_layers_pointing_at_a_file_refuses(icd_file: str, tmp_path: Path) -> None:
    # The layer path must be a DIRECTORY of manifests (bash `[ -d ]`); a plain
    # file is refused.
    not_a_dir = tmp_path / "layers.txt"
    not_a_dir.write_text("x")
    with pytest.raises(DeviceDispatchError):
        resolve_device_config(
            {"LATTE_VULKAN_LAVAPIPE_ICD": icd_file, "LATTE_VK_LAYER_PATH": str(not_a_dir)}
        )


# ---- build_probe_env -------------------------------------------------------


def test_build_probe_env_lavapipe_strips_x11_and_forces_vulkan() -> None:
    device = DeviceConfig("lavapipe", "/icd.json", "/layers")
    base = {"DISPLAY": ":0", "XAUTHORITY": "/x", "PATH": "/bin", "SCENEPROBE_TIER": "bitexact"}
    env = build_probe_env(base, Path("/run/nested"), "sceneprobe-wl", device)
    assert "DISPLAY" not in env
    assert "XAUTHORITY" not in env
    # Inherited gate exports survive (the probe reads SCENEPROBE_TIER etc.).
    assert env["PATH"] == "/bin"
    assert env["SCENEPROBE_TIER"] == "bitexact"
    assert env["QT_QPA_PLATFORM"] == "wayland"
    assert env["WAYLAND_DISPLAY"] == "sceneprobe-wl"
    assert env["XDG_RUNTIME_DIR"] == "/run/nested"
    assert env["QSG_RHI_BACKEND"] == "vulkan"
    assert env["LP_NUM_THREADS"] == "0"
    assert env["VK_ICD_FILENAMES"] == "/icd.json"
    assert env["VK_LAYER_PATH"] == "/layers"


def test_build_probe_env_dgpu_omits_lavapipe_only_vars() -> None:
    device = DeviceConfig("dgpu", "", "/layers")
    env = build_probe_env({}, Path("/rt"), "wl", device)
    # dgpu leaves the loader's enumeration alone: no forced ICD, no thread pin.
    assert "LP_NUM_THREADS" not in env
    assert "VK_ICD_FILENAMES" not in env
    assert env["VK_LAYER_PATH"] == "/layers"


def test_build_probe_env_does_not_mutate_the_base_env() -> None:
    base = {"DISPLAY": ":0"}
    build_probe_env(base, Path("/rt"), "wl", DeviceConfig("dgpu", "", "/layers"))
    assert base == {"DISPLAY": ":0"}


# ---- verdict classifiers ---------------------------------------------------


@pytest.mark.parametrize(("exit_code", "passed"), [(0, True), (1, False), (2, False), (124, False)])
def test_scene_passed_only_clean_zero(exit_code: int, passed: bool) -> None:
    assert scene_passed(exit_code) is passed


@pytest.mark.parametrize(
    ("expected", "actual", "disagrees"),
    [(0, 0, False), (0, 1, True), (1, 1, False), (1, 0, True), (1, 2, True)],
)
def test_selftest_disagrees(expected: int, actual: int, disagrees: bool) -> None:
    assert selftest_disagrees(expected, actual) is disagrees


def test_self_test_table_matches_the_bash_wording() -> None:
    good, bad, blank = SELF_TESTS
    assert (good.filename, good.expected_exit) == ("selftest-good.qml", 0)
    assert good.broken_message == "GATE BROKEN: selftest-good failed"
    assert (bad.filename, bad.expected_exit) == ("selftest-bad.qml", 1)
    assert bad.broken_message.format(actual=2) == "GATE BROKEN: selftest-bad exited 2, expected 1"
    assert (blank.filename, blank.expected_exit) == ("selftest-blank.qml", 1)
    assert (
        blank.broken_message.format(actual=3)
        == "GATE BROKEN: selftest-blank exited 3, expected 1 (output floor)"
    )
