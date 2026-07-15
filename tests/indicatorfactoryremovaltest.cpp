/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Pins Indicator::Factory's plugin-removal bookkeeping against the real
//! KDirWatch delivery path (46dc83c5): built-in indicators are never added
//! to the custom lists, so their KDirWatch removals used to feed
//! removeAt(-1), a hard assert on Qt6 where Qt5 tolerated it - exactly what
//! a restage under a running dock delivers. Also pins the id/name parallel
//! lists staying in lockstep for plugins sharing a display name; the old
//! independent name-uniqueness guard desynced them and the removal then
//! indexed past the end of the names list.
//!
//! Factory reads its search roots from QStandardPaths GenericDataLocation,
//! so XDG_DATA_HOME/XDG_DATA_DIRS are pointed at a throwaway temp dir before
//! any Factory is constructed; the host's real indicators never take part.

#include "indicator/factory.h"

// Qt
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using Latte::Indicator::Factory;

class IndicatorFactoryRemovalTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void survivesBuiltInIndicatorRemoval();
    void removesCustomIndicatorRecords();
    void keepsIdAndNameListsInLockstepForDuplicateNames();

private:
    QString indicatorsRoot() const;
    void makeIndicator(const QString &pluginId, const QString &name);
    void removeIndicator(const QString &pluginId);

    QTemporaryDir m_dataHome;
};

void IndicatorFactoryRemovalTest::initTestCase()
{
    QVERIFY(m_dataHome.isValid());

    //! both variables must point inside the temp dir so no real indicator
    //! from the host system is discovered
    qputenv("XDG_DATA_HOME", m_dataHome.path().toUtf8());
    qputenv("XDG_DATA_DIRS", m_dataHome.path().toUtf8());

    QVERIFY(QDir().mkpath(indicatorsRoot()));
}

QString IndicatorFactoryRemovalTest::indicatorsRoot() const
{
    return m_dataHome.path() + "/latte/indicators";
}

void IndicatorFactoryRemovalTest::makeIndicator(const QString &pluginId, const QString &name)
{
    //! removeIndicatorRecords() derives the plugin id from the directory
    //! name, so the directory must be named after the id (as real installed
    //! indicators are)
    const QString dir = indicatorsRoot() + "/" + pluginId;
    QVERIFY(QDir().mkpath(dir));

    QFile metadata(dir + "/metadata.json");
    QVERIFY(metadata.open(QIODevice::WriteOnly));

    const QString json = QStringLiteral(
        "{\n"
        "    \"KPlugin\": {\n"
        "        \"Id\": \"%1\",\n"
        "        \"Name\": \"%2\",\n"
        "        \"Category\": \"Latte Indicator\"\n"
        "    },\n"
        "    \"X-Latte-MainScript\": \"ui/main.qml\"\n"
        "}\n").arg(pluginId, name);

    QVERIFY(metadata.write(json.toUtf8()) > 0);
    metadata.close();
}

void IndicatorFactoryRemovalTest::removeIndicator(const QString &pluginId)
{
    QDir dir(indicatorsRoot() + "/" + pluginId);
    QVERIFY(dir.exists());
    QVERIFY(dir.removeRecursively());
}

void IndicatorFactoryRemovalTest::survivesBuiltInIndicatorRemoval()
{
    //! a built-in id discovered from disk lands in m_plugins but never in the
    //! custom lists (reload() filters the three built-in ids), which is the
    //! removeAt(-1) arm once its directory disappears - the restage scenario
    makeIndicator(QStringLiteral("org.kde.latte.default"), QStringLiteral("Latte"));
    makeIndicator(QStringLiteral("org.test.survivor"), QStringLiteral("Survivor"));

    Factory factory(this);

    QVERIFY(factory.pluginExists(QStringLiteral("org.kde.latte.default")));
    QVERIFY(factory.pluginExists(QStringLiteral("org.test.survivor")));
    QCOMPARE(factory.customPluginsCount(), 1);
    QVERIFY(!factory.customPluginIds().contains(QStringLiteral("org.kde.latte.default")));

    removeIndicator(QStringLiteral("org.kde.latte.default"));

    //! KDirWatch delivers the deletion through the event loop; before the
    //! 46dc83c5 fix this removeAt(-1)'d and the whole process died here
    QTRY_VERIFY_WITH_TIMEOUT(!factory.pluginExists(QStringLiteral("org.kde.latte.default")), 10000);

    //! the custom records must be untouched by a built-in removal
    QCOMPARE(factory.customPluginsCount(), 1);
    QVERIFY(factory.pluginExists(QStringLiteral("org.test.survivor")));

    removeIndicator(QStringLiteral("org.test.survivor"));
    QTRY_VERIFY_WITH_TIMEOUT(!factory.pluginExists(QStringLiteral("org.test.survivor")), 10000);
}

void IndicatorFactoryRemovalTest::removesCustomIndicatorRecords()
{
    makeIndicator(QStringLiteral("org.test.custom"), QStringLiteral("Custom"));

    Factory factory(this);

    QVERIFY(factory.pluginExists(QStringLiteral("org.test.custom")));
    QVERIFY(factory.customPluginIds().contains(QStringLiteral("org.test.custom")));

    QSignalSpy removedSpy(&factory, &Factory::indicatorRemoved);

    removeIndicator(QStringLiteral("org.test.custom"));

    QTRY_VERIFY_WITH_TIMEOUT(!factory.pluginExists(QStringLiteral("org.test.custom")), 10000);
    QVERIFY(!factory.customPluginIds().contains(QStringLiteral("org.test.custom")));
    QVERIFY(!factory.customPluginNames().contains(QStringLiteral("Custom")));

    //! the removal announcement rides a delayed timer (update debouncing)
    QTRY_COMPARE_WITH_TIMEOUT(removedSpy.count(), 1, 10000);
    QCOMPARE(removedSpy.constFirst().constFirst().toString(), QStringLiteral("org.test.custom"));
}

void IndicatorFactoryRemovalTest::keepsIdAndNameListsInLockstepForDuplicateNames()
{
    //! two distinct plugins may legitimately share a display name (a fork
    //! that renamed its id but kept the name); the parallel lists must both
    //! carry two entries or the removal indexes the names list out of range
    makeIndicator(QStringLiteral("org.test.alpha"), QStringLiteral("Zeta Indicator"));
    makeIndicator(QStringLiteral("org.test.beta"), QStringLiteral("Zeta Indicator"));

    Factory factory(this);

    QCOMPARE(factory.customPluginIds().count(), 2);
    QCOMPARE(factory.customPluginNames().count(), 2);

    removeIndicator(QStringLiteral("org.test.beta"));

    QTRY_VERIFY_WITH_TIMEOUT(!factory.pluginExists(QStringLiteral("org.test.beta")), 10000);
    QCOMPARE(factory.customPluginIds(), QStringList{QStringLiteral("org.test.alpha")});
    QCOMPARE(factory.customPluginNames(), QStringList{QStringLiteral("Zeta Indicator")});

    removeIndicator(QStringLiteral("org.test.alpha"));
    QTRY_VERIFY_WITH_TIMEOUT(factory.customPluginIds().isEmpty(), 10000);
    QVERIFY(factory.customPluginNames().isEmpty());
}

QTEST_MAIN(IndicatorFactoryRemovalTest)

#include "indicatorfactoryremovaltest.moc"
