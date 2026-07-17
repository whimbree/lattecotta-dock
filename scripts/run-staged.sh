#!/usr/bin/env bash
# THE ENVIRONMENT CORE, not the user entry point: stages QML, constructs
# the pinned environment (import paths, data dirs, plugin allow-list) and
# execs the dock IN THE FOREGROUND of the current session. It manages no
# other instance - deliberately, so harnesses can run it against a nested
# compositor or under a wrapper (gdb, timeout) without any risk of
# touching a dock running elsewhere. The file boundary is the safety
# boundary: the kill-and-detach lifecycle lives ONLY in restart-staged.sh
# (the desk entry point), and start-dock.sh is the daily-driver front
# door (restart-staged.sh --user-config).
#
# Uses a throwaway XDG_CONFIG_HOME by default so the user's real Latte and
# Plasma configuration is never touched; pass --user-config to use the real
# one deliberately (LATTE_CONFIG_HOME overrides the throwaway path;
# BUILD overrides the build tree - both used by the nested harness).
# Data dirs put the staged tree first so the staged shell
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

# The throwaway config dir needs the session's color scheme: without a
# kdeglobals the dock resolves the default LIGHT scheme, the bar renders
# white under a dark session, and themeExtended's dark/light palette
# variants degrade (Dark Colors then colorizes applets with a wrong
# applyColor - the black-silhouette report). Copy, do not link: throwaway
# runs must not write back into the real session config.
if [[ "$confighome" == "$build/_runconfig" && ! -f "$confighome/kdeglobals" && -f "${XDG_CONFIG_HOME:-$HOME/.config}/kdeglobals" ]]; then
    cp "${XDG_CONFIG_HOME:-$HOME/.config}/kdeglobals" "$confighome/kdeglobals"
fi

# flatten the -import list into a colon path for the app's engine
importpath=""
for ((i = 1; i < ${#imports[@]}; i += 2)); do
    importpath="${importpath:+$importpath:}${imports[$i]}"
done

export QML2_IMPORT_PATH="$importpath"
export XDG_CONFIG_HOME="$confighome"

# XDG_DATA_DIRS is rebuilt, never inherited wholesale: under `nix develop`
# the inherited value is the entire BUILD closure (~270 entries with
# duplicates - cmake, gettext, gstreamer, cups...). QStandardPaths::locate
# walks every entry per lookup, and KSvg's theme discovery issues them in
# storms: one preview adoption measured 23,255 stat() calls, 96% ENOENT,
# 20k of them for the theme 'colors' file alone - 100-400ms GUI stalls per
# adoption (2026-07-15 strace, full numbers in the PORTING_PLAN item).
# Allow-list the KDE runtime DATA dirs the dock actually reads
# (strace-derived), kept in devshell order so the pinned copies keep
# winning over the system profile exactly as they did before; the system
# profile and /usr/share back everything else. Per the regression rules
# this is an allow-list of leaves, not a shared root.
runtime_data_dirs="$stage/share"
if [[ -n "${XDG_DATA_DIRS:-}" ]]; then
    while IFS= read -r d; do
        case "$d" in
            #! store paths are single components (hash-name-version), so the
            #! package name must match as a substring, not a path component
            *libplasma-*/share|*plasma-workspace-*/share|*plasma-desktop-*/share|*-kwin-*/share|*-breeze-*/share|*kio-extras-*/share|*kcoreaddons-*/share|*kguiaddons-*/share|*-kirigami-*/share)
                runtime_data_dirs="$runtime_data_dirs:$d" ;;
        esac
    done < <(tr ':' '\n' <<<"$XDG_DATA_DIRS" | awk 'NF && !seen[$0]++')
fi
# $USER is not exported in a bare (non-login) shell such as a CI container,
# and this script runs under set -u; resolve it from the passwd db instead of
# assuming the env var. The nix-profile / per-user / current-system entries
# are NixOS-specific and simply absent (harmlessly skipped by QStandardPaths)
# on other distros, where /usr/share backs the icons/themes/plasma assets.
export XDG_DATA_DIRS="$runtime_data_dirs:$HOME/.nix-profile/share:/etc/profiles/per-user/${USER:-$(id -un)}/share:/run/current-system/sw/share:/usr/share"

export QT_QPA_PLATFORM=wayland

# The desktop session's QT_PLUGIN_PATH points at the system Plasma's plugins
# (a different Qt build); loading its platform-theme plugin into this
# process segfaults in QCoreApplication::init. The nix-built Qt finds its
# own plugins through baked-in paths, so drop the session's list and skip
# the platform theme integration entirely.
unset QT_PLUGIN_PATH

# Latte's OWN C++ plugins are staged, not system-installed - most
# importantly plasma/containmentactions/org.kde.latte.contextmenu, which
# builds the dock's right-click menu. With no system Latte install (the ng
# package removed) and no plugin path, findPluginById() cannot locate it
# and right-click falls through to the stock task menu. Hand the staged
# plugin tree over - it holds just Latte's plugins (containment actions,
# indicator loader), no platform/theme plugin, so no segfault risk.
#
# ... plus ONE allow-listed leaf: the kwindowsystem runtime plugin dir of
# the exact package the binary links (per the regression rule: specific
# leaves, never shared roots). Without it KWindowSystem has no wayland
# backend in this process, so KWindowShadow fails on every dialog (the
# "Couldn't create KWindowShadow" spam in every -d log) and
# KWindowEffects::slideWindow() is a silent no-op - applet popups never
# got the compositor slide-in. The dir ships only kwindowsystem's own
# platform plugins, nothing that can shadow modules we stage ourselves.
#
# Handed over as LATTE_EXTRA_PLUGIN_PATHS (main.cpp feeds it into the
# process-local QCoreApplication library paths), NOT as QT_PLUGIN_PATH:
# the dock's whole environment is forwarded verbatim to every app it
# launches (KIO's systemd runner copies it into the transient unit's
# Environment= property), and a child of a different Qt build dlopening
# our pinned kwindowsystem plugin is an ABI mismatch waiting to happen.
# A LATTE_-namespaced variable is inert for children.
export LATTE_EXTRA_PLUGIN_PATHS="$stage/lib/plugins"
kwspath=$(ldd "$build/bin/latte-dock" | perl -ne 'print "$1\n" if m{=> (/nix/store/[^/]+-kwindowsystem-[^/]+)/}' | head -1)
if [[ -n "$kwspath" && -d "$kwspath/lib/qt-6/plugins" ]]; then
    export LATTE_EXTRA_PLUGIN_PATHS="$LATTE_EXTRA_PLUGIN_PATHS:$kwspath/lib/qt-6/plugins"
else
    echo "WARNING: kwindowsystem plugin dir not found; dialog shadows and popup slide will be missing" >&2
fi
export QT_QPA_PLATFORMTHEME=

echo "config home: $confighome"
echo "starting staged latte-dock against WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-<unset>} ..."
# LATTE_RUN_WRAPPER="gdb -batch -ex run -ex bt --args" gives a backtrace run
exec ${LATTE_RUN_WRAPPER:-} "$build/bin/latte-dock" "$@"
