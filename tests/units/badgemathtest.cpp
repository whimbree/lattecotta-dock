/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// EX-20 BadgeMath (docs/tracking/QML_EXTRACTION_PLAN.md): behavioral tests for the
// badge identifier/record/proportion/arc/label core and its QML-facing
// wrapper (compiled into this sanitized binary, so the boundary refusals
// run instrumented too).
//
// Reference values are hand-derived from the Qt5 bodies the core replaces
// (f0ad7b23 plasmoid main.qml getBadger/updateBadge, ProgressOverlay.qml
// proportion, BadgeText.qml onPaint/sizing/label): every formula is closed
// form (2*pi*proportion, the 0.9 ring factor, the 9999 boundary, JS
// substring matching), so no generated table is needed - each row cites
// the behavior it pins.

#include <QtTest>

#include "../../declarativeimports/core/badgemathtools.h"
#include "../../declarativeimports/core/units/badgemath.h"

#include <cmath>

using namespace Latte::BadgeMath;

namespace {

QList<BadgeRecord> threeRecords()
{
    return {
        BadgeRecord{QStringLiteral("org.kde.konsole.desktop"), QStringLiteral("3")},
        BadgeRecord{QStringLiteral("firefox.desktop"), QStringLiteral("7")},
        BadgeRecord{QStringLiteral("org.kde.dolphin.desktop"), QStringLiteral("1")},
    };
}

QVariantMap recordMap(const QString &id, const QString &value)
{
    QVariantMap row;
    row.insert(QStringLiteral("id"), id);
    row.insert(QStringLiteral("value"), value);
    return row;
}

}

class BadgeMathTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void launcherTail_identifierForms_data();
    void launcherTail_identifierForms();
    void desktopEntryId_appendsSuffix();

    void findBadgeRecord_matchesTailBySubstring();
    void findBadgeRecord_firstMatchWins();
    void findBadgeRecord_missAndEmpty();

    void parseBadgeCount_table_data();
    void parseBadgeCount_table();

    void applyBadge_appendsOnMiss();
    void applyBadge_mutatesOnHit();
    void applyBadge_substringHitMutatesInsteadOfAppending();
    void applyBadge_slashIdentifierQuirkPinned();

    void proportionFor_decisionTable_data();
    void proportionFor_decisionTable();

    void arcFor_sweeps_data();
    void arcFor_sweeps();

    void ringRadiiFor_fullCircleAndRing();
    void ringRadiiFor_degenerateHeights();

    void classifyBadgeLabel_boundaries();
    void formatBadgeCount_localeGrouping();

    void wrapper_countLabel();
    void wrapper_parseCountRefusesMalformedLoudly();
    void wrapper_badgeCountForLauncher();
    void wrapper_applyBadgeRoundTrip();
    void wrapper_applyBadgeRefusesMalformedRows();
    void wrapper_taskMatchesBadge();
    void wrapper_proportionAndRingDelegate();
    void wrapper_arcRefusesNegativeProportionLoudly();
};

// ---- identifier parsing ----------------------------------------------------

void BadgeMathTest::launcherTail_identifierForms_data()
{
    QTest::addColumn<QString>("identifier");
    QTest::addColumn<QString>("expected");

    // Qt5 getBadger: n = lastIndexOf('/'); n>=0 ? substring(n+1) : identifier
    QTest::newRow("launcher url") << QStringLiteral("file:///usr/share/applications/org.kde.konsole.desktop")
                                  << QStringLiteral("org.kde.konsole.desktop");
    QTest::newRow("no slash") << QStringLiteral("org.kde.konsole.desktop")
                              << QStringLiteral("org.kde.konsole.desktop");
    QTest::newRow("trailing slash") << QStringLiteral("dir/") << QString();
    QTest::newRow("multiple slashes") << QStringLiteral("a/b/c") << QStringLiteral("c");
    QTest::newRow("empty") << QString() << QString();
}

void BadgeMathTest::launcherTail_identifierForms()
{
    QFETCH(QString, identifier);
    QFETCH(QString, expected);
    QCOMPARE(launcherTail(identifier), expected);
}

void BadgeMathTest::desktopEntryId_appendsSuffix()
{
    QCOMPARE(desktopEntryId(QStringLiteral("org.kde.konsole")), QStringLiteral("org.kde.konsole.desktop"));
    QCOMPARE(desktopEntryId(QString()), QStringLiteral(".desktop"));
}

