/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Real-link behavioral test for the two ScreenPool implementations: the Latte
// core pool (app/screenpool.cpp) and the Plasma-extended mirror pool
// (app/plasma/extended/screenpool.cpp). Both read a [ScreenConnectors] config
// group and expose id<->connector lookups; this seeds that group and asserts
// the mappings plus the known-id / not-found branches against real production
// code, linked through the lattedock-core object library.
//
// Transplanted from latte-dock-qt6 (tests/screenpooltest.cpp at origin/main);
// their per-source compile list is replaced by our object library link. The
// removeScreens case pins the absent-then-present obsolete list their
// e997ac93 fixed - the same defect was live here (an early return instead of
// continue) and is fixed in the commit right before this test landed.

// local
#include "screenpool.h"
#include "plasma/extended/screenpool.h"

// Qt
#include <QGuiApplication>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

// KDE
#include <KConfigGroup>
#include <KSharedConfig>

class ScreenPoolTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    // Latte core ScreenPool (app/screenpool.cpp)
    void lattePool_loadsSeededConnectors();
    void lattePool_unknownConnectorIsNoScreenId();
    void lattePool_unknownIdHasEmptyConnector();
    void lattePool_hasScreenId();
    void lattePool_removeScreensSkipsAbsentIdAndRemovesPresent();
    void lattePool_firstAvailableIdFillsGap();
    void lattePool_firstAvailableIdAdvancesPastContiguousRun();
    void lattePool_insertRefusesGarbageColonConnector();
    void lattePool_insertIsIdempotentForKnownConnector();
    void lattePool_loadIgnoresNonPositiveAndGarbageKeys();
    void lattePool_removalPersistsToDisk();

    // Plasma-extended ScreenPool (app/plasma/extended/screenpool.cpp)
    void plasmaPool_loadsSeededConnectors();
    void plasmaPool_unknownConnectorIsNotFound();
    void plasmaPool_unknownIdIsEmpty();
    void plasmaPool_primaryScreenResolvesToIdZero();

private:
    QTemporaryDir m_configDir;
};

void ScreenPoolTest::initTestCase()
{
    QVERIFY(m_configDir.isValid());
}

// --- Latte::ScreenPool ------------------------------------------------------

void ScreenPoolTest::lattePool_loadsSeededConnectors()
{
    auto config = KSharedConfig::openConfig(m_configDir.filePath(QStringLiteral("lattepool.rc")),
                                            KConfig::SimpleConfig);
    KConfigGroup group(config, QStringLiteral("ScreenConnectors"));
    // serialized form is "name:::x,y wxh"
    group.writeEntry(QStringLiteral("10"), QStringLiteral("DP-1:::0,0 1920x1080"));
    group.writeEntry(QStringLiteral("11"), QStringLiteral("HDMI-1:::1920,0 1280x1024"));
    group.sync();

    Latte::ScreenPool pool(config);
    pool.load();

    QCOMPARE(pool.id(QStringLiteral("DP-1")), 10);
    QCOMPARE(pool.id(QStringLiteral("HDMI-1")), 11);
    QCOMPARE(pool.connector(10), QStringLiteral("DP-1"));
    QCOMPARE(pool.connector(11), QStringLiteral("HDMI-1"));
}

void ScreenPoolTest::lattePool_unknownConnectorIsNoScreenId()
{
    auto config = KSharedConfig::openConfig(m_configDir.filePath(QStringLiteral("lattepool2.rc")),
                                            KConfig::SimpleConfig);
    KConfigGroup group(config, QStringLiteral("ScreenConnectors"));
    group.writeEntry(QStringLiteral("10"), QStringLiteral("DP-1:::0,0 1920x1080"));
    group.sync();

    Latte::ScreenPool pool(config);
    pool.load();

    QCOMPARE(pool.id(QStringLiteral("does-not-exist")), int(Latte::ScreenPool::NOSCREENID));
    QCOMPARE(int(Latte::ScreenPool::NOSCREENID), -1);
}

void ScreenPoolTest::lattePool_unknownIdHasEmptyConnector()
{
    auto config = KSharedConfig::openConfig(m_configDir.filePath(QStringLiteral("lattepool3.rc")),
                                            KConfig::SimpleConfig);
    KConfigGroup group(config, QStringLiteral("ScreenConnectors"));
    group.writeEntry(QStringLiteral("10"), QStringLiteral("DP-1:::0,0 1920x1080"));
    group.sync();

    Latte::ScreenPool pool(config);
    pool.load();

    // an id that was never mapped resolves to an empty connector string
    QVERIFY(pool.connector(9999).isEmpty());
}

