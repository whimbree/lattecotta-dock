/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef SETTINGSCONTROLRECORDS_H
#define SETTINGSCONTROLRECORDS_H

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QRectF>
#include <QString>

#include <algorithm>
#include <cmath>
#include <optional>
#include <tuple>
#include <variant>

namespace Latte::SettingsControls {

//! Closed scalar domain shared by control current values and popup row values.
//! std::monostate is the deliberate JSON null alternative. Generation values
//! do not use this type because identity tokens serialize as decimal strings.
using Scalar = std::variant<std::monostate, bool, qint64, double, QString>;

struct HitRecord {
    QString role;
    QRectF globalGeometry;
    std::optional<QRectF> clippedGeometry;
    bool mapped{false};
};

struct StateRecord {
    QList<HitRecord> hits;
    bool visible{false};
    bool enabled{false};
    bool focused{false};
    Scalar current;
};

struct PopupRowRecord {
    QString auditIdentity;
    QString kind;
    std::optional<QString> instanceKey;
    int visualIndex{-1};
    Scalar value;
    StateRecord state;
};

struct PopupRecord {
    bool open{false};
    bool mapped{false};
    std::optional<quint64> generation;
    QList<PopupRowRecord> rows;
};

struct ControlRecord {
    uint containmentId{0};
    QString surface;
    quint64 loadGeneration{0};
    std::optional<qint64> appletId;
    QString auditIdentity;
    QString kind;
    std::optional<QString> instanceKey;
    StateRecord state;
    std::optional<PopupRecord> popup;
};

struct SerializationResult {
    QString data{QStringLiteral("[]")};
    QString refusal;

    bool accepted() const
    {
        return refusal.isEmpty();
    }
};

inline bool scalarIsValid(const Scalar &value)
{
    if (const auto *number = std::get_if<double>(&value)) {
        return std::isfinite(*number);
    }

    return true;
}

inline QJsonValue serializeScalar(const Scalar &value)
{
    return std::visit([](const auto &typed) -> QJsonValue {
        using T = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return QJsonValue(QJsonValue::Null);
        } else if constexpr (std::is_same_v<T, bool>) {
            return QJsonValue(typed);
        } else if constexpr (std::is_same_v<T, qint64>) {
            return QJsonValue(typed);
        } else if constexpr (std::is_same_v<T, double>) {
            return QJsonValue(typed);
        } else if constexpr (std::is_same_v<T, QString>) {
            return QJsonValue(typed);
        }
    }, value);
}

inline bool rectIsFiniteAndPositive(const QRectF &rect)
{
    return std::isfinite(rect.x()) && std::isfinite(rect.y())
        && std::isfinite(rect.width()) && std::isfinite(rect.height())
        && rect.width() > 0.0 && rect.height() > 0.0;
}

inline QJsonArray serializeRect(const QRectF &rect)
{
    return QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()};
}

inline QJsonValue serializeOptionalString(const std::optional<QString> &value)
{
    return value ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
}

inline QString validateState(const StateRecord &state, const QString &path)
{
    if (state.hits.isEmpty()) {
        return path + QStringLiteral(" has no hit records");
    }

    if (!scalarIsValid(state.current)) {
        return path + QStringLiteral(" has a non-finite current value");
    }

    QStringList roles;
    for (const auto &hit : state.hits) {
        if (hit.role.isEmpty()) {
            return path + QStringLiteral(" has an empty hit role");
        }
        if (roles.contains(hit.role)) {
            return path + QStringLiteral(" has duplicate hit role ") + hit.role;
        }
        roles.append(hit.role);

        if (!rectIsFiniteAndPositive(hit.globalGeometry)) {
            return path + QStringLiteral(" has invalid global hit geometry");
        }
        if (hit.mapped != hit.clippedGeometry.has_value()) {
            return path + QStringLiteral(" has inconsistent mapped and clipped hit state");
        }
        if (hit.clippedGeometry) {
            if (!rectIsFiniteAndPositive(*hit.clippedGeometry)) {
                return path + QStringLiteral(" has invalid clipped hit geometry");
            }
            if (!hit.globalGeometry.contains(*hit.clippedGeometry)) {
                return path + QStringLiteral(" has clipped geometry outside its global geometry");
            }
        }
    }

    return {};
}

