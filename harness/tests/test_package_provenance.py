# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
"""The package-provenance core's contracts: ELF search-path parsing,
/proc/maps parsing, the mapped-path provenance classification, the
expected-mapping registry forms, the qtplugininfo version probe, the
process-environment readback, the plugin metadata contracts, and the
AppStream validator - each with hostile inputs and exact-message
assertions, because the refusal text is what the selftest and a packager
act on.
"""

from pathlib import Path

import pytest
from pydantic import JsonValue

from latte_harness.package_provenance import (
    ExpectedMappingRegistry,
    MappedPathViolationKind,
    ProvenanceError,
    audit_mapped_paths,
    declares_containment_actions_contract,
    declares_indicator_structure_contract,
    find_development_provider,
    is_latte_runtime_path,
    parse_elf_search_paths,
    parse_mapped_paths,
    path_is_within,
    read_environment_value,
    read_mapped_paths,
    validate_appstream_metadata,
    version_output_names_qt6_tool,
)

# ---- ELF search-path parsing -------------------------------------------------


def test_parse_elf_search_paths_reads_runpath() -> None:
    output = (
        "Dynamic section at offset 0x2df8 contains 27 entries:\n"
        "  Tag        Type                         Name/Value\n"
        " 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]\n"
        " 0x000000000000001d (RUNPATH)            Library runpath: [$ORIGIN]\n"
    )
    assert parse_elf_search_paths(output) == ["$ORIGIN"]


def test_parse_elf_search_paths_reads_rpath_and_multiple_entries() -> None:
    output = (
        " 0x000000000000000f (RPATH)              Library rpath: [$ORIGIN/../lib:$ORIGIN]\n"
        " 0x000000000000001d (RUNPATH)            Library runpath: [/usr/lib]\n"
    )
    assert parse_elf_search_paths(output) == ["$ORIGIN/../lib:$ORIGIN", "/usr/lib"]


def test_parse_elf_search_paths_no_search_paths() -> None:
    assert parse_elf_search_paths(" 0x01 (NEEDED) Shared library: [libc.so.6]\n") == []


def test_parse_elf_search_paths_empty_value_collapses() -> None:
    # An ELF whose only RUNPATH is [] yields no entries at all (the bash
    # command-substitution collapse), not one empty entry.
    assert parse_elf_search_paths(" 0x1d (RUNPATH)   Library runpath: []\n") == []
    assert (
        parse_elf_search_paths(
            " 0x1d (RUNPATH)   Library runpath: []\n 0x0f (RPATH)   Library rpath: []\n"
        )
        == []
    )


def test_parse_elf_search_paths_interior_empty_survives_trailing_dropped() -> None:
    both = " 0x1d (RUNPATH)   Library runpath: []\n 0x0f (RPATH)     Library rpath: [$ORIGIN]\n"
    assert parse_elf_search_paths(both) == ["", "$ORIGIN"]
    reversed_order = (
        " 0x0f (RPATH)     Library rpath: [$ORIGIN]\n 0x1d (RUNPATH)   Library runpath: []\n"
    )
    assert parse_elf_search_paths(reversed_order) == ["$ORIGIN"]


def test_parse_elf_search_paths_hostile_bracketless_line_passes_through() -> None:
    # A matched line without brackets survives the failed substitutions
    # unchanged, exactly like awk sub(); the engine's downstream entry
    # validation then refuses it as a non-$ORIGIN relative entry.
    line = " 0x1d (RUNPATH) mangled without brackets"
    assert parse_elf_search_paths(line + "\n") == [line]


# ---- qtplugininfo version probe ----------------------------------------------


@pytest.mark.parametrize(
    "output",
    [
        "qtplugininfo 6.9.1",
        "qtplugininfo 6.9.1\n",
        "qplugininfo 6.0.0",
        "qtplugininfo6 6.9",
        "qtplugininfo-qt6 6.10.2",
    ],
)
def test_version_probe_accepts_qt6_reports(output: str) -> None:
    assert version_output_names_qt6_tool(output)


@pytest.mark.parametrize(
    "output",
    [
        "qplugininfo 5.15.2",
        "diagnostic 6.99.0\nqplugininfo 5.15.2",  # misleading multi-line report
        "qtplugininfo v6.9.1",
        "someothertool 6.9.1",
        "qtplugininfo 6",
        "",
    ],
)
def test_version_probe_rejects_non_qt6_reports(output: str) -> None:
    assert not version_output_names_qt6_tool(output)


