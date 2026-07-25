/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../../app/view/floatinganchorwindowfilter.h"
#include "../../app/view/floatingpopuppresentation.h"

#include <QDynamicPropertyChangeEvent>
#include <QWindow>

#include <QtTest>

#include <utility>

using namespace Latte::ViewPart;

Q_DECLARE_METATYPE(FloatingPanelGeometry::Edge)

class AnchorRevisionProbe : public QObject
{
public:
    int revisionCount{0};

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::DynamicPropertyChange
            && FloatingPopupPresentation::
                isAnchorRevisionProperty(
                    static_cast<QDynamicPropertyChangeEvent *>(
                        event)->propertyName())) {
            ++revisionCount;
        }
        return false;
    }
};

class HostMigratingAnchor : public QObject
{
    Q_OBJECT

public:
    QWindow *window() const
    {
        return m_window;
    }

    void moveToWindow(QWindow *window)
    {
        if (m_window == window) {
            return;
        }
        m_window = window;
        Q_EMIT windowChanged(window);
    }

Q_SIGNALS:
    void windowChanged(QWindow *window);

private:
    QWindow *m_window{nullptr};
};

class FloatingPopupPresentationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void anchorsEveryEdgeAtEveryEndpoint_data();
    void anchorsEveryEdgeAtEveryEndpoint();
    void preservesUnknownDisplayHintBitsAcrossReversal();
    void repositionsOnlyForAnchorRevisionChanges();
    void anchorMigrationDetachesOldWindowAndFollowsNewWindow();
    void rejectsInvalidAnchorInputs();
};

void FloatingPopupPresentationTest::
    anchorsEveryEdgeAtEveryEndpoint_data()
{
    QTest::addColumn<FloatingPanelGeometry::Edge>("edge");
    QTest::addColumn<qreal>("progress");

    for (const auto [edgeName, edge] : {
             std::pair{"top", FloatingPanelGeometry::Edge::Top},
             std::pair{"right", FloatingPanelGeometry::Edge::Right},
             std::pair{"bottom", FloatingPanelGeometry::Edge::Bottom},
             std::pair{"left", FloatingPanelGeometry::Edge::Left},
         }) {
        for (const auto [progressName, progress] : {
                 std::pair{"attached", 0.0},
                 std::pair{"half", 0.5},
                 std::pair{"floated", 1.0},
             }) {
            const QByteArray rowName =
                QByteArray(edgeName) + '-'
                + QByteArray(progressName);
            QTest::newRow(rowName.constData())
                << edge << progress;
        }
    }
}

void FloatingPopupPresentationTest::
    anchorsEveryEdgeAtEveryEndpoint()
{
    QFETCH(FloatingPanelGeometry::Edge, edge);
    QFETCH(qreal, progress);

    const FloatingPanelGeometry::Inputs inputs{
        .outputGeometry = QRect(125, -75, 1000, 800),
        .edge = edge,
        .primaryAxisSpan =
            FloatingPanelGeometry::isHorizontal(edge)
            ? FloatingPanelGeometry::StablePrimaryAxisSpan{234, 517}
            : FloatingPanelGeometry::StablePrimaryAxisSpan{41, 389},
        .panelDepth = 47,
        .floatingGap = 13,
    };
    const auto solution = FloatingPanelGeometry::solve(inputs);
    QVERIFY(solution.has_value());

    QRectF visibleGlobal =
        solution->visibleMask(progress).value;
    visibleGlobal.translate(913.25, -417.75);
    const QSize popupSize{211, 137};
    constexpr int popupMargin{9};
    const auto anchor =
        FloatingPopupPresentation::perpendicularAnchor(
            edge, visibleGlobal, popupSize, popupMargin);
    QVERIFY(anchor.has_value());

    switch (edge) {
    case FloatingPanelGeometry::Edge::Left:
        QCOMPARE(*anchor,
                 qCeil(visibleGlobal.right()) + popupMargin);
        break;
    case FloatingPanelGeometry::Edge::Right:
        QCOMPARE(*anchor,
                 qFloor(visibleGlobal.left())
                     - popupSize.width() - popupMargin);
        break;
    case FloatingPanelGeometry::Edge::Top:
        QCOMPARE(*anchor,
                 qCeil(visibleGlobal.bottom()) + popupMargin);
        break;
    case FloatingPanelGeometry::Edge::Bottom:
        QCOMPARE(*anchor,
                 qFloor(visibleGlobal.top())
                     - popupSize.height() - popupMargin);
        break;
    }
}

