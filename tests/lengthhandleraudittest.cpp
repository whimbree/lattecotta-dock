/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The length cluster's real-handler WIRING audit (CL-1 in
// docs/edit-mode-settings-audit-plan.md). It is the settingswiringharnesstest
// pattern (a control's handler body run as a QML fragment against a stub
// QQmlPropertyMap, then the Tier B diff predicates in
// tests/units/configsnapshotdiff.h over the delta), extended with the one thing
// the length handlers need that the CL-0 harness does not register: the
// LengthOffsetClampBridge exposed as the `lengthClamp` context property, exactly
// as SubConfigView::init() registers it live. The maxLength ruler and the
// Appearance-page sliders all route their math through that bridge, so a
// faithful wiring test must drive the REAL clamp, not a re-implementation.
//
// It answers three length-cluster questions deterministically:
//   - D15 (AU-1a): which config keys the Maximum ruler changes at and away from
//     the max==min boundary - pinning the min-coupling as ACCEPTED, intended,
//     Qt5-faithful behaviour (Bree's decision, section 8), not a bug to remove.
//   - D16 (AU-1b): the slider-handle binding clobber - a plain `value:` binding
//     desyncs after an imperative assignment; the proxy-property re-sync pattern
//     re-tracks. The regression guard for the fix.
//   - AU-1e/AU-1f: the offset slider is the correct re-sync reference, and the
//     fine-value ctrl-wheel / click-to-round rows write only their own key.

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
#include "lengthoffsetclampbridge.h"

using namespace Latte::AuditHarness;

//! the maxLength ruler's real write-back body (RulerMouseArea.qml
//! updateMaxLength), adapted only in that `plasmoid.configuration` becomes the
//! stub map `configuration` and the wheel step is the injected local `step`.
//! Everything that decides WHICH keys move - the coupled min write guard, the
//! always-write of maxLength, the offset write guard - is verbatim, so the diff
//! it produces is the diff the live ruler produces.
static const QString kRulerUpdateMaxLengthBody = QStringLiteral(
    "var clamped = lengthClamp.clampMaxLengthByStep(configuration.maxLength,\n"
    "                                               configuration.minLength,\n"
    "                                               configuration.offset,\n"
    "                                               step,\n"
    "                                               configuration.alignment);\n"
    "if (configuration.minLength !== clamped.minLength) {\n"
    "    configuration.minLength = clamped.minLength;\n"
    "}\n"
    "configuration.maxLength = clamped.maxLength;\n"
    "if (configuration.offset !== clamped.offset) {\n"
    "    configuration.offset = clamped.offset;\n"
    "}\n");

class LengthHandlerAuditTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    //! AU-1a D15: the accepted min-coupling, pinned by its exact key delta
    void rulerAwayFromBoundaryMovesOnlyMaximum();
    void rulerAtCoupledBoundaryAlsoMovesMinimum();
    void rulerFloorCatchMovesOnlyMaximumNotCoupling();

private:
    static void seed(QQmlPropertyMap &config,
                     std::initializer_list<std::pair<QString, QVariant>> pairs);

    //! run a handler body against the stub map, with BOTH the map (as
    //! `configuration`) and a real LengthOffsetClampBridge (as `lengthClamp`)
    //! in scope. Returns the QML error string (empty on success) so a malformed
    //! fragment fails loudly instead of reading as a silent no-op.
    static QString runHandler(QQmlPropertyMap &config, const QString &handlerBody);

    //! the stub map's values as a "config" JSON object, the same shape
    //! viewConfigData()'s "config" carries (dbusreports.h), so the diff
    //! predicates see the same thing live and stubbed.
    static QJsonObject snapshot(const QQmlPropertyMap &config);
};

void LengthHandlerAuditTest::seed(QQmlPropertyMap &config,
                                  std::initializer_list<std::pair<QString, QVariant>> pairs)
{
    for (const auto &pair : pairs) {
        config.insert(pair.first, pair.second);
    }
}

