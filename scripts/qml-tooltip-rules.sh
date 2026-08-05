#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Thin exec shim (BP-1e): the tooltip-rule source scan now lives in the typed
# harness at latte_harness.qml_tooltip_rules; this stable path keeps the ctest
# COMMAND entry and any muscle memory working. See
# docs/tracking/bash-python-migration-plan.md.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
exec uv run --locked --project "$repo/harness" python -m latte_harness.qml_tooltip_rules
