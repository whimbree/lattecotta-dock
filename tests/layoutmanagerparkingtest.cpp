/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Behavioral regression test for the containment LayoutManager's removal
//! parking state machine (71b0d75a): a removed widget's container parks the
//! moment its applet is marked destroyed() (undo still possible), unparks in
//! place on undo, and finalizes exactly once when the applet object really
//! dies - or immediately, for direct deletions that never had an undo
//! window (synced applets).
//!
//! The test compiles the REAL plugin sources (containment/plugin/
//! layoutmanager.cpp) and drives them the way the containment QML does:
//! containers created through the invokable harness functions
//! (parkingharness/Harness.qml plays main.qml's role), park/unpark driven
//! from Plasma::Applet::destroyedChanged and Containment::appletRemoved
//! exactly as AppletItem.qml and main.qml wire them, and the applets are
//! real Plasma::Applets carried by real AppletQuickItems built through
//! itemForApplet() - LayoutManager resolves containers by casting their
//! applet property to PlasmaQuick::AppletQuickItem and reading
//! applet()->id(), so nothing lighter would exercise the lookup.
//!
//! The plasmoid handed to LayoutManager is a mock carrying a REAL
//! KConfigPropertyMap over a throwaway KConfigLoader: the map is where
//! save() publishes appletOrder, which is how finalization is observable
//! (docs/TESTING.md: assert observable effects).

// Qt
#include <QBuffer>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

// KDE
#include <KConfigLoader>
#include <KConfigPropertyMap>
#include <KPluginMetaData>

// Plasma
#include <Plasma/Applet>
#include <Plasma/Containment>
#include <Plasma/Corona>
#include <PlasmaQuick/AppletQuickItem>

// the class under test, compiled from the real plugin sources
#include <plugin/layoutmanager.h>

namespace {

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

//! the containment plasmoid stand-in: LayoutManager reads exactly the
//! configuration property (must resolve to a real KConfigPropertyMap - the
//! single-loader chain from c3d15966) and, on drop paths, formFactor
class MockPlasmoid : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *configuration READ configuration CONSTANT)
    Q_PROPERTY(int formFactor READ formFactor CONSTANT)

public:
    explicit MockPlasmoid(KConfigPropertyMap *map, QObject *parent = nullptr)
        : QObject(parent)
        , m_map(map)
    {
    }

    QObject *configuration() const
    {
        return m_map;
    }

    int formFactor() const
    {
        return 0;
    }

private:
    KConfigPropertyMap *m_map;
};

//! plain concatenated literals, not a raw string: moc's lexer silently
//! stops parsing the translation unit at a raw string literal here and the
//! test class below then never gets its metaobject (undefined vtable at
//! link time)
constexpr const char *s_layoutSchema =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<kcfg xmlns=\"http://www.kde.org/standards/kcfg/1.0\">\n"
    "  <kcfgfile name=\"\"/>\n"
    "  <group name=\"General\">\n"
    "    <entry name=\"appletOrder\" type=\"String\"><default></default></entry>\n"
    "    <entry name=\"alignment\" type=\"Int\"><default>0</default></entry>\n"
    "    <entry name=\"splitterPosition\" type=\"Int\"><default>-1</default></entry>\n"
    "    <entry name=\"splitterPosition2\" type=\"Int\"><default>-1</default></entry>\n"
    "    <entry name=\"lockedZoomApplets\" type=\"String\"><default></default></entry>\n"
    "    <entry name=\"userBlocksColorizingApplets\" type=\"String\"><default></default></entry>\n"
    "  </group>\n"
    "</kcfg>\n";

} // namespace

class LayoutManagerParkingTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void parkingLifecycle();
    void parkingWithoutContainerRefusesLoudly();

private:
    struct Fixture {
        QQmlEngine *engine{nullptr};
        QQuickItem *rootItem{nullptr};
        QQuickItem *mainLayout{nullptr};
        TestCorona *corona{nullptr};
        Plasma::Containment *host{nullptr};
        QTemporaryDir *configDir{nullptr};
        KConfigPropertyMap *configMap{nullptr};
        MockPlasmoid *plasmoid{nullptr};
        Latte::Containment::LayoutManager *manager{nullptr};
    };

    void buildFixture(Fixture &f, QObject *parent);
    Plasma::Applet *createRealApplet(Fixture &f, int id);
    static QQuickItem *containerFor(QQuickItem *layout, QQuickItem *appletGraphic);
};

void LayoutManagerParkingTest::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);

    //! same two environment rules as the contract suite: imports resolve
    //! ONLY from the devShell's pinned module set, and the session bus is
    //! unreachable so askDestroy's KNotification cannot hit the desktop
    const QByteArray pinnedModules = qgetenv("LATTE_QML_MODULE_PATH");
    QVERIFY2(!pinnedModules.isEmpty(),
             "LATTE_QML_MODULE_PATH is not set - this test must run inside the flake devShell (nix develop), like the QML harnesses");
    qputenv("QML2_IMPORT_PATH", pinnedModules);
    qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/dev/null");
}

