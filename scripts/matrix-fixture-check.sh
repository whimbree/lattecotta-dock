#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Thin shim: the matrix-fixture check moved into latte_harness.matrix_fixture_check
# (BP-1b, the bash-to-python migration's fixture chunk). This stable path stays so
# gate-all.sh, skills, and muscle memory keep working; uv provides the pinned
# interpreter and deps, and THE MODULE'S EXIT CODE IS THE VERDICT.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
# uv self-heal: a direct call from a bare or pre-BP shell has no uv on PATH, so
# re-exec into the flake devShell instead of dying with command-not-found - the
# same guard the vehicle front doors carry (matrix-fixture-check's pre-shim
# original had it too; the other shims gain it for consistency). PR #162 review
# finding 1; the wave-1 follow-up in docs/tracking/bash-python-migration-plan.md.
command -v uv >/dev/null 2>&1 || exec nix develop "$repo" -c "$0" "$@"
exec uv run --locked --project "$repo/harness" python -m latte_harness.matrix_fixture_check "$@"
