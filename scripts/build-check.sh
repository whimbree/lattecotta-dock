#!/usr/bin/env bash
# One-command build check (porting plan Phase 0). Run it before pushing and
# after any CMake or source change:
#
#   ./scripts/build-check.sh          # incremental
#   ./scripts/build-check.sh --fresh  # wipe build dirs first
#
# Re-execs itself inside the flake devShell unless the PINNED toolchain is
# already in PATH, so it works from a bare shell too. "cmake exists" is not
# that test: this host's system profile ships its own cmake (4.1, no ninja),
# and a mere presence check once let that one reconfigure both build dirs
# with the wrong toolchain - the fresh variant then died on "CMake was
# unable to find Ninja". Same pinned-closure check qmllint-gate uses: the
# devshell's cmake resolves under /nix/store/*, the system profile's under
# /run/current-system.
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"

# The ratchet leg is a uv shim since BP-1c (the coverage-ratchet port),
# so the pinned-toolchain test covers uv as well as cmake - the same
# stale-proxy lesson gate-all.sh learned when the harness leg landed: a
# pre-BP shell carries a store cmake but no uv.
for tool in cmake uv; do
    case "$(command -v "$tool" || true)" in
        /nix/store/*) ;;
        *) exec nix develop "$repo" -c "$0" "$@";;
    esac
done

fresh=0
[[ "${1:-}" == "--fresh" ]] && fresh=1

check() {
    local dir="$repo/$1"
    shift
    [[ "$fresh" == 1 ]] && rm -rf "$dir"
    cmake -S "$repo" -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo "$@"
    cmake --build "$dir"
}

# One tree: the port is Wayland-only (the WITH_X11 option and its second
# build variant were removed with the X11 backend, 2026-07-17).
check build

# Hermetic ctest env (D271, 2026-08-04): the ambient session exports
# QML2_IMPORT_PATH for the staged dev loop, and that list ends in
# build/_qmlstage - with the stage populated, QML-engine tests then
# resolve org.kde.latte.* from disk on top of their own C++
# registrations (themeawareicontest's namespace collision). nix develop
# passes ambient vars through, so the re-exec above does not sanitize.
# The import-path doctrine is explicit lists only; lib-qml-env strips
# these same vars for the QML gates, and ctest gets the same discipline.
#
# Part two (D277, 2026-08-05): the nixpkgs Qt6 runtime patch also reads
# NIXPKGS_QT6_QML_IMPORT_PATH/NIXPKGS_QML_SEARCH_PATHS independently of
# QML2_IMPORT_PATH. With the packaged latte-dock installed in the system
# profile those vars hand every in-process QML engine the package's
# org.kde.latte.* on-disk modules - the same namespace collision, D8's
# shadow in ctest form. Per the D8 doctrine only the packaged leaf is
# stripped (the vars also carry KDE framework modules tests resolve), via
# the same qmlenv helper the QML gates eval.
# The assignment-then-eval split keeps set -e watching the uv exit code; a
# bare eval "$(...)" would swallow a substitution failure into a no-op.
seed_env="$(uv run --locked --project "$repo/harness" python -m latte_harness.qmlenv seed-env)"
eval "$seed_env"
env -u QML2_IMPORT_PATH -u QML_IMPORT_PATH \
    ctest --test-dir "$repo/build" --output-on-failure

# Structural coverage ratchet (docs/tracking/QML_EXTRACTION_PLAN.md section D):
# unit-header/test pairing plus the committed ctest entry-list baseline.
"$repo/tests/coverage/coverage-ratchet.sh" "$repo/build"

echo "build-check: OK"
