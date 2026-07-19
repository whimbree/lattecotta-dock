/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// EX-02 ParabolicRouter (docs/tracking/QML_EXTRACTION_PLAN.md): equality tests
// against the REAL shipped propagation chain.
//
// Reference-vector generation method (per docs/reference/TESTING.md): every expected
// vector below was recorded by driving the SHIPPED containment chain
// offscreen (14 cases at HEAD 0613c2ae, the dead_* cases at 4fca5a39) -
// the real abilities definition ParabolicEffect hub, real containment
// ParabolicArea deciders and the real ParabolicEdgeSpacer, instantiated
// inside per-item mock context scopes (the files resolve appletItem/
// wrapper/communicator/parabolic through the context chain exactly as in
// production) over an 8-position row: [tailSpacer, 1..6, headSpacer],
// spread=5, zoom=1.6, item length 40, Center alignment, a mock bridge
// client recording handoffs. The plasmoid twin's decider
// (ParabolicEventsArea sltUpdateItemScale) was verified a strict subset
// by side-by-side read: same slice/splice and broadcast bodies, no
// bridge/margins-separator arms.
//
// The Sim below models the POST-CUTOVER receiver side (the two arms the
// QML slots keep: exact apply / clear broadcast) driven by the core's
// emission plan; equality of Sim output against the recorded chain
// vectors is therefore the whole-pipeline equivalence check, core and
// application contract together.
//
// The chain does NOT touch the hovered item itself (its zoom is applied
// by calculateParabolicScales in the shell), so expected vectors cover
// positions 1..6 minus the hovered one.

#include <QtTest>

#include "../../declarativeimports/core/units/parabolicrouter.h"

using namespace Latte::ParabolicRouter;
using Latte::ParabolicMath::computeScales;
using Latte::ParabolicMath::ScaleStacks;

namespace {

constexpr int kSpreadSteps = 2;   // spread 5
constexpr double kZoom = 1.6;
constexpr double kTotalsLength = 40.0;

struct Sim {
    QVector<double> scales;             // positions 0..7; spacers unused
    double tailLen = 0.0;
    double headLen = 0.0;
    QHash<int, QVector<QVector<double>>> clientStacks;

    explicit Sim(double preset = 1.0)
        : scales(8, preset)
    {
    }

    //! one item slot receiving an emission - the two arms every slot
    //! keeps after the cutover (deciders deleted)
    void receive(const QVector<RowItem> &row, int pos, const QVector<double> &stack)
    {
        switch (row[pos].kind) {
        case ItemKind::Normal:
        case ItemKind::Transparent:
            // the shells apply through updateScale(max(1, s))
            scales[pos] = qMax(1.0, stack.first());
            break;
        case ItemKind::BridgeClient:
            clientStacks[pos].append(stack);
            break;
        case ItemKind::EdgeSpacer:
        case ItemKind::DeadStop:
            // spacers have no exact-apply arm for emissions (absorbs are
            // direct calls); dead positions have no slots at all
            break;
        }
    }

    void apply(const QVector<RowItem> &row, const RouteResult &route, int step)
    {
        for (const auto &action : route.actions) {
            // exhaustive by construction: a new Action alternative fails
            // the static_assert at compile time
            std::visit([&](const auto &act) {
                using T = std::decay_t<decltype(act)>;
                if constexpr (std::is_same_v<T, ApplyScale>) {
                    receive(row, act.pos, {act.scale});
                } else if constexpr (std::is_same_v<T, SpacerAbsorb>) {
                    if (act.pos == 0) {
                        tailLen = act.factor * kTotalsLength;
                    } else {
                        headLen = act.factor * kTotalsLength;
                    }
                } else {
                    static_assert(std::is_same_v<T, ClientHandoff>);
                    receive(row, act.pos, act.stack);
                }
            }, action);
        }

        if (route.clearEmissionPos) {
            // ONE [1] emission: exact arm at the target, broadcast arm on
            // everything beyond it in the emission's direction (spacers
            // and dead positions excepted - no broadcast arm / no slots)
            receive(row, *route.clearEmissionPos, {1.0});
            for (int p = *route.clearEmissionPos + step; p >= 0 && p < row.size(); p += step) {
                if (row[p].kind == ItemKind::EdgeSpacer || row[p].kind == ItemKind::DeadStop) {
                    continue;
                }
                receive(row, p, {1.0});
            }
        }
    }
};

Sim run(const QVector<RowItem> &row, int hovered, double pct, double preset = 1.0)
{
    const ScaleStacks stacks = computeScales(pct, kSpreadSteps, kZoom, false);
    const Assignment a = assignScales(row, hovered, stacks, kSpreadSteps);

    Sim sim(preset);
    sim.apply(row, a.lower, -1);
    sim.apply(row, a.higher, +1);
    return sim;
}

QVector<RowItem> makeRow(const QVector<int> &transparents = {},
                         const QVector<int> &clients = {},
                         const QVector<int> &deads = {})
{
    QVector<RowItem> row(8);
    row[0].kind = ItemKind::EdgeSpacer;
    row[7].kind = ItemKind::EdgeSpacer;
    for (int t : transparents) {
        row[t].kind = ItemKind::Transparent;
    }
    for (int c : clients) {
        row[c].kind = ItemKind::BridgeClient;
    }
    for (int d : deads) {
        row[d].kind = ItemKind::DeadStop;
    }
    return row;
}

// recorded scales cover positions 1..6; the hovered position is skipped
// (the chain never touches it)
void compareScales(const Sim &sim, int hovered, const QVector<double> &recorded)
{
    for (int pos = 1; pos <= 6; ++pos) {
        if (pos == hovered) {
            continue;
        }
        const double got = sim.scales[pos];
        const double want = recorded[pos - 1];
        QVERIFY2(qAbs(got - want) < 1e-9,
                 qPrintable(QStringLiteral("pos %1: got %2 recorded %3")
                            .arg(pos).arg(got, 0, 'f', 12).arg(want, 0, 'f', 12)));
    }
}

}

class ParabolicRouterTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void plainRow_midHover();
    void plainRow_offCenterPointer();
    void edgeHover_tailSpacerAbsorbs();
    void edgeHover_headSpacerAbsorbs();
    void separator_isTransparent();
    void hiddenRun_partialStackReachesSpacer();
    void client_receivesLiveStackAsReceived_walkStops();
    void client_lowerDirection();
    void client_receivesClearTailThroughBroadcast();
    void clearTail_resetsPresetsBeyondSpread();
    void spacer_keepsStaleLengthWithoutExactTargeting();
    void overflow_leavesRowEdge();
    void clearEmissionPos_flagsInRowClearEmissions();
    void deadStop_liveStackDies();
    void deadStop_broadcastPassesDeadPosition();
    void deadStop_exactClearTargetSkipped();
    void entryOutsideRow_returnsStackAsOverflow();
};

void ParabolicRouterTest::plainRow_midHover()
{
    // CASE plain_mid_hover_idx3_pct50: [1.15,1.45,-,1.45,1.15,1] tail 0 head 0
    const Sim sim = run(makeRow(), 3, 0.5);
    compareScales(sim, 3, {1.15, 1.45, 1.0, 1.45, 1.15, 1.0});
    QCOMPARE(sim.tailLen, 0.0);
    QCOMPARE(sim.headLen, 0.0);
    QVERIFY(sim.clientStacks.isEmpty());
}

void ParabolicRouterTest::plainRow_offCenterPointer()
{
    // CASE plain_mid_hover_idx3_pct20: [1.24,1.54,-,1.36,1.06,1]
    const Sim sim = run(makeRow(), 3, 0.2);
    compareScales(sim, 3, {1.24, 1.54, 1.0, 1.36, 1.06, 1.0});
}

void ParabolicRouterTest::edgeHover_tailSpacerAbsorbs()
{
    // CASE edge_hover_idx1_pct50: [-,1.45,1.15,1,1,1] tailLen 24
    const Sim sim = run(makeRow(), 1, 0.5);
    compareScales(sim, 1, {1.0, 1.45, 1.15, 1.0, 1.0, 1.0});
    QVERIFY(qAbs(sim.tailLen - 24.0) < 1e-9);
    QCOMPARE(sim.headLen, 0.0);
}

void ParabolicRouterTest::edgeHover_headSpacerAbsorbs()
{
    // CASE edge_hover_idx6_pct50: [1,1,1,1.15,1.45,-] headLen 24
    const Sim sim = run(makeRow(), 6, 0.5);
    compareScales(sim, 6, {1.0, 1.0, 1.0, 1.15, 1.45, 1.0});
    QVERIFY(qAbs(sim.headLen - 24.0) < 1e-9);
    QCOMPARE(sim.tailLen, 0.0);
}

void ParabolicRouterTest::separator_isTransparent()
{
    // CASE separator_at3_hover_idx4_pct50 == marginssep case:
    // [1.15,1.45,1,-,1.45,1.15] - the stack passes position 3 unconsumed
    const Sim sim = run(makeRow({3}), 4, 0.5);
    compareScales(sim, 4, {1.15, 1.45, 1.0, 1.0, 1.45, 1.15});
    QCOMPARE(sim.tailLen, 0.0);
}

