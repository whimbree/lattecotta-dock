#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Install the completed build as a package payload, then execute the real
# installed-package nested-runtime gate. This acceptance is intentionally
# separate from the fast --check-only adversarial controls.
# The install-then-assert idea was informed by latte-dock-ng's
# docker/verify-install.sh at 9c12a79aaf9350e73059da5b293c931218419c05
# (github.com/ruizhi-lab/latte-dock-ng); the nested runtime is original.
set -euo pipefail

script_dir="${BASH_SOURCE[0]%/*}"
[[ "$script_dir" != "${BASH_SOURCE[0]}" ]] || script_dir=.
repo="$(cd "$script_dir/.." && pwd -P)"
build="${BUILD:-$repo/build}"

for required_command in cmake mkdir mktemp rm; do
    command -v "$required_command" >/dev/null 2>&1 || {
        echo "installed-package-gate-runtime-test: FAIL: required command '$required_command' is missing" >&2
        exit 2
    }
done
[[ -f "$build/CMakeCache.txt" ]] \
    || { echo "installed-package-gate-runtime-test: FAIL: build is not configured at $build" >&2; exit 2; }
: "${LATTE_QML_MODULE_PATH:?installed runtime acceptance needs the distro QML allow-list}"

work="$(mktemp -d /tmp/latte-installed-runtime-test.XXXXXX)"
cleanup() {
    rm -rf "$work"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
package_root="$work/root"
mkdir -p "$package_root"

echo "installed-package-gate-runtime-test: installing package payload"
DESTDIR="$package_root" cmake --install "$build" --prefix /usr
manifest="$build/install_manifest.txt"
[[ -s "$manifest" ]] \
    || { echo "installed-package-gate-runtime-test: FAIL: cmake produced no install manifest" >&2; exit 2; }

LATTE_RUNTIME_DATA_PATH="${LATTE_RUNTIME_DATA_PATH:-/usr/local/share:/usr/share}" \
    "$repo/scripts/installed-package-gate.sh" \
    --root "$package_root" --prefix /usr --manifest "$manifest"

echo "installed-package-gate-runtime-test: PASS (package install and nested runtime)"