void ScreenPoolTest::lattePool_hasScreenId()
{
    auto config = KSharedConfig::openConfig(m_configDir.filePath(QStringLiteral("lattepool4.rc")),
                                            KConfig::SimpleConfig);
    KConfigGroup group(config, QStringLiteral("ScreenConnectors"));
    group.writeEntry(QStringLiteral("10"), QStringLiteral("DP-1:::0,0 1920x1080"));
    group.sync();

    Latte::ScreenPool pool(config);
    pool.load();

    QVERIFY(pool.hasScreenId(10));
    QVERIFY(!pool.hasScreenId(9999));
    // negative ids are never valid screen ids
    QVERIFY(!pool.hasScreenId(-1));
    QVERIFY(!pool.hasScreenId(int(Latte::ScreenPool::NOSCREENID)));
}

void ScreenPoolTest::lattePool_removeScreensSkipsAbsentIdAndRemovesPresent()
{
    auto config = KSharedConfig::openConfig(m_configDir.filePath(QStringLiteral("lattepool_remove.rc")),
                                            KConfig::SimpleConfig);
    KConfigGroup group(config, QStringLiteral("ScreenConnectors"));
    group.writeEntry(QStringLiteral("10"), QStringLiteral("DP-1:::0,0 1920x1080"));
    group.writeEntry(QStringLiteral("11"), QStringLiteral("HDMI-1:::1920,0 1280x1024"));
    group.sync();

    Latte::ScreenPool pool(config);
    pool.load();
    QVERIFY(pool.hasScreenId(10));
    QVERIFY(pool.hasScreenId(11));

    // Obsolete list: an id that is NOT mapped, followed by one that IS. The
    // absent id must not abort processing of the present one (the bug returned
    // on the first miss, silently leaving later obsolete screens mapped).
    Latte::Data::ScreensTable obsolete;
    obsolete << Latte::Data::Screen(QStringLiteral("99"), QStringLiteral("ghost:::0,0 800x600"));
    obsolete << Latte::Data::Screen(QStringLiteral("11"), QStringLiteral("HDMI-1:::1920,0 1280x1024"));

    pool.removeScreens(obsolete);

    // id 11 was present and listed obsolete -> gone, despite the earlier absent id 99
    QVERIFY(!pool.hasScreenId(11));
    // id 10 was never obsolete -> still mapped
    QVERIFY(pool.hasScreenId(10));
}

void ScreenPoolTest::lattePool_firstAvailableIdFillsGap()
{
    auto config = KSharedConfig::openConfig(m_configDir.filePath(QStringLiteral("lattepool_gap.rc")),
                                            KConfig::SimpleConfig);
    KConfigGroup group(config, QStringLiteral("ScreenConnectors"));
    // Seed three connectors so the QGuiApp virtual screen (which load() auto-inserts
    // for any QScreen not yet in the table) occupies id 13, leaving 10-12 under our control.
    group.writeEntry(QStringLiteral("10"), QStringLiteral("DP-0:::0,0 1920x1080"));
    group.writeEntry(QStringLiteral("11"), QStringLiteral("DP-1:::1920,0 1920x1080"));
    group.writeEntry(QStringLiteral("12"), QStringLiteral("HDMI-1:::3840,0 1280x1024"));
    group.sync();

    Latte::ScreenPool pool(config);
    pool.load();
    // After load, ids 10-12 are ours; the virtual QScreen fills 13+.
    QVERIFY(pool.hasScreenId(10));
    QVERIFY(pool.hasScreenId(11));
    QVERIFY(pool.hasScreenId(12));

    // Punch a hole at 11 so there is a gap between 10 and 12.
    Latte::Data::ScreensTable toRemove;
    toRemove << Latte::Data::Screen(QStringLiteral("11"), QStringLiteral("DP-1:::1920,0 1920x1080"));
    pool.removeScreens(toRemove);
    QVERIFY(!pool.hasScreenId(11));

    // A new connector must reuse the lowest free id -- the hole at 11, not 13+.
    pool.insertScreenMapping(QStringLiteral("NEW-1"));

    QCOMPARE(pool.id(QStringLiteral("NEW-1")), 11);
    QCOMPARE(pool.connector(11), QStringLiteral("NEW-1"));
}

