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
mkdir -p "$fake_bin" "$scratch/build one"

printf '#!/usr/bin/env bash\nprintf "called\\n" >>"%s"\n' "$invocation_log" >"$fake_bin/kbuildsycoca6"
chmod +x "$fake_bin/kbuildsycoca6"

readonly first_binary="$scratch/build one/latte-dock"
readonly second_binary="$scratch/latte-dock-second"
readonly percent_binary="$scratch/latte%-dock"
printf '#!/usr/bin/env bash\n' >"$first_binary"
printf '#!/usr/bin/env bash\n' >"$second_binary"
printf '#!/usr/bin/env bash\n' >"$percent_binary"
chmod +x "$first_binary" "$second_binary" "$percent_binary"

run_helper() {
    PATH="$fake_bin:$PATH" XDG_DATA_HOME="$data_home" \
        "$repo/scripts/ensure-dev-wayland-interfaces.sh" "$1"
}

run_helper "$first_binary"
readonly desktop_entry="$data_home/applications/org.kde.latte-dock.current-dev.desktop"

grep -Fqx 'X-KDE-Wayland-Interfaces=org_kde_plasma_window_management,org_kde_kwin_keystate,zkde_screencast_unstable_v1' "$desktop_entry"
grep -Fqx "Exec=\"$first_binary\"" "$desktop_entry"
[[ "$(wc -l <"$invocation_log")" -eq 1 ]]

# An unchanged entry still refreshes KService so a prior cache failure cannot
# silently launch a dock without the window-management protocol.
run_helper "$first_binary"
[[ "$(wc -l <"$invocation_log")" -eq 2 ]]

run_helper "$second_binary"
grep -Fqx "Exec=\"$second_binary\"" "$desktop_entry"
! grep -Fq "$first_binary" "$desktop_entry"
[[ "$(wc -l <"$invocation_log")" -eq 3 ]]

if run_helper "$percent_binary" 2>/dev/null; then
    echo "percent-containing executable path was accepted" >&2
    exit 1
fi
grep -Fqx "Exec=\"$second_binary\"" "$desktop_entry"
[[ "$(wc -l <"$invocation_log")" -eq 3 ]]

if run_helper "$scratch/missing-latte-dock" 2>/dev/null; then
    echo "missing development binary was accepted" >&2
    exit 1
fi
grep -Fqx "Exec=\"$second_binary\"" "$desktop_entry"
[[ "$(wc -l <"$invocation_log")" -eq 3 ]]

# Exercise the common real-config restart boundary with a BUILD override. Fake
# process tools keep the test isolated from any dock running on the host.
readonly fixture_repo="$scratch/fixture-repo"
readonly override_build="$fixture_repo/override-build"
readonly selected_binary_log="$scratch/selected-binary"
readonly process_probe_log="$scratch/process-probe"
mkdir -p "$fixture_repo/scripts" "$fixture_repo/app" "$fixture_repo/build" "$override_build/bin"
cp "$repo/scripts/restart-staged.sh" "$repo/scripts/ensure-dev-wayland-interfaces.sh" "$fixture_repo/scripts/"
cp "$repo/app/org.kde.latte-dock.desktop.cmake" "$fixture_repo/app/"
printf ':\n' >"$fixture_repo/build/_devshell.env"
printf '#!/usr/bin/env bash\n' >"$override_build/bin/latte-dock"
chmod +x "$override_build/bin/latte-dock"

printf '#!/usr/bin/env bash\nprintf "called\\n" >>"%s"\nexit 1\n' "$process_probe_log" >"$fake_bin/pgrep"
printf '#!/usr/bin/env bash\nprintf "%%s\\n" "$*" >"%s"\n' "$selected_binary_log" >"$fake_bin/setsid"
chmod +x "$fake_bin/pgrep" "$fake_bin/setsid"

PATH="$fake_bin:$PATH" XDG_DATA_HOME="$data_home" BUILD="$override_build" \
    "$fixture_repo/scripts/restart-staged.sh" --user-config
grep -Fqx "Exec=\"$override_build/bin/latte-dock\"" "$desktop_entry"
grep -Fqx "bash -c source \"\$1\"; shift; exec \"\$@\" _ $fixture_repo/build/_devshell.env $fixture_repo/scripts/run-staged.sh --user-config" "$selected_binary_log"
[[ "$(wc -l <"$invocation_log")" -eq 4 ]]
readonly process_probe_count="$(wc -l <"$process_probe_log")"

# A syntactically present but insufficient interface list must fail before it
# can replace the valid entry, refresh KService, or enter the process lifecycle.
printf 'X-KDE-Wayland-Interfaces=org_kde_kwin_keystate\n' >"$fixture_repo/app/org.kde.latte-dock.desktop.cmake"
if PATH="$fake_bin:$PATH" XDG_DATA_HOME="$data_home" BUILD="$override_build" \
    "$fixture_repo/scripts/restart-staged.sh" --user-config 2>/dev/null; then
    echo "desktop template without window-management authority was accepted" >&2
    exit 1
fi
grep -Fqx "Exec=\"$override_build/bin/latte-dock\"" "$desktop_entry"
[[ "$(wc -l <"$invocation_log")" -eq 4 ]]
[[ "$(wc -l <"$process_probe_log")" -eq "$process_probe_count" ]]
