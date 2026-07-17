/*
    SPDX-FileCopyrightText: 2021 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.7
import QtQuick.Effects
import Qt5Compat.GraphicalEffects as GraphicalEffects
import org.kde.graphicaleffects as KGraphicalEffects

import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.components 3.0 as PlasmaComponents
import org.kde.plasma.plasmoid 2.0
import org.kde.latte.private.tasks 0.1 as LatteTasks

import org.kde.kirigami 2.0 as Kirigami

import org.kde.latte.core 0.2 as LatteCore
import org.kde.latte.components 1.0 as LatteComponents

import "animations" as TaskAnimations

Item {
    id: taskIconContainer
    anchors.fill: parent
    property bool toBeDestroyed: false

    readonly property color backgroundColor: iconColorsLoader.active ? iconColorsLoader.item.backgroundColor : Kirigami.Theme.backgroundColor
    readonly property color glowColor: iconColorsLoader.active ? iconColorsLoader.item.glowColor : Kirigami.Theme.textColor

    readonly property bool smartLauncherEnabled: (taskItem.isStartup === false) //! it needs to be enabled independent of user-set option because it is used from indicators
    readonly property bool progressVisible: smartLauncherItem && smartLauncherItem.progressVisible
    readonly property real progress: smartLauncherItem && smartLauncherItem.progress ? smartLauncherItem.progress : 0
    readonly property QtObject smartLauncherItem: smartLauncherLoader.active ? smartLauncherLoader.item : null

    //! the owning TaskItem, bound at the TaskItem instantiation site and
    //! threaded down to the audio badge's screen-reader wiring (new
    //! references stay qualified for the qmllint ratchet)
    property Item ownerTask: null

    //! what the info/progress badges currently SHOW, surfaced for the
    //! screen-reader description TaskItem composes (Phase 10 AT-SPI)
    readonly property alias infoBadgeShown: badgesLoader.showInfo
    readonly property alias progressBadgeShown: badgesLoader.showProgress

    readonly property Item monochromizedItem: badgesLoader.active ? badgesLoader.item : taskIconItem

    Rectangle{
        id: draggedRectangle
        width: parent.width + 2
        height: parent.height + 2
        anchors.centerIn: taskIconContainer
        opacity: 0
        radius: 3
        anchors.margins: 5

        property color tempColor: Kirigami.Theme.highlightColor
        color: tempColor
        border.width: 1
        border.color: Kirigami.Theme.highlightColor

        onTempColorChanged: tempColor.a = 0.35;
    }

    Loader {
        id: smartLauncherLoader
        active: taskIconContainer.smartLauncherEnabled
        sourceComponent: LatteTasks.SmartLauncherItem {
            //! It creates issues with Valgrind and needs to be completely removed in that case
            launcherUrl: taskItem.launcherUrlWithIcon
        }
    }

    //! Provide icon background and glow colors
    Loader {
        id: iconColorsLoader
        active: taskItem.abilities.indicators.info.needsIconColors
        visible: false

        sourceComponent: LatteCore.IconItem{
            width:64
            height:64
            source: taskIconItem.source
            providesColors: true
        }
    }

    //! any of the three state effects below is showing; while true the LIVE
    //! source (badgesLoader when active, taskIconItem otherwise) must be a
    //! real texture provider. Qt6 MultiEffect does not auto-wrap plain items
    //! (family: 73da8400), and these three sampled unlayered sources since
    //! the port: every icon repaint re-triggered invalid sampling ('No
    //! QSGTexture provided' accruing at idle) - a rendering no-op and
    //! scenegraph corruption vector. Opacity-tracking gates keep the layer
    //! alive through the whole fade, same contract as the applet colorizer.
    readonly property bool anyStateEffectShown: stateColorizer.opacity > 0
                                                || hoveredImage.opacity > 0
                                                || clickedAnimation.running

    Kirigami.Icon {
        id: taskIconItem
        anchors.fill: parent
        //roundToIconSize: false
        source: decoration
        visible: !badgesLoader.active

        //! forceMonochromaticIcons keeps the layer on PERMANENTLY while the
        //! monochromize overlay exists: that overlay is visible whenever
        //! active (unlike the fading state effects below, which leave the
        //! scenegraph at opacity 0), and the effects' SourceProxy never
        //! repolishes when a source's layer.enabled flips - a proxy that
        //! chose the direct path during a hover would keep sampling the
        //! destroyed layer after the hover ends. A settings-stable gate can
        //! never strand it (same stability rule as df747ebf).
        layer.enabled: (taskIconContainer.anyStateEffectShown || Plasmoid.configuration.forceMonochromaticIcons)
                       && !badgesLoader.active
                       && taskItem.abilities.environment.isGraphicsSystemAccelerated

        readonly property real size: Math.min(width,height)

        ///states for launcher animation
        states: [
            State{
                name: "*"
                //! since qt 5.14 default state can not use "when" property
                //! it breaks restoring transitions otherwise

                AnchorChanges{
                    target: taskIconItem;
                    anchors.horizontalCenter: !root.vertical ? parent.horizontalCenter : undefined;
                    anchors.verticalCenter: root.vertical ? parent.verticalCenter : undefined;
                    anchors.right: root.location === PlasmaCore.Types.RightEdge ? parent.right : undefined;
                    anchors.left: root.location === PlasmaCore.Types.LeftEdge ? parent.left : undefined;
                    anchors.top: root.location === PlasmaCore.Types.TopEdge ? parent.top : undefined;
                    anchors.bottom: root.location === PlasmaCore.Types.BottomEdge ? parent.bottom : undefined;
                }
            },

            State{
                name: "animating"
                when: !taskItem.inAddRemoveAnimation && (launcherAnimation.running || newWindowAnimation.running)

                AnchorChanges{
                    target: taskIconItem;
                    anchors.horizontalCenter: !root.vertical ? parent.horizontalCenter : undefined;
                    anchors.verticalCenter: root.vertical ? parent.verticalCenter : undefined;
                    anchors.right: root.location === PlasmaCore.Types.LeftEdge ? parent.right : undefined;
                    anchors.left: root.location === PlasmaCore.Types.RightEdge ? parent.left : undefined;
                    anchors.top: root.location === PlasmaCore.Types.BottomEdge ? parent.top : undefined;
                    anchors.bottom: root.location === PlasmaCore.Types.TopEdge ? parent.bottom : undefined;
                }
            }
        ]

        ///transitions, basic for the anchor changes
        transitions: [
            Transition{
                from: "animating"
                to: "*"
                AnchorAnimation { duration: 1.5 * taskItem.abilities.animations.speedFactor.current * taskItem.abilities.animations.duration.large }
            }
        ]
    }

    //! Combined Loader for Progress and Audio badges masks
    Loader{
        id: badgesLoader
        anchors.fill: taskIconContainer
        active: (activateProgress > 0) && taskItem.abilities.environment.isGraphicsSystemAccelerated
        asynchronous: true
        opacity: stateColorizer.opacity > 0 ? 0 : 1

        //! provider duty while a state effect samples this loader (see
        //! anyStateEffectShown above)
        layer.enabled: taskIconContainer.anyStateEffectShown && badgesLoader.active

        property real activateProgress: showInfo || showProgress || showAudio ? 1 : 0

        property bool showInfo: (root.showInfoBadge
                                 && taskIcon.smartLauncherItem
                                 && (taskIcon.smartLauncherItem.countVisible || taskItem.badgeIndicator > 0)
                                 && !showProgress)

        property bool showProgress: root.showProgressBadge
                                    && taskIcon.smartLauncherItem
                                    && taskIcon.smartLauncherItem.progressVisible

        property bool showAudio: (root.showAudioBadge && taskItem.hasAudioStream && taskItem.playingAudio)

        Behavior on activateProgress {
            NumberAnimation { duration: 2 * taskItem.abilities.animations.speedFactor.current * taskItem.abilities.animations.duration.large }
        }

        sourceComponent: Item{
            KGraphicalEffects.BadgeEffect {
                id: iconOverlay
                anchors.fill: parent
                source: ShaderEffectSource {
                    sourceItem: Kirigami.Icon{
                        width: taskIconItem.width
                        height: taskIconItem.height
                        smooth: taskIconItem.smooth
                        source: taskIconItem.source
                        //roundToIconSize: taskIconItem.roundToIconSize
                        active: taskIconItem.active

                        Loader{
                            anchors.fill: parent
                            active: Plasmoid.configuration.forceMonochromaticIcons

                            //! Qt5-faithful forced monochromatic is
                            //! ColorOverlay, flat through the alpha;
                            //! MultiEffect.colorization multiplies by the
                            //! source's gray level instead (1f835402 family)
                            //! and monochromized badged icons since the port
                            sourceComponent: GraphicalEffects.ColorOverlay {
                                anchors.fill: parent
                                color: latteBridge ? latteBridge.colorPalette.textColor : "transparent"
                                source: taskIconItem
                            }
                        }
                    }
                }
                mask: ShaderEffectSource {
                    sourceItem: Item{
                        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft && !root.vertical
                        LayoutMirroring.childrenInherit: true

                        width: taskIconContainer.width
                        height: taskIconContainer.height

                        Rectangle{
                            id: maskRect
                            width: Math.max(badgeVisualsLoader.infoBadgeWidth, parent.width / 2)
                            height: parent.height / 2
                            radius: parent.height
                            visible: badgesLoader.showInfo || badgesLoader.showProgress

                            //! Removes any remainings from the icon around the roundness at the corner
                            Rectangle{
                                id: maskCorner
                                width: parent.width/2
                                height: parent.height/2
                            }

                            states: [
                                State {
                                    name: "default"
                                    when: (root.location !== PlasmaCore.Types.RightEdge)

                                    AnchorChanges {
                                        target: maskRect
                                        anchors{ top:parent.top; bottom:undefined; left:undefined; right:parent.right;}
                                    }
                                    AnchorChanges {
                                        target: maskCorner
                                        anchors{ top:parent.top; bottom:undefined; left:undefined; right:parent.right;}
                                    }
                                },
                                State {
                                    name: "right"
                                    when: (root.location === PlasmaCore.Types.RightEdge)

                                    AnchorChanges {
                                        target: maskRect
                                        anchors{ top:parent.top; bottom:undefined; left:parent.left; right:undefined;}
                                    }
                                    AnchorChanges {
                                        target: maskCorner
                                        anchors{ top:parent.top; bottom:undefined; left:parent.left; right:undefined;}
                                    }
                                }
                            ]
                        } // progressMask

                        Rectangle{
                            id: maskRect2
                            width: parent.width/2
                            height: width
                            radius: width
                            visible: badgesLoader.showAudio

                            Rectangle{
                                id: maskCorner2
                                width:parent.width/2
                                height:parent.height/2
                            }

                            states: [
                                State {
                                    name: "default"
                                    when: (root.location !== PlasmaCore.Types.RightEdge)

                                    AnchorChanges {
                                        target: maskRect2
                                        anchors{ top:parent.top; bottom:undefined; left:parent.left; right:undefined;}
                                    }
                                    AnchorChanges {
                                        target: maskCorner2
                                        anchors{ top:parent.top; bottom:undefined; left:parent.left; right:undefined;}
                                    }
                                },
                                State {
                                    name: "right"
                                    when: (root.location === PlasmaCore.Types.RightEdge)

                                    AnchorChanges {
                                        target: maskRect2
                                        anchors{ top:parent.top; bottom:undefined; left:undefined; right:parent.right;}
                                    }
                                    AnchorChanges {
                                        target: maskCorner2
                                        anchors{ top:parent.top; bottom:undefined; left:undefined; right:parent.right;}
                                    }
                                }
                            ]
                        } // audio mask
                    }
                    hideSource: true
                    live: true
                } //end of mask

            } //end of sourceComponent
        }
    }
    ////!

    //! START: Badges Visuals
    //! the badges visual get out from iconGraphic in order to be able to draw shadows that
    //! extend beyond the iconGraphic boundaries
    Loader {
        id: badgeVisualsLoader
        anchors.fill: taskIconContainer
        active: (badgesLoader.activateProgress > 0)

        readonly property int infoBadgeWidth: active ? publishedInfoBadgeWidth : 0
        property int publishedInfoBadgeWidth: 0

        //! threads ownerTask into the audio badge from HERE (document
        //! scope) instead of a binding inside sourceComponent: the qmllint
        //! ratchet counts any outside-id read within the inline component
        //! as an unqualified access
        onLoaded: item.audioOwnerTask = Qt.binding(function() { return taskIconContainer.ownerTask; })

        sourceComponent: Item {
            property alias audioOwnerTask: audioStreamBadge.ownerTask

            ProgressOverlay{
                id: infoBadge
                anchors.right: parent.right
                anchors.top: parent.top
                width: Math.max(parent.width, contentWidth)
                height: parent.height

                opacity: badgesLoader.activateProgress
                visible: badgesLoader.showInfo || badgesLoader.showProgress
            }

            AudioStream{
                id: audioStreamBadge
                anchors.fill: parent
                opacity: badgesLoader.activateProgress
                visible: badgesLoader.showAudio
            }

            Binding {
                target: badgeVisualsLoader
                property: "publishedInfoBadgeWidth"
                value: infoBadge.contentWidth
            }
        }
    }

    //! GREY-ing the information badges when the task is dragged
    //! moved out of badgeVisualsLoader in order to avoid crashes
    //! when the latte view is removed
    Loader {
        anchors.fill: parent
        active: badgeVisualsLoader.active
                && taskItem.abilities.environment.isGraphicsSystemAccelerated
        sourceComponent: MultiEffect{
            source: badgeVisualsLoader.item
            opacity: stateColorizer.opacity

            //! same gate as the three state effects below (69baabf0): an
            //! opacity-0 effect still preprocesses and samples its source on
            //! every repaint, so while badges showed, every badge repaint
            //! re-grabbed the source for an effect that drew nothing
            visible: opacity > 0

            saturation: -1
        }
    }
    //! END: Badges Visuals

    //! Effects
    MultiEffect{
        id: stateColorizer
        anchors.fill: parent
        source: badgesLoader.active ? badgesLoader : taskIconItem

        //! invisible effects still sample their source on every repaint;
        //! without this gate the invalid sampling accrued at idle
        visible: opacity > 0

        opacity:0

        saturation: -1
    }

    MultiEffect{
        id:hoveredImage
        anchors.fill: parent

        source: badgesLoader.active ? badgesLoader : taskIconItem

        visible: opacity > 0

        opacity: taskItem.containsMouse && !clickedAnimation.running && !taskItem.abilities.indicators.info.providesHoveredAnimation ? 1 : 0
        brightness: 0.30
        contrast: 0.1

        Behavior on opacity {
            NumberAnimation { duration: taskItem.abilities.animations.speedFactor.current * taskItem.abilities.animations.duration.large }
        }
    }

    MultiEffect {
        id: brightnessTaskEffect
        anchors.fill: parent

        source: badgesLoader.active ? badgesLoader : taskIconItem

        visible: clickedAnimation.running
    }
    //! Effects

    Loader {
        id: dropFilesVisual
        active: applyOpacity>0

        width: !root.vertical ? length : thickness
        height: !root.vertical ? thickness : length
        anchors.centerIn: parent

        readonly property int length: taskItem.abilities.metrics.totals.length
        readonly property int thickness: taskItem.abilities.metrics.totals.thickness

        readonly property real applyOpacity: (mouseHandler.isDroppingSeparator || mouseHandler.isDroppingFiles)
                                             && (root.dragSource === null)
                                             && (mouseHandler.hoveredItem === taskItem) ? 0.7 : 0

        sourceComponent: LatteComponents.AddItem {
            anchors.fill: parent
            backgroundOpacity: dropFilesVisual.applyOpacity
        }
    }

    //! Animations
    TaskAnimations.ClickedAnimation { id: clickedAnimation }

    TaskAnimations.LauncherAnimation { id:launcherAnimation }

    TaskAnimations.NewWindowAnimation { id: newWindowAnimation }

    TaskAnimations.RemoveWindowFromGroupAnimation { id: removingAnimation }
    //! Animations

    Component.onDestruction: {
        taskIcon.toBeDestroyed = true;

        if(removingAnimation.removingItem)
            removingAnimation.removingItem.destroy();
    }

    //////////// States ////////////////////
    states: [
        State{
            name: "*"
            //! since qt 5.14 default state can not use "when" property
            //! it breaks restoring transitions otherwise
        },

        State{
            name: "isDragged"
            when: taskItem.isDragged
        }
    ]

    //////////// Transitions //////////////

    readonly property string draggingNeedThicknessEvent: taskIcon + "_dragging"

    transitions: [
        Transition{
            id: isDraggedTransition
            to: "isDragged"
            property int speed: taskItem.abilities.animations.speedFactor.current * taskItem.abilities.animations.duration.large

            SequentialAnimation{
                ScriptAction{
                    script: {
                        taskItem.abilities.animations.needThickness.addEvent(draggingNeedThicknessEvent);
                        taskItem.inBlockingAnimation = true;
                        taskItem.abilities.parabolic.setDirectRenderingEnabled(false);
                    }
                }

                PropertyAnimation {
                    target: taskItem.parabolicItem
                    property: "zoom"
                    to: 1
                    duration: taskItem.abilities.parabolic.factor.zoom === 1 ? 0 : (isDraggedTransition.speed*1.2)
                    easing.type: Easing.OutQuad
                }

                ParallelAnimation{
                    PropertyAnimation {
                        target: draggedRectangle
                        property: "opacity"
                        to: 1
                        duration: isDraggedTransition.speed
                        easing.type: Easing.OutQuad
                    }

                    PropertyAnimation {
                        target: taskIconItem
                        property: "opacity"
                        to: 0
                        duration: isDraggedTransition.speed
                        easing.type: Easing.OutQuad
                    }

                    PropertyAnimation {
                        target: stateColorizer
                        property: "opacity"
                        to: taskItem.isSeparator ? 0 : 1
                        duration: isDraggedTransition.speed
                        easing.type: Easing.OutQuad
                    }
                }

                ScriptAction{
                    script: {
                        taskItem.abilities.animations.needThickness.removeEvent(draggingNeedThicknessEvent);
                    }
                }
            }

            onRunningChanged: {
                if(running){
                    taskItem.animationStarted();
                } else {
                    taskItem.abilities.animations.needThickness.removeEvent(draggingNeedThicknessEvent);
                }
            }
        },
        Transition{
            id: defaultTransition
            from: "isDragged"
            to: "*"
            property int speed: taskItem.abilities.animations.speedFactor.current * taskItem.abilities.animations.duration.large

            SequentialAnimation{
                ScriptAction{
                    script: {
                        taskItem.abilities.parabolic.setDirectRenderingEnabled(false);
                    }
                }

                ParallelAnimation{
                    PropertyAnimation {
                        target: draggedRectangle
                        property: "opacity"
                        to: 0
                        duration: defaultTransition.speed
                        easing.type: Easing.OutQuad
                    }

                    PropertyAnimation {
                        target: taskIconItem
                        property: "opacity"
                        to: 1
                        duration: defaultTransition.speed
                        easing.type: Easing.OutQuad
                    }

                    PropertyAnimation {
                        target: stateColorizer
                        property: "opacity"
                        to: 0
                        duration: isDraggedTransition.speed
                        easing.type: Easing.OutQuad
                    }
                }

                ScriptAction{
                    script: {
                        taskItem.inBlockingAnimation = false;
                    }
                }
            }

            onRunningChanged: {
                if(!running){
                    taskItem.animationEnded();
                }
            }
        }
    ]
}