void LayoutManagerParkingTest::buildFixture(Fixture &f, QObject *parent)
{
    f.engine = new QQmlEngine(parent);
    QQmlComponent harness(f.engine, QUrl(QStringLiteral("qrc:/parkingtest/Harness.qml")));
    f.rootItem = qobject_cast<QQuickItem *>(harness.create());
    QVERIFY2(f.rootItem, qPrintable(harness.errorString()));
    f.rootItem->setParent(parent);

    f.mainLayout = f.rootItem->findChild<QQuickItem *>(QStringLiteral("mainLayout"));
    auto *startLayout = f.rootItem->findChild<QQuickItem *>(QStringLiteral("startLayout"));
    auto *endLayout = f.rootItem->findChild<QQuickItem *>(QStringLiteral("endLayout"));
    auto *dndSpacer = f.rootItem->findChild<QQuickItem *>(QStringLiteral("dndSpacer"));
    QVERIFY(f.mainLayout && startLayout && endLayout && dndSpacer);

    f.corona = new TestCorona(parent);

    QJsonObject hostJson{
        {QStringLiteral("KPlugin"), QJsonObject{{QStringLiteral("Id"), QStringLiteral("test.host")}, {QStringLiteral("Name"), QStringLiteral("Host")}}},
        {QStringLiteral("X-Plasma-ContainmentType"), QStringLiteral("Panel")},
    };
    f.host = new Plasma::Containment(f.corona, KPluginMetaData(hostJson, QString()), {QVariant(), QVariant(1)});
    QVERIFY(f.host->isContainment());

    f.configDir = new QTemporaryDir();
    QVERIFY(f.configDir->isValid());
    QBuffer schema;
    schema.setData(QByteArray(s_layoutSchema));
    QVERIFY(schema.open(QIODevice::ReadOnly));
    auto *loader = new KConfigLoader(f.configDir->filePath(QStringLiteral("layoutrc")), &schema, parent);
    f.configMap = new KConfigPropertyMap(loader, parent);
    f.plasmoid = new MockPlasmoid(f.configMap, parent);

    f.manager = new Latte::Containment::LayoutManager(parent);
    f.manager->setPlasmoid(f.plasmoid);
    f.manager->setRootItem(f.rootItem);
    f.manager->setMainLayout(f.mainLayout);
    f.manager->setStartLayout(startLayout);
    f.manager->setEndLayout(endLayout);
    f.manager->setDndSpacer(dndSpacer);
}

Plasma::Applet *LayoutManagerParkingTest::createRealApplet(Fixture &f, int id)
{
    QJsonObject json{
        {QStringLiteral("KPlugin"), QJsonObject{{QStringLiteral("Id"), QStringLiteral("test.parkapplet")}, {QStringLiteral("Name"), QStringLiteral("Park Applet")}}},
    };
    auto *applet = new Plasma::Applet(f.host, KPluginMetaData(json, QString()), {QVariant(), QVariant(id)});
    f.host->addApplet(applet);

    //! started + materialized config, the same preconditions askDestroy has
    //! in a live shell (see askdestroysignalorderingtest.cpp)
    applet->updateConstraints(Plasma::Applet::StartupCompletedConstraint);
    applet->config();

    //! the real graphic item; LayoutManager's container lookup casts to
    //! AppletQuickItem and reads applet()->id() through it
    PlasmaQuick::AppletQuickItem *item = PlasmaQuick::AppletQuickItem::itemForApplet(applet);
    if (!item) {
        return nullptr;
    }

    return applet;
}

QQuickItem *LayoutManagerParkingTest::containerFor(QQuickItem *layout, QQuickItem *appletGraphic)
{
    const auto children = layout->childItems();
    for (QQuickItem *child : children) {
        if (child->property("applet").value<QQuickItem *>() == appletGraphic) {
            return child;
        }
    }
    return nullptr;
}

