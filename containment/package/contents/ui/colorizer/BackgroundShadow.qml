/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick

import org.kde.latte.components 1.0 as LatteComponents

//! Background shadows have no directional offset. ShadowedItem supplies a
//! fixed pixel padding on both axes, unlike Kirigami.ShadowedRectangle's
//! aspect-ratio-scaled render node.
LatteComponents.ShadowedItem {
    shadowVerticalOffset: 0
}
