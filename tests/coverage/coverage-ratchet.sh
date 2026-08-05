#!/usr/bin/env bash
# Coverage ratchet: unit-header/test pairing plus the committed ctest
# entry-list baseline (docs/tracking/QML_EXTRACTION_PLAN.md section D). Ported
# to the typed harness in BP-1c; the logic and rationale now live in
# harness/src/latte_harness/coverage_ratchet.py. This .sh stays as the stable
# entry point build-check.sh and muscle memory call.
#
# Usage: tests/coverage/coverage-ratchet.sh [build-dir]   (default: <repo>/build)
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
exec uv run --locked --project "$repo/harness" python -m latte_harness.coverage_ratchet "$@"