// ---- record lookup ----------------------------------------------------------

void BadgeMathTest::findBadgeRecord_matchesTailBySubstring()
{
    const auto records = threeRecords();

    // applications: form has no '/': the tail is the whole identifier and
    // it CONTAINS the record id, so it matches (JS indexOf semantics)
    const auto byUrl = findBadgeRecord(records, QStringLiteral("applications:org.kde.konsole.desktop"));
    QVERIFY(byUrl.has_value());
    QCOMPARE(*byUrl, qsizetype(0));

    const auto bySlashUrl = findBadgeRecord(records, QStringLiteral("file:///usr/share/applications/firefox.desktop"));
    QVERIFY(bySlashUrl.has_value());
    QCOMPARE(*bySlashUrl, qsizetype(1));

    // substring semantics preserved from JS indexOf: a record id that is a
    // fragment of the tail still matches
    QList<BadgeRecord> fragment{BadgeRecord{QStringLiteral("konsole.desktop"), QStringLiteral("2")}};
    const auto byFragment = findBadgeRecord(fragment, QStringLiteral("x/org.kde.konsole.desktop"));
    QVERIFY(byFragment.has_value());
    QCOMPARE(*byFragment, qsizetype(0));
}

void BadgeMathTest::findBadgeRecord_firstMatchWins()
{
    QList<BadgeRecord> records{
        BadgeRecord{QStringLiteral("konsole.desktop"), QStringLiteral("1")},
        BadgeRecord{QStringLiteral("org.kde.konsole.desktop"), QStringLiteral("2")},
    };
    // both ids are contained in the tail; Qt5 returned the first
    const auto found = findBadgeRecord(records, QStringLiteral("a/org.kde.konsole.desktop"));
    QVERIFY(found.has_value());
    QCOMPARE(*found, qsizetype(0));
}

void BadgeMathTest::findBadgeRecord_missAndEmpty()
{
    QVERIFY(!findBadgeRecord(threeRecords(), QStringLiteral("x/unknown.desktop")).has_value());
    QVERIFY(!findBadgeRecord({}, QStringLiteral("x/org.kde.konsole.desktop")).has_value());
}

// ---- value parsing ----------------------------------------------------------

void BadgeMathTest::parseBadgeCount_table_data()
{
    QTest::addColumn<QString>("value");
    QTest::addColumn<bool>("parses");
    QTest::addColumn<int>("expected");

    // Qt5's `value === "" ? 0 : Number(value)` branch
    QTest::newRow("empty") << QString() << true << 0;
    QTest::newRow("whitespace only") << QStringLiteral("  ") << true << 0; // JS Number("  ") == 0
    QTest::newRow("plain integer") << QStringLiteral("5") << true << 5;
    QTest::newRow("negative") << QStringLiteral("-3") << true << -3;
    QTest::newRow("decimal truncates toward zero") << QStringLiteral("3.7") << true << 3;
    QTest::newRow("negative decimal truncates toward zero") << QStringLiteral("-3.7") << true << -3;
    QTest::newRow("exponent form") << QStringLiteral("1e2") << true << 100;
    QTest::newRow("leading plus") << QStringLiteral("+5") << true << 5;
    QTest::newRow("padded integer") << QStringLiteral(" 42 ") << true << 42;

    // refused loudly instead of coerced (recorded deviation: Qt5's two
    // paths disagreed here - Number() gave 0, parseInt() gave a prefix)
    QTest::newRow("trailing garbage") << QStringLiteral("2abc") << false << 0;
    QTest::newRow("pure garbage") << QStringLiteral("abc") << false << 0;
    QTest::newRow("grouped digits") << QStringLiteral("1,000") << false << 0;
    QTest::newRow("hex not emulated") << QStringLiteral("0x10") << false << 0;
    QTest::newRow("infinity") << QStringLiteral("inf") << false << 0;
    QTest::newRow("beyond int range") << QStringLiteral("1e10") << false << 0;
    QTest::newRow("below int range") << QStringLiteral("-1e10") << false << 0;
}

