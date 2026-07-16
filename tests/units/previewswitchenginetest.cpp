/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// EX-01 PreviewSwitchEngine (docs/QML_EXTRACTION_PLAN.md): behavioral tests
// for the previews decision core, written BEFORE the engine from the spec's
// tables. Time is injected (plain qint64), no timers, no QML. Each slot
// names the excavation commit whose trap it pins; the QML mechanics that
// stay QML-side (rootIndex token, imperative host sizing, stream gating)
// remain pinned by scripts/preview-contract-rules.sh, untouched here.

#include <QtTest>

#include "../../plasmoid/plugin/units/previewswitchengine.h"

using Latte::Tasks::PreviewSwitchEngine;
using SwitchDecision = PreviewSwitchEngine::SwitchDecision;
using HideDecision = PreviewSwitchEngine::HideDecision;
using Materialize = PreviewSwitchEngine::Materialize;

class PreviewSwitchEngineTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void constants_settleStaysBelowBurstThreshold();
    void decide_firstShowIsImmediate();
    void decide_sameShownTaskNeverDefers();
    void decide_burstBoundaryTable();
    void decide_deferCancelsHideCountdownAndArmsSettle();
    void decide_cadenceAboveThresholdAdoptsPerIcon();
    void settle_adoptsLastHoveredTask();
    void settle_neverReentersBurstCheck();
    void settle_noAdoptionWhenPointerLeft();
    void settle_noAdoptionWhenDialogHidden();
    void shown_supersedesPendingSwitch();
    void dialogHidden_clearsPendingAndShownTask();
    void hide_countdownArmAndCancel();
    void hide_compositeTable();
    void lru_buildsOnceThenKeepsActive();
    void lru_reviveKeepsWarmEntries();
    void lru_buildAtCapacityEvictsOldest();
    void lru_dropRemovesWithoutEvictionSideEffects();
    void lru_activeSlotSurvivesDialogHide();
};

// helper: put the engine in the "dialog visible, task shown" state the
// sweep scenarios start from
static void showTask(PreviewSwitchEngine &e, int task, qint64 nowMs)
{
    e.materialize(task);
    e.decide({task, nowMs, false});
    e.shown(task);
}

void PreviewSwitchEngineTest::constants_settleStaysBelowBurstThreshold()
{
    // gate rule 4's invariant, migrated (4b533b8d): a settle interval at or
    // above the burst threshold degenerates a steady sweep back to one
    // rebuild stall per icon. Also enforced at compile time by the header's
    // static_assert; asserted here so the relation shows up in test output.
    QVERIFY(PreviewSwitchEngine::settleIntervalMs < PreviewSwitchEngine::switchBurstThresholdMs);

    // the shipped values themselves are contract: QML reads them from the
    // engine so the tested thresholds and the running ones cannot drift
    QCOMPARE(PreviewSwitchEngine::switchBurstThresholdMs, 350);
    QCOMPARE(PreviewSwitchEngine::settleIntervalMs, 250);
    QCOMPARE(PreviewSwitchEngine::hideCountdownMs, 300);
}

void PreviewSwitchEngineTest::decide_firstShowIsImmediate()
{
    PreviewSwitchEngine e;

    // dialog hidden, nothing shown: never defer, whatever the cadence
    auto r1 = e.decide({1, 1000, false});
    QCOMPARE(r1.decision, SwitchDecision::ShowNow);
    auto r2 = e.decide({2, 1001, false});
    QCOMPARE(r2.decision, SwitchDecision::ShowNow);

    // dialog visible but no shown task (first show after a forced hide)
    PreviewSwitchEngine e2;
    QCOMPARE(e2.decide({1, 1000, true}).decision, SwitchDecision::ShowNow);
}

void PreviewSwitchEngineTest::decide_sameShownTaskNeverDefers()
{
    PreviewSwitchEngine e;
    showTask(e, 1, 1000);

    // rapid same-task requests stay immediate (the QML arm stamps the
    // request time but never defers)
    QCOMPARE(e.decide({1, 1010, true}).decision, SwitchDecision::ShowNow);
    QCOMPARE(e.decide({1, 1020, true}).decision, SwitchDecision::ShowNow);
}

