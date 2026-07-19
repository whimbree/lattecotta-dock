/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The Behavior page's real-handler WIRING audit (CL-3 in
// docs/tracking/edit-mode-settings-audit-plan.md). Same pattern as
// settingswiringharnesstest / lengthhandleraudittest: a control's handler body
// runs as a QML fragment against a stub QQmlPropertyMap standing in for
// plasmoid.configuration, then the Tier B diff predicates
// (tests/units/configsnapshotdiff.h) decide which keys the handler changed.
//
// The Behavior page mixes four writer classes (catalog section 5.3):
//   - cfg   : the Actions/Environment/Items CHECKBOXES and the two action
//             COMBOS write plasmoid.configuration.<key> - answered here,
//             deterministically, because "which config key" is exactly the
//             stub-map question (AU-3c, S-d).
//   - pos   : Location/Alignment/Screen call positioner.setNextLocation(...)
//   - vis   : Visibility mode + Delay timers write latteView.visibility.<prop>
//   - view  : byPassWM / isPreferredForShortcuts write latteView.<prop>
// The pos/vis/view controls write NO config key, so their live effect is
// verified in the nested vehicle (tests/e2e/032-behavior-live-reflect.sh reads
// viewsData edge/alignment/screen/visibilityMode and the viewConfigData "view"
// half). What THIS file pins deterministically for those is the P3 half the
// plan asks for: the buttons' `checked` binding REFLECTS the live view state
// and re-tracks when it changes (a pure reflection, unlike D16's clobbered
// slider), driven through a real QML binding so a broken reflection is caught.
//
// AU-3d (the S-a finding) is the sharp one: TypeSelection.qml's Dock/Panel
// preset writes plasmoid.configuration.solidPanel and .colorizeTransparentPanels,
// and NEITHER key exists in containment/package/contents/config/main.xml nor is
// read anywhere in the tree (the only "solidPanel*" symbols are the
// differently-named BackgroundStateResolver::solidPanelForced concept, fed by
// the real solidBackgroundForMaximized key). Whatever a write to such a key does
// on the running map, no subsystem consumes it: it is a DEAD KEY. That verdict
// rests on two STATIC facts (schema-absent + zero readers), not on a runtime
// probe, so the harness cannot "prove it lands nowhere" - and must not pretend
// to. A plain QQmlPropertyMap does NOT enforce a schema: a QML write to an
// unheld key CREATES it (verified here), unlike the assumption that seeding is
// mandatory. So what the harness pins deterministically is the P2-violation
// SHAPE: the real preset body writes solidPanel and colorizeTransparentPanels
// IN ADDITION to its schema keys, i.e. it touches two keys beyond the labelled
// set. The live half (tests/e2e/032, viewConfigData over the real
// KConfigPropertyMap) is where whether the real map even keeps a schema-absent
// key is observed; the "nothing reads them" half is the reported grep evidence.

// Qt
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QScopedPointer>
#include <QTest>
#include <QVariant>

#include "configsnapshotdiff.h"

using namespace Latte::AuditHarness;

//! LatteCore.Types / LatteContainment.Types enum values used verbatim below.
//! The stub engine does not import those QML modules, so the numeric values are
//! inlined with the name they stand for (the same technique lengthhandleraudittest
//! uses for `alignment`). Sourced from declarativeimports/coretypes.h.in
//! (Alignment) and containment/plugin/types.h (ActiveWindowFilterFlags,
//! ScrollAction) - keep in sync if either enum is renumbered.
namespace {
constexpr int kAlignCenter = 0;   // LatteCore.Types.Center
constexpr int kAlignJustify = 10; // LatteCore.Types.Justify

constexpr int kActiveInCurrentScreen = 0; // LatteContainment.Types.ActiveInCurrentScreen
constexpr int kActiveFromAllScreens = 1;  // LatteContainment.Types.ActiveFromAllScreens

constexpr int kScrollNone = 0;            // LatteContainment.Types.ScrollNone
constexpr int kScrollToggleMinimized = 4; // LatteContainment.Types.ScrollToggleMinimized
}

//! a live C++ property source with a NOTIFY, standing in for the QObject-backed
//! values the Behavior buttons reflect: plasmoid.location and
//! latteView.visibility.mode. A QQmlPropertyMap models the config-backed
//! alignment source; this models the two that are real Q_PROPERTYs, so both
//! reflection paths (config-map valueChanged and Q_PROPERTY NOTIFY) are exercised.
class LiveIntSource : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int value READ value WRITE setValue NOTIFY valueChanged)

