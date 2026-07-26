/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../../app/view/placementrequeststate.h"

#include <QtTest>

namespace {

using Latte::ViewPart::PlacementIntent;
using Latte::ViewPart::PlacementPatch;
using Latte::ViewPart::PlacementRequestState;

constexpr int SingleScreen{0};
constexpr int AllScreens{1};
constexpr int AllSecondaryScreens{2};
constexpr int BottomEdge{4};
constexpr int TopEdge{3};
constexpr int CenterAlignment{0};
constexpr int StartAlignment{1};
constexpr int EndAlignment{2};

[[nodiscard]] PlacementIntent appliedPlacement()
{
    return {
        "Layout A",
        SingleScreen,
        "primary",
        "DP-1",
        true,
        BottomEdge,
        CenterAlignment,
    };
}

class PlacementRequestStateTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void returnToCommittedPlacementSupersedesPendingTarget();
    void independentPatchesComposeOnLatestCompleteTarget();
    void followPrimaryPolicyCanReturnOnSamePhysicalOutput();
    void screenGroupAndDerivedOutputReturnAtomically();
    void staleCompletionCannotConsumeLatestIntent();
    void cancellationRestoresCapturedCompleteIntent();
    void identicalTargetIsNoOp();
};

void PlacementRequestStateTest::
returnToCommittedPlacementSupersedesPendingTarget()
{
    PlacementRequestState state;
    const PlacementIntent applied =
        appliedPlacement();

    PlacementPatch away;
    away.edge = TopEdge;
    away.alignment = StartAlignment;
    const auto first = state.submit(applied, away);
    QVERIFY(first.accepted);
    QCOMPARE(first.request.token, 1);

    PlacementPatch home;
    home.edge = BottomEdge;
    home.alignment = CenterAlignment;
    const auto second = state.submit(applied, home);
    QVERIFY(second.accepted);
    QCOMPARE(second.request.token, 2);
    QVERIFY(second.request.intent == applied);
    QVERIFY(!state.isCurrent(first.request.token));
    QVERIFY(!state.completeIfCurrent(first.request.token));
    QVERIFY(state.completeIfCurrent(second.request.token));
}

void PlacementRequestStateTest::
independentPatchesComposeOnLatestCompleteTarget()
{
    PlacementRequestState state;
    const PlacementIntent applied =
        appliedPlacement();

    PlacementPatch output;
    output.logicalOutputName = "HDMI-A-1";
    output.resolvedOutputName = "HDMI-A-1";
    output.followsPrimary = false;
    const auto first = state.submit(applied, output);

    PlacementPatch edge;
    edge.edge = TopEdge;
    const auto second = state.submit(applied, edge);

    PlacementPatch alignment;
    alignment.alignment = EndAlignment;
    const auto third = state.submit(applied, alignment);

    QVERIFY(first.accepted);
    QVERIFY(second.accepted);
    QVERIFY(third.accepted);
    QCOMPARE(third.request.token, 3);
    QVERIFY(
        third.request.intent.resolvedOutputName
        == "HDMI-A-1");
    QCOMPARE(third.request.intent.edge, TopEdge);
    QCOMPARE(
        third.request.intent.alignment,
        EndAlignment);
}

void PlacementRequestStateTest::
followPrimaryPolicyCanReturnOnSamePhysicalOutput()
{
    PlacementRequestState state;
    const PlacementIntent applied =
        appliedPlacement();

    PlacementPatch explicitOutput;
    explicitOutput.logicalOutputName = "DP-1";
    explicitOutput.resolvedOutputName = "DP-1";
    explicitOutput.followsPrimary = false;
    const auto first =
        state.submit(applied, explicitOutput);
    QVERIFY(first.accepted);

    PlacementPatch followPrimary;
    followPrimary.logicalOutputName = "primary";
    followPrimary.resolvedOutputName = "DP-1";
    followPrimary.followsPrimary = true;
    const auto second =
        state.submit(applied, followPrimary);
    QVERIFY(second.accepted);
    QCOMPARE(second.request.token, 2);
    QVERIFY(second.request.intent == applied);
}

