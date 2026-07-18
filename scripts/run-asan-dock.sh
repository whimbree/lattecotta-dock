#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Run the SANITIZED dock (build-asan, -DLATTE_SANITIZE=ON) in the NESTED
# VEHICLE only, never the real session (docs/ub-catching-plan.md, Prong A2).
# It is a thin front door onto scripts/run-e2e.sh: it points the vehicle at
# the sanitized build tree (BUILD=build-asan) and sets ASan/UBSan to abort on
# the first finding with a stack, so OUR-code UB in the driven paths fails the
# run loudly instead of passing silently.
#
#   scripts/run-asan-dock.sh                 # bring the sanitized dock up in
#                                            # the vehicle and run every recipe
#   scripts/run-asan-dock.sh 000-smoke       # a single recipe (bring-up smoke)
#
# The sanitizer output goes to the vehicle dock log on stderr (NOT a file):
# run-e2e.sh redirects the dock's stderr into $E2E_BUILD/_e2e-artifacts and
# tails it on failure, so an abort shows up right there. The LIVE counterpart
# for Bree's manual desk testing (real session, real ~/.config, log to a file)
# is scripts/start-dock-sanitized.sh - this script is the desk-independent one.
#
# Build the tree first if it is missing:
#   nix develop -c cmake -S . -B build-asan -G Ninja \
#       -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLATTE_SANITIZE=ON -DBUILD_TESTING=OFF
#   nix develop -c cmake --build build-asan
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
: "${ASAN_BUILD:=$repo/build-asan}"

if [[ ! -x "$ASAN_BUILD/bin/latte-dock" ]]; then
    echo "run-asan-dock: no sanitized binary at $ASAN_BUILD/bin/latte-dock" >&2
    echo "run-asan-dock: build it with -DLATTE_SANITIZE=ON (see this script's header)" >&2
    exit 2
fi

# Nested tuning: halt+abort with a stack on the first finding; leak detection
# OFF (Qt/Plasma leak at exit by design - LSan noise is not what we are hunting
# and would fail the vehicle's clean-shutdown check). Output stays on stderr so
# it lands in the vehicle dock log; the live launcher is the one that files to
# a log_path. Suppressions start empty (scripts/asan/*.supp).
common="halt_on_error=1:abort_on_error=1:print_stacktrace=1:detect_leaks=0"
export ASAN_OPTIONS="${common}:suppressions=$repo/scripts/asan/asan.supp${ASAN_OPTIONS:+:$ASAN_OPTIONS}"
export UBSAN_OPTIONS="${common}:suppressions=$repo/scripts/asan/ubsan.supp${UBSAN_OPTIONS:+:$UBSAN_OPTIONS}"
export BUILD="$ASAN_BUILD"

# Tell the shadow-assertion recipe (tests/e2e/070-asan-binary-shadow.sh) that the
# dock under test MUST be the sanitized build: it then asserts libasan is mapped
# into the running process, so a shadow that swapped in an uninstrumented binary
# (which would silently catch no UB) fails the run instead of passing green.
export E2E_EXPECT_ASAN=1

echo "run-asan-dock: sanitized dock from $ASAN_BUILD in the nested vehicle"
echo "run-asan-dock: ASAN_OPTIONS=$ASAN_OPTIONS"
echo "run-asan-dock: UBSAN_OPTIONS=$UBSAN_OPTIONS"
exec "$repo/scripts/run-e2e.sh" "$@"
