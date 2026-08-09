/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Equality-harness generator for EX-02: drives the REAL containment chain
// (definition ParabolicEffect hub + real ParabolicArea deciders + real
// ParabolicEdgeSpacer) over a synthetic 8-position row and prints the
// resulting vectors. Output lines: CASE|<name>|<json>
//
// Row layout (index space):
//   0 tailSpacer | 1 A | 2 A | 3 SEP | 4 A | 5 CLIENT | 6 A | 7 headSpacer
// kinds are re-configured per case. spread=5 (spreadSteps=2), zoom=1.6.
import QtQuick 2.7
import QtTest 1.2

import "../../../declarativeimports/abilities/definition" as Defs

Item {
    id: root
    property bool isHorizontal: true
    width: 400; height: 60

    Defs.ParabolicEffect {
        id: parabolic
        spread: 5
        property bool directRenderingEnabled: true
        factor.zoom: 1.6
        factor.maxZoom: 1.6
    }

    MockSpacer { id: sp0; spacerIndex: 0; beginIndex: 1; parabolic: parabolic }
    MockApplet { id: it1; index: 1; parabolic: parabolic }
    MockApplet { id: it2; index: 2; parabolic: parabolic }
    MockApplet { id: it3; index: 3; parabolic: parabolic }
    MockApplet { id: it4; index: 4; parabolic: parabolic }
    MockApplet { id: it5; index: 5; parabolic: parabolic }
    MockApplet { id: it6; index: 6; parabolic: parabolic }
    MockSpacer { id: sp7; spacerIndex: 7; beginIndex: 1; parabolic: parabolic }

    TestCase {
        name: "ParabolicChainGen"
        when: windowShown

        readonly property var items: [it1, it2, it3, it4, it5, it6]

        function configure(seps, hiddens, marginsSeps, clients, deads) {
            for (var i = 0; i < items.length; ++i) {
                var it = items[i];
                it.isSeparator = seps.indexOf(it.index) >= 0;
                it.isHidden = hiddens.indexOf(it.index) >= 0;
                it.isMarginsAreaSeparator = marginsSeps.indexOf(it.index) >= 0;
                it.isBridgeClient = clients.indexOf(it.index) >= 0;
                it.hasParabolicArea = deads === undefined || deads.indexOf(it.index) < 0;
            }
        }

        function resetAll(presetScale) {
            for (var i = 0; i < items.length; ++i) {
                items[i].resetScale();
                if (presetScale !== undefined) {
                    // preset non-1 scales so clear-tails are observable
                    items[i].presetScale(presetScale);
                }
                items[i].resetClientLog();
            }
            sp0.resetLength();
            sp7.resetLength();
        }

        function snapshot(hoveredIndex) {
            var scales = [];
            for (var i = 0; i < items.length; ++i) {
                scales.push(Number(items[i].zoomScale.toFixed(12)));
            }
            return {
                scales: scales,
                tailLen: Number(sp0.spacerLength.toFixed(12)),
                headLen: Number(sp7.spacerLength.toFixed(12)),
                clientLower: clientLogs("receivedLower"),
                clientHigher: clientLogs("receivedHigher"),
                hovered: hoveredIndex
            };
        }

        function clientLogs(prop) {
            var logs = {};
            for (var i = 0; i < items.length; ++i) {
                if (items[i].isBridgeClient && items[i][prop].length > 0) {
                    logs[items[i].index] = items[i][prop];
                }
            }
            return logs;
        }

        function drive(name, hoveredIndex, mousePct) {
            var it = null;
            for (var i = 0; i < items.length; ++i) {
                if (items[i].index === hoveredIndex) { it = items[i]; }
            }
            var length = 40;
            parabolic.applyParabolicEffect(hoveredIndex, mousePct * length, length);
            console.info("CASE|" + name + "|" + JSON.stringify(snapshot(hoveredIndex)));
        }

        function test_generate() {
            // 1. plain row, hover mid (idx 3 normal here)
            configure([], [], [], []);
            resetAll();
            drive("plain_mid_hover_idx3_pct50", 3, 0.5);

            resetAll();
            drive("plain_mid_hover_idx3_pct20", 3, 0.2);

            // 2. hover first item: tail spacer absorption at center alignment
            resetAll();
            drive("edge_hover_idx1_pct50", 1, 0.5);

            // 3. hover last item: head spacer absorption
            resetAll();
            drive("edge_hover_idx6_pct50", 6, 0.5);

            // 4. separator adjacent: idx3 separator, hover idx4
            configure([3], [], [], []);
            resetAll();
            drive("separator_at3_hover_idx4_pct50", 4, 0.5);

            // 5. margins-area separator transparency: idx3, hover idx4
            configure([], [], [3], []);
            resetAll();
            drive("marginssep_at3_hover_idx4_pct50", 4, 0.5);

            // 6. hidden run: idx2+idx3 hidden, hover idx4 (stack crosses two)
            configure([], [2, 3], [], []);
            resetAll();
            drive("hiddenrun_23_hover_idx4_pct50", 4, 0.5);

            // 7. bridge client mid-row: idx5 client, hover idx4 (higher dir hits client)
            configure([], [], [], [5]);
            resetAll();
            drive("client_at5_hover_idx4_pct50", 4, 0.5);

            // 8. bridge client receives clear-tail: hover idx1 far from client at 5
            configure([], [], [], [5]);
            resetAll();
            drive("client_at5_hover_idx1_pct50", 1, 0.5);

            // 8b. lower-direction client handoff: client at 2, hover 3
            configure([], [], [], [2]);
            resetAll();
            drive("client_at2_hover_idx3_pct50", 3, 0.5);

            // 9. clear-tail resets presets beyond the spread
            configure([], [], [], []);
            resetAll(1.5);
            drive("preset_reset_hover_idx3_pct50", 3, 0.5);

            // 10. spacer staleness: inflate tail spacer by edge hover, then
            // hover inward; spacer must KEEP its value (no broadcast arm)
            configure([], [], [], []);
            resetAll();
            drive("stale_setup_hover_idx1_pct50", 1, 0.5);
            drive("stale_check_hover_idx4_pct50", 4, 0.5);

            // 11. preset beyond a client under a clear-tail: the broadcast
            // clears items past the client too (index-range, not walk)
            configure([], [], [], [5]);
            resetAll(1.5);
            drive("client_at5_preset_hover_idx1_pct50", 1, 0.5);

            // 12. DEAD position (no ParabolicArea instance - the production
            // case is a lockZoom applet with thin tooltips off): a live
            // stack DIES at it, nothing beyond is touched
            configure([], [], [], [], [3]);
            resetAll(1.5);
            drive("dead_at3_live_dies_hover_idx4_pct50", 4, 0.5);

            // 13. the clear-tail BROADCAST passes a dead position: emitted
            // at 4, the dead 5 keeps its preset, 6 still clears
            configure([], [], [], [], [5]);
            resetAll(1.5);
            drive("dead_at5_broadcast_passes_hover_idx1_pct50", 1, 0.5);

            // 14. clear-tail emitted exactly AT a dead position: no exact
            // application there, the broadcast beyond still clears
            configure([], [], [], [], [5]);
            resetAll(1.5);
            drive("dead_at5_exact_clear_hover_idx2_pct50", 2, 0.5);

            verify(true);
        }
    }
}
