# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The staged-run environment core: the typed port of scripts/run-staged.sh (BP-2e).

Not the desk entry point. This assembles the pinned runtime environment for the
staged dock (the QML import path, the XDG config/data trees, the plugin
allow-list) and then os.exec's the built binary IN THE FOREGROUND of the current
session. It manages no other instance - deliberately, so a harness can drive it
against a nested compositor or under a wrapper (gdb, timeout) with no risk of
touching a dock running elsewhere. The kill-and-detach lifecycle lives only in
restart-staged.sh; this module is the env core that script delegates to.

WHY os.exec, not a spawned child (the pid-identity contract). run-staged.sh is
exec'd by restart-staged.sh under setsid, so its pid is the detached session
leader; the e2e/seed callers record that same pid as THE DOCK PID and later
SIGTERM it. Both only hold if the launcher pid IS the dock's pid. So the shim
execs the venv interpreter directly (not `uv run`, which forks a wrapper child
that would sit between the recorded pid and the binary), and this module then
os.exec's the binary in place: run-staged.sh -> python -> latte-dock, one pid
throughout.

The import-path doctrine, the D8/D271 packaged-latte-dock leaf strip, the
linked-provider pins, and the stage/restore manifest are all owned by
latte_harness.qmlenv; this module reuses those primitives and adds only the
run-staged-specific pieces the bash carried: the throwaway config home, the
XDG_DATA_DIRS allow-list, the QT platform/plugin pins, and the kwindowsystem
plugin leaf. Every variable is mirrored byte-for-byte against the bash; the
equivalence is proven by an env-dump diff (recorded in the BP-2e PR).
"""

from __future__ import annotations

import fnmatch
import os
import pwd
import re
import shlex
import shutil
import sys
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from latte_harness import paths, qmlenv
from latte_harness.proc import install_conventional_signal_exits, run

TOOL = "staged_run"

# The built binary, relative to the build tree. run-staged always execs exactly
# this path (never a PATH-resolved dock); the asan-binary-shadow e2e recipe is
# the standing guard on that invariant.
BINARY_REL = Path("bin/latte-dock")

# The XDG_DATA_DIRS allow-list, byte-for-byte the bash `case` globs. XDG_DATA_DIRS
# is rebuilt rather than inherited wholesale: under `nix develop` the inherited
# value is the entire BUILD closure (~270 entries), and QStandardPaths::locate
# walks every entry per lookup (a measured 23,255 stat()s, 96% ENOENT, for one
# KSvg theme adoption). Only the KDE runtime data dirs the dock actually reads
# (strace-derived) are kept, in devshell order so the pinned copies keep winning
# over the system profile. Per the regression rules this is an allow-list of
# leaves, not a shared root. fnmatch matches these exactly as bash `case` does:
# `*` spans any characters including '/'.
_DATA_DIR_ALLOW_GLOBS = (
    "*libplasma-*/share",
    "*plasma-workspace-*/share",
    "*plasma-desktop-*/share",
    "*-kwin-*/share",
    "*-breeze-*/share",
    "*kio-extras-*/share",
    "*kcoreaddons-*/share",
    "*kguiaddons-*/share",
    "*-kirigami-*/share",
)

# The system data roots appended after the allow-list, verbatim from the bash.
# The nix-profile / per-user / current-system entries are NixOS-specific and are
# harmlessly skipped by QStandardPaths on other distros, where /usr/share backs
# the icons/themes/plasma assets.
_SYSTEM_DATA_ROOTS_TAIL = (
    "{home}/.nix-profile/share",
    "/etc/profiles/per-user/{user}/share",
    "/run/current-system/sw/share",
    "/usr/share",
)

# The kwindowsystem runtime plugin leaf: the first ldd store path of that exact
# package. Mirrors the bash `perl -ne 'm{=> (/nix/store/[^/]+-kwindowsystem-[^/]+)/}'
# | head -1`. Without this leaf on LATTE_EXTRA_PLUGIN_PATHS, KWindowSystem has no
# wayland backend in-process (KWindowShadow fails on every dialog, slideWindow is
# a silent no-op). It ships only kwindowsystem's own platform plugins, nothing
# that can shadow a module the stage provides.
_KWINDOWSYSTEM_STORE = re.compile(r"=> (/nix/store/[^/]+-kwindowsystem-[^/]+)/")


class MissingHomeError(RuntimeError):
    """Neither the requested config-home nor $HOME is resolvable.

    Mirrors the bash `set -u` abort on a bare `$HOME` in the --user-config and
    kdeglobals paths: those branches assume a real session and fail loudly, not
    silently, when it is absent.
    """


def _passwd_home(env: Mapping[str, str]) -> str:
    """$HOME, or the current user's passwd home when unset (bash `${HOME:-...}`).

    Only the XDG_DATA_DIRS assembly uses this fallback, matching the bash: a bare
    (non-login) shell such as a CI container may export neither $USER nor $HOME,
    and that line always runs, so it resolves both from the passwd db.
    """
    home = env.get("HOME")
    if home:
        return home
    return pwd.getpwuid(os.getuid()).pw_dir


def _passwd_user(env: Mapping[str, str]) -> str:
    """$USER, or the current user's passwd name when unset (bash `${USER:-$(id -un)}`)."""
    user = env.get("USER")
    if user:
        return user
    return pwd.getpwuid(os.getuid()).pw_name


