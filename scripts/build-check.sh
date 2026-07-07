#!/usr/bin/env bash
# One-command build check (porting plan Phase 0). Run it before pushing and
# after any CMake or source change:
#
#   ./scripts/build-check.sh          # incremental
#   ./scripts/build-check.sh --fresh  # wipe build dirs first
#
# Re-execs itself inside the flake devShell when the toolchain isn't already
# in PATH, so it works from a bare shell too.
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"

if ! command -v cmake >/dev/null 2>&1; then
    exec nix develop "$repo" -c "$0" "$@"
fi

fresh=0
[[ "${1:-}" == "--fresh" ]] && fresh=1

check() {
    local dir="$repo/$1"
    shift
    [[ "$fresh" == 1 ]] && rm -rf "$dir"
    cmake -S "$repo" -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo "$@"
    cmake --build "$dir"
}

# Single configuration until Phase 4 turns HAVE_X11 into a real option();
# from then on this must build both variants, since the author only runs
# Wayland and an unbuilt #ifdef path rots silently:
#
#   check build          -DHAVE_X11=ON
#   check build-no-x11   -DHAVE_X11=OFF
check build

echo "build-check: OK"