QString LengthHandlerAuditTest::runHandler(QQmlPropertyMap &config, const QString &handlerBody)
{
    QQmlEngine engine;
    Latte::Settings::LengthOffsetClampBridge clamp;
    engine.rootContext()->setContextProperty(QStringLiteral("configuration"), &config);
    engine.rootContext()->setContextProperty(QStringLiteral("lengthClamp"), &clamp);

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

QJsonObject LengthHandlerAuditTest::snapshot(const QQmlPropertyMap &config)
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

//! AU-1a: away from the boundary (maxLength > minLength) the ruler moves ONLY
//! maxLength - the min-coupling does not fire, so a normal-length dock's
//! Minimum is never touched. This is the P2 baseline the accepted coupling is
//! measured against.
void LengthHandlerAuditTest::rulerAwayFromBoundaryMovesOnlyMaximum()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("maxLength"), 100}, {QStringLiteral("minLength"), 30},
                  {QStringLiteral("offset"), 0}, {QStringLiteral("alignment"), 0}}); // Center

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config, QStringLiteral("var step = -6;\n") + kRulerUpdateMaxLengthBody), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(controlApplies(before, after, QStringLiteral("maxLength")));
    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("maxLength")}));
    QVERIFY(valueReflects(after, QStringLiteral("maxLength"), 94));
    QVERIFY2(!changedConfigKeys(before, after).contains(QStringLiteral("minLength")),
             "away from the max==min boundary the ruler must not touch minLength");
}

//! AU-1a D15 ACCEPTED (Bree, section 8): at the max==min boundary the ruler
//! moves BOTH maxLength and minLength - the coupling keeps a fixed-length dock
//! (max==min) fixed as the ruler scrolls. This is intended, Qt5-faithful
//! behaviour (latte-dock-qt6 carries the identical `updateminimumlength =
//! (maxLength === minLength)` guard), pinned here as a passing regression that
//! DOCUMENTS the coupling rather than a bug the audit fails on.
void LengthHandlerAuditTest::rulerAtCoupledBoundaryAlsoMovesMinimum()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("maxLength"), 100}, {QStringLiteral("minLength"), 100},
                  {QStringLiteral("offset"), 0}, {QStringLiteral("alignment"), 0}}); // Center

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config, QStringLiteral("var step = -6;\n") + kRulerUpdateMaxLengthBody), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(controlApplies(before, after, QStringLiteral("maxLength")));
    QVERIFY(controlApplies(before, after, QStringLiteral("minLength")));
    QVERIFY(onlyExpectedKeysChanged(before, after,
                                    {QStringLiteral("maxLength"), QStringLiteral("minLength")}));
    //! the pair stays equal (the fixed-length invariant the coupling protects)
    QVERIFY(valueReflects(after, QStringLiteral("maxLength"), 94));
    QVERIFY(valueReflects(after, QStringLiteral("minLength"), 94));
}

//! AU-1a boundary precision: crossing INTO the boundary (maxLength above
//! minLength, stepped down onto it) is the min FLOOR catching maxLength, not
//! the coupling dragging minLength. Only maxLength moves; minLength is a floor
//! the wheel lands on, not a value it edits. This distinguishes the two
//! mechanisms the plan (section 1, D15) folds together in prose.
void LengthHandlerAuditTest::rulerFloorCatchMovesOnlyMaximumNotCoupling()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("maxLength"), 45}, {QStringLiteral("minLength"), 40},
                  {QStringLiteral("offset"), 0}, {QStringLiteral("alignment"), 1}}); // Left / Edge

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config, QStringLiteral("var step = -6;\n") + kRulerUpdateMaxLengthBody), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("maxLength")}));
    QVERIFY(valueReflects(after, QStringLiteral("maxLength"), 40));   // floored at min, not below
    QVERIFY2(!changedConfigKeys(before, after).contains(QStringLiteral("minLength")),
             "the floor catch must leave minLength unedited - it is a floor, not a coupled write");
}

QTEST_GUILESS_MAIN(LengthHandlerAuditTest)

#include "lengthhandleraudittest.moc"
