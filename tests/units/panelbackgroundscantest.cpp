/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Ported from David Goree's latte-dock-qt6 tests/panelbackgroundscantest.cpp
// at 81384003, github.com/CaptSilver/latte-dock-qt6 (15a317ff; EX-25 in
// docs/QML_EXTRACTION_PLAN.md): their 17 slots
// re-derived against our extracted bodies - every expected value below
// carries its hand-walk in a comment instead of Goree's instrument-first
// pins. One deliberate fixture divergence: Goree wrote straight-alpha
// pixel values into premultiplied images (invalid premultiplied data),
// so their color-pick slot asserted an RGB the live scan never sees.
// Our fixtures premultiply (setPixelPremultiplied), matching what KSvg
// hands the real scan, and the color-pick expectation is the
// premultiplied color - the honest Qt5 behavior. The three
// *_emptyImage_* slots are ours, pinning the loud-refusal contract.

#include "../../app/plasma/extended/panelbackgroundscan.h"

#include <QImage>
#include <QtTest>

using namespace Latte::PlasmaExtended;
using PanelBackgroundScan::BandOrientation;
using PanelBackgroundScan::Corner;

class PanelBackgroundScanTest : public QObject
{
    Q_OBJECT

private:
    //! Fully transparent premultiplied-ARGB image, the base of every fixture.
    static QImage makeTransparentImage(int w, int h)
    {
        QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        return img;
    }

    //! Writes one pixel with correctly premultiplied components, the way the
    //! data arrives from KSvg in the live scan.
    static void setPixelPremultiplied(QImage &img, int x, int y, int alpha, QRgb rgb = qRgb(0, 0, 0))
    {
        img.setPixel(x, y, qPremultiply(qRgba(qRed(rgb), qGreen(rgb), qBlue(rgb), alpha)));
    }

private Q_SLOTS:
    // ---- measureMaxOpacity ----
    void maxOpacity_fullyOpaqueCenter_returnsOne();
    void maxOpacity_fullyTransparent_clampsToFloor();
    void maxOpacity_halfAlpha_onlyFirstTwoRowsCount();
    void maxOpacity_singleRowTall_averagesTheOneRow();
    void maxOpacity_exactlyTwoRowsTall_scansBoth();
    void maxOpacity_emptyImage_refusesWithFloor();

    // ---- measureRoundnessFromMaskCorner ----
    void maskRoundness_roundedPointOpaque_returnsZero();
    void maskRoundness_squareCorner_returnsZero();
    void maskRoundness_steppedCorner_returnsLineCount();
    void maskRoundness_topLeft_mirrorsBottomRight();
    void maskRoundness_singleRowTall_noCrash();
    void maskRoundness_singleColumnWide_noCrash();
    void maskRoundness_onePixelImage_returnsZero();
    void maskRoundness_emptyImage_refusesWithZero();

    // ---- measureRoundnessFromShadowCorner ----
    void shadowRoundness_emptyShadow_returnsZero();
    void shadowRoundness_zigZagCollapsesToZero();
    void shadowRoundness_monotonicRamp_returnsLineCount();
    void shadowRoundness_singleRowTall_noCrash();
    void shadowRoundness_singleColumnWide_noCrash();
    void shadowRoundness_onePixelImage_returnsZero();
    void shadowRoundness_emptyImage_refusesWithZero();

    // ---- measureShadowBand ----
    void shadowBand_horizontalBand_sizeIsSpan();
    void shadowBand_verticalBand_sizeIsSpan();
    void shadowBand_color_picksMaxAlphaPixel();
    void shadowBand_noVisiblePixel_zeroSizeAndNoColor();
    void shadowBand_onePixelImage_sizeOneAndItsColor();
    void shadowBand_emptyImage_refusesWithNeutralBand();
};

// ---- measureMaxOpacity ----

void PanelBackgroundScanTest::maxOpacity_fullyOpaqueCenter_returnsOne()
{
    // 100x50 all alpha=255: every sampled term is exactly 1.0f -> average 1.0.
    QImage img = makeTransparentImage(100, 50);
    img.fill(qRgba(0, 0, 0, 255));

    QVERIFY(qFuzzyCompare(PanelBackgroundScan::measureMaxOpacity(img), 1.0f));
}

