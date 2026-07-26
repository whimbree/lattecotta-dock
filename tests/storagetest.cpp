/*
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Real-link behavioral test for Latte::Layouts::Storage, the layout-file
// engine everything .latte rides on. Drives the Storage::self() singleton
// (linked through lattedock-core) against temp KConfig fixtures; every method
// exercised operates on a file path or KConfigGroup without a live Corona:
// containment/applet enumeration, the view() deserializer and updateView()
// serializer, subcontainment detection, clone handling, the errors/warnings
// scanners, template export/import and the null-corona screen-id branch.
//
// Transplanted from latte-dock-qt6 (tests/storagetest.cpp at 81384003, github.com/CaptSilver/latte-dock-qt6)
// and raised: adds the view() defaults table for unset keys, the
// updateView() non-Latte refusal, the clean-layout negative for
// errors/warnings, the containment-id filter observability in plugins()
// (their case only asserted rowCount >= 1), and the LayoutSettings +
// isPreferredForShortcuts clearing in exportTemplate. Their
// updateViewRoundTripsThroughKConfig caught upstream's dead-key maxLength
// write (their fix b48903ec); the same defect was live here and is fixed in
// the commit right before this test landed - the round-trip case pins it.

// local
#include "data/appletdata.h"
#include "data/errordata.h"
#include "data/generictable.h"
#include "data/genericdata.h"
#include "data/viewdata.h"
#include "data/viewstable.h"
#include "layout/centrallayout.h"
#include "layouts/importer.h"
#include "layouts/storage.h"
#include "screenpool.h"

#include <coretypes.h>

// Qt
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QObject>
#include <QScopeGuard>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

// C++
#include <optional>

// KDE
#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>

using Latte::Layouts::Storage;

struct DurableMoveFixture final
{
    QString originLayout;
    QString originFile;
    QString destinationLayout;
    QString destinationFile;
    QString hiddenFile;
    QString snapshotFile;
    KSharedConfigPtr hiddenConfig;
};

struct DurableMoveLifecycleReadback final
{
    quint64 journalCreatedGeneration;
    quint64 commitDecisionGeneration;
    quint64 journalRetiredGeneration;
    QJsonArray transactions;
};

[[nodiscard]] std::optional<
    DurableMoveLifecycleReadback>
parseDurableMoveLifecycleReadback(
    const QString &payload)
{
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(
            payload.toUtf8(),
            &parseError);
    if (parseError.error
            != QJsonParseError::NoError
            || !document.isObject()) {
        return std::nullopt;
    }

    const QJsonObject readback =
        document.object();
    const QStringList expectedKeys{
        QStringLiteral(
            "commitDecisionGeneration"),
        QStringLiteral(
            "journalCreatedGeneration"),
        QStringLiteral(
            "journalRetiredGeneration"),
        QStringLiteral("schemaVersion"),
        QStringLiteral("transactions"),
    };
    if (readback.keys() != expectedKeys
            || readback.value(
                QStringLiteral(
                    "schemaVersion"))
                .toInt()
                != 2
            || !readback.value(
                QStringLiteral(
                    "transactions"))
                .isArray()) {
        return std::nullopt;
    }

    const auto parseGeneration =
        [&readback](
            const QString &key)
            -> std::optional<quint64> {
        const QJsonValue value =
            readback.value(key);
        if (!value.isString()) {
            return std::nullopt;
        }
        bool parsed{false};
        const quint64 generation =
            value.toString()
                .toULongLong(
                    &parsed);
        if (!parsed
                || value.toString()
                    != QString::number(
                        generation)) {
            return std::nullopt;
        }
        return generation;
    };
    const auto journalCreated =
        parseGeneration(
            QStringLiteral(
                "journalCreatedGeneration"));
    const auto commitDecision =
        parseGeneration(
            QStringLiteral(
                "commitDecisionGeneration"));
    const auto journalRetired =
        parseGeneration(
            QStringLiteral(
                "journalRetiredGeneration"));
    if (!journalCreated
            || !commitDecision
            || !journalRetired) {
        return std::nullopt;
    }

    return DurableMoveLifecycleReadback{
        .journalCreatedGeneration =
            *journalCreated,
        .commitDecisionGeneration =
            *commitDecision,
        .journalRetiredGeneration =
            *journalRetired,
        .transactions =
            readback.value(
                QStringLiteral(
                    "transactions"))
                .toArray(),
    };
}

class PersistenceEndpointLayout final : public Latte::CentralLayout
{
public:
    explicit PersistenceEndpointLayout(
        const QString &filePath)
        : Latte::CentralLayout(
            nullptr,
            QString(),
            QString())
    {
        setFile(filePath);
    }
};

class StorageTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    //! id helpers
    void pinIdSentinelsAndValidity();

    //! group classification
    void classifyLatteContainmentByPlugin();
    void resolveSubContainmentIdFromBothIdentityKeys();
    void rejectPreloadShellAsAppletGroup();

    //! view() deserializer
    void deserializeViewFromContainmentGroup();
    void deserializeViewDefaultsForUnsetKeys();
    void refuseMalformedLinkedPlacement();
    void refuseViewForNonLatteContainment();

    //! updateView() serializer
    void roundTripViewThroughKConfig();
    void refuseUpdateViewOnNonLatteGroup();

    //! enumeration
    void enumerateOnlyLatteContainmentsAsViews();
    void listSubcontainmentsOfContainmentGroup();
    void reportContainsViewOnlyForLatteIds();
    void enumerateViewsOfInactiveLayout();
    void validatePersistedRelationshipGraphs_data();
    void validatePersistedRelationshipGraphs();
    void tombstoneRemovalSnapshotDeletesExactGroupsOnDisk();
    void tombstoneRemovalSnapshotRefreshesStaleSharedRepository();
    void restoreRemovalSnapshotReplacesPartialGroup();
    void removalPersistenceReportsWriteFailure();
    void classifyLayoutPersistenceEndpoints();
    void exposeExactDurableMoveLifecycleSchema();
    void refuseUndurableTransactionRootBeforeEndpointMutation();
    void refuseImmutableDurableMoveBeforeCommit();
    void refuseImmutableActiveOwnerBeforeStaging();
    void refuseLockedDurableMoveBeforeCommit();
    void rejectPreparedMoveRetiresJournalWithoutCommit();
    void commitDurableMoveAndRetireJournal();
    void commitFromSnapshotWhenStandaloneSourceIsStale();
    void discardUnpublishedPreparedJournalDuringRecovery();
    void refuseHeldRecoveryLockWithoutMutationThenRetry_data();
    void refuseHeldRecoveryLockWithoutMutationThenRetry();
    void refuseRecoveryForMixedSubtreeOwnership();
    void resumeRecoveryAfterEachRepositoryPublication_data();
    void resumeRecoveryAfterEachRepositoryPublication();
    void recoverAfterEachDirectoryFlushFailure_data();
    void recoverAfterEachDirectoryFlushFailure();
    void repeatCompletedRecoveryIsIdempotent();
    void refuseTraversalBearingRecoveryManifest();
    void recoverInterruptedDestinationStagingByRollingBack();
    void recoverCommittedMoveByRollingForward();
    void refuseRecoveryFromCorruptedJournal();

    //! clones
    void detectClonedViewsOnlyForLatteContainments();
    void removeScreenGroupDerivedViewsKeepsPersistentRelationships();

    //! removal
    void removeContainmentDeletesExactlyThatGroup();
    void removeViewDropsViewAndItsSubcontainments();

    //! screen id resolution
    void resolveNoScreenIdWithoutCorona();

    //! integrity scanners
    void reportNoErrorsOrWarningsForCleanLayout();
    void reportDuplicateAppletIdsAsError();
    void reportOrphanedSubcontainmentAsWarning();

    //! template export/import
    //! (importContainments is private here, unlike the fork's tree; its
    //! observable effect is pinned through newView's inactive branch in
    //! remapIdsWhenAddingTemplateViewToInactiveLayout)
    void stripUnapprovedAppletsFromExportedTemplate();
    void clearLayoutSettingsInExportedTemplate();

    //! metadata
    void fallBackToPluginIdForUnknownApplet();
    void gatherAppletPluginsFilteredByContainmentId();

    //! template instantiation
    void remapIdsWhenAddingTemplateViewToInactiveLayout();
    void writeStoredViewOfInactiveLayoutToTempFile();

private:
    //! Writes a layout file with one Latte containment (id 1) carrying a
    //! plasmoid applet (id 2) and a systray applet (id 3) whose configuration
    //! points at subcontainment 99, plus a non-Latte containment (id 5) with
    //! an own applet (id 6) and the subcontainment itself (id 99). Returns
    //! the file path.
    QString writeLayoutFixture(const QString &name);
    DurableMoveFixture createDurableMoveFixture(
        const QString &suffix);

    QTemporaryDir m_dir;
};

void StorageTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    QVERIFY(Storage::self() != nullptr);
}

DurableMoveFixture StorageTest::
createDurableMoveFixture(
    const QString &suffix)
{
    const QString layoutsDirectory =
        Latte::Layouts::Importer::
            layoutUserDir();
    if (!QDir().mkpath(layoutsDirectory)) {
        return {};
    }

    const QString transactionRoot =
        QDir(layoutsDirectory)
            .filePath(
                QStringLiteral(
                    ".view-move-transactions"));
    if (QFileInfo::exists(transactionRoot)
            && !QDir(transactionRoot)
                .removeRecursively()) {
        return {};
    }

    DurableMoveFixture fixture;
    fixture.originLayout =
        QStringLiteral("move-origin-%1")
            .arg(suffix);
    fixture.destinationLayout =
        QStringLiteral("move-destination-%1")
            .arg(suffix);
    fixture.originFile =
        Latte::Layouts::Importer::
            layoutUserFilePath(
                fixture.originLayout);
    fixture.destinationFile =
        Latte::Layouts::Importer::
            layoutUserFilePath(
                fixture.destinationLayout);
    fixture.hiddenFile =
        Latte::Layouts::Importer::
            layoutUserFilePath(
                QString::fromLatin1(
                    Latte::Layout::
                        MULTIPLELAYOUTSHIDDENNAME));
    fixture.snapshotFile =
        m_dir.filePath(
            QStringLiteral(
                "move-snapshot-%1.latte")
                .arg(suffix));
    const QStringList fixtureFiles{
        fixture.originFile,
        fixture.destinationFile,
        fixture.hiddenFile,
        fixture.snapshotFile,
    };
    for (const QString &fixtureFile :
            fixtureFiles) {
        if (QFileInfo::exists(fixtureFile)
                && !QFile::remove(fixtureFile)) {
            return {};
        }
    }

    const auto writeSubtree =
        [](const KSharedConfigPtr &config,
           const QString &layoutId) {
            KConfigGroup containments(
                config,
                QStringLiteral(
                    "Containments"));
            KConfigGroup root =
                containments.group(
                    QStringLiteral("12"));
            root.writeEntry(
                QStringLiteral("plugin"),
                QStringLiteral(
                    "org.kde.latte.containment"));
            root.writeEntry(
                QStringLiteral("layoutId"),
                layoutId);
            root.group(
                QStringLiteral("General"))
                .writeEntry(
                    QStringLiteral("name"),
                    QStringLiteral(
                        "transaction-root"));

            KConfigGroup child =
                containments.group(
                    QStringLiteral("13"));
            child.writeEntry(
                QStringLiteral("plugin"),
                QStringLiteral(
                    "org.kde.plasma.private.systemtray"));
            child.writeEntry(
                QStringLiteral("layoutId"),
                layoutId);
            child.group(
                QStringLiteral("General"))
                .writeEntry(
                    QStringLiteral("owner"),
                    12);
            return config->sync();
        };

    {
        const KSharedConfigPtr origin =
            KSharedConfig::openConfig(
                fixture.originFile,
                KConfig::SimpleConfig);
        if (!writeSubtree(
                origin,
                QString())) {
            return {};
        }
    }
    {
        const KSharedConfigPtr destination =
            KSharedConfig::openConfig(
                fixture.destinationFile,
                KConfig::SimpleConfig);
        destination
            ->group(
                QStringLiteral(
                    "LayoutSettings"))
            .writeEntry(
                QStringLiteral("version"),
                2);
        if (!destination->sync()) {
            return {};
        }
    }
    fixture.hiddenConfig =
        KSharedConfig::openConfig(
            fixture.hiddenFile,
            KConfig::SimpleConfig);
    if (!writeSubtree(
            fixture.hiddenConfig,
            fixture.originLayout)) {
        return {};
    }
    {
        const KSharedConfigPtr snapshot =
            KSharedConfig::openConfig(
                fixture.snapshotFile,
                KConfig::SimpleConfig);
        if (!writeSubtree(
                snapshot,
                fixture.originLayout)) {
            return {};
        }
    }
    return fixture;
}

void StorageTest::
exposeExactDurableMoveLifecycleSchema()
{
    const QJsonObject readback =
        QJsonDocument::fromJson(
            Storage::self()
                ->viewMoveTransactionsData()
                .toUtf8())
            .object();
    const QStringList expectedKeys{
        QStringLiteral(
            "commitDecisionGeneration"),
        QStringLiteral(
            "journalCreatedGeneration"),
        QStringLiteral(
            "journalRetiredGeneration"),
        QStringLiteral(
            "schemaVersion"),
        QStringLiteral(
            "transactions"),
    };
    QCOMPARE(
        readback.keys(),
        expectedKeys);
    QCOMPARE(
        readback.value(
            QStringLiteral(
                "schemaVersion"))
            .toInt(),
        2);

    const auto lifecycle =
        parseDurableMoveLifecycleReadback(
            Storage::self()
                ->viewMoveTransactionsData());
    QVERIFY(lifecycle);
    QCOMPARE(
        lifecycle
            ->journalCreatedGeneration,
        quint64{0});
    QCOMPARE(
        lifecycle
            ->commitDecisionGeneration,
        quint64{0});
    QCOMPARE(
        lifecycle
            ->journalRetiredGeneration,
        quint64{0});
}

void StorageTest::
refuseUndurableTransactionRootBeforeEndpointMutation()
{
    const DurableMoveFixture fixture =
        createDurableMoveFixture(
            QStringLiteral(
                "undurable-transaction-root"));
    QVERIFY(!fixture.originFile.isEmpty());

    const auto readFile =
        [](const QString &path) {
            QFile file(path);
            if (!file.open(
                    QIODevice::ReadOnly)) {
                return QByteArray{};
            }
            return file.readAll();
        };
    const QByteArray originBefore =
        readFile(fixture.originFile);
    const QByteArray destinationBefore =
        readFile(
            fixture.destinationFile);
    const QByteArray hiddenBefore =
        readFile(fixture.hiddenFile);
    QVERIFY(!originBefore.isEmpty());
    QVERIFY(!destinationBefore.isEmpty());
    QVERIFY(!hiddenBefore.isEmpty());
    const auto lifecycleBefore =
        parseDurableMoveLifecycleReadback(
            Storage::self()
                ->viewMoveTransactionsData());
    QVERIFY(lifecycleBefore);

    const auto result =
        Storage::self()
            ->persistViewMoveSnapshot(
                fixture.originLayout,
                fixture.originFile,
                fixture.destinationLayout,
                fixture.destinationFile,
                fixture.hiddenConfig,
                12,
                fixture.snapshotFile,
                Storage::
                    ViewMoveInterruption::
                        None,
                Storage::
                    ViewMoveDirectoryFlushFailure::
                        TransactionRootPublication);
    QCOMPARE(
        result.status,
        Latte::Layouts::
            ViewMovePersistenceResult::
                Status::Rejected);
    QVERIFY(result.transactionPath.isEmpty());
    QCOMPARE(
        result.error,
        QStringLiteral(
            "could not durably publish transaction storage"));
    QVERIFY(Storage::self()
        ->pendingViewMoveTransactions()
        .isEmpty());

    const QString transactionRoot =
        QDir(
            Latte::Layouts::Importer::
                layoutUserDir())
            .filePath(
                QStringLiteral(
                    ".view-move-transactions"));
    QVERIFY(QFileInfo(
        transactionRoot)
        .isDir());
    QVERIFY(QDir(transactionRoot)
        .entryList(
            QDir::AllEntries
                | QDir::NoDotAndDotDot)
        .isEmpty());
    QCOMPARE(
        readFile(fixture.originFile),
        originBefore);
    QCOMPARE(
        readFile(fixture.destinationFile),
        destinationBefore);
    QCOMPARE(
        readFile(fixture.hiddenFile),
        hiddenBefore);

    const auto lifecycleAfter =
        parseDurableMoveLifecycleReadback(
            Storage::self()
                ->viewMoveTransactionsData());
    QVERIFY(lifecycleAfter);
    QCOMPARE(
        lifecycleAfter
            ->journalCreatedGeneration,
        lifecycleBefore
            ->journalCreatedGeneration);
    QCOMPARE(
        lifecycleAfter
            ->commitDecisionGeneration,
        lifecycleBefore
            ->commitDecisionGeneration);
    QCOMPARE(
        lifecycleAfter
            ->journalRetiredGeneration,
        lifecycleBefore
            ->journalRetiredGeneration);
}

void StorageTest::
refuseImmutableDurableMoveBeforeCommit()
{
    const DurableMoveFixture fixture =
        createDurableMoveFixture(
            QStringLiteral("immutable"));
    QVERIFY(!fixture.originFile.isEmpty());
    const auto lifecycleBefore =
        parseDurableMoveLifecycleReadback(
            Storage::self()
                ->viewMoveTransactionsData());
    QVERIFY(lifecycleBefore);

    QFile immutableDestination(
        fixture.destinationFile);
    QVERIFY(immutableDestination.open(
        QIODevice::WriteOnly
            | QIODevice::Truncate));
    const QByteArray immutableConfig(
        "[$i]\n"
        "[LayoutSettings]\n"
        "version=2\n");
    QCOMPARE(
        immutableDestination.write(
            immutableConfig),
        immutableConfig.size());
    immutableDestination.close();
    const KConfig immutableReadback(
        fixture.destinationFile,
        KConfig::SimpleConfig);
    QVERIFY(immutableReadback.isImmutable());

    const auto result =
        Storage::self()
            ->persistViewMoveSnapshot(
                fixture.originLayout,
                fixture.originFile,
                fixture.destinationLayout,
                fixture.destinationFile,
                fixture.hiddenConfig,
                12,
                fixture.snapshotFile);
    QCOMPARE(
        result.status,
        Latte::Layouts::
            ViewMovePersistenceResult::
                Status::Rejected);
    QVERIFY(Storage::self()
        ->pendingViewMoveTransactions()
        .isEmpty());
    const auto lifecycleAfter =
        parseDurableMoveLifecycleReadback(
            Storage::self()
                ->viewMoveTransactionsData());
    QVERIFY(lifecycleAfter);
    QCOMPARE(
        lifecycleAfter
            ->journalCreatedGeneration,
        lifecycleBefore
            ->journalCreatedGeneration);
    QCOMPARE(
        lifecycleAfter
            ->commitDecisionGeneration,
        lifecycleBefore
            ->commitDecisionGeneration);
    QCOMPARE(
        lifecycleAfter
            ->journalRetiredGeneration,
        lifecycleBefore
            ->journalRetiredGeneration);

    const KConfig origin(
        fixture.originFile,
        KConfig::SimpleConfig);
    const KConfig destination(
        fixture.destinationFile,
        KConfig::SimpleConfig);
    const KConfig hidden(
        fixture.hiddenFile,
        KConfig::SimpleConfig);
    QVERIFY(origin.group(
        QStringLiteral("Containments"))
        .hasGroup(QStringLiteral("12")));
    QVERIFY(!destination.group(
        QStringLiteral("Containments"))
        .hasGroup(QStringLiteral("12")));
    QCOMPARE(
        hidden.group(
            QStringLiteral("Containments"))
            .group(QStringLiteral("12"))
            .readEntry(
                QStringLiteral("layoutId"),
                QString()),
        fixture.originLayout);
}

void StorageTest::
refuseLockedDurableMoveBeforeCommit()
{
    const DurableMoveFixture fixture =
        createDurableMoveFixture(
            QStringLiteral("locked"));
    QVERIFY(!fixture.originFile.isEmpty());

    QLockFile destinationLock(
        fixture.destinationFile
        + QStringLiteral(".lock"));
    destinationLock.setStaleLockTime(0);
    QVERIFY(destinationLock.tryLock(0));

    const auto result =
        Storage::self()
            ->persistViewMoveSnapshot(
                fixture.originLayout,
                fixture.originFile,
                fixture.destinationLayout,
                fixture.destinationFile,
                fixture.hiddenConfig,
                12,
                fixture.snapshotFile);
    QCOMPARE(
        result.status,
        Latte::Layouts::
            ViewMovePersistenceResult::
                Status::Rejected);
    QVERIFY(Storage::self()
        ->pendingViewMoveTransactions()
        .isEmpty());

    const KConfig hidden(
        fixture.hiddenFile,
        KConfig::SimpleConfig);
    QCOMPARE(
        hidden.group(
            QStringLiteral("Containments"))
            .group(QStringLiteral("12"))
            .readEntry(
                QStringLiteral("layoutId"),
                QString()),
        fixture.originLayout);
}

void StorageTest::
refuseImmutableActiveOwnerBeforeStaging()
{
    const DurableMoveFixture fixture =
        createDurableMoveFixture(
            QStringLiteral(
                "immutable-owner"));
    QVERIFY(!fixture.originFile.isEmpty());

    QFile hiddenFile(fixture.hiddenFile);
    QVERIFY(hiddenFile.open(
        QIODevice::ReadOnly));
    QByteArray payload = hiddenFile.readAll();
    hiddenFile.close();
    const QByteArray mutableOwner =
        QByteArray("layoutId=")
        + fixture.originLayout.toUtf8();
    const QByteArray immutableOwner =
        QByteArray("layoutId[$i]=")
        + fixture.originLayout.toUtf8();
    QVERIFY(payload.contains(mutableOwner));
    payload.replace(
        mutableOwner,
        immutableOwner);
    QVERIFY(hiddenFile.open(
        QIODevice::WriteOnly
            | QIODevice::Truncate));
    QCOMPARE(
        hiddenFile.write(payload),
        payload.size());
    hiddenFile.close();
    fixture.hiddenConfig
        ->reparseConfiguration();

    const KConfigGroup hiddenContainments(
        fixture.hiddenConfig,
        QStringLiteral("Containments"));
    QVERIFY(hiddenContainments
        .group(QStringLiteral("12"))
        .isEntryImmutable(
            QStringLiteral("layoutId")));

    const auto result =
        Storage::self()
            ->persistViewMoveSnapshot(
                fixture.originLayout,
                fixture.originFile,
                fixture.destinationLayout,
                fixture.destinationFile,
                fixture.hiddenConfig,
                12,
                fixture.snapshotFile);
    QCOMPARE(
        result.status,
        Latte::Layouts::
            ViewMovePersistenceResult::
                Status::Rejected);
    QVERIFY(Storage::self()
        ->pendingViewMoveTransactions()
        .isEmpty());

    const KConfig destination(
        fixture.destinationFile,
        KConfig::SimpleConfig);
    QVERIFY(!destination.group(
        QStringLiteral("Containments"))
        .hasGroup(QStringLiteral("12")));
}

void StorageTest::
rejectPreparedMoveRetiresJournalWithoutCommit()
{
    const DurableMoveFixture fixture =
        createDurableMoveFixture(
            QStringLiteral(
                "reject-and-retire"));
    QVERIFY(!fixture.originFile.isEmpty());
    const auto lifecycleBefore =
        parseDurableMoveLifecycleReadback(
            Storage::self()
                ->viewMoveTransactionsData());
    QVERIFY(lifecycleBefore);

    const auto result =
        Storage::self()
            ->persistViewMoveSnapshot(
                fixture.originLayout,
                fixture.originFile,
                fixture.destinationLayout,
                fixture.destinationFile,
                fixture.hiddenConfig,
                12,
                fixture.snapshotFile,
                Storage::
                    ViewMoveInterruption::
                        RejectCommitDecision);
    QCOMPARE(
        result.status,
        Latte::Layouts::
            ViewMovePersistenceResult::
                Status::Rejected);
    QVERIFY(!result.transactionPath.isEmpty());
    QVERIFY(!QFileInfo::exists(
        result.transactionPath));
    QVERIFY(Storage::self()
        ->pendingViewMoveTransactions()
        .isEmpty());

    const auto lifecycleAfter =
        parseDurableMoveLifecycleReadback(
            Storage::self()
                ->viewMoveTransactionsData());
    QVERIFY(lifecycleAfter);
    QCOMPARE(
        lifecycleAfter
            ->journalCreatedGeneration,
        lifecycleBefore
                ->journalCreatedGeneration
            + 1);
    QCOMPARE(
        lifecycleAfter
            ->commitDecisionGeneration,
        lifecycleBefore
            ->commitDecisionGeneration);
    QCOMPARE(
        lifecycleAfter
            ->journalRetiredGeneration,
        lifecycleBefore
                ->journalRetiredGeneration
            + 1);
}

void StorageTest::
commitDurableMoveAndRetireJournal()
{
    const DurableMoveFixture fixture =
        createDurableMoveFixture(
            QStringLiteral(
                "commit-and-retire"));
    QVERIFY(!fixture.originFile.isEmpty());
    const auto lifecycleBefore =
        parseDurableMoveLifecycleReadback(
            Storage::self()
                ->viewMoveTransactionsData());
    QVERIFY(lifecycleBefore);

    const auto result =
        Storage::self()
            ->persistViewMoveSnapshot(
                fixture.originLayout,
                fixture.originFile,
                fixture.destinationLayout,
                fixture.destinationFile,
                fixture.hiddenConfig,
                12,
                fixture.snapshotFile);
    QCOMPARE(
        result.status,
        Latte::Layouts::
            ViewMovePersistenceResult::
                Status::Committed);
    QVERIFY(!result.transactionPath.isEmpty());
    QCOMPARE(
        Storage::self()
            ->pendingViewMoveTransactions(),
        QStringList{
            result.transactionPath});
    QFile manifest(
        QDir(result.transactionPath)
            .filePath(
                QStringLiteral(
                    "manifest.json")));
    QVERIFY(manifest.open(
        QIODevice::ReadOnly));
    const QJsonObject manifestReadback =
        QJsonDocument::fromJson(
            manifest.readAll())
            .object();
    QCOMPARE(
        manifestReadback.value(
            QStringLiteral(
                "schemaVersion"))
            .toInt(),
        1);
    const auto lifecycleAfterCommit =
        parseDurableMoveLifecycleReadback(
            Storage::self()
                ->viewMoveTransactionsData());
    QVERIFY(lifecycleAfterCommit);
    QCOMPARE(
        lifecycleAfterCommit
            ->journalCreatedGeneration,
        lifecycleBefore
                ->journalCreatedGeneration
            + 1);
    QCOMPARE(
        lifecycleAfterCommit
            ->commitDecisionGeneration,
        lifecycleBefore
                ->commitDecisionGeneration
            + 1);
    QCOMPARE(
        lifecycleAfterCommit
            ->journalRetiredGeneration,
        lifecycleBefore
            ->journalRetiredGeneration);

    {
        const KConfig origin(
            fixture.originFile,
            KConfig::SimpleConfig);
        const KConfig destination(
            fixture.destinationFile,
            KConfig::SimpleConfig);
        const KConfig hidden(
            fixture.hiddenFile,
            KConfig::SimpleConfig);
        QVERIFY(!origin.group(
            QStringLiteral("Containments"))
            .hasGroup(
                QStringLiteral("12")));
        QVERIFY(!origin.group(
            QStringLiteral("Containments"))
            .hasGroup(
                QStringLiteral("13")));
        QVERIFY(destination.group(
            QStringLiteral("Containments"))
            .hasGroup(
                QStringLiteral("12")));
        QVERIFY(destination.group(
            QStringLiteral("Containments"))
            .hasGroup(
                QStringLiteral("13")));
        QCOMPARE(
            hidden.group(
                QStringLiteral("Containments"))
                .group(QStringLiteral("12"))
                .readEntry(
                    QStringLiteral("layoutId"),
                    QString()),
            fixture.destinationLayout);
        QCOMPARE(
            hidden.group(
                QStringLiteral("Containments"))
                .group(QStringLiteral("13"))
                .readEntry(
                    QStringLiteral("layoutId"),
                    QString()),
            fixture.destinationLayout);
    }

    QVERIFY(Storage::self()
        ->completeViewMovePersistence(
            result.transactionPath));
    QVERIFY(Storage::self()
        ->pendingViewMoveTransactions()
        .isEmpty());
    QVERIFY(!QFileInfo::exists(
        result.transactionPath));
    const auto lifecycleAfterRetirement =
        parseDurableMoveLifecycleReadback(
            Storage::self()
                ->viewMoveTransactionsData());
    QVERIFY(lifecycleAfterRetirement);
    QCOMPARE(
        lifecycleAfterRetirement
            ->journalCreatedGeneration,
        lifecycleAfterCommit
            ->journalCreatedGeneration);
    QCOMPARE(
        lifecycleAfterRetirement
            ->commitDecisionGeneration,
        lifecycleAfterCommit
            ->commitDecisionGeneration);
    QCOMPARE(
        lifecycleAfterRetirement
            ->journalRetiredGeneration,
        lifecycleAfterCommit
                ->journalRetiredGeneration
            + 1);
}

void StorageTest::
commitFromSnapshotWhenStandaloneSourceIsStale()
{
    const DurableMoveFixture fixture =
        createDurableMoveFixture(
            QStringLiteral(
                "stale-standalone"));
    QVERIFY(!fixture.originFile.isEmpty());

    {
        const KSharedConfigPtr origin =
            KSharedConfig::openConfig(
                fixture.originFile,
                KConfig::SimpleConfig);
        origin->group(
            QStringLiteral("Containments"))
            .group(QStringLiteral("12"))
            .group(QStringLiteral("General"))
            .writeEntry(
                QStringLiteral("name"),
                QStringLiteral(
                    "stale-standalone-mirror"));
        QVERIFY(origin->sync());
    }

    const auto result =
        Storage::self()
            ->persistViewMoveSnapshot(
                fixture.originLayout,
                fixture.originFile,
                fixture.destinationLayout,
                fixture.destinationFile,
                fixture.hiddenConfig,
                12,
                fixture.snapshotFile);
    QCOMPARE(
        result.status,
        Latte::Layouts::
            ViewMovePersistenceResult::
                Status::Committed);
    QVERIFY(!result.transactionPath.isEmpty());

    const KConfig destination(
        fixture.destinationFile,
        KConfig::SimpleConfig);
    QCOMPARE(
        destination.group(
            QStringLiteral("Containments"))
            .group(QStringLiteral("12"))
            .group(QStringLiteral("General"))
            .readEntry(
                QStringLiteral("name"),
                QString()),
        QStringLiteral(
            "transaction-root"));
    QVERIFY(Storage::self()
        ->completeViewMovePersistence(
            result.transactionPath));
    QVERIFY(Storage::self()
        ->pendingViewMoveTransactions()
        .isEmpty());
}

void StorageTest::
discardUnpublishedPreparedJournalDuringRecovery()
{
    const DurableMoveFixture fixture =
        createDurableMoveFixture(
            QStringLiteral(
                "prepared-residue"));
    QVERIFY(!fixture.originFile.isEmpty());

    const auto readFile =
        [](const QString &path) {
            QFile file(path);
            if (!file.open(
                    QIODevice::ReadOnly)) {
                return QByteArray{};
            }
            return file.readAll();
        };
    const QByteArray originBefore =
        readFile(fixture.originFile);
    const QByteArray destinationBefore =
        readFile(fixture.destinationFile);
    const QByteArray hiddenBefore =
        readFile(fixture.hiddenFile);
    QVERIFY(!originBefore.isEmpty());
    QVERIFY(!destinationBefore.isEmpty());
    QVERIFY(!hiddenBefore.isEmpty());

    const QString transactionRoot =
        QDir(
            Latte::Layouts::Importer::
                layoutUserDir())
            .filePath(
                QStringLiteral(
                    ".view-move-transactions"));
    const auto cleanup =
        qScopeGuard(
            [&transactionRoot]() {
                QDir(transactionRoot)
                    .removeRecursively();
            });
    const QString preparedPath =
        QDir(transactionRoot)
            .filePath(
                QStringLiteral(
                    "unpublished.prepare"));
    QVERIFY(QDir().mkpath(
        preparedPath));
    QFile partialManifest(
        QDir(preparedPath)
            .filePath(
                QStringLiteral(
                    "manifest.json")));
    QVERIFY(partialManifest.open(
        QIODevice::WriteOnly));
    const QByteArray incompletePayload{
        "{\"schemaVersion\":1"};
    QCOMPARE(
        partialManifest.write(
            incompletePayload),
        incompletePayload.size());
    partialManifest.close();

    QVERIFY(Storage::self()
        ->recoverPendingViewMoves());
    QVERIFY(!QFileInfo::exists(
        preparedPath));
    QVERIFY(Storage::self()
        ->pendingViewMoveTransactions()
        .isEmpty());
    QCOMPARE(
        readFile(fixture.originFile),
        originBefore);
    QCOMPARE(
        readFile(fixture.destinationFile),
        destinationBefore);
    QCOMPARE(
        readFile(fixture.hiddenFile),
        hiddenBefore);
}

void StorageTest::
refuseHeldRecoveryLockWithoutMutationThenRetry_data()
{
    QTest::addColumn<QString>(
        "lockedEndpoint");

    QTest::newRow("origin")
        << QStringLiteral("origin");
    QTest::newRow("destination")
        << QStringLiteral("destination");
    QTest::newRow("active-owner")
        << QStringLiteral("hidden");
}

void StorageTest::
refuseHeldRecoveryLockWithoutMutationThenRetry()
{
    QFETCH(QString, lockedEndpoint);

    const DurableMoveFixture fixture =
        createDurableMoveFixture(
            QStringLiteral("held-recovery-lock-%1")
                .arg(lockedEndpoint));
    QVERIFY(!fixture.originFile.isEmpty());

    const auto result =
        Storage::self()
            ->persistViewMoveSnapshot(
                fixture.originLayout,
                fixture.originFile,
                fixture.destinationLayout,
                fixture.destinationFile,
                fixture.hiddenConfig,
                12,
                fixture.snapshotFile,
                Storage::
                    ViewMoveInterruption::
                        AfterDestinationPublish);
    QCOMPARE(
        result.status,
        Latte::Layouts::
            ViewMovePersistenceResult::
                Status::
                    RejectedRecoveryRequired);
    QVERIFY(!result.transactionPath.isEmpty());
    const QString transactionRoot =
        QFileInfo(result.transactionPath)
            .absolutePath();
    const auto cleanup =
        qScopeGuard(
            [&transactionRoot]() {
                QDir(transactionRoot)
                    .removeRecursively();
            });

    const auto readFile =
        [](const QString &path) {
            QFile file(path);
            if (!file.open(
                    QIODevice::ReadOnly)) {
                return QByteArray{};
            }
            return file.readAll();
        };
    const QByteArray originBefore =
        readFile(fixture.originFile);
    const QByteArray destinationBefore =
        readFile(fixture.destinationFile);
    const QByteArray hiddenBefore =
        readFile(fixture.hiddenFile);
    QVERIFY(!originBefore.isEmpty());
    QVERIFY(!destinationBefore.isEmpty());
    QVERIFY(!hiddenBefore.isEmpty());

    const QString lockedEndpointPath =
        lockedEndpoint
                == QStringLiteral("origin")
            ? fixture.originFile
            : lockedEndpoint
                    == QStringLiteral("destination")
                ? fixture.destinationFile
                : fixture.hiddenFile;
    QLockFile endpointLock(
        lockedEndpointPath
        + QStringLiteral(".lock"));
    endpointLock.setStaleLockTime(0);
    QVERIFY(endpointLock.tryLock(0));
    QElapsedTimer recoveryTimer;
    recoveryTimer.start();
    QVERIFY(!Storage::self()
        ->recoverPendingViewMoves());
    QVERIFY2(
        recoveryTimer.elapsed() < 1000,
        "Recovery waited on a contended KConfig lock");
    QCOMPARE(
        Storage::self()
            ->pendingViewMoveTransactions(),
        QStringList{
            result.transactionPath});
    QCOMPARE(
        readFile(fixture.originFile),
        originBefore);
    QCOMPARE(
        readFile(fixture.destinationFile),
        destinationBefore);
    QCOMPARE(
        readFile(fixture.hiddenFile),
        hiddenBefore);

    endpointLock.unlock();
    QVERIFY(Storage::self()
        ->recoverPendingViewMoves());
    QVERIFY(Storage::self()
        ->pendingViewMoveTransactions()
        .isEmpty());
    const KConfig destination(
        fixture.destinationFile,
        KConfig::SimpleConfig);
    QVERIFY(!destination.group(
        QStringLiteral("Containments"))
        .hasGroup(
            QStringLiteral("12")));
}

void StorageTest::
refuseRecoveryForMixedSubtreeOwnership()
{
    const DurableMoveFixture fixture =
        createDurableMoveFixture(
            QStringLiteral(
                "mixed-owner"));
    QVERIFY(!fixture.originFile.isEmpty());

    const auto result =
        Storage::self()
            ->persistViewMoveSnapshot(
                fixture.originLayout,
                fixture.originFile,
                fixture.destinationLayout,
                fixture.destinationFile,
                fixture.hiddenConfig,
                12,
                fixture.snapshotFile,
                Storage::
                    ViewMoveInterruption::
                        AfterDestinationPublish);
    QCOMPARE(
        result.status,
        Latte::Layouts::
            ViewMovePersistenceResult::
                Status::
                    RejectedRecoveryRequired);
    QVERIFY(!result.transactionPath.isEmpty());
    const QString transactionRoot =
        QFileInfo(result.transactionPath)
            .absolutePath();
    const auto cleanup =
        qScopeGuard(
            [&transactionRoot]() {
                QDir(transactionRoot)
                    .removeRecursively();
            });

    fixture.hiddenConfig
        ->group(
            QStringLiteral("Containments"))
        .group(QStringLiteral("13"))
        .writeEntry(
            QStringLiteral("layoutId"),
            fixture.destinationLayout);
    QVERIFY(fixture.hiddenConfig->sync());

    const auto readFile =
        [](const QString &path) {
            QFile file(path);
            if (!file.open(
                    QIODevice::ReadOnly)) {
                return QByteArray{};
            }
            return file.readAll();
        };
    const QByteArray originBefore =
        readFile(fixture.originFile);
    const QByteArray destinationBefore =
        readFile(fixture.destinationFile);
    const QByteArray hiddenBefore =
        readFile(fixture.hiddenFile);
    QVERIFY(!originBefore.isEmpty());
    QVERIFY(!destinationBefore.isEmpty());
    QVERIFY(!hiddenBefore.isEmpty());

    QVERIFY(!Storage::self()
        ->recoverPendingViewMoves());
    QCOMPARE(
        Storage::self()
            ->pendingViewMoveTransactions(),
        QStringList{
            result.transactionPath});
    QCOMPARE(
        readFile(fixture.originFile),
        originBefore);
    QCOMPARE(
        readFile(fixture.destinationFile),
        destinationBefore);
    QCOMPARE(
        readFile(fixture.hiddenFile),
        hiddenBefore);
}

void StorageTest::
resumeRecoveryAfterEachRepositoryPublication_data()
{
    QTest::addColumn<bool>(
        "rollForward");
    QTest::addColumn<int>(
        "interruptionValue");

    const int afterFirst =
        static_cast<int>(
            Storage::
                ViewMoveRecoveryInterruption::
                    AfterFirstRepositoryPublication);
    const int afterSecond =
        static_cast<int>(
            Storage::
                ViewMoveRecoveryInterruption::
                    AfterSecondRepositoryPublication);
    QTest::newRow("rollback-after-origin")
        << false << afterFirst;
    QTest::newRow("rollback-after-active-owner")
        << false << afterSecond;
    QTest::newRow("rollforward-after-destination")
        << true << afterFirst;
    QTest::newRow("rollforward-after-active-owner")
        << true << afterSecond;
}

void StorageTest::
resumeRecoveryAfterEachRepositoryPublication()
{
    QFETCH(bool, rollForward);
    QFETCH(int, interruptionValue);

    const auto interruption =
        static_cast<Storage::
            ViewMoveRecoveryInterruption>(
                interruptionValue);
    const QString suffix =
        QStringLiteral("%1-%2")
            .arg(
                rollForward
                    ? QStringLiteral("forward")
                    : QStringLiteral("rollback"))
            .arg(interruptionValue);
    const DurableMoveFixture fixture =
        createDurableMoveFixture(
            suffix);
    QVERIFY(!fixture.originFile.isEmpty());

    const auto result =
        Storage::self()
            ->persistViewMoveSnapshot(
                fixture.originLayout,
                fixture.originFile,
                fixture.destinationLayout,
                fixture.destinationFile,
                fixture.hiddenConfig,
                12,
                fixture.snapshotFile,
                rollForward
                    ? Storage::
                        ViewMoveInterruption::
                            AfterCommitDecision
                    : Storage::
                        ViewMoveInterruption::
                            AfterDestinationPublish);
    QCOMPARE(
        result.status,
        rollForward
            ? Latte::Layouts::
                ViewMovePersistenceResult::
                    Status::
                        CommittedRecoveryRequired
            : Latte::Layouts::
                ViewMovePersistenceResult::
                    Status::
                        RejectedRecoveryRequired);
    QVERIFY(!result.transactionPath.isEmpty());
    const QString transactionRoot =
        QFileInfo(result.transactionPath)
            .absolutePath();
    const auto cleanup =
        qScopeGuard(
            [&transactionRoot]() {
                QDir(transactionRoot)
                    .removeRecursively();
            });

    //! Make the first repository publication a real repair instead of an
    //! already-satisfied no-op.
    const QString firstRepository =
        rollForward
            ? fixture.destinationFile
            : fixture.originFile;
    {
        const KSharedConfigPtr config =
            KSharedConfig::openConfig(
                firstRepository,
                KConfig::SimpleConfig);
        KConfigGroup containments(
            config,
            QStringLiteral("Containments"));
        containments.group(
            QStringLiteral("12"))
            .deleteGroup();
        containments.group(
            QStringLiteral("13"))
            .deleteGroup();
        QVERIFY(config->sync());
    }

    QVERIFY(!Storage::self()
        ->recoverPendingViewMovesIn(
            transactionRoot,
            fixture.hiddenFile,
            interruption));
    QCOMPARE(
        Storage::self()
            ->pendingViewMoveTransactions(),
        QStringList{
            result.transactionPath});
    {
        const KConfig firstRepositoryReadback(
            firstRepository,
            KConfig::SimpleConfig);
        const KConfigGroup containments =
            firstRepositoryReadback.group(
                QStringLiteral(
                    "Containments"));
        QVERIFY(containments.hasGroup(
            QStringLiteral("12")));
        QVERIFY(containments.hasGroup(
            QStringLiteral("13")));
    }

    QVERIFY(Storage::self()
        ->recoverPendingViewMoves());
    QVERIFY(Storage::self()
        ->pendingViewMoveTransactions()
        .isEmpty());

    const KConfig origin(
        fixture.originFile,
        KConfig::SimpleConfig);
    const KConfig destination(
        fixture.destinationFile,
        KConfig::SimpleConfig);
    const KConfig hidden(
        fixture.hiddenFile,
        KConfig::SimpleConfig);
    const KConfigGroup originContainments =
        origin.group(
            QStringLiteral("Containments"));
    const KConfigGroup destinationContainments =
        destination.group(
            QStringLiteral("Containments"));
    const KConfigGroup hiddenContainments =
        hidden.group(
            QStringLiteral("Containments"));
    QCOMPARE(
        originContainments.hasGroup(
            QStringLiteral("12")),
        !rollForward);
    QCOMPARE(
        destinationContainments.hasGroup(
            QStringLiteral("12")),
        rollForward);
    QCOMPARE(
        hiddenContainments
            .group(QStringLiteral("12"))
            .readEntry(
                QStringLiteral("layoutId"),
                QString()),
        rollForward
            ? fixture.destinationLayout
            : fixture.originLayout);
}

void StorageTest::
recoverAfterEachDirectoryFlushFailure_data()
{
    QTest::addColumn<int>(
        "failureValue");
    QTest::addColumn<int>(
        "expectedStatusValue");
    QTest::addColumn<QString>(
        "expectedOwner");
    QTest::addColumn<QString>(
        "expectedAction");
    QTest::addColumn<bool>(
        "rollForward");

    using FlushFailure =
        Storage::
            ViewMoveDirectoryFlushFailure;
    using Status =
        Latte::Layouts::
            ViewMovePersistenceResult::
                Status;
    QTest::newRow(
        "destination-publication")
        << static_cast<int>(
               FlushFailure::Destination)
        << static_cast<int>(
               Status::
                   RejectedRecoveryRequired)
        << QStringLiteral("origin")
        << QStringLiteral("rollBack")
        << false;
    QTest::newRow(
        "hidden-owner-publication")
        << static_cast<int>(
               FlushFailure::HiddenOwner)
        << static_cast<int>(
               Status::
                   RejectedRecoveryRequired)
        << QStringLiteral("destination")
        << QStringLiteral("rollForward")
        << true;
    QTest::newRow(
        "origin-retirement")
        << static_cast<int>(
               FlushFailure::Origin)
        << static_cast<int>(
               Status::
                   CommittedRecoveryRequired)
        << QStringLiteral("destination")
        << QStringLiteral("rollForward")
        << true;
}

void StorageTest::
recoverAfterEachDirectoryFlushFailure()
{
    QFETCH(int, failureValue);
    QFETCH(int, expectedStatusValue);
    QFETCH(QString, expectedOwner);
    QFETCH(QString, expectedAction);
    QFETCH(bool, rollForward);

    const auto failure =
        static_cast<Storage::
            ViewMoveDirectoryFlushFailure>(
                failureValue);
    const auto expectedStatus =
        static_cast<Latte::Layouts::
            ViewMovePersistenceResult::
                Status>(
                    expectedStatusValue);
    const DurableMoveFixture fixture =
        createDurableMoveFixture(
            QStringLiteral(
                "directory-flush-%1")
                .arg(failureValue));
    QVERIFY(!fixture.originFile.isEmpty());

    const auto result =
        Storage::self()
            ->persistViewMoveSnapshot(
                fixture.originLayout,
                fixture.originFile,
                fixture.destinationLayout,
                fixture.destinationFile,
                fixture.hiddenConfig,
                12,
                fixture.snapshotFile,
                Storage::
                    ViewMoveInterruption::
                        None,
                failure);
    QCOMPARE(
        result.status,
        expectedStatus);
    QVERIFY(!result.transactionPath.isEmpty());
    const QString transactionRoot =
        QFileInfo(result.transactionPath)
            .absolutePath();
    const auto cleanup =
        qScopeGuard(
            [&transactionRoot]() {
                QDir(transactionRoot)
                    .removeRecursively();
            });
    QCOMPARE(
        Storage::self()
            ->pendingViewMoveTransactions(),
        QStringList{
            result.transactionPath});

    const QJsonObject readback =
        QJsonDocument::fromJson(
            Storage::self()
                ->viewMoveTransactionsData()
                .toUtf8())
            .object();
    const QJsonArray transactions =
        readback.value(
            QStringLiteral(
                "transactions"))
            .toArray();
    QCOMPARE(transactions.size(), 1);
    const QJsonObject transaction =
        transactions.first()
            .toObject();
    QVERIFY(transaction.value(
        QStringLiteral(
            "journalValid"))
        .toBool());
    QCOMPARE(
        transaction.value(
            QStringLiteral(
                "persistentOwner"))
            .toString(),
        expectedOwner);
    QCOMPARE(
        transaction.value(
            QStringLiteral(
                "recoveryAction"))
            .toString(),
        expectedAction);

    //! Repeat the same durability fault during recovery. The endpoint's
    //! semantic state may converge, but the journal must remain until every
    //! containing-directory entry is durable.
    QVERIFY(!Storage::self()
        ->recoverPendingViewMovesIn(
            transactionRoot,
            fixture.hiddenFile,
            Storage::
                ViewMoveRecoveryInterruption::
                    None,
            failure));
    QCOMPARE(
        Storage::self()
            ->pendingViewMoveTransactions(),
        QStringList{
            result.transactionPath});

    QVERIFY(Storage::self()
        ->recoverPendingViewMoves());
    QVERIFY(Storage::self()
        ->pendingViewMoveTransactions()
        .isEmpty());

    const KConfig origin(
        fixture.originFile,
        KConfig::SimpleConfig);
    const KConfig destination(
        fixture.destinationFile,
        KConfig::SimpleConfig);
    const KConfig hidden(
        fixture.hiddenFile,
        KConfig::SimpleConfig);
    const KConfigGroup originContainments =
        origin.group(
            QStringLiteral("Containments"));
    const KConfigGroup destinationContainments =
        destination.group(
            QStringLiteral("Containments"));
    const KConfigGroup hiddenContainments =
        hidden.group(
            QStringLiteral("Containments"));
    for (const QString &id :
            {QStringLiteral("12"),
             QStringLiteral("13")}) {
        QCOMPARE(
            originContainments.hasGroup(id),
            !rollForward);
        QCOMPARE(
            destinationContainments
                .hasGroup(id),
            rollForward);
        QCOMPARE(
            hiddenContainments.group(id)
                .readEntry(
                    QStringLiteral(
                        "layoutId"),
                    QString()),
            rollForward
                ? fixture.destinationLayout
                : fixture.originLayout);
    }
}

void StorageTest::
repeatCompletedRecoveryIsIdempotent()
{
    const DurableMoveFixture fixture =
        createDurableMoveFixture(
            QStringLiteral(
                "idempotent-recovery"));
    QVERIFY(!fixture.originFile.isEmpty());

    const auto result =
        Storage::self()
            ->persistViewMoveSnapshot(
                fixture.originLayout,
                fixture.originFile,
                fixture.destinationLayout,
                fixture.destinationFile,
                fixture.hiddenConfig,
                12,
                fixture.snapshotFile,
                Storage::
                    ViewMoveInterruption::
                        AfterCommitDecision);
    QCOMPARE(
        result.status,
        Latte::Layouts::
            ViewMovePersistenceResult::
                Status::
                    CommittedRecoveryRequired);
    QVERIFY(Storage::self()
        ->recoverPendingViewMoves());

    const auto readFile =
        [](const QString &path) {
            QFile file(path);
            if (!file.open(
                    QIODevice::ReadOnly)) {
                return QByteArray{};
            }
            return file.readAll();
        };
    const QByteArray originAfterRecovery =
        readFile(fixture.originFile);
    const QByteArray destinationAfterRecovery =
        readFile(fixture.destinationFile);
    const QByteArray hiddenAfterRecovery =
        readFile(fixture.hiddenFile);
    QVERIFY(QFileInfo::exists(
        fixture.originFile));
    QVERIFY(!destinationAfterRecovery.isEmpty());
    QVERIFY(!hiddenAfterRecovery.isEmpty());

    QVERIFY(Storage::self()
        ->recoverPendingViewMoves());
    QVERIFY(Storage::self()
        ->pendingViewMoveTransactions()
        .isEmpty());
    QCOMPARE(
        readFile(fixture.originFile),
        originAfterRecovery);
    QCOMPARE(
        readFile(fixture.destinationFile),
        destinationAfterRecovery);
    QCOMPARE(
        readFile(fixture.hiddenFile),
        hiddenAfterRecovery);
}

void StorageTest::
refuseTraversalBearingRecoveryManifest()
{
    const DurableMoveFixture fixture =
        createDurableMoveFixture(
            QStringLiteral(
                "traversal-manifest"));
    QVERIFY(!fixture.originFile.isEmpty());

    const auto result =
        Storage::self()
            ->persistViewMoveSnapshot(
                fixture.originLayout,
                fixture.originFile,
                fixture.destinationLayout,
                fixture.destinationFile,
                fixture.hiddenConfig,
                12,
                fixture.snapshotFile,
                Storage::
                    ViewMoveInterruption::
                        AfterDestinationPublish);
    QCOMPARE(
        result.status,
        Latte::Layouts::
            ViewMovePersistenceResult::
                Status::
                    RejectedRecoveryRequired);
    QVERIFY(!result.transactionPath.isEmpty());
    const QString transactionRoot =
        QFileInfo(result.transactionPath)
            .absolutePath();
    const QString escapedFile =
        QDir::cleanPath(
            Latte::Layouts::Importer::
                layoutUserFilePath(
                    QStringLiteral(
                        "../escape-control")));
    const auto cleanup =
        qScopeGuard(
            [&transactionRoot,
             &escapedFile]() {
                QDir(transactionRoot)
                    .removeRecursively();
                QFile::remove(
                    escapedFile);
            });

    const QByteArray sentinel{
        "escape-control-sentinel\n"};
    QFile escaped(escapedFile);
    QVERIFY(escaped.open(
        QIODevice::WriteOnly
            | QIODevice::Truncate));
    QCOMPARE(
        escaped.write(sentinel),
        sentinel.size());
    escaped.close();

    const auto readFile =
        [](const QString &path) {
            QFile file(path);
            if (!file.open(
                    QIODevice::ReadOnly)) {
                return QByteArray{};
            }
            return file.readAll();
        };
    const QByteArray originBefore =
        readFile(fixture.originFile);
    const QByteArray destinationBefore =
        readFile(fixture.destinationFile);
    const QByteArray hiddenBefore =
        readFile(fixture.hiddenFile);
    QVERIFY(!originBefore.isEmpty());
    QVERIFY(!destinationBefore.isEmpty());
    QVERIFY(!hiddenBefore.isEmpty());

    const QString manifestPath =
        QDir(result.transactionPath)
            .filePath(
                QStringLiteral(
                    "manifest.json"));
    QFile manifest(manifestPath);
    QVERIFY(manifest.open(
        QIODevice::ReadOnly));
    QJsonParseError parseError;
    const QJsonDocument manifestDocument =
        QJsonDocument::fromJson(
            manifest.readAll(),
            &parseError);
    manifest.close();
    QCOMPARE(
        parseError.error,
        QJsonParseError::NoError);
    QVERIFY(manifestDocument.isObject());
    QJsonObject manifestObject =
        manifestDocument.object();
    manifestObject[
        QStringLiteral(
            "originLayout")] =
        QStringLiteral(
            "../escape-control");
    manifestObject[
        QStringLiteral(
            "originFile")] =
        QFileInfo(escapedFile)
            .canonicalFilePath();
    const QByteArray traversalPayload =
        QJsonDocument(manifestObject)
            .toJson(
                QJsonDocument::Compact);
    QVERIFY(manifest.open(
        QIODevice::WriteOnly
            | QIODevice::Truncate));
    QCOMPARE(
        manifest.write(
            traversalPayload),
        traversalPayload.size());
    manifest.close();

    QVERIFY(!Storage::self()
        ->recoverPendingViewMoves());
    QCOMPARE(
        Storage::self()
            ->pendingViewMoveTransactions(),
        QStringList{
            result.transactionPath});
    QCOMPARE(
        readFile(fixture.originFile),
        originBefore);
    QCOMPARE(
        readFile(fixture.destinationFile),
        destinationBefore);
    QCOMPARE(
        readFile(fixture.hiddenFile),
        hiddenBefore);
    QCOMPARE(
        readFile(escapedFile),
        sentinel);
}

void StorageTest::
recoverInterruptedDestinationStagingByRollingBack()
{
    const DurableMoveFixture fixture =
        createDurableMoveFixture(
            QStringLiteral("rollback"));
    QVERIFY(!fixture.originFile.isEmpty());

    const auto result =
        Storage::self()
            ->persistViewMoveSnapshot(
                fixture.originLayout,
                fixture.originFile,
                fixture.destinationLayout,
                fixture.destinationFile,
                fixture.hiddenConfig,
                12,
                fixture.snapshotFile,
                Storage::
                    ViewMoveInterruption::
                        AfterDestinationPublish);
    QCOMPARE(
        result.status,
        Latte::Layouts::
            ViewMovePersistenceResult::
                Status::
                    RejectedRecoveryRequired);
    QCOMPARE(
        Storage::self()
            ->pendingViewMoveTransactions()
            .size(),
        1);
    {
        const QJsonObject readback =
            QJsonDocument::fromJson(
                Storage::self()
                    ->viewMoveTransactionsData()
                    .toUtf8())
                .object();
        QCOMPARE(
            readback.value(
                QStringLiteral(
                    "schemaVersion"))
                .toInt(),
            2);
        const QJsonArray transactions =
            readback.value(
                QStringLiteral(
                    "transactions"))
                .toArray();
        QCOMPARE(transactions.size(), 1);
        const QJsonObject transaction =
            transactions.first()
                .toObject();
        QVERIFY(transaction.value(
            QStringLiteral(
                "journalValid"))
            .toBool());
        QCOMPARE(
            transaction.value(
                QStringLiteral(
                    "originLayout"))
                .toString(),
            fixture.originLayout);
        QCOMPARE(
            transaction.value(
                QStringLiteral(
                    "destinationLayout"))
                .toString(),
            fixture.destinationLayout);
        QCOMPARE(
            transaction.value(
                QStringLiteral(
                    "persistentOwner"))
                .toString(),
            QStringLiteral("origin"));
        QCOMPARE(
            transaction.value(
                QStringLiteral(
                    "recoveryAction"))
                .toString(),
            QStringLiteral("rollBack"));
    }

    {
        const KConfig destination(
            fixture.destinationFile,
            KConfig::SimpleConfig);
        QVERIFY(destination.group(
            QStringLiteral("Containments"))
            .hasGroup(
                QStringLiteral("12")));
    }

    QVERIFY(Storage::self()
        ->recoverPendingViewMoves());
    QVERIFY(Storage::self()
        ->pendingViewMoveTransactions()
        .isEmpty());

    const KConfig origin(
        fixture.originFile,
        KConfig::SimpleConfig);
    const KConfig destination(
        fixture.destinationFile,
        KConfig::SimpleConfig);
    const KConfig hidden(
        fixture.hiddenFile,
        KConfig::SimpleConfig);
    QVERIFY(origin.group(
        QStringLiteral("Containments"))
        .hasGroup(QStringLiteral("12")));
    QVERIFY(origin.group(
        QStringLiteral("Containments"))
        .hasGroup(QStringLiteral("13")));
    QVERIFY(!destination.group(
        QStringLiteral("Containments"))
        .hasGroup(QStringLiteral("12")));
    QVERIFY(!destination.group(
        QStringLiteral("Containments"))
        .hasGroup(QStringLiteral("13")));
    QCOMPARE(
        hidden.group(
            QStringLiteral("Containments"))
            .group(QStringLiteral("12"))
            .readEntry(
                QStringLiteral("layoutId"),
                QString()),
        fixture.originLayout);
}

void StorageTest::
recoverCommittedMoveByRollingForward()
{
    const DurableMoveFixture fixture =
        createDurableMoveFixture(
            QStringLiteral("rollforward"));
    QVERIFY(!fixture.originFile.isEmpty());
    const auto lifecycleBefore =
        parseDurableMoveLifecycleReadback(
            Storage::self()
                ->viewMoveTransactionsData());
    QVERIFY(lifecycleBefore);

    const auto result =
        Storage::self()
            ->persistViewMoveSnapshot(
                fixture.originLayout,
                fixture.originFile,
                fixture.destinationLayout,
                fixture.destinationFile,
                fixture.hiddenConfig,
                12,
                fixture.snapshotFile,
                Storage::
                    ViewMoveInterruption::
                        AfterCommitDecision);
    QCOMPARE(
        result.status,
        Latte::Layouts::
            ViewMovePersistenceResult::
                Status::
                    CommittedRecoveryRequired);
    QCOMPARE(
        Storage::self()
            ->pendingViewMoveTransactions()
            .size(),
        1);
    const auto lifecycleAfterCommit =
        parseDurableMoveLifecycleReadback(
            Storage::self()
                ->viewMoveTransactionsData());
    QVERIFY(lifecycleAfterCommit);
    QCOMPARE(
        lifecycleAfterCommit
            ->journalCreatedGeneration,
        lifecycleBefore
                ->journalCreatedGeneration
            + 1);
    QCOMPARE(
        lifecycleAfterCommit
            ->commitDecisionGeneration,
        lifecycleBefore
                ->commitDecisionGeneration
            + 1);
    QCOMPARE(
        lifecycleAfterCommit
            ->journalRetiredGeneration,
        lifecycleBefore
            ->journalRetiredGeneration);

    {
        const KConfig origin(
            fixture.originFile,
            KConfig::SimpleConfig);
        const KConfig destination(
            fixture.destinationFile,
            KConfig::SimpleConfig);
        const KConfig hidden(
            fixture.hiddenFile,
            KConfig::SimpleConfig);
        QVERIFY(origin.group(
            QStringLiteral("Containments"))
            .hasGroup(
                QStringLiteral("12")));
        QVERIFY(destination.group(
            QStringLiteral("Containments"))
            .hasGroup(
                QStringLiteral("12")));
        QCOMPARE(
            hidden.group(
                QStringLiteral("Containments"))
                .group(QStringLiteral("12"))
                .readEntry(
                    QStringLiteral("layoutId"),
                    QString()),
            fixture.destinationLayout);
    }

    QVERIFY(Storage::self()
        ->recoverPendingViewMoves());
    QVERIFY(Storage::self()
        ->pendingViewMoveTransactions()
        .isEmpty());
    const auto lifecycleAfterRecovery =
        parseDurableMoveLifecycleReadback(
            Storage::self()
                ->viewMoveTransactionsData());
    QVERIFY(lifecycleAfterRecovery);
    QCOMPARE(
        lifecycleAfterRecovery
            ->journalCreatedGeneration,
        lifecycleAfterCommit
            ->journalCreatedGeneration);
    QCOMPARE(
        lifecycleAfterRecovery
            ->commitDecisionGeneration,
        lifecycleAfterCommit
            ->commitDecisionGeneration);
    QCOMPARE(
        lifecycleAfterRecovery
            ->journalRetiredGeneration,
        lifecycleAfterCommit
                ->journalRetiredGeneration
            + 1);

    const KConfig origin(
        fixture.originFile,
        KConfig::SimpleConfig);
    const KConfig destination(
        fixture.destinationFile,
        KConfig::SimpleConfig);
    const KConfig hidden(
        fixture.hiddenFile,
        KConfig::SimpleConfig);
    QVERIFY(!origin.group(
        QStringLiteral("Containments"))
        .hasGroup(QStringLiteral("12")));
    QVERIFY(!origin.group(
        QStringLiteral("Containments"))
        .hasGroup(QStringLiteral("13")));
    QVERIFY(destination.group(
        QStringLiteral("Containments"))
        .hasGroup(QStringLiteral("12")));
    QVERIFY(destination.group(
        QStringLiteral("Containments"))
        .hasGroup(QStringLiteral("13")));
    QCOMPARE(
        hidden.group(
            QStringLiteral("Containments"))
            .group(QStringLiteral("12"))
            .readEntry(
                QStringLiteral("layoutId"),
                QString()),
        fixture.destinationLayout);
    QCOMPARE(
        hidden.group(
            QStringLiteral("Containments"))
            .group(QStringLiteral("13"))
            .readEntry(
                QStringLiteral("layoutId"),
                QString()),
        fixture.destinationLayout);
}

void StorageTest::
refuseRecoveryFromCorruptedJournal()
{
    const DurableMoveFixture fixture =
        createDurableMoveFixture(
            QStringLiteral(
                "corrupt-journal"));
    QVERIFY(!fixture.originFile.isEmpty());

    const auto result =
        Storage::self()
            ->persistViewMoveSnapshot(
                fixture.originLayout,
                fixture.originFile,
                fixture.destinationLayout,
                fixture.destinationFile,
                fixture.hiddenConfig,
                12,
                fixture.snapshotFile,
                Storage::
                    ViewMoveInterruption::
                        AfterCommitDecision);
    QCOMPARE(
        result.status,
        Latte::Layouts::
            ViewMovePersistenceResult::
                Status::
                    CommittedRecoveryRequired);
    QVERIFY(!result.transactionPath.isEmpty());
    const QString transactionRoot =
        QFileInfo(result.transactionPath)
            .absolutePath();
    const auto cleanup =
        qScopeGuard(
            [&transactionRoot]() {
                QDir(transactionRoot)
                    .removeRecursively();
            });

    QFile snapshot(
        QDir(result.transactionPath)
            .filePath(
                QStringLiteral(
                    "snapshot.latte")));
    QVERIFY(snapshot.open(
        QIODevice::WriteOnly
            | QIODevice::Append));
    QCOMPARE(
        snapshot.write(
            QByteArray(
                "\n# corruption control\n")),
        QByteArray(
            "\n# corruption control\n")
            .size());
    snapshot.close();

    const QJsonObject readback =
        QJsonDocument::fromJson(
            Storage::self()
                ->viewMoveTransactionsData()
                .toUtf8())
            .object();
    const QJsonObject transaction =
        readback.value(
            QStringLiteral(
                "transactions"))
            .toArray()
            .first()
            .toObject();
    QVERIFY(!transaction.value(
        QStringLiteral(
            "journalValid"))
        .toBool());
    QCOMPARE(
        transaction.value(
            QStringLiteral(
                "persistentOwner"))
            .toString(),
        QStringLiteral("unknown"));
    QCOMPARE(
        transaction.value(
            QStringLiteral(
                "recoveryAction"))
            .toString(),
        QStringLiteral("refuse"));
    QVERIFY(!Storage::self()
        ->recoverPendingViewMoves());
    QCOMPARE(
        Storage::self()
            ->pendingViewMoveTransactions()
            .size(),
        1);
}

void StorageTest::validatePersistedRelationshipGraphs_data()
{
    QTest::addColumn<QString>("shape");
    QTest::addColumn<bool>("valid");

    QTest::newRow("direct-root") << QStringLiteral("direct") << true;
    QTest::newRow("missing-root") << QStringLiteral("missing") << false;
    QTest::newRow("linked-chain") << QStringLiteral("chain") << false;
    QTest::newRow("self-cycle") << QStringLiteral("self") << false;
    QTest::newRow("two-member-cycle") << QStringLiteral("cycle") << false;
    QTest::newRow("nonnumeric-independent-id") << QStringLiteral("nonnumeric") << false;
    QTest::newRow("zero-independent-id") << QStringLiteral("zero") << false;
    QTest::newRow("leading-zero-independent-id") << QStringLiteral("leading-zero") << false;
    QTest::newRow("explicit-member-shared-screen-group")
        << QStringLiteral("explicit-multiscreen") << false;
}

void StorageTest::validatePersistedRelationshipGraphs()
{
    QFETCH(QString, shape);
    QFETCH(bool, valid);

    const QString path = m_dir.filePath(QStringLiteral("relationship-%1.layout.latte").arg(shape));
    KSharedConfigPtr config = KSharedConfig::openConfig(path);
    KConfigGroup containments(config, QStringLiteral("Containments"));

    const auto writeDock = [&containments](const QString &id,
                                           const int rootId,
                                           const Latte::Data::View::LinkPlacement placement,
                                           const Latte::Types::ScreensGroup screensGroup = Latte::Types::SingleScreenGroup) {
        KConfigGroup group = containments.group(id);
        group.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
        group.writeEntry(QStringLiteral("isClonedFrom"), rootId);
        group.writeEntry(QStringLiteral("linkPlacement"), static_cast<int>(placement));
        group.writeEntry(QStringLiteral("screensGroup"), static_cast<int>(screensGroup));
    };

    constexpr auto local = Latte::Data::View::LinkPlacement::ScreenGroupDerived;
    constexpr auto linked = Latte::Data::View::LinkPlacement::ExplicitTarget;
    const QString rootIdentity = shape == QStringLiteral("nonnumeric")
            ? QStringLiteral("dock")
            : shape == QStringLiteral("zero")
            ? QStringLiteral("0")
            : shape == QStringLiteral("leading-zero")
            ? QStringLiteral("01")
            : QStringLiteral("1");
    writeDock(rootIdentity, Latte::Data::View::ISCLONEDNULL, local);

    if (shape == QStringLiteral("direct")) {
        writeDock(QStringLiteral("2"), 1, linked);
    } else if (shape == QStringLiteral("missing")) {
        writeDock(QStringLiteral("2"), 99, linked);
    } else if (shape == QStringLiteral("chain")) {
        writeDock(QStringLiteral("2"), 1, linked);
        writeDock(QStringLiteral("3"), 2, linked);
    } else if (shape == QStringLiteral("self")) {
        writeDock(QStringLiteral("2"), 2, linked);
    } else if (shape == QStringLiteral("cycle")) {
        writeDock(QStringLiteral("2"), 3, linked);
        writeDock(QStringLiteral("3"), 2, linked);
    } else if (shape == QStringLiteral("explicit-multiscreen")) {
        writeDock(QStringLiteral("2"), 1, linked, Latte::Types::AllScreensGroup);
    }
    config->sync();

    const Latte::Data::ViewsTable views = Storage::self()->views(path);
    QCOMPARE(views.relationshipValidationError().isEmpty(), valid);
}

void StorageTest::restoreRemovalSnapshotReplacesPartialGroup()
{
    const QString snapshotPath = m_dir.filePath(QStringLiteral("removal-snapshot.layout.latte"));
    const QString destinationPath = m_dir.filePath(QStringLiteral("removal-destination.layout.latte"));

    {
        const KSharedConfigPtr snapshot =
            KSharedConfig::openConfig(snapshotPath, KConfig::SimpleConfig);
        KConfigGroup containments(snapshot, QStringLiteral("Containments"));
        KConfigGroup containment = containments.group(QStringLiteral("12"));
        containment.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
        containment.writeEntry(QStringLiteral("isClonedFrom"), 1);
        containment.writeEntry(QStringLiteral("linkPlacement"),
                               static_cast<int>(Latte::Data::View::LinkPlacement::ExplicitTarget));
        KConfigGroup applet = containment.group(QStringLiteral("Applets")).group(QStringLiteral("40"));
        applet.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.plasma.minimizeall"));
        snapshot->sync();
    }

    const KSharedConfigPtr activeConfig =
        KSharedConfig::openConfig(destinationPath, KConfig::SimpleConfig);
    {
        KConfigGroup containments(activeConfig, QStringLiteral("Containments"));
        KConfigGroup partial = containments.group(QStringLiteral("12"));
        partial.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
        partial.writeEntry(QStringLiteral("stalePartialValue"), true);
        QVERIFY(activeConfig->sync());
    }

    QVERIFY(Storage::self()->restoreView(activeConfig, snapshotPath));

    //! Model libplasma clearing a child transient marker after the root Undo
    //! signal returns. The restored subtree must remain owned by the same live
    //! repository through that later sync.
    activeConfig->group(QStringLiteral("Containments"))
        .group(QStringLiteral("12"))
        .group(QStringLiteral("Applets"))
        .group(QStringLiteral("40"))
        .deleteEntry(QStringLiteral("transient"));
    QVERIFY(activeConfig->sync());

    QFile rawDestination(destinationPath);
    QVERIFY(rawDestination.open(QIODevice::ReadOnly));
    const QString persisted = QString::fromUtf8(rawDestination.readAll());
    QVERIFY2(persisted.contains(QStringLiteral("[Containments][12]")),
             qPrintable(persisted));
    QVERIFY2(persisted.contains(QStringLiteral("[Containments][12][Applets][40]")),
             qPrintable(persisted));

    const KConfigGroup containments(activeConfig, QStringLiteral("Containments"));
    const KConfigGroup containment = containments.group(QStringLiteral("12"));
    QCOMPARE(containment.readEntry(QStringLiteral("isClonedFrom"), -1), 1);
    QCOMPARE(containment.readEntry(QStringLiteral("linkPlacement"), -1),
             static_cast<int>(Latte::Data::View::LinkPlacement::ExplicitTarget));
    QVERIFY(!containment.hasKey(QStringLiteral("stalePartialValue")));
    QCOMPARE(containment.group(QStringLiteral("Applets")).group(QStringLiteral("40"))
                 .readEntry(QStringLiteral("plugin"), QString{}),
             QStringLiteral("org.kde.plasma.minimizeall"));
}

void StorageTest::tombstoneRemovalSnapshotDeletesExactGroupsOnDisk()
{
    const QString snapshotPath = m_dir.filePath(QStringLiteral("tombstone-snapshot.layout.latte"));
    const QString destinationPath = writeLayoutFixture(
        QStringLiteral("tombstone-destination.layout.latte"));

    {
        const KSharedConfigPtr snapshot =
            KSharedConfig::openConfig(snapshotPath, KConfig::SimpleConfig);
        KConfigGroup containments(snapshot, QStringLiteral("Containments"));
        containments.group(QStringLiteral("1"))
            .writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
        containments.group(QStringLiteral("99"))
            .writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.plasma.private.systemtray"));
        QVERIFY(snapshot->sync());
    }

    const KSharedConfigPtr activeConfig =
        KSharedConfig::openConfig(destinationPath, KConfig::SimpleConfig);
    QVERIFY(Storage::self()->tombstoneViewFromSnapshot(activeConfig, snapshotPath));
    QVERIFY(Storage::self()->tombstoneViewFromSnapshot(activeConfig, snapshotPath));

    QFile rawLayout(destinationPath);
    QVERIFY(rawLayout.open(QIODevice::ReadOnly));
    const QString persisted = QString::fromUtf8(rawLayout.readAll());
    QVERIFY2(!persisted.contains(QStringLiteral("[Containments][1]")),
             qPrintable(persisted));
    QVERIFY2(!persisted.contains(QStringLiteral("[Containments][99]")),
             qPrintable(persisted));
    QVERIFY2(persisted.contains(QStringLiteral("[Containments][5]")),
             qPrintable(persisted));
}

void StorageTest::removalPersistenceReportsWriteFailure()
{
    const QString snapshotPath = m_dir.filePath(
        QStringLiteral("write-failure-snapshot.layout.latte"));
    {
        const KSharedConfigPtr snapshot =
            KSharedConfig::openConfig(
                snapshotPath,
                KConfig::SimpleConfig);
        snapshot->group(QStringLiteral("Containments"))
            .group(QStringLiteral("12"))
            .writeEntry(
                QStringLiteral("plugin"),
                QStringLiteral(
                    "org.kde.latte.containment"));
        QVERIFY(snapshot->sync());
    }

    const QString unavailablePath =
        m_dir.filePath(
            QStringLiteral(
                "unwritable.layout.latte"));
    QVERIFY(QDir().mkpath(unavailablePath));
    const KSharedConfigPtr unavailable =
        KSharedConfig::openConfig(
            unavailablePath,
            KConfig::SimpleConfig);
    unavailable->group(
        QStringLiteral("Containments"))
        .group(QStringLiteral("12"))
        .writeEntry(
            QStringLiteral("plugin"),
            QStringLiteral(
                "org.kde.latte.containment"));

    QVERIFY(!Storage::self()->restoreView(
        unavailable,
        snapshotPath));
    QVERIFY(!Storage::self()
        ->tombstoneViewFromSnapshot(
            unavailable,
            snapshotPath));
    QVERIFY2(
        QFileInfo(unavailablePath).isDir(),
        qPrintable(unavailablePath));
}

void StorageTest::classifyLayoutPersistenceEndpoints()
{
    const QString writablePath =
        writeLayoutFixture(
            QStringLiteral(
                "writable-endpoint"));
    Latte::CentralLayout writableLayout(
        nullptr,
        writablePath,
        QStringLiteral("writable"));
    QVERIFY(writableLayout.isWritable());

    QFile existingFile(writablePath);
    const QFileDevice::Permissions originalPermissions =
        existingFile.permissions();
    const auto restoreExistingPermissions =
        qScopeGuard(
            [&existingFile,
             originalPermissions]() {
                existingFile.setPermissions(
                    originalPermissions);
            });

    QVERIFY(existingFile.setPermissions(
        QFileDevice::ReadUser
        | QFileDevice::ReadGroup
        | QFileDevice::ReadOther));
    QVERIFY(!writableLayout.isWritable());
    QVERIFY(existingFile.setPermissions(
        originalPermissions));

    const KSharedConfigPtr existingRepository =
        KSharedConfig::openConfig(
            writablePath,
            KConfig::SimpleConfig);
    QVERIFY(existingFile.setPermissions(
        QFileDevice::WriteUser));
    QVERIFY(
        QFileInfo(writablePath)
            .isWritable());
    QVERIFY(
        !QFileInfo(writablePath)
            .isReadable());
    QVERIFY(!writableLayout.isWritable());
    existingRepository
        ->group(QStringLiteral(
            "LayoutSettings"))
        .writeEntry(
            QStringLiteral(
                "writeOnlyProbe"),
            true);
    QVERIFY(!existingRepository->sync());
    QVERIFY(existingFile.setPermissions(
        originalPermissions));
    QVERIFY(existingRepository->sync());

    const QString absentPath =
        writeLayoutFixture(
            QStringLiteral(
                "creatable-endpoint"));
    Latte::CentralLayout absentLayout(
        nullptr,
        absentPath,
        QStringLiteral("absent"));
    QVERIFY(QFile::remove(absentPath));
    QVERIFY(!QFileInfo::exists(absentPath));
    QVERIFY(absentLayout.isWritable());

    const QString directoryPath =
        m_dir.filePath(
            QStringLiteral(
                "directory-endpoint.layout.latte"));
    QVERIFY(QDir().mkpath(directoryPath));
    Latte::CentralLayout directoryLayout(
        nullptr,
        directoryPath,
        QStringLiteral("directory"));
    QVERIFY(!directoryLayout.isWritable());

    const QString missingParentPath =
        m_dir.filePath(
            QStringLiteral("missing-parent"));
    QVERIFY(!QFileInfo::exists(
        missingParentPath));
    const QString missingParentEndpointPath =
        QDir(missingParentPath)
            .filePath(
                QStringLiteral(
                    "absent.layout.latte"));
    PersistenceEndpointLayout
        missingParentLayout(
            missingParentEndpointPath);
    QCOMPARE(
        missingParentLayout.file(),
        missingParentEndpointPath);
    QVERIFY(!missingParentLayout.isWritable());

    const QString regularFileParentPath =
        writeLayoutFixture(
            QStringLiteral(
                "regular-file-parent"));
    QVERIFY(
        QFileInfo(regularFileParentPath)
            .isFile());
    const QString regularFileParentEndpointPath =
        QDir(regularFileParentPath)
            .filePath(
                QStringLiteral(
                    "absent.layout.latte"));
    PersistenceEndpointLayout
        regularFileParentLayout(
            regularFileParentEndpointPath);
    QCOMPARE(
        regularFileParentLayout.file(),
        regularFileParentEndpointPath);
    QVERIFY(!regularFileParentLayout.isWritable());

    const QString restrictedParentPath =
        m_dir.filePath(
            QStringLiteral("restricted-parent"));
    QVERIFY(QDir().mkpath(
        restrictedParentPath));
    QFile restrictedParent(
        restrictedParentPath);
    const QFileDevice::Permissions
        originalParentPermissions =
            restrictedParent.permissions();
    const auto restoreParentPermissions =
        qScopeGuard(
            [&restrictedParent,
             originalParentPermissions]() {
                restrictedParent.setPermissions(
                    originalParentPermissions);
            });

    const QString nestedExistingPath =
        QDir(restrictedParentPath)
            .filePath(
                QStringLiteral(
                    "existing.layout.latte"));
    QFile nestedExistingFile(
        nestedExistingPath);
    QVERIFY(nestedExistingFile.open(
        QIODevice::WriteOnly));
    QVERIFY(nestedExistingFile.write(
        "[LayoutSettings]\nversion=2\n")
        > 0);
    nestedExistingFile.close();
    Latte::CentralLayout nestedExistingLayout(
        nullptr,
        nestedExistingPath,
        QStringLiteral("nested-existing"));

    const QString nestedAbsentPath =
        QDir(restrictedParentPath)
            .filePath(
                QStringLiteral(
                    "absent.layout.latte"));
    {
        QFile nestedAbsentFile(
            nestedAbsentPath);
        QVERIFY(nestedAbsentFile.open(
            QIODevice::WriteOnly));
        QVERIFY(nestedAbsentFile.write(
            "[LayoutSettings]\nversion=2\n")
            > 0);
    }
    Latte::CentralLayout nestedAbsentLayout(
        nullptr,
        nestedAbsentPath,
        QStringLiteral("nested-absent"));
    QVERIFY(QFile::remove(
        nestedAbsentPath));

    QVERIFY(restrictedParent.setPermissions(
        QFileDevice::ReadUser
        | QFileDevice::ExeUser));
    QVERIFY(
        QFileInfo(nestedExistingPath)
            .isWritable());
    QVERIFY(!nestedExistingLayout.isWritable());
    QVERIFY(!nestedAbsentLayout.isWritable());

    QVERIFY(restrictedParent.setPermissions(
        QFileDevice::ReadUser
        | QFileDevice::WriteUser));
    QVERIFY(
        QFileInfo(restrictedParentPath)
            .isWritable());
    QVERIFY(
        !QFileInfo(restrictedParentPath)
            .isExecutable());
    QVERIFY(!nestedAbsentLayout.isWritable());

    QVERIFY(restrictedParent.setPermissions(
        originalParentPermissions));

    const QString canonicalParentPath =
        m_dir.filePath(
            QStringLiteral("canonical-parent"));
    QVERIFY(QDir().mkpath(
        canonicalParentPath));
    QFile canonicalParent(
        canonicalParentPath);
    const QFileDevice::Permissions
        originalCanonicalParentPermissions =
            canonicalParent.permissions();
    const auto restoreCanonicalParentPermissions =
        qScopeGuard(
            [&canonicalParent,
             originalCanonicalParentPermissions]() {
                canonicalParent.setPermissions(
                    originalCanonicalParentPermissions);
            });
    const QString canonicalTargetPath =
        QDir(canonicalParentPath)
            .filePath(
                QStringLiteral(
                    "target.layout.latte"));
    QFile canonicalTarget(
        canonicalTargetPath);
    QVERIFY(canonicalTarget.open(
        QIODevice::WriteOnly));
    QVERIFY(canonicalTarget.write(
        "[LayoutSettings]\nversion=2\n")
        > 0);
    canonicalTarget.close();

    const QString symbolicEndpointPath =
        m_dir.filePath(
            QStringLiteral(
                "symbolic-endpoint.layout.latte"));
    QVERIFY(QFile::link(
        canonicalTargetPath,
        symbolicEndpointPath));
    QVERIFY(
        QFileInfo(symbolicEndpointPath)
            .isSymLink());
    Latte::CentralLayout symbolicLayout(
        nullptr,
        symbolicEndpointPath,
        QStringLiteral("symbolic"));
    QVERIFY(symbolicLayout.isWritable());

    QVERIFY(canonicalParent.setPermissions(
        QFileDevice::ReadUser
        | QFileDevice::ExeUser));
    QVERIFY(
        QFileInfo(symbolicEndpointPath)
            .isWritable());
    QVERIFY(!symbolicLayout.isWritable());
    QVERIFY(canonicalParent.setPermissions(
        originalCanonicalParentPermissions));
}

void StorageTest::tombstoneRemovalSnapshotRefreshesStaleSharedRepository()
{
    const QString snapshotPath = m_dir.filePath(
        QStringLiteral("stale-tombstone-snapshot.layout.latte"));
    const QString destinationPath = m_dir.filePath(
        QStringLiteral("stale-tombstone-destination.layout.latte"));

    {
        const KSharedConfigPtr snapshot =
            KSharedConfig::openConfig(snapshotPath, KConfig::SimpleConfig);
        snapshot->group(QStringLiteral("Containments"))
            .group(QStringLiteral("12"))
            .writeEntry(QStringLiteral("plugin"),
                        QStringLiteral("org.kde.latte.containment"));
        QVERIFY(snapshot->sync());
    }

    const KSharedConfigPtr staleRepository =
        KSharedConfig::openConfig(destinationPath);
    KConfigGroup staleContainments(staleRepository,
                                   QStringLiteral("Containments"));
    staleContainments.group(QStringLiteral("1"))
        .writeEntry(QStringLiteral("plugin"),
                    QStringLiteral("org.kde.latte.containment"));
    QVERIFY(staleRepository->sync());

    const KSharedConfigPtr activeConfig =
        KSharedConfig::openConfig(destinationPath, KConfig::SimpleConfig);
    {
        //! Model a live Corona projection that adds a dock after the stale
        //! FullConfig observer was opened.
        activeConfig->group(QStringLiteral("Containments"))
            .group(QStringLiteral("12"))
            .writeEntry(QStringLiteral("plugin"),
                        QStringLiteral("org.kde.latte.containment"));
        QVERIFY(activeConfig->sync());
    }

    QVERIFY(!staleContainments.hasGroup(QStringLiteral("12")));
    QVERIFY(Storage::self()->tombstoneViewFromSnapshot(activeConfig,
                                                       snapshotPath));
    staleContainments.group(QStringLiteral("1"))
        .writeEntry(QStringLiteral("observerWrite"), true);
    QVERIFY(staleRepository->sync());

    QFile rawLayout(destinationPath);
    QVERIFY(rawLayout.open(QIODevice::ReadOnly));
    const QString persisted = QString::fromUtf8(rawLayout.readAll());
    QVERIFY2(persisted.contains(QStringLiteral("[Containments][1]")),
             qPrintable(persisted));
    QVERIFY2(!persisted.contains(QStringLiteral("[Containments][12]")),
             qPrintable(persisted));
}

QString StorageTest::writeLayoutFixture(const QString &name)
{
    const QString path = m_dir.filePath(name);
    KSharedConfigPtr ptr = KSharedConfig::openConfig(path);
    KConfigGroup conts(ptr, QStringLiteral("Containments"));

    KConfigGroup c1 = conts.group(QStringLiteral("1"));
    c1.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
    c1.writeEntry(QStringLiteral("name"), QStringLiteral("My Dock"));
    c1.writeEntry(QStringLiteral("location"), (int)Plasma::Types::LeftEdge);
    c1.writeEntry(QStringLiteral("onPrimary"), false);
    c1.writeEntry(QStringLiteral("lastScreen"), 12);
    c1.writeEntry(QStringLiteral("screensGroup"), (int)Latte::Types::AllSecondaryScreensGroup);
    c1.writeEntry(QStringLiteral("isClonedFrom"), 5);
    c1.group(QStringLiteral("General")).writeEntry(QStringLiteral("maxLength"), (float)80.0);
    c1.group(QStringLiteral("General")).writeEntry(QStringLiteral("alignment"), (int)Latte::Types::Justify);
    c1.group(QStringLiteral("General")).writeEntry(QStringLiteral("screenEdgeMargin"), 7);

    KConfigGroup applets = c1.group(QStringLiteral("Applets"));
    KConfigGroup a2 = applets.group(QStringLiteral("2"));
    a2.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.plasmoid"));
    KConfigGroup a3 = applets.group(QStringLiteral("3"));
    a3.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.plasma.private.systemtray"));
    a3.group(QStringLiteral("Configuration")).writeEntry(QStringLiteral("SystrayContainmentId"), 99);

    KConfigGroup c5 = conts.group(QStringLiteral("5"));
    c5.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.desktopcontainment"));
    KConfigGroup a6 = c5.group(QStringLiteral("Applets")).group(QStringLiteral("6"));
    a6.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.plasma.kickoff"));

    KConfigGroup c99 = conts.group(QStringLiteral("99"));
    c99.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.plasma.private.systemtray"));

    ptr->sync();
    return path;
}

void StorageTest::pinIdSentinelsAndValidity()
{
    QCOMPARE(Storage::IDNULL, -1);
    QCOMPARE(Storage::IDBASE, 0);
    QVERIFY(!Storage::isValid(-1));
    QVERIFY(Storage::isValid(0));
    QVERIFY(Storage::isValid(99));
}

void StorageTest::classifyLatteContainmentByPlugin()
{
    KConfig cfg(m_dir.filePath(QStringLiteral("plugincheck.latte")));

    KConfigGroup latte = cfg.group(QStringLiteral("latte"));
    latte.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
    QVERIFY(Storage::self()->isLatteContainment(latte));

    KConfigGroup other = cfg.group(QStringLiteral("other"));
    other.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.plasma.desktop"));
    QVERIFY(!Storage::self()->isLatteContainment(other));

    KConfigGroup empty = cfg.group(QStringLiteral("empty"));
    QVERIFY(!Storage::self()->isLatteContainment(empty));
}

void StorageTest::resolveSubContainmentIdFromBothIdentityKeys()
{
    const QString path = writeLayoutFixture(QStringLiteral("subs.latte"));
    KSharedConfigPtr ptr = KSharedConfig::openConfig(path);
    KConfigGroup applets =
        KConfigGroup(ptr, QStringLiteral("Containments")).group(QStringLiteral("1")).group(QStringLiteral("Applets"));

    // the systray applet declares [Configuration]SystrayContainmentId 99
    QCOMPARE(Storage::self()->subContainmentId(applets.group(QStringLiteral("3"))), 99);

    // a plain plasmoid is not a subcontainment
    QCOMPARE(Storage::self()->subContainmentId(applets.group(QStringLiteral("2"))), Storage::IDNULL);

    // ContainmentId is the second recognized identity key
    KConfig cfg(m_dir.filePath(QStringLiteral("groupapplet.latte")));
    KConfigGroup grp = cfg.group(QStringLiteral("g"));
    grp.group(QStringLiteral("Configuration")).writeEntry(QStringLiteral("ContainmentId"), 42);
    QCOMPARE(Storage::self()->subContainmentId(grp), 42);
}

void StorageTest::rejectPreloadShellAsAppletGroup()
{
    KConfig cfg(m_dir.filePath(QStringLiteral("validity.latte")));

    // no own keys, only [Configuration]PreloadWeight: the husk a removed
    // applet leaves behind, not a real applet
    KConfigGroup shell = cfg.group(QStringLiteral("shell"));
    shell.group(QStringLiteral("Configuration")).writeEntry(QStringLiteral("PreloadWeight"), 42);
    QVERIFY(!Storage::appletGroupIsValid(shell));

    // a real applet has a plugin key
    KConfigGroup real = cfg.group(QStringLiteral("real"));
    real.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.plasmoid"));
    real.group(QStringLiteral("Configuration")).writeEntry(QStringLiteral("PreloadWeight"), 42);
    QVERIFY(Storage::appletGroupIsValid(real));
}

void StorageTest::deserializeViewFromContainmentGroup()
{
    const QString path = writeLayoutFixture(QStringLiteral("viewread.latte"));
    KSharedConfigPtr ptr = KSharedConfig::openConfig(path);
    KConfigGroup c1 = KConfigGroup(ptr, QStringLiteral("Containments")).group(QStringLiteral("1"));

    Latte::Data::View v = Storage::self()->view(c1);

    QVERIFY(v.isValid());
    QCOMPARE(v.id, QStringLiteral("1"));
    QCOMPARE(v.name, QStringLiteral("My Dock"));
    QCOMPARE(v.onPrimary, false);
    QCOMPARE(v.screen, 12);
    QCOMPARE(v.isClonedFrom, 5);
    QCOMPARE(v.linkPlacement, Latte::Data::View::LinkPlacement::ScreenGroupDerived);
    QCOMPARE(v.screenEdgeMargin, 7);
    QCOMPARE(v.screensGroup, Latte::Types::AllSecondaryScreensGroup);
    QCOMPARE(v.edge, Plasma::Types::LeftEdge);
    QCOMPARE(v.maxLength, (float)80.0);
    QCOMPARE(v.alignment, Latte::Types::Justify);

    // the systray applet under this view is reported as a subcontainment
    QCOMPARE(v.subcontainments.rowCount(), 1);
    QCOMPARE(v.subcontainments[(uint)0].id, QStringLiteral("99"));
}

void StorageTest::deserializeViewDefaultsForUnsetKeys()
{
    // a bare Latte containment with nothing but the plugin key: view() must
    // hand back the documented default for every field, not garbage
    KConfig cfg(m_dir.filePath(QStringLiteral("defaults.latte")));
    KConfigGroup g = cfg.group(QStringLiteral("Containments")).group(QStringLiteral("21"));
    g.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));

    Latte::Data::View v = Storage::self()->view(g);

    QVERIFY(v.isValid());
    QCOMPARE(v.name, QString());
    QCOMPARE(v.onPrimary, true);
    QCOMPARE(v.screen, Storage::IDNULL);
    QCOMPARE(v.isClonedFrom, Latte::Data::View::ISCLONEDNULL);
    QCOMPARE(v.linkPlacement, Latte::Data::View::LinkPlacement::ScreenGroupDerived);
    QCOMPARE(v.screenEdgeMargin, -1);
    QCOMPARE(v.screensGroup, Latte::Types::SingleScreenGroup);
    QCOMPARE(v.edge, Plasma::Types::BottomEdge);
    QCOMPARE(v.maxLength, (float)100.0);
    QCOMPARE(v.alignment, Latte::Types::Center);
    QCOMPARE(v.subcontainments.rowCount(), 0);
}

void StorageTest::refuseMalformedLinkedPlacement()
{
    KConfig cfg(m_dir.filePath(QStringLiteral("malformed-linked-placement.latte")));

    KConfigGroup invalidEnum = cfg.group(QStringLiteral("Containments")).group(QStringLiteral("31"));
    invalidEnum.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
    invalidEnum.writeEntry(QStringLiteral("isClonedFrom"), 1);
    invalidEnum.writeEntry(QStringLiteral("linkPlacement"), 99);
    QVERIFY(!Storage::self()->view(invalidEnum).isValid());

    KConfigGroup missingRoot = cfg.group(QStringLiteral("Containments")).group(QStringLiteral("32"));
    missingRoot.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
    missingRoot.writeEntry(QStringLiteral("isClonedFrom"), Latte::Data::View::ISCLONEDNULL);
    missingRoot.writeEntry(
        QStringLiteral("linkPlacement"),
        static_cast<int>(Latte::Data::View::LinkPlacement::ExplicitTarget));
    QVERIFY(!Storage::self()->view(missingRoot).isValid());
}

void StorageTest::refuseViewForNonLatteContainment()
{
    KConfig cfg(m_dir.filePath(QStringLiteral("nonlatte.latte")));
    KConfigGroup g = cfg.group(QStringLiteral("Containments")).group(QStringLiteral("5"));
    g.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.desktopcontainment"));
    g.writeEntry(QStringLiteral("name"), QStringLiteral("desktop"));

    Latte::Data::View v = Storage::self()->view(g);
    QVERIFY(!v.isValid());
    QVERIFY(v.name.isEmpty());
}

void StorageTest::roundTripViewThroughKConfig()
{
    const QString path = m_dir.filePath(QStringLiteral("update.latte"));
    {
        KSharedConfigPtr ptr = KSharedConfig::openConfig(path);
        KConfigGroup g = KConfigGroup(ptr, QStringLiteral("Containments")).group(QStringLiteral("7"));
        g.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
        g.sync();

        Latte::Data::View nv;
        nv.name = QStringLiteral("Written");
        nv.screensGroup = Latte::Types::AllScreensGroup;
        nv.onPrimary = false;
        nv.isClonedFrom = 4;
        nv.linkPlacement = Latte::Data::View::LinkPlacement::ExplicitTarget;
        nv.screen = 13;
        nv.screenEdgeMargin = 9;
        nv.edge = Plasma::Types::TopEdge;
        nv.maxLength = (float)55.0;
        nv.alignment = Latte::Types::Justify;
        Storage::self()->updateView(g, nv);
    }

    // read back from a fresh handle: a real on-disk round trip through view()
    KConfig fresh(path);
    KConfigGroup g = fresh.group(QStringLiteral("Containments")).group(QStringLiteral("7"));
    Latte::Data::View r = Storage::self()->view(g);

    QCOMPARE(r.name, QStringLiteral("Written"));
    QCOMPARE(r.screensGroup, Latte::Types::AllScreensGroup);
    QCOMPARE(r.onPrimary, false);
    QCOMPARE(r.isClonedFrom, 4);
    QCOMPARE(r.linkPlacement, Latte::Data::View::LinkPlacement::ExplicitTarget);
    QCOMPARE(r.screen, 13);
    QCOMPARE(r.screenEdgeMargin, 9);
    QCOMPARE(r.edge, Plasma::Types::TopEdge);
    QCOMPARE(r.maxLength, (float)55.0);
    QCOMPARE(r.alignment, Latte::Types::Justify);

    // maxLength must serialize under [General] where view() reads it; a
    // containment-level write is the dead key the fix right before this test
    // retired (upstream inheritance, fork parallel b48903ec)
    QCOMPARE(g.readEntry(QStringLiteral("maxLength"), (float)-1.0), (float)-1.0);
    QCOMPARE(g.group(QStringLiteral("General")).readEntry(QStringLiteral("maxLength"), (float)-1.0), (float)55.0);
}

void StorageTest::refuseUpdateViewOnNonLatteGroup()
{
    // updateView() must not scribble view keys over a foreign containment
    const QString path = m_dir.filePath(QStringLiteral("foreign.latte"));
    KSharedConfigPtr ptr = KSharedConfig::openConfig(path);
    KConfigGroup g = KConfigGroup(ptr, QStringLiteral("Containments")).group(QStringLiteral("8"));
    g.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.desktopcontainment"));
    g.sync();

    Latte::Data::View nv;
    nv.name = QStringLiteral("MustNotLand");
    Storage::self()->updateView(g, nv);

    KConfig fresh(path);
    KConfigGroup freshGroup = fresh.group(QStringLiteral("Containments")).group(QStringLiteral("8"));
    QVERIFY(!freshGroup.hasKey(QStringLiteral("name")));
}

void StorageTest::enumerateOnlyLatteContainmentsAsViews()
{
    const QString path = writeLayoutFixture(QStringLiteral("viewsfile.latte"));
    Latte::Data::ViewsTable table = Storage::self()->views(path);

    // only containment 1 is org.kde.latte.containment; 5 and 99 are not views
    QCOMPARE(table.rowCount(), 1);
    QVERIFY(table.containsId(QStringLiteral("1")));
    QVERIFY(!table.containsId(QStringLiteral("5")));
    QVERIFY(!table.containsId(QStringLiteral("99")));
    QCOMPARE(table[(uint)0].name, QStringLiteral("My Dock"));

    // 99 is reachable only as view 1's subcontainment, not as its own view
    QVERIFY(table.hasContainmentId(QStringLiteral("1")));
    QVERIFY(table.hasContainmentId(QStringLiteral("99")));
    QVERIFY(!table.hasContainmentId(QStringLiteral("5")));
}

void StorageTest::listSubcontainmentsOfContainmentGroup()
{
    const QString path = writeLayoutFixture(QStringLiteral("subsfromgroup.latte"));
    KSharedConfigPtr ptr = KSharedConfig::openConfig(path);
    KConfigGroup conts(ptr, QStringLiteral("Containments"));

    Latte::Data::GenericTable<Latte::Data::Generic> subs =
        Storage::self()->subcontainments(conts.group(QStringLiteral("1")));
    QCOMPARE(subs.rowCount(), 1);
    QCOMPARE(subs[(uint)0].id, QStringLiteral("99"));

    // a non-Latte containment group yields no subcontainments
    QCOMPARE(Storage::self()->subcontainments(conts.group(QStringLiteral("5"))).rowCount(), 0);
}

void StorageTest::reportContainsViewOnlyForLatteIds()
{
    const QString path = writeLayoutFixture(QStringLiteral("contains.latte"));

    QVERIFY(Storage::self()->containsView(path, 1));
    QVERIFY(!Storage::self()->containsView(path, 5));    // exists but not Latte
    QVERIFY(!Storage::self()->containsView(path, 99));   // exists but not Latte
    QVERIFY(!Storage::self()->containsView(path, 1234)); // missing
}

void StorageTest::enumerateViewsOfInactiveLayout()
{
    const QString path = writeLayoutFixture(QStringLiteral("viewslayout.latte"));
    Latte::CentralLayout layout(nullptr, path, QStringLiteral("viewslayout"));
    QVERIFY(!layout.isActive());

    Latte::Data::ViewsTable table = Storage::self()->views(&layout);
    QCOMPARE(table.rowCount(), 1);
    QVERIFY(table.containsId(QStringLiteral("1")));
}

void StorageTest::detectClonedViewsOnlyForLatteContainments()
{
    KConfig cfg(m_dir.filePath(QStringLiteral("clones.latte")));
    KConfigGroup conts = cfg.group(QStringLiteral("Containments"));

    KConfigGroup cloned = conts.group(QStringLiteral("10"));
    cloned.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
    cloned.writeEntry(QStringLiteral("isClonedFrom"), 3);
    QVERIFY(Storage::self()->isClonedView(cloned));

    // default isClonedFrom == ISCLONEDNULL: not a clone
    KConfigGroup notcloned = conts.group(QStringLiteral("11"));
    notcloned.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
    QVERIFY(!Storage::self()->isClonedView(notcloned));

    // a non-Latte containment is never a cloned view even with isClonedFrom set
    KConfigGroup nonlatte = conts.group(QStringLiteral("12"));
    nonlatte.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.desktopcontainment"));
    nonlatte.writeEntry(QStringLiteral("isClonedFrom"), 3);
    QVERIFY(!Storage::self()->isClonedView(nonlatte));
}

void StorageTest::removeScreenGroupDerivedViewsKeepsPersistentRelationships()
{
    const QString path = m_dir.filePath(QStringLiteral("clonesremove.latte"));
    {
        KSharedConfigPtr ptr = KSharedConfig::openConfig(path);
        KConfigGroup conts(ptr, QStringLiteral("Containments"));
        KConfigGroup c1 = conts.group(QStringLiteral("1"));
        c1.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
        KConfigGroup c10 = conts.group(QStringLiteral("10"));
        c10.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
        c10.writeEntry(QStringLiteral("isClonedFrom"), 1);
        KConfigGroup c11 = conts.group(QStringLiteral("11"));
        c11.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
        c11.writeEntry(QStringLiteral("isClonedFrom"), 1);
        c11.writeEntry(
            QStringLiteral("linkPlacement"),
            static_cast<int>(Latte::Data::View::LinkPlacement::ExplicitTarget));
        ptr->sync();
    }

    Storage::self()->removeScreenGroupDerivedViews(path);

    KConfig fresh(path);
    KConfigGroup conts = fresh.group(QStringLiteral("Containments"));
    QVERIFY(conts.hasGroup(QStringLiteral("1")));    // relationship root kept
    QVERIFY(!conts.hasGroup(QStringLiteral("10"))); // derived replica removed
    QVERIFY(conts.hasGroup(QStringLiteral("11")));  // explicit linked member kept
}

void StorageTest::removeContainmentDeletesExactlyThatGroup()
{
    const QString path = writeLayoutFixture(QStringLiteral("removecont.latte"));

    Storage::self()->removeContainment(path, QStringLiteral("5"));

    KConfig fresh(path);
    KConfigGroup conts = fresh.group(QStringLiteral("Containments"));
    QVERIFY(!conts.hasGroup(QStringLiteral("5")));
    QVERIFY(conts.hasGroup(QStringLiteral("1")));

    // an empty id and a missing id are no-ops that must not touch the file
    Storage::self()->removeContainment(path, QString());
    Storage::self()->removeContainment(path, QStringLiteral("777"));
    KConfig after(path);
    QVERIFY(after.group(QStringLiteral("Containments")).hasGroup(QStringLiteral("1")));
    QVERIFY(after.group(QStringLiteral("Containments")).hasGroup(QStringLiteral("99")));
}

void StorageTest::removeViewDropsViewAndItsSubcontainments()
{
    const QString path = writeLayoutFixture(QStringLiteral("removeview.latte"));

    KSharedConfigPtr ptr = KSharedConfig::openConfig(path);
    KConfigGroup c1 = KConfigGroup(ptr, QStringLiteral("Containments")).group(QStringLiteral("1"));
    Latte::Data::View v = Storage::self()->view(c1);
    QCOMPARE(v.subcontainments.rowCount(), 1);

    Storage::self()->removeView(path, v);

    KConfig fresh(path);
    KConfigGroup conts = fresh.group(QStringLiteral("Containments"));
    QVERIFY(!conts.hasGroup(QStringLiteral("1")));
    QVERIFY(!conts.hasGroup(QStringLiteral("99")));
    QVERIFY(conts.hasGroup(QStringLiteral("5"))); // untouched

    // an invalid view is a no-op
    Latte::Data::View invalid;
    Storage::self()->removeView(path, invalid);
    QVERIFY(KConfig(path).group(QStringLiteral("Containments")).hasGroup(QStringLiteral("5")));
}

void StorageTest::resolveNoScreenIdWithoutCorona()
{
    // the Corona overload short-circuits to NOSCREENID when corona is null,
    // regardless of the view payload
    Latte::Data::View v;
    v.setState(Latte::Data::View::IsCreated);
    v.onPrimary = true;
    QCOMPARE(Storage::self()->expectedViewScreenId(static_cast<Latte::Corona *>(nullptr), v),
             Latte::ScreenPool::NOSCREENID);
}

void StorageTest::reportNoErrorsOrWarningsForCleanLayout()
{
    // a well-formed layout: unique applet ids and every non-Latte containment
    // reachable as some view's subcontainment - neither scanner may cry wolf.
    // The shared fixture does not qualify: its foreign desktop containment
    // (id 5) hangs off no view, which IS an orphan to the warnings scanner.
    const QString path = m_dir.filePath(QStringLiteral("clean.latte"));
    {
        KSharedConfigPtr ptr = KSharedConfig::openConfig(path);
        KConfigGroup conts(ptr, QStringLiteral("Containments"));
        KConfigGroup c1 = conts.group(QStringLiteral("1"));
        c1.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
        KConfigGroup applets = c1.group(QStringLiteral("Applets"));
        applets.group(QStringLiteral("2")).writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.plasmoid"));
        KConfigGroup a3 = applets.group(QStringLiteral("3"));
        a3.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.plasma.private.systemtray"));
        a3.group(QStringLiteral("Configuration")).writeEntry(QStringLiteral("SystrayContainmentId"), 99);
        conts.group(QStringLiteral("99")).writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.plasma.private.systemtray"));
        ptr->sync();
    }
    Latte::CentralLayout layout(nullptr, path, QStringLiteral("clean"));
    QVERIFY(!layout.isActive());

    QVERIFY(Storage::self()->errors(&layout).isEmpty());
    QVERIFY(Storage::self()->warnings(&layout).isEmpty());
}

void StorageTest::reportDuplicateAppletIdsAsError()
{
    // two Latte containments each carry applet id "7" -> APPLETSWITHSAMEID
    const QString path = m_dir.filePath(QStringLiteral("dupapplets.latte"));
    {
        KSharedConfigPtr ptr = KSharedConfig::openConfig(path);
        KConfigGroup conts(ptr, QStringLiteral("Containments"));
        for (const QString &cid : {QStringLiteral("1"), QStringLiteral("2")}) {
            KConfigGroup c = conts.group(cid);
            c.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
            KConfigGroup a = c.group(QStringLiteral("Applets")).group(QStringLiteral("7"));
            a.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.plasmoid"));
        }
        ptr->sync();
    }

    Latte::CentralLayout layout(nullptr, path, QStringLiteral("dupapplets"));
    QVERIFY(!layout.isActive());

    const Latte::Data::ErrorsList errs = Storage::self()->errors(&layout);
    QCOMPARE(errs.count(), 1);
    QCOMPARE(errs[0].id, QString(QLatin1String(Latte::Data::Error::APPLETSWITHSAMEID)));
    QCOMPARE(errs[0].information.rowCount(), 2); // both occurrences of "7"
}

void StorageTest::reportOrphanedSubcontainmentAsWarning()
{
    // a non-Latte containment reachable from no view -> ORPHANEDSUBCONTAINMENT
    const QString path = m_dir.filePath(QStringLiteral("orphansub.latte"));
    {
        KSharedConfigPtr ptr = KSharedConfig::openConfig(path);
        KConfigGroup conts(ptr, QStringLiteral("Containments"));

        KConfigGroup c1 = conts.group(QStringLiteral("1"));
        c1.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));

        KConfigGroup c5 = conts.group(QStringLiteral("5"));
        c5.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.plasma.private.systemtray"));
        ptr->sync();
    }

    Latte::CentralLayout layout(nullptr, path, QStringLiteral("orphansub"));
    QVERIFY(!layout.isActive());

    const Latte::Data::WarningsList warns = Storage::self()->warnings(&layout);
    QCOMPARE(warns.count(), 1);
    QCOMPARE(warns[0].id, QString(QLatin1String(Latte::Data::Warning::ORPHANEDSUBCONTAINMENT)));
}

void StorageTest::stripUnapprovedAppletsFromExportedTemplate()
{
    // both applets carry a [Configuration] subgroup so the strip is
    // observable: exportTemplate deletes the config subgroups of unapproved,
    // non-subcontainment applets and leaves the approved applet intact.
    // (a dedicated non-clone fixture: exportTemplate's derived-view cleanup
    // pass would drop the shared fixture's legacy derived containment)
    const QString origin = m_dir.filePath(QStringLiteral("exportsrc.latte"));
    {
        KSharedConfigPtr ptr = KSharedConfig::openConfig(origin);
        KConfigGroup conts(ptr, QStringLiteral("Containments"));
        KConfigGroup c1 = conts.group(QStringLiteral("1"));
        c1.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
        c1.writeEntry(QStringLiteral("layoutId"), QStringLiteral("SomeLayout"));
        c1.writeEntry(QStringLiteral("isPreferredForShortcuts"), true);
        KConfigGroup applets = c1.group(QStringLiteral("Applets"));

        KConfigGroup a2 = applets.group(QStringLiteral("2"));
        a2.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.plasmoid"));
        a2.group(QStringLiteral("Configuration")).writeEntry(QStringLiteral("keep"), QStringLiteral("yes"));

        KConfigGroup a3 = applets.group(QStringLiteral("3"));
        a3.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.plasma.private.systemtray"));
        a3.group(QStringLiteral("Configuration")).writeEntry(QStringLiteral("gone"), QStringLiteral("soon"));
        ptr->sync();
    }
    const QString dest = m_dir.filePath(QStringLiteral("exported.latte"));

    // approve only the plasmoid (applet "2"); the systray applet "3" is not
    Latte::Data::AppletsTable approved;
    Latte::Data::Applet a;
    a.id = QStringLiteral("org.kde.latte.plasmoid");
    approved << a;

    QVERIFY(Storage::self()->exportTemplate(origin, dest, approved));
    QVERIFY(QFile(dest).exists());

    KConfig cfg(dest);
    KConfigGroup c1 = cfg.group(QStringLiteral("Containments")).group(QStringLiteral("1"));
    KConfigGroup exportedApplets = c1.group(QStringLiteral("Applets"));

    // unapproved applet 3: its configuration subgroup is stripped, but the
    // applet's own plugin key survives (the strip removes config, not applets)
    QVERIFY(exportedApplets.group(QStringLiteral("3")).groupList().isEmpty());
    QCOMPARE(exportedApplets.group(QStringLiteral("3")).readEntry(QStringLiteral("plugin"), QString()),
             QStringLiteral("org.kde.plasma.private.systemtray"));

    // approved applet 2: configuration and plugin survive untouched
    QCOMPARE(exportedApplets.group(QStringLiteral("2")).group(QStringLiteral("Configuration")).readEntry(QStringLiteral("keep"), QString()),
             QStringLiteral("yes"));

    // per-layout identity is cleared on every exported containment
    QCOMPARE(c1.readEntry(QStringLiteral("layoutId"), QStringLiteral("x")), QString());
    QCOMPARE(c1.readEntry(QStringLiteral("isPreferredForShortcuts"), true), false);
}

void StorageTest::clearLayoutSettingsInExportedTemplate()
{
    // an exported template must not leak the origin's activity assignment or
    // shortcut preference (clearExportedLayoutSettings)
    const QString origin = m_dir.filePath(QStringLiteral("settingssrc.latte"));
    {
        KSharedConfigPtr ptr = KSharedConfig::openConfig(origin);
        KConfigGroup c1 = KConfigGroup(ptr, QStringLiteral("Containments")).group(QStringLiteral("1"));
        c1.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));

        KConfigGroup settings(ptr, QStringLiteral("LayoutSettings"));
        settings.writeEntry(QStringLiteral("preferredForShortcutsTouched"), true);
        settings.writeEntry(QStringLiteral("lastUsedActivity"), QStringLiteral("someactivity"));
        settings.writeEntry(QStringLiteral("activities"), QStringList{QStringLiteral("a1"), QStringLiteral("a2")});
        ptr->sync();
    }
    const QString dest = m_dir.filePath(QStringLiteral("settingsexported.latte"));

    QVERIFY(Storage::self()->exportTemplate(origin, dest, Latte::Data::AppletsTable()));

    KConfig cfg(dest);
    KConfigGroup settings = cfg.group(QStringLiteral("LayoutSettings"));
    QCOMPARE(settings.readEntry(QStringLiteral("preferredForShortcutsTouched"), true), false);
    QCOMPARE(settings.readEntry(QStringLiteral("lastUsedActivity"), QStringLiteral("x")), QString());
    QCOMPARE(settings.readEntry(QStringLiteral("activities"), QStringList{QStringLiteral("x")}), QStringList());
}

void StorageTest::fallBackToPluginIdForUnknownApplet()
{
    // an unknown plugin id yields data named after the id itself - the
    // "not installed here" marker the settings dialogs display
    Latte::Data::Applet data = Storage::self()->metadata(QStringLiteral("org.kde.nonexistent.applet.xyz"));
    QCOMPARE(data.id, QStringLiteral("org.kde.nonexistent.applet.xyz"));
    QCOMPARE(data.name, QStringLiteral("org.kde.nonexistent.applet.xyz"));
}

void StorageTest::gatherAppletPluginsFilteredByContainmentId()
{
    const QString path = writeLayoutFixture(QStringLiteral("pluginsfile.latte"));

    // containment 1 (and its subcontainment 99): plasmoid + systray, but NOT
    // the kickoff applet living in the foreign containment 5
    Latte::Data::AppletsTable scoped = Storage::self()->plugins(path, 1);
    QCOMPARE(scoped.rowCount(), 2);
    QVERIFY(scoped.containsId(QStringLiteral("org.kde.latte.plasmoid")));
    QVERIFY(scoped.containsId(QStringLiteral("org.kde.plasma.private.systemtray")));
    QVERIFY(!scoped.containsId(QStringLiteral("org.kde.plasma.kickoff")));

    // IDNULL means all containments: kickoff joins the table exactly once
    Latte::Data::AppletsTable all = Storage::self()->plugins(path, Storage::IDNULL);
    QCOMPARE(all.rowCount(), 3);
    QVERIFY(all.containsId(QStringLiteral("org.kde.plasma.kickoff")));
}

void StorageTest::remapIdsWhenAddingTemplateViewToInactiveLayout()
{
    // the destination inactive layout already owns id "1"; the origin
    // template also uses "1". newView() must remap the incoming ids and
    // import the result, so the returned view carries a fresh id and the
    // destination ends with both views present.
    const QString destPath = writeLayoutFixture(QStringLiteral("dest.latte"));
    Latte::CentralLayout dest(nullptr, destPath, QStringLiteral("dest"));
    QVERIFY(!dest.isActive());

    const QString originPath = writeLayoutFixture(QStringLiteral("origin.latte"));

    Latte::Data::View nextViewData;
    nextViewData.setState(Latte::Data::View::IsCreated, originPath);

    Latte::Data::View added = Storage::self()->newView(&dest, nextViewData);
    QVERIFY(added.isValid());
    QVERIFY(!added.id.isEmpty());
    QVERIFY(added.id != QStringLiteral("1")); // remapped away from the collision

    Latte::Data::ViewsTable table = Storage::self()->views(destPath);
    QCOMPARE(table.rowCount(), 2);
    QVERIFY(table.containsId(QStringLiteral("1")));
    QVERIFY(table.containsId(added.id));
}

void StorageTest::writeStoredViewOfInactiveLayoutToTempFile()
{
    const QString path = writeLayoutFixture(QStringLiteral("storedview.latte"));
    Latte::CentralLayout layout(nullptr, path, QStringLiteral("storedview"));
    QVERIFY(!layout.isActive());

    const QString stored = Storage::self()->storedView(&layout, 1);
    QVERIFY(!stored.isEmpty());
    QVERIFY(QFile(stored).exists());

    // the stored file carries the view containment and its subcontainment
    KConfig cfg(stored);
    KConfigGroup conts = cfg.group(QStringLiteral("Containments"));
    QVERIFY(conts.hasGroup(QStringLiteral("1")));
    QVERIFY(conts.hasGroup(QStringLiteral("99")));

    // a non-existent containment id yields an empty path
    QVERIFY(Storage::self()->storedView(&layout, 4242).isEmpty());
}

int main(int argc, char *argv[])
{
    // Point the XDG homes at a throwaway dir before QGuiApplication:
    // CentralLayout/Storage only touch explicit file paths, but metadata()
    // walks KPackage lookups and nothing here may read or write the real
    // desktop's config (same pattern as screenpooltest).
    static QTemporaryDir xdgHome;
    qputenv("XDG_CONFIG_HOME", (xdgHome.path() + QStringLiteral("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (xdgHome.path() + QStringLiteral("/data")).toUtf8());

    QGuiApplication app(argc, argv);
    StorageTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "storagetest.moc"
