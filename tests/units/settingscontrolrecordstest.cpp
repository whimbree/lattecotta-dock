/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "settingscontrolrecords.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <limits>

using namespace Latte::SettingsControls;

namespace {

HitRecord hit(QString role = QStringLiteral("primary"))
{
    return {std::move(role), QRectF(10.25, -2.5, 40.75, 15.125),
            QRectF(12.5, -1.25, 20.5, 10.75), true};
}

StateRecord state(Scalar current = Scalar{std::monostate{}})
{
    return {{hit()}, true, true, false, std::move(current)};
}

PopupRowRecord row(QString identity, int index, Scalar value)
{
    return {std::move(identity), QStringLiteral("option"), std::nullopt,
            index, std::move(value), state(QStringLiteral("row-current"))};
}

ControlRecord control(QString identity = QStringLiteral("appearance.zoom"),
                      std::optional<QString> instance = std::nullopt)
{
    ControlRecord record;
    record.containmentId = 7;
    record.surface = QStringLiteral("primary-settings");
    record.loadGeneration = 9007199254740993ULL;
    record.appletId = 42;
    record.auditIdentity = std::move(identity);
    record.kind = QStringLiteral("slider");
    record.instanceKey = std::move(instance);
    record.state = state(qint64{55});
    return record;
}

QStringList sortedKeys(const QJsonObject &object)
{
    QStringList keys = object.keys();
    keys.sort();
    return keys;
}

QJsonArray serializedArray(const QList<ControlRecord> &records)
{
    const auto result = serializeControlRecords(records);
    if (!result.accepted()) {
        return {};
    }
    return QJsonDocument::fromJson(result.data.toUtf8()).array();
}

}

class SettingsControlRecordsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void everyScalarAlternativeKeepsItsJsonType();
    void controlRecordPinsExactKeysTypesAndFractions();
    void nullOptionalsAndNonPopupAreExplicit();
    void emptyAggregateIsCompactArray();
    void popupClosedAndOpenShapesAreExact();
    void completeIdentityAndRowsSortDeterministically();
    void malformedControlStatesRefuseWholeAggregate();
    void malformedPopupStatesRefuseWholeAggregate();
};

void SettingsControlRecordsTest::everyScalarAlternativeKeepsItsJsonType()
{
    const QList<Scalar> values{std::monostate{}, true, qint64{9223372036854775807LL},
                               12.75, QStringLiteral("exact")};
    const QList<QJsonValue::Type> expected{QJsonValue::Null, QJsonValue::Bool,
                                           QJsonValue::Double, QJsonValue::Double,
                                           QJsonValue::String};

    for (int index = 0; index < values.size(); ++index) {
        const QJsonValue json = serializeScalar(values.at(index));
        QCOMPARE(json.type(), expected.at(index));
    }

    QCOMPARE(serializeScalar(std::get<qint64>(values.at(2))).toInteger(),
             std::numeric_limits<qint64>::max());
    QCOMPARE(serializeScalar(std::get<double>(values.at(3))).toDouble(), 12.75);
    QCOMPARE(serializeScalar(std::get<QString>(values.at(4))).toString(), QStringLiteral("exact"));
}

void SettingsControlRecordsTest::controlRecordPinsExactKeysTypesAndFractions()
{
    const QJsonObject json = serializedArray({control()}).at(0).toObject();
    QCOMPARE(sortedKeys(json),
             (QStringList{QStringLiteral("appletId"), QStringLiteral("auditIdentity"),
                          QStringLiteral("containmentId"), QStringLiteral("current"),
                          QStringLiteral("enabled"), QStringLiteral("focused"),
                          QStringLiteral("hits"), QStringLiteral("instanceKey"),
                          QStringLiteral("kind"), QStringLiteral("loadGeneration"),
                          QStringLiteral("popup"), QStringLiteral("surface"),
                          QStringLiteral("visible")}));
    QCOMPARE(json.value(QStringLiteral("containmentId")).toInteger(), 7);
    QCOMPARE(json.value(QStringLiteral("surface")).toString(), QStringLiteral("primary-settings"));
    QCOMPARE(json.value(QStringLiteral("loadGeneration")).type(), QJsonValue::String);
    QCOMPARE(json.value(QStringLiteral("loadGeneration")).toString(), QStringLiteral("9007199254740993"));
    QCOMPARE(json.value(QStringLiteral("appletId")).toInteger(), 42);
    QCOMPARE(json.value(QStringLiteral("auditIdentity")).toString(), QStringLiteral("appearance.zoom"));
    QCOMPARE(json.value(QStringLiteral("kind")).toString(), QStringLiteral("slider"));
    QCOMPARE(json.value(QStringLiteral("current")).toInteger(), 55);

    const QJsonObject hitJson = json.value(QStringLiteral("hits")).toArray().at(0).toObject();
    QCOMPARE(sortedKeys(hitJson),
             (QStringList{QStringLiteral("clippedGeometry"), QStringLiteral("globalGeometry"),
                          QStringLiteral("mapped"), QStringLiteral("role")}));
    QCOMPARE(hitJson.value(QStringLiteral("globalGeometry")).toArray(),
             (QJsonArray{10.25, -2.5, 40.75, 15.125}));
    QCOMPARE(hitJson.value(QStringLiteral("clippedGeometry")).toArray(),
             (QJsonArray{12.5, -1.25, 20.5, 10.75}));
    QVERIFY(hitJson.value(QStringLiteral("mapped")).toBool());
}

