#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Fast provenance checks for scripts/installed-package-gate.sh. The fixture is
# intentionally not runnable: --check-only verifies package discovery and the
# rejection boundaries without requiring a compositor or Qt runtime.
set -euo pipefail

for required_command in \
        awk bash cc chmod cp env grep ld ln mkdir mktemp mv pgrep readelf realpath rm setsid sleep sort; do
    command -v "$required_command" >/dev/null 2>&1 || {
        printf "installed-package-gate-selftest: FAIL: required command '%s' is missing\n" \
            "$required_command" >&2
        exit 1
    }
done
unset required_command

script_dir="${BASH_SOURCE[0]%/*}"
[[ "$script_dir" != "${BASH_SOURCE[0]}" ]] || script_dir=.
repo="$(cd "$script_dir/.." && pwd -P)"
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
    cp "$fixture_binary" "$root/usr/bin/latte-dock"
    : >"$qml/org/kde/latte/core/qmldir"
    cp "$fixture_plugin" "$qml/org/kde/latte/core/liblattecoreplugin.so"
    : >"$qml/org/kde/latte/components/deep/Installed.qml"
    : >"$qml/org/kde/latte/private/containment/qmldir"
    cp "$fixture_plugin" "$qml/org/kde/latte/private/containment/liblattecontainmentplugin.so"
    : >"$qml/org/kde/latte/private/tasks/qmldir"
    cp "$fixture_plugin" "$qml/org/kde/latte/private/tasks/liblattetasksplugin.so"
    cp "$fixture_plugin" "$plugins/kpackage/packagestructure/latte_indicator.so"
    cp "$fixture_plugin" "$plugins/plasma/containmentactions/org.kde.latte.contextmenu.so"
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

missing_awk_path="$work/missing-awk-path"
mkdir -p "$missing_awk_path"
expect_failure "missing awk parser" "required validation command 'awk' is missing" \
    env PATH="$missing_awk_path" LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    "$(command -v bash)" "$gate" --root "$work/not-used" --prefix /usr --check-only

elf_source="$work/elf-fixture.c"
fixture_binary="$work/fixture-binary"
fixture_plugin="$work/fixture-plugin.so"
fixture_object="$work/elf-fixture.o"
printf 'int fixture(void) { return 0; }\nint main(void) { return fixture(); }\n' >"$elf_source"
printf '#!/usr/bin/env bash\nexit 0\n' >"$fixture_binary"
chmod +x "$fixture_binary"
cc -fPIC -c "$elf_source" -o "$fixture_object"
NIX_LDFLAGS= NIX_LDFLAGS_BEFORE= ld -shared "$fixture_object" -o "$fixture_plugin"
readelf -h "$fixture_plugin" >/dev/null 2>&1 \
    || { echo "FAIL: positive package plugin ELF fixture is invalid" >&2; exit 1; }

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

invalid_plugin_specs=(
    "core QML|usr/lib/qt6/qml/org/kde/latte/core/liblattecoreplugin.so"
    "containment QML|usr/lib/qt6/qml/org/kde/latte/private/containment/liblattecontainmentplugin.so"
    "tasks QML|usr/lib/qt6/qml/org/kde/latte/private/tasks/liblattetasksplugin.so"
    "indicator package-structure|usr/lib/qt6/plugins/kpackage/packagestructure/latte_indicator.so"
    "containment-actions|usr/lib/qt6/plugins/plasma/containmentactions/org.kde.latte.contextmenu.so"
)
for invalid_plugin_spec in "${invalid_plugin_specs[@]}"; do
    invalid_plugin_label="${invalid_plugin_spec%%|*}"
    invalid_plugin_path="${invalid_plugin_spec#*|}"
    invalid_plugin_root="$work/invalid-${invalid_plugin_label// /-}-plugin"
    cp -a "$good" "$invalid_plugin_root"
    printf 'not an ELF artifact\n' >"$invalid_plugin_root/$invalid_plugin_path"
    expect_failure "invalid $invalid_plugin_label plugin ELF" "is not a valid ELF artifact" \
        env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
        bash "$gate" --root "$invalid_plugin_root" --prefix /usr --check-only
done

