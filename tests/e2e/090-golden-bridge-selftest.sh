#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# HC3 acceptance test for the render-golden bridge (P2 / C-I6,
# docs/tracking/e2e-interaction-test-plan.md). A golden compare that only ever passes on
# a match cannot catch a visual regression - so, exactly like the sceneprobe
# gate's good/bad/blank self-test and the matrix harness's own tripwire, this
# proves the bridge OBSERVES A REJECTION: a deliberately-wrong golden and a
# real visual difference beyond Tolerance are reported as FAIL, a missing
# required golden is a hard FAIL, and only a genuine match passes.
#
# It captures ONE settled crop of the vehicle dock and drives every verdict off
# that single frame, so the proof is deterministic (no cross-shot dependence -
# that determinism is the scenario chunks' concern when they bless real
# goldens, gated at Tolerance per O3). The legs:
#   1. e2e_screenshot_crop produces a real crop of the running dock,
#   2. an IDENTICAL golden -> MATCH (the compare passes on a match)          ,
#   3. a WRONG golden (a painted-over band) -> FAIL beyond Tolerance   [HC3] ,
#   4. a MISSING golden -> hard FAIL (mirror sceneprobe selftest-blank)      ,
#   5. the tier axis is REAL, not hardcoded: a sub-Tolerance +1 shift PASSES
#      at tolerance but FAILS at bitexact - the same pair flips verdict at the
#      delta boundary, proving toleranceForTier is actually consulted         ,
#   6. e2e_assert_golden end-to-end: --bless writes a golden, a missing golden
#      is refused, and a re-shot crop matches the just-blessed golden.
set -uo pipefail
repo="${E2E_REPO:?run through scripts/run-e2e.sh}"
source "$repo/tests/e2e/lib.sh"
source "$repo/tests/e2e/matrix/golden-bridge.sh"

work="$(mktemp -d "${E2E_ARTIFACTS:?}/golden-selftest.XXXXXX")"
trap 'rm -rf "$work"' EXIT

e2e_wait_settled 90 || e2e_fail "the vehicle dock never settled"

# one deterministic crop of the settled dock, off which every verdict is driven
rect="$(matrix_view_crop_rect)" || e2e_fail "could not compute a view crop rect"
actual="$work/actual.png"
e2e_screenshot_crop "$actual" "$rect" || e2e_fail "e2e_screenshot_crop failed for rect $rect"
[[ -s "$actual" ]] || e2e_fail "e2e_screenshot_crop produced an empty image"
# here-string, not process substitution: `magick identify` emits no trailing
# newline, and `read` returns non-zero on an unterminated line even after it
# assigns - the <<< form appends the newline so the assignment is trusted.
read -r cw ch <<<"$(magick identify -format '%w %h' "$actual")"
[[ -n "$cw" && -n "$ch" ]] || e2e_fail "could not read crop dimensions"
echo "golden-bridge-selftest: crop ${cw}x${ch} of the settled dock at $rect"

# A narrow left strip of the view for the e2e_assert_golden legs (7, 8): those
# RE-SHOOT the dock, so their crop must be static across two shots. The default
# seed carries an analog clock (a moving hand), which sits right of the centered
# applet block; the far-left strip is background/first-launcher and never
# animates at rest, so a bless-then-re-shoot compare there is deterministic.
[[ "$rect" =~ ^([0-9]+)x([0-9]+)\+([0-9]+)\+([0-9]+)$ ]] || e2e_fail "unparseable view rect '$rect'"
vw="${BASH_REMATCH[1]}"; vh="${BASH_REMATCH[2]}"; vx="${BASH_REMATCH[3]}"; vy="${BASH_REMATCH[4]}"
sw=$(( vw < 120 ? vw : 120 ))
strip="${sw}x${vh}+${vx}+${vy}"

# derived goldens off the single captured frame (all deterministic)
match="$work/match.png";  cp "$actual" "$match"                      # identical
wrong="$work/wrong.png"                                              # painted-over band
magick "$actual" -fill '#ff00ff' -draw "rectangle 0,0 $((cw/2)),$((ch/2))" "$wrong" \
    || e2e_fail "could not synthesize the wrong golden"
