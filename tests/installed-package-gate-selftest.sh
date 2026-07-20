#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Fast provenance checks for scripts/installed-package-gate.sh. The fixture is
# intentionally not runnable: --check-only verifies package discovery and the
# rejection boundaries without requiring a compositor or Qt runtime.
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
gate="$repo/scripts/installed-package-gate.sh"
work="$(mktemp -d /tmp/latte-installed-gate-selftest.XXXXXX)"
trap 'rm -rf "$work"' EXIT

make_package() {
    local root="$1"
    local qml="$root/usr/lib/qt6/qml"
    local plugins="$root/usr/lib/qt6/plugins"
    local data="$root/usr/share"
    mkdir -p \
        "$root/usr/bin" \
        "$qml/org/kde/latte/core" \
        "$qml/org/kde/latte/private/containment" \
        "$qml/org/kde/latte/private/tasks" \
        "$plugins/plasma/containmentactions" \
        "$data/plasma/shells/org.kde.latte.shell" \
        "$data/plasma/plasmoids/org.kde.latte.containment" \
        "$data/plasma/plasmoids/org.kde.latte.plasmoid" \
        "$data/applications" \
        "$data/latte/indicators/default"
    printf '#!/usr/bin/env bash\nexit 0\n' >"$root/usr/bin/latte-dock"
    chmod +x "$root/usr/bin/latte-dock"
    : >"$qml/org/kde/latte/core/qmldir"
    : >"$qml/org/kde/latte/core/liblattecoreplugin.so"
    : >"$qml/org/kde/latte/private/containment/qmldir"
    : >"$qml/org/kde/latte/private/containment/liblattecontainmentplugin.so"
    : >"$qml/org/kde/latte/private/tasks/qmldir"
    : >"$qml/org/kde/latte/private/tasks/liblattetasksplugin.so"
    : >"$plugins/plasma/containmentactions/org.kde.latte.contextmenu.so"
    : >"$data/plasma/shells/org.kde.latte.shell/metadata.json"
    : >"$data/plasma/plasmoids/org.kde.latte.containment/metadata.json"
    : >"$data/plasma/plasmoids/org.kde.latte.plasmoid/metadata.json"
    : >"$data/applications/org.kde.latte-dock.desktop"
}

run_check() {
    local root="$1"
    shift
    env \
        LATTE_QML_MODULE_PATH="$framework" \
        LATTE_RUNTIME_DATA_PATH="$runtime_data" \
        "$@" \
        bash "$gate" --root "$root" --prefix /usr --check-only
}

expect_failure() {
    local name="$1" needle="$2"
    shift 2
    local output rc
    set +e
    output="$("$@" 2>&1)"
    rc=$?
    set -e
    if [[ "$rc" -eq 0 ]]; then
        echo "FAIL: $name was accepted" >&2
        exit 1
    fi
    if [[ "$output" != *"$needle"* ]]; then
        echo "FAIL: $name produced no actionable '$needle' diagnostic:" >&2
        echo "$output" >&2
        exit 1
    fi
    echo "PASS: rejected $name"
}

framework="$work/framework-qml"
runtime_data="$work/framework-data"
mkdir -p "$framework" "$runtime_data"

good="$work/good"
make_package "$good"
hostile_qml="$work/hostile-qml"
hostile_data="$work/hostile-data"
hostile_plugins="$work/hostile-plugins"
mkdir -p \
    "$hostile_qml/org/kde/latte/core" \
    "$hostile_data/plasma/shells/org.kde.latte.shell" \
    "$hostile_plugins"
positive="$(
    QML2_IMPORT_PATH="$hostile_qml" \
    QML_IMPORT_PATH="$hostile_qml" \
    NIXPKGS_QT6_QML_IMPORT_PATH="$hostile_qml" \
    NIXPKGS_QML_SEARCH_PATHS="$hostile_qml" \
    QT_PLUGIN_PATH="$hostile_plugins" \
    XDG_DATA_DIRS="$hostile_data" \
    run_check "$good"
)"
[[ "$positive" == *"installed-package-gate: CHECK OK"* ]] \
    || { echo "FAIL: valid package root was rejected" >&2; echo "$positive" >&2; exit 1; }
