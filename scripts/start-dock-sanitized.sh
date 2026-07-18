#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# LIVE sanitized-dock launcher for MANUAL PRE-RELEASE testing on the REAL
# Wayland session against the REAL ~/.config (docs/ub-catching-plan.md, Prong
# A2). This runs the build-asan dock (ASan+UBSan, forced Q_ASSERTs) as your
# actual desktop dock so real, in-hand usage - the best UB surface there is -
# aborts LOUDLY with a stack the instant it hits our-code UB or a tripped
# invariant, instead of misbehaving silently the way the field insert(-1) did.
#
#   scripts/start-dock-sanitized.sh          # sanitized dock, your real config
#   scripts/start-dock-sanitized.sh -d       # + debug logging on stdout
#
# It is the daily-driver start-dock.sh with two differences: it runs the
# build-asan tree (BUILD=build-asan) and it sets ASan/UBSan for a long-running
# GUI. It delegates the safe kill+detach to restart-staged.sh, so any existing
# dock (including a SIGSTOPped orphan) is taken down by the sanctioned TERM ->
# CONT -> KILL sequence, never a bare pkill.
#
# WHAT TO EXPECT
#   - The dock comes up like normal and you drive it normally.
#   - On the FIRST undefined behavior or failed Q_ASSERT in our code, the dock
#     ABORTS (it vanishes) and writes a symbolized report to a log file. That
#     is the tool working, not a crash to shrug off: capture the log.
#   - Leak detection is OFF on purpose (Qt/Plasma leak at exit by design);
#     you are hunting UB and asserts, not leaks.
#
# WHERE THE ABORT LOG IS
#   $HOME/.cache/latte-dock-asan/{asan,ubsan}-<timestamp>.<pid>
#   (override the dir with LATTE_ASAN_LOGDIR). The path is printed at launch.
#   Read the file top-down: the first "runtime error" / "ERROR: AddressSanitizer"
#   line names the fault, the frames under it are the stack - the top OUR-code
#   frame (app/, containment/plugin/, declarativeimports/) is the site to fix.
#
# GETTING BACK TO THE NORMAL DOCK
#   scripts/start-dock.sh                     # your normal RelWithDebInfo dock
#
# Build the sanitized tree first if it is missing:
#   nix develop -c cmake -S . -B build-asan -G Ninja \
#       -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLATTE_SANITIZE=ON -DBUILD_TESTING=OFF
#   nix develop -c cmake --build build-asan
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
: "${ASAN_BUILD:=$repo/build-asan}"

if [[ ! -x "$ASAN_BUILD/bin/latte-dock" ]]; then
    echo "start-dock-sanitized: no sanitized binary at $ASAN_BUILD/bin/latte-dock" >&2
    echo "start-dock-sanitized: build it with -DLATTE_SANITIZE=ON (see this script's header)" >&2
    exit 2
fi

logdir="${LATTE_ASAN_LOGDIR:-$HOME/.cache/latte-dock-asan}"
mkdir -p "$logdir"
ts="$(date +%Y%m%d-%H%M%S)"

# Long-running-GUI tuning:
#   halt_on_error / abort_on_error / print_stacktrace  -> abort loudly with a stack
#   detect_leaks=0                                      -> LSan OFF (see header)
#   log_path=<file>                                     -> the report survives the
#                                                          detached dock's vanish
#   suppressions=<file>                                 -> library-noise escape hatch
#                                                          (starts empty; add as needed)
# ASan writes to "<log_path>.<pid>"; UBSan likewise.
common="halt_on_error=1:abort_on_error=1:print_stacktrace=1:detect_leaks=0"
export ASAN_OPTIONS="${common}:log_path=$logdir/asan-$ts:suppressions=$repo/scripts/asan/asan.supp${ASAN_OPTIONS:+:$ASAN_OPTIONS}"
export UBSAN_OPTIONS="${common}:log_path=$logdir/ubsan-$ts:suppressions=$repo/scripts/asan/ubsan.supp${UBSAN_OPTIONS:+:$UBSAN_OPTIONS}"
export BUILD="$ASAN_BUILD"

echo "start-dock-sanitized: SANITIZED dock (build-asan) against your REAL ~/.config"
echo "start-dock-sanitized: any ASan/UBSan abort -> $logdir/{asan,ubsan}-$ts.<pid>"
echo "start-dock-sanitized: back to the normal dock afterwards: scripts/start-dock.sh"
exec "$repo/scripts/restart-staged.sh" --user-config "$@"