public:
    using QObject::QObject;

    int value() const { return m_value; }
    void setValue(int v)
    {
        if (m_value == v) {
            return;
        }
        m_value = v;
        Q_EMIT valueChanged();
    }

Q_SIGNALS:
    void valueChanged();

private:
    int m_value = 0;
};

class BehaviorWiringAuditTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    //! AU-3d / S-a: the TypeSelection dead-key finding
    void solidPanelDeadKeyWriteIsLiveButSchemaAbsent();
    void colorizeTransparentPanelsDeadKeyWriteIsLiveButSchemaAbsent();
    void dockPresetWritesSchemaKeysPlusTheTwoDeadKeys();
    void panelPresetWritesSchemaKeysPlusTheTwoDeadKeys();

    //! AU-3c: the Actions/Environment/Items checkboxes (cfg) write only their key
    void actionCheckboxesEachWriteOnlyTheirOwnKey();

    //! AU-3c / S-d: the two action combos apply and round-trip identity
    void activeWindowFilterComboAppliesAndRoundTripsIdentity();
    void scrollActionComboAppliesAndRoundTripsIdentity();

    //! AU-3a / AU-3b P3: the buttons' `checked` reflects the live view state
    void locationButtonCheckedReflectsLiveLocation();
    void alignmentButtonCheckedReflectsConfigAlignment();
    void visibilityButtonCheckedReflectsLiveMode();

private:
    static void seed(QQmlPropertyMap &config,
                     std::initializer_list<std::pair<QString, QVariant>> pairs);

    //! run a handler body against the stub map (exposed as `configuration`,
    //! mirroring plasmoid.configuration). Returns the QML error string (empty on
    //! success) so a malformed fragment - or a write the map rejects with an
    //! error - fails loudly instead of reading as a silent no-op.
    static QString runHandler(QQmlPropertyMap &config, const QString &handlerBody);

    //! the stub map's values as a "config" JSON object, the same shape
    //! viewConfigData()'s "config" carries (dbusreports.h), so the diff
    //! predicates see the same thing live and stubbed.
    static QJsonObject snapshot(const QQmlPropertyMap &config);

    //! evaluate a `checked`-style reflection binding once its source has been
    //! set, returning the bound bool. `sourceExpr` is the binding body, e.g.
    //! "location.value === 3".
    static bool evalCheckedBinding(QObject *source, const QString &sourceName,
                                   const QString &sourceExpr, QString *err);
};

void BehaviorWiringAuditTest::seed(QQmlPropertyMap &config,
                                   std::initializer_list<std::pair<QString, QVariant>> pairs)
{
    for (const auto &pair : pairs) {
        config.insert(pair.first, pair.second);
    }
}

QString BehaviorWiringAuditTest::runHandler(QQmlPropertyMap &config, const QString &handlerBody)
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

QJsonObject BehaviorWiringAuditTest::snapshot(const QQmlPropertyMap &config)
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

bool BehaviorWiringAuditTest::evalCheckedBinding(QObject *source, const QString &sourceName,
                                                 const QString &sourceExpr, QString *err)
{
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(sourceName, source);

    QQmlComponent component(&engine);
    const QString qml = QStringLiteral(
        "import QtQml\n"
        "QtObject { property bool checked: %1 }\n").arg(sourceExpr);
    component.setData(qml.toUtf8(), QUrl());

    QObject *root = component.create();
    if (!root) {
        if (err) {
            *err = component.errorString();
        }
        return false;
    }
    root->setParent(&engine); // engine owns it; destroyed with the local engine
    const bool checked = root->property("checked").toBool();
    return checked;
}

// ============================ AU-3d / S-a ==================================

