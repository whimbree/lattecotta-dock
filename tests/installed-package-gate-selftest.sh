#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Fast provenance checks for scripts/installed-package-gate.sh. The fixture is
# an ELF executable but not a dock runtime: --check-only verifies package
# discovery and the rejection boundaries without requiring a compositor.
set -euo pipefail

for required_command in \
        awk bash c++ cc chmod cp dirname env find ld ln mkdir mktemp mv patchelf pgrep \
        pkg-config readelf realpath rm setsid sleep; do
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
    cp "$fixture_core_plugin" "$qml/org/kde/latte/core/liblattecoreplugin.so"
    cp -L "$fixture_qt_core" "$fixture_libstdcxx" "$fixture_libgcc" \
        "$qml/org/kde/latte/core/"
    : >"$qml/org/kde/latte/components/deep/Installed.qml"
    : >"$qml/org/kde/latte/private/containment/qmldir"
    cp "$fixture_containment_plugin" "$qml/org/kde/latte/private/containment/liblattecontainmentplugin.so"
    cp -L "$fixture_qt_core" "$fixture_libstdcxx" "$fixture_libgcc" \
        "$qml/org/kde/latte/private/containment/"
    : >"$qml/org/kde/latte/private/tasks/qmldir"
    cp "$fixture_tasks_plugin" "$qml/org/kde/latte/private/tasks/liblattetasksplugin.so"
    cp -L "$fixture_qt_core" "$fixture_libstdcxx" "$fixture_libgcc" \
        "$qml/org/kde/latte/private/tasks/"
    cp "$fixture_indicator_plugin" "$plugins/kpackage/packagestructure/latte_indicator.so"
    cp -L "$fixture_qt_core" "$fixture_libstdcxx" "$fixture_libgcc" \
        "$plugins/kpackage/packagestructure/"
    cp "$fixture_action_plugin" "$plugins/plasma/containmentactions/org.kde.latte.contextmenu.so"
    cp -L "$fixture_qt_core" "$fixture_libstdcxx" "$fixture_libgcc" \
        "$plugins/plasma/containmentactions/"
    : >"$data/plasma/shells/org.kde.latte.shell/metadata.json"
    : >"$data/plasma/shells/org.kde.latte.shell/Installed.qml"
    : >"$data/plasma/plasmoids/org.kde.latte.containment/metadata.json"
    : >"$data/plasma/plasmoids/org.kde.latte.containment/Installed.qml"
    : >"$data/plasma/plasmoids/org.kde.latte.plasmoid/metadata.json"
    : >"$data/plasma/plasmoids/org.kde.latte.plasmoid/Installed.qml"
    : >"$data/applications/org.kde.latte-dock.desktop"
    : >"$data/latte/indicators/default/Installed.qml"
}

build_qt_plugin() {
    local output="$1" class_name="$2" iid="$3" metadata_file="${4:-}" constructor="${5:-}"
    local source_file="$work/$class_name.cpp" moc_file="$work/$class_name.moc"
    local qt_cflags qt_libs
    {
        printf '#include <QObject>\n'
        [[ -z "$constructor" ]] || printf '%s\n' "$constructor"
        printf 'class %s : public QObject {\n' "$class_name"
        printf '    Q_OBJECT\n'
        if [[ -n "$metadata_file" ]]; then
            printf '    Q_PLUGIN_METADATA(IID "%s" FILE "%s")\n' "$iid" "$metadata_file"
        else
            printf '    Q_PLUGIN_METADATA(IID "%s")\n' "$iid"
        fi
        printf '};\n#include "%s"\n' "${moc_file##*/}"
    } >"$source_file"
    "$moc" "$source_file" -o "$moc_file"
    qt_cflags="$(pkg-config --cflags Qt6Core)" \
        || { echo "FAIL: Qt6Core compiler flags are unavailable" >&2; exit 1; }
    qt_libs="$(pkg-config --libs Qt6Core)" \
        || { echo "FAIL: Qt6Core linker flags are unavailable" >&2; exit 1; }
    # shellcheck disable=SC2086 -- pkg-config emits shell-ready flag words.
    c++ -std=c++20 -fPIC -shared "$source_file" -o "$output" $qt_cflags $qt_libs
    patchelf --set-rpath '$ORIGIN' "$output"
}

