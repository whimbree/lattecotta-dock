/*
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Real-link behavioral test for the two shortcut helpers behind the badge
// overlay (Meta+number task activation):
//   ShortcutsPart::ShortcutsTracker - parses the [lattedock] group out of
//     kglobalshortcutsrc into activation badges; the file format pinned here
//     is what kglobalaccel actually writes (comma-separated
//     active,default,description records), so the fixtures write raw text
//     instead of going through KConfig's list serializer.
//   ShortcutsPart::ModifierTracker - modifier press bookkeeping; headless
//     there is no way to inject KModifierKeyInfo key events, so only the
//     idle-state predicates are reachable (same limit the fork hit).
//
// Transplanted from latte-dock-qt6 (tests/shortcutstest.cpp at 81384003, github.com/CaptSilver/latte-dock-qt6)
// and raised: the badge parser is a data-driven table that also pins the
// branches the fork skipped (non-Meta two-token uppercase, bare single
// token, bare lowercase letter uppercased, entry 19 boundary), the applet
// registry gets a multi-widget case, and main() neuters the session bus
// address BEFORE QGuiApplication so the constructor's KGlobalAccel calls
// (clearAllAppletShortcuts) can never reach the desktop's kglobalacceld -
// the fork's version talked to the real daemon and registered a throwaway
// test component there on every run.

// local
#include "shortcuts/modifiertracker.h"
#include "shortcuts/shortcutstracker.h"

// Qt
#include <QFile>
#include <QGuiApplication>
#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

using Latte::ShortcutsPart::ModifierTracker;
using Latte::ShortcutsPart::ShortcutsTracker;

namespace {
// ShortcutsTracker seeds one empty badge per supported activation entry;
// entries are 1-based in the config keys ("activate entry 1".."activate
// entry 19"), so entry N lands at badge index N-1.
constexpr int c_badgeCount = 19;
}

class ShortcutsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();

    //! ShortcutsTracker - badge parsing
    void missingLatteGroupKeepsSeededEmptyBadges();
    void parseEntryValueIntoBadge_data();
    void parseEntryValueIntoBadge();
    void parseKeepsBadgeListAtFullLength();
    void parseLandsLastEntryAtLastIndex();

    //! ShortcutsTracker - position-based activation switch
    void enableBasedOnPositionOnlyWhenFirstTwoBadgesExist_data();
    void enableBasedOnPositionOnlyWhenFirstTwoBadgesExist();

    //! ShortcutsTracker - applet (widget) shortcut registry
    void parseWidgetKeysIntoAppletRegistry();
    void reportEmptyBadgeForUnknownApplet();

    //! ModifierTracker - idle-state predicates
    void reportNoModifierPressedOnFreshTracker();
    void refuseSingleModifierWhenNothingIsPressed_data();
    void refuseSingleModifierWhenNothingIsPressed();
    void refuseEmptySequence();
    void refuseSequenceWhenNothingIsPressed_data();
    void refuseSequenceWhenNothingIsPressed();
    void keepIdleStateThroughBlockUnblockCycles();

private:
    //! write a kglobalshortcutsrc under the temp XDG_CONFIG_HOME whose
    //! [lattedock] group carries the given raw lines (kglobalaccel's own
    //! key=active,default,description format).
    void writeShortcutsConfig(const QStringList &latteLines);

    QString shortcutsConfigFilePath() const;
};

void ShortcutsTest::init()
{
    // every case starts without a config file; cases that need one write it
    QFile::remove(shortcutsConfigFilePath());
}

QString ShortcutsTest::shortcutsConfigFilePath() const
{
    return qEnvironmentVariable("XDG_CONFIG_HOME") + QStringLiteral("/kglobalshortcutsrc");
}

void ShortcutsTest::writeShortcutsConfig(const QStringList &latteLines)
{
    QFile f(shortcutsConfigFilePath());
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
    QTextStream out(&f);
    out << "[lattedock]\n";
    out << "_k_friendly_name=Latte Dock\n";
    for (const QString &line : latteLines) {
        out << line << "\n";
    }
}

void ShortcutsTest::missingLatteGroupKeepsSeededEmptyBadges()
{
    // a config file with no [lattedock] group at all: parseGlobalShortcuts()
    // leaves the seeded placeholders untouched
    QFile f(shortcutsConfigFilePath());
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
    f.write("[someothergroup]\nfoo=bar\n");
    f.close();

    ShortcutsTracker tracker(nullptr);
    QCOMPARE(tracker.badgesForActivate().count(), c_badgeCount);
    for (const QString &badge : tracker.badgesForActivate()) {
        QVERIFY(badge.isEmpty());
    }
    QVERIFY(!tracker.basedOnPositionEnabled());
}

void ShortcutsTest::parseEntryValueIntoBadge_data()
{
    QTest::addColumn<QString>("entryValue");
    QTest::addColumn<QString>("expectedBadge");

    // the badge is the last '+'-token of the active (first) record; the
    // Meta+<char> scheme lowercases it, every other shape uppercases it
    QTest::newRow("meta-letter lowercased") << "Meta+Z,Meta+Z,Activate Entry" << "z";
    QTest::newRow("meta-digit stays digit") << "Meta+3,Meta+3,Activate Entry" << "3";
    QTest::newRow("multi-modifier uppercased") << "Meta+Ctrl+5,Meta+Ctrl+5,Activate Entry" << "5";
    // branches the fork's table skipped:
    QTest::newRow("non-meta two-token uppercased") << "Ctrl+x,Ctrl+x,Activate Entry" << "X";
    QTest::newRow("bare function key kept") << "F5,F5,Activate Entry" << "F5";
    QTest::newRow("bare letter uppercased") << "q,q,Activate Entry" << "Q";
    // no-badge shapes
    QTest::newRow("literal none is no badge") << "none,none,Activate Entry" << "";
    QTest::newRow("unset active record is no badge") << ",Meta+5,Activate Entry" << "";
    // KConfig escapes a literal tab as \t; the parser splits the active
    // record on tab and keeps the head
    QTest::newRow("tab-separated record trimmed") << "Meta+Q\\tjunk,Meta+Q,Activate Entry" << "q";
}

void ShortcutsTest::parseEntryValueIntoBadge()
{
    QFETCH(QString, entryValue);
    QFETCH(QString, expectedBadge);

    writeShortcutsConfig({QStringLiteral("activate entry 7=") + entryValue});

    ShortcutsTracker tracker(nullptr);
    QCOMPARE(tracker.badgesForActivate().at(6), expectedBadge);
}

void ShortcutsTest::parseKeepsBadgeListAtFullLength()
{
    // when [lattedock] exists the parser rebuilds the list; it must come back
    // at full length with empty badges for every unset entry
    writeShortcutsConfig({QStringLiteral("activate entry 11=Meta+Z,Meta+Z,Activate Entry 11")});

    ShortcutsTracker tracker(nullptr);
    QCOMPARE(tracker.badgesForActivate().count(), c_badgeCount);
    QCOMPARE(tracker.badgesForActivate().at(10), QStringLiteral("z"));

    for (int i = 0; i < c_badgeCount; ++i) {
        if (i != 10) {
            QVERIFY2(tracker.badgesForActivate().at(i).isEmpty(),
                     qPrintable(QStringLiteral("badge %1 expected empty").arg(i)));
        }
    }
}

void ShortcutsTest::parseLandsLastEntryAtLastIndex()
{
    writeShortcutsConfig({QStringLiteral("activate entry 19=Meta+9,Meta+9,Activate Entry 19")});

    ShortcutsTracker tracker(nullptr);
    QCOMPARE(tracker.badgesForActivate().count(), c_badgeCount);
    QCOMPARE(tracker.badgesForActivate().at(c_badgeCount - 1), QStringLiteral("9"));
}

void ShortcutsTest::enableBasedOnPositionOnlyWhenFirstTwoBadgesExist_data()
{
    QTest::addColumn<bool>("withEntry1");
    QTest::addColumn<bool>("withEntry2");
    QTest::addColumn<bool>("expectedEnabled");

    QTest::newRow("both first entries set") << true << true << true;
    QTest::newRow("second entry missing") << true << false << false;
    QTest::newRow("first entry missing") << false << true << false;
}

void ShortcutsTest::enableBasedOnPositionOnlyWhenFirstTwoBadgesExist()
{
    QFETCH(bool, withEntry1);
    QFETCH(bool, withEntry2);
    QFETCH(bool, expectedEnabled);

    QStringList lines;
    if (withEntry1) {
        lines << QStringLiteral("activate entry 1=Meta+1,Meta+1,Activate Entry 1");
    }
    if (withEntry2) {
        lines << QStringLiteral("activate entry 2=Meta+2,Meta+2,Activate Entry 2");
    }
    writeShortcutsConfig(lines);

    ShortcutsTracker tracker(nullptr);
    QCOMPARE(tracker.basedOnPositionEnabled(), expectedEnabled);
}

void ShortcutsTest::parseWidgetKeysIntoAppletRegistry()
{
    // "activate widget <applet id>" keys feed appletShortcutBadge(id);
    // both applets must land, each with its own parsed badge
    writeShortcutsConfig({
        QStringLiteral("activate widget 42=Meta+W,Meta+W,Activate Widget 42"),
        QStringLiteral("activate widget 7=Meta+Ctrl+B,Meta+Ctrl+B,Activate Widget 7"),
    });

    ShortcutsTracker tracker(nullptr);
    QCOMPARE(tracker.appletShortcutBadge(42), QStringLiteral("w"));
    QCOMPARE(tracker.appletShortcutBadge(7), QStringLiteral("B"));

    QList<uint> applets = tracker.appletsWithPlasmaShortcuts();
    std::sort(applets.begin(), applets.end());
    QCOMPARE(applets, (QList<uint>{7, 42}));
}

void ShortcutsTest::reportEmptyBadgeForUnknownApplet()
{
    writeShortcutsConfig({QStringLiteral("activate entry 1=Meta+1,Meta+1,Activate Entry 1")});

    ShortcutsTracker tracker(nullptr);
    QVERIFY(tracker.appletShortcutBadge(999).isEmpty());
    QVERIFY(tracker.appletsWithPlasmaShortcuts().isEmpty());
}

void ShortcutsTest::reportNoModifierPressedOnFreshTracker()
{
    ModifierTracker tracker(nullptr);
    QVERIFY(tracker.noModifierPressed());
}

void ShortcutsTest::refuseSingleModifierWhenNothingIsPressed_data()
{
    QTest::addColumn<Qt::Key>("key");

    // the four tracked modifiers (Super_R normalizes onto Super_L)
    QTest::newRow("super") << Qt::Key_Super_L;
    QTest::newRow("control") << Qt::Key_Control;
    QTest::newRow("alt") << Qt::Key_Alt;
    QTest::newRow("shift") << Qt::Key_Shift;
}

void ShortcutsTest::refuseSingleModifierWhenNothingIsPressed()
{
    QFETCH(Qt::Key, key);

    // singleModifierPressed(key) demands <key> pressed AND all others
    // released; at idle the target-key half fails
    ModifierTracker tracker(nullptr);
    QVERIFY(!tracker.singleModifierPressed(key));
}

void ShortcutsTest::refuseEmptySequence()
{
    ModifierTracker tracker(nullptr);
    QVERIFY(!tracker.sequenceModifierPressed(QKeySequence()));
}

void ShortcutsTest::refuseSequenceWhenNothingIsPressed_data()
{
    QTest::addColumn<QKeySequence>("sequence");

    // one row per modifier mask the predicate inspects
    QTest::newRow("meta") << QKeySequence(Qt::META | Qt::Key_A);
    QTest::newRow("ctrl") << QKeySequence(Qt::CTRL | Qt::Key_C);
    QTest::newRow("alt") << QKeySequence(Qt::ALT | Qt::Key_F4);
    QTest::newRow("shift") << QKeySequence(Qt::SHIFT | Qt::Key_Tab);
    QTest::newRow("stacked masks") << QKeySequence(Qt::META | Qt::CTRL | Qt::Key_5);
}

void ShortcutsTest::refuseSequenceWhenNothingIsPressed()
{
    QFETCH(QKeySequence, sequence);

    ModifierTracker tracker(nullptr);
    QVERIFY(!tracker.sequenceModifierPressed(sequence));
}

void ShortcutsTest::keepIdleStateThroughBlockUnblockCycles()
{
    // block/unblock mutate the tracked-modifier filter; with nothing pressed
    // every observable predicate must stay at its idle value, including after
    // unblocking a key that was never blocked (a documented no-op)
    ModifierTracker tracker(nullptr);
    tracker.blockModifierTracking(Qt::Key_Super_L);
    tracker.blockModifierTracking(Qt::Key_Super_L); // double block is idempotent
    tracker.unblockModifierTracking(Qt::Key_Super_L);
    tracker.unblockModifierTracking(Qt::Key_Alt); // never blocked: no-op
    tracker.cancelMetaPressed();

    QVERIFY(tracker.noModifierPressed());
    QVERIFY(!tracker.singleModifierPressed(Qt::Key_Super_L));
}

int main(int argc, char *argv[])
{
    // Both env changes must precede QGuiApplication:
    // - XDG_CONFIG_HOME: Latte::configPath() resolves kglobalshortcutsrc
    //   from it, so the tracker reads our fixtures and never the desktop's
    //   real shortcuts file.
    // - DBUS_SESSION_BUS_ADDRESS: ShortcutsTracker's constructor ends in
    //   clearAllAppletShortcuts(), which calls KGlobalAccel for every
    //   "activate widget" key; pointing the bus at a dead address makes
    //   those calls fail fast instead of registering a test component in
    //   the desktop's live kglobalacceld (same neutering as
    //   contracts/askdestroysignalorderingtest.cpp).
    static QTemporaryDir xdgConfig;
    qputenv("XDG_CONFIG_HOME", xdgConfig.path().toUtf8());
    qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/dev/null");

    QGuiApplication app(argc, argv);
    ShortcutsTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "shortcutstest.moc"
