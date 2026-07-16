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
import org.kde.latte.private.tasks 0.1 as LatteTasks

import org.kde.draganddrop 2.0

import org.kde.taskmanager 0.1 as TaskManager

import org.kde.plasma.private.mpris as Mpris

Column {
    id: instance
    property var submodelIndex
    property int flatIndex: isGroup && itemIndex>=0 ? itemIndex : 0

    //! fed by the placeholder shell in ToolTipDelegate2: with instances
    //! wrapped in async Loaders, .parent is the Loader, not the Grid the
    //! old structural instance.parent.hasVisibleDescription reach expected
    property bool siblingHasVisibleDescription: false

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
    //! mainToolTip, never the global active-delegate pointer: with cached
    //! standby delegates two instances coexist, and a standby instance
    //! reading the ACTIVE delegate's launcherUrl would bind another task's
    //! player to this task's previews
    readonly property Mpris.PlayerContainer playerData: mpris2Source.playerForLauncherUrl(mainToolTip.launcherUrl, isGroup ? appPid : pidParent)
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
                visible: text !== "" || instance.siblingHasVisibleDescription
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
                         //! Wayland stream warm-up: every preview is a live
                         //! kwin screencast, and the negotiation measured
                         //! 270-500ms per window (STREAM-PROBE, 2026-07-15).
                         //! Until the stream's first frame the thumbnail area
                         //! rendered BLANK - crossing tasks at hover-trigger
                         //! speed felt like lag at the desk. The icon fills
                         //! that hole instantly and yields the moment the
                         //! stream is ready. Strict === false: the X11
                         //! thumbnail item has no hasThumbnail property, and
                         //! undefined must never enable this branch there.
                         || (previewThumbLoader.active && previewThumbLoader.item !== null
                             && previewThumbLoader.item.hasThumbnail === false && !albumArtImage.visible)
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

    //! thin shell over the EX-17 core (plasmoid/plugin/units/tooltiptext.h):
    //! the value-domain undefined checks and the group/window source
    //! selection stay here, the string transform lives in the composer
    function generateTitle(): string {
        if (!isWin) {
            return genericName != undefined ? genericName : "";
        }

        var modelExists = (typeof model !== 'undefined');

        if (isGroup && modelExists) {
            if (model.display === undefined) {
                return "";
            }
            return LatteTasks.TooltipTextComposer.composeTitle(model.display.toString(), appName);
        }

        //! Qt5 threw on an undefined displayParent (a broken binding and an
        //! empty title); the composer receives "" and renders its em-dash
        //! placeholder instead
        return LatteTasks.TooltipTextComposer.composeTitle(
                    displayParent !== undefined ? displayParent.toString() : "", appName);
    }

    //! thin shell over the EX-17 core: the live id -> name lookups and the
    //! group/window source selection stay here (virtualDesktopInfo and
    //! activityInfo are session objects a pure core must not read), the
    //! composer owns every decision and the i18n strings. Index loops, not
    //! Array methods, on the role values: the offscreen suite feeds
    //! array-like role stand-ins (ListModel dynamicRoles nests real
    //! arrays), shaped exactly like these .length/[i] reads.
    function generateSubText(): string {
        //! Qt5 quirk preserved: an undefined activitiesParent blanks the
        //! whole subtext, desktops included, even for group children
        //! carrying their own activities
        if (activitiesParent === undefined) {
            return "";
        }

        var desktopInfo = virtualDesktopInfo;
        var virtualDesktops = isGroup ? VirtualDesktops : virtualDesktopParent;
        var desktopNames = [];

        for (var i = 0; i < virtualDesktops.length; ++i) {
            //! Plasma 6 libtaskmanager hands desktop IDs (UUID strings on
            //! wayland), not the Qt5 1-based desktop numbers; resolve
            //! through desktopIds the way ContextMenu.qml does. An id
            //! missing from desktopIds (desktop just removed) resolves to
            //! nothing instead of an empty name.
            var desktopIndex = desktopInfo.desktopIds.indexOf(virtualDesktops[i]);
            if (desktopIndex >= 0) {
                desktopNames.push(desktopInfo.desktopNames[desktopIndex]);
            }
        }

        var act = isGroup ? activities : activitiesParent;
        var activityIds = null;
        var activityNames = null;

        if (act !== undefined) {
            activityIds = [];
            activityNames = [];
            for (var j = 0; j < act.length; ++j) {
                activityIds.push(act[j]);
                activityNames.push(activityInfo.activityName(act[j]));
            }
        }

        return LatteTasks.TooltipTextComposer.composeSubText({
            showOnlyCurrentDesktop: root.showOnlyCurrentDesktop,
            showOnlyCurrentActivity: root.showOnlyCurrentActivity,
            desktopCount: desktopInfo.numberOfDesktops,
            runningActivityCount: activityInfo.numberOfRunningActivities,
            onAllVirtualDesktops: (isGroup ? IsOnAllVirtualDesktops : isOnAllVirtualDesktopsParent) === true,
            desktopNames: desktopNames,
            activityIds: activityIds,
            activityNames: activityNames,
            currentActivity: activityInfo.currentActivity
        });
    }
}
