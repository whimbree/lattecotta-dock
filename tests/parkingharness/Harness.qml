/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Containment-root stand-in for layoutmanagerparkingtest.cpp. LayoutManager
//! talks to the containment main.qml exclusively through three invokable
//! functions resolved by metaobject signature (createAppletItem(QVariant),
//! initAppletContainer(QVariant,QVariant), createJustifySplitter()) and the
//! layout items handed to its Q_PROPERTYs; this harness provides exactly
//! that surface. The container mirrors the AppletItem.qml properties
//! LayoutManager reads (applet, isInternalViewSplitter,
//! isParabolicEdgeSpacer) plus an initCount probe so the undo re-init path
//! (m_initAppletContainerMethod) is observable.

import QtQuick

Item {
    id: root
    width: 1000
    height: 48

    function createAppletItem(applet) {
        return containerComponent.createObject(root, { applet: applet });
    }

    function initAppletContainer(container, applet) {
        container.applet = applet;
        container.initCount = container.initCount + 1;
    }

    function createJustifySplitter() {
        return containerComponent.createObject(root, { isInternalViewSplitter: true });
    }

    Component {
        id: containerComponent

        Item {
            objectName: "appletContainer"
            width: 48
            height: 48

            property Item applet
            property bool isInternalViewSplitter: false
            property bool isParabolicEdgeSpacer: false
            property int initCount: 0
        }
    }

    Row { objectName: "startLayout" }
    Row { objectName: "mainLayout" }
    Row { objectName: "endLayout" }

    Item { objectName: "dndSpacer" }
}
