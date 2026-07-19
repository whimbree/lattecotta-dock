#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Deterministic render gate (docs/reference/captsilver-testability-adoption.md, P1):
# runs latte-sceneprobe over every scene in tests/sceneprobe/scenes/ through
# a throwaway nested kwin_wayland (lavapipe + LP_NUM_THREADS=0) and fails if
# any real scene fails. Three failure gates per scene: shader/scenegraph
# warnings, Vulkan validation errors, and output assertions (blank floor,
# probeExpect, golden compare at the bit-exact lavapipe tier unless the scene
# declares a measured probeTolerance).
#
# The gate SELF-TESTS first - the known-good scene must pass, the
# broken-shader scene and the blank scene must fail - so a broken gate is
# caught before trusting its verdicts (exit 3 = the gate itself is broken).
#
# Not part of default ctest: it needs a nested compositor, which is heavier
# than the offscreen ctest contract. Run it like the other script gates:
#
#   scripts/sceneprobe-gate.sh              # gate against committed goldens
#   scripts/sceneprobe-gate.sh --bless      # re-bless goldens (passing scenes
#                                           # only; commit the PNG changes)
#
# BUILD=<dir> overrides the build directory (default: ./build).
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"

# needs the pinned toolchain AND the devShell exports (kwin_wayland, ICD,
# validation layers, LATTE_QML_MODULE_PATH)
if ! command -v kwin_wayland >/dev/null 2>&1 || [[ -z "${LATTE_VULKAN_LAVAPIPE_ICD:-}" ]]; then
    exec nix develop "$repo" -c "$0" "$@"
fi

BLESS=0
[[ "${1:-}" == "--bless" ]] && { BLESS=1; shift; }

here="$repo/tests/sceneprobe"
wrap="$here/run_in_kwin.sh"

source "$repo/scripts/lib-qml-env.sh"
qml_env_setup "$repo"

probe="$build/bin/latte-sceneprobe"
[[ -x "$probe" ]] || { echo "sceneprobe-gate: FAIL no latte-sceneprobe at $probe (build first)"; exit 2; }

# stage the current source's Latte QML modules so scenes import the real
# org.kde.latte.* trees, same as every other QML gate
qml_env_stage

# flatten the resolved -import list (pinned module dirs, linked-package
# pins, staged Latte tree last/winning) for the probe's engine
importpath=""
for ((i = 1; i < ${#imports[@]}; i += 2)); do
    importpath="${importpath:+$importpath:}${imports[$i]}"
done
export LATTE_QML_IMPORT_PATH="$importpath"

# The session's plugin list points at foreign Qt builds (see run-staged.sh);
# the pinned Qt finds its own plugins. QML2_IMPORT_PATH is already unset by
# qml_env_setup.
unset QT_PLUGIN_PATH

export SCENEPROBE_DEVICE=lavapipe
# Golden-compare RIGOR, decoupled from the render device (every leg renders
# the lavapipe device and compares .expected.lavapipe.png; only the rigor
# differs). Forwarded from the environment so a tolerance-tier distro
# (Fedora, KDE neon set SCENEPROBE_TIER=tolerance in their Containerfile ENV)
# gates at a bounded delta, while the default here stays bit-exact for the
# NixOS merge gate. The probe refuses an unknown tier loudly (bitexact|tolerance).
export SCENEPROBE_TIER="${SCENEPROBE_TIER:-bitexact}"
export LATTE_VK_SUPPRESSIONS="$here/vk-suppressions.txt"
export SCENEPROBE_ARTIFACTS="${SCENEPROBE_ARTIFACTS:-$build/_sceneprobe-artifacts}"
mkdir -p "$SCENEPROBE_ARTIFACTS"
rm -f "$SCENEPROBE_ARTIFACTS"/*.actual.png "$SCENEPROBE_ARTIFACTS"/*.diff.png "$SCENEPROBE_ARTIFACTS"/*.expected.png 2>/dev/null || true
echo "sceneprobe-gate: artifacts in $SCENEPROBE_ARTIFACTS"
[[ "$BLESS" -eq 1 ]] && export SCENEPROBE_BLESS=1

out="$(mktemp)"; trap 'rm -f "$out"' EXIT

run_scene() { "$wrap" "$probe" "$1" >"$out" 2>&1; }

# Self-test: good must pass, bad and blank must fail, or the gate is broken.
run_scene "$here/scenes/selftest-good.qml" || { echo "sceneprobe-gate: GATE BROKEN: selftest-good failed"; cat "$out"; exit 3; }
run_scene "$here/scenes/selftest-bad.qml" && rc=0 || rc=$?
[[ "$rc" -eq 1 ]] || { echo "sceneprobe-gate: GATE BROKEN: selftest-bad exited $rc, expected 1"; cat "$out"; exit 3; }
run_scene "$here/scenes/selftest-blank.qml" && rc=0 || rc=$?
[[ "$rc" -eq 1 ]] || { echo "sceneprobe-gate: GATE BROKEN: selftest-blank exited $rc, expected 1 (output floor)"; cat "$out"; exit 3; }
echo "sceneprobe-gate: self-test ok (good passes, bad fails, blank fails)"

fails=0
for s in "$here"/scenes/*.qml; do
    case "$s" in *selftest-*) continue;; esac
    if run_scene "$s"; then
        echo "PASS  $(basename "$s")"
        if [[ "$BLESS" -eq 1 ]]; then
            base="${s%.qml}"; cand="$SCENEPROBE_ARTIFACTS/$(basename "$base").actual.png"
            if [[ -f "$cand" ]]; then
                cp "$cand" "${base}.expected.${SCENEPROBE_DEVICE}.png"
                echo "  blessed $(basename "$base").expected.${SCENEPROBE_DEVICE}.png"
            fi
        fi
    else
        echo "FAIL  $(basename "$s")"; cat "$out"; fails=$((fails+1))
    fi
done

if [[ "$fails" -eq 0 ]]; then
    echo "sceneprobe-gate: PASS"
else
    echo "sceneprobe-gate: FAIL ($fails scene(s); actual/expected/diff triples in $SCENEPROBE_ARTIFACTS)"
    exit 1
fi
