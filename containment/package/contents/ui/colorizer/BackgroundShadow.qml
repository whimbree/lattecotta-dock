/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Effects

//! A background shadow is a sibling of its translucent source. Applying a
//! layer effect to the source would multiply the shadow by source opacity.
//! RectangularShadow instead renders only the rounded shadow and defines its
//! scene-graph footprint in pixels, independent of the rectangle aspect ratio.
RectangularShadow {
    offset: Qt.vector2d(0, 0)
    spread: 0
}