void PreviewSwitchEngineTest::decide_burstBoundaryTable()
{
    // 4b533b8d: a switch request within switchBurstThreshold of the
    // PREVIOUS request is a sweep in flight. Boundary cases either side.
    struct Case { qint64 gapMs; SwitchDecision expected; };
    const Case table[] = {
        {1, SwitchDecision::Defer},
        {349, SwitchDecision::Defer},
        {350, SwitchDecision::ShowNow},   // gap == threshold is NOT in burst (strict <)
        {351, SwitchDecision::ShowNow},
        {1000, SwitchDecision::ShowNow},
    };

    for (const auto &c : table) {
        PreviewSwitchEngine e;
        showTask(e, 1, 1000);
        // showTask's decide stamped nowMs=1000
        const auto r = e.decide({2, 1000 + c.gapMs, true});
        QVERIFY2(r.decision == c.expected,
                 qPrintable(QStringLiteral("gap %1ms: wrong decision").arg(c.gapMs)));
    }
}

void PreviewSwitchEngineTest::decide_deferCancelsHideCountdownAndArmsSettle()
{
    // 54ed1974: a deferred switch never reaches show(), whose first act
    // cancels the hide countdown every task exit arms - the Defer decision
    // must carry the cancel itself or a scrub hides the dialog mid-sweep.
    PreviewSwitchEngine e;
    showTask(e, 1, 1000);

    const auto r = e.decide({2, 1100, true});
    QCOMPARE(r.decision, SwitchDecision::Defer);
    QVERIFY(r.cancelHideCountdown);
    QVERIFY(r.armSettleTimer);
    QCOMPARE(e.pendingTask(), 2);

    // an immediate adoption carries no countdown instruction: show() itself
    // stops the countdown as its first act (that stays QML-side)
    PreviewSwitchEngine e2;
    showTask(e2, 1, 1000);
    const auto r2 = e2.decide({2, 2000, true});
    QCOMPARE(r2.decision, SwitchDecision::ShowNow);
    QVERIFY(!r2.cancelHideCountdown);
    QVERIFY(!r2.armSettleTimer);
}

void PreviewSwitchEngineTest::decide_cadenceAboveThresholdAdoptsPerIcon()
{
    // the 4b533b8d first-cut degeneration, generalized: with a threshold of
    // 120ms, 140ms crossings each fell outside the burst window and paid one
    // rebuild stall per icon. Pin the mechanism: crossings slower than the
    // threshold adopt immediately every single time.
    PreviewSwitchEngine e;
    showTask(e, 1, 1000);

    qint64 now = 1000;
    for (int task = 2; task <= 5; ++task) {
        now += PreviewSwitchEngine::switchBurstThresholdMs + 50;
        const auto r = e.decide({task, now, true});
        QCOMPARE(r.decision, SwitchDecision::ShowNow);
        e.shown(task);
    }
}

void PreviewSwitchEngineTest::settle_adoptsLastHoveredTask()
{
    // 4b533b8d: the settle timer adopts the LAST hovered task once the
    // requests stop; earlier deferred tasks in the same sweep are skipped.
    PreviewSwitchEngine e;
    showTask(e, 1, 1000);

    QCOMPARE(e.decide({2, 1100, true}).decision, SwitchDecision::Defer);
    QCOMPARE(e.decide({3, 1200, true}).decision, SwitchDecision::Defer);
    QCOMPARE(e.pendingTask(), 3);

    QCOMPARE(e.settle(true, true), 3);

    // adoption consumed the pending switch
    QCOMPARE(e.pendingTask(), PreviewSwitchEngine::kNoTask);
    QCOMPARE(e.settle(true, true), PreviewSwitchEngine::kNoTask);
}

void PreviewSwitchEngineTest::settle_neverReentersBurstCheck()
{
    // 4b533b8d: the settle adoption must not count as a switch request. If
    // settle() stamped the request clock, the next deliberate step would
    // measure its gap from the settle instead of from the last real request
    // and could defer when it should adopt immediately.
    PreviewSwitchEngine e;
    showTask(e, 1, 1000);

    QCOMPARE(e.decide({2, 1100, true}).decision, SwitchDecision::Defer); // stamps 1100
    QCOMPARE(e.settle(true, true), 2);                                   // fires ~1350; must NOT stamp
    e.shown(2);

    // gap from the last real request (1100) is 400 > threshold: immediate.
    // Had settle() stamped its own firing time (1350), this gap would read
    // 150 and wrongly defer.
    QCOMPARE(e.decide({3, 1500, true}).decision, SwitchDecision::ShowNow);
}