write_package_manifest() {
    local root="$1" output="$2" omit="${3:-}" namespace_root="${4:-$1}" manifest_scan
    manifest_scan="$work/manifest-scan"
    if ! find "$root" \( -type f -o -type l \) -print >"$manifest_scan"; then
        echo "FAIL: fixture manifest scan failed" >&2
        exit 1
    fi
    : >"$output"
    while IFS= read -r installed_path; do
        [[ "$installed_path" == "$omit" ]] && continue
        if [[ "$namespace_root" == / ]]; then
            printf '%s\n' "$installed_path" >>"$output"
        else
            printf '%s\n' "${installed_path#"$namespace_root"}" >>"$output"
        fi
    done <"$manifest_scan"
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

missing_env_path="$work/missing-env-path"
mkdir -p "$missing_env_path"
for available_validation_command in \
        awk cat dirname find jq mktemp perl readelf readlink realpath rm timeout tr; do
    ln -s "$(command -v "$available_validation_command")" \
        "$missing_env_path/$available_validation_command"
done
expect_failure "missing env launcher" "required validation command 'env' is missing" \
    env PATH="$missing_env_path" LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    "$(command -v bash)" "$gate" --root "$work/not-used" --prefix /usr --check-only

qt_libexec="$(pkg-config --variable=libexecdir Qt6Core)" \
    || { echo "FAIL: Qt6Core host-tool directory is unavailable" >&2; exit 1; }
moc="$qt_libexec/moc"
[[ -x "$moc" ]] || { echo "FAIL: Qt6 moc is unavailable at $moc" >&2; exit 1; }
qt_libdir="$(pkg-config --variable=libdir Qt6Core)" \
    || { echo "FAIL: Qt6Core library directory is unavailable" >&2; exit 1; }
fixture_qt_core="$qt_libdir/libQt6Core.so.6"
fixture_libstdcxx="$(c++ -print-file-name=libstdc++.so.6)" \
    || { echo "FAIL: libstdc++ fixture dependency cannot be resolved" >&2; exit 1; }
fixture_libgcc="$(c++ -print-file-name=libgcc_s.so.1)" \
    || { echo "FAIL: libgcc fixture dependency cannot be resolved" >&2; exit 1; }
for fixture_dependency in "$fixture_qt_core" "$fixture_libstdcxx" "$fixture_libgcc"; do
    [[ -f "$fixture_dependency" ]] \
        || { echo "FAIL: fixture dependency is unavailable: $fixture_dependency" >&2; exit 1; }
done

elf_source="$work/elf-fixture.c"
fixture_binary="$work/fixture-binary"
fixture_plugin="$work/fixture-generic-library.so"
fixture_object="$work/elf-fixture.o"
printf '%s\n' \
    'int fixture(void) { return 0; }' \
    'int main(void) { return fixture(); }' >"$elf_source"
cc -fPIC -c "$elf_source" -o "$fixture_object"
NIX_LDFLAGS= NIX_LDFLAGS_BEFORE= ld --build-id -e main "$fixture_object" -o "$fixture_binary"
NIX_LDFLAGS= NIX_LDFLAGS_BEFORE= ld --build-id -shared "$fixture_object" -o "$fixture_plugin"
readelf -h "$fixture_plugin" >/dev/null 2>&1 \
    || { echo "FAIL: generic shared-library fixture is invalid" >&2; exit 1; }

action_metadata="$work/action-metadata.json"
indicator_metadata="$work/indicator-metadata.json"
wrong_action_metadata="$work/wrong-action-metadata.json"
wrong_indicator_metadata="$work/wrong-indicator-metadata.json"
printf '%s\n' \
    '{"KPlugin":{"Id":"org.kde.latte.contextmenu","ServiceTypes":["Plasma/ContainmentActions"]}}' \
    >"$action_metadata"
printf '%s\n' \
    '{"KPackageStructure":"Latte/Indicator","X-KDE-ParentApp":"org.kde.latte-dock"}' \
    >"$indicator_metadata"
printf '%s\n' \
    '{"KPlugin":{"Id":"org.kde.latte.contextmenu","ServiceTypes":["Plasma/Applet"]}}' \
    >"$wrong_action_metadata"
printf '%s\n' \
    '{"KPackageStructure":"Plasma/Applet","X-KDE-ParentApp":"org.kde.latte-dock"}' \
    >"$wrong_indicator_metadata"
fixture_core_plugin="$work/liblattecoreplugin.so"
fixture_containment_plugin="$work/liblattecontainmentplugin.so"
fixture_tasks_plugin="$work/liblattetasksplugin.so"
fixture_action_plugin="$work/org.kde.latte.contextmenu.so"
fixture_indicator_plugin="$work/latte_indicator.so"
build_qt_plugin "$fixture_core_plugin" LatteCorePlugin \
    org.qt-project.Qt.QQmlExtensionInterface
build_qt_plugin "$fixture_containment_plugin" LatteContainmentPlugin \
    org.qt-project.Qt.QQmlExtensionInterface
build_qt_plugin "$fixture_tasks_plugin" LatteTasksPlugin \
    org.qt-project.Qt.QQmlExtensionInterface
build_qt_plugin "$fixture_action_plugin" MenuFactory org.kde.KPluginFactory \
    "$action_metadata"
build_qt_plugin "$fixture_indicator_plugin" latte_packagestructure_indicator_factory \
    org.kde.KPluginFactory "$indicator_metadata"

good="$work/good"
make_package "$good"

real_qt_plugin_info=""
latte_package_gate_find_qt6_plugin_info real_qt_plugin_info \
    || { echo "FAIL: no real Qt 6 qtplugininfo is available for fixtures" >&2; exit 1; }
qt_selector_bin="$work/qt-selector-bin"
qt6_selector_log="$work/qt6-selector.log"
qt5_selector_log="$work/qt5-selector.log"
mkdir -p "$qt_selector_bin"
printf '#!/usr/bin/env bash\n: >%q\nexec %q "$@"\n' \
    "$qt6_selector_log" "$real_qt_plugin_info" >"$qt_selector_bin/qtplugininfo6"
printf '#!/usr/bin/env bash\n: >%q\nprintf "qplugininfo 5.15.2\\n"\n' \
    "$qt5_selector_log" >"$qt_selector_bin/qtplugininfo"
chmod +x "$qt_selector_bin/qtplugininfo6" "$qt_selector_bin/qtplugininfo"
qt_selector_output="$(run_check "$good" PATH="$qt_selector_bin:$PATH")"
[[ "$qt_selector_output" == *"installed-package-gate: CHECK OK"* \
        && -e "$qt6_selector_log" && ! -e "$qt5_selector_log" ]] \
    || { echo "FAIL: Qt 6-specific qtplugininfo was not preferred over the unsuffixed tool" >&2; echo "$qt_selector_output" >&2; exit 1; }
echo "PASS: Qt 6-specific qtplugininfo takes precedence over an unsuffixed Qt 5 tool"

fake_qt5_plugin_info="$work/qtplugininfo5"
printf '#!/usr/bin/env bash\nprintf "qplugininfo 5.15.2\\n"\n' >"$fake_qt5_plugin_info"
chmod +x "$fake_qt5_plugin_info"
if latte_package_gate_choose_qt6_plugin_info rejected_qt_plugin_info "$fake_qt5_plugin_info"; then
    echo "FAIL: Qt 5 qtplugininfo passed Qt 6 version validation" >&2
    exit 1
fi
echo "PASS: Qt 5-only qtplugininfo is rejected by version"

expect_failure "live root without ownership manifest" \
    "--manifest is required with --root /" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root / --prefix "$good/usr" --check-only

live_manifest="$work/live-root.manifest"
stale_tasks_plugin="$good/usr/lib/qt6/qml/org/kde/latte/private/tasks/liblattetasksplugin.so"
write_package_manifest "$good" "$live_manifest" "$stale_tasks_plugin" /
expect_failure "same-prefix stale tasks plugin omitted by the package under test" \
    "omitted by the package manifest" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root / --prefix "$good/usr" --manifest "$live_manifest" --check-only
echo "PASS: --root / rejects a same-prefix stale artifact without package ownership"
write_package_manifest "$good" "$live_manifest" "" /
live_positive="$(
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
        bash "$gate" --root / --prefix "$good/usr" --manifest "$live_manifest" --check-only
)"
[[ "$live_positive" == *"installed-package-gate: CHECK OK"* ]] \
    || { echo "FAIL: complete live-root package manifest was rejected" >&2; echo "$live_positive" >&2; exit 1; }