tiny="$work/tiny.png"                                                # <=Tolerance +1 shift
magick "$actual" -evaluate Add 0.3% "$tiny" \
    || e2e_fail "could not synthesize the sub-tolerance golden"

pass=0 fail=0
# check_rc <wanted-rc> <label> <command...>: run a bridge call and compare its
# exit code to the verdict this test demands of the bridge. The command may be
# a shell function, so it is invoked directly (never via `env`, which execs an
# external program and cannot see a bash function).
check_rc() {
    local want="$1" label="$2"; shift 2
    local got=0
    "$@" >/dev/null 2>&1 || got=$?
    if [[ "$got" == "$want" ]]; then
        echo "  ok   [$label] bridge returned $got as expected"
        pass=$((pass + 1))
    else
        echo "  FAIL [$label] bridge returned $got, expected $want" >&2
        fail=$((fail + 1))
    fi
}

# compare_at_tier <tier> <actual> <expected>: e2e_golden_compare with an
# explicit SCENEPROBE_TIER, in a SUBSHELL so the export is scoped to the call
# (a bash function cannot take a temporary env prefix the way `env prog` does).
compare_at_tier() { ( export SCENEPROBE_TIER="$1"; shift; e2e_golden_compare "$@" ); }
# assert_golden_in <repo> <bless> <cell> <verb> <phase> <rect>: e2e_assert_golden
# pointed at a throwaway golden repo, in a subshell so E2E_REPO/E2E_BLESS never
# leak into the committed tree or later recipes.
assert_golden_in() {
    ( export E2E_REPO="$1" E2E_ARTIFACTS="$work" E2E_BLESS="$2"; shift 2
      e2e_assert_golden "$@" )
}

echo "== 1. a MATCH (identical golden) passes the compare =="
check_rc 0 "match-passes" e2e_golden_compare "$actual" "$match" "$work/match.diff.png"

echo "== 2. HC3: a WRONG golden (painted-over band) is REJECTED as FAIL =="
check_rc 1 "wrong-fails" e2e_golden_compare "$actual" "$wrong" "$work/wrong.diff.png"

echo "== 3. a MISSING required golden is a hard FAIL, not a silent pass =="
check_rc 1 "missing-fails" e2e_golden_compare "$actual" "$work/does-not-exist.png"

echo "== 4. the tier axis is real: a +1 (<=Tolerance) shift PASSES at tolerance... =="
check_rc 0 "tiny-tolerance-passes" compare_at_tier tolerance "$actual" "$tiny"

echo "== 5. ...but the SAME pair FAILS at bitexact (toleranceForTier is consulted) =="
check_rc 1 "tiny-bitexact-fails" compare_at_tier bitexact "$actual" "$tiny"

echo "== 6. an unknown tier is REFUSED loudly (setup error 2), never a wrong rigor =="
check_rc 2 "unknown-tier-refused" compare_at_tier lenient "$actual" "$match"

echo "== 7. e2e_assert_golden: a MISSING committed golden is refused as FAIL =="
# a throwaway golden dir so the self-test never reads or writes the committed
# tree; E2E_REPO points the bridge's golden path here for these two legs.
gtmp="$work/goldenrepo"; mkdir -p "$gtmp/tests/e2e/goldens"
cell="selftest-dock-bottom-center-1out"
check_rc 1 "assert-missing-fails" assert_golden_in "$gtmp" 0 "$cell" probe before "$strip"

echo "== 8. e2e_assert_golden --bless writes the golden, then a re-shot crop MATCHES it =="
assert_golden_in "$gtmp" 1 "$cell" probe before "$strip" >/dev/null 2>&1 \
    || e2e_fail "e2e_assert_golden --bless did not write a golden"
[[ -f "$gtmp/tests/e2e/goldens/$cell.probe.before.expected.lavapipe.png" ]] \
    || e2e_fail "bless did not create the expected golden filename"
check_rc 0 "assert-reshoot-matches" assert_golden_in "$gtmp" 0 "$cell" probe before "$strip"

echo "golden-bridge-selftest: $pass ok, $fail failed"
[[ "$fail" == 0 ]] || e2e_fail "the golden bridge did not behave as a trustworthy comparator (see failures above)"
echo "PASS: 090-golden-bridge-selftest (the bridge observes rejections, not just matches)"
