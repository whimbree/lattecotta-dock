/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dbusreports.h"

// local
#include "layout/centrallayout.h"
#include "layout/genericlayout.h"
#include "layouts/manager.h"
#include "layouts/synchronizer.h"
#include "view/containmentinterface.h"
#include "view/effects.h"
#include "view/positioner.h"
#include "view/view.h"
#include "view/visibilitymanager.h"
#include "view/windowstracker/currentscreentracker.h"
#include "view/windowstracker/windowstracker.h"

// Qt
#include <QAbstractItemModel>
#include <QFileInfo>
#include <QMetaMethod>
#include <QQuickItem>
#include <QUrl>

// KDE
#include <KPluginMetaData>

// Plasma
#include <Plasma/Applet>
#include <Plasma/Containment>

// PlasmaQuick
#include <plasmaquick/appletquickitem.h>

namespace Latte {
namespace DbusReports {

ViewRecord collectViewRecord(const Latte::View *view, bool inConfigureAppletsMode)
{
    //! views come from Synchronizer::currentViews(), not from outside the
    //! process, and a registered view always carries its containment and
    //! helper ensemble - so these are invariants to assert, not input to
    //! refuse at runtime
    Q_ASSERT(view);
    Q_ASSERT(view->containment());
    Q_ASSERT(view->positioner() && view->visibility() && view->effects());

    ViewRecord record;
    record.containmentId = view->containment()->id();
    //! a view legitimately has no layout for a moment while it moves
    //! between layouts (moveViewToLayout); an empty name reports that state
    record.layout = view->layout() ? view->layout()->name() : QString();
    record.isCloned = view->isCloned();
    //! groupId() is the original's containment id for clones; originals
    //! report -1 per docs/dbus-observability-interface.md
    record.isClonedFrom = record.isCloned ? view->groupId() : -1;
    record.type = view->type();
    record.screen = view->positioner()->currentScreenName();
    record.onPrimary = view->onPrimary();
    record.edge = view->location();
    //! View's alignment surface is int (its QML property type); the value
    //! is stored as Types::Alignment internally, so the cast is lossless
    record.alignment = static_cast<Types::Alignment>(view->alignment());
    record.visibilityMode = view->visibility()->mode();
    record.isHidden = view->visibility()->isHidden();
    record.inStartup = view->positioner()->inStartup();
    record.isOffScreen = view->positioner()->isOffScreen();
    record.absoluteGeometry = view->absoluteGeometry();
    record.localGeometry = view->localGeometry();
    record.screenGeometry = view->screenGeometry();
    record.strutsThickness = view->visibility()->strutsThickness();
    record.publishedStruts = view->visibility()->publishedStruts();
    record.maskRect = view->effects()->mask();
    record.inputMask = view->effects()->inputMask();
    record.appliedInputMask = view->effects()->appliedInputMask();
    record.editMode = view->inEditMode();
    record.inConfigureAppletsMode = inConfigureAppletsMode;
    record.keyboardNavigation = view->keyboardNavigationIsActive();

    return record;
}

QString collectAppletsData(const Latte::View *view)
{
    //! same invariant layer as collectViewRecord: the view comes from
    //! Synchronizer::viewForContainment, and a registered view always
    //! carries its extended interface
    Q_ASSERT(view);
    Q_ASSERT(view->extendedInterface());

    auto interface = view->extendedInterface();

    QList<AppletRecord> records;
    const QList<int> order = interface->appletsOrder();
    records.reserve(order.count());

    for (const int id : order) {
        const auto data = interface->appletDataForId(id);

        //! appletsOrder() also carries justify-splitter pseudo-ids that own
        //! no applet data; they are layout artifacts, not applets, so the
        //! report (schema: "per applet") skips them
        if (data.id < 0) {
            continue;
        }

        //! applet data entries always carry their quick item: the tracking
        //! block in onAppletAdded only runs with a non-null AppletQuickItem
        Q_ASSERT(data.applet && data.plasmoid);

        AppletRecord record;
        record.id = data.id;
        record.plugin = data.plugin;
        record.index = interface->indexOfApplet(data.id);
        //! "geometry within the view": the applet item mapped to scene
        //! coordinates, and the scene IS the view window
        record.geometry = data.plasmoid->mapRectToScene(QRectF(0, 0, data.plasmoid->width(), data.plasmoid->height())).toAlignedRect();
        record.isExpanded = interface->appletIsExpanded(data.id);
        //! Plasma::Applet::destroyed() is the scheduled-destruction bit the
        //! interface already forwards as appletInScheduledDestructionChanged
        record.inScheduledDestruction = data.applet->destroyed();
        record.lockedZoom = interface->appletsInLockedZoom().contains(data.id);
        record.colorizingBlocked = interface->appletsDisabledColoring().contains(data.id);

        records << record;
    }

    return serializeAppletRecords(records);
}

QString collectTrackerData(const Latte::View *view)
{
    //! the tracker ensemble is constructed with the view (View::init), so
    //! non-null members are invariants, not runtime input
    Q_ASSERT(view);
    Q_ASSERT(view->containment());
    Q_ASSERT(view->windowsTracker() && view->windowsTracker()->currentScreen());

    auto tracker = view->windowsTracker();
    auto currentScreen = tracker->currentScreen();

    TrackerRecord record;
    record.containmentId = view->containment()->id();
    record.enabled = tracker->enabled();
    record.activeWindowTouching = currentScreen->activeWindowTouching();
    record.activeWindowTouchingEdge = currentScreen->activeWindowTouchingEdge();
    record.activeWindowMaximized = currentScreen->activeWindowMaximized();
    record.existsWindowActive = currentScreen->existsWindowActive();
    record.existsWindowTouching = currentScreen->existsWindowTouching();
    record.existsWindowTouchingEdge = currentScreen->existsWindowTouchingEdge();
    record.existsWindowMaximized = currentScreen->existsWindowMaximized();

    //! a legitimately absent optional: the wm tracker hands out no
    //! LastActiveWindow while the view is untracked (tracking disabled or
    //! not yet registered), and the window it does hand out is invalid
    //! until a first activation lands - both read as "not present"
    auto lastWindow = currentScreen->lastActiveWindow();
    record.lastActiveWindowPresent = lastWindow && lastWindow->isValid();
    //! appName is application identity, deliberately not the window title
    //! (titles are other applications' content - the interface doc records
    //! the rule); an absent window reports an empty name
    record.lastActiveWindowAppName = record.lastActiveWindowPresent ? lastWindow->appName() : QString();

    return serializeTrackerData(record);
}

namespace {

//! a QML-side property read with drift detection: reading a name the
//! object no longer declares is a code defect (the QML was renamed under
//! the report), said loudly instead of decaying to a default silently
QVariant readLiveProperty(const QObject *object, const char *name)
{
    const QVariant value = object->property(name);

    if (!value.isValid()) {
        qWarning() << "dbusreports: object" << object << "no longer exposes property" << name
                   << "- the D-Bus report and the QML drifted apart";
    }

    return value;
}

//! the task-model roles viewTasksData() reads, resolved BY NAME from
//! roleNames() at collect time so no libtaskmanager header is compiled in;
//! the names are the AbstractTasksModel enum keys
struct TaskModelRoles {
    int appId{-1};
    int launcherUrl{-1};
    int isLauncher{-1};
    int isGroupParent{-1};
    int childCount{-1};
    int isActive{-1};
    int isMinimized{-1};
    int isDemandingAttention{-1};
    int geometry{-1};
};

std::optional<TaskModelRoles> resolveTaskModelRoles(const QAbstractItemModel *model)
{
    const QHash<int, QByteArray> names = model->roleNames();

    auto roleOf = [&names](const QByteArray &name) {
        return names.key(name, -1);
    };

    TaskModelRoles roles;
    roles.appId = roleOf(QByteArrayLiteral("AppId"));
    //! LauncherUrlWithoutIcon, not LauncherUrl: the latter encodes a
    //! fallback icon payload into the url, which is not identity
    roles.launcherUrl = roleOf(QByteArrayLiteral("LauncherUrlWithoutIcon"));
    roles.isLauncher = roleOf(QByteArrayLiteral("IsLauncher"));
    roles.isGroupParent = roleOf(QByteArrayLiteral("IsGroupParent"));
    roles.childCount = roleOf(QByteArrayLiteral("ChildCount"));
    roles.isActive = roleOf(QByteArrayLiteral("IsActive"));
    roles.isMinimized = roleOf(QByteArrayLiteral("IsMinimized"));
    roles.isDemandingAttention = roleOf(QByteArrayLiteral("IsDemandingAttention"));
    roles.geometry = roleOf(QByteArrayLiteral("Geometry"));

    for (const int role : {roles.appId, roles.launcherUrl, roles.isLauncher, roles.isGroupParent,
                           roles.childCount, roles.isActive, roles.isMinimized,
                           roles.isDemandingAttention, roles.geometry}) {
        if (role == -1) {
            //! role-name drift is a code defect (libtaskmanager renamed a
            //! role), so the whole applet is refused loudly rather than
            //! reported with plausible-but-wrong defaults
            qWarning() << "dbusreports: the tasks model no longer exposes every role viewTasksData reads;"
                       << "available roles:" << names.values();
            return std::nullopt;
        }
    }

    return roles;
}

//! the same BadgeMath answer the badge canvas draws, through the plasmoid
//! root's typed badgeValueFor(QString) - resolved by metaobject signature
//! exactly like the updateBadge D-Bus badge path
int collectBadgeValue(QQuickItem *plasmoidRoot, const QString &launcherUrl)
{
    const QMetaObject *meta = plasmoidRoot->metaObject();
    const int methodIndex = meta->indexOfMethod("badgeValueFor(QString)");

    if (methodIndex == -1) {
        qWarning() << "dbusreports: the tasks plasmoid root exposes no badgeValueFor(QString); task badges cannot be reported";
        return 0;
    }

    int value{0};

    if (!meta->method(methodIndex).invoke(plasmoidRoot, Q_RETURN_ARG(int, value), Q_ARG(QString, launcherUrl))) {
        qWarning() << "dbusreports: badgeValueFor invocation failed; task badge reported as 0";
        return 0;
    }

    return value;
}

void appendTaskRecordsOfPlasmoid(QList<Latte::DbusReports::TaskRecord> &records, int appletId, QQuickItem *plasmoidRoot)
{
    auto model = plasmoidRoot->property("tasksModel").value<QAbstractItemModel *>();

    if (!model) {
        //! drift detector: the root publishes tasksModel by contract
        //! (plasmoid main.qml documents the D-Bus consumer at the alias)
        qWarning() << "dbusreports: the tasks plasmoid root exposes no tasksModel; viewTasksData cannot read applet" << appletId;
        return;
    }

    const auto roles = resolveTaskModelRoles(model);

    if (!roles) {
        return;
    }

    const int rows = model->rowCount();
    records.reserve(records.count() + rows);

    for (int row = 0; row < rows; ++row) {
        const QModelIndex index = model->index(row, 0);

        Latte::DbusReports::TaskRecord record;
        record.appletId = appletId;
        record.index = row;
        record.appId = model->data(index, roles->appId).toString();
        record.launcherUrl = model->data(index, roles->launcherUrl).toUrl().toString();
        record.isLauncher = model->data(index, roles->isLauncher).toBool();
        record.isGrouped = model->data(index, roles->isGroupParent).toBool();
        record.childCount = model->data(index, roles->childCount).toInt();
        record.isActive = model->data(index, roles->isActive).toBool();
        record.isMinimized = model->data(index, roles->isMinimized).toBool();
        record.demandsAttention = model->data(index, roles->isDemandingAttention).toBool();
        record.badge = collectBadgeValue(plasmoidRoot, record.launcherUrl);
        //! the rect the delegate published via requestPublishDelegateGeometry
        //! (global coordinates - the WM-facing minimize-animation rect)
        record.geometry = model->data(index, roles->geometry).toRect();

        records << record;
    }
}

}

QString collectTasksData(const Latte::View *view)
{
    Q_ASSERT(view);
    Q_ASSERT(view->containment());

    QList<TaskRecord> records;

    const auto applets = view->containment()->applets();

    for (auto *applet : applets) {
        if (applet->pluginMetaData().pluginId() != QLatin1String("org.kde.latte.plasmoid")) {
            continue;
        }

        auto plasmoidRoot = PlasmaQuick::AppletQuickItem::itemForApplet(applet);

        if (!plasmoidRoot) {
            //! startup-transient: the applet exists before its quick item;
            //! said loudly so a poll that lands too early reads as "not
            //! ready yet" in the journal, not as a silently missing applet
            qWarning() << "dbusreports: tasks plasmoid" << applet->id() << "has no quick item yet; its tasks are not reported";
            continue;
        }

        appendTaskRecordsOfPlasmoid(records, static_cast<int>(applet->id()), plasmoidRoot);
    }

    //! a view without a latte tasks plasmoid legitimately reports "[]" -
    //! that IS the state (viewAppletsData's plugin list tells the two
    //! empty answers apart), so no warning here
    return serializeTaskRecords(records);
}

QString collectColorizerData(const Latte::View *view)
{
    Q_ASSERT(view);
    Q_ASSERT(view->containment());

    auto containmentRoot = PlasmaQuick::AppletQuickItem::itemForApplet(view->containment());

    if (!containmentRoot) {
        //! the containment's QML root is where themeColors/windowColors
        //! live; without it (startup-transient) there is nothing truthful
        //! to report yet
        qWarning() << "dbusreports: containment" << view->containment()->id()
                   << "has no quick item yet; colorizerData cannot be read";
        return QStringLiteral("{}");
    }

    ColorizerRecord record;
    record.containmentId = view->containment()->id();
    record.enabled = readLiveProperty(containmentRoot, "colorizerEnabled").toBool();

    //! the two mode ints come from the containment CONFIG (user-editable
    //! on disk), so out-of-range values are refused loudly here at the
    //! boundary and the record keeps its default mode name - the warning
    //! carries the actual bad value
    const int themeColorsValue = readLiveProperty(containmentRoot, "themeColors").toInt();
    const auto themeColors = themeColorsFromConfigValue(themeColorsValue);

    if (themeColors) {
        record.themeColors = *themeColors;
    } else {
        qWarning() << "dbusreports: containment" << record.containmentId
                   << "config carries unknown themeColors value" << themeColorsValue;
    }

    const int windowColorsValue = readLiveProperty(containmentRoot, "windowColors").toInt();
    const auto windowColors = windowColorsFromConfigValue(windowColorsValue);

    if (windowColors) {
        record.windowColors = *windowColors;
    } else {
        qWarning() << "dbusreports: containment" << record.containmentId
                   << "config carries unknown windowColors value" << windowColorsValue;
    }

    //! the colorizer Manager item binds to View::colorizer from
    //! BindingsExternal.qml after QML load - a legitimately absent
    //! optional; colorizerPresent reports it and the dependent fields
    //! keep record defaults
    auto colorizer = view->colorizer();
    record.colorizerPresent = (colorizer != nullptr);

    if (colorizer) {
        record.mustBeShown = readLiveProperty(colorizer, "mustBeShown").toBool();
        record.applyingWindowColors = readLiveProperty(colorizer, "applyingWindowColors").toBool();
        record.backgroundIsBusy = readLiveProperty(colorizer, "backgroundIsBusy").toBool();
        record.currentBackgroundBrightness = readLiveProperty(colorizer, "currentBackgroundBrightness").toDouble();
        //! basename per the interface doc; the decider's named kdeglobals
        //! fallback has no path and passes through unchanged
        record.scheme = QFileInfo(readLiveProperty(colorizer, "scheme").toString()).fileName();
    }

    return serializeColorizerData(record);
}

QString collectLayoutsData(Latte::Layouts::Manager *manager)
{
    //! the manager and its synchronizer live for the corona's whole life
    Q_ASSERT(manager);
    Q_ASSERT(manager->synchronizer());

    const QList<CentralLayout *> layouts = manager->synchronizer()->centralLayouts();

    QList<LayoutRecord> records;
    records.reserve(layouts.count());

    for (const auto layout : layouts) {
        LayoutRecord record;
        record.name = layout->name();
        record.isActive = layout->isActive();
        record.activities = layout->appliedActivities();
        record.viewsCount = layout->viewsCount();

        records << record;
    }

    return serializeLayoutsData(manager->memoryUsage(), records);
}

QString collectViewsData(const QList<Latte::View *> &views, bool inConfigureAppletsMode)
{
    QList<ViewRecord> records;
    records.reserve(views.count());

    for (const auto view : views) {
        records << collectViewRecord(view, inConfigureAppletsMode);
    }

    return serializeViewRecords(records);
}

}
}
