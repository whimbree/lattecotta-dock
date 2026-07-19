/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// EX-11 LauncherListOps (docs/tracking/QML_EXTRACTION_PLAN.md): behavioral tests for
// the launcher list algebra core and its QML-facing wrappers (compiled into
// this sanitized binary, so the boundary refusals run instrumented too).
//
// Semantics pinned here are cross-checked against the SHIPPED pre-extraction
// QML by tests/qml/tst_launcherlistops.qml, which drives the real
// TasksExtendedManager.qml and launchers/Validator.qml and stays green
// across the EX-11 cutovers - the twin-equivalence evidence.

#include <QRegularExpression>
#include <QtTest>

// C++
#include <algorithm>
#include <vector>

#include "../../plasmoid/plugin/launcherlistbridge.h"
#include "../../plasmoid/plugin/units/launcherlistops.h"

using namespace Latte::LauncherLists;

namespace {

//! applies a plan with the same list-move semantics the QML caller uses
//! (QList::move == libtaskmanager TasksModel::move: remove at from, insert
//! at to)
QStringList applyPlan(QStringList list, const QVector<Move> &plan)
{
    for (const Move &move : plan) {
        list.move(move.from, move.to);
    }

    return list;
}

}

class LauncherListOpsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // stored-record grammar
    void parse_plainRecordsPreserveNonDefaultOrder();
    void parse_taggedRecords();
    void parse_malformedRecordsAreRefused_data();
    void parse_malformedRecordsAreRefused();
    void parse_emptyRecordStaysRepresentable();
    void select_activityMatchIsListMembership();
    void select_mixedListPreservesStoredOrder();

    // separator names
    void separator_candidateNamesAndOrder();
    void separator_allocationFillsGaps();
    void separator_everySingleGapIsFound();
    void separator_exhaustionIsExplicit();
    void separator_nameQuirkPinned_data();
    void separator_nameQuirkPinned();

    // move planning
    void plan_equalListsYieldEmptyPlan();
    void plan_adjacentSwapIsOneMove();
    void plan_appliesToGoalForAllPermutations();
    void plan_firstMoveStrictlyShrinksThePlan();
    void plan_membershipMismatchIsNotPlannable();
    void plan_duplicatesAreNotPlannable();

    // registries
    void launcherSet_substringMatchesBothSpellings();
    void launcherSet_substringEmptyIsInert();
    void launcherSet_exactKeepsSpellingsDistinct();
    void moveIntentSet_semantics();
    void frozenZoomSet_upsertSemantics();

    // QML-facing wrappers (boundary refusals + value mapping)
    void wrapper_launchersForActivityRefusesMalformedLoudly();
    void wrapper_freeSeparatorNameKeepsTheEmptyContract();
    void wrapper_planMovesMapsAndRefuses();
    void wrapper_registriesRefuseEmptyUrls();
    void wrapper_takeMoveIntentPositionIsAtomic();
    void wrapper_negativeMoveIntentPositionClampsLoudly();
};

void LauncherListOpsTest::parse_plainRecordsPreserveNonDefaultOrder()
{
    // launcher order is user-sacred data (the family-6 mitigation the risk
    // register mandates): a deliberately non-alphabetical stored order must
    // survive parse+select verbatim - no sorting, no dedup
    const QStringList stored{QStringLiteral("file:///c.desktop"),
                             QStringLiteral("file:///a.desktop"),
                             QStringLiteral("file:///b.desktop")};

    const ParsedStoredList parsed = parseStoredRecords(stored);

    QVERIFY(parsed.malformed.isEmpty());
    QCOMPARE(parsed.records.size(), 3);

    for (int i = 0; i < 3; ++i) {
        QCOMPARE(parsed.records.at(i).url, stored.at(i));
        QVERIFY(parsed.records.at(i).activities.isEmpty());
    }

    QCOMPARE(selectLaunchersForActivity(parsed.records, QStringLiteral("any-activity")), stored);
}

