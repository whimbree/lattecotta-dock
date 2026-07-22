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
#include "data/viewdata.h"
#include "../plasmoid/plugin/middleclickdispatch.h"
//! the containment plugin's enum home, included relatively: the theme and
//! window color modes colorizerData() names live there (a header-only
//! Q_GADGET, no moc linkage), and naming the REAL enums keeps -Wswitch
//! honest here like every other enum-name mapping in this file
#include "../containment/plugin/types.h"

// Qt
#include <QColor>
#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSet>
#include <QString>
#include <QThread>
#include <QVariant>
#include <QVariantMap>

// C++
#include <algorithm>
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

//! Opaque process-local QObject identities for the dock-system snapshot.
//! Addresses never cross D-Bus: the first observed object receives the next
//! monotonic id and keeps it for its lifetime. Entries carry a QPointer and a
//! generation-checked direct destruction connection, so reconstructing a
//! QObject at the same address cannot inherit the retired object's identity.
//! The numeric namespace is shared across every reported object kind so equal
//! tokens mean the same live QObject without exposing an address.
//!
//! The live dock graph is GUI-thread-owned. Keeping lookup, registration, and
//! direct retirement on that thread makes each registry operation atomic with
//! respect to QObject destruction instead of adding cross-thread races.
class RuntimeObjectIdentityRegistry final : public QObject
{
public:
    explicit RuntimeObjectIdentityRegistry(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    [[nodiscard]] quint64 idFor(const QObject *object)
    {
        if (!object) {
            return 0;
        }

        Q_ASSERT(hasRequiredThreadAffinity(object));
        auto existing = m_ids.find(object);

        if (existing != m_ids.end()) {
            if (existing->object.data() == object) {
                return existing->id;
            }

            //! A cleared QPointer under the same address is a retired
            //! generation. This fallback makes address reuse safe even if an
            //! entry survives an unusual destruction path.
            m_ids.erase(existing);
        }

        const quint64 id = m_nextId++;
        auto *const trackedObject = const_cast<QObject *>(object);
        m_ids.insert(object, IdentityEntry{trackedObject, id});
        const QMetaObject::Connection retirement =
            QObject::connect(trackedObject, &QObject::destroyed, this,
                             [this, object, id]() {
                                 Q_ASSERT(hasRequiredThreadAffinity(this));
                                 auto entry = m_ids.find(object);
                                 if (entry != m_ids.end() && entry->id == id) {
                                     m_ids.erase(entry);
                                 }
                             }, Qt::DirectConnection);
        Q_ASSERT(retirement);
        return id;
    }

    [[nodiscard]] QString tokenFor(const QObject *object)
    {
        const quint64 id = idFor(object);
        return id == 0 ? QString() : QStringLiteral("object-%1").arg(id);
    }

    //! Internal diagnostics for tests. The count is deliberately not exposed
    //! over D-Bus; it proves that destruction retires an entry immediately,
    //! before an address is reused or another snapshot is requested.
    [[nodiscard]] qsizetype trackedObjectCount() const
    {
        Q_ASSERT(hasRequiredThreadAffinity(this));
        return m_ids.size();
    }

    //! Pure observation of the registry's internal GUI-thread precondition.
    //! idFor() asserts this before any lookup or mutation.
    [[nodiscard]] bool hasRequiredThreadAffinity(const QObject *object) const noexcept
    {
        const auto *const application = QCoreApplication::instance();
        return application
            && thread() == application->thread()
            && QThread::currentThread() == thread()
            && object
            && object->thread() == thread();
    }

private:
    struct IdentityEntry {
        QPointer<QObject> object;
        quint64 id{0};
    };

    quint64 m_nextId{1};
    QHash<const QObject *, IdentityEntry> m_ids;
};

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
//! sentinel). Per-applet application state is reported by viewAppletsData,
//! not as a per-view fact.
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

//! One tasks applet's latest dispatch property as seen by the aggregate
//! collector. Keeping containment and applet identity beside the untrusted
//! QVariant lets the pure selector enforce query scope and report the exact
//! candidate that invalidated the aggregate.
struct MiddleClickDispatchCandidate {
    uint containmentId{0};
    int appletId{-1};
    QVariant state;
};

enum class MiddleClickDispatchRefusal {
    None = 0,
    ContainmentMismatch,
    MalformedState,
    DuplicateSequence
};

struct MiddleClickDispatchSelection {
    std::optional<Tasks::MiddleClickDispatchRecord> record;
    MiddleClickDispatchRefusal refusal{MiddleClickDispatchRefusal::None};
    uint candidateContainmentId{0};
    int appletId{-1};
    qint64 duplicateSequence{0};
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
    //! userBlocked / inlineFull.
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

//! Applet rearrangement is process-global only while a particular dock is in
//! edit mode. Keeping the expression in the value layer prevents reporting the
//! global toggle as effective state for every unrelated dock.
[[nodiscard]] constexpr bool effectiveConfigureAppletsMode(
    bool editMode, bool globalConfigureAppletsMode) noexcept
{
    return editMode && globalConfigureAppletsMode;
}

enum class DockRelationship {
    Independent,
    LinkedRoot,
    LinkedMember
};

//! The value-only part of live collection ordering. sourceIndex points back to
//! Synchronizer::currentViews(); persistentDockId is read before the identity
//! registry is touched, then becomes the sole ordering authority.
struct DockCollectionOrderInput {
    uint persistentDockId{0};
    qsizetype sourceIndex{-1};
};

[[nodiscard]] inline QList<qsizetype> orderDockCollectionByPersistentId(
    QList<DockCollectionOrderInput> inputs)
{
    std::ranges::sort(inputs, {}, &DockCollectionOrderInput::persistentDockId);

    QList<qsizetype> sourceIndexes;
    sourceIndexes.reserve(inputs.size());
    for (qsizetype index = 0; index < inputs.size(); ++index) {
        const auto &input = inputs.at(index);
        Q_ASSERT(input.persistentDockId > 0);
        Q_ASSERT(input.sourceIndex >= 0);
        //! Sorting once makes forbidden duplicate identities adjacent, avoiding
        //! a second allocation solely for debug-time uniqueness validation.
        Q_ASSERT(index == 0
                 || inputs.at(index - 1).persistentDockId != input.persistentDockId);
        sourceIndexes.append(input.sourceIndex);
    }
    return sourceIndexes;
}

//! Live lineage facts needed to classify one dock without a View dependency.
//! Root views identify themselves as their group; linked members carry a
//! distinct positive target and their placement-ownership policy.
//! classifyDockRelationshipGraph() validates that the target is a live linked
//! root, making cycles and member chains invalid.
struct DockLineageInput {
    uint persistentDockId{0};
    int groupId{-1};
    bool isOriginal{false};
    bool isCloned{false};
    bool isSingle{false};
    Data::View::LinkPlacement linkPlacement{Data::View::LinkPlacement::ScreenGroupDerived};
};

struct DockRelationshipClassification {
    uint logicalDockId{0};
    int originalDockId{-1};
    DockRelationship relationship{DockRelationship::Independent};
    std::optional<Data::View::LinkPlacement> linkPlacement;
};

[[nodiscard]] constexpr bool isValidLinkPlacement(
    Data::View::LinkPlacement placement) noexcept
{
    switch (placement) {
    case Data::View::LinkPlacement::ScreenGroupDerived:
    case Data::View::LinkPlacement::ExplicitTarget:
        return true;
    }

    return false;
}

[[nodiscard]] constexpr std::optional<DockRelationshipClassification>
classifyDockRelationship(const DockLineageInput &lineage) noexcept
{
    if (lineage.persistentDockId == 0 || lineage.isOriginal == lineage.isCloned
            || !isValidLinkPlacement(lineage.linkPlacement)) {
        return std::nullopt;
    }

    if (lineage.isOriginal) {
        if (lineage.groupId != static_cast<int>(lineage.persistentDockId)
                || lineage.linkPlacement != Data::View::LinkPlacement::ScreenGroupDerived) {
            return std::nullopt;
        }

        return DockRelationshipClassification{
            lineage.persistentDockId,
            -1,
            lineage.isSingle ? DockRelationship::Independent
                             : DockRelationship::LinkedRoot,
            std::nullopt};
    }

    if (lineage.isSingle || lineage.groupId <= 0
        || lineage.groupId == static_cast<int>(lineage.persistentDockId)) {
        return std::nullopt;
    }

    return DockRelationshipClassification{
        static_cast<uint>(lineage.groupId),
        lineage.groupId,
        DockRelationship::LinkedMember,
        lineage.linkPlacement};
}

using DockRelationshipGraph = QHash<uint, DockRelationshipClassification>;

//! Validate the whole live relationship graph before any view is serialized.
//! A linked member may point only to a present linked root. This direct-root
//! rule rejects missing targets, independent targets, member chains, and
//! cycles without a recursive traversal.
[[nodiscard]] inline std::optional<DockRelationshipGraph>
classifyDockRelationshipGraph(const QList<DockLineageInput> &lineages)
{
    DockRelationshipGraph graph;
    graph.reserve(lineages.size());

    for (const auto &lineage : lineages) {
        const auto classification = classifyDockRelationship(lineage);
        if (!classification || graph.contains(lineage.persistentDockId)) {
            return std::nullopt;
        }

        graph.insert(lineage.persistentDockId, *classification);
    }

    for (const auto &lineage : lineages) {
        const auto &classification = graph.value(lineage.persistentDockId);
        if (classification.relationship != DockRelationship::LinkedMember) {
            continue;
        }

        const auto target = graph.constFind(classification.logicalDockId);
        if (target == graph.constEnd()
            || target->relationship != DockRelationship::LinkedRoot) {
            return std::nullopt;
        }
    }

    return graph;
}

//! The live QObject graph behind one dock. Tokens are opaque, process-local,
//! and comparable within and across dockSystemData() snapshots. An empty token
//! means that optional controller does not currently exist.
struct DockObjectIdentities {
    QString view;
    QString containment;
    QString configuration;
    QString layout;
    QString layoutController;
    QString geometryController;
    QString editController;
    QString configWindow;
};

//! One dock in the atomic dockSystemData() snapshot. persistentDockId is the
//! Plasma containment id. logicalDockId is the relationship root containment
//! for a linked member and otherwise equals persistentDockId. Independent
//! duplication does not establish a relationship and therefore has no source
//! field here.
struct DockSystemViewRecord {
    quint64 runtimeViewId{0};
    uint persistentDockId{0};
    uint logicalDockId{0};
    int originalDockId{-1};
    DockRelationship relationship{DockRelationship::Independent};
    std::optional<Data::View::LinkPlacement> linkPlacement;
    Types::ScreensGroup screensGroup{Types::SingleScreenGroup};
    QList<uint> linkedDockIds;

