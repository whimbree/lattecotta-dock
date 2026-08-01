#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# KWin exposes privileged Wayland protocols only to executables named by a
# matching desktop entry. Development builds move between worktrees, so the
# installed package entry cannot authorize the binary that start-dock.sh runs.
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
readonly script_dir
repo="$(cd "$script_dir/.." && pwd)"
readonly repo
requested_binary="${1:?usage: ensure-dev-wayland-interfaces.sh BINARY}"
readonly requested_binary

if [[ ! -x "$requested_binary" ]]; then
    echo "Lattecotta development binary is not executable: $requested_binary" >&2
    exit 2
fi

binary="$(realpath -e "$requested_binary")"
readonly binary
readonly desktop_template="$repo/app/org.kde.latte-dock.desktop.cmake"
readonly data_home="${XDG_DATA_HOME:-${HOME:?HOME is required when XDG_DATA_HOME is unset}/.local/share}"
readonly applications_dir="$data_home/applications"
readonly desktop_entry="$applications_dir/org.kde.latte-dock.current-dev.desktop"

if [[ "$binary" == *$'\n'* || "$binary" == *$'\r'* ]]; then
    echo "Lattecotta development binary path contains a desktop-entry line break" >&2
    exit 2
fi

wayland_interfaces="$(sed -n 's/^X-KDE-Wayland-Interfaces=//p' "$desktop_template")"
readonly wayland_interfaces
if [[ -z "$wayland_interfaces" || "$(grep -c '^X-KDE-Wayland-Interfaces=' "$desktop_template")" -ne 1 ]]; then
    echo "Lattecotta desktop template must declare exactly one Wayland interface list" >&2
    exit 2
fi

escape_exec_path() {
    local escaped="$1"
    escaped="${escaped//\\/\\\\}"
    escaped="${escaped//\"/\\\"}"
    escaped="${escaped//\`/\\\`}"
    escaped="${escaped//\$/\\$}"
    escaped="${escaped//\%/%%}"
    printf '"%s"' "$escaped"
}

mkdir -p "$applications_dir"
pending_entry="$(mktemp "$applications_dir/.org.kde.latte-dock.current-dev.desktop.XXXXXX")"
readonly pending_entry
trap 'rm -f "$pending_entry"' EXIT

printf '%s\n' \
    '[Desktop Entry]' \
    'Type=Application' \
    'Name=Lattecotta Current Development Build' \
    "Exec=$(escape_exec_path "$binary")" \
    'NoDisplay=true' \
    'StartupNotify=false' \
    'X-DBUS-ServiceName=org.kde.lattedock' \
    "X-KDE-Wayland-Interfaces=$wayland_interfaces" \
    >"$pending_entry"
chmod 0644 "$pending_entry"

if [[ ! -f "$desktop_entry" ]] || ! cmp -s "$pending_entry" "$desktop_entry"; then
    mv -f "$pending_entry" "$desktop_entry"
fi

# Run even when the entry is unchanged. A failed or externally rebuilt service
# cache must not leave a correct-looking file paired with missing KWin access.
kbuildsycoca6 >/dev/null