void LauncherListOpsTest::parse_taggedRecords()
{
    // the grammar libtaskmanager writes (launchertasksmodel.cpp v6.6.5:365):
    // '[' + activities.join(',') + "]\n" + url
    const ParsedStoredList parsed = parseStoredRecords(
        {QStringLiteral("[act-1]\nfile:///a.desktop"),
         QStringLiteral("[act-1,act-2]\nfile:///b.desktop")});

    QVERIFY(parsed.malformed.isEmpty());
    QCOMPARE(parsed.records.size(), 2);
    QCOMPARE(parsed.records.at(0).url, QStringLiteral("file:///a.desktop"));
    QCOMPARE(parsed.records.at(0).activities, (QStringList{QStringLiteral("act-1")}));
    QCOMPARE(parsed.records.at(1).url, QStringLiteral("file:///b.desktop"));
    QCOMPARE(parsed.records.at(1).activities,
             (QStringList{QStringLiteral("act-1"), QStringLiteral("act-2")}));
}

void LauncherListOpsTest::parse_malformedRecordsAreRefused_data()
{
    QTest::addColumn<QString>("record");

    // Qt5's reader mis-handled every one of these silently; the parse
    // refuses them into `malformed` instead (the spec's loud-errors rule)
    QTest::newRow("tag without newline: Qt5 showed the whole record as a launcher")
        << QStringLiteral("[act-1]file:///a.desktop");
    QTest::newRow("tag without url tail: Qt5 skipped silently")
        << QStringLiteral("[act-1]\n");
    QTest::newRow("empty tag: a launcher on no activities can never show")
        << QStringLiteral("[]\nfile:///a.desktop");
    QTest::newRow("bracket inside the url: Qt5 vanished the launcher")
        << QStringLiteral("file:///weird[1].desktop");
    QTest::newRow("junk between tag and newline: not the written grammar")
        << QStringLiteral("[act-1] \nfile:///a.desktop");
    QTest::newRow("unterminated tag")
        << QStringLiteral("[act-1\nfile:///a.desktop");
}

void LauncherListOpsTest::parse_malformedRecordsAreRefused()
{
    QFETCH(QString, record);

    const ParsedStoredList parsed = parseStoredRecords({record});

    QVERIFY(parsed.records.isEmpty());
    QCOMPARE(parsed.malformed, QStringList{record});
}

void LauncherListOpsTest::parse_emptyRecordStaysRepresentable()
{
    // Qt5 pushed the empty string through as a launcher; an empty KConfig
    // list entry is data, and judging urls is the model layer's job
    const ParsedStoredList parsed = parseStoredRecords({QString()});

    QVERIFY(parsed.malformed.isEmpty());
    QCOMPARE(parsed.records.size(), 1);
    QVERIFY(parsed.records.at(0).url.isEmpty());
}

void LauncherListOpsTest::select_activityMatchIsListMembership()
{
    // Qt5 substring-scanned the WHOLE raw record for the current activity
    // id: an id inside the URL text false-matched. The typed parse makes
    // that unrepresentable - deliberate deviation, recorded in the spec.
    const ParsedStoredList parsed =
        parseStoredRecords({QStringLiteral("[act-a]\nfile:///act-b-editor.desktop")});
    QVERIFY(parsed.malformed.isEmpty());

    QVERIFY(selectLaunchersForActivity(parsed.records, QStringLiteral("act-b")).isEmpty());
    QCOMPARE(selectLaunchersForActivity(parsed.records, QStringLiteral("act-a")),
             QStringList{QStringLiteral("file:///act-b-editor.desktop")});

    // membership is exact, never prefix: "act" matches nothing
    QVERIFY(selectLaunchersForActivity(parsed.records, QStringLiteral("act")).isEmpty());
}

void LauncherListOpsTest::select_mixedListPreservesStoredOrder()
{
    const ParsedStoredList parsed = parseStoredRecords(
        {QStringLiteral("file:///global.desktop"),
         QStringLiteral("[cur]\nfile:///here.desktop"),
         QStringLiteral("[other]\nfile:///elsewhere.desktop"),
         QStringLiteral("file:///global2.desktop")});
    QVERIFY(parsed.malformed.isEmpty());

    QCOMPARE(selectLaunchersForActivity(parsed.records, QStringLiteral("cur")),
             (QStringList{QStringLiteral("file:///global.desktop"),
                          QStringLiteral("file:///here.desktop"),
                          QStringLiteral("file:///global2.desktop")}));
}

