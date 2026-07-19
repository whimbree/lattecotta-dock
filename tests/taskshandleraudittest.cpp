/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The Tasks page's real-handler WIRING audit (CL-5, the tasks-page settings
// audit, in docs/edit-mode-settings-audit-plan.md, controls 91-121). Same
// pattern as settingswiringharnesstest / behaviorwiringaudittest / effects-
// handleraudittest: each control's handler body runs as a QML fragment against a
// stub QQmlPropertyMap standing in for the Tasks plasmoid's configuration map,
// then the Tier B diff predicates (tests/units/configsnapshotdiff.h) decide
// which keys the handler changed. It pins P1 (the control applies) and P2 (it
// writes the RIGHT key and no stray neighbour) for every Tasks control,
// deterministically.
//
// D10 CONTEXT (the Tasks config page renders but did not apply its settings -
// the inherited half-finished upstream feature; docs/known-defects.md D10). The
// disposition is WIRE IT UP (audit plan section 8). This port already IS wired,
// two ways, and this test PINS both against regression rather than adding new
// wiring:
//
//   1. Config-access (the ng eabf7c89a root cause #1). ng found the Tasks page
//      wrote a SEPARATE KDeclarative::ConfigPropertyMap, invisible to the
//      plasmoid's own plasmoid.configuration.* bindings. This port never had
//      that split: TasksConfig.qml writes tasks.plasmoid.configuration.<key>,
//      and PlasmaQuick::AppletQuickItem exposes
//      `Q_PROPERTY(QObject *plasmoid READ applet CONSTANT)`, so `tasks.plasmoid`
//      IS the Plasma::Applet and `.configuration` IS Applet::configuration() -
//      the one KConfigPropertyMap the running plasmoid reads AND the object the
//      appletConfigData() D-Bus readback reports. One map, three views. The live
//      leg (tests/e2e/034-tasks-config-apply.sh) proves that identity on a real
//      dock: it seeds non-default tasks values, restarts, and reads them back
//      through appletConfigData (the applet's live map). This test pins the
//      other half - that each control's handler writes exactly the labelled key
//      into that map.
//
//   2. Action-dispatch completeness (the ng root cause #2). ng shipped a config
//      offering 9 TaskAction values while the click handler covered 3, so an
//      unhandled pick silently did nothing. This port routes every click/scroll
//      action through code/TaskActions.js as the single source of truth, pinned
//      by tests/qml/tst_taskactions.qml (Phase 6, e97548104). That is a distinct
//      concern (does the CHOSEN action have a handler) from this file's (does the
//      control write the RIGHT config key); the two together close D10's
//      "applies at all" question.
//
// So NO production code changes here: the wiring exists and is correct; CL-5 is
// the driven proof and the regression pins.

// Qt
#include <QJsonObject>
#include <QJsonValue>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QScopedPointer>
#include <QTest>
#include <QVariant>

#include "configsnapshotdiff.h"

//! the Tasks plasmoid's TaskAction / TaskScrollAction enums, the real source the
//! action-combo mappings below are transcribed from. Included (not moc-linked)
//! so the enum values are compile-time constants the static_asserts pin against;
//! if plasmoid/plugin/types.h renumbers, the asserts break the build here.
#include "types.h"

using namespace Latte::AuditHarness;
using LatteTasks = Latte::Tasks::Types;

