/*
    SPDX-FileCopyrightText: 2019 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.7
import QtQuick.Controls 2.15 as QQC2
import QtQuick.Layouts

import org.kde.plasma.components 3.0 as PlasmaComponents

import "." as LatteExtraControls
import org.kde.kirigami 2.20 as Kirigami

Item {
    id: item

    Layout.rightMargin: {
        if (level === 1) {
            return Qt.application.layoutDirection === Qt.RightToLeft ? 0 : 2 * Kirigami.Units.smallSpacing
        }

        return 0;
    }
    Layout.leftMargin: {
        if (level === 1) {
            return Qt.application.layoutDirection === Qt.RightToLeft ? 2 * Kirigami.Units.smallSpacing : 0
        }

        return 0;
    }

    property int level:1
    property bool checked: false
    property bool isFirstSubCategory: false

    implicitWidth: row.width

    implicitHeight: {
        if (level === 1) {
            return Math.max(headerText.implicitHeight, itemSwitch.implicitHeight);
        } else if (level === 2) {
            return Math.max(subHeaderText.implicitHeight, itemSwitch.implicitHeight)
        }

        return Math.max(labelText.implicitHeight, itemSwitch.implicitHeight);
    }

    property string text:""
    property string tooltip:""

    signal pressed();

    Item {
        id: row
        width: parent.width
        height: textElement.height
        anchors.verticalCenter: parent.verticalCenter

        RowLayout {
            id: textElement
            anchors.left: level !== 2 ? parent.left : undefined
            anchors.horizontalCenter: level === 2 ? parent.horizontalCenter : undefined
            anchors.verticalCenter: parent.verticalCenter

            LatteExtraControls.Header {
                id: headerText
                text: item.text
                enabled: item.checked && item.enabled
                visible: level === 1
            }

            LatteExtraControls.SubHeader {
                id: subHeaderText
                text: item.text
                enabled: item.checked && item.enabled
                visible: level === 2
                isFirstSubCategory: item.isFirstSubCategory
            }

            PlasmaComponents.Label {
                id: labelText
                text: item.text
                enabled: item.checked && item.enabled
                visible: level > 2
            }
        }

        PlasmaComponents.Button {
            //tooltip ghost
            anchors.fill: textElement
            QQC2.ToolTip.text: item.tooltip
            QQC2.ToolTip.visible: hovered && item.tooltip.length > 0
            opacity: 0
            //! pruned from the screen-reader tree: the switch ghost below
            //! carries the checkbox semantics for the whole header, and an
            //! unnamed twin here would announce the same control twice
            Accessible.ignored: true
            onPressedChanged: {
                if (pressed) {
                    item.pressed();
                }
            }
        }
    }

    LatteExtraControls.Switch {
        id: itemSwitch
        anchors.verticalCenter: row.verticalCenter
        anchors.right: row.right
        checked: item.checked
        enabled: item.enabled
        //! visual only: the ghost button covering it takes every press, so
        //! IT carries the accessible checkbox and this switch is pruned
        Accessible.ignored: true

        PlasmaComponents.Button {
            //tooltip ghost
            id: switchToggleGhost
            objectName: "switchToggleGhost"
            anchors.fill: parent
            QQC2.ToolTip.text: item.tooltip
            QQC2.ToolTip.visible: hovered && item.tooltip.length > 0
            opacity: 0

            //! Screen-reader surface (Phase 10 AT-SPI rollout): the ghost
            //! is the element that really takes the press, so it announces
            //! as the header's checkbox - name from the visible header
            //! text, checked mirroring the switch - and its press/toggle
            //! actions raise the SAME item.pressed() signal the mouse path
            //! raises through onPressedChanged. Pinned offscreen by
            //! tests/qml/tst_accessiblecontrols.qml.
            Accessible.role: Accessible.CheckBox
            Accessible.name: item.text
            Accessible.description: item.tooltip
            Accessible.checkable: true
            Accessible.checked: item.checked
            Accessible.onPressAction: item.pressed()
            Accessible.onToggleAction: item.pressed()

            onPressedChanged: {
                if (pressed) {
                    item.pressed();
                }
            }
        }
    }
}
