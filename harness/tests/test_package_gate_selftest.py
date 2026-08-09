# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
"""The installed-package-gate selftest: fast provenance/refusal controls.

The BP-4b port of tests/installed-package-gate-selftest.sh (the
bash-to-python migration's package-gate selftest chunk). The fixture is a
real ELF executable and real moc-compiled Qt plugins, but not a dock
runtime: --check-only proves package discovery and the rejection
boundaries without requiring a compositor, exactly like the bash.

Marker-gated: every test carries the ``package_gate_selftest`` marker and
the harness-check pytest run deselects it (pyproject addopts), because
this module is its own gate-all leg (it compiles fixtures and spawns the
gate ~50 times); running it inside the unit-test leg would double the
gate's cost for no added verdict. The leg runs it with
``pytest -m package_gate_selftest``.

Scope: this module carries the controls whose semantics no existing unit
test pins - the assembled-gate integration runs (PATH injection, ELF and
plugin fixtures, symlink trees, live-root manifests), the signal and
cleanup contracts (exit 130/143 through cleanup), the process-group
teardown gaps, and the shutdown wait-status taxonomy. Controls whose
decision logic is already pinned by test_package_gate.py,
test_package_provenance.py, test_vehicle.py or test_proc.py are not
re-ported; the BP-4b commit body carries the full 91-control
reconciliation.

Signal controls run FOREGROUND. The bash selftest's SIGINT control broke
under backgrounded/nohup runners because POSIX shells cannot trap a
signal ignored at entry; ``install_conventional_signal_exits`` has no
such restriction (``signal.signal`` overrides an inherited SIG_IGN), so
the ported controls are additionally immune - noted per control.
"""

from __future__ import annotations

import os
import shlex
import shutil
import signal
import stat
import subprocess
import sys
import tarfile
import tempfile
import time
from collections.abc import Callable, Iterator, Mapping
from contextlib import suppress
from dataclasses import dataclass
from pathlib import Path

import pytest

from latte_harness import package_gate, vehicle
from latte_harness.package_gate import (
    VALIDATION_COMMANDS,
    GateRefusal,
    RuntimeCleanupState,
    ValidatedPackage,
)
from latte_harness.package_provenance import (
    LOADER_INJECTION_VARIABLES,
    choose_qt6_plugin_info,
    find_qt6_plugin_info,
)
from latte_harness.paths import find_repo_root
from latte_harness.proc import SessionProcess

pytestmark = pytest.mark.package_gate_selftest

REPO = find_repo_root()
GATE_SHIM = REPO / "scripts" / "installed-package-gate.sh"

# Commands the FIXTURE BUILD needs (the gate's own tool contract is
# checked by the gate itself); mirrors the bash selftest's preflight.
FIXTURE_COMMANDS = (
    "bash",
    "c++",
    "cc",
    "git",
    "ld",
    "patchelf",
    "pgrep",
    "pkg-config",
    "readelf",
    "tar",
)


def _run_ok(argv: list[str], env: Mapping[str, str] | None = None, cwd: Path | None = None) -> str:
    result = subprocess.run(
        argv,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=dict(env) if env is not None else None,
        cwd=cwd,
        check=False,
    )
    assert result.returncode == 0, f"fixture command failed: {argv}\n{result.stdout}"
    return result.stdout


def _make_executable(path: Path, text: str) -> None:
    path.write_text(text)
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def _await_path(path: Path, attempts: int = 500, delay: float = 0.01) -> None:
    for _ in range(attempts):
        if path.exists():
            return
        time.sleep(delay)
    pytest.fail(f"fixture never signalled readiness at {path}")


# ---- live-root fixture-parent preflight --------------------------------------
#
# --root / fixtures use real host-absolute paths, so their ancestry is part of
# the production provenance contract (unlike an isolated package root). The
# synthetic live tree must sit outside source/build-marked ancestors so those
# fixtures test link semantics rather than an unrelated staging marker.


def _ancestor_has_development_marker(ancestor: Path) -> bool:
    return (
        (ancestor / ".git").exists()
        or (ancestor / "CMakeLists.txt").is_file()
        or (ancestor / "CMakeCache.txt").is_file()
        or (ancestor / "CMakeFiles").is_dir()
    )


def _find_marked_ancestor(start: Path, marked: Callable[[Path], bool]) -> Path | None:
    current = start
    while True:
        if marked(current):
            return current
        if current == current.parent:
            return None
        current = current.parent


def test_fixture_preflight_walks_to_and_detects_a_host_root_marker() -> None:
    # The injected predicate proves the preflight checks / itself, without
    # creating a source/build marker on the real host root (bash control 1).
    def _root_only_marker(ancestor: Path) -> bool:
        return ancestor == Path("/")

    assert _find_marked_ancestor(Path("/var/tmp"), _root_only_marker) == Path("/")

    # and an unmarked walk terminates at / with no false positive
    def _never_marked(_ancestor: Path) -> bool:
        return False

    assert _find_marked_ancestor(Path("/var/tmp"), _never_marked) is None


# ---- session fixtures --------------------------------------------------------


@pytest.fixture(scope="session")
def work() -> Iterator[Path]:
    for command in FIXTURE_COMMANDS:
        assert shutil.which(command) is not None, f"required fixture command '{command}' is missing"
    parent_raw = os.environ.get("LATTE_PACKAGE_GATE_SELFTEST_TMPDIR") or os.environ.get(
        "XDG_RUNTIME_DIR", "/var/tmp"
    )
    parent = Path(parent_raw)
    assert parent.is_absolute() and parent.is_dir() and os.access(parent, os.W_OK), (
        f"fixture parent must be an absolute writable directory: {parent}"
    )
    parent = parent.resolve()
    marked = _find_marked_ancestor(parent, _ancestor_has_development_marker)
    assert marked is None, (
        f"live-root fixture parent has a source/build-marked ancestor: {marked}; "
        "set LATTE_PACKAGE_GATE_SELFTEST_TMPDIR to a marker-free writable directory"
    )
    work_dir = Path(tempfile.mkdtemp(prefix="latte-installed-gate-selftest.", dir=parent))
    yield work_dir
    shutil.rmtree(work_dir, ignore_errors=True)


@dataclass(frozen=True)
class Toolchain:
    moc: str
    qt_cflags: tuple[str, ...]
    qt_libs: tuple[str, ...]
    dependency_libs: tuple[str, ...]
    qtplugininfo: str


@pytest.fixture(scope="session")
def toolchain() -> Toolchain:
    libexec = _run_ok(["pkg-config", "--variable=libexecdir", "Qt6Core"]).strip()
    moc = f"{libexec}/moc"
    assert os.access(moc, os.X_OK), f"Qt6 moc is unavailable at {moc}"
    cflags = tuple(shlex.split(_run_ok(["pkg-config", "--cflags", "Qt6Core"])))
    libs = tuple(shlex.split(_run_ok(["pkg-config", "--libs", "Qt6Core"])))
    libdir = _run_ok(["pkg-config", "--variable=libdir", "Qt6Core"]).strip()
    dependencies = (
        f"{libdir}/libQt6Core.so.6",
        _run_ok(["c++", "-print-file-name=libstdc++.so.6"]).strip(),
        _run_ok(["c++", "-print-file-name=libgcc_s.so.1"]).strip(),
    )
    for dependency in dependencies:
        assert Path(dependency).is_file(), f"fixture dependency is unavailable: {dependency}"
    qtplugininfo = find_qt6_plugin_info()
    assert qtplugininfo is not None, "no real Qt 6 qtplugininfo is available for fixtures"
    return Toolchain(moc, cflags, libs, dependencies, qtplugininfo)


# The linker runs with NIX_LDFLAGS scrubbed, as the bash did: the nix
# wrapper's implicit flags would drag the whole dev closure into fixtures
# meant to model a distro package.
def _scrubbed_link_env() -> dict[str, str]:
    return {**os.environ, "NIX_LDFLAGS": "", "NIX_LDFLAGS_BEFORE": ""}


@dataclass(frozen=True)
class ElfFixtures:
    source: Path
    binary: Path
    generic_library: Path


