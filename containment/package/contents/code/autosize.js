/*
    SPDX-FileCopyrightText: 2019 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! The automatic item sizer's shrink/grow stepping, extracted from
//! abilities/AutoSize.qml so the loops are drivable headlessly
//! (tests/qml/tst_autosize.qml pins their termination). Behavior is the
//! ad9b823f form: both loops clamp to their bound and exit on inequality
//! (16 floor, maxIconSize ceiling). The equality exit forms (!== 16,
//! !== maxIconSize) inherited from upstream 747d4870 stepped right past
//! their bound for sizes not congruent modulo the step and spun forever.

.pragma library

//! the sizer never proposes anything below this floor
var minIconSize = 16;

//! One shrink pass: step candidate sizes down from maxIconSize until the
//! projected layout length fits in toShrinkLimit or the floor is reached.
//! currentIconSize is the size the present layoutLength was measured at,
//! so factor = candidate / currentIconSize projects the length.
//! Returns {iconSize, nextLength}.
function shrinkStep(maxIconSize, currentIconSize, layoutLength, toShrinkLimit, step) {
    var nextIconSize = maxIconSize;
    var nextLength;

    do {
        nextIconSize = Math.max(minIconSize, nextIconSize - step);
        var factor = nextIconSize / currentIconSize;
        nextLength = factor * layoutLength;

    } while ((nextLength > toShrinkLimit) && (nextIconSize > minIconSize));

    return { iconSize: nextIconSize, nextLength: nextLength };
}

//! One grow pass: step candidate sizes up from currentIconSize as long as
//! the projected length stays under toGrowLimit, saturating at maxIconSize.
//! foundGoodSize is -1 when not even the first step fits.
//! Returns {foundGoodSize, nextLength}.
function growStep(maxIconSize, currentIconSize, layoutLength, toGrowLimit, step) {
    var nextIconSize = currentIconSize;
    var foundGoodSize = -1;
    var nextLength;

    do {
        nextIconSize = Math.min(maxIconSize, nextIconSize + step);
        var factor = nextIconSize / currentIconSize;
        nextLength = factor * layoutLength;

        if (nextLength < toGrowLimit) {
            foundGoodSize = nextIconSize;
        }
    } while ((nextLength < toGrowLimit) && (nextIconSize < maxIconSize));

    return { foundGoodSize: foundGoodSize, nextLength: nextLength };
}
