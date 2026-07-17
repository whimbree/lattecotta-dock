#!/usr/bin/env bash
# qmllint ratchet gate (docs/QML_EXTRACTION_PLAN.md step 2.5, point 6;
# docs/TESTING.md). Runs the PINNED Qt's qmllint over the same package QML
# qml-compile-gate.sh enumerates, plus the staged org.kde.latte
# declarativeimports tree, counting the curated warning categories:
#
#   unqualified                 unqualified access
#   missing-type                untyped function signatures/parameters
#   unresolved-type             unresolvable types
#   deprecated                  deprecated syntax
#   signal-handler-parameters   injected signal-handler parameters
#
# Per-file counts must match tests/coverage/qmllint-baseline EXACTLY - the same
# ratchet law as tests/coverage/ratchet-baseline: an increase is un-mergeable, an
# improvement lands with the baseline shrink in the same commit
# (regenerate with: tests/coverage/qmllint-gate.sh --write-baseline). Files that
# can never reach zero for a structural reason carry the reason as a
# comment next to their baseline entry - named, never silent.
#
# Full-strict QML is the asymptotic state the extraction converges to
# (strict-on-touch: files a cutover touches leave at zero), not a mandate
# on inherited files.
#
# Runs inside the flake devShell (ctest invokes it there via build-check.sh).
set -euo pipefail

repo="$(cd "$(dirname "$0")/../.." && pwd)"
baseline="$repo/tests/coverage/qmllint-baseline"

source "$repo/scripts/lib-qml-env.sh"
qml_env_setup "$repo"
qml_env_stage

# the pinned qmllint, never the host's
lint="$(command -v qmllint)"
case "$lint" in
    /nix/store/*) ;;
    *) echo "qmllint-gate: FAIL qmllint resolves outside the pinned closure: $lint"; exit 2;;
esac

command -v jq >/dev/null || { echo "qmllint-gate: FAIL jq not found (devShell provides it)"; exit 2; }

mapfile -t all < <(find \
    "$stage/share/plasma/shells/org.kde.latte.shell" \
    "$stage/share/plasma/plasmoids/org.kde.latte.containment" \
    "$stage/share/plasma/plasmoids/org.kde.latte.plasmoid" \
    "$stage/share/latte/indicators" \
    "$stage/lib/qml/org/kde/latte" \
    -name '*.qml' 2>/dev/null | sort)

if [[ "${#all[@]}" -eq 0 ]]; then echo "no staged QML found under $stage"; exit 2; fi

# same skip classes as qml-compile-gate.sh: org.kde.latte.private.app only
# registers inside the latte-dock binary (its types can never resolve for
# a standalone tool), and the superseded *.5.2[0-5].qml version-ladder
# rungs are dead on Plasma 6
files=(); skipped_app=0; skipped_ladder=0
for f in "${all[@]}"; do
    if grep -q 'org.kde.latte.private.app' "$f"; then skipped_app=$((skipped_app+1)); continue; fi
    if [[ "$f" =~ \.5\.2[0-5]\.qml$ ]]; then skipped_ladder=$((skipped_ladder+1)); continue; fi
    files+=("$f")
done
echo "qmllint-gate: ${#files[@]} files ($skipped_app app-module-dependent + $skipped_ladder dead-version-ladder skipped)"

# import dirs: the same resolved list the QML harnesses use (the imports
# array holds "-import <dir>" pairs; qmllint wants -I)
iflags=(-I "$stage/lib/qml")
for ((i=1; i<${#imports[@]}; i+=2)); do
    iflags+=(-I "${imports[$i]}")
done

out="$stage/_qmllint_gate.json"
# qmllint exits nonzero when any warning fires; the ratchet judges counts.
# --json is the machine interface: warnings carry a stable category id,
# so counting cannot silently drift with the human-readable text format.
"$lint" "${iflags[@]}" --json "$out" "${files[@]}" >/dev/null 2>&1 || true

current="$stage/_qmllint_gate.current"
jq -r --arg stage "$stage/" '
    .files[]
    | {rel: (.filename | ltrimstr($stage)),
       n: ([.warnings[]
            | select(.id == "unqualified"
                  or .id == "missing-type"
                  or .id == "unresolved-type"
                  or .id == "deprecated"
                  or .id == "signal-handler-parameters")]
           | length)}
    | select(.n > 0)
    | "\(.n)\t\(.rel)"' "$out" | sort -k2 > "$current"

if [[ "${1:-}" == "--write-baseline" ]]; then
    {
        echo "# Per-file curated qmllint warning counts (tests/coverage/qmllint-gate.sh)."
        echo "# Ratchet law: this file only ever shrinks. Regenerate with"
        echo "#   tests/coverage/qmllint-gate.sh --write-baseline"
        echo "# and commit the shrink together with the change that earned it."
        echo "# Files that cannot reach zero for a structural reason are NAMED"
        echo "# with their reason in docs/QML_EXTRACTION_PLAN.md (section D,"
        echo "# step-2.5 additions) - this file is regenerated wholesale, so"
        echo "# the durable record lives there."
        cat "$current"
    } > "$baseline"
    echo "qmllint-gate: baseline written to ${baseline#"$repo"/} ($(wc -l < "$current") files with findings)"
    exit 0
fi

if [[ ! -f "$baseline" ]]; then
    echo "qmllint-gate: FAIL no baseline at tests/coverage/qmllint-baseline (generate with --write-baseline)"
    exit 1
fi

expected="$stage/_qmllint_gate.expected"
grep -v '^#' "$baseline" | grep -v '^$' | sort -k2 > "$expected"

if ! diff -u "$expected" "$current" > "$stage/_qmllint_gate.diff"; then
    echo "qmllint-gate: FAIL per-file curated warning counts diverge from tests/coverage/qmllint-baseline:"
    cat "$stage/_qmllint_gate.diff"
    echo "qmllint-gate: an increase is a regression to fix; an improvement lands"
    echo "qmllint-gate: with the baseline shrink in the same commit (--write-baseline)."
    exit 1
fi

total="$(awk '{s+=$1} END {print s+0}' "$current")"
echo "qmllint-gate: OK ($(wc -l < "$current" | tr -d ' ') files with findings, $total curated warnings, baseline matched)"
