/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The Appearance page's NON-length real-handler WIRING audit (CL-2 in
// docs/tracking/edit-mode-settings-audit-plan.md). It is the settingswiringharnesstest
// pattern: a control's handler body run as a QML fragment against a stub
// QQmlPropertyMap standing in for plasmoid.configuration, then the Tier B diff
// predicates (tests/units/configsnapshotdiff.h) over the before/after delta.
// The stub map carries the same shape viewConfigData()'s "config" object
// carries live (readConfigMap in app/dbusreports.cpp reads the whole
// KConfigPropertyMap), so the key delta a stubbed handler produces is the key
// delta the live handler produces.
//
// It answers CL-2's per-control questions deterministically:
//   - AU-2a Items (controls 47-49): iconSize / proportionIconSize / zoomLevel
//     each write ONLY their own key, and their value transforms are the ones
//     the shipped handlers apply (the even snap, the -1 "disabled" sentinel,
//     the rounded inverse zoom factor).
//   - AU-2b Margins (controls 57-59): lengthExtMargin / thickMargin /
//     screenEdgeMargin each write only their own key.
//   - AU-2c Colors (controls 60-61): the Palette and From-Window combos each
//     write only their own key AND round-trip every stored value back to the
//     shown selection - the S-b index-collision check, cleared by D23 (the
//     Reverse row restored, colorsToIndex given a distinct index per value).
//     Pinned together with the types.h <-> main.xml numeric agreement the
//     round-trip silently depends on.
//   - AU-2d Background (controls 62-74): every toggle and slider writes only
//     its own key.
//
// The LIVE render/geometry effect (P1 on the dock itself - icon size resizing
// the thickness, the palette repainting) is the nested-vehicle recipe's job;
// this file is the cheaper decisive WIRING driver the plan calls for where the
// question is which key moves, not the pixel feel.

// Qt
#include <QFile>
#include <QJsonObject>
#include <QJsonValue>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QScopedPointer>
#include <QSet>
#include <QStringList>
#include <QTest>
#include <QVariant>
#include <QXmlStreamReader>

#include "configsnapshotdiff.h"
#include "types.h"

using namespace Latte::AuditHarness;
using Types = Latte::Containment::Types;

//! The Palette combo's shipped index numbering (AppearanceConfig.qml, the D23
//! fix): the model rows in order and the value each row holds. colorsToIndex()
//! must send every stored themeColors value to the row that carries it, so the
//! stored value round-trips to the shown selection. Pinned at compile time so a
//! types.h reorder that silently broke the mapping (the shape of the S-b
//! collision before D23) fails the build here, not a user's dropdown.
static_assert(static_cast<int>(Types::PlasmaThemeColors) == 0, "themeColors numbering drifted from the D23 model");
static_assert(static_cast<int>(Types::ReverseThemeColors) == 1, "themeColors numbering drifted from the D23 model");
static_assert(static_cast<int>(Types::SmartThemeColors) == 2, "themeColors numbering drifted from the D23 model");
static_assert(static_cast<int>(Types::DarkThemeColors) == 3, "themeColors numbering drifted from the D23 model");
static_assert(static_cast<int>(Types::LightThemeColors) == 4, "themeColors numbering drifted from the D23 model");
static_assert(static_cast<int>(Types::LayoutThemeColors) == 5, "themeColors numbering drifted from the D23 model");

class AppearanceHandlerAuditTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    //! AU-2a Items (controls 47-49)
    void iconSizeSliderWritesOnlyIconSize();
    void iconSizeDisplaySnapsOddToEven();
    void proportionIconSizeCollapsesUnityToDisabledSentinel();
    void proportionIconSizeNamesItsReferenceAndShowsPixels();
    void zoomLevelStoresRoundedInverseFactor();

    //! AU-2b Margins (controls 57-59)
    void marginSlidersEachWriteOnlyTheirOwnKey();

    //! AU-2c Colors (controls 60-61) incl. the S-b index-collision clearance
    void paletteComboWritesOnlyThemeColors();
    void paletteRoundTripsEveryValueToADistinctIndex();
    void themeColorsEnumAgreesBetweenTypesAndSchema();
    void fromWindowComboRoundTripsAndWritesOnlyWindowColors();

    //! AU-2d Background (controls 62-74)
    void backgroundSlidersEachWriteOnlyTheirOwnKey();
    void backgroundTogglesEachWriteOnlyTheirOwnKey();

