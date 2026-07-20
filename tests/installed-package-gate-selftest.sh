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
source "$repo/scripts/lib-installed-package-gate.sh"
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
        "$qml/org/kde/latte/components/deep" \
        "$qml/org/kde/latte/private/containment" \
        "$qml/org/kde/latte/private/tasks" \
        "$plugins/kpackage/packagestructure" \
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
    : >"$qml/org/kde/latte/components/deep/Installed.qml"
    : >"$qml/org/kde/latte/private/containment/qmldir"
    : >"$qml/org/kde/latte/private/containment/liblattecontainmentplugin.so"
    : >"$qml/org/kde/latte/private/tasks/qmldir"
    : >"$qml/org/kde/latte/private/tasks/liblattetasksplugin.so"
    : >"$plugins/kpackage/packagestructure/latte_indicator.so"
    : >"$plugins/plasma/containmentactions/org.kde.latte.contextmenu.so"
    : >"$data/plasma/shells/org.kde.latte.shell/metadata.json"
    : >"$data/plasma/shells/org.kde.latte.shell/Installed.qml"
    : >"$data/plasma/plasmoids/org.kde.latte.containment/metadata.json"
    : >"$data/plasma/plasmoids/org.kde.latte.containment/Installed.qml"
    : >"$data/plasma/plasmoids/org.kde.latte.plasmoid/metadata.json"
    : >"$data/plasma/plasmoids/org.kde.latte.plasmoid/Installed.qml"
    : >"$data/applications/org.kde.latte-dock.desktop"
    : >"$data/latte/indicators/default/Installed.qml"
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

loader_probe="$({
    LD_AUDIT=/dev/null LD_PRELOAD=/dev/null \
        env "${latte_package_gate_loader_unset_args[@]}" \
        bash -c '
            shift
            for variable in "$@"; do
                [[ ! -v "$variable" ]] || exit 1
            done
            printf clean
        ' bash "${latte_package_gate_loader_variables[@]}"
} 2>/dev/null)"
[[ "$loader_probe" == clean ]] \
    || { echo "FAIL: dynamic-loader injection variables survived the launch scrub" >&2; exit 1; }
echo "PASS: dynamic-loader injection variables, including LD_AUDIT, are removed"

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

escaped_action_plugin="$work/escaped-action-plugin"
cp -a "$good" "$escaped_action_plugin"
outside_action_plugin="$work/preinstalled-system-contextmenu.so"
: >"$outside_action_plugin"
rm "$escaped_action_plugin/usr/lib/qt6/plugins/plasma/containmentactions/org.kde.latte.contextmenu.so"
ln -s "$outside_action_plugin" "$escaped_action_plugin/usr/lib/qt6/plugins/plasma/containmentactions/org.kde.latte.contextmenu.so"
expect_failure "containment-actions plugin symlink escape" "resolves outside the package prefix" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$escaped_action_plugin" --prefix /usr --check-only

escaped_qml_content="$work/escaped-qml-content"
cp -a "$good" "$escaped_qml_content"
outside_qml="$work/preinstalled-system-content.qml"
: >"$outside_qml"
rm "$escaped_qml_content/usr/lib/qt6/qml/org/kde/latte/components/deep/Installed.qml"
ln -s "$outside_qml" "$escaped_qml_content/usr/lib/qt6/qml/org/kde/latte/components/deep/Installed.qml"
expect_failure "nested QML content symlink escape" "Latte QML tree contains a symlink escaping the package prefix" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$escaped_qml_content" --prefix /usr --check-only

source_qml="$work/source-qml-content"
cp -a "$good" "$source_qml"
rm "$source_qml/usr/lib/qt6/qml/org/kde/latte/components/deep/Installed.qml"
ln -s "$repo/CMakeLists.txt" "$source_qml/usr/lib/qt6/qml/org/kde/latte/components/deep/Installed.qml"
expect_failure "nested QML source-tree provider" "Latte QML tree contains a symlink into the source/build tree" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$source_qml" --prefix /usr --check-only

