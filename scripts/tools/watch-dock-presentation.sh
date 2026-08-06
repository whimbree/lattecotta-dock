#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Thin exec shim (BP-5a): the presentation watcher now lives in the typed
# harness at latte_harness.watch_presentation, rebased onto the typed recipe
# API (BP-2c). This stable path keeps muscle memory and any doc quoting working.
#
# Usage:
#   scripts/tools/watch-dock-presentation.sh [seconds] [sample-interval] [dock-id]
#
# Exit 0: at least one geometry transition was observed and every state fit.
# Exit 1: a composition invariant or live query failed (or a bad argument).
# Exit 2: no geometry transition was exercised; refusing a vacuous pass.
#
# See docs/tracking/bash-python-migration-plan.md.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
# uv self-heal: a direct call from a bare or pre-BP shell has no uv on PATH, so
# re-exec into the flake devShell instead of dying with command-not-found (the
# same guard the BP-1 shims carry; PR #162 review finding 1).
command -v uv >/dev/null 2>&1 || exec nix develop "$repo" -c "$0" "$@"
exec uv run --locked --project "$repo/harness" python -m latte_harness.watch_presentation "$@"