void BadgeMathTest::parseBadgeCount_table()
{
    QFETCH(QString, value);
    QFETCH(bool, parses);
    QFETCH(int, expected);

    const auto parsed = parseBadgeCount(value);
    QCOMPARE(parsed.has_value(), parses);
    if (parses) {
        QCOMPARE(*parsed, expected);
    }
}

// ---- record update ----------------------------------------------------------

void BadgeMathTest::applyBadge_appendsOnMiss()
{
    const auto updated = applyBadge(threeRecords(), QStringLiteral("org.kde.kate"), QStringLiteral("4"));
    QCOMPARE(updated.size(), 4);
    QCOMPARE(updated.last().id, QStringLiteral("org.kde.kate.desktop"));
    QCOMPARE(updated.last().value, QStringLiteral("4"));
}

void BadgeMathTest::applyBadge_mutatesOnHit()
{
    const auto updated = applyBadge(threeRecords(), QStringLiteral("firefox"), QStringLiteral("12"));
    QCOMPARE(updated.size(), 3); // no duplicate row
    QCOMPARE(updated[1].id, QStringLiteral("firefox.desktop"));
    QCOMPARE(updated[1].value, QStringLiteral("12"));
    QCOMPARE(updated[0].value, QStringLiteral("3")); // neighbors untouched
}

void BadgeMathTest::applyBadge_substringHitMutatesInsteadOfAppending()
{
    // a stored fragment id captures the update for the longer identifier -
    // JS indexOf semantics, preserved
    QList<BadgeRecord> records{BadgeRecord{QStringLiteral("konsole.desktop"), QStringLiteral("1")}};
    const auto updated = applyBadge(records, QStringLiteral("org.kde.konsole"), QStringLiteral("9"));
    QCOMPARE(updated.size(), 1);
    QCOMPARE(updated[0].id, QStringLiteral("konsole.desktop"));
    QCOMPARE(updated[0].value, QStringLiteral("9"));
}

void BadgeMathTest::applyBadge_slashIdentifierQuirkPinned()
{
    // Qt5 quirk, preserved: getBadger matched against the tail after the
    // last '/', but appended the FULL ".desktop" id - so a slash-carrying
    // identifier never finds its own record and appends again. Pinned as
    // documentation that the tail/contains semantics moved unchanged
    // (identifiers from the D-Bus path are appIds without slashes).
    auto records = applyBadge({}, QStringLiteral("a/b"), QStringLiteral("1"));
    QCOMPARE(records.size(), 1);
    QCOMPARE(records[0].id, QStringLiteral("a/b.desktop"));

    records = applyBadge(records, QStringLiteral("a/b"), QStringLiteral("2"));
    QCOMPARE(records.size(), 2); // duplicate append, exactly like Qt5
}

// ---- proportion ------------------------------------------------------------

void BadgeMathTest::proportionFor_decisionTable_data()
{
    QTest::addColumn<bool>("progressVisible");
    QTest::addColumn<double>("progress");
    QTest::addColumn<bool>("countVisible");
    QTest::addColumn<int>("badgeIndicator");
    QTest::addColumn<double>("expected");

    // ProgressOverlay.qml:93-103: progress wins, else full circle when a
    // count shows, else nothing
    QTest::newRow("progress 0") << true << 0.0 << false << 0 << 0.0;
    QTest::newRow("progress half") << true << 50.0 << false << 0 << 0.5;
    QTest::newRow("progress full") << true << 100.0 << false << 0 << 1.0;
    QTest::newRow("progress wins over badge") << true << 25.0 << true << 5 << 0.25;
    QTest::newRow("count only") << false << 0.0 << true << 0 << 1.0;
    QTest::newRow("badge indicator only") << false << 0.0 << false << 3 << 1.0;
    QTest::newRow("nothing") << false << 0.0 << false << 0 << 0.0;
    QTest::newRow("negative indicator shows nothing") << false << 0.0 << false << -1 << 0.0;
}

void BadgeMathTest::proportionFor_decisionTable()
{
    QFETCH(bool, progressVisible);
    QFETCH(double, progress);
    QFETCH(bool, countVisible);
    QFETCH(int, badgeIndicator);
    QFETCH(double, expected);

    QCOMPARE(proportionFor(progressVisible, progress, countVisible, badgeIndicator), expected);
}

// ---- arc geometry -----------------------------------------------------------

