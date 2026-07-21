/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Layouts
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid

PlasmoidItem {
    id: root

    readonly property bool horizontal: Plasmoid.formFactor === PlasmaCore.Types.Horizontal
    property int testLength: 160

    Layout.minimumWidth: horizontal ? testLength : 48
    Layout.preferredWidth: Layout.minimumWidth
    Layout.maximumWidth: Layout.minimumWidth
    Layout.minimumHeight: horizontal ? 48 : testLength
    Layout.preferredHeight: Layout.minimumHeight
    Layout.maximumHeight: Layout.minimumHeight

    Rectangle {
        anchors.fill: parent
        color: "#4f8fba"

        // AppletItem selects a Latte separator's first child as the
        // increaseLength/decreaseLength interface used by ConfigOverlay.
        function increaseLength() {
            root.testLength += 8;
        }

        function decreaseLength() {
            root.testLength -= 8;
        }
    }
}
