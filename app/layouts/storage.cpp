/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "storage.h"

// local
#include "storageidremapper.h"
#include <coretypes.h>
#include "importer.h"
#include "manager.h"
#include "../lattecorona.h"
#include "../screenpool.h"
#include "../data/errordata.h"
#include "../data/linkedconfigurationpolicy.h"
#include "../data/viewdata.h"
#include "../layout/abstractlayout.h"
#include "../layout/genericlayout.h"
#include "../view/view.h"

// Qt
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QLockFile>
#include <QSaveFile>
#include <QUuid>

// KDE
#include <KConfig>
#include <KConfigGroup>
#include <KPluginMetaData>
#include <KSharedConfig>
#include <KPackage/Package>
#include <KPackage/PackageLoader>

// Plasma
#include <Plasma/Plasma>
#include <Plasma/Applet>
#include <Plasma/Containment>

// C++
#include <fcntl.h>
#include <limits>
#include <optional>
#include <ranges>
#include <unistd.h>
#include <utility>

namespace Latte {
namespace Layouts {

namespace {

constexpr int ViewMoveJournalSchemaVersion = 1;
constexpr auto ViewMoveTransactionsDirectory =
    ".view-move-transactions";
constexpr auto ViewMoveManifestFile = "manifest.json";
constexpr auto ViewMoveSnapshotFile = "snapshot.latte";
constexpr auto ViewMovePreparedSuffix = ".prepare";
constexpr auto ViewMovePendingSuffix = ".move";
constexpr auto ViewMoveCompletedSuffix = ".complete";
constexpr QFileDevice::Permissions
    ViewMovePrivateDirectoryPermissions =
        QFileDevice::ReadOwner
        | QFileDevice::WriteOwner
        | QFileDevice::ExeOwner;

struct ExistingFilePersistenceFacts final
{
    bool isRegularFile;
    bool isReadable;
    bool isWritable;
    uint ownerId;
};

[[nodiscard]] constexpr bool supportsKConfigAtomicReplacement(
    const ExistingFilePersistenceFacts facts,
    const uint processOwnerId) noexcept
{
    return facts.isRegularFile
        && facts.isReadable
        && facts.isWritable
        && facts.ownerId == processOwnerId;
}

//! KConfig selects QSaveFile only for process-owned files. These compile-time
//! controls prevent an ownership check from being removed as redundant and
//! silently selecting its direct truncate-and-write branch.
static_assert(supportsKConfigAtomicReplacement(
    ExistingFilePersistenceFacts{
        .isRegularFile = true,
        .isReadable = true,
        .isWritable = true,
        .ownerId = 1000U},
    1000U));
static_assert(!supportsKConfigAtomicReplacement(
    ExistingFilePersistenceFacts{
        .isRegularFile = true,
        .isReadable = true,
        .isWritable = true,
        .ownerId = 1001U},
    1000U));
static_assert(!supportsKConfigAtomicReplacement(
    ExistingFilePersistenceFacts{
        .isRegularFile = true,
        .isReadable = false,
        .isWritable = true,
        .ownerId = 1000U},
    1000U));

[[nodiscard]] bool persistenceEndpointIsWritable(
    const QString &path)
{
    const QFileInfo requestedFileInfo(path);
    const bool existingEndpoint =
        requestedFileInfo.exists();
    if (requestedFileInfo.isSymLink()
            && !existingEndpoint) {
        return false;
    }

    //! KConfig canonicalizes every existing path before opening its backend.
    //! Classifying the lexical symlink parent can approve a directory that the
    //! QSaveFile replacement never uses.
    const QString backendFilePath =
        existingEndpoint
            ? requestedFileInfo
                .canonicalFilePath()
            : requestedFileInfo
                .absoluteFilePath();
    if (backendFilePath.isEmpty()) {
        qCritical()
            << "layout storage could not resolve persistence endpoint"
            << path;
        return false;
    }

    const QFileInfo backendFileInfo(
        backendFilePath);
    const QFileInfo parentDirectory(
        backendFileInfo.absolutePath());

    //! KConfig reparses an existing file before persisting through QSaveFile,
    //! which creates and renames a replacement in the containing directory.
    //! Read and write permission alone do not prove that either phase uses the
    //! atomic branch: KConfig writes non-owned files directly.
    const bool parentSupportsReplacement =
        parentDirectory.exists()
        && parentDirectory.isDir()
        && parentDirectory.isWritable()
        && parentDirectory.isExecutable();
    if (!parentSupportsReplacement) {
        return false;
    }

    return !existingEndpoint
        || supportsKConfigAtomicReplacement(
            ExistingFilePersistenceFacts{
                .isRegularFile =
                    backendFileInfo.isFile(),
                .isReadable =
                    backendFileInfo.isReadable(),
                .isWritable =
                    backendFileInfo.isWritable(),
                .ownerId =
                    backendFileInfo.ownerId()},
            static_cast<uint>(::getuid()));
}

[[nodiscard]] bool persistConfigurationOrReportFailure(
    const KSharedConfigPtr &config,
    const char *const operation)
{
    if (config->sync()) {
        return true;
    }

    qCritical() << operation
                << "could not write"
                << config->name();
    return false;
}

struct ViewMoveJournalRecord final
{
    QString transactionId;
    QString originLayoutName;
    QString originFile;
    QString destinationLayoutName;
    QString destinationFile;
    QString hiddenFile;
    uint rootContainmentId{0};
    QStringList containmentIds;
    QByteArray snapshotSha256;
    QString directoryPath;

    [[nodiscard]] QString snapshotPath() const
    {
        return QDir(directoryPath).filePath(
            QString::fromLatin1(
                ViewMoveSnapshotFile));
    }

    [[nodiscard]] QString manifestPath() const
    {
        return QDir(directoryPath).filePath(
            QString::fromLatin1(
                ViewMoveManifestFile));
    }
};

[[nodiscard]] QString persistenceBackendPath(
    const QString &path)
{
    const QFileInfo fileInfo(path);
    return fileInfo.exists()
        ? fileInfo.canonicalFilePath()
        : fileInfo.absoluteFilePath();
}

[[nodiscard]] bool layoutNameIsSafe(
    const QString &name)
{
    return !name.isEmpty()
        && name != QStringLiteral(".")
        && name != QStringLiteral("..")
        && !name.contains(QLatin1Char('/'))
        && !name.contains(QLatin1Char('\\'))
        && QFileInfo(name).fileName()
            == name;
}

[[nodiscard]] bool endpointIsDirectLayoutChild(
    const QString &path)
{
    const QString layoutDirectory =
        QFileInfo(
            Importer::layoutUserDir())
            .canonicalFilePath();
    const QString endpointDirectory =
        QFileInfo(
            QFileInfo(path)
                .absolutePath())
            .canonicalFilePath();
    return !layoutDirectory.isEmpty()
        && endpointDirectory
            == layoutDirectory;
}

[[nodiscard]] bool transactionFileIsSafe(
    const QString &path)
{
    const QFileInfo file(path);
    return file.exists()
        && file.isFile()
        && !file.isSymLink()
        && file.isReadable()
        && file.ownerId()
            == static_cast<uint>(::getuid());
}

[[nodiscard]] QString viewMoveTransactionsRoot()
{
    return QDir(Importer::layoutUserDir())
        .filePath(
            QString::fromLatin1(
                ViewMoveTransactionsDirectory));
}

[[nodiscard]] bool journalEndpointsAreConsistent(
    const ViewMoveJournalRecord &journal,
    const QString &expectedHiddenFile)
{
    if (!layoutNameIsSafe(
            journal.originLayoutName)
            || !layoutNameIsSafe(
                journal
                    .destinationLayoutName)
            || journal.originLayoutName
                == journal
                    .destinationLayoutName
            || !endpointIsDirectLayoutChild(
                journal.originFile)
            || !endpointIsDirectLayoutChild(
                journal.destinationFile)
            || !endpointIsDirectLayoutChild(
                journal.hiddenFile)
            || !endpointIsDirectLayoutChild(
                expectedHiddenFile)) {
        return false;
    }

    return journal.originFile
            == persistenceBackendPath(
                Importer::
                    layoutUserFilePath(
                        journal
                            .originLayoutName))
        && journal.destinationFile
            == persistenceBackendPath(
                Importer::
                    layoutUserFilePath(
                        journal
                            .destinationLayoutName))
        && journal.hiddenFile
            == expectedHiddenFile
        && journal.originFile
            != journal
                .destinationFile;
}

[[nodiscard]] bool flushDirectory(
    const QString &path)
{
    const QByteArray encodedPath =
        QFile::encodeName(path);
    const int descriptor =
        ::open(encodedPath.constData(),
               O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (descriptor < 0) {
        qCritical() << "view move transaction could not open directory for durable flush"
                    << path;
        return false;
    }

    const bool flushed = ::fsync(descriptor) == 0;
    ::close(descriptor);
    if (!flushed) {
        qCritical() << "view move transaction could not durably flush directory"
                    << path;
    }
    return flushed;
}

[[nodiscard]] QByteArray fileSha256(
    const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QCryptographicHash hash(
        QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return {};
    }
    return hash.result();
}

[[nodiscard]] bool copyFileAtomically(
    const QString &sourcePath,
    const QString &destinationPath)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        qCritical() << "view move transaction could not read snapshot"
                    << sourcePath;
        return false;
    }

    const QByteArray payload = source.readAll();
    if (source.error() != QFileDevice::NoError) {
        qCritical() << "view move transaction could not finish reading snapshot"
                    << sourcePath;
        return false;
    }

