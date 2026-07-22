/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../../app/settings/placementstate.h"
#include "../../containment/plugin/placementnormalizer.h"
#include <coretypes.h>

// Qt
#include <QTest>

// Plasma
#include <Plasma/Plasma>

// C++
#include <array>

using namespace Latte::Settings::PlacementState;

class PlacementStateTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void semanticAlignment_translatesAcrossEveryEdge();
    void normalizationMatrix_keepsSolvedExtentInsideOutput();
    void centerToEdgeTransition_removesNegativeOffset();
    void normalization_isIdempotent();
    void qmlBoundary_translatesAndNormalizesAtomically();
    void qmlBoundary_refusesUnknownEnums();
};

void PlacementStateTest::semanticAlignment_translatesAcrossEveryEdge()
{
    QCOMPARE(physicalAlignmentFor(OutputEdge::Top, Alignment::Start), PhysicalAlignment::Left);
    QCOMPARE(physicalAlignmentFor(OutputEdge::Bottom, Alignment::End), PhysicalAlignment::Right);
    QCOMPARE(physicalAlignmentFor(OutputEdge::Left, Alignment::Start), PhysicalAlignment::Top);
    QCOMPARE(physicalAlignmentFor(OutputEdge::Right, Alignment::End), PhysicalAlignment::Bottom);

    constexpr std::array physicalAlignments{
        PhysicalAlignment::Left,
        PhysicalAlignment::Right,
        PhysicalAlignment::Top,
        PhysicalAlignment::Bottom,
        PhysicalAlignment::Center,
        PhysicalAlignment::Justify,
    };
    for (const PhysicalAlignment physical : physicalAlignments) {
        const Alignment semantic = semanticAlignmentOf(physical);
        for (const OutputEdge edge : {OutputEdge::Top, OutputEdge::Right,
                                      OutputEdge::Bottom, OutputEdge::Left}) {
            QCOMPARE(semanticAlignmentOf(physicalAlignmentFor(edge, semantic)), semantic);
        }
    }
}

void PlacementStateTest::normalizationMatrix_keepsSolvedExtentInsideOutput()
{
    constexpr std::array edges{OutputEdge::Top, OutputEdge::Right,
                               OutputEdge::Bottom, OutputEdge::Left};
    constexpr std::array alignments{Alignment::Start, Alignment::Center, Alignment::End};
    constexpr std::array offsets{-140.0, -17.5, 0.0, 13.25, 140.0};
    constexpr std::array maximums{-5.0, 1.0, 37.5, 80.0, 100.0, 140.0};
    constexpr OutputGeometry output{137.0, -911.0, 2560.0, 1440.0};

    for (const OutputEdge edge : edges) {
        for (const Alignment alignment : alignments) {
            for (const double offset : offsets) {
                for (const double maximum : maximums) {
                    const auto placement = normalize({edge, alignment, 30.0, maximum, offset});
                    const PrimaryExtent extent = solvePrimaryExtent(placement, output);

                    QVERIFY2(liesWithinOutput(extent, edge, output),
                             "normalized placement escaped the output primary axis");
                    QVERIFY(placement.minLengthPercent() >= 0.0);
                    QVERIFY(placement.maxLengthPercent() >= placement.minLengthPercent());
                    QVERIFY(placement.maxLengthPercent() <= 100.0);

                    if (alignment == Alignment::Center) {
                        const double bound = (100.0 - placement.maxLengthPercent()) / 2.0;
                        QVERIFY(placement.offsetPercent() >= -bound);
                        QVERIFY(placement.offsetPercent() <= bound);
                    } else {
                        QVERIFY(placement.offsetPercent() >= 0.0);
                        QVERIFY(placement.offsetPercent()
                                <= 100.0 - placement.maxLengthPercent());
                    }
                }
            }
        }
    }
}

