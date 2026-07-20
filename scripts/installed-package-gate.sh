#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Validate and smoke-test one explicitly installed Latte package. Native
# package recipes own package-manager installation; this script consumes the
# resulting filesystem root and prefix without consulting PATH, restaging the
# build, or falling back to another Latte installation.
# Install-artifact assertions were informed by latte-dock-ng's
# docker/verify-install.sh at 9c12a79aaf9350e73059da5b293c931218419c05
# (github.com/ruizhi-lab/latte-dock-ng); this implementation is original.
set -euo pipefail

script_dir="${BASH_SOURCE[0]%/*}"
[[ "$script_dir" != "${BASH_SOURCE[0]}" ]] || script_dir=.
repo="$(cd "$script_dir/.." && pwd -P)"
source "$repo/scripts/lib-installed-package-gate.sh"
for loader_variable in "${latte_package_gate_loader_variables[@]}"; do
    unset "$loader_variable"
done
unset loader_variable

usage() {
    cat <<'EOF'
Usage: scripts/installed-package-gate.sh --root ROOT [--prefix PREFIX]
       [--manifest MANIFEST] [--check-only]

  --root ROOT      Package filesystem root. Use / after installing the native
                   package in a clean container, or a package staging root.
  --prefix PREFIX  Absolute install prefix inside ROOT (default: /usr).
  --manifest FILE  Newline-delimited package manifest. Entries are absolute
                   paths in ROOT's namespace (for example /usr/bin/latte-dock).
                   Required for --root /; optional for isolated extraction roots.
  --check-only     Validate artifact provenance without starting nested KWin.

LATTE_QML_MODULE_PATH must be an explicit colon-separated allow-list of the
distro's Qt/KF6/Plasma QML roots. LATTE_RUNTIME_DATA_PATH optionally supplies
the corresponding data roots (default: /usr/local/share:/usr/share).
EOF
}

fail() {
    echo "installed-package-gate: FAIL: $*" >&2
    exit 2
}

require_commands() {
    local phase="$1" required_command
    shift
    for required_command in "$@"; do
        command -v "$required_command" >/dev/null 2>&1 \
            || fail "required $phase command '$required_command' is missing"
    done
}

require_commands validation awk cat dirname env find jq mktemp perl readelf readlink realpath rm timeout tr

qt_plugin_info=""
latte_package_gate_find_qt6_plugin_info qt_plugin_info \
    || fail "required Qt 6 validation command 'qtplugininfo' is missing or reports a non-Qt-6 version"

path_is_within() {
    local path="$1" base="$2"
    [[ "$base" == / || "$path" == "$base" || "$path" == "$base/"* ]]
}

