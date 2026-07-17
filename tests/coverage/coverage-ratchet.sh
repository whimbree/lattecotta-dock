#!/usr/bin/env bash
# Coverage ratchet (docs/QML_EXTRACTION_PLAN.md, section D). Two structural
# checks, no instrumentation dependency, CI-portable as plain shell:
#
#   1. Unit pairing: every pure-core header under the units/ directories
#      (plus the app-subtree placements listed in
#      tests/units/app-subtree-units.list) must have its paired test
#      tests/units/<basename>test.cpp present AND registered in ctest.
#      A unit cannot land untested without failing here.
#   2. Entry-list ratchet: the ctest entry list must match
#      tests/coverage/ratchet-baseline exactly. Adding a test means adding its
#      line there in the same commit (routine); REMOVING a test is only
#      possible by deliberately editing the committed baseline, which
#      makes silent coverage loss un-mergeable.
#
# Usage: tests/coverage/coverage-ratchet.sh [build-dir]   (default: ./build)
set -euo pipefail

repo="$(cd "$(dirname "$0")/../.." && pwd)"
build="${1:-$repo/build}"

fail=0

# --- 1. unit pairing -------------------------------------------------------

unit_dirs=(
    containment/plugin/units
    plasmoid/plugin/units
    declarativeimports/core/units
)

headers=()
for dir in "${unit_dirs[@]}"; do
    while IFS= read -r h; do
        headers+=("$h")
    done < <(find "$repo/$dir" -name '*.h' 2>/dev/null | sort)
done

list="$repo/tests/units/app-subtree-units.list"
if [[ -f "$list" ]]; then
    while IFS= read -r line; do
        [[ -z "$line" || "$line" == \#* ]] && continue
        if [[ ! -f "$repo/$line" ]]; then
            echo "ratchet: FAIL listed unit header does not exist: $line"
            fail=1
            continue
        fi
        headers+=("$repo/$line")
    done < "$list"
fi

ctest_names="$(ctest --test-dir "$build" -N | sed -n 's/^ *Test *#[0-9]*: //p')"

for h in "${headers[@]}"; do
    base="$(basename "$h" .h)"
    test_src="$repo/tests/units/${base}test.cpp"
    if [[ ! -f "$test_src" ]]; then
        echo "ratchet: FAIL unit header ${h#"$repo"/} has no paired test tests/units/${base}test.cpp"
        fail=1
    fi
    if ! grep -qx "${base}test" <<<"$ctest_names"; then
        echo "ratchet: FAIL paired test ${base}test is not registered in ctest"
        fail=1
    fi
done

# --- 2. entry-list ratchet -------------------------------------------------

baseline="$repo/tests/coverage/ratchet-baseline"
if [[ ! -f "$baseline" ]]; then
    echo "ratchet: FAIL missing $baseline"
    exit 1
fi

recorded="$(grep -v '^#' "$baseline" | sed '/^$/d')"
recorded_count="$(head -n1 <<<"$recorded")"
recorded_list="$(tail -n +2 <<<"$recorded" | sort)"
actual_list="$(sort <<<"$ctest_names")"
actual_count="$(sed '/^$/d' <<<"$actual_list" | wc -l)"

if [[ "$recorded_count" != "$(sed '/^$/d' <<<"$recorded_list" | wc -l)" ]]; then
    echo "ratchet: FAIL tests/coverage/ratchet-baseline count line disagrees with its own entry list"
    fail=1
fi

if [[ "$recorded_list" != "$actual_list" ]]; then
    echo "ratchet: FAIL ctest entry list diverged from tests/coverage/ratchet-baseline"
    echo "ratchet: recorded $recorded_count entries, ctest reports $actual_count. Diff (-recorded +actual):"
    diff <(printf '%s\n' "$recorded_list") <(printf '%s\n' "$actual_list") | sed 's/^/ratchet:   /' || true
    echo "ratchet: additions are routine - update tests/coverage/ratchet-baseline in the same commit."
    echo "ratchet: removals must be deliberate - the baseline edit is the audit trail."
    fail=1
fi

if [[ "$fail" != 0 ]]; then
    exit 1
fi

echo "coverage-ratchet: OK (${actual_count} ctest entries, ${#headers[@]} unit headers paired)"
