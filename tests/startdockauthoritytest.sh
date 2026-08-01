#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
readonly repo
scratch="$(mktemp -d)"
readonly scratch
trap 'rm -rf "$scratch"' EXIT

readonly fake_bin="$scratch/bin"
readonly data_home="$scratch/data"
readonly invocation_log="$scratch/kbuildsycoca6.invocations"
mkdir -p "$fake_bin" "$scratch/build one%"

printf '#!/usr/bin/env bash\nprintf "called\\n" >>"%s"\n' "$invocation_log" >"$fake_bin/kbuildsycoca6"
chmod +x "$fake_bin/kbuildsycoca6"

readonly first_binary="$scratch/build one%/latte-dock"
readonly second_binary="$scratch/latte-dock-second"
printf '#!/usr/bin/env bash\n' >"$first_binary"
printf '#!/usr/bin/env bash\n' >"$second_binary"
chmod +x "$first_binary" "$second_binary"

run_helper() {
    PATH="$fake_bin:$PATH" XDG_DATA_HOME="$data_home" \
        "$repo/scripts/ensure-dev-wayland-interfaces.sh" "$1"
}

run_helper "$first_binary"
readonly desktop_entry="$data_home/applications/org.kde.latte-dock.current-dev.desktop"

grep -Fqx 'X-KDE-Wayland-Interfaces=org_kde_plasma_window_management,org_kde_kwin_keystate,zkde_screencast_unstable_v1' "$desktop_entry"
grep -Fqx "Exec=\"${first_binary//\%/%%}\"" "$desktop_entry"
[[ "$(wc -l <"$invocation_log")" -eq 1 ]]

# An unchanged entry still refreshes KService so a prior cache failure cannot
# silently launch a dock without the window-management protocol.
run_helper "$first_binary"
[[ "$(wc -l <"$invocation_log")" -eq 2 ]]

run_helper "$second_binary"
grep -Fqx "Exec=\"$second_binary\"" "$desktop_entry"
! grep -Fq "$first_binary" "$desktop_entry"
[[ "$(wc -l <"$invocation_log")" -eq 3 ]]

readonly launcher="$repo/scripts/start-dock.sh"
helper_line="$(grep -nF 'ensure-dev-wayland-interfaces.sh" "$repo/build/bin/latte-dock"' "$launcher" | cut -d: -f1)"
restart_line="$(grep -nF 'exec "$repo/scripts/restart-staged.sh"' "$launcher" | cut -d: -f1)"
readonly helper_line restart_line
[[ "$helper_line" -lt "$restart_line" ]]

if run_helper "$scratch/missing-latte-dock" 2>/dev/null; then
    echo "missing development binary was accepted" >&2
    exit 1
fi
grep -Fqx "Exec=\"$second_binary\"" "$desktop_entry"
[[ "$(wc -l <"$invocation_log")" -eq 3 ]]
