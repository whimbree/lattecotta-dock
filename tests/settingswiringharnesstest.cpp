/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The QML-handler WIRING harness of the edit-mode settings audit
// (docs/tracking/edit-mode-settings-audit-plan.md section 2, the cheaper decisive
// driver). A settings control's WIRING - which config key its handler writes -
// is the P2 question, and the fiddliest to answer through the mapped settings
// window (combos and text fields are pointer-drivable but awkward). This
// harness answers it deterministically instead: it runs a control's handler
// body as a QML fragment against a STUB config map (a QQmlPropertyMap standing
// in for plasmoid.configuration), snapshots the map before and after, and runs
// the audit's Tier B diff predicates (tests/units/configsnapshotdiff.h) over
// the delta. A cluster agent reproduces a real handler here - e.g. AU-1b's
// slider-binding re-sync check - by pasting the control's handler expression
// into runHandler().
//
// This file is also the harness's HC3 acceptance test: it proves the harness
// OBSERVES A REJECTION when a handler misbehaves (writes the wrong key, writes
// a stray coupled key, or writes nothing), not only when it behaves. A harness
// that only passes the happy path cannot be trusted to find the suspected-broken
// controls, so every rejection direction is driven through a REAL QML write.

// Qt
#include <QColor>
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

class SettingsWiringHarnessTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void harnessSeesACorrectWrite();
    void reflectReadsBackTheWrittenValue();

    //! HC3: the harness catches a wrong outcome driven through a REAL handler
    void harnessCatchesNoOpHandler();
    void harnessCatchesWrongKeyWrite();
    void harnessCatchesStrayCoupledWrite();

    //! CL-6 (AU-6a/AU-6b) - the chrome and on-canvas non-length controls'
    //! handlers write only their own key. The decisive one is the
    //! edit-background wheel: both reference forks rewired it to panelTransparency
    //! (the CLAUDE.md fork-rewire trap); this port keeps editBackgroundOpacity,
    //! pinned here by seeding panelTransparency and proving it never moves.
    void editBackgroundWheelWritesOnlyEditBackgroundOpacity();
    void editBackgroundWheelClampsWithinUnitRangeNeverPanelTransparency();
    void pinButtonWritesOnlyConfigurationSticker();
    void stickOnTopWritesOnlyIsStickedOnTopEdge();
    void stickOnBottomWritesOnlyIsStickedOnBottomEdge();
    //! the us-toggles (rearrange, advanced) write a UniversalSettings property,
    //! never a plasmoid.configuration key - proven by driving the real toggle
    //! against a stub universalSettings and asserting the config map is untouched
    void rearrangeToggleWritesUniversalSettingsNotConfig();
    void advancedToggleWritesUniversalSettingsNotConfig();

private:
    //! seed a stub config map with the keys a handler under test may touch.
    //! QML can only assign to properties that already exist on a QQmlPropertyMap
    //! (exactly like the real KConfigPropertyMap, which carries every schema
    //! key), so the keys are pre-inserted here.
    static void seed(QQmlPropertyMap &config, std::initializer_list<std::pair<QString, QVariant>> pairs);

    //! run a control's handler BODY as a QML fragment against the stub map,
    //! exposed as `configuration` (mirrors plasmoid.configuration). Returns
    //! the QML error string (empty on success) so a malformed fragment fails
    //! loudly instead of silently writing nothing and reading as a no-op.
    static QString runHandler(QQmlPropertyMap &config, const QString &handlerBody);

    //! same, plus a stub `universalSettings` map (mirrors the QML singleton the
    //! us-toggle chrome controls write). Lets a handler that touches
    //! universalSettings be driven while the config map is watched for stray
    //! writes - the P2 "writes universalSettings, not a config key" check.
    static QString runHandler(QQmlPropertyMap &config, QQmlPropertyMap &universalSettings, const QString &handlerBody);

    //! snapshot the stub map's current values as a "config" JSON object, the
    //! same shape viewConfigData()'s "config" object carries (dbusreports.h
    //! serializeConfigMap/configValueToJson), so the diff predicates see the
    //! same thing live and stubbed.
    static QJsonObject snapshot(const QQmlPropertyMap &config);
};

