/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick

import org.kde.kirigami 2.20 as Kirigami
import org.kde.latte.core 0.2 as LatteCore

Kirigami.Icon {
    id: icon

    property var iconSource

    // Task layout slots are already the authoritative fitted size. Kirigami's
    // standard-size rounding can paint a 48 px icon inside a 63 px slot and
    // make automatic sizing appear to leave space unused.
    roundToIconSize: false
    source: iconSource

    Connections {
        target: LatteCore.Environment

        function onIconThemeChanged() {
            // Preserve the original QVariant in iconSource. Rebinding source
            // synchronously invalidates Kirigami.Icon's raster without an
            // event-loop interval in which an empty icon can be rendered.
            icon.source = "";
            icon.source = Qt.binding(() => icon.iconSource);
        }
    }
}