namespace {

//! LatteCore.Types.LaunchersGroup, inlined with its name (the stub engine does
//! not import the QML module; behaviorwiringaudittest uses the same technique for
//! the Alignment enum). Sourced from declarativeimports/coretypes.h.in - keep in
//! sync if that enum is renumbered.
constexpr int kUniqueLaunchers = 0; // LatteCore.Types.UniqueLaunchers
constexpr int kLayoutLaunchers = 1; // LatteCore.Types.LayoutLaunchers
constexpr int kGlobalLaunchers = 2; // LatteCore.Types.GlobalLaunchers

//! The Actions combos map their currentIndex onto a TaskAction enum value (and
//! back) through a switch, so the stored config value is NOT the row index for
//! leftClickAction and hoverAction. These pin the exact enum values the switches
//! use, straight from types.h, so a renumber cannot silently desync the test
//! from the UI it transcribes.
static_assert(LatteTasks::PresentWindows == 6, "TaskAction::PresentWindows");
static_assert(LatteTasks::CycleThroughTasks == 4, "TaskAction::CycleThroughTasks");
static_assert(LatteTasks::PreviewWindows == 7, "TaskAction::PreviewWindows");
static_assert(LatteTasks::NoneAction == 0, "TaskAction::NoneAction");
static_assert(LatteTasks::HighlightWindows == 8, "TaskAction::HighlightWindows");
static_assert(LatteTasks::PreviewAndHighlightWindows == 9, "TaskAction::PreviewAndHighlightWindows");
static_assert(LatteTasks::ToggleGrouping == 5, "TaskAction::ToggleGrouping");
static_assert(LatteTasks::ScrollToggleMinimized == 2, "TaskScrollAction::ScrollToggleMinimized");

//! the leftClickAction combo's index<->enum bijection (TasksConfig.qml
//! leftClickAction: read switch 551-562, write switch 564-576). Row 0/1/2 map to
//! PresentWindows/CycleThroughTasks/PreviewWindows.
int leftClickEnumForIndex(int index)
{
    switch (index) {
    case 0: return LatteTasks::PresentWindows;
    case 1: return LatteTasks::CycleThroughTasks;
    case 2: return LatteTasks::PreviewWindows;
    }
    return LatteTasks::PresentWindows; // the combo's own default
}
int leftClickIndexForEnum(int stored)
{
    switch (stored) {
    case LatteTasks::PresentWindows: return 0;
    case LatteTasks::CycleThroughTasks: return 1;
    case LatteTasks::PreviewWindows: return 2;
    }
    return 0;
}

//! the hoverAction combo's index<->enum bijection (TasksConfig.qml hoverAction:
//! read switch 614-627, write switch 629-644). Row 0/1/2/3 map to
//! NoneAction/PreviewWindows/HighlightWindows/PreviewAndHighlightWindows.
int hoverEnumForIndex(int index)
{
    switch (index) {
    case 0: return LatteTasks::NoneAction;
    case 1: return LatteTasks::PreviewWindows;
    case 2: return LatteTasks::HighlightWindows;
    case 3: return LatteTasks::PreviewAndHighlightWindows;
    }
    return LatteTasks::NoneAction;
}
int hoverIndexForEnum(int stored)
{
    switch (stored) {
    case LatteTasks::NoneAction: return 0;
    case LatteTasks::PreviewWindows: return 1;
    case LatteTasks::HighlightWindows: return 2;
    case LatteTasks::PreviewAndHighlightWindows: return 3;
    }
    return 0;
}

} // namespace

class TasksHandlerAuditTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    //! AU-5b Badges (91-95) + Interaction (96-99) + Filters (100-105): every
    //! Tasks checkbox is a plain toggle writing ONLY its own tcfg key.
    void badgeCheckboxesEachWriteOnlyTheirOwnKey();
    void interactionCheckboxesEachWriteOnlyTheirOwnKey();
    void filterCheckboxesEachWriteOnlyTheirOwnKey();

    //! AU-5c Animations (106-110): the five animation checkboxes, same shape.
    void animationCheckboxesEachWriteOnlyTheirOwnKey();

    //! AU-5c Launchers (111-113): the Unique/Layout/Global buttons write ONLY
    //! launchersGroup, at the pressed button's group value.
    void launcherGroupButtonsWriteOnlyLaunchersGroup_data();
    void launcherGroupButtonsWriteOnlyLaunchersGroup();

    //! AU-5c Scrolling (114-116): the enable switch is a toggle; the two combos
    //! (manual index==enum, auto bool) apply their own key and round-trip identity.
    void scrollingEnableSwitchWritesOnlyScrollTasksEnabled();
    void manualScrollComboAppliesAndRoundTripsIdentity();
    void autoScrollComboAppliesAndRoundTripsIdentity();

    //! AU-5c Actions (117-121): the identity combos (index==enum) and the two
    //! mapped combos (leftClickAction/hoverAction switch index<->enum).
    void identityActionCombosApplyAndRoundTripIdentity_data();
    void identityActionCombosApplyAndRoundTripIdentity();
    void leftClickComboAppliesAndRoundTripsIdentity();
    void hoverComboAppliesAndRoundTripsIdentity();

