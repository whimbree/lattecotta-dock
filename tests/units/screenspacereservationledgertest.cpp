/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../../app/view/helpers/screenspacereservationledger.h"

#include <QtTest>

using Latte::ViewPart::ReservationDepth;
using Latte::ViewPart::ReservationEdge;
using Latte::ViewPart::ReservationGroupKey;
using Latte::ViewPart::ReservationMemberId;
using Latte::ViewPart::ReservationOutputId;
using Latte::ViewPart::ScreenSpaceReservationLedger;

namespace {

constexpr ReservationOutputId output(const int value)
{
    return *ReservationOutputId::fromPersistentId(value);
}

constexpr ReservationMemberId member(const std::uint64_t value)
{
    return *ReservationMemberId::fromPersistentDockId(value);
}

constexpr ReservationDepth depth(const int value)
{
    return *ReservationDepth::fromPixels(value);
}

constexpr ReservationGroupKey group(
    const int outputId,
    const ReservationEdge edge)
{
    return ReservationGroupKey{output(outputId), edge};
}

}

class ScreenSpaceReservationLedgerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void rejectsInvalidStrongValues();
    void publishesMaximumIndependentOfInsertionOrder();
    void fallsBackWhenDeepestMemberLeaves();
    void updatesOneMembersDepth();
    void tearsDownTheLastMember();
    void migratesBetweenEdgesAndOutputs();
    void keepsOutputsIndependent();
    void reportsCanonicalContributorsAndGroups();
    void keepsCommittedStateWhenCandidateIsDiscarded();
};

void ScreenSpaceReservationLedgerTest::rejectsInvalidStrongValues()
{
    QVERIFY(!ReservationOutputId::fromPersistentId(-1));
    QVERIFY(!ReservationMemberId::fromPersistentDockId(0));
    QVERIFY(!ReservationDepth::fromPixels(0));
    QVERIFY(!ReservationDepth::fromPixels(-1));
}

void ScreenSpaceReservationLedgerTest::publishesMaximumIndependentOfInsertionOrder()
{
    const auto bottom = group(10, ReservationEdge::Bottom);
    for (const bool deepestFirst : {false, true}) {
        ScreenSpaceReservationLedger ledger;
        const auto first = deepestFirst ? depth(72) : depth(48);
        const auto second = deepestFirst ? depth(48) : depth(72);

        static_cast<void>(ledger.updateContribution(member(14), bottom, first));
        static_cast<void>(ledger.updateContribution(member(19), bottom, second));

        const auto state = ledger.describeGroup(bottom);
        QVERIFY(state);
        QCOMPARE(state->maximumDepth.pixels(), 72);
        QCOMPARE(state->memberCount, std::size_t{2});
        QCOMPARE(ledger.groupCount(), std::size_t{1});
    }
}

void ScreenSpaceReservationLedgerTest::fallsBackWhenDeepestMemberLeaves()
{
    ScreenSpaceReservationLedger ledger;
    const auto left = group(10, ReservationEdge::Left);
    static_cast<void>(ledger.updateContribution(member(4), left, depth(40)));
    static_cast<void>(ledger.updateContribution(member(9), left, depth(68)));

    const auto change = ledger.removeContribution(member(9));

    QVERIFY(change.changed);
    QCOMPARE(change.affectedGroups.size(), std::size_t{1});
    const auto state = ledger.describeGroup(left);
    QVERIFY(state);
    QCOMPARE(state->maximumDepth.pixels(), 40);
    QCOMPARE(state->memberCount, std::size_t{1});
}

void ScreenSpaceReservationLedgerTest::updatesOneMembersDepth()
{
    ScreenSpaceReservationLedger ledger;
    const auto top = group(10, ReservationEdge::Top);
    static_cast<void>(ledger.updateContribution(member(1), top, depth(44)));
    static_cast<void>(ledger.updateContribution(member(2), top, depth(60)));

    const auto change = ledger.updateContribution(member(2), top, depth(36));

    QVERIFY(change.changed);
    QCOMPARE(change.affectedGroups.size(), std::size_t{1});
    const auto state = ledger.describeGroup(top);
    QVERIFY(state);
    QCOMPARE(state->maximumDepth.pixels(), 44);
    QCOMPARE(state->memberCount, std::size_t{2});
}

