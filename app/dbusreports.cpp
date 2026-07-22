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
#include "screenpool.h"
#include "data/screendata.h"
#include "view/containmentinterface.h"
#include "view/effects.h"
#include "view/indicator/indicator.h"
#include "view/positioner.h"
#include "view/view.h"
#include "view/visibilitymanager.h"
#include "view/windowstracker/currentscreentracker.h"
#include "view/windowstracker/windowstracker.h"

// Qt
#include <QAbstractItemModel>
#include <QFileInfo>
#include <QMetaMethod>
#include <QQmlPropertyMap>
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

namespace {
//! forward declaration: collectAppletsData reads the AppletItem delegate's
//! live colorize properties before this helper is defined further down
QVariant readLiveProperty(const QObject *object, const char *name);
}

ViewRecord collectViewRecord(const Latte::View *view, bool globalConfigureAppletsMode)
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
    //! report -1 per docs/reference/dbus-observability-interface.md
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
    //! QML's effective rearrange mode is root.editMode AND the global toggle.
    //! Reporting the global bit on every view made unrelated docks appear to
    //! enter applet configuration together.
    record.inConfigureAppletsMode = effectiveConfigureAppletsMode(record.editMode,
                                                                  globalConfigureAppletsMode);
    record.keyboardNavigation = view->keyboardNavigationIsActive();

    return record;
}

namespace {

//! The applet's STACKING delegate: the AppletItem.qml wrapper whose z is the
//! applet's visual stacking order (the G2 readback). collectAppletsData holds
//! the inner PlasmaQuick::AppletQuickItem (data.plasmoid), but the
//! applet-reorder machinery lifts the OUTER AppletItem delegate - ConfigOverlay
//! sets the delegate to z=900 and reparents it to root during a drag, restoring
//! z=1 on release - so the stuck-over-chrome residue (480ae30e3 class) lives on
//! the delegate, not on the inner quick item whose own z never moves. The
//! delegate is the nearest ANCESTOR that declares isInternalViewSplitter. That
//! property is NOT unique to AppletItem.qml (ParabolicEdgeSpacer.qml declares it
//! too), but the spacer is a layout SIBLING of the applet items, never an
//! ANCESTOR of an applet's quick item, so the parent walk never meets it. Of the
//! actual ancestors - the wrapper items between the quick item and its
//! AppletItem, and the layout containers above - only the AppletItem declares
//! isInternalViewSplitter, so the first match up the chain is exactly it.
QQuickItem *appletStackingDelegate(QQuickItem *appletQuickItem)
{
    for (QQuickItem *item = appletQuickItem; item; item = item->parentItem()) {
        if (item->property("isInternalViewSplitter").isValid()) {
            return item;
        }
    }

    return nullptr;
}

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