def _session_config_home(env: Mapping[str, str]) -> Path:
    """The session's config home: XDG_CONFIG_HOME, else $HOME/.config.

    The bash spells this `${XDG_CONFIG_HOME:-$HOME/.config}` with a bare $HOME, so
    an absent $HOME is a loud failure here, not a passwd fallback.
    """
    xdg = env.get("XDG_CONFIG_HOME")
    if xdg:
        return Path(xdg)
    home = env.get("HOME")
    if not home:
        raise MissingHomeError("neither XDG_CONFIG_HOME nor HOME is set")
    return Path(home) / ".config"


def resolve_confighome(build: Path, env: Mapping[str, str], user_config: bool) -> Path:
    """Where the dock reads/writes its config.

    Default: LATTE_CONFIG_HOME, else the throwaway build/_runconfig, so the real
    Latte/Plasma config is never touched. With --user-config the dock runs against
    the real session config (XDG_CONFIG_HOME, else $HOME/.config), which overrides
    LATTE_CONFIG_HOME exactly as the bash `if $1 == --user-config` branch does.
    """
    if user_config:
        return _session_config_home(env)
    latte = env.get("LATTE_CONFIG_HOME")
    return Path(latte) if latte else build / "_runconfig"


def seed_throwaway_kdeglobals(confighome: Path, build: Path, env: Mapping[str, str]) -> None:
    """Copy the session's kdeglobals into a fresh throwaway config home.

    Without a kdeglobals the dock resolves the default LIGHT scheme and the bar
    renders white under a dark session (the black-silhouette report). Copy, never
    link: a throwaway run must not write back into the real session config. Only
    the exact throwaway default (build/_runconfig) is seeded, and only when it has
    no kdeglobals yet and the session has one - byte-for-byte the bash guard.
    """
    if confighome != build / "_runconfig":
        return
    if (confighome / "kdeglobals").is_file():
        return
    source = _session_config_home(env) / "kdeglobals"
    if source.is_file():
        shutil.copyfile(source, confighome / "kdeglobals")


def rebuild_xdg_data_dirs(stage: Path, env: Mapping[str, str]) -> str:
    """The allow-listed XDG_DATA_DIRS: staged share first, KDE runtime dirs, tail.

    The staged tree leads so the staged shell package, containment and indicators
    win. Then the deduplicated, allow-list-matching entries of the inherited
    XDG_DATA_DIRS (dedup-first-wins, matching bash `awk 'NF && !seen[$0]++'` run
    before the `case`). Then the fixed system roots.
    """
    runtime = [f"{stage}/share"]
    current = env.get("XDG_DATA_DIRS")
    if current:
        seen: set[str] = set()
        for entry in current.split(":"):
            if not entry or entry in seen:
                continue
            seen.add(entry)
            if any(fnmatch.fnmatch(entry, glob) for glob in _DATA_DIR_ALLOW_GLOBS):
                runtime.append(entry)
    home = _passwd_home(env)
    user = _passwd_user(env)
    tail = [root.format(home=home, user=user) for root in _SYSTEM_DATA_ROOTS_TAIL]
    return ":".join(runtime + tail)