void LauncherListOpsTest::separator_candidateNamesAndOrder()
{
    QCOMPARE(separatorName(1), QStringLiteral("file:///latte-separator1.desktop"));
    QCOMPARE(separatorName(19), QStringLiteral("file:///latte-separator19.desktop"));

    const QStringList candidates = separatorCandidates();
    QCOMPARE(candidates.size(), MAXSEPARATORS);
    QCOMPARE(candidates.first(), separatorName(1));
    QCOMPARE(candidates.last(), separatorName(19));
}

void LauncherListOpsTest::separator_allocationFillsGaps()
{
    QCOMPARE(freeSeparatorName({}), std::optional<QString>(separatorName(1)));

    // gap before growth: 1 and 3 taken allocates 2, not 4
    QCOMPARE(freeSeparatorName({separatorName(1), separatorName(3)}),
             std::optional<QString>(separatorName(2)));

    // sequential growth
    QCOMPARE(freeSeparatorName({separatorName(1), separatorName(2)}),
             std::optional<QString>(separatorName(3)));

    // non-separator launchers in the taken list never block a candidate
    QCOMPARE(freeSeparatorName({QStringLiteral("file:///firefox.desktop")}),
             std::optional<QString>(separatorName(1)));
}

void LauncherListOpsTest::separator_everySingleGapIsFound()
{
    // for each i: all names taken except i - allocation returns exactly i
    for (int gap = 1; gap <= MAXSEPARATORS; ++gap) {
        QStringList taken;

        for (int no = 1; no <= MAXSEPARATORS; ++no) {
            if (no != gap) {
                taken.append(separatorName(no));
            }
        }

        QCOMPARE(freeSeparatorName(taken), std::optional<QString>(separatorName(gap)));
    }
}

void LauncherListOpsTest::separator_exhaustionIsExplicit()
{
    QVERIFY(!freeSeparatorName(separatorCandidates()).has_value());
}

void LauncherListOpsTest::separator_nameQuirkPinned_data()
{
    QTest::addColumn<QString>("launcher");
    QTest::addColumn<bool>("isSeparator");

    QTest::newRow("real separator name")
        << QStringLiteral("file:///latte-separator5.desktop") << true;
    QTest::newRow("ordinary launcher")
        << QStringLiteral("file:///firefox.desktop") << false;
    // Qt5's second test is indexOf(".desktop") !== 1 - a position-one
    // check, not a contains check: anything carrying "latte-separator"
    // classifies as a separator even without ".desktop"...
    QTest::newRow("no .desktop suffix still classifies")
        << QStringLiteral("xlatte-separator") << true;
    // ...UNLESS ".desktop" sits exactly at index 1 - the quirk itself
    QTest::newRow(".desktop at index 1 declassifies")
        << QStringLiteral("a.desktop-latte-separator") << false;
}

void LauncherListOpsTest::separator_nameQuirkPinned()
{
    QFETCH(QString, launcher);
    QFETCH(bool, isSeparator);

    QCOMPARE(isSeparatorName(launcher), isSeparator);
}

void LauncherListOpsTest::plan_equalListsYieldEmptyPlan()
{
    const QStringList list{QStringLiteral("a"), QStringLiteral("b")};

    QCOMPARE(planMoves(list, list), std::optional<QVector<Move>>(QVector<Move>{}));
    QCOMPARE(planMoves({}, {}), std::optional<QVector<Move>>(QVector<Move>{}));
}

