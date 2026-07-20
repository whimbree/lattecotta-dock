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

_latte_package_gate_qt_plugin_info_is_qt6() {
    local tool="$1" version_output
    version_output="$("$tool" --version 2>&1)" || return 1
    [[ "$version_output" =~ (^|[^0-9])6\.[0-9]+(\.[0-9]+)?([^0-9]|$) ]]
}

latte_package_gate_choose_qt6_plugin_info() {
    local output_name="$1" candidate
    shift
    local -n output_path="$output_name"
    output_path=""
    for candidate in "$@"; do
        if _latte_package_gate_qt_plugin_info_is_qt6 "$candidate"; then
            output_path="$candidate"
            return 0
        fi
    done
    return 2
}

latte_package_gate_find_qt6_plugin_info() {
    local output_name="$1" name candidate
    local -a candidates=()
    local -A seen=()

    # Distro-specific names cannot be shadowed by an unsuffixed Qt 5 tool.
    for name in qtplugininfo6 qtplugininfo-qt6; do
        if candidate="$(command -v "$name" 2>/dev/null)"; then
            [[ -n "${seen[$candidate]+present}" ]] || candidates+=("$candidate")
            seen["$candidate"]=1
        fi
    done
    for candidate in /usr/lib/qt6/bin/qtplugininfo /usr/lib64/qt6/bin/qtplugininfo; do
        if [[ -x "$candidate" ]]; then
            [[ -n "${seen[$candidate]+present}" ]] || candidates+=("$candidate")
            seen["$candidate"]=1
        fi
    done
    if candidate="$(command -v qtplugininfo 2>/dev/null)"; then
        [[ -n "${seen[$candidate]+present}" ]] || candidates+=("$candidate")
    fi

    latte_package_gate_choose_qt6_plugin_info "$output_name" "${candidates[@]}"
}

latte_package_gate_read_elf_search_paths() {
    local file="$1" output_name="$2" readelf_output parsed_output path
    local -n output_paths="$output_name"
    output_paths=()

    if ! readelf_output="$(LC_ALL=C readelf -d -- "$file" 2>/dev/null)"; then
        echo "installed-package-gate: FAIL: readelf could not inspect dynamic metadata for $file" >&2
        return 2
    fi
    if ! parsed_output="$(awk '
        /\((RPATH|RUNPATH)\)/ {
            value = $0
            sub(/^[^[]*\[/, "", value)
            sub(/\].*$/, "", value)
            print value
        }
    ' <<<"$readelf_output")"; then
        echo "installed-package-gate: FAIL: awk could not parse dynamic metadata for $file" >&2
        return 2
    fi
    [[ -n "$parsed_output" ]] || return 0
    while IFS= read -r path; do
        output_paths+=("$path")
    done <<<"$parsed_output"
}

# /proc/<pid>/maps has five whitespace-delimited fields before the optional
# pathname. Strip only those fields so spaces inside the pathname survive.
latte_package_gate_read_mapped_paths() {
    local maps_file="$1" output_name="$2" parsed_output path
    local -n output_paths="$output_name"
    output_paths=()

    if ! parsed_output="$(awk '
        {
            path = $0
            if (sub(/^[^[:space:]]+[[:space:]]+[^[:space:]]+[[:space:]]+[^[:space:]]+[[:space:]]+[^[:space:]]+[[:space:]]+[^[:space:]]+[[:space:]]+/, "", path) && substr(path, 1, 1) == "/") {
                print path
            }
        }
    ' "$maps_file")"; then
        echo "installed-package-gate: FAIL: cannot parse process mappings from $maps_file" >&2
        return 2
    fi
    [[ -n "$parsed_output" ]] || return 0
    while IFS= read -r path; do
        output_paths+=("$path")
    done <<<"$parsed_output"
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
    local -A examined=() seen=()
    local -a mapped_paths=()

    latte_package_gate_read_mapped_paths "$maps_file" mapped_paths || return 2
    for mapped in "${mapped_paths[@]}"; do
        [[ -z "${examined[$mapped]+present}" ]] || continue
        examined["$mapped"]=1
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
        if [[ -z "${expected_paths[$name]+present}" ]]; then
            echo "installed-package-gate: FAIL: unexpected Latte runtime is mapped: $resolved" >&2
            return 2
        fi
        if [[ "$resolved" != "${expected_paths[$name]}" ]]; then
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

latte_package_gate_read_environment_value() {
    local environment_file="$1" variable="$2" output_name="$3" parsed_value
    local -n output_value="$output_name"

    if ! parsed_value="$(tr '\0' '\n' <"$environment_file")"; then
        echo "installed-package-gate: FAIL: cannot read process environment from $environment_file" >&2
        return 2
    fi
    if ! output_value="$(awk -F= -v variable="$variable" '
        $1 == variable {
            sub(/^[^=]*=/, "")
            print
            found = 1
            exit
        }
        END { if (!found) exit 3 }
    ' <<<"$parsed_value")"; then
        echo "installed-package-gate: FAIL: process environment has no $variable entry" >&2
        return 2
    fi
}

latte_package_gate_wait_until_process_exits() {
    local pid="$1" attempts="$2" delay="$3" attempt
    for ((attempt = 0; attempt < attempts; attempt++)); do
        kill -0 "$pid" 2>/dev/null || return 0
        sleep "$delay"
    done
    ! kill -0 "$pid" 2>/dev/null
}

latte_package_gate_require_exit_status() {
    local pid="$1" expected_status="$2" label="$3" actual_status
    if wait "$pid"; then
        actual_status=0
    else
        actual_status=$?
    fi
    [[ "$actual_status" -eq "$expected_status" ]] || {
        echo "installed-package-gate: FAIL: $label exited with status $actual_status, expected $expected_status" >&2
        return 2
    }
}

latte_package_gate_process_group_has_live_members() {
    local process_group="$1" pgrep_output pgrep_status pid stat_line stat_tail state

    if pgrep_output="$(pgrep -g "$process_group" 2>&1)"; then
        [[ -n "$pgrep_output" ]] || {
            echo "installed-package-gate: FAIL: pgrep returned success without members for process group $process_group" >&2
            return 2
        }
    else
        pgrep_status=$?
        [[ "$pgrep_status" -eq 1 ]] && return 1
        echo "installed-package-gate: FAIL: pgrep failed while polling process group $process_group with status $pgrep_status${pgrep_output:+: $pgrep_output}" >&2
        return 2
    fi

    while IFS= read -r pid; do
        [[ "$pid" =~ ^[0-9]+$ ]] || {
            echo "installed-package-gate: FAIL: pgrep returned an invalid pid while polling process group $process_group: $pid" >&2
            return 2
        }
        if ! IFS= read -r stat_line <"/proc/$pid/stat"; then
            # A member can disappear between pgrep and the procfs read.
            [[ ! -d "/proc/$pid" ]] && continue
            echo "installed-package-gate: FAIL: cannot read state for process-group member $pid" >&2
            return 2
        fi
        stat_tail="${stat_line##*) }"
        state="${stat_tail%% *}"
        case "$state" in
            Z|X) ;;
            [A-Z]) return 0 ;;
            *)
                echo "installed-package-gate: FAIL: cannot parse state for process-group member $pid" >&2
                return 2
                ;;
        esac
    done <<<"$pgrep_output"
    return 1
}

