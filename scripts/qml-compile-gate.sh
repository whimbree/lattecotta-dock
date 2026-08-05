#!/usr/bin/env bash
# Headless compile-check for every QML file in the shell, containment,
# plasmoid and indicator packages (porting plan Phase 5). Ported to the typed
# harness in BP-1f (the bash-to-python migration's QML compile/interaction
# gates chunk); the logic and rationale now live in
# harness/src/latte_harness/qml_compile_gate.py. This .sh stays as the stable
# entry point ctest (tests/CMakeLists.txt qmlcompilegate) and muscle memory
# call.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
exec uv run --locked --project "$repo/harness" python -m latte_harness.qml_compile_gate "$@"
