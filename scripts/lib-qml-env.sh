# Shared QML environment assembly for the headless QML checks (sourced by
# qml-compile-gate.sh and qml-interaction-tests.sh). Assumes bash with
# nounset; sets the `imports` array, the `stage`/`build` directories and the
# `qmldir` install subdir (lib/qml on nixpkgs, lib/qt6/qml on Arch/Fedora/
# Debian - the caller needs it to find the staged org.kde.latte.* modules).
#
# The user profile's QML2_IMPORT_PATH must not leak in: it carries Qt 5 and
# differently-pinned Qt 6 builds whose plugins fail to load in this runtime
# (private-API symbol versioning). The same applies to the engine's ambient
# defaults on this host, so every needed module is passed explicitly with
# -import; a later -import outranks earlier ones and the ambient defaults.

qml_env_setup() {
    local repo="$1"
    build="${BUILD:-$repo/build}"
    stage="${STAGE:-$build/_qmlstage}"

    unset QML2_IMPORT_PATH QML_IMPORT_PATH

    # The nixpkgs Qt6 runtime wrapper reads NIXPKGS_QT6_QML_IMPORT_PATH /
    # NIXPKGS_QML_SEARCH_PATHS from the ENV to seed the QML engine's import
    # path, independently of QML2_IMPORT_PATH - so ignoring them while building
    # `imports` from LATTE_QML_MODULE_PATH below does NOT stop them winning.
    # A live case (caught 2026-07-18 via /proc/<dock>/maps): the desktop
    # session leaks NIXPKGS_QT6_QML_IMPORT_PATH carrying the SYSTEM-INSTALLED
    # packaged latte-dock (the NixOS module's environment.systemPackages copy,
    # when programs.latte-dock.enable is on), whose
    # org.kde.latte.private.containment plugin then shadowed the staged one -
    # so every containment/plugin change (layoutmanager, maskgeometry) ran the
    # packaged binary, not the worktree build ("landed but never ran").
    # Strip ONLY the packaged latte-dock leaf, not the whole var: these paths
    # also carry KDE framework modules the QML gates import (unsetting the var
    # wholesale drops a module qmlcontracts needs). Deny-list the specific
    # store leaf, per the regression rule (never nuke a shared root). The
    # staged latte-dock lives under a $HOME path, not a /nix/store/*-latte-dock-*
    # one, so it is never matched.
    local _v _filtered
    for _v in NIXPKGS_QT6_QML_IMPORT_PATH NIXPKGS_QML_SEARCH_PATHS; do
        [[ -n "${!_v:-}" ]] || continue
        _filtered=$(tr ':' '\n' <<<"${!_v}" | grep -vE '/nix/store/[^/]*-latte-dock-' | paste -sd:)
        export "$_v=$_filtered"
    done
    local p
    imports=()
    IFS=':' read -ra _qmldirs <<< "${LATTE_QML_MODULE_PATH:?scripts/lib-qml-env.sh needs the flake devShell (LATTE_QML_MODULE_PATH)}"
    for p in "${_qmldirs[@]}"; do
        [[ -d "$p" ]] && imports+=(-import "$p")
    done

    # Pin QML modules to the exact packages our binaries link (libplasma,
    # plasma-workspace via the tasks plugin, ...) in case a second copy of a
    # provider exists in the environment - the user session leaks its own
    # Plasma's paths in, and a module resolved from a foreign build fails to
    # dlopen. Later -import wins, so these outrank everything above.
    local so linked
    for so in "$build/bin/latte-dock" "$build/bin/liblattetasksplugin.so"; do
        [[ -e "$so" ]] || continue
        for linked in $(ldd "$so" | perl -ne 'print "$1\n" if m{=> (/nix/store/[^/]+)/}' | sort -u); do
            [[ -d "$linked/lib/qt-6/qml" ]] && imports+=(-import "$linked/lib/qt-6/qml")
        done
    done

    # the staged Latte modules win over everything, so this import goes last.
    # KDE_INSTALL_QMLDIR (where cmake --install drops org.kde.latte.*) follows
    # the distro's Qt6 qml layout - nixpkgs lib/qml, Arch/Fedora/Debian
    # lib/qt6/qml - and the stage does not exist yet when this runs
    # (qml_env_stage installs later), so the filesystem cannot be probed for
    # it. The build emits the authoritative configure-time value to
    # latte-qmldir.txt; read it, defaulting to lib/qml only if the file is
    # somehow absent. Without this, scenes importing org.kde.latte.* fail
    # "module not installed" on any distro whose QMLDIR is not lib/qml.
    # leaked to the caller (like build/stage above): qml-interaction-tests.sh
    # reuses it to probe whether the staged Latte tree already exists on this
    # distro's QMLDIR, instead of hardcoding one distro's lib/qml spelling.
    qmldir=""
    [[ -f "$build/latte-qmldir.txt" ]] && read -r qmldir < "$build/latte-qmldir.txt"
    [[ -n "$qmldir" ]] || qmldir="lib/qml"
    imports+=(-import "$stage/$qmldir")
}

