/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// EX-19 ColorLuminance (docs/QML_EXTRACTION_PLAN.md): equivalence tests for
// the brightness/luminance core against the JS implementation it replaces,
// plus the Tools QML-boundary contract (delegation and loud refusal of
// invalid colors).
//
// Reference-table generation method (per docs/TESTING.md): the rows in
// referenceTable_data were produced by driving the SHIPPED
// declarativeimports/components/code/ColorizerTools.js (byte-identical to
// the containment copy; the plasmoid copy differs only by a semicolon and
// by not carrying normalize()) through qmltestrunner offscreen at HEAD
// 1dcf542c - a scratch TestCase evaluated colorBrightness/colorLuminas
// over Qt.color(<hex>) inputs and printed color.r and both results at
// toPrecision(17). They were NOT derived from colortools.h. Comparisons
// are bit-exact (operator==, not QCOMPARE's fuzzy double path): the float
// channel quantization the header documents is part of the pinned
// behavior.
//
// Cross-reference to David Goree's latte-dock-qt6
// (github.com/CaptSilver/latte-dock-qt6): the case shapes
// black/white/weighted-primaries/mixed-158.677/overloads-agree/monotonic-
// greys are folded from his tests/commontoolstest.cpp +
// tests/coretoolstest.cpp at 81384003. This is idea-only reuse - his code
// tests the float implementation and is not adopted, and the expected
// values here are generated from our own ColorizerTools.js (above), so no
// David Goree copyright line is carried.

#include <QColor>
#include <QtTest>

#include "../../declarativeimports/core/tools.h"
#include "../../declarativeimports/core/units/colortools.h"

using namespace Latte;

class ColorToolsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void referenceTable_data();
    void referenceTable();
    void brightnessFromRGB_exactDoubleDomain();
    void luminaBranchBoundary();
    void luminaMonotonicOverGreys();
    void rgbEntryIsExactIntegerArithmetic();
    void colorAndRgbEntriesAgreeToQuantization();
    void isLight_thresholdSemantics();
    void toolsSingleton_delegatesToCore();
    void toolsSingleton_refusesInvalidColorLoudly();
};

//! bit-exact double comparison with a readable failure message; QCOMPARE
//! on doubles is deliberately avoided (it fuzzy-compares)
#define COMPARE_EXACT(actual, expected) \
    QVERIFY2((actual) == (expected), \
             qPrintable(QStringLiteral("%1 != %2") \
                        .arg((actual), 0, 'g', 17).arg((expected), 0, 'g', 17)))

void ColorToolsTest::referenceTable_data()
{
    QTest::addColumn<QString>("hex");
    QTest::addColumn<double>("channelR");     // QML color.r, pins redF() equivalence
    QTest::addColumn<double>("brightness");   // shipped JS colorBrightness()
    QTest::addColumn<double>("lumina");       // shipped JS colorLuminas()

    QTest::newRow("black") << "#000000" << 0.0 << 0.0 << 0.0;
    QTest::newRow("white") << "#ffffff" << 1.0 << 255.0 << 1.0;
    QTest::newRow("red") << "#ff0000" << 1.0 << 76.245000000000005 << 0.21260000000000001;
    QTest::newRow("green") << "#00ff00" << 0.0 << 149.68500000000000 << 0.71519999999999995;
    QTest::newRow("blue") << "#0000ff" << 0.0 << 29.070000000000000 << 0.07220000000000000;
    QTest::newRow("grey127") << "#7f7f7f" << 0.49803921580314636 << 127.00000002980232 << 0.21223075752169512;
    QTest::newRow("grey128") << "#808080" << 0.50196081399917603 << 128.00000756978989 << 0.21586052772625525;
    QTest::newRow("grey64") << "#404040" << 0.25098040699958801 << 64.000003784894943 << 0.051269464342884617;
    QTest::newRow("grey220") << "#dcdcdc" << 0.86274510622024536 << 220.00000208616257 << 0.71569351581820895;
    QTest::newRow("capt-mixed") << "#ff8040" << 1.0 << 158.67700487494469 << 0.37068510475537403;
    QTest::newRow("capt-purple") << "#7b2dc8" << 0.48235294222831726 << 85.992001109942791 << 0.10257856962031991;
    QTest::newRow("lumina-linear-branch") << "#0a0a0a" << 0.039215687662363052 << 10.000000353902578 << 0.003035269942907357;
    QTest::newRow("lumina-power-branch") << "#0b0b0b" << 0.043137256056070328 << 11.000000294297934 << 0.0033465358583530469;
    QTest::newRow("mixed-dark") << "#123456" << 0.070588238537311554 << 45.710000940375032 << 0.032564669951474536;
}

void ColorToolsTest::referenceTable()
{
    QFETCH(QString, hex);
    QFETCH(double, channelR);
    QFETCH(double, brightness);
    QFETCH(double, lumina);

    const QColor color(hex);
    QVERIFY(color.isValid());

    // the premise the header documents: C++ redF() IS QML color.r
    COMPARE_EXACT(static_cast<double>(color.redF()), channelR);

    COMPARE_EXACT(ColorTools::colorBrightness(color), brightness);
    COMPARE_EXACT(ColorTools::colorLumina(color), lumina);
}

void ColorToolsTest::brightnessFromRGB_exactDoubleDomain()
{
    // the AERT midpoint every light/dark split compares against is exactly
    // representable through the FromRGB entry (no color quantization)
    COMPARE_EXACT(ColorTools::colorBrightnessFromRGB(127.5, 127.5, 127.5), 127.5);
}