private:
    static void seed(QQmlPropertyMap &config,
                     std::initializer_list<std::pair<QString, QVariant>> pairs);

    //! run a handler body against the stub map (exposed as `configuration`,
    //! mirroring tasks.plasmoid.configuration). Returns the QML error string
    //! (empty on success) so a malformed fragment - or a write the map rejects -
    //! fails loudly rather than reading as a silent no-op.
    static QString runHandler(QQmlPropertyMap &config, const QString &handlerBody);

    //! the stub map's values as a "config" JSON object, the shape
    //! appletConfigData()'s config carries, so the diff predicates see the same
    //! thing live and stubbed.
    static QJsonObject snapshot(const QQmlPropertyMap &config);

    //! shared body for the plain-toggle groups (badges/interaction/filters/
    //! animations): every key is a checkbox whose handler is
    //! `configuration.X = !configuration.X`; flipping one must apply exactly its
    //! own key with no neighbour touched.
    void assertToggleCheckboxesWriteOnlyOwnKey(const QStringList &keys);
};

void TasksHandlerAuditTest::seed(QQmlPropertyMap &config,
                                 std::initializer_list<std::pair<QString, QVariant>> pairs)
{
    for (const auto &pair : pairs) {
        config.insert(pair.first, pair.second);
    }
}

QString TasksHandlerAuditTest::runHandler(QQmlPropertyMap &config, const QString &handlerBody)
{
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("configuration"), &config);

    QQmlComponent component(&engine);
    const QString qml = QStringLiteral(
        "import QtQml\n"
        "QtObject { Component.onCompleted: { %1 } }\n").arg(handlerBody);
    component.setData(qml.toUtf8(), QUrl());

    if (component.isError()) {
        return component.errorString();
    }

    QScopedPointer<QObject> root(component.create());

    if (!root) {
        return component.errorString().isEmpty()
            ? QStringLiteral("handler fragment produced no object") : component.errorString();
    }

    return QString();
}

QJsonObject TasksHandlerAuditTest::snapshot(const QQmlPropertyMap &config)
{
    QJsonObject json;
    const QStringList keys = config.keys();

    for (const QString &key : keys) {
        const QVariant value = config.value(key);
        const QJsonValue direct = QJsonValue::fromVariant(value);

        if (direct.type() != QJsonValue::Null && direct.type() != QJsonValue::Undefined) {
            json[key] = direct;
        } else {
            json[key] = value.toString();
        }
    }

    return json;
}

void TasksHandlerAuditTest::assertToggleCheckboxesWriteOnlyOwnKey(const QStringList &keys)
{
    for (const QString &key : keys) {
        //! a full group with every checkbox key present (the schema shape), all
        //! false; the neighbours must stay put when one is toggled
        QQmlPropertyMap config;
        for (const QString &k : keys) {
            config.insert(k, false);
        }

        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config,
                 QStringLiteral("configuration.%1 = !configuration.%1;").arg(key)), QString());
        const QJsonObject after = snapshot(config);

        QVERIFY2(controlApplies(before, after, key),
                 qPrintable(QStringLiteral("Tasks checkbox '%1' must apply its own key").arg(key)));
        QVERIFY2(onlyExpectedKeysChanged(before, after, {key}),
                 qPrintable(QStringLiteral("Tasks checkbox '%1' must write ONLY its own key").arg(key)));
        QVERIFY(valueReflects(after, key, true));
    }
}

// ============================ AU-5b Badges/Interaction/Filters =============

