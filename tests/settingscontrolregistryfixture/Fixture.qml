/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick

Item {
    width: 240
    height: 180
    focus: true

    Item {
        objectName: "clipper"
        x: 20
        y: 20
        width: 100
        height: 80
        clip: true

        Item {
            objectName: "controlA"
            x: 10
            y: 10
            width: 40
            height: 20
            property var currentValue: "alpha"

            Item {
                objectName: "focusChild"
                width: 10
                height: 10
                focus: true
            }
        }

        Item {
            objectName: "controlB"
            x: 70
            y: 35
            width: 60
            height: 30
            property var currentValue: true
        }
    }

    Item {
        objectName: "hiddenAncestor"
        visible: false

        Item {
            objectName: "hiddenControl"
            x: 130
            y: 10
            width: 30
            height: 20
            property var currentValue: 4
        }
    }

    Item {
        objectName: "disabledAncestor"
        enabled: false

        Item {
            objectName: "disabledControl"
            x: 130
            y: 40
            width: 30
            height: 20
            property var currentValue: 5.5
        }
    }

    Item {
        id: popupControl
        objectName: "popupControl"
        x: 130
        y: 70
        width: 30
        height: 20
        property var currentValue: "closed"
        property bool popupOpen: false
    }

    Item {
        objectName: "popupItem"
        x: 135
        y: 95
        width: 90
        height: 70
        visible: popupControl.popupOpen

        Item {
            objectName: "popupRow"
            x: 5
            y: 5
            width: 70
            height: 20
            property var currentValue: "first"
        }

        Item {
            objectName: "popupSeparator"
            x: 5
            y: 30
            width: 70
            height: 4
            enabled: false
            property var currentValue: null
        }
    }
}
