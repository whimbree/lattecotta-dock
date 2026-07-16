/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// EX-07 StorageIdRemapper, application side: Storage::newUniqueIdsFile's
// KConfig surgery driven over real layout files through the friend seam
// (the function is private and has no public headless entry). Pins the
// order-list rewrite and the two behavior fixes the extraction flagged:
// clone references follow the remap, and a clone whose original is not
// part of the import is orphaned to ISCLONEDNULL instead of binding to
// whatever containment owns the stale id in the destination.

#include <QtTest>
#include <QTemporaryDir>

#include <KConfigGroup>
#include <KSharedConfig>

#include "layout/centrallayout.h"
#include "layouts/storage.h"
#include "data/viewdata.h"

using Latte::Layouts::Storage;

class StorageIdRemapApplicationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void remapApplication_rewritesIdsOrderListsAndCloneReferences();
    void remapApplication_orphansCloneWithoutItsOriginal();

private:
    //! a minimal origin layout: containments 3 (original), 4 (its clone),
    //! 5 (carrying applets 30/31 and an appletOrder). All ids sit below
    //! their bases so every one of them is forced through the remap.
    QString writeOriginLayout(const QTemporaryDir &dir) const;
};

QString StorageIdRemapApplicationTest::writeOriginLayout(const QTemporaryDir &dir) const
{
    const QString path = dir.filePath(QStringLiteral("origin.layout.latte"));
    KSharedConfigPtr file = KSharedConfig::openConfig(path);
    KConfigGroup containments(file, QStringLiteral("Containments"));

    //! lastScreen doubles as an order-independent marker: groupList()
    //! order decides which new id each containment receives, so the
    //! assertions locate groups by marker instead of assuming the mapping
    KConfigGroup original = containments.group(QStringLiteral("3"));
    original.writeEntry("plugin", QStringLiteral("org.kde.latte.containment"));
    original.writeEntry("lastScreen", 71);

    KConfigGroup clone = containments.group(QStringLiteral("4"));
    clone.writeEntry("plugin", QStringLiteral("org.kde.latte.containment"));
    clone.writeEntry("isClonedFrom", 3);
    clone.writeEntry("lastScreen", 72);

    KConfigGroup carrier = containments.group(QStringLiteral("5"));
    carrier.writeEntry("plugin", QStringLiteral("org.kde.latte.containment"));
    carrier.writeEntry("lastScreen", 73);
    carrier.group(QStringLiteral("General")).writeEntry("appletOrder", QStringLiteral("30;31"));
    carrier.group(QStringLiteral("Applets")).group(QStringLiteral("30")).writeEntry("plugin", QStringLiteral("org.kde.plasma.kickoff"));
    carrier.group(QStringLiteral("Applets")).group(QStringLiteral("31")).writeEntry("plugin", QStringLiteral("org.kde.plasma.digitalclock"));

    file->sync();
    return path;
}

void StorageIdRemapApplicationTest::remapApplication_rewritesIdsOrderListsAndCloneReferences()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString originPath = writeOriginLayout(dir);

    //! a corona-less destination: newUniqueIdsFile derives the used ids
    //! from the (empty) destination file, so 3/4/5 -> 12/13/14 (groupList
    //! order) and 30/31 -> 40/41
    const QString destinationPath = dir.filePath(QStringLiteral("destination.layout.latte"));
    Latte::CentralLayout destination(nullptr, destinationPath, QStringLiteral("destination"));

    const QString remappedPath = Storage::self()->newUniqueIdsFile(originPath, &destination);
    QVERIFY(!remappedPath.isEmpty());

    KSharedConfigPtr remapped = KSharedConfig::openConfig(remappedPath);
    remapped->reparseConfiguration();
    KConfigGroup containments(remapped, QStringLiteral("Containments"));

    QStringList groups = containments.groupList();
    groups.sort();
    QCOMPARE(groups, QStringList({QStringLiteral("12"), QStringLiteral("13"), QStringLiteral("14")}));

    QString originalId, cloneId, carrierId;
    for (const auto &gId : groups) {
        switch (containments.group(gId).readEntry("lastScreen", 0)) {
        case 71: originalId = gId; break;
        case 72: cloneId = gId; break;
        case 73: carrierId = gId; break;
        }
    }
    QVERIFY(!originalId.isEmpty() && !cloneId.isEmpty() && !carrierId.isEmpty());

    //! the clone follows its remapped original
    QCOMPARE(containments.group(cloneId).readEntry("isClonedFrom", Latte::Data::View::ISCLONEDNULL),
             originalId.toInt());

    //! the applet order references the remapped applet ids
    QCOMPARE(containments.group(carrierId).group(QStringLiteral("General")).readEntry("appletOrder", QString()),
             QStringLiteral("40;41"));

    QStringList appletGroups = containments.group(carrierId).group(QStringLiteral("Applets")).groupList();
    appletGroups.sort();
    QCOMPARE(appletGroups, QStringList({QStringLiteral("40"), QStringLiteral("41")}));
}

void StorageIdRemapApplicationTest::remapApplication_orphansCloneWithoutItsOriginal()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString originPath = dir.filePath(QStringLiteral("orphan.layout.latte"));
    {
        KSharedConfigPtr file = KSharedConfig::openConfig(originPath);
        KConfigGroup containments(file, QStringLiteral("Containments"));
        KConfigGroup clone = containments.group(QStringLiteral("6"));
        clone.writeEntry("plugin", QStringLiteral("org.kde.latte.containment"));
        clone.writeEntry("isClonedFrom", 99); //! 99 is not part of the import
        file->sync();
    }

    const QString destinationPath = dir.filePath(QStringLiteral("destination2.layout.latte"));
    Latte::CentralLayout destination(nullptr, destinationPath, QStringLiteral("destination2"));

    const QString remappedPath = Storage::self()->newUniqueIdsFile(originPath, &destination);
    QVERIFY(!remappedPath.isEmpty());

    KSharedConfigPtr remapped = KSharedConfig::openConfig(remappedPath);
    remapped->reparseConfiguration();
    KConfigGroup containments(remapped, QStringLiteral("Containments"));

    QCOMPARE(containments.groupList(), QStringList({QStringLiteral("12")}));
    QCOMPARE(containments.group(QStringLiteral("12")).readEntry("isClonedFrom", 0),
             Latte::Data::View::ISCLONEDNULL);
}

QTEST_GUILESS_MAIN(StorageIdRemapApplicationTest)

#include "storageidremapapplicationtest.moc"
