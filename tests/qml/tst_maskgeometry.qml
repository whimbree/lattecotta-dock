/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Shell pin for the visibility mask / input-region math (EX-10). The
//! per-edge rect tables this file used to exercise in
//! VisibilityManager.qml live in the MaskGeometry core now
//! (containment/plugin/units/maskgeometry.h; tests/units/
//! maskgeometrytest.cpp pins the full edge x state matrix). What must
//! stay pinned HERE is the boundary: the MaskGeometryBridge the
//! containment instantiates resolves from the staged install, delegates
//! to the core, and maps the core's decision alternatives onto the
//! effects protocol's sentinel rects (Qt.rect(0,0,-1,-1) accept
//! everywhere, Qt.rect(-1,-1,1,1) accept nowhere - Effects::setInputMask,
//! app/view/effects.cpp). Driving the full VisibilityManager.qml
//! offscreen is not feasible (it needs the whole containment context
//! chain); the EX-10 live matrix covers that end.

import QtQuick
import QtTest

import org.kde.latte.private.containment 0.1 as LatteContainment

TestCase {
    id: root
    name: "MaskGeometryBridgeShell"

    //! PlasmaCore.Types.Location values; the staged PlasmaCore import is
    //! not needed for them
    readonly property int bottomEdge: 4

    LatteContainment.MaskGeometryBridge {
        id: maskGeometry
    }

    //! component-wise on purpose: the invokables return QRect while
    //! Qt.rect() builds a QRectF, and TestCase.compare's deep-equals
    //! across two different value types is not a pinned behavior
    function compareRect(actual, x, y, width, height) {
        compare(actual.x, x, "rect.x");
        compare(actual.y, y, "rect.y");
        compare(actual.width, width, "rect.width");
        compare(actual.height, height, "rect.height");
    }

    function test_localGeometryBottomEdge() {
        //! the shared fixture of maskgeometrytest.cpp: applets spanning
        //! effects.rect 200..1000 at clean thickness 56 over a 12px
        //! floating gap in a 1200x140 window
        var rect = maskGeometry.localGeometryFor(bottomEdge,
                                                 false, //! behaveAsPlasmaPanel
                                                 1200, 140,
                                                 1200, 140,
                                                 Qt.rect(200, 0, 800, 140),
                                                 56, 12);
        compareRect(rect, 200, 72, 800, 56);
    }

    function test_stableJustifyDockOccupancyBottomEdge() {
        var rect = maskGeometry.stableJustifyDockOccupancyFor(
            bottomEdge,
            1200, 140,
            1200, 140,
            800, 56, 12);
        compareRect(rect, 200, 72, 800, 56);
    }

    function test_inputMaskNormalStateFollowsLocalGeometry() {
        var rect = maskGeometry.inputMaskFor(bottomEdge,
                                             true,  //! compositingActive
                                             false, //! behaveAsPlasmaPanel
                                             false, //! isHidden
                                             false, //! isSidebar
                                             false, //! parabolicAnimating
                                             false, //! floatingGapInputDisabled
                                             2, 102, 12, 56, 12,
                                             Qt.rect(200, 72, 800, 56),
                                             1200, 140,
                                             1200, 140);
        //! thickness = mask.screenEdge 12 + totals.thickness 56
        compareRect(rect, 200, 72, 800, 68);
    }

    function test_hiddenSidebarMapsToBlockAllSentinel() {
        var rect = maskGeometry.inputMaskFor(bottomEdge,
                                             true, false,
                                             true,  //! isHidden
                                             true,  //! isSidebar
                                             false, false,
                                             2, 102, 12, 56, 12,
                                             Qt.rect(200, 72, 800, 56),
                                             1200, 140,
                                             1200, 140);
        compareRect(rect, -1, -1, 1, 1);
    }

    function test_invalidInputsAreRefused() {
        //! a negative dimension is refused loudly (qCritical in the log):
        //! empty local geometry, accept-everywhere input mask
        var localRect = maskGeometry.localGeometryFor(bottomEdge, false,
                                                      -5, 140,
                                                      1200, 140,
                                                      Qt.rect(200, 0, 800, 140),
                                                      56, 12);
        compareRect(localRect, 0, 0, 0, 0);

        var inputRect = maskGeometry.inputMaskFor(99, //! unknown location
                                                  true, false,
                                                  false, false, false, false,
                                                  2, 102, 12, 56, 12,
                                                  Qt.rect(200, 72, 800, 56),
                                                  1200, 140,
                                                  1200, 140);
        compareRect(inputRect, 0, 0, -1, -1);
    }
}
