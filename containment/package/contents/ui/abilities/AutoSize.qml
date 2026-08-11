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
        function onAutomaticSizingMaximumLengthChanged() {
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
        target: sizer.layouter

        function onAutomaticSizingContentsMaxLengthChanged() {
            //! Internal background padding can change the usable span without
            //! changing the configured resting maximum. Painted shadow
            //! margins and live presentation length are deliberately absent
            //! from this budget. Defer the refit so all bindings publish one
            //! coherent geometry snapshot; Qt coalesces repeated calls to
            //! this same bound method.
            Qt.callLater(sizer.updateIconSize);
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

        if (sizer.containment.automaticSizingMaximumLength <= 0) {
            //! the view window has no geometry yet (early startup on wayland:
            //! the first call arrives from visibilityChanged before the window
            //! is sized), so every shrink limit would be negative and any
            //! computed size garbage;
            //! onAutomaticSizingMaximumLengthChanged re-runs this as soon as
            //! a real content budget exists
            return;
        }

        if ( !confirmAppliedSizeTimer.running && !sizer.visibility.inRelocationHiding /*dont apply during relocation hiding*/
                && (sizer.visibility.inNormalState && sizer.isActive) /*in normal and auto size active state*/
                && (sizer.metrics.iconSize === sizer.metrics.maxIconSize || sizer.metrics.iconSize === sizer.iconSize) /*not during animations*/) {

            //! The solid background owns primary-axis end padding outside the
            //! measured applet row. The layouter's content budget subtracts
            //! that internal padding from maxLength. Shadows remain external
            //! paint and do not reduce the stable icon-size budget.
            const availableContentLength =
                sizer.layouter.automaticSizingContentsMaxLength;
            if (availableContentLength <= 0) {
                console.error("AutoSize: background end padding leaves no content length within maxLength",
                              sizer.containment.automaticSizingMaximumLength,
                              availableContentLength);
                return;
            }

            const layoutLength = (sizer.alignment === LatteCore.Types.Justify) ?
                        sizer.layouts.startLayout.length + sizer.layouts.mainLayout.length + sizer.layouts.endLayout.length : sizer.layouts.mainLayout.length

            const result = stepper.step(layoutLength,
                                        availableContentLength,
                                        sizer.metrics.iconSize,
                                        sizer.metrics.maxIconSize,
                                        sizer.iconSize);

            if (result.found) {
                //! shield only a CHANGED size, and BEFORE the write: the
                //! assignment's reactions (margin and background bindings,
                //! their deferred refit echoes) must already see the running
                //! timer and be blocked. A found-but-equal size (the engine's
                //! shrink branch re-proposing the floor while the row
                //! overflows even at 16px) is a no-op write with no geometry
                //! reaction and no echo, so it needs no shield - re-arming on
                //! it would keep the confirmation chain running at 1Hz
                //! forever on any over-full dock.
                if (result.nextIconSize !== sizer.iconSize) {
                    confirmAppliedSizeTimer.restart();
                }
                //! a found nextIconSize of -1 restores automatic sizing (a
                //! grow reached maxIconSize); the stepper maps the core's
                //! alternatives onto the sizer's own -1 sentinel
                sizer.iconSize = result.nextIconSize;
            }
        }
    }

    //! One confirming re-run after each applied size, with every other pass
    //! blocked until it fires. An applied size changes margins and applet
    //! geometry over the following frames, so a pass running before the row
    //! re-settles attributes the OLD row length to the NEW size and feeds the
    //! engine a measurement it never made - stepping past the real fit. Caught
    //! live (D274, the maximize-length input-region defect): a deferred refit
    //! echo landed two frames after each applied size, the predecessor timer's
    //! bookkeeping left the shield down after its own confirming pass applied
    //! a size, and the stale-row passes drove a permanent 1Hz grow/shrink
    //! cycle (60->61->62->61->60) invisible to the engine's two-pass endless
    //! loop protector. The shield therefore stays up across EVERY applied
    //! size - a confirming pass that applies a DIFFERENT size rearms itself
    //! for one more confirmation - and a pass that keeps or merely re-proposes
    //! the current size leaves the chain ended with the shield down (a settled
    //! row needs no confirmation, further passes are cheap pure-math keeps,
    //! and the floor-overflow steady state must rest instead of confirming at
    //! 1Hz forever).
    Timer{
        id: confirmAppliedSizeTimer
        interval: 1000
        onTriggered: sizer.updateIconSize();
    }
}