void PanelBackgroundScanTest::maxOpacity_fullyTransparent_clampsToFloor()
{
    // Raw average 0.0 -> clamped to the 0.01f floor (the border-only
    // theme case the floor exists for).
    QImage img = makeTransparentImage(100, 50);

    QVERIFY(qFuzzyCompare(PanelBackgroundScan::measureMaxOpacity(img), 0.01f));
}

void PanelBackgroundScanTest::maxOpacity_halfAlpha_onlyFirstTwoRowsCount()
{
    // Rows 0-1 alpha=128, rows 2+ opaque. Only rows 0..min(2,h)-1 are
    // sampled, so the expected average is 128/255 ~ 0.502; if the scan
    // read further rows the opaque ones would pull it far above that.
    // The fixture is straight-alpha ARGB32 on purpose: it drives the
    // ensurePremultiplied conversion path (alpha survives conversion).
    QImage img(100, 50, QImage::Format_ARGB32);
    img.fill(qRgba(0, 0, 0, 255));
    for (int r = 0; r < 2; ++r) {
        for (int c = 0; c < 100; ++c) {
            img.setPixel(c, r, qRgba(0, 0, 0, 128));
        }
    }

    const float result = PanelBackgroundScan::measureMaxOpacity(img);
    QVERIFY2(result > 0.50f && result < 0.505f,
             qPrintable(QStringLiteral("expected ~0.502, got %1").arg(static_cast<double>(result))));
}

void PanelBackgroundScanTest::maxOpacity_singleRowTall_averagesTheOneRow()
{
    // 100x1: min(2,h)=1, only row 0 is scanned - scanLine(1) would be past
    // the image. All alpha=255 -> exactly 1.0.
    QImage img = makeTransparentImage(100, 1);
    img.fill(qRgba(0, 0, 0, 255));

    QVERIFY(qFuzzyCompare(PanelBackgroundScan::measureMaxOpacity(img), 1.0f));
}

void PanelBackgroundScanTest::maxOpacity_exactlyTwoRowsTall_scansBoth()
{
    // 100x2, the min(2,h) boundary: row 0 opaque, row 1 transparent. BOTH
    // rows must count -> (100*1.0 + 100*0.0) / (2*100) = 0.5 exactly.
    // A scan that sampled only one row would read 1.0 or 0.0.
    QImage img = makeTransparentImage(100, 2);
    for (int c = 0; c < 100; ++c) {
        setPixelPremultiplied(img, c, 0, 255);
    }

    QVERIFY(qFuzzyCompare(PanelBackgroundScan::measureMaxOpacity(img), 0.5f));
}

void PanelBackgroundScanTest::maxOpacity_emptyImage_refusesWithFloor()
{
    // ignoreMessage FAILS the test if the qCritical does not appear, so this
    // pins the refusal being loud, not just the neutral result.
    QTest::ignoreMessage(QtCriticalMsg,
                         "PanelBackgroundScan: measureMaxOpacity received an empty image (missing or zero-sized theme element) - returning the neutral result");
    QVERIFY(qFuzzyCompare(PanelBackgroundScan::measureMaxOpacity(QImage()), 0.01f));
}

// ---- measureRoundnessFromMaskCorner ----

void PanelBackgroundScanTest::maskRoundness_roundedPointOpaque_returnsZero()
{
    // BottomRight corner: the "is it rounded?" probe is pixel (w-1,h-1).
    // Opaque there means no roundness at all -> 0 without any line walk.
    QImage img = makeTransparentImage(8, 8);
    setPixelPremultiplied(img, 7, 7, 200);

    QCOMPARE(PanelBackgroundScan::measureRoundnessFromMaskCorner(img, Corner::BottomRight), 0);
}

void PanelBackgroundScanTest::maskRoundness_squareCorner_returnsZero()
{
    // TopLeft corner, fully opaque image: probe pixel (0,0) is opaque,
    // so the square corner reports 0 immediately.
    QImage img = makeTransparentImage(8, 8);
    img.fill(qRgba(0, 0, 0, 255));

    QCOMPARE(PanelBackgroundScan::measureRoundnessFromMaskCorner(img, Corner::TopLeft), 0);
}