//! S-a, the isolated write: TypeSelection's `plasmoid.configuration.solidPanel =
//! false` is LIVE code that executes and hits the config map (here it even
//! CREATES the key, because a plain QQmlPropertyMap does not enforce a schema).
//! The defect is not that the write fails - it is that solidPanel has no schema
//! entry in containment/package/contents/config/main.xml and no reader anywhere
//! in the tree (grep: the only "solidPanel*" symbols are the differently-named
//! BackgroundStateResolver::solidPanelForced concept, fed by solidBackgroundFor-
//! Maximized). So wherever the value lands, nothing consumes it. This pins the
//! write as live and its key as one the audit reports schema-absent.
void BehaviorWiringAuditTest::solidPanelDeadKeyWriteIsLiveButSchemaAbsent()
{
    QQmlPropertyMap config;
    //! a General group WITHOUT solidPanel (it has no schema entry to seed from)
    seed(config, {{QStringLiteral("useThemePanel"), true}, {QStringLiteral("panelSize"), 5}});

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config, QStringLiteral("configuration.solidPanel = false;")), QString());
    const QJsonObject after = snapshot(config);

    //! the write executed and reached the map (a live write, not dead-stripped)
    QVERIFY2(changedConfigKeys(before, after) == QStringList{QStringLiteral("solidPanel")},
             "the solidPanel write is live: it targets exactly the solidPanel key");
    QVERIFY(valueReflects(after, QStringLiteral("solidPanel"), false));
    //! neighbours are untouched - it is one stray key, not a wider miswrite
    QVERIFY(valueReflects(after, QStringLiteral("useThemePanel"), true));
}

//! S-a, the second dead key. colorizeTransparentPanels is even deader than
//! solidPanel: grep finds ZERO readers anywhere in the tree AND no schema entry.
//! Same shape - a live write to a key nothing defines or reads.
void BehaviorWiringAuditTest::colorizeTransparentPanelsDeadKeyWriteIsLiveButSchemaAbsent()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("useThemePanel"), true}, {QStringLiteral("panelSize"), 5}});

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config, QStringLiteral("configuration.colorizeTransparentPanels = false;")), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY2(changedConfigKeys(before, after) == QStringList{QStringLiteral("colorizeTransparentPanels")},
             "the colorizeTransparentPanels write is live: it targets exactly that key");
    QVERIFY(valueReflects(after, QStringLiteral("colorizeTransparentPanels"), false));
}

//! S-a in situ: the Dock preset's config-write body verbatim (TypeSelection.qml
//! dockTypeButton.onPressedChanged lines 60-84, plasmoid.configuration -> the
//! stub map, LatteCore/LatteContainment enum constants -> their inlined values).
//! The two non-config lines (userRequestedViewType, visibility.mode) are the
//! preset's C++ actions, not config writes, so they are excluded - the S-a
//! question is purely about the config writes. The map is seeded with every
//! SCHEMA key the preset touches, each at a value the preset does NOT write, so
//! every schema write registers as a change; solidPanel / colorizeTransparentPanels
//! are NOT seeded (they have no schema entry). The decisive assertion: the preset
//! changes the schema keys PLUS the two schema-absent dead keys - it writes two
//! keys beyond the labelled set (the P2 violation). The "and nothing reads those
//! two" half is the reported grep evidence; whether the real KConfigPropertyMap
//! keeps them is the live tests/e2e/032 question.
void BehaviorWiringAuditTest::dockPresetWritesSchemaKeysPlusTheTwoDeadKeys()
{
    QQmlPropertyMap config;
    //! seeded values all differ from the Dock preset's writes so every schema
    //! key flips (999 / opposite bool are sentinels no preset write uses)
    seed(config, {
        {QStringLiteral("alignment"), 99},
        {QStringLiteral("useThemePanel"), false},
        {QStringLiteral("panelSize"), 999},
        {QStringLiteral("appletShadowsEnabled"), false},
        {QStringLiteral("zoomLevel"), 999},
        {QStringLiteral("dragActiveWindowEnabled"), true},
        {QStringLiteral("scrollAction"), 9},
        {QStringLiteral("autoSizeEnabled"), false},
        {QStringLiteral("solidBackgroundForMaximized"), true},
        {QStringLiteral("backgroundOnlyOnMaximized"), true},
        {QStringLiteral("disablePanelShadowForMaximized"), true},
        {QStringLiteral("plasmaBackgroundForPopups"), true},
        {QStringLiteral("floatingInternalGapIsForced"), false},
    });

    //! verbatim write order (a QQmlPropertyMap write to an unheld key creates it
    //! rather than aborting, so the two dead-key writes stay in their real spots)
    const QString dockPresetConfigBody = QStringLiteral(
        "configuration.alignment = %1;\n"                         // LatteCore.Types.Center
        "configuration.useThemePanel = true;\n"
        "configuration.solidPanel = false;\n"                     // DEAD KEY (no schema, no reader)
        "configuration.panelSize = 5;\n"
        "configuration.appletShadowsEnabled = true;\n"
        "configuration.zoomLevel = 16;\n"
        "configuration.dragActiveWindowEnabled = false;\n"
        "configuration.scrollAction = %2;\n"                      // LatteContainment.Types.ScrollNone
        "configuration.autoSizeEnabled = true;\n"
        "configuration.solidBackgroundForMaximized = false;\n"
        "configuration.colorizeTransparentPanels = false;\n"      // DEAD KEY (no schema, no reader)
        "configuration.backgroundOnlyOnMaximized = false;\n"
        "configuration.disablePanelShadowForMaximized = false;\n"
        "configuration.plasmaBackgroundForPopups = false;\n"
        "configuration.floatingInternalGapIsForced = true;\n")
        .arg(kAlignCenter).arg(kScrollNone);

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config, dockPresetConfigBody), QString());
    const QJsonObject after = snapshot(config);

    //! the 13 schema keys AND the two dead keys change - the preset touches two
    //! keys beyond its schema set
    QVERIFY(onlyExpectedKeysChanged(before, after, {
        QStringLiteral("alignment"), QStringLiteral("useThemePanel"), QStringLiteral("panelSize"),
        QStringLiteral("appletShadowsEnabled"), QStringLiteral("zoomLevel"),
        QStringLiteral("dragActiveWindowEnabled"), QStringLiteral("scrollAction"),
        QStringLiteral("autoSizeEnabled"), QStringLiteral("solidBackgroundForMaximized"),
        QStringLiteral("backgroundOnlyOnMaximized"), QStringLiteral("disablePanelShadowForMaximized"),
        QStringLiteral("plasmaBackgroundForPopups"), QStringLiteral("floatingInternalGapIsForced"),
        QStringLiteral("solidPanel"), QStringLiteral("colorizeTransparentPanels"),
    }));
    //! the dead-key writes are the two beyond the schema set
    QVERIFY2(after.contains(QStringLiteral("solidPanel")),
             "the Dock preset writes solidPanel (a schema-absent key)");
    QVERIFY2(after.contains(QStringLiteral("colorizeTransparentPanels")),
             "the Dock preset writes colorizeTransparentPanels (a schema-absent key)");
    //! and the live schema writes did apply (P1 for the preset)
    QVERIFY(valueReflects(after, QStringLiteral("alignment"), kAlignCenter));
    QVERIFY(valueReflects(after, QStringLiteral("zoomLevel"), 16));
}