build_provider="$work/external-build-provider"
mkdir -p "$build_provider"
: >"$build_provider/CMakeCache.txt"
: >"$build_provider/generated.qml"
build_qml="$work/build-qml-content"
cp -a "$good" "$build_qml"
rm "$build_qml/usr/lib/qt6/qml/org/kde/latte/components/deep/Installed.qml"
ln -s "$build_provider/generated.qml" "$build_qml/usr/lib/qt6/qml/org/kde/latte/components/deep/Installed.qml"
expect_failure "nested QML build-tree provider" "Latte QML tree contains a symlink into a CMake build tree" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$build_qml" --prefix /usr --check-only

stage_provider="$work/provider/_qmlstage"
mkdir -p "$stage_provider"
: >"$stage_provider/Staged.qml"
stage_qml="$work/stage-qml-content"
cp -a "$good" "$stage_qml"
rm "$stage_qml/usr/lib/qt6/qml/org/kde/latte/components/deep/Installed.qml"
ln -s "$stage_provider/Staged.qml" "$stage_qml/usr/lib/qt6/qml/org/kde/latte/components/deep/Installed.qml"
expect_failure "nested QML development-stage provider" "Latte QML tree contains a symlink into a development _qmlstage" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$stage_qml" --prefix /usr --check-only

nix_qml="$work/nix-qml-content"
cp -a "$good" "$nix_qml"
rm "$nix_qml/usr/lib/qt6/qml/org/kde/latte/components/deep/Installed.qml"
ln -s /nix/store/fake-latte-qml/Injected.qml "$nix_qml/usr/lib/qt6/qml/org/kde/latte/components/deep/Installed.qml"
expect_failure "nested QML Nix provider" "Latte QML tree contains a symlink into /nix/store" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$nix_qml" --prefix /usr --check-only

escaped_data_content="$work/escaped-data-content"
cp -a "$good" "$escaped_data_content"
outside_data="$work/preinstalled-system-indicator.qml"
: >"$outside_data"
rm "$escaped_data_content/usr/share/latte/indicators/default/Installed.qml"
ln -s "$outside_data" "$escaped_data_content/usr/share/latte/indicators/default/Installed.qml"
expect_failure "nested Latte data symlink escape" "Latte data tree contains a symlink escaping the package prefix" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$escaped_data_content" --prefix /usr --check-only

hostile_loader="$work/foreign loader"
mkdir -p "$hostile_loader"
elf_source="$work/elf-fixture.c"
printf 'int fixture(void) { return 0; }\nint main(void) { return fixture(); }\n' >"$elf_source"

rpath_binary="$work/rpath-binary"
cp -a "$good" "$rpath_binary"
NIX_LDFLAGS= NIX_LDFLAGS_BEFORE= \
    cc "$elf_source" -Wl,-rpath,"$hostile_loader" -o "$rpath_binary/usr/bin/latte-dock"
readelf -d "$rpath_binary/usr/bin/latte-dock" | grep -Fq "$hostile_loader" \
    || { echo "FAIL: hostile binary RUNPATH fixture has no requested entry" >&2; exit 1; }
expect_failure "binary ELF RUNPATH escape" "installed binary ELF RUNPATH/RPATH entry escapes the package prefix" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$rpath_binary" --prefix /usr --check-only

rpath_action="$work/rpath-action-plugin"
cp -a "$good" "$rpath_action"
NIX_LDFLAGS= NIX_LDFLAGS_BEFORE= \
    cc -shared -fPIC "$elf_source" -Wl,-rpath,"$hostile_loader" \
    -o "$rpath_action/usr/lib/qt6/plugins/plasma/containmentactions/org.kde.latte.contextmenu.so"
readelf -d "$rpath_action/usr/lib/qt6/plugins/plasma/containmentactions/org.kde.latte.contextmenu.so" \
    | grep -Fq "$hostile_loader" \
    || { echo "FAIL: hostile containment-actions RUNPATH fixture has no requested entry" >&2; exit 1; }
