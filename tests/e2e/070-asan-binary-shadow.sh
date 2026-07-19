#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# E2E: the dock under test is the build tree we think it is, not a shadow. Reads
# the vehicle dock's own /proc/<pid>/maps and asserts that the executable AND
# the containment plugin actually mapped into the running process resolve under
# $E2E_BUILD - never a packaged /nix/store latte-dock copy.
#
# Two reasons this guard exists:
#  1. The PR #23 shadow (caught 2026-07-18 via /proc/<dock>/maps): the desktop
#     session leaked NIXPKGS_QT6_QML_IMPORT_PATH carrying the SYSTEM-INSTALLED
#     packaged latte-dock, whose org.kde.latte.private.containment plugin then
#     shadowed the staged one - so every containment/plugin change "landed but
#     never ran". lib-qml-env.sh now strips that leaf; this recipe is the
#     standing guard that it stays stripped, in any nested run.
#  2. The sanitized gate (docs/tracking/ub-catching-plan.md A3): a driven ASan/UBSan gate
#     that ran a shadowed, NON-instrumented binary would catch no UB and pass
#     green - the worst kind of false confidence. When the caller sets
#     E2E_EXPECT_ASAN=1 (scripts/run-asan-dock.sh does), this recipe additionally
#     asserts libasan is mapped into the dock, proving the process really is the
#     sanitized build and not an uninstrumented shadow.
#
# The containment plugin is dlopened at view creation, so this waits for the
# dock to settle before reading the map - an empty map would be "not loaded yet",
# not "no shadow".
set -u

repo="${E2E_REPO:?run through scripts/run-e2e.sh}"
source "$repo/tests/e2e/lib.sh"

build="$(realpath -m "${E2E_BUILD:?E2E_BUILD unset}")"

e2e_wait_settled 45 || e2e_fail "vehicle dock never settled (containment plugin not loaded yet)"

pid="$(e2e_dock_pid)"
[[ -n "$pid" ]] || e2e_fail "no dock pid recorded"
kill -0 "$pid" 2>/dev/null || e2e_fail "dock (pid $pid) is not alive"
maps="/proc/$pid/maps"
[[ -r "$maps" ]] || e2e_fail "cannot read $maps"

# 1. the executable itself. run-staged.sh execs "$build/bin/latte-dock", so this
#    is guaranteed by construction - assert it anyway, cheaply, so a future
#    launcher change that reintroduces a PATH-resolved dock is caught here.
exe="$(realpath -m "/proc/$pid/exe")"
[[ "$exe" == "$build/"* ]] \
    || e2e_fail "dock exe resolves to $exe, not under the build tree under test ($build) - a shadow binary is running"
echo "  exe: $exe"

# 2. the containment plugin actually mapped into THIS process. A packaged copy
#    would map from /nix/store/*-latte-dock-*/...; the staged one maps from
#    $build/_qmlstage/... - both absolute, so a prefix check on the resolved
#    path separates them cleanly.
mapfile -t plugpaths < <(awk '/liblattecontainmentplugin\.so/ {print $NF}' "$maps" | sort -u)
[[ "${#plugpaths[@]}" -gt 0 ]] \
    || e2e_fail "liblattecontainmentplugin.so is not mapped into the dock (pid $pid) - the containment plugin never loaded, so the shadow check has nothing to verify"
for p in "${plugpaths[@]}"; do
    rp="$(realpath -m "$p")"
    [[ "$rp" == "$build/"* ]] \
        || e2e_fail "containment plugin mapped from $rp, not under the build tree under test ($build) - a shadow copy is running (PR #23 class: a leaked NIXPKGS_QT6_QML_IMPORT_PATH?)"
    echo "  containment plugin: $rp"
done

# 3. when the caller declares the dock should be sanitized, prove it: libasan
#    must be mapped. GCC's shared ASan runtime is pulled by the instrumented
#    executable and the dlopened plugins resolve __asan_* from it, so its
#    absence means the running binary is not the sanitized build - exactly the
#    silent no-op the driven UB gate must never accept.
if [[ "${E2E_EXPECT_ASAN:-0}" == 1 ]]; then
    grep -q 'libasan\.so' "$maps" \
        || e2e_fail "E2E_EXPECT_ASAN=1 but libasan is not mapped into the dock (pid $pid) - the sanitized binary was shadowed by an uninstrumented one; the UB gate would catch nothing"
    asan="$(awk '/libasan\.so/ {print $NF; exit}' "$maps")"
    echo "  sanitized: libasan mapped ($asan)"
else
    echo "  (E2E_EXPECT_ASAN unset: skipping the libasan check - normal non-sanitized run)"
fi

echo "PASS: asan-binary-shadow"
exit 0
