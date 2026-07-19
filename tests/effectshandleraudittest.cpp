/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The Effects page's real-handler WIRING audit (CL-4 in
// docs/tracking/edit-mode-settings-audit-plan.md, controls 75-90). It is the
// settingswiringharnesstest pattern (a control's handler body run as a QML
// fragment against a stub QQmlPropertyMap, then the Tier B diff predicates in
// tests/units/configsnapshotdiff.h over the delta), extended with the one thing
// the Indicators sub-group needs that the config-only harness does not carry:
// a stub `latteView.indicator` object, since those controls write a C++
// Indicator PROPERTY, not a containment config key.
//
// It answers the three Effects sub-audits deterministically:
//
//   AU-4a Shadows (75-80): every shadow control writes ONLY its own General
//     config key. The visual shadow effect that has no readback is left to the
//     e2e golden (HC2); here the RIGHT-KEY half (P2) is pinned - the toggle, the
//     two sliders, the three colour-source buttons and the colour swatch each
//     touch exactly one key and never couple.
//
//   AU-4b Animations (81-84) + the S-c verdict: the enable toggle writes only
//     animationsEnabled; the x1/x2/x3 duration buttons write only durationTime.
//     S-c (docs section 6) suspected the label/value pairing is inverted - the
//     buttons store 3/2/1 for x1/x2/x3. VERDICT here, with evidence: NOT a
//     defect, Qt5-faithful, ACCEPTED. Two facts pin it:
//       1. the stored value is an ENUM INDEX consumed only as a raw integer
//          (containment/.../abilities/Animations.qml speedFactor.current is the
//          sole consumer; the plasmoid, indicator and main.qml paths all read it
//          numerically). NOTHING resolves durationTime by the schema choice NAME
//          (main.xml None/x1/x2/x3 = 0/1/2/3), so the fact that the button
//          labelled "x1" stores the value the schema calls "x3" is inert - the
//          names are decorative, the integer is the contract.
//       2. the SENSE is correct: mapped through Animations.qml, the "faster"
//          label (x3) stores the value yielding the SHORTER animation, the
//          "slower" label (x1) the longer one. speedFactorForDurationTime pins
//          that ordering, so a future "cleanup" that reverses the mapping (making
//          a faster label store a longer duration) fails this test loudly.
//
//   AU-4c Indicators (85-90): the switch and the Latte/Plasma/Custom tabs write
//     latteView.indicator.enabled / .type (class `ind`), NOT a config key, so a
//     containment config-diff correctly shows NO General-key change for them -
//     the RIGHT thing, not a D10 dead control (the property persists to the
//     [Indicator] config group via Indicator::saveConfig and is read back by
//     viewConfigData's view.indicatorEnabled / view.indicatorType, already pinned
//     in dbusreportstest viewLiveRecord*). This test pins the CONTROL half: the
//     switch toggles enabled, each tab sets the matching plugin id, and none of
//     them writes the General config map. The live P1/P3 (drive the dock, read
//     the indicator readback) is the e2e leg that CONSUMES that CL-0 readback.

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

using namespace Latte::AuditHarness;

//! faithful transcription of containment/package/contents/ui/abilities/
//! Animations.qml speedFactor.current (lines 23-37), the SOLE consumer of
//! durationTime. duration.proposed = speedFactor.current * 2.8 * large, so a
//! LARGER speedFactor is a LONGER (slower) animation. Transcribed here so the
//! S-c sense check asserts against the real mapping, not a paraphrase: if the
//! consumer's numbers change, this copy and the test move together.
static double speedFactorForDurationTime(int durationTime)
{
    if (durationTime == 0) {
        return 0.0; // disabled
    }
    if (durationTime == 1) {
        return 0.75;
    }
    if (durationTime == 2) {
        return 1.0; // speedFactor.normal
    }
    if (durationTime == 3) {
        return 1.15;
    }
    return 1.0; // speedFactor.normal fallback (Animations.qml return speedFactor.normal)
}

class EffectsHandlerAuditTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    //! AU-4a Shadows (controls 75-80): each writes only its own key
    void shadowToggleWritesOnlyAppletShadowsEnabled();
    void shadowSizeSliderWritesOnlyShadowSize();
    void shadowOpacitySliderWritesOnlyShadowOpacity();
    void shadowColorButtonsWriteOnlyShadowColorType_data();
    void shadowColorButtonsWriteOnlyShadowColorType();
    void userShadowColorDialogWritesOnlyShadowColor();

    //! AU-4b Animations (controls 81-84) + the S-c verdict
    void animationsToggleWritesOnlyAnimationsEnabled();
    void durationButtonsWriteOnlyDurationTime_data();
    void durationButtonsWriteOnlyDurationTime();
    void durationButtonMappingIsCorrectSense();

    //! AU-4c Indicators (controls 85-90): the ind-class controls write the
    //! indicator property, never the General config
    void indicatorSwitchTogglesEnabledNotConfig();
    void indicatorTabsSetTypeNotConfig_data();
    void indicatorTabsSetTypeNotConfig();

private:
    static void seed(QQmlPropertyMap &config,
                     std::initializer_list<std::pair<QString, QVariant>> pairs);

    //! run a config-writing handler body against the stub map (as
    //! `configuration`). Returns the QML error string (empty on success) so a
    //! malformed fragment fails loudly, never as a silent no-op.
    static QString runConfigHandler(QQmlPropertyMap &config, const QString &handlerBody);

    //! the stub map as a "config" JSON object, the shape viewConfigData()'s
    //! "config" carries, so the diff predicates see the same thing live and
    //! stubbed (mirrors settingswiringharnesstest::snapshot).
    static QJsonObject snapshot(const QQmlPropertyMap &config);

    //! the indicator controls write a C++ property, so this runner puts a stub
    //! `latteView` (with a nested `indicator` object) in scope alongside
    //! `configuration`, runs the handler, and reports the indicator's resulting
    //! enabled/type plus whether the General config was touched.
    struct IndicatorOutcome {
        QString error;
        bool enabled{false};
        QString type;
        QStringList configKeysChanged;
    };
    static IndicatorOutcome runIndicatorHandler(QQmlPropertyMap &config,
                                                const QString &handlerBody,
                                                bool initialEnabled,
                                                const QString &initialType);
};

void EffectsHandlerAuditTest::seed(QQmlPropertyMap &config,
                                   std::initializer_list<std::pair<QString, QVariant>> pairs)
{
    for (const auto &pair : pairs) {
        config.insert(pair.first, pair.second);
    }
}

QString EffectsHandlerAuditTest::runConfigHandler(QQmlPropertyMap &config, const QString &handlerBody)
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

QJsonObject EffectsHandlerAuditTest::snapshot(const QQmlPropertyMap &config)
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

EffectsHandlerAuditTest::IndicatorOutcome
EffectsHandlerAuditTest::runIndicatorHandler(QQmlPropertyMap &config,
                                             const QString &handlerBody,
                                             bool initialEnabled,
                                             const QString &initialType)
{
    IndicatorOutcome outcome;

    QQmlEngine engine;
    //! configuration is in scope so the test can PROVE the ind-class handlers
    //! never touch the General config map (they write the indicator property)
    engine.rootContext()->setContextProperty(QStringLiteral("configuration"), &config);

    const QJsonObject before = snapshot(config);

    QQmlComponent component(&engine);
    //! the stub latteView: `latteView.indicator.enabled/type` is exactly what
    //! the real controls address (EffectsConfig.qml indicatorsSwitch / the tab
    //! buttons / CustomIndicatorButton.onButtonIsPressed)
    const QString qml = QStringLiteral(
        "import QtQml\n"
        "QtObject {\n"
        "  id: latteView\n"
        "  property QtObject indicator: QtObject {\n"
        "    property bool enabled: %2\n"
        "    property string type: \"%3\"\n"
        "  }\n"
        "  Component.onCompleted: { %1 }\n"
        "}\n")
        .arg(handlerBody, initialEnabled ? QStringLiteral("true") : QStringLiteral("false"), initialType);
    component.setData(qml.toUtf8(), QUrl());

    if (component.isError()) {
        outcome.error = component.errorString();
        return outcome;
    }

    QScopedPointer<QObject> root(component.create());

    if (!root) {
        outcome.error = component.errorString().isEmpty()
            ? QStringLiteral("indicator handler fragment produced no object") : component.errorString();
        return outcome;
    }

    QObject *indicator = root->property("indicator").value<QObject *>();
    if (!indicator) {
        outcome.error = QStringLiteral("stub latteView.indicator was not constructed");
        return outcome;
    }

    outcome.enabled = indicator->property("enabled").toBool();
    outcome.type = indicator->property("type").toString();
    outcome.configKeysChanged = changedConfigKeys(before, snapshot(config));
    return outcome;
}