    QString layout;
    int screenId{-1};
    QString screen;
    bool onPrimary{false};
    Types::ViewType type{Types::DockView};
    Plasma::Types::Location edge{Plasma::Types::Floating};
    Plasma::Types::FormFactor orientation{Plasma::Types::Planar};
    Types::Alignment alignment{Types::NoneAlignment};
    float maximumLengthRatio{1.0F};
    float offsetRatio{0.0F};

    std::optional<int> configuredIconSize;
    std::optional<int> effectiveIconSize;
    std::optional<int> availablePrimaryLength;
    int normalThickness{0};
    int maximumNormalThickness{0};

    QRect windowGeometry;
    QRect absoluteGeometry;
    QRect localGeometry;
    QRect screenGeometry;
    QRect canvasGeometry;
    QRect effectsRect;
    QRect appletsLayoutGeometry;
    QRect maskRect;
    QRect inputMask;
    QRect appliedInputMask;
    int strutsThickness{0};
    QRect publishedStruts;

    Types::Visibility visibilityMode{Types::None};
    bool isHidden{false};
    bool inStartup{false};
    bool isOffScreen{false};
    bool inRelocationAnimation{false};
    bool inRelocationShowing{false};
    bool geometrySettled{false};
    quint64 relocationGeneration{0};
    quint64 appliedRelocationGeneration{0};
    bool inDelete{false};
    bool inReadyState{false};
    bool editMode{false};
    bool settingsWindowShown{false};
    DockObjectIdentities objects;
};

//! Same-edge stacking has no explicit authority in the current runtime. This
//! typed negative capability is part of the schema so a consumer cannot infer
//! an order from the canonical JSON array or from QObject creation order.
struct DockStackModelRecord {
    bool available{false};
    QString reason{QStringLiteral("No runtime authority models same-edge stack order or accumulated offsets.")};
};

struct DockSystemSnapshot {
    static constexpr int SchemaVersion = 2;