void BadgeMathTest::arcFor_sweeps_data()
{
    QTest::addColumn<double>("proportion");
    QTest::addColumn<double>("sweep");

    QTest::newRow("zero") << 0.0 << 0.0;
    QTest::newRow("quarter") << 0.25 << M_PI / 2.0;
    QTest::newRow("half") << 0.5 << M_PI;
    QTest::newRow("full") << 1.0 << 2.0 * M_PI;
    // >1 stays unclamped like Qt5 (the canvas wraps); the shipped pipeline
    // cannot produce it (smartlauncheritem clamps progress to 0..100)
    QTest::newRow("overflow unclamped") << 1.37 << 2.0 * M_PI * 1.37;
}

void BadgeMathTest::arcFor_sweeps()
{
    QFETCH(double, proportion);
    QFETCH(double, sweep);

    const BadgeArc arc = arcFor(proportion);
    QCOMPARE(arc.startRadian, -M_PI / 2.0);
    QCOMPARE(arc.sweepRadian, sweep);
}

// ---- ring sizing ------------------------------------------------------------

void BadgeMathTest::ringRadiiFor_fullCircleAndRing()
{
    const RingRadii disc = ringRadiiFor(24.0, true);
    QCOMPARE(disc.outer, 12.0);
    QCOMPARE(disc.inner, 0.0);

    const RingRadii ring = ringRadiiFor(24.0, false);
    QCOMPARE(ring.outer, 12.0);
    QCOMPARE(ring.inner, 12.0 * 0.9);
}

void BadgeMathTest::ringRadiiFor_degenerateHeights()
{
    const RingRadii flat = ringRadiiFor(0.0, false);
    QCOMPARE(flat.outer, 0.0);
    QCOMPARE(flat.inner, 0.0);

    // Qt5's partSize<0 clamp, kept as a deliberate contract: bindings
    // evaluate transiently during creation before anchors settle
    const RingRadii transient = ringRadiiFor(-10.0, true);
    QCOMPARE(transient.outer, 0.0);
    QCOMPARE(transient.inner, 0.0);
}

// ---- label ------------------------------------------------------------------

void BadgeMathTest::classifyBadgeLabel_boundaries()
{
    QCOMPARE(classifyBadgeLabel(-1), BadgeLabelKind::None);
    QCOMPARE(classifyBadgeLabel(0), BadgeLabelKind::None);
    QCOMPARE(classifyBadgeLabel(1), BadgeLabelKind::Number);
    QCOMPARE(classifyBadgeLabel(9999), BadgeLabelKind::Number);
    QCOMPARE(classifyBadgeLabel(10000), BadgeLabelKind::Overflow);
}

void BadgeMathTest::formatBadgeCount_localeGrouping()
{
    // Qt5 QML used numberValue.toLocaleString(Qt.locale(), 'f', 0):
    // grouped, no decimals - QLocale::toString(int) is the same rendering
    QCOMPARE(formatBadgeCount(9999, QLocale(QStringLiteral("en_US"))), QStringLiteral("9,999"));
    QCOMPARE(formatBadgeCount(9999, QLocale(QStringLiteral("de_DE"))), QStringLiteral("9.999"));
    // the C locale omits grouping by default
    QCOMPARE(formatBadgeCount(9999, QLocale::c()), QStringLiteral("9999"));
    QCOMPARE(formatBadgeCount(1, QLocale::c()), QStringLiteral("1"));
}

// ---- wrapper ----------------------------------------------------------------

void BadgeMathTest::wrapper_countLabel()
{
    Latte::BadgeMathTools tools;
    // offscreen test env loads no catalogs: the overflow label is the
    // English msgid, deterministically (LANGUAGE/LC_ALL pinned in ctest)
    QCOMPARE(tools.countLabel(10000), QStringLiteral("9,999+"));
    QCOMPARE(tools.countLabel(0), QString());
    QCOMPARE(tools.countLabel(-2), QString());
    QCOMPARE(tools.countLabel(7), QLocale().toString(7));
}