echo "PASS: --root / accepts artifacts owned by the explicit package manifest"

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

absolute_internal="$work/absolute-internal-link"
cp -a "$good" "$absolute_internal"
absolute_internal_dir="$absolute_internal/usr/lib/qt6/qml/org/kde/latte/components/deep"
mv "$absolute_internal_dir/Installed.qml" "$absolute_internal_dir/AbsoluteTarget.qml"
ln -s /usr/lib/qt6/qml/org/kde/latte/components/deep/AbsoluteTarget.qml \
    "$absolute_internal_dir/Installed.qml"
absolute_internal_output="$(run_check "$absolute_internal")"
[[ "$absolute_internal_output" == *"installed-package-gate: CHECK OK"* ]] \
    || { echo "FAIL: isolated package rejected an in-tree absolute namespace symlink" >&2; echo "$absolute_internal_output" >&2; exit 1; }
echo "PASS: isolated roots resolve absolute symlinks inside the package namespace"

selected_absolute="$work/selected-absolute-links"
cp -a "$good" "$selected_absolute"
selected_binary_target="$selected_absolute/usr/libexec/latte-dock.real"
mkdir -p "${selected_binary_target%/*}"
mv "$selected_absolute/usr/bin/latte-dock" "$selected_binary_target"
ln -s /usr/libexec/latte-dock.real "$selected_absolute/usr/bin/latte-dock"
selected_core_dir="$selected_absolute/usr/lib/qt6/qml/org/kde/latte/core"
mv "$selected_core_dir/liblattecoreplugin.so" \
    "$selected_core_dir/liblattecoreplugin.real.so"
ln -s /usr/lib/qt6/qml/org/kde/latte/core/liblattecoreplugin.real.so \
    "$selected_core_dir/liblattecoreplugin.so"
selected_shell_dir="$selected_absolute/usr/share/plasma/shells/org.kde.latte.shell"
mv "$selected_shell_dir/metadata.json" "$selected_shell_dir/metadata.real.json"
ln -s /usr/share/plasma/shells/org.kde.latte.shell/metadata.real.json \
    "$selected_shell_dir/metadata.json"
selected_absolute_output="$(run_check "$selected_absolute")"
[[ "$selected_absolute_output" == *"installed-package-gate: CHECK OK"* \
        && "$selected_absolute_output" == *"installed-package-gate: binary: $selected_binary_target"* ]] \
    || { echo "FAIL: package-internal absolute selected-artifact links were rejected or kept unresolved" >&2; echo "$selected_absolute_output" >&2; exit 1; }
echo "PASS: selected executable, plugin, and metadata links resolve inside the package namespace"

selected_cross_tree="$work/selected-cross-tree-plugin"
cp -a "$good" "$selected_cross_tree"
selected_cross_tree_target="$selected_cross_tree/usr/share/liblattecoreplugin.so"
cp "$selected_cross_tree/usr/lib/qt6/qml/org/kde/latte/core/liblattecoreplugin.so" \
    "$selected_cross_tree_target"
rm "$selected_cross_tree/usr/lib/qt6/qml/org/kde/latte/core/liblattecoreplugin.so"
ln -s /usr/share/liblattecoreplugin.so \
    "$selected_cross_tree/usr/lib/qt6/qml/org/kde/latte/core/liblattecoreplugin.so"
expect_failure "selected plugin absolute cross-tree link" \
    "installed core QML plugin resolves outside its allowed package tree" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$selected_cross_tree" --prefix /usr --check-only

