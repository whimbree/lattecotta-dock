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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QRect>
#include <QString>

// C++
#include <array>
#include <optional>

// Plasma
#include <Plasma/Plasma>

namespace Latte {
class View;
namespace Layouts {
class Manager;
}
}

namespace Latte {
namespace DbusReports {

//! The org.kde.LatteDock JSON read surface
//! (docs/dbus-observability-interface.md). Two layers, so serialization
//! stays a pure core: ViewRecord is a value snapshot of every field
//! viewsData() reports and the inline name/serialize functions below are
//! pure (unit-tested without a corona in tests/units/dbusreportstest.cpp);
//! only the collect* functions in dbusreports.cpp read live View objects.

//! One view's windows-tracker facts as trackerData() reports them
//! (docs/dbus-observability-interface.md, step 3)
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
//! (docs/dbus-observability-interface.md, step 4)
struct LayoutRecord {
    QString name;
    bool isActive{false};
    QStringList activities;
    int viewsCount{0};
};

//! One view's colorizer decision facts as colorizerData() reports them
//! (docs/dbus-observability-interface.md, step 4). The doc's prose mapped
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
};

//! One task item of a view's tasks plasmoid as viewTasksData() reports it
//! (docs/dbus-observability-interface.md, step 4). Identity is
//! appId/launcherUrl only - window titles are other applications'
//! content and stay out by design (the interface doc records the rule).
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
//! (docs/dbus-observability-interface.md, step 2)
struct AppletRecord {
    int id{-1};
    QString plugin;
    int index{-1};
    QRect geometry;
    bool isExpanded{false};
    bool inScheduledDestruction{false};
    bool lockedZoom{false};
    bool colorizingBlocked{false};
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
    bool editMode{false};
    bool inConfigureAppletsMode{false};
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
    json[QStringLiteral("editMode")] = record.editMode;
    json[QStringLiteral("inConfigureAppletsMode")] = record.inConfigureAppletsMode;

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

//! serialize the loaded layouts for the layoutsData() D-Bus read
QString collectLayoutsData(Latte::Layouts::Manager *manager);

}
}

#endif
