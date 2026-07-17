#!/usr/bin/env bash
# End-to-end recipe driver (docs/TESTING.md, the e2e tier). Runs every
# tests/e2e/*.sh recipe against a LIVE Wayland session with the staged
# throwaway dock, serially, and reports pass/fail per recipe.
#
# This is deliberately NOT part of ctest: recipes need a real compositor,
# the fakepointer injector and a display. They are the executable form of
# the specs' live-verification recipes - the future microvm GUI CI runs
# exactly these. Locally:
#
#   scripts/run-e2e.sh              # all recipes
#   scripts/run-e2e.sh <name>...    # specific recipes by filename
#
# The driver restarts the dock into the THROWAWAY config (never your real
# one) and, if it displaced a --user-config dock, restores it afterwards.
set -uo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
export E2E_REPO="$repo"
export E2E_FAKEPOINTER="${E2E_FAKEPOINTER:-$HOME/.local/bin/fakepointer}"

if [[ -z "${WAYLAND_DISPLAY:-}" ]]; then
    echo "run-e2e: FAIL no wayland session (WAYLAND_DISPLAY unset)"; exit 2
fi
if [[ ! -x "$E2E_FAKEPOINTER" ]]; then
    echo "run-e2e: FAIL fakepointer not found at $E2E_FAKEPOINTER (build recipe: latte-live-verification skill)"; exit 2
fi

#! remember whether a user-config dock was running, to restore it
restore_user_config=0
if pgrep -x latte-dock >/dev/null; then
    if tr '\0' '\n' < "/proc/$(pgrep -x latte-dock | head -1)/environ" | grep -q "XDG_CONFIG_HOME=$HOME/.config"; then
        restore_user_config=1
    fi
fi

echo "run-e2e: starting the throwaway dock..."
"$repo/scripts/restart-staged.sh" -d > /tmp/latte-e2e.log 2>&1 &
sleep 16

if ! pgrep -x latte-dock >/dev/null; then
    echo "run-e2e: FAIL the throwaway dock did not come up (see /tmp/latte-e2e.log)"; exit 2
fi

mapfile -t recipes < <(
    if [[ $# -gt 0 ]]; then
        for r in "$@"; do echo "$repo/tests/e2e/${r%.sh}.sh"; done
    else
        find "$repo/tests/e2e" -name '*.sh' | sort
    fi
)

failed=0
for recipe in "${recipes[@]}"; do
    name="$(basename "$recipe")"
    if [[ ! -x "$recipe" ]]; then
        echo "run-e2e: FAIL missing or non-executable recipe: $name"; failed=$((failed+1)); continue
    fi
    echo "run-e2e: ---- $name"
    if "$recipe"; then
        echo "run-e2e: PASS $name"
    else
        echo "run-e2e: FAIL $name"
        failed=$((failed+1))
    fi
done

if [[ "$restore_user_config" == 1 ]]; then
    echo "run-e2e: restoring the user-config dock..."
    "$repo/scripts/restart-staged.sh" --user-config -d > /tmp/latte-e2e-restore.log 2>&1 &
    sleep 8
fi

echo "run-e2e: $((${#recipes[@]} - failed))/${#recipes[@]} recipes passed"
[[ "$failed" == 0 ]]
