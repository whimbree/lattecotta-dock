/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.0
import QtQuick.Layouts

import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.ksvg 1.0 as KSvg

KSvg.FrameSvgItem {
    id: root

    //! The Latte containment paints its own background (background/MultiLayered.qml) and asks
    //! for Plasmoid.backgroundHints: NoBackground. On Plasma 6 the containment graphic object
    //! no longer carries a backgroundHints property, so the old "draw panel-background unless
    //! NoBackground" check resolved to undefined and fell back to the SVG, painting it across
    //! the whole oversized view. X11 hid that overflow with the visual shape-mask; Wayland has
    //! no such mask, so it showed up as a dark band over the parabolic-zoom reserve. Latte never
    //! wants this wrapper background, so keep it empty and let the containment own all drawing.
    imagePath: ""
    prefix:""
    // onRepaintNeeded: adjustPrefix();

    property Item containment
    property Item viewLayout

    readonly property bool verticalPanel: containment && containment.formFactor === PlasmaCore.Types.Vertical

    /*  Rectangle{
        anchors.fill: parent
        color: "transparent"
        border.color: "blue"
        border.width: 1
    }*/

    readonly property var containmentApplet: containment && containment.plasmoid ? containment.plasmoid : containment

    function adjustPrefix() {
        if (!containmentApplet) {
            return "";
        }
        var pre;
        switch (containmentApplet.location) {
        case PlasmaCore.Types.LeftEdge:
            pre = "west";
            break;
        case PlasmaCore.Types.TopEdge:
            pre = "north";
            break;
        case PlasmaCore.Types.RightEdge:
            pre = "east";
            break;
        case PlasmaCore.Types.BottomEdge:
            pre = "south";
            break;
        default:
            prefix = "";
        }
        if (hasElementPrefix(pre)) {
            prefix = pre;
        } else {
            prefix = "";
        }
    }

    Component.onDestruction: {
        console.log("latte view qml source deleting...");
    }

    Connections {
        target: root.containmentApplet
        function onLocationChanged() {
            root.adjustPrefix();
        }
    }

    onContainmentChanged: {
        console.log("latte view qml source - containment changed 1...");
        if (!containment) {
            return;
        }
        console.log("latte view qml source - containment changed 2...");

        containment.parent = containmentParent;
        containment.visible = true;
        containment.anchors.fill = containmentParent;
        adjustPrefix();

        //! Plasma 6: containment roots must BE ContainmentItem/PlasmoidItem
        //! types, so the item handed to us IS the containment's own root (it
        //! carries the containmentViewLayout objectName itself) instead of
        //! wrapping it as a child like Plasma 5 did. Check the item first,
        //! keep the child scan for safety.
        if (containment.objectName === "containmentViewLayout") {
            viewLayout = containment;
        } else {
            for(var i=0; i<containment.children.length; ++i){
                if (containment.children[i].objectName === "containmentViewLayout") {
                    viewLayout = containment.children[i];
                }
            }
        }

        if (!viewLayout) {
            console.warn("latte view qml source - containmentViewLayout NOT found;",
                         "applet-position lookups (right-click menus) will be broken");
        }
    }

    Item {
        id: containmentParent
        anchors.fill: parent
    }

    //! it is used in order to check the right click position
    //! the only way to take into account the visual appearance
    //! of the applet (including its spacers)
    function appletContainsPos(appletId, pos) {
        if (viewLayout) {
            return viewLayout.appletContainsPos(appletId, pos);
        }

        return false;
    }
}