void PreviewSwitchEngineTest::settle_noAdoptionWhenPointerLeft()
{
    // d56a26aa family interleaving: defer -> exit everything -> settle
    // fires (its interval is shorter than the hide countdown). Adopting
    // would show content over nothing.
    PreviewSwitchEngine e;
    showTask(e, 1, 1000);

    QCOMPARE(e.decide({2, 1100, true}).decision, SwitchDecision::Defer);
    QCOMPARE(e.settle(false, true), PreviewSwitchEngine::kNoTask);
}

void PreviewSwitchEngineTest::settle_noAdoptionWhenDialogHidden()
{
    PreviewSwitchEngine e;
    showTask(e, 1, 1000);

    QCOMPARE(e.decide({2, 1100, true}).decision, SwitchDecision::Defer);
    QCOMPARE(e.settle(true, false), PreviewSwitchEngine::kNoTask);
}

void PreviewSwitchEngineTest::shown_supersedesPendingSwitch()
{
    // main.qml show(): a directly adopted task supersedes any deferred
    // switch still waiting on the settle timer
    PreviewSwitchEngine e;
    showTask(e, 1, 1000);

    QCOMPARE(e.decide({2, 1100, true}).decision, SwitchDecision::Defer);
    e.shown(3);
    QCOMPARE(e.pendingTask(), PreviewSwitchEngine::kNoTask);
    QCOMPARE(e.settle(true, true), PreviewSwitchEngine::kNoTask);
}

void PreviewSwitchEngineTest::dialogHidden_clearsPendingAndShownTask()
{
    PreviewSwitchEngine e;
    showTask(e, 1, 1000);
    QCOMPARE(e.decide({2, 1100, true}).decision, SwitchDecision::Defer);

    e.dialogHidden();

    QCOMPARE(e.pendingTask(), PreviewSwitchEngine::kNoTask);
    // no shown task anymore: the next request is a first show, immediate
    // even at burst cadence
    QCOMPARE(e.decide({2, 1150, true}).decision, SwitchDecision::ShowNow);
}

void PreviewSwitchEngineTest::hide_countdownArmAndCancel()
{
    PreviewSwitchEngine e;

    // pointer left everything, dialog visible: arm the countdown
    QCOMPARE(e.hoverChanged({true, false, false, false}), HideDecision::StartCountdown);
    // pointer entered the dialog: cancel
    QCOMPARE(e.hoverChanged({true, true, false, false}), HideDecision::CancelCountdown);
    // dialog not visible: hide() early-returns, nothing to arm
    QCOMPARE(e.hoverChanged({false, false, false, false}), HideDecision::None);
}

void PreviewSwitchEngineTest::hide_compositeTable()
{
    // hidePreviewWinTimer's composite (main.qml): any contains-mouse keeps
    // the dialog; an external drag hovering the active task keeps it too
    PreviewSwitchEngine e;

    struct Case {
        bool dialogContains, taskContains, dragOnActive;
        bool expectHide;
    };
    const Case table[] = {
        {false, false, false, true},
        {true, false, false, false},
        {false, true, false, false},
        {false, false, true, false},
        {true, true, false, false},
        {true, false, true, false},
        {false, true, true, false},
        {true, true, true, false},
    };

    for (const auto &c : table) {
        const bool hide = e.shouldHideNow({true, c.dialogContains, c.taskContains, c.dragOnActive});
        QVERIFY2(hide == c.expectHide,
                 qPrintable(QStringLiteral("composite (%1,%2,%3): wrong hide verdict")
                            .arg(c.dialogContains).arg(c.taskContains).arg(c.dragOnActive)));
    }
}