    quint64 snapshotSequence{0};
    bool globalConfigureAppletsMode{false};
    DockStackModelRecord stacking;
    QList<DockSystemViewRecord> views;
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

inline QString orientationName(Plasma::Types::FormFactor orientation)
{
    switch (orientation) {
    case Plasma::Types::Planar:
        return QStringLiteral("planar");
    case Plasma::Types::MediaCenter:
        return QStringLiteral("mediaCenter");
    case Plasma::Types::Horizontal:
        return QStringLiteral("horizontal");
    case Plasma::Types::Vertical:
        return QStringLiteral("vertical");
    case Plasma::Types::Application:
        return QStringLiteral("application");
    }

    Q_UNREACHABLE();
}

inline QString screensGroupName(Types::ScreensGroup group)
{
    switch (group) {
    case Types::SingleScreenGroup:
        return QStringLiteral("single");
    case Types::AllScreensGroup:
        return QStringLiteral("allScreens");
    case Types::AllSecondaryScreensGroup:
        return QStringLiteral("allSecondaryScreens");
    }

    Q_UNREACHABLE();
}

inline QString dockRelationshipName(DockRelationship relationship)
{
    switch (relationship) {
    case DockRelationship::Independent:
        return QStringLiteral("independent");
    case DockRelationship::LinkedRoot:
        return QStringLiteral("linkedRoot");
    case DockRelationship::LinkedMember:
        return QStringLiteral("linkedMember");
    }

    Q_UNREACHABLE();
}

inline QString linkPlacementName(Data::View::LinkPlacement placement)
{
    switch (placement) {
    case Data::View::LinkPlacement::ScreenGroupDerived:
        return QStringLiteral("screenGroupDerived");
    case Data::View::LinkPlacement::ExplicitTarget:
        return QStringLiteral("explicitTarget");
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

inline QString middleClickRowKindName(Tasks::MiddleClickRowKind kind)
{
    switch (kind) {
    case Tasks::MiddleClickRowKind::Launcher:
        return QStringLiteral("launcher");
    case Tasks::MiddleClickRowKind::Task:
        return QStringLiteral("task");
    }

    Q_UNREACHABLE();
}

inline QString taskActionName(Tasks::Types::TaskAction action)
{
    switch (action) {
    case Tasks::Types::NoneAction:
        return QStringLiteral("none");
    case Tasks::Types::Close:
        return QStringLiteral("close");
    case Tasks::Types::NewInstance:
        return QStringLiteral("newInstance");
    case Tasks::Types::ToggleMinimized:
        return QStringLiteral("toggleMinimized");
    case Tasks::Types::CycleThroughTasks:
        return QStringLiteral("cycleThroughTasks");
    case Tasks::Types::ToggleGrouping:
        return QStringLiteral("toggleGrouping");
    case Tasks::Types::PresentWindows:
        return QStringLiteral("presentWindows");
    case Tasks::Types::PreviewWindows:
        return QStringLiteral("previewWindows");
    case Tasks::Types::HighlightWindows:
        return QStringLiteral("highlightWindows");
    case Tasks::Types::PreviewAndHighlightWindows:
        return QStringLiteral("previewAndHighlightWindows");
    }

    Q_UNREACHABLE();
}

inline QString middleClickOperationName(Tasks::MiddleClickOperation operation)
{
    switch (operation) {
    case Tasks::MiddleClickOperation::None:
        return QStringLiteral("none");
    case Tasks::MiddleClickOperation::RequestActivate:
        return QStringLiteral("requestActivate");
    case Tasks::MiddleClickOperation::RequestClose:
        return QStringLiteral("requestClose");
    case Tasks::MiddleClickOperation::RequestNewInstance:
        return QStringLiteral("requestNewInstance");
    case Tasks::MiddleClickOperation::RequestToggleMinimized:
        return QStringLiteral("requestToggleMinimized");
    case Tasks::MiddleClickOperation::CycleOrActivate:
        return QStringLiteral("cycleOrActivate");
    case Tasks::MiddleClickOperation::RequestToggleGrouping:
        return QStringLiteral("requestToggleGrouping");
    }

    Q_UNREACHABLE();
}

inline std::optional<Tasks::MiddleClickRowKind> middleClickRowKindFromValue(int value)
{
    switch (value) {
    case static_cast<int>(Tasks::MiddleClickRowKind::Launcher):
        return Tasks::MiddleClickRowKind::Launcher;
    case static_cast<int>(Tasks::MiddleClickRowKind::Task):
        return Tasks::MiddleClickRowKind::Task;
    }

    return std::nullopt;
}

inline std::optional<Tasks::MiddleClickOperation> middleClickOperationFromValue(int value)
{
    switch (value) {
    case static_cast<int>(Tasks::MiddleClickOperation::None):
    case static_cast<int>(Tasks::MiddleClickOperation::RequestActivate):
    case static_cast<int>(Tasks::MiddleClickOperation::RequestClose):
    case static_cast<int>(Tasks::MiddleClickOperation::RequestNewInstance):
    case static_cast<int>(Tasks::MiddleClickOperation::RequestToggleMinimized):
    case static_cast<int>(Tasks::MiddleClickOperation::CycleOrActivate):
    case static_cast<int>(Tasks::MiddleClickOperation::RequestToggleGrouping):
        return static_cast<Tasks::MiddleClickOperation>(value);
    }

    return std::nullopt;
}

//! Validate the tasks-backend -> app report boundary without coercing malformed
//! values into plausible defaults. An empty map is the legitimate no-event
//! state; callers distinguish it before using this parser.
inline std::optional<Tasks::MiddleClickDispatchRecord> middleClickDispatchRecordFromMap(const QVariantMap &data)
{
    const QVariant rowIdentity = data.value(QStringLiteral("rowIdentity"));
    const QVariant rowKindValue = data.value(QStringLiteral("rowKind"));
    const QVariant configuredActionValue = data.value(QStringLiteral("configuredAction"));
    const QVariant operationValue = data.value(QStringLiteral("dispatchedOperation"));
    const QVariant sequenceValue = data.value(QStringLiteral("sequence"));

    if (data.size() != 5
        || rowIdentity.typeId() != QMetaType::QString || rowIdentity.toString().isEmpty()
        || rowKindValue.typeId() != QMetaType::Int
        || configuredActionValue.typeId() != QMetaType::Int
        || operationValue.typeId() != QMetaType::Int
        || sequenceValue.typeId() != QMetaType::LongLong) {
        return std::nullopt;
    }

    const auto rowKind = middleClickRowKindFromValue(rowKindValue.toInt());
    const auto action = Tasks::classifyOfferedMiddleClickAction(configuredActionValue.toInt());
    const auto operation = middleClickOperationFromValue(operationValue.toInt());
    const qint64 sequence = sequenceValue.toLongLong();

    if (!rowKind || !action || !operation || sequence <= 0
        || !Tasks::middleClickDispatchPairIsValid(*rowKind, *action, *operation)) {
        return std::nullopt;
    }

    Tasks::MiddleClickDispatchRecord record;
    record.rowIdentity = rowIdentity.toString();
    record.rowKind = *rowKind;
    record.configuredAction = action->configuredAction;
    record.dispatchedOperation = *operation;
    record.sequence = sequence;
    return record;
}

//! Select one newest event only when every reported candidate is trustworthy.
//! Empty maps are the legitimate no-event state. A candidate from another
//! containment, malformed nonempty state, or any repeated sequence invalidates
//! the whole aggregate instead of allowing an older plausible event through.
inline MiddleClickDispatchSelection selectLatestMiddleClickDispatch(
    uint requestedContainmentId,
    const QList<MiddleClickDispatchCandidate> &candidates)
{
    std::optional<Tasks::MiddleClickDispatchRecord> latest;
    QSet<qint64> sequences;

    for (const auto &candidate : candidates) {
        if (candidate.containmentId != requestedContainmentId) {
            MiddleClickDispatchSelection result;
            result.refusal = MiddleClickDispatchRefusal::ContainmentMismatch;
            result.candidateContainmentId = candidate.containmentId;
            result.appletId = candidate.appletId;
            return result;
        }

        if (candidate.state.typeId() != QMetaType::QVariantMap) {
            MiddleClickDispatchSelection result;
            result.refusal = MiddleClickDispatchRefusal::MalformedState;
            result.candidateContainmentId = candidate.containmentId;
            result.appletId = candidate.appletId;
            return result;
        }

        const QVariantMap data = candidate.state.toMap();
        if (data.isEmpty()) {
            continue;
        }

        const auto record = middleClickDispatchRecordFromMap(data);
        if (!record) {
            MiddleClickDispatchSelection result;
            result.refusal = MiddleClickDispatchRefusal::MalformedState;
            result.candidateContainmentId = candidate.containmentId;
            result.appletId = candidate.appletId;
            return result;
        }

        if (sequences.contains(record->sequence)) {
            MiddleClickDispatchSelection result;
            result.refusal = MiddleClickDispatchRefusal::DuplicateSequence;
            result.candidateContainmentId = candidate.containmentId;
            result.appletId = candidate.appletId;
            result.duplicateSequence = record->sequence;
            return result;
        }

        sequences.insert(record->sequence);
        if (!latest || record->sequence > latest->sequence) {
            latest = record;
        }
    }

    MiddleClickDispatchSelection result;
    result.record = latest;
    return result;
}

inline QJsonObject serializeMiddleClickDispatchRecord(const Tasks::MiddleClickDispatchRecord &record)
{
    QJsonObject json;
    json[QStringLiteral("rowIdentity")] = record.rowIdentity;
    json[QStringLiteral("rowKind")] = middleClickRowKindName(record.rowKind);
    json[QStringLiteral("configuredAction")] = taskActionName(record.configuredAction);
    json[QStringLiteral("dispatchedOperation")] = middleClickOperationName(record.dispatchedOperation);
    json[QStringLiteral("sequence")] = record.sequence;
    return json;
}

inline QString serializeMiddleClickDispatchData(const std::optional<Tasks::MiddleClickDispatchRecord> &record)
{
    const QJsonObject json = record ? serializeMiddleClickDispatchRecord(*record) : QJsonObject{};
    return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
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

inline QJsonValue serializeOptionalInt(const std::optional<int> &value)
{
    return value ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
}

inline QJsonValue serializeOptionalLinkPlacement(
    const std::optional<Data::View::LinkPlacement> &placement)
{
    return placement ? QJsonValue(linkPlacementName(*placement))
                     : QJsonValue(QJsonValue::Null);
}

inline QJsonValue serializeOptionalObjectToken(const QString &token)
{
    return token.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(token);
}

inline QJsonObject serializeDockObjectIdentities(const DockObjectIdentities &objects)
{
    QJsonObject json;
    json[QStringLiteral("view")] = serializeOptionalObjectToken(objects.view);
    json[QStringLiteral("containment")] = serializeOptionalObjectToken(objects.containment);
    json[QStringLiteral("configuration")] = serializeOptionalObjectToken(objects.configuration);
    json[QStringLiteral("layout")] = serializeOptionalObjectToken(objects.layout);
    json[QStringLiteral("layoutController")] = serializeOptionalObjectToken(objects.layoutController);
    json[QStringLiteral("geometryController")] = serializeOptionalObjectToken(objects.geometryController);
    json[QStringLiteral("editController")] = serializeOptionalObjectToken(objects.editController);
    json[QStringLiteral("configWindow")] = serializeOptionalObjectToken(objects.configWindow);
    return json;
}

inline QList<DockSystemViewRecord> canonicalizeDockSystemViews(QList<DockSystemViewRecord> records)
{
    QHash<uint, QList<uint>> membersByRoot;

    for (const auto &record : records) {
        if (record.relationship == DockRelationship::LinkedMember) {
            membersByRoot[record.logicalDockId].append(record.persistentDockId);
        }
    }

    for (auto it = membersByRoot.begin(); it != membersByRoot.end(); ++it) {
        std::sort(it.value().begin(), it.value().end());
    }

    for (auto &record : records) {
        if (record.relationship == DockRelationship::LinkedRoot) {
            record.linkedDockIds = membersByRoot.value(record.persistentDockId);
        } else {
            record.linkedDockIds.clear();
        }
    }

    std::sort(records.begin(), records.end(), [](const auto &left, const auto &right) {
        if (left.persistentDockId != right.persistentDockId) {
            return left.persistentDockId < right.persistentDockId;
        }

        return left.runtimeViewId < right.runtimeViewId;
    });
    return records;
}

inline QJsonObject serializeDockSystemViewRecord(const DockSystemViewRecord &record,
                                                 bool globalConfigureAppletsMode)
{
    QJsonObject json;
    json[QStringLiteral("runtimeViewId")] = QString::number(record.runtimeViewId);
    json[QStringLiteral("persistentDockId")] = static_cast<qint64>(record.persistentDockId);
    json[QStringLiteral("logicalDockId")] = static_cast<qint64>(record.logicalDockId);
    json[QStringLiteral("originalDockId")] = record.originalDockId < 0
        ? QJsonValue(QJsonValue::Null) : QJsonValue(record.originalDockId);
    json[QStringLiteral("relationship")] = dockRelationshipName(record.relationship);
    json[QStringLiteral("linkPlacement")] = serializeOptionalLinkPlacement(record.linkPlacement);
    json[QStringLiteral("screensGroup")] = screensGroupName(record.screensGroup);

    QJsonArray linkedDockIds;
    for (const uint id : record.linkedDockIds) {
        linkedDockIds.append(static_cast<qint64>(id));
    }
    json[QStringLiteral("linkedDockIds")] = linkedDockIds;

    json[QStringLiteral("layout")] = record.layout;
    json[QStringLiteral("screenId")] = record.screenId;
    json[QStringLiteral("screen")] = record.screen;
    json[QStringLiteral("onPrimary")] = record.onPrimary;
    json[QStringLiteral("type")] = viewTypeName(record.type);
    json[QStringLiteral("edge")] = edgeName(record.edge);
    json[QStringLiteral("orientation")] = orientationName(record.orientation);
    json[QStringLiteral("alignment")] = alignmentName(record.alignment);
    json[QStringLiteral("maximumLengthRatio")] = record.maximumLengthRatio;
    json[QStringLiteral("offsetRatio")] = record.offsetRatio;

    json[QStringLiteral("configuredIconSize")] = serializeOptionalInt(record.configuredIconSize);
    json[QStringLiteral("effectiveIconSize")] = serializeOptionalInt(record.effectiveIconSize);
    json[QStringLiteral("availablePrimaryLength")] = serializeOptionalInt(record.availablePrimaryLength);
    json[QStringLiteral("normalThickness")] = record.normalThickness;
    json[QStringLiteral("maximumNormalThickness")] = record.maximumNormalThickness;

    json[QStringLiteral("windowGeometry")] = serializeRect(record.windowGeometry);
    json[QStringLiteral("absoluteGeometry")] = serializeRect(record.absoluteGeometry);
    json[QStringLiteral("localGeometry")] = serializeRect(record.localGeometry);
    json[QStringLiteral("screenGeometry")] = serializeRect(record.screenGeometry);
    json[QStringLiteral("canvasGeometry")] = serializeRect(record.canvasGeometry);
    json[QStringLiteral("effectsRect")] = serializeRect(record.effectsRect);
    json[QStringLiteral("appletsLayoutGeometry")] = serializeRect(record.appletsLayoutGeometry);
    json[QStringLiteral("maskRect")] = serializeRect(record.maskRect);
    json[QStringLiteral("inputMask")] = serializeRect(record.inputMask);
    json[QStringLiteral("appliedInputMask")] = serializeRect(record.appliedInputMask);
    json[QStringLiteral("strutsThickness")] = record.strutsThickness;
    json[QStringLiteral("publishedStruts")] = serializeRect(record.publishedStruts);

    json[QStringLiteral("visibilityMode")] = visibilityModeName(record.visibilityMode);
    json[QStringLiteral("isHidden")] = record.isHidden;
    json[QStringLiteral("inStartup")] = record.inStartup;
    json[QStringLiteral("isOffScreen")] = record.isOffScreen;
    json[QStringLiteral("inRelocationAnimation")] = record.inRelocationAnimation;
    json[QStringLiteral("inRelocationShowing")] = record.inRelocationShowing;
    json[QStringLiteral("geometrySettled")] = record.geometrySettled;
    json[QStringLiteral("relocationGeneration")] = QString::number(record.relocationGeneration);
    json[QStringLiteral("appliedRelocationGeneration")] = QString::number(record.appliedRelocationGeneration);
    json[QStringLiteral("inDelete")] = record.inDelete;
    json[QStringLiteral("inReadyState")] = record.inReadyState;
    json[QStringLiteral("editMode")] = record.editMode;
    json[QStringLiteral("effectiveConfigureAppletsMode")] =
        effectiveConfigureAppletsMode(record.editMode, globalConfigureAppletsMode);
    json[QStringLiteral("settingsWindowShown")] = record.settingsWindowShown;
    json[QStringLiteral("objects")] = serializeDockObjectIdentities(record.objects);
    return json;
}

inline QString serializeDockSystemSnapshot(const DockSystemSnapshot &snapshot)
{
    QJsonObject json;
    json[QStringLiteral("schemaVersion")] = DockSystemSnapshot::SchemaVersion;
    json[QStringLiteral("snapshotSequence")] = QString::number(snapshot.snapshotSequence);
    json[QStringLiteral("globalConfigureAppletsMode")] = snapshot.globalConfigureAppletsMode;

    QJsonObject stacking;
    stacking[QStringLiteral("available")] = snapshot.stacking.available;
    stacking[QStringLiteral("reason")] = snapshot.stacking.reason;
    json[QStringLiteral("stacking")] = stacking;

    QJsonArray views;
    const auto canonicalViews = canonicalizeDockSystemViews(snapshot.views);
    for (const auto &record : canonicalViews) {
        views.append(serializeDockSystemViewRecord(record, snapshot.globalConfigureAppletsMode));
    }
    json[QStringLiteral("views")] = views;

    return QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
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
ViewRecord collectViewRecord(const Latte::View *view, bool globalConfigureAppletsMode);

//! serialize a set of live views for the viewsData() D-Bus read
QString collectViewsData(const QList<Latte::View *> &views, bool globalConfigureAppletsMode);

//! Snapshot every live dock synchronously from its current runtime authorities.
//! The caller owns the process-local sequence and identity registry so tokens
//! remain comparable across D-Bus calls without adding identity state to View.
std::optional<DockSystemSnapshot> collectDockSystemSnapshot(
    const QList<Latte::View *> &views,
    bool globalConfigureAppletsMode,
    quint64 snapshotSequence,
    RuntimeObjectIdentityRegistry *identities);

//! Collect and compact-serialize dockSystemData() in one synchronous call.
QString collectDockSystemData(const QList<Latte::View *> &views,
                              bool globalConfigureAppletsMode,
                              quint64 snapshotSequence,
                              RuntimeObjectIdentityRegistry *identities);

//! serialize one live view's applets for the viewAppletsData() D-Bus read
QString collectAppletsData(const Latte::View *view);

//! serialize one live view's windows-tracker facts for the trackerData()
//! D-Bus read
QString collectTrackerData(const Latte::View *view);

//! serialize one live view's latte-tasks items for the viewTasksData()
//! D-Bus read
QString collectTasksData(const Latte::View *view);

//! Serialize the newest TaskMouseArea middle-click dispatch across a view's
//! tasks applets. A null view is an unknown containment boundary and reports
//! "{}" with a warning; a view with no recorded event reports "{}" quietly.
QString collectMiddleClickDispatchData(const Latte::View *view, uint containmentId);

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
