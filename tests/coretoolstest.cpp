/*
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Real-source behavioral test for the engine-free helpers under
// declarativeimports/core that the QML runtime imports as LatteCore:
//   environment.cpp - the version/duration constants QML animations and
//                     version gates read (compiled in)
//   extras.h        - the inline string/enum/epsilon helpers
//
// Transplanted from latte-dock-qt6 (tests/coretoolstest.cpp at 81384003, github.com/CaptSilver/latte-dock-qt6)
// minus their Latte::Tools color cases: that math is the ColorTools pure
// core here (EX-19) and tests/units/colortoolstest.cpp pins it sanitized -
// re-driving the same compiled code through the Tools shell adds nothing.
// Their frameworksVersion assertion is replaced by THIS tree's contract:
// plasma/version.h is gone in Plasma 6, so frameworksVersion() returns the
// runtime KDE Frameworks version (KCoreAddons::version()), keeping the
// KF5-era QML version gates unconditionally true on KF6.

// local
#include "environment.h"
#include "extras.h"

// Qt
#include <QRect>
#include <QString>
#include <QTest>

// KDE
#include <KCoreAddons>

// Plasma
#include <Plasma/Plasma>

class CoreToolsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    //! environment
    void pinAnimationDurations();
    void pinSeparatorLength();
    void composeVersionFromParts_data();
    void composeVersionFromParts();
    void reportRuntimeFrameworksVersion();

    //! extras
    void formatRectForLogging();
    void resolveEnumKeyNames();
    void compareWithinMachineEpsilon();
};

void CoreToolsTest::pinAnimationDurations()
{
    Latte::Environment env;

    // the QML animation base durations; every LatteCore.Environment consumer
    // scales off these two numbers
    QCOMPARE(env.shortDuration(), 40u);
    QCOMPARE(env.longDuration(), 240u);
    QVERIFY(env.shortDuration() < env.longDuration());
}

void CoreToolsTest::pinSeparatorLength()
{
    Latte::Environment env;
    QCOMPARE(env.separatorLength(), Latte::Environment::SeparatorLength);
    QCOMPARE(env.separatorLength(), 5);
}

void CoreToolsTest::composeVersionFromParts_data()
{
    QTest::addColumn<uint>("major");
    QTest::addColumn<uint>("minor");
    QTest::addColumn<uint>("release");
    QTest::addColumn<uint>("expected");

    // (major << 16) | (minor << 8) | release
    QTest::newRow("zero") << 0u << 0u << 0u << 0u;
    QTest::newRow("major only") << 1u << 0u << 0u << 0x10000u;
    QTest::newRow("minor only") << 0u << 1u << 0u << 0x100u;
    QTest::newRow("release only") << 0u << 0u << 1u << 0x1u;
    QTest::newRow("5.27.11") << 5u << 27u << 11u << 334603u;
}

void CoreToolsTest::composeVersionFromParts()
{
    QFETCH(uint, major);
    QFETCH(uint, minor);
    QFETCH(uint, release);
    QFETCH(uint, expected);

    Latte::Environment env;
    QCOMPARE(env.makeVersion(major, minor, release), expected);
}

void CoreToolsTest::reportRuntimeFrameworksVersion()
{
    // THIS tree's contract (documented in environment.cpp): plasma/version.h
    // is gone in Plasma 6, so the property carries the runtime KF version -
    // the KF5-era QML gates compare against 5.x and stay true forever on KF6
    Latte::Environment env;
    QCOMPARE(env.frameworksVersion(), static_cast<uint>(KCoreAddons::version()));
    QVERIFY(env.frameworksVersion() >= env.makeVersion(6, 0, 0));
}

void CoreToolsTest::formatRectForLogging()
{
    QCOMPARE(qRectToStr(QRect(1, 2, 3, 4)), QStringLiteral("(1, 2) 3x4"));
    QCOMPARE(qRectToStr(QRect(0, 0, 0, 0)), QStringLiteral("(0, 0) 0x0"));
    QCOMPARE(qRectToStr(QRect(-5, -6, 100, 200)), QStringLiteral("(-5, -6) 100x200"));
}

void CoreToolsTest::resolveEnumKeyNames()
{
    // the Plasma::Types::Location overload resolves through the static
    // metaobject; these names feed categorized debug traces
    QCOMPARE(QString::fromLatin1(qEnumToStr(Plasma::Types::BottomEdge)), QStringLiteral("BottomEdge"));
    QCOMPARE(QString::fromLatin1(qEnumToStr(Plasma::Types::TopEdge)), QStringLiteral("TopEdge"));
    QCOMPARE(QString::fromLatin1(qEnumToStr(Plasma::Types::Floating)), QStringLiteral("Floating"));

    QCOMPARE(QString::fromLatin1(qEnumToStr(Plasma::Types::Horizontal)), QStringLiteral("Horizontal"));
    QCOMPARE(QString::fromLatin1(qEnumToStr(Plasma::Types::Vertical)), QStringLiteral("Vertical"));
}

void CoreToolsTest::compareWithinMachineEpsilon()
{
    // rounding noise is equal within a couple of ulps; a real difference is not
    QVERIFY(almost_equal(0.1 + 0.2, 0.3, 2));
    QVERIFY(almost_equal(1.0f, 1.0f, 1));
    QVERIFY(!almost_equal(0.3, 0.30001, 2));
    QVERIFY(almost_equal(0.0, 0.0, 1));
}

QTEST_GUILESS_MAIN(CoreToolsTest)

#include "coretoolstest.moc"
