# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The staged-run env-assembly contract: every variable, mirrored from the bash.

The negative controls carry the weight (the non-vacuous-guard rule): the packaged
latte-dock leaf must be stripped from the seed vars, an incoming QML2_IMPORT_PATH
must be REPLACED (not appended) by the pinned import path, QML_IMPORT_PATH and
QT_PLUGIN_PATH must be dropped, and the XDG_DATA_DIRS allow-list must keep only
the KDE runtime leaves. argv forwarding must consume --user-config and forward
the rest, and a missing binary must refuse (exit 2) before anything is staged.
"""

from pathlib import Path

import pytest

from latte_harness import qmlenv
from latte_harness.staged_run import (
    MissingHomeError,
    StagedRun,
    assemble_dock_env,
    main,
    parse_kwindowsystem_prefix,
    rebuild_xdg_data_dirs,
    resolve_confighome,
    seed_throwaway_kdeglobals,
)

# Seed-var slices: the packaged latte-dock leaf that must be stripped, plus a KDE
# framework leaf that must survive.
_PACKAGED_QML = "/nix/store/aaaa-latte-dock-0.11/lib/qt-6/qml"
_LIBPLASMA_QML = "/nix/store/bbbb-libplasma-6.7.3/lib/qt-6/qml"

# XDG_DATA_DIRS slices: two allow-list matches and one non-match.
_LIBPLASMA_SHARE = "/nix/store/bbbb-libplasma-6.7.3/share"
_KIRIGAMI_SHARE = "/nix/store/cccc-kirigami-6.28.0/share"
_RANDOM_SHARE = "/nix/store/dddd-random-1.0/share"


def _base_env(
    tmp_path: Path, *, extra: dict[str, str] | None = None
) -> tuple[dict[str, str], Path, Path]:
    module = tmp_path / "modules"
    module.mkdir()
    build = tmp_path / "build"
    build.mkdir()
    home = tmp_path / "home"
    (home / ".config").mkdir(parents=True)
    env: dict[str, str] = {
        "LATTE_QML_MODULE_PATH": str(module),
        "BUILD": str(build),
        "HOME": str(home),
        "USER": "tester",
        # kept, non-match, kept, then a duplicate of the first (dedup-first-wins).
        "XDG_DATA_DIRS": ":".join(
            [_LIBPLASMA_SHARE, _RANDOM_SHARE, _KIRIGAMI_SHARE, _LIBPLASMA_SHARE]
        ),
        "NIXPKGS_QT6_QML_IMPORT_PATH": f"{_PACKAGED_QML}:{_LIBPLASMA_QML}",
        "WAYLAND_DISPLAY": "wayland-7",
        # These three must not survive into the dock's env verbatim.
        "QML_IMPORT_PATH": "/leak/qml-import",
        "QT_PLUGIN_PATH": "/leak/qt-plugins",
        "QML2_IMPORT_PATH": "/pre/existing/must/be/replaced",
    }
    if extra:
        env.update(extra)
    return env, module, build


def _expected_importpath(module: Path, build: Path) -> str:
    # No binary in the tmp build, so the linked-provider leg is empty: module dir
    # first, staged tree last (qmldir defaults to lib/qml with no marker).
    return f"{module}:{build / '_qmlstage' / 'lib/qml'}"


def _expected_data_dirs(build: Path, home: Path) -> str:
    stage = build / "_qmlstage"
    return ":".join(
        [
            f"{stage}/share",
            _LIBPLASMA_SHARE,
            _KIRIGAMI_SHARE,
            f"{home}/.nix-profile/share",
            "/etc/profiles/per-user/tester/share",
            "/run/current-system/sw/share",
            "/usr/share",
        ]
    )


def test_assemble_sets_every_dock_variable(tmp_path: Path) -> None:
    env, module, build = _base_env(tmp_path)

    run = assemble_dock_env(tmp_path / "repo", env, ["-d"])

    assert isinstance(run, StagedRun)
    # The pinned import path replaces the inherited one wholesale.
    assert run.env["QML2_IMPORT_PATH"] == _expected_importpath(module, build)
    assert "/pre/existing/must/be/replaced" not in run.env["QML2_IMPORT_PATH"]
    # The two profile QML vars are dropped, never inherited.
    assert "QML_IMPORT_PATH" not in run.env
    assert "QT_PLUGIN_PATH" not in run.env
    # The packaged latte-dock leaf is stripped from the seed var, the framework
    # leaf survives (the D8/D271 shadowing doctrine).
    assert run.env["NIXPKGS_QT6_QML_IMPORT_PATH"] == _LIBPLASMA_QML
    # The throwaway config home and the rebuilt data dirs.
    assert run.env["XDG_CONFIG_HOME"] == str(build / "_runconfig")
    assert run.confighome == build / "_runconfig"
    assert run.env["XDG_DATA_DIRS"] == _expected_data_dirs(build, tmp_path / "home")
    # The Qt platform pins.
    assert run.env["QT_QPA_PLATFORM"] == "wayland"
    assert run.env["QT_QPA_PLATFORMTHEME"] == ""
    # No built binary in the tmp build, so no kwindowsystem leaf appended.
    assert run.env["LATTE_EXTRA_PLUGIN_PATHS"] == f"{build / '_qmlstage'}/lib/plugins"
    assert run.kwindowsystem_missing is True
    # argv: no wrapper, the binary path, the forwarded flag.
    assert run.exec_argv == [str(build / "bin/latte-dock"), "-d"]


def test_assemble_strip_is_a_noop_without_the_packaged_leaf(tmp_path: Path) -> None:
    # Negative control: a seed var carrying no latte-dock leaf is passed through
    # untouched, and an absent second seed var is never introduced.
    env, _module, _build = _base_env(
        tmp_path, extra={"NIXPKGS_QT6_QML_IMPORT_PATH": _LIBPLASMA_QML}
    )
    run = assemble_dock_env(tmp_path / "repo", env, [])
    assert run.env["NIXPKGS_QT6_QML_IMPORT_PATH"] == _LIBPLASMA_QML
    assert "NIXPKGS_QML_SEARCH_PATHS" not in run.env


def test_assemble_leaves_unrelated_env_untouched(tmp_path: Path) -> None:
    env, _module, _build = _base_env(tmp_path, extra={"KEEP_ME": "value"})
    run = assemble_dock_env(tmp_path / "repo", env, [])
    assert run.env["KEEP_ME"] == "value"
    assert run.env["WAYLAND_DISPLAY"] == "wayland-7"


def test_assemble_empty_data_dirs_is_stage_plus_tail(tmp_path: Path) -> None:
    env, _module, build = _base_env(tmp_path, extra={"XDG_DATA_DIRS": ""})
    run = assemble_dock_env(tmp_path / "repo", env, [])
    stage = build / "_qmlstage"
    assert run.env["XDG_DATA_DIRS"] == ":".join(
        [
            f"{stage}/share",
            f"{tmp_path / 'home'}/.nix-profile/share",
            "/etc/profiles/per-user/tester/share",
            "/run/current-system/sw/share",
            "/usr/share",
        ]
    )


def test_assemble_refuses_without_module_path(tmp_path: Path) -> None:
    env, _module, _build = _base_env(tmp_path)
    del env["LATTE_QML_MODULE_PATH"]
    with pytest.raises(qmlenv.MissingModulePathError):
        assemble_dock_env(tmp_path / "repo", env, [])


def test_assemble_honours_stage_override(tmp_path: Path) -> None:
    stage = tmp_path / "custom-stage"
    env, module, _build = _base_env(tmp_path, extra={"STAGE": str(stage)})
    run = assemble_dock_env(tmp_path / "repo", env, [])
    assert run.stage == stage
    assert run.env["QML2_IMPORT_PATH"] == f"{module}:{stage / 'lib/qml'}"
    assert run.env["LATTE_EXTRA_PLUGIN_PATHS"] == f"{stage}/lib/plugins"


# ---- argv forwarding -------------------------------------------------------


def test_user_config_is_consumed_and_rest_forwarded(tmp_path: Path) -> None:
    xdg = tmp_path / "real-config"
    xdg.mkdir()
    env, _module, build = _base_env(tmp_path, extra={"XDG_CONFIG_HOME": str(xdg)})
    run = assemble_dock_env(tmp_path / "repo", env, ["--user-config", "-d", "--foo"])
    # --user-config selects the real session config and is stripped from argv.
    assert run.confighome == xdg
    assert run.env["XDG_CONFIG_HOME"] == str(xdg)
    assert run.exec_argv == [str(build / "bin/latte-dock"), "-d", "--foo"]


def test_user_config_only_matches_first_arg(tmp_path: Path) -> None:
    # A --user-config that is not the first token is a normal forwarded flag.
    env, _module, build = _base_env(tmp_path)
    run = assemble_dock_env(tmp_path / "repo", env, ["-d", "--user-config"])
    assert run.confighome == build / "_runconfig"
    assert run.exec_argv == [str(build / "bin/latte-dock"), "-d", "--user-config"]


def test_run_wrapper_prepends_words(tmp_path: Path) -> None:
    env, _module, build = _base_env(
        tmp_path, extra={"LATTE_RUN_WRAPPER": "gdb -batch -ex run --args"}
    )
    run = assemble_dock_env(tmp_path / "repo", env, ["-d"])
    assert run.exec_argv == [
        "gdb",
        "-batch",
        "-ex",
        "run",
        "--args",
        str(build / "bin/latte-dock"),
        "-d",
    ]


# ---- resolve_confighome ----------------------------------------------------


def test_confighome_default_is_throwaway(tmp_path: Path) -> None:
    build = tmp_path / "build"
    assert resolve_confighome(build, {}, user_config=False) == build / "_runconfig"


def test_confighome_honours_latte_config_home(tmp_path: Path) -> None:
    build = tmp_path / "build"
    custom = tmp_path / "seed"
    got = resolve_confighome(build, {"LATTE_CONFIG_HOME": str(custom)}, user_config=False)
    assert got == custom


def test_confighome_user_config_uses_xdg(tmp_path: Path) -> None:
    build = tmp_path / "build"
    xdg = tmp_path / "xdg"
    got = resolve_confighome(build, {"XDG_CONFIG_HOME": str(xdg)}, user_config=True)
    assert got == xdg


def test_confighome_user_config_falls_back_to_home_config(tmp_path: Path) -> None:
    build = tmp_path / "build"
    got = resolve_confighome(build, {"HOME": str(tmp_path)}, user_config=True)
    assert got == tmp_path / ".config"


def test_confighome_user_config_without_home_refuses(tmp_path: Path) -> None:
    with pytest.raises(MissingHomeError):
        resolve_confighome(tmp_path / "build", {}, user_config=True)


# ---- rebuild_xdg_data_dirs -------------------------------------------------


def test_data_dirs_keeps_only_allow_listed_leaves(tmp_path: Path) -> None:
    stage = tmp_path / "stage"
    env = {
        "XDG_DATA_DIRS": ":".join([_LIBPLASMA_SHARE, _RANDOM_SHARE, _KIRIGAMI_SHARE]),
        "HOME": "/h",
        "USER": "u",
    }
    got = rebuild_xdg_data_dirs(stage, env)
    parts = got.split(":")
    assert parts[0] == f"{stage}/share"
    assert _LIBPLASMA_SHARE in parts
    assert _KIRIGAMI_SHARE in parts
    # The non-KDE leaf is dropped (allow-list, not a shared root).
    assert _RANDOM_SHARE not in parts
    assert parts[-4:] == [
        "/h/.nix-profile/share",
        "/etc/profiles/per-user/u/share",
        "/run/current-system/sw/share",
        "/usr/share",
    ]


def test_data_dirs_dedups_first_wins(tmp_path: Path) -> None:
    stage = tmp_path / "stage"
    env = {
        "XDG_DATA_DIRS": ":".join([_LIBPLASMA_SHARE, _LIBPLASMA_SHARE]),
        "HOME": "/h",
        "USER": "u",
    }
    got = rebuild_xdg_data_dirs(stage, env)
    assert got.split(":").count(_LIBPLASMA_SHARE) == 1


def test_data_dirs_skips_empty_components(tmp_path: Path) -> None:
    stage = tmp_path / "stage"
    # A leading, doubled, and trailing colon produce empty components that must
    # not become entries (bash `awk NF` skips them).
    env = {
        "XDG_DATA_DIRS": f":{_LIBPLASMA_SHARE}::",
        "HOME": "/h",
        "USER": "u",
    }
    got = rebuild_xdg_data_dirs(stage, env)
    assert "" not in got.split(":")[1:-4]  # the tail roots are all non-empty too


# ---- kwindowsystem ldd parsing --------------------------------------------


def test_parse_kwindowsystem_first_match_wins() -> None:
    prefix = "/nix/store/hhhh-kwindowsystem-6.28.0"
    ldd = (
        "\tlinux-vdso.so.1 (0x00007fff)\n"
        f"\tlibKF6WindowSystem.so.6 => {prefix}/lib/libKF6WindowSystem.so.6 (0x1)\n"
        "\tlibkwindowsystem-later => /nix/store/zzzz-kwindowsystem-9/lib/x.so (0x2)\n"
    )
    assert parse_kwindowsystem_prefix(ldd) == prefix


def test_parse_kwindowsystem_absent_is_none() -> None:
    ldd = (
        "\tlibc.so.6 => /usr/lib/libc.so.6 (0x1)\n"
        "\tlibplasma.so => /nix/store/x-libplasma-6/lib/y.so (0x2)\n"
    )
    assert parse_kwindowsystem_prefix(ldd) is None


# ---- kdeglobals seed -------------------------------------------------------


def test_kdeglobals_seeded_into_fresh_throwaway(tmp_path: Path) -> None:
    build = tmp_path / "build"
    confighome = build / "_runconfig"
    confighome.mkdir(parents=True)
    session = tmp_path / "session"
    session.mkdir()
    (session / "kdeglobals").write_text("[Colors]\n")
    seed_throwaway_kdeglobals(confighome, build, {"XDG_CONFIG_HOME": str(session)})
    assert (confighome / "kdeglobals").read_text() == "[Colors]\n"


def test_kdeglobals_not_seeded_when_present(tmp_path: Path) -> None:
    build = tmp_path / "build"
    confighome = build / "_runconfig"
    confighome.mkdir(parents=True)
    (confighome / "kdeglobals").write_text("existing\n")
    session = tmp_path / "session"
    session.mkdir()
    (session / "kdeglobals").write_text("session\n")
    seed_throwaway_kdeglobals(confighome, build, {"XDG_CONFIG_HOME": str(session)})
    # The existing throwaway kdeglobals is not overwritten.
    assert (confighome / "kdeglobals").read_text() == "existing\n"


def test_kdeglobals_not_seeded_for_non_throwaway(tmp_path: Path) -> None:
    build = tmp_path / "build"
    confighome = tmp_path / "elsewhere"
    confighome.mkdir()
    session = tmp_path / "session"
    session.mkdir()
    (session / "kdeglobals").write_text("session\n")
    seed_throwaway_kdeglobals(confighome, build, {"XDG_CONFIG_HOME": str(session)})
    # confighome is not build/_runconfig, so nothing is copied.
    assert not (confighome / "kdeglobals").exists()


# ---- main: the loud refusals (no exec on these paths) ----------------------


def test_main_refuses_without_module_path(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("LATTE_QML_MODULE_PATH", raising=False)
    with pytest.raises(SystemExit) as exc:
        main([])
    assert exc.value.code == 1


def test_main_refuses_missing_binary_before_staging(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    module = tmp_path / "modules"
    module.mkdir()
    build = tmp_path / "build"
    build.mkdir()
    monkeypatch.setenv("LATTE_QML_MODULE_PATH", str(module))
    monkeypatch.setenv("BUILD", str(build))
    monkeypatch.setenv("HOME", str(tmp_path))
    monkeypatch.setenv("USER", "tester")
    with pytest.raises(SystemExit) as exc:
        main([])
    assert exc.value.code == 2
    # Exit 2 is refused before staging, so no stage tree is created.
    assert not (build / "_qmlstage").exists()
