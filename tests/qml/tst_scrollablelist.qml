/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Pins the task-row overflow scrolling behavior (EX-21) against the REAL
//! shipped ScrollableList.qml, instantiated here and driven through its
//! public surface (contentsExceed/contentsExtraSpace/increasePos/
//! decreasePos/focusOn/autoScrollFor and the extra-space clamp). Written
//! against the pre-extraction QML bodies and kept green across the EX-21
//! cutover, so it is the twin-equivalence proof that the C++ core preserves
//! shipped behavior; tests/units/scrollmathtest.cpp pins the same scenarios
//! against the core directly.
//!
//! Context plumbing: every context-chain name ScrollableList resolves is
//! provided - the plasmoid main.qml root properties become properties of
//! this root, appletAbilities is a value bag shaped like the ability reads
//! the functions perform, and the instance carries the id scrollableList
//! exactly as in main.qml (its own functions map task coordinates against
//! that id). The harness binds width/height from length the way main.qml
//! does, keeps root.alignment at Center (the non-center anchor states
//! reference an inherited dangling flickableContainer.lastMargin - present
//! in Qt5 too, out of this unit's scope), and zeroes the animation speed so
//! Behavior writes settle immediately. Plasmoid.formFactor (the
//! flickableDirection binding) resolves against a null attached object
//! outside an applet - a warning only, and the math never reads it
//! (interactive is false; positions are driven programmatically).

import QtQuick 2.7
import QtTest 1.2

import org.kde.latte.core 0.2 as LatteCore

import "../../plasmoid/package/contents/ui/taskslayout" as TasksLayout