void PanelBackgroundScanTest::maskRoundness_steppedCorner_returnsLineCount()
{
    // BottomRight staircase, 8x8. Hand-walk of the extracted body:
    //   baseRow=0, baseCol=0; probe (7,7) transparent -> proceed.
    //   Row 0 cols 0..5 opaque, col 6 transparent -> baseLineLength=6.
    //   Head walk r=1..: col 0 opaque on rows 1..4 -> headLimitR=4;
    //     row 5 col 0 transparent -> stop.
    //   Tail walk at col baseLineLength-1=5: row 1 alpha=200 != 255
    //     -> tailLimitR=1.
    //   roundness = headLimitR - tailLimitR + 1 = 4.
    QImage img = makeTransparentImage(8, 8);
    for (int c = 0; c <= 5; ++c) {
        setPixelPremultiplied(img, c, 0, 255);
    }
    for (int r = 1; r <= 4; ++r) {
        setPixelPremultiplied(img, 0, r, 255);
        for (int c = 1; c <= 4; ++c) {
            setPixelPremultiplied(img, c, r, 128);
        }
        setPixelPremultiplied(img, 5, r, 200);
    }

    QCOMPARE(PanelBackgroundScan::measureRoundnessFromMaskCorner(img, Corner::BottomRight), 4);
}

void PanelBackgroundScanTest::maskRoundness_topLeft_mirrorsBottomRight()
{
    // The same staircase mirrored for the TopLeft branch, 8x8:
    //   baseRow=7, baseCol=7; probe (0,0) transparent -> proceed.
    //   Row 7 cols 2..7 opaque, col 1 transparent -> baseLineLength=6.
    //   Head walk r=6..: col 7 opaque on rows 6..3 -> headLimitR=3;
    //     row 2 col 7 transparent -> stop.
    //   Tail walk at col max(0, 8-6)=2: row 6 alpha=200 != 255
    //     -> tailLimitR=6.
    //   roundness = tailLimitR - headLimitR + 1 = 4, mirroring the
    //   BottomRight case above.
    QImage img = makeTransparentImage(8, 8);
    for (int c = 2; c <= 7; ++c) {
        setPixelPremultiplied(img, c, 7, 255);
    }
    for (int r = 3; r <= 6; ++r) {
        setPixelPremultiplied(img, 7, r, 255);
        for (int c = 3; c <= 6; ++c) {
            setPixelPremultiplied(img, c, r, 128);
        }
        setPixelPremultiplied(img, 2, r, 200);
    }

    QCOMPARE(PanelBackgroundScan::measureRoundnessFromMaskCorner(img, Corner::TopLeft), 4);
}

void PanelBackgroundScanTest::maskRoundness_singleRowTall_noCrash()
{
    // 8x1 runs the corner walks with zero rows above/below the base line.
    // Under ASan this slot is the tripwire for the 39c6517d bound fix: with
    // the Qt5 inclusive bound the BottomRight walk reads scanLine(1) of a
    // 1-row image (heap-buffer-overflow).
    //   BottomRight: probe (7,0) transparent; (0,0) opaque -> baseLineLength=1;
    //     both row walks cover r=1..<1 = nothing -> head==tail -> 0.
    //   TopLeft: probe (0,0) is opaque -> 0 immediately.
    QImage img = makeTransparentImage(8, 1);
    setPixelPremultiplied(img, 0, 0, 255);

    QCOMPARE(PanelBackgroundScan::measureRoundnessFromMaskCorner(img, Corner::BottomRight), 0);
    QCOMPARE(PanelBackgroundScan::measureRoundnessFromMaskCorner(img, Corner::TopLeft), 0);
}

void PanelBackgroundScanTest::maskRoundness_singleColumnWide_noCrash()
{
    // 1x8 exercises the column loops at their left/right edges (baseCol=0
    // for both corners). Only (0,7) opaque. Hand-walk:
    //   TopLeft: probe (0,0) transparent; basePoint (0,7) opaque ->
    //     baseLineLength=1; head walk breaks at once (row 6 transparent, so
    //     headLimitR stays 7); tail walk at col max(0,1-1)=0 finds row 6
    //     not-255 -> tailLimitR=6 -> roundness = 6-7+1 = 0.
    //   BottomRight: probe (w-1,h-1)=(0,7) is opaque -> 0 immediately.
    QImage img = makeTransparentImage(1, 8);
    setPixelPremultiplied(img, 0, 7, 255);

    QCOMPARE(PanelBackgroundScan::measureRoundnessFromMaskCorner(img, Corner::TopLeft), 0);
    QCOMPARE(PanelBackgroundScan::measureRoundnessFromMaskCorner(img, Corner::BottomRight), 0);
}

