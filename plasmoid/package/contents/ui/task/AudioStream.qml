/*
    SPDX-FileCopyrightText: 2017 Kai Uwe Broulik <kde@privat.broulik.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.0

import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.ksvg 1.0 as KSvg
import org.kde.kirigami 2.20 as Kirigami

import org.kde.latte.core 0.2 as LatteCore
import org.kde.latte.components 1.0 as LatteComponents
import org.kde.latte.private.tasks 0.1 as LatteTasks

Item {
    id: background

    //! the owning TaskItem, threaded from TaskItem through TaskIcon at the
    //! instantiation sites: the qmllint ratchet keeps NEW references
    //! qualified, so the context-chain taskItem reads the older handlers
    //! in this file carry are not an option for new code
    property Item ownerTask: null

    //! Screen-reader surface (Phase 10 AT-SPI rollout): the badge is the
    //! same checkable mute control the context menu offers - identical
    //! msgid (the composer's muteToggleLabel), identical toggleMuted()
    //! handler as the click below - so Orca announces and drives both
    //! mute surfaces the same way.
    Accessible.ignored: !background.visible
    Accessible.role: Accessible.CheckBox
    Accessible.name: LatteTasks.TooltipTextComposer.muteToggleLabel()
    Accessible.checkable: true
    Accessible.checked: background.ownerTask ? background.ownerTask.muted === true : false
    Accessible.onPressAction: background.requestMuteToggle()
    Accessible.onToggleAction: background.requestMuteToggle()

    //! same gate and same handler as the badge click (audioBadgeMouseArea)
    function requestMuteToggle() {
        if (audioBadgeMouseArea.enabled && background.ownerTask) {
            background.ownerTask.toggleMuted();
        }
    }

    //! Show plasmashell's centered media-player volume OSD (app icon + level
    //! bar + %) for this task, matching the native volume popup. Driven from
    //! the audio-badge scroll handler.
    function showVolumeOsd() {
        backend.showAudioStreamOsd(taskItem.volume, taskItem.appName, taskItem.modelLauncherUrl);
    }

    Item {
        id: subRectangle
        width: parent.width/ 2
        height: width

        states: [
            State {
                name: "default"
                when: (root.location !== PlasmaCore.Types.RightEdge)

                AnchorChanges {
                    target: subRectangle
                    anchors{ top:parent.top; bottom:undefined; left:parent.left; right:undefined;}
                }
            },
            State {
                name: "right"
                when: (root.location === PlasmaCore.Types.RightEdge)

                AnchorChanges {
                    target: subRectangle
                    anchors{ top:parent.top; bottom:undefined; left:undefined; right:parent.right;}
                }
            }
        ]

        LatteComponents.BadgeText {
            anchors.centerIn: parent
            width: 0.8 * parent.width
            height: width
            minimumWidth: width
            maximumWidth: width

            fullCircle: true
            showNumber: false
            showText: true

            color: Kirigami.Theme.backgroundColor
            borderColor: root.lightTextColor
            proportion: 0
            radiusPerCentage: 100

            style3d: taskItem.abilities.myView.badgesIn3DStyle

            LatteCore.IconItem{
                id: audioStreamIcon
                anchors.centerIn: parent
                width: 0.9*parent.width
                height: width
                colorGroup: KSvg.Svg.Button
                usesPlasmaTheme: true

                //opacity: taskItem.playingAudio && !taskItem.muted ? 1 : 0.85
                source: {
                    if (taskItem.volume <= 0 || taskItem.muted) {
                        return "audio-volume-muted";
                    } else if (taskItem.volume <= 25) {
                        return "audio-volume-low";
                    } else if (taskItem.volume <= 75) {
                        return "audio-volume-medium";
                    } else {
                        return "audio-volume-high" ;
                    }
                }

                MouseArea{
                    id: audioBadgeMouseArea
                    anchors.fill: parent
                    enabled: root.audioBadgeActionsEnabled

                    onClicked: {
                        taskItem.toggleMuted();
                    }

                    //! Same wheel semantics as the plasma volume applet
                    //! (plasma-pa applet/main.qml): accumulate angleDelta and
                    //! apply one step per full 120 units, immediately, however
                    //! many fit in the event - no lockout timer; a direction
                    //! reversal drops the residue so the first step of the new
                    //! direction needs the same travel as every other step.
                    //! The old handler stepped on ANY event past 16 units but
                    //! then blocked for 80ms, so held scrolling lagged
                    //! plasma's, and reversals inside the block window were
                    //! swallowed (caught live: different thresholds up vs
                    //! down, slower response than plasma's own applet).
                    //! EX-15: the accumulation lives in LatteCore.WheelStepper
                    //! and is pinned per event in wheelaccumulatortest.
                    LatteCore.WheelStepper {
                        id: volumeWheelStepper
                        axisPick: LatteCore.WheelStepper.VerticalElseNegatedHorizontal
                        stepSize: 120
                        resetOnReversal: true
                    }

                    onWheel: (wheel) => {
                        const steps = volumeWheelStepper.add(wheel.angleDelta, wheel.inverted);

                        for (let i = 0; i < Math.abs(steps); ++i) {
                            if (steps > 0) {
                                taskItem.increaseVolume(wheel.modifiers & Qt.ShiftModifier);
                            } else {
                                taskItem.decreaseVolume(wheel.modifiers & Qt.ShiftModifier);
                            }
                        }

                        if (steps !== 0) {
                            //! Deferred so the (optimistic) volume change has settled
                            //! before we read taskItem.volume for the OSD percentage.
                            Qt.callLater(background.showVolumeOsd);
                        }
                    }
                }
            }
        }
    }
}
