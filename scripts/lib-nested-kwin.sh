# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Shared nested kwin_wayland bring-up/teardown for the out-of-session
# harnesses: the sceneprobe render gate (tests/sceneprobe/run_in_kwin.sh),
# the e2e recipe driver (scripts/run-e2e.sh), the installed-package gate and
# the default-config seeder.
#
# THIN BRIDGE (BP-2a): the hard-won lifecycle (process-group kill, FUSE
# unmount, X11 stripping, the one private dbus-run-session bus) lives in the
# typed latte_harness.vehicle module now; each incident that earned a step is
# recorded there. This file keeps the SAME sourced interface the consumers rely
# on so they need no change.
#
# Usage (bash, nounset-safe):
#   source scripts/lib-nested-kwin.sh
#   nested_kwin_prepare                     # creates the private runtime dir
#   nested_kwin_env+=(VAR=VALUE ...)        # optional extra env for the session
#   nested_kwin_start <width> <height> <socket> [output-count]
#   trap nested_kwin_cleanup EXIT INT TERM  # caller owns the trap
#
# output-count defaults to 1 (a single <width>x<height> virtual output). >1 asks
# kwin_wayland for that many virtual outputs (--virtual --output-count N), which
# the multi-output e2e vehicle uses to prove per-screen view placement.
#
# After nested_kwin_start:
#   NESTED_RT        private XDG_RUNTIME_DIR of the session
#   NESTED_SOCK      the wayland socket name (inside $NESTED_RT)
#   NESTED_KWIN_PID  the session leader pid (== the process-group id)
#   NESTED_KWIN_STARTTIME  the leader's /proc starttime, the teardown identity
#                    gate's input (bridge-owned; no consumer reads it)
#   NESTED_KWIN_LOG  kwin's captured stdout+stderr ($NESTED_RT/kwin.log)
#   NESTED_BUS       the session's private D-Bus address
#
# The whole nested session lives on ONE private dbus-run-session bus and
# NESTED_BUS hands that address to further clients. This is load-bearing for
# anything that mutates KWin over D-Bus: a probe on a different bus talks to a
# different (or no) KWin and every scripting call silently no-ops.

# Resolve the harness from this file's own location so the bridge works from any
# consumer cwd (run-e2e sources it by $repo path, run_in_kwin.sh by a relative
# path). All consumers re-exec into the flake devShell before sourcing, so `uv`
# is on PATH, matching scripts/lib-qml-env.sh.
_NESTED_KWIN_HARNESS="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/harness"

# nested_kwin_prepare: create the private runtime dir and initialise the
# session env array, so callers can stage session-scoped state before kwin
# starts. Sets NESTED_RT and NESTED_KWIN_LOG via the module's emitted shell.
nested_kwin_prepare() {
    local _out
    _out="$(uv run --locked --project "$_NESTED_KWIN_HARNESS" python -m latte_harness.vehicle prepare)" || return
    eval "$_out"
    nested_kwin_env=()
}

# nested_kwin_start <width> <height> <socket> [output-count]: bring up the
# virtual compositor inside its own dbus-run-session and wait for its socket.
# Returns non-zero (after the module prints kwin's log) if the socket never
# appears. Sets NESTED_SOCK, NESTED_KWIN_PID, NESTED_KWIN_STARTTIME and
# NESTED_BUS on success.
nested_kwin_start() {
    local width="$1" height="$2" socket="$3" outputs="${4:-1}"
    local -a _env_args=()
    if ((${#nested_kwin_env[@]})); then
        _env_args=(--env "${nested_kwin_env[@]}")
    fi
    local _out
    _out="$(uv run --locked --project "$_NESTED_KWIN_HARNESS" python -m latte_harness.vehicle start \
        --runtime-dir "$NESTED_RT" --width "$width" --height "$height" \
        --socket "$socket" --outputs "$outputs" \
        ${_env_args[@]+"${_env_args[@]}"})" || return
    eval "$_out"
}

# nested_kwin_cleanup: the module's `stop` teardown, driven by the two live
# shell variables the bash version read - an empty NESTED_KWIN_PID skips the
# group kill (the installed-package gate clears it after stopping the group
# itself), an empty NESTED_RT skips the FUSE unmount and runtime-dir removal.
# Idempotent: a dead group and a missing dir succeed quietly.
nested_kwin_cleanup() {
    uv run --locked --project "$_NESTED_KWIN_HARNESS" python -m latte_harness.vehicle stop \
        --runtime-dir "${NESTED_RT:-}" --pgid "${NESTED_KWIN_PID:-}" \
        --starttime "${NESTED_KWIN_STARTTIME:-}"
}