void PanelBackgroundScanTest::maskRoundness_onePixelImage_returnsZero()
{
    // 1x1: base point and rounded-probe point are the SAME pixel, so no
    // state can report roundness. Transparent -> base line never starts;
    // opaque -> the probe says "not rounded". Both corners, both states.
    QImage transparent = makeTransparentImage(1, 1);
    QCOMPARE(PanelBackgroundScan::measureRoundnessFromMaskCorner(transparent, Corner::TopLeft), 0);
    QCOMPARE(PanelBackgroundScan::measureRoundnessFromMaskCorner(transparent, Corner::BottomRight), 0);

    QImage opaque = makeTransparentImage(1, 1);
    setPixelPremultiplied(opaque, 0, 0, 255);
    QCOMPARE(PanelBackgroundScan::measureRoundnessFromMaskCorner(opaque, Corner::TopLeft), 0);
    QCOMPARE(PanelBackgroundScan::measureRoundnessFromMaskCorner(opaque, Corner::BottomRight), 0);
}

void PanelBackgroundScanTest::maskRoundness_emptyImage_refusesWithZero()
{
    // The live reachable case: a mask theme shipping mask-topleft without
    // mask-bottomright hands the scan a null image.
    QTest::ignoreMessage(QtCriticalMsg,
                         "PanelBackgroundScan: measureRoundnessFromMaskCorner received an empty image (missing or zero-sized theme element) - returning the neutral result");
    QCOMPARE(PanelBackgroundScan::measureRoundnessFromMaskCorner(QImage(), Corner::BottomRight), 0);
}

// ---- measureRoundnessFromShadowCorner ----

void PanelBackgroundScanTest::shadowRoundness_emptyShadow_returnsZero()
{
    // Fully transparent corner: basePoint alpha is 0 so the base-line scan
    // runs, but finds no opacity -> baseLineLength stays 0 -> 0.
    QImage img = makeTransparentImage(8, 8);

    QCOMPARE(PanelBackgroundScan::measureRoundnessFromShadowCorner(img, Corner::BottomRight), 0);
}

void PanelBackgroundScanTest::shadowRoundness_zigZagCollapsesToZero()
{
    // The 3.1 zig-zag reset (Air theme case), TopLeft branch where
    // baseLineLength stays fixed per row. Goree's fixture reset from an
    // already-zero count, so deleting the reset branch would not fail it;
    // this one accumulates two roundness lines FIRST, then wanders.
    // Hand-walk, 8x8 (rows are processed 6 down to 0):
    //   baseRow=7, baseCol=7; basePoint (7,7) transparent -> base scan:
    //   row 7 peak 200 at col 3 -> baseLineLength=7-3+1=5, ranged walk
    //   covers cols 7..3.
    //   Rows 6,5: first pixel (col 7) transparent; peak 200 at col 5
    //   (inside the range) reached after transPixels=2 != 5 ->
    //   roundness increments -> 2.
    //   Row 4 (the zig-zag): peak 200 at col 2, OUTSIDE the range, so
    //   every ranged pixel (alpha 0/50) differs from rowMaxOpacity ->
    //   transPixels=5 == baseLineLength -> roundness RESETS to 0.
    //   Row 3: first pixel (col 7) alpha=200 -> walk stops.
    // Without the reset branch the result would be 2.
    QImage img = makeTransparentImage(8, 8);
    setPixelPremultiplied(img, 6, 7, 50);
    setPixelPremultiplied(img, 5, 7, 50);
    setPixelPremultiplied(img, 4, 7, 50);
    setPixelPremultiplied(img, 3, 7, 200);

    for (int r = 5; r <= 6; ++r) {
        setPixelPremultiplied(img, 5, r, 200);
        setPixelPremultiplied(img, 4, r, 50);
        setPixelPremultiplied(img, 3, r, 50);
    }

    setPixelPremultiplied(img, 6, 4, 50);
    setPixelPremultiplied(img, 5, 4, 50);
    setPixelPremultiplied(img, 4, 4, 50);
    setPixelPremultiplied(img, 3, 4, 50);
    setPixelPremultiplied(img, 2, 4, 200);

    setPixelPremultiplied(img, 7, 3, 200);

    QCOMPARE(PanelBackgroundScan::measureRoundnessFromShadowCorner(img, Corner::TopLeft), 0);
}