//! controls 91-95 (TasksConfig.qml Badges block): each badge checkbox flips only
//! its own tcfg key.
void TasksHandlerAuditTest::badgeCheckboxesEachWriteOnlyTheirOwnKey()
{
    assertToggleCheckboxesWriteOnlyOwnKey({
        QStringLiteral("showInfoBadge"),
        QStringLiteral("showProgressBadge"),
        QStringLiteral("showAudioBadge"),
        QStringLiteral("infoBadgeProminentColorEnabled"),
        QStringLiteral("audioBadgeActionsEnabled"),
    });
}

//! controls 96-99 (TasksConfig.qml Interaction block).
void TasksHandlerAuditTest::interactionCheckboxesEachWriteOnlyTheirOwnKey()
{
    assertToggleCheckboxesWriteOnlyOwnKey({
        QStringLiteral("isPreferredForDroppedLaunchers"),
        QStringLiteral("showWindowActions"),
        QStringLiteral("previewWindowAsPopup"),
        QStringLiteral("isPreferredForPositionShortcuts"),
    });
}

//! controls 100-105 (TasksConfig.qml Filters block).
void TasksHandlerAuditTest::filterCheckboxesEachWriteOnlyTheirOwnKey()
{
    assertToggleCheckboxesWriteOnlyOwnKey({
        QStringLiteral("showOnlyCurrentScreen"),
        QStringLiteral("showOnlyCurrentDesktop"),
        QStringLiteral("showOnlyCurrentActivity"),
        QStringLiteral("showWindowsOnlyFromLaunchers"),
        QStringLiteral("hideAllTasks"),
        QStringLiteral("groupTasksByDefault"),
    });
}

// ============================ AU-5c Animations =============================

//! controls 106-110 (TasksConfig.qml Animations block).
void TasksHandlerAuditTest::animationCheckboxesEachWriteOnlyTheirOwnKey()
{
    assertToggleCheckboxesWriteOnlyOwnKey({
        QStringLiteral("animationLauncherBouncing"),
        QStringLiteral("animationWindowInAttention"),
        QStringLiteral("animationNewWindowSliding"),
        QStringLiteral("animationWindowAddedInGroup"),
        QStringLiteral("animationWindowRemovedFromGroup"),
    });
}

// ============================ AU-5c Launchers ==============================

//! controls 111-113 (TasksConfig.qml Launchers group). Each button's handler is
//! `onPressedChanged: if (pressed) configuration.launchersGroup = group`, where
//! `group` is the button's LatteCore.Types.LaunchersGroup value. Picking one
//! writes only launchersGroup, at that value.
void TasksHandlerAuditTest::launcherGroupButtonsWriteOnlyLaunchersGroup_data()
{
    QTest::addColumn<int>("group");
    QTest::newRow("Unique Group (control 111)") << kUniqueLaunchers;
    QTest::newRow("Layout Group (control 112)") << kLayoutLaunchers;
    QTest::newRow("Global Group (control 113)") << kGlobalLaunchers;
}

void TasksHandlerAuditTest::launcherGroupButtonsWriteOnlyLaunchersGroup()
{
    QFETCH(int, group);

    QQmlPropertyMap config;
    //! start at a different group so every case is a real change
    seed(config, {{QStringLiteral("launchersGroup"),
                   (group == kUniqueLaunchers) ? kGlobalLaunchers : kUniqueLaunchers}});

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config,
        QStringLiteral("configuration.launchersGroup = %1;").arg(group)), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(controlApplies(before, after, QStringLiteral("launchersGroup")));
    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("launchersGroup")}));
    QVERIFY(valueReflects(after, QStringLiteral("launchersGroup"), group));
}

// ============================ AU-5c Scrolling =============================

//! control 114 (TasksConfig.qml scrollingHeader.onPressed): the Scrolling switch
//! flips only scrollTasksEnabled.
void TasksHandlerAuditTest::scrollingEnableSwitchWritesOnlyScrollTasksEnabled()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("scrollTasksEnabled"), false},
                  {QStringLiteral("manualScrollTasksType"), 0},
                  {QStringLiteral("autoScrollTasksEnabled"), false}});

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config, QStringLiteral(
        "configuration.scrollTasksEnabled = !configuration.scrollTasksEnabled;")), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(controlApplies(before, after, QStringLiteral("scrollTasksEnabled")));
    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("scrollTasksEnabled")}));
    QVERIFY(valueReflects(after, QStringLiteral("scrollTasksEnabled"), true));
}

