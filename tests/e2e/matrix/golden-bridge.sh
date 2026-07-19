# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# The RENDER-GOLDEN BRIDGE for the e2e interaction suite (P2 / C-I6,
# docs/tracking/e2e-interaction-test-plan.md sections 4 and 5). Goldens are the LAST
# resort in this suite (HC2): the overwhelming majority of scenarios assert by
# D-Bus readback, and a golden is blessed ONLY where a pixel fact has no
# queryable readback (the F1 blueprint grid, F2/F3 justify zone distribution,
# the A5 PR#20 ghost-overlay residue). This file is the shared plumbing those
# few scenarios and the abort backbone call; it does NOT itself bless any
# golden - each surviving golden is justified case-by-case in the plan.
#
# It REUSES the sceneprobe comparator (tests/sceneprobe): a golden compare is a
# `latte-imgdiff` run, and the compare RIGOR is the Phase C GoldenTier axis
# (imagecompare.h) resolved in C++ - this bridge never re-derives the
# delta/budget numbers, it hands latte-imgdiff `--tier` and lets the shared
# toleranceForTier table decide.
#
# Sourced by tests/e2e/matrix/matrix-lib.sh (which routes its abort
# frame-compare hook through matrix_frame_equals_baseline -> e2e_golden_compare)
# and directly by any scenario recipe that asserts a committed golden. Needs
# tests/e2e/lib.sh sourced first (e2e_screenshot, e2e_json) and the run-e2e
# env (E2E_REPO, E2E_BUILD, E2E_ARTIFACTS).
#
# ============================ BRIDGE API ===================================
# e2e_golden_compare <actual.png> <expected.png> [diff.png]
#     Compare two PNGs at the interaction golden tier (below). Writes an
#     amplified diff to diff.png when given.
#       return 0  MATCH   1  MISMATCH or MISSING expected   2  setup/usage error
#
# e2e_screenshot_crop <out.png> <WxH+X+Y>
#     A DETERMINISTIC golden shot: the vehicle workspace with the cursor
#     EXCLUDED, cropped to the given rect. Localizes the golden and drops the
#     wallpaper margin.
#
# matrix_view_crop_rect [view-id]
#     The WxH+X+Y screen rect of the view under test (viewsData.absoluteGeometry),
#     the plan's default golden crop. Defaults to the single non-cloned view.
#
# e2e_assert_golden <cell> <verb> <phase> [crop-rect]
#     Shoot + crop the view, compare against the committed golden
#     tests/e2e/goldens/<cell>.<verb>.<phase>.expected.lavapipe.png at the tier.
#     E2E_BLESS=1 (re)writes the golden from the shot and returns 0. A MISSING
#     required golden is a hard FAIL (mirror sceneprobe main.cpp), never a
#     silent skip. Saves actual/expected/diff under E2E_ARTIFACTS on mismatch.
#       return 0  PASS (or blessed)   1  FAIL (mismatch or missing)   2  error
# ===========================================================================

# _golden_tier: the compare rigor for INTERACTION goldens. Defaults to
# 'tolerance', NOT the 'bitexact' the sceneprobe render gate defaults to.
# Sceneprobe renders offscreen through a pinned lavapipe stack with a
# deterministic animation clock, so it is byte-reproducible; the e2e VEHICLE
# dock is HOST-rendered (run-e2e does not force lavapipe on the dock the way
# tests/sceneprobe/run_in_kwin.sh does for the probe), so a live shot carries
# host-GPU rasterization and text-AA variance no bit-exact gate survives
# (open question O3, resolved by Bree 2026-07-18: gate the interaction goldens
# at Tolerance, keep sceneprobe BitExact). The 'lavapipe' in the golden
# FILENAME is the device-keying convention only, kept uniform with sceneprobe;
# it does not claim the dock rendered on lavapipe. SCENEPROBE_TIER is honored
# when a multi-distro leg exports it, and an explicit bitexact override is
# honored too (the lever a future frozen-render-clock investment would use).
_golden_tier() { echo "${SCENEPROBE_TIER:-tolerance}"; }

