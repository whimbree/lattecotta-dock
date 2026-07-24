#!/usr/bin/env bash
# Self-test for D150 (hovered applet row escaped its resting background). The
# controlled bad payload is the live regression shape: applets reach both
# beyond a background capped to the resting maximum, while the complete row
# still fits the output.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"

views_good='{"views":[{"persistentDockId":14,"orientation":"horizontal","isHidden":false,"effectsRect":[20,220,2520,148],"canvasGeometry":[1440,1643,2560,222]}]}'
views_bad='{"views":[{"persistentDockId":14,"orientation":"horizontal","isHidden":false,"effectsRect":[225,220,2110,148],"canvasGeometry":[1440,1643,2560,222]}]}'
applets='[
  {"inScheduledDestruction":false,"geometry":[54,241,165,106]},
  {"inScheduledDestruction":false,"geometry":[235,220,1906,164]},
  {"inScheduledDestruction":false,"geometry":[2392,241,107,106]}
]'

if ! _e2e_assert_presentation_payloads "$views_good" "$applets" 14 2; then
    e2e_fail "the valid complete-background fixture was rejected"
fi

if _e2e_assert_presentation_payloads "$views_bad" "$applets" 14 2 \
        >"$E2E_ARTIFACTS/presentation-coverage-negative.log" 2>&1; then
    e2e_fail "the D150 background/content separation fixture was not rejected"
fi

grep -q "content starts at 54, before background 225" \
    "$E2E_ARTIFACTS/presentation-coverage-negative.log" \
    || e2e_fail "the negative control failed for an unexpected reason"
grep -q "content ends at 2499, after background 2335" \
    "$E2E_ARTIFACTS/presentation-coverage-negative.log" \
    || e2e_fail "the negative control did not observe the escaped tail"

echo "PASS: presentation coverage accepts complete chrome and rejects the D150 escaped-row shape"