inline auto optionalStringSortKey(const std::optional<QString> &value)
{
    return std::tuple{value.has_value(), value.value_or(QString())};
}

inline auto optionalIntegerSortKey(const std::optional<qint64> &value)
{
    return std::tuple{value.has_value(), value.value_or(0)};
}

inline auto rowIdentityKey(const PopupRowRecord &row)
{
    return std::tuple{row.auditIdentity, row.kind, optionalStringSortKey(row.instanceKey)};
}

inline auto controlIdentityKey(const ControlRecord &record)
{
    return std::tuple{record.containmentId, record.surface, record.loadGeneration,
                      optionalIntegerSortKey(record.appletId), record.auditIdentity,
                      record.kind, optionalStringSortKey(record.instanceKey)};
}

inline QString validatePopup(PopupRecord &popup, const QString &path)
{
    if (popup.open != popup.generation.has_value()) {
        return path + QStringLiteral(" has inconsistent open and generation state");
    }
    if (popup.generation && *popup.generation == 0) {
        return path + QStringLiteral(" has a zero generation");
    }
    if (popup.mapped && !popup.open) {
        return path + QStringLiteral(" is mapped while closed");
    }

    QList<decltype(rowIdentityKey(PopupRowRecord{}))> identities;
    QList<int> visualIndexes;
    for (auto &row : popup.rows) {
        if (row.auditIdentity.isEmpty() || row.kind.isEmpty()) {
            return path + QStringLiteral(" has a row with empty identity or kind");
        }
        if (row.instanceKey && row.instanceKey->isEmpty()) {
            return path + QStringLiteral(" has a row with an empty instance key");
        }
        if (row.visualIndex < 0) {
            return path + QStringLiteral(" has a row with a negative visual index");
        }
        if (!scalarIsValid(row.value)) {
            return path + QStringLiteral(" has a row with a non-finite stable value");
        }

        const QString stateRefusal = validateState(
            row.state, path + QStringLiteral(" row ") + row.auditIdentity);
        if (!stateRefusal.isEmpty()) {
            return stateRefusal;
        }

        const auto identity = rowIdentityKey(row);
        if (identities.contains(identity)) {
            return path + QStringLiteral(" has duplicate row identity ") + row.auditIdentity;
        }
        if (visualIndexes.contains(row.visualIndex)) {
            return path + QStringLiteral(" has duplicate row visual index ")
                + QString::number(row.visualIndex);
        }
        identities.append(identity);
        visualIndexes.append(row.visualIndex);
    }

    std::sort(popup.rows.begin(), popup.rows.end(), [](const auto &left, const auto &right) {
        return std::tuple{left.visualIndex, rowIdentityKey(left)}
            < std::tuple{right.visualIndex, rowIdentityKey(right)};
    });
    return {};
}

inline QJsonObject serializeHitRecord(const HitRecord &hit)
{
    QJsonObject json;
    json[QStringLiteral("role")] = hit.role;
    json[QStringLiteral("globalGeometry")] = serializeRect(hit.globalGeometry);
    json[QStringLiteral("clippedGeometry")] = hit.clippedGeometry
        ? QJsonValue(serializeRect(*hit.clippedGeometry)) : QJsonValue(QJsonValue::Null);
    json[QStringLiteral("mapped")] = hit.mapped;
    return json;
}

inline QJsonArray serializeHits(const QList<HitRecord> &hits)
{
    QJsonArray json;
    for (const auto &hit : hits) {
        json.append(serializeHitRecord(hit));
    }
    return json;
}

inline void serializeStateInto(QJsonObject &json, const StateRecord &state)
{
    json[QStringLiteral("hits")] = serializeHits(state.hits);
    json[QStringLiteral("visible")] = state.visible;
    json[QStringLiteral("enabled")] = state.enabled;
    json[QStringLiteral("focused")] = state.focused;
    json[QStringLiteral("current")] = serializeScalar(state.current);
}