void SettingsWiringHarnessTest::seed(QQmlPropertyMap &config, std::initializer_list<std::pair<QString, QVariant>> pairs)
{
    for (const auto &pair : pairs) {
        config.insert(pair.first, pair.second);
    }
}

QString SettingsWiringHarnessTest::runHandler(QQmlPropertyMap &config, const QString &handlerBody)
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

QString SettingsWiringHarnessTest::runHandler(QQmlPropertyMap &config, QQmlPropertyMap &universalSettings, const QString &handlerBody)
{
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("configuration"), &config);
    engine.rootContext()->setContextProperty(QStringLiteral("universalSettings"), &universalSettings);

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

QJsonObject SettingsWiringHarnessTest::snapshot(const QQmlPropertyMap &config)
{
    QJsonObject json;
    const QStringList keys = config.keys();

    for (const QString &key : keys) {
        const QVariant value = config.value(key);
        //! same stable-scalar rule as dbusreports.h configValueToJson: a JSON
        //! scalar where one exists, a canonical string for a typed value (a
        //! QColor) that has none, so the diff compares equal-means-equal
        const QJsonValue direct = QJsonValue::fromVariant(value);

        if (direct.type() != QJsonValue::Null && direct.type() != QJsonValue::Undefined) {
            json[key] = direct;
        } else if (value.canConvert<QColor>()) {
            json[key] = value.value<QColor>().name(QColor::HexArgb);
        } else {
            json[key] = value.toString();
        }
    }

    return json;
}

//! a well-behaved handler writes exactly its own key: the harness sees the
//! write (P1) and confirms no stray key moved (P2).
void SettingsWiringHarnessTest::harnessSeesACorrectWrite()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("maxLength"), 100}, {QStringLiteral("minLength"), 30}, {QStringLiteral("offset"), 0}});

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config, QStringLiteral("configuration.maxLength = 90")), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(controlApplies(before, after, QStringLiteral("maxLength")));
    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("maxLength")}));
}

void SettingsWiringHarnessTest::reflectReadsBackTheWrittenValue()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("iconSize"), 48}});

    QCOMPARE(runHandler(config, QStringLiteral("configuration.iconSize = 64")), QString());

    QVERIFY(valueReflects(snapshot(config), QStringLiteral("iconSize"), 64));
}

//! HC3: a handler that writes nothing (the D10 dead-control class) leaves the
//! map unchanged; the harness must FAIL P1, not pass because "nothing broke".
void SettingsWiringHarnessTest::harnessCatchesNoOpHandler()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("titleTooltips"), false}, {QStringLiteral("maxLength"), 100}});

    const QJsonObject before = snapshot(config);
    //! a handler that reads but never writes - the shape of a control wired to
    //! a value nothing consumes
    QCOMPARE(runHandler(config, QStringLiteral("var v = configuration.titleTooltips")), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY2(!controlApplies(before, after, QStringLiteral("titleTooltips")),
             "a no-op handler must be REJECTED by the harness (P1 fails), not silently passed");
    QVERIFY(changedConfigKeys(before, after).isEmpty());
}

//! HC3: a handler mislabelled to its key - it writes offset when the audit
//! expects maxLength. The harness must FAIL both P1 (maxLength did not move)
//! and P2 (a key outside the expected set moved).
void SettingsWiringHarnessTest::harnessCatchesWrongKeyWrite()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("maxLength"), 100}, {QStringLiteral("offset"), 0}});

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config, QStringLiteral("configuration.offset = 5")), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY2(!controlApplies(before, after, QStringLiteral("maxLength")),
             "a handler that wrote the WRONG key must FAIL the maxLength P1 check");
    QVERIFY2(!onlyExpectedKeysChanged(before, after, {QStringLiteral("maxLength")}),
             "a handler that wrote the WRONG key must FAIL the P2 right-key-only check");
    //! and the harness names what actually moved, so the audit reports the bug
    QCOMPARE(changedConfigKeys(before, after), (QStringList{QStringLiteral("offset")}));
}