def parse_kwindowsystem_prefix(ldd_output: str) -> str | None:
    """The first /nix/store/<pkg>-kwindowsystem-<ver> prefix in an ldd dump.

    Mirrors the bash `perl ... | head -1`: the first matching line wins, non-matching
    lines contribute nothing, and no match yields None.
    """
    for line in ldd_output.splitlines():
        match = _KWINDOWSYSTEM_STORE.search(line)
        if match:
            return match.group(1)
    return None


def resolve_kwindowsystem_plugin_dir(build: Path) -> Path | None:
    """The kwindowsystem qt-6 plugins dir the built binary links, or None.

    None (which drives the warning) covers all three bash cases: no built binary,
    no kwindowsystem in the ldd, and a first-match whose plugins dir is absent
    (`head -1` never tries a second match, so neither does this).
    """
    binary = build / BINARY_REL
    if not binary.exists():
        return None
    ldd = run(["ldd", str(binary)], capture=True)
    prefix = parse_kwindowsystem_prefix(ldd.stdout)
    if prefix is None:
        return None
    plugins = Path(prefix) / "lib" / "qt-6" / "plugins"
    return plugins if plugins.is_dir() else None


def _resolve_wrapper(env: Mapping[str, str]) -> list[str]:
    """The optional LATTE_RUN_WRAPPER prefix words (e.g. gdb -batch -ex run --args).

    Empty or unset yields no words, matching the bash `${LATTE_RUN_WRAPPER:-}`
    empty expansion. shlex.split is used for the split (quote-aware where the bare
    bash word-split is not); the documented wrapper values are plain whitespace
    tokens, so the two agree, and quote-awareness is only ever a strict improvement.
    """
    wrapper = env.get("LATTE_RUN_WRAPPER")
    return shlex.split(wrapper) if wrapper else []


@dataclass(frozen=True, slots=True)
class StagedRun:
    """The fully-assembled staged run: the dock's environment and its exec argv.

    ``env`` is the complete environment the binary is exec'd with (a copy of the
    inherited env plus the run-staged mutations). ``exec_argv`` is the argv,
    optionally led by the LATTE_RUN_WRAPPER words. ``kwindowsystem_missing`` drives
    the one-line warning the bash printed when the plugin leaf could not be found.
    """

    env: dict[str, str]
    exec_argv: list[str]
    confighome: Path
    build: Path
    stage: Path
    kwindowsystem_missing: bool


def assemble_dock_env(repo: Path, base_env: Mapping[str, str], args: Sequence[str]) -> StagedRun:
    """Assemble the staged dock's environment and exec argv from the inherited env.

    Pure: no filesystem writes, no process spawns beyond the read-only ldd probe.
    The side effects (config-home mkdir, kdeglobals seed, staging, exec) live in
    ``main`` so this stays the testable, diff-able env contract.
    """
    module_path = base_env.get("LATTE_QML_MODULE_PATH")
    if not module_path:
        raise qmlenv.MissingModulePathError

    build = Path(base_env["BUILD"]) if base_env.get("BUILD") else repo / "build"
    stage = Path(base_env["STAGE"]) if base_env.get("STAGE") else build / "_qmlstage"
    qmldir = qmlenv.resolve_install_qmldir(build)

    # --user-config is consumed off the front; the rest forwards to the binary.
    remaining = list(args)
    user_config = bool(remaining) and remaining[0] == "--user-config"
    if user_config:
        remaining = remaining[1:]

    confighome = resolve_confighome(build, base_env, user_config)

    # The `imports` array flattened into the colon path the engine reads from
    # QML2_IMPORT_PATH (the -import dirs are the odd elements of the pair list).
    imports = qmlenv.assemble_imports(module_path, build, stage, qmldir)
    importpath = ":".join(imports[1::2])

    env = dict(base_env)
    # QML_IMPORT_PATH is unset and never restored; QML2_IMPORT_PATH is unset then
    # re-set to the pinned import path (the user profile's copies carry Qt5 and
    # differently-pinned Qt6 builds whose plugins fail to load in this runtime).
    env.pop("QML_IMPORT_PATH", None)
    for var in qmlenv.NIXPKGS_SEED_VARS:
        current = env.get(var)
        if current:
            env[var] = qmlenv.strip_packaged_latte_dock(current)
    env["QML2_IMPORT_PATH"] = importpath
    env["XDG_CONFIG_HOME"] = str(confighome)
    env["XDG_DATA_DIRS"] = rebuild_xdg_data_dirs(stage, base_env)
    env["QT_QPA_PLATFORM"] = "wayland"
    # The desktop session's QT_PLUGIN_PATH points at the system Plasma's plugins (a
    # different Qt build); loading its platform-theme plugin segfaults in
    # QCoreApplication::init. Drop it; the nix-built Qt finds its own via baked-in
    # paths. QT_QPA_PLATFORMTHEME is emptied to skip the platform-theme integration.
    env.pop("QT_PLUGIN_PATH", None)
    # Latte's own staged C++ plugins are handed over as LATTE_EXTRA_PLUGIN_PATHS
    # (main.cpp feeds it into the process-local library paths), never as
    # QT_PLUGIN_PATH: the dock forwards its whole env to every app it launches, and
    # a child of a different Qt build dlopening the pinned plugins is an ABI
    # mismatch. The kwindowsystem leaf is appended when present.
    extra_plugin_paths = f"{stage}/lib/plugins"
    kwindowsystem = resolve_kwindowsystem_plugin_dir(build)
    if kwindowsystem is not None:
        extra_plugin_paths = f"{extra_plugin_paths}:{kwindowsystem}"
    env["LATTE_EXTRA_PLUGIN_PATHS"] = extra_plugin_paths
    env["QT_QPA_PLATFORMTHEME"] = ""

    exec_argv = [*_resolve_wrapper(base_env), str(build / BINARY_REL), *remaining]

    return StagedRun(
        env=env,
        exec_argv=exec_argv,
        confighome=confighome,
        build=build,
        stage=stage,
        kwindowsystem_missing=kwindowsystem is None,
    )


