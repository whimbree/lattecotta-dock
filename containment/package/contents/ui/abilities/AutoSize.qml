/*
    SPDX-FileCopyrightText: 2019 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.8

import org.kde.latte.core 0.2 as LatteCore
import org.kde.latte.private.containment 0.1 as LatteContainment

Item {
    id: sizer

    //! required elements
    required property int alignment
    required property Item animations
    required property bool autoSizeEnabled
    required property Item containment
    required property Item layouts
    required property Item layouter
    required property Item metrics
    required property Item parabolic
    required property QtObject view
    required property Item visibility

    // when there are only plasma style task managers OR any applets that fill width or height
    // the automatic icon size algorithm should better be disabled
    readonly property bool isActive: sizer.containment.behaveAsDockWithMask
                                     && sizer.autoSizeEnabled
                                     && !sizer.containment.containsOnlyPlasmaTasks
                                     && sizer.layouter.fillApplets<=0
                                     && !(sizer.containment.inConfigureAppletsMode && sizer.alignment === LatteCore.Types.Justify) /*block shrinking for justify splitters*/
                                     && sizer.view
                                     && sizer.view.visibility
                                     && sizer.view.visibility.mode !== LatteCore.Types.SidebarOnDemand
                                     && sizer.view.visibility.mode !== LatteCore.Types.SidebarAutoHide

    property int iconSize: -1 //it is not set, this is the default

    readonly property bool inCalculatedIconSize: ((sizer.metrics.iconSize === sizer.iconSize) || (sizer.metrics.iconSize === sizer.metrics.maxIconSize))
    readonly property bool inAutoSizeAnimation: !sizer.inCalculatedIconSize

    //! The search itself - shrink/grow branch selection, the stepping
    //! fit calculation, stable-row limits and endless-loop protector - lives
    //! in the AutoSizeEngine core (containment/plugin/units/
    //! autosizeengine.h, pinned by tests/units/autosizeenginetest.cpp and
    //! tests/qml/tst_autosize.qml). The stepper owns the prediction
    //! history the protector reads; this file keeps the gates, the timers
    //! and the property reads.
    LatteContainment.AutoSizeStepper {
        id: stepper
    }

    onInAutoSizeAnimationChanged: {
        if (sizer.inAutoSizeAnimation) {
            sizer.animations.needBothAxis.addEvent(sizer);
        } else {
            sizer.animations.needBothAxis.removeEvent(sizer);
        }
    }

    onIsActiveChanged: {
        stepper.clearHistory();
        sizer.updateIconSize();
    }

    Connections {
        target: sizer.containment
        function onContainsOnlyPlasmaTasksChanged() {
            sizer.updateIconSize();
        }
        function onMaxLengthChanged() {
            if (sizer.view && sizer.view.positioner && !sizer.view.positioner.isOffScreen) {
                sizer.updateIconSize();
            }
        }
    }

    Connections {
        target: sizer.metrics

        function onPortionIconSizeChanged() {
            if (sizer.metrics.portionIconSize!==-1) {
                sizer.updateIconSize();
            }
        }
    }

    Connections {
        target: sizer.view
        function onWidthChanged() {
            if (sizer.containment.isHorizontal && sizer.metrics.portionIconSize!==-1) {
                sizer.updateIconSize();
            }
        }

        function onHeightChanged() {
            if (sizer.containment.isVertical && sizer.metrics.portionIconSize!==-1) {
                sizer.updateIconSize();
            }
        }
    }

    Connections {
        target: sizer.view && sizer.view.positioner ? sizer.view.positioner : null
        function onIsOffScreenChanged() {
            if (!sizer.view.positioner.isOffScreen) {
                sizer.updateIconSize();
            }
        }
    }

    Connections {
        target: sizer.visibility
        function onInNormalStateChanged() {
            if (sizer.visibility.inNormalState) {
                Qt.callLater(sizer.updateIconSize);
            }
        }
    }

    function updateIconSize() : void {
        if (!sizer.isActive && sizer.iconSize !== -1) {
            // restore original icon size
            sizer.iconSize = -1;
        }

        if (sizer.containment.maxLength <= 0) {
            //! the view window has no geometry yet (early startup on wayland:
            //! the first call arrives from visibilityChanged before the window
            //! is sized), so every shrink limit would be negative and any
            //! computed size garbage; onMaxLengthChanged re-runs this as soon
            //! as a real length exists
            return;
        }

        if ( !doubleCallAutomaticUpdateIconSize.running && !sizer.visibility.inRelocationHiding /*block too many calls and dont apply during relocatinon hiding*/
                && (sizer.visibility.inNormalState && sizer.isActive) /*in normal and auto size active state*/
                && (sizer.metrics.iconSize === sizer.metrics.maxIconSize || sizer.metrics.iconSize === sizer.iconSize) /*not during animations*/) {

            //!doubler timer
            if (!doubleCallAutomaticUpdateIconSize.secondTimeCallApplied) {
                doubleCallAutomaticUpdateIconSize.start();
            } else {
                doubleCallAutomaticUpdateIconSize.secondTimeCallApplied = false;
            }

            const layoutLength = (sizer.alignment === LatteCore.Types.Justify) ?
                        sizer.layouts.startLayout.length + sizer.layouts.mainLayout.length + sizer.layouts.endLayout.length : sizer.layouts.mainLayout.length

            const result = stepper.step(layoutLength,
                                        sizer.containment.maxLength,
                                        sizer.metrics.iconSize,
                                        sizer.metrics.maxIconSize,
                                        sizer.parabolic.factor.zoom,
                                        sizer.iconSize);

            if (result.found) {
                //! a found nextIconSize of -1 restores automatic sizing (a
                //! grow reached maxIconSize); the stepper maps the core's
                //! alternatives onto the sizer's own -1 sentinel
                sizer.iconSize = result.nextIconSize;
            }
        }
    }

    //! This functions makes sure to call the updateIconSize(); function which is costly
    //! one more time after its last call to confirm the applied icon size found
    Timer{
        id:doubleCallAutomaticUpdateIconSize
        interval: 1000
        property bool secondTimeCallApplied: false

        onTriggered: {
            if (!doubleCallAutomaticUpdateIconSize.secondTimeCallApplied) {
                doubleCallAutomaticUpdateIconSize.secondTimeCallApplied = true;
                sizer.updateIconSize();
            }
        }
    }
}
