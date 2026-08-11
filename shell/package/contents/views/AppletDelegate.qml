/*
    SPDX-FileCopyrightText: 2011 Marco Martin <mart@kde.org>
    SPDX-FileCopyrightText: 2015 Kai Uwe Broulik <kde@privat.broulik.de>
    SPDX-FileCopyrightText: 2021 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

import QtQuick 2.15
import QtQuick.Layouts

import org.kde.plasma.components 3.0 as PlasmaComponents
import org.kde.plasma.extras 2.0 as PlasmaExtras
import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.kirigami 2.0 as Kirigami
import org.kde.kwindowsystem 1.0
import org.kde.draganddrop 2.0

Item {
    id: delegate

    //! required-properties mode: the view fills these directly, and bare
    //! context-injected role reads are OFF once any required property
    //! exists - so every role read below goes through `model` (a root
    //! property, resolvable by the qmllint ratchet) or `delegate.index`
    required property var model
    required property int index

    readonly property real badgeHeightRatio: 1.3
    readonly property string pluginName: model.pluginName
    readonly property bool pendingUninstall: pendingUninstallTimer.applets.indexOf(pluginName) > -1
    readonly property int runningInstances: main.runningInstancesFor(pluginName)

    width: list.cellWidth
    height: list.cellHeight

    //! Screen-reader surface (Phase 10 AT-SPI rollout): the whole card is
    //! one button that adds the widget - name and description straight
    //! from the model fields the card draws, the press action forwarding
    //! to the same addWidget() body the tap runs. Accessible.focused
    //! follows the grid's current item, the same highlight the keyboard
    //! moves. Pinned offscreen by tests/qml/tst_addwidgetsaccessible.qml.
    Accessible.role: Accessible.Button
    Accessible.name: model.name
    Accessible.description: model.description
    Accessible.focusable: true
    Accessible.focused: delegate.GridView.isCurrentItem === true
    Accessible.onPressAction: delegate.addWidget()

    //! adds this widget to the dock - the one body shared by the tap
    //! handler and the screen-reader press action
    function addWidget() {
        main.addApplet(delegate.pluginName);
        main.scheduleRunningCountRefresh();
    }

    HoverHandler {
        onHoveredChanged: if (hovered) delegate.GridView.view.currentIndex = delegate.index
    }

    DragArea {
        anchors.fill: parent
        supportedActions: Qt.MoveAction | Qt.LinkAction
        delegateImage: delegate.model.decoration
        enabled: !delegate.pendingUninstall
        mimeData {
            source: parent
        }
        Component.onCompleted: mimeData.setData("text/x-plasmoidservicename", pluginName)

        onDragStarted: {
            KWindowSystem.showingDesktop = true;
            main.draggingWidget = true;
        }
        onDrop: {
            main.draggingWidget = false;
        }

        // Single-click to add widget, matching standard Plasma shell behaviour.
        // TapHandler coexists with DragArea — TapHandler handles taps,
        // DragArea handles drags. They don't interfere with each other.
        TapHandler {
            id: tapHandler
            enabled: !delegate.pendingUninstall
            onTapped: delegate.addWidget()
        }

        ColumnLayout {
            id: mainLayout
            spacing: Kirigami.Units.smallSpacing
            anchors {
                left: parent.left
                right: parent.right
                margins: Kirigami.Units.smallSpacing * 2
                rightMargin: Kirigami.Units.smallSpacing * 2
                top: parent.top
            }

            Item {
                id: iconContainer
                width: Kirigami.Units.iconSizes.enormous
                height: width
                Layout.alignment: Qt.AlignHCenter
                opacity: delegate.pendingUninstall ? 0.6 : 1
                Behavior on opacity {
                    OpacityAnimator {
                        duration: Kirigami.Units.longDuration
                        easing.type: Easing.InOutQuad
                    }
                }

                Kirigami.Icon {
                    anchors.fill: parent
                    source: model.decoration
                    visible: model.screenshot === ""
                }
                Image {
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectFit
                    source: model.screenshot
                }

                // Top-left: instance-count badge (or "New!" badge)
                Rectangle {
                    id: overlayedBadge
                    anchors {
                        top: parent.top
                        left: parent.left
                    }
                    width: countLabel.implicitWidth + height
                    height: Math.round(Kirigami.Units.iconSizes.sizeForLabels * delegate.badgeHeightRatio)
                    radius: height / 2
                    color: (delegate.runningInstances > 0 && delegate.GridView.isCurrentItem) ? Kirigami.Theme.highlightColor : Kirigami.Theme.positiveTextColor
                    visible: ((delegate.runningInstances > 0 && delegate.GridView.isCurrentItem) || model.recent) ?? false

                    PlasmaComponents.Label {
                        id: countLabel
                        anchors.centerIn: parent
                        text: (delegate.runningInstances > 0 && delegate.GridView.isCurrentItem)
                            ? delegate.runningInstances
                            : i18ndc("plasma_shell_org.kde.plasma.desktop", "Text displayed on top of newly installed widgets", "New!")
                        color: "white"
                    }
                }

                // Top-right (when uninstall present): remove all running instances
                PlasmaComponents.ToolButton {
                    id: removeInstancesButton
                    Kirigami.Theme.colorSet: (typeof themeExtended !== "undefined" && themeExtended && themeExtended.isDarkTheme)
                        ? Kirigami.Theme.Complementary
                        : Kirigami.Theme.Button
                    Kirigami.Theme.inherit: false
                    anchors {
                        top: parent.top
                        right: uninstallButton.visible ? uninstallButton.left : parent.right
                        rightMargin: uninstallButton.visible ? Kirigami.Units.smallSpacing : 0
                    }
                    icon.name: "edit-clear-all"
                    display: PlasmaComponents.AbstractButton.IconOnly
                    flat: false
                    visible: (delegate.runningInstances > 0 && delegate.GridView.isCurrentItem) ?? false

                    text: i18ndc("plasma_shell_org.kde.plasma.desktop", "@action:button", "Remove all instances")
                    PlasmaComponents.ToolTip.delay: Kirigami.Units.toolTipDelay
                    PlasmaComponents.ToolTip.visible: hovered
                    PlasmaComponents.ToolTip.text: text

                    onHoveredChanged: {
                        if (hovered) {
                            delegate.GridView.view.currentIndex = delegate.index
                        }
                    }

                    onClicked: {
                        if (typeof widgetExplorer.removeAllInstances === "function") {
                            widgetExplorer.removeAllInstances(pluginName)
                        }
                    }
                }

                // Top-right: uninstall (only for user-installed widgets)
                PlasmaComponents.ToolButton {
                    id: uninstallButton
                    Kirigami.Theme.colorSet: (typeof themeExtended !== "undefined" && themeExtended && themeExtended.isDarkTheme)
                        ? Kirigami.Theme.Complementary
                        : Kirigami.Theme.Button
                    Kirigami.Theme.inherit: false
                    anchors {
                        top: parent.top
                        right: parent.right
                    }
                    icon.name: delegate.pendingUninstall ? "edit-undo" : "edit-delete"
                    display: PlasmaComponents.AbstractButton.IconOnly
                    flat: false
                    visible: (model.local && delegate.GridView.isCurrentItem) ?? false

                    text: delegate.pendingUninstall
                        ? i18nd("plasma_shell_org.kde.plasma.desktop", "Cancel Uninstallation")
                        : i18nd("plasma_shell_org.kde.plasma.desktop", "Uninstall widget")
                    PlasmaComponents.ToolTip.delay: Kirigami.Units.toolTipDelay
                    PlasmaComponents.ToolTip.visible: hovered
                    PlasmaComponents.ToolTip.text: text

                    onHoveredChanged: {
                        if (hovered) {
                            delegate.GridView.view.currentIndex = delegate.index
                        }
                    }

                    onClicked: {
                        var pending = pendingUninstallTimer.applets
                        if (delegate.pendingUninstall) {
                            var idx = pending.indexOf(pluginName)
                            if (idx > -1) {
                                pending.splice(idx, 1)
                            }
                        } else {
                            pending.push(pluginName)
                        }
                        pendingUninstallTimer.applets = pending

                        if (pending.length) {
                            pendingUninstallTimer.restart()
                        } else {
                            pendingUninstallTimer.stop()
                        }
                    }
                }
            }
            PlasmaExtras.Heading {
                id: heading
                Layout.fillWidth: true
                level: 4
                text: model.name
                elide: Text.ElideRight
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                lineHeight: 0.95
                horizontalAlignment: Text.AlignHCenter
            }
            PlasmaComponents.Label {
                Layout.fillWidth: true
                height: implicitHeight
                text: model.description
                font: Kirigami.Theme.smallFont
                wrapMode: Text.WordWrap
                elide: Text.ElideRight
                maximumLineCount: heading.lineCount === 1 ? 3 : 2
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
