/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.plasmoid

PlasmoidItem {
    id: root

    preferredRepresentation: fullRepresentation

    fullRepresentation: Item {
        Layout.minimumWidth: 80
        Layout.preferredWidth: 80
        Layout.maximumWidth: 80
        Layout.minimumHeight: 48
        Layout.preferredHeight: 48
        Layout.maximumHeight: 48

        Row {
            anchors.centerIn: parent
            spacing: 8

            Rectangle {
                width: 28
                height: 28
                color: Kirigami.Theme.textColor
                antialiasing: false
            }

            Rectangle {
                width: 28
                height: 28
                color: "#d62976"
                antialiasing: false
            }
        }
    }
}
