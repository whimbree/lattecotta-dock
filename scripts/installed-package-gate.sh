#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Validate and smoke-test one explicitly installed Latte package. Native
# package recipes own package-manager installation; this script consumes the
# resulting filesystem root and prefix without consulting PATH, restaging the
# build, or falling back to another Latte installation.
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"

usage() {
    cat <<'EOF'
Usage: scripts/installed-package-gate.sh --root ROOT [--prefix PREFIX] [--check-only]

  --root ROOT      Package filesystem root. Use / after installing the native
                   package in a clean container, or a package staging root.
  --prefix PREFIX  Absolute install prefix inside ROOT (default: /usr).
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

path_is_within() {
    local path="$1" base="$2"
    [[ "$base" == / || "$path" == "$base" || "$path" == "$base/"* ]]
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

require_package_file() {
    local label="$1" path="$2" resolved
    require_file "$label" "$path"
    resolved="$(resolve_native_path "installed $label" "$path")"
    path_is_within "$resolved" "$artifact_prefix" \
        || fail "installed $label resolves outside the package prefix: $resolved"
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
[[ "$prefix" != *'/../'* && "$prefix" != */.. && "$prefix" != *'/./'* ]] \
    || fail "--prefix must not contain . or .. components: $prefix"

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

binary_path="$artifact_prefix/bin/latte-dock"
[[ -x "$binary_path" ]] \
    || fail "installed binary is missing or not executable at $binary_path; PATH fallback is forbidden"
binary="$(resolve_native_path "installed binary" "$binary_path")"
path_is_within "$binary" "$artifact_prefix" \
    || fail "installed binary resolves outside the package prefix: $binary"

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

mapfile -d '' -t qml_manifests < <(
    find "${library_roots[@]}" -type f -path '*/org/kde/latte/core/qmldir' -print0
)
qml_manifest="$(require_one_match "org.kde.latte.core/qmldir" "${qml_manifests[@]}")"
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

mapfile -d '' -t action_plugins < <(
    find "${library_roots[@]}" -type f \
        -path '*/plasma/containmentactions/org.kde.latte.contextmenu.so' -print0
)
action_plugin="$(require_one_match "Latte containment-actions plugin" "${action_plugins[@]}")"
require_package_file "Latte containment-actions plugin" "$action_plugin"
package_plugins="${action_plugin%/plasma/containmentactions/org.kde.latte.contextmenu.so}"
package_plugins="$(resolve_native_path "installed Latte plugin root" "$package_plugins")"
path_is_within "$package_plugins" "$artifact_prefix" \
    || fail "installed Latte plugin root escapes the package prefix: $package_plugins"

package_data="$(resolve_native_path "installed Latte data root" "$artifact_prefix/share")"
path_is_within "$package_data" "$artifact_prefix" \
    || fail "installed Latte data root escapes the package prefix: $package_data"
require_package_file "shell package metadata" "$package_data/plasma/shells/org.kde.latte.shell/metadata.json"
require_package_file "containment package metadata" "$package_data/plasma/plasmoids/org.kde.latte.containment/metadata.json"
require_package_file "tasks applet package metadata" "$package_data/plasma/plasmoids/org.kde.latte.plasmoid/metadata.json"
require_package_file "desktop entry" "$package_data/applications/org.kde.latte-dock.desktop"
[[ -d "$package_data/latte/indicators/default" ]] \
    || fail "package is incomplete: missing default indicator under $package_data/latte/indicators"

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

for command in kwin_wayland dbus-run-session busctl setsid; do
    command -v "$command" >/dev/null 2>&1 \
        || fail "required runtime command '$command' is missing; install the package-gate dependencies"
done

source "$repo/scripts/lib-nested-kwin.sh"

dock_pid=""
cleanup() {
    set +e
    if [[ -n "$dock_pid" ]] && kill -0 "$dock_pid" 2>/dev/null; then
        kill -TERM "$dock_pid" 2>/dev/null
        wait "$dock_pid" 2>/dev/null
    fi
    nested_kwin_cleanup
}
trap cleanup EXIT INT TERM

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
    -u LD_LIBRARY_PATH -u LD_PRELOAD \
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
    state="$(busctl --user call org.kde.lattedock /Latte org.kde.LatteDock lifecycleState 2>/dev/null || true)"
    views="$(busctl --user call org.kde.lattedock /Latte org.kde.LatteDock viewsData 2>/dev/null || true)"
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

process_env="$(tr '\0' '\n' <"/proc/$dock_pid/environ")"
actual_qml="$(awk -F= '$1 == "QML2_IMPORT_PATH" {sub(/^[^=]*=/, ""); print; exit}' <<<"$process_env")"
actual_plugins="$(awk -F= '$1 == "LATTE_EXTRA_PLUGIN_PATHS" {sub(/^[^=]*=/, ""); print; exit}' <<<"$process_env")"
[[ "$actual_qml" == "$qml_import_path" ]] \
    || fail "running QML2_IMPORT_PATH differs from the validated allow-list (actual '$actual_qml', expected '$qml_import_path')"
[[ "$actual_plugins" == "$package_plugins" ]] \
    || fail "running LATTE_EXTRA_PLUGIN_PATHS differs from the installed plugin root (actual '$actual_plugins', expected '$package_plugins')"
for forbidden in QML_IMPORT_PATH NIXPKGS_QT6_QML_IMPORT_PATH NIXPKGS_QML_SEARCH_PATHS QT_PLUGIN_PATH LD_LIBRARY_PATH LD_PRELOAD; do
    [[ "$process_env" != *$'\n'"$forbidden="* && "$process_env" != "$forbidden="* ]] \
        || fail "forbidden ambient variable $forbidden leaked into the installed dock"
done

audit_mapped_plugin() {
    local name="$1" expected="$2" expected_resolved mapped_resolved
    expected_resolved="$(realpath "$expected")"
    mapfile -t mapped_paths < <(
        awk -v suffix="/$name" \
            'length($NF) >= length(suffix) && substr($NF, length($NF) - length(suffix) + 1) == suffix {print $NF}' \
            "/proc/$dock_pid/maps" | sort -u
    )
    [[ "${#mapped_paths[@]}" -gt 0 ]] \
        || fail "$name is not mapped by the settled dock; the installed QML module did not load"
    for mapped in "${mapped_paths[@]}"; do
        mapped_resolved="$(realpath "$mapped" 2>/dev/null)" \
            || fail "cannot resolve mapped $name path: $mapped"
        [[ "$mapped_resolved" == "$expected_resolved" ]] \
            || fail "$name mapped from $mapped_resolved, expected installed artifact $expected_resolved"
    done
    echo "installed-package-gate: mapped plugin verified: $expected_resolved"
}

audit_mapped_plugin liblattecoreplugin.so "$core_plugin"
audit_mapped_plugin liblattecontainmentplugin.so "$containment_plugin"
audit_mapped_plugin liblattetasksplugin.so "$tasks_plugin"

kill -TERM "$dock_pid"
exited=0
for ((i = 0; i < 125; i++)); do
    if ! kill -0 "$dock_pid" 2>/dev/null; then
        exited=1
        break
    fi
    sleep 0.2
done
[[ "$exited" == 1 ]] \
    || fail "installed dock (pid $dock_pid) survived SIGTERM for 25 seconds"
wait "$dock_pid" 2>/dev/null || true
dock_pid=""

cleanup
trap - EXIT INT TERM
echo "installed-package-gate: PASS (installed executable, QML plugins, data, startup, and shutdown verified)"