@pytest.fixture(scope="session")
def elf(work: Path) -> ElfFixtures:
    source = work / "elf-fixture.c"
    source.write_text("int fixture(void) { return 0; }\nint main(void) { return fixture(); }\n")
    obj = work / "elf-fixture.o"
    _run_ok(["cc", "-fPIC", "-c", str(source), "-o", str(obj)])
    binary = work / "fixture-binary"
    generic = work / "fixture-generic-library.so"
    _run_ok(
        ["ld", "--build-id", "-e", "main", str(obj), "-o", str(binary)], env=_scrubbed_link_env()
    )
    _run_ok(["ld", "--build-id", "-shared", str(obj), "-o", str(generic)], env=_scrubbed_link_env())
    _run_ok(["readelf", "-h", str(generic)])
    return ElfFixtures(source, binary, generic)


def _build_qt_plugin(
    toolchain: Toolchain,
    work: Path,
    output: Path,
    class_name: str,
    iid: str,
    metadata_file: Path | None = None,
    constructor: str = "",
) -> None:
    source = work / f"{class_name}.cpp"
    moc_file = work / f"{class_name}.moc"
    lines = ["#include <QObject>"]
    if constructor:
        lines.append(constructor)
    lines.append(f"class {class_name} : public QObject {{")
    lines.append("    Q_OBJECT")
    if metadata_file is not None:
        lines.append(f'    Q_PLUGIN_METADATA(IID "{iid}" FILE "{metadata_file}")')
    else:
        lines.append(f'    Q_PLUGIN_METADATA(IID "{iid}")')
    lines.append("};")
    lines.append(f'#include "{moc_file.name}"')
    source.write_text("\n".join(lines) + "\n")
    _run_ok([toolchain.moc, str(source), "-o", str(moc_file)])
    _run_ok(
        [
            "c++",
            "-std=c++20",
            "-fPIC",
            "-shared",
            str(source),
            "-o",
            str(output),
            *toolchain.qt_cflags,
            *toolchain.qt_libs,
        ]
    )
    _run_ok(["patchelf", "--set-rpath", "$ORIGIN", str(output)])


@dataclass(frozen=True)
class Plugins:
    core: Path
    containment: Path
    tasks: Path
    action: Path
    indicator: Path
    action_metadata: Path
    wrong_action_metadata: Path
    indicator_metadata: Path


@pytest.fixture(scope="session")
def plugins(work: Path, toolchain: Toolchain) -> Plugins:
    action_metadata = work / "action-metadata.json"
    action_metadata.write_text(
        '{"KPlugin":{"Id":"org.kde.latte.contextmenu",'
        '"ServiceTypes":["Plasma/ContainmentActions"]}}\n'
    )
    wrong_action_metadata = work / "wrong-action-metadata.json"
    wrong_action_metadata.write_text(
        '{"KPlugin":{"Id":"org.kde.latte.contextmenu","ServiceTypes":["Plasma/Applet"]}}\n'
    )
    indicator_metadata = work / "indicator-metadata.json"
    indicator_metadata.write_text(
        '{"KPackageStructure":"Latte/Indicator","X-KDE-ParentApp":"org.kde.latte-dock"}\n'
    )
    core = work / "liblattecoreplugin.so"
    containment = work / "liblattecontainmentplugin.so"
    tasks = work / "liblattetasksplugin.so"
    action = work / "org.kde.latte.contextmenu.so"
    indicator = work / "latte_indicator.so"
    qml_iid = "org.qt-project.Qt.QQmlExtensionInterface"
    _build_qt_plugin(toolchain, work, core, "LatteCorePlugin", qml_iid)
    _build_qt_plugin(toolchain, work, containment, "LatteContainmentPlugin", qml_iid)
    _build_qt_plugin(toolchain, work, tasks, "LatteTasksPlugin", qml_iid)
    _build_qt_plugin(
        toolchain, work, action, "MenuFactory", "org.kde.KPluginFactory", action_metadata
    )
    _build_qt_plugin(
        toolchain,
        work,
        indicator,
        "latte_packagestructure_indicator_factory",
        "org.kde.KPluginFactory",
        indicator_metadata,
    )
    return Plugins(
        core,
        containment,
        tasks,
        action,
        indicator,
        action_metadata,
        wrong_action_metadata,
        indicator_metadata,
    )


def _write_appstream_metadata(
    root: Path,
    component_type: str = "desktop-application",
    component_id: str = "org.kde.latte-dock",
    launchable: str = "org.kde.latte-dock.desktop",
) -> None:
    lines = [
        '<?xml version="1.0" encoding="utf-8"?>',
        f'<component type="{component_type}">',
        f"  <id>{component_id}</id>",
        "  <name>Latte</name>",
        "  <summary>Dock and task launcher</summary>",
        "  <metadata_license>CC0-1.0</metadata_license>",
        "  <project_license>GPL-2.0-or-later</project_license>",
        "  <provides>",
        "    <binary>latte-dock</binary>",
        "  </provides>",
        f'  <launchable type="desktop-id">{launchable}</launchable>',
        "  <replaces>",
        "    <id>org.kde.latte-dock.desktop</id>",
        "  </replaces>",
        "</component>",
    ]
    target = root / "usr" / "share" / "metainfo" / "org.kde.latte-dock.appdata.xml"
    target.write_text("\n".join(lines) + "\n")


def _make_package(root: Path, elf: ElfFixtures, plugins: Plugins, toolchain: Toolchain) -> None:
    qml = root / "usr" / "lib" / "qt6" / "qml"
    plugin_root = root / "usr" / "lib" / "qt6" / "plugins"
    data = root / "usr" / "share"
    latte_qml = qml / "org" / "kde" / "latte"
    for directory in (
        root / "usr" / "bin",
        latte_qml / "core",
        latte_qml / "components" / "deep",
        latte_qml / "private" / "containment",
        latte_qml / "private" / "tasks",
        plugin_root / "kpackage" / "packagestructure",
        plugin_root / "plasma" / "containmentactions",
        data / "plasma" / "shells" / "org.kde.latte.shell",
        data / "plasma" / "plasmoids" / "org.kde.latte.containment",
        data / "plasma" / "plasmoids" / "org.kde.latte.plasmoid",
        data / "applications",
        data / "metainfo",
        data / "latte" / "indicators" / "default",
    ):
        directory.mkdir(parents=True)
    shutil.copy(elf.binary, root / "usr" / "bin" / "latte-dock")

    def _stage_module(directory: Path, plugin: Path) -> None:
        shutil.copy(plugin, directory / plugin.name)
        for dependency in toolchain.dependency_libs:
            shutil.copy(dependency, directory / Path(dependency).name)  # cp -L: the real file

    (latte_qml / "core" / "qmldir").touch()
    _stage_module(latte_qml / "core", plugins.core)
    (latte_qml / "components" / "deep" / "Installed.qml").touch()
    (latte_qml / "private" / "containment" / "qmldir").touch()
    _stage_module(latte_qml / "private" / "containment", plugins.containment)
    (latte_qml / "private" / "tasks" / "qmldir").touch()
    _stage_module(latte_qml / "private" / "tasks", plugins.tasks)
    _stage_module(plugin_root / "kpackage" / "packagestructure", plugins.indicator)
    _stage_module(plugin_root / "plasma" / "containmentactions", plugins.action)
    (data / "plasma" / "shells" / "org.kde.latte.shell" / "metadata.json").touch()
    (data / "plasma" / "shells" / "org.kde.latte.shell" / "Installed.qml").touch()
    (data / "plasma" / "plasmoids" / "org.kde.latte.containment" / "metadata.json").touch()
    (data / "plasma" / "plasmoids" / "org.kde.latte.containment" / "Installed.qml").touch()
    (data / "plasma" / "plasmoids" / "org.kde.latte.plasmoid" / "metadata.json").touch()
    (data / "plasma" / "plasmoids" / "org.kde.latte.plasmoid" / "Installed.qml").touch()
    (data / "applications" / "org.kde.latte-dock.desktop").touch()
    _write_appstream_metadata(root)
    (data / "latte" / "indicators" / "default" / "Installed.qml").touch()


