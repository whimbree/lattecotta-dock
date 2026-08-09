#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Validate and smoke-test one explicitly installed Latte package. Ported to
# the typed harness in BP-4a (the bash-to-python migration's package-gate
# engine chunk); the engine lives in
# harness/src/latte_harness/package_gate.py with the ELF/maps provenance
# core in package_provenance.py. This .sh stays as the stable entry point
# (tests/installed-package-gate-runtime-test.sh, ci/build-and-gate.sh, the
# selftest's refusal controls); THE MODULE'S EXIT CODE IS THE VERDICT.
#
# The validation-command preflight below is the one piece that must stay
# bash: the selftest proves the gate refuses loudly under a PATH that
# cannot resolve ANY interpreter (no uv, no nix, no env), a property only
# bash builtins can provide. The list and message are byte-identical to the
# module's require_commands contract; test_package_gate.py pins the two in
# lockstep so the copy cannot drift.
set -euo pipefail

script_dir="${BASH_SOURCE[0]%/*}"
[[ "$script_dir" != "${BASH_SOURCE[0]}" ]] || script_dir=.
repo="$(cd "$script_dir/.." && pwd -P)"

for required_command in awk cat dirname env find jq mktemp perl readelf readlink realpath rm timeout tr; do
    command -v "$required_command" >/dev/null 2>&1 || {
        echo "installed-package-gate: FAIL: required validation command '$required_command' is missing" >&2
        exit 2
    }
done

# uv self-heal: a bare shell re-execs into the flake devShell instead of
# dying with a command-not-found (the standard BP shim guard).
if ! command -v uv >/dev/null 2>&1; then
    exec nix develop "$repo" -c "$0" "$@"
fi

exec uv run --locked --project "$repo/harness" python -m latte_harness.package_gate "$@"