void FloatingPopupPresentationTest::
    preservesUnknownDisplayHintBitsAcrossReversal()
{
    constexpr quint32 unknownBits{0b1010'0000U};
    constexpr quint32 floatingBit{0b0000'0100U};
    quint32 hints = unknownBits;

    hints =
        FloatingPopupPresentation::
            displayHintsWithFloatingPreference(
                hints, floatingBit, true);
    QCOMPARE(hints, unknownBits | floatingBit);

    // Reversing toward attached clears the preference immediately from the
    // target decision, without waiting for transition progress to reach zero.
    hints =
        FloatingPopupPresentation::
            displayHintsWithFloatingPreference(
                hints, floatingBit, false);
    QCOMPARE(hints, unknownBits);

    hints =
        FloatingPopupPresentation::
            displayHintsWithFloatingPreference(
                hints, floatingBit, true);
    QCOMPARE(hints, unknownBits | floatingBit);
}

void FloatingPopupPresentationTest::
    repositionsOnlyForAnchorRevisionChanges()
{
    QVERIFY(FloatingPopupPresentation::isAnchorRevisionProperty(
        QByteArrayLiteral("_floating_anchor_revision")));
    QVERIFY(!FloatingPopupPresentation::isAnchorRevisionProperty(
        QByteArrayLiteral("_floating_visible_geometry")));
    QVERIFY(!FloatingPopupPresentation::isAnchorRevisionProperty(
        QByteArrayLiteral("_applets_popup_margin")));
}

void FloatingPopupPresentationTest::
    anchorMigrationDetachesOldWindowAndFollowsNewWindow()
{
    AnchorRevisionProbe probe;
    FloatingPopupPresentation::AnchorWindowEventFilter
        anchorFilter{probe};
    QWindow oldHost;
    QWindow newHost;
    HostMigratingAnchor anchor;

    anchor.moveToWindow(&oldHost);
    QVERIFY(anchorFilter.followWindow(anchor.window()));

    connect(&anchor,
            &HostMigratingAnchor::windowChanged,
            &probe,
            [&](QWindow *window) {
                (void)anchorFilter.followWindow(window);
            });

    oldHost.setProperty("_floating_anchor_revision", 1);
    QCOMPARE(probe.revisionCount, 1);

    QSignalSpy hostChanges{
        &anchor, &HostMigratingAnchor::windowChanged};
    anchor.moveToWindow(&newHost);
    QCOMPARE(anchor.window(), &newHost);
    QVERIFY(!hostChanges.isEmpty());
    QVERIFY(anchorFilter.observes(&newHost));
    QVERIFY(!anchorFilter.observes(&oldHost));

    oldHost.setProperty("_floating_anchor_revision", 2);
    QCOMPARE(probe.revisionCount, 1);

    newHost.setProperty("_floating_anchor_revision", 1);
    QCOMPARE(probe.revisionCount, 2);
}

void FloatingPopupPresentationTest::rejectsInvalidAnchorInputs()
{
    QVERIFY(!FloatingPopupPresentation::perpendicularAnchor(
        FloatingPanelGeometry::Edge::Bottom,
        QRectF{},
        QSize(200, 100),
        4));
    QVERIFY(!FloatingPopupPresentation::perpendicularAnchor(
        FloatingPanelGeometry::Edge::Bottom,
        QRectF(0, 0, 100, 40),
        QSize{},
        4));
    QVERIFY(!FloatingPopupPresentation::perpendicularAnchor(
        FloatingPanelGeometry::Edge::Bottom,
        QRectF(0, 0, 100, 40),
        QSize(200, 100),
        -1));
}

QTEST_MAIN(FloatingPopupPresentationTest)

#include "floatingpopuppresentationtest.moc"