@dataclass(frozen=True)
class GateFixture:
    """Everything a control needs: the good package plus its environment."""

    work: Path
    good: Path
    framework: Path
    runtime_data: Path
    elf: ElfFixtures
    plugins: Plugins
    toolchain: Toolchain

    def copy_good(self, name: str) -> Path:
        target = self.work / name
        shutil.copytree(self.good, target, symlinks=True)
        return target

    def gate_env(self, extra: Mapping[str, str] | None = None) -> dict[str, str]:
        env = dict(os.environ)
        env["LATTE_QML_MODULE_PATH"] = str(self.framework)
        env["LATTE_RUNTIME_DATA_PATH"] = str(self.runtime_data)
        if extra:
            env.update(extra)
        return env


@pytest.fixture(scope="session")
def pkg(work: Path, elf: ElfFixtures, plugins: Plugins, toolchain: Toolchain) -> GateFixture:
    framework = work / "framework-qml"
    runtime_data = work / "framework-data"
    framework.mkdir()
    runtime_data.mkdir()
    good = work / "good"
    _make_package(good, elf, plugins, toolchain)
    return GateFixture(work, good, framework, runtime_data, elf, plugins, toolchain)


def _run_gate(
    pkg: GateFixture, args: list[str], extra_env: Mapping[str, str] | None = None
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-m", "latte_harness.package_gate", *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=pkg.gate_env(extra_env),
        check=False,
    )


def _run_check(
    pkg: GateFixture, root: Path, extra_env: Mapping[str, str] | None = None
) -> subprocess.CompletedProcess[str]:
    return _run_gate(pkg, ["--root", str(root), "--prefix", "/usr", "--check-only"], extra_env)


def _expect_refusal(result: subprocess.CompletedProcess[str], needle: str) -> None:
    """The bash expect_failure: nonzero exit AND the actionable diagnostic."""
    assert result.returncode == 2, (
        f"expected refusal exit 2, got {result.returncode}; output:\n{result.stdout}"
    )
    assert needle in result.stdout, f"no actionable '{needle}' diagnostic; output:\n{result.stdout}"


def _expect_check_ok(result: subprocess.CompletedProcess[str]) -> None:
    assert result.returncode == 0, f"valid package was rejected; output:\n{result.stdout}"
    assert "installed-package-gate: CHECK OK" in result.stdout


def _relative_symlink(link: Path, target: Path) -> None:
    link.symlink_to(os.path.relpath(target, link.parent))


# ---- Void packaging staging (rides the selftest as in the bash) --------------


def test_void_staging_pins_recipe_and_archives_corrected_metadata(work: Path) -> None:
    void_packages = work / "void-packages"
    (void_packages / "srcpkgs").mkdir(parents=True)
    _run_ok(["git", "init", "-q", str(void_packages)])
    _make_executable(void_packages / "xbps-src", "#!/bin/sh\nexit 99\n")
    stage_output = _run_ok(
        [str(REPO / "packaging" / "void" / "build-package"), "--stage-only", str(void_packages)]
    )
    expected_commit = _run_ok(["git", "-C", str(REPO), "rev-parse", "HEAD"]).strip()
    assert f"source_commit={expected_commit}" in stage_output
    recipe = void_packages / "srcpkgs" / "latte-dock"
    assert not (recipe / "patches").exists()
    staged_commits = [
        line
        for line in (recipe / "template").read_text().splitlines()
        if line.startswith("_commit=")
    ]
    assert staged_commits == [f"_commit={expected_commit}"], (
        f"staged Void template must pin exactly _commit={expected_commit}, got {staged_commits}"
    )
    archive = (
        void_packages / "hostdir" / "sources" / "latte-dock-0.10.77" / f"{expected_commit}.tar.gz"
    )
    with tarfile.open(archive) as tar:
        member = tar.extractfile(
            f"lattecotta-dock-{expected_commit}/app/org.kde.latte-dock.appdata.xml.cmake"
        )
        assert member is not None
        metadata = member.read().decode()
    assert '<component type="desktop-application">' in metadata
    assert "<id>org.kde.latte-dock</id>" in metadata
    assert "<extends>" not in metadata
    assert "liblatte2plugin.so" not in metadata


# ---- restricted-PATH refusals through the shim -------------------------------