//! S-a in situ for the Panel preset (TypeSelection.qml panelTypeButton
//! lines 103-131). Same shape, a different 15-key schema set (adds panelShadows,
//! titleTooltips, backgroundRadius, backgroundShadowSize; drops
//! solidBackgroundForMaximized and scrollAction), and the SAME two dead-key
//! writes beyond the schema set.
void BehaviorWiringAuditTest::panelPresetWritesSchemaKeysPlusTheTwoDeadKeys()
{
    QQmlPropertyMap config;
    seed(config, {
        {QStringLiteral("alignment"), 99},
        {QStringLiteral("useThemePanel"), false},
        {QStringLiteral("panelSize"), 999},
        {QStringLiteral("panelShadows"), false},
        {QStringLiteral("appletShadowsEnabled"), true},
        {QStringLiteral("zoomLevel"), 999},
        {QStringLiteral("titleTooltips"), true},
        {QStringLiteral("dragActiveWindowEnabled"), false},
        {QStringLiteral("autoSizeEnabled"), true},
        {QStringLiteral("backgroundOnlyOnMaximized"), true},
        {QStringLiteral("disablePanelShadowForMaximized"), true},
        {QStringLiteral("plasmaBackgroundForPopups"), false},
        {QStringLiteral("floatingInternalGapIsForced"), true},
        {QStringLiteral("backgroundRadius"), 999},
        {QStringLiteral("backgroundShadowSize"), 999},
    });

    //! verbatim write order (see the Dock note on QQmlPropertyMap dynamic keys)
    const QString panelPresetConfigBody = QStringLiteral(
        "configuration.alignment = %1;\n"                         // LatteCore.Types.Justify
        "configuration.useThemePanel = true;\n"
        "configuration.solidPanel = false;\n"                     // DEAD KEY
        "configuration.panelSize = 100;\n"
        "configuration.panelShadows = true;\n"
        "configuration.appletShadowsEnabled = false;\n"
        "configuration.zoomLevel = 0;\n"
        "configuration.titleTooltips = false;\n"
        "configuration.dragActiveWindowEnabled = true;\n"
        "configuration.autoSizeEnabled = false;\n"
        "configuration.colorizeTransparentPanels = false;\n"      // DEAD KEY
        "configuration.backgroundOnlyOnMaximized = false;\n"
        "configuration.disablePanelShadowForMaximized = false;\n"
        "configuration.plasmaBackgroundForPopups = true;\n"
        "configuration.floatingInternalGapIsForced = false;\n"
        "configuration.backgroundRadius = -1;\n"
        "configuration.backgroundShadowSize = -1;\n")
        .arg(kAlignJustify);

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config, panelPresetConfigBody), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(onlyExpectedKeysChanged(before, after, {
        QStringLiteral("alignment"), QStringLiteral("useThemePanel"), QStringLiteral("panelSize"),
        QStringLiteral("panelShadows"), QStringLiteral("appletShadowsEnabled"),
        QStringLiteral("zoomLevel"), QStringLiteral("titleTooltips"),
        QStringLiteral("dragActiveWindowEnabled"), QStringLiteral("autoSizeEnabled"),
        QStringLiteral("backgroundOnlyOnMaximized"), QStringLiteral("disablePanelShadowForMaximized"),
        QStringLiteral("plasmaBackgroundForPopups"), QStringLiteral("floatingInternalGapIsForced"),
        QStringLiteral("backgroundRadius"), QStringLiteral("backgroundShadowSize"),
        QStringLiteral("solidPanel"), QStringLiteral("colorizeTransparentPanels"),
    }));
    QVERIFY2(after.contains(QStringLiteral("solidPanel")),
             "the Panel preset writes solidPanel (a schema-absent key)");
    QVERIFY2(after.contains(QStringLiteral("colorizeTransparentPanels")),
             "the Panel preset writes colorizeTransparentPanels (a schema-absent key)");
    QVERIFY(valueReflects(after, QStringLiteral("alignment"), kAlignJustify));
    QVERIFY(valueReflects(after, QStringLiteral("backgroundRadius"), -1));
}

