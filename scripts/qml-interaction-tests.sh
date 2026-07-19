#!/usr/bin/env bash
# Headless QML interaction harness (porting plan Phase 5, docs/reference/TESTING.md):
# drives real Latte QML components offscreen through qmltestrunner. Tests
# live in tests/qml/tst_*.qml and resolve Latte's modules through the staged
# install, so module registration and type resolution are part of what every
# test exercises.
#
# Runs inside the flake devShell (ctest invokes it there via build-check.sh).
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
source "$repo/scripts/lib-qml-env.sh"

qml_env_setup "$repo"

# reuse the compile gate's staging when it already ran this build; stage
# otherwise. The staged Latte tree lands under the distro's KDE_INSTALL_QMLDIR
# ($qmldir, set by qml_env_setup: lib/qml on nixpkgs, lib/qt6/qml on Arch),
# not a hardcoded lib/qml - else the guard always misses off-nix and re-stages.
if [[ ! -d "$stage/$qmldir/org/kde/latte" ]]; then
    qml_env_stage
fi

# optional argument selects a different test directory (used by the
# qmlcontracts ctest entry for tests/contracts)
inputdir="${1:-$repo/tests/qml}"

QT_QPA_PLATFORM=offscreen qmltestrunner "${imports[@]}" -input "$inputdir"