private:
    static void seed(QQmlPropertyMap &config,
                     std::initializer_list<std::pair<QString, QVariant>> pairs);

    //! run a handler body against the stub map (exposed as `configuration`,
    //! mirroring plasmoid.configuration). Returns the QML error string (empty
    //! on success) so a malformed fragment fails loudly instead of reading as a
    //! silent no-op.
    static QString runHandler(QQmlPropertyMap &config, const QString &handlerBody);

    //! the stub map's values as a "config" JSON object, the same shape
    //! viewConfigData()'s "config" carries, so the diff predicates see the same
    //! thing live and stubbed.
    static QJsonObject snapshot(const QQmlPropertyMap &config);

    //! the ordered list of <choice name="..."> under a named <entry> in a
    //! KConfigXT main.xml. KConfig maps an Enum choice to its DECLARATION-ORDER
    //! index, so this order is the numeric meaning the schema gives each name.
    static QStringList enumChoiceOrder(const QString &xmlPath, const QString &entryName);
};

void AppearanceHandlerAuditTest::seed(QQmlPropertyMap &config,
                                      std::initializer_list<std::pair<QString, QVariant>> pairs)
{
    for (const auto &pair : pairs) {
        config.insert(pair.first, pair.second);
    }
}

QString AppearanceHandlerAuditTest::runHandler(QQmlPropertyMap &config, const QString &handlerBody)
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

QJsonObject AppearanceHandlerAuditTest::snapshot(const QQmlPropertyMap &config)
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

QStringList AppearanceHandlerAuditTest::enumChoiceOrder(const QString &xmlPath, const QString &entryName)
{
    QFile file(xmlPath);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    QXmlStreamReader xml(&file);
    QStringList choices;
    bool inTargetEntry = false;

    while (!xml.atEnd()) {
        const auto token = xml.readNext();

        if (token == QXmlStreamReader::StartElement && xml.name() == QLatin1String("entry")) {
            inTargetEntry = (xml.attributes().value(QLatin1String("name")) == entryName);
        } else if (token == QXmlStreamReader::EndElement && xml.name() == QLatin1String("entry")) {
            if (inTargetEntry) {
                break;
            }
        } else if (inTargetEntry && token == QXmlStreamReader::StartElement
                   && xml.name() == QLatin1String("choice")) {
            choices << xml.attributes().value(QLatin1String("name")).toString();
        }
    }

    return choices;
}

//! ---- AU-2a Items -------------------------------------------------------

//! Control 47 (appletsSizeSlider.updateIconSize): the not-pressed branch writes
//! iconSize = value and restarts a geometry timer (a live re-layout, not a
//! config write). Only iconSize moves.
void AppearanceHandlerAuditTest::iconSizeSliderWritesOnlyIconSize()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("iconSize"), 48}, {QStringLiteral("proportionIconSize"), -1.0},
                  {QStringLiteral("zoomLevel"), 16}});

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config, QStringLiteral("var value = 64; configuration.iconSize = value;")), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(controlApplies(before, after, QStringLiteral("iconSize")));
    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("iconSize")}));
    QVERIFY(valueReflects(after, QStringLiteral("iconSize"), 64));
}