    QSaveFile destination(destinationPath);
    destination.setDirectWriteFallback(false);
    if (!destination.open(QIODevice::WriteOnly)
            || destination.write(payload)
                != payload.size()
            || !destination.commit()) {
        qCritical() << "view move transaction could not persist snapshot"
                    << destinationPath;
        return false;
    }
    return flushDirectory(
        QFileInfo(destinationPath)
            .absolutePath());
}

//! KConfig owns this lock while sync() runs, so it cannot remain held by the
//! transaction. This immediate probe rejects a lock that is already
//! contended. A later contender is handled by the durable journal and exact
//! semantic readback instead of being mistaken for a successful move.
[[nodiscard]] bool kConfigEndpointLockIsAvailable(
    const QString &path)
{
    QLockFile endpointLock(
        path + QStringLiteral(".lock"));
    endpointLock.setStaleLockTime(0);
    return endpointLock.tryLock(0);
}

[[nodiscard]] QJsonObject journalJson(
    const ViewMoveJournalRecord &journal)
{
    QJsonArray containmentIds;
    for (const QString &id :
            journal.containmentIds) {
        containmentIds.append(id);
    }

    QJsonObject json;
    json[QStringLiteral("schemaVersion")] =
        ViewMoveJournalSchemaVersion;
    json[QStringLiteral("transactionId")] =
        journal.transactionId;
    json[QStringLiteral("originLayout")] =
        journal.originLayoutName;
    json[QStringLiteral("originFile")] =
        journal.originFile;
    json[QStringLiteral("destinationLayout")] =
        journal.destinationLayoutName;
    json[QStringLiteral("destinationFile")] =
        journal.destinationFile;
    json[QStringLiteral("hiddenFile")] =
        journal.hiddenFile;
    json[QStringLiteral("rootContainmentId")] =
        static_cast<qint64>(
            journal.rootContainmentId);
    json[QStringLiteral("containmentIds")] =
        containmentIds;
    json[QStringLiteral("snapshotSha256")] =
        QString::fromLatin1(
            journal.snapshotSha256.toHex());
    return json;
}

[[nodiscard]] bool writeJournalManifest(
    const ViewMoveJournalRecord &journal)
{
    QSaveFile manifest(
        journal.manifestPath());
    manifest.setDirectWriteFallback(false);
    const QByteArray payload =
        QJsonDocument(journalJson(journal))
            .toJson(QJsonDocument::Compact);
    if (!manifest.open(QIODevice::WriteOnly)
            || manifest.write(payload)
                != payload.size()
            || !manifest.commit()) {
        qCritical() << "view move transaction could not persist manifest"
                    << journal.manifestPath();
        return false;
    }
    return flushDirectory(journal.directoryPath);
}

[[nodiscard]] bool promotePreparedJournal(
    ViewMoveJournalRecord &journal,
    const QString &transactionsRoot)
{
    const QFileInfo preparedInfo(
        journal.directoryPath);
    if (!preparedInfo.exists()
            || !preparedInfo.isDir()
            || preparedInfo.absolutePath()
                != QFileInfo(
                    transactionsRoot)
                    .absoluteFilePath()
            || !preparedInfo.fileName()
                .endsWith(
                    QString::fromLatin1(
                        ViewMovePreparedSuffix))) {
        qCritical() << "view move transaction refused an invalid prepared journal"
                    << journal.directoryPath;
        return false;
    }

    QString pendingName =
        preparedInfo.fileName();
    pendingName.chop(
        QString::fromLatin1(
            ViewMovePreparedSuffix)
            .size());
    pendingName +=
        QString::fromLatin1(
            ViewMovePendingSuffix);
    QDir root(transactionsRoot);
    if (!root.rename(
            preparedInfo.fileName(),
            pendingName)) {
        qCritical() << "view move transaction could not publish prepared journal"
                    << journal.directoryPath;
        return false;
    }

    journal.directoryPath =
        root.filePath(pendingName);
    return flushDirectory(
        root.absolutePath());
}

[[nodiscard]] bool discardPreparedJournal(
    const QString &path,
    const QString &transactionsRoot)
{
    const QFileInfo preparedInfo(path);
    if (!preparedInfo.exists()) {
        return true;
    }
    if (!preparedInfo.isDir()
            || preparedInfo.absolutePath()
                != QFileInfo(
                    transactionsRoot)
                    .absoluteFilePath()
            || !preparedInfo.fileName()
                .endsWith(
                    QString::fromLatin1(
                        ViewMovePreparedSuffix))) {
        qCritical() << "view move transaction refused invalid prepared cleanup path"
                    << path;
        return false;
    }
    return QDir(preparedInfo.absoluteFilePath())
            .removeRecursively()
        && flushDirectory(
            transactionsRoot);
}

[[nodiscard]] std::optional<ViewMoveJournalRecord>
readJournalManifest(
    const QString &directoryPath)
{
    const QString manifestPath =
        QDir(directoryPath).filePath(
            QString::fromLatin1(
                ViewMoveManifestFile));
    if (!transactionFileIsSafe(
            manifestPath)) {
        qCritical() << "view move recovery refused an unsafe manifest in"
                    << directoryPath;
        return std::nullopt;
    }
    QFile manifest(manifestPath);
    if (!manifest.open(QIODevice::ReadOnly)) {
        qCritical() << "view move recovery could not read manifest in"
                    << directoryPath;
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(
            manifest.readAll(),
            &parseError);
    if (parseError.error
            != QJsonParseError::NoError
            || !document.isObject()) {
        qCritical() << "view move recovery found a malformed manifest in"
                    << directoryPath
                    << parseError.errorString();
        return std::nullopt;
    }

    const QJsonObject json = document.object();
    const QJsonValue schema =
        json.value(QStringLiteral(
            "schemaVersion"));
    const QJsonValue rootId =
        json.value(QStringLiteral(
            "rootContainmentId"));
    const QJsonValue ids =
        json.value(QStringLiteral(
            "containmentIds"));
    if (!schema.isDouble()
            || schema.toInt()
                != ViewMoveJournalSchemaVersion
            || !rootId.isDouble()
            || rootId.toDouble() <= 0
            || rootId.toDouble()
                > std::numeric_limits<uint>::max()
            || !ids.isArray()) {
        qCritical() << "view move recovery found invalid typed fields in"
                    << directoryPath;
        return std::nullopt;
    }

    ViewMoveJournalRecord journal;
    journal.directoryPath =
        QFileInfo(directoryPath)
            .absoluteFilePath();
    journal.transactionId =
        json.value(QStringLiteral(
            "transactionId")).toString();
    journal.originLayoutName =
        json.value(QStringLiteral(
            "originLayout")).toString();
    journal.originFile =
        json.value(QStringLiteral(
            "originFile")).toString();
    journal.destinationLayoutName =
        json.value(QStringLiteral(
            "destinationLayout")).toString();
    journal.destinationFile =
        json.value(QStringLiteral(
            "destinationFile")).toString();
    journal.hiddenFile =
        json.value(QStringLiteral(
            "hiddenFile")).toString();
    journal.rootContainmentId =
        static_cast<uint>(rootId.toDouble());
    journal.snapshotSha256 =
        QByteArray::fromHex(
            json.value(QStringLiteral(
                "snapshotSha256"))
                .toString()
                .toLatin1());
    for (const QJsonValue id : ids.toArray()) {
        if (!id.isString()
                || id.toString().isEmpty()) {
            qCritical() << "view move recovery found an invalid containment id in"
                        << directoryPath;
            return std::nullopt;
        }
        journal.containmentIds.append(
            id.toString());
    }

    const QString rootIdString =
        QString::number(
            journal.rootContainmentId);
    if (journal.transactionId.isEmpty()
            || journal.originLayoutName.isEmpty()
            || journal.destinationLayoutName.isEmpty()
            || journal.originLayoutName
                == journal.destinationLayoutName
            || journal.originFile.isEmpty()
            || journal.destinationFile.isEmpty()
            || journal.hiddenFile.isEmpty()
            || journal.snapshotSha256.size()
                != QCryptographicHash::hashLength(
                    QCryptographicHash::Sha256)
            || journal.containmentIds.isEmpty()
            || !journal.containmentIds.contains(
                rootIdString)
            || journal.containmentIds
                .removeDuplicates() > 0) {
        qCritical() << "view move recovery found incomplete or duplicate manifest state in"
                    << directoryPath;
        return std::nullopt;
    }

    journal.containmentIds.sort();
    if (!transactionFileIsSafe(
            journal.snapshotPath())) {
        qCritical() << "view move recovery refused an unsafe snapshot in"
                    << directoryPath;
        return std::nullopt;
    }
    return journal;
}

[[nodiscard]] QStringList snapshotContainmentIds(
    const QString &snapshotFile)
{
    const KConfig snapshot(
        snapshotFile,
        KConfig::SimpleConfig);
    QStringList ids =
        snapshot.group(
            QStringLiteral("Containments"))
            .groupList();
    ids.sort();
    return ids;
}

[[nodiscard]] bool groupMatchesSnapshot(
    const KConfigGroup &snapshot,
    const KConfigGroup &target,
    const std::optional<QString> &layoutId)
{
    auto expectedEntries =
        snapshot.entryMap();
    if (layoutId) {
        expectedEntries[QStringLiteral(
            "layoutId")] = *layoutId;
    }
    if (expectedEntries
            != target.entryMap()) {
        return false;
    }

    QStringList expectedGroups =
        snapshot.groupList();
    QStringList targetGroups =
        target.groupList();
    expectedGroups.sort();
    targetGroups.sort();
    if (expectedGroups != targetGroups) {
        return false;
    }

    for (const QString &group :
            expectedGroups) {
        if (!groupMatchesSnapshot(
                snapshot.group(group),
                target.group(group),
                std::nullopt)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool configMatchesSnapshot(
    const QString &targetFile,
    const QString &snapshotFile,
    const QStringList &containmentIds,
    const QString &layoutId)
{
    const KConfig snapshot(
        snapshotFile,
        KConfig::SimpleConfig);
    const KConfig target(
        targetFile,
        KConfig::SimpleConfig);
    const KConfigGroup snapshotContainments(
        &snapshot,
        QStringLiteral("Containments"));
    const KConfigGroup targetContainments(
        &target,
        QStringLiteral("Containments"));
    for (const QString &id :
            containmentIds) {
        if (!snapshotContainments.hasGroup(id)
                || !targetContainments.hasGroup(id)
                || !groupMatchesSnapshot(
                    snapshotContainments.group(id),
                    targetContainments.group(id),
                    layoutId)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool configOmitsSnapshot(
    const QString &targetFile,
    const QStringList &containmentIds)
{
    const KConfig target(
        targetFile,
        KConfig::SimpleConfig);
    const KConfigGroup containments(
        &target,
        QStringLiteral("Containments"));
    return std::ranges::none_of(
        containmentIds,
        [&containments](const QString &id) {
            return containments.hasGroup(id);
        });
}

[[nodiscard]] bool configAllowsMutation(
    const KSharedConfigPtr &config)
{
    if (!config
            || config->name().isEmpty()
            || config->accessMode()
                != KConfigBase::ReadWrite
            || config->isImmutable()) {
        return false;
    }

    const KConfigGroup containments(
        config,
        QStringLiteral("Containments"));
    return !containments.isImmutable();
}

[[nodiscard]] bool groupAllowsReplacement(
    const KConfigGroup &group)
{
    if (group.isImmutable()) {
        return false;
    }
    for (const QString &key :
            group.keyList()) {
        if (group.isEntryImmutable(key)) {
            return false;
        }
    }
    return std::ranges::all_of(
        group.groupList(),
        [&group](const QString &child) {
            return groupAllowsReplacement(
                group.group(child));
        });
}

[[nodiscard]] bool configAllowsContainmentReplacement(
    const KSharedConfigPtr &config,
    const QStringList &containmentIds)
{
    if (!configAllowsMutation(config)) {
        return false;
    }

    const KConfigGroup containments(
        config,
        QStringLiteral("Containments"));
    return std::ranges::all_of(
        containmentIds,
        [&containments](const QString &id) {
            const KConfigGroup containment =
                containments.group(id);
            return !containment.exists()
                || groupAllowsReplacement(
                    containment);
        });
}

[[nodiscard]] bool configAllowsLayoutOwnerMutation(
    const KSharedConfigPtr &config,
    const QStringList &containmentIds)
{
    if (!configAllowsMutation(config)) {
        return false;
    }

    const KConfigGroup containments(
        config,
        QStringLiteral("Containments"));
    return std::ranges::all_of(
        containmentIds,
        [&containments](const QString &id) {
            const KConfigGroup containment =
                containments.group(id);
            return containment.exists()
                && !containment.isImmutable()
                && !containment
                    .isEntryImmutable(
                        QStringLiteral(
                            "layoutId"));
        });
}

[[nodiscard]] bool publishSnapshot(
    const KSharedConfigPtr &target,
    const QString &snapshotFile,
    const QStringList &containmentIds,
    const QString &layoutId,
    const bool replaceExisting)
{
    if (configMatchesSnapshot(
            target->name(),
            snapshotFile,
            containmentIds,
            layoutId)) {
        return true;
    }
    //! Re-read the standalone repository at the mutation boundary. A
    //! concurrent destination identity must be observed and refused rather
    //! than overwritten from the entry map captured during preflight.
    target->reparseConfiguration();
    if (!configAllowsContainmentReplacement(
            target,
            containmentIds)) {
        qCritical() << "view move transaction refused immutable or read-only snapshot destination"
                    << target->name();
        return false;
    }

    KConfigGroup destinationContainments(
        target,
        QStringLiteral("Containments"));
    const KConfig snapshot(
        snapshotFile,
        KConfig::SimpleConfig);
    const KConfigGroup sourceContainments(
        &snapshot,
        QStringLiteral("Containments"));
    for (const QString &id :
            containmentIds) {
        if (!sourceContainments.hasGroup(id)
                || (!replaceExisting
                    && destinationContainments
                        .hasGroup(id))) {
            qCritical() << "view move transaction refused missing snapshot or destination collision for containment"
                        << id << "in" << target->name();
            return false;
        }
    }

    for (const QString &id :
            containmentIds) {
        KConfigGroup destination =
            destinationContainments.group(id);
        destination.deleteGroup();
        sourceContainments.group(id)
            .copyTo(&destination);
        destination.writeEntry(
            QStringLiteral("layoutId"),
            layoutId);
    }

    if (!target->sync()) {
        qCritical() << "view move transaction could not publish snapshot to"
                    << target->name();
        target->reparseConfiguration();
        return false;
    }
    target->reparseConfiguration();
    const bool persisted =
        configMatchesSnapshot(
            target->name(),
            snapshotFile,
            containmentIds,
            layoutId);
    if (!persisted) {
        qCritical() << "view move transaction detected a semantically ignored snapshot write to"
                    << target->name();
        target->reparseConfiguration();
    }
    return persisted;
}

[[nodiscard]] bool publishLayoutOwner(
    const KSharedConfigPtr &activeConfig,
    const QStringList &containmentIds,
    const QString &layoutId)
{
    bool alreadyPublished{true};
    {
        const KConfig fresh(
            activeConfig->name(),
            KConfig::SimpleConfig);
        const KConfigGroup containments(
            &fresh,
            QStringLiteral("Containments"));
        for (const QString &id :
                containmentIds) {
            if (!containments.hasGroup(id)
                    || containments.group(id)
                        .readEntry(
                            QStringLiteral(
                                "layoutId"),
                            QString())
                        != layoutId) {
                alreadyPublished = false;
                break;
            }
        }
    }
    if (alreadyPublished) {
        return true;
    }
    if (!configAllowsLayoutOwnerMutation(
            activeConfig,
            containmentIds)) {
        qCritical() << "view move transaction refused immutable active layout ownership in"
                    << activeConfig->name();
        return false;
    }

    KConfigGroup containments(
        activeConfig,
        QStringLiteral("Containments"));
    for (const QString &id :
            containmentIds) {
        if (!containments.hasGroup(id)) {
            qCritical() << "view move transaction could not resolve active containment"
                        << id << "in" << activeConfig->name();
            return false;
        }
        containments.group(id)
            .writeEntry(
                QStringLiteral("layoutId"),
                layoutId);
    }
    if (!activeConfig->sync()) {
        qCritical() << "view move transaction could not publish active layout ownership to"
                    << activeConfig->name();
        activeConfig->reparseConfiguration();
        return false;
    }
    activeConfig->reparseConfiguration();

    const KConfig fresh(
        activeConfig->name(),
        KConfig::SimpleConfig);
    const KConfigGroup persistedContainments(
        &fresh,
        QStringLiteral("Containments"));
    for (const QString &id :
            containmentIds) {
        if (!persistedContainments.hasGroup(id)
                || persistedContainments.group(id)
                    .readEntry(
                        QStringLiteral(
                            "layoutId"),
                        QString())
                    != layoutId) {
            qCritical() << "view move transaction detected a semantically ignored active-owner write for containment"
                        << id << "in" << activeConfig->name();
            activeConfig->reparseConfiguration();
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool tombstoneSnapshot(
    const KSharedConfigPtr &target,
    const QStringList &containmentIds)
{
    if (configOmitsSnapshot(
            target->name(),
            containmentIds)) {
        return true;
    }
    //! Recovery and forward commit both mutate standalone layout files. Use
    //! their current entry maps so an external immutability change is a loud
    //! refusal and a post-decision retry remains journal-driven.
    target->reparseConfiguration();
    if (!configAllowsContainmentReplacement(
            target,
            containmentIds)) {
        qCritical() << "view move transaction refused immutable snapshot retirement in"
                    << target->name();
        return false;
    }

    KConfigGroup containments(
        target,
        QStringLiteral("Containments"));
    for (const QString &id :
            containmentIds) {
        containments.group(id)
            .deleteGroup();
    }
    if (!target->sync()) {
        qCritical() << "view move transaction could not retire snapshot from"
                    << target->name();
        target->reparseConfiguration();
        return false;
    }
    target->reparseConfiguration();
    const bool persisted =
        configOmitsSnapshot(
            target->name(),
            containmentIds);
    if (!persisted) {
        qCritical() << "view move transaction detected a semantically ignored snapshot retirement in"
                    << target->name();
        target->reparseConfiguration();
    }
    return persisted;
}

[[nodiscard]] Layout::ViewMoveTransaction::
PersistentOwner persistentOwnerForLayoutName(
    const QString &owner,
    const QString &originLayoutName,
    const QString &destinationLayoutName)
{
    if (owner == originLayoutName) {
        return Layout::ViewMoveTransaction::
            PersistentOwner::Origin;
    }
    if (owner == destinationLayoutName) {
        return Layout::ViewMoveTransaction::
            PersistentOwner::Destination;
    }
    return Layout::ViewMoveTransaction::
        PersistentOwner::Unknown;
}

[[nodiscard]] Layout::ViewMoveTransaction::
PersistentOwner observePersistentOwner(
    const QString &hiddenFile,
    const QStringList &containmentIds,
    const QString &originLayoutName,
    const QString &destinationLayoutName)
{
    const KConfig hidden(
        hiddenFile,
        KConfig::SimpleConfig);
    const KConfigGroup containments(
        &hidden,
        QStringLiteral("Containments"));
    if (containmentIds.isEmpty()) {
        return Layout::ViewMoveTransaction::
            PersistentOwner::Unknown;
    }

    std::optional<Layout::ViewMoveTransaction::
        PersistentOwner> commonOwner;
    for (const QString &containmentId :
            containmentIds) {
        if (!containments.hasGroup(
                containmentId)) {
            return Layout::
                ViewMoveTransaction::
                    PersistentOwner::Unknown;
        }

        const QString owner =
            containments
                .group(containmentId)
                .readEntry(
                    QStringLiteral(
                        "layoutId"),
                    QString());
        const auto observed =
            persistentOwnerForLayoutName(
                owner,
                originLayoutName,
                destinationLayoutName);
        if (observed
                == Layout::
                    ViewMoveTransaction::
                        PersistentOwner::Unknown
                || (commonOwner
                    && *commonOwner
                        != observed)) {
            return Layout::
                ViewMoveTransaction::
                    PersistentOwner::Unknown;
        }
        commonOwner = observed;
    }
    return *commonOwner;
}

[[nodiscard]] constexpr const char *persistentOwnerName(
    const Layout::ViewMoveTransaction::
        PersistentOwner owner) noexcept
{
    switch (owner) {
    case Layout::ViewMoveTransaction::
            PersistentOwner::Origin:
        return "origin";
    case Layout::ViewMoveTransaction::
            PersistentOwner::Destination:
        return "destination";
    case Layout::ViewMoveTransaction::
            PersistentOwner::Unknown:
        return "unknown";
    }

    return "unknown";
}

[[nodiscard]] constexpr const char *recoveryActionName(
    const Layout::ViewMoveTransaction::
        RecoveryAction action) noexcept
{
    switch (action) {
    case Layout::ViewMoveTransaction::
            RecoveryAction::RollBack:
        return "rollBack";
    case Layout::ViewMoveTransaction::
            RecoveryAction::RollForward:
        return "rollForward";
    case Layout::ViewMoveTransaction::
            RecoveryAction::Refuse:
        return "refuse";
    }

    return "refuse";
}

}

const int Storage::IDNULL = -1;
const int Storage::IDBASE = 0;

Storage::Storage()
{
    qDebug() << " >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> LAYOUTS::STORAGE, TEMP DIR ::: " << m_storageTmpDir.path();

    //! Known Errors / Warnings
    s_knownErrors << Data::Generic(Data::Error::APPLETSWITHSAMEID, i18n("Different Applets With Same Id"));
    s_knownErrors << Data::Generic(Data::Error::ORPHANEDPARENTAPPLETOFSUBCONTAINMENT, i18n("Orphaned Parent Applet Of Subcontainment"));
    s_knownErrors<< Data::Generic(Data::Warning::APPLETANDCONTAINMENTWITHSAMEID, i18n("Different Applet And Containment With Same Id"));
    s_knownErrors << Data::Generic(Data::Warning::ORPHANEDSUBCONTAINMENT, i18n("Orphaned Subcontainment"));


    //! Known SubContainment Families
    SubContaimentIdentityData data;
    //! Systray Family
    m_subIdentities << SubContaimentIdentityData{.cfgGroup="Configuration", .cfgProperty="SystrayContainmentId"};
    //! Group applet Family
    m_subIdentities << SubContaimentIdentityData{.cfgGroup="Configuration", .cfgProperty="ContainmentId"};
}

Storage::~Storage()
{
}

Storage *Storage::self()
{
    static Storage store;
    return &store;
}

bool Storage::isWritable(const Layout::GenericLayout *layout) const
{
    Q_ASSERT(layout);
    return persistenceEndpointIsWritable(
        layout->file());
}

bool Storage::isLatteContainment(const Plasma::Containment *containment) const
{
    if (!containment) {
        return false;
    }

    if (containment->pluginMetaData().pluginId() == QLatin1String("org.kde.latte.containment")) {
        return true;
    }

    return false;
}

bool Storage::isLatteContainment(const KConfigGroup &group) const
{
    QString pluginId = group.readEntry("plugin", "");
    return pluginId == QLatin1String("org.kde.latte.containment");
}

bool Storage::isSubContainment(const Plasma::Corona *corona, const Plasma::Applet *applet) const
{
    if (!corona || !applet) {
        return false;
    }

    for (const auto containment : corona->containments()) {
        Plasma::Applet *parentApplet = qobject_cast<Plasma::Applet *>(containment->parent());
        if (parentApplet && parentApplet == applet) {
            return true;
        }
    }

    return false;
}

bool Storage::isSubContainment(const KConfigGroup &appletGroup) const
{
    return isValid(subContainmentId(appletGroup));
}

bool Storage::isValid(const int &id)
{
    return id >= IDBASE;
}

int Storage::subContainmentId(const KConfigGroup &appletGroup) const
{
    //! cycle through subcontainments identities
    for (auto subidentity : m_subIdentities) {
        KConfigGroup appletConfigGroup = appletGroup;

        if (!subidentity.cfgGroup.isEmpty()) {
            //! if identity provides specific configuration group
            if (appletConfigGroup.hasGroup(subidentity.cfgGroup)) {
                appletConfigGroup = appletGroup.group(subidentity.cfgGroup);
            }
        }

        if (!subidentity.cfgProperty.isEmpty()) {
            //! if identity provides specific property for configuration group
            if (appletConfigGroup.hasKey(subidentity.cfgProperty)) {
                return appletConfigGroup.readEntry(subidentity.cfgProperty, IDNULL);
            }
        }
    }

    return IDNULL;
}

int Storage::subIdentityIndex(const KConfigGroup &appletGroup) const
{
    if (!isSubContainment(appletGroup)) {
        return IDNULL;
    }

    //! cycle through subcontainments identities
    for (int i=0; i<m_subIdentities.count(); ++i) {
        KConfigGroup appletConfigGroup = appletGroup;

        if (!m_subIdentities[i].cfgGroup.isEmpty()) {
            //! if identity provides specific configuration group
            if (appletConfigGroup.hasGroup(m_subIdentities[i].cfgGroup)) {
                appletConfigGroup = appletGroup.group(m_subIdentities[i].cfgGroup);
            }
        }

        if (!m_subIdentities[i].cfgProperty.isEmpty()) {
            //! if identity provides specific property for configuration group
            if (appletConfigGroup.hasKey(m_subIdentities[i].cfgProperty)) {
                int subId = appletConfigGroup.readEntry(m_subIdentities[i].cfgProperty, IDNULL);
                return isValid(subId) ? i : IDNULL;
            }
        }
    }

    return IDNULL;
}

Plasma::Containment *Storage::subContainmentOf(const Plasma::Corona *corona, const Plasma::Applet *applet)
{
    if (!corona || !applet) {
        return nullptr;
    }

    if (isSubContainment(corona, applet)) {
        for (const auto containment : corona->containments()) {
            Plasma::Applet *parentApplet = qobject_cast<Plasma::Applet *>(containment->parent());
            if (parentApplet && parentApplet == applet) {
                return containment;
            }
        }
    }

    return nullptr;
}

void Storage::lock(const Layout::GenericLayout *layout)
{
    QFileInfo layoutFileInfo(layout->file());

    if (layoutFileInfo.exists() && layoutFileInfo.isWritable()) {
        QFile(layout->file()).setPermissions(QFileDevice::ReadUser | QFileDevice::ReadGroup | QFileDevice::ReadOther);
    }
}

void Storage::unlock(const Layout::GenericLayout *layout)
{
    QFileInfo layoutFileInfo(layout->file());

    if (layoutFileInfo.exists() && !layoutFileInfo.isWritable()) {
        QFile(layout->file()).setPermissions(QFileDevice::ReadUser | QFileDevice::WriteUser | QFileDevice::ReadGroup | QFileDevice::ReadOther);
    }
}


void Storage::importToCorona(const Layout::GenericLayout *layout)
{
    if (!layout->corona()) {
        return;
    }

    //! Setting mutable for create a containment
    layout->corona()->setImmutability(Plasma::Types::Mutable);

    removeScreenGroupDerivedViews(layout->file());

    QString temp1FilePath = m_storageTmpDir.path() +  "/" + layout->name() + ".multiple.views";
    //! we need to copy first the layout file because the kde cache
    //! may not have yet been updated (KSharedConfigPtr)
    //! this way we make sure at the latest changes stored in the layout file
    //! will be also available when changing to Multiple Layouts
    QString tempLayoutFilePath = m_storageTmpDir.path() +  "/" + layout->name() + ".multiple.tmplayout";

    //! WE NEED A WAY TO COPY A CONTAINMENT!!!!
    QFile tempLayoutFile(tempLayoutFilePath);
    QFile copyFile(temp1FilePath);
    QFile layoutOriginalFile(layout->file());

    if (tempLayoutFile.exists()) {
        tempLayoutFile.remove();
    }

    if (copyFile.exists()) {
        copyFile.remove();
    }

    layoutOriginalFile.copy(tempLayoutFilePath);

    KSharedConfigPtr filePtr = KSharedConfig::openConfig(tempLayoutFilePath);
    KSharedConfigPtr newFile = KSharedConfig::openConfig(temp1FilePath);
    KConfigGroup copyGroup = KConfigGroup(newFile, "Containments");
    KConfigGroup current_containments = KConfigGroup(filePtr, "Containments");

    current_containments.copyTo(&copyGroup);

    newFile->reparseConfiguration();

    //! update ids to unique ones
    QString temp2File = newUniqueIdsFile(temp1FilePath, layout);

    if (temp2File.isEmpty()) {
        qCritical() << "layout import to corona aborted: id remap failed for" << layout->name();
        return;
    }

    //! Finally import the configuration
    importLayoutFile(layout, temp2File);
}


bool Storage::appletGroupIsValid(const KConfigGroup &appletGroup)
{
    return !( appletGroup.keyList().count() == 0
              && appletGroup.groupList().count() == 1
              && appletGroup.groupList().at(0) == QLatin1String("Configuration")
              && appletGroup.group("Configuration").keyList().count() == 1
              && appletGroup.group("Configuration").hasKey("PreloadWeight") );
}

QStringList Storage::containmentsIds(const QString &filepath)
{
    QStringList ids;

    KSharedConfigPtr filePtr = KSharedConfig::openConfig(filepath);
    KConfigGroup containments = KConfigGroup(filePtr, "Containments");

    for(const auto &cId : containments.groupList()) {
        ids << cId;
    }

    return ids;
}

QStringList Storage::appletsIds(const QString &filepath)
{
    QStringList ids;

    KSharedConfigPtr filePtr = KSharedConfig::openConfig(filepath);
    KConfigGroup containments = KConfigGroup(filePtr, "Containments");

    for(const auto &cId : containments.groupList()) {
        for(const auto &aId : containments.group(cId).group("Applets").groupList()) {
            ids << aId;
        }
    }

    return ids;
}

QString Storage::newUniqueIdsFile(QString originFile, const Layout::GenericLayout *destinationLayout)
{
    if (!destinationLayout) {
        return QString();
    }

    QString currentdestinationname = destinationLayout->name();
    QString currentdestinationfile = "";

    if (!destinationLayout->hasCorona()) {
        currentdestinationfile = destinationLayout->file();
    }

    QString tempFile = m_storageTmpDir.path() + "/" + currentdestinationname + ".views.newids";

    QFile copyFile(tempFile);

    if (copyFile.exists()) {
        copyFile.remove();
    }

    //! BEGIN updating the ids in the temp file
    QStringList allIds;

    if (destinationLayout->hasCorona()) {
        allIds << destinationLayout->corona()->containmentsIds();
        allIds << destinationLayout->corona()->appletsIds();
    } else {
        allIds << containmentsIds(currentdestinationfile);
        allIds << appletsIds(currentdestinationfile);
    }

    QStringList toInvestigateContainmentIds;
    QStringList toInvestigateAppletIds;
    QStringList toInvestigateSubContIds;

    //! first is the subcontainment id
    QHash<QString, QString> subParentContainmentIds;
    QHash<QString, QString> subAppletIds;

    //qDebug() << "Ids:" << allIds;

    //qDebug() << "to copy containments: " << toCopyContainmentIds;
    //qDebug() << "to copy applets: " << toCopyAppletIds;

    KSharedConfigPtr filePtr = KSharedConfig::openConfig(originFile);
    KConfigGroup investigate_conts = KConfigGroup(filePtr, "Containments");

    //! Record the containment and applet ids
    for (const auto &cId : investigate_conts.groupList()) {
        toInvestigateContainmentIds << cId;
        auto appletsEntries = investigate_conts.group(cId).group("Applets");
        toInvestigateAppletIds << appletsEntries.groupList();

        //! investigate for subcontainments
        for (const auto &appletId : appletsEntries.groupList()) {
            int subId = subContainmentId(appletsEntries.group(appletId));

            //! It is a subcontainment !!!
            if (isValid(subId)) {
                QString tSubIdStr = QString::number(subId);
                toInvestigateSubContIds << tSubIdStr;
                subParentContainmentIds[tSubIdStr] = cId;
                subAppletIds[tSubIdStr] = appletId;
                qDebug() << "subcontainment was found in the containment...";
            }
        }
    }

    //! Reassign containment and applet ids to unique ones (EX-07 in
    //! docs/tracking/QML_EXTRACTION_PLAN.md: the assignment math lives in the
    //! tested StorageIdRemapper core; this function keeps the KConfig
    //! application)
    const StorageIdRemapper::IdRemap idRemap =
        StorageIdRemapper::remap({allIds, toInvestigateContainmentIds, toInvestigateAppletIds});
    const QHash<QString, QString> &assigned = idRemap.assigned;

    if (idRemap.exhausted) {
        //! writing "" group names would corrupt the destination layout far
        //! from the cause; refuse the whole import instead
        qCritical() << "layout import: id remap EXHAUSTED the id space (cap"
                    << StorageIdRemapper::maxId << ") for" << originFile
                    << "- aborting the import";
        return QString();
    }

    qDebug() << "ALL CORONA IDS ::: " << allIds;
    qDebug() << "FULL ASSIGNMENTS ::: " << assigned;

    //! update applet ids in their containment order and in MultipleLayouts update also the layoutId
    for (const auto &cId : investigate_conts.groupList()) {
        //! Update options that contain applet ids
        //! (appletOrder) and (lockedZoomApplets) and (userBlocksColorizingApplets)
        QStringList options;
        options << "appletOrder" << "lockedZoomApplets" << "userBlocksColorizingApplets";

        for (const auto &settingStr : options) {
            QString order1 = investigate_conts.group(cId).group("General").readEntry(settingStr, QString());

            if (!order1.isEmpty()) {
                QStringList order1Ids = order1.split(";");
                QStringList fixedOrder1Ids;

                for (int i = 0; i < order1Ids.count(); ++i) {
                    fixedOrder1Ids.append(assigned.value(order1Ids[i]));
                }

                QString fixedOrder1 = fixedOrder1Ids.join(";");
                investigate_conts.group(cId).group("General").writeEntry(settingStr, fixedOrder1);
            }
        }

        if (destinationLayout->hasCorona() && destinationLayout->corona()->layoutsManager()->memoryUsage() == MemoryUsage::MultipleLayouts) {
            //! will be added in main corona multiple layouts file
            investigate_conts.group(cId).writeEntry("layoutId", destinationLayout->name());
        } else {
            //! will be added in inactive layout
            investigate_conts.group(cId).writeEntry("layoutId", QString());
        }

        //! keep clone references pointing at their remapped originals; the
        //! shipped code left isClonedFrom stale, so an imported clone bound
        //! to whatever unrelated containment owned the old id in the
        //! destination. A clone whose original is not part of this import
        //! cannot bind to anything valid - orphan it loudly.
        const int clonedFrom = investigate_conts.group(cId).readEntry("isClonedFrom", Data::View::ISCLONEDNULL);

        if (clonedFrom != Data::View::ISCLONEDNULL) {
            const QString mappedOriginal = assigned.value(QString::number(clonedFrom));

            if (!mappedOriginal.isEmpty()) {
                investigate_conts.group(cId).writeEntry("isClonedFrom", mappedOriginal.toInt());
            } else {
                qWarning() << "layout import: clone containment" << cId << "references original"
                           << clonedFrom << "which is not part of the import; orphaning the clone";
                investigate_conts.group(cId).writeEntry("isClonedFrom", Data::View::ISCLONEDNULL);
            }
        }
    }

    //! must update also the sub id in its applet
    for (const auto &subId : toInvestigateSubContIds) {
        KConfigGroup subParentContainment = investigate_conts.group(subParentContainmentIds[subId]);
        KConfigGroup subAppletConfig = subParentContainment.group("Applets").group(subAppletIds[subId]);

        int entityIndex = subIdentityIndex(subAppletConfig);

        if (entityIndex >= 0) {
            if (!m_subIdentities[entityIndex].cfgGroup.isEmpty()) {
                subAppletConfig = subAppletConfig.group(m_subIdentities[entityIndex].cfgGroup);
            }

            if (!m_subIdentities[entityIndex].cfgProperty.isEmpty()) {
                subAppletConfig.writeEntry(m_subIdentities[entityIndex].cfgProperty, assigned.value(subId));
                subParentContainment.sync();
            }
        }
    }

    investigate_conts.sync();

    //! Copy To Temp 2 File And Update Correctly The Ids
    KSharedConfigPtr file2Ptr = KSharedConfig::openConfig(tempFile);
    KConfigGroup fixedNewContainmets = KConfigGroup(file2Ptr, "Containments");

    for (const auto &contId : investigate_conts.groupList()) {
        QString pluginId = investigate_conts.group(contId).readEntry("plugin", "");

        if (pluginId != "org.kde.desktopcontainment") { //!don't add ghost containments
            KConfigGroup newContainmentGroup = fixedNewContainmets.group(assigned.value(contId));
            investigate_conts.group(contId).copyTo(&newContainmentGroup);

            newContainmentGroup.group("Applets").deleteGroup();

            for (const auto &appId : investigate_conts.group(contId).group("Applets").groupList()) {
                KConfigGroup appletGroup = investigate_conts.group(contId).group("Applets").group(appId);
                KConfigGroup newAppletGroup = fixedNewContainmets.group(assigned.value(contId)).group("Applets").group(assigned.value(appId));
                appletGroup.copyTo(&newAppletGroup);
            }
        }
    }

    file2Ptr->reparseConfiguration();

    return tempFile;
}

bool Storage::syncToLayoutFile(
    const Layout::GenericLayout *layout,
    const bool removeLayoutId)
{
    if (!layout->corona() || !isWritable(layout)) {
        qCritical() << "Storage::syncToLayoutFile refused an unavailable or read-only layout"
                    << layout->name();
        return false;
    }

    const auto *const layoutContainments =
        layout->containments();
    QList<Plasma::Containment *> retainedContainments;
    retainedContainments.reserve(
        layoutContainments->size());
    QHash<const Plasma::Containment *,
          const Plasma::Containment *>
        ownerByContainment;
    QSet<const Plasma::Containment *>
        screenGroupDerivedSubtree;

    //! Validate the complete projection before deleting the old group. A
    //! malformed ownership chain must leave the previous file intact rather
    //! than turning one bad pointer into a truncated layout.
    for (auto *const containment : *layoutContainments) {
        if (!containment) {
            qCritical() << "Storage::syncToLayoutFile refused a null containment in layout"
                        << layout->name();
            return false;
        }

        const auto *const parentApplet = qobject_cast<const Plasma::Applet *>(containment->parent());
        const auto *const owner = parentApplet ? parentApplet->containment() : nullptr;
        if (parentApplet && !owner) {
            qCritical() << "Storage::syncToLayoutFile refused subcontainment"
                        << containment->id() << "with no owning containment in layout"
                        << layout->name();
            return false;
        }

        ownerByContainment.insert(
            containment,
            owner);
        if (isScreenGroupDerivedView(containment)) {
            screenGroupDerivedSubtree.insert(
                containment);
        }
    }

    //! A derived screen-group View and every owned subcontainment are runtime
    //! projections. Excluding only the root would persist its children as
    //! orphan groups after the old file projection is replaced.
    bool subtreeExpanded{true};
    while (subtreeExpanded) {
        subtreeExpanded = false;
        for (auto *const containment :
                *layoutContainments) {
            const auto *const owner =
                ownerByContainment.value(
                    containment);
            if (owner
                    && screenGroupDerivedSubtree
                        .contains(owner)
                    && !screenGroupDerivedSubtree
                        .contains(containment)) {
                screenGroupDerivedSubtree.insert(
                    containment);
                subtreeExpanded = true;
            }
        }
    }

    for (auto *const containment : *layoutContainments) {
        const auto *const owner =
            ownerByContainment.value(containment);
        const auto *const parentApplet =
            qobject_cast<const Plasma::Applet *>(
                containment->parent());
        //! Plasma keeps removed objects alive during its Undo window. The
        //! destroyed state is a persistence tombstone for the whole owned
        //! subtree; destroyedChanged(false) projects the live objects again.
        const bool scheduledForDestruction = containment->destroyed()
                || (parentApplet && (parentApplet->destroyed() || owner->destroyed()));
        if (!scheduledForDestruction
                && !screenGroupDerivedSubtree
                    .contains(containment)) {
            retainedContainments.append(containment);
        }
    }

    KSharedConfigPtr filePtr = KSharedConfig::openConfig(layout->file());

    KConfigGroup oldContainments = KConfigGroup(filePtr, "Containments");
    oldContainments.deleteGroup();

    qDebug() << " LAYOUT :: " << layout->name() << " is syncing its original file.";

    for (auto *const containment : retainedContainments) {
        if (removeLayoutId) {
            containment->config().writeEntry("layoutId", "");
        }

        KConfigGroup newGroup = oldContainments.group(QString::number(containment->id()));
        containment->config().copyTo(&newGroup);

        if (!removeLayoutId) {
            newGroup.writeEntry("layoutId", "");
        }

    }

    if (!persistConfigurationOrReportFailure(
            filePtr,
            "Storage::syncToLayoutFile")) {
        return false;
    }
    filePtr->reparseConfiguration();
    return true;
}

void Storage::moveToLayoutFile(const QString &layoutName)
{
    if (layoutName.isEmpty()) {
        return;
    }

    QString linkedFilePath = Importer::layoutUserFilePath(Layout::MULTIPLELAYOUTSHIDDENNAME);
    QString layoutFilePath = Importer::layoutUserFilePath(layoutName);

    if (linkedFilePath.isEmpty() || layoutFilePath.isEmpty() || !QFileInfo(linkedFilePath).exists() || !QFileInfo(layoutFilePath).exists()) {
        return;
    }

    KSharedConfigPtr layoutFilePtr = KSharedConfig::openConfig(layoutFilePath);
    KConfigGroup singleContainments = KConfigGroup(layoutFilePtr, "Containments");
    singleContainments.deleteGroup();

    KSharedConfigPtr multiFilePtr = KSharedConfig::openConfig(linkedFilePath);
    KConfigGroup multiContainments = KConfigGroup(multiFilePtr, "Containments");

    for(const auto &cId : multiContainments.groupList()) {
        QString cname = multiContainments.group(cId).readEntry("layoutId", QString());

        if (!cname.isEmpty() && cname == layoutName) {
            multiContainments.group(cId).writeEntry("layoutId", "");
            KConfigGroup singleGroup = singleContainments.group(cId);
            multiContainments.group(cId).copyTo(&singleGroup);
            singleGroup.writeEntry("layoutId", "");
            singleGroup.sync();

            multiContainments.group(cId).deleteGroup();
        }
    }

    layoutFilePtr->reparseConfiguration();
    removeScreenGroupDerivedViews(layoutFilePath);
}

QList<Plasma::Containment *> Storage::importLayoutFile(const Layout::GenericLayout *layout, QString file)
{
    KSharedConfigPtr filePtr = KSharedConfig::openConfig(file);
    auto newContainments = layout->corona()->importLayout(KConfigGroup(filePtr, ""));

    QList<Plasma::Containment *> importedViews;

    //! importLayout() triggers containmentAdded handlers synchronously (view
    //! creation and friends) and a containment can be DESTROYED before this
    //! loop runs; touching such a pointer crashed inside pluginMetaData()
    //! (dangling containment, coredumps 02:06 and 02:57 on 2026-07-11, race
    //! not yet root-caused). The corona's containment list only holds live
    //! objects and contains() compares pointer values without dereferencing,
    //! so it is a safe liveness filter until the deleter is identified.
    const auto livecontainments = layout->corona()->containments();

    for (const auto containment : newContainments) {
        if (!livecontainments.contains(containment)) {
            qWarning() << "importLayoutFile: a containment imported from" << file
                       << "was destroyed during the import; skipping it. The deleter"
                       << "is still unidentified, look for destroyContainment logs right above.";
            continue;
        }

        if (isLatteContainment(containment)) {
            importedViews << containment;
        }
    }

    return importedViews;
}

void Storage::importContainments(const QString &originFile, const QString &destinationFile)
{
    if (originFile.isEmpty() || destinationFile.isEmpty()) {
        return;
    }

    KSharedConfigPtr originPtr = KSharedConfig::openConfig(originFile);
    KSharedConfigPtr destinationPtr = KSharedConfig::openConfig(destinationFile);

    KConfigGroup originContainments = KConfigGroup(originPtr, "Containments");
    KConfigGroup destinationContainments = KConfigGroup(destinationPtr, "Containments");

    for (const auto originContId : originContainments.groupList()) {
        KConfigGroup destinationContainment(&destinationContainments, originContId);
        originContainments.group(originContId).copyTo(&destinationContainment);
    }

    destinationContainments.sync();
}

Data::View Storage::newView(const Layout::GenericLayout *destinationLayout, const Data::View &nextViewData)
{
    if (!destinationLayout || nextViewData.originFile().isEmpty()) {
        return Data::View();
    }

    qDebug() << "new view for layout";

    if (destinationLayout->hasCorona()) {
        //! Setting mutable for create a containment
        destinationLayout->corona()->setImmutability(Plasma::Types::Mutable);
    }

    QString templateFile = nextViewData.originFile();
    //! copy view template path in temp file
    QString templateTmpAbsolutePath = m_storageTmpDir.path() + "/" + QFileInfo(templateFile).fileName() + ".newids";

    if (QFile(templateTmpAbsolutePath).exists()) {
        QFile(templateTmpAbsolutePath).remove();
    }

    QFile(templateFile).copy(templateTmpAbsolutePath);

    //! update ids to unique ones
    QString temp2File = newUniqueIdsFile(templateTmpAbsolutePath, destinationLayout);

    if (temp2File.isEmpty()) {
        qCritical() << "new view from template aborted: id remap failed for" << destinationLayout->name();
        return Data::View();
    }

    //! update view containment data in case next data are provided
    if (nextViewData.state() != Data::View::IsInvalid) {

        KSharedConfigPtr lFile = KSharedConfig::openConfig(temp2File);
        KConfigGroup containments = KConfigGroup(lFile, "Containments");

        for (const auto cId : containments.groupList()) {
            if (Layouts::Storage::self()->isLatteContainment(containments.group(cId))) {
                //! first view we will find, we update its value
                updateView(containments.group(cId), nextViewData);
                break;
            }
        }

        lFile->reparseConfiguration();
    }

    if (nextViewData.isCloned()) {
        clearLinkedMemberLocalAppletConfiguration(temp2File);
    }

    Data::ViewsTable updatedNextViews = views(temp2File);

    if (updatedNextViews.rowCount() <= 0) {
        return Data::View();
    }

    if (destinationLayout->hasCorona()) {
        //! import views for active layout
        QList<Plasma::Containment *> importedViews = importLayoutFile(destinationLayout, temp2File);

        Plasma::Containment *newContainment = (importedViews.size() == 1 ? importedViews[0] : nullptr);

        if (!newContainment || !newContainment->pluginMetaData().isValid()) {
            qWarning() << "the requested containment plugin can not be located or loaded from:" << templateFile;
            return Data::View();
        }
    } else {
        //! import views for inactive layout
        importContainments(temp2File, destinationLayout->file());
    }

    return updatedNextViews[0];
}

void Storage::clearLinkedMemberLocalAppletConfiguration(const QString &layoutFile)
{
    const KSharedConfigPtr config = KSharedConfig::openConfig(layoutFile);
    const KConfigGroup containments(config, QStringLiteral("Containments"));

    for (const QString &containmentId : containments.groupList()) {
        const KConfigGroup containment = containments.group(containmentId);
        if (!isLatteContainment(containment)) {
            continue;
        }

        const KConfigGroup applets = containment.group(QStringLiteral("Applets"));
        for (const QString &appletId : applets.groupList()) {
            KConfigGroup appletConfiguration = applets.group(appletId)
                .group(QStringLiteral("Configuration"))
                .group(QStringLiteral("General"));
            appletConfiguration.deleteEntry(
                Data::LinkedConfigurationPolicy::appletLengthKey());
        }
    }

    config->sync();
}

void Storage::clearExportedLayoutSettings(KConfigGroup &layoutSettingsGroup)
{
    layoutSettingsGroup.writeEntry("preferredForShortcutsTouched", false);
    layoutSettingsGroup.writeEntry("lastUsedActivity", QString());
    layoutSettingsGroup.writeEntry("activities", QStringList());
    layoutSettingsGroup.sync();
}

bool Storage::exportTemplate(const QString &originFile, const QString &destinationFile,const Data::AppletsTable &approvedApplets)
{
    if (originFile.isEmpty() || !QFile(originFile).exists() || destinationFile.isEmpty()) {
        return false;
    }

    if (QFile(destinationFile).exists()) {
        QFile::remove(destinationFile);
    }

    QFile(originFile).copy(destinationFile);

    KSharedConfigPtr destFilePtr = KSharedConfig::openConfig(destinationFile);
    destFilePtr->reparseConfiguration();

    KConfigGroup containments = KConfigGroup(destFilePtr, "Containments");

    QStringList rejectedSubContainments;

    //! clear applets that are not approved
    for (const auto &cId : containments.groupList()) {
        //! clear properties
        containments.group(cId).writeEntry("layoutId", QString());
        if (isLatteContainment(containments.group(cId))) {
            containments.group(cId).writeEntry("isPreferredForShortcuts", false);
        }

        //! clear applets
        auto applets = containments.group(cId).group("Applets");
        for (const auto &aId: applets.groupList()) {
            QString pluginId = applets.group(aId).readEntry("plugin", "");

            if (!approvedApplets.containsId(pluginId)) {
                if (!isSubContainment(applets.group(aId))) {
                    //!remove all configuration for that applet
                    for (const auto &configId: applets.group(aId).groupList()) {
                        applets.group(aId).group(configId).deleteGroup();
                    }
                } else {
                    //! register which subcontaiments should return to default properties
                    rejectedSubContainments << QString::number(subContainmentId(applets.group(aId)));
                }
            }
        }
    }

    //! clear rejected SubContainments
    for (const auto &cId : containments.groupList()) {
        if (rejectedSubContainments.contains(cId)) {
            containments.group(cId).group("General").deleteGroup();
        }
    };

    KConfigGroup layoutSettingsGrp(destFilePtr, "LayoutSettings");
    clearExportedLayoutSettings(layoutSettingsGrp);
    destFilePtr->reparseConfiguration();
    removeScreenGroupDerivedViews(destinationFile);

    return true;
}

bool Storage::exportTemplate(const Layout::GenericLayout *layout, Plasma::Containment *containment, const QString &destinationFile, const Data::AppletsTable &approvedApplets)
{
    if (!layout || !containment || destinationFile.isEmpty()) {
        return false;
    }

    if (QFile(destinationFile).exists()) {
        QFile::remove(destinationFile);
    }

    KSharedConfigPtr destFilePtr = KSharedConfig::openConfig(destinationFile);
    destFilePtr->reparseConfiguration();

    KConfigGroup copied_conts = KConfigGroup(destFilePtr, "Containments");
    KConfigGroup copied_c1 = KConfigGroup(&copied_conts, QString::number(containment->id()));

    containment->config().copyTo(&copied_c1);

    //!investigate if there are subcontainments in the containment to copy also

    //! subId, subAppletId
    QHash<uint, QString> subInfo;
    auto applets = containment->config().group("Applets");

    for (const auto &applet : applets.groupList()) {
        int tSubId = subContainmentId(applets.group(applet));

        //! It is a subcontainment !!!
        if (isValid(tSubId)) {
            subInfo[tSubId] = applet;
            qDebug() << "subcontainment with id "<< tSubId << " was found in the containment... ::: " << containment->id();
        }
    }

    if (subInfo.count() > 0) {
        for(const auto subId : subInfo.keys()) {
            Plasma::Containment *subcontainment{nullptr};

            for (const auto containment : layout->corona()->containments()) {
                if (containment->id() == subId) {
                    subcontainment = containment;
                    break;
                }
            }

            if (subcontainment) {
                KConfigGroup copied_sub = KConfigGroup(&copied_conts, QString::number(subcontainment->id()));
                subcontainment->config().copyTo(&copied_sub);
            }
        }
    }
    //! end of subcontainments specific code

    QStringList rejectedSubContainments;

    //! clear applets that are not approved
    for (const auto &cId : copied_conts.groupList()) {
        //! clear properties
        copied_conts.group(cId).writeEntry("layoutId", QString());
        if (isLatteContainment(copied_conts.group(cId))) {
            copied_conts.group(cId).writeEntry("isPreferredForShortcuts", false);
        }

        //! clear applets
        auto applets = copied_conts.group(cId).group("Applets");
        for (const auto &aId: applets.groupList()) {
            QString pluginId = applets.group(aId).readEntry("plugin", "");

            if (!approvedApplets.containsId(pluginId)) {
                if (!isSubContainment(applets.group(aId))) {
                    //!remove all configuration for that applet
                    for (const auto &configId: applets.group(aId).groupList()) {
                        applets.group(aId).group(configId).deleteGroup();
                    }
                } else {
                    //! register which subcontaiments should return to default properties
                    rejectedSubContainments << QString::number(subContainmentId(applets.group(aId)));
                }
            }
        }
    }

    //! clear rejected SubContainments
    for (const auto &cId : copied_conts.groupList()) {
        if (rejectedSubContainments.contains(cId)) {
            copied_conts.group(cId).group("General").deleteGroup();
        }
    };

    KConfigGroup layoutSettingsGrp(destFilePtr, "LayoutSettings");
    clearExportedLayoutSettings(layoutSettingsGrp);
    destFilePtr->reparseConfiguration();
    removeScreenGroupDerivedViews(destinationFile);

    return true;
}

bool Storage::hasDifferentAppletsWithSameId(const Layout::GenericLayout *layout, Data::Error &error)
{
    if (!layout  || layout->file().isEmpty() || !QFile(layout->file()).exists()) {
        return false;
    }

    error.id = s_knownErrors[Data::Error::APPLETSWITHSAMEID].id;
    error.name = s_knownErrors[Data::Error::APPLETSWITHSAMEID].name;

    if (layout->isActive()) { // active layout
        QStringList registeredapplets;
        QStringList conflictedapplets;

        //! split ids to normal registered and conflicted
        for (const auto containment : *layout->containments()) {
            QString cid = QString::number(containment->id());

            for (const auto applet : containment->applets()) {
                QString aid = QString::number(applet->id());

                if (!registeredapplets.contains(aid)) {
                    registeredapplets << aid;
                } else if (!conflictedapplets.contains(aid)) {
                    conflictedapplets << aid;
                }
            }
        }

        //! create error data
        for (const auto containment : *layout->containments()) {
            QString cid = QString::number(containment->id());

            for (const auto applet : containment->applets()) {
                QString aid = QString::number(applet->id());

                if (!conflictedapplets.contains(aid)) {
                   continue;
                }

                Data::ErrorInformation errorinfo;
                errorinfo.id = QString::number(error.information.rowCount());
                errorinfo.containment = metadata(containment->pluginMetaData().pluginId());
                errorinfo.containment.storageId = cid;
                errorinfo.applet = metadata(applet->pluginMetaData().pluginId());
                errorinfo.applet.storageId = aid;

                error.information << errorinfo;
            }
        }
    } else { // inactive layout
        KSharedConfigPtr lfile = KSharedConfig::openConfig(layout->file());
        KConfigGroup containmentsEntries = KConfigGroup(lfile, "Containments");

        QStringList registeredapplets;
        QStringList conflictedapplets;

        //! split ids to normal registered and conflicted
        for (const auto &cid : containmentsEntries.groupList()) {
            for (const auto &aid : containmentsEntries.group(cid).group("Applets").groupList()) {
                if (!registeredapplets.contains(aid)) {
                    registeredapplets << aid;
                } else if (!conflictedapplets.contains(aid)) {
                    conflictedapplets << aid;
                }
            }
        }

        //! create error data
        for (const auto &cid : containmentsEntries.groupList()) {
            for (const auto &aid : containmentsEntries.group(cid).group("Applets").groupList()) {
                if (!conflictedapplets.contains(aid)) {
                   continue;
                }

                Data::ErrorInformation errorinfo;
                errorinfo.id = QString::number(error.information.rowCount());
                errorinfo.containment = metadata(containmentsEntries.group(cid).readEntry("plugin", ""));
                errorinfo.containment.storageId = cid;
                errorinfo.applet = metadata(containmentsEntries.group(cid).group("Applets").group(aid).readEntry("plugin", ""));
                errorinfo.applet.storageId = aid;

                error.information << errorinfo;
            }
        }
    }

    return !error.information.isEmpty();
}

bool Storage::hasAppletsAndContainmentsWithSameId(const Layout::GenericLayout *layout, Data::Warning &warning)
{
    if (!layout  || layout->file().isEmpty() || !QFile(layout->file()).exists()) {
        return false;
    }

    warning.id = s_knownErrors[Data::Error::APPLETANDCONTAINMENTWITHSAMEID].id;
    warning.name = s_knownErrors[Data::Error::APPLETANDCONTAINMENTWITHSAMEID].name;

    if (layout->isActive()) { // active layout
        QStringList registeredcontainments;
        QStringList conflicted;

        //! discover normal containment ids
        for (const auto containment : *layout->containments()) {
            QString cid = QString::number(containment->id());

            if (registeredcontainments.contains(cid)) {
                continue;
            }

            registeredcontainments << cid;
        }

        //! discover conflicted ids between containments and applets
        for (const auto containment : *layout->containments()) {
            QString cid = QString::number(containment->id());

            for (const auto applet : containment->applets()) {
                QString aid = QString::number(applet->id());

                if (!registeredcontainments.contains(aid)) {
                    continue;
                } else if (!conflicted.contains(aid)) {
                    conflicted << aid;
                }
            }
        }

        //! create warning data
        for (const auto containment : *layout->containments()) {
            QString cid = QString::number(containment->id());

            if (conflicted.contains(cid)) {
                Data::WarningInformation warninginfo;
                warninginfo.id = QString::number(warning.information.rowCount());
                warninginfo.containment = metadata(containment->pluginMetaData().pluginId());
                warninginfo.containment.storageId = cid;

                warning.information << warninginfo;
            }

            for (const auto applet : containment->applets()) {
                QString aid = QString::number(applet->id());

                if (!conflicted.contains(aid)) {
                   continue;
                }

                Data::WarningInformation warninginfo;
                warninginfo.id = QString::number(warning.information.rowCount());
                warninginfo.containment = metadata(containment->pluginMetaData().pluginId());
                warninginfo.containment.storageId = cid;
                warninginfo.applet = metadata(applet->pluginMetaData().pluginId());
                warninginfo.applet.storageId = aid;

                warning.information << warninginfo;
            }
        }
    } else { // inactive layout
        KSharedConfigPtr lfile = KSharedConfig::openConfig(layout->file());
        KConfigGroup containmentsEntries = KConfigGroup(lfile, "Containments");

        QStringList registeredcontainments;
        QStringList conflicted;

        //! discover normal containment ids
        for (const auto &cid : containmentsEntries.groupList()) {
            if (registeredcontainments.contains(cid)) {
                continue;
            }

            registeredcontainments << cid;
        }

        //! discover conflicted ids between containments and applets
        for (const auto &cid : containmentsEntries.groupList()) {
            for (const auto &aid : containmentsEntries.group(cid).group("Applets").groupList()) {
                if (!registeredcontainments.contains(aid)) {
                    continue;
                } else if (!conflicted.contains(aid)) {
                    conflicted << aid;
                }
            }
        }

        //! create warning data
        for (const auto &cid : containmentsEntries.groupList()) {
            if (conflicted.contains(cid)) {
                Data::WarningInformation warninginfo;
                warninginfo.id = QString::number(warning.information.rowCount());
                warninginfo.containment = metadata(containmentsEntries.group(cid).readEntry("plugin", ""));
                warninginfo.containment.storageId = cid;

                warning.information << warninginfo;
            }

            for (const auto &aid : containmentsEntries.group(cid).group("Applets").groupList()) {
                if (!conflicted.contains(aid)) {
                   continue;
                }

                Data::WarningInformation warninginfo;
                warninginfo.id = QString::number(warning.information.rowCount());
                warninginfo.containment = metadata(containmentsEntries.group(cid).readEntry("plugin", ""));
                warninginfo.containment.storageId = cid;
                warninginfo.applet = metadata(containmentsEntries.group(cid).group("Applets").group(aid).readEntry("plugin", ""));
                warninginfo.applet.storageId = aid;

                warning.information << warninginfo;
            }
        }
    }

    return !warning.information.isEmpty();
}

bool Storage::hasOrphanedParentAppletOfSubContainment(const Layout::GenericLayout *layout, Data::Error &error)
{
    if (!layout  || layout->file().isEmpty() || !QFile(layout->file()).exists()) {
        return false;
    }

    error.id = s_knownErrors[Data::Error::ORPHANEDPARENTAPPLETOFSUBCONTAINMENT].id;
    error.name = s_knownErrors[Data::Error::ORPHANEDPARENTAPPLETOFSUBCONTAINMENT].name;

    Data::ViewsTable views = Layouts::Storage::self()->views(layout);

    if (layout->isActive()) { // active layout

        //! create error data
        for (const auto containment : *layout->containments()) {
            QString cid = QString::number(containment->id());

            for (const auto applet : containment->applets()) {
                QString aid = QString::number(applet->id());

                int subid = subContainmentId(applet->config());

                if (subid == IDNULL || hasContainment(layout, subid)) {
                    continue;
                }

                Data::ErrorInformation errorinfo;
                errorinfo.id = QString::number(error.information.rowCount());
                errorinfo.containment = metadata(containment->pluginMetaData().pluginId());
                errorinfo.containment.storageId = cid;
                errorinfo.applet = metadata(applet->pluginMetaData().pluginId());
                errorinfo.applet.storageId = aid;
                errorinfo.applet.subcontainmentId = QString::number(subid);

                error.information << errorinfo;
            }
        }
    } else {
        KSharedConfigPtr lfile = KSharedConfig::openConfig(layout->file());
        KConfigGroup containmentsEntries = KConfigGroup(lfile, "Containments");

        //! create error data
        for (const auto &cid : containmentsEntries.groupList()) {
            for (const auto &aid : containmentsEntries.group(cid).group("Applets").groupList()) {
                int subid = subContainmentId(containmentsEntries.group(cid).group("Applets").group(aid));

                if (subid == IDNULL || hasContainment(layout, subid)) {
                    continue;
                }

                Data::ErrorInformation errorinfo;
                errorinfo.id = QString::number(error.information.rowCount());
                errorinfo.containment = metadata(containmentsEntries.group(cid).readEntry("plugin", ""));
                errorinfo.containment.storageId = cid;
                errorinfo.applet = metadata(containmentsEntries.group(cid).group("Applets").group(aid).readEntry("plugin", ""));
                errorinfo.applet.storageId = aid;
                errorinfo.applet.subcontainmentId = QString::number(subid);

                error.information << errorinfo;
            }
        }
    }

    Data::Warning warning1;
    if (!error.information.isEmpty() && hasOrphanedSubContainments(layout, warning1)) {
        error.information << warning1.information;
    }

    return !error.information.isEmpty();
}

bool Storage::hasOrphanedSubContainments(const Layout::GenericLayout *layout, Data::Warning &warning)
{
    if (!layout  || layout->file().isEmpty() || !QFile(layout->file()).exists()) {
        return false;
    }

    warning.id = s_knownErrors[Data::Error::ORPHANEDSUBCONTAINMENT].id;
    warning.name = s_knownErrors[Data::Error::ORPHANEDSUBCONTAINMENT].name;

    Data::ViewsTable views = Layouts::Storage::self()->views(layout);

    if (layout->isActive()) { // active layout
        //! create warning data
        for (const auto containment : *layout->containments()) {
            QString cid = QString::number(containment->id());

            Plasma::Applet *parentApplet = qobject_cast<Plasma::Applet *>(containment->parent());
            Plasma::Containment *parentContainment = parentApplet ? qobject_cast<Plasma::Containment *>(parentApplet->parent()) : nullptr;

            if (isLatteContainment(containment) || (parentApplet && parentContainment && layout->contains(parentContainment))) {
                //! is latte containment or is subcontainment that belongs to latte containment
                continue;
            }

            Data::WarningInformation warninginfo;
            warninginfo.id = QString::number(warning.information.rowCount());
            warninginfo.containment = metadata(containment->pluginMetaData().pluginId());
            warninginfo.containment.storageId = cid;
            warning.information << warninginfo;
        }
    } else { // inactive layout
        KSharedConfigPtr lfile = KSharedConfig::openConfig(layout->file());
        KConfigGroup containmentsEntries = KConfigGroup(lfile, "Containments");

        //! create warning data
        for (const auto &cid : containmentsEntries.groupList()) {
            if (views.hasContainmentId(cid)) {
                continue;
            }

            Data::WarningInformation warninginfo;
            warninginfo.id = QString::number(warning.information.rowCount());
            warninginfo.containment = metadata(containmentsEntries.group(cid).readEntry("plugin", ""));
            warninginfo.containment.storageId = cid;
            warning.information << warninginfo;
        }
    }

    return !warning.information.isEmpty();
}

Data::ErrorsList Storage::errors(const Layout::GenericLayout *layout)
{
    Data::ErrorsList errs;

    if (!layout  || layout->file().isEmpty() || !QFile(layout->file()).exists()) {
        return errs;
    }

    Data::Error error1;

    if (hasDifferentAppletsWithSameId(layout, error1)) {
        errs << error1;
    }

    Data::Error error2;

    if (hasOrphanedParentAppletOfSubContainment(layout, error2)) {
        errs << error2;
    }

    return errs;
}

Data::WarningsList Storage::warnings(const Layout::GenericLayout *layout)
{
    Data::WarningsList warns;

    if (!layout  || layout->file().isEmpty() || !QFile(layout->file()).exists()) {
        return warns;
    }

    Data::Warning warning1;

    if (hasAppletsAndContainmentsWithSameId(layout, warning1)) {
        warns << warning1;
    }

    Data::Error error1;
    Data::Warning warning2;

    if (!hasOrphanedParentAppletOfSubContainment(layout, error1) /*this is needed because this error has higher priority*/
            && hasOrphanedSubContainments(layout, warning2)) {
        warns << warning2;
    }

    return warns;
}

//! AppletsData Information
Data::Applet Storage::metadata(const QString &pluginId)
{
    Data::Applet data;
    data.id = pluginId;

    KPackage::Package pkg = KPackage::PackageLoader::self()->loadPackage(QStringLiteral("Plasma/Applet"));
    pkg.setDefaultPackageRoot(QStringLiteral("plasma/plasmoids"));
    pkg.setPath(pluginId);

    if (pkg.isValid()) {
        data.name = pkg.metadata().name();
        data.description = pkg.metadata().description();

        QString iconName = pkg.metadata().iconName();
        if (!iconName.startsWith("/") && iconName.contains("/")) {
            data.icon = QFileInfo(pkg.metadata().fileName()).absolutePath() + "/" + iconName;
        } else {
            data.icon = iconName;
        }
    }

    if (data.name.isEmpty()) {
        //! this is also a way to identify if a package is installed or not in current system
        data.name = data.id;
    }

    return data;
}

Data::AppletsTable Storage::plugins(const Layout::GenericLayout *layout, const int containmentid)
{
    Data::AppletsTable knownapplets;
    Data::AppletsTable unknownapplets;

    if (!layout) {
        return knownapplets;
    }

    //! empty means all containments are valid
    QList<int> validcontainmentids;

    if (isValid(containmentid)) {
        validcontainmentids << containmentid;

        //! searching for specific containment and subcontainments and ignore all other containments
        for(auto containment : *layout->containments()) {
            if (((int)containment->id()) != containmentid) {
                //! ignore irrelevant containments
                continue;
            }

            for (auto applet : containment->applets()) {
                if (isSubContainment(layout->corona(), applet)) {
                    validcontainmentids << subContainmentId(applet->config());
                }
            }
        }
    }

    //! cycle through valid contaiments in order to retrieve their metadata
    for(auto containment : *layout->containments()) {
        if (validcontainmentids.count()>0 && !validcontainmentids.contains(containment->id())) {
            //! searching only for valid containments
            continue;
        }

        for (auto applet : containment->applets()) {
            QString pluginId = applet->pluginMetaData().pluginId();
            if (!knownapplets.containsId(pluginId) && !unknownapplets.containsId(pluginId)) {
                Data::Applet appletdata = metadata(pluginId);

                if (appletdata.isInstalled()) {
                    knownapplets.insertBasedOnName(appletdata);
                } else if (appletdata.isValid()) {
                    unknownapplets.insertBasedOnName(appletdata);
                }
            }
        }
    }

    knownapplets << unknownapplets;

    return knownapplets;
}

Data::AppletsTable Storage::plugins(const QString &layoutfile, const int containmentid)
{
    Data::AppletsTable knownapplets;
    Data::AppletsTable unknownapplets;

    if (layoutfile.isEmpty()) {
        return knownapplets;
    }

    KSharedConfigPtr lFile = KSharedConfig::openConfig(layoutfile);
    KConfigGroup containmentGroups = KConfigGroup(lFile, "Containments");

    //! empty means all containments are valid
    QList<int> validcontainmentids;

    if (isValid(containmentid)) {
        validcontainmentids << containmentid;

        //! searching for specific containment and subcontainments and ignore all other containments
        for (const auto &cId : containmentGroups.groupList()) {
            if (cId.toInt() != containmentid) {
                //! ignore irrelevant containments
                continue;
            }

            auto appletGroups = containmentGroups.group(cId).group("Applets");

            for (const auto &appletId : appletGroups.groupList()) {
                KConfigGroup appletCfg = appletGroups.group(appletId);
                if (isSubContainment(appletCfg)) {
                    validcontainmentids << subContainmentId(appletCfg);
                }
            }
        }
    }

    //! cycle through valid contaiments in order to retrieve their metadata
    for (const auto &cId : containmentGroups.groupList()) {
        if (validcontainmentids.count()>0 && !validcontainmentids.contains(cId.toInt())) {
            //! searching only for valid containments
            continue;
        }

        auto appletGroups = containmentGroups.group(cId).group("Applets");

        for (const auto &appletId : appletGroups.groupList()) {
            KConfigGroup appletCfg = appletGroups.group(appletId);
            QString pluginId = appletCfg.readEntry("plugin", "");

            if (!knownapplets.containsId(pluginId) && !unknownapplets.containsId(pluginId)) {
                Data::Applet appletdata = metadata(pluginId);

                if (appletdata.isInstalled()) {
                    knownapplets.insertBasedOnName(appletdata);
                } else if (appletdata.isValid()) {
                    unknownapplets.insertBasedOnName(appletdata);
                }
            }
        }
    }

    knownapplets << unknownapplets;

    return knownapplets;
}

//! Views Data

void Storage::syncContainmentConfig(Plasma::Containment *containment)
{
    if (!containment) {
        return;
    }

    for(auto applet: containment->applets()) {
        KConfigGroup appletGeneralConfig = applet->config().group("General");

        if (appletGeneralConfig.exists()) {
            appletGeneralConfig.sync();
        }

        applet->config().sync();
    }

    containment->config().sync();
}

bool Storage::containsView(const QString &filepath, const int &viewId)
{
    KSharedConfigPtr lFile = KSharedConfig::openConfig(filepath);
    KConfigGroup containmentGroups = KConfigGroup(lFile, "Containments");
    KConfigGroup viewGroup = containmentGroups.group(QString::number(viewId));
    return viewGroup.exists() && isLatteContainment(viewGroup);
}

bool Storage::hasContainment(const Layout::GenericLayout *layout, const int &id)
{
    if (!layout  || layout->file().isEmpty() || !QFile(layout->file()).exists()) {
        return false;
    }

    if (layout->isActive()) { // active layout
        for(const auto containment : *layout->containments()) {
            if ((int)containment->id() == id) {
                return true;
            }
        }
    } else { // inactive layout
        KSharedConfigPtr lfile = KSharedConfig::openConfig(layout->file());
        KConfigGroup containmentsEntries = KConfigGroup(lfile, "Containments");

        //! create warning data
        for (const auto &cid : containmentsEntries.groupList()) {
            if (cid.toInt() == id) {
                return true;
            }
        }
    }

    return false;
}

bool Storage::isClonedView(const Plasma::Containment *containment) const
{
    if (!containment) {
        return false;
    }

    return isClonedView(containment->config());
}

bool Storage::isClonedView(const KConfigGroup &containmentGroup) const
{
    if (!isLatteContainment(containmentGroup)) {
        return false;
    }

    int isClonedFrom = containmentGroup.readEntry("isClonedFrom", Data::View::ISCLONEDNULL);
    return (isClonedFrom != IDNULL);
}

bool Storage::isScreenGroupDerivedView(const Plasma::Containment *containment) const
{
    return containment && isScreenGroupDerivedView(containment->config());
}

bool Storage::isScreenGroupDerivedView(const KConfigGroup &containmentGroup) const
{
    if (!isClonedView(containmentGroup)) {
        return false;
    }

    const int linkPlacement = containmentGroup.readEntry(
        "linkPlacement", static_cast<int>(Data::View::LinkPlacement::ScreenGroupDerived));
    return linkPlacement == static_cast<int>(Data::View::LinkPlacement::ScreenGroupDerived);
}

void Storage::removeScreenGroupDerivedViews(const QString &filepath)
{
    KSharedConfigPtr lFile = KSharedConfig::openConfig(filepath);
    KConfigGroup containmentGroups = KConfigGroup(lFile, "Containments");

    QList<Data::View> derivedViews;

    for (const auto &contId : containmentGroups.groupList()) {
        if (isScreenGroupDerivedView(containmentGroups.group(contId))) {
            derivedViews << view(containmentGroups.group(contId));
        }
    }

    if (derivedViews.isEmpty()) {
        return;
    }

    qDebug() << "org.kde.layout :: Removing derived screen-group views from file:" << filepath;

    for (const auto &derivedView : derivedViews) {
        qDebug() << "org.kde.layout :: Removing derived screen-group view:" << derivedView.id
                 << "and its subcontainments:" << derivedView.subcontainments;
        removeView(filepath, derivedView);
    }
}

Data::GenericTable<Data::Generic> Storage::subcontainments(const Layout::GenericLayout *layout, const Plasma::Containment *lattecontainment) const
{
    Data::GenericTable<Data::Generic> subs;

    if (!layout || !Layouts::Storage::self()->isLatteContainment(lattecontainment)) {
        return subs;
    }

    for (const auto containment : (*layout->containments())) {
        if (containment == lattecontainment) {
            continue;
        }

        Plasma::Applet *parentApplet = qobject_cast<Plasma::Applet *>(containment->parent());

        //! add subcontainments for that lattecontainment
        if (parentApplet && parentApplet->containment() && parentApplet->containment() == lattecontainment) {
            Data::Generic subdata;
            subdata.id = QString::number(containment->id());
            subs << subdata;
        }
    }

    return subs;
}

Data::GenericTable<Data::Generic> Storage::subcontainments(const KConfigGroup &containmentGroup)
{
    Data::GenericTable<Data::Generic> subs;

    if (!Layouts::Storage::self()->isLatteContainment(containmentGroup)) {
        return subs;
    }

    auto applets = containmentGroup.group("Applets");

    for (const auto &applet : applets.groupList()) {
        if (isSubContainment(applets.group(applet))) {
            Data::Generic subdata;
            subdata.id = QString::number(subContainmentId(applets.group(applet)));
            subs << subdata;
        }
    }

    return subs;
}

Data::View Storage::view(const Layout::GenericLayout *layout, const Plasma::Containment *lattecontainment)
{
    Data::View vdata;

    if (!layout || !Layouts::Storage::self()->isLatteContainment(lattecontainment)) {
        return vdata;
    }

    vdata = view(lattecontainment->config());

    vdata.screen = lattecontainment->screen();
    if (!isValid(vdata.screen)) {
        vdata.screen = lattecontainment->lastScreen();
    }

    vdata.subcontainments = subcontainments(layout, lattecontainment);

    return vdata;
}

Data::View Storage::view(const KConfigGroup &containmentGroup)
{
    Data::View vdata;

    if (!Layouts::Storage::self()->isLatteContainment(containmentGroup)) {
        return vdata;
    }

    vdata.id = containmentGroup.name();
    vdata.name = containmentGroup.readEntry("name", QString());
    vdata.isActive = false;
    vdata.screensGroup = static_cast<Latte::Types::ScreensGroup>(containmentGroup.readEntry("screensGroup", (int)Latte::Types::SingleScreenGroup));
    vdata.onPrimary = containmentGroup.readEntry("onPrimary", true);
    vdata.screen = containmentGroup.readEntry("lastScreen", IDNULL);
    vdata.isClonedFrom = containmentGroup.readEntry("isClonedFrom", Data::View::ISCLONEDNULL);

    const int storedLinkPlacement = containmentGroup.readEntry(
        "linkPlacement", static_cast<int>(Data::View::LinkPlacement::ScreenGroupDerived));
    if (storedLinkPlacement < static_cast<int>(Data::View::LinkPlacement::ScreenGroupDerived)
            || storedLinkPlacement > static_cast<int>(Data::View::LinkPlacement::ExplicitTarget)) {
        qWarning() << "Storage: refused view" << containmentGroup.name()
                   << "with invalid linked placement" << storedLinkPlacement;
        return Data::View{};
    }
    vdata.linkPlacement = static_cast<Data::View::LinkPlacement>(storedLinkPlacement);

    if (vdata.linkPlacement == Data::View::LinkPlacement::ExplicitTarget && !vdata.isCloned()) {
        qWarning() << "Storage: refused independent view" << containmentGroup.name()
                   << "with explicit linked placement";
        return Data::View{};
    }

    vdata.screenEdgeMargin = containmentGroup.group("General").readEntry("screenEdgeMargin", (int)-1);

    int location = containmentGroup.readEntry("location", (int)Plasma::Types::BottomEdge);
    vdata.edge = (Plasma::Types::Location)location;

    vdata.maxLength = containmentGroup.group("General").readEntry("maxLength", (float)100.0);

    int alignment = containmentGroup.group("General").readEntry("alignment", (int)Latte::Types::Center) ;
    vdata.alignment = (Latte::Types::Alignment)alignment;

    vdata.subcontainments = subcontainments(containmentGroup);
    vdata.setState(Data::View::IsCreated);

    return vdata;
}

void Storage::updateView(KConfigGroup viewGroup, const Data::View &viewData)
{
    if (!Layouts::Storage::self()->isLatteContainment(viewGroup)) {
        return;
    }

    if (!viewData.hasValidLinkPlacement()
            || (viewData.linkPlacement == Data::View::LinkPlacement::ExplicitTarget
                && !viewData.isCloned())) {
        qWarning() << "Storage: refused invalid linked placement for view" << viewData.id;
        return;
    }

    viewGroup.writeEntry("name", viewData.name);
    viewGroup.writeEntry("screensGroup", (int)viewData.screensGroup);
    viewGroup.writeEntry("onPrimary", viewData.onPrimary);
    viewGroup.writeEntry("isClonedFrom", viewData.isClonedFrom);
    viewGroup.writeEntry("linkPlacement", static_cast<int>(viewData.linkPlacement));
    viewGroup.writeEntry("lastScreen", viewData.screen);
    viewGroup.group("General").writeEntry("screenEdgeMargin", viewData.screenEdgeMargin);
    viewGroup.writeEntry("location", (int)viewData.edge);
    //! maxLength lives under [General] on disk (the containment plugin's
    //! config key); view() reads it from there, so writing it at the
    //! containment level would land on a dead key and the edit would be lost
    viewGroup.group("General").writeEntry("maxLength", viewData.maxLength);
    viewGroup.group("General").writeEntry("alignment", (int)viewData.alignment);
    viewGroup.sync();
}

void Storage::updateView(const Layout::GenericLayout *layout, const Data::View &viewData)
{
    if (!layout) {
        return;
    }

    auto view = layout->viewForContainment(viewData.id.toUInt());

    if (view) {
        qDebug() << "Storage::updateView should not be called because view is active and present...";
        return;
    }

    if (layout->isActive()) {
        //! active view but is not present in active screens;
        auto containment = layout->containmentForId(viewData.id.toUInt());
        if (containment) {
            //! update containment
            containment->setLocation(viewData.edge);
            updateView(containment->config(), viewData);
        }
    } else {
        //! inactive view and in layout storage
        KSharedConfigPtr lFile = KSharedConfig::openConfig(layout->file());
        KConfigGroup containmentGroups = KConfigGroup(lFile, "Containments");
        KConfigGroup viewContainment = containmentGroups.group(viewData.id);

        if (viewContainment.exists() && Layouts::Storage::self()->isLatteContainment(viewContainment)) {
            updateView(viewContainment, viewData);
        }
    }
}

void Storage::removeView(const QString &filepath, const Data::View &viewData)
{
    if (!viewData.isValid()) {
        return;
    }

    removeContainment(filepath, viewData.id);

    for (int i=0; i<viewData.subcontainments.rowCount(); ++i) {
        removeContainment(filepath, viewData.subcontainments[i].id);
    }
}

bool Storage::tombstoneViewFromSnapshot(const KSharedConfigPtr &activeConfig,
                                        const QString &snapshotFile)
{
    if (!activeConfig || activeConfig->name().isEmpty() || snapshotFile.isEmpty()) {
        qCritical() << "Storage::tombstoneViewFromSnapshot refused an invalid active config or snapshot path";
        return false;
    }

    const KSharedConfigPtr sourceFile =
        KSharedConfig::openConfig(snapshotFile, KConfig::SimpleConfig);
    const KConfigGroup sourceContainments(sourceFile, QStringLiteral("Containments"));
    const QStringList containmentIds = sourceContainments.groupList();
    if (containmentIds.isEmpty()) {
        qCritical() << "Storage::tombstoneViewFromSnapshot refused an empty removal snapshot"
                    << snapshotFile;
        return false;
    }

    //! This must be Corona's live SimpleConfig repository. KConfig group
    //! deletion only marks keys known to the supplied entry map; reopening the
    //! same pathname under different flags creates a stale second authority.
    KConfigGroup destinationContainments(activeConfig, QStringLiteral("Containments"));
    for (const QString &containmentId : containmentIds) {
        destinationContainments.group(containmentId).deleteGroup();
    }

    if (!persistConfigurationOrReportFailure(
            activeConfig,
            "Storage::tombstoneViewFromSnapshot")) {
        return false;
    }

    const KConfig persistedFile(activeConfig->name(), KConfig::SimpleConfig);
    const KConfigGroup persistedContainments(&persistedFile,
                                             QStringLiteral("Containments"));
    for (const QString &containmentId : containmentIds) {
        if (destinationContainments.hasGroup(containmentId)
                || persistedContainments.hasGroup(containmentId)) {
            qCritical() << "Storage::tombstoneViewFromSnapshot found containment"
                        << containmentId << "after persisting removal to"
                        << activeConfig->name();
            return false;
        }
    }

    return true;
}

bool Storage::restoreView(const KSharedConfigPtr &activeConfig,
                          const QString &snapshotFile)
{
    if (!activeConfig || activeConfig->name().isEmpty() || snapshotFile.isEmpty()) {
        qCritical() << "Storage::restoreView refused an invalid active config or snapshot path";
        return false;
    }

    const KSharedConfigPtr sourceFile =
        KSharedConfig::openConfig(snapshotFile, KConfig::SimpleConfig);
    const KConfigGroup sourceContainments(sourceFile, QStringLiteral("Containments"));
    const QStringList containmentIds = sourceContainments.groupList();
    if (containmentIds.isEmpty()) {
        qCritical() << "Storage::restoreView refused an empty removal snapshot" << snapshotFile;
        return false;
    }

    KConfigGroup destinationContainments(activeConfig, QStringLiteral("Containments"));
    for (const QString &containmentId : containmentIds) {
        //! Plasma recreates a partial group before destroyedChanged(false).
        //! Replace only the identities captured by this transaction so stale
        //! partial values cannot override the complete pre-removal snapshot.
        destinationContainments.group(containmentId).deleteGroup();
        KConfigGroup destination = destinationContainments.group(containmentId);
        sourceContainments.group(containmentId).copyTo(&destination);
    }

    if (!persistConfigurationOrReportFailure(
            activeConfig,
            "Storage::restoreView")) {
        return false;
    }

    const KConfig persistedFile(activeConfig->name(), KConfig::SimpleConfig);
    const KConfigGroup persistedContainments(&persistedFile,
                                             QStringLiteral("Containments"));
    for (const QString &containmentId : containmentIds) {
        if (!destinationContainments.hasGroup(containmentId)
                || !persistedContainments.hasGroup(containmentId)) {
            qCritical() << "Storage::restoreView did not persist containment"
                        << containmentId << "to" << activeConfig->name();
            return false;
        }
    }

    return true;
}

ViewMovePersistenceResult Storage::persistViewMove(
    const Layout::GenericLayout *originLayout,
    const uint originViewId,
    const Layout::GenericLayout *destinationLayout,
    const KSharedConfigPtr &activeConfig)
{
    if (!originLayout
            || !destinationLayout
            || originLayout == destinationLayout
            || originViewId == 0
            || !activeConfig) {
        qCritical() << "Storage::persistViewMove refused invalid move participants";
        return {
            .status =
                ViewMovePersistenceResult::
                    Status::Rejected,
            .transactionPath = {},
            .error =
                QStringLiteral(
                    "invalid durable move participants")};
    }

    const QString snapshotFile =
        storedView(
            originLayout,
            static_cast<int>(
                originViewId));
    if (snapshotFile.isEmpty()) {
        qCritical() << "Storage::persistViewMove could not capture containment"
                    << originViewId
                    << "from" << originLayout->name();
        return {
            .status =
                ViewMovePersistenceResult::
                    Status::Rejected,
            .transactionPath = {},
            .error =
                QStringLiteral(
                    "could not capture the complete source subtree")};
    }

    return persistViewMoveSnapshot(
        originLayout->name(),
        originLayout->file(),
        destinationLayout->name(),
        destinationLayout->file(),
        activeConfig,
        originViewId,
        snapshotFile);
}

ViewMovePersistenceResult Storage::persistViewMoveSnapshot(
    const QString &originLayoutName,
    const QString &originFile,
    const QString &destinationLayoutName,
    const QString &destinationFile,
    const KSharedConfigPtr &activeConfig,
    const uint originViewId,
    const QString &snapshotFile,
    const ViewMoveInterruption interruption)
{
    const QString originBackend =
        persistenceBackendPath(originFile);
    const QString destinationBackend =
        persistenceBackendPath(destinationFile);
    const QString hiddenBackend =
        activeConfig
            ? persistenceBackendPath(
                activeConfig->name())
            : QString();
    const QString expectedHiddenBackend =
        persistenceBackendPath(
            Importer::layoutUserFilePath(
                QString::fromLatin1(
                    Layout::
                        MULTIPLELAYOUTSHIDDENNAME)));
    QStringList containmentIds =
        snapshotContainmentIds(
            snapshotFile);
    const QString rootId =
        QString::number(originViewId);
    if (!layoutNameIsSafe(
            originLayoutName)
            || !layoutNameIsSafe(
                destinationLayoutName)
            || originLayoutName
                == destinationLayoutName
            || originBackend.isEmpty()
            || destinationBackend.isEmpty()
            || originBackend
                == destinationBackend
            || hiddenBackend.isEmpty()
            || hiddenBackend
                != expectedHiddenBackend
            || hiddenBackend
                == originBackend
            || hiddenBackend
                == destinationBackend
            || !endpointIsDirectLayoutChild(
                originBackend)
            || !endpointIsDirectLayoutChild(
                destinationBackend)
            || !endpointIsDirectLayoutChild(
                hiddenBackend)
            || !persistenceEndpointIsWritable(
                originBackend)
            || !persistenceEndpointIsWritable(
                destinationBackend)
            || !persistenceEndpointIsWritable(
                hiddenBackend)
            || containmentIds.isEmpty()
            || !containmentIds.contains(rootId)) {
        qCritical() << "Storage::persistViewMoveSnapshot refused an inconsistent durable move"
                    << originLayoutName
                    << destinationLayoutName
                    << originViewId;
        return {
            .status =
                ViewMovePersistenceResult::
                    Status::Rejected,
            .transactionPath = {},
            .error =
                QStringLiteral(
                    "inconsistent durable move paths or snapshot")};
    }

    const auto initialOwner =
        observePersistentOwner(
            hiddenBackend,
            containmentIds,
            originLayoutName,
            destinationLayoutName);
    if (initialOwner
            != Layout::ViewMoveTransaction::
                PersistentOwner::Origin) {
        qCritical() << "Storage::persistViewMoveSnapshot refused containment"
                    << originViewId
                    << "whose persistent owner is not"
                    << originLayoutName;
        return {
            .status =
                ViewMovePersistenceResult::
                    Status::Rejected,
            .transactionPath = {},
            .error =
                QStringLiteral(
                    "active persistence does not name the source layout")};
    }

    const QString transactionsRoot =
        viewMoveTransactionsRoot();
    if (!QDir().mkpath(transactionsRoot)) {
        qCritical() << "Storage::persistViewMoveSnapshot could not create transaction root"
                    << transactionsRoot;
        return {
            .status =
                ViewMovePersistenceResult::
                    Status::Rejected,
            .transactionPath = {},
            .error =
                QStringLiteral(
                    "could not create the durable transaction directory")};
    }
    const QFileInfo transactionsRootInfo(
        transactionsRoot);
    if (!transactionsRootInfo.isDir()
            || transactionsRootInfo.isSymLink()
            || transactionsRootInfo.ownerId()
                != static_cast<uint>(::getuid())
            || !QFile::setPermissions(
                transactionsRoot,
                ViewMovePrivateDirectoryPermissions)) {
        qCritical() << "Storage::persistViewMoveSnapshot refused an unsafe transaction root"
                    << transactionsRoot;
        return {
            .status =
                ViewMovePersistenceResult::
                    Status::Rejected,
            .transactionPath = {},
            .error =
                QStringLiteral(
                    "durable transaction storage is not private")};
    }

    QLockFile transactionLock(
        QDir(transactionsRoot)
            .filePath(
                QStringLiteral(
                    "transaction.lock")));
    transactionLock.setStaleLockTime(0);
    if (!transactionLock.tryLock(0)) {
        qCritical() << "Storage::persistViewMoveSnapshot refused a concurrent durable move";
        return {
            .status =
                ViewMovePersistenceResult::
                    Status::Rejected,
            .transactionPath = {},
            .error =
                QStringLiteral(
                    "another durable move owns the transaction lock")};
    }

    if (!pendingViewMoveTransactions()
             .isEmpty()) {
        qCritical() << "Storage::persistViewMoveSnapshot refused a new move while recovery is pending";
        return {
            .status =
                ViewMovePersistenceResult::
                    Status::RejectedRecoveryRequired,
            .transactionPath = {},
            .error =
                QStringLiteral(
                    "an earlier durable move requires recovery")};
    }

    const KSharedConfigPtr destinationConfig =
        KSharedConfig::openConfig(
            destinationBackend,
            KConfig::SimpleConfig);
    const KSharedConfigPtr originConfig =
        KSharedConfig::openConfig(
            originBackend,
            KConfig::SimpleConfig);
    if (!configAllowsContainmentReplacement(
            originConfig,
            containmentIds)
            || !configAllowsContainmentReplacement(
                destinationConfig,
                containmentIds)
            || !configAllowsLayoutOwnerMutation(
                activeConfig,
                containmentIds)) {
        qCritical() << "Storage::persistViewMoveSnapshot refused an immutable or read-only move participant";
        return {
            .status =
                ViewMovePersistenceResult::
                    Status::Rejected,
            .transactionPath = {},
            .error =
                QStringLiteral(
                    "a durable move participant is immutable or read-only")};
    }

    const QStringList endpointPaths{
        originBackend,
        destinationBackend,
        hiddenBackend,
    };
    for (const QString &endpointPath :
            endpointPaths) {
        if (!kConfigEndpointLockIsAvailable(
                endpointPath)) {
            qCritical() << "Storage::persistViewMoveSnapshot refused a locked move participant"
                        << endpointPath;
            return {
                .status =
                    ViewMovePersistenceResult::
                        Status::Rejected,
                .transactionPath = {},
                .error =
                    QStringLiteral(
                        "a durable move participant is locked")};
        }
    }

    if (!configOmitsSnapshot(
            destinationBackend,
            containmentIds)) {
        qCritical() << "Storage::persistViewMoveSnapshot refused destination id collision for"
                    << containmentIds
                    << "in" << destinationBackend;
        return {
            .status =
                ViewMovePersistenceResult::
                    Status::Rejected,
            .transactionPath = {},
            .error =
                QStringLiteral(
                    "destination already contains an affected identity")};
    }

    ViewMoveJournalRecord journal;
    journal.transactionId =
        QUuid::createUuid()
            .toString(
                QUuid::WithoutBraces);
    journal.originLayoutName =
        originLayoutName;
    journal.originFile =
        originBackend;
    journal.destinationLayoutName =
        destinationLayoutName;
    journal.destinationFile =
        destinationBackend;
    journal.hiddenFile =
        hiddenBackend;
    journal.rootContainmentId =
        originViewId;
    journal.containmentIds =
        containmentIds;
    journal.directoryPath =
        QDir(transactionsRoot)
            .filePath(
                journal.transactionId
                + QString::fromLatin1(
                    ViewMovePreparedSuffix));

    Layout::ViewMoveTransaction transaction;
    const auto pureResult =
        transaction.commit(
            [&journal,
             &snapshotFile,
             &transactionsRoot]() {
                if (!QDir().mkdir(
                        journal.directoryPath)) {
                    qCritical() << "view move transaction could not create"
                                << journal.directoryPath;
                    return false;
                }
                if (!QFile::setPermissions(
                        journal.directoryPath,
                        ViewMovePrivateDirectoryPermissions)) {
                    qCritical() << "view move transaction could not make its journal private"
                                << journal.directoryPath;
                    return false;
                }
                if (!copyFileAtomically(
                        snapshotFile,
                        journal.snapshotPath())) {
                    return false;
                }
                journal.snapshotSha256 =
                    fileSha256(
                        journal.snapshotPath());
                if (journal.snapshotSha256
                        .isEmpty()
                        || !writeJournalManifest(
                            journal)
                        || !promotePreparedJournal(
                            journal,
                            transactionsRoot)) {
                    return false;
                }

                const auto persisted =
                    readJournalManifest(
                        journal.directoryPath);
                return persisted
                    && persisted
                        ->snapshotSha256
                        == journal
                            .snapshotSha256
                    && fileSha256(
                        journal.snapshotPath())
                        == journal
                            .snapshotSha256;
            },
            [destinationConfig,
             &journal]() {
                return publishSnapshot(
                    destinationConfig,
                    journal.snapshotPath(),
                    journal.containmentIds,
                    QString(),
                    false);
            },
            [activeConfig,
             &journal,
             interruption]() {
                if (interruption
                        == ViewMoveInterruption::
                            AfterDestinationPublish) {
                    return false;
                }
                return publishLayoutOwner(
                    activeConfig,
                    journal.containmentIds,
                    journal
                        .destinationLayoutName);
            },
            [&journal,
             interruption]() {
                if (interruption
                        == ViewMoveInterruption::
                            AfterDestinationPublish) {
                    return Layout::
                        ViewMoveTransaction::
                            PersistentOwner::Unknown;
                }
                return observePersistentOwner(
                    journal.hiddenFile,
                    journal
                        .containmentIds,
                    journal.originLayoutName,
                    journal
                        .destinationLayoutName);
            },
            [destinationConfig,
             &journal]() {
                return tombstoneSnapshot(
                    destinationConfig,
                    journal
                        .containmentIds);
            },
            [&journal,
             interruption]() {
                if (interruption
                        == ViewMoveInterruption::
                            AfterCommitDecision) {
                    return false;
                }
                const KSharedConfigPtr
                    originConfig =
                        KSharedConfig::
                            openConfig(
                                journal
                                    .originFile,
                                KConfig::
                                    SimpleConfig);
                return tombstoneSnapshot(
                    originConfig,
                    journal
                        .containmentIds);
            });

    ViewMovePersistenceResult result;
    result.transactionPath =
        journal.directoryPath;
    switch (pureResult) {
    case Layout::ViewMoveTransaction::
            Result::Rejected: {
        result.status =
            ViewMovePersistenceResult::
                Status::Rejected;
        result.error =
            QStringLiteral(
                "durable move was refused before its commit decision");
        const bool journalCleanupSucceeded =
            !QFileInfo::exists(
                journal.directoryPath)
            || (journal.directoryPath
                    .endsWith(
                        QString::fromLatin1(
                            ViewMovePendingSuffix))
                ? completeViewMovePersistence(
                    journal.directoryPath)
                : discardPreparedJournal(
                    journal.directoryPath,
                    transactionsRoot));
        if (!journalCleanupSucceeded) {
            result.status =
                ViewMovePersistenceResult::
                    Status::
                        RejectedRecoveryRequired;
            result.error =
                QStringLiteral(
                    "durable move rollback completed but journal cleanup requires recovery");
        }
        break;
    }
    case Layout::ViewMoveTransaction::
            Result::
                RejectedRecoveryRequired:
        result.status =
            ViewMovePersistenceResult::
                Status::
                    RejectedRecoveryRequired;
        result.error =
            QStringLiteral(
                "durable move was not committed and rollback requires recovery");
        break;
    case Layout::ViewMoveTransaction::
            Result::Committed:
        result.status =
            ViewMovePersistenceResult::
                Status::Committed;
        break;
    case Layout::ViewMoveTransaction::
            Result::
                CommittedRecoveryRequired:
        result.status =
            ViewMovePersistenceResult::
                Status::
                    CommittedRecoveryRequired;
        result.error =
            QStringLiteral(
                "durable move committed but source retirement requires recovery");
        break;
    }
    return result;
}

QStringList Storage::pendingViewMoveTransactions() const
{
    const QDir root(
        viewMoveTransactionsRoot());
    if (!root.exists()) {
        return {};
    }

    QStringList transactions;
    const QFileInfoList entries =
        root.entryInfoList(
            {QStringLiteral("*")
                + QString::fromLatin1(
                    ViewMovePendingSuffix)},
            QDir::Dirs
                | QDir::NoDotAndDotDot,
            QDir::Name);
    transactions.reserve(entries.size());
    for (const QFileInfo &entry :
            entries) {
        transactions.append(
            entry.absoluteFilePath());
    }
    return transactions;
}

QString Storage::viewMoveTransactionsData() const
{
    QJsonArray transactions;
    const QString expectedHiddenFile =
        persistenceBackendPath(
            Importer::layoutUserFilePath(
                QString::fromLatin1(
                    Layout::
                        MULTIPLELAYOUTSHIDDENNAME)));
    const QStringList paths =
        pendingViewMoveTransactions();
    for (const QString &path : paths) {
        const auto journal =
            readJournalManifest(path);
        if (!journal) {
            QString transactionId =
                QFileInfo(path).fileName();
            transactionId.chop(
                QString::fromLatin1(
                    ViewMovePendingSuffix)
                    .size());
            transactions.append(
                QJsonObject{
                    {QStringLiteral(
                         "transactionId"),
                     transactionId},
                    {QStringLiteral(
                         "journalValid"),
                     false},
                    {QStringLiteral(
                         "persistentOwner"),
                     QStringLiteral(
                         "unknown")},
                    {QStringLiteral(
                         "recoveryAction"),
                     QStringLiteral(
                         "refuse")},
                });
            continue;
        }

        const bool journalValid =
            journalEndpointsAreConsistent(
                *journal,
                expectedHiddenFile)
            && snapshotContainmentIds(
                journal->snapshotPath())
                == journal
                    ->containmentIds
            && fileSha256(
                journal->snapshotPath())
                == journal
                    ->snapshotSha256;
        const auto owner =
            journalValid
                ? observePersistentOwner(
                    journal->hiddenFile,
                    journal
                        ->containmentIds,
                    journal
                        ->originLayoutName,
                    journal
                        ->destinationLayoutName)
                : Layout::
                    ViewMoveTransaction::
                        PersistentOwner::Unknown;
        const auto action =
            Layout::ViewMoveTransaction::
                recoveryAction(owner);
        QJsonArray containmentIds;
        for (const QString &id :
                journal->containmentIds) {
            containmentIds.append(id);
        }
        transactions.append(
            QJsonObject{
                {QStringLiteral(
                     "transactionId"),
                 journal->transactionId},
                {QStringLiteral(
                     "journalValid"),
                 journalValid},
                {QStringLiteral(
                     "originLayout"),
                 journal->originLayoutName},
                {QStringLiteral(
                     "destinationLayout"),
                 journal
                     ->destinationLayoutName},
                {QStringLiteral(
                     "rootContainmentId"),
                 static_cast<qint64>(
                     journal
                         ->rootContainmentId)},
                {QStringLiteral(
                     "containmentIds"),
                 containmentIds},
                {QStringLiteral(
                     "persistentOwner"),
                 QString::fromLatin1(
                     persistentOwnerName(
                         owner))},
                {QStringLiteral(
                     "recoveryAction"),
                 QString::fromLatin1(
                     recoveryActionName(
                         action))},
            });
    }

    return QString::fromUtf8(
        QJsonDocument(
            QJsonObject{
                {QStringLiteral(
                     "schemaVersion"),
                 ViewMoveJournalSchemaVersion},
                {QStringLiteral(
                     "transactions"),
                 transactions},
            })
            .toJson(
                QJsonDocument::Compact));
}

bool Storage::completeViewMovePersistence(
    const QString &transactionPath)
{
    const QFileInfo transactionInfo(
        transactionPath);
    const QFileInfo rootInfo(
        viewMoveTransactionsRoot());
    if (!transactionInfo.exists()
            || !transactionInfo.isDir()
            || transactionInfo.isSymLink()
            || !transactionInfo.fileName()
                .endsWith(
                    QString::fromLatin1(
                        ViewMovePendingSuffix))
            || transactionInfo
                .absolutePath()
                != rootInfo
                    .absoluteFilePath()) {
        qCritical() << "Storage::completeViewMovePersistence refused transaction path"
                    << transactionPath;
        return false;
    }

    QString completedName =
        transactionInfo.fileName();
    completedName.chop(
        QString::fromLatin1(
            ViewMovePendingSuffix)
            .size());
    completedName +=
        QString::fromLatin1(
            ViewMoveCompletedSuffix);
    QDir root(rootInfo.absoluteFilePath());
    if (!root.rename(
            transactionInfo.fileName(),
            completedName)
            || !flushDirectory(
                root.absolutePath())) {
        qCritical() << "Storage::completeViewMovePersistence could not retire transaction"
                    << transactionPath;
        return false;
    }

    const QString completedPath =
        root.filePath(completedName);
    if (!QDir(completedPath)
            .removeRecursively()) {
        qCritical() << "Storage::completeViewMovePersistence left completed journal cleanup residue"
                    << completedPath;
        return false;
    }
    return flushDirectory(
        root.absolutePath());
}

bool Storage::recoverPendingViewMoves()
{
    return recoverPendingViewMovesIn(
        viewMoveTransactionsRoot(),
        persistenceBackendPath(
            Importer::layoutUserFilePath(
                QString::fromLatin1(
                    Layout::
                        MULTIPLELAYOUTSHIDDENNAME))));
}

bool Storage::recoverPendingViewMovesIn(
    const QString &transactionsRoot,
    const QString &expectedHiddenFile,
    const ViewMoveRecoveryInterruption
        interruption)
{
    QDir root(transactionsRoot);
    if (!root.exists()) {
        return true;
    }

    const QFileInfo rootInfo(
        root.absolutePath());
    if (!rootInfo.isDir()
            || rootInfo.isSymLink()
            || rootInfo.ownerId()
                != static_cast<uint>(::getuid())
            || !QFile::setPermissions(
                root.absolutePath(),
                ViewMovePrivateDirectoryPermissions)) {
        qCritical() << "Storage::recoverPendingViewMoves refused an unsafe transaction root"
                    << root.absolutePath();
        return false;
    }

    QLockFile transactionLock(
        root.filePath(
            QStringLiteral(
                "transaction.lock")));
    transactionLock.setStaleLockTime(0);
    if (!transactionLock.tryLock(0)) {
        qCritical() << "Storage::recoverPendingViewMoves could not acquire the durable move lock";
        return false;
    }

    const QFileInfoList completedEntries =
        root.entryInfoList(
            {QStringLiteral("*")
                + QString::fromLatin1(
                    ViewMoveCompletedSuffix)},
            QDir::Dirs
                | QDir::NoDotAndDotDot,
            QDir::Name);
    for (const QFileInfo &entry :
            completedEntries) {
        if (entry.isSymLink()
                || !QDir(entry.absoluteFilePath())
                .removeRecursively()) {
            qCritical() << "Storage::recoverPendingViewMoves could not remove completed transaction residue"
                        << entry.absoluteFilePath();
            return false;
        }
    }
    if (!completedEntries.isEmpty()
            && !flushDirectory(
                root.absolutePath())) {
        return false;
    }

    const QFileInfoList preparedEntries =
        root.entryInfoList(
            {QStringLiteral("*")
                + QString::fromLatin1(
                    ViewMovePreparedSuffix)},
            QDir::Dirs
                | QDir::NoDotAndDotDot,
            QDir::Name);
    for (const QFileInfo &entry :
            preparedEntries) {
        //! Targets are not touched until a complete journal is atomically
        //! promoted from .prepare to .move.
        if (entry.isSymLink()
                || !QDir(entry.absoluteFilePath())
                .removeRecursively()) {
            qCritical() << "Storage::recoverPendingViewMoves could not remove an unpublished journal"
                        << entry.absoluteFilePath();
            return false;
        }
    }
    if (!preparedEntries.isEmpty()
            && !flushDirectory(
                root.absolutePath())) {
        return false;
    }

    const QFileInfoList pendingEntries =
        root.entryInfoList(
            {QStringLiteral("*")
                + QString::fromLatin1(
                    ViewMovePendingSuffix)},
            QDir::Dirs
                | QDir::NoDotAndDotDot,
            QDir::Name);
    for (const QFileInfo &entry :
            pendingEntries) {
        if (entry.isSymLink()) {
            qCritical() << "Storage::recoverPendingViewMoves refused a linked transaction directory"
                        << entry.absoluteFilePath();
            return false;
        }
        const auto journal =
            readJournalManifest(
                entry.absoluteFilePath());
        if (!journal) {
            qCritical() << "Storage::recoverPendingViewMoves refused a malformed committed journal";
            return false;
        }

        QString directoryTransactionId =
            entry.fileName();
        directoryTransactionId.chop(
            QString::fromLatin1(
                ViewMovePendingSuffix)
                .size());
        const QStringList snapshotIds =
            snapshotContainmentIds(
                journal
                    ->snapshotPath());
        if (journal->transactionId
                != directoryTransactionId
                || !journalEndpointsAreConsistent(
                    *journal,
                    expectedHiddenFile)
                || snapshotIds
                    != journal
                        ->containmentIds
                || fileSha256(
                    journal
                        ->snapshotPath())
                    != journal
                        ->snapshotSha256) {
            qCritical() << "Storage::recoverPendingViewMoves refused inconsistent journal"
                        << entry.absoluteFilePath();
            return false;
        }

        const QStringList endpointPaths{
            journal->originFile,
            journal->destinationFile,
            journal->hiddenFile,
        };
        for (const QString &endpointPath :
                endpointPaths) {
            //! A held KConfig lock otherwise makes synchronous startup
            //! recovery wait for KConfig's long backend timeout. Leave the
            //! journal intact and retry on the next startup instead.
            if (!persistenceEndpointIsWritable(
                    endpointPath)
                    || !kConfigEndpointLockIsAvailable(
                        endpointPath)) {
                qCritical() << "Storage::recoverPendingViewMoves refused an unavailable recovery endpoint"
                            << endpointPath;
                return false;
            }
        }

        const KSharedConfigPtr
            originConfig =
                KSharedConfig::openConfig(
                    journal->originFile,
                    KConfig::SimpleConfig);
        const KSharedConfigPtr
            destinationConfig =
                KSharedConfig::openConfig(
                    journal
                        ->destinationFile,
                    KConfig::SimpleConfig);
        const KSharedConfigPtr
            hiddenConfig =
                KSharedConfig::openConfig(
                    journal->hiddenFile,
                    KConfig::SimpleConfig);
        const auto owner =
            observePersistentOwner(
                journal->hiddenFile,
                journal
                    ->containmentIds,
                journal
                    ->originLayoutName,
                journal
                    ->destinationLayoutName);
        const auto action =
            Layout::ViewMoveTransaction::
                recoveryAction(owner);

        bool recovered{false};
        switch (action) {
        case Layout::ViewMoveTransaction::
                RecoveryAction::RollBack: {
            const bool firstRepositoryPublished =
                publishSnapshot(
                    originConfig,
                    journal->snapshotPath(),
                    journal
                        ->containmentIds,
                    QString(),
                    true);
            const bool firstInterruption =
                interruption
                == ViewMoveRecoveryInterruption::
                    AfterFirstRepositoryPublication;
            const bool secondRepositoryPublished =
                firstRepositoryPublished
                && !firstInterruption
                && publishLayoutOwner(
                    hiddenConfig,
                    journal
                        ->containmentIds,
                    journal
                        ->originLayoutName);
            const bool secondInterruption =
                interruption
                == ViewMoveRecoveryInterruption::
                    AfterSecondRepositoryPublication;
            recovered =
                secondRepositoryPublished
                && !secondInterruption
                && tombstoneSnapshot(
                    destinationConfig,
                    journal
                        ->containmentIds);
            break;
        }
        case Layout::ViewMoveTransaction::
                RecoveryAction::RollForward: {
            const bool firstRepositoryPublished =
                publishSnapshot(
                    destinationConfig,
                    journal->snapshotPath(),
                    journal
                        ->containmentIds,
                    QString(),
                    true);
            const bool firstInterruption =
                interruption
                == ViewMoveRecoveryInterruption::
                    AfterFirstRepositoryPublication;
            const bool secondRepositoryPublished =
                firstRepositoryPublished
                && !firstInterruption
                && publishLayoutOwner(
                    hiddenConfig,
                    journal
                        ->containmentIds,
                    journal
                        ->destinationLayoutName);
            const bool secondInterruption =
                interruption
                == ViewMoveRecoveryInterruption::
                    AfterSecondRepositoryPublication;
            recovered =
                secondRepositoryPublished
                && !secondInterruption
                && tombstoneSnapshot(
                    originConfig,
                    journal
                        ->containmentIds);
            break;
        }
        case Layout::ViewMoveTransaction::
                RecoveryAction::Refuse:
            qCritical() << "Storage::recoverPendingViewMoves could not identify the committed owner for"
                        << journal
                            ->rootContainmentId;
            return false;
        }

        if (!recovered
                || !completeViewMovePersistence(
                    entry.absoluteFilePath())) {
            qCritical() << "Storage::recoverPendingViewMoves could not converge transaction"
                        << journal
                            ->transactionId;
            return false;
        }
    }
    return root.entryList(
        {QStringLiteral("*")
            + QString::fromLatin1(
                ViewMovePendingSuffix)},
        QDir::Dirs
            | QDir::NoDotAndDotDot)
        .isEmpty();
}

void Storage::removeContainment(const QString &filepath, const QString &containmentId)
{
    if (containmentId.isEmpty()) {
        return;
    }

    KSharedConfigPtr lFile = KSharedConfig::openConfig(filepath);
    KConfigGroup containmentGroups = KConfigGroup(lFile, "Containments");

    if (!containmentGroups.group(containmentId).exists()) {
        return;
    }

    containmentGroups.group(containmentId).deleteGroup();
    lFile->reparseConfiguration();
}

QStringList Storage::storedLayoutsInMultipleFile()
{
    QStringList layouts;
    QString linkedFilePath = Importer::layoutUserFilePath(Layout::MULTIPLELAYOUTSHIDDENNAME);

    if (linkedFilePath.isEmpty() || !QFileInfo(linkedFilePath).exists()) {
        return layouts;
    }

    KSharedConfigPtr filePtr = KSharedConfig::openConfig(linkedFilePath);
    KConfigGroup linkedContainments = KConfigGroup(filePtr, "Containments");

    for(const auto &cId : linkedContainments.groupList()) {
        QString layoutName = linkedContainments.group(cId).readEntry("layoutId", QString());

        if (!layoutName.isEmpty() && !layouts.contains(layoutName)) {
            layouts << layoutName;
        }
    }

    return layouts;
}


QString Storage::storedView(const Layout::GenericLayout *layout, const int &containmentId)
{
    //! make sure that layout and containmentId are valid
    if (!layout) {
        return QString();
    }

    if (layout->isActive()) {
        auto containment = layout->containmentForId((uint)containmentId);
        if (!containment || !isLatteContainment(containment)) {
            return QString();
        }
    } else {
        if (!containsView(layout->file(), containmentId)) {
            return QString();
        }
    }

    //! at this point we are sure that both layout and containmentId are acceptable
    QString nextTmpStoredViewAbsolutePath = m_storageTmpDir.path() + "/" + QFileInfo(layout->name()).fileName() + "." + QString::number(containmentId) + ".stored.tmp";

    QFile tempStoredViewFile(nextTmpStoredViewAbsolutePath);

    if (tempStoredViewFile.exists()) {
        tempStoredViewFile.remove();
    }

    KSharedConfigPtr destinationPtr = KSharedConfig::openConfig(nextTmpStoredViewAbsolutePath);
    KConfigGroup destinationContainments = KConfigGroup(destinationPtr, "Containments");
    QStringList capturedContainmentIds;

    if (layout->isActive()) {
        //! update and copy containments
        auto containment = layout->containmentForId((uint)containmentId);
        syncContainmentConfig(containment);

        const QString rootId = QString::number(containment->id());
        capturedContainmentIds.append(rootId);
        KConfigGroup destinationViewContainment(&destinationContainments, rootId);
        containment->config().copyTo(&destinationViewContainment);

        QList<Plasma::Containment *> subconts = layout->subContainmentsOf(containment->id());

        for(const auto subcont : subconts) {
            syncContainmentConfig(subcont);
            const QString subId = QString::number(subcont->id());
            capturedContainmentIds.append(subId);
            KConfigGroup destinationsubcontainment(&destinationContainments, subId);
            subcont->config().copyTo(&destinationsubcontainment);
        }

        //! update with latest view data if active view is present
        auto view = layout->viewForContainment(containment);

        if (view) {
            Data::View currentviewdata = view->data();
            updateView(destinationViewContainment, currentviewdata);
        }
    } else {
        QString containmentid = QString::number(containmentId);
        capturedContainmentIds.append(containmentid);
        KConfigGroup destinationViewContainment(&destinationContainments, containmentid);

        KSharedConfigPtr originPtr = KSharedConfig::openConfig(layout->file());
        KConfigGroup originContainments = KConfigGroup(originPtr, "Containments");

        originContainments.group(containmentid).copyTo(&destinationViewContainment);

        Data::GenericTable<Data::Generic> subconts = subcontainments(originContainments.group(containmentid));

        for(int i=0; i<subconts.rowCount(); ++i) {
            QString subid = subconts[i].id;
            capturedContainmentIds.append(subid);
            KConfigGroup destinationsubcontainment(&destinationContainments, subid);
            originContainments.group(subid).copyTo(&destinationsubcontainment);
        }
    }

    if (!destinationPtr->sync()) {
        qCritical() << "Storage::storedView could not persist removal snapshot"
                    << nextTmpStoredViewAbsolutePath;
        return {};
    }
    destinationPtr->reparseConfiguration();
    for (const QString &capturedId : std::as_const(capturedContainmentIds)) {
        if (!destinationContainments.hasGroup(capturedId)) {
            qCritical() << "Storage::storedView did not persist containment"
                        << capturedId << "to removal snapshot"
                        << nextTmpStoredViewAbsolutePath;
            return {};
        }
    }

    return nextTmpStoredViewAbsolutePath;
}

int Storage::expectedViewScreenId(const Latte::Corona *corona, const KConfigGroup &containmentGroup) const
{
    return expectedViewScreenId(corona, self()->view(containmentGroup));
}

int Storage::expectedViewScreenId(const Layout::GenericLayout *layout, const Plasma::Containment *lattecontainment) const
{
    if (!layout || !layout->corona()) {
        return Latte::ScreenPool::NOSCREENID;
    }

    return expectedViewScreenId(layout->corona(), self()->view(layout, lattecontainment));
}

int Storage::expectedViewScreenId(const Latte::Corona *corona, const Data::View &view) const
{
    if (!corona || !view.isValid()) {
        return Latte::ScreenPool::NOSCREENID;
    }

    if (view.screensGroup == Latte::Types::SingleScreenGroup || view.isCloned()) {
        return view.onPrimary ? corona->screenPool()->primaryScreenId() : view.screen;
    } else if (view.screensGroup == Latte::Types::AllScreensGroup) {
        return corona->screenPool()->primaryScreenId();
    } else if (view.screensGroup == Latte::Types::AllSecondaryScreensGroup) {
        QList<int> secondaryscreens = corona->screenPool()->secondaryScreenIds();
        return secondaryscreens.contains(view.screen) || secondaryscreens.isEmpty() ? view.screen : secondaryscreens[0];
    }

    return Latte::ScreenPool::NOSCREENID;
}

Data::ViewsTable Storage::views(const Layout::GenericLayout *layout)
{
    Data::ViewsTable vtable;

    if (!layout) {
        return vtable;
    } else if (!layout->isActive()) {
        return views(layout->file());
    }

    for (const auto containment : (*layout->containments())) {
        if (!isLatteContainment(containment)) {
            continue;
        }

        Latte::View *vw = layout->viewForContainment(containment);

        if (vw) {
            vtable << vw->data();
        } else {
            vtable << view(layout, containment);
        }
    }

    return vtable;
}

Data::ViewsTable Storage::views(const QString &file)
{
    Data::ViewsTable vtable;

    KSharedConfigPtr lFile = KSharedConfig::openConfig(file);
    KConfigGroup containmentGroups = KConfigGroup(lFile, "Containments");

    for (const auto &cId : containmentGroups.groupList()) {
        if (Layouts::Storage::self()->isLatteContainment(containmentGroups.group(cId))) {
            vtable << view(containmentGroups.group(cId));
        }
    }

    return vtable;
}

}
}
