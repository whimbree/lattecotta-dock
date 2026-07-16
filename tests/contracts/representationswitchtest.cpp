/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Upstream contract (tests/contracts/README.md): AppletQuickItem's
//! representation-switch behavior at the pinned libplasma v6.6.5, earned in
//! 9ea29eaa (representation churn crash and stale-parent walks) and
//! 1aa5238c (release must not hide - the comic's empty slot).
//!
//! compactRepresentationCheck() runs on every geometry change; when the
//! item's size crosses switchWidth/switchHeight it swaps representations
//! INLINE. The behaviors Latte's shell CompactApplet.qml relies on:
//!
//! - the switch to full unwires the expander by writing NULL into its
//!   compactRepresentation AND fullRepresentation properties and hides it;
//!   Latte's release path runs from exactly those property changes, so a
//!   null incoming value is a normal, expected state, not an error.
//!
//! - the full representation item is a CACHED object: the same instance is
//!   re-parented into the AppletQuickItem for the inline switch and handed
//!   back to the expander for the popup flow. It is never recreated per
//!   switch.
//!
//! - re-parenting is ALL the switch does to a re-used item: setVisible(true)
//!   is never called on it. Anything that hid the item earlier (the
//!   pre-1aa5238c release did) leaves it invisible forever.
//!
//! - the un-switch back to compact releases the full representation by
//!   detaching it to a NULL parent, touching neither visibility nor the
//!   object itself. This is the release shape Latte mirrors.
//!
//! Constructed offscreen through the real creation path,
//! AppletQuickItem::itemForApplet(): a concrete Corona parents a host
//! containment (isContainment() demands a Corona parent), the child applet's
//! main.qml and the host's expander qml ride the qrc applet path libplasma
//! checks before KPackage (Applet::qrcPath()), so no package structure
//! plugins are needed. The QML import path for org.kde.plasma.plasmoid comes
//! from the devShell's pinned LATTE_QML_MODULE_PATH, the same source the
//! QML harnesses use - session paths must not leak in (import path
//! doctrine, latte-build-env).

// Qt
#include <QJsonObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QtTest>

// KDE
#include <KPluginMetaData>

// Plasma
#include <Plasma/Applet>
#include <Plasma/Containment>
#include <Plasma/Corona>
#include <PlasmaQuick/AppletQuickItem>

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

class RepresentationSwitchTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void inlineSwitchContract();

private:
    static QQuickItem *findProbeExpander(QQuickItem *item);
};

void RepresentationSwitchTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);

    //! the SharedQmlEngine inside itemForApplet resolves imports through the
    //! standard engine import path; hand it the devShell's pinned module set
    //! and nothing else. Without this the plasmoid module resolves from
    //! nowhere (mkShell runs no Qt env hooks) or, worse, from the desktop
    //! session's differently pinned tree.
    const QByteArray pinnedModules = qgetenv("LATTE_QML_MODULE_PATH");
    QVERIFY2(!pinnedModules.isEmpty(),
             "LATTE_QML_MODULE_PATH is not set - this contract test must run inside the flake devShell (nix develop), like the QML harnesses");
    qputenv("QML2_IMPORT_PATH", pinnedModules);
}

QQuickItem *RepresentationSwitchTest::findProbeExpander(QQuickItem *item)
{
    const auto children = item->childItems();
    for (QQuickItem *child : children) {
        if (child->objectName() == QLatin1String("probeExpander")) {
            return child;
        }
    }
    return nullptr;
}

