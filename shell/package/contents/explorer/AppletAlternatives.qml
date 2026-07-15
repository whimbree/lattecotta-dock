/*
    SPDX-FileCopyrightText: 2014 Marco Martin <mart@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Latte's own copy of plasma-desktop 6.6.5's
//! explorer/AppletAlternatives.qml. Without a package-local file,
//! kPackage().filePath("appletalternativesui") falls back to
//! plasma-desktop's copy, and that one cannot load outside plasmashell:
//! it imports org.kde.plasma.shell, a module plasmashell registers only
//! inside its own process, to type its required alternativesHelper
//! property (plasmashell passes it via setInitialProperties). Two
//! deviations from the upstream file, both forced by that constraint:
//! the org.kde.plasma.shell import is dropped, and the dialog reads the
//! untyped alternativesHelper CONTEXT property that
//! Corona::showAlternativesForApplet has always provided (the Plasma 5
//! contract, which our C++ kept). alternativesHelper.applet here is the
//! applet's quick item (AppletQuickItem), not the Plasma::Applet -
//! Plasmoid attaches to either, so the location read is unchanged.

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQuick.Window
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.components as PlasmaComponents3
import org.kde.plasma.extras as PlasmaExtras
import org.kde.plasma.plasmoid
import org.kde.kirigami as Kirigami

import org.kde.plasma.private.shell

PlasmaCore.Dialog {
    id: dialog

    visualParent: alternativesHelper.applet
    location: alternativesHelper.applet.Plasmoid.location
    hideOnWindowDeactivate: true
    backgroundHints: (alternativesHelper.applet.Plasmoid.containmentDisplayHints & PlasmaCore.Types.ContainmentPrefersOpaqueBackground) ? PlasmaCore.Dialog.SolidBackground : PlasmaCore.Dialog.StandardBackground

    Component.onCompleted: {
        flags = flags |  Qt.WindowStaysOnTopHint;
        dialog.show();
    }

    ColumnLayout {
        id: root

        signal configurationChanged

        Layout.minimumWidth: Kirigami.Units.gridUnit * 20
        Layout.minimumHeight: Math.min(Screen.height - Kirigami.Units.gridUnit * 10, implicitHeight)

        LayoutMirroring.enabled: Application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        property string currentPlugin: ""

        Shortcut {
            sequence: "Escape"
            onActivated: dialog.close()
        }
        Shortcut {
            sequence: "Return"
            onActivated: root.savePluginAndClose()
        }
        Shortcut {
            sequence: "Enter"
            onActivated: root.savePluginAndClose()
        }


        WidgetExplorer {
            id: widgetExplorer
            provides: alternativesHelper.appletProvides
        }

        PlasmaExtras.PlasmoidHeading {
            Kirigami.Heading {
                id: heading
                text: i18ndc("plasma_shell_org.kde.plasma.desktop", "@title:window for widget alternatives explorer", "Alternative Widgets")
                textFormat: Text.PlainText
            }
        }

        // This timer checks with a short delay whether a new item in the list has been hovered by the cursor.
        // If not, then the cursor has left the view and thus no item should be selected.
        Timer {
            id: resetCurrentIndex
            property string oldPlugin
            interval: 100
            onTriggered: {
                if (root.currentPlugin === oldPlugin) {
                    mainList.currentIndex = -1
                    root.currentPlugin = ""
                }
            }
        }

        function savePluginAndClose() {
            alternativesHelper.loadAlternative(currentPlugin);
            dialog.close();
        }

        PlasmaComponents3.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Layout.preferredHeight: mainList.contentHeight

            focus: true

            ListView {
                id: mainList

                focus: dialog.visible
                model: widgetExplorer.widgetsModel
                boundsBehavior: Flickable.StopAtBounds
                highlight: PlasmaExtras.Highlight {
                    pressed: mainList.currentItem && mainList.currentItem.pressed
                }
                highlightMoveDuration : 0
                highlightResizeDuration: 0

                height: contentHeight+Kirigami.Units.smallSpacing

                delegate: PlasmaComponents3.ItemDelegate {
                    id: listItem

                    implicitHeight: contentLayout.implicitHeight + Kirigami.Units.smallSpacing * 2
                    width: ListView.view.width

                    Accessible.name: model.name
                    Accessible.description: model.description

                    onHoveredChanged: {
                        if (hovered) {
                            resetCurrentIndex.stop()
                            mainList.currentIndex = index
                        } else {
                            resetCurrentIndex.oldPlugin = model.pluginName
                            resetCurrentIndex.restart()
                        }
                    }

                    Connections {
                        target: mainList
                        function onCurrentIndexChanged() {
                            if (mainList.currentIndex === index) {
                                root.currentPlugin = model.pluginName
                            }
                        }
                    }

                    onClicked: root.savePluginAndClose()

                    Component.onCompleted: {
                        if (model.pluginName === alternativesHelper.currentPlugin) {
                            root.currentPlugin = model.pluginName
                            setAsCurrent.restart()
                        }
                    }

                    // we don't want to select any entry by default
                    // this cannot be set in Component.onCompleted
                    Timer {
                        id: setAsCurrent
                        interval: 100
                        onTriggered: {
                            mainList.currentIndex = index
                        }
                    }

                    contentItem: RowLayout {
                        id: contentLayout
                        spacing: Kirigami.Units.largeSpacing

                        Kirigami.Icon {
                            implicitWidth: Kirigami.Units.iconSizes.huge
                            implicitHeight: Kirigami.Units.iconSizes.huge
                            source: model.decoration
                        }

                        ColumnLayout {
                            id: labelLayout

                            readonly property color textColor: listItem.pressed ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor

                            Layout.fillHeight: true
                            Layout.fillWidth: true
                            spacing: 0 // The labels bring their own bottom margins

                            Kirigami.Heading {
                                level: 4
                                Layout.fillWidth: true
                                text: model.name
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                type: model.pluginName === alternativesHelper.currentPlugin ? PlasmaExtras.Heading.Type.Primary : PlasmaExtras.Heading.Type.Normal
                                color: labelLayout.textColor
                            }

                            PlasmaComponents3.Label {
                                Layout.fillWidth: true
                                text: model.description
                                textFormat: Text.PlainText
                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                font.family: Kirigami.Theme.smallFont.family
                                font.bold: model.pluginName === alternativesHelper.currentPlugin
                                opacity: 0.75
                                maximumLineCount: 2
                                wrapMode: Text.WordWrap
                                elide: Text.ElideRight
                                color: labelLayout.textColor
                            }
                        }
                    }
                }
            }
        }
    }
}
