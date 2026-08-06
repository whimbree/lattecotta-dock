#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Thin exec shim (BP-5a): the KWin window dump now lives in the typed harness at
# latte_harness.dumpwins; this stable path keeps the latte-live-verification
# skill, the e2e recipes, and muscle memory working. The DUMPWIN|... line format
# is unchanged. See docs/tracking/bash-python-migration-plan.md.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
# uv self-heal: a direct call from a bare or pre-BP shell has no uv on PATH, so
# re-exec into the flake devShell instead of dying with command-not-found (the
# same guard the BP-1 shims carry; PR #162 review finding 1).
command -v uv >/dev/null 2>&1 || exec nix develop "$repo" -c "$0" "$@"
exec uv run --locked --project "$repo/harness" python -m latte_harness.dumpwins
