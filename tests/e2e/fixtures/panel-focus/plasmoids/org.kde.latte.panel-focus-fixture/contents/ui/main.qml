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

    Layout.minimumWidth: 240
    Layout.preferredWidth: 240
    Layout.maximumWidth: 240
    Layout.minimumHeight: 64
    Layout.preferredHeight: 64
    Layout.maximumHeight: 64

    Shortcut {
        sequence: "F8"
        context: Qt.WindowShortcut
        onActivated: root.Plasmoid.status = PlasmaCore.Types.PassiveStatus
    }

    Shortcut {
        sequence: "F9"
        context: Qt.WindowShortcut
        onActivated: root.Plasmoid.status = PlasmaCore.Types.ActiveStatus
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#2673bf"

            MouseArea {
                anchors.fill: parent
                activeFocusOnTab: true
                onClicked: root.Plasmoid.status = PlasmaCore.Types.AcceptingInputStatus
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#2f9e44"

            MouseArea {
                anchors.fill: parent
                onClicked: root.Plasmoid.status = PlasmaCore.Types.PassiveStatus
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#c92a2a"

            MouseArea {
                anchors.fill: parent
                onClicked: root.Plasmoid.status = PlasmaCore.Types.ActiveStatus
            }
        }
    }
}