//! Control 47 display transform: the slider handle binds to
//! `2 * Math.round(iconSize / 2)`, so an odd stored size shows (and, once the
//! handler fires, is written back as) the nearest even value. Math.round breaks
//! the .5 tie upward, so an odd size snaps UP by one (49 -> 50). Pinned so a
//! future "simplify the binding" cannot silently start storing odd sizes.
void AppearanceHandlerAuditTest::iconSizeDisplaySnapsOddToEven()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("iconSize"), 49}});

    const QJsonObject before = snapshot(config);
    //! `value` is the display binding verbatim; the handler then writes it
    QCOMPARE(runHandler(config, QStringLiteral(
        "var value = 2 * Math.round(configuration.iconSize / 2);\n"
        "configuration.iconSize = value;")), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("iconSize")}));
    QVERIFY(valueReflects(after, QStringLiteral("iconSize"), 50));   // 49 snaps up to the nearest even
}

//! Control 48 (proportionSizeSlider.updateProportionIconSize): a slider value
//! of exactly 1 means "no proportional sizing" and is stored as the -1
//! sentinel; any other value is stored raw. Only proportionIconSize moves in
//! both cases.
void AppearanceHandlerAuditTest::proportionIconSizeCollapsesUnityToDisabledSentinel()
{
    static const QString kBody = QStringLiteral(
        "if (value === 1) {\n"
        "    configuration.proportionIconSize = -1;\n"
        "} else {\n"
        "    configuration.proportionIconSize = value;\n"
        "}\n");

    {
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("proportionIconSize"), 5.0}, {QStringLiteral("iconSize"), 48}});
        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config, QStringLiteral("var value = 1;\n") + kBody), QString());
        const QJsonObject after = snapshot(config);
        QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("proportionIconSize")}));
        QVERIFY(valueReflects(after, QStringLiteral("proportionIconSize"), -1));  // unity -> disabled sentinel
    }
    {
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("proportionIconSize"), -1.0}, {QStringLiteral("iconSize"), 48}});
        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config, QStringLiteral("var value = 5;\n") + kBody), QString());
        const QJsonObject after = snapshot(config);
        QVERIFY(controlApplies(before, after, QStringLiteral("proportionIconSize")));
        QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("proportionIconSize")}));
        QVERIFY(valueReflects(after, QStringLiteral("proportionIconSize"), 5));
    }
}

//! D131 (the relative-size percentage hid its reference): the control is a
//! percentage of the output's full height, not of Absolute Size. Keep that
//! reference visible and show the resolved pixel ceiling by default; hovering
//! may reveal the stored percentage, and the sentinel position says Off.
void AppearanceHandlerAuditTest::proportionIconSizeNamesItsReferenceAndShowsPixels()
{
    QFile file(QStringLiteral(APPEARANCE_CONFIG_QML_PATH));
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(file.errorString()));
    const QString source = QString::fromUtf8(file.readAll());

    QVERIFY(source.contains(QStringLiteral(
        "i18nc(\"icon size relative to screen height\", \"Screen height\")")));
    QVERIFY(source.contains(QStringLiteral(
        "i18nc(\"resolved icon size in pixels, e.g. 64 px.\", \"%1 px.\"")));
    QVERIFY(source.contains(QStringLiteral(
        "i18nc(\"percentage of screen height, e.g. 5.0 %\", \"%1 %\"")));
    QVERIFY(source.contains(QStringLiteral(
        "i18nc(\"screen-relative icon sizing is disabled\", \"Off\")")));
    QVERIFY(source.contains(QStringLiteral(
        "i18n(\"Turn Screen height off to use Absolute size.\")")));
}

//! Control 49 (zoomSlider.updateZoomLevel): the slider is a zoom FACTOR
//! (1.0 .. 2.25); the stored zoomLevel is the rounded percentage
//! `round((value - 1) * 20)`, so factor 1.5 stores 10 and factor 2.25 stores
//! 25. Only zoomLevel moves.
void AppearanceHandlerAuditTest::zoomLevelStoresRoundedInverseFactor()
{
    static const QString kBody = QStringLiteral(
        "var result = Math.round((value - 1) * 20);\n"
        "configuration.zoomLevel = result;\n");

    {
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("zoomLevel"), 16}, {QStringLiteral("iconSize"), 48}});
        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config, QStringLiteral("var value = 1.5;\n") + kBody), QString());
        const QJsonObject after = snapshot(config);
        QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("zoomLevel")}));
        QVERIFY(valueReflects(after, QStringLiteral("zoomLevel"), 10));
    }
    {
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("zoomLevel"), 16}, {QStringLiteral("iconSize"), 48}});
        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config, QStringLiteral("var value = 2.25;\n") + kBody), QString());
        const QJsonObject after = snapshot(config);
        QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("zoomLevel")}));
        QVERIFY(valueReflects(after, QStringLiteral("zoomLevel"), 25));
    }
}