unloadable_action="$work/unloadable-action-plugin"
cp -a "$good" "$unloadable_action"
unloadable_source="$work/unloadable-plugin.c"
unloadable_object="$work/unloadable-plugin.o"
printf 'extern void missing_latte_gate_symbol(void);\nvoid fixture(void) { missing_latte_gate_symbol(); }\n' >"$unloadable_source"
cc -fPIC -c "$unloadable_source" -o "$unloadable_object"
NIX_LDFLAGS= NIX_LDFLAGS_BEFORE= ld -shared "$unloadable_object" -o \
    "$unloadable_action/usr/lib/qt6/plugins/plasma/containmentactions/org.kde.latte.contextmenu.so"
[[ "$(readelf -Ws "$unloadable_action/usr/lib/qt6/plugins/plasma/containmentactions/org.kde.latte.contextmenu.so")" \
        == *missing_latte_gate_symbol* ]] \
    || { echo "FAIL: unloadable containment-actions fixture has no unresolved symbol" >&2; exit 1; }
expect_failure "unloadable containment-actions plugin" "Latte containment-actions plugin cannot be loaded" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$unloadable_action" --prefix /usr --check-only

escaped_qml_content="$work/escaped-qml-content"
cp -a "$good" "$escaped_qml_content"
outside_qml="$work/preinstalled-system-content.qml"
: >"$outside_qml"
rm "$escaped_qml_content/usr/lib/qt6/qml/org/kde/latte/components/deep/Installed.qml"
ln -s "$outside_qml" "$escaped_qml_content/usr/lib/qt6/qml/org/kde/latte/components/deep/Installed.qml"
expect_failure "nested QML content symlink escape" "Latte QML tree contains a symlink escaping its installed runtime tree" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$escaped_qml_content" --prefix /usr --check-only

escaped_qml_directory="$work/escaped-qml-directory"
cp -a "$good" "$escaped_qml_directory"
outside_qml_directory="$work/preinstalled-system-qml-directory"
mkdir -p "$outside_qml_directory"
: >"$outside_qml_directory/Injected.qml"
rm -rf "$escaped_qml_directory/usr/lib/qt6/qml/org/kde/latte/components/deep"
ln -s "$outside_qml_directory" "$escaped_qml_directory/usr/lib/qt6/qml/org/kde/latte/components/deep"
expect_failure "nested external QML directory symlink" "Latte QML tree contains a symlink escaping its installed runtime tree" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$escaped_qml_directory" --prefix /usr --check-only

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
expect_failure "nested Latte data symlink escape" "Latte data tree contains a symlink escaping its installed runtime tree" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$escaped_data_content" --prefix /usr --check-only

cross_tree_qml="$work/cross-tree-qml-content"
cp -a "$good" "$cross_tree_qml"
internal_unowned="$cross_tree_qml/usr/share/internal-provider"
mkdir -p "$internal_unowned"
: >"$internal_unowned/Injected.qml"
rm "$cross_tree_qml/usr/lib/qt6/qml/org/kde/latte/components/deep/Installed.qml"
ln -s "$internal_unowned/Injected.qml" \
    "$cross_tree_qml/usr/lib/qt6/qml/org/kde/latte/components/deep/Installed.qml"
expect_failure "in-prefix cross-tree QML symlink" "Latte QML tree contains a symlink escaping its installed runtime tree" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$cross_tree_qml" --prefix /usr --check-only

root_symlink_data="$work/root-symlink-data"
cp -a "$good" "$root_symlink_data"
mv "$root_symlink_data/usr/share/latte" "$root_symlink_data/usr/share/latte-real"
ln -s latte-real "$root_symlink_data/usr/share/latte"
expect_failure "Latte runtime-tree root symlink" "Latte data tree root must not be a symlink" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$root_symlink_data" --prefix /usr --check-only

hostile_loader="$work/foreign loader"
mkdir -p "$hostile_loader"

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
NIX_LDFLAGS= NIX_LDFLAGS_BEFORE= ld -shared "$fixture_object" -rpath "$hostile_loader" \
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

status_cleanup_log="$work/status-cleanup.log"
set +e
bash -c '
    source "$1"
    cleanup_log="$2"
    cleanup_probe() { printf "cleanup\n" >>"$cleanup_log"; return 91; }
    latte_package_gate_install_exit_cleanup cleanup_probe
    exit 37
' bash "$repo/scripts/lib-installed-package-gate.sh" "$status_cleanup_log"
status_cleanup_rc=$?
set -e
[[ "$status_cleanup_rc" -eq 37 && "$(<"$status_cleanup_log")" == cleanup ]] \
    || { echo "FAIL: EXIT cleanup did not preserve status 37 or run exactly once" >&2; exit 1; }
