#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# End-to-end recipe driver (docs/reference/TESTING.md, the e2e tier). Ported to
# the typed harness in BP-2b (the bash-to-python migration's e2e runner chunk);
# the driver now lives in harness/src/latte_harness/e2e_runner.py - discovery,
# the # e2e-mode / # e2e-expect marker parsing, the bit-identical
# PASS/FAIL/XFAIL/XPASS/SKIP classification matrix plus its self-test, vehicle
# bring-up via latte_harness.vehicle, the recipe env contract, the per-recipe
# dock lifecycle, artifacts, the summary line, and the exit-code verdict. This
# .sh stays as the stable entry point the front doors (run-matrix,
# run-multi-output-e2e, run-colorizer-e2e, run-asan-dock, asan-e2e-gate) and
# muscle memory call; THE MODULE'S EXIT CODE IS THE VERDICT.
#
#   scripts/run-e2e.sh [recipes...]            # nested (default): desk-independent
#   scripts/run-e2e.sh --live [recipes...]     # against the real Wayland session
#   scripts/run-e2e.sh --self-test-expectations # the classifier self-test only
#
# Recipes declare a mode constraint with "# e2e-mode: nested-only" or
# "# e2e-mode: live-only" and a known-open-bug expectation with
# "# e2e-expect: fail" or "# e2e-expect: status N"; the module carries the full
# semantics and the DISCOVERY DEVIATION (recipes only, no subdir libs).
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"

# The vehicle needs the FULL pinned devShell, not just uv: kwin_wayland for the
# nested compositor, uv for the harness, LATTE_QML_MODULE_PATH for run-staged.
# Re-exec into the flake devShell if any is missing instead of dying with a
# command-not-found (the uv self-heal generalized to the whole vehicle
# toolchain; run-e2e's pre-shim original carried this exact three-part guard).
if ! command -v kwin_wayland >/dev/null 2>&1 || ! command -v uv >/dev/null 2>&1 \
   || [[ -z "${LATTE_QML_MODULE_PATH:-}" ]]; then
    exec nix develop "$repo" -c "$0" "$@"
fi

exec uv run --locked --project "$repo/harness" python -m latte_harness.e2e_runner "$@"