//! ---- AU-2b Margins -----------------------------------------------------

//! Controls 57-59 (lengthExtMarginSlider / thickMarginSlider /
//! screenEdgeMarginSlider): each onPressedChanged not-pressed branch writes
//! `<key> = value` and nothing else. Same shape, three keys, each pinned to its
//! own key so a copy-paste that crossed the wires would fail here.
void AppearanceHandlerAuditTest::marginSlidersEachWriteOnlyTheirOwnKey()
{
    struct Case { QString key; int value; };
    const QList<Case> cases = {
        {QStringLiteral("lengthExtMargin"), 12},
        {QStringLiteral("thickMargin"), 20},
        {QStringLiteral("screenEdgeMargin"), 8},
    };

    for (const Case &c : cases) {
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("lengthExtMargin"), 0}, {QStringLiteral("thickMargin"), 6},
                      {QStringLiteral("screenEdgeMargin"), -1}});

        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config, QStringLiteral("var value = %1; configuration.%2 = value;")
                                        .arg(c.value).arg(c.key)), QString());
        const QJsonObject after = snapshot(config);

        QVERIFY2(controlApplies(before, after, c.key), qPrintable(QStringLiteral("P1 %1").arg(c.key)));
        QVERIFY2(onlyExpectedKeysChanged(before, after, {c.key}), qPrintable(QStringLiteral("P2 %1").arg(c.key)));
        QVERIFY(valueReflects(after, c.key, c.value));
    }
}

//! ---- AU-2c Colors ------------------------------------------------------

//! Control 60 (Palette combo): onCurrentIndexChanged writes
//! `themeColors = model[currentIndex].value`. Whatever row is selected, ONLY
//! themeColors moves - no stray write to a neighbouring colour key.
void AppearanceHandlerAuditTest::paletteComboWritesOnlyThemeColors()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("themeColors"), int(Types::PlasmaThemeColors)},
                  {QStringLiteral("windowColors"), int(Types::NoneWindowColors)}});

    const QJsonObject before = snapshot(config);
    //! the write with the selection resolved to its model value (Layout row)
    QCOMPARE(runHandler(config, QStringLiteral("configuration.themeColors = %1;")
                                    .arg(int(Types::LayoutThemeColors))), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(controlApplies(before, after, QStringLiteral("themeColors")));
    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("themeColors")}));
    QVERIFY(valueReflects(after, QStringLiteral("themeColors"), int(Types::LayoutThemeColors)));
}

