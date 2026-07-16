/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Upstream contract (tests/contracts/README.md): libplasma askDestroy()
//! signal ordering at the pinned v6.6.5, earned in 71b0d75a where the System
//! Tray's ghost slot lived exactly as long as the undo window.
//!
//! askDestroy() - the widget-removal entry point behind every applet's
//! "remove" action - marks the applet destroyed() and keeps the OBJECT alive
//! for the undo notification (60s fallback timer). What it emits depends on
//! the applet's type, and Latte's containment LayoutManager keys its
//! two-phase parking on exactly this timeline:
//!
//! - plain applet: destroyedChanged(true), then Containment::appletRemoved
//!   fires IMMEDIATELY (synchronously inside askDestroy), and fires a SECOND
//!   time when the object really dies (~Applet emits appletDeleted, which
//!   the containment re-broadcasts as appletRemoved). removeAppletItem must
//!   treat destroyed()==true as "park, undo still possible" both times.
//!
//! - containment-type applet (the System Tray is a Plasma::Containment of
//!   type CustomEmbedded): askDestroy() guards its immediate emit AND the
//!   applets() list removal with !isContainment(), so NO appletRemoved fires
//!   and the applet stays in Containment::applets() for the whole undo
//!   window. Its ONLY appletRemoved arrives at object death. Latte parks on
//!   destroyedChanged(true) instead of waiting for a signal that will not
//!   come (the AppletItem.qml destruction watcher).
//!
//! The undo window's end is simulated with a direct delete: the real timer
//! path (AppletPrivate::cleanUpAndDelete) ends in deleteLater() on the same
//! object, and the appletRemoved-at-death emission under test lives in
//! ~Applet -> appletDeleted -> ContainmentPrivate::appletDeleted, which both
//! paths share.
//!
//! Constructed offscreen: Plasma::Corona, Plasma::Containment and
//! Plasma::Applet all have public constructors; the applet id rides in
//! args[1]. The Corona is not optional decoration: Applet::isContainment()
//! is true only for a Containment whose parent IS a Corona (or one of type
//! CustomEmbedded), and both the askDestroy() emit guard and
//! Containment::init() key off it. askDestroy() is a Q_PRIVATE_SLOT,
//! reachable through the metaobject exactly as the remove QAction reaches
//! it.

// Qt
#include <QJsonObject>
#include <QPointer>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QtTest>

// KDE
#include <KPluginMetaData>

// Plasma
#include <Plasma/Applet>
#include <Plasma/Containment>
#include <Plasma/Corona>

//! Corona's one pure virtual is screenGeometry(); everything else works
//! offscreen as-is
class TestCorona : public Plasma::Corona
{
    Q_OBJECT

public:
    using Plasma::Corona::Corona;

    QRect screenGeometry(int id) const override
    {
        Q_UNUSED(id);
        return QRect(0, 0, 1920, 1080);
    }
};

class AskDestroySignalOrderingTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void plainAppletRemovedImmediatelyAndAgainAtDeath();
    void containmentTypeAppletRemovedOnlyAtDeath();

private:
    Plasma::Containment *createHostContainment(Plasma::Corona *corona);
    static void startApplet(Plasma::Applet *applet);
    static bool invokeAskDestroy(Plasma::Applet *applet);
};

void AskDestroySignalOrderingTest::initTestCase()
{
    //! keep every config write in throwaway test paths
    QStandardPaths::setTestModeEnabled(true);

    //! askDestroy() posts a real "Widget Removed" KNotification. Point the
    //! session bus somewhere unconnectable BEFORE anything touches DBus, so
    //! a ctest run never pops notifications on the developer's desktop and
    //! the test cannot depend on a session bus being present.
    qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/dev/null");
}

Plasma::Containment *AskDestroySignalOrderingTest::createHostContainment(Plasma::Corona *corona)
{
    //! valid metadata is required: Containment derives its type from
    //! X-Plasma-ContainmentType at construction. Panel matches Latte's dock
    //! containment. The Corona parent is what makes isContainment() true.
    QJsonObject json{
        {QStringLiteral("KPlugin"),
         QJsonObject{{QStringLiteral("Id"), QStringLiteral("test.latte.hostcontainment")}, {QStringLiteral("Name"), QStringLiteral("Host Containment")}}},
        {QStringLiteral("X-Plasma-ContainmentType"), QStringLiteral("Panel")},
    };
    KPluginMetaData md(json, QString());

    auto *host = new Plasma::Containment(corona, md, {QVariant(), QVariant(1)});
    startApplet(host);
    return host;
}

void AskDestroySignalOrderingTest::startApplet(Plasma::Applet *applet)
{
    //! askDestroy() refuses to run before the applet is started; the corona
    //! normally delivers this constraint at startup
    applet->updateConstraints(Plasma::Applet::StartupCompletedConstraint);

    //! setDestroyed() writes the "transient" entry through the lazily
    //! created main config group without materializing it first; in a real
    //! shell the restore path has always done that by removal time. Touch
    //! config() once so the test reaches askDestroy in the same state.
    applet->config();
}