        //! G2 stacking readback: the delegate's z, so an applet left lifted
        //! over the edit chrome (the reorder-residue class) is queryable. A
        //! missing delegate is a QML-restructure code defect, reported loudly
        //! rather than papered over with a plausible z=0.
        if (QQuickItem *delegate = appletStackingDelegate(data.plasmoid)) {
            record.z = delegate->z();
            //! D21: the effective colorize decision lives on the AppletItem
            //! delegate (it folds the manager decision, the user opt-out, and
            //! the inline-full state); read it here so a
            //! contrast test can assert per applet why it is or is not recoloured
            record.colorizerActive = readLiveProperty(delegate, "colorizerPaletteActive").toBool();
            record.colorizerReason = readLiveProperty(delegate, "colorizerExemptionReason").toString();
        } else {
            qWarning() << "dbusreports: applet" << data.id
                       << "quick item has no AppletItem delegate ancestor; its stacking z cannot be read";
        }

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

QString collectMiddleClickDispatchData(const Latte::View *view, uint containmentId)
{
    if (!view) {
        qWarning() << "dbusreports: taskMiddleClickDispatchData queried for containment"
                   << containmentId << "which has no view";
        return QStringLiteral("{}");
    }

    Q_ASSERT(view->containment());
    const uint actualContainmentId = view->containment()->id();
    if (actualContainmentId != containmentId) {
        qWarning() << "dbusreports: taskMiddleClickDispatchData resolved requested containment"
                   << containmentId << "to view containment" << actualContainmentId;
        return QStringLiteral("{}");
    }

    QList<MiddleClickDispatchCandidate> candidates;

    //! Plasma keeps an applet in containment()->applets() during its undo
    //! window. It remains queryable here until actual destruction removes it
    //! from this iteration; filtering scheduled destruction would hide live
    //! state while Plasma still owns the applet.
    for (auto *applet : view->containment()->applets()) {
        if (applet->pluginMetaData().pluginId() != QLatin1String("org.kde.latte.plasmoid")) {
            continue;
        }

        auto plasmoidRoot = PlasmaQuick::AppletQuickItem::itemForApplet(applet);
        if (!plasmoidRoot) {
            qWarning() << "dbusreports: tasks plasmoid" << applet->id()
                       << "has no quick item yet; taskMiddleClickDispatchData cannot be read";
            continue;
        }

        const QVariant value = readLiveProperty(plasmoidRoot, "latestMiddleClickDispatch");
        candidates.append(MiddleClickDispatchCandidate{
            actualContainmentId, static_cast<int>(applet->id()), value});
    }

    const auto selection = selectLatestMiddleClickDispatch(containmentId, candidates);
    switch (selection.refusal) {
    case MiddleClickDispatchRefusal::None:
        return serializeMiddleClickDispatchData(selection.record);
    case MiddleClickDispatchRefusal::ContainmentMismatch:
        qWarning() << "dbusreports: tasks plasmoid" << selection.appletId
                   << "belongs to containment" << selection.candidateContainmentId
                   << "outside requested containment" << containmentId;
        break;
    case MiddleClickDispatchRefusal::MalformedState:
        qWarning() << "dbusreports: tasks plasmoid" << selection.appletId
                   << "exposes malformed or unknown latestMiddleClickDispatch state; refusing aggregate";
        break;
    case MiddleClickDispatchRefusal::DuplicateSequence:
        qWarning() << "dbusreports: tasks plasmoids expose duplicate middle-click dispatch sequence"
                   << selection.duplicateSequence << "; refusing aggregate";
        break;
    }

    return QStringLiteral("{}");
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

        //! D21: the decided colours the applets are pushed to. applyColor is
        //! Manager.applyColor (its foreground, equal to textColor); its
        //! brightness is Manager.themeTextColorBrightness (0..255, computed the
        //! same way the decision core measures).
        record.applyColor = readLiveProperty(colorizer, "applyColor").value<QColor>();
        record.textColor = readLiveProperty(colorizer, "textColor").value<QColor>();
        record.backgroundColor = readLiveProperty(colorizer, "backgroundColor").value<QColor>();
        record.applyColorBrightness = readLiveProperty(colorizer, "themeTextColorBrightness").toDouble();
        record.backgroundColorBrightness = readLiveProperty(colorizer, "backgroundColorBrightness").toDouble();
    }

    return serializeColorizerData(record);
}

namespace {

//! read a Plasma applet's (or containment's) live config property map into a
//! value snapshot. The map is Applet::configuration() - a KConfigPropertyMap
//! (a QQmlPropertyMap) - the SAME object the settings pages write through
//! plasmoid.configuration.<key>, so the snapshot reflects unsaved in-process
//! writes AND carries every schema key with its current value. Reading the
//! in-process map (never the on-disk file) is the fix for the KConfig
//! default-deletion trap the audit's snapshot-diff would otherwise hit: the
//! file drops a key whose value returned to its default, but the map keeps it,
//! so a key at its default reads as its default value here, not as "missing".
QQmlPropertyMap *configurationMapFor(const QObject *configOwner)
{
    auto map = qobject_cast<QQmlPropertyMap *>(configOwner->property("configuration").value<QObject *>());

    if (!map) {
        //! a live applet always exposes its configuration map; a null one is a
        //! code/porting defect (the Plasma property name drifted), said loudly
        //! rather than reported as a silently empty config
        qWarning() << "dbusreports: object" << configOwner
                   << "exposes no configuration property map; its config values cannot be read";
        return nullptr;
    }

    return map;
}

QVariantMap readConfigMap(const QObject *configOwner)
{
    auto map = configurationMapFor(configOwner);

    if (!map) {
        return {};
    }

    QVariantMap values;
    const QStringList keys = map->keys();

    for (const QString &key : keys) {
        values.insert(key, map->value(key));
    }

    return values;
}

}