//! Control 60 S-b, CLEARED by D23: every stored themeColors value must map to a
//! DISTINCT dropdown index and round-trip back to itself
//! (model[colorsToIndex(v)].value === v). Before D23, Reverse was missing from
//! the model while colorsToIndex still returned 3 for it, colliding with Layout
//! on index 3: a Reverse config showed "Layout" and, via onCurrentIndexChanged,
//! was rewritten to Layout. This reproduces the SHIPPED model + colorsToIndex
//! verbatim (LatteContainment.Types.* fed as the real enum values from types.h,
//! not hardcoded numbers) and proves the collision is gone.
void AppearanceHandlerAuditTest::paletteRoundTripsEveryValueToADistinctIndex()
{
    QQmlEngine engine;
    QQmlContext *ctx = engine.rootContext();
    ctx->setContextProperty(QStringLiteral("PlasmaThemeColors"), int(Types::PlasmaThemeColors));
    ctx->setContextProperty(QStringLiteral("DarkThemeColors"), int(Types::DarkThemeColors));
    ctx->setContextProperty(QStringLiteral("LightThemeColors"), int(Types::LightThemeColors));
    ctx->setContextProperty(QStringLiteral("LayoutThemeColors"), int(Types::LayoutThemeColors));
    ctx->setContextProperty(QStringLiteral("ReverseThemeColors"), int(Types::ReverseThemeColors));
    ctx->setContextProperty(QStringLiteral("SmartThemeColors"), int(Types::SmartThemeColors));

    QQmlComponent component(&engine);
    //! model rows + colorsToIndex copied from AppearanceConfig.qml, with
    //! LatteContainment.Types.X -> the injected context property of the same
    //! name (so the numbers are the real enum's, and a types.h reorder flows
    //! straight through into a failing round-trip here)
    component.setData(
        "import QtQml\n"
        "QtObject {\n"
        "  property var paletteModel: [\n"
        "    { value: PlasmaThemeColors },\n"
        "    { value: DarkThemeColors },\n"
        "    { value: LightThemeColors },\n"
        "    { value: LayoutThemeColors },\n"
        "    { value: ReverseThemeColors },\n"
        "    { value: SmartThemeColors }\n"
        "  ]\n"
        "  function colorsToIndex(colors) {\n"
        "    if (colors === PlasmaThemeColors) { return 0; }\n"
        "    else if (colors === DarkThemeColors) { return 1; }\n"
        "    else if (colors === LightThemeColors) { return 2; }\n"
        "    else if (colors === LayoutThemeColors) { return 3; }\n"
        "    else if (colors === ReverseThemeColors) { return 4; }\n"
        "    else if (colors === SmartThemeColors) { return 5; }\n"
        "    return -1;\n"
        "  }\n"
        "  function indexOf(v) { return colorsToIndex(v); }\n"
        "  function valueAt(i) { return paletteModel[i].value; }\n"
        "}\n", QUrl());

    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));

    const QList<int> everyStoredValue = {
        int(Types::PlasmaThemeColors), int(Types::ReverseThemeColors), int(Types::SmartThemeColors),
        int(Types::DarkThemeColors), int(Types::LightThemeColors), int(Types::LayoutThemeColors),
    };

    QSet<int> seenIndices;

    for (int stored : everyStoredValue) {
        QVariant idxVar;
        QMetaObject::invokeMethod(root.data(), "indexOf", Q_RETURN_ARG(QVariant, idxVar),
                                  Q_ARG(QVariant, stored));
        const int idx = idxVar.toInt();

        QVERIFY2(idx >= 0 && idx <= 5,
                 qPrintable(QStringLiteral("themeColors value %1 maps to no dropdown row (colorsToIndex fell through)")
                                .arg(stored)));
        QVERIFY2(!seenIndices.contains(idx),
                 qPrintable(QStringLiteral("themeColors value %1 collides on index %2 (the S-b collision)")
                                .arg(stored).arg(idx)));
        seenIndices.insert(idx);

        QVariant valueVar;
        QMetaObject::invokeMethod(root.data(), "valueAt", Q_RETURN_ARG(QVariant, valueVar),
                                  Q_ARG(QVariant, idx));
        QVERIFY2(valueVar.toInt() == stored,
                 qPrintable(QStringLiteral("themeColors value %1 does not round-trip: the row at its index holds %2")
                                .arg(stored).arg(valueVar.toInt())));
    }

    //! all six values landed on the six distinct rows {0..5}
    QCOMPARE(seenIndices.size(), 6);
}