inline QJsonObject serializePopupRow(const PopupRowRecord &row)
{
    QJsonObject json;
    json[QStringLiteral("auditIdentity")] = row.auditIdentity;
    json[QStringLiteral("kind")] = row.kind;
    json[QStringLiteral("instanceKey")] = serializeOptionalString(row.instanceKey);
    json[QStringLiteral("visualIndex")] = row.visualIndex;
    json[QStringLiteral("value")] = serializeScalar(row.value);
    serializeStateInto(json, row.state);
    return json;
}

inline QJsonObject serializePopup(const PopupRecord &popup)
{
    QJsonArray rows;
    for (const auto &row : popup.rows) {
        rows.append(serializePopupRow(row));
    }

    QJsonObject json;
    json[QStringLiteral("open")] = popup.open;
    json[QStringLiteral("mapped")] = popup.mapped;
    json[QStringLiteral("generation")] = popup.generation
        ? QJsonValue(QString::number(*popup.generation)) : QJsonValue(QJsonValue::Null);
    json[QStringLiteral("rows")] = rows;
    return json;
}

inline QJsonObject serializeControl(const ControlRecord &record)
{
    QJsonObject json;
    json[QStringLiteral("containmentId")] = static_cast<qint64>(record.containmentId);
    json[QStringLiteral("surface")] = record.surface;
    json[QStringLiteral("loadGeneration")] = QString::number(record.loadGeneration);
    json[QStringLiteral("appletId")] = record.appletId
        ? QJsonValue(*record.appletId) : QJsonValue(QJsonValue::Null);
    json[QStringLiteral("auditIdentity")] = record.auditIdentity;
    json[QStringLiteral("kind")] = record.kind;
    json[QStringLiteral("instanceKey")] = serializeOptionalString(record.instanceKey);
    serializeStateInto(json, record.state);
    json[QStringLiteral("popup")] = record.popup
        ? QJsonValue(serializePopup(*record.popup)) : QJsonValue(QJsonValue::Null);
    return json;
}

//! Validate the complete aggregate before emitting any record. Sorting operates
//! on the value snapshot, so registration order cannot leak into D-Bus output.
inline SerializationResult serializeControlRecords(QList<ControlRecord> records)
{
    for (auto &record : records) {
        if (record.surface.isEmpty() || record.auditIdentity.isEmpty() || record.kind.isEmpty()) {
            return {QStringLiteral("[]"), QStringLiteral("control has empty surface, identity, or kind")};
        }
        if (record.loadGeneration == 0) {
            return {QStringLiteral("[]"), QStringLiteral("control has a zero load generation")};
        }
        if (record.appletId && *record.appletId < 0) {
            return {QStringLiteral("[]"), QStringLiteral("control has a negative applet id")};
        }
        if (record.instanceKey && record.instanceKey->isEmpty()) {
            return {QStringLiteral("[]"), QStringLiteral("control has an empty instance key")};
        }

        const QString stateRefusal = validateState(record.state, QStringLiteral("control ") + record.auditIdentity);
        if (!stateRefusal.isEmpty()) {
            return {QStringLiteral("[]"), stateRefusal};
        }

        if (record.popup) {
            const QString popupRefusal = validatePopup(*record.popup,
                                                       QStringLiteral("control ") + record.auditIdentity + QStringLiteral(" popup"));
            if (!popupRefusal.isEmpty()) {
                return {QStringLiteral("[]"), popupRefusal};
            }
        }
    }

    std::sort(records.begin(), records.end(), [](const auto &left, const auto &right) {
        return controlIdentityKey(left) < controlIdentityKey(right);
    });

    for (int index = 1; index < records.size(); ++index) {
        if (controlIdentityKey(records.at(index - 1)) == controlIdentityKey(records.at(index))) {
            return {QStringLiteral("[]"), QStringLiteral("aggregate has duplicate complete control identity")};
        }
    }

    QJsonArray json;
    for (const auto &record : records) {
        json.append(serializeControl(record));
    }

    return {QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)), {}};
}

}

#endif
