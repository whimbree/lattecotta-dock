/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Reference generator for EX-06: drives the SHIPPED indexer twins
//! offscreen (the REAL containment abilities/Indexer.qml over its
//! IndexerPrivate.qml, and the REAL client abilities/Indexer.qml wired to
//! it through a mock bridge exactly the way the live communicator wires
//! them) plus verbatim copies of the AppletItem.qml/BasicItem.qml walk
//! bodies (those two files cannot be instantiated outside a full dock;
//! the copies execute the original expressions against the real twins'
//! arrays - source lines cited at each copy). Prints one CASE|<name>|json
//! line per scenario; the vectors baked into
//! tests/units/visibleindextest.cpp come from this harness at b0e606b2:
//!
//!     scripts/qml-interaction-tests.sh tests/generators/visibleindex
//!
//! Containment world (index space): startLayout a0 a1 a2 | mainLayout
//! a3 clientApplet | endLayout a5 a6. clientApplet hosts the real client
//! twin (its tasks row is the tasks Repeater). Indexes and flags are
//! reconfigured per case; "absent" items park at index 9999, beyond
//! every probe (a true absence is impossible to fake at index -1: the
//! shipped visibleItemsBeforeCount counts index -1 children, which is
//! part of what the extraction fixes).
import QtQuick 2.7
import QtTest 1.2

import "../../../containment/package/contents/ui/abilities" as ContainmentAbilities
import "../../../declarativeimports/abilities/client" as ClientAbility

