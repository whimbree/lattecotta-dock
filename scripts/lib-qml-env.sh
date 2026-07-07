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

qml_env_stage() {
    echo "staging $build -> $stage ..."
    rm -rf "$stage"

    # cmake --install unconditionally rewrites build/install_manifest.txt,
    # which ECM's appstreamtest reads; leaving the staged manifest behind
    # changes what that test validates. Preserve whatever state it had.
    local manifest="$build/install_manifest.txt" had_manifest=""
    [[ -f "$manifest" ]] && { had_manifest=1; cp "$manifest" "$manifest.pre-stage"; }

    cmake --install "$build" --prefix "$stage" >"$stage.log" 2>&1 || {
        echo "STAGE FAILED:"; tail -15 "$stage.log"; return 2;
    }

    if [[ -n "$had_manifest" ]]; then
        mv "$manifest.pre-stage" "$manifest"
    else
        rm -f "$manifest"
    fi
}
