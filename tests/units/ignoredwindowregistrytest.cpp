/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../../app/wm/ignoredwindowregistry.h"

#include <QtTest>

using Latte::WindowSystem::IgnoredWindowRegistry;
using Latte::WindowSystem::WindowId;

class IgnoredWindowRegistryTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void lastOwnerControlsTrackability();
    void duplicateAndForeignOwnerChangesAreNoOps();
    void compositorRemovalClearsEveryOwner();
};

void IgnoredWindowRegistryTest::lastOwnerControlsTrackability()
{
    IgnoredWindowRegistry registry;
    const auto window = WindowId::fromWaylandUuid(QByteArrayLiteral("dock-window"));

    QCOMPARE(registry.registerOwner(window, 1), IgnoredWindowRegistry::Change::BecameIgnored);
    QCOMPARE(registry.registerOwner(window, 2), IgnoredWindowRegistry::Change::None);
    QVERIFY(registry.contains(window));

    QCOMPARE(registry.unregisterOwner(window, 1), IgnoredWindowRegistry::Change::None);
    QVERIFY(registry.contains(window));

    QCOMPARE(registry.unregisterOwner(window, 2), IgnoredWindowRegistry::Change::BecameTrackable);
    QVERIFY(!registry.contains(window));
    QVERIFY(registry.windows().isEmpty());
}

void IgnoredWindowRegistryTest::duplicateAndForeignOwnerChangesAreNoOps()
{
    IgnoredWindowRegistry registry;
    const auto window = WindowId::fromWaylandUuid(QByteArrayLiteral("config-window"));

    QCOMPARE(registry.registerOwner(window, 5), IgnoredWindowRegistry::Change::BecameIgnored);
    QCOMPARE(registry.registerOwner(window, 5), IgnoredWindowRegistry::Change::None);
    QCOMPARE(registry.unregisterOwner(window, 7), IgnoredWindowRegistry::Change::None);
    QCOMPARE(registry.windows(), QList<WindowId>{window});

    QCOMPARE(registry.registerOwner(WindowId{}, 5), IgnoredWindowRegistry::Change::None);
    QCOMPARE(registry.registerOwner(window, 0), IgnoredWindowRegistry::Change::None);
}

void IgnoredWindowRegistryTest::compositorRemovalClearsEveryOwner()
{
    IgnoredWindowRegistry registry;
    const auto window = WindowId::fromWaylandUuid(QByteArrayLiteral("removed-window"));
    static_cast<void>(registry.registerOwner(window, 1));
    static_cast<void>(registry.registerOwner(window, 2));

    QCOMPARE(registry.clear(window), IgnoredWindowRegistry::Change::BecameTrackable);
    QVERIFY(!registry.contains(window));
    QCOMPARE(registry.clear(window), IgnoredWindowRegistry::Change::None);
}

QTEST_GUILESS_MAIN(IgnoredWindowRegistryTest)

#include "ignoredwindowregistrytest.moc"