void LauncherListOpsTest::plan_adjacentSwapIsOneMove()
{
    // the spec's minimality floor: an adjacent transposition is one move
    const QStringList current{QStringLiteral("a"), QStringLiteral("c"), QStringLiteral("b")};
    const QStringList goal{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")};

    const auto plan = planMoves(current, goal);

    QVERIFY(plan.has_value());
    QCOMPARE(plan->size(), 1);
    QCOMPARE(plan->first(), (Move{2, 1}));
    QCOMPARE(applyPlan(current, *plan), goal);
}

void LauncherListOpsTest::plan_appliesToGoalForAllPermutations()
{
    // exhaustive property check, n = 1..5 (all 153 permutations, generated
    // in-test with std::next_permutation - not derived from the
    // implementation): applying the plan to current always yields goal,
    // and every move is in-bounds with from > to never required (only
    // from > position being fixed)
    for (int n = 1; n <= 5; ++n) {
        QStringList goal;

        for (int i = 0; i < n; ++i) {
            goal.append(QString(QChar(u'a' + i)));
        }

        std::vector<QString> permutation(goal.cbegin(), goal.cend());

        do {
            QStringList current;

            for (const QString &item : permutation) {
                current.append(item);
            }

            const auto plan = planMoves(current, goal);
            QVERIFY(plan.has_value());

            for (const Move &move : *plan) {
                QVERIFY(move.from >= 0 && move.from < n);
                QVERIFY(move.to >= 0 && move.to < n);
                QVERIFY(move.from > move.to); // the pull always reaches right
            }

            QCOMPARE(applyPlan(current, *plan), goal);
        } while (std::next_permutation(permutation.begin(), permutation.end()));
    }
}

void LauncherListOpsTest::plan_firstMoveStrictlyShrinksThePlan()
{
    // the termination guarantee the Qt5 heuristic loop lacked, and what
    // the one-move-per-tick QML caller leans on: applying only the first
    // move and replanning strictly shrinks the plan, for every permutation
    const int n = 5;
    QStringList goal;

    for (int i = 0; i < n; ++i) {
        goal.append(QString(QChar(u'a' + i)));
    }

    std::vector<QString> permutation(goal.cbegin(), goal.cend());

    do {
        QStringList working;

        for (const QString &item : permutation) {
            working.append(item);
        }

        auto plan = planMoves(working, goal);
        QVERIFY(plan.has_value());

        while (!plan->isEmpty()) {
            const qsizetype planSize = plan->size();

            working.move(plan->first().from, plan->first().to);
            plan = planMoves(working, goal);

            QVERIFY(plan.has_value());
            QVERIFY(plan->size() < planSize);
        }

        QCOMPARE(working, goal);
    } while (std::next_permutation(permutation.begin(), permutation.end()));
}

void LauncherListOpsTest::plan_membershipMismatchIsNotPlannable()
{
    // same length, different membership: the normal settling state - the
    // caller retries when the model catches up
    QVERIFY(!planMoves({QStringLiteral("a"), QStringLiteral("y")},
                       {QStringLiteral("a"), QStringLiteral("b")}).has_value());

    // different lengths
    QVERIFY(!planMoves({QStringLiteral("a")},
                       {QStringLiteral("a"), QStringLiteral("b")}).has_value());
    QVERIFY(!planMoves({QStringLiteral("a"), QStringLiteral("b")}, {}).has_value());
}

void LauncherListOpsTest::plan_duplicatesAreNotPlannable()
{
    // duplicate urls cannot happen in a launcher model; a plan over them
    // would be ambiguous
    const QStringList doubled{QStringLiteral("a"), QStringLiteral("a")};

    QVERIFY(!planMoves(doubled, doubled).has_value());
    QVERIFY(!planMoves(doubled, {QStringLiteral("a"), QStringLiteral("b")}).has_value());
}

void LauncherListOpsTest::launcherSet_substringMatchesBothSpellings()
{
    // WHY substring: the same launcher travels as the bare url and as the
    // url?iconData=... spelling (launcherUrlWithIcon); either probe must
    // find a record stored under the other spelling
    LauncherSet set(LauncherSet::Equality::SubstringEitherWay);

    QVERIFY(set.add(QStringLiteral("file:///a.desktop")));
    QVERIFY(set.contains(QStringLiteral("file:///a.desktop?iconData=payload")));
    QVERIFY(set.contains(QStringLiteral("file:///a.desktop")));

    // add-if-absent across spellings: the iconData variant is already
    // covered, so it does not insert
    QVERIFY(!set.add(QStringLiteral("file:///a.desktop?iconData=payload")));
    QCOMPARE(set.count(), 1);

    // stored-contains-probe direction: record with icon data, bare probe
    LauncherSet reversed(LauncherSet::Equality::SubstringEitherWay);
    QVERIFY(reversed.add(QStringLiteral("file:///b.desktop?iconData=payload")));
    QVERIFY(reversed.contains(QStringLiteral("file:///b.desktop")));

    // remove through the other spelling removes the one record
    QVERIFY(reversed.remove(QStringLiteral("file:///b.desktop")));
    QCOMPARE(reversed.count(), 0);
    QVERIFY(!reversed.remove(QStringLiteral("file:///b.desktop")));
}

void LauncherListOpsTest::launcherSet_substringEmptyIsInert()
{
    // Qt5's equals() required both sides nonempty: an empty record or
    // probe never matches anything (the wrapper refuses empty adds before
    // they reach here; the core stays total)
    LauncherSet set(LauncherSet::Equality::SubstringEitherWay);

    QVERIFY(set.add(QString())); // inserts: nothing can match it, even itself
    QVERIFY(!set.contains(QString()));
    QVERIFY(!set.contains(QStringLiteral("file:///a.desktop")));
    QVERIFY(!set.remove(QString()));
    QCOMPARE(set.count(), 1);

    set.clear();
    QCOMPARE(set.count(), 0);
}

void LauncherListOpsTest::launcherSet_exactKeepsSpellingsDistinct()
{
    // exact registries (immediate, to-be-removed): the two spellings are
    // distinct keys - RealRemovalAnimation probes both deliberately
    LauncherSet set(LauncherSet::Equality::Exact);

    QVERIFY(set.add(QStringLiteral("file:///a.desktop")));
    QVERIFY(!set.contains(QStringLiteral("file:///a.desktop?iconData=payload")));
    QVERIFY(set.contains(QStringLiteral("file:///a.desktop")));

    QVERIFY(set.add(QStringLiteral("file:///a.desktop?iconData=payload")));
    QCOMPARE(set.count(), 2);

    QVERIFY(set.remove(QStringLiteral("file:///a.desktop")));
    QVERIFY(set.contains(QStringLiteral("file:///a.desktop?iconData=payload")));
    QCOMPARE(set.items(), QStringList{QStringLiteral("file:///a.desktop?iconData=payload")});
}

void LauncherListOpsTest::moveIntentSet_semantics()
{
    MoveIntentSet intents;

    QVERIFY(intents.add(QStringLiteral("file:///a.desktop"), 3));
    QCOMPARE(intents.positionOf(QStringLiteral("file:///a.desktop")), std::optional<int>(3));
    QVERIFY(intents.contains(QStringLiteral("file:///a.desktop")));

    // a second intent for the same launcher keeps the first position (the
    // Qt5 isLauncherToBeMoved add guard)
    QVERIFY(!intents.add(QStringLiteral("file:///a.desktop"), 7));
    QCOMPARE(intents.positionOf(QStringLiteral("file:///a.desktop")), std::optional<int>(3));
    QCOMPARE(intents.count(), 1);

    QVERIFY(!intents.positionOf(QStringLiteral("file:///other.desktop")).has_value());
    QVERIFY(!intents.remove(QStringLiteral("file:///other.desktop")));

    QVERIFY(intents.remove(QStringLiteral("file:///a.desktop")));
    QCOMPARE(intents.count(), 0);
}

void LauncherListOpsTest::frozenZoomSet_upsertSemantics()
{
    FrozenZoomSet zooms;

    zooms.setZoom(QStringLiteral("task-a"), 1.5);
    QCOMPARE(zooms.zoomOf(QStringLiteral("task-a")), std::optional<qreal>(1.5));

    // upsert in place: no duplicate entry, the zoom updates
    zooms.setZoom(QStringLiteral("task-a"), 1.8);
    QCOMPARE(zooms.zoomOf(QStringLiteral("task-a")), std::optional<qreal>(1.8));
    QCOMPARE(zooms.count(), 1);

    QVERIFY(!zooms.zoomOf(QStringLiteral("task-b")).has_value());

    QVERIFY(zooms.remove(QStringLiteral("task-a")));
    QVERIFY(!zooms.zoomOf(QStringLiteral("task-a")).has_value());
    QVERIFY(!zooms.remove(QStringLiteral("task-a")));
}

void LauncherListOpsTest::wrapper_launchersForActivityRefusesMalformedLoudly()
{
    const Latte::Tasks::LauncherListOps ops;

    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QStringLiteral("refusing malformed stored launcher record")));

    const QStringList launchers = ops.launchersForActivity(
        {QStringLiteral("file:///global.desktop"),
         QStringLiteral("[act-1]file:///broken.desktop"), // tag without newline
         QStringLiteral("[cur]\nfile:///here.desktop")},
        QStringLiteral("cur"));

    QCOMPARE(launchers, (QStringList{QStringLiteral("file:///global.desktop"),
                                     QStringLiteral("file:///here.desktop")}));
}