//! control 115 (TasksConfig.qml manualScrolling combo, onCurrentIndexChanged 484):
//! `configuration.manualScrollTasksType = currentIndex`. index == enum
//! (ManualScrollType 0/1/2), so it applies its own key and re-firing on the
//! stored value is a no-op (S-d identity).
void TasksHandlerAuditTest::manualScrollComboAppliesAndRoundTripsIdentity()
{
    //! APPLY every row from a distinct start so each transition is exercised
    for (int row = 0; row <= 2; ++row) {
        const int start = (row == 0) ? 2 : 0;
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("manualScrollTasksType"), start},
                      {QStringLiteral("scrollTasksEnabled"), true}});
        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config,
            QStringLiteral("configuration.manualScrollTasksType = %1;").arg(row)), QString());
        const QJsonObject after = snapshot(config);
        QVERIFY2(onlyExpectedKeysChanged(before, after, {QStringLiteral("manualScrollTasksType")}),
                 qPrintable(QStringLiteral("manualScrollTasksType row %1 must write only its key").arg(row)));
        QVERIFY(valueReflects(after, QStringLiteral("manualScrollTasksType"), row));
    }

    //! IDENTITY: re-firing on the stored value writes the same value - no change
    for (int row = 0; row <= 2; ++row) {
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("manualScrollTasksType"), row},
                      {QStringLiteral("scrollTasksEnabled"), true}});
        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config,
            QStringLiteral("configuration.manualScrollTasksType = %1;").arg(row)), QString());
        const QJsonObject after = snapshot(config);
        QVERIFY2(changedConfigKeys(before, after).isEmpty(),
                 qPrintable(QStringLiteral("manualScrollTasksType row %1 re-fire must be a no-op").arg(row)));
    }
}

//! control 116 (TasksConfig.qml autoScrolling combo, onCurrentIndexChanged
//! 503-509): index 0 -> autoScrollTasksEnabled=false, index 1 -> true. The stored
//! value is a BOOL, the combo index an int; the round-trip (config bool ->
//! currentIndex -> handler bool) must be identity.
void TasksHandlerAuditTest::autoScrollComboAppliesAndRoundTripsIdentity()
{
    //! the real handler body, currentIndex injected as a local
    const QString autoSwitch = QStringLiteral(
        "if (currentIndex === 0) { configuration.autoScrollTasksEnabled = false; }\n"
        "else { configuration.autoScrollTasksEnabled = true; }\n");

    //! APPLY: stored false, user picks row 1 -> writes true, only its key
    {
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("autoScrollTasksEnabled"), false},
                      {QStringLiteral("scrollTasksEnabled"), true}});
        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config, QStringLiteral("var currentIndex = 1;\n") + autoSwitch), QString());
        const QJsonObject after = snapshot(config);
        QVERIFY(controlApplies(before, after, QStringLiteral("autoScrollTasksEnabled")));
        QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("autoScrollTasksEnabled")}));
        QVERIFY(valueReflects(after, QStringLiteral("autoScrollTasksEnabled"), true));
    }

    //! IDENTITY both ways: stored true re-firing at index 1, stored false at
    //! index 0, each a no-op (the index the currentIndex binding re-derives from
    //! the stored bool feeds the same value back)
    for (bool stored : {false, true}) {
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("autoScrollTasksEnabled"), stored},
                      {QStringLiteral("scrollTasksEnabled"), true}});
        const int index = stored ? 1 : 0;
        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config,
            QStringLiteral("var currentIndex = %1;\n").arg(index) + autoSwitch), QString());
        const QJsonObject after = snapshot(config);
        QVERIFY2(changedConfigKeys(before, after).isEmpty(),
                 "autoScrollTasksEnabled re-fire on the stored value must be a no-op");
    }
}

// ============================ AU-5c Actions ===============================

