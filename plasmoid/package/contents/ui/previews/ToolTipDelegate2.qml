/*
    SPDX-FileCopyrightText: 2013 Sebastian Kügler <sebas@kde.org>
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2016 Kai Uwe Broulik <kde@privat.broulik.de>
    SPDX-FileCopyrightText: 2017 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

import QtQuick 2.6
import QtQuick.Layouts
import QtQuick.Controls 2.15 as QQC2
import QtQml.Models 2.2

import org.kde.draganddrop 2.0

import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.plasmoid 2.0
import org.kde.plasma.components 3.0 as PlasmaComponents
import org.kde.kquickcontrolsaddons 2.0 as KQuickControlsAddons

import org.kde.taskmanager 0.1 as TaskManager
import org.kde.kirigami 2.20 as Kirigami

QQC2.ScrollView {
    id: mainToolTip
    property Item parentTask: null
    property var rootIndex: []

    //! bumped by preparePreviewWindow() after every rootIndex assignment.
    //! DelegateModel SILENTLY resets its root whenever its model swaps
    //! (isGroup flips the model between 1 and tasksModel), and assigning
    //! an EQUAL rootIndex value emits no change signal, so the binding
    //! below would never re-apply the correct root - a revived cached
    //! group then showed a single window, and a delegate caught at the
    //! model root rendered the top-level TASKS as preview instances
    //! (both caught at the desk within minutes of the two-slot cache).
    //! Referencing the token forces the binding to re-evaluate; when the
    //! DelegateModel's internal root already matches, re-setting it is a
    //! no-op.
    property int rootRefreshToken: 0

    property string appName
    property int pidParent
    property bool isGroup
    property bool hideCloseButtons

    property var windows: []
    readonly property bool isWin: windows !== undefined

    //! natural content size, published as plain properties so the previews
    //! host (which shows one of several cached delegates) can mirror the
    //! ACTIVE delegate's geometry into the dialog. Layout attached values
    //! cannot be read across items, and ScrollView already owns the
    //! contentWidth/contentHeight names.
    readonly property real previewContentWidth: contentItem.width
    readonly property real previewContentHeight: contentItem.height

    //! placeholder geometry for instances that are still incubating (see
    //! the shell in the Repeater delegate): a coarse formula from the same
    //! inputs ToolTipInstance's header and thumbnail use, PINNED to the
    //! exact size the first ready instance reports - instances are uniform
    //! by construction, so one correction settles every later shell.
    property real estimatedInstanceWidth: textWidth + Kirigami.Units.iconSizes.medium + Kirigami.Units.gridUnit * 2
    property real estimatedInstanceHeight: Math.round(estimatedInstanceWidth
                                                      * (appletAbilities.myView.screenGeometry.height / appletAbilities.myView.screenGeometry.width))
                                           + Kirigami.Units.gridUnit * 3

    function pinEstimatedInstanceSize(w, h) {
        if (w > 0 && h > 0) {
            estimatedInstanceWidth = w;
            estimatedInstanceHeight = h;
        }
    }

    property variant icon
    property url launcherUrl
    property bool isLauncher
    property bool isMinimizedParent

    // Needed for generateSubtext()
    property string displayParent
    property string genericName
    property var virtualDesktopParent
    property bool isOnAllVirtualDesktopsParent
    property var activitiesParent
    //
    readonly property bool isVerticalPanel: Plasmoid.formFactor === PlasmaCore.Types.Vertical

    Layout.minimumWidth: contentItem.width
    Layout.maximumWidth: Layout.minimumWidth

    Layout.minimumHeight: contentItem.height
    Layout.maximumHeight: Layout.minimumHeight

    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true

    property int textWidth: defaultFontMetrics.advanceWidth * 20

    TextMetrics {
        id: defaultFontMetrics
        text: "M"
        font: Kirigami.Theme.defaultFont
    }

    QQC2.ScrollBar.vertical.policy: QQC2.ScrollBar.AlwaysOff
    QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

    Item{
        width: contentItem.width
        height: contentItem.height

        //! DropArea
        DropArea {
            id: dropMainArea
            anchors.fill: parent
            enabled: isGroup
            preventStealing: true

            property QtObject currentWindow

            onDragLeave: {
                windowsPreviewDlg.hide(9.9);
            }

            onDragMove: (event) => {
                var current = mainToolTip.instanceAtPos(event.x, event.y);

                if (current && currentWindow !== current && current.submodelIndex) {
                    currentWindow = current;
                    tasksModel.requestActivate(current.submodelIndex);
                }
            }
        } //! DropArea

        Loader {
            id: contentItem
            active: !isLauncher
            sourceComponent: Grid {
                rows: !isVerticalPanel
                columns: isVerticalPanel
                flow: isVerticalPanel ? Grid.TopToBottom : Grid.LeftToRight
                spacing: Kirigami.Units.largeSpacing

                readonly property bool hasVisibleDescription: {
                    for (var i=0; i<children.length; ++i) {
                        var child = children[i];

                        if (child && child.instanceItem && child.instanceItem.descriptionIsVisible) {
                            return true;
                        }
                    }

                    return false;
                }

                Repeater {
                    id: groupRepeater
                    model: DelegateModel {
                        model: isGroup ? tasksModel : 1
                        //! token forces re-application, see rootRefreshToken
                        rootIndex: {
                            mainToolTip.rootRefreshToken;
                            return mainToolTip.rootIndex;
                        }

                        //! Each instance incubates ASYNCHRONOUSLY behind a
                        //! placeholder shell: building a ToolTipInstance is
                        //! the expensive part of a preview adoption (the
                        //! 100-400ms synchronous GUI stall, KSvg-dominated,
                        //! measured 2026-07-15), and slicing it across
                        //! frames keeps the parabolic zoom and input alive
                        //! while content pops in. The shell carries the
                        //! ESTIMATED instance size so the grid - and through
                        //! it the dialog - sizes correctly before any
                        //! content exists; instances are uniform by
                        //! construction, so the first ready one corrects
                        //! the estimate exactly and the rest land on it.
                        delegate: Item {
                            id: instanceShell

                            readonly property Item instanceItem: instanceLoader.item

                            width: instanceLoader.item ? instanceLoader.item.width : mainToolTip.estimatedInstanceWidth
                            height: instanceLoader.item ? instanceLoader.item.height : mainToolTip.estimatedInstanceHeight

                            Loader {
                                id: instanceLoader
                                asynchronous: true
                                sourceComponent: ToolTipInstance {
                                    submodelIndex: isGroup ? tasksModel.makeModelIndex(mainToolTip.rootIndex.row, index) : mainToolTip.rootIndex
                                    siblingHasVisibleDescription: instanceShell.parent ? instanceShell.parent.hasVisibleDescription === true : false
                                }
                                onLoaded: mainToolTip.pinEstimatedInstanceSize(item.width, item.height)
                            }
                        }
                    }
                }
            }
        } //! Loader
    } //! Item

    function instanceAtPos(x, y){
        //! children are the placeholder SHELLS; the real instance (when its
        //! async incubation has finished) hangs off each shell's
        //! instanceItem. Position comes from the shell, content from the
        //! instance - a still-incubating shell has no instance to activate.
        var shells = isGroup ? contentItem.children[0].children : contentItem.children;
        var instancesLength = shells.length;

        for(var i=0; i<instancesLength; ++i){
            var shell = shells[i];
            var instance = shell && shell.instanceItem ? shell.instanceItem : null;
            if (!instance) {
                continue;
            }
            var choords = contentItem.mapFromItem(shell,0, 0);

            if(choords.y < 0)
                choords.y = 0;
            if(choords.x < 0)
                choords.x = 0;

            if( (x>=choords.x) && (x<=choords.x+instance.width)
                    && (y>=choords.y) && (y<=choords.y+instance.height)){
                return instance;
            }
        }
        return null;
    }
}