// ============================ AU-3c cfg checkboxes =========================

//! Each Actions/Environment/Items checkbox (catalog 32-42, the cfg ones) is a
//! plain toggle handler `plasmoid.configuration.X = !plasmoid.configuration.X`
//! (BehaviorConfig.qml). P1+P2: flipping one writes exactly its own key, never a
//! neighbour. Driven for every cfg checkbox so a future copy-paste that toggles
//! the wrong key is caught. (isPreferredForShortcuts and byPassWM are NOT here -
//! they are `view` C++ writes verified live in the nested vehicle.)
void BehaviorWiringAuditTest::actionCheckboxesEachWriteOnlyTheirOwnKey()
{
    const QStringList cfgCheckboxKeys = {
        QStringLiteral("titleTooltips"),
        QStringLiteral("mouseWheelActions"),
        QStringLiteral("autoSizeEnabled"),
        QStringLiteral("dragActiveWindowEnabled"),
        QStringLiteral("closeActiveWindowEnabled"),
        QStringLiteral("floatingInternalGapIsForced"),
        QStringLiteral("hideFloatingGapForMaximized"),
        QStringLiteral("floatingGapHidingWaitsMouse"),
        QStringLiteral("floatingGapIsMirrored"),
    };

    for (const QString &key : cfgCheckboxKeys) {
        //! a full General group with every cfg-checkbox key present (the schema
        //! shape), all false; the neighbours must stay put when one is toggled
        QQmlPropertyMap config;
        for (const QString &k : cfgCheckboxKeys) {
            config.insert(k, false);
        }

        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config,
                 QStringLiteral("configuration.%1 = !configuration.%1;").arg(key)), QString());
        const QJsonObject after = snapshot(config);

        QVERIFY2(controlApplies(before, after, key),
                 qPrintable(QStringLiteral("checkbox '%1' must apply its own key").arg(key)));
        QVERIFY2(onlyExpectedKeysChanged(before, after, {key}),
                 qPrintable(QStringLiteral("checkbox '%1' must write ONLY its own key").arg(key)));
        QVERIFY(valueReflects(after, key, true));
    }
}

// ============================ AU-3c / S-d combos ============================