//! HC3: the D15 shape driven through a real handler - writing maxLength also
//! writes minLength. P2 with the expected set {maxLength} must FAIL on the
//! stray coupled key.
void SettingsWiringHarnessTest::harnessCatchesStrayCoupledWrite()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("maxLength"), 100}, {QStringLiteral("minLength"), 100}, {QStringLiteral("offset"), 0}});

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config, QStringLiteral("configuration.maxLength = 90; configuration.minLength = 90")), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY2(!onlyExpectedKeysChanged(before, after, {QStringLiteral("maxLength")}),
             "a coupled minLength side effect must be REJECTED by the harness (P2 fails)");
    QCOMPARE(changedConfigKeys(before, after), (QStringList{QStringLiteral("maxLength"), QStringLiteral("minLength")}));
}

//! CL-6 AU-6a control 2: THE fork-rewire trap. The edit-background wheel
//! (CanvasConfiguration.qml:144-161) adjusts the edit-mode grid opacity, which
//! is editBackgroundOpacity - a Double in [0,1]. BOTH reference forks
//! (latte-dock-qt6 CanvasConfiguration.qml:153-155, latte-dock-ng :98) rewired
//! this handler to write panelTransparency instead (an Int in [0,100] that is
//! the dock's real runtime background opacity, a persistent user setting). This
//! port keeps the Qt5-faithful editBackgroundOpacity; the seed carries
//! panelTransparency so a rewire would show as a stray changed key and FAIL P2.
void SettingsWiringHarnessTest::editBackgroundWheelWritesOnlyEditBackgroundOpacity()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("editBackgroundOpacity"), 0.5},
                  {QStringLiteral("panelTransparency"), 40},
                  {QStringLiteral("offset"), 0}});

    const QJsonObject before = snapshot(config);
    //! the up-detent write, verbatim from the handler (opacityStep = 0.1)
    QCOMPARE(runHandler(config, QStringLiteral(
                 "var opacityStep = 0.1;\n"
                 "var current = configuration.editBackgroundOpacity;\n"
                 "configuration.editBackgroundOpacity = Math.min(1, current + opacityStep);")),
             QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(controlApplies(before, after, QStringLiteral("editBackgroundOpacity")));
    QVERIFY2(onlyExpectedKeysChanged(before, after, {QStringLiteral("editBackgroundOpacity")}),
             "the edit-background wheel must write ONLY editBackgroundOpacity - a panelTransparency "
             "write is the fork-rewire trap and a P2 failure");
    //! spell the trap out: panelTransparency never moved
    QVERIFY(!changedConfigKeys(before, after).contains(QStringLiteral("panelTransparency")));
}

//! CL-6 AU-6a control 2 (clamp half): the handler floors/ceilings the value in
//! [0,1] with Math.max/Math.min, and STILL only ever touches editBackgroundOpacity.
void SettingsWiringHarnessTest::editBackgroundWheelClampsWithinUnitRangeNeverPanelTransparency()
{
    //! up-detent at the ceiling: 0.95 + 0.1 clamps to exactly 1.0
    {
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("editBackgroundOpacity"), 0.95},
                      {QStringLiteral("panelTransparency"), 40}});
        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config, QStringLiteral(
                     "var current = configuration.editBackgroundOpacity;\n"
                     "configuration.editBackgroundOpacity = Math.min(1, current + 0.1);")),
                 QString());
        const QJsonObject after = snapshot(config);
        QVERIFY(valueReflects(after, QStringLiteral("editBackgroundOpacity"), 1.0));
        QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("editBackgroundOpacity")}));
    }

    //! down-detent at the floor: 0.05 - 0.1 clamps to exactly 0.0
    {
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("editBackgroundOpacity"), 0.05},
                      {QStringLiteral("panelTransparency"), 40}});
        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config, QStringLiteral(
                     "var current = configuration.editBackgroundOpacity;\n"
                     "configuration.editBackgroundOpacity = Math.max(0, current - 0.1);")),
                 QString());
        const QJsonObject after = snapshot(config);
        QVERIFY(valueReflects(after, QStringLiteral("editBackgroundOpacity"), 0.0));
        QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("editBackgroundOpacity")}));
    }
}

