#!/usr/bin/env bash
# Thin exec shim (BP-1e): the previews-pipeline contract scan now lives in the
# typed harness at latte_harness.preview_contract_rules; this stable path keeps
# the ctest COMMAND entry and any muscle memory working. Only EX-01 commits may
# edit the rules themselves (now in the module). See
# docs/tracking/bash-python-migration-plan.md.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
exec uv run --locked --project "$repo/harness" python -m latte_harness.preview_contract_rules