# Undo qml_env_stage's manifest displacement. Global state instead of
# locals because the EXIT/INT/TERM traps fire after the function's scope
# is gone. Idempotent: the trap and the in-line calls can both run.
_qml_env_restore_manifest() {
    [[ -n "${_QML_MANIFEST:-}" ]] || return 0
    if [[ -n "$_QML_HAD_MANIFEST" ]]; then
        mv -f "$_QML_MANIFEST.pre-stage" "$_QML_MANIFEST"
    else
        rm -f "$_QML_MANIFEST"
    fi
    _QML_MANIFEST=""
    trap - EXIT INT TERM
}

qml_env_stage() {
    echo "staging $build -> $stage ..."

    # cmake --install unconditionally rewrites build/install_manifest.txt,
    # which ECM's appstreamtest reads; leaving the staged manifest behind
    # changes what that test validates. Preserve whatever state it had -
    # UNLESS what it had is itself a leaked staged manifest: staging runs
    # older than the preserve logic (45c15433) left their manifests behind,
    # and preserving one keeps appstreamtest un-vacuumed with staged paths
    # forever (caught 2026-07-15: full ctest failed on the known Phase 11
    # hyphen warning because of exactly such a leftover). Staged paths are
    # never a legitimate manifest state; drop them and a real install
    # regenerates the manifest.
    _QML_MANIFEST="$build/install_manifest.txt" _QML_HAD_MANIFEST=""
    if [[ -f "$_QML_MANIFEST" ]]; then
        if grep -q -- "$stage" "$_QML_MANIFEST"; then
            echo "dropping leaked staged install_manifest.txt"
            rm -f "$_QML_MANIFEST"
        else
            _QML_HAD_MANIFEST=1
            cp "$_QML_MANIFEST" "$_QML_MANIFEST.pre-stage"
        fi
    fi
    # Restore even when the install below is interrupted; without these an
    # aborted staging leaks the staged manifest (SIGKILL still leaks, which
    # is what the self-heal branch above is for).
    trap '_qml_env_restore_manifest' EXIT
    trap '_qml_env_restore_manifest; exit 130' INT
    trap '_qml_env_restore_manifest; exit 143' TERM

    # Install to a throwaway prefix, then checksum-sync into the real stage
    # so files whose CONTENT did not change keep their existing mtime. The
    # QML disk cache (~/.cache/lattedock/qmlcache) validates entries against
    # the source file's timestamp: the old rm-rf-and-reinstall staging gave
    # every QML file a fresh mtime on every restart, so the dock recompiled
    # its entire QML tree per run (239 cache entries rewritten each restart,
    # measured 2026-07-15; the previews' first adoption alone paid ~480ms of
    # compile-plus-create). Deliberately NOT rsync -a: -t would stamp the
    # fresh install's mtimes onto content-identical files and defeat the
    # whole point.
    rm -rf "$stage.new"
    cmake --install "$build" --prefix "$stage.new" >"$stage.log" 2>&1 || {
        echo "STAGE FAILED:"; tail -15 "$stage.log"; rm -rf "$stage.new"
        _qml_env_restore_manifest; return 2;
    }
    mkdir -p "$stage"
    rsync -rlp --checksum --delete "$stage.new/" "$stage/" >>"$stage.log" 2>&1 || {
        echo "STAGE SYNC FAILED:"; tail -15 "$stage.log"; rm -rf "$stage.new"
        _qml_env_restore_manifest; return 2;
    }
    rm -rf "$stage.new"

    _qml_env_restore_manifest
}