//! controls 118, 120, 121 (and the two modifier combos): the identity combos
//! whose currentIndex == the stored enum value, so `onCurrentIndexChanged:
//! configuration.<key> = currentIndex`. Each applies only its own key and
//! re-fires as identity across its whole model range (S-d).
void TasksHandlerAuditTest::identityActionCombosApplyAndRoundTripIdentity_data()
{
    QTest::addColumn<QString>("key");
    QTest::addColumn<int>("maxIndex");
    //! middleClickAction (control 118): 6 rows, index == TaskAction 0..5
    QTest::newRow("middleClickAction") << QStringLiteral("middleClickAction") << 5;
    //! taskScrollAction (control 120, the Wheel combo): 3 rows, TaskScrollAction 0..2
    QTest::newRow("taskScrollAction") << QStringLiteral("taskScrollAction") << 2;
    //! modifierClickAction (control 121): 6 rows, index == TaskAction 0..5
    QTest::newRow("modifierClickAction") << QStringLiteral("modifierClickAction") << 5;
    //! the modifier + modifierClick selectors (control 121 cluster), index == enum
    QTest::newRow("modifier") << QStringLiteral("modifier") << 3;         // Shift..Meta
    QTest::newRow("modifierClick") << QStringLiteral("modifierClick") << 2; // Left/Middle/Right
}

void TasksHandlerAuditTest::identityActionCombosApplyAndRoundTripIdentity()
{
    QFETCH(QString, key);
    QFETCH(int, maxIndex);

    //! APPLY every row from a distinct start (the opposite end) so each write is
    //! a real change
    for (int row = 0; row <= maxIndex; ++row) {
        const int start = (row == 0) ? maxIndex : 0;
        QQmlPropertyMap config;
        seed(config, {{key, start}});
        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config,
            QStringLiteral("configuration.%1 = %2;").arg(key).arg(row)), QString());
        const QJsonObject after = snapshot(config);
        QVERIFY2(onlyExpectedKeysChanged(before, after, {key}),
                 qPrintable(QStringLiteral("%1 row %2 must write only its key").arg(key).arg(row)));
        QVERIFY(valueReflects(after, key, row));
    }

    //! IDENTITY: re-firing on the stored value is a no-op across the range
    for (int row = 0; row <= maxIndex; ++row) {
        QQmlPropertyMap config;
        seed(config, {{key, row}});
        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config,
            QStringLiteral("configuration.%1 = %2;").arg(key).arg(row)), QString());
        const QJsonObject after = snapshot(config);
        QVERIFY2(changedConfigKeys(before, after).isEmpty(),
                 qPrintable(QStringLiteral("%1 row %2 re-fire must be a no-op").arg(key).arg(row)));
    }
}