# ---- /proc/maps parsing ------------------------------------------------------


def test_parse_mapped_paths_extracts_pathnames_and_skips_pseudo_entries() -> None:
    maps = (
        "1000-2000 r-xp 00000000 08:01 42 /usr/bin/latte-dock\n"
        "2000-3000 rw-p 00000000 00:00 0\n"
        "3000-4000 rw-p 00000000 00:00 0 [heap]\n"
        "4000-5000 r--p 00000000 08:01 43 /usr/lib/libQt6Core.so.6\n"
    )
    assert parse_mapped_paths(maps) == ["/usr/bin/latte-dock", "/usr/lib/libQt6Core.so.6"]


def test_parse_mapped_paths_preserves_spaces_in_pathnames() -> None:
    maps = (
        "1000-2000 r-xp 00000000 00:00 1 /pkg/mapped artifact/usr/bin/latte-dock.real\n"
        "2000-3000 r--p 00000000 00:00 2 /pkg/mapped artifact/usr/lib/liblattecoreplugin.so\n"
    )
    assert parse_mapped_paths(maps) == [
        "/pkg/mapped artifact/usr/bin/latte-dock.real",
        "/pkg/mapped artifact/usr/lib/liblattecoreplugin.so",
    ]


def test_read_mapped_paths_refuses_unreadable_maps(tmp_path: Path) -> None:
    missing = tmp_path / "no-such.maps"
    with pytest.raises(ProvenanceError) as refusal:
        read_mapped_paths(str(missing))
    assert str(refusal.value) == f"cannot parse process mappings from {missing}"


# ---- provenance predicates ---------------------------------------------------


def test_path_is_within_string_containment() -> None:
    assert path_is_within("/anything", "/")
    assert path_is_within("/usr/lib", "/usr/lib")
    assert path_is_within("/usr/lib/qt6", "/usr/lib")
    assert not path_is_within("/usr/lib64", "/usr/lib")
    assert not path_is_within("/usr", "/usr/lib")


@pytest.mark.parametrize(
    "path",
    [
        "/usr/bin/latte-dock",
        "/usr/lib/liblattecoreplugin.so",
        "/usr/lib/latte_indicator.so",
        "/usr/lib/plugins/org.kde.latte.contextmenu.so",
        "/usr/lib/qt6/qml/org/kde/latte/core/anything.qml",
    ],
)
def test_is_latte_runtime_path_accepts_latte_artifacts(path: str) -> None:
    assert is_latte_runtime_path(path)


@pytest.mark.parametrize(
    "path",
    ["/usr/lib/libQt6Core.so.6", "/usr/lib/libc.so.6", "/usr/share/latterday/file"],
)
def test_is_latte_runtime_path_ignores_foreign_artifacts(path: str) -> None:
    assert not is_latte_runtime_path(path)


def test_find_development_provider_names_marked_ancestors(tmp_path: Path) -> None:
    source = tmp_path / "checkout" / "sub"
    source.mkdir(parents=True)
    (tmp_path / "checkout" / "CMakeLists.txt").touch()
    assert find_development_provider(str(source / "libx.so")) == "source tree"

    worktree = tmp_path / "worktree" / "sub"
    worktree.mkdir(parents=True)
    (tmp_path / "worktree" / ".git").touch()  # a linked worktree's .git is a file
    assert find_development_provider(str(worktree)) == "source tree"

    build = tmp_path / "build" / "lib"
    build.mkdir(parents=True)
    (tmp_path / "build" / "CMakeCache.txt").touch()
    assert find_development_provider(str(build / "liby.so")) == "CMake build tree"

    cmakefiles = tmp_path / "build2"
    (cmakefiles / "CMakeFiles").mkdir(parents=True)
    assert find_development_provider(str(cmakefiles / "libz.so")) == "CMake build tree"

    clean = tmp_path / "clean" / "lib"
    clean.mkdir(parents=True)
    assert find_development_provider(str(clean / "libc.so")) is None


# ---- expected-mapping registry -----------------------------------------------


def test_registry_register_keeps_required_order_and_optional_out() -> None:
    registry = ExpectedMappingRegistry()
    registry.register("binary", "/pkg/usr/bin/latte-dock", True)
    registry.register("optional indicator", "/pkg/usr/lib/latte_indicator.so", False)
    registry.register("core QML plugin", "/pkg/usr/lib/liblattecoreplugin.so", True)
    assert registry.required == ["latte-dock", "liblattecoreplugin.so"]
    assert registry.expected["latte_indicator.so"] == "/pkg/usr/lib/latte_indicator.so"