//! Control 60 depends silently on the C++ enum and the KConfigXT schema numbering
//! agreeing: the QML reads the stored int and compares it to
//! LatteContainment.Types.X, while KConfig maps the same int to a choice NAME by
//! declaration order. If those two orderings disagree, a stored palette means one
//! thing to the dropdown and another to the config system. The static_asserts at
//! the top of this file pin the C++ side; this pins the schema side matches it.
void AppearanceHandlerAuditTest::themeColorsEnumAgreesBetweenTypesAndSchema()
{
    const QStringList schemaOrder = enumChoiceOrder(QStringLiteral(THEMECOLORS_MAINXML_PATH),
                                                    QStringLiteral("themeColors"));
    QVERIFY2(!schemaOrder.isEmpty(), "could not read the themeColors <entry> from main.xml");

    //! declaration-order index == the value KConfig gives the choice; must match
    //! the types.h enum values pinned by the file-top static_asserts
    const QStringList expectedByValue = {
        QStringLiteral("PlasmaThemeColors"),   // 0
        QStringLiteral("ReverseThemeColors"),  // 1
        QStringLiteral("SmartThemeColors"),    // 2
        QStringLiteral("DarkThemeColors"),     // 3
        QStringLiteral("LightThemeColors"),    // 4
        QStringLiteral("LayoutThemeColors"),   // 5
    };

    QCOMPARE(schemaOrder, expectedByValue);
}

//! Control 61 (From Window combo): a straight index==value combo
//! (currentIndex: windowColors; onCurrentIndexChanged: windowColors =
//! currentIndex). The round-trip is trivially identity, but pinned so the combo
//! cannot regress into the mapped S-b shape, and P2 confirms only windowColors
//! moves.
void AppearanceHandlerAuditTest::fromWindowComboRoundTripsAndWritesOnlyWindowColors()
{
    const QList<int> everyValue = {
        int(Types::NoneWindowColors), int(Types::ActiveWindowColors), int(Types::TouchingWindowColors),
    };

    for (int stored : everyValue) {
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("windowColors"), stored},
                      {QStringLiteral("themeColors"), int(Types::PlasmaThemeColors)}});

        const QJsonObject before = snapshot(config);
        //! currentIndex binds to the stored value, the handler writes it back
        QCOMPARE(runHandler(config, QStringLiteral(
            "var currentIndex = configuration.windowColors;\n"
            "configuration.windowColors = currentIndex;")), QString());
        const QJsonObject after = snapshot(config);

        //! identity re-write: nothing changes, and crucially no OTHER key moves
        QVERIFY(changedConfigKeys(before, after).isEmpty());
        QVERIFY(valueReflects(after, QStringLiteral("windowColors"), stored));
    }

    //! and a genuine selection change writes only windowColors
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("windowColors"), int(Types::NoneWindowColors)},
                  {QStringLiteral("themeColors"), int(Types::PlasmaThemeColors)}});
    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config, QStringLiteral("configuration.windowColors = %1;")
                                    .arg(int(Types::TouchingWindowColors))), QString());
    const QJsonObject after = snapshot(config);
    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("windowColors")}));
    QVERIFY(valueReflects(after, QStringLiteral("windowColors"), int(Types::TouchingWindowColors)));
}

//! ---- AU-2d Background --------------------------------------------------

//! Controls 63-66 (panelSizeSlider / transparencySlider / radiusSlider /
//! shadowSlider): each not-pressed update writes `<key> = value` and only that
//! key. panelTransparency/backgroundRadius/backgroundShadowSize all admit a -1
//! "Default" value; the write is the raw slider value either way.
void AppearanceHandlerAuditTest::backgroundSlidersEachWriteOnlyTheirOwnKey()
{
    struct Case { QString key; int value; };
    const QList<Case> cases = {
        {QStringLiteral("panelSize"), 8},
        {QStringLiteral("panelTransparency"), 40},
        {QStringLiteral("backgroundRadius"), 12},
        {QStringLiteral("backgroundShadowSize"), 20},
    };

    for (const Case &c : cases) {
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("panelSize"), 4}, {QStringLiteral("panelTransparency"), -1},
                      {QStringLiteral("backgroundRadius"), -1}, {QStringLiteral("backgroundShadowSize"), -1},
                      {QStringLiteral("useThemePanel"), true}});

        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config, QStringLiteral("var value = %1; configuration.%2 = value;")
                                        .arg(c.value).arg(c.key)), QString());
        const QJsonObject after = snapshot(config);

        QVERIFY2(controlApplies(before, after, c.key), qPrintable(QStringLiteral("P1 %1").arg(c.key)));
        QVERIFY2(onlyExpectedKeysChanged(before, after, {c.key}), qPrintable(QStringLiteral("P2 %1").arg(c.key)));
        QVERIFY(valueReflects(after, c.key, c.value));
    }
}