//! control 117 (TasksConfig.qml leftClickAction combo). The MAPPED case: the
//! currentIndex reads leftClickAction through a switch (enum -> row) and the
//! handler writes it back through the inverse switch (row -> enum), so the stored
//! value (6/4/7) is NOT the row index. P1+P2: picking a row writes the mapped
//! enum, only leftClickAction. IDENTITY (S-d): an external config change fires
//! onCurrentIndexChanged with the re-derived index, which must write the SAME
//! enum back - no oscillation across a combo whose index != stored value.
void TasksHandlerAuditTest::leftClickComboAppliesAndRoundTripsIdentity()
{
    //! the real write switch (row -> enum), currentIndex injected as a local
    const QString writeSwitch = QStringLiteral(
        "switch (currentIndex) {\n"
        "case 0: configuration.leftClickAction = %1; break;\n"  // PresentWindows
        "case 1: configuration.leftClickAction = %2; break;\n"  // CycleThroughTasks
        "case 2: configuration.leftClickAction = %3; break;\n"  // PreviewWindows
        "}\n")
        .arg(LatteTasks::PresentWindows)
        .arg(LatteTasks::CycleThroughTasks)
        .arg(LatteTasks::PreviewWindows);

    //! APPLY: for each offered row, from a distinct stored start, the write lands
    //! the mapped enum and nothing else
    for (int row = 0; row <= 2; ++row) {
        const int startEnum = leftClickEnumForIndex((row + 1) % 3); // a different offered value
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("leftClickAction"), startEnum}});
        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config,
            QStringLiteral("var currentIndex = %1;\n").arg(row) + writeSwitch), QString());
        const QJsonObject after = snapshot(config);
        QVERIFY(controlApplies(before, after, QStringLiteral("leftClickAction")));
        QVERIFY2(onlyExpectedKeysChanged(before, after, {QStringLiteral("leftClickAction")}),
                 qPrintable(QStringLiteral("leftClickAction row %1 must write only its key").arg(row)));
        QVERIFY(valueReflects(after, QStringLiteral("leftClickAction"), leftClickEnumForIndex(row)));
    }

    //! IDENTITY: stored enum -> re-derived index -> write switch -> same enum
    for (int stored : {LatteTasks::PresentWindows, LatteTasks::CycleThroughTasks, LatteTasks::PreviewWindows}) {
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("leftClickAction"), stored}});
        const int reDerivedIndex = leftClickIndexForEnum(stored);
        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config,
            QStringLiteral("var currentIndex = %1;\n").arg(reDerivedIndex) + writeSwitch), QString());
        const QJsonObject after = snapshot(config);
        QVERIFY2(changedConfigKeys(before, after).isEmpty(),
                 qPrintable(QStringLiteral("leftClickAction re-fire on stored enum %1 must be a no-op").arg(stored)));
    }
}

//! control 119 (TasksConfig.qml hoverAction combo). Same MAPPED shape as
//! leftClickAction, over the 4-row Preview/Highlight set: rows 0/1/2/3 ->
//! NoneAction/PreviewWindows/HighlightWindows/PreviewAndHighlightWindows.
void TasksHandlerAuditTest::hoverComboAppliesAndRoundTripsIdentity()
{
    const QString writeSwitch = QStringLiteral(
        "switch (currentIndex) {\n"
        "case 0: configuration.hoverAction = %1; break;\n"  // NoneAction
        "case 1: configuration.hoverAction = %2; break;\n"  // PreviewWindows
        "case 2: configuration.hoverAction = %3; break;\n"  // HighlightWindows
        "case 3: configuration.hoverAction = %4; break;\n"  // PreviewAndHighlightWindows
        "}\n")
        .arg(LatteTasks::NoneAction)
        .arg(LatteTasks::PreviewWindows)
        .arg(LatteTasks::HighlightWindows)
        .arg(LatteTasks::PreviewAndHighlightWindows);

    for (int row = 0; row <= 3; ++row) {
        const int startEnum = hoverEnumForIndex((row + 1) % 4);
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("hoverAction"), startEnum}});
        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config,
            QStringLiteral("var currentIndex = %1;\n").arg(row) + writeSwitch), QString());
        const QJsonObject after = snapshot(config);
        QVERIFY(controlApplies(before, after, QStringLiteral("hoverAction")));
        QVERIFY2(onlyExpectedKeysChanged(before, after, {QStringLiteral("hoverAction")}),
                 qPrintable(QStringLiteral("hoverAction row %1 must write only its key").arg(row)));
        QVERIFY(valueReflects(after, QStringLiteral("hoverAction"), hoverEnumForIndex(row)));
    }

    for (int stored : {LatteTasks::NoneAction, LatteTasks::PreviewWindows,
                       LatteTasks::HighlightWindows, LatteTasks::PreviewAndHighlightWindows}) {
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("hoverAction"), stored}});
        const int reDerivedIndex = hoverIndexForEnum(stored);
        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config,
            QStringLiteral("var currentIndex = %1;\n").arg(reDerivedIndex) + writeSwitch), QString());
        const QJsonObject after = snapshot(config);
        QVERIFY2(changedConfigKeys(before, after).isEmpty(),
                 qPrintable(QStringLiteral("hoverAction re-fire on stored enum %1 must be a no-op").arg(stored)));
    }
}

QTEST_GUILESS_MAIN(TasksHandlerAuditTest)

#include "taskshandleraudittest.moc"