live_selected="$work/live-selected-link"
cp -a "$good" "$live_selected"
live_selected_dir="$live_selected/usr/lib/qt6/qml/org/kde/latte/private/tasks"
mv "$live_selected_dir/liblattetasksplugin.so" \
    "$live_selected_dir/liblattetasksplugin.real.so"
ln -s "$live_selected_dir/liblattetasksplugin.real.so" \
    "$live_selected_dir/liblattetasksplugin.so"
live_selected_manifest="$work/live-selected.manifest"
write_package_manifest "$live_selected" "$live_selected_manifest" \
    "$live_selected_dir/liblattetasksplugin.real.so" /
expect_failure "selected plugin target omitted by the live package manifest" \
    "resolved installed tasks QML plugin is present under the package prefix but omitted by the package manifest" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root / --prefix "$live_selected/usr" \
    --manifest "$live_selected_manifest" --check-only

live_absolute="$work/live-absolute-link"
cp -a "$good" "$live_absolute"
live_absolute_dir="$live_absolute/usr/lib/qt6/qml/org/kde/latte/components/deep"
mv "$live_absolute_dir/Installed.qml" "$live_absolute_dir/AbsoluteTarget.qml"
ln -s "$live_absolute_dir/AbsoluteTarget.qml" "$live_absolute_dir/Installed.qml"
live_absolute_manifest="$work/live-absolute.manifest"
write_package_manifest "$live_absolute" "$live_absolute_manifest" "" /
live_absolute_output="$(
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
        bash "$gate" --root / --prefix "$live_absolute/usr" \
        --manifest "$live_absolute_manifest" --check-only
)"
[[ "$live_absolute_output" == *"installed-package-gate: CHECK OK"* ]] \
    || { echo "FAIL: live root rejected an in-tree host-absolute symlink" >&2; echo "$live_absolute_output" >&2; exit 1; }
echo "PASS: live roots retain host-absolute symlink semantics"

partial_find_bin="$work/partial-find-bin"
mkdir -p "$partial_find_bin"
printf '#!/usr/bin/env bash\nprintf "%%s\\0" %q\nexit 73\n' \
    "$good/usr/lib/qt6/qml/org/kde/latte/core/qmldir" >"$partial_find_bin/find"
chmod +x "$partial_find_bin/find"
expect_failure "partial find producer" "scan failed before a complete result was available" \
    env PATH="$partial_find_bin:$PATH" LATTE_QML_MODULE_PATH="$framework" \
    LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$good" --prefix /usr --check-only

real_readelf="$(command -v readelf)"
partial_readelf_bin="$work/partial-readelf-bin"
mkdir -p "$partial_readelf_bin"
printf '#!/usr/bin/env bash\nif [[ "$1" == -d ]]; then\n  %q "$@"\n  exit 73\nfi\nexec %q "$@"\n' \
    "$real_readelf" "$real_readelf" >"$partial_readelf_bin/readelf"
chmod +x "$partial_readelf_bin/readelf"
expect_failure "partial readelf producer" "ELF search metadata could not be read completely" \
    env PATH="$partial_readelf_bin:$PATH" LATTE_QML_MODULE_PATH="$framework" \
    LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$good" --prefix /usr --check-only
echo "PASS: partial find and readelf producers cannot yield validation success"

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

non_elf_binary="$work/non-elf-binary"
cp -a "$good" "$non_elf_binary"
printf '#!/usr/bin/env bash\nexit 0\n' >"$non_elf_binary/usr/bin/latte-dock"
chmod +x "$non_elf_binary/usr/bin/latte-dock"
expect_failure "non-ELF executable wrapper" "installed binary is not a valid ELF artifact" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$non_elf_binary" --prefix /usr --check-only

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
escaped_binary_link="$escaped/usr/bin/latte-dock"
ln -s "$(realpath --relative-to="$(dirname "$escaped_binary_link")" "$outside_binary")" \
    "$escaped_binary_link"
expect_failure "binary symlink escape" "installed binary escapes the package root through a symlink" \
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
escaped_plugin_link="$escaped_plugin/usr/lib/qt6/qml/org/kde/latte/private/tasks/liblattetasksplugin.so"
ln -s "$(realpath --relative-to="$(dirname "$escaped_plugin_link")" "$outside_plugin")" \
    "$escaped_plugin_link"
expect_failure "QML plugin symlink escape" "installed tasks QML plugin escapes the package root through a symlink" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$escaped_plugin" --prefix /usr --check-only

escaped_action_plugin="$work/escaped-action-plugin"
cp -a "$good" "$escaped_action_plugin"
outside_action_plugin="$work/preinstalled-system-contextmenu.so"
: >"$outside_action_plugin"
rm "$escaped_action_plugin/usr/lib/qt6/plugins/plasma/containmentactions/org.kde.latte.contextmenu.so"
escaped_action_link="$escaped_action_plugin/usr/lib/qt6/plugins/plasma/containmentactions/org.kde.latte.contextmenu.so"
ln -s "$(realpath --relative-to="$(dirname "$escaped_action_link")" "$outside_action_plugin")" \
    "$escaped_action_link"
expect_failure "containment-actions plugin symlink escape" \
    "installed Latte containment-actions plugin escapes the package root through a symlink" \
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

generic_action="$work/generic-action-plugin"
cp -a "$good" "$generic_action"
cp "$fixture_plugin" \
    "$generic_action/usr/lib/qt6/plugins/plasma/containmentactions/org.kde.latte.contextmenu.so"
expect_failure "generic shared library in a plugin slot" "has no valid Qt plugin metadata" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$generic_action" --prefix /usr --check-only