Item {
    id: root
    width: 800
    height: 600

    // plasmoid main.qml root properties ScrollableList reads by id.
    // location 4 = PlasmaCore.Types.BottomEdge (kept numeric: importing
    // org.kde.plasma.core just for one enum would drag the whole plasma
    // runtime into the harness).
    property int location: 4
    property int alignment: LatteCore.Types.Center
    property bool vertical: false
    property bool scrollingEnabled: true
    property bool autoScrollTasksEnabled: true
    property real tasksLength: 600
    property int tasksCount: 5

    // the ability reads the scroll functions perform, as one value bag:
    // 24px icons, 70px icon slots with a 6px edge -> 30px trigger zone,
    // 245px wheel step; zoom/lastVisibleItemIndex are mutated per test
    // (function-time reads, not bindings)
    property var appletAbilities: ({
        metrics: {
            iconSize: 24,
            totals: { length: 70, lengthEdge: 6 }
        },
        indexer: { lastVisibleItemIndex: 4 },
        parabolic: { factor: { zoom: 1.0 } },
        animations: {
            speedFactor: { current: 0 },
            duration: { large: 0 }
        }
    })

    TasksLayout.ScrollableList {
        id: scrollableList
        length: 400
        thickness: 60
        width: !root.vertical ? length : thickness
        height: root.vertical ? length : thickness
        contentWidth: !root.vertical ? root.tasksLength : thickness
        contentHeight: root.vertical ? root.tasksLength : thickness

        //! a positionable task stand-in inside the flickable content; the
        //! shipped functions read mapToItem(scrollableList), itemIndex,
        //! width and height from it
        Item {
            id: task
            property int itemIndex: 2
            width: 60
            height: 60
        }
    }

    TestCase {
        name: "ScrollableListMath"
        when: windowShown

        function init() {
            root.vertical = false;
            root.scrollingEnabled = true;
            root.autoScrollTasksEnabled = true;
            root.tasksLength = 600;
            root.tasksCount = 5;
            root.appletAbilities.parabolic.factor.zoom = 1.0;
            task.itemIndex = 2;
            task.x = 170;
            task.y = 0;
            scrollableList.contentX = 0;
            scrollableList.contentY = 0;
            awaitSettled();
        }

        //! Behavior durations are zero but their animations still tick
        //! once; every action settles before the next read
        function awaitSettled() {
            tryVerify(function() { return scrollableList.animationsFinished; });
        }

        function setPos(pos) {
            scrollableList.contentX = pos;
            awaitSettled();
            compare(scrollableList.currentPos, pos);
        }

        //_______________ overflow predicate

        function test_overflowPredicate_data() {
            return [
                { tag: "scrollingOff", enabled: false, content: 1000, exceed: false, extra: 0 },
                { tag: "underflow", enabled: true, content: 300, exceed: false, extra: 0 },
                { tag: "exactFit", enabled: true, content: 400, exceed: false, extra: 0 },
                { tag: "fractionalSliver", enabled: true, content: 400.6, exceed: false, extra: 0 },
                { tag: "wholePixelBeyond", enabled: true, content: 401, exceed: true, extra: 1 },
                { tag: "overflow", enabled: true, content: 600, exceed: true, extra: 200 },
                { tag: "fractionalTruncates", enabled: true, content: 600.7, exceed: true, extra: 200 }
            ];
        }

        function test_overflowPredicate(data) {
            root.scrollingEnabled = data.enabled;
            root.tasksLength = data.content;
            compare(scrollableList.contentsExceed, data.exceed);
            compare(scrollableList.contentsExtraSpace, data.extra);
        }

        //_______________ wheel steps and clamps

        function test_wheelStepIsThreeAndAHalfSlots() {
            compare(scrollableList.scrollStep, 245);
        }

        function test_increaseClampsAtTheEnd() {
            scrollableList.increasePos(); // 0 + 245 clamps at extra space 200
            tryCompare(scrollableList, "contentX", 200);
            scrollableList.increasePos();
            tryCompare(scrollableList, "contentX", 200);
        }

        function test_decreaseClampsAtTheStart() {
            setPos(100);
            scrollableList.decreasePos(); // 100 - 245 clamps at 0
            tryCompare(scrollableList, "contentX", 0);
            scrollableList.decreasePos();
            tryCompare(scrollableList, "contentX", 0);
        }

        function test_partialStepsAccumulate() {
            scrollableList.increasePosWithStep(50);
            tryCompare(scrollableList, "contentX", 50);
            scrollableList.increasePosWithStep(50);
            tryCompare(scrollableList, "contentX", 100);
            scrollableList.decreasePosWithStep(30);
            tryCompare(scrollableList, "contentX", 70);
        }

        function test_verticalStepsDriveContentY() {
            root.vertical = true;
            scrollableList.increasePosWithStep(50);
            tryCompare(scrollableList, "contentY", 50);
            compare(scrollableList.contentX, 0);
            scrollableList.decreasePosWithStep(245);
            tryCompare(scrollableList, "contentY", 0);
        }

        //_______________ focusOn (scroll into view)

        function test_focusNeedsOverflow() {
            root.tasksLength = 300; // fits: focusOn must not move
            task.x = -70;
            scrollableList.focusOn(task);
            awaitSettled();
            compare(scrollableList.contentX, 0);
        }

        function test_focusScrollsBackWithIconMargin() {
            setPos(100);
            task.x = 30; // viewport position 30 - 100 = -70, off the start
            scrollableList.focusOn(task);
            // distance |(-70) - 24| = 94 -> 100 - 94 = 6
            tryCompare(scrollableList, "contentX", 6);
        }

        function test_focusScrollsForwardWithIconMargin() {
            task.x = 420; // beyond the 400px viewport at pos 0
            scrollableList.focusOn(task);
            // distance |420 - 400 + 60 + 24| = 104
            tryCompare(scrollableList, "contentX", 104);
        }

        function test_focusLeavesVisibleItemsAlone() {
            setPos(100);
            task.x = 270; // viewport position 170, fully visible
            scrollableList.focusOn(task);
            awaitSettled();
            compare(scrollableList.contentX, 100);
        }

        function test_focusVertical() {
            root.vertical = true;
            root.tasksLength = 600;
            scrollableList.contentY = 100;
            awaitSettled();
            task.y = 30; // viewport position -70, off the top
            scrollableList.focusOn(task);
            tryCompare(scrollableList, "contentY", 6);
        }

        //_______________ autoScrollFor (trigger zones and blocks)

        function test_autoScrollBlockedWhenDisabledAndNotDragging() {
            root.autoScrollTasksEnabled = false;
            setPos(100);
            task.x = 110; // viewport position 10, inside the 30px start zone
            scrollableList.autoScrollFor(task, false);
            awaitSettled();
            compare(scrollableList.contentX, 100);
        }

        function test_autoScrollDraggingOverridesTheSetting() {
            root.autoScrollTasksEnabled = false;
            setPos(100);
            task.x = 110;
            scrollableList.autoScrollFor(task, true);
            tryCompare(scrollableList, "contentX", 30); // one 70px slot back
        }

        function test_autoScrollNeedsThreeTasks() {
            root.tasksCount = 2;
            setPos(100);
            task.x = 110;
            scrollableList.autoScrollFor(task, false);
            awaitSettled();
            compare(scrollableList.contentX, 100);

            root.tasksCount = 3; // the boundary is inclusive
            scrollableList.autoScrollFor(task, false);
            tryCompare(scrollableList, "contentX", 30);
        }

        function test_autoScrollLastItemWithParabolicZoomBlocks() {
            root.appletAbilities.parabolic.factor.zoom = 1.6;
            task.itemIndex = 4; // == lastVisibleItemIndex
            setPos(100);
            task.x = 110;
            scrollableList.autoScrollFor(task, false);
            awaitSettled();
            compare(scrollableList.contentX, 100);

            root.appletAbilities.parabolic.factor.zoom = 1.0; // no zoom unblocks
            scrollableList.autoScrollFor(task, false);
            tryCompare(scrollableList, "contentX", 30);
        }

        function test_autoScrollStartZoneNotAtTheBoundary() {
            // at the first position the start zone must not fire (Qt5's
            // wheel-at-the-first-task issue)
            task.x = 10;
            scrollableList.autoScrollFor(task, false);
            awaitSettled();
            compare(scrollableList.contentX, 0);

            setPos(100);
            task.x = 110;
            scrollableList.autoScrollFor(task, false);
            tryCompare(scrollableList, "contentX", 30);
        }

        function test_autoScrollEndZoneNotAtTheBoundary() {
            setPos(200); // the last position: end zone must not fire
            task.x = 580; // viewport position 380, end 440 > 370
            scrollableList.autoScrollFor(task, false);
            awaitSettled();
            compare(scrollableList.contentX, 200);

            setPos(100); // off the boundary the same item scrolls forward
            task.x = 480;
            scrollableList.autoScrollFor(task, false);
            tryCompare(scrollableList, "contentX", 170);
        }

        function test_autoScrollIgnoresMidViewportItems() {
            setPos(100);
            task.x = 270; // viewport position 170: in neither 30px zone
            scrollableList.autoScrollFor(task, false);
            awaitSettled();
            compare(scrollableList.contentX, 100);
        }

        //_______________ the extra-space clamp

        function test_shrinkingOverflowClampsThePosition() {
            setPos(200);
            root.tasksLength = 500; // extra space shrinks to 100
            tryCompare(scrollableList, "contentX", 100);
        }

        function test_vanishingOverflowReturnsToTheStart() {
            setPos(200);
            root.tasksLength = 300; // no overflow anymore
            tryCompare(scrollableList, "contentX", 0);
        }
    }
}