echo "PASS: EXIT cleanup preserves an existing failure status"

for signal_case in INT:130 TERM:143; do
    signal="${signal_case%%:*}"
    expected_signal_status="${signal_case##*:}"
    signal_log="$work/signal-$signal.log"
    set +e
    bash -c '
        source "$1"
        cleanup_log="$2"
        cleanup_probe() { printf "cleanup\n" >>"$cleanup_log"; }
        latte_package_gate_install_exit_cleanup cleanup_probe
        kill -s "$3" "$$"
        printf "PASS\n" >>"$2"
    ' bash "$repo/scripts/lib-installed-package-gate.sh" "$signal_log" "$signal"
    signal_status=$?
    set -e
    [[ "$signal_status" -eq "$expected_signal_status" ]] \
        || { echo "FAIL: $signal exited $signal_status instead of $expected_signal_status" >&2; exit 1; }
    [[ "$(<"$signal_log")" == cleanup ]] \
        || { echo "FAIL: $signal continued after cleanup or skipped cleanup" >&2; exit 1; }
    echo "PASS: $signal terminates through cleanup without reaching PASS"
done

term_ignored_ready="$work/term-ignored.ready"
bash -c '
    trap "" TERM
    : >"$1"
    while :; do sleep 1; done
' bash "$term_ignored_ready" &
term_ignored_pid=$!
for ((ready_wait = 0; ready_wait < 50; ready_wait++)); do
    [[ -e "$term_ignored_ready" ]] && break
    sleep 0.01
done
[[ -e "$term_ignored_ready" ]] \
    || { kill -KILL "$term_ignored_pid" 2>/dev/null || true; echo "FAIL: TERM-ignoring fixture did not start" >&2; exit 1; }
latte_package_gate_stop_process "$term_ignored_pid" "TERM-ignoring fixture" 1 0.01 50 0.01
kill -0 "$term_ignored_pid" 2>/dev/null \
    && { echo "FAIL: cleanup left the TERM-ignoring fixture alive" >&2; exit 1; }
echo "PASS: cleanup escalates a TERM-ignoring process to SIGKILL"

bounded_wait_log="$work/unbounded-wait-called"
set +e
bounded_wait_output="$(
    (
        source "$repo/scripts/lib-installed-package-gate.sh"
        kill() { return 0; }
        sleep() { :; }
        wait() { : >"$bounded_wait_log"; }
        latte_package_gate_stop_process 424242 "unkillable fixture" 1 0 1 0
    ) 2>&1
)"
bounded_wait_status=$?
set -e
[[ "$bounded_wait_status" -eq 2 && "$bounded_wait_output" == *"still exists after bounded SIGKILL wait"* ]] \
    || { echo "FAIL: simulated unkillable cleanup was not bounded" >&2; exit 1; }
[[ ! -e "$bounded_wait_log" ]] \
    || { echo "FAIL: cleanup called unbounded wait while the process still existed" >&2; exit 1; }
echo "PASS: post-SIGKILL cleanup returns without an unbounded wait"

group_ignored_ready="$work/group-term-ignored.ready"
setsid bash -c '
    trap "" TERM
    : >"$1"
    while :; do sleep 1; done
' bash "$group_ignored_ready" &
group_ignored_pid=$!
for ((ready_wait = 0; ready_wait < 50; ready_wait++)); do
    [[ -e "$group_ignored_ready" ]] && break
    sleep 0.01
done
[[ -e "$group_ignored_ready" ]] \
    || { kill -KILL -- "-$group_ignored_pid" 2>/dev/null || true; echo "FAIL: process-group fixture did not start" >&2; exit 1; }
latte_package_gate_stop_process_group "$group_ignored_pid" "TERM-ignoring process group" 1 0.01 50 0.01
latte_package_gate_process_group_exists "$group_ignored_pid" \
    && { echo "FAIL: cleanup left the TERM-ignoring process group alive" >&2; exit 1; }
echo "PASS: nested-style process-group cleanup escalates within fixed bounds"