ViewLiveRecord collectViewLiveRecord(const Latte::View *view)
{
    //! visibility() is constructed with the view (View::init), same invariant
    //! layer as collectViewRecord; indicator() binds later and is checked below
    Q_ASSERT(view);
    Q_ASSERT(view->visibility());

    ViewLiveRecord record;
    record.byPassWM = view->byPassWM();
    record.isPreferredForShortcuts = view->isPreferredForShortcuts();

    auto visibility = view->visibility();
    record.visibilityTimerShow = visibility->timerShow();
    record.visibilityTimerHide = visibility->timerHide();
    record.visibilityEnableKWinEdges = visibility->enableKWinEdges();
    record.visibilityRaiseOnDesktop = visibility->raiseOnDesktop();
    record.visibilityRaiseOnActivity = visibility->raiseOnActivity();

    //! the indicator is a legitimately absent optional until BindingsExternal
    //! wires it after QML load (like the colorizer): report its presence and
    //! keep the record defaults when it is not up yet
    auto indicator = view->indicator();
    record.indicatorPresent = (indicator != nullptr);

    if (indicator) {
        record.indicatorEnabled = indicator->enabled();
        record.indicatorType = indicator->type();
        record.indicatorCustomType = indicator->customType();
    }

    return record;
}

QString collectConfigData(const Latte::View *view, const EditSettingsState &editSettings)
{
    Q_ASSERT(view);
    Q_ASSERT(view->containment());

    //! the containment IS a Plasma::Applet, so its config values are the same
    //! plasmoid.configuration.<key> map the settings pages write; read it
    //! in-process (readConfigMap's default-deletion note)
    const QVariantMap config = readConfigMap(view->containment());

    //! the advanced-mode toggle and drag-corner scales have no containment config
    //! key: they live on UniversalSettings and the Corona resolves them, so merge
    //! them onto the per-view live record here (CL-6, edit-mode-settings-audit-plan)
    ViewLiveRecord live = collectViewLiveRecord(view);
    live.inAdvancedModeForEditSettings = editSettings.inAdvancedMode;
    live.settingsWindowScaleWidth = editSettings.settingsWindowScaleWidth;
    live.settingsWindowScaleHeight = editSettings.settingsWindowScaleHeight;

    return serializeConfigData(view->containment()->id(), config, live);
}

