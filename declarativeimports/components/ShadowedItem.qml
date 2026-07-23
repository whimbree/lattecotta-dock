/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Effects

// A MultiEffect preconfigured as a drop shadow. The root IS a MultiEffect, so
// callers set shadowColor/source/shadowHorizontalOffset/shadowVerticalOffset
// directly; this only adds shadowSizePx (the old px radius) and the blur ceiling.
MultiEffect {
    id: root
    property real shadowSizePx: 0      // == old DropShadow.radius in px
    // Pixel radius at which shadowBlur saturates to 1.0 (shadowSizePx == blurMaxPx
    // -> full blur). Sits above the largest itemShadow.size (0.5 * the 512px
    // icon-size cap = 256), so big-icon shadows scale instead of clamping early.
    property int  blurMaxPx: 256

    // STATIC padding, never autoPaddingEnabled. With autoPadding the effect
    // recomputes its padding continuously and re-dirties itself, so every
    // window carrying an applet shadow re-rendered EMPTY frames forever
    // (measured: 18.2% idle CPU on the main thread, ~19,500 failing statx/s
    // from per-frame theme lookups, both flat even with the docks hidden;
    // 0.1% with static padding, bisected across three probe builds).
    // autoPadding also pads by blurMax*(1+blurMultiplier) = 256px PER SIDE
    // around every applet regardless of the actual shadow size; the static
    // rect pads by what the shadow can actually paint instead.
    //
    // paddingRect semantics per Qt's updateSourcePadding() (qtdeclarative
    // qquickmultieffect.cpp): x/y/width/height are the EXTRA pixels on the
    // left/top/right/bottom respectively - per side values, NOT totals.
    // Getting that wrong draws the source scaled and offset inside itself
    // (caught live: a smaller ghost copy of every applet).
    readonly property int shadowPaddingPx: EffectMetrics.shadowPaddingFor(
                                               shadowSizePx,
                                               shadowHorizontalOffset,
                                               shadowVerticalOffset)
    autoPaddingEnabled: false
    paddingRect: Qt.rect(shadowPaddingPx, shadowPaddingPx, shadowPaddingPx, shadowPaddingPx)

    shadowEnabled: true
    shadowOpacity: 1.0
    shadowHorizontalOffset: 0
    // Default matches the common old DropShadow.verticalOffset: 2; sites whose
    // old shadow used a different offset override this.
    shadowVerticalOffset: 2
    blurMax: blurMaxPx
    shadowBlur: EffectMetrics.shadowBlurFor(shadowSizePx, blurMaxPx)
}