bounded_group_wait_log="$work/unbounded-group-wait-called"
set +e
bounded_group_output="$(
    (
        source "$repo/scripts/lib-installed-package-gate.sh"
        pgrep() { return 0; }
        kill() { return 0; }
        sleep() { :; }
        wait() { : >"$bounded_group_wait_log"; }
        latte_package_gate_stop_process_group 424242 "unkillable process group" 1 0 1 0
    ) 2>&1
)"
bounded_group_status=$?
set -e
[[ "$bounded_group_status" -eq 2 && "$bounded_group_output" == *"still exists after bounded SIGKILL wait"* ]] \
    || { echo "FAIL: simulated unkillable process-group cleanup was not bounded" >&2; exit 1; }
[[ ! -e "$bounded_group_wait_log" ]] \
    || { echo "FAIL: process-group cleanup called unbounded wait while members still existed" >&2; exit 1; }
echo "PASS: nested-style post-SIGKILL cleanup never waits on a live group"

clean_term_ready="$work/clean-term.ready"
bash -c '
    trap "exit 0" TERM
    : >"$1"
    while :; do sleep 1; done
' bash "$clean_term_ready" &
clean_term_pid=$!
for ((ready_wait = 0; ready_wait < 50; ready_wait++)); do
    [[ -e "$clean_term_ready" ]] && break
    sleep 0.01
done
[[ -e "$clean_term_ready" ]] \
    || { kill -KILL "$clean_term_pid" 2>/dev/null || true; echo "FAIL: clean-SIGTERM fixture did not start" >&2; exit 1; }
kill -TERM "$clean_term_pid"
latte_package_gate_wait_until_process_exits "$clean_term_pid" 200 0.01 \
    || { echo "FAIL: clean-SIGTERM fixture did not exit" >&2; exit 1; }
latte_package_gate_require_exit_status "$clean_term_pid" 0 "clean-SIGTERM fixture"
echo "PASS: expected zero SIGTERM wait status is preserved"

aborted_status_log="$work/aborted-status.log"
bash -c 'ulimit -c 0; kill -ABRT "$$"' &
aborted_pid=$!
latte_package_gate_wait_until_process_exits "$aborted_pid" 200 0.01 \
    || { kill -KILL "$aborted_pid" 2>/dev/null || true; echo "FAIL: SIGABRT fixture did not exit" >&2; exit 1; }
set +e
latte_package_gate_require_exit_status "$aborted_pid" 0 "SIGABRT fixture" 2>"$aborted_status_log"
aborted_status_rc=$?
set -e
[[ "$aborted_status_rc" -eq 2 && "$(<"$aborted_status_log")" == *"status 134, expected 0"* ]] \
    || { echo "FAIL: prompt SIGABRT was not rejected by wait status" >&2; exit 1; }
echo "PASS: prompt SIGABRT exit status is rejected"

nonzero_term_ready="$work/nonzero-term.ready"
bash -c '
    trap "exit 7" TERM
    : >"$1"
    while :; do sleep 1; done
' bash "$nonzero_term_ready" &
nonzero_term_pid=$!
for ((ready_wait = 0; ready_wait < 50; ready_wait++)); do
    [[ -e "$nonzero_term_ready" ]] && break
    sleep 0.01
done
[[ -e "$nonzero_term_ready" ]] \
    || { kill -KILL "$nonzero_term_pid" 2>/dev/null || true; echo "FAIL: nonzero-SIGTERM fixture did not start" >&2; exit 1; }
kill -TERM "$nonzero_term_pid"
latte_package_gate_wait_until_process_exits "$nonzero_term_pid" 200 0.01 \
    || { kill -KILL "$nonzero_term_pid" 2>/dev/null || true; echo "FAIL: nonzero-SIGTERM fixture did not exit" >&2; exit 1; }
nonzero_status_log="$work/nonzero-status.log"
set +e
latte_package_gate_require_exit_status "$nonzero_term_pid" 0 "nonzero-SIGTERM fixture" 2>"$nonzero_status_log"
nonzero_status_rc=$?
set -e
[[ "$nonzero_status_rc" -eq 2 && "$(<"$nonzero_status_log")" == *"status 7, expected 0"* ]] \
    || { echo "FAIL: nonzero SIGTERM wait status was not rejected" >&2; exit 1; }
echo "PASS: nonzero SIGTERM wait status is rejected"

incomplete="$work/incomplete"
cp -a "$good" "$incomplete"
rm "$incomplete/usr/lib/qt6/qml/org/kde/latte/private/tasks/liblattetasksplugin.so"
expect_failure "incomplete package" "missing tasks QML plugin" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$incomplete" --prefix /usr --check-only

echo "installed-package-gate-selftest: PASS (48 focused controls)"
