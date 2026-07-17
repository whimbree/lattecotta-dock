# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Shared nested kwin_wayland bring-up/teardown for the out-of-session
# harnesses: the sceneprobe render gate (tests/sceneprobe/run_in_kwin.sh)
# and the e2e recipe driver (scripts/run-e2e.sh). Extracted from
# run_in_kwin.sh so the two cannot drift apart on the hard-won cleanup
# discipline (process-group kill, FUSE unmount, X11 stripping - each
# comment below records the incident that earned it).
#
# Usage (bash, nounset-safe):
#   source scripts/lib-nested-kwin.sh
#   nested_kwin_prepare                     # creates the private runtime dir
#   nested_kwin_env+=(VAR=VALUE ...)        # optional extra env for the session
#   nested_kwin_start <width> <height> <socket>
#   trap nested_kwin_cleanup EXIT INT TERM  # caller owns the trap
#
# After nested_kwin_start:
#   NESTED_RT        private XDG_RUNTIME_DIR of the session
#   NESTED_SOCK      the wayland socket name (inside $NESTED_RT)
#   NESTED_KWIN_PID  the session leader pid (dbus-run-session's group)
#   NESTED_KWIN_LOG  kwin's captured stdout+stderr ($NESTED_RT/kwin.log)
#   NESTED_BUS       the session's private D-Bus address
#
# The whole nested session lives on ONE private dbus-run-session bus and
# NESTED_BUS hands that address to further clients. This is load-bearing
# for anything that mutates KWin over D-Bus: a probe on a different bus
# talks to a different (or no) KWin and every scripting call silently
# no-ops - the e2e vehicle's v2 prototype lost a day to exactly that.

# nested_kwin_prepare: create the private runtime dir, so callers can
# stage session-scoped state (config homes, logs) before kwin starts.
nested_kwin_prepare() {
    NESTED_RT="$(mktemp -d /tmp/nested-kwin-xdg.XXXXXX)"
    chmod 700 "$NESTED_RT"
    NESTED_KWIN_LOG="$NESTED_RT/kwin.log"
    nested_kwin_env=()
}

# nested_kwin_start <width> <height> <socket>: bring up the virtual
# compositor inside its own dbus-run-session and wait for its socket.
# Returns non-zero (after printing kwin's log) if the socket never appears.
nested_kwin_start() {
    local width="$1" height="$2" socket="$3"
    NESTED_SOCK="$socket"

    # DISPLAY/XAUTHORITY are STRIPPED for the whole nested session: nothing in
    # it needs the real X server, and leaving them inherited let every
    # dbus-activated service (xdg-desktop-portal, ksecretd - one set PER RUN)
    # open connections to the session Xwayland that never closed. A night of
    # runs saturated the X client limit (254/256, "Maximum number of clients
    # reached") and took down both the desk session's headroom and this gate.
    #
    # The sh -c wrapper publishes the private bus address to $NESTED_RT before
    # exec'ing kwin (pid and process shape unchanged), so callers can put more
    # clients on the same bus - see the one-bus note in the header.
    setsid env -u DISPLAY -u XAUTHORITY \
        XDG_RUNTIME_DIR="$NESTED_RT" KWIN_WAYLAND_NO_PERMISSION_CHECKS=1 \
        "${nested_kwin_env[@]}" \
        dbus-run-session -- sh -c \
        'printf %s "$DBUS_SESSION_BUS_ADDRESS" >"$1/bus-address"; exec kwin_wayland --virtual --width "$2" --height "$3" --no-lockscreen --socket "$4"' \
        sh "$NESTED_RT" "$width" "$height" "$socket" >"$NESTED_KWIN_LOG" 2>&1 &
    NESTED_KWIN_PID=$!

    local _i
    for _i in $(seq 1 150); do
        [ -S "$NESTED_RT/$NESTED_SOCK" ] && break
        kill -0 "$NESTED_KWIN_PID" 2>/dev/null || break
        sleep 0.1
    done
    if [ ! -S "$NESTED_RT/$NESTED_SOCK" ]; then
        echo "nested kwin_wayland never brought up its socket; its log:" >&2
        cat "$NESTED_KWIN_LOG" >&2
        return 2
    fi
    NESTED_BUS="$(cat "$NESTED_RT/bus-address")"
}

nested_kwin_cleanup() {
    # kill the whole process GROUP, not just dbus-run-session: killing the
    # wrapper pid alone orphans the kwin child often enough that a day of
    # runs left 315 virtual compositors alive (the setsid above gives the
    # session its own pgid so the negative-pid kill has a precise target)
    if [ -n "${NESTED_KWIN_PID:-}" ]; then
        kill -- "-$NESTED_KWIN_PID" 2>/dev/null || kill "$NESTED_KWIN_PID" 2>/dev/null
        wait "$NESTED_KWIN_PID" 2>/dev/null
        # ... and wait for the WHOLE group to be gone before removing its
        # runtime dir: waiting on the session leader alone races the members'
        # exit paths - kwin and kded flush their config on SIGTERM and
        # recreated XDG dirs inside $NESTED_RT after the rm below had run
        # (leftover /tmp/nested-kwin-xdg.* dirs holding a fresh
        # kwinoutputconfig.json, caught on the e2e driver's first day)
        local _i
        for _i in $(seq 1 50); do
            pgrep -g "$NESTED_KWIN_PID" >/dev/null 2>&1 || break
            sleep 0.1
        done
        if pgrep -g "$NESTED_KWIN_PID" >/dev/null 2>&1; then
            kill -KILL -- "-$NESTED_KWIN_PID" 2>/dev/null
            sleep 0.2
        fi
    fi
    # the xdg-desktop-portal the nested bus activates FUSE-mounts $RT/doc;
    # unmount before removing or the rm leaves the mountpoint behind
    [ -n "${NESTED_RT:-}" ] && {
        fusermount3 -u "$NESTED_RT/doc" 2>/dev/null || fusermount -u "$NESTED_RT/doc" 2>/dev/null || true
        rm -rf "$NESTED_RT" 2>/dev/null || true
        # contract check: nothing may outlive the session and recreate state
        # in here (seen once even after the group wait: a leftover dir with a
        # fresh kwinoutputconfig.json). If the dir reappears, NAME the writer
        # loudly - a silent survivor holds a live config path and pollutes
        # the next run's isolation - then remove again.
        sleep 0.3
        if [ -d "$NESTED_RT" ]; then
            echo "nested-kwin cleanup: $NESTED_RT was recreated after removal; survivors referencing it:" >&2
            pgrep -af "$NESTED_RT" >&2 || echo "  (none found by cmdline; an env-only reference)" >&2
            find "$NESTED_RT" -type f >&2 2>/dev/null || true
            rm -rf "$NESTED_RT" 2>/dev/null || true
        fi
    }
}