void PlacementRequestStateTest::
screenGroupAndDerivedOutputReturnAtomically()
{
    PlacementRequestState state;
    const PlacementIntent applied =
        appliedPlacement();

    PlacementPatch secondaryGroup;
    secondaryGroup.screensGroup =
        AllSecondaryScreens;
    secondaryGroup.logicalOutputName = "HDMI-A-1";
    secondaryGroup.resolvedOutputName = "HDMI-A-1";
    secondaryGroup.followsPrimary = false;
    const auto first =
        state.submit(applied, secondaryGroup);
    QVERIFY(first.accepted);

    PlacementPatch allScreens;
    allScreens.screensGroup = AllScreens;
    allScreens.logicalOutputName = "primary";
    allScreens.resolvedOutputName = "DP-1";
    allScreens.followsPrimary = true;
    const auto second =
        state.submit(applied, allScreens);
    QVERIFY(second.accepted);

    PlacementPatch singlePrimary;
    singlePrimary.screensGroup = SingleScreen;
    singlePrimary.logicalOutputName = "primary";
    singlePrimary.resolvedOutputName = "DP-1";
    singlePrimary.followsPrimary = true;
    const auto third =
        state.submit(applied, singlePrimary);
    QVERIFY(third.accepted);
    QVERIFY(third.request.intent == applied);
}

void PlacementRequestStateTest::
staleCompletionCannotConsumeLatestIntent()
{
    PlacementRequestState state;
    const PlacementIntent applied =
        appliedPlacement();

    PlacementPatch firstPatch;
    firstPatch.edge = TopEdge;
    const auto first =
        state.submit(applied, firstPatch);

    PlacementPatch secondPatch;
    secondPatch.alignment = StartAlignment;
    const auto second =
        state.submit(applied, secondPatch);

    QVERIFY(!state.completeIfCurrent(
        first.request.token));
    QVERIFY(state.pending().has_value());
    QVERIFY(
        state.pending()->intent
        == second.request.intent);
}

void PlacementRequestStateTest::
cancellationRestoresCapturedCompleteIntent()
{
    PlacementRequestState state;
    const PlacementIntent capturedPrior =
        appliedPlacement();

    PlacementPatch target;
    target.layoutName = "Layout B";
    target.screensGroup = AllSecondaryScreens;
    target.logicalOutputName = "HDMI-A-1";
    target.resolvedOutputName = "HDMI-A-1";
    target.followsPrimary = false;
    target.edge = TopEdge;
    target.alignment = EndAlignment;
    const auto request =
        state.submit(capturedPrior, target);
    QVERIFY(request.accepted);

    PlacementIntent partiallyApplied =
        request.request.intent;
    partiallyApplied.resolvedOutputName = "DP-1";
    partiallyApplied.edge = BottomEdge;
    QVERIFY(partiallyApplied != capturedPrior);

    QVERIFY(state.cancelToCommittedIfCurrent(
        request.request.token,
        capturedPrior));
    QVERIFY(state.pending().has_value());
    QVERIFY(
        state.pending()->intent
        == capturedPrior);
    QVERIFY(
        state.pending()->intent
        != partiallyApplied);
}

void PlacementRequestStateTest::
identicalTargetIsNoOp()
{
    PlacementRequestState state;
    const PlacementIntent applied =
        appliedPlacement();

    const auto first =
        state.submit(applied, PlacementPatch{});
    QVERIFY(!first.accepted);
    QCOMPARE(first.request.token, 0);
    QVERIFY(!state.pending().has_value());

    PlacementPatch away;
    away.edge = TopEdge;
    const auto second = state.submit(applied, away);
    const auto repeated = state.submit(applied, away);
    QVERIFY(second.accepted);
    QVERIFY(!repeated.accepted);
    QCOMPARE(
        repeated.request.token,
        second.request.token);
}

}

QTEST_MAIN(PlacementRequestStateTest)

#include "placementrequeststatetest.moc"