void RepresentationSwitchTest::inlineSwitchContract()
{
    TestCorona corona;

    //! host containment: its pluginId anchors the qrc path libplasma reads
    //! the expander from (Containment::compactApplet() ->
    //! :/qt/qml/plasma/applet/test/host/CompactApplet.qml)
    QJsonObject hostJson{
        {QStringLiteral("KPlugin"), QJsonObject{{QStringLiteral("Id"), QStringLiteral("test.host")}, {QStringLiteral("Name"), QStringLiteral("Host")}}},
        {QStringLiteral("X-Plasma-ContainmentType"), QStringLiteral("Panel")},
    };
    auto *host = new Plasma::Containment(&corona, KPluginMetaData(hostJson, QString()), {QVariant(), QVariant(1)});
    QVERIFY(host->isContainment());

    //! child applet: pluginId anchors the qrc main.qml with the
    //! representations and switch thresholds
    QJsonObject appletJson{
        {QStringLiteral("KPlugin"), QJsonObject{{QStringLiteral("Id"), QStringLiteral("test.repswitch")}, {QStringLiteral("Name"), QStringLiteral("RepSwitch")}}},
    };
    auto *applet = new Plasma::Applet(host, KPluginMetaData(appletJson, QString()), {QVariant(), QVariant(7)});
    host->addApplet(applet);

    //! the REAL creation path: SharedQmlEngine + AppletContext + init()
    PlasmaQuick::AppletQuickItem *item = PlasmaQuick::AppletQuickItem::itemForApplet(applet);
    QVERIFY2(item, "itemForApplet must build the PlasmoidItem from the qrc main script");
    QCOMPARE(item->applet(), applet);

    //! host the item in a shown window so effective visibility is readable;
    //! parentless items always read invisible
    QQuickWindow window;
    window.resize(400, 400);
    item->setParentItem(window.contentItem());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    //! --- compact state: size below the switch thresholds ---
    item->setSize(QSizeF(50, 50));

    QQuickItem *compactRep = item->compactRepresentationItem();
    QVERIFY2(compactRep, "sizing below the thresholds must build the compact representation");
    QCOMPARE(compactRep->objectName(), QLatin1String("testCompactRep"));

    QQuickItem *expander = findProbeExpander(item);
    QVERIFY2(expander, "the expander must be instantiated from the host containment's compactApplet qml");
    QVERIFY(expander->isVisible());
    QCOMPARE(expander->property("compactRepresentation").value<QQuickItem *>(), compactRep);
    QCOMPARE(expander->property("fullRepresentation").value<QQuickItem *>(), nullptr);
    QVERIFY(!item->isExpanded());

    //! --- popup flow: expanding while compact wires the full rep into the
    //! expander (preloadForExpansion) - the moment Latte's
    //! onFullRepresentationChanged adopts it into the popup dialog ---
    item->setExpanded(true);
    QQuickItem *fullRep = item->fullRepresentationItem();
    QVERIFY2(fullRep, "expanding must build the full representation");
    QCOMPARE(fullRep->objectName(), QLatin1String("testFullRep"));
    QCOMPARE(expander->property("fullRepresentation").value<QQuickItem *>(), fullRep);

    //! --- inline switch: the item grows past switchWidth/switchHeight (the
    //! comic on every parabolic zoom) ---
    item->setSize(QSizeF(150, 150));

    QVERIFY2(expander->property("fullRepresentation").value<QQuickItem *>() == nullptr,
             "the inline switch must unwire the expander's fullRepresentation with NULL - "
             "Latte's release path runs from this write and must treat null as normal");
    QVERIFY2(expander->property("compactRepresentation").value<QQuickItem *>() == nullptr,
             "the inline switch must unwire the expander's compactRepresentation with NULL");
    QVERIFY2(!expander->isVisible(), "the inline switch must hide the expander");

    QCOMPARE(item->fullRepresentationItem(), fullRep);
    QCOMPARE(fullRep->parentItem(), item);
    QVERIFY2(fullRep->isVisible(), "a never-hidden full rep renders inline after the switch");

    //! --- un-switch: shrinking back releases the full rep the way
    //! AppletQuickItem itself releases - detach to a null parent, leave
    //! visibility alone (the release shape 1aa5238c mirrors) ---
    item->setSize(QSizeF(50, 50));

    QCOMPARE(item->fullRepresentationItem(), fullRep);
    QVERIFY2(fullRep->parentItem() == nullptr,
             "the un-switch must detach the full rep to a NULL parent - the same release Latte performs");
    QVERIFY(expander->isVisible());
    QCOMPARE(expander->property("compactRepresentation").value<QQuickItem *>(), compactRep);
    QVERIFY2(!item->isExpanded(), "an inline-visible full rep collapsing back must clear expanded");

    //! --- the 1aa5238c pin: a full rep hidden by ANY earlier code path is
    //! re-parented by the next inline switch WITHOUT setVisible(true); it
    //! stays invisible forever. This is why Latte's release must never hide.
    fullRep->setVisible(false);
    item->setSize(QSizeF(150, 150));

    QCOMPARE(item->fullRepresentationItem(), fullRep);
    QCOMPARE(fullRep->parentItem(), item);
    QVERIFY2(!fullRep->isVisible(),
             "the inline switch re-parented the cached item and re-showed it - if upstream now calls "
             "setVisible(true) here, the 1aa5238c hidden-release hazard is gone and the release "
             "contract in shell CompactApplet.qml can be revisited");

    delete host;
}

QTEST_MAIN(RepresentationSwitchTest)

#include "representationswitchtest.moc"
