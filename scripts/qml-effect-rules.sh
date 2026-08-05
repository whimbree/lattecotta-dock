#!/usr/bin/env bash
# Thin exec shim (BP-1e): the effect-rule source scan now lives in the typed
# harness at latte_harness.qml_effect_rules; this stable path keeps the ctest
# COMMAND entry and any muscle memory working. See
# docs/tracking/bash-python-migration-plan.md.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
exec uv run --locked --project "$repo/harness" python -m latte_harness.qml_effect_rules