def test_registry_refuses_basename_collision() -> None:
    registry = ExpectedMappingRegistry()
    registry.register("binary", "/pkg/usr/bin/latte-dock", True)
    with pytest.raises(ProvenanceError) as refusal:
        registry.register("stale binary", "/other/bin/latte-dock", False)
    assert str(refusal.value) == (
        "installed stale binary has mapped-artifact basename 'latte-dock', "
        "already used by /pkg/usr/bin/latte-dock"
    )


# ---- the mapped-path provenance audit ----------------------------------------


def _write_maps(tmp_path: Path, *paths: str) -> str:
    lines = [
        f"{1000 * (index + 1)}-{1000 * (index + 2)} r-xp 00000000 00:00 {index + 1} {path}"
        for index, path in enumerate(paths)
    ]
    maps_file = tmp_path / "proc.maps"
    maps_file.write_text("".join(line + "\n" for line in lines))
    return str(maps_file)


def _package_fixture(tmp_path: Path) -> tuple[str, ExpectedMappingRegistry, str, str]:
    """A minimal installed prefix: (prefix, registry, binary path, plugin path).

    Anchored at the physically resolved tmp path so the audit's realpath
    comparisons hold even when the pytest tmp dir sits behind a symlink.
    """
    tmp_path = tmp_path.resolve()
    prefix = tmp_path / "pkg" / "usr"
    binary = prefix / "bin" / "latte-dock"
    plugin = prefix / "lib" / "qt6" / "qml" / "org" / "kde" / "latte" / "core"
    plugin_so = plugin / "liblattecoreplugin.so"
    binary.parent.mkdir(parents=True)
    plugin.mkdir(parents=True)
    binary.touch()
    plugin_so.touch()
    registry = ExpectedMappingRegistry()
    registry.register("binary", str(binary), True)
    registry.register("core QML plugin", str(plugin_so), True)
    return str(prefix), registry, str(binary), str(plugin_so)


def test_audit_verifies_required_mappings_in_registration_order(tmp_path: Path) -> None:
    prefix, registry, binary, plugin_so = _package_fixture(tmp_path)
    maps = _write_maps(tmp_path, "/usr/lib/libc.so.6", plugin_so, binary)
    result = audit_mapped_paths(maps, prefix, str(tmp_path / "repo"), registry)
    assert result.passed
    assert result.verified_paths == [binary, plugin_so]  # registration order, not maps order


def test_audit_refuses_nix_store_artifact(tmp_path: Path) -> None:
    prefix, registry, _, _ = _package_fixture(tmp_path)
    mapped = "/nix/store/fake-latte-provider/lib/libQt6Core.so.6"
    result = audit_mapped_paths(
        _write_maps(tmp_path, mapped), prefix, str(tmp_path / "r"), registry
    )
    assert result.violation is not None
    assert result.violation.kind is MappedPathViolationKind.NIX_ARTIFACT
    assert result.violation.message == f"running dock mapped a Nix artifact: {mapped}"


def test_audit_refuses_source_tree_mapping_negative_control(tmp_path: Path) -> None:
    # The driven negative control: a source-tree path injected into an
    # otherwise-clean audit must refuse with the exact diagnostic.
    prefix, registry, binary, plugin_so = _package_fixture(tmp_path)
    source_root = str(tmp_path / "checkout")
    injected = f"{source_root}/build/liblattecoreplugin.so"
    maps = _write_maps(tmp_path, binary, plugin_so, injected)
    result = audit_mapped_paths(maps, prefix, source_root, registry)
    assert result.violation is not None
    assert result.violation.kind is MappedPathViolationKind.SOURCE_TREE
    assert result.violation.message == f"running dock mapped the source/build tree: {injected}"
    assert result.verified_paths == []  # the audit stops before verification


def test_audit_refuses_qmlstage_artifact(tmp_path: Path) -> None:
    prefix, registry, _, _ = _package_fixture(tmp_path)
    mapped = "/home/user/stage/_qmlstage/org/kde/latte/core/qmldir"
    result = audit_mapped_paths(
        _write_maps(tmp_path, mapped), prefix, str(tmp_path / "r"), registry
    )
    assert result.violation is not None
    assert result.violation.kind is MappedPathViolationKind.QMLSTAGE_ARTIFACT
    assert result.violation.message == (
        f"running dock mapped a development _qmlstage artifact: {mapped}"
    )