void ScreenPoolTest::lattePool_firstAvailableIdAdvancesPastContiguousRun()
{
    auto config = KSharedConfig::openConfig(m_configDir.filePath(QStringLiteral("lattepool_contig.rc")),
                                            KConfig::SimpleConfig);
    KConfigGroup group(config, QStringLiteral("ScreenConnectors"));
    // Seed two connectors; load() will auto-insert the virtual QScreen at 12.
    // insertScreenMapping("NEW-2") must then advance to 13.
    group.writeEntry(QStringLiteral("10"), QStringLiteral("DP-1:::0,0 1920x1080"));
    group.writeEntry(QStringLiteral("11"), QStringLiteral("HDMI-1:::1920,0 1280x1024"));
    group.sync();

    Latte::ScreenPool pool(config);
    pool.load();
    // ids 10 and 11 are ours; the QGuiApp virtual screen fills 12.
    QVERIFY(pool.hasScreenId(10));
    QVERIFY(pool.hasScreenId(11));
    QVERIFY(pool.hasScreenId(12));

    // The contiguous run 10,11,12 means the next new id is 13.
    pool.insertScreenMapping(QStringLiteral("NEW-2"));

    QCOMPARE(pool.id(QStringLiteral("NEW-2")), 13);
}

void ScreenPoolTest::lattePool_insertRefusesGarbageColonConnector()
{
    auto config = KSharedConfig::openConfig(m_configDir.filePath(QStringLiteral("lattepool_colon.rc")),
                                            KConfig::SimpleConfig);
    KConfigGroup group(config, QStringLiteral("ScreenConnectors"));
    group.writeEntry(QStringLiteral("10"), QStringLiteral("DP-1:::0,0 1920x1080"));
    group.sync();

    Latte::ScreenPool pool(config);
    pool.load();

    // Plasma/Qt sometimes report a garbage connector like "0:0" instead of the
    // real output name when layouts change; insertScreenMapping refuses any
    // ":"-prefixed name so the screens database never accumulates garbage ids
    // (the guard's comment in screenpool.cpp carries the history).
    pool.insertScreenMapping(QStringLiteral(":0"));

    QCOMPARE(pool.id(QStringLiteral(":0")), int(Latte::ScreenPool::NOSCREENID));
}

void ScreenPoolTest::lattePool_insertIsIdempotentForKnownConnector()
{
    auto config = KSharedConfig::openConfig(m_configDir.filePath(QStringLiteral("lattepool_idem.rc")),
                                            KConfig::SimpleConfig);
    KConfigGroup group(config, QStringLiteral("ScreenConnectors"));
    group.writeEntry(QStringLiteral("10"), QStringLiteral("DP-1:::0,0 1920x1080"));
    group.writeEntry(QStringLiteral("11"), QStringLiteral("HDMI-1:::1920,0 1280x1024"));
    group.sync();

    Latte::ScreenPool pool(config);
    pool.load();
    // ids 10 and 11 are ours; the QGuiApp virtual screen fills 12.
    QVERIFY(pool.hasScreenId(12));

    // Re-inserting a known connector must keep its id (a remap would orphan
    // every view configured against the old id) and must not burn a fresh id:
    // the next genuinely new connector still gets 13.
    pool.insertScreenMapping(QStringLiteral("DP-1"));
    QCOMPARE(pool.id(QStringLiteral("DP-1")), 10);

    pool.insertScreenMapping(QStringLiteral("NEW-3"));
    QCOMPARE(pool.id(QStringLiteral("NEW-3")), 13);
}

void ScreenPoolTest::lattePool_loadIgnoresNonPositiveAndGarbageKeys()
{
    auto config = KSharedConfig::openConfig(m_configDir.filePath(QStringLiteral("lattepool_keys.rc")),
                                            KConfig::SimpleConfig);
    KConfigGroup group(config, QStringLiteral("ScreenConnectors"));
    // Ids below FIRSTSCREENID are reserved for special cases (0 = primary),
    // and a non-numeric key parses to 0; load() must skip all of them instead
    // of materializing mappings for reserved or garbage ids.
    group.writeEntry(QStringLiteral("0"), QStringLiteral("ZeroScr:::0,0 800x600"));
    group.writeEntry(QStringLiteral("-3"), QStringLiteral("NegScr:::0,0 800x600"));
    group.writeEntry(QStringLiteral("abc"), QStringLiteral("AbcScr:::0,0 800x600"));
    group.writeEntry(QStringLiteral("10"), QStringLiteral("DP-1:::0,0 1920x1080"));
    group.sync();

    Latte::ScreenPool pool(config);
    pool.load();

    QCOMPARE(pool.id(QStringLiteral("DP-1")), 10);
    QCOMPARE(pool.id(QStringLiteral("ZeroScr")), int(Latte::ScreenPool::NOSCREENID));
    QCOMPARE(pool.id(QStringLiteral("NegScr")), int(Latte::ScreenPool::NOSCREENID));
    QCOMPARE(pool.id(QStringLiteral("AbcScr")), int(Latte::ScreenPool::NOSCREENID));
    QVERIFY(pool.connector(0).isEmpty());
    QVERIFY(pool.connector(-3).isEmpty());
}