//! CL-6 AU-6b control 6: the Pin button (LatteDockConfiguration.qml:224-227) writes
//! configurationSticker (its C++ side effect, viewConfig.setSticker, is not a
//! config write and is covered by the live drive).
void SettingsWiringHarnessTest::pinButtonWritesOnlyConfigurationSticker()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("configurationSticker"), false},
                  {QStringLiteral("panelTransparency"), 40}});

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config, QStringLiteral("configuration.configurationSticker = true")), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(controlApplies(before, after, QStringLiteral("configurationSticker")));
    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("configurationSticker")}));
    QVERIFY(valueReflects(after, QStringLiteral("configurationSticker"), true));
}

//! CL-6 AU-6a control 4: Stick On Top (HeaderSettings.qml:79-83) toggles
//! isStickedOnTopEdge and touches nothing else (never the sibling bottom key).
void SettingsWiringHarnessTest::stickOnTopWritesOnlyIsStickedOnTopEdge()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("isStickedOnTopEdge"), false},
                  {QStringLiteral("isStickedOnBottomEdge"), false}});

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config, QStringLiteral(
                 "configuration.isStickedOnTopEdge = !configuration.isStickedOnTopEdge")), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(controlApplies(before, after, QStringLiteral("isStickedOnTopEdge")));
    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("isStickedOnTopEdge")}));
    QVERIFY(valueReflects(after, QStringLiteral("isStickedOnTopEdge"), true));
}

//! CL-6 AU-6a control 5: Stick On Bottom (HeaderSettings.qml:143-147) - the mirror.
void SettingsWiringHarnessTest::stickOnBottomWritesOnlyIsStickedOnBottomEdge()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("isStickedOnTopEdge"), false},
                  {QStringLiteral("isStickedOnBottomEdge"), false}});

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config, QStringLiteral(
                 "configuration.isStickedOnBottomEdge = !configuration.isStickedOnBottomEdge")), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(controlApplies(before, after, QStringLiteral("isStickedOnBottomEdge")));
    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("isStickedOnBottomEdge")}));
    QVERIFY(valueReflects(after, QStringLiteral("isStickedOnBottomEdge"), true));
}

//! CL-6 AU-6a control 3: the Rearrange toggle (HeaderSettings.qml:125-129) flips
//! universalSettings.inConfigureAppletsMode. P2 here is "no config key leaks":
//! the config map is seeded and watched, and must stay untouched while the
//! us flag flips.
void SettingsWiringHarnessTest::rearrangeToggleWritesUniversalSettingsNotConfig()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("editBackgroundOpacity"), 0.5},
                  {QStringLiteral("panelTransparency"), 40}});
    QQmlPropertyMap universal;
    universal.insert(QStringLiteral("inConfigureAppletsMode"), false);

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config, universal, QStringLiteral(
                 "universalSettings.inConfigureAppletsMode = !universalSettings.inConfigureAppletsMode")),
             QString());
    const QJsonObject after = snapshot(config);

    QVERIFY2(changedConfigKeys(before, after).isEmpty(),
             "the rearrange toggle must not write any plasmoid.configuration key");
    QCOMPARE(universal.value(QStringLiteral("inConfigureAppletsMode")).toBool(), true);
}

//! CL-6 AU-6b control 7: the Advanced switch (LatteDockConfiguration.qml:285-289)
//! writes universalSettings.inAdvancedModeForEditSettings, no config key.
void SettingsWiringHarnessTest::advancedToggleWritesUniversalSettingsNotConfig()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("editBackgroundOpacity"), 0.5},
                  {QStringLiteral("panelTransparency"), 40}});
    QQmlPropertyMap universal;
    universal.insert(QStringLiteral("inAdvancedModeForEditSettings"), false);

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config, universal, QStringLiteral(
                 "universalSettings.inAdvancedModeForEditSettings = true")), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY2(changedConfigKeys(before, after).isEmpty(),
             "the advanced switch must not write any plasmoid.configuration key");
    QCOMPARE(universal.value(QStringLiteral("inAdvancedModeForEditSettings")).toBool(), true);
}

QTEST_GUILESS_MAIN(SettingsWiringHarnessTest)

#include "settingswiringharnesstest.moc"