void ParabolicRouterTest::hiddenRun_partialStackReachesSpacer()
{
    // CASE hiddenrun_23_hover_idx4_pct50: [1.45,1,1,-,1.45,1.15] tailLen 6 -
    // two transparents pass, pos1 consumes 1.45, and the spacer absorbs the
    // PARTIAL leftover [1.15, 1]: (0.15+0)*40 = 6
    const Sim sim = run(makeRow({2, 3}), 4, 0.5);
    compareScales(sim, 4, {1.45, 1.0, 1.0, 1.0, 1.45, 1.15});
    QVERIFY(qAbs(sim.tailLen - 6.0) < 1e-9);
}

void ParabolicRouterTest::client_receivesLiveStackAsReceived_walkStops()
{
    // CASE client_at5_hover_idx4_pct50: [1,1.15,1.45,-,CLIENT,1] and the
    // client received the UNSPLICED stack [1.45..., 1.15, 1]; position 6
    // beyond it untouched at this level
    const Sim sim = run(makeRow({}, {5}), 4, 0.5);
    compareScales(sim, 4, {1.0, 1.15, 1.45, 1.0, 1.0, 1.0});

    QCOMPARE(sim.clientStacks[5].size(), 1);
    const auto &stack = sim.clientStacks[5].first();
    const ScaleStacks ref = computeScales(0.5, kSpreadSteps, kZoom, false);
    QCOMPARE(stack, ref.right);
}

void ParabolicRouterTest::client_lowerDirection()
{
    // CASE client_at2_hover_idx3_pct50: client at 2 receives the lower
    // stack as-received; position 1 beyond it untouched
    const Sim sim = run(makeRow({}, {2}), 3, 0.5);
    compareScales(sim, 3, {1.0, 1.0, 1.0, 1.45, 1.15, 1.0});

    QCOMPARE(sim.clientStacks[2].size(), 1);
    const ScaleStacks ref = computeScales(0.5, kSpreadSteps, kZoom, false);
    QCOMPARE(sim.clientStacks[2].first(), ref.left);
}

void ParabolicRouterTest::client_receivesClearTailThroughBroadcast()
{
    // CASE client_at5_preset_hover_idx1_pct50 (preset 1.5): the clear-tail
    // broadcast passes THROUGH the client region: pos 4 exact-clears, the
    // client receives [1], pos 6 clears via broadcast; the client's own
    // scale is never touched
    const Sim sim = run(makeRow({}, {5}), 1, 0.5, 1.5);
    compareScales(sim, 1, {1.5, 1.45, 1.15, 1.0, 1.5, 1.0});

    QCOMPARE(sim.clientStacks[5].size(), 1);
    QCOMPARE(sim.clientStacks[5].first(), QVector<double>({1.0}));
    QVERIFY(qAbs(sim.tailLen - 24.0) < 1e-9);
}

void ParabolicRouterTest::clearTail_resetsPresetsBeyondSpread()
{
    // CASE preset_reset_hover_idx3_pct50 (preset 1.5): position 6 resets
    // to 1 via the exact clear-tail; hovered stays preset (chain-untouched)
    const Sim sim = run(makeRow(), 3, 0.5, 1.5);
    compareScales(sim, 3, {1.15, 1.45, 1.5, 1.45, 1.15, 1.0});
}

void ParabolicRouterTest::spacer_keepsStaleLengthWithoutExactTargeting()
{
    // CASES stale_setup + stale_check: inflate the tail spacer from an edge
    // hover, then hover inward - the spacer keeps 24 because clear-tails
    // only reach spacers exactly targeted (no broadcast arm; Qt5-inherited)
    const auto row = makeRow();
    const ScaleStacks stacks = computeScales(0.5, kSpreadSteps, kZoom, false);

    Sim sim;
    sim.apply(row, assignScales(row, 1, stacks, kSpreadSteps).lower, -1);
    sim.apply(row, assignScales(row, 1, stacks, kSpreadSteps).higher, +1);
    QVERIFY(qAbs(sim.tailLen - 24.0) < 1e-9);

    const Assignment inward = assignScales(row, 4, stacks, kSpreadSteps);
    sim.apply(row, inward.lower, -1);
    sim.apply(row, inward.higher, +1);
    QVERIFY(qAbs(sim.tailLen - 24.0) < 1e-9); // untouched, not re-zeroed
    compareScales(sim, 4, {1.0, 1.15, 1.45, 1.0, 1.45, 1.15});
}