bool AskDestroySignalOrderingTest::invokeAskDestroy(Plasma::Applet *applet)
{
    return QMetaObject::invokeMethod(applet, "askDestroy", Qt::DirectConnection);
}

void AskDestroySignalOrderingTest::plainAppletRemovedImmediatelyAndAgainAtDeath()
{
    TestCorona corona;
    Plasma::Containment *host = createHostContainment(&corona);
    QVERIFY(host->isContainment());

    //! invalid metadata is the plain-applet shape with the least machinery;
    //! id rides in args[1]
    auto *applet = new Plasma::Applet(host, KPluginMetaData(), {QVariant(), QVariant(7)});
    host->addApplet(applet);
    startApplet(applet);

    QVERIFY(host->applets().contains(applet));
    QVERIFY(!applet->destroyed());

    QSignalSpy removedSpy(host, &Plasma::Containment::appletRemoved);
    QSignalSpy destroyedChangedSpy(applet, &Plasma::Applet::destroyedChanged);
    QPointer<Plasma::Applet> alive(applet);

    QVERIFY(invokeAskDestroy(applet));

    QVERIFY2(applet->destroyed(), "askDestroy must mark the applet destroyed() for the undo window");
    QCOMPARE(destroyedChangedSpy.count(), 1);
    QCOMPARE(destroyedChangedSpy.at(0).at(0).toBool(), true);

    QVERIFY2(removedSpy.count() == 1, "plain applets must get appletRemoved IMMEDIATELY (synchronously) from askDestroy - "
                                       "LayoutManager::removeAppletItem parks on this call");
    QVERIFY2(!host->applets().contains(applet), "askDestroy prunes plain applets from Containment::applets() immediately");
    QVERIFY2(!alive.isNull(), "the applet OBJECT must survive askDestroy: it is the undo window");

    //! the undo window ends: the object dies, and the containment
    //! re-broadcasts appletRemoved a SECOND time from appletDeleted.
    //! LayoutManager::removeAppletItem runs again here and must see
    //! destroyed() still true (it no-ops on the already-parked id).
    delete applet;
    QVERIFY2(removedSpy.count() == 2, "appletRemoved must fire again at object death; if this stops, the parked-entry "
                                       "pruning in setAppletInScheduledDestruction is the only cleanup left");

    delete host;
}

void AskDestroySignalOrderingTest::containmentTypeAppletRemovedOnlyAtDeath()
{
    TestCorona corona;
    Plasma::Containment *host = createHostContainment(&corona);

    //! the System Tray shape: an applet that IS a Plasma::Containment of
    //! type CustomEmbedded
    QJsonObject json{
        {QStringLiteral("KPlugin"),
         QJsonObject{{QStringLiteral("Id"), QStringLiteral("test.latte.embeddedtray")}, {QStringLiteral("Name"), QStringLiteral("Embedded Tray")}}},
        {QStringLiteral("X-Plasma-ContainmentType"), QStringLiteral("CustomEmbedded")},
    };
    KPluginMetaData md(json, QString());

    auto *tray = new Plasma::Containment(host, md, {QVariant(), QVariant(8)});
    host->addApplet(tray);
    startApplet(tray);

    QVERIFY(tray->isContainment());
    QVERIFY(host->applets().contains(tray));

    QSignalSpy removedSpy(host, &Plasma::Containment::appletRemoved);
    QSignalSpy destroyedChangedSpy(tray, &Plasma::Applet::destroyedChanged);
    QPointer<Plasma::Applet> alive(tray);

    QVERIFY(invokeAskDestroy(tray));

    //! destroyedChanged(true) is the ONLY instant signal for this class -
    //! the AppletItem.qml destruction watcher parks on it (71b0d75a)
    QVERIFY(tray->destroyed());
    QCOMPARE(destroyedChangedSpy.count(), 1);
    QCOMPARE(destroyedChangedSpy.at(0).at(0).toBool(), true);

    QVERIFY2(removedSpy.count() == 0, "containment-type applets must NOT get appletRemoved from askDestroy "
                                       "(!isContainment() guard) - if one arrives now, the 71b0d75a parking "
                                       "machinery double-handles the removal");
    QVERIFY2(host->applets().contains(tray), "containment-type applets stay in Containment::applets() for the whole undo window");
    QVERIFY(!alive.isNull());

    //! only object death delivers their appletRemoved
    delete tray;
    QVERIFY2(removedSpy.count() == 1, "the containment-type applet's ONLY appletRemoved must arrive at object death; "
                                       "removeAppletItem finalizes nothing before this");

    delete host;
}

QTEST_MAIN(AskDestroySignalOrderingTest)

#include "askdestroysignalorderingtest.moc"
