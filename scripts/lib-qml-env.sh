# Shared QML environment assembly for the headless QML checks (sourced by
# qml-compile-gate.sh and qml-interaction-tests.sh). Assumes bash with
# nounset; sets the `imports` array and the `stage` directory.
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

    # NIXPKGS_QT6_QML_IMPORT_PATH covers the KDE framework modules;
    # NIXPKGS_QML_SEARCH_PATHS the pinned Qt modules themselves. Entries from
    # a foreign Qt closure (the plasma-workspace build dependency currently
    # rides a different Qt pin) cannot dlopen here and are filtered out.
    # LATTE_QML_MODULE_PATH is exported by the flake devShell and lists the
    # qml dirs of the pinned dependency set - the only trustworthy source.
    # The session's NIXPKGS_QT6_QML_IMPORT_PATH leaks the desktop's own
    # (differently pinned) module paths and is deliberately ignored.
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

    # the staged Latte modules win over everything
    imports+=(-import "$stage/lib/qml")
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
