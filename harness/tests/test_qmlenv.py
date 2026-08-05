# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The QML env assembly contract: import-list logic and manifest round-trip.

The negative controls carry the weight (the non-vacuous-guard rule): an empty
module-path component must NOT become ``-import .``, a leaked staged manifest
must be dropped not preserved, and the manifest must be restored even when the
staged body raises.
"""

import shlex
from pathlib import Path

import pytest

from latte_harness.qmlenv import (
    MissingModulePathError,
    assemble_imports,
    build_setup_script,
    parse_linked_store_prefixes,
    preserved_install_manifest,
    resolve_install_qmldir,
    seed_var_exports,
    strip_packaged_latte_dock,
)

# A realistic slice of a nixpkgs Qt6 seed var: KDE framework modules plus the
# packaged latte-dock leaf that must be stripped.
_PACKAGED = "/nix/store/aaaa-latte-dock-0.11/lib/qt-6/qml"
_LIBPLASMA = "/nix/store/bbbb-libplasma-6.7.3/lib/qt-6/qml"
_KIRIGAMI = "/nix/store/cccc-kirigami-6.28.0/lib/qt-6/qml"


def test_strip_removes_only_the_packaged_leaf() -> None:
    value = f"{_KIRIGAMI}:{_PACKAGED}:{_LIBPLASMA}"
    assert strip_packaged_latte_dock(value) == f"{_KIRIGAMI}:{_LIBPLASMA}"


def test_strip_leaves_a_leaf_free_path_untouched() -> None:
    # Negative control: nothing matches the deny regex, nothing is dropped.
    value = f"{_KIRIGAMI}:{_LIBPLASMA}"
    assert strip_packaged_latte_dock(value) == value


def test_strip_preserves_empty_components() -> None:
    # Byte-for-byte with bash tr|grep|paste: a middle "::" stays "::".
    assert strip_packaged_latte_dock(f"{_KIRIGAMI}::{_LIBPLASMA}") == f"{_KIRIGAMI}::{_LIBPLASMA}"


def test_strip_of_only_the_leaf_yields_empty() -> None:
    assert strip_packaged_latte_dock(_PACKAGED) == ""


def test_seed_var_exports_strip_the_leaf_from_each_var() -> None:
    # The D277 ctest hermeticity surface: the packaged leaf is dropped, the
    # framework modules stay, and a leaf-only var re-exports as empty (still
    # masking the package) rather than disappearing.
    env = {
        "NIXPKGS_QT6_QML_IMPORT_PATH": f"{_KIRIGAMI}:{_PACKAGED}:{_LIBPLASMA}",
        "NIXPKGS_QML_SEARCH_PATHS": _PACKAGED,
    }
    assert seed_var_exports(env) == [
        f"export NIXPKGS_QT6_QML_IMPORT_PATH={_KIRIGAMI}:{_LIBPLASMA}",
        "export NIXPKGS_QML_SEARCH_PATHS=''",
    ]


def test_seed_var_exports_skip_absent_and_empty_vars() -> None:
    assert seed_var_exports({}) == []
    assert seed_var_exports({"NIXPKGS_QT6_QML_IMPORT_PATH": ""}) == []


def test_parse_ldd_prefixes_sorted_unique() -> None:
    ldd = (
        f"\tlibplasma.so.6 => {_LIBPLASMA[: -len('/lib/qt-6/qml')]}/lib/libplasma.so.6 (0x1)\n"
        f"\tlibKirigami.so => {_KIRIGAMI[: -len('/lib/qt-6/qml')]}/lib/libKirigami.so (0x2)\n"
        # a second reference to the same store prefix must dedupe:
        f"\tlibplasma2.so => {_LIBPLASMA[: -len('/lib/qt-6/qml')]}/lib/libplasma2.so (0x3)\n"
    )
    prefixes = parse_linked_store_prefixes(ldd)
    assert prefixes == sorted(
        {
            _LIBPLASMA[: -len("/lib/qt-6/qml")],
            _KIRIGAMI[: -len("/lib/qt-6/qml")],
        }
    )


def test_parse_ldd_ignores_non_store_lines() -> None:
    # Negative control: the vdso, the loader, and a system lib all lack a
    # "=> /nix/store/<pkg>/" and must contribute nothing.
    ldd = (
        "\tlinux-vdso.so.1 (0x00007fff)\n"
        "\t/lib64/ld-linux-x86-64.so.2 (0x00007f00)\n"
        "\tlibc.so.6 => /usr/lib/libc.so.6 (0x00007f01)\n"
    )
    assert parse_linked_store_prefixes(ldd) == []


def test_resolve_qmldir_reads_the_marker(tmp_path: Path) -> None:
    (tmp_path / "latte-qmldir.txt").write_text("lib/qt6/qml\n")
    assert resolve_install_qmldir(tmp_path) == "lib/qt6/qml"


def test_resolve_qmldir_defaults_when_absent(tmp_path: Path) -> None:
    assert resolve_install_qmldir(tmp_path) == "lib/qml"


@pytest.mark.parametrize("body", ["", "\n", "   \n"])
def test_resolve_qmldir_defaults_on_empty_marker(tmp_path: Path, body: str) -> None:
    # Negative control: an empty or whitespace-only marker must not become the
    # import path "-import <stage>/" - it falls back to lib/qml.
    (tmp_path / "latte-qmldir.txt").write_text(body)
    assert resolve_install_qmldir(tmp_path) == "lib/qml"


def test_assemble_imports_orders_module_then_staged_last(tmp_path: Path) -> None:
    module_a = tmp_path / "mod-a"
    module_b = tmp_path / "mod-b"
    module_a.mkdir()
    module_b.mkdir()
    build = tmp_path / "build"  # no binaries -> the linked-provider leg is empty
    stage = tmp_path / "stage"
    module_path = f"{module_a}:{module_b}"

    imports = assemble_imports(module_path, build, stage, "lib/qml")

    assert imports == [
        "-import",
        str(module_a),
        "-import",
        str(module_b),
        "-import",
        str(stage / "lib/qml"),
    ]


def test_assemble_imports_skips_missing_and_empty_module_dirs(tmp_path: Path) -> None:
    real = tmp_path / "real"
    real.mkdir()
    missing = tmp_path / "gone"
    build = tmp_path / "build"
    stage = tmp_path / "stage"
    # An empty component (the "::" and trailing ":") is the trap: bash's
    # [[ -d "" ]] is false, but Path("").is_dir() is "." (true). The guard
    # must skip it, so no "-import ." appears.
    module_path = f"{real}::{missing}:"

    imports = assemble_imports(module_path, build, stage, "lib/qml")

    assert "." not in imports
    assert str(missing) not in imports
    assert imports == ["-import", str(real), "-import", str(stage / "lib/qml")]


def test_build_setup_script_shape(tmp_path: Path) -> None:
    module = tmp_path / "mod"
    module.mkdir()
    repo = tmp_path / "repo"
    build = repo / "build"  # no marker, no binaries
    env = {
        "LATTE_QML_MODULE_PATH": str(module),
        "NIXPKGS_QT6_QML_IMPORT_PATH": f"{_KIRIGAMI}:{_PACKAGED}",
    }

    script = build_setup_script(repo, env)
    lines = script.splitlines()

    assert f"build={shlex.quote(str(build))}" in lines
    assert f"stage={shlex.quote(str(build / '_qmlstage'))}" in lines
    assert "qmldir=lib/qml" in lines  # shlex.quote leaves a slash-only word bare
    assert "unset QML2_IMPORT_PATH QML_IMPORT_PATH" in lines
    # The seed var is re-exported with the packaged leaf stripped.
    assert f"export NIXPKGS_QT6_QML_IMPORT_PATH={shlex.quote(_KIRIGAMI)}" in lines
    # The unset seed var produces no export line at all.
    assert not any(line.startswith("export NIXPKGS_QML_SEARCH_PATHS=") for line in lines)
    # The imports array round-trips through shlex back to the token list.
    imports_line = next(line for line in lines if line.startswith("imports=("))
    inner = imports_line[len("imports=(") : -1]
    staged = str(build / "_qmlstage/lib/qml")
    assert shlex.split(inner) == ["-import", str(module), "-import", staged]


def test_build_setup_script_honours_build_and_stage_overrides(tmp_path: Path) -> None:
    module = tmp_path / "mod"
    module.mkdir()
    build = tmp_path / "custom-build"
    stage = tmp_path / "custom-stage"
    env = {
        "LATTE_QML_MODULE_PATH": str(module),
        "BUILD": str(build),
        "STAGE": str(stage),
    }
    lines = build_setup_script(tmp_path / "repo", env).splitlines()
    assert f"build={shlex.quote(str(build))}" in lines
    assert f"stage={shlex.quote(str(stage))}" in lines


def test_build_setup_script_omits_empty_seed_var(tmp_path: Path) -> None:
    module = tmp_path / "mod"
    module.mkdir()
    env = {"LATTE_QML_MODULE_PATH": str(module), "NIXPKGS_QT6_QML_IMPORT_PATH": ""}
    lines = build_setup_script(tmp_path / "repo", env).splitlines()
    # Empty (":-" default in bash) means "leave untouched": no export emitted.
    assert not any(line.startswith("export NIXPKGS_QT6_QML_IMPORT_PATH=") for line in lines)


def test_build_setup_script_refuses_without_module_path(tmp_path: Path) -> None:
    with pytest.raises(MissingModulePathError):
        build_setup_script(tmp_path / "repo", {})


def test_build_setup_script_quotes_paths_with_spaces(tmp_path: Path) -> None:
    module = tmp_path / "has space"
    module.mkdir()
    env = {"LATTE_QML_MODULE_PATH": str(module)}
    lines = build_setup_script(tmp_path / "repo", env).splitlines()
    imports_line = next(line for line in lines if line.startswith("imports=("))
    inner = imports_line[len("imports=(") : -1]
    # The space survives an eval-equivalent shlex round-trip as one token.
    assert str(module) in shlex.split(inner)


def _write(path: Path, text: str) -> None:
    path.write_text(text)


def test_manifest_preserved_across_staging(tmp_path: Path) -> None:
    manifest = tmp_path / "install_manifest.txt"
    stage = tmp_path / "_qmlstage"
    original = "/usr/lib/real-install-path\n"
    _write(manifest, original)

    with preserved_install_manifest(manifest, stage):
        # cmake --install rewrites the manifest with staged paths mid-run.
        _write(manifest, f"{stage}/share/staged\n")

    assert manifest.read_text() == original
    assert not (tmp_path / "install_manifest.txt.pre-stage").exists()


def test_manifest_leaked_staged_is_dropped(tmp_path: Path) -> None:
    manifest = tmp_path / "install_manifest.txt"
    stage = tmp_path / "_qmlstage"
    # A prior interrupted run left a staged manifest behind: never a real
    # state, so it is self-healed away, and the new staged one is dropped too.
    _write(manifest, f"{stage}/share/leaked\n")

    with preserved_install_manifest(manifest, stage):
        assert not manifest.exists()  # dropped on entry
        _write(manifest, f"{stage}/share/staged\n")

    assert not manifest.exists()
    assert not (tmp_path / "install_manifest.txt.pre-stage").exists()


def test_manifest_absent_leaves_no_staged_manifest(tmp_path: Path) -> None:
    manifest = tmp_path / "install_manifest.txt"
    stage = tmp_path / "_qmlstage"

    with preserved_install_manifest(manifest, stage):
        _write(manifest, f"{stage}/share/staged\n")

    # No manifest existed before; the staged one is rm -f'd on the way out.
    assert not manifest.exists()


def test_manifest_restored_on_exception(tmp_path: Path) -> None:
    manifest = tmp_path / "install_manifest.txt"
    stage = tmp_path / "_qmlstage"
    original = "/usr/lib/real-install-path\n"
    _write(manifest, original)

    with pytest.raises(RuntimeError, match="stage blew up"):  # noqa: SIM117
        with preserved_install_manifest(manifest, stage):
            _write(manifest, f"{stage}/share/staged\n")
            raise RuntimeError("stage blew up")

    # The finally-path restored the real manifest despite the raise.
    assert manifest.read_text() == original
    assert not (tmp_path / "install_manifest.txt.pre-stage").exists()