def _refuse_without_module_path() -> None:
    print(
        f"{TOOL}: FAIL LATTE_QML_MODULE_PATH is unset; run inside the flake "
        "devShell (nix develop provides it)",
        file=sys.stderr,
        flush=True,
    )
    raise SystemExit(1)


def main(argv: Sequence[str] | None = None) -> None:
    """Assemble the environment, stage the QML modules, and exec the dock in place.

    The step order mirrors the bash: refuse without the module path, refuse a
    missing binary (exit 2), stage, seed the throwaway config, warn on a missing
    kwindowsystem leaf, then exec. install_conventional_signal_exits guards the
    pre-exec window (chiefly staging) so a SIGINT/SIGTERM restores the install
    manifest; os.exec then resets the handlers as it replaces the process image.
    """
    args = list(sys.argv[1:] if argv is None else argv)
    repo = paths.find_repo_root()

    try:
        assembled = assemble_dock_env(repo, os.environ, args)
    except qmlenv.MissingModulePathError:
        _refuse_without_module_path()
        return  # unreachable; _refuse raises. keeps the type checker's flow honest.

    binary = assembled.build / BINARY_REL
    if not os.access(binary, os.X_OK):
        print(f"no built binary at {binary}", flush=True)
        raise SystemExit(2)

    install_conventional_signal_exits()

    qmlenv.stage_qml_modules(assembled.build, assembled.stage)

    assembled.confighome.mkdir(parents=True, exist_ok=True)
    seed_throwaway_kdeglobals(assembled.confighome, assembled.build, os.environ)

    if assembled.kwindowsystem_missing:
        print(
            "WARNING: kwindowsystem plugin dir not found; dialog shadows and popup "
            "slide will be missing",
            file=sys.stderr,
            flush=True,
        )

    print(f"config home: {assembled.confighome}", flush=True)
    wayland = os.environ.get("WAYLAND_DISPLAY", "<unset>")
    print(f"starting staged latte-dock against WAYLAND_DISPLAY={wayland} ...", flush=True)

    try:
        os.execvpe(assembled.exec_argv[0], assembled.exec_argv, assembled.env)
    except OSError as exc:
        # exec only fails to START the binary (missing wrapper on PATH, ENOEXEC);
        # once it succeeds this process is gone. Fail loudly, never silently.
        target = assembled.exec_argv[0]
        print(f"{TOOL}: FAILED to exec {target}: {exc}", file=sys.stderr, flush=True)
        raise SystemExit(127) from exc


if __name__ == "__main__":
    main()