QString collectAppletConfigData(const Latte::View *view, int appletId)
{
    Q_ASSERT(view);
    Q_ASSERT(view->containment());

    const auto applets = view->containment()->applets();

    Plasma::Applet *target = nullptr;

    for (auto *applet : applets) {
        if (static_cast<int>(applet->id()) == appletId) {
            target = applet;
            break;
        }
    }

    if (!target) {
        //! appletId arrives from the D-Bus caller (outside input): an id that
        //! names no applet on this view is refused loudly here, never a silent
        //! empty answer a consumer could mistake for "the applet has no config"
        qWarning() << "dbusreports: appletConfigData found no applet" << appletId
                   << "on containment" << view->containment()->id();
        return QStringLiteral("{}");
    }

    //! the tasks plasmoid (the D10-class target) is a plain applet, so its own
    //! configuration map is the tasks.plasmoid.configuration.<key> surface the
    //! Tasks page writes; a subcontainment applet would keep its config on the
    //! subcontainment, but the audit only reads plain applets here
    const QVariantMap config = readConfigMap(target);

    return serializeAppletConfigData(view->containment()->id(), appletId,
                                     target->pluginMetaData().pluginId(), config);
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

QString collectViewsData(const QList<Latte::View *> &views, bool globalConfigureAppletsMode)
{
    QList<ViewRecord> records;
    records.reserve(views.count());

    for (const auto view : views) {
        records << collectViewRecord(view, globalConfigureAppletsMode);
    }

    return serializeViewRecords(records);
}

std::optional<DockSystemSnapshot> collectDockSystemSnapshot(
    const QList<Latte::View *> &views,
    bool globalConfigureAppletsMode,
    quint64 snapshotSequence,
    RuntimeObjectIdentityRegistry *identities)
{
    Q_ASSERT(identities);

    DockSystemSnapshot snapshot;
    snapshot.snapshotSequence = snapshotSequence;
    snapshot.globalConfigureAppletsMode = globalConfigureAppletsMode;
    snapshot.views.reserve(views.count());

    //! Synchronizer stores current views in a QHash-derived container. Resolve
    //! the persistent containment order before the first registry lookup, so
    //! runtime view ids and shared-controller tokens do not depend on hash
    //! iteration order.
    QList<DockCollectionOrderInput> collectionOrder;
    QList<DockLineageInput> lineages;
    collectionOrder.reserve(views.size());
    lineages.reserve(views.size());
    for (qsizetype sourceIndex = 0; sourceIndex < views.size(); ++sourceIndex) {
        const auto *view = views.at(sourceIndex);
        if (!view || !view->containment()) {
            qCritical() << "dbusreports: refusing dock-system snapshot with a null view or containment";
            Q_ASSERT(view && view->containment());
            return std::nullopt;
        }
        const uint persistentDockId = view->containment()->id();
        collectionOrder.append(DockCollectionOrderInput{persistentDockId, sourceIndex});
        lineages.append(DockLineageInput{
            persistentDockId,
            view->groupId(),
            view->isOriginal(),
            view->isCloned(),
            view->isSingle(),
            view->linkPlacement()});
    }

    const auto relationships = classifyDockRelationshipGraph(lineages);
    if (!relationships) {
        qCritical() << "dbusreports: refusing dock-system snapshot with malformed dock lineage"
                    << lineages.size() << "records";
        for (const auto &lineage : lineages) {
            qCritical() << "dbusreports: lineage containment" << lineage.persistentDockId
                        << "group" << lineage.groupId << "original" << lineage.isOriginal
                        << "clone" << lineage.isCloned << "single" << lineage.isSingle
                        << "link placement" << static_cast<int>(lineage.linkPlacement);
        }
        Q_ASSERT(relationships.has_value());
        return std::nullopt;
    }
    const QList<qsizetype> orderedSourceIndexes =
        orderDockCollectionByPersistentId(collectionOrder);

    //! Screen-group-derived members report SingleScreenGroup locally. Resolve
    //! their policy from the root that owns it. Explicitly placed linked
    //! members retain their own local SingleScreenGroup policy.
    QHash<uint, Types::ScreensGroup> rootScreensGroups;
    for (const qsizetype sourceIndex : orderedSourceIndexes) {
        const auto *view = views.at(sourceIndex);
        const auto relationship = relationships->value(view->containment()->id());
        if (relationship.relationship == DockRelationship::LinkedRoot) {
            rootScreensGroups.insert(view->containment()->id(), view->screensGroup());
        }
    }

    for (const qsizetype sourceIndex : orderedSourceIndexes) {
        const auto *view = views.at(sourceIndex);
        Q_ASSERT(view->positioner() && view->effects() && view->visibility()
                 && view->extendedInterface());

        DockSystemViewRecord record;
        record.persistentDockId = view->containment()->id();
        const auto relationship = relationships->value(record.persistentDockId);
        record.logicalDockId = relationship.logicalDockId;
        record.originalDockId = relationship.originalDockId;
        record.relationship = relationship.relationship;
        record.linkPlacement = relationship.linkPlacement;

        const bool derivesScreenGroup = record.linkPlacement
                == Data::View::LinkPlacement::ScreenGroupDerived;
        if (record.relationship == DockRelationship::LinkedMember && derivesScreenGroup) {
            const auto group = rootScreensGroups.constFind(record.logicalDockId);
            if (group == rootScreensGroups.constEnd()) {
                qCritical() << "dbusreports: validated linked member" << record.persistentDockId
                            << "lost root screen-group authority" << record.logicalDockId;
                Q_ASSERT(group != rootScreensGroups.constEnd());
                return std::nullopt;
            }
            record.screensGroup = group.value();
        } else if (record.relationship == DockRelationship::Independent
                   || record.relationship == DockRelationship::LinkedMember) {
            record.screensGroup = Types::SingleScreenGroup;
        } else {
            record.screensGroup = view->screensGroup();
        }

        record.runtimeViewId = identities->idFor(view);

        record.layout = view->layout() ? view->layout()->name() : QString();
        record.screenId = view->positioner()->currentScreenId();
        record.screen = view->positioner()->currentScreenName();
        record.onPrimary = view->onPrimary();
        record.type = view->type();
        record.edge = view->location();
        record.orientation = view->formFactor();
        record.alignment = static_cast<Types::Alignment>(view->alignment());
        record.maximumLengthRatio = view->maxLength();
        record.offsetRatio = view->offset();

        const auto *const configuration = configurationMapFor(view->containment());
        if (configuration) {
            const QVariant configuredIconSize = configuration->value(QStringLiteral("iconSize"));
            if (configuredIconSize.isValid()) {
                record.configuredIconSize = configuredIconSize.toInt();
            } else {
                qWarning() << "dbusreports: containment" << record.persistentDockId
                           << "configuration exposes no iconSize";
            }
        }

        if (const auto *metrics = view->metrics()) {
            const QVariant effectiveIconSize = readLiveProperty(metrics, "iconSize");
            if (effectiveIconSize.isValid()) {
                record.effectiveIconSize = effectiveIconSize.toInt();
            }
        }

        const auto *const editController = view->rootObject();
        if (editController) {
            const QVariant availableLength = readLiveProperty(editController, "maxLength");
            if (availableLength.isValid()) {
                record.availablePrimaryLength = availableLength.toInt();
            }
        }

        record.normalThickness = view->normalThickness();
        record.maximumNormalThickness = view->maxNormalThickness();
        record.windowGeometry = view->geometry();
        record.absoluteGeometry = view->absoluteGeometry();
        record.localGeometry = view->localGeometry();
        record.screenGeometry = view->screenGeometry();
        record.canvasGeometry = view->positioner()->canvasGeometry();
        record.effectsRect = view->effects()->rect();
        record.appletsLayoutGeometry = view->effects()->appletsLayoutGeometry();
        record.maskRect = view->effects()->mask();
        record.inputMask = view->effects()->inputMask();
        record.appliedInputMask = view->effects()->appliedInputMask();
        record.strutsThickness = view->visibility()->strutsThickness();
        record.publishedStruts = view->visibility()->publishedStruts();

        record.visibilityMode = view->visibility()->mode();
        record.isHidden = view->visibility()->isHidden();
        record.inStartup = view->positioner()->inStartup();
        record.isOffScreen = view->positioner()->isOffScreen();
        record.inRelocationAnimation = view->positioner()->inRelocationAnimation();
        record.inDelete = view->inDelete();
        record.inReadyState = view->inReadyState();
        record.editMode = view->inEditMode();
        record.settingsWindowShown = view->settingsWindowIsShown();

        record.objects.view = identities->tokenFor(view);
        record.objects.containment = identities->tokenFor(view->containment());
        record.objects.configuration = identities->tokenFor(configuration);
        record.objects.layout = identities->tokenFor(view->layout());
        record.objects.layoutController = identities->tokenFor(view->extendedInterface()->layoutManager());
        record.objects.geometryController = identities->tokenFor(view->positioner());
        record.objects.editController = identities->tokenFor(editController);
        record.objects.configWindow = identities->tokenFor(view->configView());

        snapshot.views.append(record);
    }

    return snapshot;
}

QString collectDockSystemData(const QList<Latte::View *> &views,
                              bool globalConfigureAppletsMode,
                              quint64 snapshotSequence,
                              RuntimeObjectIdentityRegistry *identities)
{
    const auto snapshot = collectDockSystemSnapshot(views,
                                                     globalConfigureAppletsMode,
                                                     snapshotSequence,
                                                     identities);
    return snapshot ? serializeDockSystemSnapshot(*snapshot) : QString();
}

QString collectScreensData(Latte::ScreenPool *pool)
{
    //! the screen pool lives for the corona's whole life (constructed in the
    //! Corona ctor); a null pool would be a code defect, not runtime input
    Q_ASSERT(pool);

    //! primaryScreenId() dereferences the primary QScreen; a running corona
    //! always has one (the same assumption genericlayout's placement path
    //! makes). screensData is a post-settle readback, never called before the
    //! first primary output exists.
    const int primaryId = pool->primaryScreenId();

    Latte::Data::ScreensTable table = pool->screensTable();

    QList<ScreenRecord> records;
    records.reserve(table.rowCount());

    for (int i = 0; i < table.rowCount(); ++i) {
        const Latte::Data::Screen screen = table[static_cast<uint>(i)];

        ScreenRecord record;
        record.id = screen.id.toInt();
        record.name = screen.name;
        record.geometry = screen.geometry;
        //! isActive = the connector is currently connected (a live QScreen
        //! carries this name); a pool entry can outlive its output (a
        //! remembered-but-unplugged monitor), so this is a real per-read fact,
        //! not a stored flag
        record.isActive = pool->isScreenActive(record.id);
        record.isPrimary = (record.id == primaryId);

        records << record;
    }

    return serializeScreensData(records);
}

}
}
