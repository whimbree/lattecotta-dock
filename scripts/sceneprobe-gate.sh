#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Deterministic render gate (docs/archive/captsilver-testability-adoption.md,
# P1): runs latte-sceneprobe over every scene in tests/sceneprobe/scenes/
# through a throwaway nested kwin_wayland (lavapipe + LP_NUM_THREADS=0) and fails
# if any real scene fails, self-testing its own pass/fail wiring first. Ported to
# the typed harness in BP-2d (the bash-to-python migration's sceneprobe chunk);
# the choreography (QML staging, the nested compositor, the probe execution, the
# golden compare env, the self-test, and the exit-code contract) now lives in
# harness/src/latte_harness/sceneprobe_gate.py, riding the same vehicle module
# the e2e driver uses. This .sh stays as the stable entry point gate-all.sh,
# ci/build-and-gate.sh, skills, and muscle memory call; THE MODULE'S EXIT CODE
# IS THE VERDICT (0 pass, 1 a scene failed, 2 setup, 3 the gate is broken).
#
#   scripts/sceneprobe-gate.sh              # gate against committed goldens
#   scripts/sceneprobe-gate.sh --bless      # re-bless goldens (passing scenes
#                                           # only; commit the PNG changes)
#
# BUILD=<dir> overrides the build directory (default: ./build).
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"

# The gate needs the FULL pinned devShell, not just uv: kwin_wayland for the
# nested compositor, uv for the harness, LATTE_QML_MODULE_PATH for the QML
# staging, and LATTE_VULKAN_LAVAPIPE_ICD for the lavapipe render device. Re-exec
# into the flake devShell if any is missing instead of dying with a
# command-not-found (the uv self-heal generalized to the whole vehicle+vulkan
# toolchain, matching run-e2e.sh; the pre-shim original carried the kwin_wayland
# + LATTE_VULKAN_LAVAPIPE_ICD half of this guard).
if ! command -v kwin_wayland >/dev/null 2>&1 || ! command -v uv >/dev/null 2>&1 \
   || [[ -z "${LATTE_QML_MODULE_PATH:-}" ]] || [[ -z "${LATTE_VULKAN_LAVAPIPE_ICD:-}" ]]; then
    exec nix develop "$repo" -c "$0" "$@"
fi

exec uv run --locked --project "$repo/harness" python -m latte_harness.sceneprobe_gate "$@"
