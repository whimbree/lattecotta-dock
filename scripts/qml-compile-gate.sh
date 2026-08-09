#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Headless compile-check for every QML file in the shell, containment,
# plasmoid and indicator packages (porting plan Phase 5). Ported to the typed
# harness in BP-1f (the bash-to-python migration's QML compile/interaction
# gates chunk); the logic and rationale now live in
# harness/src/latte_harness/qml_compile_gate.py. This .sh stays as the stable
# entry point ctest (tests/CMakeLists.txt qmlcompilegate) and muscle memory
# call.
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
# uv self-heal: a direct call from a bare or pre-BP shell has no uv on PATH, so
# re-exec into the flake devShell instead of dying with command-not-found - the
# same guard the vehicle front doors carry (matrix-fixture-check's pre-shim
# original had it too; the other shims gain it for consistency). PR #162 review
# finding 1; the wave-1 follow-up in docs/tracking/bash-python-migration-plan.md.
command -v uv >/dev/null 2>&1 || exec nix develop "$repo" -c "$0" "$@"
exec uv run --locked --project "$repo/harness" python -m latte_harness.qml_compile_gate "$@"
