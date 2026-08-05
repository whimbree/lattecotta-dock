# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The compile-gate contract: file selection, generation, and env resolution.

The negative controls carry the weight (the non-vacuous-guard rule): a
version-ladder rung one step outside the range must NOT be skipped, a file that
is both app-module-dependent and a ladder rung must count as app only (the
bash's check order), and the resolved import list / offscreen env must match
qmlenv.build_setup_script exactly so the recompute cannot drift from the bridge.
"""

import shlex
from dataclasses import dataclass, field
from pathlib import Path

import pytest

from latte_harness.qml_compile_gate import (
    PACKAGE_QML_ROOTS,
    QmlEnv,
    classify_qml_files,
    generate_compile_testcase,
    is_app_module_dependent,
    is_dead_version_ladder,
    resolve_qml_env,
    scan_package_qml,
)
from latte_harness.qmlenv import (
    NIXPKGS_SEED_VARS,
    MissingModulePathError,
    build_setup_script,
)

_KIRIGAMI = "/nix/store/cccc-kirigami-6.28.0/lib/qt-6/qml"
_PACKAGED = "/nix/store/aaaa-latte-dock-0.11/lib/qt-6/qml"


# --- file selection --------------------------------------------------------


def test_app_module_dependency_detected_as_literal_substring() -> None:
    assert is_app_module_dependent("import org.kde.latte.private.app 0.1\n")
    assert not is_app_module_dependent("import org.kde.latte.core 0.2\n")


@pytest.mark.parametrize("rung", ["20", "21", "22", "23", "24", "25"])
def test_version_ladder_rungs_in_range_are_dead(rung: str) -> None:
    assert is_dead_version_ladder(Path(f"/stage/Foo.5.{rung}.qml"))


@pytest.mark.parametrize("rung", ["19", "26", "30"])
def test_version_ladder_rungs_outside_range_are_live(rung: str) -> None:
    # Negative control: the [0-5] boundary. .5.19 and .5.26 must NOT be skipped.
    assert not is_dead_version_ladder(Path(f"/stage/Foo.5.{rung}.qml"))


def test_plain_qml_is_neither_skip_class() -> None:
    assert not is_app_module_dependent("import QtQuick\n")
    assert not is_dead_version_ladder(Path("/stage/Main.qml"))


def test_classify_counts_and_partitions() -> None:
    contents = {
        Path("/s/a.qml"): "import QtQuick\n",  # kept
        Path("/s/b.qml"): "import org.kde.latte.private.app\n",  # app
        Path("/s/Foo.5.22.qml"): "import QtQuick\n",  # ladder
        Path("/s/c.qml"): "import org.kde.latte.core\n",  # kept
    }
    selection = classify_qml_files(list(contents), read_text=lambda p: contents[p])
    assert selection.skipped_app == 1
    assert selection.skipped_ladder == 1
    assert selection.files == (Path("/s/a.qml"), Path("/s/c.qml"))


def test_classify_app_check_wins_over_ladder() -> None:
    # A file that is BOTH a ladder rung AND app-module-dependent counts as app
    # only, never ladder - the bash checks app first and continues.
    path = Path("/s/Tip.5.20.qml")
    contents = {path: "import org.kde.latte.private.app\n"}
    selection = classify_qml_files([path], read_text=lambda p: contents[p])
    assert selection.skipped_app == 1
    assert selection.skipped_ladder == 0
    assert selection.files == ()


def test_classify_empty_input() -> None:
    selection = classify_qml_files([], read_text=lambda _p: "")
    assert selection == type(selection)((), 0, 0)


# --- scanning --------------------------------------------------------------


def test_scan_finds_recursively_across_roots_sorted(tmp_path: Path) -> None:
    stage = tmp_path / "stage"
    shell = stage / PACKAGE_QML_ROOTS[0] / "contents" / "ui"
    indicators = stage / PACKAGE_QML_ROOTS[3] / "default" / "ui"
    shell.mkdir(parents=True)
    indicators.mkdir(parents=True)
    (shell / "main.qml").write_text("import QtQuick\n")
    (indicators / "z.qml").write_text("import QtQuick\n")
    (indicators / "a.qml").write_text("import QtQuick\n")
    # A non-qml file and a file outside the roots must be ignored.
    (shell / "notes.txt").write_text("x")
    outside = stage / "share" / "other"
    outside.mkdir(parents=True)
    (outside / "stray.qml").write_text("import QtQuick\n")

    found = scan_package_qml(stage)

    assert found == sorted(
        [shell / "main.qml", indicators / "z.qml", indicators / "a.qml"],
        key=str,
    )
    assert all("stray.qml" not in str(p) for p in found)


def test_scan_missing_roots_contribute_nothing(tmp_path: Path) -> None:
    # Negative control: no roots exist at all -> empty, no error (find 2>/dev/null).
    assert scan_package_qml(tmp_path / "nonexistent-stage") == []


# --- generated TestCase ----------------------------------------------------


def test_generate_testcase_exact_shape() -> None:
    text = generate_compile_testcase([Path("/abs/a.qml"), Path("/abs/b.qml")])
    expected = (
        "import QtQuick\n"
        "import QtTest\n"
        "TestCase {\n"
        '    name: "QmlCompileGate"\n'
        "    property var files: [\n"
        '        "file:///abs/a.qml",\n'
        '        "file:///abs/b.qml",\n'
        "    ]\n"
        "    function test_compileAll() {\n"
        "        var failed = [];\n"
        "        for (var i = 0; i < files.length; i++) {\n"
        "            var c = Qt.createComponent(files[i]);\n"
        "            if (c.status === Component.Error) {\n"
        '                console.warn("FAIL " + files[i] + "\\n      " + c.errorString().trim());\n'
        "                failed.push(files[i]);\n"
        "            }\n"
        "            if (c) c.destroy();\n"
        "        }\n"
        '        console.warn("=== " + failed.length + " of " + files.length'
        ' + " package QML files failed to compile ===");\n'
        '        verify(failed.length === 0, failed.length + " QML files failed to compile");\n'
        "    }\n"
        "}\n"
    )
    assert text == expected


def test_generate_testcase_empty_file_list_is_valid() -> None:
    text = generate_compile_testcase([])
    # An empty compile set (everything skipped) still produces a valid TestCase
    # with an empty array - the bash proceeds and compiles zero files (exit 0).
    assert "property var files: [\n    ]\n" in text
    assert text.endswith("}\n")


def test_generate_testcase_absolute_path_yields_triple_slash() -> None:
    # An absolute path gives file:// + /abs = file:///abs (three slashes).
    text = generate_compile_testcase([Path("/x/y.qml")])
    assert '        "file:///x/y.qml",' in text


# --- env resolution --------------------------------------------------------


def _env(module: Path, **extra: str) -> dict[str, str]:
    base = {"LATTE_QML_MODULE_PATH": str(module)}
    base.update(extra)
    return base


def test_resolve_env_refuses_without_module_path(tmp_path: Path) -> None:
    with pytest.raises(MissingModulePathError):
        resolve_qml_env(tmp_path / "repo", {})


def test_resolve_env_defaults_build_and_stage(tmp_path: Path) -> None:
    module = tmp_path / "mod"
    module.mkdir()
    repo = tmp_path / "repo"
    env = resolve_qml_env(repo, _env(module))
    assert env.build == repo / "build"
    assert env.stage == repo / "build" / "_qmlstage"
    assert env.qmldir == "lib/qml"  # no marker -> default


def test_resolve_env_honours_build_and_stage_overrides(tmp_path: Path) -> None:
    module = tmp_path / "mod"
    module.mkdir()
    build = tmp_path / "custom-build"
    stage = tmp_path / "custom-stage"
    env = resolve_qml_env(tmp_path / "repo", _env(module, BUILD=str(build), STAGE=str(stage)))
    assert env.build == build
    assert env.stage == stage


def test_resolve_env_empty_build_falls_back_to_default(tmp_path: Path) -> None:
    # A set-but-empty BUILD is falsy in the bash (":-" default) and here too.
    module = tmp_path / "mod"
    module.mkdir()
    repo = tmp_path / "repo"
    env = resolve_qml_env(repo, _env(module, BUILD=""))
    assert env.build == repo / "build"


def test_resolve_env_child_env_mutations(tmp_path: Path) -> None:
    module = tmp_path / "mod"
    module.mkdir()
    env = resolve_qml_env(
        tmp_path / "repo",
        _env(
            module,
            QML2_IMPORT_PATH="/should/be/dropped",
            QML_IMPORT_PATH="/dropped/too",
            NIXPKGS_QT6_QML_IMPORT_PATH=f"{_KIRIGAMI}:{_PACKAGED}",
        ),
    )
    child = env.child_env
    assert "QML2_IMPORT_PATH" not in child
    assert "QML_IMPORT_PATH" not in child
    assert child["QT_QPA_PLATFORM"] == "offscreen"
    # The seed var re-exported with only the packaged latte-dock leaf stripped.
    assert child["NIXPKGS_QT6_QML_IMPORT_PATH"] == _KIRIGAMI


@dataclass
class _ParsedSetup:
    """The values the bridge's ``eval "$_out"`` would have set in the shell."""

    build: str = ""
    stage: str = ""
    qmldir: str = ""
    unset: list[str] = field(default_factory=list)
    exports: dict[str, str] = field(default_factory=dict)
    imports: list[str] = field(default_factory=list)


def _scalar(rhs: str) -> str:
    parts = shlex.split(rhs)
    return parts[0] if parts else ""


def _parse_setup_script(script: str) -> _ParsedSetup:
    """Interpret the bridge's eval-able shell into the values it would set.

    This is the bash ``eval "$_out"`` the five consumers ran, in miniature:
    it lets the test assert the recompute in resolve_qml_env matches the
    canonical qmlenv.build_setup_script byte-for-byte (the drift net).
    """
    parsed = _ParsedSetup()
    for line in script.splitlines():
        key, _, rhs = line.partition("=")
        if key == "build":
            parsed.build = _scalar(rhs)
        elif key == "stage":
            parsed.stage = _scalar(rhs)
        elif key == "qmldir":
            parsed.qmldir = _scalar(rhs)
        elif line.startswith("unset "):
            parsed.unset = line.removeprefix("unset ").split()
        elif line.startswith("export "):
            name, _, export_rhs = line.removeprefix("export ").partition("=")
            parsed.exports[name] = _scalar(export_rhs)
        elif line.startswith("imports=("):
            parsed.imports = shlex.split(line[len("imports=(") : -1])
    return parsed


def test_resolve_env_matches_build_setup_script(tmp_path: Path) -> None:
    module = tmp_path / "mod"
    module.mkdir()
    repo = tmp_path / "repo"
    # Set EVERY canonical seed var (the public NIXPKGS_SEED_VARS list) so
    # the net covers additions too: a var qmlenv starts stripping that the
    # gate's child_env does not apply fails the per-var comparison below.
    raw = _env(
        module,
        **dict.fromkeys(NIXPKGS_SEED_VARS, f"{_KIRIGAMI}:{_PACKAGED}"),
    )

    env = resolve_qml_env(repo, raw)
    parsed = _parse_setup_script(build_setup_script(repo, raw))

    assert str(env.build) == parsed.build
    assert str(env.stage) == parsed.stage
    assert env.qmldir == parsed.qmldir
    assert list(env.imports) == parsed.imports
    # The env mutations the bridge produced match what child_env applied.
    assert parsed.unset == ["QML2_IMPORT_PATH", "QML_IMPORT_PATH"]
    # Every canonical seed var produced an export line, and each matches
    # what child_env applied - additions to NIXPKGS_SEED_VARS are covered
    # because raw sets the whole list.
    assert set(parsed.exports) == set(NIXPKGS_SEED_VARS)
    for name, value in parsed.exports.items():
        assert env.child_env[name] == value


def test_qmlenv_is_frozen() -> None:
    module = QmlEnv(Path("/b"), Path("/s"), "lib/qml", (), {})
    with pytest.raises((AttributeError, TypeError)):
        module.qmldir = "other"  # type: ignore[misc]