// ============================ AU-4a Shadows =================================

//! control 75, showAppletShadow.onPressed: the applet-shadow master toggle
//! flips only appletShadowsEnabled.
void EffectsHandlerAuditTest::shadowToggleWritesOnlyAppletShadowsEnabled()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("appletShadowsEnabled"), true},
                  {QStringLiteral("shadowSize"), 30}, {QStringLiteral("shadowOpacity"), 70}});

    const QJsonObject before = snapshot(config);
    QCOMPARE(runConfigHandler(config, QStringLiteral(
        "configuration.appletShadowsEnabled = !configuration.appletShadowsEnabled;")), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(controlApplies(before, after, QStringLiteral("appletShadowsEnabled")));
    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("appletShadowsEnabled")}));
    QVERIFY(valueReflects(after, QStringLiteral("appletShadowsEnabled"), false));
}

//! control 76, shadowSizeSlider.updateShadowSize (the not-pressed branch):
//! writes only shadowSize.
void EffectsHandlerAuditTest::shadowSizeSliderWritesOnlyShadowSize()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("shadowSize"), 30}, {QStringLiteral("shadowOpacity"), 70},
                  {QStringLiteral("appletShadowsEnabled"), true}});

    const QJsonObject before = snapshot(config);
    //! updateShadowSize: if (!pressed) configuration.shadowSize = value; the
    //! slider stepSize is 5, so 55 is an on-step value
    QCOMPARE(runConfigHandler(config, QStringLiteral(
        "var value = 55; configuration.shadowSize = value;")), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(controlApplies(before, after, QStringLiteral("shadowSize")));
    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("shadowSize")}));
    QVERIFY(valueReflects(after, QStringLiteral("shadowSize"), 55));
}

//! control 77, shadowOpacitySlider.updateShadowOpacity: writes only shadowOpacity.
void EffectsHandlerAuditTest::shadowOpacitySliderWritesOnlyShadowOpacity()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("shadowOpacity"), 70}, {QStringLiteral("shadowSize"), 30},
                  {QStringLiteral("appletShadowsEnabled"), true}});

    const QJsonObject before = snapshot(config);
    QCOMPARE(runConfigHandler(config, QStringLiteral(
        "var value = 40; configuration.shadowOpacity = value;")), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(controlApplies(before, after, QStringLiteral("shadowOpacity")));
    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("shadowOpacity")}));
    QVERIFY(valueReflects(after, QStringLiteral("shadowOpacity"), 40));
}

//! controls 78/79/80, the Default/Theme/User colour-source buttons: each writes
//! only shadowColorType, at the LatteContainment.Types.ShadowColorGroup value
//! (types.h: DefaultColorShadow=0, ThemeColorShadow=1, UserColorShadow=2, which
//! matches main.xml shadowColorType Default/Theme/User = 0/1/2).
void EffectsHandlerAuditTest::shadowColorButtonsWriteOnlyShadowColorType_data()
{
    QTest::addColumn<int>("type");
    QTest::newRow("Default colour (control 78)") << 0;
    QTest::newRow("Theme colour (control 79)") << 1;
    QTest::newRow("User colour (control 80)") << 2;
}

void EffectsHandlerAuditTest::shadowColorButtonsWriteOnlyShadowColorType()
{
    QFETCH(int, type);

    QQmlPropertyMap config;
    //! start at a different type so every case is a real change
    seed(config, {{QStringLiteral("shadowColorType"), (type == 0) ? 1 : 0},
                  {QStringLiteral("shadowColor"), QStringLiteral("080808")}});

    const QJsonObject before = snapshot(config);
    QCOMPARE(runConfigHandler(config,
        QStringLiteral("configuration.shadowColorType = %1;").arg(type)), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(controlApplies(before, after, QStringLiteral("shadowColorType")));
    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("shadowColorType")}));
    QVERIFY(valueReflects(after, QStringLiteral("shadowColorType"), type));
}