void ParabolicRouterTest::overflow_leavesRowEdge()
{
    // plasmoid-twin shape: no spacers, the stack leaves the row edge and
    // the remainder is exported through the bridge (sltTrack's
    // delegateIndex===-1 / >=itemsCount arms)
    QVector<RowItem> row(3); // three normals, indexes 0..2

    const ScaleStacks stacks = computeScales(0.5, kSpreadSteps, kZoom, false);
    const Assignment a = assignScales(row, 0, stacks, kSpreadSteps);

    // higher: 1 consumes 1.45, 2 consumes 1.15, clear-tail leaves at pos 3
    QCOMPARE(a.higher.overflow, QVector<double>({1.0}));
    QVERIFY(!a.higher.clearEmissionPos.has_value());
    // lower: the whole stack leaves at pos -1 untouched
    QCOMPARE(a.lower.overflow, stacks.left);
}

void ParabolicRouterTest::clearEmissionPos_flagsInRowClearEmissions()
{
    // the chain's sltTrack* forwarded every in-row clear-tail emission out
    // through the bridge; clearEmissionPos drives that export in the
    // plasmoid shell and the single [1] emission in both shells
    QVector<RowItem> row(6);
    const ScaleStacks stacks = computeScales(0.5, kSpreadSteps, kZoom, false);

    const Assignment mid = assignScales(row, 2, stacks, kSpreadSteps);
    QCOMPARE(mid.higher.clearEmissionPos.value_or(-1), 5); // exhausts at pos 5, in-row
    QVERIFY(mid.higher.overflow.isEmpty());
    QVERIFY(!mid.lower.clearEmissionPos.has_value()); // leaves at pos -1 instead
    QCOMPARE(mid.lower.overflow, QVector<double>({1.0}));
}

void ParabolicRouterTest::deadStop_liveStackDies()
{
    // CASE dead_at3_live_dies_hover_idx4_pct50 (preset 1.5): the lower
    // walk dies AT the dead position 3 - positions 1..3 keep their
    // presets, no clear ever fires in that direction
    const Sim sim = run(makeRow({}, {}, {3}), 4, 0.5, 1.5);
    compareScales(sim, 4, {1.5, 1.5, 1.5, 1.5, 1.45, 1.15});
    QCOMPARE(sim.tailLen, 0.0);
    QCOMPARE(sim.headLen, 0.0);
}

void ParabolicRouterTest::deadStop_broadcastPassesDeadPosition()
{
    // CASE dead_at5_broadcast_passes_hover_idx1_pct50 (preset 1.5): clear
    // emitted at 4; the dead 5 keeps its preset (no slots), 6 still
    // clears through the broadcast arm
    const Sim sim = run(makeRow({}, {}, {5}), 1, 0.5, 1.5);
    compareScales(sim, 1, {1.5, 1.45, 1.15, 1.0, 1.5, 1.0});
    QVERIFY(qAbs(sim.tailLen - 24.0) < 1e-9);
}

void ParabolicRouterTest::deadStop_exactClearTargetSkipped()
{
    // CASE dead_at5_exact_clear_hover_idx2_pct50 (preset 1.5): the clear
    // emission targets the dead 5 exactly - nothing applies there, the
    // broadcast still clears 6; lower direction: 1 consumes 1.45, the
    // tail spacer absorbs the partial leftover (0.15+0)*40 = 6
    const Sim sim = run(makeRow({}, {}, {5}), 2, 0.5, 1.5);
    compareScales(sim, 2, {1.45, 1.5, 1.45, 1.15, 1.5, 1.0});
    QVERIFY(qAbs(sim.tailLen - 6.0) < 1e-9);
}

void ParabolicRouterTest::entryOutsideRow_returnsStackAsOverflow()
{
    // the chain's emission at a gap index (the containment's inter-layout
    // index gaps): nothing matches exactly, the stack comes back whole so
    // the shell can re-emit it at the boundary (live: matches nobody;
    // clear: the broadcast arms act) - today's terminal behavior
    QVector<RowItem> row(3);
    const ScaleStacks stacks = computeScales(0.5, kSpreadSteps, kZoom, false);

    const RouteResult below = routeStack(row, -1, -1, stacks.left, kSpreadSteps);
    QVERIFY(below.actions.isEmpty());
    QCOMPARE(below.overflow, stacks.left);

    const RouteResult above = routeStack(row, 3, +1, stacks.right, kSpreadSteps);
    QVERIFY(above.actions.isEmpty());
    QCOMPARE(above.overflow, stacks.right);
}

QTEST_GUILESS_MAIN(ParabolicRouterTest)
#include "parabolicroutertest.moc"
