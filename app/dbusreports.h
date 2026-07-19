/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef DBUSREPORTS_H
#define DBUSREPORTS_H

// local
#include <coretypes.h>
#include "apptypes.h"
//! the containment plugin's enum home, included relatively: the theme and
//! window color modes colorizerData() names live there (a header-only
//! Q_GADGET, no moc linkage), and naming the REAL enums keeps -Wswitch
//! honest here like every other enum-name mapping in this file
#include "../containment/plugin/types.h"

// Qt
#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QRect>
#include <QString>
#include <QVariant>
#include <QVariantMap>

// C++
#include <array>
#include <optional>

// Plasma
#include <Plasma/Plasma>

namespace Latte {
class View;
class ScreenPool;
namespace Layouts {
class Manager;
}
}

namespace Latte {
namespace DbusReports {

//! The org.kde.LatteDock JSON read surface
//! (docs/reference/dbus-observability-interface.md). Two layers, so serialization
//! stays a pure core: ViewRecord is a value snapshot of every field
//! viewsData() reports and the inline name/serialize functions below are
//! pure (unit-tested without a corona in tests/units/dbusreportstest.cpp);
//! only the collect* functions in dbusreports.cpp read live View objects.

//! One view's windows-tracker facts as trackerData() reports them
//! (docs/reference/dbus-observability-interface.md, step 3)
struct TrackerRecord {
    uint containmentId{0};
    bool enabled{false};
    bool activeWindowTouching{false};
    bool activeWindowTouchingEdge{false};
    bool activeWindowMaximized{false};
    bool existsWindowActive{false};
    bool existsWindowTouching{false};
    bool existsWindowTouchingEdge{false};
    bool existsWindowMaximized{false};
    bool lastActiveWindowPresent{false};
    QString lastActiveWindowAppName;
};

//! One loaded layout as layoutsData() reports it
//! (docs/reference/dbus-observability-interface.md, step 4)
struct LayoutRecord {
    QString name;
    bool isActive{false};
    QStringList activities;
    int viewsCount{0};
};

//! One output as ScreenPool knows it, as screensData() reports it
//! (docs/reference/dbus-observability-interface.md). This is the pull-queryable
//! screen<->output mapping the multi-output e2e vehicle (C-I2/P1) needs:
//! which Latte screen id resolves to which connector, its geometry, whether
//! the connector is currently connected (isActive), and which id is the
//! primary. Before it existed the mapping was only reachable by scraping
//! ScreenPool's qDebug, which the observability-first rule forbids.
struct ScreenRecord {
    int id{-1};
    QString name;
    QRect geometry;
    bool isActive{false};
    bool isPrimary{false};
};

//! One view's colorizer decision facts as colorizerData() reports them
//! (docs/reference/dbus-observability-interface.md, step 4). The doc's prose mapped
//! onto the state that exists: "mode" is the containment's
//! themeColors/windowColors settings, the decision in force is
//! mustBeShown/applyingWindowColors off the colorizer Manager item, and
//! the measured bucket is backgroundIsBusy plus
//! currentBackgroundBrightness (-1000 is the Manager's own "unmeasured"
//! sentinel). Per-applet colorfulness exemption is viewAppletsData's
//! colorizingBlocked, not a per-view fact.
struct ColorizerRecord {
    uint containmentId{0};
    bool enabled{false};
    Containment::Types::ThemeColorsGroup themeColors{Containment::Types::PlasmaThemeColors};
    Containment::Types::WindowColorsGroup windowColors{Containment::Types::NoneWindowColors};
    bool colorizerPresent{false};
    bool mustBeShown{false};
    bool applyingWindowColors{false};
    bool backgroundIsBusy{false};
    double currentBackgroundBrightness{-1000};
    QString scheme;
    //! D21: the decided colours the colorizer resolves. applyColor is the
    //! foreground the applets are pushed to (Manager.applyColor, which equals
    //! its textColor); backgroundColor is the scheme background the contrast is
    //! measured against. The brightnesses are the Manager's own values (0..255),
    //! so a D21 regression test can assert foreground/background contrast at the
    //! STATE level, complementing the rendered-pixel check.
    QColor applyColor;
    QColor textColor;
    QColor backgroundColor;
    double applyColorBrightness{-1};
    double backgroundColorBrightness{-1};
};

//! One task item of a view's tasks plasmoid as viewTasksData() reports it
//! (docs/reference/dbus-observability-interface.md, step 4). Identity is
//! appId/launcherUrl only - window titles are other applications'
//! content and stay out by design (the interface doc records the rule).
//!
//! G4 (docs/tracking/e2e-interaction-test-plan.md section 9): index + appId ARE the
//! window-task order readback. index is the tasks model ROW (records are
//! emitted in row order, so the array position and index agree), and it moves
//! when a reorder calls tasksModel.move; appId is the stable per-window
//! identity that travels WITH its window across the reorder. Tracking a window
//! by appId and reading its index before/after a drag is how the F4/A3
//! window-task reorder scenarios assert the order changed (launchers, whose
//! order also persists to the launchers config key, use launcherUrl the same
//! way). No new field is needed for G4 - the pair was already reported; this
//! note records that the pair is the order contract those scenarios rely on.
struct TaskRecord {
    int appletId{-1};
    int index{-1};
    QString appId;
    QString launcherUrl;
    bool isLauncher{false};
    bool isGrouped{false};
    int childCount{0};
    bool isActive{false};
    bool isMinimized{false};
    bool demandsAttention{false};
    int badge{0};
    QRect geometry;
};

//! One applet of a view as viewAppletsData() reports it
//! (docs/reference/dbus-observability-interface.md, step 2)
struct AppletRecord {
    int id{-1};
    QString plugin;
    int index{-1};
    QRect geometry;
    bool isExpanded{false};
    bool inScheduledDestruction{false};
    bool lockedZoom{false};
    bool colorizingBlocked{false};
    //! D21: whether the colorizer's scheme is EFFECTIVELY pushed into this
    //! applet's own colour group (AppletItem.colorizerPaletteActive), and the
    //! single reason it is not when it is not ("applied" when it is). Unlike
    //! colorizingBlocked - which reflects only the user opt-out list - this is
    //! the whole decision: applied / notEngaged / splitter / selfColored /
    //! userBlocked / inlineFull / colorful.
    bool colorizerActive{false};
    QString colorizerReason;
    //! the G2 stacking readback (docs/tracking/e2e-interaction-test-plan.md): the z of
    //! the applet's AppletItem delegate. At rest every applet sits at the
    //! layout default (0); the applet-reorder machinery lifts the dragged
    //! delegate to z=900 over the edit chrome and restores it on release, so a
    //! reorder left stranded shows here as a delegate stuck above its
    //! siblings - the 480ae30e3 "icons stuck over chrome" residue made
    //! queryable instead of golden-only. Real (qreal z), not int.
    double z{0};
};

struct ViewRecord {
    uint containmentId{0};
    QString layout;
    bool isCloned{false};
    int isClonedFrom{-1};
    Types::ViewType type{Types::DockView};
    QString screen;
    bool onPrimary{false};
    Plasma::Types::Location edge{Plasma::Types::Floating};
    Types::Alignment alignment{Types::NoneAlignment};
    Types::Visibility visibilityMode{Types::None};
    bool isHidden{false};
    bool inStartup{false};
    bool isOffScreen{false};
    QRect absoluteGeometry;
    QRect localGeometry;
    QRect screenGeometry;
    int strutsThickness{0};
    QRect publishedStruts;
    QRect maskRect;
    QRect inputMask;
    QRect appliedInputMask;
    bool editMode{false};
    bool inConfigureAppletsMode{false};
    bool keyboardNavigation{false};
};

//! The live C++-property half of viewConfigData() (the "view" object):
//! settings-panel controls that write a View/VisibilityManager/Indicator
//! property instead of a containment config key, so the edit-mode audit's
//! P3 leg (a control reflects the dock's current state) has a readback for
//! them (docs/reference/dbus-observability-interface.md, docs/tracking/edit-mode-settings-audit-plan.md
//! section 3). Config-backed controls read from viewConfigData()'s "config"
//! object instead; these are the ones whose live value lives only in C++.
struct ViewLiveRecord {
    bool byPassWM{false};
    bool isPreferredForShortcuts{false};
    int visibilityTimerShow{0};
    int visibilityTimerHide{0};
    bool visibilityEnableKWinEdges{false};
    bool visibilityRaiseOnDesktop{false};
    bool visibilityRaiseOnActivity{false};
    //! the indicator subsystem binds after QML load, so it is a legitimately
    //! absent optional (like the colorizer): indicatorPresent reports it and
    //! the dependent fields keep their record defaults when it is not up yet
    bool indicatorPresent{false};
    bool indicatorEnabled{false};
    QString indicatorType;
    QString indicatorCustomType;
    //! universalSettings edit-settings state the CL-6 chrome audit reads for its
    //! P3 leg: the settings window's advanced-mode toggle (control 7) and the
    //! drag-corner's per-screen settings-window scales (control 8). These live on
    //! UniversalSettings (global / per-screen), not on the view or its containment
    //! config, so a config-map read cannot see them; the Corona (which owns
    //! UniversalSettings) resolves them and merges them onto this record in
    //! collectConfigData, the same way viewsData threads inConfigureAppletsMode.
    bool inAdvancedModeForEditSettings{false};
    double settingsWindowScaleWidth{1.0};
    double settingsWindowScaleHeight{1.0};
};

//! Enum-to-string names for the JSON fields. Exhaustive switches with no
//! default, so adding an enum value trips -Wswitch right here instead of
//! silently serializing something stale; Q_UNREACHABLE() documents that
//! every value returns above.

inline QString viewTypeName(Types::ViewType type)
{
    switch (type) {
    case Types::DockView: return QStringLiteral("dock");
    case Types::PanelView: return QStringLiteral("panel");
    }

    Q_UNREACHABLE();
}

inline QString edgeName(Plasma::Types::Location location)
{
    switch (location) {
    case Plasma::Types::Floating: return QStringLiteral("floating");
    case Plasma::Types::Desktop: return QStringLiteral("desktop");
    case Plasma::Types::FullScreen: return QStringLiteral("fullscreen");
    case Plasma::Types::TopEdge: return QStringLiteral("top");
    case Plasma::Types::BottomEdge: return QStringLiteral("bottom");
    case Plasma::Types::LeftEdge: return QStringLiteral("left");
    case Plasma::Types::RightEdge: return QStringLiteral("right");
    }

    Q_UNREACHABLE();
}

inline QString alignmentName(Types::Alignment alignment)
{
    switch (alignment) {
    case Types::NoneAlignment: return QStringLiteral("none");
    case Types::Center: return QStringLiteral("center");
    case Types::Left: return QStringLiteral("left");
    case Types::Right: return QStringLiteral("right");
    case Types::Top: return QStringLiteral("top");
    case Types::Bottom: return QStringLiteral("bottom");
    case Types::Justify: return QStringLiteral("justify");
    }

    Q_UNREACHABLE();
}

inline QString visibilityModeName(Types::Visibility mode)
{
    switch (mode) {
    case Types::None: return QStringLiteral("none");
    case Types::AlwaysVisible: return QStringLiteral("alwaysVisible");
    case Types::AutoHide: return QStringLiteral("autoHide");
    case Types::DodgeActive: return QStringLiteral("dodgeActive");
    case Types::DodgeMaximized: return QStringLiteral("dodgeMaximized");
    case Types::DodgeAllWindows: return QStringLiteral("dodgeAllWindows");
    case Types::WindowsGoBelow: return QStringLiteral("windowsGoBelow");
    case Types::WindowsCanCover: return QStringLiteral("windowsCanCover");
    case Types::WindowsAlwaysCover: return QStringLiteral("windowsAlwaysCover");
    case Types::SidebarOnDemand: return QStringLiteral("sidebarOnDemand");
    case Types::SidebarAutoHide: return QStringLiteral("sidebarAutoHide");
    case Types::NormalWindow: return QStringLiteral("normalWindow");
    }

    Q_UNREACHABLE();
}

inline QString memoryUsageName(MemoryUsage::LayoutsMemory memory)
{
    switch (memory) {
    //! Current is the query sentinel some APIs take ("whatever is in
    //! force"); Layouts::Manager::memoryUsage() never returns it, but the
    //! mapping stays exhaustive so -Wswitch keeps meaning something
    case MemoryUsage::Current: return QStringLiteral("current");
    case MemoryUsage::SingleLayout: return QStringLiteral("single");
    case MemoryUsage::MultipleLayouts: return QStringLiteral("multiple");
    }

    Q_UNREACHABLE();
}

inline QString themeColorsModeName(Containment::Types::ThemeColorsGroup mode)
{
    switch (mode) {
    case Containment::Types::PlasmaThemeColors: return QStringLiteral("plasma");
    case Containment::Types::ReverseThemeColors: return QStringLiteral("reverse");
    case Containment::Types::SmartThemeColors: return QStringLiteral("smart");
    case Containment::Types::DarkThemeColors: return QStringLiteral("dark");
    case Containment::Types::LightThemeColors: return QStringLiteral("light");
    case Containment::Types::LayoutThemeColors: return QStringLiteral("layout");
    }

    Q_UNREACHABLE();
}

inline QString windowColorsModeName(Containment::Types::WindowColorsGroup mode)
{
    switch (mode) {
    case Containment::Types::NoneWindowColors: return QStringLiteral("none");
    case Containment::Types::ActiveWindowColors: return QStringLiteral("active");
    case Containment::Types::TouchingWindowColors: return QStringLiteral("touching");
    }

    Q_UNREACHABLE();
}

//! Config-value validation for the two color-mode ints: they arrive from
//! the containment CONFIG (user-editable on disk), so an out-of-range int
//! is outside input to refuse at the boundary, never to cast blindly into
//! a switch whose fallthrough is Q_UNREACHABLE.

inline std::optional<Containment::Types::ThemeColorsGroup> themeColorsFromConfigValue(int value)
{
    constexpr std::array modes{Containment::Types::PlasmaThemeColors, Containment::Types::ReverseThemeColors,
                               Containment::Types::SmartThemeColors, Containment::Types::DarkThemeColors,
                               Containment::Types::LightThemeColors, Containment::Types::LayoutThemeColors};

    for (const auto mode : modes) {
        if (static_cast<int>(mode) == value) {
            return mode;
        }
    }

    return std::nullopt;
}

inline std::optional<Containment::Types::WindowColorsGroup> windowColorsFromConfigValue(int value)
{
    constexpr std::array modes{Containment::Types::NoneWindowColors, Containment::Types::ActiveWindowColors,
                               Containment::Types::TouchingWindowColors};

    for (const auto mode : modes) {
        if (static_cast<int>(mode) == value) {
            return mode;
        }
    }

    return std::nullopt;
}

//! the inverse of visibilityModeName, implemented by searching that
//! function's own output so the two directions can never drift apart;
//! nullopt for a name no mode serializes to
inline std::optional<Types::Visibility> visibilityModeFromName(const QString &name)
{
    constexpr std::array modes{Types::None, Types::AlwaysVisible, Types::AutoHide,
                               Types::DodgeActive, Types::DodgeMaximized, Types::DodgeAllWindows,
                               Types::WindowsGoBelow, Types::WindowsCanCover, Types::WindowsAlwaysCover,
                               Types::SidebarOnDemand, Types::SidebarAutoHide, Types::NormalWindow};

    for (const auto mode : modes) {
        if (visibilityModeName(mode) == name) {
            return mode;
        }
    }

    return std::nullopt;
}

//! the parse the setViewVisibilityMode D-Bus boundary uses: "none" is the
//! serializer's name for the unset state, not a mode a user can set
//! (VisibilityManager::setMode asserts against Types::None), so it is
//! refused here alongside unknown names
inline std::optional<Types::Visibility> settableVisibilityModeFromName(const QString &name)
{
    const auto mode = visibilityModeFromName(name);

    if (mode && *mode == Types::None) {
        return std::nullopt;
    }

    return mode;
}

inline QJsonArray serializeRect(const QRect &rect)
{
    return QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()};
}

inline QJsonObject serializeTrackerRecord(const TrackerRecord &record)
{
    QJsonObject json;
    json[QStringLiteral("containmentId")] = static_cast<qint64>(record.containmentId);
    json[QStringLiteral("enabled")] = record.enabled;
    json[QStringLiteral("activeWindowTouching")] = record.activeWindowTouching;
    json[QStringLiteral("activeWindowTouchingEdge")] = record.activeWindowTouchingEdge;
    json[QStringLiteral("activeWindowMaximized")] = record.activeWindowMaximized;
    json[QStringLiteral("existsWindowActive")] = record.existsWindowActive;
    json[QStringLiteral("existsWindowTouching")] = record.existsWindowTouching;
    json[QStringLiteral("existsWindowTouchingEdge")] = record.existsWindowTouchingEdge;
    json[QStringLiteral("existsWindowMaximized")] = record.existsWindowMaximized;
    json[QStringLiteral("lastActiveWindowPresent")] = record.lastActiveWindowPresent;
    json[QStringLiteral("lastActiveWindowAppName")] = record.lastActiveWindowAppName;

    return json;
}

inline QString serializeTrackerData(const TrackerRecord &record)
{
    return QString::fromUtf8(QJsonDocument(serializeTrackerRecord(record)).toJson(QJsonDocument::Compact));
}

inline QJsonObject serializeAppletRecord(const AppletRecord &record)
{
    QJsonObject json;
    json[QStringLiteral("id")] = record.id;
    json[QStringLiteral("plugin")] = record.plugin;
    json[QStringLiteral("index")] = record.index;
    json[QStringLiteral("geometry")] = serializeRect(record.geometry);
    json[QStringLiteral("isExpanded")] = record.isExpanded;
    json[QStringLiteral("inScheduledDestruction")] = record.inScheduledDestruction;
    json[QStringLiteral("lockedZoom")] = record.lockedZoom;
    json[QStringLiteral("colorizingBlocked")] = record.colorizingBlocked;
    json[QStringLiteral("colorizerActive")] = record.colorizerActive;
    json[QStringLiteral("colorizerReason")] = record.colorizerReason;
    json[QStringLiteral("z")] = record.z;

    return json;
}

inline QString serializeAppletRecords(const QList<AppletRecord> &records)
{
    QJsonArray array;

    for (const auto &record : records) {
        array.append(serializeAppletRecord(record));
    }

    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

//! The stable applet-instance-id order the viewAppletsOrder() D-Bus read
//! reports: the raw ContainmentInterface::appletsOrder() with the justify
//! layout's splitter markers removed. A justify view threads two
//! JUSTIFYSPLITTERID (-10) sentinels into that list to mark its three zone
//! boundaries; they are layout artifacts that own no applet, and
//! collectAppletsData() already excludes them (it skips data.id < 0), so
//! the id-order readback draws the same line and keeps only real Plasma
//! applet ids in visual order. Two applets of the SAME plugin still carry
//! distinct instance ids, so this order disambiguates them where the plugin
//! string cannot - the G1 readback in docs/tracking/e2e-interaction-test-plan.md.
inline QList<int> appletIdOrder(const QList<int> &appletsOrder)
{
    QList<int> ids;
    ids.reserve(appletsOrder.count());

    for (const int id : appletsOrder) {
        //! real applet ids are non-negative; the only negative entries are
        //! the justify-splitter sentinels (LayoutManager::JUSTIFYSPLITTERID),
        //! the same boundary collectAppletsData draws with data.id < 0
        if (id >= 0) {
            ids << id;
        }
    }

    return ids;
}

//! The G3 drop-marker sentinel (docs/tracking/e2e-interaction-test-plan.md): the
//! viewDropMarkerIndex() D-Bus read reports the drag placeholder spacer's
//! visual insert index while a drag hovers the view, or -1 when no marker is
//! live. Index 0 is the LEADING insert position - a real, live marker, not
//! "absent"; only a negative value means no marker. This predicate pins that
//! off-by-one boundary so an abort assertion can read "clean" without
//! mistaking the leading-index case for a stranded marker - the direct
//! insert(-1) observability the A1/A2 aborts assert on.
inline bool dropMarkerIsLive(int dropMarkerIndex)
{
    return dropMarkerIndex >= 0;
}

inline QJsonObject serializeLayoutRecord(const LayoutRecord &record)
{
    QJsonObject json;
    json[QStringLiteral("name")] = record.name;
    json[QStringLiteral("isActive")] = record.isActive;
    json[QStringLiteral("activities")] = QJsonArray::fromStringList(record.activities);
    json[QStringLiteral("viewsCount")] = record.viewsCount;

    return json;
}

inline QString serializeLayoutsData(MemoryUsage::LayoutsMemory memory, const QList<LayoutRecord> &records)
{
    QJsonArray layouts;

    for (const auto &record : records) {
        layouts.append(serializeLayoutRecord(record));
    }

    QJsonObject json;
    json[QStringLiteral("memoryUsage")] = memoryUsageName(memory);
    json[QStringLiteral("layouts")] = layouts;

    return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
}

inline QJsonObject serializeScreenRecord(const ScreenRecord &record)
{
    QJsonObject json;
    json[QStringLiteral("id")] = record.id;
    json[QStringLiteral("name")] = record.name;
    json[QStringLiteral("geometry")] = serializeRect(record.geometry);
    json[QStringLiteral("isActive")] = record.isActive;
    json[QStringLiteral("isPrimary")] = record.isPrimary;

    return json;
}

inline QString serializeScreensData(const QList<ScreenRecord> &records)
{
    QJsonArray array;

    for (const auto &record : records) {
        array.append(serializeScreenRecord(record));
    }

    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

inline QJsonObject serializeColorizerRecord(const ColorizerRecord &record)
{
    QJsonObject json;
    json[QStringLiteral("containmentId")] = static_cast<qint64>(record.containmentId);
    json[QStringLiteral("enabled")] = record.enabled;
    json[QStringLiteral("themeColorsMode")] = themeColorsModeName(record.themeColors);
    json[QStringLiteral("windowColorsMode")] = windowColorsModeName(record.windowColors);
    json[QStringLiteral("colorizerPresent")] = record.colorizerPresent;
    json[QStringLiteral("mustBeShown")] = record.mustBeShown;
    json[QStringLiteral("applyingWindowColors")] = record.applyingWindowColors;
    json[QStringLiteral("backgroundIsBusy")] = record.backgroundIsBusy;
    json[QStringLiteral("currentBackgroundBrightness")] = record.currentBackgroundBrightness;
    json[QStringLiteral("scheme")] = record.scheme;
    //! "#rrggbb" hex, or empty for an unresolved (invalid) colour rather than
    //! QColor's silent "#000000" default which would read as a real black
    json[QStringLiteral("applyColor")] = record.applyColor.isValid() ? record.applyColor.name() : QString();
    json[QStringLiteral("textColor")] = record.textColor.isValid() ? record.textColor.name() : QString();
    json[QStringLiteral("backgroundColor")] = record.backgroundColor.isValid() ? record.backgroundColor.name() : QString();
    json[QStringLiteral("applyColorBrightness")] = record.applyColorBrightness;
    json[QStringLiteral("backgroundColorBrightness")] = record.backgroundColorBrightness;

    return json;
}

inline QString serializeColorizerData(const ColorizerRecord &record)
{
    return QString::fromUtf8(QJsonDocument(serializeColorizerRecord(record)).toJson(QJsonDocument::Compact));
}

inline QJsonObject serializeTaskRecord(const TaskRecord &record)
{
    QJsonObject json;
    json[QStringLiteral("appletId")] = record.appletId;
    json[QStringLiteral("index")] = record.index;
    json[QStringLiteral("appId")] = record.appId;
    json[QStringLiteral("launcherUrl")] = record.launcherUrl;
    json[QStringLiteral("isLauncher")] = record.isLauncher;
    json[QStringLiteral("isGrouped")] = record.isGrouped;
    json[QStringLiteral("childCount")] = record.childCount;
    json[QStringLiteral("isActive")] = record.isActive;
    json[QStringLiteral("isMinimized")] = record.isMinimized;
    json[QStringLiteral("demandsAttention")] = record.demandsAttention;
    json[QStringLiteral("badge")] = record.badge;
    json[QStringLiteral("geometry")] = serializeRect(record.geometry);

    return json;
}

inline QString serializeTaskRecords(const QList<TaskRecord> &records)
{
    QJsonArray array;

    for (const auto &record : records) {
        array.append(serializeTaskRecord(record));
    }

    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

inline QJsonObject serializeViewRecord(const ViewRecord &record)
{
    QJsonObject json;
    json[QStringLiteral("containmentId")] = static_cast<qint64>(record.containmentId);
    json[QStringLiteral("layout")] = record.layout;
    json[QStringLiteral("isCloned")] = record.isCloned;
    json[QStringLiteral("isClonedFrom")] = record.isClonedFrom;
    json[QStringLiteral("type")] = viewTypeName(record.type);
    json[QStringLiteral("screen")] = record.screen;
    json[QStringLiteral("onPrimary")] = record.onPrimary;
    json[QStringLiteral("edge")] = edgeName(record.edge);
    json[QStringLiteral("alignment")] = alignmentName(record.alignment);
    json[QStringLiteral("visibilityMode")] = visibilityModeName(record.visibilityMode);
    json[QStringLiteral("isHidden")] = record.isHidden;
    json[QStringLiteral("inStartup")] = record.inStartup;
    json[QStringLiteral("isOffScreen")] = record.isOffScreen;
    json[QStringLiteral("absoluteGeometry")] = serializeRect(record.absoluteGeometry);
    json[QStringLiteral("localGeometry")] = serializeRect(record.localGeometry);
    json[QStringLiteral("screenGeometry")] = serializeRect(record.screenGeometry);
    json[QStringLiteral("strutsThickness")] = record.strutsThickness;
    json[QStringLiteral("publishedStruts")] = serializeRect(record.publishedStruts);
    json[QStringLiteral("maskRect")] = serializeRect(record.maskRect);

    //! the input region is a single rect today (Effects::inputMask), but the
    //! interface doc specifies an array of rects so multi-rect input regions
    //! can land later without a schema break. An invalid or empty rect means
    //! "no input restriction published" (Effects::setInputMask clears the
    //! window mask for those), reported as an empty array.
    QJsonArray inputRegion;

    if (record.inputMask.isValid() && !record.inputMask.isEmpty()) {
        inputRegion.append(serializeRect(record.inputMask));
    }

    json[QStringLiteral("inputRegionRects")] = inputRegion;

    //! the region actually handed to QWindow::setMask (Effects::appliedInputMask):
    //! the union kept wide across a length shrink so Qt6 wayland does not clip the
    //! vacated region's clearing damage, collapsing back to inputRegionRects once
    //! the band settles. It differs from inputRegionRects only mid-shrink, which is
    //! exactly the window the maximize-length settle is asserted across. Same
    //! empty-means-cleared and array-for-future-multi-rect conventions as above.
    QJsonArray appliedInputRegion;

    if (record.appliedInputMask.isValid() && !record.appliedInputMask.isEmpty()) {
        appliedInputRegion.append(serializeRect(record.appliedInputMask));
    }

    json[QStringLiteral("appliedInputRegionRects")] = appliedInputRegion;
    json[QStringLiteral("editMode")] = record.editMode;
    json[QStringLiteral("inConfigureAppletsMode")] = record.inConfigureAppletsMode;
    json[QStringLiteral("keyboardNavigation")] = record.keyboardNavigation;

    return json;
}

inline QString serializeViewRecords(const QList<ViewRecord> &records)
{
    QJsonArray array;

    for (const auto &record : records) {
        array.append(serializeViewRecord(record));
    }

    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

//! One config value as a STABLE, comparable JSON scalar for viewConfigData()
//! and appletConfigData() (docs/reference/dbus-observability-interface.md). Config
//! values are the user's own dock settings - almost all are int/double/bool/
//! string/stringlist, which QJsonValue::fromVariant maps straight to a JSON
//! scalar. The handful of typed values with no JSON scalar (a QColor for
//! shadowColor) fall back to a canonical string, NOT the JSON null that
//! fromVariant would otherwise hand back: the snapshot-diff the edit-mode
//! audit runs needs equal-means-equal, and collapsing every color to null
//! would make two different colors compare equal (a false PASS). Config keys
//! always carry a schema default, so a genuinely-null value never occurs;
//! null out of fromVariant therefore means "no JSON scalar for this type",
//! which is the only case the fallback handles.
inline QJsonValue configValueToJson(const QVariant &value)
{
    const QJsonValue direct = QJsonValue::fromVariant(value);

    if (direct.type() != QJsonValue::Null && direct.type() != QJsonValue::Undefined) {
        return direct;
    }

    if (value.canConvert<QColor>()) {
        return value.value<QColor>().name(QColor::HexArgb);
    }

    return value.toString();
}

//! A whole config property map serialized key -> value. Keys are whatever the
//! live KConfigPropertyMap carries (every schema key with its current value,
//! plus any stray key a control wrote outside the schema - the S-a dead-key
//! finding surfaces exactly here), so the audit's config-snapshot diff sees
//! the full surface, not a curated subset.
inline QJsonObject serializeConfigMap(const QVariantMap &config)
{
    QJsonObject json;

    for (auto it = config.constBegin(); it != config.constEnd(); ++it) {
        json[it.key()] = configValueToJson(it.value());
    }

    return json;
}

inline QJsonObject serializeViewLiveRecord(const ViewLiveRecord &record)
{
    QJsonObject json;
    json[QStringLiteral("byPassWM")] = record.byPassWM;
    json[QStringLiteral("isPreferredForShortcuts")] = record.isPreferredForShortcuts;
    json[QStringLiteral("visibilityTimerShow")] = record.visibilityTimerShow;
    json[QStringLiteral("visibilityTimerHide")] = record.visibilityTimerHide;
    json[QStringLiteral("visibilityEnableKWinEdges")] = record.visibilityEnableKWinEdges;
    json[QStringLiteral("visibilityRaiseOnDesktop")] = record.visibilityRaiseOnDesktop;
    json[QStringLiteral("visibilityRaiseOnActivity")] = record.visibilityRaiseOnActivity;
    json[QStringLiteral("indicatorPresent")] = record.indicatorPresent;
    json[QStringLiteral("indicatorEnabled")] = record.indicatorEnabled;
    json[QStringLiteral("indicatorType")] = record.indicatorType;
    json[QStringLiteral("indicatorCustomType")] = record.indicatorCustomType;
    json[QStringLiteral("inAdvancedModeForEditSettings")] = record.inAdvancedModeForEditSettings;
    json[QStringLiteral("settingsWindowScaleWidth")] = record.settingsWindowScaleWidth;
    json[QStringLiteral("settingsWindowScaleHeight")] = record.settingsWindowScaleHeight;

    return json;
}

//! The viewConfigData() payload: the containment's config VALUES (the "config"
//! object) plus the live C++-property half (the "view" object). Two objects,
//! not one flat map, so the audit's config-snapshot diff runs over "config"
//! alone (a config key and a live C++ property never collide) while the P3
//! leg reads "view". docs/reference/dbus-observability-interface.md.
inline QString serializeConfigData(uint containmentId, const QVariantMap &config, const ViewLiveRecord &live)
{
    QJsonObject json;
    json[QStringLiteral("containmentId")] = static_cast<qint64>(containmentId);
    json[QStringLiteral("config")] = serializeConfigMap(config);
    json[QStringLiteral("view")] = serializeViewLiveRecord(live);

    return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
}

//! The appletConfigData() payload: one child applet's config VALUES, keyed by
//! containment id, applet id and plugin. The D10-class surface the tasks-page
//! audit (CL-5) diffs to prove whether tasks.plasmoid.configuration.* writes
//! land anywhere. docs/reference/dbus-observability-interface.md.
inline QString serializeAppletConfigData(uint containmentId, int appletId, const QString &plugin, const QVariantMap &config)
{
    QJsonObject json;
    json[QStringLiteral("containmentId")] = static_cast<qint64>(containmentId);
    json[QStringLiteral("appletId")] = appletId;
    json[QStringLiteral("plugin")] = plugin;
    json[QStringLiteral("config")] = serializeConfigMap(config);

    return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
}

//! snapshot one live view into a value record (dbusreports.cpp)
ViewRecord collectViewRecord(const Latte::View *view, bool inConfigureAppletsMode);

//! serialize a set of live views for the viewsData() D-Bus read
QString collectViewsData(const QList<Latte::View *> &views, bool inConfigureAppletsMode);

//! serialize one live view's applets for the viewAppletsData() D-Bus read
QString collectAppletsData(const Latte::View *view);

//! serialize one live view's windows-tracker facts for the trackerData()
//! D-Bus read
QString collectTrackerData(const Latte::View *view);

//! serialize one live view's latte-tasks items for the viewTasksData()
//! D-Bus read
QString collectTasksData(const Latte::View *view);

//! serialize one live view's colorizer facts for the colorizerData()
//! D-Bus read
QString collectColorizerData(const Latte::View *view);

//! read one live view's live C++-property half of viewConfigData() off
//! View/VisibilityManager/Indicator (dbusreports.cpp)
ViewLiveRecord collectViewLiveRecord(const Latte::View *view);

//! The universalSettings edit-settings state the Corona resolves and merges into
//! viewConfigData()'s "view" object (the CL-6 chrome audit's P3 leg for the
//! advanced-mode toggle and the drag-corner scales). Sourced from
//! UniversalSettings, which the Corona owns; a named bundle rather than three
//! positional scalars so the two same-typed scales cannot be transposed at the
//! call site.
struct EditSettingsState {
    bool inAdvancedMode{false};
    double settingsWindowScaleWidth{1.0};
    double settingsWindowScaleHeight{1.0};
};

//! serialize one live view's containment config VALUES plus the live
//! C++-property half (with the universalSettings edit-settings state merged in)
//! for the viewConfigData() D-Bus read
QString collectConfigData(const Latte::View *view, const EditSettingsState &editSettings);

//! serialize one child applet's config VALUES for the appletConfigData()
//! D-Bus read; "{}" (warned) when no applet on the view carries appletId
QString collectAppletConfigData(const Latte::View *view, int appletId);

//! serialize the loaded layouts for the layoutsData() D-Bus read
QString collectLayoutsData(Latte::Layouts::Manager *manager);

//! serialize the ScreenPool's id<->connector mapping for the screensData()
//! D-Bus read (the queryable screen<->output topology)
QString collectScreensData(Latte::ScreenPool *pool);

}
}

#endif