wrong_iid_core="$work/wrong-iid-core-plugin"
cp -a "$good" "$wrong_iid_core"
build_qt_plugin \
    "$wrong_iid_core/usr/lib/qt6/qml/org/kde/latte/core/liblattecoreplugin.so" \
    LatteCorePlugin org.kde.KPluginFactory
expect_failure "valid core plugin with the wrong IID" \
    "Latte core QML plugin has IID 'org.kde.KPluginFactory', expected 'org.qt-project.Qt.QQmlExtensionInterface'" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$wrong_iid_core" --prefix /usr --check-only

wrong_class_action="$work/wrong-class-action-plugin"
cp -a "$good" "$wrong_class_action"
build_qt_plugin \
    "$wrong_class_action/usr/lib/qt6/plugins/plasma/containmentactions/org.kde.latte.contextmenu.so" \
    WrongMenuFactory org.kde.KPluginFactory "$action_metadata"
expect_failure "valid containment-actions plugin with the wrong class" \
    "Latte containment-actions plugin has class 'WrongMenuFactory', expected 'MenuFactory'" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$wrong_class_action" --prefix /usr --check-only

wrong_category_action="$work/wrong-category-action-plugin"
cp -a "$good" "$wrong_category_action"
build_qt_plugin \
    "$wrong_category_action/usr/lib/qt6/plugins/plasma/containmentactions/org.kde.latte.contextmenu.so" \
    MenuFactory org.kde.KPluginFactory "$wrong_action_metadata"
expect_failure "valid containment-actions plugin with the wrong category" \
    "metadata does not declare the org.kde.latte.contextmenu Plasma/ContainmentActions type" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$wrong_category_action" --prefix /usr --check-only

wrong_category_indicator="$work/wrong-category-indicator-plugin"
cp -a "$good" "$wrong_category_indicator"
build_qt_plugin \
    "$wrong_category_indicator/usr/lib/qt6/plugins/kpackage/packagestructure/latte_indicator.so" \
    latte_packagestructure_indicator_factory org.kde.KPluginFactory "$wrong_indicator_metadata"
expect_failure "valid indicator plugin with the wrong package category" \
    "metadata does not declare the Latte/Indicator package-structure type for org.kde.latte-dock" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$wrong_category_indicator" --prefix /usr --check-only

unloadable_action="$work/unloadable-action-plugin"
cp -a "$good" "$unloadable_action"
unloadable_action_path="$unloadable_action/usr/lib/qt6/plugins/plasma/containmentactions/org.kde.latte.contextmenu.so"
build_qt_plugin "$unloadable_action_path" MenuFactory org.kde.KPluginFactory \
    "$action_metadata" \
    'extern "C" void missing_latte_gate_symbol(); __attribute__((constructor)) static void require_missing_symbol() { missing_latte_gate_symbol(); }'
[[ "$(readelf -Ws "$unloadable_action_path")" \
        == *missing_latte_gate_symbol* ]] \
    || { echo "FAIL: unloadable containment-actions fixture has no unresolved symbol" >&2; exit 1; }
expect_failure "unloadable containment-actions plugin" "Latte containment-actions plugin cannot be loaded" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$unloadable_action" --prefix /usr --check-only

hanging_core="$work/hanging-core-plugin"
cp -a "$good" "$hanging_core"
hanging_core_path="$hanging_core/usr/lib/qt6/qml/org/kde/latte/core/liblattecoreplugin.so"
build_qt_plugin "$hanging_core_path" LatteCorePlugin \
    org.qt-project.Qt.QQmlExtensionInterface "" \
    '#include <csignal>
#include <unistd.h>
__attribute__((constructor)) static void hang_in_constructor() { std::signal(SIGTERM, SIG_IGN); while (true) pause(); }'
expect_failure "hanging plugin constructor" "loader timed out" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$hanging_core" --prefix /usr --check-only
echo "PASS: plugin slots require expected Qt types and plugin loading is bounded"

escaped_qml_content="$work/escaped-qml-content"
cp -a "$good" "$escaped_qml_content"
outside_qml="$work/preinstalled-system-content.qml"
: >"$outside_qml"
rm "$escaped_qml_content/usr/lib/qt6/qml/org/kde/latte/components/deep/Installed.qml"
escaped_qml_link="$escaped_qml_content/usr/lib/qt6/qml/org/kde/latte/components/deep/Installed.qml"
ln -s "$(realpath --relative-to="$(dirname "$escaped_qml_link")" "$outside_qml")" "$escaped_qml_link"
expect_failure "nested QML content symlink escape" "Latte QML tree contains a symlink target escaping the package root" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$escaped_qml_content" --prefix /usr --check-only

escaped_qml_directory="$work/escaped-qml-directory"
cp -a "$good" "$escaped_qml_directory"
outside_qml_directory="$work/preinstalled-system-qml-directory"
mkdir -p "$outside_qml_directory"
: >"$outside_qml_directory/Injected.qml"
rm -rf "$escaped_qml_directory/usr/lib/qt6/qml/org/kde/latte/components/deep"
escaped_qml_directory_link="$escaped_qml_directory/usr/lib/qt6/qml/org/kde/latte/components/deep"
ln -s "$(realpath --relative-to="$(dirname "$escaped_qml_directory_link")" "$outside_qml_directory")" \
    "$escaped_qml_directory_link"
expect_failure "nested external QML directory symlink" "Latte QML tree contains a symlink target escaping the package root" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$escaped_qml_directory" --prefix /usr --check-only

