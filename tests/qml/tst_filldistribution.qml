/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! EX-05 FillLengthDistributor end-to-end equality harness: drives the
//! REAL shipped fill-length distribution entry point (containment
//! abilities/privates/LayouterPrivate.qml - post-cutover the gather/
//! apply shell around the FillDistributor plugin core, resolved through
//! the staged org.kde.latte.private.containment import) over mock grids
//! and asserts the per-item maxAutoFillLength/minAutoFillLength results.
//!
//! Generation method (docs/reference/TESTING.md): the expected values were recorded
//! by THIS file in record mode (property record: true prints CASE| lines
//! instead of asserting) against the pre-cutover in-QML implementation at
//! 71737d61, byte-identical to Qt5 f0ad7b23; the post-cutover run printed
//! byte-identical vectors before they were baked in. The same numbers pin
//! the pure core in tests/units/filldistributortest.cpp. The
//! intCoercion probe pins the QML double->int property write semantics
//! the core's coerceToQmlInt mirrors - if Qt ever changes them, this
//! fails before the dock misrenders.

import QtQuick 2.7
import QtTest 1.2

import org.kde.latte.core 0.2 as LatteCore

import "../../containment/package/contents/ui/abilities/privates" as Ability

Item {
    id: root
    width: 800
    height: 600

    //! flip to regenerate the reference vectors instead of asserting
    readonly property bool record: false

    // ---- context surface the shipped files resolve through the chain ----
    property bool isHorizontal: true
    property bool isVertical: false
    property real minLength: 600
    property real maxLength: 800
    readonly property int maxJustifySplitterSize: 64
    property bool editMode: false
    property bool inConfigureAppletsMode: false
    property bool inNormalFillCalculationsState: true
    property bool appletsInParentChange: false
    property var dragOverlay: null

    property QtObject myView: QtObject {
        property int alignment: LatteCore.Types.Center
    }

    property QtObject background: QtObject {
        property QtObject totals: QtObject {
            property real paddingsLength: 20
        }
    }

    //! the private's onFillAppletsChanged handlers call the public
    //! ability's debounced entry; the harness drives the private entry
    //! directly instead
    property QtObject layouter: QtObject {
        function updateSizeForAppletsInFill() {}
    }

    //! the applet-facing surface the shell's gatherer and the counter
    //! Bindings read. int metric/fill-length properties like the real
    //! AppletItem, so raw config values flow through the same double->int
    //! property coercion production has.
    component MockFill: Item {
        property bool isAutoFillApplet: false
        property bool isHidden: true
        property var applet: null
        property bool isInternalViewSplitter: false
        property bool isPlaceHolder: false
        property bool isParabolicEdgeSpacer: false
        property int appletMinimumLength: -1
        property int appletPreferredLength: -1
        property int appletMaximumLength: -1
        property int maxAutoFillLength: -1
        property int minAutoFillLength: -1
        property int index: 0
        width: 0
        height: 0
    }

    Item {
        id: mockIndexer
        property var hidden: []
        property var separators: []
    }

    Item {
        id: startGrid
        property real length: 0
        MockFill { index: 0 }
        MockFill { index: 1 }
        MockFill { index: 2 }
        MockFill { index: 3 }
    }

    Item {
        id: mainGrid
        property real length: 0
        MockFill { index: 4 }
        MockFill { index: 5 }
        MockFill { index: 6 }
        MockFill { index: 7 }
    }

    Item {
        id: endGrid
        property real length: 0
        MockFill { index: 8 }
        MockFill { index: 9 }
        MockFill { index: 10 }
        MockFill { index: 11 }
    }

    Item {
        id: mockLayouts
        property Item startLayout: startGrid
        property Item mainLayout: mainGrid
        property Item endLayout: endGrid
    }

    Ability.LayouterPrivate {
        id: layouterPrivate
        layouts: mockLayouts
        indexer: mockIndexer
    }

    //! coercion probe target: same declaration class as maxAutoFillLength
    property int coerceProbe: 0

    TestCase {
        name: "FillDistribution"
        when: windowShown

        readonly property var grids: [startGrid, mainGrid, endGrid]

        function resetAll() {
            root.myView.alignment = LatteCore.Types.Center;
            root.maxLength = 800;
            root.minLength = 600;
            root.background.totals.paddingsLength = 20;
            for (var g = 0; g < grids.length; ++g) {
                grids[g].length = 0;
                var kids = grids[g].children;
                for (var i = 0; i < kids.length; ++i) {
                    var it = kids[i];
                    it.isAutoFillApplet = false;
                    it.isHidden = true;
                    it.applet = null;
                    it.isInternalViewSplitter = false;
                    it.appletMinimumLength = -1;
                    it.appletPreferredLength = -1;
                    it.appletMaximumLength = -1;
                    it.maxAutoFillLength = -1;
                    it.minAutoFillLength = -1;
                    it.width = 0;
                }
            }
        }

        // kinds: fixed(w) | fill(min,pref,max) | fillNoApplet(min,pref,max)
        //        | hiddenFill | splitter | fillSplitter
        function configureGrid(grid, configs) {
            for (var i = 0; i < configs.length; ++i) {
                var c = configs[i];
                var it = grid.children[i];
                it.isHidden = false;
                if (c.k === "fixed") {
                    it.applet = {};
                    it.width = c.w;
                } else if (c.k === "fill") {
                    it.applet = {};
                    it.isAutoFillApplet = true;
                    it.appletMinimumLength = c.min;
                    it.appletPreferredLength = c.pref;
                    it.appletMaximumLength = c.max;
                } else if (c.k === "fillNoApplet") {
                    it.isAutoFillApplet = true;
                    it.appletMinimumLength = c.min;
                    it.appletPreferredLength = c.pref;
                    it.appletMaximumLength = c.max;
                } else if (c.k === "hiddenFill") {
                    it.isHidden = true;
                    it.applet = {};
                    it.isAutoFillApplet = true;
                    it.appletMinimumLength = c.min;
                    it.appletPreferredLength = c.pref;
                    it.appletMaximumLength = c.max;
                } else if (c.k === "splitter" || c.k === "fillSplitter") {
                    it.isInternalViewSplitter = true;
                    it.isAutoFillApplet = (c.k === "fillSplitter");
                    //! what ItemWrapper delivers for splitters, through
                    //! the same int coercion (Infinity max collapses to 0)
                    it.appletMinimumLength = root.maxJustifySplitterSize;
                    it.appletPreferredLength = root.maxJustifySplitterSize;
                    it.appletMaximumLength = Infinity;
                }
                if (c.presetMax !== undefined) {
                    it.maxAutoFillLength = c.presetMax;
                }
                if (c.presetMin !== undefined) {
                    it.minAutoFillLength = c.presetMin;
                }
            }
        }

        function outputsOf(grid, configs) {
            var res = [];
            for (var i = 0; i < configs.length; ++i) {
                res.push([grid.children[i].maxAutoFillLength,
                          grid.children[i].minAutoFillLength]);
            }
            return res;
        }

        function drive(name, c) {
            resetAll();
            if (c.justify) {
                root.myView.alignment = LatteCore.Types.Justify;
            }
            if (c.maxLength !== undefined) { root.maxLength = c.maxLength; }
            if (c.minLength !== undefined) { root.minLength = c.minLength; }
            configureGrid(startGrid, c.start || []);
            configureGrid(mainGrid, c.main || []);
            configureGrid(endGrid, c.end || []);
            startGrid.length = c.startLength || 0;
            mainGrid.length = c.mainLength || 0;
            endGrid.length = c.endLength || 0;

            wait(0); // let the counter Bindings settle before driving

            layouterPrivate._updateSizeForAppletsInFill();

            var out = {
                start: outputsOf(startGrid, c.start || []),
                main: outputsOf(mainGrid, c.main || []),
                end: outputsOf(endGrid, c.end || [])
            };

            if (root.record) {
                console.info("CASE|" + name + "|" + JSON.stringify(out));
                return;
            }

            var sides = ["start", "main", "end"];
            for (var s = 0; s < sides.length; ++s) {
                var expected = c.exp[sides[s]] || [];
                compare(out[sides[s]].length, expected.length, name + ": " + sides[s] + " item count");
                for (var i = 0; i < expected.length; ++i) {
                    compare(out[sides[s]][i][0], expected[i][0], name + ": " + sides[s] + "[" + i + "].maxAutoFillLength");
                    compare(out[sides[s]][i][1], expected[i][1], name + ": " + sides[s] + "[" + i + "].minAutoFillLength");
                }
            }
        }

        function test_intCoercionMatchesRecordedSemantics() {
            var values = [0.4, 0.5, 0.6, 1.5, 2.5, -0.5, -1.5, 33.333,
                          260.33333333333334, 390.5, Infinity, -Infinity];
            var expected = [0, 0, 0, 1, 2, 0, -1, 33, 260, 390, 0, 0];
            for (var i = 0; i < values.length; ++i) {
                root.coerceProbe = 0;
                root.coerceProbe = values[i];
                if (root.record) {
                    console.info("PROBE|" + values[i] + "|" + root.coerceProbe);
                } else {
                    compare(root.coerceProbe, expected[i],
                            "int property coercion of " + values[i]);
                }
            }
        }

        function test_onePool() {
            drive("onestep_single_neutral_fill", {
                main: [{k:"fill", min:-1, pref:-1, max:-1}],
                exp: {main: [[780, 600]]}
            });

            drive("onestep_two_fills_leftover_to_most_demanding", {
                main: [{k:"fill", min:10, pref:50, max:60},
                       {k:"fill", min:10, pref:40, max:60}],
                exp: {main: [[740, 50], [40, 0]]}
            });

            drive("onestep_mixed_fill_nofill", {
                main: [{k:"fixed", w:120},
                       {k:"fill", min:-1, pref:-1, max:-1},
                       {k:"fixed", w:60}],
                mainLength: 180,
                exp: {main: [[-1, -1], [600, 420], [-1, -1]]}
            });

            drive("onestep_zero_space", {
                maxLength: 200, minLength: 150,
                main: [{k:"fixed", w:120}, {k:"fixed", w:100},
                       {k:"fill", min:-1, pref:-1, max:-1}],
                mainLength: 220,
                exp: {main: [[-1, -1], [-1, -1], [0, 0]]}
            });

            drive("onestep_huge_pref_two_fills", {
                main: [{k:"fill", min:0, pref:2931, max:-1},
                       {k:"fill", min:-1, pref:-1, max:-1},
                       {k:"fixed", w:100}],
                mainLength: 100,
                exp: {main: [[340, 250], [340, 250], [-1, -1]]}
            });

            drive("onestep_huge_pref_single_fill", {
                main: [{k:"fill", min:0, pref:2931, max:-1},
                       {k:"fixed", w:100}],
                mainLength: 100,
                exp: {main: [[680, 500], [-1, -1]]}
            });

            drive("onestep_static_size", {
                main: [{k:"fill", min:80, pref:-1, max:80},
                       {k:"fill", min:-1, pref:-1, max:-1}],
                exp: {main: [[80, 80], [700, 520]]}
            });

            drive("onestep_min_gates_pref_quirk", {
                main: [{k:"fill", min:-1, pref:200, max:300},
                       {k:"fill", min:50, pref:60, max:70}],
                exp: {main: [[720, 540], [60, 60]]}
            });

            drive("onestep_max_zero_ignored_bug445869", {
                main: [{k:"fill", min:0, pref:30, max:0}],
                exp: {main: [[780, 30]]}
            });

            drive("onestep_no_fill_applets", {
                main: [{k:"fixed", w:100}],
                mainLength: 100,
                exp: {main: [[-1, -1]]}
            });

            drive("onestep_all_fill_odd_totals", {
                maxLength: 801, minLength: 601,
                main: [{k:"fill", min:-1, pref:-1, max:-1},
                       {k:"fill", min:-1, pref:-1, max:-1},
                       {k:"fill", min:-1, pref:-1, max:-1}],
                exp: {main: [[260, 200], [260, 200], [260, 200]]}
            });

            drive("onestep_neutral_split", {
                main: [{k:"fill", min:0, pref:0, max:-1},
                       {k:"fill", min:0, pref:0, max:-1}],
                exp: {main: [[390, 0], [390, 0]]}
            });

            drive("onestep_infinity_metrics_collapse_at_int_boundary", {
                main: [{k:"fill", min:Infinity, pref:Infinity, max:Infinity},
                       {k:"fill", min:-1, pref:-1, max:-1}],
                exp: {main: [[0, 0], [780, 600]]}
            });

            drive("onestep_hidden_fill_untouched", {
                main: [{k:"hiddenFill", min:-1, pref:-1, max:-1},
                       {k:"fill", min:-1, pref:-1, max:-1}],
                exp: {main: [[-1, -1], [780, 600]]}
            });

            drive("onestep_fill_without_applet_quirk", {
                main: [{k:"fillNoApplet", min:-1, pref:-1, max:-1},
                       {k:"fixed", w:100}],
                mainLength: 100,
                exp: {main: [[680, 500], [-1, -1]]}
            });

            drive("onestep_justify_empty_main", {
                justify: true,
                start: [{k:"fill", min:-1, pref:-1, max:-1}],
                end: [{k:"fill", min:20, pref:30, max:40}],
                exp: {start: [[750, 570]], end: [[30, 30]]}
            });

            drive("onestep_justify_remainder_order_start_first", {
                justify: true,
                start: [{k:"fill", min:10, pref:20, max:30}],
                end: [{k:"fill", min:10, pref:20, max:30}],
                exp: {start: [[760, 20]], end: [[20, 0]]}
            });
        }

        function test_twoSided() {
            drive("twostep_justify_basic", {
                justify: true,
                main: [{k:"fixed", w:100}],
                start: [{k:"fill", min:-1, pref:-1, max:-1}, {k:"fixed", w:50}],
                end: [{k:"fill", min:-1, pref:-1, max:-1}],
                mainLength: 100, startLength: 50, endLength: 0,
                exp: {start: [[290, 200], [-1, -1]], main: [[-1, -1]], end: [[340, 250]]}
            });

            drive("twostep_main_fill", {
                justify: true,
                main: [{k:"fill", min:-1, pref:-1, max:-1}, {k:"fixed", w:100}],
                start: [{k:"fill", min:-1, pref:-1, max:-1}],
                end: [{k:"fixed", w:50}],
                mainLength: 100, startLength: 0, endLength: 50,
                exp: {start: [[340, 250]], main: [[580, 400], [-1, -1]], end: [[-1, -1]]}
            });

            drive("twostep_odd_totals", {
                justify: true, maxLength: 801, minLength: 601,
                main: [{k:"fixed", w:101}],
                start: [{k:"fill", min:-1, pref:-1, max:-1}],
                end: [{k:"fill", min:-1, pref:-1, max:-1}],
                mainLength: 101,
                exp: {start: [[340, 250]], main: [[-1, -1]], end: [[340, 250]]}
            });

            drive("twostep_splitters_counted_at_max_size", {
                justify: true,
                start: [{k:"splitter"}, {k:"fill", min:-1, pref:-1, max:-1}],
                main: [{k:"fixed", w:100}],
                end: [{k:"fill", min:-1, pref:-1, max:-1}, {k:"splitter"}],
                mainLength: 100, startLength: 64, endLength: 64,
                exp: {start: [[-1, -1], [276, 186]], main: [[-1, -1]], end: [[276, 186], [-1, -1]]}
            });

            drive("twostep_fill_splitter_latent_branch", {
                justify: true,
                start: [{k:"fillSplitter"}],
                main: [{k:"fixed", w:100}],
                end: [{k:"fill", min:-1, pref:-1, max:-1}],
                mainLength: 100,
                exp: {start: [[340, 64]], main: [[-1, -1]], end: [[340, 250]]}
            });

            drive("twostep_main_skip_keeps_previous_values", {
                justify: true,
                main: [{k:"fill", min:-1, pref:-1, max:-1,
                        presetMax:333, presetMin:222},
                       {k:"fixed", w:100}],
                start: [{k:"fill", min:-1, pref:-1, max:-1}],
                end: [{k:"fixed", w:40}],
                mainLength: 100, startLength: 500, endLength: 40,
                exp: {start: [[340, 250]], main: [[333, 222], [-1, -1]], end: [[-1, -1]]}
            });

            drive("twostep_min_pass_subtracts_max_value_quirk", {
                justify: true, maxLength: 520, minLength: 700,
                main: [{k:"fill", min:0, pref:100, max:-1}, {k:"fixed", w:60}],
                start: [{k:"fill", min:0, pref:115, max:-1}],
                end: [{k:"fixed", w:40}],
                mainLength: 100, startLength: 0, endLength: 40,
                exp: {start: [[115, 300]], main: [[420, 620], [-1, -1]], end: [[-1, -1]]}
            });

            drive("twostep_all_in_main_sum_branch", {
                justify: true,
                main: [{k:"fill", min:-1, pref:-1, max:-1}, {k:"fixed", w:100}],
                mainLength: 100,
                exp: {main: [[680, 500], [-1, -1]]}
            });

            drive("twostep_negative_space_and_negative_spa_gate", {
                justify: true,
                main: [{k:"fixed", w:600}, {k:"fill", min:-1, pref:-1, max:-1}],
                start: [{k:"fixed", w:400}, {k:"fill", min:-1, pref:-1, max:-1}],
                end: [{k:"fixed", w:30}],
                mainLength: 600, startLength: 400, endLength: 30,
                exp: {start: [[-1, -1], [-1, -1]], main: [[-1, -1], [-1, -1]], end: [[-1, -1]]}
            });
        }
    }
}
