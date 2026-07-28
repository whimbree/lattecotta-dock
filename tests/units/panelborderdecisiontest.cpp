/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../../app/view/panelborderdecision.h"

#include <QtTest>

#include <array>
#include <limits>

using namespace Latte::ViewPart;

class PanelBorderDecisionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void everyFloatingAlignmentKeepsAllBorders();
    void attachedAndDegeneratePanelsKeepBoundaryClipping();
    void attachedFullSpanOverridesAllCornersAtOutputBoundaries();
};

void PanelBorderDecisionTest::everyFloatingAlignmentKeepsAllBorders()
{
    constexpr std::array edges{
        FloatingPanelGeometry::Edge::Top,
        FloatingPanelGeometry::Edge::Right,
        FloatingPanelGeometry::Edge::Bottom,
        FloatingPanelGeometry::Edge::Left,
    };
    constexpr std::array alignments{
        PanelBorderDecision::Alignment::Start,
        PanelBorderDecision::Alignment::Center,
        PanelBorderDecision::Alignment::End,
        PanelBorderDecision::Alignment::Justify,
    };

    for (const auto edge : edges) {
        const auto solution =
            FloatingPanelGeometry::solve({
                .outputGeometry = QRect(0, 0, 1920, 1080),
                .edge = edge,
                .primaryAxisSpan =
                    FloatingPanelGeometry::isHorizontal(edge)
                    ? FloatingPanelGeometry::StablePrimaryAxisSpan{
                          320, 1280}
                    : FloatingPanelGeometry::StablePrimaryAxisSpan{
                          160, 760},
                .panelDepth = 48,
                .floatingGap = 11,
            });
        QVERIFY(solution.has_value());
        for (const auto alignment : alignments) {
            for (const qreal progress : {
                     std::numeric_limits<qreal>::denorm_min(),
                     0.25,
                     0.5,
                     1.0,
                 }) {
                for (const qreal maxLength : {0.5, 1.0}) {
                    for (const bool forceStart : {false, true}) {
                        for (const bool forceEnd : {false, true}) {
                            const PanelBorderDecision::Inputs inputs{
                                .edge = edge,
                                .alignment = alignment,
                                .configuredFloatingPresentation = true,
                                .screenEdgeBorderVisible =
                                    solution->screenEdgeBorderVisible(
                                        progress),
                                .floatingCornersVisible =
                                    solution->floatingCornersVisible(
                                        progress),
                                .screenEdgeMarginEnabled = false,
                                .backgroundAllCorners = false,
                                .forcePrimaryStartBorder = forceStart,
                                .forcePrimaryEndBorder = forceEnd,
                                .maxLength = maxLength,
                                .offset = 0.0,
                            };
                            QCOMPARE(
                                PanelBorderDecision::enabledBorders(
                                    inputs),
                                KSvg::FrameSvg::AllBorders);
                        }
                    }
                }
            }
        }
    }
}