void ScreenPoolTest::lattePool_removalPersistsToDisk()
{
    const QString path = m_configDir.filePath(QStringLiteral("lattepool_persist.rc"));
    {
        auto config = KSharedConfig::openConfig(path, KConfig::SimpleConfig);
        KConfigGroup group(config, QStringLiteral("ScreenConnectors"));
        group.writeEntry(QStringLiteral("10"), QStringLiteral("DP-1:::0,0 1920x1080"));
        group.writeEntry(QStringLiteral("11"), QStringLiteral("HDMI-1:::1920,0 1280x1024"));
        group.sync();

        Latte::ScreenPool pool(config);
        pool.load();

        Latte::Data::ScreensTable obsolete;
        obsolete << Latte::Data::Screen(QStringLiteral("11"), QStringLiteral("HDMI-1:::1920,0 1280x1024"));
        pool.removeScreens(obsolete);
        // pool dtor syncs the group; the 10s save debounce timer never fires
        // inside a test, so the dtor sync is the only flush - exactly the path
        // a dock shutdown takes.
    }

    // Read the file back from DISK (plain KConfig, not the process-shared
    // instance): the deletion must have reached storage, or the obsolete
    // screen resurrects on the next dock start.
    KConfig disk(path, KConfig::SimpleConfig);
    const KConfigGroup group = disk.group(QStringLiteral("ScreenConnectors"));
    QVERIFY(group.hasKey(QStringLiteral("10")));
    QVERIFY(!group.hasKey(QStringLiteral("11")));
}

// --- Latte::PlasmaExtended::ScreenPool --------------------------------------
//
// This pool hardcodes KSharedConfig::openConfig("plasmashellrc"), which resolves
// against XDG_CONFIG_HOME. main() points XDG_CONFIG_HOME at a temp dir and the
// file is seeded there before each construction.

static void seedPlasmaShellRc()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmashellrc"));
    KConfigGroup group(config, QStringLiteral("ScreenConnectors"));
    group.writeEntry(QStringLiteral("0"), QStringLiteral("eDP-1"));
    group.writeEntry(QStringLiteral("1"), QStringLiteral("DP-2"));
    group.writeEntry(QStringLiteral("2"), QStringLiteral("HDMI-A-1"));
    group.sync();
}

void ScreenPoolTest::plasmaPool_loadsSeededConnectors()
{
    seedPlasmaShellRc();

    Latte::PlasmaExtended::ScreenPool pool;

    QCOMPARE(pool.id(QStringLiteral("DP-2")), 1);
    QCOMPARE(pool.id(QStringLiteral("HDMI-A-1")), 2);
    QCOMPARE(pool.connector(1), QStringLiteral("DP-2"));
    QCOMPARE(pool.connector(2), QStringLiteral("HDMI-A-1"));
}

void ScreenPoolTest::plasmaPool_unknownConnectorIsNotFound()
{
    seedPlasmaShellRc();

    Latte::PlasmaExtended::ScreenPool pool;

    // a connector that is neither mapped nor the primary screen returns -1
    QCOMPARE(pool.id(QStringLiteral("totally-unknown-output")), -1);
}

void ScreenPoolTest::plasmaPool_unknownIdIsEmpty()
{
    seedPlasmaShellRc();

    Latte::PlasmaExtended::ScreenPool pool;

    // an unmapped, non-zero id has no connector
    QVERIFY(pool.connector(4242).isEmpty());
}

void ScreenPoolTest::plasmaPool_primaryScreenResolvesToIdZero()
{
    seedPlasmaShellRc();

    Latte::PlasmaExtended::ScreenPool pool;

    // Plasma reserves id 0 for the primary screen: a connector that is not in
    // the [ScreenConnectors] map but IS the current primary resolves to 0, and
    // connector(0) answers with the primary's name. Latte's multi-screen
    // assignment logic relies on this exact fallback pair.
    const QString primaryName = qGuiApp->primaryScreen()->name();
    QCOMPARE(pool.id(primaryName), 0);
    QCOMPARE(pool.connector(0), primaryName);
}

int main(int argc, char *argv[])
{
    // Point KSharedConfig at a throwaway config dir before QGuiApplication so the
    // Plasma-extended pool's hardcoded "plasmashellrc" resolves to our seed file
    // and never touches the real desktop config.
    static QTemporaryDir xdgConfig;
    qputenv("XDG_CONFIG_HOME", xdgConfig.path().toUtf8());

    QGuiApplication app(argc, argv);
    ScreenPoolTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "screenpooltest.moc"