def test_audit_refuses_development_provider(tmp_path: Path) -> None:
    prefix, registry, _, _ = _package_fixture(tmp_path)
    build = tmp_path / "external-build"
    (build / "lib").mkdir(parents=True)
    (build / "CMakeCache.txt").touch()
    mapped = str(build / "lib" / "libQt6Core.so.6")
    result = audit_mapped_paths(
        _write_maps(tmp_path, mapped), prefix, str(tmp_path / "r"), registry
    )
    assert result.violation is not None
    assert result.violation.kind is MappedPathViolationKind.DEVELOPMENT_PROVIDER
    assert result.violation.message == f"running dock mapped a CMake build tree artifact: {mapped}"


def test_audit_ignores_benign_foreign_libraries(tmp_path: Path) -> None:
    prefix, registry, binary, plugin_so = _package_fixture(tmp_path)
    deps = tmp_path / "deps"
    deps.mkdir()
    maps = _write_maps(tmp_path, str(deps / "libglib-2.0.so.0"), binary, plugin_so)
    result = audit_mapped_paths(maps, prefix, str(tmp_path / "r"), registry)
    assert result.passed


def test_audit_refuses_unresolvable_latte_runtime(tmp_path: Path) -> None:
    prefix, registry, binary, plugin_so = _package_fixture(tmp_path)
    dangling = f"{prefix}/lib/liblattemissing.so"
    maps = _write_maps(tmp_path, binary, plugin_so, dangling)
    result = audit_mapped_paths(maps, prefix, str(tmp_path / "r"), registry)
    assert result.violation is not None
    assert result.violation.kind is MappedPathViolationKind.UNRESOLVABLE_RUNTIME
    assert result.violation.message == f"mapped Latte runtime cannot be resolved: {dangling}"


def test_audit_refuses_latte_runtime_escaping_the_prefix(tmp_path: Path) -> None:
    prefix, registry, binary, plugin_so = _package_fixture(tmp_path)
    foreign = tmp_path.resolve() / "foreign" / "liblattecoreplugin.so"
    foreign.parent.mkdir()
    foreign.touch()
    maps = _write_maps(tmp_path, binary, plugin_so, str(foreign))
    result = audit_mapped_paths(maps, prefix, str(tmp_path / "r"), registry)
    assert result.violation is not None
    assert result.violation.kind is MappedPathViolationKind.ESCAPES_PREFIX
    assert result.violation.message == (
        f"mapped Latte runtime escapes the package prefix: {foreign} -> {foreign}"
    )


def test_audit_refuses_unexpected_latte_runtime_inside_prefix(tmp_path: Path) -> None:
    prefix, registry, binary, plugin_so = _package_fixture(tmp_path)
    stray = Path(prefix) / "lib" / "liblatteextra.so"
    stray.touch()
    maps = _write_maps(tmp_path, binary, plugin_so, str(stray))
    result = audit_mapped_paths(maps, prefix, str(tmp_path / "r"), registry)
    assert result.violation is not None
    assert result.violation.kind is MappedPathViolationKind.UNEXPECTED_RUNTIME
    assert result.violation.message == f"unexpected Latte runtime is mapped: {stray}"


def test_audit_refuses_registered_basename_from_wrong_provider(tmp_path: Path) -> None:
    prefix, registry, binary, plugin_so = _package_fixture(tmp_path)
    imposter = Path(prefix) / "lib" / "latte-dock"
    imposter.touch()
    maps = _write_maps(tmp_path, str(imposter), plugin_so)
    result = audit_mapped_paths(maps, prefix, str(tmp_path / "r"), registry)
    assert result.violation is not None
    assert result.violation.kind is MappedPathViolationKind.WRONG_PROVIDER
    assert result.violation.message == f"latte-dock mapped from {imposter}, expected {binary}"


def test_audit_refuses_missing_required_after_verifying_earlier_ones(tmp_path: Path) -> None:
    prefix, registry, binary, _ = _package_fixture(tmp_path)
    maps = _write_maps(tmp_path, binary)  # the core plugin never mapped
    result = audit_mapped_paths(maps, prefix, str(tmp_path / "r"), registry)
    assert result.violation is not None
    assert result.violation.kind is MappedPathViolationKind.MISSING_REQUIRED
    assert result.violation.message == (
        "required installed artifact liblattecoreplugin.so is not mapped by the settled dock"
    )
    assert result.verified_paths == [binary]  # verified before the refusal, like the bash


