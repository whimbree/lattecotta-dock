/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Per-item mock scope around the REAL containment ParabolicArea.qml.
// The real file resolves appletItem/wrapper/communicator/applet/root/
// parabolic/... through the QML context chain, exactly as in production;
// only inert environment is mocked, every routing decision runs the real
// shipped code.
import QtQuick 2.7

import "../../../containment/package/contents/ui/applet" as RealApplet

Item {
    id: appletItem

    property int index: -1
    property bool isSeparator: false
    property bool isMarginsAreaSeparator: false
    property bool isHidden: false
    property bool isSpacer: false
    property bool parabolicEffectIsSupported: true
    property bool originalAppletBehavior: false
    property bool firstAppletInContainer: false
    property bool lastAppletInContainer: false
    property bool isBridgeClient: false
    //! mirrors AppletItem's parabolicAreaLoader.active: when false the item
    //! has NO ParabolicArea instance, so no slots are connected - the
    //! production case is a zoom-unsupported applet (systray, autofill,
    //! wide applets: lockZoom) with thin tooltips disabled
    property bool hasParabolicArea: true

    property var parabolic: null   // assigned by the harness (the hub)
    property var indexer: null
    property var layouts: null
    property var animations: null
    property var thinTooltip: null
    property var tooltipVisualParent: null

    readonly property real zoomScale: wrapper.zoomScale
    function resetScale() { wrapper.zoomScale = 1; }
    function presetScale(v) { wrapper.zoomScale = v; }

    property var receivedLower: []
    property var receivedHigher: []
    function resetClientLog() { receivedLower = []; receivedHigher = []; }

    QtObject {
        id: mockMyView
        property bool isShownFully: true
        property bool isReady: true
    }
    property var myView: mockMyView

    QtObject {
        id: mockPlasmoid
        property int status: 2 // PlasmaCore.Types.ActiveStatus
    }
    QtObject {
        id: applet
        property var plasmoid: mockPlasmoid
    }

    Item {
        id: wrapper
        property real zoomScale: 1
    }

    QtObject {
        id: mockRequires
        property bool parabolicEffectLocked: false
    }
    QtObject {
        id: mockClient
        function hostRequestUpdateLowerItemScale(newScales) {
            appletItem.receivedLower.push(JSON.parse(JSON.stringify(newScales)));
        }
        function hostRequestUpdateHigherItemScale(newScales) {
            appletItem.receivedHigher.push(JSON.parse(JSON.stringify(newScales)));
        }
    }
    QtObject {
        id: mockParabolicBridgeEntry
        property var client: mockClient
    }
    QtObject {
        id: mockBridge
        property var parabolic: mockParabolicBridgeEntry
    }
    property Item communicator: Item {
        id: communicatorItem
        property bool parabolicEffectIsSupported: appletItem.isBridgeClient
        property bool indexerIsSupported: false
        property var requires: mockRequires
        property var bridge: mockBridge
    }

    Item {
        id: parabolicAreaLoader
        property bool hasParabolicMessagesEnabled: true
        property bool isParabolicEnabled: true
        property bool isThinTooltipEnabled: false
    }

    ParallelAnimation {
        id: restoreAnimation
    }

    Loader {
        active: appletItem.hasParabolicArea
        anchors.fill: parent
        sourceComponent: RealApplet.ParabolicArea {}
    }
}
