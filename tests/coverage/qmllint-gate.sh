#!/usr/bin/env bash
# qmllint ratchet gate (docs/tracking/QML_EXTRACTION_PLAN.md step 2.5 point 6;
# docs/reference/TESTING.md).
#
# THIN SHIM (BP-1d): the logic lives in the typed latte_harness.qmllint_gate
# module now - staging and import assembly through latte_harness.qmlenv, the
# pinned-qmllint --json run, the curated-category exact-count ratchet against
# tests/coverage/qmllint-baseline, the app-module and dead-version-ladder skip
# lists, --write-baseline (now C-collation ordered by construction, retiring
# D270), and the D269 per-warning fingerprint diagnostic. See
# harness/src/latte_harness/qmllint_gate.py. ctest and gate-all.sh keep invoking
# this path; arguments (notably --write-baseline) forward through unchanged.
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"
exec uv run --locked --project "$repo/harness" python -m latte_harness.qmllint_gate "$@"