void PanelBackgroundScanTest::shadowRoundness_monotonicRamp_returnsLineCount()
{
    // BottomRight branch. Hand-walk, 8x8:
    //   baseRow=0, baseCol=0; basePoint (0,0) transparent -> base scan:
    //   row 0 alpha 100 at col 1, peak 200 at col 2 -> baseLineLength=3.
    //   Rows 1..3: first pixel (col 0) transparent; per-row peak 200 at
    //   col 2 re-pins baseLineLength=3; the ranged walk cols 0..2 counts
    //   transPixels=2 (cols 0,1) then hits the peak with 2 != 3 ->
    //   roundness++ per row -> 3.
    //   Row 4: first pixel (col 0) opaque -> walk stops.
    QImage img = makeTransparentImage(8, 8);
    setPixelPremultiplied(img, 1, 0, 100);
    setPixelPremultiplied(img, 2, 0, 200);
    for (int r = 1; r <= 3; ++r) {
        setPixelPremultiplied(img, 1, r, 100);
        setPixelPremultiplied(img, 2, r, 200);
    }
    setPixelPremultiplied(img, 0, 4, 255);

    QCOMPARE(PanelBackgroundScan::measureRoundnessFromShadowCorner(img, Corner::BottomRight), 3);
}

void PanelBackgroundScanTest::shadowRoundness_singleRowTall_noCrash()
{
    // 8x1 fully transparent: both branches find no base-line opacity -> 0.
    // Like the mask twin, this is the ASan tripwire for the shadow walk's
    // exclusive bound (39c6517d).
    QImage img = makeTransparentImage(8, 1);

    QCOMPARE(PanelBackgroundScan::measureRoundnessFromShadowCorner(img, Corner::BottomRight), 0);
    QCOMPARE(PanelBackgroundScan::measureRoundnessFromShadowCorner(img, Corner::TopLeft), 0);
}

void PanelBackgroundScanTest::shadowRoundness_singleColumnWide_noCrash()
{
    // 1x8: base line scans cover exactly one column. A visible base point
    // means "no shadow corner here" (the algorithm expects transparency at
    // the corner), a transparent column finds no opacity peak; both -> 0.
    QImage img = makeTransparentImage(1, 8);
    setPixelPremultiplied(img, 0, 7, 255);

    QCOMPARE(PanelBackgroundScan::measureRoundnessFromShadowCorner(img, Corner::TopLeft), 0);
    QCOMPARE(PanelBackgroundScan::measureRoundnessFromShadowCorner(img, Corner::BottomRight), 0);
}

void PanelBackgroundScanTest::shadowRoundness_onePixelImage_returnsZero()
{
    // 1x1, both states: opaque -> base point visible, walk never starts;
    // transparent -> no opacity peak, baseLineLength stays 0. Both -> 0.
    QImage transparent = makeTransparentImage(1, 1);
    QCOMPARE(PanelBackgroundScan::measureRoundnessFromShadowCorner(transparent, Corner::TopLeft), 0);
    QCOMPARE(PanelBackgroundScan::measureRoundnessFromShadowCorner(transparent, Corner::BottomRight), 0);

    QImage opaque = makeTransparentImage(1, 1);
    setPixelPremultiplied(opaque, 0, 0, 255);
    QCOMPARE(PanelBackgroundScan::measureRoundnessFromShadowCorner(opaque, Corner::TopLeft), 0);
    QCOMPARE(PanelBackgroundScan::measureRoundnessFromShadowCorner(opaque, Corner::BottomRight), 0);
}

void PanelBackgroundScanTest::shadowRoundness_emptyImage_refusesWithZero()
{
    QTest::ignoreMessage(QtCriticalMsg,
                         "PanelBackgroundScan: measureRoundnessFromShadowCorner received an empty image (missing or zero-sized theme element) - returning the neutral result");
    QCOMPARE(PanelBackgroundScan::measureRoundnessFromShadowCorner(QImage(), Corner::TopLeft), 0);
}

