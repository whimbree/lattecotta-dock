#!/usr/bin/env bash
# Run the staged latte-dock against the live Wayland session (porting plan
# Phase 5 first-runnable milestone, later phases' live verification cadence).
#
# Uses a throwaway XDG_CONFIG_HOME by default so the user's real Latte and
# Plasma configuration is never touched; pass --user-config to use the real
# one deliberately. Data dirs put the staged tree first so the staged shell
# package, containment and indicators win, with the system dirs behind them
# for icons, themes and plasma assets.
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
source "$repo/scripts/lib-qml-env.sh"

qml_env_setup "$repo"
if [[ ! -x "$build/bin/latte-dock" ]]; then
    echo "no built binary at $build/bin/latte-dock"; exit 2
fi
# always re-stage so QML edits in the working tree reach the run
qml_env_stage

confighome="${LATTE_CONFIG_HOME:-$build/_runconfig}"
if [[ "${1:-}" == "--user-config" ]]; then
    confighome="${XDG_CONFIG_HOME:-$HOME/.config}"
    shift
fi
mkdir -p "$confighome"

# flatten the -import list into a colon path for the app's engine
importpath=""
for ((i = 1; i < ${#imports[@]}; i += 2)); do
    importpath="${importpath:+$importpath:}${imports[$i]}"
done

export QML2_IMPORT_PATH="$importpath"
export XDG_CONFIG_HOME="$confighome"
export XDG_DATA_DIRS="$stage/share:${XDG_DATA_DIRS:-/run/current-system/sw/share:/usr/share}"
export QT_QPA_PLATFORM=wayland

# The desktop session's QT_PLUGIN_PATH points at the system Plasma's plugins
# (a different Qt build); loading its platform-theme plugin into this
# process segfaults in QCoreApplication::init. The nix-built Qt finds its
# own plugins through baked-in paths, so drop the session's list and skip
# the platform theme integration entirely.
#
# But Latte's OWN C++ plugins are staged, not system-installed - most
# importantly plasma/containmentactions/org.kde.latte.contextmenu, which
# builds the dock's right-click menu. With no system Latte install (the ng
# package removed) and QT_PLUGIN_PATH empty, findPluginById() cannot locate
# it and right-click falls through to the stock task menu. Point the path at
# the staged plugin tree ONLY - it holds just Latte's plugins (containment
# actions, indicator loader), no platform/theme plugin, so no segfault risk.
export QT_PLUGIN_PATH="$stage/lib/plugins"
export QT_QPA_PLATFORMTHEME=

echo "config home: $confighome"
echo "starting staged latte-dock against WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-<unset>} ..."
# LATTE_RUN_WRAPPER="gdb -batch -ex run -ex bt --args" gives a backtrace run
exec ${LATTE_RUN_WRAPPER:-} "$build/bin/latte-dock" "$@"
