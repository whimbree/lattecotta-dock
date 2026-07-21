#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

for required_command in chmod git grep mkdir mktemp rm tar; do
    command -v "$required_command" >/dev/null 2>&1 || {
        printf "void-package-staging-selftest: FAIL: required command '%s' is missing\n" \
            "$required_command" >&2
        exit 1
    }
done

script_dir="${BASH_SOURCE[0]%/*}"
[[ "$script_dir" != "${BASH_SOURCE[0]}" ]] || script_dir=.
repo="$(cd "$script_dir/.." && pwd -P)"
helper="$repo/packaging/void/build-package"
work="$(mktemp -d /tmp/latte-void-staging-selftest.XXXXXX)"
trap 'rm -rf "$work"' EXIT

void_packages="$work/void-packages"
mkdir -p "$void_packages/srcpkgs"
git init -q "$void_packages"
printf '%s\n' '#!/bin/sh' 'exit 99' >"$void_packages/xbps-src"
chmod +x "$void_packages/xbps-src"

output="$("$helper" --stage-only "$void_packages")"
expected_commit="$(git -C "$repo" rev-parse HEAD)"
destination="$void_packages/srcpkgs/latte-dock"
archive="$void_packages/hostdir/sources/latte-dock-0.10.77/$expected_commit.tar.gz"

[[ "$output" == *"source_commit=$expected_commit"* ]]
grep -Fxq "_commit=$expected_commit" "$destination/template"
[[ ! -e "$destination/patches" ]] || {
    echo "void-package-staging-selftest: FAIL: current-HEAD recipe staged obsolete patches" >&2
    exit 1
}

metadata="$(tar -xOf "$archive" \
    "lattecotta-dock-$expected_commit/app/org.kde.latte-dock.appdata.xml.cmake")"
[[ "$metadata" == *'<component type="desktop-application">'* \
        && "$metadata" == *'<id>org.kde.latte-dock</id>'* \
        && "$metadata" != *'<extends>'* \
        && "$metadata" != *'liblatte2plugin.so'* ]] || {
    echo "void-package-staging-selftest: FAIL: archived current HEAD lacks corrected AppStream identity" >&2
    exit 1
}

echo "void-package-staging-selftest: PASS"