latte_package_gate_wait_until_process_group_exits() {
    local process_group="$1" attempts="$2" delay="$3" attempt poll_status
    for ((attempt = 0; attempt < attempts; attempt++)); do
        if latte_package_gate_process_group_has_live_members "$process_group"; then
            :
        else
            poll_status=$?
            [[ "$poll_status" -eq 1 ]] && return 0
            return 2
        fi
        sleep "$delay"
    done
    if latte_package_gate_process_group_has_live_members "$process_group"; then
        return 1
    else
        poll_status=$?
        [[ "$poll_status" -eq 1 ]] && return 0
        return 2
    fi
}

latte_package_gate_stop_process_group() {
    local process_group="$1" label="$2"
    local term_attempts="${3:-25}" term_delay="${4:-0.2}"
    local kill_attempts="${5:-25}" kill_delay="${6:-0.2}"
    local poll_status

    if latte_package_gate_process_group_has_live_members "$process_group"; then
        :
    else
        poll_status=$?
        [[ "$poll_status" -eq 1 ]] || return 2
        wait "$process_group" 2>/dev/null || true
        return 0
    fi
    kill -TERM -- "-$process_group" 2>/dev/null || kill -TERM "$process_group" 2>/dev/null || true
    if latte_package_gate_wait_until_process_group_exits "$process_group" "$term_attempts" "$term_delay"; then
        wait "$process_group" 2>/dev/null || true
        return 0
    else
        poll_status=$?
        [[ "$poll_status" -eq 1 ]] || return 2
    fi

    echo "installed-package-gate: cleanup: $label survived SIGTERM; sending SIGKILL" >&2
    kill -KILL -- "-$process_group" 2>/dev/null || kill -KILL "$process_group" 2>/dev/null || true
    if latte_package_gate_wait_until_process_group_exits "$process_group" "$kill_attempts" "$kill_delay"; then
        wait "$process_group" 2>/dev/null || true
        return 0
    else
        poll_status=$?
        if [[ "$poll_status" -eq 1 ]]; then
            echo "installed-package-gate: cleanup: $label still exists after bounded SIGKILL wait" >&2
        fi
        return 2
    fi
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