//! The Track-active-window combo (BehaviorConfig.qml activeWindowFilterCmb,
//! onCurrentIndexChanged 633-642). S-d asks whether a combo whose currentIndex
//! binds to config and whose handler writes config back round-trips as identity.
//! Here the switch maps index -> the SAME enum value (0->0, 1->1), so:
//!   APPLY  - picking a different row writes that value, only that key (P1+P2);
//!   IDENTITY - re-firing the handler on the already-stored value (what an
//!              external config change does through the currentIndex binding)
//!              writes the same value, so nothing changes (no oscillation).
void BehaviorWiringAuditTest::activeWindowFilterComboAppliesAndRoundTripsIdentity()
{
    //! the real onCurrentIndexChanged switch, currentIndex injected as a local
    const QString filterSwitch = QStringLiteral(
        "switch (currentIndex) {\n"
        "case %1: configuration.activeWindowFilter = %1; break;\n"  // ActiveInCurrentScreen
        "case %2: configuration.activeWindowFilter = %2; break;\n"  // ActiveFromAllScreens
        "}\n").arg(kActiveInCurrentScreen).arg(kActiveFromAllScreens);

    //! APPLY: stored 0, user picks row 1 -> writes 1, only activeWindowFilter
    {
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("activeWindowFilter"), kActiveInCurrentScreen},
                      {QStringLiteral("scrollAction"), kScrollNone}});
        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config,
                 QStringLiteral("var currentIndex = %1;\n").arg(kActiveFromAllScreens) + filterSwitch),
                 QString());
        const QJsonObject after = snapshot(config);
        QVERIFY(controlApplies(before, after, QStringLiteral("activeWindowFilter")));
        QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("activeWindowFilter")}));
        QVERIFY(valueReflects(after, QStringLiteral("activeWindowFilter"), kActiveFromAllScreens));
    }

    //! IDENTITY: stored 1, currentIndex re-fires at 1 (external change) -> no-op
    {
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("activeWindowFilter"), kActiveFromAllScreens},
                      {QStringLiteral("scrollAction"), kScrollNone}});
        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config,
                 QStringLiteral("var currentIndex = %1;\n").arg(kActiveFromAllScreens) + filterSwitch),
                 QString());
        const QJsonObject after = snapshot(config);
        QVERIFY2(changedConfigKeys(before, after).isEmpty(),
                 "an identity combo re-firing on its stored value must not rewrite anything");
    }
}

//! The Mouse-wheel-action combo (BehaviorConfig.qml scrollAction,
//! onCurrentIndexChanged 723-741). Five rows, identity-mapped 0..4. Same S-d
//! shape: every row applies only scrollAction, and re-firing on the stored value
//! is a no-op across the whole range.
void BehaviorWiringAuditTest::scrollActionComboAppliesAndRoundTripsIdentity()
{
    const QString scrollSwitch = QStringLiteral(
        "switch (currentIndex) {\n"
        "case 0: configuration.scrollAction = 0; break;\n"  // ScrollNone
        "case 1: configuration.scrollAction = 1; break;\n"  // ScrollDesktops
        "case 2: configuration.scrollAction = 2; break;\n"  // ScrollActivities
        "case 3: configuration.scrollAction = 3; break;\n"  // ScrollTasks
        "case 4: configuration.scrollAction = 4; break;\n"  // ScrollToggleMinimized
        "}\n");

    //! APPLY every row from a distinct start so each transition is exercised
    for (int row = kScrollNone; row <= kScrollToggleMinimized; ++row) {
        const int start = (row == kScrollNone) ? kScrollToggleMinimized : kScrollNone;
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("scrollAction"), start},
                      {QStringLiteral("activeWindowFilter"), kActiveInCurrentScreen}});
        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config,
                 QStringLiteral("var currentIndex = %1;\n").arg(row) + scrollSwitch), QString());
        const QJsonObject after = snapshot(config);
        QVERIFY2(onlyExpectedKeysChanged(before, after, {QStringLiteral("scrollAction")}),
                 qPrintable(QStringLiteral("scrollAction row %1 must write only scrollAction").arg(row)));
        QVERIFY(valueReflects(after, QStringLiteral("scrollAction"), row));
    }

    //! IDENTITY: re-firing on the stored value is a no-op for every row
    for (int row = kScrollNone; row <= kScrollToggleMinimized; ++row) {
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("scrollAction"), row},
                      {QStringLiteral("activeWindowFilter"), kActiveInCurrentScreen}});
        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config,
                 QStringLiteral("var currentIndex = %1;\n").arg(row) + scrollSwitch), QString());
        const QJsonObject after = snapshot(config);
        QVERIFY2(changedConfigKeys(before, after).isEmpty(),
                 qPrintable(QStringLiteral("scrollAction row %1 re-fire must be a no-op").arg(row)));
    }
}