void BadgeMathTest::wrapper_parseCountRefusesMalformedLoudly()
{
    Latte::BadgeMathTools tools;
    QCOMPARE(tools.parseCount(QStringLiteral("5"), QStringLiteral("org.kde.konsole")), 5);
    QCOMPARE(tools.parseCount(QString(), QStringLiteral("org.kde.konsole")), 0);

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("badge value.*2abc.*org\\.kde\\.konsole")));
    QCOMPARE(tools.parseCount(QStringLiteral("2abc"), QStringLiteral("org.kde.konsole")), 0);
}

void BadgeMathTest::wrapper_badgeCountForLauncher()
{
    Latte::BadgeMathTools tools;
    const QVariantList records{
        recordMap(QStringLiteral("org.kde.konsole.desktop"), QStringLiteral("3")),
        recordMap(QStringLiteral("firefox.desktop"), QStringLiteral("7")),
    };

    QCOMPARE(tools.badgeCountForLauncher(records, QStringLiteral("file:///apps/firefox.desktop")), 7);
    QCOMPARE(tools.badgeCountForLauncher(records, QStringLiteral("file:///apps/unknown.desktop")), 0);
    QCOMPARE(tools.badgeCountForLauncher({}, QStringLiteral("file:///apps/firefox.desktop")), 0);

    // a stored malformed value resolves loudly to 0 (parse unification)
    const QVariantList malformed{recordMap(QStringLiteral("firefox.desktop"), QStringLiteral("2abc"))};
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("badge value.*2abc")));
    QCOMPARE(tools.badgeCountForLauncher(malformed, QStringLiteral("file:///apps/firefox.desktop")), 0);
}

void BadgeMathTest::wrapper_applyBadgeRoundTrip()
{
    Latte::BadgeMathTools tools;
    QVariantList records;

    records = tools.applyBadge(records, QStringLiteral("org.kde.konsole"), QStringLiteral("3"));
    QCOMPARE(records.size(), 1);

    records = tools.applyBadge(records, QStringLiteral("org.kde.konsole"), QStringLiteral("5"));
    QCOMPARE(records.size(), 1); // mutated, not duplicated

    const QVariantMap row = records[0].toMap();
    QCOMPARE(row.value(QStringLiteral("id")).toString(), QStringLiteral("org.kde.konsole.desktop"));
    QCOMPARE(row.value(QStringLiteral("value")).toString(), QStringLiteral("5"));
}

void BadgeMathTest::wrapper_applyBadgeRefusesMalformedRows()
{
    Latte::BadgeMathTools tools;
    const QVariantList malformed{QVariant(QStringLiteral("not a record map"))};

    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral("malformed badge record")));
    const QVariantList out = tools.applyBadge(malformed, QStringLiteral("org.kde.konsole"), QStringLiteral("3"));
    QCOMPARE(out, malformed); // refused: input returned unchanged
}

void BadgeMathTest::wrapper_taskMatchesBadge()
{
    Latte::BadgeMathTools tools;
    QVERIFY(tools.taskMatchesBadge(QStringLiteral("file:///apps/org.kde.konsole.desktop"), QStringLiteral("org.kde.konsole")));
    QVERIFY(!tools.taskMatchesBadge(QStringLiteral("file:///apps/firefox.desktop"), QStringLiteral("org.kde.konsole")));
    QVERIFY(!tools.taskMatchesBadge(QString(), QStringLiteral("org.kde.konsole")));
}

void BadgeMathTest::wrapper_proportionAndRingDelegate()
{
    Latte::BadgeMathTools tools;
    QCOMPARE(tools.proportionFor(true, 50.0, false, 0), 0.5);
    QCOMPARE(tools.proportionFor(false, 0.0, false, 3), 1.0);
    QCOMPARE(tools.ringOuterRadius(24.0), 12.0);
    QCOMPARE(tools.ringInnerRadius(24.0, false), 12.0 * 0.9);
    QCOMPARE(tools.ringInnerRadius(24.0, true), 0.0);
    QCOMPARE(tools.arcStartRadian(), -M_PI / 2.0);
    QCOMPARE(tools.arcSweepRadian(0.5), M_PI);
}

void BadgeMathTest::wrapper_arcRefusesNegativeProportionLoudly()
{
    Latte::BadgeMathTools tools;
    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral("negative badge proportion")));
    QCOMPARE(tools.arcSweepRadian(-0.5), 0.0); // refused: nothing drawn
}

QTEST_GUILESS_MAIN(BadgeMathTest)
#include "badgemathtest.moc"
