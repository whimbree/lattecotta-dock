/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Integration pin for D26 (VisibilityManager inNormalState binding-loop
//! warning). The production cycle is Tracker count -> declarative normal
//! state -> AutoSize update -> target icon size -> AutoSize animation ->
//! Tracker count. This harness drives the shipped AutoSize ability and the
//! shipped Tracker instances inside Animations; only the large
//! VisibilityManager shell is reduced to its declarative inNormalState
//! expression.

import QtQuick
import QtTest

import org.kde.latte.core 0.2 as LatteCore

import "../../containment/package/contents/ui/abilities" as ContainmentAbility
import "../../declarativeimports/abilities/definition" as AbilityDefinition

Item {
    id: root
    width: 800
    height: 64

    property bool behaveAsDockWithMask: true
    property bool containsOnlyPlasmaTasks: false
    property bool inConfigureAppletsMode: false
    property bool isHorizontal: true
    property bool isVertical: false
    property real maxLength: 1100

    property int destroyedContinuationCalls: 0
    property int rapidContinuationCalls: 0

    QtObject { id: blocker }

    AbilityDefinition.Animations {
        id: productionAnimations
    }

    Item {
        id: viewMock
        property Item visibility: Item {
            property bool containsMouse: false
            property bool isHidden: false
            property int mode: LatteCore.Types.AlwaysVisible
        }
        property Item positioner: Item {
            property bool isOffScreen: false
        }
    }

    Item {
        id: visibilityManager
        property bool inNormalState: ((productionAnimations.needBothAxis.count === 0)
                                      && (productionAnimations.needLength.count === 0))
                                     || (viewMock.visibility.isHidden
                                         && !viewMock.visibility.containsMouse
                                         && productionAnimations.needThickness.count === 0)
        property bool inRelocationHiding: false
    }

    Item {
        id: metricsMock
        property int iconSize: 64
        property int maxIconSize: 64
        property int portionIconSize: -1
        property QtObject totals: QtObject {
            property real length: 64
        }
    }

    Item {
        id: layoutsMock
        property Item startLayout: Item { property real length: 0 }
        property Item mainLayout: Item { property real length: 1000 }
        property Item endLayout: Item { property real length: 0 }
    }

    Item {
        id: layouterMock
        property int fillApplets: 0
    }

    Item {
        id: parabolicMock
        property QtObject factor: QtObject {
            property real zoom: 1.6
        }
    }

    Component {
        id: productionSizerComponent
        ContainmentAbility.AutoSize {
            alignment: LatteCore.Types.Center
            animations: productionAnimations
            autoSizeEnabled: true
            containment: root
            layouts: layoutsMock
            layouter: layouterMock
            metrics: metricsMock
            parabolic: parabolicMock
            view: viewMock
            visibility: visibilityManager
        }
    }

    Component {
        id: rapidCountingSizerComponent
        ContainmentAbility.AutoSize {
            alignment: LatteCore.Types.Center
            animations: productionAnimations
            autoSizeEnabled: true
            containment: root
            layouts: layoutsMock
            layouter: layouterMock
            metrics: metricsMock
            parabolic: parabolicMock
            view: viewMock
            visibility: visibilityManager

            function updateIconSize() : void {
                root.rapidContinuationCalls += 1;
            }
        }
    }

    Component {
        id: destroyedCountingSizerComponent
        ContainmentAbility.AutoSize {
            alignment: LatteCore.Types.Center
            animations: productionAnimations
            autoSizeEnabled: true
            containment: root
            layouts: layoutsMock
            layouter: layouterMock
            metrics: metricsMock
            parabolic: parabolicMock
            view: viewMock
            visibility: visibilityManager

            function updateIconSize() : void {
                root.destroyedContinuationCalls += 1;
            }
        }
    }

    Loader {
        id: pendingSizerLoader
        active: false
    }

    TestCase {
        id: testCase
        name: "NormalStateAutoSizeContinuation"
        when: windowShown

        property var sizer: null

        function resetTracker(tracker: Item) : void {
            tracker.events = [];
            tracker.count = 0;
        }

        function createBlockedSizer(component: Component) : var {
            productionAnimations.needLength.addEvent(blocker);
            const created = createTemporaryObject(component, root);
            verify(created, "the shipped AutoSize ability must instantiate");
            return created;
        }

        function init() {
            resetTracker(productionAnimations.needBothAxis);
            resetTracker(productionAnimations.needLength);
            resetTracker(productionAnimations.needThickness);
            metricsMock.iconSize = 64;
            metricsMock.maxIconSize = 64;
            metricsMock.portionIconSize = -1;
            metricsMock.totals.length = 64;
            layoutsMock.mainLayout.length = 1000;
            parabolicMock.factor.zoom = 1.6;
            root.maxLength = 1100;
            root.destroyedContinuationCalls = 0;
            root.rapidContinuationCalls = 0;
        }

        function cleanup() {
            pendingSizerLoader.active = false;
            pendingSizerLoader.sourceComponent = undefined;
            if (sizer) {
                sizer.destroy();
                sizer = null;
            }
            wait(0);
            resetTracker(productionAnimations.needBothAxis);
            resetTracker(productionAnimations.needLength);
            resetTracker(productionAnimations.needThickness);
        }

        function test_resizeRunsAfterTheNormalStateBindingSettles() {
            sizer = createBlockedSizer(productionSizerComponent);
            compare(visibilityManager.inNormalState, false);
            compare(sizer.iconSize, -1);

            productionAnimations.needLength.removeEvent(blocker);

            compare(sizer.iconSize, -1,
                    "normal-state notification must not resize synchronously inside the count binding");
            wait(0);
            compare(sizer.iconSize, 63,
                    "the largest fitting size must be applied on the next event-loop turn");

            metricsMock.iconSize = 60;
            compare(productionAnimations.needBothAxis.count, 1,
                    "an intermediate animated metric must feed the real AutoSize event into the real Tracker");
            compare(visibilityManager.inNormalState, false,
                    "the target size must make the declarative normal-state binding false again");
        }

        function test_rapidNormalStateNotificationsDeduplicate() {
            sizer = createBlockedSizer(rapidCountingSizerComponent);
            root.rapidContinuationCalls = 0;

            productionAnimations.needLength.removeEvent(blocker);
            productionAnimations.needLength.addEvent(blocker);
            productionAnimations.needLength.removeEvent(blocker);
            productionAnimations.needLength.addEvent(blocker);
            productionAnimations.needLength.removeEvent(blocker);

            compare(root.rapidContinuationCalls, 0,
                    "none of the rapid notifications may continue synchronously");
            wait(0);
            compare(root.rapidContinuationCalls, 1,
                    "Qt.callLater must coalesce the same bound AutoSize method and arguments");
        }

        function test_finalFalseStatePreventsStaleResize() {
            sizer = createBlockedSizer(productionSizerComponent);

            productionAnimations.needLength.removeEvent(blocker);
            productionAnimations.needLength.addEvent(blocker);
            compare(visibilityManager.inNormalState, false);

            wait(0);
            compare(sizer.iconSize, -1,
                    "updateIconSize must recheck the final normal state before applying a target");
            compare(productionAnimations.needBothAxis.count, 0,
                    "a stale continuation must not start an AutoSize animation");
        }

        function test_destroyingSizerWithPendingContinuationIsSafe() {
            productionAnimations.needLength.addEvent(blocker);
            pendingSizerLoader.sourceComponent = destroyedCountingSizerComponent;
            pendingSizerLoader.active = true;
            verify(pendingSizerLoader.item, "the shipped AutoSize ability must instantiate through Loader");
            root.destroyedContinuationCalls = 0;

            productionAnimations.needLength.removeEvent(blocker);
            compare(root.destroyedContinuationCalls, 0,
                    "the pending continuation must still be deferred before Loader teardown");
            pendingSizerLoader.active = false;
            compare(pendingSizerLoader.item, null,
                    "Loader deactivation must release its AutoSize item before the queued continuation runs");

            wait(0);
            compare(root.destroyedContinuationCalls, 1,
                    "Qt must retain the bound method safely through teardown and invoke it only once");
        }

        function test_noResizeTargetDoesNotCreateTrackerFeedback() {
            layoutsMock.mainLayout.length = 890;
            root.maxLength = 1000;
            sizer = createBlockedSizer(productionSizerComponent);

            productionAnimations.needLength.removeEvent(blocker);
            wait(0);

            compare(sizer.iconSize, -1,
                    "the robustness band has no target and must leave automatic sizing selected");
            compare(productionAnimations.needBothAxis.count, 0,
                    "without a target AutoSize cannot feed back into the tracker count");
            compare(visibilityManager.inNormalState, true,
                    "the declarative state stays true when the causal feedback edge is absent");
        }
    }
}