[[ "$positive" != *"hostile-qml"* && "$positive" != *"hostile-data"* \
        && "$positive" != *"hostile-plugins"* ]] \
    || { echo "FAIL: ambient QML paths leaked into the validated allow-list" >&2; echo "$positive" >&2; exit 1; }
echo "PASS: explicit package root accepted and hostile ambient paths ignored"

missing_binary="$work/missing-binary"
cp -a "$good" "$missing_binary"
rm "$missing_binary/usr/bin/latte-dock"
fake_path="$work/fake-path"
mkdir -p "$fake_path"
printf '#!/usr/bin/env bash\ntouch %q\n' "$work/path-fallback-ran" >"$fake_path/latte-dock"
chmod +x "$fake_path/latte-dock"
expect_failure "PATH fallback" "PATH fallback is forbidden" \
    env PATH="$fake_path:$PATH" LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$missing_binary" --prefix /usr --check-only
[[ ! -e "$work/path-fallback-ran" ]] \
    || { echo "FAIL: fallback latte-dock from PATH was executed" >&2; exit 1; }

expect_failure "source tree root" "inside the source/build tree" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$repo" --prefix /usr --check-only

source_tree="$work/source-tree"
cp -a "$good" "$source_tree"
: >"$source_tree/CMakeLists.txt"
expect_failure "marked source tree root" "package root is a source tree" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$source_tree" --prefix /usr --check-only

build_tree="$work/build-tree"
cp -a "$good" "$build_tree"
: >"$build_tree/CMakeCache.txt"
expect_failure "marked build tree root" "package root is a CMake build tree" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$build_tree" --prefix /usr --check-only

development_stage="$work/_qmlstage"
cp -a "$good" "$development_stage"
expect_failure "development QML stage" "development _qmlstage" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$development_stage" --prefix /usr --check-only

expect_failure "Nix package root" "points into /nix/store" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root /nix/store/fake-latte-dock-0.10.77 --prefix /usr --check-only

expect_failure "Nix QML fallback" "points into /nix/store" \
    env LATTE_QML_MODULE_PATH=/nix/store/fake-framework-qml LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$good" --prefix /usr --check-only

escaped="$work/escaped-binary"
cp -a "$good" "$escaped"
outside_binary="$work/preinstalled-system-latte-dock"
printf '#!/usr/bin/env bash\nexit 0\n' >"$outside_binary"
chmod +x "$outside_binary"
rm "$escaped/usr/bin/latte-dock"
ln -s "$outside_binary" "$escaped/usr/bin/latte-dock"
expect_failure "binary symlink escape" "resolves outside the package prefix" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$escaped" --prefix /usr --check-only

shadow_qml="$work/preinstalled-qml"
mkdir -p "$shadow_qml/org/kde/latte/core"
: >"$shadow_qml/org/kde/latte/core/qmldir"
expect_failure "preinstalled Latte QML shadow" "contains a foreign org/kde/latte tree" \
    env LATTE_QML_MODULE_PATH="$shadow_qml" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$good" --prefix /usr --check-only

shadow_data="$work/preinstalled-data"
mkdir -p "$shadow_data/plasma/shells/org.kde.latte.shell"
expect_failure "preinstalled Latte data shadow" "contains foreign Latte data" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$shadow_data" \
    bash "$gate" --root "$good" --prefix /usr --check-only

escaped_plugin="$work/escaped-plugin"
cp -a "$good" "$escaped_plugin"
outside_plugin="$work/preinstalled-system-liblattetasksplugin.so"
: >"$outside_plugin"
rm "$escaped_plugin/usr/lib/qt6/qml/org/kde/latte/private/tasks/liblattetasksplugin.so"
ln -s "$outside_plugin" "$escaped_plugin/usr/lib/qt6/qml/org/kde/latte/private/tasks/liblattetasksplugin.so"
expect_failure "QML plugin symlink escape" "resolves outside the package prefix" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$escaped_plugin" --prefix /usr --check-only

incomplete="$work/incomplete"
cp -a "$good" "$incomplete"
rm "$incomplete/usr/lib/qt6/qml/org/kde/latte/private/tasks/liblattetasksplugin.so"
expect_failure "incomplete package" "missing tasks QML plugin" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$incomplete" --prefix /usr --check-only

echo "installed-package-gate-selftest: PASS (1 valid contract, 12 rejection controls)"