def test_audit_reports_unreadable_maps_as_violation(tmp_path: Path) -> None:
    prefix, registry, _, _ = _package_fixture(tmp_path)
    missing = str(tmp_path / "gone.maps")
    result = audit_mapped_paths(missing, prefix, str(tmp_path / "r"), registry)
    assert result.violation is not None
    assert result.violation.kind is MappedPathViolationKind.UNREADABLE_MAPS
    assert result.violation.message == f"cannot parse process mappings from {missing}"


# ---- process-environment readback --------------------------------------------


def test_read_environment_value_extracts_nul_separated_entries(tmp_path: Path) -> None:
    environ = tmp_path / "environ"
    environ.write_bytes(b"PATH=/bin\0QML2_IMPORT_PATH=/a:/b\0WITH=eq=uals\0EMPTY=\0")
    assert read_environment_value(str(environ), "QML2_IMPORT_PATH") == "/a:/b"
    assert read_environment_value(str(environ), "WITH") == "eq=uals"
    assert read_environment_value(str(environ), "EMPTY") == ""


def test_read_environment_value_refuses_missing_entry(tmp_path: Path) -> None:
    environ = tmp_path / "environ"
    environ.write_bytes(b"PATH=/bin\0")
    with pytest.raises(ProvenanceError) as refusal:
        read_environment_value(str(environ), "QML2_IMPORT_PATH")
    assert str(refusal.value) == "process environment has no QML2_IMPORT_PATH entry"


def test_read_environment_value_refuses_unreadable_file(tmp_path: Path) -> None:
    missing = tmp_path / "no-environ"
    with pytest.raises(ProvenanceError) as refusal:
        read_environment_value(str(missing), "PATH")
    assert str(refusal.value) == f"cannot read process environment from {missing}"


# ---- plugin metadata contracts -----------------------------------------------


def test_containment_actions_contract_requires_array_service_types() -> None:
    good: JsonValue = {
        "MetaData": {
            "KPlugin": {
                "Id": "org.kde.latte.contextmenu",
                "ServiceTypes": ["Plasma/ContainmentActions"],
            }
        }
    }
    assert declares_containment_actions_contract(good)
    wrong_type: JsonValue = {
        "MetaData": {
            "KPlugin": {"Id": "org.kde.latte.contextmenu", "ServiceTypes": ["Plasma/Applet"]}
        }
    }
    assert not declares_containment_actions_contract(wrong_type)
    string_types: JsonValue = {
        "MetaData": {
            "KPlugin": {
                "Id": "org.kde.latte.contextmenu",
                "ServiceTypes": "Plasma/ContainmentActions",
            }
        }
    }
    assert not declares_containment_actions_contract(string_types)
    assert not declares_containment_actions_contract({})
    assert not declares_containment_actions_contract("not an object")


def test_indicator_structure_contract_requires_string_structure() -> None:
    good: JsonValue = {
        "MetaData": {
            "KPackageStructure": "Latte/Indicator",
            "X-KDE-ParentApp": "org.kde.latte-dock",
        }
    }
    assert declares_indicator_structure_contract(good)
    wrong: JsonValue = {
        "MetaData": {"KPackageStructure": "Plasma/Applet", "X-KDE-ParentApp": "org.kde.latte-dock"}
    }
    assert not declares_indicator_structure_contract(wrong)
    array_structure: JsonValue = {
        "MetaData": {
            "KPackageStructure": ["Latte/Indicator"],
            "X-KDE-ParentApp": "org.kde.latte-dock",
        }
    }
    assert not declares_indicator_structure_contract(array_structure)


# ---- AppStream validation ----------------------------------------------------


def _appstream_xml(
    component_type: str = "desktop-application",
    component_id: str = "org.kde.latte-dock",
    launchable: str = "org.kde.latte-dock.desktop",
    extends: str | None = None,
    library: str | None = None,
    replaced_component_id: str | None = "org.kde.latte-dock.desktop",
    replacement_extra: str = "",
) -> str:
    """The selftest's write_appstream_metadata fixture, as a string builder."""
    lines = [
        '<?xml version="1.0" encoding="utf-8"?>',
        f'<component type="{component_type}">',
        f"  <id>{component_id}</id>",
    ]
    if extends is not None:
        lines.append(f"  <extends>{extends}</extends>")
    lines += [
        "  <name>Latte</name>",
        "  <summary>Dock and task launcher</summary>",
        "  <metadata_license>CC0-1.0</metadata_license>",
        "  <project_license>GPL-2.0-or-later</project_license>",
        "  <provides>",
        "    <binary>latte-dock</binary>",
    ]
    if library is not None:
        lines.append(f"    <library>{library}</library>")
    lines.append("  </provides>")
    lines.append(f'  <launchable type="desktop-id">{launchable}</launchable>')
    if replaced_component_id is not None:
        lines.append(f"  <replaces>\n    <id>{replaced_component_id}</id>")
        if replacement_extra:
            lines.append(f"    {replacement_extra}")
        lines.append("  </replaces>")
    lines.append("</component>")
    return "\n".join(lines) + "\n"


