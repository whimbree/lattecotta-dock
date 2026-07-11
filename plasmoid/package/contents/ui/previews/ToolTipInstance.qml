/*
    SPDX-FileCopyrightText: 2013 Sebastian Kügler <sebas@kde.org>
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2016 Kai Uwe Broulik <kde@privat.broulik.de>
    SPDX-FileCopyrightText: 2017 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

import QtQuick 2.6
import QtQuick.Layouts 1.1
import QtQuick.Effects
import QtQml.Models 2.2

import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.kirigami 2.20 as Kirigami
import org.kde.plasma.components 3.0 as PlasmaComponents
import org.kde.plasma.extras 2.0 as PlasmaExtras
import org.kde.kquickcontrolsaddons 2.0 as KQuickControlsAddons

import org.kde.latte.core 0.2 as LatteCore
import org.kde.latte.components 1.0 as LatteComponents

import org.kde.draganddrop 2.0

import org.kde.taskmanager 0.1 as TaskManager

import org.kde.plasma.private.mpris as Mpris

Column {
    id: instance
    property var submodelIndex
    property int flatIndex: isGroup && itemIndex>=0 ? itemIndex : 0

    property bool isActive: (typeof model !== 'undefined') && (typeof model.IsActive !== 'undefined') ? IsActive : false
    property bool isMinimized: (typeof model !== 'undefined') && (typeof model.IsMinimized !== 'undefined') ? IsMinimized : false

    property int appPid: (typeof model !== 'undefined') && (typeof model.AppPid !== 'undefined') ? AppPid : -1
    property int itemIndex: (typeof model !== 'undefined') && (typeof model.index !== 'undefined') ? index : 0
    property int virtualDesktop: (typeof model !== 'undefined') && (typeof model.VirtualDesktop !== 'undefined') ? VirtualDesktop : 0
    property var activities : (typeof model !== 'undefined') && (typeof model.Activities !== 'undefined') ? Activities : []

    spacing: Kirigami.Units.smallSpacing

    readonly property bool descriptionIsVisible: winDescription.text !== ""

    //! Plasma 6 Mpris2Model: playerForLauncherUrl returns a PlayerContainer
    //! (or null) exposing player state directly - no dataengine source names
    //! or xesam metadata parsing anymore.
    readonly property Mpris.PlayerContainer playerData: mpris2Source.playerForLauncherUrl(toolTipDelegate.launcherUrl, isGroup ? appPid : pidParent)
    readonly property bool hasPlayer: !!playerData
    readonly property bool playing: hasPlayer && playerData.playbackStatus === Mpris.PlaybackStatus.Playing
    readonly property bool canControl: hasPlayer && playerData.canControl
    readonly property bool canPlay: hasPlayer && playerData.canPlay
    readonly property bool canPause: hasPlayer && playerData.canPause
    readonly property bool canGoBack: hasPlayer && playerData.canGoPrevious
    readonly property bool canGoNext: hasPlayer && playerData.canGoNext
    readonly property bool canRaise: hasPlayer && playerData.canRaise

    readonly property string track: hasPlayer ? (playerData.track || "") : ""
    readonly property string artist: hasPlayer ? (playerData.artist || "") : ""
    readonly property string albumArt: hasPlayer ? (playerData.artUrl || "") : ""

    //
    function isTaskActive() {
        return (isGroup ? isActive : (parentTask ? parentTask.isActive : false));
    }

    // launcher icon + text labels + close button
    RowLayout {
        id: header
        Layout.minimumWidth: childrenRect.width
        Layout.maximumWidth: Layout.minimumWidth

        Layout.minimumHeight: childrenRect.height
        Layout.maximumHeight: Layout.minimumHeight

        anchors.horizontalCenter: parent.horizontalCenter

        // launcher icon
        Kirigami.Icon {
            Layout.preferredWidth: Kirigami.Units.iconSizes.medium
            Layout.preferredHeight: Kirigami.Units.iconSizes.medium
            source: icon
            animated: false
            visible: !isWin
        }
        // all textlabels
        Column {
            PlasmaExtras.Heading {
                level: 3
                width: isWin ? textWidth : undefined
                height: undefined
                maximumLineCount: 1
                elide: Text.ElideRight
                text: appName
                opacity: flatIndex == 0
                textFormat: Text.PlainText
                visible: text !== ""
            }
            // window title
            PlasmaExtras.Heading {
                id: winTitle
                level: 5
                width: isWin ? textWidth : undefined
                height: undefined
                maximumLineCount: 1
                elide: Text.ElideRight
                text: generateTitle()
                textFormat: Text.PlainText
                opacity: 0.75
                visible: !hasPlayer
            }
            // subtext
            PlasmaExtras.Heading {
                id: winDescription
                level: 6
                width: isWin ? textWidth : undefined
                height: undefined
                maximumLineCount: 1
                elide: Text.ElideRight
                text: isWin ? generateSubText() : ""
                textFormat: Text.PlainText
                opacity: 0.6
                visible: text !== "" || instance.parent.hasVisibleDescription
            }
        }
        // close button
        PlasmaComponents.ToolButton {
            //! It creates issues with Valgrind and needs to be completely removed in that case
            id: closeButton
            Layout.alignment: Qt.AlignRight | Qt.AlignTop
            visible: isWin && !hideCloseButtons
            icon.name: "window-close"
            onClicked: {
                if (!isGroup) {
                    //! force windowsPreviewDlg hiding when the last instance is closed
                    windowsPreviewDlg.visible = false;
                }

                backend.cancelHighlightWindows();
                tasksModel.requestClose(submodelIndex);
            }
        }
    }

    // thumbnail container
    Item {
        id: thumbnail
        anchors.horizontalCenter: parent.horizontalCenter

        width: header.width
        // similar to 0.5625 = 1 / (16:9) as most screens are
        // round necessary, otherwise shadow mask for players has gap!
        height: Math.round(screenGeometryHeightRatio * width) + (!winTitle.visible? Math.round(winTitle.height) : 0) + activeTaskLine.height

        visible: isWin

        readonly property real screenGeometryHeightRatio: appletAbilities.myView.screenGeometry.height / appletAbilities.myView.screenGeometry.width

        Item {
            id: thumbnailSourceItem
            anchors.fill: parent
            anchors.bottomMargin: 2

            readonly property bool isMinimized: isGroup ? instance.isMinimized : mainToolTip.isMinimizedParent
            // TODO: this causes XCB error message when being visible the first time
            readonly property var winId: isWin && windows[flatIndex] !== undefined ? windows[flatIndex] : 0

            //! Shadow size in px; drives both the preview-loader inset (so the
            //! halo isn't clipped) and the MultiEffect shadowBlur below.
            readonly property int shadowPx: Math.round(8.0 * Kirigami.Units.devicePixelRatio)

            PlasmaExtras.Highlight {
                anchors.fill: hoverHandler
                visible: hoverHandler.containsMouse
                pressed: hoverHandler.containsPress
            }

            Loader{
                id:previewThumbLoader
                anchors.fill: parent
                anchors.margins: Math.max(2, thumbnailSourceItem.shadowPx)
                visible: !albumArtImage.visible && !thumbnailSourceItem.isMinimized
                //! The Plasma 5 version ladder (5.24/5.25/5.26 variants) is dead on a
                //! Plasma 6-only port; wayland always takes the kpipewire path.
                source: LatteCore.WindowSystem.isPlatformWayland ? "PipeWireThumbnail.qml" : "PlasmaCoreThumbnail.qml"

                LatteComponents.ShadowedItem {
                    anchors.fill: previewThumbLoader.item
                    visible: previewThumbLoader.item.visible
                    source: previewThumbLoader.item
                    shadowColor: "Black"
                    //! Same shadowPx that insets the loader margin above, so the halo
                    //! width tracks the layout inset.
                    shadowSizePx: thumbnailSourceItem.shadowPx
                    shadowVerticalOffset: Math.round(3 * Kirigami.Units.devicePixelRatio)
                }
            }

            ToolTipWindowMouseArea {
                id: hoverHandler

                anchors.fill: parent
                rootTask: parentTask
                modelIndex: submodelIndex
                winId: thumbnailSourceItem.winId
            }

            Image {
                id: albumArtBackground
                source: albumArt
                anchors.fill: parent
                fillMode: Image.PreserveAspectCrop
                //! source is sampled by the sibling blur below; never drawn directly.
                visible: false
            }

            MultiEffect {
                source: albumArtBackground
                anchors.fill: albumArtBackground
                visible: albumArtImage.available
                opacity: 0.25
                blurEnabled: true
                blur: 1.0
                blurMax: 32
                autoPaddingEnabled: false
            }

            Image {
                id: albumArtImage
                // also Image.Loading to prevent loading thumbnails just because the album art takes a split second to load
                readonly property bool available: status === Image.Ready || status === Image.Loading

                height: thumbnail.height - playbackLoader.realHeight
                anchors.horizontalCenter: parent.horizontalCenter
                sourceSize: Qt.size(parent.width, parent.height)
                asynchronous: true
                source: albumArt
                fillMode: Image.PreserveAspectFit
                visible: available
            }

            // when minimized, we don't have a preview, so show the icon
            Kirigami.Icon {
                width: parent.width
                height: thumbnail.height - playbackLoader.realHeight
                anchors.horizontalCenter: parent.horizontalCenter
                source: icon
                animated: false
                visible: (thumbnailSourceItem.isMinimized && !albumArtImage.visible) //X11 case
                         || (!previewThumbLoader.active && !albumArtImage.visible) //Wayland case
            }
        }


        Loader {
            id: playbackLoader

            property real realHeight: item? item.realHeight : 0

            anchors.fill: thumbnail
            active: hasPlayer
            sourceComponent: playerControlsComp
        }

        Component {
            id: playerControlsComp

            Item {
                property real realHeight: playerControlsRow.height

                anchors.fill: parent

                // TODO: When could this really be the case? A not-launcher-task always has a window!?
                // if there's no window associated with this task, we might still be able to raise the player
                //                MouseArea {
                //                    id: raisePlayerArea
                //                    anchors.fill: parent

                //                    visible: !isWin || !windows[0] && canRaise
                //                    onClicked: mpris2Source.raise(mprisSourceName)
                //                }

                //! Drawn directly, deliberately WITHOUT the Qt5 OpacityMask
                //! (glass trimmed to the thumbnail's alpha). Its port here was a
                //! MultiEffect with maskSource: thumbnailSourceItem - a plain
                //! non-layered Item, which is not a valid texture provider, so
                //! the mask never actually applied ('ShaderEffect: Texture t1 is
                //! not assigned a valid texture provider' on every media preview
                //! render) and the glass already rendered unmasked. Worse, that
                //! permanently-invalid sampler made popup teardown/content
                //! switches crash the render thread (SIGSEGV in buildRenderLists
                //! during QSGRhiLayer::grab; user recipe: right-click a widget
                //! and dismiss while a media app's preview churns). Layering the
                //! live pipewire thumbnail to make the mask real would feed the
                //! same crash class, so the mask stays dropped: identical pixels
                //! to what this port always showed, without the crash vector.
                Item {
                    id: playerControlsFrostedGlass
                    anchors.fill: parent

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: playerControlsRow.height
                        color: Kirigami.Theme.backgroundColor
                        opacity: 0.8
                    }
                }

                // prevent accidental click-through when a control is disabled
                MouseArea {
                    id: area3
                    anchors.fill: playerControlsRow
                }

                RowLayout {
                    id: playerControlsRow
                    anchors {
                        horizontalCenter: parent.horizontalCenter
                        bottom: parent.bottom
                    }
                    width: parent.width
                    spacing: 0
                    enabled: canControl

                    ColumnLayout {
                        Layout.margins: 2
                        Layout.fillWidth: true
                        spacing: 0

                        PlasmaComponents.Label {
                            Layout.fillWidth: true
                            lineHeight: 1
                            maximumLineCount: artistText.visible? 1 : 2
                            wrapMode: artistText.visible? Text.NoWrap : Text.Wrap
                            elide: Text.ElideRight
                            text: track || ""
                        }

                         PlasmaExtras.DescriptiveLabel {
                            id: artistText
                            Layout.fillWidth: true
                            wrapMode: Text.NoWrap
                            lineHeight: 1
                            elide: Text.ElideRight
                            text: artist || ""
                            visible: text != ""
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                        }
                    }

                   PlasmaComponents.ToolButton {
                       //! It creates issues with Valgrind and needs to be completely removed in that case
                       id: canGoBackButton
                       enabled: canGoBack
                       icon.name: LayoutMirroring.enabled ? "media-skip-forward" : "media-skip-backward"
                       onClicked: instance.playerData.Previous()
                   }

                   PlasmaComponents.ToolButton {
                       //! It creates issues with Valgrind and needs to be completely removed in that case
                       id: playingButton
                       enabled: playing ? canPause : canPlay
                       icon.name: playing ? "media-playback-pause" : "media-playback-start"
                       onClicked: {
                           if (!playing) {
                               instance.playerData.Play();
                           } else {
                               instance.playerData.Pause();
                           }
                       }
                   }

                   PlasmaComponents.ToolButton {
                       //! It creates issues with Valgrind and needs to be completely removed in that case
                       id: canGoNextButton
                       enabled: canGoNext
                       icon.name: LayoutMirroring.enabled ? "media-skip-backward" : "media-skip-forward"
                       onClicked: instance.playerData.Next()
                   }

                }
            }
        }

        Rectangle{
            id: activeTaskLine
            anchors.bottom: parent.bottom
            width: header.width
            height: 3
            opacity: isTaskActive() ? 1 : 0
            color: Kirigami.Theme.focusColor
        }
    }

    function generateTitle() {
        if (!isWin) {
            return genericName != undefined ? genericName : "";
        }

        var text;
        var modelExists = (typeof model !== 'undefined');

        if (isGroup && modelExists) {
            if (model.display === undefined) {
                return "";
            }
            text = model.display.toString();
        } else {
            text = displayParent;
        }

        // KWin appends increasing integers in between pointy brackets to otherwise equal window titles.
        // In this case save <#number> as counter and delete it at the end of text.
        var counter = text.match(/<\d+>\W*$/);
        text = text.replace(/\s*<\d+>\W*$/, "");

        // Remove appName from the end of text.
        var appNameRegex = new RegExp(appName + "$", "i");
        text = text.replace(appNameRegex, "");
        text = text.replace(/\s*(?:-|—)*\s*$/, "");

        // Add counter back at the end.
        if (counter !== null) {
            if (text === "") {
                text = counter;
            } else {
                text = text + " " + counter;
            }
        }

        // In case the window title had only redundant information (i.e. appName), text is now empty.
        // Add a hyphen to indicate that and avoid empty space.
        if (text === "") {
            text = "—";
        }
        return text.toString();
    }

    function generateSubText() {
        if (activitiesParent === undefined) {
            return "";
        }

        var subTextEntries = [];

        var virtualDesktops = isGroup ? VirtualDesktops : virtualDesktopParent;
        var virtualDesktopNameList = [];

        for (var i = 0; i < virtualDesktops.length; ++i) {
            virtualDesktopNameList.push(virtualDesktopInfo.desktopNames[virtualDesktops[i] - 1]);
        }

        if (!root.showOnlyCurrentDesktop
            && virtualDesktopInfo.numberOfDesktops > 1
            && (isGroup ? IsOnAllVirtualDesktops : isOnAllVirtualDesktopsParent) !== true
            && virtualDesktops.length > 0) {
            subTextEntries.push(i18nc("Comma-separated list of desktops", "On %1", virtualDesktopNameList.join()));
        }

        var act = isGroup ? activities : activitiesParent;

        if (act === undefined) {
            return subTextEntries.join("\n");
        }

        if (act.length === 0 && activityInfo.numberOfRunningActivities > 1) {
            subTextEntries.push(i18nc("Which virtual desktop a window is currently on",
                                      "Available on all activities"));
        } else if (act.length > 0) {
            var activityNames = [];

            for (var i = 0; i < act.length; i++) {
                var activity = act[i];
                var activityName = activityInfo.activityName(act[i]);
                if (activityName === "") {
                    continue;
                }
                if (root.showOnlyCurrentActivity) {
                    if (activity !== activityInfo.currentActivity) {
                        activityNames.push(activityName);
                    }
                } else if (activity !== activityInfo.currentActivity) {
                    activityNames.push(activityName);
                }
            }

            if (root.showOnlyCurrentActivity) {
                if (activityNames.length > 0) {
                    subTextEntries.push(i18nc("Activities a window is currently on (apart from the current one)",
                                              "Also available on %1", activityNames.join(", ")));
                }
            } else if (activityNames.length > 0) {
                subTextEntries.push(i18nc("Which activities a window is currently on",
                                          "Available on %1", activityNames.join(", ")));
            }
        }

        return subTextEntries.join("\n");
    }
}
