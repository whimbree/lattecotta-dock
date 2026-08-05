# Shared QML environment assembly for the headless QML checks and staged runs
# (sourced by qml-compile-gate.sh, qml-interaction-tests.sh, run-staged.sh,
# sceneprobe-gate.sh and tests/coverage/qmllint-gate.sh). Assumes bash with
# nounset.
#
# THIN BRIDGE (BP-1a): the logic lives in the typed latte_harness.qmlenv module
# now (path allow-list construction and the stage/restore manifest are data
# logic, which is Python's home turf). This file keeps the SAME sourced
# interface the five consumers rely on - the functions qml_env_setup and
# qml_env_stage, and the variables/array they set: the `imports` array (holding
# "-import <dir>" pairs), the `stage`/`build` directories, and the `qmldir`
# install subdir (lib/qml on nixpkgs, lib/qt6/qml on Arch/Fedora/Debian). The
# import-path doctrine, the D8/D271 nixpkgs-seed-var leaf-strip, and the
# manifest preserve/restore are all documented in the module; see
# harness/src/latte_harness/qmlenv.py.

# qml_env_setup <repo>: eval the module's emitted shell so `imports`, `build`,
# `stage`, `qmldir` and the env mutations (QML2 import-path unset, filtered
# nixpkgs seed exports) land in the sourcing shell exactly as before.
qml_env_setup() {
    local repo="$1"
    _QMLENV_HARNESS="$repo/harness"
    local _out
    _out="$(uv run --locked --project "$_QMLENV_HARNESS" python -m latte_harness.qmlenv setup "$repo")" || return
    eval "$_out"
}

# qml_env_stage: stage build -> stage/_qmlstage with the install-manifest
# preserved. The module owns the whole choreography (throwaway-prefix install,
# checksum rsync, restore-on-interrupt); it exits 2 on a stage failure and
# 130/143 on INT/TERM, which propagates as this function's return code.
qml_env_stage() {
    uv run --locked --project "$_QMLENV_HARNESS" python -m latte_harness.qmlenv stage "$build" "$stage"
}