# realpath follows absolute links from host /. Walk each component so an
# absolute link inside an extraction root restarts from that package root.
resolve_package_namespace_path() {
    local label="$1" raw="$2" normalized relative resolved pending component candidate
    local target remainder link_count=0

    if [[ "$package_root" == / ]]; then
        resolve_native_path "$label" "$raw"
        return
    fi

    normalized="$(realpath -ms -- "$raw" 2>/dev/null)" \
        || fail "$label cannot be normalized in the package namespace: $raw"
    path_is_within "$normalized" "$package_root" \
        || fail "$label escapes the package root: $raw"
    relative="${normalized#"$package_root"}"
    pending="${relative#/}"
    resolved="$package_root"

    while [[ -n "$pending" ]]; do
        component="${pending%%/*}"
        if [[ "$pending" == */* ]]; then
            remainder="${pending#*/}"
        else
            remainder=""
        fi
        candidate="$resolved/$component"
        if [[ -L "$candidate" ]]; then
            ((link_count += 1))
            [[ "$link_count" -le 40 ]] \
                || fail "$label contains more than 40 chained symlinks: $raw"
            target="$(readlink "$candidate")" \
                || fail "$label contains an unreadable symlink: $candidate"
            if [[ "$target" == /* ]]; then
                candidate="$package_root/${target#/}"
            else
                candidate="$resolved/$target"
            fi
            [[ -z "$remainder" ]] || candidate="$candidate/$remainder"
            normalized="$(realpath -ms -- "$candidate" 2>/dev/null)" \
                || fail "$label cannot normalize a chained symlink target: $candidate"
            path_is_within "$normalized" "$package_root" \
                || fail "$label escapes the package root through a symlink: $normalized"
            relative="${normalized#"$package_root"}"
            pending="${relative#/}"
            resolved="$package_root"
            continue
        fi
        resolved="$candidate"
        pending="$remainder"
    done

    [[ -e "$resolved" ]] || fail "$label does not exist in the package namespace: $raw"
    printf '%s\n' "$resolved"
}

resolve_native_path() {
    local label="$1" raw="$2" resolved
    case "$raw" in
        /nix/store/*)
            fail "$label points into /nix/store ($raw); native package validation must not consume the Nix package or development closure"
            ;;
        *'/_qmlstage'|*'/_qmlstage/'*)
            fail "$label points into a development _qmlstage ($raw); install the native package into the caller-supplied root first"
            ;;
    esac
    resolved="$(realpath "$raw" 2>/dev/null)" \
        || fail "$label does not exist or cannot be resolved: $raw"
    case "$resolved" in
        /nix/store/*)
            fail "$label resolves into /nix/store ($resolved); native package validation must not consume the Nix package or development closure"
            ;;
        "$repo"|"$repo/"*)
            fail "$label resolves inside the source/build tree ($resolved); pass an installed package root, not the checkout"
            ;;
        *'/_qmlstage'|*'/_qmlstage/'*)
            fail "$label resolves into a development _qmlstage ($resolved); pass an installed package root"
            ;;
    esac
    printf '%s\n' "$resolved"
}

require_file() {
    local label="$1" path="$2"
    [[ -f "$path" ]] || fail "package is incomplete: missing $label at $path"
}

declare -A package_manifest_paths=()
manifest_enforced=0

require_manifest_ownership() {
    local label="$1" path="$2" logical_path
    [[ "$manifest_enforced" == 1 ]] || return 0
    logical_path="$(realpath -ms -- "$path" 2>/dev/null)" \
        || fail "cannot normalize $label for package-manifest ownership: $path"
    [[ -n "${package_manifest_paths[$logical_path]+present}" ]] \
        || fail "$label is present under the package prefix but omitted by the package manifest: $logical_path"
}

require_package_file() {
    local label="$1" path="$2" resolved
    require_file "$label" "$path"
    require_manifest_ownership "installed $label" "$path"
    resolved="$(resolve_native_path "installed $label" "$path")"
    path_is_within "$resolved" "$artifact_prefix" \
        || fail "installed $label resolves outside the package prefix: $resolved"
    require_manifest_ownership "resolved installed $label" "$resolved"
}

find_scan_index=0
collect_find_results() {
    local label="$1" output_name="$2" output_file
    shift 2
    local -n output_paths="$output_name"
    output_paths=()
    ((find_scan_index += 1))
    output_file="$validation_tmp/find-$find_scan_index"
    if ! find "$@" -print0 >"$output_file"; then
        fail "$label scan failed before a complete result was available"
    fi
    if ! mapfile -d '' -t output_paths <"$output_file"; then
        fail "$label scan output could not be read completely"
    fi
}

audit_package_tree() {
    local label="$1" tree="$2" tree_resolved entry link target target_candidate target_normalized
    local logical_target link_resolved provider_dir
    local -a tree_entries=() tree_links=()
    [[ ! -L "$tree" ]] || fail "$label root must not be a symlink: $tree"
    [[ -d "$tree" ]] || fail "package is incomplete: missing $label at $tree"
    tree_resolved="$(resolve_native_path "installed $label" "$tree")"
    path_is_within "$tree_resolved" "$artifact_prefix" \
        || fail "installed $label resolves outside the package prefix: $tree_resolved"

    collect_find_results "$label contents" tree_entries "$tree" \( -type f -o -type l \)
    for entry in "${tree_entries[@]}"; do
        require_manifest_ownership "$label content" "$entry"
        [[ -L "$entry" ]] && tree_links+=("$entry")
    done
    for link in "${tree_links[@]}"; do
        target="$(readlink "$link")" \
            || fail "$label contains an unreadable symlink: $link"
        if [[ "$target" == /* && "$package_root" != / ]]; then
            target_candidate="$package_root/${target#/}"
        elif [[ "$target" == /* ]]; then
            target_candidate="$target"
        else
            target_candidate="$(dirname "$link")/$target"
        fi
        target_normalized="$(realpath -ms -- "$target_candidate" 2>/dev/null)" \
            || fail "$label symlink target cannot be normalized: $link -> $target"
        path_is_within "$target_normalized" "$package_root" \
            || fail "$label contains a symlink target escaping the package root: $link -> $target_normalized"
        if [[ "$package_root" == / ]]; then
            logical_target="$target_normalized"
        else
            logical_target="${target_normalized#"$package_root"}"
            [[ -n "$logical_target" ]] || logical_target=/
        fi
        case "$logical_target" in
            /nix/store/*)
                fail "$label contains a symlink into /nix/store: $link -> $logical_target"
                ;;
        esac
        link_resolved="$(resolve_package_namespace_path "$label symlink" "$target_candidate")" \
            || fail "$label contains a broken or unresolvable symlink: $link -> $target"
        case "$link_resolved" in
            "$repo"|"$repo/"*)
                fail "$label contains a symlink into the source/build tree: $link -> $link_resolved"
                ;;
            *'/_qmlstage'|*'/_qmlstage/'*)
                fail "$label contains a symlink into a development _qmlstage: $link -> $link_resolved"
                ;;
        esac
        provider_dir="$link_resolved"
        [[ -d "$provider_dir" ]] || provider_dir="$(dirname "$provider_dir")"
        while [[ "$provider_dir" != / ]]; do
            if [[ -e "$provider_dir/.git" || -f "$provider_dir/CMakeLists.txt" ]]; then
                fail "$label contains a symlink into a source tree: $link -> $target_normalized"
            fi
            if [[ -f "$provider_dir/CMakeCache.txt" || -d "$provider_dir/CMakeFiles" ]]; then
                fail "$label contains a symlink into a CMake build tree: $link -> $target_normalized"
            fi
            provider_dir="$(dirname "$provider_dir")"
        done
        path_is_within "$link_resolved" "$tree_resolved" \
            || fail "$label contains a symlink escaping its installed runtime tree: $link -> $link_resolved"
    done
}

audit_elf_search_paths() {
    local label="$1" elf="$2" require_elf="${3:-1}" origin search_path entry expanded normalized resolved
    local -a search_paths=() entries=()
    if ! readelf -h -- "$elf" >/dev/null 2>&1; then
        [[ "$require_elf" == 0 ]] && return 0
        fail "$label is not a valid ELF artifact: $elf"
    fi

    latte_package_gate_read_elf_search_paths "$elf" search_paths \
        || fail "$label ELF search metadata could not be read completely"
    origin="$(dirname "$elf")"
    for search_path in "${search_paths[@]}"; do
        case "$search_path" in
            :*|*:|*::* ) fail "$label ELF RUNPATH/RPATH contains an empty loader search entry: $search_path" ;;
        esac
        IFS=':' read -r -a entries <<<"$search_path"
        for entry in "${entries[@]}"; do
            expanded="${entry//\$\{ORIGIN\}/$origin}"
            expanded="${expanded//\$ORIGIN/$origin}"
            [[ "$expanded" != *'$'* ]] \
                || fail "$label ELF RUNPATH/RPATH uses an unsupported dynamic loader token: $entry"
            [[ "$expanded" == /* ]] \
                || fail "$label ELF RUNPATH/RPATH entry is relative to ambient working state: $entry"
            normalized="$(realpath -m "$expanded" 2>/dev/null)" \
                || fail "$label ELF RUNPATH/RPATH entry cannot be normalized: $entry"
            resolved="$(resolve_native_path "$label ELF RUNPATH/RPATH entry" "$normalized")"
            [[ -d "$resolved" ]] \
                || fail "$label ELF RUNPATH/RPATH entry is not an installed directory: $entry -> $resolved"
            path_is_within "$resolved" "$artifact_prefix" \
                || fail "$label ELF RUNPATH/RPATH entry escapes the package prefix: $entry -> $resolved"
        done
    done
}

require_loadable_plugin() {
    local label="$1" plugin="$2" loader_output loader_status
    if loader_output="$(
        timeout --kill-after=1s 5s env LD_BIND_NOW=1 perl -MDynaLoader -e '
            my $handle = DynaLoader::dl_load_file($ARGV[0], 0x02);
            die DynaLoader::dl_error() unless $handle;
        ' "$plugin" 2>&1
    )"; then
        return 0
    else
        loader_status=$?
    fi
    case "$loader_status" in
        124|137) fail "$label loader timed out for installed artifact $plugin" ;;
        *) fail "$label cannot be loaded from the installed artifact $plugin: $loader_output" ;;
    esac
}

require_plugin_metadata() {
    local label="$1" plugin="$2" expected_iid="$3" expected_class="$4"
    local metadata_filter="$5" metadata_description="$6"
    local metadata_output metadata_iid metadata_class metadata_status

    if metadata_output="$(timeout --kill-after=1s 5s "$qt_plugin_info" \
            --full-json -f compact "$plugin" 2>&1)"; then
        :
    else
        metadata_status=$?
        case "$metadata_status" in
            124|137) fail "$label metadata inspection timed out for $plugin" ;;
            *) fail "$label has no valid Qt plugin metadata at $plugin: $metadata_output" ;;
        esac
    fi
    metadata_iid="$(jq -er '.IID | strings' <<<"$metadata_output")" \
        || fail "$label metadata has no string IID at $plugin"
    metadata_class="$(jq -er '.className | strings' <<<"$metadata_output")" \
        || fail "$label metadata has no string className at $plugin"
    [[ "$metadata_iid" == "$expected_iid" ]] \
        || fail "$label has IID '$metadata_iid', expected '$expected_iid'"
    [[ "$metadata_class" == "$expected_class" ]] \
        || fail "$label has class '$metadata_class', expected '$expected_class'"
    jq -e "$metadata_filter" <<<"$metadata_output" >/dev/null \
        || fail "$label metadata does not declare $metadata_description"
}

require_one_match() {
    local label="$1"
    shift
    local -a matches=("$@")
    [[ "${#matches[@]}" -eq 1 ]] \
        || fail "expected exactly one installed $label, found ${#matches[@]} (${matches[*]:-none})"
    printf '%s\n' "${matches[0]}"
}

root_raw=""
prefix="/usr"
manifest_raw=""
check_only=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --root)
            [[ $# -ge 2 ]] || fail "--root needs a value"
            root_raw="$2"
            shift 2
            ;;
        --prefix)
            [[ $# -ge 2 ]] || fail "--prefix needs a value"
            prefix="$2"
            shift 2
            ;;
        --manifest)
            [[ $# -ge 2 ]] || fail "--manifest needs a value"
            manifest_raw="$2"
            shift 2
            ;;
        --check-only)
            check_only=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            fail "unknown argument '$1' (see --help)"
            ;;
    esac
done

[[ -n "$root_raw" ]] || fail "--root is required; implicit system-package lookup is forbidden"
[[ "$root_raw" == /* ]] || fail "--root must be absolute: $root_raw"
[[ "$prefix" == /* ]] || fail "--prefix must be absolute: $prefix"
[[ -z "$manifest_raw" || "$manifest_raw" == /* ]] \
    || fail "--manifest must be absolute: $manifest_raw"
[[ "$prefix" != *'/../'* && "$prefix" != */.. && "$prefix" != *'/./'* ]] \
    || fail "--prefix must not contain . or .. components: $prefix"

validation_tmp="$(mktemp -d /tmp/latte-installed-validation.XXXXXX)" \
    || fail "cannot create validation scratch directory"
cleanup_validation() {
    rm -rf "$validation_tmp"
}
latte_package_gate_install_exit_cleanup cleanup_validation

package_root="$(resolve_native_path "package root" "$root_raw")"
if [[ -e "$package_root/.git" || -f "$package_root/CMakeLists.txt" ]]; then
    fail "package root is a source tree ($package_root); install the native package into a separate filesystem root first"
fi
if [[ -f "$package_root/CMakeCache.txt" || -d "$package_root/CMakeFiles" ]]; then
    fail "package root is a CMake build tree ($package_root); pass the package installation root instead"
fi
if [[ "$package_root" == / ]]; then
    prefix_path="$prefix"
else
    prefix_path="$package_root/${prefix#/}"
fi
artifact_prefix="$(resolve_native_path "package prefix" "$prefix_path")"
path_is_within "$artifact_prefix" "$package_root" \
    || fail "package prefix escapes package root: $artifact_prefix is outside $package_root"

if [[ "$package_root" == / && -z "$manifest_raw" ]]; then
    fail "--manifest is required with --root / so preinstalled same-prefix artifacts cannot satisfy the package gate"
fi
if [[ -n "$manifest_raw" ]]; then
    [[ -f "$manifest_raw" ]] || fail "package manifest is missing or not a regular file: $manifest_raw"
    manifest_entries=()
    mapfile -t manifest_entries <"$manifest_raw" \
        || fail "package manifest could not be read completely: $manifest_raw"
    [[ "${#manifest_entries[@]}" -gt 0 ]] || fail "package manifest is empty: $manifest_raw"
    for manifest_entry in "${manifest_entries[@]}"; do
        [[ -n "$manifest_entry" ]] || fail "package manifest contains an empty entry: $manifest_raw"
        [[ "$manifest_entry" != *$'\r'* ]] \
            || fail "package manifest contains a carriage return: $manifest_entry"
        [[ "$manifest_entry" == /* ]] \
            || fail "package manifest entries must be absolute in the package namespace: $manifest_entry"
        if [[ "$package_root" == / ]]; then
            manifest_host_path="$manifest_entry"
        else
            manifest_host_path="$package_root/${manifest_entry#/}"
        fi
        manifest_host_path="$(realpath -ms -- "$manifest_host_path" 2>/dev/null)" \
            || fail "package manifest entry cannot be normalized: $manifest_entry"
        path_is_within "$manifest_host_path" "$artifact_prefix" \
            || fail "package manifest entry is outside the package prefix: $manifest_entry"
        [[ -z "${package_manifest_paths[$manifest_host_path]+present}" ]] \
            || fail "package manifest contains a duplicate entry: $manifest_entry"
        package_manifest_paths["$manifest_host_path"]=1
    done
    manifest_enforced=1
fi

binary_path="$artifact_prefix/bin/latte-dock"
[[ -x "$binary_path" ]] \
    || fail "installed binary is missing or not executable at $binary_path; PATH fallback is forbidden"
require_manifest_ownership "installed binary" "$binary_path"
binary="$(resolve_native_path "installed binary" "$binary_path")"
path_is_within "$binary" "$artifact_prefix" \
    || fail "installed binary resolves outside the package prefix: $binary"
require_manifest_ownership "resolved installed binary" "$binary"

library_roots=()
for candidate in "$artifact_prefix/lib" "$artifact_prefix/lib64"; do
    [[ -d "$candidate" ]] || continue
    resolved="$(resolve_native_path "package library root" "$candidate")"
    path_is_within "$resolved" "$artifact_prefix" \
        || fail "package library root escapes the package prefix: $resolved"
    duplicate=0
    for present in "${library_roots[@]}"; do
        [[ "$present" == "$resolved" ]] && duplicate=1
    done
    [[ "$duplicate" == 1 ]] || library_roots+=("$resolved")
done
[[ "${#library_roots[@]}" -gt 0 ]] \
    || fail "package prefix has no lib or lib64 directory: $artifact_prefix"

collect_find_results "Latte QML manifest discovery" qml_manifests \
    "${library_roots[@]}" \( -type f -o -type l \) \
    -path '*/org/kde/latte/core/qmldir'
qml_manifest="$(require_one_match "org.kde.latte.core/qmldir" "${qml_manifests[@]}")"
require_manifest_ownership "installed org.kde.latte.core/qmldir" "$qml_manifest"
package_qml="${qml_manifest%/org/kde/latte/core/qmldir}"
package_qml="$(resolve_native_path "installed Latte QML root" "$package_qml")"
path_is_within "$package_qml" "$artifact_prefix" \
    || fail "installed Latte QML root escapes the package prefix: $package_qml"

core_plugin="$package_qml/org/kde/latte/core/liblattecoreplugin.so"
containment_plugin="$package_qml/org/kde/latte/private/containment/liblattecontainmentplugin.so"
tasks_plugin="$package_qml/org/kde/latte/private/tasks/liblattetasksplugin.so"
require_package_file "core QML plugin" "$core_plugin"
require_package_file "containment QML plugin" "$containment_plugin"
require_package_file "tasks QML plugin" "$tasks_plugin"
require_package_file "containment QML module metadata" "$package_qml/org/kde/latte/private/containment/qmldir"
require_package_file "tasks QML module metadata" "$package_qml/org/kde/latte/private/tasks/qmldir"
audit_package_tree "Latte QML tree" "$package_qml/org/kde/latte"

collect_find_results "Latte containment-actions plugin discovery" action_plugins \
    "${library_roots[@]}" \( -type f -o -type l \) \
    -path '*/plasma/containmentactions/org.kde.latte.contextmenu.so'
action_plugin="$(require_one_match "Latte containment-actions plugin" "${action_plugins[@]}")"
require_package_file "Latte containment-actions plugin" "$action_plugin"
package_plugins="${action_plugin%/plasma/containmentactions/org.kde.latte.contextmenu.so}"
package_plugins="$(resolve_native_path "installed Latte plugin root" "$package_plugins")"
path_is_within "$package_plugins" "$artifact_prefix" \
    || fail "installed Latte plugin root escapes the package prefix: $package_plugins"
indicator_package_plugin="$package_plugins/kpackage/packagestructure/latte_indicator.so"
require_package_file "Latte indicator package-structure plugin" "$indicator_package_plugin"

package_data="$(resolve_native_path "installed Latte data root" "$artifact_prefix/share")"
path_is_within "$package_data" "$artifact_prefix" \
    || fail "installed Latte data root escapes the package prefix: $package_data"
require_package_file "shell package metadata" "$package_data/plasma/shells/org.kde.latte.shell/metadata.json"
require_package_file "containment package metadata" "$package_data/plasma/plasmoids/org.kde.latte.containment/metadata.json"
require_package_file "tasks applet package metadata" "$package_data/plasma/plasmoids/org.kde.latte.plasmoid/metadata.json"
require_package_file "desktop entry" "$package_data/applications/org.kde.latte-dock.desktop"
audit_package_tree "Latte shell package" "$package_data/plasma/shells/org.kde.latte.shell"
audit_package_tree "Latte containment package" "$package_data/plasma/plasmoids/org.kde.latte.containment"
audit_package_tree "Latte tasks applet package" "$package_data/plasma/plasmoids/org.kde.latte.plasmoid"
audit_package_tree "Latte data tree" "$package_data/latte"
[[ -d "$package_data/latte/indicators/default" ]] \
    || fail "package is incomplete: missing default indicator under $package_data/latte/indicators"

audit_elf_search_paths "installed binary" "$binary"
package_plugin_labels=(
    "Latte core QML plugin"
    "Latte containment QML plugin"
    "Latte tasks QML plugin"
    "Latte containment-actions plugin"
    "Latte indicator package-structure plugin"
)
package_plugin_paths=(
    "$core_plugin"
    "$containment_plugin"
    "$tasks_plugin"
    "$action_plugin"
    "$indicator_package_plugin"
)
package_plugin_iids=(
    "org.qt-project.Qt.QQmlExtensionInterface"
    "org.qt-project.Qt.QQmlExtensionInterface"
    "org.qt-project.Qt.QQmlExtensionInterface"
    "org.kde.KPluginFactory"
    "org.kde.KPluginFactory"
)
package_plugin_classes=(
    "LatteCorePlugin"
    "LatteContainmentPlugin"
    "LatteTasksPlugin"
    "MenuFactory"
    "latte_packagestructure_indicator_factory"
)
package_plugin_metadata_filters=(
    'true'
    'true'
    'true'
    '.MetaData.KPlugin.Id == "org.kde.latte.contextmenu" and (.MetaData.KPlugin.ServiceTypes | index("Plasma/ContainmentActions") != null)'
    '.MetaData.KPackageStructure == "Latte/Indicator" and .MetaData["X-KDE-ParentApp"] == "org.kde.latte-dock"'
)
package_plugin_metadata_descriptions=(
    "the Latte core QML extension type"
    "the Latte containment QML extension type"
    "the Latte tasks QML extension type"
    "the org.kde.latte.contextmenu Plasma/ContainmentActions type"
    "the Latte/Indicator package-structure type for org.kde.latte-dock"
)
for ((plugin_index = 0; plugin_index < ${#package_plugin_paths[@]}; plugin_index++)); do
    audit_elf_search_paths "${package_plugin_labels[$plugin_index]}" "${package_plugin_paths[$plugin_index]}"
    require_plugin_metadata \
        "${package_plugin_labels[$plugin_index]}" \
        "${package_plugin_paths[$plugin_index]}" \
        "${package_plugin_iids[$plugin_index]}" \
        "${package_plugin_classes[$plugin_index]}" \
        "${package_plugin_metadata_filters[$plugin_index]}" \
        "${package_plugin_metadata_descriptions[$plugin_index]}"
    require_loadable_plugin "${package_plugin_labels[$plugin_index]}" "${package_plugin_paths[$plugin_index]}"
done

: "${LATTE_QML_MODULE_PATH:?installed-package-gate needs an explicit distro QML allow-list in LATTE_QML_MODULE_PATH}"
case "$LATTE_QML_MODULE_PATH" in
    :*|*:|*::* ) fail "LATTE_QML_MODULE_PATH contains an empty entry, which would search an ambient directory" ;;
esac

qml_imports=()
IFS=':' read -r -a qml_candidates <<<"$LATTE_QML_MODULE_PATH"
for candidate in "${qml_candidates[@]}"; do
    [[ "$candidate" == /* ]] \
        || fail "LATTE_QML_MODULE_PATH entries must be absolute: $candidate"
    resolved="$(resolve_native_path "QML allow-list entry" "$candidate")"
    if [[ -e "$resolved/org/kde/latte" && "$resolved" != "$package_qml" ]]; then
        fail "QML allow-list entry $resolved contains a foreign org/kde/latte tree; remove the preinstalled Latte package before validation"
    fi
    duplicate=0
    for present in "${qml_imports[@]}"; do
        [[ "$present" == "$resolved" ]] && duplicate=1
    done
    [[ "$duplicate" == 1 || "$resolved" == "$package_qml" ]] || qml_imports+=("$resolved")
done
# The package's Latte modules are last, matching lib-qml-env.sh's precedence:
# later entries win and no dependency root can shadow org.kde.latte.*.
qml_imports+=("$package_qml")
qml_import_path="$(IFS=:; printf '%s' "${qml_imports[*]}")"

runtime_data_raw="${LATTE_RUNTIME_DATA_PATH:-/usr/local/share:/usr/share}"
case "$runtime_data_raw" in
    :*|*:|*::* ) fail "LATTE_RUNTIME_DATA_PATH contains an empty entry" ;;
esac
runtime_data=("$package_data")
IFS=':' read -r -a data_candidates <<<"$runtime_data_raw"
for candidate in "${data_candidates[@]}"; do
    [[ "$candidate" == /* ]] \
        || fail "LATTE_RUNTIME_DATA_PATH entries must be absolute: $candidate"
    [[ -d "$candidate" ]] || continue
    resolved="$(resolve_native_path "runtime data allow-list entry" "$candidate")"
    if [[ "$resolved" != "$package_data" ]]; then
        for marker in \
            plasma/shells/org.kde.latte.shell \
            plasma/plasmoids/org.kde.latte.containment \
            plasma/plasmoids/org.kde.latte.plasmoid \
            latte/indicators; do
            [[ ! -e "$resolved/$marker" ]] \
                || fail "runtime data root $resolved contains foreign Latte data ($marker); remove the preinstalled package before validation"
        done
        runtime_data+=("$resolved")
    fi
done
xdg_data_dirs="$(IFS=:; printf '%s' "${runtime_data[*]}")"

echo "installed-package-gate: artifact prefix: $artifact_prefix"
echo "installed-package-gate: binary: $binary"
echo "installed-package-gate: Latte QML root: $package_qml"
echo "installed-package-gate: Latte plugin root: $package_plugins"
echo "installed-package-gate: Latte data root: $package_data"
echo "installed-package-gate: QML allow-list: $qml_import_path"

if [[ "$check_only" == 1 ]]; then
    echo "installed-package-gate: CHECK OK"
    exit 0
fi

require_commands runtime \
    busctl cat chmod dbus-run-session env find kwin_wayland mkdir mktemp pgrep rm seq setsid sh sleep tail tr
if ! command -v fusermount3 >/dev/null 2>&1 && ! command -v fusermount >/dev/null 2>&1; then
    fail "required runtime command 'fusermount3' or 'fusermount' is missing"
fi

source "$repo/scripts/lib-nested-kwin.sh"

dock_pid=""
cleanup_nested_vehicle() {
    local cleanup_status=0
    if [[ -n "${NESTED_KWIN_PID:-}" ]]; then
        latte_package_gate_stop_process_group "$NESTED_KWIN_PID" \
            "nested KWin process group $NESTED_KWIN_PID" || cleanup_status=2
        # Process cleanup is bounded above. Clear the handle so the shared
        # helper performs only its FUSE and runtime-directory cleanup.
        NESTED_KWIN_PID=""
    fi
    nested_kwin_cleanup || cleanup_status=2
    return "$cleanup_status"
}

cleanup() {
    local cleanup_status=0
    if [[ -n "$dock_pid" ]]; then
        latte_package_gate_stop_process_group "$dock_pid" \
            "dock process group $dock_pid" || cleanup_status=2
    fi
    cleanup_nested_vehicle || cleanup_status=2
    rm -rf "$validation_tmp" || cleanup_status=2
    return "$cleanup_status"
}
latte_package_gate_install_exit_cleanup cleanup

nested_kwin_prepare
mkdir -p "$NESTED_RT/kwin-config" "$NESTED_RT/kwin-cache"
nested_kwin_env+=(
    WAYLAND_DISPLAY=latte-installed-gate-wl
    XDG_CONFIG_HOME="$NESTED_RT/kwin-config"
    XDG_CACHE_HOME="$NESTED_RT/kwin-cache"
    QT_FORCE_STDERR_LOGGING=1
)
nested_kwin_start 1600 1000 latte-installed-gate-wl || exit 2

export XDG_RUNTIME_DIR="$NESTED_RT"
export WAYLAND_DISPLAY="$NESTED_SOCK"
export DBUS_SESSION_BUS_ADDRESS="$NESTED_BUS"
unset DISPLAY XAUTHORITY

config_home="$NESTED_RT/latte-config"
cache_home="$NESTED_RT/latte-cache"
data_home="$NESTED_RT/latte-data"
mkdir -p "$config_home" "$cache_home" "$data_home"
dock_log="$NESTED_RT/latte-dock.log"

echo "installed-package-gate: starting installed dock in the nested compositor"
setsid env \
    "${latte_package_gate_loader_unset_args[@]}" \
    -u QML_IMPORT_PATH -u NIXPKGS_QT6_QML_IMPORT_PATH -u NIXPKGS_QML_SEARCH_PATHS \
    -u QT_PLUGIN_PATH \
    QML2_IMPORT_PATH="$qml_import_path" \
    XDG_CONFIG_HOME="$config_home" \
    XDG_CACHE_HOME="$cache_home" \
    XDG_DATA_HOME="$data_home" \
    XDG_DATA_DIRS="$xdg_data_dirs" \
    QT_QPA_PLATFORM=wayland \
    QT_QPA_PLATFORMTHEME= \
    QT_FORCE_STDERR_LOGGING=1 \
    LATTE_EXTRA_PLUGIN_PATHS="$package_plugins" \
    "$binary" -d >"$dock_log" 2>&1 &
dock_pid=$!

state=""
views=""
previous_views=""
settled=0
for ((i = 0; i < 90; i++)); do
    if ! kill -0 "$dock_pid" 2>/dev/null; then
        echo "installed-package-gate: installed dock exited during startup; log tail:" >&2
        tail -40 "$dock_log" >&2 || true
        fail "installed binary did not survive startup"
    fi
    if state_candidate="$(busctl --user call org.kde.lattedock /Latte org.kde.LatteDock lifecycleState 2>/dev/null)"; then
        state="$state_candidate"
    else
        state=""
    fi
    if views_candidate="$(busctl --user call org.kde.lattedock /Latte org.kde.LatteDock viewsData 2>/dev/null)"; then
        views="$views_candidate"
    else
        views=""
    fi
    if [[ "$state" == 's "running"' && -n "$views" && "$views" != 's "[]"' \
            && "$views" != *'inStartup\\":true'* ]]; then
        if [[ "$views" == "$previous_views" ]]; then
            settled=1
            break
        fi
        previous_views="$views"
    fi
    sleep 1
done
if [[ "$settled" != 1 ]]; then
    echo "installed-package-gate: dock failed to settle (last lifecycle reply: ${state:-none}); log tail:" >&2
    tail -40 "$dock_log" >&2 || true
    fail "installed dock did not reach a stable running view within 90 seconds"
fi

running_exe="$(realpath "/proc/$dock_pid/exe" 2>/dev/null)" \
    || fail "cannot resolve /proc/$dock_pid/exe for the running dock"
[[ "$running_exe" == "$binary" ]] \
    || fail "running executable is $running_exe, expected the installed artifact $binary"
echo "installed-package-gate: running executable verified: $running_exe"

latte_package_gate_read_environment_value \
    "/proc/$dock_pid/environ" QML2_IMPORT_PATH actual_qml \
    || fail "cannot verify the running dock's QML2_IMPORT_PATH"
latte_package_gate_read_environment_value \
    "/proc/$dock_pid/environ" LATTE_EXTRA_PLUGIN_PATHS actual_plugins \
    || fail "cannot verify the running dock's LATTE_EXTRA_PLUGIN_PATHS"
[[ "$actual_qml" == "$qml_import_path" ]] \
    || fail "running QML2_IMPORT_PATH differs from the validated allow-list (actual '$actual_qml', expected '$qml_import_path')"
[[ "$actual_plugins" == "$package_plugins" ]] \
    || fail "running LATTE_EXTRA_PLUGIN_PATHS differs from the installed plugin root (actual '$actual_plugins', expected '$package_plugins')"
process_env="$(tr '\0' '\n' <"/proc/$dock_pid/environ")" \
    || fail "cannot read /proc/$dock_pid/environ while checking forbidden variables"
for forbidden in QML_IMPORT_PATH NIXPKGS_QT6_QML_IMPORT_PATH NIXPKGS_QML_SEARCH_PATHS QT_PLUGIN_PATH \
        "${latte_package_gate_loader_variables[@]}"; do
    [[ "$process_env" != *$'\n'"$forbidden="* && "$process_env" != "$forbidden="* ]] \
        || fail "forbidden ambient variable $forbidden leaked into the installed dock"
done

core_plugin_resolved="$(realpath "$core_plugin" 2>/dev/null)" \
    || fail "cannot resolve installed core QML plugin for mapping validation"
containment_plugin_resolved="$(realpath "$containment_plugin" 2>/dev/null)" \
    || fail "cannot resolve installed containment QML plugin for mapping validation"
tasks_plugin_resolved="$(realpath "$tasks_plugin" 2>/dev/null)" \
    || fail "cannot resolve installed tasks QML plugin for mapping validation"
action_plugin_resolved="$(realpath "$action_plugin" 2>/dev/null)" \
    || fail "cannot resolve installed containment-actions plugin for mapping validation"
indicator_package_plugin_resolved="$(realpath "$indicator_package_plugin" 2>/dev/null)" \
    || fail "cannot resolve installed indicator package-structure plugin for mapping validation"
declare -A expected_mapped_artifacts=(
    [latte-dock]="$binary"
    [liblattecoreplugin.so]="$core_plugin_resolved"
    [liblattecontainmentplugin.so]="$containment_plugin_resolved"
    [liblattetasksplugin.so]="$tasks_plugin_resolved"
    [org.kde.latte.contextmenu.so]="$action_plugin_resolved"
    [latte_indicator.so]="$indicator_package_plugin_resolved"
)
required_mapped_artifacts=(
    latte-dock
    liblattecoreplugin.so
    liblattecontainmentplugin.so
    liblattetasksplugin.so
    org.kde.latte.contextmenu.so
)
# latte_indicator.so is a KPackage structure used while opening/installing
# indicator packages, not by normal dock startup. Its applicable runtime
# contract is the bounded metadata/type/dlopen validation above. Keeping it in
# expected_mapped_artifacts still rejects a foreign copy if startup ever maps it.
latte_package_gate_audit_mapped_paths "/proc/$dock_pid/maps" "$artifact_prefix" "$repo" \
    expected_mapped_artifacts required_mapped_artifacts

kill -TERM -- "-$dock_pid"
if latte_package_gate_wait_until_process_group_exits "$dock_pid" 125 0.2; then
    :
else
    group_wait_status=$?
    if [[ "$group_wait_status" -eq 1 ]]; then
        fail "installed dock process group $dock_pid survived SIGTERM for 25 seconds"
    fi
    fail "cannot determine whether installed dock process group $dock_pid exited after SIGTERM"
fi
# KSignalHandler turns SIGTERM into qGuiApp->quit(), so a clean installed
# shutdown returns from the event loop with status zero rather than 143.
latte_package_gate_require_exit_status "$dock_pid" 0 "installed dock after SIGTERM"
dock_pid=""

echo "installed-package-gate: PASS (installed executable, QML plugins, data, startup, and shutdown verified)"
