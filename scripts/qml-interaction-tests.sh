#!/usr/bin/env bash
# Headless QML interaction harness (porting plan Phase 5, docs/reference/TESTING.md):
# drives real Latte QML components offscreen through qmltestrunner. Ported to
# the typed harness in BP-1f (the bash-to-python migration's QML compile/
# interaction gates chunk); the logic now lives in
# harness/src/latte_harness/qml_interaction_tests.py. This .sh stays as the
# stable entry point ctest (qmlinteraction bare, qmlcontracts with tests/
# contracts) and muscle memory call; the optional test-directory argument is
# forwarded verbatim.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
# uv self-heal: a direct call from a bare or pre-BP shell has no uv on PATH, so
# re-exec into the flake devShell instead of dying with command-not-found - the
# same guard the vehicle front doors carry (matrix-fixture-check's pre-shim
# original had it too; the other shims gain it for consistency). PR #162 review
# finding 1; the wave-1 follow-up in docs/tracking/bash-python-migration-plan.md.
command -v uv >/dev/null 2>&1 || exec nix develop "$repo" -c "$0" "$@"
exec uv run --locked --project "$repo/harness" python -m latte_harness.qml_interaction_tests "$@"