Item {
    id: root
    width: 400
    height: 60

    //! context the shipped containment Indexer.qml resolves through the
    //! QML context chain in the real containment
    property bool appletIsDragged: false
    property var dragOverlay: null

    QtObject {
        id: layouter
        property bool appletsInParentChange: false
    }

    //! per-case configuration arrays; each entry {index, isSeparator,
    //! isHidden, isMarginsAreaSeparator, appletId}
    property var startConfigs: []
    property var mainConfigs: []
    property var endConfigs: []
    property int clientAppletIndex: 9999
    property bool clientAppletIsHidden: false
    //! per-case task entries {index, isSeparator, isHidden, isSeparatorHidden}
    property var taskConfigs: []

    Item {
        id: layoutsMock
        property alias startLayout: _startLayout
        property alias mainLayout: _mainLayout
        property alias endLayout: _endLayout

        Item {
            id: _startLayout
            Repeater {
                model: root.startConfigs
                delegate: MockAppletItem {
                    index: modelData.index
                    isSeparator: modelData.isSeparator === true
                    isHidden: modelData.isHidden === true
                    isMarginsAreaSeparator: modelData.isMarginsAreaSeparator === true
                    applet: ({ plasmoid: { id: modelData.appletId !== undefined ? modelData.appletId : -1 } })
                }
            }
        }
        Item {
            id: _mainLayout
            Repeater {
                model: root.mainConfigs
                delegate: MockAppletItem {
                    index: modelData.index
                    isSeparator: modelData.isSeparator === true
                    isHidden: modelData.isHidden === true
                    isMarginsAreaSeparator: modelData.isMarginsAreaSeparator === true
                    applet: ({ plasmoid: { id: modelData.appletId !== undefined ? modelData.appletId : -1 } })
                }
            }
            MockAppletItem {
                id: clientApplet
                index: root.clientAppletIndex
                isHidden: root.clientAppletIsHidden
                applet: ({ plasmoid: { id: 400 } })
                communicator: Item {
                    property bool indexerIsSupported: true
                    property var requires: ({ windowsTrackingEnabled: false })
                    property Item bridge: Item {
                        property Item indexer: Item {
                            property int appletIndex: clientApplet.index
                            property var client: null
                            property var host: containmentIndexer
                            //! live in the real dock the communicator binds these
                            //! from the AppletItem properties; here from the
                            //! verbatim walk copies over the same containment state
                            property bool tailAppletIsSeparator: appletWalks.tailAppletIsSeparator(clientApplet.isSeparator, clientApplet.index)
                            property bool headAppletIsSeparator: appletWalks.headAppletIsSeparator(clientApplet.isSeparator, clientApplet.index)
                            property bool inMarginsArea: false
                        }
                    }
                }
            }
        }
        Item {
            id: _endLayout
            Repeater {
                model: root.endConfigs
                delegate: MockAppletItem {
                    index: modelData.index
                    isSeparator: modelData.isSeparator === true
                    isHidden: modelData.isHidden === true
                    isMarginsAreaSeparator: modelData.isMarginsAreaSeparator === true
                    applet: ({ plasmoid: { id: modelData.appletId !== undefined ? modelData.appletId : -1 } })
                }
            }
        }
    }

    Item {
        id: tasksLayout
        Repeater {
            model: root.taskConfigs
            delegate: MockTaskItem {
                itemIndex: modelData.index
                isSeparator: modelData.isSeparator === true
                isHidden: modelData.isHidden === true
                isSeparatorHidden: modelData.isSeparatorHidden === true
            }
        }
    }

    //! the REAL shipped containment twin
    ContainmentAbilities.Indexer {
        id: containmentIndexer
        layouts: layoutsMock
    }

    //! the REAL shipped client twin, wired through the mock bridge the
    //! way the live communicator wires it (it assigns itself as
    //! bridge.indexer.client, which the containment twin then reads)
    ClientAbility.Indexer {
        id: indexer
        layout: tasksLayout
        bridge: clientApplet.communicator.bridge
    }

    QtObject {
        id: appletWalks

        //! VERBATIM copy of containment AppletItem.qml:221-245
        //! tailAppletIsSeparator at b0e606b2, remapped: indexer ->
        //! containmentIndexer, self property reads -> parameters
        function tailAppletIsSeparator(selfIsSeparator, index) {
            if (selfIsSeparator || index<0) {
                return false;
            }

            var tail = index - 1;

            while(tail>=0 && containmentIndexer.hidden.indexOf(tail)>=0) {
                tail = tail - 1;
            }

            if (tail >= 0 && containmentIndexer.clients.indexOf(tail)>=0) {
                var tailBridge = containmentIndexer.getClientBridge(tail);

                if (tailBridge && tailBridge.client) {
                    return tailBridge.client.lastHeadItemIsSeparator;
                }
            }

            return (containmentIndexer.separators.indexOf(tail)>=0);
        }

        //! VERBATIM copy of containment AppletItem.qml:247-271
        //! headAppletIsSeparator at b0e606b2, same remaps
        function headAppletIsSeparator(selfIsSeparator, index) {
            if (selfIsSeparator || index<0) {
                return false;
            }

            var head = index + 1;

            while(head>=0 && containmentIndexer.hidden.indexOf(head)>=0) {
                head = head + 1;
            }

            if (head >= 0 && containmentIndexer.clients.indexOf(head)>=0) {
                var headBridge = containmentIndexer.getClientBridge(head);

                if (headBridge && headBridge.client) {
                    return headBridge.client.firstTailItemIsSeparator;
                }
            }

            return (containmentIndexer.separators.indexOf(head)>=0);
        }

        //! VERBATIM copy of containment AppletItem.qml:273-289
        //! inMarginsArea at b0e606b2, same remaps
        function inMarginsArea(selfIsMarginsAreaSeparator, index) {
            if (selfIsMarginsAreaSeparator || containmentIndexer.marginsAreaSeparators.length === 0) {
                return false;
            }

            var tailMarginsAreaSeparatorsCount = 0;

            for(var i=0; i<containmentIndexer.marginsAreaSeparators.length; ++i) {
                if (containmentIndexer.marginsAreaSeparators[i] < index) {
                    tailMarginsAreaSeparatorsCount++;
                }
            }

            return (tailMarginsAreaSeparatorsCount % 2 === 1);
        }
    }

    QtObject {
        id: basicWalks

        //! VERBATIM copies of the BasicItem.qml walk bodies (178-260) at
        //! b0e606b2, remapped: abilityItem.abilities.indexer -> indexer
        //! (the real client twin above; its tail/headAppletIsSeparator
        //! properties resolve through the mock bridge exactly like the
        //! live ability chain), isSeparator/index/itemIndex -> parameters
        //! (BasicItem binds itemIndex: index, so both names are the same
        //! value here).
        function tailItemIsSeparator(selfIsSeparator, index) {
            if (selfIsSeparator || index < 0 ) {
                return false;
            }

            var tail = index - 1;

            while(tail>=0
                  && indexer.hidden.indexOf(tail)>=0
                  && indexer.separators.indexOf(tail)<0) {
                tail = tail - 1;
            }

            var hasTailItemSeparator = indexer.separators.indexOf(tail)>=0;

            if (!hasTailItemSeparator && index === indexer.firstVisibleItemIndex){
                return indexer.tailAppletIsSeparator;
            }

            return hasTailItemSeparator;
        }

        function tailItemIsVisibleSeparator(selfIsSeparator, index) {
            if (selfIsSeparator || index < 0 || !tailItemIsSeparator(selfIsSeparator, index) ) {
                return false;
            }

            var tail = index - 1;

            while(tail>=0 && indexer.hidden.indexOf(tail)>=0) {
                tail = tail - 1;
            }

            var hasTailItemSeparator = indexer.separators.indexOf(tail)>=0 && indexer.hidden.indexOf(tail)<0;

            if (!hasTailItemSeparator && index === indexer.firstVisibleItemIndex){
                return indexer.tailAppletIsSeparator;
            }

            return hasTailItemSeparator;
        }

        function headItemIsSeparator(selfIsSeparator, index) {
            if (selfIsSeparator || index < 0 ) {
                return false;
            }

            var head = index + 1;

            while(head>=0
                  && indexer.hidden.indexOf(head)>=0
                  && indexer.separators.indexOf(head)<0) {
                head = head + 1;
            }

            var hasHeadItemSeparator = indexer.separators.indexOf(head)>=0;

            if (!hasHeadItemSeparator && index === indexer.lastVisibleItemIndex){
                return indexer.headAppletIsSeparator;
            }

            return hasHeadItemSeparator;
        }

        function headItemIsVisibleSeparator(selfIsSeparator, index) {
            if (selfIsSeparator || index < 0 || !headItemIsSeparator(selfIsSeparator, index)) {
                return false;
            }

            var head = index + 1;

            while(head>=0 && indexer.hidden.indexOf(head)>=0) {
                head = head + 1;
            }

            var hasHeadItemSeparator = indexer.separators.indexOf(head)>=0 && indexer.hidden.indexOf(head)<0;

            if (!hasHeadItemSeparator && index === indexer.lastVisibleItemIndex){
                return indexer.headAppletIsSeparator;
            }

            return hasHeadItemSeparator;
        }
    }

    TestCase {
        name: "VisibleIndexGen"
        when: windowShown

        function configure(startConfigs, mainConfigs, endConfigs, clientIndex, clientHidden, taskConfigs) {
            root.startConfigs = startConfigs;
            root.mainConfigs = mainConfigs;
            root.endConfigs = endConfigs;
            root.clientAppletIndex = clientIndex;
            root.clientAppletIsHidden = clientHidden === true;
            root.taskConfigs = taskConfigs;
            wait(100); //! let Repeaters rebuild and Bindings settle
        }

        function allAppletConfigs() {
            var all = root.startConfigs.concat(root.mainConfigs).concat(root.endConfigs);
            all.push({ index: root.clientAppletIndex, isClient: true });
            return all;
        }

        function itemForIndex(idx) {
            var grids = [layoutsMock.startLayout, layoutsMock.mainLayout, layoutsMock.endLayout];
            for (var g = 0; g < grids.length; ++g) {
                for (var i = 0; i < grids[g].children.length; ++i) {
                    var child = grids[g].children[i];
                    if (child.index !== undefined && child.index === idx) {
                        return child;
                    }
                }
            }
            return null;
        }

        function snapshot(probeVisibleMax) {
            var s = { vis: {}, belongs: {}, appletId: [],
                      apTail: {}, apHead: {}, margins: {},
                      client: {}, clientVis: {}, basic: {} };

            var configs = allAppletConfigs();
            for (var c = 0; c < configs.length; ++c) {
                var idx = configs[c].index;
                if (idx >= 9999) {
                    continue; //! parked
                }
                var item = itemForIndex(idx);
                s.vis[idx] = containmentIndexer.visibleIndex(idx);
                s.apTail[idx] = appletWalks.tailAppletIsSeparator(item.isSeparator, idx);
                s.apHead[idx] = appletWalks.headAppletIsSeparator(item.isSeparator, idx);
                s.margins[idx] = appletWalks.inMarginsArea(item.isMarginsAreaSeparator, idx);
                var b = [];
                for (var v = 0; v <= probeVisibleMax; ++v) {
                    b.push(containmentIndexer.visibleIndexBelongsAtApplet(item, v));
                }
                s.belongs[idx] = b;
            }

            for (var v2 = 1; v2 <= probeVisibleMax; ++v2) {
                s.appletId.push(containmentIndexer.appletIdForVisibleIndex(v2));
            }

            s.client = {
                firstVisible: indexer.firstVisibleItemIndex,
                lastVisible: indexer.lastVisibleItemIndex,
                visibleItemsCount: indexer.visibleItemsCount,
                itemsCount: indexer.itemsCount,
                firstTailItemIsSeparator: indexer.firstTailItemIsSeparator,
                lastHeadItemIsSeparator: indexer.lastHeadItemIsSeparator
            };

            for (var t = 0; t < root.taskConfigs.length; ++t) {
                var ti = root.taskConfigs[t].index;
                s.clientVis[ti] = indexer.visibleIndex(ti);
                var sep = root.taskConfigs[t].isSeparator === true;
                s.basic[ti] = {
                    tail: basicWalks.tailItemIsSeparator(sep, ti),
                    tailVisible: basicWalks.tailItemIsVisibleSeparator(sep, ti),
                    head: basicWalks.headItemIsSeparator(sep, ti),
                    headVisible: basicWalks.headItemIsVisibleSeparator(sep, ti)
                };
            }

            return s;
        }

        function emit(name, probeVisibleMax) {
            console.info("CASE|" + name + "|" + JSON.stringify(snapshot(probeVisibleMax)));
        }

        function test_generate() {
            var plainStart = [{index:0, appletId:100}, {index:1, appletId:101}, {index:2, appletId:102}];
            var plainMain = [{index:3, appletId:103}];
            var plainEnd = [{index:5, appletId:105}, {index:6, appletId:106}];
            var threeTasks = [{index:0}, {index:1}, {index:2}];

            //! C1 all plain, client(3 tasks) at 4
            configure(plainStart, plainMain, plainEnd, 4, false, threeTasks);
            emit("plainRow", 9);

            //! C2 separators head/middle/tail
            configure([{index:0, isSeparator:true, appletId:100}, {index:1, appletId:101}, {index:2, appletId:102}],
                      [{index:3, isSeparator:true, appletId:103}], [{index:5, appletId:105}, {index:6, isSeparator:true, appletId:106}],
                      4, false, threeTasks);
            emit("separatorsHeadMiddleTail", 9);

            //! C3 hidden interleaved with separators
            configure([{index:0, appletId:100}, {index:1, isHidden:true, appletId:101}, {index:2, isSeparator:true, appletId:102}],
                      [{index:3, appletId:103}], [{index:5, isHidden:true, appletId:105}, {index:6, appletId:106}],
                      4, false, threeTasks);
            emit("hiddenInterleaved", 9);

            //! C4 client with 0 visible tasks (empty tasks row)
            configure(plainStart, plainMain, plainEnd, 4, false, []);
            emit("clientEmpty", 9);

            //! C5 client with 1 task
            configure(plainStart, plainMain, plainEnd, 4, false, [{index:0}]);
            emit("clientOne", 9);

            //! C6 client with mixed tasks: v sep v v hiddenTask
            configure(plainStart, plainMain, plainEnd, 4, false,
                      [{index:0}, {index:1, isSeparator:true}, {index:2}, {index:3}, {index:4, isHidden:true}]);
            emit("clientMixed", 9);

            //! C7 HIDDEN client applet with 3 tasks - the Qt5 -1-base
            //! defect evidence (belongs grid of index 4)
            configure(plainStart, plainMain, plainEnd, 4, true, threeTasks);
            emit("hiddenClient", 9);

            //! C8 margins-area separator pair at 1 and 5
            configure([{index:0, appletId:100}, {index:1, isMarginsAreaSeparator:true, appletId:101}, {index:2, appletId:102}],
                      [{index:3, appletId:103}], [{index:5, isMarginsAreaSeparator:true, appletId:105}, {index:6, appletId:106}],
                      4, false, threeTasks);
            emit("marginsPair", 9);

            //! C9 single margins-area separator at 3
            configure([{index:0, appletId:100}, {index:1, appletId:101}, {index:2, appletId:102}],
                      [{index:3, isMarginsAreaSeparator:true, appletId:103}], [{index:5, appletId:105}, {index:6, appletId:106}],
                      4, false, threeTasks);
            emit("marginsOdd", 9);

            //! C10 client edge separators feeding the containment walk
            //! delegation: tasks sep@0 (firstTail) and sep@2 (lastHead);
            //! a3 and a5 are the client's containment neighbors
            configure(plainStart, plainMain, plainEnd, 4, false,
                      [{index:0, isSeparator:true}, {index:1}, {index:2, isSeparator:true}]);
            emit("clientEdgeSeparators", 9);

            //! C11 hidden separator among tasks: plain vs visible walk split
            configure(plainStart, plainMain, plainEnd, 4, false,
                      [{index:0}, {index:1, isSeparator:true, isSeparatorHidden:true}, {index:2}]);
            emit("hiddenSeparatorTask", 9);

            //! C12 gap walk: endLayout at high indexes, 100 hidden
            configure(plainStart, plainMain, [{index:100, isHidden:true, appletId:105}, {index:101, appletId:106}],
                      4, false, threeTasks);
            emit("gapWalk", 9);

            //! C13 separator-only tasks row: the Qt5 firstTail/lastHead
            //! asymmetry (bounds collapse to -1)
            configure(plainStart, plainMain, plainEnd, 4, false, [{index:0, isSeparator:true}]);
            emit("separatorOnlyClient", 9);

            //! C14 containment separator neighboring the client + first
            //! task riding the row-edge fallback: a3 separator, tasks v v
            configure([{index:0, appletId:100}, {index:1, appletId:101}, {index:2, appletId:102}],
                      [{index:3, isSeparator:true, appletId:103}], plainEnd,
                      4, false, [{index:0}, {index:1}]);
            emit("firstVisibleFallback", 9);

            verify(true);
        }
    }
}