// ---- measureShadowBand ----

void PanelBackgroundScanTest::shadowBand_horizontalBand_sizeIsSpan()
{
    // Horizontal: scan column 0 down all rows. Visible rows 3..9 inclusive
    // -> span 7.
    QImage img = makeTransparentImage(4, 15);
    for (int r = 3; r <= 9; ++r) {
        setPixelPremultiplied(img, 0, r, 128);
    }

    QCOMPARE(PanelBackgroundScan::measureShadowBand(img, BandOrientation::Horizontal).size, 7);
}

void PanelBackgroundScanTest::shadowBand_verticalBand_sizeIsSpan()
{
    // Vertical: scan row 0 across all columns. Visible cols 2..6 inclusive
    // -> span 5.
    QImage img = makeTransparentImage(10, 4);
    for (int c = 2; c <= 6; ++c) {
        setPixelPremultiplied(img, c, 0, 128);
    }

    QCOMPARE(PanelBackgroundScan::measureShadowBand(img, BandOrientation::Vertical).size, 5);
}

void PanelBackgroundScanTest::shadowBand_color_picksMaxAlphaPixel()
{
    // Three colored pixels; the alpha=200 red one wins. The expected
    // components are the PREMULTIPLIED red (255*200/255 = 200) because the
    // scan reads the premultiplied data as-is - the same color the live
    // code stores from a KSvg image (Qt5 behavior, pinned here).
    QImage img = makeTransparentImage(4, 4);
    setPixelPremultiplied(img, 1, 1, 100, qRgb(0, 255, 0));
    setPixelPremultiplied(img, 2, 2, 200, qRgb(255, 0, 0));
    setPixelPremultiplied(img, 3, 3, 50, qRgb(0, 0, 255));

    const auto band = PanelBackgroundScan::measureShadowBand(img, BandOrientation::Horizontal);
    QVERIFY2(band.color.has_value(), "color must be present when visible pixels exist");
    QCOMPARE(band.color->red(), 200);
    QCOMPARE(band.color->green(), 0);
    QCOMPARE(band.color->blue(), 0);
    QCOMPARE(band.color->alpha(), 200);
}

void PanelBackgroundScanTest::shadowBand_noVisiblePixel_zeroSizeAndNoColor()
{
    // Fully transparent border: size 0 and NO color - nullopt, not some
    // sentinel color value; the adapter keeps its previous color (Qt5
    // behavior).
    QImage img = makeTransparentImage(8, 8);

    const auto band = PanelBackgroundScan::measureShadowBand(img, BandOrientation::Horizontal);
    QCOMPARE(band.size, 0);
    QVERIFY2(!band.color.has_value(), "color must be nullopt when no visible pixel exists");
}

void PanelBackgroundScanTest::shadowBand_onePixelImage_sizeOneAndItsColor()
{
    // 1x1 opaque: the smallest visible band. Both orientations must report
    // size 1 and the pixel's color (first==last==0 -> span 1).
    QImage img = makeTransparentImage(1, 1);
    setPixelPremultiplied(img, 0, 0, 255, qRgb(10, 20, 30));

    for (auto orientation : {BandOrientation::Horizontal, BandOrientation::Vertical}) {
        const auto band = PanelBackgroundScan::measureShadowBand(img, orientation);
        QCOMPARE(band.size, 1);
        QVERIFY(band.color.has_value());
        QCOMPARE(band.color->red(), 10);
        QCOMPARE(band.color->green(), 20);
        QCOMPARE(band.color->blue(), 30);
        QCOMPARE(band.color->alpha(), 255);
    }
}

void PanelBackgroundScanTest::shadowBand_emptyImage_refusesWithNeutralBand()
{
    QTest::ignoreMessage(QtCriticalMsg,
                         "PanelBackgroundScan: measureShadowBand received an empty image (missing or zero-sized theme element) - returning the neutral result");
    const auto band = PanelBackgroundScan::measureShadowBand(QImage(), BandOrientation::Horizontal);
    QCOMPARE(band.size, 0);
    QVERIFY(!band.color.has_value());
}

QTEST_GUILESS_MAIN(PanelBackgroundScanTest)
#include "panelbackgroundscantest.moc"
