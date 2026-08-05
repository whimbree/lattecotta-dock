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
exec uv run --locked --project "$repo/harness" python -m latte_harness.qml_interaction_tests "$@"
