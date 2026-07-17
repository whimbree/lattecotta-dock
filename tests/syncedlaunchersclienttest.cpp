/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Behavioral test for SyncedLaunchers (app/layouts/syncedlaunchers.cpp),
// driven end to end through its public surface: register recording probe
// clients the way the tasks plasmoid does (addAbilityClient with the
// layoutName/syncedGroupId context properties), broadcast through
// addLauncher(), and observe which probes' addSyncedLauncher(QVariant,
// QVariant) got invoked. This covers the d6d57e61 dangling-client crash
// class: destroyed() fires from ~QObject where the QQuickItem part is
// already gone, so the old cast-then-remove handler removed nothing and the
// next broadcast walked a freed pointer (SIGSEGV on a launcher drop, caught
// live 2026-07-13).
//
// The transplant source (latte-dock-qt6 tests/syncedlaunchersclienttest.cpp
// at 81384003, github.com/CaptSilver/latte-dock-qt6) mirrors the handler's removal pattern over a plain QList
// because their SyncedLaunchers cannot link headlessly. Ours can (plain
// QObject parent, lives in lattedock-core), so this test is redesigned to
// drive the REAL object - the mirror would keep passing if the production
// handler regressed. The underlying Qt contract (qobject_cast returns null
// during destruction, pointer identity survives) is pinned separately by
// tests/contracts/destroyedtypedemotiontest.cpp and not duplicated here.

#include "layouts/syncedlaunchers.h"

#include <coretypes.h>

#include <QGuiApplication>
#include <QQuickItem>
#include <QStringList>
#include <QtTest>

using Latte::Layouts::SyncedLaunchers;

// A client shaped like the tasks plasmoid's launchers-syncer ability: the
// grouping context properties SyncedLaunchers reads, plus the
// addSyncedLauncher method its broadcasts invoke. Received launchers are
// recorded so the test asserts delivery, not registration.
class ProbeClient : public QQuickItem
{
    Q_OBJECT

public:
    ProbeClient(const QString &layoutName, const QString &groupId)
    {
        setProperty("layoutName", layoutName);
        setProperty("syncedGroupId", groupId);
    }

    Q_INVOKABLE void addSyncedLauncher(QVariant group, QVariant launcher)
    {
        Q_UNUSED(group);
        receivedLaunchers << launcher.toString();
    }

    QStringList receivedLaunchers;
};

class SyncedLauncherClientTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void broadcastReachesEveryGroupClient();
    void broadcastFiltersByLayoutAndGroup();
    void destroyedClientLeavesTheRegistry();
    void explicitlyRemovedClientReceivesNothing();
};

void SyncedLauncherClientTest::broadcastReachesEveryGroupClient()
{
    SyncedLaunchers synced(nullptr);

    ProbeClient first(QStringLiteral("L1"), QStringLiteral("g1"));
    ProbeClient second(QStringLiteral("L1"), QStringLiteral("g1"));
    synced.addAbilityClient(&first);
    synced.addAbilityClient(&second);

    synced.addLauncher(QStringLiteral("L1"), 0, Latte::Types::LayoutLaunchers,
                       QStringLiteral("g1"), QStringLiteral("app.desktop"));

    QCOMPARE(first.receivedLaunchers, QStringList({QStringLiteral("app.desktop")}));
    QCOMPARE(second.receivedLaunchers, QStringList({QStringLiteral("app.desktop")}));
}

void SyncedLauncherClientTest::broadcastFiltersByLayoutAndGroup()
{
    SyncedLaunchers synced(nullptr);

    ProbeClient matched(QStringLiteral("L1"), QStringLiteral("g1"));
    ProbeClient otherGroup(QStringLiteral("L1"), QStringLiteral("g2"));
    ProbeClient otherLayout(QStringLiteral("L2"), QStringLiteral("g1"));
    synced.addAbilityClient(&matched);
    synced.addAbilityClient(&otherGroup);
    synced.addAbilityClient(&otherLayout);

    // Layout-scoped launchers must stay inside their layout AND group: a
    // wrong match syncs a launcher into an unrelated dock.
    synced.addLauncher(QStringLiteral("L1"), 0, Latte::Types::LayoutLaunchers,
                       QStringLiteral("g1"), QStringLiteral("scoped.desktop"));

    QCOMPARE(matched.receivedLaunchers, QStringList({QStringLiteral("scoped.desktop")}));
    QVERIFY2(otherGroup.receivedLaunchers.isEmpty(), "a launcher leaked into another synced group");
    QVERIFY2(otherLayout.receivedLaunchers.isEmpty(), "a layout-scoped launcher leaked into another layout");

    // Global launchers ignore the layout scope: the same group in another
    // layout receives them too, the foreign group still never does.
    synced.addLauncher(QStringLiteral("L1"), 0, Latte::Types::GlobalLaunchers,
                       QStringLiteral("g1"), QStringLiteral("global.desktop"));

    QCOMPARE(matched.receivedLaunchers.last(), QStringLiteral("global.desktop"));
    QCOMPARE(otherLayout.receivedLaunchers, QStringList({QStringLiteral("global.desktop")}));
    QVERIFY2(otherGroup.receivedLaunchers.isEmpty(), "a global launcher leaked into another synced group");
}

void SyncedLauncherClientTest::destroyedClientLeavesTheRegistry()
{
    SyncedLaunchers synced(nullptr);

    auto *dying = new ProbeClient(QStringLiteral("L1"), QStringLiteral("g1"));
    ProbeClient surviving(QStringLiteral("L1"), QStringLiteral("g1"));
    synced.addAbilityClient(dying);
    synced.addAbilityClient(&surviving);

    // Destruction must run the real destroyed() handler. The next broadcast
    // walks the registry calling metaObject() on every entry, so a stale
    // pointer here is exactly the freed-memory read that crashed live.
    delete dying;

    synced.addLauncher(QStringLiteral("L1"), 0, Latte::Types::LayoutLaunchers,
                       QStringLiteral("g1"), QStringLiteral("after.desktop"));

    QCOMPARE(surviving.receivedLaunchers, QStringList({QStringLiteral("after.desktop")}));
}

void SyncedLauncherClientTest::explicitlyRemovedClientReceivesNothing()
{
    SyncedLaunchers synced(nullptr);

    ProbeClient removed(QStringLiteral("L1"), QStringLiteral("g1"));
    ProbeClient kept(QStringLiteral("L1"), QStringLiteral("g1"));
    synced.addAbilityClient(&removed);
    synced.addAbilityClient(&kept);

    // The ability-teardown path: unregistered clients stop receiving, and
    // their later destruction must not disturb the registry (the handler was
    // disconnected on removal).
    synced.removeAbilityClient(&removed);

    synced.addLauncher(QStringLiteral("L1"), 0, Latte::Types::LayoutLaunchers,
                       QStringLiteral("g1"), QStringLiteral("only-kept.desktop"));

    QVERIFY2(removed.receivedLaunchers.isEmpty(), "an unregistered client still received a broadcast");
    QCOMPARE(kept.receivedLaunchers, QStringList({QStringLiteral("only-kept.desktop")}));
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    SyncedLauncherClientTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "syncedlaunchersclienttest.moc"