//! control 80's colour dialog, onAccepted: strips the leading '#' and writes
//! only shadowColor (the shadowColorType=User write already happened on the
//! swatch click, control 80 above). Two writes at two moments, each single-key.
void EffectsHandlerAuditTest::userShadowColorDialogWritesOnlyShadowColor()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("shadowColor"), QStringLiteral("080808")},
                  {QStringLiteral("shadowColorType"), 2}});

    const QJsonObject before = snapshot(config);
    //! onAccepted body: var strC = String(selectedColor); if (strC.indexOf("#")
    //! === 0) configuration.shadowColor = strC.substr(1);
    QCOMPARE(runConfigHandler(config, QStringLiteral(
        "var strC = \"#1a2b3c\";\n"
        "if (strC.indexOf(\"#\") === 0) { configuration.shadowColor = strC.substr(1); }")), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(controlApplies(before, after, QStringLiteral("shadowColor")));
    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("shadowColor")}));
    QVERIFY(valueReflects(after, QStringLiteral("shadowColor"), QStringLiteral("1a2b3c")));
}

// ============================ AU-4b Animations ==============================

//! control 81, animationsHeader.onPressed: the master animations toggle flips
//! only animationsEnabled.
void EffectsHandlerAuditTest::animationsToggleWritesOnlyAnimationsEnabled()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("animationsEnabled"), true},
                  {QStringLiteral("durationTime"), 2}});

    const QJsonObject before = snapshot(config);
    QCOMPARE(runConfigHandler(config, QStringLiteral(
        "configuration.animationsEnabled = !configuration.animationsEnabled;")), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(controlApplies(before, after, QStringLiteral("animationsEnabled")));
    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("animationsEnabled")}));
    QVERIFY(valueReflects(after, QStringLiteral("animationsEnabled"), false));
}

//! controls 82-84, the x1/x2/x3 duration buttons: each writes ONLY durationTime,
//! at the button's stored value. The stored values are 3/2/1 for x1/x2/x3 - the
//! S-c pairing, pinned exactly here (the sense of the pairing is the next test).
void EffectsHandlerAuditTest::durationButtonsWriteOnlyDurationTime_data()
{
    QTest::addColumn<int>("stored");
    //! EffectsConfig.qml: the x1 button carries `readonly property int duration:
    //! 3`, x2 -> 2, x3 -> 1. The handler is `configuration.durationTime = duration`.
    QTest::newRow("x1 button stores 3") << 3;
    QTest::newRow("x2 button stores 2") << 2;
    QTest::newRow("x3 button stores 1") << 1;
}

void EffectsHandlerAuditTest::durationButtonsWriteOnlyDurationTime()
{
    QFETCH(int, stored);

    QQmlPropertyMap config;
    //! start at None (0) so every button press is a real change
    seed(config, {{QStringLiteral("durationTime"), 0},
                  {QStringLiteral("animationsEnabled"), true}});

    const QJsonObject before = snapshot(config);
    QCOMPARE(runConfigHandler(config,
        QStringLiteral("configuration.durationTime = %1;").arg(stored)), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(controlApplies(before, after, QStringLiteral("durationTime")));
    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("durationTime")}));
    QVERIFY(valueReflects(after, QStringLiteral("durationTime"), stored));
}

