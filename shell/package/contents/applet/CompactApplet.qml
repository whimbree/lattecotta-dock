/*
    SPDX-FileCopyrightText: 2013 Marco Martin <mart@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick
import QtQuick.Layouts 1.1
import QtQuick.Window 2.0
import QtQuick.Effects

import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.ksvg 1.0 as KSvg
import org.kde.kirigami 2.20 as Kirigami
import org.kde.plasma.components 3.0 as PlasmaComponents
import org.kde.plasma.plasmoid
import org.kde.kquickcontrolsaddons 2.0

import org.kde.latte.core 0.2 as LatteCore

PlasmaCore.ToolTipArea {
    id: root
    objectName: "org.kde.desktop-CompactApplet"
    anchors.fill: parent

    mainText: hostedApplet ? hostedApplet.toolTipMainText : ""
    subText: hostedApplet ? hostedApplet.toolTipSubText : ""
    location: hostedPlasmoid ? hostedPlasmoid.location : PlasmaCore.Types.Floating
    active: hostedApplet ? !hostedApplet.expanded : true
    textFormat: hostedApplet ? hostedApplet.toolTipTextFormat : Text.AutoText
    mainItem: hostedApplet && hostedApplet.toolTipItem ? hostedApplet.toolTipItem : null

    property Item fullRepresentation: null
    property Item compactRepresentation: null

    //! Assigned by AppletQuickItem when it instantiates this expander (Plasma 6
    //! API; plasma-desktop's own CompactApplet.qml relies on it the same way).
    //! It is the applet's PlasmoidItem: carries the representation-side members
    //! (expanded, toolTip*, hideOnWindowDeactivate). Its .plasmoid is the
    //! Plasma::Applet (location/status/containmentDisplayHints, actions).
    property PlasmoidItem plasmoidItem

    readonly property Item hostedApplet: plasmoidItem
    readonly property QtObject hostedPlasmoid: hostedApplet ? hostedApplet.plasmoid : null

    //! Latte's containment AppletItem that hosts this applet, found by walking
    //! up to its isLatteAppletContainer marker. Never derive it from a
    //! representation item's parent chain: representations are destroyed and
    //! recreated at runtime on Plasma 6 (e.g. a zoom crossing an applet's
    //! switchWidth expands it), and a fixed-hop walk from a churned item lands
    //! on arbitrary objects. Reading .parent in the loop makes the binding
    //! re-evaluate if any ancestor is reparented.
    readonly property Item appletItem: {
        let item = root.parent;
        while (item) {
            if (item.isLatteAppletContainer === true) {
                return item;
            }
            item = item.parent;
        }
        return null;
    }

    //! The previously adopted representations. Plasma 6's AppletQuickItem owns
    //! representation items and swaps them at runtime (the comic applet does so
    //! on every parabolic zoom past its switchWidth); when that happens the
    //! outgoing item must be released cleanly (anchors and size bindings
    //! neutralized, and for the full representation: returned to this window)
    //! or its scenegraph nodes are torn down while another window's render
    //! pass still walks them.
    property Item _adoptedCompactRep: null
    property Item _adoptedFullRep: null

    onCompactRepresentationChanged: {
        if (compactRepresentation === _adoptedCompactRep) {
            return;
        }

        if (_adoptedCompactRep) {
            //! release our anchors and break the size bindings; do not touch
            //! its parent - AppletQuickItem owns it and may cache and revive it
            _adoptedCompactRep.anchors.centerIn = undefined;
            _adoptedCompactRep.width = _adoptedCompactRep.width;
            _adoptedCompactRep.height = _adoptedCompactRep.height;
        }

        if (compactRepresentation) {
            compactRepresentation.parent = root;
            compactRepresentation.anchors.centerIn = root;
            compactRepresentation.width = Qt.binding(function() {
                return root.width;
            });

            compactRepresentation.height = Qt.binding(function() {
                return root.height;
            });

            compactRepresentation.visible = true;
        }

        _adoptedCompactRep = compactRepresentation;
        root.visible = true;
    }

    onFullRepresentationChanged: {
        if (_adoptedFullRep && _adoptedFullRep !== fullRepresentation) {
            //! bring the outgoing item's scenegraph nodes back to the dock
            //! window BEFORE AppletQuickItem tears the item down: destroying it
            //! while it still lives in the popup dialog's scenegraph is exactly
            //! the buildRenderLists SIGSEGV the comic applet's zoom-induced
            //! representation churn reproduced
            _adoptedFullRep.anchors.fill = null;
            _adoptedFullRep.visible = false;
            _adoptedFullRep.parent = root;
        }
        _adoptedFullRep = fullRepresentation;

        if (!fullRepresentation) {
            return;
        }

        //! Plasma 6 popup sizing contract, from libplasma v6.6.5
        //! appletpopup.cpp (LayoutChangedProxy): the representation's
        //! IMPLICIT size is the live base, Layout.preferred overrides it
        //! when set, Layout.minimum is enforced. The Qt5-era chain here
        //! sampled these ONCE at representation-change time and picked a
        //! branch; Plasma 6 applets grow their implicit size as content
        //! builds AFTER that moment (probe on org.kde.plasma.volume:
        //! implicit 0x0 at rep-change, 213x108 by first show), so the
        //! chain fell through to binding mainItem.width to the rep's LIVE
        //! width - a feedback loop, since the rep is anchored to fill the
        //! mainItem - and latched the 58px compact size it still carried.
        //! The dialog's minimum enforcement then dragged the window to
        //! 260x152 and the volume popup rendered with wrapping tabs and
        //! clipped content. Everything below is a live binding, so the
        //! popup tracks the implicit size as the content settles.
        popupWindow.mainItem.width = Qt.binding(function() {
            if (!fullRepresentation) {
                return _mSize.advanceWidth * 35;
            }
            var pref = fullRepresentation.Layout ? fullRepresentation.Layout.preferredWidth : -1;
            var min = fullRepresentation.Layout ? fullRepresentation.Layout.minimumWidth : 0;
            var base = pref > 0 ? pref : fullRepresentation.implicitWidth;
            if (base <= 0) {
                //! reps that publish no hints at all (Qt5-era fallback)
                base = _mSize.advanceWidth * 35;
            }
            return Math.max(base, min);
        })

        popupWindow.mainItem.height = Qt.binding(function() {
            if (!fullRepresentation) {
                return _mSize.height * 25;
            }
            var pref = fullRepresentation.Layout ? fullRepresentation.Layout.preferredHeight : -1;
            var min = fullRepresentation.Layout ? fullRepresentation.Layout.minimumHeight : 0;
            var base = pref > 0 ? pref : fullRepresentation.implicitHeight;
            if (base <= 0) {
                base = _mSize.height * 25;
            }
            return Math.max(base, min);
        })

        fullRepresentation.anchors.fill = null;
        fullRepresentation.parent = appletParent;
        fullRepresentation.anchors.fill = appletParent;
    }

   /* KSvg.FrameSvgItem {
        id: expandedItem
        anchors.fill: parent
        imagePath: "widgets/tabbar"
        visible: fromCurrentTheme && opacity > 0
        prefix: {
            var prefix;
            switch (hostedPlasmoid ? hostedPlasmoid.location : PlasmaCore.Types.Floating) {
                case PlasmaCore.Types.LeftEdge:
                    prefix = "west-active-tab";
                    break;
                case PlasmaCore.Types.TopEdge:
                    prefix = "north-active-tab";
                    break;
                case PlasmaCore.Types.RightEdge:
                    prefix = "east-active-tab";
                    break;
                default:
                    prefix = "south-active-tab";
                }
                if (!hasElementPrefix(prefix)) {
                    prefix = "active-tab";
                }
                return prefix;
            }
        opacity: hostedApplet && hostedApplet.expanded ? 1 : 0
        Behavior on opacity {
            NumberAnimation {
                duration: Kirigami.Units.shortDuration
                easing.type: Easing.InOutQuad
            }
        }
    }*/

    TextMetrics {
        id: _mSize
        text: "M"
        font: Kirigami.Theme.defaultFont
    }

    //! This timer is needed in order for the applet popup to not reshow instantly and faulty after the user
    //! clicks compact representation to hide it
    Timer {
        id: expandedSync
        interval: 500
        onTriggered: {
            if (hostedApplet) {
                hostedApplet.expanded = popupWindow.visible;
            }
        }
    }

    Connections {
        target: hostedPlasmoid ? hostedPlasmoid.internalAction("configure") : null
        function onTriggered() {
            if (hostedApplet) {
                hostedApplet.expanded = false;
            }
        }
    }

    Connections {
        target: hostedPlasmoid
        function onContextualActionsAboutToShow() { root.hideToolTip() }
    }

    LatteCore.Dialog {
        id: popupWindow
        objectName: "popupWindow"
        flags: Qt.WindowStaysOnTopHint
        //! the plasma surface role plasmashell's own applet popups carry.
        //! Without it the default Normal type ends up Dock-classified by
        //! KWin (popupWindow=false, specialWindow=true - read live from a
        //! KWin windowAdded probe) and the sliding-popups effect never
        //! animates the window even with valid kde-slide data on the
        //! surface; plasmashell's popups classify popupWindow=true and
        //! slide. Same fix keeps every other popup behavior plasma-aligned.
        type: PlasmaCore.Dialog.AppletPopup
        visible: (hostedApplet && hostedApplet.expanded) && fullRepresentation
        visualParent: compactRepresentation ? compactRepresentation : null
       // location: PlasmaCore.Types.Floating //hostedPlasmoid.location
        edge: hostedPlasmoid ? hostedPlasmoid.location : PlasmaCore.Types.Floating /*this way dialog borders are not updated and it is used only for adjusting dialog position*/
        hideOnWindowDeactivate: hostedApplet ? hostedApplet.hideOnWindowDeactivate : false
        backgroundHints: (hostedPlasmoid && (hostedPlasmoid.containmentDisplayHints & PlasmaCore.Types.ContainmentPrefersOpaqueBackground)) ? PlasmaCore.Dialog.SolidBackground : PlasmaCore.Dialog.StandardBackground

        property var oldStatus: PlasmaCore.Types.UnknownStatus

        //It's a MouseEventListener to get all the events, so the eventfilter will be able to catch them
        mainItem: MouseEventListener {
            id: appletParent

            focus: true

            Keys.onEscapePressed: {
                if (hostedApplet) {
                    hostedApplet.expanded = false;
                }
            }

            LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
            LayoutMirroring.childrenInherit: true

            Layout.minimumWidth: (fullRepresentation && fullRepresentation.Layout) ? fullRepresentation.Layout.minimumWidth : 0
            Layout.minimumHeight: (fullRepresentation && fullRepresentation.Layout) ? fullRepresentation.Layout.minimumHeight: 0

            Layout.preferredWidth: (fullRepresentation && fullRepresentation.Layout) ? fullRepresentation.Layout.preferredWidth : -1
            Layout.preferredHeight: (fullRepresentation && fullRepresentation.Layout) ? fullRepresentation.Layout.preferredHeight: -1

            Layout.maximumWidth: (fullRepresentation && fullRepresentation.Layout) ? fullRepresentation.Layout.maximumWidth : Infinity
            Layout.maximumHeight: (fullRepresentation && fullRepresentation.Layout) ? fullRepresentation.Layout.maximumHeight: Infinity

            onActiveFocusChanged: {
                if (activeFocus && fullRepresentation) {
                    fullRepresentation.forceActiveFocus()
                }
            }
        }

        onVisibleChanged: {
            if (!visible) {
                expandedSync.restart();
                if (hostedPlasmoid) {
                    hostedPlasmoid.status = oldStatus;
                }
            } else {
                if (hostedPlasmoid) {
                    oldStatus = hostedPlasmoid.status;
                    hostedPlasmoid.status = PlasmaCore.Types.RequiresAttentionStatus;
                }
                // This call currently fails and complains at runtime:
                // QWindow::setWindowState: QWindow::setWindowState does not accept Qt::WindowActive
                popupWindow.requestActivate();
            }
        }
    }

    ////Indicators API ////
    Binding {
        target: compactRepresentation ? compactRepresentation.anchors : null
        property: "horizontalCenterOffset"
        when: compactRepresentation
        value: appletItem ? appletItem.iconOffsetX : 0
        restoreMode: Binding.RestoreNone
    }

    Binding {
        target: compactRepresentation ? compactRepresentation.anchors : null
        property: "verticalCenterOffset"
        when: compactRepresentation
        value: appletItem ? appletItem.iconOffsetY : 0
        restoreMode: Binding.RestoreNone
    }

    Binding {
        target: root
        property: "transformOrigin"
        value: appletItem && compactRepresentation ? appletItem.iconTransformOrigin : Item.Center
    }

    Binding {
        target: root
        property: "opacity"
        value: appletItem && compactRepresentation ? appletItem.iconOpacity : 1.0
    }

    Binding {
        target: root
        property: "rotation"
        value: appletItem && compactRepresentation ? appletItem.iconRotation : 0
    }

    Binding {
        target: root
        property: "scale"
        value: appletItem && compactRepresentation ? appletItem.iconScale : 1.0
    }

    //! provider duty for the clicked flash: Qt6 MultiEffect does not
    //! auto-wrap plain items (family 73da8400), and this effect sampled the
    //! unlayered compact representation since the port - a rendering no-op
    //! and corruption vector. The layer tracks the animation; alwaysRunToEnd
    //! keeps it alive through the whole flash.
    Binding {
        target: compactRepresentation
        property: "layer.enabled"
        when: compactRepresentation !== null && clickedAnimation.running
        value: true
        restoreMode: Binding.RestoreBindingOrValue
    }

    ////Clicked Effect ////
    MultiEffect {
        id: _clickedEffect
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: compactRepresentation ? compactRepresentation.anchors.horizontalCenterOffset : 0
        anchors.verticalCenterOffset: compactRepresentation ? compactRepresentation.anchors.verticalCenterOffset : 0
        source: compactRepresentation
        width: root.width
        height: root.height
        visible: appletItem && clickedAnimation.running && !appletItem.indicators.info.providesClickedAnimation
        z:1000
    }

    /////Clicked Animation/////
    SequentialAnimation{
        id: clickedAnimation
        alwaysRunToEnd: true
        running: appletItem
                 && appletItem.animations
                 && appletItem.indicators
                 && appletItem.isSquare
                 && appletItem.pressed
                 && !appletItem.originalAppletBehavior
                 && (appletItem.animations.speedFactor.current > 0)
                 && !appletItem.indicators.info.providesClickedAnimation

        ParallelAnimation{
            PropertyAnimation {
                target: _clickedEffect
                property: "brightness"
                to: -0.35
                duration: appletItem && appletItem.animations ? appletItem.animations.duration.large : 0
                easing.type: Easing.OutQuad
            }
        }
        ParallelAnimation{
            PropertyAnimation {
                target: _clickedEffect
                property: "brightness"
                to: 0
                duration: appletItem && appletItem.animations ? appletItem.animations.duration.large : 0
                easing.type: Easing.OutQuad
            }
        }
    }
    //END animations
}