void PreviewSwitchEngineTest::lru_buildsOnceThenKeepsActive()
{
    PreviewSwitchEngine e;

    auto r = e.materialize(1);
    QCOMPARE(r.kind, Materialize::Build);
    QVERIFY(r.fresh);
    QCOMPARE(r.evictedTask, PreviewSwitchEngine::kNoTask);

    // same task again: the active delegate is reused, no rebuild
    r = e.materialize(1);
    QCOMPARE(r.kind, Materialize::KeepActive);
    QVERIFY(!r.fresh);
}

void PreviewSwitchEngineTest::lru_reviveKeepsWarmEntries()
{
    // f1edd103: a parked delegate's bindings stay live; a revisit must
    // revive, never rebuild
    PreviewSwitchEngine e;
    e.materialize(1);
    e.materialize(2); // parks 1

    auto r = e.materialize(1);
    QCOMPARE(r.kind, Materialize::Revive);
    QVERIFY(!r.fresh);
    QCOMPARE(e.parkedTasks(), QVector<int>({2}));
    QCOMPARE(e.activeCacheTask(), 1);
}

void PreviewSwitchEngineTest::lru_buildAtCapacityEvictsOldest()
{
    // capacity 4 (f1edd103): parking the fifth delegate evicts the oldest
    PreviewSwitchEngine e(4);
    for (int t = 1; t <= 5; ++t) {
        const auto r = e.materialize(t);
        QCOMPARE(r.kind, Materialize::Build);
        QCOMPARE(r.evictedTask, PreviewSwitchEngine::kNoTask);
    }
    QCOMPARE(e.parkedTasks(), QVector<int>({1, 2, 3, 4}));

    const auto r = e.materialize(6); // parks 5 -> depth 5 -> evict 1
    QCOMPARE(r.kind, Materialize::BuildEvicting);
    QCOMPARE(r.evictedTask, 1);
    QCOMPARE(e.parkedTasks(), QVector<int>({2, 3, 4, 5}));
}

void PreviewSwitchEngineTest::lru_dropRemovesWithoutEvictionSideEffects()
{
    // f1edd103's Component.onDestruction contract: a dying task's parked
    // delegate is dropped in place; nothing else moves
    PreviewSwitchEngine e(4);
    for (int t = 1; t <= 5; ++t) {
        e.materialize(t);
    }

    QVERIFY(e.drop(2));
    QCOMPARE(e.parkedTasks(), QVector<int>({1, 3, 4}));

    // dropping something not parked (active task, unknown task) is a no-op:
    // faithful to the QML body, which only scans the parked entries
    QVERIFY(!e.drop(5));
    QVERIFY(!e.drop(99));
    QCOMPARE(e.parkedTasks(), QVector<int>({1, 3, 4}));

    // the freed slot means the next build does NOT evict
    const auto r = e.materialize(6); // parks 5 -> depth 4, no eviction
    QCOMPARE(r.kind, Materialize::Build);
    QCOMPARE(r.evictedTask, PreviewSwitchEngine::kNoTask);
    QCOMPARE(e.parkedTasks(), QVector<int>({1, 3, 4, 5}));
}

void PreviewSwitchEngineTest::lru_activeSlotSurvivesDialogHide()
{
    // Defect found during extraction (fixed at origin here): the QML cache
    // keyed parking off windowsPreviewDlg.activeItem, which
    // forcePreviewsHiding() nulls BEFORE the next materialize runs - so
    // re-hovering the task just previewed rebuilt its delegate from scratch
    // (the 100-400ms stall the cache exists to prevent) and parked the old
    // delegate under a null task, unreachable for both revive and the
    // destruction-contract drop until depth eviction. The engine's cache
    // slot is independent UI state: hiding the dialog clears the SHOWN
    // task, never the cache association.
    PreviewSwitchEngine e;
    e.materialize(1);
    e.decide({1, 1000, false});
    e.shown(1);

    e.dialogHidden();

    const auto r = e.materialize(1);
    QCOMPARE(r.kind, Materialize::KeepActive);
    QVERIFY(!r.fresh);
    QCOMPARE(e.parkedTasks(), QVector<int>());
}

QTEST_GUILESS_MAIN(PreviewSwitchEngineTest)
#include "previewswitchenginetest.moc"
