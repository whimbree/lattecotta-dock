/*
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Real-link behavioral test for the app/tools/commontools.cpp free helpers
// (linked through lattedock-core): the rect<->string serialization that the
// persisted screen records ride ([ScreenConnectors] entries deserialize
// through Data::Screen::init -> stringToRect), and the XDG path resolvers.
//
// Transplanted from latte-dock-qt6 (tests/commontoolstest.cpp at
// 81384003, github.com/CaptSilver/latte-dock-qt6) and reshaped for this tree: their colorBrightness/colorLumina
// cases do not apply - that math moved to the ColorTools pure core (EX-19,
// one authority instead of nine copies) and tests/units/colortoolstest.cpp
// already pins it sanitized. What this suite adds over theirs: the
// malformed-rect refusal rows (pinning the out-of-range fix landed right
// before this test - the fork's version still indexes blindly) and the
// standardPath resolution through a planted XDG_DATA_HOME fixture.

// local
#include "tools/commontools.h"

// Qt
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QRect>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

class CommonToolsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void formatRectAsString();
    void roundTripRectThroughString_data();
    void roundTripRectThroughString();
    void refuseMalformedRectString_data();
    void refuseMalformedRectString();

    void resolveConfigPathFromXdg();
    void resolveStandardPathLocalFirst();
};

void CommonToolsTest::formatRectAsString()
{
    // the exact on-disk format of persisted screen geometry
    QCOMPARE(Latte::rectToString(QRect(10, 20, 30, 40)), QStringLiteral("10,20 30x40"));
    QCOMPARE(Latte::rectToString(QRect(-5, -7, 100, 200)), QStringLiteral("-5,-7 100x200"));
}

void CommonToolsTest::roundTripRectThroughString_data()
{
    QTest::addColumn<QRect>("rect");

    QTest::newRow("origin") << QRect(0, 0, 0, 0);
    QTest::newRow("typical") << QRect(10, 20, 30, 40);
    QTest::newRow("negative position") << QRect(-100, -200, 1920, 1080);
    QTest::newRow("large 8k") << QRect(3840, 2160, 7680, 4320);
}

void CommonToolsTest::roundTripRectThroughString()
{
    QFETCH(QRect, rect);
    QCOMPARE(Latte::stringToRect(Latte::rectToString(rect)), rect);
}

void CommonToolsTest::refuseMalformedRectString_data()
{
    QTest::addColumn<QString>("input");

    // every row crashed (out-of-range list index) before the boundary
    // refusal landed; the strings model a hand-corrupted config entry
    QTest::newRow("empty") << "";
    QTest::newRow("no space") << "garbage";
    QTest::newRow("position only") << "1,2";
    QTest::newRow("three parts") << "1,2 3x4 extra";
    QTest::newRow("size not wxh") << "1,2 34";
    QTest::newRow("non-numeric components") << "a,b cxd";
    QTest::newRow("empty size component") << "1,2 3x";
}

void CommonToolsTest::refuseMalformedRectString()
{
    QFETCH(QString, input);

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("stringToRect: .* refusing")));
    const QRect refused = Latte::stringToRect(input);
    QVERIFY(refused.isNull());
}

void CommonToolsTest::resolveConfigPathFromXdg()
{
    // main() pointed XDG_CONFIG_HOME at a temp dir before the app object;
    // configPath() must resolve exactly there, never at the real desktop's
    QCOMPARE(Latte::configPath(), qEnvironmentVariable("XDG_CONFIG_HOME"));
}

void CommonToolsTest::resolveStandardPathLocalFirst()
{
    // a subPath planted under the temp XDG_DATA_HOME resolves (local head
    // of the standard locations); an unplanted one yields empty
    const QString dataHome = qEnvironmentVariable("XDG_DATA_HOME");
    QVERIFY(QDir().mkpath(dataHome + QStringLiteral("/latteprobe")));
    QFile f(dataHome + QStringLiteral("/latteprobe/marker.txt"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("x");
    f.close();

    QCOMPARE(Latte::standardPath(QStringLiteral("latteprobe/marker.txt"), true),
             dataHome + QStringLiteral("/latteprobe/marker.txt"));
    QVERIFY(Latte::standardPath(QStringLiteral("latteprobe/absent.txt"), true).isEmpty());
}

int main(int argc, char *argv[])
{
    // both XDG homes move to temp dirs before QGuiApplication so the path
    // resolvers are asserted against known locations and the real desktop
    // config/data stay untouched (same pattern as screenpooltest)
    static QTemporaryDir xdgConfig;
    static QTemporaryDir xdgData;
    qputenv("XDG_CONFIG_HOME", xdgConfig.path().toUtf8());
    qputenv("XDG_DATA_HOME", xdgData.path().toUtf8());
    qputenv("XDG_DATA_DIRS", xdgData.path().toUtf8());

    QGuiApplication app(argc, argv);
    CommonToolsTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "commontoolstest.moc"