source_qml="$work/source-qml-content"
cp -a "$good" "$source_qml"
source_provider="$source_qml/usr/share/source-provider"
mkdir -p "$source_provider"
: >"$source_provider/CMakeLists.txt"
: >"$source_provider/Generated.qml"
rm "$source_qml/usr/lib/qt6/qml/org/kde/latte/components/deep/Installed.qml"
ln -s /usr/share/source-provider/Generated.qml \
    "$source_qml/usr/lib/qt6/qml/org/kde/latte/components/deep/Installed.qml"
expect_failure "nested QML source-tree provider" "Latte QML tree contains a symlink into a source tree" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$source_qml" --prefix /usr --check-only

build_qml="$work/build-qml-content"
cp -a "$good" "$build_qml"
build_provider="$build_qml/usr/share/external-build-provider"
mkdir -p "$build_provider"
: >"$build_provider/CMakeCache.txt"
: >"$build_provider/generated.qml"
rm "$build_qml/usr/lib/qt6/qml/org/kde/latte/components/deep/Installed.qml"
ln -s /usr/share/external-build-provider/generated.qml \
    "$build_qml/usr/lib/qt6/qml/org/kde/latte/components/deep/Installed.qml"
expect_failure "nested QML build-tree provider" "Latte QML tree contains a symlink into a CMake build tree" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$build_qml" --prefix /usr --check-only

stage_qml="$work/stage-qml-content"
cp -a "$good" "$stage_qml"
stage_provider="$stage_qml/usr/share/provider/_qmlstage"
mkdir -p "$stage_provider"
: >"$stage_provider/Staged.qml"
rm "$stage_qml/usr/lib/qt6/qml/org/kde/latte/components/deep/Installed.qml"
ln -s /usr/share/provider/_qmlstage/Staged.qml \
    "$stage_qml/usr/lib/qt6/qml/org/kde/latte/components/deep/Installed.qml"
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
escaped_data_link="$escaped_data_content/usr/share/latte/indicators/default/Installed.qml"
ln -s "$(realpath --relative-to="$(dirname "$escaped_data_link")" "$outside_data")" "$escaped_data_link"
expect_failure "nested Latte data symlink escape" "Latte data tree contains a symlink target escaping the package root" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$escaped_data_content" --prefix /usr --check-only

cross_tree_qml="$work/cross-tree-qml-content"
cp -a "$good" "$cross_tree_qml"
internal_unowned="$cross_tree_qml/usr/share/internal-provider"
mkdir -p "$internal_unowned"
: >"$internal_unowned/Injected.qml"
rm "$cross_tree_qml/usr/lib/qt6/qml/org/kde/latte/components/deep/Installed.qml"
ln -s /usr/share/internal-provider/Injected.qml \
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

absolute_rpath="$work/absolute-rpath"
cp -a "$good" "$absolute_rpath"
NIX_LDFLAGS= NIX_LDFLAGS_BEFORE= \
    cc "$elf_source" -o "$absolute_rpath/usr/bin/latte-dock"
patchelf --remove-rpath "$absolute_rpath/usr/bin/latte-dock"
patchelf --set-rpath /usr/lib "$absolute_rpath/usr/bin/latte-dock"
expect_failure "isolated-root absolute ELF RUNPATH" \
    "uses an absolute entry that the loader cannot interpret inside isolated package root" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$absolute_rpath" --prefix /usr --check-only

live_absolute_rpath="$work/live-absolute-rpath"
cp -a "$good" "$live_absolute_rpath"
NIX_LDFLAGS= NIX_LDFLAGS_BEFORE= \
    cc "$elf_source" -o "$live_absolute_rpath/usr/bin/latte-dock"
patchelf --remove-rpath "$live_absolute_rpath/usr/bin/latte-dock"
patchelf --set-rpath "$live_absolute_rpath/usr/lib" \
    "$live_absolute_rpath/usr/bin/latte-dock"
live_absolute_rpath_manifest="$work/live-absolute-rpath.manifest"
write_package_manifest "$live_absolute_rpath" "$live_absolute_rpath_manifest" "" /
live_absolute_rpath_output="$(
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
        bash "$gate" --root / --prefix "$live_absolute_rpath/usr" \
        --manifest "$live_absolute_rpath_manifest" --check-only
)"
[[ "$live_absolute_rpath_output" == *"installed-package-gate: CHECK OK"* ]] \
    || { echo "FAIL: live-root absolute RUNPATH inside the package prefix was rejected" >&2; echo "$live_absolute_rpath_output" >&2; exit 1; }
echo "PASS: absolute ELF search paths are accepted only with live-root semantics"

rpath_binary="$work/rpath-binary"
cp -a "$good" "$rpath_binary"
mkdir -p "$rpath_binary/foreign-loader"
NIX_LDFLAGS= NIX_LDFLAGS_BEFORE= \
    cc "$elf_source" -o "$rpath_binary/usr/bin/latte-dock"
patchelf --remove-rpath "$rpath_binary/usr/bin/latte-dock"
patchelf --set-rpath '$ORIGIN/../../foreign-loader' \
    "$rpath_binary/usr/bin/latte-dock"
binary_dynamic_metadata="$(readelf -d "$rpath_binary/usr/bin/latte-dock")" \
    || { echo "FAIL: hostile binary RUNPATH metadata could not be read" >&2; exit 1; }