# e2e_golden_compare <actual> <expected> [diff]: the tier-aware pixel compare.
# Delegates the delta/budget to latte-imgdiff --tier so the numbers live once,
# in the C++ tier table. A missing expected golden is a compare FAIL (1), kept
# distinct from a setup error (2) so the caller can tell "the dock regressed /
# no golden yet" from "the tool is not built".
e2e_golden_compare() {
    local actual="$1" expected="$2" diff="${3:-}"
    local imgdiff="${E2E_BUILD:?}/bin/latte-imgdiff"
    [[ -x "$imgdiff" ]] || { echo "e2e_golden_compare: no latte-imgdiff at $imgdiff (build first)" >&2; return 2; }
    [[ -f "$actual" ]]  || { echo "e2e_golden_compare: no actual image $actual" >&2; return 2; }
    if [[ ! -f "$expected" ]]; then
        echo "e2e_golden_compare: MISSING expected golden $expected (a required golden absent is a hard fail)" >&2
        return 1
    fi
    local args=(--tier)
    [[ -n "$diff" ]] && args+=(--out "$diff")
    SCENEPROBE_TIER="$(_golden_tier)" "$imgdiff" "$actual" "$expected" "${args[@]}"
}

# e2e_screenshot_crop <out> <WxH+X+Y>: shoot the workspace with the cursor
# excluded (so a stray pointer never lands in a golden, the biggest live-shot
# nondeterminism after animation) and crop to the rect. +repage drops the
# virtual canvas offset so the crop is a plain image.
e2e_screenshot_crop() {
    local out="$1" rect="$2" full rc
    full="$(mktemp --suffix=.png)"
    if ! e2e_screenshot "$full" include-cursor b false; then
        rm -f "$full"; return 1
    fi
    magick "$full" -crop "$rect" +repage "$out"; rc=$?
    rm -f "$full"
    return "$rc"
}

# matrix_view_crop_rect [view-id]: the view's screen rect for the default
# golden crop. A scenario that needs the edit-chrome union refines this itself
# (the plan's crop rule); the bare view rect is the common default.
matrix_view_crop_rect() {
    local view="${1:-}"
    { echo "$view"; e2e_json viewsData; } | python3 -c '
import json, sys
want = sys.stdin.readline().strip()
views = json.load(sys.stdin)
if want:
    v = next((v for v in views if v["containmentId"] == int(want)), None)
    if v is None:
        sys.exit("matrix_view_crop_rect: no view %s" % want)
else:
    nc = [v for v in views if not v.get("isCloned")]
    if len(nc) != 1:
        sys.exit("matrix_view_crop_rect: expected exactly one non-cloned view, saw %d" % len(nc))
    v = nc[0]
x, y, w, h = v["absoluteGeometry"]
if w <= 0 or h <= 0:
    sys.exit("matrix_view_crop_rect: degenerate view rect %dx%d" % (w, h))
print("%dx%d+%d+%d" % (w, h, x, y))
'
}

# e2e_assert_golden <cell> <verb> <phase> [crop-rect]: the committed-golden
# assertion scenarios call. Shoots+crops the view, then blesses / hard-fails a
# missing golden / compares at the tier.
e2e_assert_golden() {
    local cell="$1" verb="$2" phase="$3" rect="${4:-}"
    local goldendir="${E2E_REPO:?}/tests/e2e/goldens"
    local golden="$goldendir/$cell.$verb.$phase.expected.lavapipe.png"
    local stem="${E2E_ARTIFACTS:?}/$cell.$verb.$phase"
    local actual="$stem.actual.png"

    if [[ -z "$rect" ]]; then
        rect="$(matrix_view_crop_rect)" || { echo "e2e_assert_golden: could not compute a view crop rect" >&2; return 2; }
    fi
    e2e_screenshot_crop "$actual" "$rect" || { echo "e2e_assert_golden: shot/crop failed for $cell/$verb/$phase" >&2; return 2; }

    if [[ "${E2E_BLESS:-0}" == 1 ]]; then
        mkdir -p "$goldendir"
        cp "$actual" "$golden"
        echo "e2e_assert_golden: BLESSED $golden - verify the pixels by eye before committing"
        return 0
    fi
    if [[ ! -f "$golden" ]]; then
        echo "e2e_assert_golden: MISSING required golden $golden (run once with E2E_BLESS=1, verify by eye, commit)" >&2
        cp "$actual" "$stem.actual-no-golden.png" 2>/dev/null || true
        return 1
    fi
    if e2e_golden_compare "$actual" "$golden" "$stem.diff.png"; then
        echo "e2e_assert_golden: PASS $cell/$verb/$phase matches golden"
        return 0
    fi
    cp "$golden" "$stem.expected.png" 2>/dev/null || true
    echo "e2e_assert_golden: FAIL $cell/$verb/$phase differs from golden (see $actual, $stem.expected.png, $stem.diff.png)" >&2
    return 1
}
