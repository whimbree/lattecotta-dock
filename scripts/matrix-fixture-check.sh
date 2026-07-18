#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Fast HERMETIC check of the matrix fixture generator (tests/e2e/matrix/
# fixture.py). This is the per-commit tier-1 hook for C-I1 (O4: keep the merge
# gate fast, the full nested-vehicle suite periodic). It needs no compositor
# and no built dock - it drives the generator (a pure config transform) over a
# seed built from the shipped Default template, asserts the parametrized keys
# land, and asserts every malformed descriptor is REFUSED with no output. The
# full harness acceptance (fixture -> live dock -> readback, incl. the HC3
# reject-observing self-test) is scripts/run-matrix.sh, run periodically.
#
# THE EXIT CODE IS THE VERDICT (CLAUDE.md gate discipline): run it, read $?, done.
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"

# python3 is a devShell tool; re-exec unless it is already resolvable
if ! command -v python3 >/dev/null 2>&1; then
    exec nix develop "$repo" -c "$0" "$@"
fi

fixture="$repo/tests/e2e/matrix/fixture.py"
template="$repo/shell/package/contents/templates/Default.layout.latte"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT

# a minimal seed from the shipped Default template (the same layout a first-run
# dock writes, so the generator patches a real, loadable base)
seed="$work/seed"; mkdir -p "$seed/latte"
cp "$template" "$seed/latte/My Layout.layout.latte"
printf '[UniversalSettings]\nsingleModeLayoutName=My Layout\nmemoryUsage=0\n' > "$seed/lattedockrc"

fails=0
gen() { python3 "$fixture" --seed-dir "$seed" --out-dir "$1" \
        --view-type "$2" --edge "$3" --alignment "$4" --display "${5:-1out}" "${@:6}"; }
layout_of() { echo "$1/latte/My Layout.layout.latte"; }

# assert_key <label> <file> <regex>: the layout must carry a line matching regex
assert_key() {
    if grep -qE "$3" "$2"; then
        echo "  ok   [$1]"
    else
        echo "  FAIL [$1]: no line matching /$3/ in $2" >&2; fails=$((fails + 1))
    fi
}

echo "matrix-fixture-check: DOCK top/left (horizontal axis)"
gen "$work/dtl" dock top left >/dev/null
L="$(layout_of "$work/dtl")"
assert_key "location=TopEdge(3)" "$L" '^location=3$'
assert_key "formfactor=Horizontal(2)" "$L" '^formfactor=2$'
assert_key "alignment=Left(1)" "$L" '^alignment=1$'
assert_key "zoomLevel on (dock)" "$L" '^zoomLevel=16$'
assert_key "onPrimary=true (1out)" "$L" '^onPrimary=true$'
assert_key "lastScreen=-1 (1out)" "$L" '^lastScreen=-1$'

echo "matrix-fixture-check: DOCK left/right (vertical axis rotates alignment)"
gen "$work/dlr" dock left right >/dev/null
L="$(layout_of "$work/dlr")"
assert_key "location=LeftEdge(5)" "$L" '^location=5$'
assert_key "formfactor=Vertical(3)" "$L" '^formfactor=3$'
assert_key "alignment=Bottom(4) [right->far end]" "$L" '^alignment=4$'

echo "matrix-fixture-check: PANEL bottom/justify (zoom off + justify + thick bg + no bounce)"
gen "$work/pbj" panel bottom justify >/dev/null
L="$(layout_of "$work/pbj")"
assert_key "zoomLevel=0" "$L" '^zoomLevel=0$'
assert_key "alignment=Justify(10)" "$L" '^alignment=10$'
assert_key "panelSize=100 (thick)" "$L" '^panelSize=100$'
assert_key "useThemePanel=true" "$L" '^useThemePanel=true$'
assert_key "tasks bounce anim off" "$L" '^animationLauncherBouncing=false$'

echo "matrix-fixture-check: PANEL top/center (full-span static length)"
gen "$work/ptc" panel top center >/dev/null
L="$(layout_of "$work/ptc")"
assert_key "minLength=100" "$L" '^minLength=100$'
assert_key "maxLength=100" "$L" '^maxLength=100$'

echo "matrix-fixture-check: 2out per-screen pin"
gen "$work/d2" dock bottom center 2out --screen HDMI-A-2 >/dev/null
L="$(layout_of "$work/d2")"
assert_key "onPrimary=false (2out)" "$L" '^onPrimary=false$'
assert_key "explicitScreen=HDMI-A-2" "$L" '^explicitScreen=HDMI-A-2$'

echo "matrix-fixture-check: REFUSALS (each must exit 2, leave no output dir)"
# assert_refused <label> <out-dir> <args...>
assert_refused() {
    local label="$1" out="$2"; shift 2
    rm -rf "$out"
    local rc=0
    python3 "$fixture" --seed-dir "$seed" --out-dir "$out" "$@" >/dev/null 2>&1 || rc=$?
    if [[ "$rc" == 2 && ! -d "$out" ]]; then
        echo "  ok   [$label] refused (exit 2, no output)"
    else
        echo "  FAIL [$label] exit=$rc out-dir-exists=$([[ -d "$out" ]] && echo yes || echo no)" >&2
        fails=$((fails + 1))
    fi
}
assert_refused "bad edge"      "$work/r1" --view-type dock  --edge diagonal --alignment left   --display 1out
assert_refused "bad alignment" "$work/r2" --view-type dock  --edge top      --alignment skew   --display 1out
assert_refused "bad view-type" "$work/r3" --view-type slab  --edge top      --alignment left   --display 1out
assert_refused "bad display"   "$work/r4" --view-type dock  --edge top      --alignment left   --display 3out

# a seed with no Latte containment must be refused (the promised view is absent)
badseed="$work/badseed"; mkdir -p "$badseed/latte"
printf '[Containments][1]\nplugin=org.kde.plasma.folder\n' > "$badseed/latte/x.layout.latte"
printf '[UniversalSettings]\nsingleModeLayoutName=x\n' > "$badseed/lattedockrc"
rc=0
python3 "$fixture" --seed-dir "$badseed" --out-dir "$work/rb" --view-type dock --edge top --alignment left --display 1out >/dev/null 2>&1 || rc=$?
if [[ "$rc" == 2 ]]; then echo "  ok   [no-latte-containment] refused"; else echo "  FAIL [no-latte-containment] exit=$rc" >&2; fails=$((fails + 1)); fi

if [[ "$fails" == 0 ]]; then
    echo "matrix-fixture-check: PASS"
else
    echo "matrix-fixture-check: FAIL ($fails check(s))"; exit 1
fi
