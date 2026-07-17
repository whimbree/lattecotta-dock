/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Pins the vendored SmartLauncher::Item (plasmoid/plugin/smartlauncheritem,
// from plasma-desktop's taskmanager applet) so the periodic upstream sync
// pass has a behavioral tripwire: a synced hunk that changes the default
// badge state or the launcherUrl contract fails here, not in a live dock.
//
// Transplanted from latte-dock-qt6 (tests/smartlauncheritemtest.cpp at
// 81384003, github.com/CaptSilver/latte-dock-qt6) and extended with the launcherUrlChanged signal discipline.
// The badge mutation path (count/progress/urgent) only moves on Unity
// LauncherAPI DBus traffic through the shared backend, so it stays
// live-verified; everything below is the honest headless surface.

#include <QSignalSpy>
#include <QtTest>

#include "smartlauncheritem.h"

class SmartLauncherItemTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void defaultsAreEmpty();
    void retainsLauncherUrl();
    void launcherUrlChangeEmitsOnceAndIsGuarded();
};

void SmartLauncherItemTest::defaultsAreEmpty()
{
    SmartLauncher::Item item;
    QCOMPARE(item.count(), 0);
    QCOMPARE(item.countVisible(), false);
    QCOMPARE(item.progress(), 0);
    QCOMPARE(item.progressVisible(), false);
    QCOMPARE(item.urgent(), false);
}

void SmartLauncherItemTest::retainsLauncherUrl()
{
    SmartLauncher::Item item;
    // A deliberately unresolvable launcher: no service matches, so the item stays
    // empty but must still expose the url it was handed.
    const QUrl url(QStringLiteral("applications:org.kde.latte.nonexistent-test.desktop"));
    item.setLauncherUrl(url);
    QCOMPARE(item.launcherUrl(), url);
    QCOMPARE(item.count(), 0);
}

void SmartLauncherItemTest::launcherUrlChangeEmitsOnceAndIsGuarded()
{
    SmartLauncher::Item item;
    QSignalSpy urlSpy(&item, &SmartLauncher::Item::launcherUrlChanged);

    // The tasks delegate rebinds its badge state on this signal; a re-emit on
    // an identical url would re-run the service resolution and badge clear
    // for nothing on every model refresh.
    const QUrl first(QStringLiteral("applications:org.kde.latte.first-test.desktop"));
    item.setLauncherUrl(first);
    QCOMPARE(urlSpy.count(), 1);
    QCOMPARE(urlSpy.last().first().toUrl(), first);

    item.setLauncherUrl(first);
    QCOMPARE(urlSpy.count(), 1);

    const QUrl second(QStringLiteral("applications:org.kde.latte.second-test.desktop"));
    item.setLauncherUrl(second);
    QCOMPARE(urlSpy.count(), 2);
    QCOMPARE(item.launcherUrl(), second);
}

QTEST_GUILESS_MAIN(SmartLauncherItemTest)
#include "smartlauncheritemtest.moc"