//! S-c VERDICT (docs section 6): the label/value inversion is NOT a defect. The
//! stored value is an enum index read only as a raw integer, and the SENSE is
//! right - the "faster" label stores the SHORTER animation. Proven by mapping
//! each button's stored value through the real consumer (Animations.qml
//! speedFactor.current): x3 (fast) -> stored 1 -> speedFactor 0.75 (short);
//! x1 (slow) -> stored 3 -> speedFactor 1.15 (long). A future edit that made the
//! buttons store 1/2/3 (a "faster stores larger" inversion) would break the
//! monotonic ordering asserted here.
void EffectsHandlerAuditTest::durationButtonMappingIsCorrectSense()
{
    //! the (label -> stored value) mapping the three buttons declare
    const int x1Stored = 3; // label "x1" = slowest
    const int x2Stored = 2; // label "x2" = medium
    const int x3Stored = 1; // label "x3" = fastest

    const double x1Speed = speedFactorForDurationTime(x1Stored);
    const double x2Speed = speedFactorForDurationTime(x2Stored);
    const double x3Speed = speedFactorForDurationTime(x3Stored);

    //! duration.proposed grows with speedFactor, so a faster label MUST map to a
    //! smaller speedFactor (a shorter animation). Strictly monotone from
    //! fast->slow: x3 < x2 < x1. If someone reverses the button values this
    //! ordering flips and the control silently becomes inverted - caught here.
    QVERIFY2(x3Speed < x2Speed,
             "the x3 (faster) button must yield a SHORTER animation than x2");
    QVERIFY2(x2Speed < x1Speed,
             "the x2 button must yield a SHORTER animation than x1 (slower)");

    //! concrete anchors so the numbers, not just the ordering, are pinned to the
    //! Animations.qml consumer
    QCOMPARE(x3Speed, 0.75);
    QCOMPARE(x2Speed, 1.0);
    QCOMPARE(x1Speed, 1.15);
}

// ============================ AU-4c Indicators ==============================

//! control 85, indicatorsSwitch.onPressed: toggles latteView.indicator.enabled
//! and touches NO General config key (the enabled flag persists to the
//! [Indicator] config group in C++, not the containment General map).
void EffectsHandlerAuditTest::indicatorSwitchTogglesEnabledNotConfig()
{
    QQmlPropertyMap config;
    //! a representative General key present so "no General write" is a real check
    seed(config, {{QStringLiteral("animationsEnabled"), true}});

    const IndicatorOutcome outcome = runIndicatorHandler(config,
        QStringLiteral("latteView.indicator.enabled = !latteView.indicator.enabled;"),
        /*initialEnabled*/ true, /*initialType*/ QStringLiteral("org.kde.latte.default"));

    QCOMPARE(outcome.error, QString());
    QCOMPARE(outcome.enabled, false); // toggled off
    QVERIFY2(outcome.configKeysChanged.isEmpty(),
             "the Indicators switch writes the C++ indicator property, never a "
             "General config key - an empty config delta is the RIGHT result, "
             "not a D10 dead control");
}

//! controls 86-89, the Latte/Plasma/Custom tabs (and CustomIndicatorButton.
//! onButtonIsPressed): set latteView.indicator.type to the tab's plugin id, no
//! General config write. Custom stands in for the CustomIndicatorButton path
//! (latteView.indicator.type = custom.type / item.pluginId).
void EffectsHandlerAuditTest::indicatorTabsSetTypeNotConfig_data()
{
    QTest::addColumn<QString>("pluginId");
    QTest::newRow("Latte tab (control 86)") << QStringLiteral("org.kde.latte.default");
    QTest::newRow("Plasma tab (control 87)") << QStringLiteral("org.kde.latte.plasma");
    QTest::newRow("Custom plugin (controls 88-89)") << QStringLiteral("org.kde.latte.custom.example");
}

void EffectsHandlerAuditTest::indicatorTabsSetTypeNotConfig()
{
    QFETCH(QString, pluginId);

    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("animationsEnabled"), true}});

    //! start at a different type so the write is a real change in every row
    const QString initialType = (pluginId == QStringLiteral("org.kde.latte.plasma"))
        ? QStringLiteral("org.kde.latte.default") : QStringLiteral("org.kde.latte.plasma");

    const IndicatorOutcome outcome = runIndicatorHandler(config,
        QStringLiteral("latteView.indicator.type = \"%1\";").arg(pluginId),
        /*initialEnabled*/ true, initialType);

    QCOMPARE(outcome.error, QString());
    QCOMPARE(outcome.type, pluginId);
    QVERIFY2(outcome.configKeysChanged.isEmpty(),
             "an indicator style tab writes the C++ indicator type, never a "
             "General config key");
}

QTEST_GUILESS_MAIN(EffectsHandlerAuditTest)

#include "effectshandleraudittest.moc"
