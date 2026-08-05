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
# uv self-heal: a direct call from a bare or pre-BP shell has no uv on PATH, so
# re-exec into the flake devShell instead of dying with command-not-found - the
# same guard the vehicle front doors carry (matrix-fixture-check's pre-shim
# original had it too; the other shims gain it for consistency). PR #162 review
# finding 1; the wave-1 follow-up in docs/tracking/bash-python-migration-plan.md.
command -v uv >/dev/null 2>&1 || exec nix develop "$repo" -c "$0" "$@"
exec uv run --locked --project "$repo/harness" python -m latte_harness.coverage_ratchet "$@"