void SettingsControlRecordsTest::nullOptionalsAndNonPopupAreExplicit()
{
    ControlRecord record = control();
    record.appletId.reset();
    record.instanceKey.reset();
    record.state.current = std::monostate{};
    record.state.hits[0].mapped = false;
    record.state.hits[0].clippedGeometry.reset();

    const QJsonObject json = serializedArray({record}).at(0).toObject();
    QVERIFY(json.value(QStringLiteral("appletId")).isNull());
    QVERIFY(json.value(QStringLiteral("instanceKey")).isNull());
    QVERIFY(json.value(QStringLiteral("current")).isNull());
    QVERIFY(json.value(QStringLiteral("popup")).isNull());
    QVERIFY(json.value(QStringLiteral("hits")).toArray().at(0).toObject()
                .value(QStringLiteral("clippedGeometry")).isNull());
}

void SettingsControlRecordsTest::emptyAggregateIsCompactArray()
{
    const auto result = serializeControlRecords({});
    QVERIFY(result.accepted());
    QCOMPARE(result.data, QStringLiteral("[]"));
}

void SettingsControlRecordsTest::popupClosedAndOpenShapesAreExact()
{
    ControlRecord record = control();
    PopupRecord popup;
    popup.rows = {row(QStringLiteral("row.separator"), 1, std::monostate{}),
                  row(QStringLiteral("row.first"), 0, QStringLiteral("stable-first"))};
    popup.rows[0].kind = QStringLiteral("separator");
    popup.rows[0].state.enabled = false;
    record.popup = popup;

    QJsonObject popupJson = serializedArray({record}).at(0).toObject()
                                .value(QStringLiteral("popup")).toObject();
    QCOMPARE(sortedKeys(popupJson),
             (QStringList{QStringLiteral("generation"), QStringLiteral("mapped"),
                          QStringLiteral("open"), QStringLiteral("rows")}));
    QVERIFY(!popupJson.value(QStringLiteral("open")).toBool());
    QVERIFY(!popupJson.value(QStringLiteral("mapped")).toBool());
    QVERIFY(popupJson.value(QStringLiteral("generation")).isNull());
    QCOMPARE(popupJson.value(QStringLiteral("rows")).toArray().size(), 2);

    const QJsonObject firstRow = popupJson.value(QStringLiteral("rows")).toArray().at(0).toObject();
    QCOMPARE(sortedKeys(firstRow),
             (QStringList{QStringLiteral("auditIdentity"), QStringLiteral("current"),
                          QStringLiteral("enabled"), QStringLiteral("focused"),
                          QStringLiteral("hits"), QStringLiteral("instanceKey"),
                          QStringLiteral("kind"), QStringLiteral("value"),
                          QStringLiteral("visible"), QStringLiteral("visualIndex")}));
    QCOMPARE(firstRow.value(QStringLiteral("auditIdentity")).toString(), QStringLiteral("row.first"));
    QCOMPARE(firstRow.value(QStringLiteral("value")).toString(), QStringLiteral("stable-first"));

    record.popup->open = true;
    record.popup->mapped = true;
    record.popup->generation = 9007199254740995ULL;
    popupJson = serializedArray({record}).at(0).toObject().value(QStringLiteral("popup")).toObject();
    QVERIFY(popupJson.value(QStringLiteral("open")).toBool());
    QVERIFY(popupJson.value(QStringLiteral("mapped")).toBool());
    QCOMPARE(popupJson.value(QStringLiteral("generation")).type(), QJsonValue::String);
    QCOMPARE(popupJson.value(QStringLiteral("generation")).toString(), QStringLiteral("9007199254740995"));
    const QJsonObject separator = popupJson.value(QStringLiteral("rows")).toArray().at(1).toObject();
    QCOMPARE(separator.value(QStringLiteral("kind")).toString(), QStringLiteral("separator"));
    QVERIFY(separator.value(QStringLiteral("value")).isNull());
    QVERIFY(!separator.value(QStringLiteral("enabled")).toBool());
}

