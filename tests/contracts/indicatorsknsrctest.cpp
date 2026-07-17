/*
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Guards the indicators NewStuff config (app/latte-indicators.knsrc) against
// the KF5->KF6 KNSCore key rename. KF6 KNSCore::Installation reads only
// "KPackageStructure"; the KF5-era "KPackageType" key is dead. When
// Uncompress=kpackage is set but no structure is given, EngineBase init fails
// with ConfigFileError and the "Get New Indicators..." dialog opens with no
// providers - renders fine, silently does nothing. The structure id must stay
// "Latte/Indicator" to match the registered KPackage structure plugin.
//
// This tree's knsrc was fixed in 7a1e63ae3; nothing pinned it until this test
// (transplanted from latte-dock-qt6's indicatorsknsrctest). The layouts knsrc
// uses Uncompress=archive, which needs no structure key, so the indicators
// file is the only one at risk.

#include <KConfig>
#include <KConfigGroup>
#include <QObject>
#include <QString>
#include <QtTest>

class IndicatorsKnsrcTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void usesKf6PackageStructureKey();
    void doesNotUseDeadKf5PackageTypeKey();
};

void IndicatorsKnsrcTest::usesKf6PackageStructureKey()
{
    //! KConfig must outlive the KConfigGroup it vends, so read within this scope
    KConfig config(QStringLiteral(INDICATORS_KNSRC_PATH));
    const KConfigGroup grp = config.group(QStringLiteral("KNewStuff3"));
    QVERIFY2(grp.exists(), "[KNewStuff3] group missing from latte-indicators.knsrc");
    //! kpackage uncompression requires a registered structure id under the KF6 key
    QCOMPARE(grp.readEntry("Uncompress", QString()), QStringLiteral("kpackage"));
    QCOMPARE(grp.readEntry("KPackageStructure", QString()), QStringLiteral("Latte/Indicator"));
}

void IndicatorsKnsrcTest::doesNotUseDeadKf5PackageTypeKey()
{
    KConfig config(QStringLiteral(INDICATORS_KNSRC_PATH));
    const KConfigGroup grp = config.group(QStringLiteral("KNewStuff3"));
    //! KF6 KNSCore ignores KPackageType; leaving it is a silent store-dialog break
    QVERIFY2(!grp.hasKey("KPackageType"),
             "dead KF5 key KPackageType still present in latte-indicators.knsrc");
}

QTEST_GUILESS_MAIN(IndicatorsKnsrcTest)

#include "indicatorsknsrctest.moc"
