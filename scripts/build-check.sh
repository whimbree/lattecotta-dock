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

case "$(command -v cmake || true)" in
    /nix/store/*) ;;
    *) exec nix develop "$repo" -c "$0" "$@";;
esac

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

ctest --test-dir "$repo/build" --output-on-failure

# Structural coverage ratchet (docs/tracking/QML_EXTRACTION_PLAN.md section D):
# unit-header/test pairing plus the committed ctest entry-list baseline.
"$repo/tests/coverage/coverage-ratchet.sh" "$repo/build"

echo "build-check: OK"