[[ "$binary_dynamic_metadata" == *'$ORIGIN/../../foreign-loader'* ]] \
    || { echo "FAIL: hostile binary RUNPATH fixture has no requested entry" >&2; exit 1; }
expect_failure "binary ELF RUNPATH escape" "installed binary ELF RUNPATH/RPATH entry escapes the package prefix" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$rpath_binary" --prefix /usr --check-only

rpath_action="$work/rpath-action-plugin"
cp -a "$good" "$rpath_action"
mkdir -p "$rpath_action/foreign-loader"
patchelf --set-rpath '$ORIGIN/../../../../../../foreign-loader' \
    "$rpath_action/usr/lib/qt6/plugins/plasma/containmentactions/org.kde.latte.contextmenu.so"
action_dynamic_metadata="$(readelf -d \
    "$rpath_action/usr/lib/qt6/plugins/plasma/containmentactions/org.kde.latte.contextmenu.so")" \
    || { echo "FAIL: hostile containment-actions RUNPATH metadata could not be read" >&2; exit 1; }
[[ "$action_dynamic_metadata" == *'$ORIGIN/../../../../../../foreign-loader'* ]] \
    || { echo "FAIL: hostile containment-actions RUNPATH fixture has no requested entry" >&2; exit 1; }
expect_failure "lazy containment-actions ELF RUNPATH escape" "Latte containment-actions plugin ELF RUNPATH/RPATH entry escapes the package prefix" \
    env LATTE_QML_MODULE_PATH="$framework" LATTE_RUNTIME_DATA_PATH="$runtime_data" \
    bash "$gate" --root "$rpath_action" --prefix /usr --check-only

mapped_artifact="$work/mapped artifact"
mapped_prefix="$mapped_artifact/usr"
mkdir -p "$mapped_prefix/bin" "$mapped_prefix/lib/qt6/qml/org/kde/latte/core"
mapped_binary="$mapped_prefix/bin/latte-dock.real"
mapped_core="$mapped_prefix/lib/qt6/qml/org/kde/latte/core/liblattecoreplugin.so"
: >"$mapped_binary"
: >"$mapped_core"
maps_with_spaces="$work/maps with spaces"
printf '1000-2000 r-xp 00000000 00:00 1 %s\n2000-3000 r--p 00000000 00:00 2 %s\n' \
    "$mapped_binary" "$mapped_core" >"$maps_with_spaces"
parsed_space_paths=()
latte_package_gate_read_mapped_paths "$maps_with_spaces" parsed_space_paths
[[ "${parsed_space_paths[0]}" == "$mapped_binary" && "${parsed_space_paths[1]}" == "$mapped_core" ]] \
    || { echo "FAIL: /proc maps parser split a pathname containing spaces" >&2; exit 1; }
declare -A expected_space_mappings=(
    [latte-dock.real]="$mapped_binary"
    [liblattecoreplugin.so]="$mapped_core"
)
required_space_mappings=(latte-dock.real liblattecoreplugin.so)
latte_package_gate_audit_mapped_paths "$maps_with_spaces" "$mapped_prefix" "$repo" \
    expected_space_mappings required_space_mappings >/dev/null
echo "PASS: /proc maps pathnames with spaces preserve exact installed-artifact identity"

declare -A optional_mapping=()
optional_mapping_required=()
latte_package_gate_register_expected_mapping optional_mapping \
    optional_mapping_required "optional indicator" "$mapped_prefix/latte_indicator.so" 0
[[ "${optional_mapping[latte_indicator.so]}" == "$mapped_prefix/latte_indicator.so" \
        && "${#optional_mapping_required[@]}" -eq 0 ]] \
    || { echo "FAIL: optional mapped-artifact registration changed the required set" >&2; exit 1; }
echo "PASS: optional mapped-artifact registration continues under set -e"

set +e
partial_maps_output="$(
    (
        source "$repo/scripts/lib-installed-package-gate.sh"
        awk() {
            printf '1000-2000 r-xp 00000000 00:00 1 %s\n' "$mapped_binary"
            printf '2000-3000 r--p 00000000 00:00 2 %s\n' "$mapped_core"
            return 73
        }
        latte_package_gate_audit_mapped_paths "$maps_with_spaces" "$mapped_prefix" "$repo" \
            expected_space_mappings required_space_mappings
    ) 2>&1
)"
partial_maps_status=$?
set -e
[[ "$partial_maps_status" -eq 2 && "$partial_maps_output" == *"cannot parse process mappings"* ]] \
    || { echo "FAIL: partial /proc maps parser output was not rejected" >&2; echo "$partial_maps_output" >&2; exit 1; }
echo "PASS: partial /proc maps parser output cannot satisfy mapping requirements"

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
latte_package_gate_process_group_has_live_members "$group_ignored_pid" \
    && { echo "FAIL: cleanup left the TERM-ignoring process group alive" >&2; exit 1; }
echo "PASS: nested-style process-group cleanup escalates within fixed bounds"

descendant_ready="$work/group-descendant.ready"
descendant_pid_file="$work/group-descendant.pid"
setsid bash -c '
    bash -c '\''
        trap "" TERM
        printf "%s\n" "$$" >"$1"
        : >"$2"
        while :; do sleep 1; done
    '\'' bash "$1" "$2" &
    trap "exit 0" TERM
    while :; do sleep 1; done
' bash "$descendant_pid_file" "$descendant_ready" &
descendant_group_pid=$!
for ((ready_wait = 0; ready_wait < 50; ready_wait++)); do
    [[ -e "$descendant_ready" ]] && break
    sleep 0.01