void ColorToolsTest::luminaBranchBoundary()
{
    // values from the shipped JS at the 0.03928 branch point (generation
    // method in the header comment): <= goes linear, above goes power -
    // and the inherited curve is DISCONTINUOUS there (the power value just
    // above the boundary is LOWER than the linear value at it)
    const double atBoundary = ColorTools::colorLuminaFromRGB(0.03928, 0.03928, 0.03928);
    const double justAbove = ColorTools::colorLuminaFromRGB(0.039281, 0.039281, 0.039281);

    COMPARE_EXACT(atBoundary, 0.003040247678018576);
    COMPARE_EXACT(justAbove, 0.0030395698603892978);
    QVERIFY(justAbove < atBoundary);
}

void ColorToolsTest::luminaMonotonicOverGreys()
{
    // Goree's latte-dock-qt6 case shape (81384003): luminance rises from
    // black through greys to white and stays inside [0, 1]
    const double dark = ColorTools::colorLumina(QColor(64, 64, 64));
    const double mid = ColorTools::colorLumina(QColor(128, 128, 128));
    const double light = ColorTools::colorLumina(QColor(220, 220, 220));

    QVERIFY(dark >= 0.0);
    QVERIFY(dark < mid);
    QVERIFY(mid < light);
    QVERIFY(light <= 1.0);
}

void ColorToolsTest::rgbEntryIsExactIntegerArithmetic()
{
    // the QRgb entry computes on integer channels: Goree's latte-dock-qt6
    // (81384003) hand-computed values hold exactly
    COMPARE_EXACT(ColorTools::colorBrightness(qRgb(255, 0, 0)), 76.245);
    COMPARE_EXACT(ColorTools::colorBrightness(qRgb(0, 255, 0)), 149.685);
    COMPARE_EXACT(ColorTools::colorBrightness(qRgb(0, 0, 255)), 29.07);
    COMPARE_EXACT(ColorTools::colorBrightness(qRgb(255, 128, 64)), 158.677);
    COMPARE_EXACT(ColorTools::colorBrightness(qRgb(128, 128, 128)), 128.0);
}

void ColorToolsTest::colorAndRgbEntriesAgreeToQuantization()
{
    // the two entries differ only by the float channel quantization the
    // header documents; on the 0..255 scale that is well under 1e-4
    const QRgb samples[] = {qRgb(0, 0, 0), qRgb(255, 255, 255), qRgb(128, 128, 128),
                            qRgb(255, 128, 64), qRgb(123, 45, 200), qRgb(18, 52, 86)};

    for (const QRgb rgb : samples) {
        const double viaColor = ColorTools::colorBrightness(QColor(rgb));
        const double viaRgb = ColorTools::colorBrightness(rgb);
        QVERIFY2(std::abs(viaColor - viaRgb) < 1e-4,
                 qPrintable(QStringLiteral("QColor %1 vs QRgb %2")
                            .arg(viaColor, 0, 'g', 17).arg(viaRgb, 0, 'g', 17)));
    }
}

void ColorToolsTest::isLight_thresholdSemantics()
{
    // the inherited split is strictly greater than 127.5: grey 127 sits
    // just above 127 (float quantization) but below the midpoint, grey 128
    // just above it
    QVERIFY(!ColorTools::isLight(QColor("#7f7f7f")));
    QVERIFY(ColorTools::isLight(QColor("#808080")));
    QVERIFY(!ColorTools::isLight(QColor(Qt::black)));
    QVERIFY(ColorTools::isLight(QColor(Qt::white)));

    // explicit threshold, and strictness pinned at an exactly-representable
    // brightness: white is 255.0 on the nose, NOT light against 255.0
    QVERIFY(!ColorTools::isLight(QColor("#808080"), 200.0));
    QVERIFY(ColorTools::isLight(QColor(Qt::white), 254.9));
    QVERIFY(!ColorTools::isLight(QColor(Qt::white), 255.0));
}

void ColorToolsTest::toolsSingleton_delegatesToCore()
{
    Tools tools;
    const QColor sample(255, 128, 64);

    COMPARE_EXACT(tools.colorBrightness(sample), ColorTools::colorBrightness(sample));
    COMPARE_EXACT(tools.colorLumina(sample), ColorTools::colorLumina(sample));
    QCOMPARE(tools.isLight(QColor("#808080")), true);
    QCOMPARE(tools.isLight(QColor("#7f7f7f")), false);
    QCOMPARE(tools.isLight(QColor("#808080"), 200.0), false);
}

void ColorToolsTest::toolsSingleton_refusesInvalidColorLoudly()
{
    // the singleton is public API for third-party indicator packages, so a
    // bad value CAN arrive from outside; the boundary refuses it loudly
    // (qCritical) and returns the inherited silent value, 0 (dark). This
    // runs with QT_FORCE_ASSERTS: reaching the core's isValid() assert
    // would abort, so passing proves the boundary refuses BEFORE the core.
    Tools tools;
    const QColor invalid;

    QTest::ignoreMessage(QtCriticalMsg,
                         "Tools.colorBrightness: invalid color from QML, returning 0 (dark)");
    QCOMPARE(tools.colorBrightness(invalid), 0.0);

    QTest::ignoreMessage(QtCriticalMsg,
                         "Tools.colorLumina: invalid color from QML, returning 0 (dark)");
    QCOMPARE(tools.colorLumina(invalid), 0.0);

    QTest::ignoreMessage(QtCriticalMsg,
                         "Tools.isLight: invalid color from QML, returning false (dark)");
    QCOMPARE(tools.isLight(invalid), false);
}

QTEST_GUILESS_MAIN(ColorToolsTest)

#include "colortoolstest.moc"