void SettingsControlRecordsTest::completeIdentityAndRowsSortDeterministically()
{
    ControlRecord second = control(QStringLiteral("same"), QStringLiteral("b"));
    second.popup = PopupRecord{false, false, std::nullopt,
                               {row(QStringLiteral("row.two"), 2, qint64{2}),
                                row(QStringLiteral("row.zero"), 0, qint64{0}),
                                row(QStringLiteral("row.one"), 1, qint64{1})}};
    ControlRecord first = control(QStringLiteral("same"), QStringLiteral("a"));

    const auto forward = serializeControlRecords({first, second});
    const auto reverse = serializeControlRecords({second, first});
    QVERIFY(forward.accepted());
    QCOMPARE(forward.data, reverse.data);

    const QJsonArray array = QJsonDocument::fromJson(forward.data.toUtf8()).array();
    QCOMPARE(array.at(0).toObject().value(QStringLiteral("instanceKey")).toString(), QStringLiteral("a"));
    const QJsonArray rows = array.at(1).toObject().value(QStringLiteral("popup")).toObject()
                                .value(QStringLiteral("rows")).toArray();
    QCOMPARE(rows.at(0).toObject().value(QStringLiteral("visualIndex")).toInt(), 0);
    QCOMPARE(rows.at(1).toObject().value(QStringLiteral("visualIndex")).toInt(), 1);
    QCOMPARE(rows.at(2).toObject().value(QStringLiteral("visualIndex")).toInt(), 2);
}

void SettingsControlRecordsTest::malformedControlStatesRefuseWholeAggregate()
{
    auto refuses = [](ControlRecord malformed) {
        const auto result = serializeControlRecords({control(QStringLiteral("valid")), malformed});
        QVERIFY(!result.accepted());
        QCOMPARE(result.data, QStringLiteral("[]"));
        QVERIFY(!result.refusal.isEmpty());
    };

    ControlRecord malformed = control(QStringLiteral("bad"));
    malformed.surface.clear();
    refuses(malformed);
    malformed = control(QStringLiteral("bad"));
    malformed.loadGeneration = 0;
    refuses(malformed);
    malformed = control(QStringLiteral("bad"));
    malformed.appletId = -1;
    refuses(malformed);
    malformed = control(QStringLiteral("bad"));
    malformed.instanceKey = QString();
    refuses(malformed);
    malformed = control(QStringLiteral("bad"));
    malformed.state.hits.clear();
    refuses(malformed);
    malformed = control(QStringLiteral("bad"));
    malformed.state.hits[0].role.clear();
    refuses(malformed);
    malformed = control(QStringLiteral("bad"));
    malformed.state.hits[0].globalGeometry.setWidth(-1.0);
    refuses(malformed);
    malformed = control(QStringLiteral("bad"));
    malformed.state.hits[0].mapped = false;
    refuses(malformed);
    malformed = control(QStringLiteral("bad"));
    malformed.state.current = std::numeric_limits<double>::infinity();
    refuses(malformed);

    const ControlRecord duplicate = control(QStringLiteral("duplicate"));
    const auto duplicateResult = serializeControlRecords({duplicate, duplicate});
    QVERIFY(!duplicateResult.accepted());
    QCOMPARE(duplicateResult.data, QStringLiteral("[]"));
}

void SettingsControlRecordsTest::malformedPopupStatesRefuseWholeAggregate()
{
    auto refuses = [](PopupRecord popup) {
        ControlRecord malformed = control();
        malformed.popup = std::move(popup);
        const auto result = serializeControlRecords({malformed});
        QVERIFY(!result.accepted());
        QCOMPARE(result.data, QStringLiteral("[]"));
    };

    refuses(PopupRecord{true, false, std::nullopt, {}});
    refuses(PopupRecord{false, false, 4, {}});
    refuses(PopupRecord{false, true, std::nullopt, {}});
    refuses(PopupRecord{true, true, 0, {}});

    PopupRowRecord malformedRow = row(QString(), 0, QStringLiteral("value"));
    refuses(PopupRecord{false, false, std::nullopt, {malformedRow}});
    malformedRow = row(QStringLiteral("row"), -1, QStringLiteral("value"));
    refuses(PopupRecord{false, false, std::nullopt, {malformedRow}});
    malformedRow = row(QStringLiteral("row"), 0, std::numeric_limits<double>::quiet_NaN());
    refuses(PopupRecord{false, false, std::nullopt, {malformedRow}});

    const PopupRowRecord duplicate = row(QStringLiteral("row"), 0, QStringLiteral("value"));
    refuses(PopupRecord{false, false, std::nullopt, {duplicate, duplicate}});
    refuses(PopupRecord{false, false, std::nullopt,
                        {duplicate, row(QStringLiteral("other"), 0, QStringLiteral("other"))}});
}

QTEST_MAIN(SettingsControlRecordsTest)

#include "settingscontrolrecordstest.moc"