expect_failure "lazy containment-actions ELF RUNPATH escape" "Latte containment-actions plugin ELF RUNPATH/RPATH entry escapes the package prefix" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$rpath_action" --prefix /usr --check-only

mapped_artifact="$work/mapped artifact"
mapped_prefix="$mapped_artifact/usr"
mkdir -p "$mapped_prefix/bin" "$mapped_prefix/lib/qt6/qml/org/kde/latte/core"
mapped_binary="$mapped_prefix/bin/latte-dock"
mapped_core="$mapped_prefix/lib/qt6/qml/org/kde/latte/core/liblattecoreplugin.so"
: >"$mapped_binary"
: >"$mapped_core"
maps_with_spaces="$work/maps with spaces"
printf '1000-2000 r-xp 00000000 00:00 1 %s\n2000-3000 r--p 00000000 00:00 2 %s\n' \
    "$mapped_binary" "$mapped_core" >"$maps_with_spaces"
mapfile -t parsed_space_paths < <(latte_package_gate_read_mapped_paths "$maps_with_spaces")
[[ "${parsed_space_paths[0]}" == "$mapped_binary" && "${parsed_space_paths[1]}" == "$mapped_core" ]] \
    || { echo "FAIL: /proc maps parser split a pathname containing spaces" >&2; exit 1; }
declare -A expected_space_mappings=(
    [latte-dock]="$mapped_binary"
    [liblattecoreplugin.so]="$mapped_core"
)
required_space_mappings=(latte-dock liblattecoreplugin.so)
latte_package_gate_audit_mapped_paths "$maps_with_spaces" "$mapped_prefix" "$repo" \
    expected_space_mappings required_space_mappings >/dev/null
echo "PASS: /proc maps pathnames with spaces preserve exact installed-artifact identity"

foreign_mapped_root="$work/foreign mapped runtime"
mkdir -p "$foreign_mapped_root"
foreign_mapped_core="$foreign_mapped_root/liblattecoreplugin.so"
: >"$foreign_mapped_core"
hostile_maps="$work/hostile maps with spaces"
printf '1000-2000 r-xp 00000000 00:00 1 %s\n2000-3000 r--p 00000000 00:00 2 %s\n' \
    "$mapped_binary" "$foreign_mapped_core" >"$hostile_maps"
expect_failure "foreign mapped Latte runtime with spaces" "mapped Latte runtime escapes the package prefix" \
    latte_package_gate_audit_mapped_paths "$hostile_maps" "$mapped_prefix" "$repo" \
    expected_space_mappings required_space_mappings

nix_maps="$work/nix-provider.maps"
printf '1000-2000 r-xp 00000000 00:00 1 /nix/store/fake-latte-provider/lib/libQt6Core.so.6\n' >"$nix_maps"
empty_required_mappings=()
expect_failure "mapped Nix runtime provider" "running dock mapped a Nix artifact" \
    latte_package_gate_audit_mapped_paths "$nix_maps" "$mapped_prefix" "$repo" \
    expected_space_mappings empty_required_mappings

mapped_build_provider="$work/mapped-build-provider"
mkdir -p "$mapped_build_provider/lib"
: >"$mapped_build_provider/CMakeCache.txt"
: >"$mapped_build_provider/lib/libQt6Core.so.6"
build_maps="$work/build-provider.maps"
printf '1000-2000 r-xp 00000000 00:00 1 %s\n' \
    "$mapped_build_provider/lib/libQt6Core.so.6" >"$build_maps"
expect_failure "mapped CMake build-tree provider" "running dock mapped a CMake build tree artifact" \
    latte_package_gate_audit_mapped_paths "$build_maps" "$mapped_prefix" "$repo" \
    expected_space_mappings empty_required_mappings

incomplete="$work/incomplete"
cp -a "$good" "$incomplete"
rm "$incomplete/usr/lib/qt6/qml/org/kde/latte/private/tasks/liblattetasksplugin.so"
expect_failure "incomplete package" "missing tasks QML plugin" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$incomplete" --prefix /usr --check-only

echo "installed-package-gate-selftest: PASS (4 positive contracts, 24 rejection controls)"