// ==================== AU-3a / AU-3b P3 checked reflection ===================

//! AU-3a P3: a Location edge button is `checked: plasmoid.location === edge`
//! (BehaviorConfig.qml bottomEdgeBtn etc.). plasmoid.location is a live C++
//! property; the binding must re-track when the view is relocated from any
//! surface, so exactly one edge button reads checked at a time. Modelled with a
//! NOTIFYing Q_PROPERTY source (LiveIntSource), the real shape of plasmoid.location.
void BehaviorWiringAuditTest::locationButtonCheckedReflectsLiveLocation()
{
    LiveIntSource location;
    //! PlasmaCore.Types.BottomEdge = 4, TopEdge = 3 (Plasma edge enum); the exact
    //! values are immaterial to the reflection, only that the binding re-tracks
    const QString bottomBtn = QStringLiteral("location.value === 4"); // this button's edge
    QString err;

    location.setValue(4); // view on the bottom edge
    QVERIFY2(evalCheckedBinding(&location, QStringLiteral("location"), bottomBtn, &err),
             qPrintable(QStringLiteral("bottom button must read checked when located bottom: %1").arg(err)));

    location.setValue(3); // view relocated to the top edge (e.g. via the ruler/config)
    QVERIFY2(!evalCheckedBinding(&location, QStringLiteral("location"), bottomBtn, &err),
             "bottom button must drop checked once the view leaves the bottom edge");
}

//! AU-3a P3: an Alignment button is `checked: configAlignment === alignment`
//! where configAlignment: plasmoid.configuration.alignment (BehaviorConfig.qml
//! alignmentRow). The source here is the config MAP (QQmlPropertyMap valueChanged),
//! the other reflection path. Justify feeds D17 but its reflection is the same
//! pure binding as the rest.
void BehaviorWiringAuditTest::alignmentButtonCheckedReflectsConfigAlignment()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("alignment"), kAlignCenter}});

    const QString justifyBtn = QStringLiteral("configuration.alignment === %1").arg(kAlignJustify);
    QString err;

    QVERIFY2(!evalCheckedBinding(&config, QStringLiteral("configuration"), justifyBtn, &err),
             qPrintable(QStringLiteral("Justify button must be unchecked while alignment is Center: %1").arg(err)));

    config.insert(QStringLiteral("alignment"), kAlignJustify);
    QVERIFY2(evalCheckedBinding(&config, QStringLiteral("configuration"), justifyBtn, &err),
             "Justify button must read checked once the config alignment becomes Justify");
}

//! AU-3b P3: a Visibility mode button is `checked: parent.mode === mode` where
//! parent.mode: latteView.visibility.mode (BehaviorConfig.qml Visibility grid).
//! Live C++ property again; the button must re-track when the mode changes from
//! the ruler/settings/another surface so the selected mode is always the shown one.
void BehaviorWiringAuditTest::visibilityButtonCheckedReflectsLiveMode()
{
    LiveIntSource mode;
    //! LatteCore.Types.AutoHide vs AlwaysVisible - two distinct modes; the values
    //! themselves do not matter, only that the reflection re-tracks
    const int autoHide = 6;
    const int alwaysVisible = 2;
    const QString autoHideBtn = QStringLiteral("mode.value === %1").arg(autoHide);
    QString err;

    mode.setValue(alwaysVisible);
    QVERIFY2(!evalCheckedBinding(&mode, QStringLiteral("mode"), autoHideBtn, &err),
             qPrintable(QStringLiteral("AutoHide button must be unchecked in AlwaysVisible: %1").arg(err)));

    mode.setValue(autoHide);
    QVERIFY2(evalCheckedBinding(&mode, QStringLiteral("mode"), autoHideBtn, &err),
             "AutoHide button must read checked once the live visibility mode is AutoHide");
}

QTEST_GUILESS_MAIN(BehaviorWiringAuditTest)

#include "behaviorwiringaudittest.moc"