void PlacementStateTest::centerToEdgeTransition_removesNegativeOffset()
{
    const auto centered = normalize({OutputEdge::Bottom, Alignment::Center,
                                     20.0, 60.0, -17.0});
    QCOMPARE(centered.offsetPercent(), -17.0);

    for (const Alignment edgeAlignment : {Alignment::Start, Alignment::End}) {
        const auto transitioned = normalize({OutputEdge::Bottom, edgeAlignment,
                                             centered.minLengthPercent(),
                                             centered.maxLengthPercent(),
                                             centered.offsetPercent()});
        QCOMPARE(transitioned.offsetPercent(), 0.0);
        QVERIFY(liesWithinOutput(solvePrimaryExtent(transitioned, {800.0, 240.0, 1920.0, 1080.0}),
                                 transitioned.edge(), {800.0, 240.0, 1920.0, 1080.0}));
    }
}

void PlacementStateTest::normalization_isIdempotent()
{
    constexpr std::array edges{OutputEdge::Top, OutputEdge::Right,
                               OutputEdge::Bottom, OutputEdge::Left};
    constexpr std::array alignments{Alignment::Start, Alignment::Center,
                                    Alignment::End, Alignment::Justify};

    for (const OutputEdge edge : edges) {
        for (const Alignment alignment : alignments) {
            for (const double offset : {-200.0, -10.0, 0.0, 10.0, 200.0}) {
                const auto once = normalize({edge, alignment, 125.0, -30.0, offset});
                const auto twice = normalize(requestedFrom(once));
                QCOMPARE(twice, once);
                QVERIFY(liesWithinOutput(solvePrimaryExtent(twice, {-3840.0, 775.0, 3840.0, 2160.0}),
                                         edge, {-3840.0, 775.0, 3840.0, 2160.0}));
            }
        }
    }
}

void PlacementStateTest::qmlBoundary_translatesAndNormalizesAtomically()
{
    Latte::Containment::PlacementNormalizer normalizer;

    const QVariantMap horizontal = normalizer.normalize(Plasma::Types::BottomEdge,
                                                        Latte::Types::Top,
                                                        20.0, 60.0, -17.0);
    QCOMPARE(horizontal.value(QStringLiteral("accepted")).toBool(), true);
    QCOMPARE(horizontal.value(QStringLiteral("alignment")).toInt(),
             static_cast<int>(Latte::Types::Left));
    QCOMPARE(horizontal.value(QStringLiteral("offset")).toDouble(), 0.0);

    const QVariantMap vertical = normalizer.normalize(Plasma::Types::LeftEdge,
                                                      Latte::Types::Right,
                                                      20.0, 60.0, 13.0);
    QCOMPARE(vertical.value(QStringLiteral("accepted")).toBool(), true);
    QCOMPARE(vertical.value(QStringLiteral("alignment")).toInt(),
             static_cast<int>(Latte::Types::Bottom));
    QCOMPARE(vertical.value(QStringLiteral("offset")).toDouble(), 13.0);

    const QVariantMap centered = normalizer.normalize(Plasma::Types::RightEdge,
                                                      Latte::Types::Center,
                                                      20.0, 60.0, -17.0);
    QCOMPARE(centered.value(QStringLiteral("alignment")).toInt(),
             static_cast<int>(Latte::Types::Center));
    QCOMPARE(centered.value(QStringLiteral("offset")).toDouble(), -17.0);
}

void PlacementStateTest::qmlBoundary_refusesUnknownEnums()
{
    Latte::Containment::PlacementNormalizer normalizer;

    QTest::ignoreMessage(QtCriticalMsg,
                         "PlacementNormalizer.normalize: refused invalid placement, location 0 alignment 0 min/max/offset 20 60 0");
    const QVariantMap floating = normalizer.normalize(Plasma::Types::Floating,
                                                      Latte::Types::Center,
                                                      20.0, 60.0, 0.0);
    QCOMPARE(floating.value(QStringLiteral("accepted")).toBool(), false);

    QTest::ignoreMessage(QtCriticalMsg,
                         "PlacementNormalizer.normalize: refused invalid placement, location 4 alignment -1 min/max/offset 20 60 0");
    const QVariantMap none = normalizer.normalize(Plasma::Types::BottomEdge,
                                                  Latte::Types::NoneAlignment,
                                                  20.0, 60.0, 0.0);
    QCOMPARE(none.value(QStringLiteral("accepted")).toBool(), false);
}

QTEST_GUILESS_MAIN(PlacementStateTest)
#include "placementstatetest.moc"
