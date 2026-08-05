#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# THE ENVIRONMENT CORE, not the desk entry point: stages QML, constructs the
# pinned environment (import paths, data dirs, plugin allow-list) and execs the
# staged dock IN THE FOREGROUND of the current session. It manages no other
# instance - deliberately, so a harness can drive it against a nested compositor
# or under a wrapper (gdb, timeout) with no risk of touching a dock running
# elsewhere. The kill-and-detach lifecycle lives ONLY in restart-staged.sh (the
# desk entry point); start-dock.sh is the daily-driver front door.
#
# Ported to the typed harness in BP-2e (the bash-to-python migration's
# staged-run chunk): the env assembly and the exec now live in
# harness/src/latte_harness/staged_run.py (reusing latte_harness.qmlenv for the
# import-path doctrine, the D8/D271 leaf strip and the stage/restore manifest).
# This .sh stays as the stable entry point restart-staged.sh and the e2e/seed
# callers invoke, and as their argv contract - a throwaway config by default,
# --user-config for the real one:
#
#   scripts/run-staged.sh [--user-config] [dock args...]
#
# WHY the venv interpreter is exec'd directly, not `uv run`: run-staged.sh is
# exec'd under setsid by restart-staged, and the e2e/seed callers record its pid
# as THE DOCK PID before SIGTERMing it - both need the launcher pid to BE the
# dock's pid. `uv run` forks a wrapper child that would sit between that recorded
# pid and the binary; execing .venv/bin/python keeps ONE pid from run-staged.sh
# through python to latte-dock (the module then os.exec's the binary in place).
# `uv sync --locked` first so the venv is present and version-locked (fast when
# warm); its output goes to stderr so the dock's stdout stays clean.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"

# Self-heal into the flake devShell if the pinned toolchain is missing rather
# than dying with command-not-found from a bare shell (the BP shim guard):
# staged_run needs uv (to sync the venv) and LATTE_QML_MODULE_PATH (qmlenv
# refuses without it). restart-staged sources the devshell env cache before
# execing this, and the e2e/seed callers already run inside `nix develop`, so
# the hot paths carry both and never re-exec - only a bare manual invocation
# does. The re-exec inserts the nix process between the caller's recorded pid
# and the dock, so THIS PATH DOES NOT PRESERVE PID IDENTITY; interactive use
# only, never a pidfile-recording caller.
if ! command -v uv >/dev/null 2>&1 || [[ -z "${LATTE_QML_MODULE_PATH:-}" ]]; then
    exec nix develop "$repo" -c "$0" "$@"
fi

uv sync --locked --project "$repo/harness" >&2
exec "$repo/harness/.venv/bin/python" -m latte_harness.staged_run "$@"