void LauncherListOpsTest::wrapper_freeSeparatorNameKeepsTheEmptyContract()
{
    const Latte::Tasks::LauncherListOps ops;

    QCOMPARE(ops.freeSeparatorName({}), separatorName(1));

    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QStringLiteral("internal separator names are taken")));
    QCOMPARE(ops.freeSeparatorName(separatorCandidates()), QString());
}

void LauncherListOpsTest::wrapper_planMovesMapsAndRefuses()
{
    const Latte::Tasks::LauncherListOps ops;

    const QVariant plan = ops.planMoves({QStringLiteral("a"), QStringLiteral("c"), QStringLiteral("b")},
                                        {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    QVERIFY(plan.isValid());

    const QVariantList moves = plan.toList();
    QCOMPARE(moves.size(), 1);
    QCOMPARE(moves.first().toMap().value(QStringLiteral("from")).toInt(), 2);
    QCOMPARE(moves.first().toMap().value(QStringLiteral("to")).toInt(), 1);

    // membership gap: undefined, silently - normal settling, the QML
    // caller restarts its timer
    QVERIFY(!ops.planMoves({QStringLiteral("a")}, {QStringLiteral("b")}).isValid());

    // duplicates: undefined AND loud - a model anomaly, not churn
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QStringLiteral("duplicate launcher urls")));
    QVERIFY(!ops.planMoves({QStringLiteral("a"), QStringLiteral("a")},
                           {QStringLiteral("a"), QStringLiteral("a")}).isValid());
}

