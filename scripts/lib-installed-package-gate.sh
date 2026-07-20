#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later

latte_package_gate_loader_variables=(
    LD_ASSUME_KERNEL
    LD_AUDIT
    LD_BIND_NOT
    LD_BIND_NOW
    LD_CONFIG_FILE
    LD_DEBUG
    LD_DEBUG_OUTPUT
    LD_DYNAMIC_WEAK
    LD_HWCAP_MASK
    LD_LIBRARY_PATH
    LD_ORIGIN_PATH
    LD_PRELOAD
    LD_PREFER_MAP_32BIT_EXEC
    LD_PROFILE
    LD_PROFILE_OUTPUT
    LD_SHOW_AUXV
    LD_TRACE_LOADED_OBJECTS
    LD_USE_LOAD_BIAS
    LD_VERBOSE
    LD_WARN
)

latte_package_gate_loader_unset_args=()
for latte_package_gate_loader_variable in "${latte_package_gate_loader_variables[@]}"; do
    latte_package_gate_loader_unset_args+=(-u "$latte_package_gate_loader_variable")
done
unset latte_package_gate_loader_variable

latte_package_gate_read_elf_search_paths() {
    local file="$1"
    LC_ALL=C readelf -d -- "$file" 2>/dev/null | awk '
        /\((RPATH|RUNPATH)\)/ {
            value = $0
            sub(/^[^[]*\[/, "", value)
            sub(/\].*$/, "", value)
            print value
        }
    '
}

# /proc/<pid>/maps has five whitespace-delimited fields before the optional
# pathname. Strip only those fields so spaces inside the pathname survive.
latte_package_gate_read_mapped_paths() {
    local maps_file="$1"
    awk '
        {
            path = $0
            if (sub(/^[^[:space:]]+[[:space:]]+[^[:space:]]+[[:space:]]+[^[:space:]]+[[:space:]]+[^[:space:]]+[[:space:]]+[^[:space:]]+[[:space:]]+/, "", path) && substr(path, 1, 1) == "/") {
                print path
            }
        }
    ' "$maps_file"
}

_latte_package_gate_path_is_within() {
    local path="$1" base="$2"
    [[ "$base" == / || "$path" == "$base" || "$path" == "$base/"* ]]
}

_latte_package_gate_is_latte_runtime_path() {
    local path="$1" name="${1##*/}"
    case "$name" in
        latte-dock|liblatte*.so*|latte_*.so*|org.kde.latte*.so*) return 0 ;;
    esac
    [[ "$path" == */org/kde/latte/* ]]
}

_latte_package_gate_development_provider() {
    local provider_dir="$1"
    [[ -d "$provider_dir" ]] || provider_dir="${provider_dir%/*}"
    while [[ -n "$provider_dir" && "$provider_dir" != / ]]; do
        if [[ -e "$provider_dir/.git" || -f "$provider_dir/CMakeLists.txt" ]]; then
            printf 'source tree\n'
            return 0
        fi
        if [[ -f "$provider_dir/CMakeCache.txt" || -d "$provider_dir/CMakeFiles" ]]; then
            printf 'CMake build tree\n'
            return 0
        fi
        provider_dir="${provider_dir%/*}"
        [[ -n "$provider_dir" ]] || provider_dir=/
    done
    return 1
}

latte_package_gate_audit_mapped_paths() {
    local maps_file="$1" artifact_prefix="$2" source_root="$3"
    local expected_name="$4" required_name="$5" mapped resolved name required provider
    local -n expected_paths="$expected_name"
    local -n required_paths="$required_name"
    local -A seen=()
    local -a mapped_paths=()

    mapfile -t mapped_paths < <(latte_package_gate_read_mapped_paths "$maps_file" | sort -u)
    for mapped in "${mapped_paths[@]}"; do
        case "$mapped" in
            /nix/store/*)
                echo "installed-package-gate: FAIL: running dock mapped a Nix artifact: $mapped" >&2
                return 2
                ;;
            "$source_root"|"$source_root/"*)
                echo "installed-package-gate: FAIL: running dock mapped the source/build tree: $mapped" >&2
                return 2
                ;;
            *'/_qmlstage'|*'/_qmlstage/'*)
                echo "installed-package-gate: FAIL: running dock mapped a development _qmlstage artifact: $mapped" >&2
                return 2
                ;;
        esac

        if provider="$(_latte_package_gate_development_provider "$mapped")"; then
            echo "installed-package-gate: FAIL: running dock mapped a $provider artifact: $mapped" >&2
            return 2
        fi

        _latte_package_gate_is_latte_runtime_path "$mapped" || continue
        resolved="$(realpath "$mapped" 2>/dev/null)" || {
            echo "installed-package-gate: FAIL: mapped Latte runtime cannot be resolved: $mapped" >&2
            return 2
        }
        _latte_package_gate_path_is_within "$resolved" "$artifact_prefix" || {
            echo "installed-package-gate: FAIL: mapped Latte runtime escapes the package prefix: $mapped -> $resolved" >&2
            return 2
        }
        name="${resolved##*/}"
        if [[ -n "${expected_paths[$name]+present}" && "$resolved" != "${expected_paths[$name]}" ]]; then
            echo "installed-package-gate: FAIL: $name mapped from $resolved, expected ${expected_paths[$name]}" >&2
            return 2
        fi
        seen["$name"]=1
    done

    for required in "${required_paths[@]}"; do
        [[ -n "${seen[$required]+present}" ]] || {
            echo "installed-package-gate: FAIL: required installed artifact $required is not mapped by the settled dock" >&2
            return 2
        }
        echo "installed-package-gate: mapped artifact verified: ${expected_paths[$required]}"
    done
}

latte_package_gate_wait_until_process_exits() {
    local pid="$1" attempts="$2" delay="$3" attempt
    for ((attempt = 0; attempt < attempts; attempt++)); do
        kill -0 "$pid" 2>/dev/null || return 0
        sleep "$delay"
    done
    ! kill -0 "$pid" 2>/dev/null
}

latte_package_gate_stop_process() {
    local pid="$1" label="$2"
    local term_attempts="${3:-25}" term_delay="${4:-0.2}"
    local kill_attempts="${5:-25}" kill_delay="${6:-0.2}"

    kill -0 "$pid" 2>/dev/null || return 0
    kill -TERM "$pid" 2>/dev/null || true
    if latte_package_gate_wait_until_process_exits "$pid" "$term_attempts" "$term_delay"; then
        wait "$pid" 2>/dev/null || true
        return 0
    fi

    echo "installed-package-gate: cleanup: $label survived SIGTERM; sending SIGKILL" >&2
    kill -KILL "$pid" 2>/dev/null || true
    if ! latte_package_gate_wait_until_process_exits "$pid" "$kill_attempts" "$kill_delay"; then
        echo "installed-package-gate: cleanup: $label still exists after bounded SIGKILL wait" >&2
        return 2
    fi
    wait "$pid" 2>/dev/null || true
}

_latte_package_gate_exit_with_cleanup() {
    local status=$? cleanup_status
    trap - EXIT INT TERM
    set +e
    "$LATTE_PACKAGE_GATE_EXIT_CLEANUP_CALLBACK"
    cleanup_status=$?
    if [[ "$status" -eq 0 && "$cleanup_status" -ne 0 ]]; then
        status="$cleanup_status"
    fi
    exit "$status"
}

latte_package_gate_install_exit_cleanup() {
    local callback="$1"
    declare -F "$callback" >/dev/null \
        || { echo "installed-package-gate: FAIL: cleanup callback is not a function: $callback" >&2; return 2; }
    LATTE_PACKAGE_GATE_EXIT_CLEANUP_CALLBACK="$callback"
    trap _latte_package_gate_exit_with_cleanup EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
}
