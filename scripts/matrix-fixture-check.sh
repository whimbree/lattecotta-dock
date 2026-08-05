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
exec uv run --locked --project "$repo/harness" python -m latte_harness.matrix_fixture_check "$@"