void ScreenSpaceReservationLedgerTest::tearsDownTheLastMember()
{
    ScreenSpaceReservationLedger ledger;
    const auto right = group(10, ReservationEdge::Right);
    static_cast<void>(ledger.updateContribution(member(7), right, depth(52)));

    const auto change = ledger.removeContribution(member(7));

    QVERIFY(change.changed);
    QVERIFY(!ledger.describeGroup(right));
    QCOMPARE(ledger.memberCount(), std::size_t{0});
    QCOMPARE(ledger.groupCount(), std::size_t{0});
}

void ScreenSpaceReservationLedgerTest::migratesBetweenEdgesAndOutputs()
{
    ScreenSpaceReservationLedger ledger;
    const auto oldGroup = group(10, ReservationEdge::Bottom);
    const auto newGroup = group(11, ReservationEdge::Right);
    static_cast<void>(ledger.updateContribution(member(3), oldGroup, depth(36)));

    const auto change = ledger.updateContribution(member(3), newGroup, depth(64));

    QVERIFY(change.changed);
    QCOMPARE(change.affectedGroups.size(), std::size_t{2});
    QVERIFY(!ledger.describeGroup(oldGroup));
    const auto state = ledger.describeGroup(newGroup);
    QVERIFY(state);
    QCOMPARE(state->maximumDepth.pixels(), 64);
    QCOMPARE(ledger.findGroup(member(3)), std::optional{newGroup});
}

void ScreenSpaceReservationLedgerTest::keepsOutputsIndependent()
{
    ScreenSpaceReservationLedger ledger;
    const auto firstOutput = group(10, ReservationEdge::Bottom);
    const auto secondOutput = group(11, ReservationEdge::Bottom);
    static_cast<void>(ledger.updateContribution(member(1), firstOutput, depth(80)));
    static_cast<void>(ledger.updateContribution(member(2), secondOutput, depth(32)));

    const auto firstState = ledger.describeGroup(firstOutput);
    const auto secondState = ledger.describeGroup(secondOutput);

    QVERIFY(firstState);
    QVERIFY(secondState);
    QCOMPARE(firstState->maximumDepth.pixels(), 80);
    QCOMPARE(secondState->maximumDepth.pixels(), 32);
    QCOMPARE(ledger.groupCount(), std::size_t{2});
}

void ScreenSpaceReservationLedgerTest::reportsCanonicalContributorsAndGroups()
{
    ScreenSpaceReservationLedger ledger;
    const auto bottom = group(10, ReservationEdge::Bottom);
    const auto right = group(11, ReservationEdge::Right);
    static_cast<void>(
        ledger.updateContribution(member(9), bottom, depth(64)));
    static_cast<void>(
        ledger.updateContribution(member(3), bottom, depth(40)));
    static_cast<void>(
        ledger.updateContribution(member(7), right, depth(52)));

    QCOMPARE(ledger.groups(), (std::vector{bottom, right}));
    const auto state = ledger.describeGroup(bottom);
    QVERIFY(state);
    QCOMPARE(state->contributions.size(), std::size_t{2});
    QCOMPARE(state->contributions.at(0).member, member(3));
    QCOMPARE(state->contributions.at(0).depth, depth(40));
    QCOMPARE(state->contributions.at(1).member, member(9));
    QCOMPARE(state->contributions.at(1).depth, depth(64));
}

void ScreenSpaceReservationLedgerTest::keepsCommittedStateWhenCandidateIsDiscarded()
{
    ScreenSpaceReservationLedger committed;
    const auto bottom = group(10, ReservationEdge::Bottom);
    const auto secondaryTop = group(11, ReservationEdge::Top);
    static_cast<void>(
        committed.updateContribution(
            member(4),
            bottom,
            depth(48)));

    ScreenSpaceReservationLedger candidate = committed;
    static_cast<void>(
        candidate.updateContribution(
            member(4),
            secondaryTop,
            depth(72)));

    QCOMPARE(
        committed.findGroup(member(4)),
        std::optional{bottom});
    QCOMPARE(
        committed.findContributionDepth(member(4)),
        std::optional{depth(48)});
    QVERIFY(committed.describeGroup(bottom));
    QVERIFY(!committed.describeGroup(secondaryTop));

    QCOMPARE(
        candidate.findGroup(member(4)),
        std::optional{secondaryTop});
    QVERIFY(!candidate.describeGroup(bottom));
    QVERIFY(candidate.describeGroup(secondaryTop));
}

QTEST_GUILESS_MAIN(ScreenSpaceReservationLedgerTest)

#include "screenspacereservationledgertest.moc"
