# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Shared default-layout config seeder for the nested e2e harnesses (sourced by
# ci/build-and-gate.sh, scripts/asan-e2e-gate.sh, scripts/run-matrix.sh and
# scripts/run-multi-output-e2e.sh). Single source of truth so the container
# release gate and the NixOS sanitized gate can never drift on how a hermetic
# seed config is produced.
#
# THIN BRIDGE (BP-2a): the seeding logic (throwaway compositor bring-up via the
# vehicle module, staged dock run until the default layout self-writes, bounded
# teardown) lives in the typed latte_harness.seed module now; the full rationale
# (why a NORMAL dock, the WAYLAND_DISPLAY preseed, the loud-not-empty failure
# contract) is documented there. This file keeps the SAME sourced interface the
# consumers rely on so they need no change.

# e2e_seed_default_config <repo> <build> <seeddir>: seed a default-layout config
# at <seeddir> by driving the staged dock from <build> once. Returns non-zero
# (loudly, with the seed dock's log tail) if the dock never self-initializes a
# layout - never a silent empty seed.
e2e_seed_default_config() {
    local repo="$1" build="$2" seeddir="$3"
    uv run --locked --project "$repo/harness" python -m latte_harness.seed default-config \
        "$repo" "$build" "$seeddir"
}