void LayoutManagerParkingTest::parkingLifecycle()
{
    QObject arena; //! owns the fixture pieces; destroyed (in order) at test end
    Fixture f;
    buildFixture(f, &arena);
    auto *lm = f.manager;

    Plasma::Applet *a1 = createRealApplet(f, 7);
    Plasma::Applet *a2 = createRealApplet(f, 8);
    QVERIFY(a1 && a2);
    QQuickItem *g1 = PlasmaQuick::AppletQuickItem::itemForApplet(a1);
    QQuickItem *g2 = PlasmaQuick::AppletQuickItem::itemForApplet(a2);

    //! --- baseline: both applets land as containers in the main layout ---
    lm->addAppletItem(a1, -1, -1);
    lm->addAppletItem(a2, -1, -1);

    QQuickItem *c1 = containerFor(f.mainLayout, g1);
    QQuickItem *c2 = containerFor(f.mainLayout, g2);
    QVERIFY(c1 && c2);
    QCOMPARE(f.mainLayout->childItems().count(), 2);
    QCOMPARE(lm->appletOrder(), (QList<int>{7, 8}));
    QCOMPARE(f.configMap->value(QStringLiteral("appletOrder")).toString(), QStringLiteral("7;8"));

    QSignalSpy parkedSpy(lm, &Latte::Containment::LayoutManager::appletsInScheduledDestructionChanged);

    //! --- undo arriving as appletAdded while still parked: libplasma can
    //! re-add before the destroyedChanged(false) watcher unparks. The
    //! parked container must be re-initialized in place, never duplicated.
    lm->setAppletInScheduledDestruction(8, true);
    QCOMPARE(lm->appletsInScheduledDestruction(), (QList<int>{8}));
    QCOMPARE(parkedSpy.count(), 1);

    lm->addAppletItem(a2, -1, -1);
    QVERIFY2(lm->appletsInScheduledDestruction().isEmpty(), "re-adding a parked, not-destroyed applet must unpark it");
    QCOMPARE(parkedSpy.count(), 2);
    QCOMPARE(f.mainLayout->childItems().count(), 2);
    QCOMPARE(containerFor(f.mainLayout, g2), c2);
    QCOMPARE(c2->property("initCount").toInt(), 1);
    parkedSpy.clear();

    //! --- the real removal chain: wire LayoutManager to the applet exactly
    //! as AppletItem.qml and main.qml do, then drive libplasma's askDestroy
    QMetaObject::Connection destroyedWatch = connect(a1, &Plasma::Applet::destroyedChanged, lm, [lm, a1](bool destroyed) {
        lm->setAppletInScheduledDestruction(a1->id(), destroyed);
    });
    QMetaObject::Connection removedWatch = connect(f.host, &Plasma::Containment::appletRemoved, lm, [lm](Plasma::Applet *applet) {
        lm->removeAppletItem(applet);
    });

    QVERIFY(QMetaObject::invokeMethod(a1, "askDestroy", Qt::DirectConnection));

    QVERIFY(a1->destroyed());
    QCOMPARE(lm->appletsInScheduledDestruction(), (QList<int>{7}));
    QVERIFY2(parkedSpy.count() == 1,
             "askDestroy delivers both destroyedChanged(true) and appletRemoved; parking must happen exactly once");
    QCOMPARE(containerFor(f.mainLayout, g1), c1);
    QVERIFY2(c1->parentItem() == f.mainLayout, "parking must keep the container in the layout for undo-in-place; hiding is QML's isScheduledForDestruction binding");
    QCOMPARE(lm->appletOrder(), (QList<int>{7, 8}));
    parkedSpy.clear();

    //! --- undo: destroyedChanged(false) unparks, then libplasma re-emits
    //! appletAdded; the still-present container must not be duplicated
    lm->setAppletInScheduledDestruction(7, false);
    QVERIFY(lm->appletsInScheduledDestruction().isEmpty());
    QCOMPARE(parkedSpy.count(), 1);

    lm->addAppletItem(a1, -1, -1);
    QCOMPARE(f.mainLayout->childItems().count(), 2);
    QCOMPARE(containerFor(f.mainLayout, g1), c1);
    parkedSpy.clear();

    //! --- the undo window ends: the container self-destructs (the
    //! AppletItem.onAppletChanged path); the parking entry must prune
    //! itself by identity and persist the layout without the applet
    lm->setAppletInScheduledDestruction(7, true);
    QCOMPARE(lm->appletsInScheduledDestruction(), (QList<int>{7}));
    parkedSpy.clear();

    delete c1;
    QVERIFY2(lm->appletsInScheduledDestruction().isEmpty(), "a dying parked container must prune its own parking entry");
    QCOMPARE(parkedSpy.count(), 1);
    QCOMPARE(lm->appletOrder(), (QList<int>{8}));
    QCOMPARE(f.configMap->value(QStringLiteral("appletOrder")).toString(), QStringLiteral("8"));

    //! --- direct deletion without an undo window (synced applets):
    //! destroyed()==false finalizes immediately
    QVERIFY(!a2->destroyed());
    lm->removeAppletItem(a2);
    QCOMPARE(f.mainLayout->childItems().count(), 0);
    QCOMPARE(c2->parentItem(), f.rootItem);
    QVERIFY(lm->appletOrder().isEmpty());
    QCOMPARE(f.configMap->value(QStringLiteral("appletOrder")).toString(), QString());

    //! detach the QML-mirror wiring before teardown deletes the applets:
    //! live, containers outlive their applets' death signals; in the
    //! fixture teardown order they would not
    disconnect(destroyedWatch);
    disconnect(removedWatch);
    delete f.host;
}

void LayoutManagerParkingTest::parkingWithoutContainerRefusesLoudly()
{
    QObject arena;
    Fixture f;
    buildFixture(f, &arena);
    auto *lm = f.manager;

    QSignalSpy parkedSpy(lm, &Latte::Containment::LayoutManager::appletsInScheduledDestructionChanged);

    //! no container exists for this id: the manager must refuse the park
    //! and say so (silently parking nothing was the pre-71b0d75a shape of
    //! exactly this kind of gap)
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("no container exists")));
    lm->setAppletInScheduledDestruction(99, true);

    QVERIFY(lm->appletsInScheduledDestruction().isEmpty());
    QCOMPARE(parkedSpy.count(), 0);

    delete f.host;
}

QTEST_MAIN(LayoutManagerParkingTest)

#include "layoutmanagerparkingtest.moc"
