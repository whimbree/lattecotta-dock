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

Item {
    id: background

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

                    property int wheelDelta: 0

                    onClicked: {
                        taskItem.toggleMuted();
                    }

                    //! Same wheel semantics as the plasma volume applet
                    //! (plasma-pa applet/main.qml): accumulate angleDelta and
                    //! apply one step per full 120 units, immediately, however
                    //! many fit in the event - no lockout timer. The old
                    //! handler stepped on ANY event past 16 units but then
                    //! blocked for 80ms, so held scrolling lagged plasma's,
                    //! and reversals inside the block window were swallowed
                    //! (user-reported: different thresholds up vs down,
                    //! slower response than plasma's own applet).
                    onWheel: (wheel) => {
                        const delta = (wheel.inverted ? -1 : 1) * (wheel.angleDelta.y ? wheel.angleDelta.y : -wheel.angleDelta.x);

                        if ((wheelDelta > 0 && delta < 0) || (wheelDelta < 0 && delta > 0)) {
                            //! direction change: drop the residue so the first
                            //! step of the new direction needs the same travel
                            //! as every other step
                            wheelDelta = 0;
                        }

                        wheelDelta += delta;

                        var steps = 0;

                        while (wheelDelta >= 120) {
                            wheelDelta -= 120;
                            taskItem.increaseVolume(wheel.modifiers & Qt.ShiftModifier);
                            steps++;
                        }

                        while (wheelDelta <= -120) {
                            wheelDelta += 120;
                            taskItem.decreaseVolume(wheel.modifiers & Qt.ShiftModifier);
                            steps++;
                        }

                        if (steps > 0) {
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