void PanelBorderDecisionTest::attachedAndDegeneratePanelsKeepBoundaryClipping()
{
    constexpr std::array attachedEdges{
        std::pair{FloatingPanelGeometry::Edge::Top,
                  KSvg::FrameSvg::TopBorder},
        std::pair{FloatingPanelGeometry::Edge::Right,
                  KSvg::FrameSvg::RightBorder},
        std::pair{FloatingPanelGeometry::Edge::Bottom,
                  KSvg::FrameSvg::BottomBorder},
        std::pair{FloatingPanelGeometry::Edge::Left,
                  KSvg::FrameSvg::LeftBorder},
    };

    for (const auto &[edge, physicalBorder] : attachedEdges) {
        const PanelBorderDecision::Inputs attached{
            .edge = edge,
            .alignment = PanelBorderDecision::Alignment::Center,
            .configuredFloatingPresentation = true,
            .screenEdgeBorderVisible = false,
            .floatingCornersVisible = false,
            .screenEdgeMarginEnabled = true,
            .backgroundAllCorners = false,
            .maxLength = 0.5,
        };
        const auto attachedBorders =
            PanelBorderDecision::enabledBorders(attached);
        QVERIFY(!(attachedBorders & physicalBorder));
        QCOMPARE(attachedBorders | physicalBorder,
                 KSvg::FrameSvg::AllBorders);
    }

    PanelBorderDecision::Inputs attached{
        .edge = FloatingPanelGeometry::Edge::Bottom,
        .alignment = PanelBorderDecision::Alignment::Justify,
        .configuredFloatingPresentation = true,
        .screenEdgeBorderVisible = false,
        .floatingCornersVisible = false,
        .screenEdgeMarginEnabled = true,
        .backgroundAllCorners = false,
        .maxLength = 1.0,
    };
    const auto attachedBorders =
        PanelBorderDecision::enabledBorders(attached);
    QVERIFY(!(attachedBorders & KSvg::FrameSvg::BottomBorder));
    QVERIFY(!(attachedBorders & KSvg::FrameSvg::LeftBorder));
    QVERIFY(!(attachedBorders & KSvg::FrameSvg::RightBorder));
    QVERIFY(attachedBorders & KSvg::FrameSvg::TopBorder);

    // A flush gap=0 panel rests at controller progress 1. Configuration is
    // the boundary guard that prevents raw progress from inventing corners.
    attached.configuredFloatingPresentation = false;
    attached.screenEdgeMarginEnabled = false;
    attached.screenEdgeBorderVisible = true;
    attached.floatingCornersVisible = true;
    QCOMPARE(PanelBorderDecision::enabledBorders(attached), attachedBorders);

    attached.configuredFloatingPresentation = true;
    attached.floatingCornersVisible = true;
    QCOMPARE(PanelBorderDecision::enabledBorders(attached),
             KSvg::FrameSvg::AllBorders);
}

void PanelBorderDecisionTest::
    attachedFullSpanOverridesAllCornersAtOutputBoundaries()
{
    constexpr std::array attachedEdges{
        std::pair{
            FloatingPanelGeometry::Edge::Top,
            KSvg::FrameSvg::EnabledBorders{
                KSvg::FrameSvg::BottomBorder}},
        std::pair{
            FloatingPanelGeometry::Edge::Right,
            KSvg::FrameSvg::EnabledBorders{
                KSvg::FrameSvg::LeftBorder}},
        std::pair{
            FloatingPanelGeometry::Edge::Bottom,
            KSvg::FrameSvg::EnabledBorders{
                KSvg::FrameSvg::TopBorder}},
        std::pair{
            FloatingPanelGeometry::Edge::Left,
            KSvg::FrameSvg::EnabledBorders{
                KSvg::FrameSvg::RightBorder}},
    };

    for (const auto &[edge, expectedBorders] : attachedEdges) {
        const PanelBorderDecision::Inputs attached{
            .edge = edge,
            .alignment = PanelBorderDecision::Alignment::Justify,
            .configuredFloatingPresentation = true,
            .screenEdgeBorderVisible = false,
            .floatingCornersVisible = false,
            .screenEdgeMarginEnabled = true,
            .backgroundAllCorners = true,
            .maxLength = 1.0,
        };
        QCOMPARE(
            PanelBorderDecision::enabledBorders(attached),
            expectedBorders);

        auto partial = attached;
        partial.alignment = PanelBorderDecision::Alignment::Center;
        partial.maxLength = 0.6;
        const auto partialBorders =
            PanelBorderDecision::enabledBorders(partial);
        const auto physicalBorder = [edge]() {
            switch (edge) {
            case FloatingPanelGeometry::Edge::Top:
                return KSvg::FrameSvg::TopBorder;
            case FloatingPanelGeometry::Edge::Right:
                return KSvg::FrameSvg::RightBorder;
            case FloatingPanelGeometry::Edge::Bottom:
                return KSvg::FrameSvg::BottomBorder;
            case FloatingPanelGeometry::Edge::Left:
                return KSvg::FrameSvg::LeftBorder;
            }

            Q_UNREACHABLE_RETURN(KSvg::FrameSvg::NoBorder);
        }();
        QCOMPARE(
            partialBorders | physicalBorder,
            KSvg::FrameSvg::AllBorders);
    }
}

QTEST_MAIN(PanelBorderDecisionTest)

#include "panelborderdecisiontest.moc"