def test_appstream_accepts_the_standalone_component() -> None:
    assert validate_appstream_metadata(_appstream_xml()) is None


@pytest.mark.parametrize(
    ("xml", "diagnostic"),
    [
        (
            _appstream_xml(component_type="addon"),
            "component type is 'addon', expected 'desktop-application'",
        ),
        (
            _appstream_xml(component_id="org.kde.latte-dock.desktop"),
            "component ID must be exactly org.kde.latte-dock",
        ),
        (
            _appstream_xml(launchable="org.kde.latte.desktop"),
            "launchable must be exactly desktop-id org.kde.latte-dock.desktop",
        ),
        (
            _appstream_xml(extends="org.kde.plasmashell"),
            "standalone component must not declare extends",
        ),
        (
            _appstream_xml(library="liblatte2plugin.so"),
            "provider must not advertise library liblatte2plugin.so",
        ),
        (
            _appstream_xml(replaced_component_id=None),
            "replaces must contain only the released org.kde.latte-dock.desktop component ID",
        ),
        (
            _appstream_xml(replaced_component_id="org.kde.latte.desktop"),
            "replaces must contain only the released org.kde.latte-dock.desktop component ID",
        ),
        (
            _appstream_xml(replacement_extra="unexpected"),
            "replaces must contain only the released org.kde.latte-dock.desktop component ID",
        ),
        (
            _appstream_xml(replacement_extra="<binary>latte-dock</binary>"),
            "replaces must contain only the released org.kde.latte-dock.desktop component ID",
        ),
        (
            _appstream_xml(replaced_component_id="org.kde.latte-dock.desktop<unexpected/>"),
            "replaces must contain only the released org.kde.latte-dock.desktop component ID",
        ),
    ],
)
def test_appstream_rejects_contract_violations(xml: str, diagnostic: str) -> None:
    assert validate_appstream_metadata(xml) == diagnostic


@pytest.mark.parametrize(
    ("xml", "diagnostic"),
    [
        ("<foo/>", "root element is <foo>, expected <component>"),
        ("stray text", "non-whitespace text is not allowed outside the component root"),
        ('<component type="desktop-application">', "unclosed tag <component>"),
        ("</component>", "closing tag </component> has no opening tag"),
        (
            '<component type="desktop-application"></id>',
            "closing tag </id> does not match <component>",
        ),
        (
            '<component type="desktop-application" type="addon"/>',
            "<component> repeats attribute type",
        ),
        ("<component type=unquoted/>", "<component> contains malformed attributes"),
        ("<![CDATA[x]]>", "CDATA is not allowed outside the component root"),
        ("< component/>", "metadata contains unsupported or malformed XML"),
        (
            '<component type="desktop-application"/><component/>',
            "metadata contains 2 root elements, expected one",
        ),
        ("", "metadata contains 0 root elements, expected one"),
    ],
)
def test_appstream_rejects_malformed_xml(xml: str, diagnostic: str) -> None:
    assert validate_appstream_metadata(xml) == diagnostic


def test_appstream_allows_comments_prolog_and_cdata_inside_root() -> None:
    xml = _appstream_xml().replace(
        "  <name>Latte</name>",
        "  <!-- a comment -->\n  <name><![CDATA[Latte]]></name>",
    )
    assert validate_appstream_metadata(xml) is None


def test_read_mapped_paths_survives_non_utf8_bytes(tmp_path: Path) -> None:
    # The PR #177 review finding: bash/awk were byte-transparent; a legal
    # non-UTF-8 byte in a mapped path must not crash the gate outside its
    # exit-code contract. surrogateescape carries it through.
    maps = tmp_path / "maps"
    maps.write_bytes(b"7f0000000000-7f0000001000 r--p 00000000 00:00 1 /usr/lib/f\xe9ont.so\n")
    paths = read_mapped_paths(str(maps))
    assert any("ont.so" in p for p in paths)
