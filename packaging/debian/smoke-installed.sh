#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Package-specific smoke test for the installed /usr/bin/latte-dock binary.
# This is the installation equivalent of tests/e2e/000-smoke.sh: it starts the
# packaged binary in a throwaway nested kwin_wayland + private D-Bus session,
# waits for the dock to reach running state and settle its views, and asserts
# a clean SIGTERM exit. An optional .deb path can be passed so a fresh CI layer
# installs the artifact it just produced before exercising it.
set -uo pipefail

bin="/usr/bin/latte-dock"
repo="$(cd "$(dirname "$0")/../.." && pwd)"
export E2E_REPO="$repo"

source "$repo/scripts/lib-nested-kwin.sh"
source "$repo/tests/e2e/lib.sh"

fail() { echo "SMOKE FAIL: $*" >&2; exit 1; }

usage() { echo "usage: $0 [path/to/latte-dock_<version>_<arch>.deb]"; exit 2; }

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then usage; fi
if [[ $# -gt 1 ]]; then usage; fi

if [[ -n "${1:-}" ]]; then
    deb="$1"
    [[ -f "$deb" ]] || fail ".deb not found: $deb"
    if dpkg -s latte-dock >/dev/null 2>&1; then
        echo "latte-dock already installed; skipping package install"
    else
        echo "installing $deb ..."
        apt-get update
        apt-get install -y --no-install-recommends "$deb" || fail "dpkg install of $deb failed"
    fi
fi

[[ -x "$bin" ]] || fail "no installed binary at $bin"

rt="$(mktemp -d /tmp/latte-smoke-installed.XXXXXX)"
chmod 700 "$rt"
docklog="$rt/dock.log"
confighome="$rt/config"
mkdir -p "$confighome"

cleanup() {
    local p
    if [[ -f "$rt/dock.pid" ]]; then
        p="$(cat "$rt/dock.pid" 2>/dev/null || true)"
        if [[ -n "$p" ]] && kill -0 "$p" 2>/dev/null; then
            kill -TERM "$p" 2>/dev/null || true
            wait "$p" 2>/dev/null || true
        fi
    fi
    nested_kwin_cleanup 2>/dev/null || true
    rm -rf "$rt"
}
trap 'cleanup; exit 130' INT
trap 'cleanup; exit 143' TERM
trap cleanup EXIT

nested_kwin_prepare
mkdir -p "$NESTED_RT/kwin-config" "$NESTED_RT/kwin-cache"
nested_kwin_env+=(
    WAYLAND_DISPLAY=latte-smoke-wl
    XDG_CONFIG_HOME="$NESTED_RT/kwin-config"
    XDG_CACHE_HOME="$NESTED_RT/kwin-cache"
    QT_FORCE_STDERR_LOGGING=1
)
nested_kwin_start 1600 1000 latte-smoke-wl || fail "nested kwin_wayland failed to start"

export XDG_RUNTIME_DIR="$NESTED_RT"
export WAYLAND_DISPLAY="$NESTED_SOCK"
export DBUS_SESSION_BUS_ADDRESS="$NESTED_BUS"
export XDG_CONFIG_HOME="$confighome"
export QT_QPA_PLATFORM=wayland
unset DISPLAY XAUTHORITY

echo "starting installed $bin ..."
setsid env QT_FORCE_STDERR_LOGGING=1 "$bin" -d >"$docklog" 2>&1 &
dockpid=$!
echo "$dockpid" > "$rt/dock.pid"

if ! e2e_wait_running 90; then
    tail -50 "$docklog" >&2 || true
    fail "installed binary did not reach lifecycleState running"
fi
echo "installed binary reached running state"

if ! e2e_wait_settled 90; then
    tail -50 "$docklog" >&2 || true
    fail "installed binary views did not settle"
fi

view_count="$(e2e_json viewsData | python3 -c 'import json,sys; print(len(json.load(sys.stdin)))')"
if [[ -z "$view_count" || "$view_count" == "0" ]]; then
    fail "no views reported after settling"
fi
echo "$view_count view(s) settled"

# clean SIGTERM
kill -TERM "$dockpid"
clean=0
for ((i = 0; i < 125; i++)); do
    if ! kill -0 "$dockpid" 2>/dev/null; then
        clean=1
        break
    fi
    sleep 0.2
done
if [[ "$clean" != "1" ]]; then
    fail "installed binary survived SIGTERM for 25s"
fi
wait "$dockpid" 2>/dev/null || true
echo "clean SIGTERM exit"

echo "SMOKE PASS: installed /usr/bin/latte-dock started, settled $view_count view(s), and exited cleanly"
