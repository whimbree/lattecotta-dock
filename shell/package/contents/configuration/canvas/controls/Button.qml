/*
    SPDX-FileCopyrightText: 2019 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.7
import QtQuick.Layouts

import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.components 3.0 as PlasmaComponents
import org.kde.kirigami 2.20 as Kirigami

Item{
    id: button
    width: visibleButton.width
    height: visibleButton.height

    signal pressedChanged(bool pressed);

    //! The actually interactive chip: the (invisible) tooltip button fills
    //! exactly this item, so IT is the click target. Hit-area consumers (the
    //! canvas input mask) must map THIS rect, never the outer Item: the outer
    //! width has been observed stretched to near-full row width after chrome
    //! retargeting between views (origin not yet identified, filed in the
    //! plan), while the chip stays content-sized.
    readonly property Item interactiveChip: visibleButtonRoot

    property bool checked: false

    property bool iconPositionReversed: false
    property string text: "Default Text"
    property string tooltip: ""

    readonly property bool containsMouse: tooltipBtn.hovered
    implicitHeight: visibleButton.height

    readonly property color appliedTextColor: checked ? checkedTextColor : textColor
    readonly property color appliedBackgroundColor: checked ? checkedBackgroundColor : backgroundColor
    readonly property color appliedBorderColor: checked ? checkedBorderColor : borderColor

    readonly property color textColor: containsMouse ? latteView.colorizer.buttonTextColor : settingsRoot.textColor
    readonly property color backgroundColor: containsMouse ? hoveredBackground :  normalBackground
    readonly property color borderColor: containsMouse ? hoveredBorder : normalBorder// "transparent"

    readonly property color checkedTextColor: latteView.colorizer.buttonTextColor
    readonly property color checkedBackgroundColor: latteView.colorizer.buttonFocusColor
    readonly property color checkedBorderColor: hoveredBorder //"transparent" //checkedTextColor

    readonly property color normalBackground: Qt.rgba(latteView.colorizer.buttonHoverColor.r,
                                                      latteView.colorizer.buttonHoverColor.g,
                                                      latteView.colorizer.buttonHoverColor.b,
                                                      0.04)

    readonly property color hoveredBackground: Qt.rgba(latteView.colorizer.buttonHoverColor.r,
                                                       latteView.colorizer.buttonHoverColor.g,
                                                       latteView.colorizer.buttonHoverColor.b,
                                                       0.7)

    readonly property color normalBorder: Qt.rgba(settingsRoot.textColor.r,
                                                  settingsRoot.textColor.g,
                                                  settingsRoot.textColor.b,
                                                  0.7)

    readonly property color hoveredBorder: "#222222"

    property Component icon

    Item{
        id: visibleButtonRoot
        width: visibleButton.width
        height: visibleButton.height

        Rectangle {
            id: visibleButton
            width: buttonRow.width + 4 * margin
            height: buttonRow.height + 2 * margin
            radius: 2
            color: appliedBackgroundColor
            border.width: 1
            border.color: appliedBorderColor

            readonly property int margin: Kirigami.Units.smallSpacing

            RowLayout{
                id: buttonRow
                anchors.centerIn: parent
                spacing: Kirigami.Units.smallSpacing
                layoutDirection: iconPositionReversed ? Qt.RightToLeft : Qt.LeftToRight

                Loader {
                    width: height
                    height: textLbl.implicitHeight
                    active: button.icon
                    sourceComponent: button.icon
                    visible: active

                    readonly property color iconColor: button.appliedTextColor
                }

                PlasmaComponents.Label{
                    id: textLbl
                    text: button.text
                    color: button.appliedTextColor
                }
            }
        }
    }

    PlasmaComponents.Button {
        id: tooltipBtn
        anchors.fill: visibleButtonRoot
        opacity: 0

        //! Screen-reader surface (Phase 10 AT-SPI rollout): this invisible
        //! button is the real click target, so it announces the drawn chip -
        //! visible text as name, tooltip as description, checked mirroring
        //! the chip state (every consumer is a toggle). The explicit press
        //! handler is REQUIRED, not optional: the chip's consumers listen to
        //! pressedChanged, and Qt's native AT press on a QQC2 button only
        //! emits clicked() (QQuickAbstractButtonPrivate::accessiblePress-
        //! Action -> trigger(), verified in the pinned 6.11 sources), which
        //! nothing here connects. Declaring the handler also suppresses that
        //! native path (attached handlers win in QAccessibleQuickItem::
        //! doAction), so the press cycle below runs exactly once.
        Accessible.name: button.text
        Accessible.description: button.tooltip
        Accessible.checkable: true
        Accessible.checked: button.checked
        Accessible.onPressAction: {
            //! the same press-then-release cycle a pointer produces
            button.pressedChanged(true);
            button.pressedChanged(false);
        }
        Accessible.onToggleAction: {
            button.pressedChanged(true);
            button.pressedChanged(false);
        }

        onPressedChanged: button.pressedChanged(pressed)
    }

    //! The hint the buttons carry deliberately renders INSIDE this window as
    //! a plain Rectangle, never as an attached QQC2.ToolTip. On Wayland a
    //! QQC2.ToolTip maps its OWN popup surface at the cursor, and in a cramped
    //! edit-mode header that surface lands directly over the button and eats
    //! the click - the "Rearrange..." toggle went unclickable whenever space
    //! was tight (caught live on a top panel, 2026-07-17). This is the same
    //! defect family the edit-handle flicker and the wheel-hint chip already
    //! retired: containment ConfigOverlay.qml and CanvasConfiguration.qml both
    //! ban per-control QQC2.ToolTip and draw the hint in-window instead. A
    //! Rectangle+Label carries no input handler, so it is pointer-transparent:
    //! it shows over the button yet the press falls straight through to
    //! tooltipBtn beneath it. Don't re-add a QQC2.ToolTip here.
    Rectangle {
        id: hintChip
        objectName: "buttonHintChip"
        z: 10
        visible: opacity > 0
        opacity: (hintDwell.dwellCompleted && tooltipBtn.hovered && button.tooltip.length > 0) ? 1 : 0

        //! sits just past the button chip toward the header interior; the
        //! header rotates for vertical docks, so button-local "below" tracks
        //! the correct visual side automatically
        anchors.horizontalCenter: visibleButtonRoot.horizontalCenter
        anchors.top: visibleButtonRoot.bottom
        anchors.topMargin: Kirigami.Units.smallSpacing

        width: hintLabel.width + 4 * Kirigami.Units.smallSpacing
        height: hintLabel.height + 2 * Kirigami.Units.smallSpacing
        radius: Kirigami.Units.smallSpacing
        color: Kirigami.Theme.backgroundColor
        border.width: 1
        border.color: Qt.rgba(Kirigami.Theme.textColor.r,
                              Kirigami.Theme.textColor.g,
                              Kirigami.Theme.textColor.b,
                              0.3)

        Behavior on opacity {
            NumberAnimation { duration: Kirigami.Units.longDuration; easing.type: Easing.OutCubic }
        }

        //! Plasma's tooltip dwell: the hint only appears once the pointer has
        //! rested on the button, so the brief hover bounces the compositor
        //! sends while the edit-mode input mask re-carves never flash it. Same
        //! guard the wheel-hint chip uses.
        Timer {
            id: hintDwell
            objectName: "buttonHintDwell"
            property bool dwellCompleted: false
            interval: Kirigami.Units.toolTipDelay
            running: tooltipBtn.hovered && !dwellCompleted
            onTriggered: dwellCompleted = true
        }

        Connections {
            target: tooltipBtn
            function onHoveredChanged() {
                if (!tooltipBtn.hovered) {
                    hintDwell.dwellCompleted = false;
                }
            }
        }

        PlasmaComponents.Label {
            id: hintLabel
            anchors.centerIn: parent
            //! the rearrange hint is a full sentence; cap the chip width and
            //! wrap so it never grows into a screen-wide stripe
            width: Math.min(implicitWidth, Kirigami.Units.gridUnit * 16)
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            text: button.tooltip
            textFormat: Text.PlainText
            color: Kirigami.Theme.textColor
        }
    }
}