void LauncherListOpsTest::wrapper_registriesRefuseEmptyUrls()
{
    // deviation from Qt5, recorded in the spec: Qt5 pushed an inert ghost
    // record whose add still latched the paused-state counts until the 30s
    // GC; the boundary refuses instead, so the false return keeps the QML
    // count latches untouched
    Latte::Tasks::TasksExtendedRegistries registries;

    const QRegularExpression refusal(QStringLiteral("refusing an empty launcher url"));

    QTest::ignoreMessage(QtWarningMsg, refusal);
    QVERIFY(!registries.addWaiting(QString()));
    QCOMPARE(registries.waitingCount(), 0);

    QTest::ignoreMessage(QtWarningMsg, refusal);
    QVERIFY(!registries.addImmediate(QString()));

    QTest::ignoreMessage(QtWarningMsg, refusal);
    QVERIFY(!registries.addToBeAdded(QString()));

    QTest::ignoreMessage(QtWarningMsg, refusal);
    QVERIFY(!registries.addToBeRemoved(QString()));

    QTest::ignoreMessage(QtWarningMsg, refusal);
    QVERIFY(!registries.addMoveIntent(QString(), 2));

    QTest::ignoreMessage(QtWarningMsg, refusal);
    registries.setFrozenZoom(QString(), 1.4);
    QVERIFY(!registries.frozenZoom(QString()).isValid());
}

void LauncherListOpsTest::wrapper_takeMoveIntentPositionIsAtomic()
{
    Latte::Tasks::TasksExtendedRegistries registries;

    QVERIFY(registries.addMoveIntent(QStringLiteral("file:///a.desktop"), 4));

    const QVariant taken = registries.takeMoveIntentPosition(QStringLiteral("file:///a.desktop"));
    QVERIFY(taken.isValid());
    QCOMPARE(taken.toInt(), 4);

    // consumed: the intent is gone, a second take finds nothing
    QVERIFY(!registries.moveIntentExists(QStringLiteral("file:///a.desktop")));
    QVERIFY(!registries.takeMoveIntentPosition(QStringLiteral("file:///a.desktop")).isValid());
}

void LauncherListOpsTest::wrapper_negativeMoveIntentPositionClampsLoudly()
{
    // Qt5 clamped silently (Math.max(0,toPos)); the only live emitter
    // already clamps, so this boundary path is a caller bug worth hearing
    // about - the clamp keeps the Qt5 outcome
    Latte::Tasks::TasksExtendedRegistries registries;

    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QStringLiteral("negative move-intent position")));
    QVERIFY(registries.addMoveIntent(QStringLiteral("file:///a.desktop"), -4));
    QCOMPARE(registries.takeMoveIntentPosition(QStringLiteral("file:///a.desktop")).toInt(), 0);
}

QTEST_GUILESS_MAIN(LauncherListOpsTest)
#include "launcherlistopstest.moc"
