/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../../app/view/effectregion.h"

#include <QtTest>

using namespace Latte::ViewPart;

class EffectRegionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void translatesRoundedShapeIntoFractionalPaintRaster();
    void preservesAsymmetricCornersOnVerticalFractionalTranslation();
};

void EffectRegionTest::translatesRoundedShapeIntoFractionalPaintRaster()
{
    const QRectF visibleShape{0, 5.5, 800, 48};
    const QRect outwardPaintBounds{0, 5, 800, 49};
    QRegion roundedLocal{QRect(0, 0, 800, 48)};
    roundedLocal -= QRegion(QRect(0, 0, 3, 3));

    const QRegion translated =
        EffectRegion::rasterizedTranslatedShape(visibleShape, roundedLocal);

    QVERIFY(!translated.contains(QPoint(0, 5)));
    QVERIFY(!translated.contains(QPoint(2, 7)));
    QVERIFY(translated.contains(QPoint(3, 8)));
    QVERIFY(translated.contains(QPoint(400, 53)));
    QVERIFY(translated.contains(outwardPaintBounds.center()));
    QCOMPARE(translated.boundingRect(), outwardPaintBounds);

    const QRegion rectangular =
        EffectRegion::rasterizedTranslatedShape(visibleShape, {});
    QVERIFY(rectangular.contains(QPoint(0, 5)));
    QCOMPARE(rectangular.boundingRect(), outwardPaintBounds);

    QRegion bareTranslation = roundedLocal;
    bareTranslation.translate(0, 5);
    QVERIFY(!bareTranslation.contains(QPoint(400, 53)));
}

void EffectRegionTest::
    preservesAsymmetricCornersOnVerticalFractionalTranslation()
{
    const QRectF visibleShape{7.25, 0, 48, 500};
    const QRect outwardPaintBounds{7, 0, 49, 500};
    QRegion asymmetricLocal{QRect(0, 0, 48, 500)};
    asymmetricLocal -= QRegion(QRect(0, 0, 2, 5));
    asymmetricLocal -= QRegion(QRect(44, 0, 4, 3));
    asymmetricLocal -= QRegion(QRect(0, 494, 3, 6));
    asymmetricLocal -= QRegion(QRect(43, 496, 5, 4));

    const QRegion translated =
        EffectRegion::rasterizedTranslatedShape(
            visibleShape, asymmetricLocal);

    QVERIFY(!translated.contains(QPoint(7, 0)));
    QVERIFY(!translated.contains(QPoint(55, 0)));
    QVERIFY(!translated.contains(QPoint(7, 499)));
    QVERIFY(!translated.contains(QPoint(55, 499)));
    QVERIFY(translated.contains(QPoint(9, 5)));
    QVERIFY(translated.contains(QPoint(54, 3)));
    QVERIFY(translated.contains(QPoint(10, 493)));
    QVERIFY(translated.contains(QPoint(50, 495)));
    QCOMPARE(translated.boundingRect(), outwardPaintBounds);

    QRegion bareTranslation = asymmetricLocal;
    bareTranslation.translate(7, 0);
    QVERIFY(!bareTranslation.contains(QPoint(55, 250)));
    QVERIFY(translated.contains(QPoint(55, 250)));
}

QTEST_MAIN(EffectRegionTest)

#include "effectregiontest.moc"