//! Controls 62, 67-74: every background toggle writes only its own bool. Three
//! shapes are represented exactly as shipped: the HeaderSwitch and the four
//! CheckBoxes invert the stored value (`= !configuration.<key>`); the four
//! checkable Buttons assign their post-click `checked` state (`= checked`).
void AppearanceHandlerAuditTest::backgroundTogglesEachWriteOnlyTheirOwnKey()
{
    //! control 62 (Background switch) + the four Dynamic-Visibility/Exceptions
    //! checkboxes: the invert shape
    const QStringList invertKeys = {
        QStringLiteral("useThemePanel"),                  // 62
        QStringLiteral("solidBackgroundForMaximized"),    // 71
        QStringLiteral("backgroundOnlyOnMaximized"),      // 72
        QStringLiteral("disablePanelShadowForMaximized"), // 73
        QStringLiteral("plasmaBackgroundForPopups"),      // 74
    };

    for (const QString &key : invertKeys) {
        for (bool start : {false, true}) {
            QQmlPropertyMap config;
            seed(config, {{QStringLiteral("useThemePanel"), true},
                          {QStringLiteral("solidBackgroundForMaximized"), false},
                          {QStringLiteral("backgroundOnlyOnMaximized"), false},
                          {QStringLiteral("disablePanelShadowForMaximized"), false},
                          {QStringLiteral("plasmaBackgroundForPopups"), false}});
            config.insert(key, start);

            const QJsonObject before = snapshot(config);
            QCOMPARE(runHandler(config, QStringLiteral("configuration.%1 = !configuration.%1;").arg(key)), QString());
            const QJsonObject after = snapshot(config);

            QVERIFY2(onlyExpectedKeysChanged(before, after, {key}),
                     qPrintable(QStringLiteral("P2 invert %1").arg(key)));
            QVERIFY(valueReflects(after, key, !start));
        }
    }

    //! controls 67-70 (Blur / Shadows / Outline / All Corners): the assign-checked
    //! shape - a checkable button writes its post-click `checked` state
    const QStringList checkedKeys = {
        QStringLiteral("blurEnabled"),          // 67
        QStringLiteral("panelShadows"),         // 68
        QStringLiteral("panelOutline"),         // 69
        QStringLiteral("backgroundAllCorners"), // 70
    };

    for (const QString &key : checkedKeys) {
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("blurEnabled"), false}, {QStringLiteral("panelShadows"), true},
                      {QStringLiteral("panelOutline"), false}, {QStringLiteral("backgroundAllCorners"), false},
                      {QStringLiteral("useThemePanel"), true}});

        const QJsonObject before = snapshot(config);
        //! the button's `checked` has already toggled when onClicked fires; drive
        //! the write with the flipped state
        QCOMPARE(runHandler(config, QStringLiteral(
            "var checked = !configuration.%1;\n"
            "configuration.%1 = checked;").arg(key)), QString());
        const QJsonObject after = snapshot(config);

        QVERIFY2(controlApplies(before, after, key), qPrintable(QStringLiteral("P1 %1").arg(key)));
        QVERIFY2(onlyExpectedKeysChanged(before, after, {key}), qPrintable(QStringLiteral("P2 %1").arg(key)));
    }
}

QTEST_GUILESS_MAIN(AppearanceHandlerAuditTest)

#include "appearancehandleraudittest.moc"
