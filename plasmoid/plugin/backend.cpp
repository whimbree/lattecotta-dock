/*
    SPDX-FileCopyrightText: 2012-2016 Eike Hein <hein@kde.org>
    SPDX-FileCopyrightText: 2026 Bree Spektor

    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Vendored from plasma-desktop applets/taskmanager (Eike Hein's backend),
//! adopted 2026-07-07 (our commit 14c973b3) following the vendoring
//! decision in David Goree's latte-dock-qt6
//! (github.com/CaptSilver/latte-dock-qt6):
//! Plasma 6 folded the Task Manager applet into C++ and ships no public
//! replacement for these helpers (docs/reference/taskmanager-integration-research.md).
//! Latte extensions on top of upstream: the KWin WindowView/HighlightWindow
//! QDBusServiceWatcher wiring and showAudioStreamOsd. At reference-fork
//! sync time, diff upstream's file against this one to port their fixes.

#include "backend.h"

#include <KConfigGroup>
#include <KDesktopFile>
#include <KFileItem>
#include <KFilePlacesModel>
#include <KLocalizedString>
#include <KNotificationJobUiDelegate>
#include <KProtocolInfo>
#include <KService>
#include <KServiceAction>
#include <KWindowEffects>
#include <KWindowSystem>

#include <KApplicationTrader>
#include <KIO/ApplicationLauncherJob>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDebug>
#include <QJsonArray>
#include <QMenu>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QVersionNumber>

#include <limits>
#include <optional>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusServiceWatcher>

#include <PlasmaActivities/Consumer>
#include <PlasmaActivities/Stats/Cleaning>
#include <PlasmaActivities/Stats/ResultSet>
#include <PlasmaActivities/Stats/Terms>

#include <processcore/process.h>
#include <processcore/processes.h>

namespace KAStats = KActivities::Stats;

using namespace KAStats;
using namespace KAStats::Terms;

static constexpr int NoApplications = 2; // kactivitymanager StatsPlugin WhatToRemember.

namespace {

// Every Backend instance is driven on the GUI thread. One process-wide counter
// lets the D-Bus collector order events from multiple tasks applets without
// retaining a history or relying on wall-clock time.
qint64 s_middleClickDispatchSequence{0};

std::optional<Latte::Tasks::MiddleClickOperation> middleClickOperationFromToken(const QString &token)
{
    using Operation = Latte::Tasks::MiddleClickOperation;

    if (token.isEmpty()) {
        return Operation::None;
    }
    if (token == QLatin1String("activate")) {
        return Operation::RequestActivate;
    }
    if (token == QLatin1String("close")) {
        return Operation::RequestClose;
    }
    if (token == QLatin1String("newInstance")) {
        return Operation::RequestNewInstance;
    }
    if (token == QLatin1String("toggleMinimized")) {
        return Operation::RequestToggleMinimized;
    }
    if (token == QLatin1String("cycleOrActivate")) {
        return Operation::CycleOrActivate;
    }
    if (token == QLatin1String("toggleGrouping")) {
        return Operation::RequestToggleGrouping;
    }

    return std::nullopt;
}

}

Backend::Backend(QObject *parent)
    : QObject(parent)
    , m_actionGroup(new QActionGroup(this))
    , m_activityManagerPluginsSettingsWatcher(KConfigWatcher::create(m_activityManagerPluginsSettings.sharedConfig()))
{
    connect(m_activityManagerPluginsSettingsWatcher.get(),
            &KConfigWatcher::configChanged,
            this,
            [this](const KConfigGroup &group, const QByteArrayList &names) {
                if (group.name() == QLatin1String("Plugin-org.kde.ActivityManager.Resources.Scoring")
                    && names.contains(QByteArrayLiteral("what-to-remember"))) {
                    m_activityManagerPluginsSettings.load();
                }
            });

    m_windowViewWatcher = new QDBusServiceWatcher(QStringLiteral("org.kde.KWin.Effect.WindowView1"),
                                                  QDBusConnection::sessionBus(),
                                                  QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration,
                                                  this);
    connect(m_windowViewWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this] {
        if (!m_windowViewAvailable) {
            m_windowViewAvailable = true;
            Q_EMIT windowViewAvailableChanged();
        }
    });
    connect(m_windowViewWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] {
        if (m_windowViewAvailable) {
            m_windowViewAvailable = false;
            Q_EMIT windowViewAvailableChanged();
        }
    });

    QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    m_windowViewAvailable = bus && bus->isServiceRegistered(QStringLiteral("org.kde.KWin.Effect.WindowView1"));
}

Backend::~Backend()
{
}

QVariantMap Backend::latestMiddleClickDispatch() const
{
    if (!m_latestMiddleClickDispatch) {
        return {};
    }

    const auto &record = *m_latestMiddleClickDispatch;
    QVariantMap data;
    data.insert(QStringLiteral("rowIdentity"), record.rowIdentity);
    data.insert(QStringLiteral("rowKind"), static_cast<int>(record.rowKind));
    data.insert(QStringLiteral("configuredAction"), static_cast<int>(record.configuredAction));
    data.insert(QStringLiteral("dispatchedOperation"), static_cast<int>(record.dispatchedOperation));
    data.insert(QStringLiteral("sequence"), record.sequence);
    return data;
}

bool Backend::recordMiddleClickDispatch(const QString &rowIdentity,
                                        bool isLauncher,
                                        int configuredAction,
                                        const QString &operation)
{
    if (rowIdentity.isEmpty()) {
        qWarning() << "tasks backend: refusing middle-click dispatch with no stable row identity";
        return false;
    }

    const auto action = Latte::Tasks::classifyOfferedMiddleClickAction(configuredAction);
    if (!action) {
        qWarning() << "tasks backend: refusing middle-click dispatch with configured action outside the offered middle-click set"
                   << configuredAction;
        return false;
    }

    const auto parsedOperation = middleClickOperationFromToken(operation);
    if (!parsedOperation) {
        qWarning() << "tasks backend: refusing middle-click dispatch with unknown operation" << operation;
        return false;
    }

    const auto rowKind = isLauncher ? Latte::Tasks::MiddleClickRowKind::Launcher
                                    : Latte::Tasks::MiddleClickRowKind::Task;
    if (!Latte::Tasks::middleClickDispatchPairIsValid(rowKind, *action, *parsedOperation)) {
        qWarning() << "tasks backend: refusing middle-click dispatch whose configured action, row kind, and operation disagree";
        return false;
    }

    if (s_middleClickDispatchSequence == std::numeric_limits<qint64>::max()) {
        qCritical() << "tasks backend: middle-click dispatch sequence exhausted";
        return false;
    }

    Latte::Tasks::MiddleClickDispatchRecord record;
    record.rowIdentity = rowIdentity;
    record.rowKind = rowKind;
    record.configuredAction = action->configuredAction;
    record.dispatchedOperation = *parsedOperation;
    record.sequence = ++s_middleClickDispatchSequence;

    m_latestMiddleClickDispatch = std::move(record);
    Q_EMIT middleClickDispatchChanged();
    return true;
}

QUrl Backend::tryDecodeApplicationsUrl(const QUrl &launcherUrl)
{
    if (launcherUrl.isValid() && launcherUrl.scheme() == QLatin1String("applications")) {
        const KService::Ptr service = KService::serviceByMenuId(launcherUrl.path());

        if (service) {
            return QUrl::fromLocalFile(service->entryPath());
        }
    }

    return launcherUrl;
}

QStringList Backend::applicationCategories(const QUrl &launcherUrl)
{
    const QUrl desktopEntryUrl = tryDecodeApplicationsUrl(launcherUrl);

    if (!desktopEntryUrl.isValid() || !desktopEntryUrl.isLocalFile() || !KDesktopFile::isDesktopFile(desktopEntryUrl.toLocalFile())) {
        return QStringList();
    }

    KDesktopFile desktopFile(desktopEntryUrl.toLocalFile());

    // Since we can't have dynamic jump list actions, at least add the user's "Places" for file managers.
    return desktopFile.desktopGroup().readXdgListEntry(QStringLiteral("Categories"));
}

QVariantList Backend::jumpListActions(const QUrl &launcherUrl, QObject *parent)
{
    QVariantList actions;

    if (!parent) {
        return actions;
    }

    QUrl desktopEntryUrl = tryDecodeApplicationsUrl(launcherUrl);

    if (!desktopEntryUrl.isValid() || !desktopEntryUrl.isLocalFile() || !KDesktopFile::isDesktopFile(desktopEntryUrl.toLocalFile())) {
        return actions;
    }

    const KService::Ptr service = KService::serviceByDesktopPath(desktopEntryUrl.toLocalFile());
    if (!service) {
        return actions;
    }

    if (service->storageId() == QLatin1String("systemsettings.desktop")) {
        actions = systemSettingsActions(parent);
        if (!actions.isEmpty()) {
            return actions;
        }
    }

    const auto jumpListActions = service->actions();

    for (const KServiceAction &serviceAction : jumpListActions) {
        if (serviceAction.noDisplay()) {
            continue;
        }

        QAction *action = new QAction(parent);
        action->setText(serviceAction.text());
        action->setIcon(QIcon::fromTheme(serviceAction.icon()));
        if (serviceAction.isSeparator()) {
            action->setSeparator(true);
        }

        connect(action, &QAction::triggered, this, [serviceAction]() {
            auto *job = new KIO::ApplicationLauncherJob(serviceAction);
            auto *delegate = new KNotificationJobUiDelegate;
            delegate->setAutoErrorHandlingEnabled(true);
            job->setUiDelegate(delegate);
            job->start();
        });

        actions << QVariant::fromValue<QAction *>(action);
    }

    return actions;
}

QVariantList Backend::systemSettingsActions(QObject *parent) const
{
    QVariantList actions;

    if (m_activityManagerPluginsSettings.whatToRemember() == NoApplications) {
        return actions;
    }

    auto query = AllResources | Agent(QStringLiteral("org.kde.systemsettings")) | HighScoredFirst | Limit(5);

    ResultSet results(query);

    QStringList ids;
    for (const ResultSet::Result &result : results) {
        ids << QUrl(result.resource()).path();
    }

    if (ids.count() < 5) {
        // We'll load the default set of settings from its jump list actions.
        return actions;
    }

    for (const QString &id : std::as_const(ids)) {
        KService::Ptr service = KService::serviceByStorageId(id);
        if (!service || !service->isValid()) {
            continue;
        }

        QAction *action = new QAction(parent);
        action->setText(service->name());
        action->setIcon(QIcon::fromTheme(service->icon()));

        connect(action, &QAction::triggered, this, [service]() {
            auto *job = new KIO::ApplicationLauncherJob(service);
            auto *delegate = new KNotificationJobUiDelegate;
            delegate->setAutoErrorHandlingEnabled(true);
            job->setUiDelegate(delegate);
            job->start();
        });

        actions << QVariant::fromValue<QAction *>(action);
    }
    return actions;
}

QVariantList Backend::placesActions(const QUrl &launcherUrl, bool showAllPlaces, QObject *parent)
{
    if (!parent) {
        return QVariantList();
    }

    QUrl desktopEntryUrl = tryDecodeApplicationsUrl(launcherUrl);

    if (!desktopEntryUrl.isValid() || !desktopEntryUrl.isLocalFile() || !KDesktopFile::isDesktopFile(desktopEntryUrl.toLocalFile())) {
        return QVariantList();
    }

    QVariantList actions;

    // Since we can't have dynamic jump list actions, at least add the user's "Places" for file managers.
    if (!applicationCategories(launcherUrl).contains(QLatin1String("FileManager"))) {
        return actions;
    }

    QString previousGroup;
    QMenu *subMenu = nullptr;

    std::unique_ptr<KFilePlacesModel> placesModel(new KFilePlacesModel());
    for (int i = 0; i < placesModel->rowCount(); ++i) {
        QModelIndex idx = placesModel->index(i, 0);

        if (placesModel->isHidden(idx)) {
            continue;
        }

        const QString &title = idx.data(Qt::DisplayRole).toString();
        const QIcon &icon = idx.data(Qt::DecorationRole).value<QIcon>();
        const QUrl &url = idx.data(KFilePlacesModel::UrlRole).toUrl();

        QAction *placeAction = new QAction(icon, title, parent);

        connect(placeAction, &QAction::triggered, this, [url, desktopEntryUrl] {
            KService::Ptr service = KService::serviceByDesktopPath(desktopEntryUrl.toLocalFile());
            if (!service) {
                return;
            }

            auto *job = new KIO::ApplicationLauncherJob(service);
            auto *delegate = new KNotificationJobUiDelegate;
            delegate->setAutoErrorHandlingEnabled(true);
            job->setUiDelegate(delegate);

            job->setUrls({url});
            job->start();
        });

        const QString &groupName = idx.data(KFilePlacesModel::GroupRole).toString();
        if (previousGroup.isEmpty()) { // Skip first group heading.
            previousGroup = groupName;
        }

        // Put all subsequent categories into a submenu.
        if (previousGroup != groupName) {
            QAction *subMenuAction = new QAction(groupName, parent);
            subMenu = new QMenu();
            // Breeze and Oxygen have rounded corners on menus. They set this attribute in polish()
            // but at that time the underlying surface has already been created where setting this
            // flag makes no difference anymore (Bug 385311)
            subMenu->setAttribute(Qt::WA_TranslucentBackground);
            // Cannot parent a QMenu to a QAction, need to delete it manually.
            connect(parent, &QObject::destroyed, subMenu, &QObject::deleteLater);
            subMenuAction->setMenu(subMenu);

            actions << QVariant::fromValue(subMenuAction);

            previousGroup = groupName;
        }

        if (subMenu) {
            subMenu->addAction(placeAction);
        } else {
            actions << QVariant::fromValue(placeAction);
        }
    }

    // There is nothing more frustrating than having a "More" entry that ends up showing just one or two
    // additional entries. Therefore we truncate to max. 5 entries only if there are more than 7 in total.
    if (!showAllPlaces && actions.count() > 7) {
        const int totalActionCount = actions.count();

        while (actions.count() > 5) {
            actions.removeLast();
        }

        QAction *action = new QAction(parent);
        action->setIcon(QIcon::fromTheme(QStringLiteral("view-more-symbolic")));
        action->setText(i18ncp("Show all user Places", "%1 more Place…", "%1 more Places…", totalActionCount - actions.count()));
        connect(action, &QAction::triggered, this, &Backend::showAllPlaces);
        actions << QVariant::fromValue(action);
    }

    return actions;
}

QVariantList Backend::recentDocumentActions(const QUrl &launcherUrl, QObject *parent)
{
    QVariantList actions;
    if (!parent) {
        return actions;
    }

    if (m_activityManagerPluginsSettings.whatToRemember() == NoApplications) {
        return actions;
    }

    QUrl desktopEntryUrl = tryDecodeApplicationsUrl(launcherUrl);

    if (!desktopEntryUrl.isValid() || !desktopEntryUrl.isLocalFile() || !KDesktopFile::isDesktopFile(desktopEntryUrl.toLocalFile())) {
        return QVariantList();
    }

    QString desktopName = desktopEntryUrl.fileName();
    QString storageId = desktopName;

    if (storageId.endsWith(QLatin1String(".desktop"))) {
        storageId = storageId.left(storageId.length() - 8);
    }

    auto query = UsedResources | RecentlyUsedFirst | Agent(storageId) | Type::any() | Activity::current();

    ResultSet results(query);

    ResultSet::const_iterator resultIt = results.begin();

    int actionCount = 0;

    bool allFolders = true;
    bool allDownloads = true;
    bool allRemoteWithoutFileName = true;
    const QString downloadsPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);

    while (actionCount < 5 && resultIt != results.end()) {
        const QString resource = (*resultIt).resource();
        const QString mimetype = (*resultIt).mimetype();
        const QUrl url = (*resultIt).url();
        ++resultIt;

        if (!url.isValid()) {
            continue;
        }

        allFolders = allFolders && mimetype == QLatin1String("inode/directory");
        allDownloads = allDownloads && url.toLocalFile().startsWith(downloadsPath);
        allRemoteWithoutFileName = allRemoteWithoutFileName && !url.isLocalFile() && url.fileName().isEmpty();

        QString name;

        if (url.isLocalFile() && !url.fileName().isEmpty()) {
            name = url.fileName();
        } else {
            name = url.toDisplayString();
        }

        QAction *action = new QAction(parent);
        action->setText(name);
        action->setIcon(QIcon::fromTheme(KIO::iconNameForUrl(url)));
        action->setProperty("agent", storageId);
        action->setProperty("entryPath", desktopEntryUrl);
        action->setProperty("mimeType", mimetype);
        action->setData(url);
        connect(action, &QAction::triggered, this, &Backend::handleRecentDocumentAction);

        actions << QVariant::fromValue<QAction *>(action);

        ++actionCount;
    }

    if (actionCount > 0) {
        // Overrides section heading on QML side
        if (allDownloads) {
            actions.prepend(i18n("Recent Downloads"));
        } else if (allRemoteWithoutFileName) {
            actions.prepend(i18n("Recent Connections"));
        } else if (allFolders) {
            actions.prepend(i18n("Recent Places"));
        }

        QAction *separatorAction = new QAction(parent);
        separatorAction->setSeparator(true);
        actions << QVariant::fromValue<QAction *>(separatorAction);

        QAction *action = new QAction(parent);
        if (allDownloads) {
            action->setText(i18nc("@action:inmenu", "Forget Recent Downloads"));
        } else if (allRemoteWithoutFileName) {
            action->setText(i18nc("@action:inmenu", "Forget Recent Connections"));
        } else if (allFolders) {
            action->setText(i18nc("@action:inmenu", "Forget Recent Places"));
        } else {
            action->setText(i18nc("@action:inmenu", "Forget Recent Files"));
        }
        action->setIcon(QIcon::fromTheme(QStringLiteral("edit-clear-history")));
        action->setProperty("agent", storageId);
        connect(action, &QAction::triggered, this, &Backend::handleRecentDocumentAction);
        actions << QVariant::fromValue<QAction *>(action);
    }

    return actions;
}

void Backend::handleRecentDocumentAction() const
{
    const QAction *action = qobject_cast<QAction *>(sender());

    if (!action) {
        return;
    }

    const QString agent = action->property("agent").toString();

    if (agent.isEmpty()) {
        return;
    }

    const QString desktopPath = action->property("entryPath").toUrl().toLocalFile();
    const QUrl url = action->data().toUrl();

    if (desktopPath.isEmpty() || url.isEmpty()) {
        auto query = UsedResources | Agent(agent) | Type::any() | Activity::current();

        KAStats::forgetResources(query);

        return;
    }

    KService::Ptr service = KService::serviceByDesktopPath(desktopPath);

    if (!service) {
        return;
    }

    // prevents using a service file that does not support opening a mime type for a file it created
    // for instance spectacle
    const auto mimetype = action->property("mimeType").toString();
    if (!mimetype.isEmpty() && mimetype != QLatin1String("application/octet-stream")) {
        if (!service->hasMimeType(mimetype)) {
            // needs to find the application that supports this mimetype
            service = KApplicationTrader::preferredService(mimetype);

            if (!service) {
                // no service found to handle the mimetype
                return;
            } else {
                qWarning() << "Preventing the file to open with " << service->desktopEntryName() << "no alternative found";
            }
        }
    }

    auto *job = new KIO::ApplicationLauncherJob(service);
    auto *delegate = new KNotificationJobUiDelegate;
    delegate->setAutoErrorHandlingEnabled(true);
    job->setUiDelegate(delegate);
    job->setUrls({url});
    job->start();
}

void Backend::setActionGroup(QAction *action) const
{
    if (action) {
        action->setActionGroup(m_actionGroup);
    }
}

QRect Backend::globalRect(QQuickItem *item) const
{
    if (!item || !item->window()) {
        return QRect();
    }

    QRect iconRect(item->x(), item->y(), item->width(), item->height());
    iconRect.moveTopLeft(item->parentItem()->mapToScene(iconRect.topLeft()).toPoint());
    iconRect.moveTopLeft(item->window()->mapToGlobal(iconRect.topLeft()));

    return iconRect;
}

bool Backend::isApplication(const QUrl &url) const
{
    if (!url.isValid() || !url.isLocalFile()) {
        return false;
    }

    const QString &localPath = url.toLocalFile();

    if (!KDesktopFile::isDesktopFile(localPath)) {
        return false;
    }

    KDesktopFile desktopFile(localPath);
    return desktopFile.hasApplicationType();
}

qint64 Backend::parentPid(qint64 pid) const
{
    KSysGuard::Processes procs;
    procs.updateOrAddProcess(pid);

    KSysGuard::Process *proc = procs.getProcess(pid);
    if (!proc) {
        return -1;
    }

    int parentPid = proc->parentPid();
    if (parentPid != -1) {
        procs.updateOrAddProcess(parentPid);

        KSysGuard::Process *parentProc = procs.getProcess(parentPid);
        if (!parentProc) {
            return -1;
        }

        if (!proc->cGroup().isEmpty() && parentProc->cGroup() == proc->cGroup()) {
            return parentProc->pid();
        }
    }

    return -1;
}

QQuickItem *Backend::taskManagerItem() const
{
    return m_taskManagerItem;
}

void Backend::setTaskManagerItem(QQuickItem *item)
{
    if (item != m_taskManagerItem) {
        m_taskManagerItem = item;
        Q_EMIT taskManagerItemChanged();
    }
}

bool Backend::highlightWindows() const
{
    return m_highlightWindows;
}

void Backend::setHighlightWindows(bool highlight)
{
    if (highlight != m_highlightWindows) {
        m_highlightWindows = highlight;
        Q_EMIT highlightWindowsChanged();
    }
}

QList<QUrl> Backend::jsonArrayToUrlList(const QJsonArray &array) const
{
    QList<QUrl> urls;
    urls.reserve(array.count());

    for (auto it = array.constBegin(), end = array.constEnd(); it != end; ++it) {
        urls << QUrl(it->toString());
    }

    return urls;
}

QVariantMap Backend::generateMimeData(const QString &mimeType, const QVariant &mimeData, const QUrl &url)
{
    QVariantMap mimedata;

    const QString &taskUrlData = Backend::tryDecodeApplicationsUrl(url).toString();
    mimedata.insert(QStringLiteral("text/x-orgkdeplasmataskmanager_taskurl"), taskUrlData);

    mimedata.insert(mimeType, mimeData);
    mimedata.insert(QStringLiteral("application/x-orgkdeplasmataskmanager_taskbuttonitem"), mimeData);

    return mimedata;
}

void Backend::ungrabMouse(QQuickItem *item) const
{
    if (item) {
        item->ungrabMouse();
    }
}

bool Backend::windowViewAvailable() const
{
    return m_windowViewAvailable;
}

QStringList Backend::winIdsToStrings(const QVariant &winIds) const
{
    QStringList ids;

    const QVariantList list = winIds.toList();
    for (const QVariant &id : list) {
        ids << id.toString();
    }

    if (ids.isEmpty() && winIds.isValid() && !winIds.toString().isEmpty()) {
        ids << winIds.toString();
    }

    return ids;
}

void Backend::windowsHovered(const QVariant &winIds, bool hovered)
{
    if (!m_highlightWindows) {
        return;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin.HighlightWindow"),
                                                      QStringLiteral("/org/kde/KWin/HighlightWindow"),
                                                      QStringLiteral("org.kde.KWin.HighlightWindow"),
                                                      QStringLiteral("highlightWindows"));
    msg << (hovered ? winIdsToStrings(winIds) : QStringList());
    QDBusConnection::sessionBus().asyncCall(msg);
}

void Backend::cancelHighlightWindows()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin.HighlightWindow"),
                                                      QStringLiteral("/org/kde/KWin/HighlightWindow"),
                                                      QStringLiteral("org.kde.KWin.HighlightWindow"),
                                                      QStringLiteral("highlightWindows"));
    msg << QStringList();
    QDBusConnection::sessionBus().asyncCall(msg);
}

void Backend::showAudioStreamOsd(int percent, const QString &appName, const QUrl &launcherUrl)
{
    //! Resolve the app icon from the launcher's .desktop file so the OSD shows
    //! the application icon (audio-volume-* as a sane fallback).
    QString iconName;

    const QUrl desktopEntryUrl = tryDecodeApplicationsUrl(launcherUrl);

    if (desktopEntryUrl.isValid() && desktopEntryUrl.isLocalFile()
            && KDesktopFile::isDesktopFile(desktopEntryUrl.toLocalFile())) {
        const KService::Ptr service = KService::serviceByDesktopPath(desktopEntryUrl.toLocalFile());

        if (service) {
            iconName = service->icon();
        }
    }

    if (iconName.isEmpty()) {
        iconName = QStringLiteral("audio-volume-high");
    }

    //! plasmashell's centered media-player volume OSD (app icon + bar + %).
    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"),
                                                      QStringLiteral("/org/kde/osdService"),
                                                      QStringLiteral("org.kde.osdService"),
                                                      QStringLiteral("mediaPlayerVolumeChanged"));
    msg << percent << appName << iconName;
    QDBusConnection::sessionBus().asyncCall(msg);
}

void Backend::activateWindowView(const QVariant &winIds)
{
    cancelHighlightWindows();

    QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin.Effect.WindowView1"),
                                                      QStringLiteral("/org/kde/KWin/Effect/WindowView1"),
                                                      QStringLiteral("org.kde.KWin.Effect.WindowView1"),
                                                      QStringLiteral("activate"));
    msg << winIdsToStrings(winIds);
    QDBusConnection::sessionBus().asyncCall(msg);
}

#include "moc_backend.cpp"
