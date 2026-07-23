/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

pragma Singleton

import QtQml

//! Stateless effect geometry shared by renderers and their layout owners.
QtObject {
    readonly property int postBlurGuardPx: 2

    function shadowBlurFor(sizePx: real, blurMaxPx: real): real {
        if (sizePx <= 0 || blurMaxPx <= 0) {
            return 0.0;
        }

        return Math.min(1.0, sizePx / blurMaxPx);
    }

    function shadowPaddingFor(sizePx: real,
                              horizontalOffset: real,
                              verticalOffset: real): int {
        const greatestOffset = Math.max(Math.abs(horizontalOffset),
                                        Math.abs(verticalOffset));
        return Math.ceil(sizePx + greatestOffset + postBlurGuardPx);
    }
}