done
[[ -e "$descendant_ready" ]] \
    || { kill -KILL -- "-$descendant_group_pid" 2>/dev/null || true; echo "FAIL: descendant-survival fixture did not start" >&2; exit 1; }
descendant_pid="$(<"$descendant_pid_file")"
latte_package_gate_stop_process_group "$descendant_group_pid" \
    "process group with TERM-ignoring descendant" 1 0.01 50 0.01
kill -0 "$descendant_pid" 2>/dev/null \
    && { echo "FAIL: cleanup left a TERM-ignoring dock descendant alive" >&2; exit 1; }
echo "PASS: dock-group cleanup escalates after the leader exits but a descendant survives"

zombie_holder_source="$work/zombie-holder.c"
zombie_holder="$work/zombie-holder"
printf '%s\n' \
    '#include <signal.h>' \
    '#include <stdio.h>' \
    '#include <stdlib.h>' \
    '#include <sys/wait.h>' \
    '#include <unistd.h>' \
    'static pid_t child;' \
    'static void finish(int signal_number) {' \
    '    (void)signal_number;' \
    '    waitpid(child, NULL, 0);' \
    '    _exit(0);' \
    '}' \
    'int main(int argc, char **argv) {' \
    '    siginfo_t child_info;' \
    '    FILE *pid_file;' \
    '    if (argc != 3) return 2;' \
    '    child = fork();' \
    '    if (child < 0) return 3;' \
    '    if (child == 0) {' \
    '        if (setpgid(0, 0) != 0) _exit(4);' \
    '        _exit(0);' \
    '    }' \
    '    if (waitid(P_PID, child, &child_info, WEXITED | WNOWAIT) != 0) return 5;' \
    '    pid_file = fopen(argv[1], "w");' \
    '    if (!pid_file) return 6;' \
    '    fprintf(pid_file, "%ld\n", (long)child);' \
    '    fclose(pid_file);' \
    '    pid_file = fopen(argv[2], "w");' \
    '    if (!pid_file) return 7;' \
    '    fclose(pid_file);' \
    '    signal(SIGTERM, finish);' \
    '    for (;;) pause();' \
    '}' >"$zombie_holder_source"
cc "$zombie_holder_source" -o "$zombie_holder"
zombie_pid_file="$work/zombie.pid"
zombie_ready="$work/zombie.ready"
"$zombie_holder" "$zombie_pid_file" "$zombie_ready" &
zombie_holder_pid=$!
for ((ready_wait = 0; ready_wait < 50; ready_wait++)); do
    [[ -e "$zombie_ready" ]] && break
    sleep 0.01
done
[[ -e "$zombie_ready" ]] \
    || { kill -KILL "$zombie_holder_pid" 2>/dev/null || true; echo "FAIL: zombie fixture did not start" >&2; exit 1; }
zombie_pid="$(<"$zombie_pid_file")"
if latte_package_gate_process_group_has_live_members "$zombie_pid"; then
    kill -TERM "$zombie_holder_pid" 2>/dev/null || true
    wait "$zombie_holder_pid" 2>/dev/null || true
    echo "FAIL: defunct process-group member was treated as live" >&2
    exit 1
else
    zombie_poll_status=$?
fi
[[ "$zombie_poll_status" -eq 1 ]] \
    || { kill -TERM "$zombie_holder_pid" 2>/dev/null || true; wait "$zombie_holder_pid" 2>/dev/null || true; echo "FAIL: zombie-only group poll returned $zombie_poll_status" >&2; exit 1; }
latte_package_gate_stop_process_group "$zombie_pid" "zombie-only process group" 1 0 1 0
[[ -d "/proc/$zombie_pid" ]] \
    || { kill -TERM "$zombie_holder_pid" 2>/dev/null || true; wait "$zombie_holder_pid" 2>/dev/null || true; echo "FAIL: zombie fixture was reaped before cleanup completed" >&2; exit 1; }
kill -TERM "$zombie_holder_pid"
wait "$zombie_holder_pid"
echo "PASS: zombie-only process groups count as successfully stopped"

bounded_group_wait_log="$work/unbounded-group-wait-called"
set +e
bounded_group_output="$(
    (
        source "$repo/scripts/lib-installed-package-gate.sh"
        pgrep() { printf '%s\n' "$$"; return 0; }
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

for pgrep_error_status in 2 3; do
    polling_error_wait_log="$work/polling-error-$pgrep_error_status-wait-called"
    set +e
    polling_error_output="$(
        (
            source "$repo/scripts/lib-installed-package-gate.sh"
            pgrep() { return "$pgrep_error_status"; }
            wait() { : >"$polling_error_wait_log"; }
            latte_package_gate_stop_process_group 424242 "unpollable process group" 1 0 1 0
        ) 2>&1
    )"
    polling_error_rc=$?
    set -e
    [[ "$polling_error_rc" -eq 2 \
            && "$polling_error_output" == *"pgrep failed while polling process group 424242 with status $pgrep_error_status"* ]] \
        || { echo "FAIL: pgrep status $pgrep_error_status was not propagated" >&2; echo "$polling_error_output" >&2; exit 1; }
    [[ ! -e "$polling_error_wait_log" ]] \
        || { echo "FAIL: polling failure entered wait for process group 424242" >&2; exit 1; }
done
echo "PASS: pgrep operational failures abort cleanup without entering wait"

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

echo "installed-package-gate-selftest: PASS (73 focused controls)"