def _run_shim_restricted(pkg: GateFixture, path_value: str) -> subprocess.CompletedProcess[str]:
    bash = shutil.which("bash")
    assert bash is not None
    env = pkg.gate_env({"PATH": path_value})
    return subprocess.run(
        [
            bash,
            str(GATE_SHIM),
            "--root",
            str(pkg.work / "not-used"),
            "--prefix",
            "/usr",
            "--check-only",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=env,
        check=False,
    )


def test_shim_refuses_missing_awk_before_any_interpreter(pkg: GateFixture) -> None:
    empty = pkg.work / "missing-awk-path"
    empty.mkdir()
    result = _run_shim_restricted(pkg, str(empty))
    _expect_refusal(result, "required validation command 'awk' is missing")


def test_shim_refuses_missing_env_launcher(pkg: GateFixture) -> None:
    restricted = pkg.work / "missing-env-path"
    restricted.mkdir()
    for command in VALIDATION_COMMANDS:
        if command == "env":
            continue
        real = shutil.which(command)
        assert real is not None
        (restricted / command).symlink_to(real)
    result = _run_shim_restricted(pkg, str(restricted))
    _expect_refusal(result, "required validation command 'env' is missing")


# ---- Qt 6 qtplugininfo selection ---------------------------------------------


def test_qt6_suffixed_tool_takes_precedence_over_unsuffixed_qt5(pkg: GateFixture) -> None:
    qt_bin = pkg.work / "qt-selector-bin"
    qt_bin.mkdir()
    qt6_log = pkg.work / "qt6-selector.log"
    qt5_log = pkg.work / "qt5-selector.log"
    _make_executable(
        qt_bin / "qtplugininfo6",
        f"#!/usr/bin/env bash\n: >{shlex.quote(str(qt6_log))}\n"
        f'exec {shlex.quote(pkg.toolchain.qtplugininfo)} "$@"\n',
    )
    _make_executable(
        qt_bin / "qtplugininfo",
        f"#!/usr/bin/env bash\n: >{shlex.quote(str(qt5_log))}\nprintf 'qplugininfo 5.15.2\\n'\n",
    )
    result = _run_check(pkg, pkg.good, {"PATH": f"{qt_bin}:{os.environ['PATH']}"})
    _expect_check_ok(result)
    assert qt6_log.exists(), "the Qt 6-specific tool was never consulted"
    assert not qt5_log.exists(), "the unsuffixed Qt 5 tool shadowed the Qt 6-specific one"


def test_hanging_version_probe_is_bounded_and_continues(pkg: GateFixture) -> None:
    hanging = pkg.work / "hanging-qtplugininfo"
    _make_executable(hanging, "#!/usr/bin/env bash\nexec sleep 60\n")
    probe_start = time.monotonic()
    selected = choose_qt6_plugin_info([str(hanging), pkg.toolchain.qtplugininfo])
    probe_seconds = time.monotonic() - probe_start
    assert selected == pkg.toolchain.qtplugininfo
    assert probe_seconds < 10, "hanging version probe did not continue boundedly"


# ---- AppStream metadata through the assembled gate ---------------------------


def test_missing_appstream_metadata_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("missing-appstream-metadata")
    (root / "usr" / "share" / "metainfo" / "org.kde.latte-dock.appdata.xml").unlink()
    _expect_refusal(_run_check(pkg, root), "missing AppStream metadata")


def test_invalid_appstream_metadata_is_refused_with_the_wrapped_diagnostic(
    pkg: GateFixture,
) -> None:
    # One representative through the full gate pins the engine's
    # violates-the-contract wrapping; the other nine bash variants are the
    # validator's own taxonomy, pinned per-diagnostic by
    # test_package_provenance.test_appstream_rejects_contract_violations.
    root = pkg.copy_good("appstream-wrong-component-type")
    _write_appstream_metadata(root, component_type="addon")
    _expect_refusal(
        _run_check(pkg, root),
        "installed AppStream metadata violates the standalone application contract: "
        "component type is 'addon', expected 'desktop-application'",
    )


# ---- live-root (--root /) manifest ownership ---------------------------------


def _write_package_manifest(root: Path, output: Path, omit: Path | None) -> None:
    entries: list[str] = []
    for path in sorted(root.rglob("*")):
        if not (path.is_symlink() or path.is_file()):
            continue
        if omit is not None and path == omit:
            continue
        entries.append(str(path))
    output.write_text("".join(f"{entry}\n" for entry in entries))


def _run_live_root(
    pkg: GateFixture, root: Path, manifest: Path | None
) -> subprocess.CompletedProcess[str]:
    args = ["--root", "/", "--prefix", f"{root}/usr"]
    if manifest is not None:
        args += ["--manifest", str(manifest)]
    args.append("--check-only")
    return _run_gate(pkg, args)


def test_live_root_requires_an_ownership_manifest(pkg: GateFixture) -> None:
    _expect_refusal(
        _run_live_root(pkg, pkg.good, None),
        "--manifest is required with --root /",
    )


def test_live_root_rejects_same_prefix_artifacts_outside_the_manifest(pkg: GateFixture) -> None:
    manifest = pkg.work / "live-root.manifest"
    unowned_appstream = pkg.good / "usr" / "share" / "metainfo" / "org.kde.latte-dock.appdata.xml"
    _write_package_manifest(pkg.good, manifest, unowned_appstream)
    _expect_refusal(
        _run_live_root(pkg, pkg.good, manifest),
        "installed AppStream metadata is present under the package prefix but "
        "omitted by the package manifest",
    )
    stale_tasks = (
        pkg.good
        / "usr"
        / "lib"
        / "qt6"
        / "qml"
        / "org"
        / "kde"
        / "latte"
        / "private"
        / "tasks"
        / "liblattetasksplugin.so"
    )
    _write_package_manifest(pkg.good, manifest, stale_tasks)
    _expect_refusal(_run_live_root(pkg, pkg.good, manifest), "omitted by the package manifest")


def test_live_root_accepts_manifest_owned_artifacts(pkg: GateFixture) -> None:
    manifest = pkg.work / "live-root-complete.manifest"
    _write_package_manifest(pkg.good, manifest, None)
    _expect_check_ok(_run_live_root(pkg, pkg.good, manifest))


def test_live_root_rejects_a_selected_plugin_target_outside_the_manifest(
    pkg: GateFixture,
) -> None:
    root = pkg.copy_good("live-selected-link")
    tasks_dir = root / "usr" / "lib" / "qt6" / "qml" / "org" / "kde" / "latte" / "private" / "tasks"
    real_plugin = tasks_dir / "liblattetasksplugin.real.so"
    (tasks_dir / "liblattetasksplugin.so").rename(real_plugin)
    (tasks_dir / "liblattetasksplugin.so").symlink_to(real_plugin)
    manifest = pkg.work / "live-selected.manifest"
    _write_package_manifest(root, manifest, real_plugin)
    _expect_refusal(
        _run_live_root(pkg, root, manifest),
        "resolved installed tasks QML plugin is present under the package prefix "
        "but omitted by the package manifest",
    )


def test_live_root_retains_host_absolute_symlink_semantics(pkg: GateFixture) -> None:
    root = pkg.copy_good("live-absolute-link")
    deep = root / "usr" / "lib" / "qt6" / "qml" / "org" / "kde" / "latte" / "components" / "deep"
    (deep / "Installed.qml").rename(deep / "AbsoluteTarget.qml")
    (deep / "Installed.qml").symlink_to(deep / "AbsoluteTarget.qml")  # host-absolute target
    manifest = pkg.work / "live-absolute.manifest"
    _write_package_manifest(root, manifest, None)
    _expect_check_ok(_run_live_root(pkg, root, manifest))


# ---- ambient hostility and namespace symlink semantics -----------------------


def test_good_package_accepted_and_hostile_ambient_paths_ignored(pkg: GateFixture) -> None:
    hostile_qml = pkg.work / "hostile-qml"
    hostile_data = pkg.work / "hostile-data"
    hostile_plugins = pkg.work / "hostile-plugins"
    (hostile_qml / "org" / "kde" / "latte" / "core").mkdir(parents=True)
    (hostile_data / "plasma" / "shells" / "org.kde.latte.shell").mkdir(parents=True)
    hostile_plugins.mkdir()
    result = _run_check(
        pkg,
        pkg.good,
        {
            "QML2_IMPORT_PATH": str(hostile_qml),
            "QML_IMPORT_PATH": str(hostile_qml),
            "NIXPKGS_QT6_QML_IMPORT_PATH": str(hostile_qml),
            "NIXPKGS_QML_SEARCH_PATHS": str(hostile_qml),
            "QT_PLUGIN_PATH": str(hostile_plugins),
            "XDG_DATA_DIRS": str(hostile_data),
        },
    )
    _expect_check_ok(result)
    for leak in ("hostile-qml", "hostile-data", "hostile-plugins"):
        assert leak not in result.stdout, "ambient paths leaked into the validated allow-list"


def test_isolated_roots_resolve_absolute_symlinks_inside_the_package(pkg: GateFixture) -> None:
    root = pkg.copy_good("absolute-internal-link")
    deep = root / "usr" / "lib" / "qt6" / "qml" / "org" / "kde" / "latte" / "components" / "deep"
    (deep / "Installed.qml").rename(deep / "AbsoluteTarget.qml")
    (deep / "Installed.qml").symlink_to(
        "/usr/lib/qt6/qml/org/kde/latte/components/deep/AbsoluteTarget.qml"
    )
    _expect_check_ok(_run_check(pkg, root))


def test_isolated_package_provenance_stops_at_the_package_root(pkg: GateFixture) -> None:
    marked_parent = pkg.work / "source-marked-parent"
    marked_parent.mkdir()
    (marked_parent / "CMakeLists.txt").touch()
    root = marked_parent / "installed-root"
    shutil.copytree(pkg.work / "absolute-internal-link", root, symlinks=True)
    _expect_check_ok(_run_check(pkg, root))


def test_selected_artifact_links_resolve_inside_the_package_namespace(pkg: GateFixture) -> None:
    root = pkg.copy_good("selected-absolute-links")
    binary_target = root / "usr" / "libexec" / "latte-dock.real"
    binary_target.parent.mkdir()
    (root / "usr" / "bin" / "latte-dock").rename(binary_target)
    (root / "usr" / "bin" / "latte-dock").symlink_to("/usr/libexec/latte-dock.real")
    core = root / "usr" / "lib" / "qt6" / "qml" / "org" / "kde" / "latte" / "core"
    (core / "liblattecoreplugin.so").rename(core / "liblattecoreplugin.real.so")
    (core / "liblattecoreplugin.so").symlink_to(
        "/usr/lib/qt6/qml/org/kde/latte/core/liblattecoreplugin.real.so"
    )
    shell = root / "usr" / "share" / "plasma" / "shells" / "org.kde.latte.shell"
    (shell / "metadata.json").rename(shell / "metadata.real.json")
    (shell / "metadata.json").symlink_to(
        "/usr/share/plasma/shells/org.kde.latte.shell/metadata.real.json"
    )
    result = _run_check(pkg, root)
    _expect_check_ok(result)
    assert f"installed-package-gate: binary: {binary_target}" in result.stdout


def test_selected_plugin_absolute_cross_tree_link_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("selected-cross-tree-plugin")
    core_plugin = (
        root
        / "usr"
        / "lib"
        / "qt6"
        / "qml"
        / "org"
        / "kde"
        / "latte"
        / "core"
        / "liblattecoreplugin.so"
    )
    cross_target = root / "usr" / "share" / "liblattecoreplugin.so"
    shutil.copy(core_plugin, cross_target)
    core_plugin.unlink()
    core_plugin.symlink_to("/usr/share/liblattecoreplugin.so")
    _expect_refusal(
        _run_check(pkg, root),
        "installed core QML plugin resolves outside its allowed package tree",
    )


# ---- hostile external producers (PATH injection) -----------------------------


def test_partial_find_producer_cannot_yield_validation_success(pkg: GateFixture) -> None:
    partial_bin = pkg.work / "partial-find-bin"
    partial_bin.mkdir()
    partial_result = (
        pkg.good / "usr" / "lib" / "qt6" / "qml" / "org" / "kde" / "latte" / "core" / "qmldir"
    )
    _make_executable(
        partial_bin / "find",
        f"#!/usr/bin/env bash\nprintf '%s\\0' {shlex.quote(str(partial_result))}\nexit 73\n",
    )
    result = _run_check(pkg, pkg.good, {"PATH": f"{partial_bin}:{os.environ['PATH']}"})
    _expect_refusal(result, "scan failed before a complete result was available")


def test_partial_readelf_producer_cannot_yield_validation_success(pkg: GateFixture) -> None:
    real_readelf = shutil.which("readelf")
    assert real_readelf is not None
    partial_bin = pkg.work / "partial-readelf-bin"
    partial_bin.mkdir()
    _make_executable(
        partial_bin / "readelf",
        "#!/usr/bin/env bash\n"
        'if [[ "$1" == -d ]]; then\n'
        f'  {shlex.quote(real_readelf)} "$@"\n'
        "  exit 73\n"
        "fi\n"
        f'exec {shlex.quote(real_readelf)} "$@"\n',
    )
    result = _run_check(pkg, pkg.good, {"PATH": f"{partial_bin}:{os.environ['PATH']}"})
    _expect_refusal(result, "ELF search metadata could not be read completely")


def test_path_fallback_is_forbidden_and_never_executed(pkg: GateFixture) -> None:
    root = pkg.copy_good("missing-binary")
    (root / "usr" / "bin" / "latte-dock").unlink()
    fake_path = pkg.work / "fake-path"
    fake_path.mkdir()
    fallback_marker = pkg.work / "path-fallback-ran"
    _make_executable(
        fake_path / "latte-dock",
        f"#!/usr/bin/env bash\ntouch {shlex.quote(str(fallback_marker))}\n",
    )
    result = _run_check(pkg, root, {"PATH": f"{fake_path}:{os.environ['PATH']}"})
    _expect_refusal(result, "PATH fallback is forbidden")
    assert not fallback_marker.exists(), "fallback latte-dock from PATH was executed"


# ---- root and allow-list provenance refusals ---------------------------------


def test_non_elf_executable_wrapper_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("non-elf-binary")
    _make_executable(root / "usr" / "bin" / "latte-dock", "#!/usr/bin/env bash\nexit 0\n")
    _expect_refusal(_run_check(pkg, root), "installed binary is not a valid ELF artifact")


def test_source_tree_root_is_refused(pkg: GateFixture) -> None:
    _expect_refusal(_run_check(pkg, REPO), "inside the source/build tree")


def test_marked_source_tree_root_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("source-tree")
    (root / "CMakeLists.txt").touch()
    _expect_refusal(_run_check(pkg, root), "package root is a source tree")


def test_marked_build_tree_root_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("build-tree")
    (root / "CMakeCache.txt").touch()
    _expect_refusal(_run_check(pkg, root), "package root is a CMake build tree")


def test_development_qml_stage_root_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("_qmlstage")
    _expect_refusal(_run_check(pkg, root), "development _qmlstage")


def test_nix_store_package_root_is_refused(pkg: GateFixture) -> None:
    _expect_refusal(
        _run_check(pkg, Path("/nix/store/fake-latte-dock-0.10.77")), "points into /nix/store"
    )


def test_nix_store_qml_allowlist_entry_is_refused(pkg: GateFixture) -> None:
    result = _run_check(pkg, pkg.good, {"LATTE_QML_MODULE_PATH": "/nix/store/fake-framework-qml"})
    _expect_refusal(result, "points into /nix/store")


def test_binary_symlink_escape_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("escaped-binary")
    outside_binary = pkg.work / "preinstalled-system-latte-dock"
    _make_executable(outside_binary, "#!/usr/bin/env bash\nexit 0\n")
    link = root / "usr" / "bin" / "latte-dock"
    link.unlink()
    _relative_symlink(link, outside_binary)
    _expect_refusal(
        _run_check(pkg, root), "installed binary escapes the package root through a symlink"
    )


def test_preinstalled_latte_qml_shadow_is_refused(pkg: GateFixture) -> None:
    shadow_qml = pkg.work / "preinstalled-qml"
    (shadow_qml / "org" / "kde" / "latte" / "core").mkdir(parents=True)
    (shadow_qml / "org" / "kde" / "latte" / "core" / "qmldir").touch()
    result = _run_check(pkg, pkg.good, {"LATTE_QML_MODULE_PATH": str(shadow_qml)})
    _expect_refusal(result, "contains a foreign org/kde/latte tree")


def test_preinstalled_latte_data_shadow_is_refused(pkg: GateFixture) -> None:
    shadow_data = pkg.work / "preinstalled-data"
    (shadow_data / "plasma" / "shells" / "org.kde.latte.shell").mkdir(parents=True)
    result = _run_check(pkg, pkg.good, {"LATTE_RUNTIME_DATA_PATH": str(shadow_data)})
    _expect_refusal(result, "contains foreign Latte data")


def test_qml_plugin_symlink_escape_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("escaped-plugin")
    outside_plugin = pkg.work / "preinstalled-system-liblattetasksplugin.so"
    outside_plugin.touch()
    link = (
        root
        / "usr"
        / "lib"
        / "qt6"
        / "qml"
        / "org"
        / "kde"
        / "latte"
        / "private"
        / "tasks"
        / "liblattetasksplugin.so"
    )
    link.unlink()
    _relative_symlink(link, outside_plugin)
    _expect_refusal(
        _run_check(pkg, root),
        "installed tasks QML plugin escapes the package root through a symlink",
    )


def test_containment_actions_plugin_symlink_escape_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("escaped-action-plugin")
    outside_plugin = pkg.work / "preinstalled-system-contextmenu.so"
    outside_plugin.touch()
    link = (
        root
        / "usr"
        / "lib"
        / "qt6"
        / "plugins"
        / "plasma"
        / "containmentactions"
        / "org.kde.latte.contextmenu.so"
    )
    link.unlink()
    _relative_symlink(link, outside_plugin)
    _expect_refusal(
        _run_check(pkg, root),
        "installed Latte containment-actions plugin escapes the package root through a symlink",
    )


# ---- plugin slot contents ----------------------------------------------------

_PLUGIN_SLOTS = (
    ("core QML", "usr/lib/qt6/qml/org/kde/latte/core/liblattecoreplugin.so"),
    (
        "containment QML",
        "usr/lib/qt6/qml/org/kde/latte/private/containment/liblattecontainmentplugin.so",
    ),
    ("tasks QML", "usr/lib/qt6/qml/org/kde/latte/private/tasks/liblattetasksplugin.so"),
    (
        "indicator package-structure",
        "usr/lib/qt6/plugins/kpackage/packagestructure/latte_indicator.so",
    ),
    (
        "containment-actions",
        "usr/lib/qt6/plugins/plasma/containmentactions/org.kde.latte.contextmenu.so",
    ),
)


@pytest.mark.parametrize(("label", "slot"), _PLUGIN_SLOTS, ids=[s[0] for s in _PLUGIN_SLOTS])
def test_invalid_plugin_elf_is_refused_per_slot(pkg: GateFixture, label: str, slot: str) -> None:
    root = pkg.copy_good(f"invalid-{label.replace(' ', '-')}-plugin")
    (root / slot).write_text("not an ELF artifact\n")
    _expect_refusal(_run_check(pkg, root), "is not a valid ELF artifact")


def test_generic_shared_library_in_a_plugin_slot_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("generic-action-plugin")
    shutil.copy(
        pkg.elf.generic_library,
        root
        / "usr"
        / "lib"
        / "qt6"
        / "plugins"
        / "plasma"
        / "containmentactions"
        / "org.kde.latte.contextmenu.so",
    )
    _expect_refusal(_run_check(pkg, root), "has no valid Qt plugin metadata")


def test_valid_core_plugin_with_the_wrong_iid_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("wrong-iid-core-plugin")
    _build_qt_plugin(
        pkg.toolchain,
        pkg.work,
        root
        / "usr"
        / "lib"
        / "qt6"
        / "qml"
        / "org"
        / "kde"
        / "latte"
        / "core"
        / "liblattecoreplugin.so",
        "LatteCorePlugin",
        "org.kde.KPluginFactory",
    )
    _expect_refusal(
        _run_check(pkg, root),
        "Latte core QML plugin has IID 'org.kde.KPluginFactory', "
        "expected 'org.qt-project.Qt.QQmlExtensionInterface'",
    )


def test_valid_containment_actions_plugin_with_the_wrong_class_is_refused(
    pkg: GateFixture,
) -> None:
    root = pkg.copy_good("wrong-class-action-plugin")
    _build_qt_plugin(
        pkg.toolchain,
        pkg.work,
        root
        / "usr"
        / "lib"
        / "qt6"
        / "plugins"
        / "plasma"
        / "containmentactions"
        / "org.kde.latte.contextmenu.so",
        "WrongMenuFactory",
        "org.kde.KPluginFactory",
        pkg.plugins.action_metadata,
    )
    _expect_refusal(
        _run_check(pkg, root),
        "Latte containment-actions plugin has class 'WrongMenuFactory', expected 'MenuFactory'",
    )


def test_containment_actions_plugin_with_the_wrong_category_is_refused(pkg: GateFixture) -> None:
    # The category refusal through real qtplugininfo output; the string/array
    # metadata-typing variants are pinned by the declares_* predicate tests in
    # test_package_provenance.py (same decision point, same diagnostics).
    root = pkg.copy_good("wrong-category-action-plugin")
    _build_qt_plugin(
        pkg.toolchain,
        pkg.work,
        root
        / "usr"
        / "lib"
        / "qt6"
        / "plugins"
        / "plasma"
        / "containmentactions"
        / "org.kde.latte.contextmenu.so",
        "MenuFactory",
        "org.kde.KPluginFactory",
        pkg.plugins.wrong_action_metadata,
    )
    _expect_refusal(
        _run_check(pkg, root),
        "metadata does not declare the org.kde.latte.contextmenu Plasma/ContainmentActions type",
    )


def test_unloadable_containment_actions_plugin_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("unloadable-action-plugin")
    plugin = (
        root
        / "usr"
        / "lib"
        / "qt6"
        / "plugins"
        / "plasma"
        / "containmentactions"
        / "org.kde.latte.contextmenu.so"
    )
    _build_qt_plugin(
        pkg.toolchain,
        pkg.work,
        plugin,
        "MenuFactory",
        "org.kde.KPluginFactory",
        pkg.plugins.action_metadata,
        'extern "C" void missing_latte_gate_symbol(); __attribute__((constructor)) '
        "static void require_missing_symbol() { missing_latte_gate_symbol(); }",
    )
    symbols = _run_ok(["readelf", "-Ws", str(plugin)])
    assert "missing_latte_gate_symbol" in symbols, "fixture has no unresolved symbol"
    _expect_refusal(_run_check(pkg, root), "Latte containment-actions plugin cannot be loaded")


def test_hanging_plugin_constructor_is_bounded_by_the_loader_timeout(pkg: GateFixture) -> None:
    root = pkg.copy_good("hanging-core-plugin")
    _build_qt_plugin(
        pkg.toolchain,
        pkg.work,
        root
        / "usr"
        / "lib"
        / "qt6"
        / "qml"
        / "org"
        / "kde"
        / "latte"
        / "core"
        / "liblattecoreplugin.so",
        "LatteCorePlugin",
        "org.qt-project.Qt.QQmlExtensionInterface",
        None,
        "#include <csignal>\n#include <unistd.h>\n"
        "__attribute__((constructor)) static void hang_in_constructor() "
        "{ std::signal(SIGTERM, SIG_IGN); while (true) pause(); }",
    )
    _expect_refusal(_run_check(pkg, root), "loader timed out")


# ---- installed-tree audits (audit_package_tree) ------------------------------


def _deep_qml_dir(root: Path) -> Path:
    return root / "usr" / "lib" / "qt6" / "qml" / "org" / "kde" / "latte" / "components" / "deep"


def test_nested_qml_content_symlink_escape_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("escaped-qml-content")
    outside = pkg.work / "preinstalled-system-content.qml"
    outside.touch()
    link = _deep_qml_dir(root) / "Installed.qml"
    link.unlink()
    _relative_symlink(link, outside)
    _expect_refusal(
        _run_check(pkg, root),
        "Latte QML tree contains a symlink target escaping the package root",
    )


def test_nested_external_qml_directory_symlink_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("escaped-qml-directory")
    outside_dir = pkg.work / "preinstalled-system-qml-directory"
    outside_dir.mkdir()
    (outside_dir / "Injected.qml").touch()
    deep = _deep_qml_dir(root)
    shutil.rmtree(deep)
    _relative_symlink(deep, outside_dir)
    _expect_refusal(
        _run_check(pkg, root),
        "Latte QML tree contains a symlink target escaping the package root",
    )


def test_nested_qml_source_tree_provider_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("source-qml-content")
    provider = root / "usr" / "share" / "source-provider"
    provider.mkdir()
    (provider / "CMakeLists.txt").touch()
    (provider / "Generated.qml").touch()
    link = _deep_qml_dir(root) / "Installed.qml"
    link.unlink()
    link.symlink_to("/usr/share/source-provider/Generated.qml")
    _expect_refusal(_run_check(pkg, root), "Latte QML tree contains a symlink into a source tree")


def test_nested_qml_build_tree_provider_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("build-qml-content")
    provider = root / "usr" / "share" / "external-build-provider"
    provider.mkdir()
    (provider / "CMakeCache.txt").touch()
    (provider / "generated.qml").touch()
    link = _deep_qml_dir(root) / "Installed.qml"
    link.unlink()
    link.symlink_to("/usr/share/external-build-provider/generated.qml")
    _expect_refusal(
        _run_check(pkg, root), "Latte QML tree contains a symlink into a CMake build tree"
    )


def test_nested_qml_development_stage_provider_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("stage-qml-content")
    provider = root / "usr" / "share" / "provider" / "_qmlstage"
    provider.mkdir(parents=True)
    (provider / "Staged.qml").touch()
    link = _deep_qml_dir(root) / "Installed.qml"
    link.unlink()
    link.symlink_to("/usr/share/provider/_qmlstage/Staged.qml")
    _expect_refusal(
        _run_check(pkg, root),
        "Latte QML tree contains a symlink into a development _qmlstage",
    )


def test_nested_qml_nix_provider_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("nix-qml-content")
    link = _deep_qml_dir(root) / "Installed.qml"
    link.unlink()
    link.symlink_to("/nix/store/fake-latte-qml/Injected.qml")
    _expect_refusal(_run_check(pkg, root), "Latte QML tree contains a symlink into /nix/store")


def test_nested_latte_data_symlink_escape_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("escaped-data-content")
    outside = pkg.work / "preinstalled-system-indicator.qml"
    outside.touch()
    link = root / "usr" / "share" / "latte" / "indicators" / "default" / "Installed.qml"
    link.unlink()
    _relative_symlink(link, outside)
    _expect_refusal(
        _run_check(pkg, root),
        "Latte data tree contains a symlink target escaping the package root",
    )


def test_in_prefix_cross_tree_qml_symlink_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("cross-tree-qml-content")
    provider = root / "usr" / "share" / "internal-provider"
    provider.mkdir()
    (provider / "Injected.qml").touch()
    link = _deep_qml_dir(root) / "Installed.qml"
    link.unlink()
    link.symlink_to("/usr/share/internal-provider/Injected.qml")
    _expect_refusal(
        _run_check(pkg, root),
        "Latte QML tree contains a symlink escaping its installed runtime tree",
    )


def test_latte_runtime_tree_root_symlink_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("root-symlink-data")
    latte_data = root / "usr" / "share" / "latte"
    latte_data.rename(root / "usr" / "share" / "latte-real")
    latte_data.symlink_to("latte-real")
    _expect_refusal(_run_check(pkg, root), "Latte data tree root must not be a symlink")


# ---- ELF RUNPATH/RPATH audits ------------------------------------------------


def _rebuild_binary_with_rpath(pkg: GateFixture, root: Path, rpath: str) -> Path:
    binary = root / "usr" / "bin" / "latte-dock"
    _run_ok(["cc", str(pkg.elf.source), "-o", str(binary)], env=_scrubbed_link_env())
    _run_ok(["patchelf", "--remove-rpath", str(binary)])
    _run_ok(["patchelf", "--set-rpath", rpath, str(binary)])
    return binary


def test_isolated_root_absolute_elf_runpath_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("absolute-rpath")
    _rebuild_binary_with_rpath(pkg, root, "/usr/lib")
    _expect_refusal(
        _run_check(pkg, root),
        "uses an absolute entry that the loader cannot interpret inside isolated package root",
    )


def test_live_root_accepts_an_absolute_runpath_inside_the_prefix(pkg: GateFixture) -> None:
    root = pkg.copy_good("live-absolute-rpath")
    _rebuild_binary_with_rpath(pkg, root, f"{root}/usr/lib")
    manifest = pkg.work / "live-absolute-rpath.manifest"
    _write_package_manifest(root, manifest, None)
    _expect_check_ok(_run_live_root(pkg, root, manifest))


def test_binary_elf_runpath_escape_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("rpath-binary")
    (root / "foreign-loader").mkdir()
    binary = _rebuild_binary_with_rpath(pkg, root, "$ORIGIN/../../foreign-loader")
    dynamic_metadata = _run_ok(["readelf", "-d", str(binary)])
    assert "$ORIGIN/../../foreign-loader" in dynamic_metadata, "fixture has no requested entry"
    _expect_refusal(
        _run_check(pkg, root),
        "installed binary ELF RUNPATH/RPATH entry escapes the package prefix",
    )


def test_containment_actions_elf_runpath_escape_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("rpath-action-plugin")
    (root / "foreign-loader").mkdir()
    plugin = (
        root
        / "usr"
        / "lib"
        / "qt6"
        / "plugins"
        / "plasma"
        / "containmentactions"
        / "org.kde.latte.contextmenu.so"
    )
    _run_ok(["patchelf", "--set-rpath", "$ORIGIN/../../../../../../foreign-loader", str(plugin)])
    dynamic_metadata = _run_ok(["readelf", "-d", str(plugin)])
    assert "$ORIGIN/../../../../../../foreign-loader" in dynamic_metadata
    _expect_refusal(
        _run_check(pkg, root),
        "Latte containment-actions plugin ELF RUNPATH/RPATH entry escapes the package prefix",
    )


def test_incomplete_package_is_refused(pkg: GateFixture) -> None:
    root = pkg.copy_good("incomplete")
    (
        root
        / "usr"
        / "lib"
        / "qt6"
        / "qml"
        / "org"
        / "kde"
        / "latte"
        / "private"
        / "tasks"
        / "liblattetasksplugin.so"
    ).unlink()
    _expect_refusal(_run_check(pkg, root), "missing tasks QML plugin")


# ---- dynamic-loader injection scrub ------------------------------------------


def test_dock_environment_scrubs_loader_injection_including_ld_audit(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    assert "LD_AUDIT" in LOADER_INJECTION_VARIABLES
    assert "LD_PRELOAD" in LOADER_INJECTION_VARIABLES
    for variable in LOADER_INJECTION_VARIABLES:
        monkeypatch.setenv(variable, "/dev/null")
    ambient_qml_variables = (
        "QML_IMPORT_PATH",
        "NIXPKGS_QT6_QML_IMPORT_PATH",
        "NIXPKGS_QML_SEARCH_PATHS",
        "QT_PLUGIN_PATH",
    )
    for variable in ambient_qml_variables:
        monkeypatch.setenv(variable, "/hostile")
    package = ValidatedPackage(
        binary="/pkg/usr/bin/latte-dock",
        package_qml="/pkg/usr/lib/qt6/qml",
        package_plugins="/pkg/usr/lib/qt6/plugins",
        package_data="/pkg/usr/share",
        qml_import_path="/allow:/pkg/usr/lib/qt6/qml",
        xdg_data_dirs="/pkg/usr/share:/usr/share",
        plugin_paths=(),
    )
    dock_env = package_gate._build_dock_environment(  # pyright: ignore[reportPrivateUsage]
        package, tmp_path
    )
    for variable in (*LOADER_INJECTION_VARIABLES, *ambient_qml_variables):
        assert variable not in dock_env, f"loader/ambient variable {variable} survived the scrub"
    assert dock_env["QML2_IMPORT_PATH"] == package.qml_import_path
    assert dock_env["XDG_DATA_DIRS"] == package.xdg_data_dirs
    assert dock_env["LATTE_EXTRA_PLUGIN_PATHS"] == package.package_plugins


# ---- exit-status and signal contracts ----------------------------------------


def test_failure_status_wins_over_cleanup_failure(monkeypatch: pytest.MonkeyPatch) -> None:
    # The bash EXIT-trap control: an existing failure status (37) survives a
    # failing cleanup, and a cleanup failure escalates only a would-be success.
    cleanup_calls: list[RuntimeCleanupState] = []

    def failing_cleanup(state: RuntimeCleanupState) -> int:
        cleanup_calls.append(state)
        return 2

    def no_signal_handlers() -> None:
        return None

    monkeypatch.setattr(package_gate, "run_exit_cleanup", failing_cleanup)
    monkeypatch.setattr(package_gate, "install_conventional_signal_exits", no_signal_handlers)

    def exit_37(_argv: object, _state: RuntimeCleanupState) -> None:
        raise SystemExit(37)

    monkeypatch.setattr(package_gate, "_run_gate", exit_37)
    with pytest.raises(SystemExit) as excinfo:
        package_gate.main([])
    assert excinfo.value.code == 37
    assert len(cleanup_calls) == 1, "cleanup did not run exactly once"

    def succeed(_argv: object, _state: RuntimeCleanupState) -> None:
        return None

    monkeypatch.setattr(package_gate, "_run_gate", succeed)
    assert package_gate.main([]) == 2, "cleanup failure must escalate a would-be success"


_SIGNAL_CHILD = """\
import os
import signal
import sys
from pathlib import Path

from latte_harness.package_gate import RuntimeCleanupState, run_exit_cleanup
from latte_harness.proc import install_conventional_signal_exits

install_conventional_signal_exits()
log = Path(sys.argv[2])
state = RuntimeCleanupState()
state.runtime_dir = Path(sys.argv[3])
try:
    os.kill(os.getpid(), getattr(signal, sys.argv[1]))
    with log.open("a") as handle:
        handle.write("CONTINUED\\n")
finally:
    run_exit_cleanup(state)
    with log.open("a") as handle:
        handle.write("cleanup\\n")
"""


@pytest.mark.parametrize(("signal_name", "expected_status"), [("SIGINT", 130), ("SIGTERM", 143)])
def test_signal_terminates_through_cleanup_with_the_conventional_exit(
    signal_name: str, expected_status: int, tmp_path: Path
) -> None:
    # FOREGROUND control. install_conventional_signal_exits overrides any
    # inherited SIG_IGN (signal.signal has no bash no-retrap restriction), so
    # this port is immune to the backgrounded-runner gotcha the bash selftest
    # recorded; the raised SystemExit(128+sig) must unwind through the
    # cleanup finally and exit with the distinguished code, never continue.
    log = tmp_path / f"signal-{signal_name}.log"
    log.touch()
    doomed_runtime = tmp_path / "doomed-runtime"
    doomed_runtime.mkdir()
    (doomed_runtime / "marker").write_text("x")
    result = subprocess.run(
        [sys.executable, "-c", _SIGNAL_CHILD, signal_name, str(log), str(doomed_runtime)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    assert result.returncode == expected_status, (
        f"{signal_name} exited {result.returncode} instead of {expected_status}:\n{result.stdout}"
    )
    assert log.read_text() == "cleanup\n", (
        f"{signal_name} continued after the kill or skipped cleanup: {log.read_text()!r}"
    )
    assert not doomed_runtime.exists(), "the real exit cleanup never ran"


# ---- process-group teardown gaps ---------------------------------------------

_DESCENDANT_LEADER = """\
import signal
import subprocess
import sys
import time

child_code = (
    "import os, signal, sys, time\\n"
    "signal.signal(signal.SIGTERM, signal.SIG_IGN)\\n"
    "open(sys.argv[1], 'w').write(str(os.getpid()))\\n"
    "open(sys.argv[2], 'w').close()\\n"
    "while True:\\n"
    "    time.sleep(1)\\n"
)
subprocess.Popen([sys.executable, "-c", child_code, sys.argv[1], sys.argv[2]])


def finish(signum, frame):
    sys.exit(0)


signal.signal(signal.SIGTERM, finish)
while True:
    time.sleep(1)
"""


def test_group_cleanup_escalates_when_a_descendant_survives_the_leader(tmp_path: Path) -> None:
    # The leader exits cleanly on SIGTERM but its TERM-ignoring descendant
    # keeps the group alive, so only the bounded SIGKILL escalation clears it.
    pid_file = tmp_path / "descendant.pid"
    ready = tmp_path / "descendant.ready"
    leader = subprocess.Popen(
        [sys.executable, "-c", _DESCENDANT_LEADER, str(pid_file), str(ready)],
        start_new_session=True,
    )
    try:
        _await_path(ready)
        descendant_pid = int(pid_file.read_text())
        code = vehicle.stop_process_group(
            leader.pid,
            "process group with TERM-ignoring descendant",
            term_attempts=1,
            term_delay=0.01,
            kill_attempts=100,
            kill_delay=0.01,
        )
        assert code == 0
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            try:
                os.kill(descendant_pid, 0)
            except ProcessLookupError:
                break
            time.sleep(0.01)
        else:
            pytest.fail("cleanup left a TERM-ignoring descendant alive")
    finally:
        # Best-effort: the leader is this process's unreaped zombie child, so
        # its pgid stays valid (killpg succeeds as a no-op) until the wait.
        with suppress(ProcessLookupError):
            os.killpg(leader.pid, signal.SIGKILL)
        leader.wait(timeout=5)


_ZOMBIE_HOLDER = """\
import os
import signal

child = os.fork()
if child == 0:
    os.setpgid(0, 0)
    os._exit(0)
os.waitid(os.P_PID, child, os.WEXITED | os.WNOWAIT)
print(child, flush=True)


def finish(signum, frame):
    os.waitpid(child, 0)
    os._exit(0)


signal.signal(signal.SIGTERM, finish)
while True:
    signal.pause()
"""


def test_zombie_only_process_group_counts_as_stopped_without_reaping(tmp_path: Path) -> None:
    # A dead-but-unreaped group member (held zombie by a live parent outside
    # the group) is not a live group: the teardown succeeds without signalling
    # and must not steal the holder's reap.
    holder = subprocess.Popen(
        [sys.executable, "-c", _ZOMBIE_HOLDER], stdout=subprocess.PIPE, text=True
    )
    try:
        stdout = holder.stdout
        assert stdout is not None
        zombie_pid = int(stdout.readline())
        assert vehicle.group_live_status(zombie_pid) == "gone"
        assert vehicle.stop_process_group(zombie_pid, "zombie-only process group", 1, 0, 1, 0) == 0
        assert Path(f"/proc/{zombie_pid}").is_dir(), "the zombie was reaped before its holder"
    finally:
        holder.send_signal(signal.SIGTERM)
        holder.wait(timeout=5)


def test_unkillable_group_cleanup_is_bounded_and_loud(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    # The simulated-unkillable control: a group that never leaves 'live' must
    # exhaust its bounded TERM and KILL waits, refuse with exit 2, and say so.
    # (The bash twin also guarded its `wait` builtin; the Python teardown holds
    # no such wait, so the ported contract is the bound and the loud refusal.)
    def always_live(_pgid: int) -> str:
        return "live"

    sent: list[signal.Signals] = []

    def record_signal(_pgid: int, sig: signal.Signals) -> None:
        sent.append(sig)

    monkeypatch.setattr(vehicle, "group_live_status", always_live)
    monkeypatch.setattr(vehicle, "_signal_group_or_leader", record_signal)  # pyright: ignore[reportPrivateUsage]
    code = vehicle.stop_process_group(424242, "unkillable process group", 1, 0, 1, 0)
    assert code == 2
    assert sent == [signal.SIGTERM, signal.SIGKILL]
    assert "still exists after bounded SIGKILL wait" in capsys.readouterr().err


@pytest.mark.parametrize("pgrep_status", [2, 3])
def test_pgrep_operational_failure_aborts_cleanup(
    pgrep_status: int,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
) -> None:
    fake_bin = tmp_path / "bin"
    fake_bin.mkdir()
    _make_executable(fake_bin / "pgrep", f"#!/usr/bin/env bash\nexit {pgrep_status}\n")
    monkeypatch.setenv("PATH", f"{fake_bin}:{os.environ['PATH']}")
    code = vehicle.stop_process_group(424242, "unpollable process group", 1, 0, 1, 0)
    assert code == 2
    assert (
        f"pgrep failed while polling process group 424242 with status {pgrep_status}"
        in capsys.readouterr().err
    )


# ---- shutdown wait-status taxonomy (_shut_down_dock) -------------------------


def _spawn_dock_stub(code: str) -> tuple[SessionProcess, str | None]:
    proc = SessionProcess.spawn([sys.executable, "-c", code], stdout=subprocess.PIPE)
    stdout = proc.stdout
    assert stdout is not None
    assert stdout.readline().strip() == "ready"
    return proc, vehicle.leader_starttime(proc.pid)


_CLEAN_TERM_STUB = (
    "import signal, sys, time\n"
    "signal.signal(signal.SIGTERM, lambda *args: sys.exit(0))\n"
    "print('ready', flush=True)\n"
    "while True:\n"
    "    time.sleep(1)\n"
)

_ABORT_ON_TERM_STUB = (
    "import os, resource, signal, time\n"
    "resource.setrlimit(resource.RLIMIT_CORE, (0, 0))\n"
    "signal.signal(signal.SIGTERM, lambda *args: os.abort())\n"
    "print('ready', flush=True)\n"
    "while True:\n"
    "    time.sleep(1)\n"
)

_EXIT_SEVEN_ON_TERM_STUB = (
    "import signal, sys, time\n"
    "signal.signal(signal.SIGTERM, lambda *args: sys.exit(7))\n"
    "print('ready', flush=True)\n"
    "while True:\n"
    "    time.sleep(1)\n"
)


def test_expected_zero_sigterm_wait_status_is_preserved() -> None:
    dock, starttime = _spawn_dock_stub(_CLEAN_TERM_STUB)
    state = RuntimeCleanupState(dock=dock, dock_starttime=starttime)
    package_gate._shut_down_dock(state)  # pyright: ignore[reportPrivateUsage]
    assert state.dock is None


def test_sigabrt_wait_status_is_rejected() -> None:
    dock, starttime = _spawn_dock_stub(_ABORT_ON_TERM_STUB)
    state = RuntimeCleanupState(dock=dock, dock_starttime=starttime)
    with pytest.raises(GateRefusal) as refusal:
        package_gate._shut_down_dock(state)  # pyright: ignore[reportPrivateUsage]
    assert str(refusal.value) == "installed dock after SIGTERM exited with status 134, expected 0"


def test_nonzero_sigterm_wait_status_is_rejected() -> None:
    dock, starttime = _spawn_dock_stub(_EXIT_SEVEN_ON_TERM_STUB)
    state = RuntimeCleanupState(dock=dock, dock_starttime=starttime)
    with pytest.raises(GateRefusal) as refusal:
        package_gate._shut_down_dock(state)  # pyright: ignore[reportPrivateUsage]
    assert str(refusal.value) == "installed dock after SIGTERM exited with status 7, expected 0"
