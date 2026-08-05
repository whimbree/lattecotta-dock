#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Run a command under a throwaway nested kwin_wayland session so it gets a
# Vulkan-capable wayland QPA (QVulkanInstance needs platform glue the offscreen
# QPA lacks). Ported to the typed harness in BP-2d (the bash-to-python
# migration's sceneprobe chunk): the Vulkan device dispatch (SCENEPROBE_DEVICE:
# lavapipe default, dgpu opt-in), the probe env, and the nested kwin bring-up
# and teardown now live in harness/src/latte_harness/sceneprobe_gate.py, sharing
# the latte_harness.vehicle lifecycle with the e2e driver and the gate. This .sh
# stays as the stable entry point the latte-debugging skill and dgpu exploration
# invoke; it runs the given command inside the nested session and exits with the
# command's own code (2 on a setup failure, 124 on the bounded 90s timeout).
#
#   nix develop -c tests/sceneprobe/run_in_kwin.sh latte-sceneprobe scene.qml
#   nix develop -c tests/sceneprobe/run_in_kwin.sh dbus-run-session -- <cmd...>
set -euo pipefail
repo="$(cd "$(dirname "$0")/../.." && pwd)"

# The nested session needs the FULL pinned devShell (kwin_wayland, uv, and the
# Vulkan ICD/layer manifests it exports). Re-exec into the flake devShell if the
# two binaries are missing instead of dying with a command-not-found; the module
# then validates the Vulkan env itself, device-aware and with the original loud
# wording (dgpu, for instance, never needs the lavapipe ICD).
if ! command -v kwin_wayland >/dev/null 2>&1 || ! command -v uv >/dev/null 2>&1; then
    exec nix develop "$repo" -c "$0" "$@"
fi

exec uv run --locked --project "$repo/harness" python -m latte_harness.sceneprobe_gate run-in-kwin "$@"
