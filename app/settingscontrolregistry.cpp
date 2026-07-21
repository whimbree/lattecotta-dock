/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "settingscontrolregistry.h"

#include <QCoreApplication>
#include <QDebug>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QQuickWindow>
#include <QThread>
#include <QTransform>
#include <QVariant>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <tuple>

namespace Latte
{

namespace
{

bool finiteRectCoordinates(const QRectF &rect)
{
    return std::isfinite(rect.x()) && std::isfinite(rect.y()) && std::isfinite(rect.width()) &&
           std::isfinite(rect.height());
}

bool transformPreservesAxisAlignedRectangles(const QTransform &transform)
{
    constexpr qreal epsilon = 1.0e-12;
    const std::array values{transform.m11(), transform.m12(), transform.m13(), transform.m21(), transform.m22(),
                            transform.m23(), transform.m31(), transform.m32(), transform.m33()};
    if (!std::all_of(values.begin(), values.end(), [](qreal value) { return std::isfinite(value); }))
    {
        return false;
    }

    return transform.isAffine() && std::abs(transform.m12()) <= epsilon && std::abs(transform.m21()) <= epsilon &&
           std::abs(transform.m13()) <= epsilon && std::abs(transform.m23()) <= epsilon &&
           std::abs(transform.m33() - 1.0) <= epsilon && transform.m11() > 0.0 && transform.m22() > 0.0;
}

bool isVisualDescendantOf(QQuickItem *item, QQuickItem *ancestor)
{
    for (QQuickItem *cursor = item; cursor; cursor = cursor->parentItem())
    {
        if (cursor == ancestor)
        {
            return true;
        }
    }
    return false;
}

bool inspectTransformsToSurface(QQuickItem *item, QQuickItem *surfaceItem, QString &refusal)
{
    for (QQuickItem *cursor = item; cursor && cursor != surfaceItem; cursor = cursor->parentItem())
    {
        QQuickItem *parent = cursor->parentItem();
        if (!parent)
        {
            refusal = QStringLiteral("item is outside the registered surface ancestry");
            return false;
        }

        bool ok = false;
        const QTransform transform = cursor->itemTransform(parent, &ok);
        if (!ok || !transformPreservesAxisAlignedRectangles(transform))
        {
            refusal = QStringLiteral("rotation, shear, projective, mirrored, or non-finite "
                                     "transform cannot describe one hit rectangle");
            return false;
        }
    }

    if (!item || !surfaceItem || !isVisualDescendantOf(item, surfaceItem))
    {
        refusal = QStringLiteral("item is outside the registered surface ancestry");
        return false;
    }
    return true;
}

bool effectiveItemState(QQuickItem *item, QQuickItem *surfaceItem, bool &visible, bool &enabled, QString &refusal)
{
    visible = true;
    enabled = true;
    for (QQuickItem *cursor = item; cursor; cursor = cursor->parentItem())
    {
        visible = visible && cursor->isVisible();
        enabled = enabled && cursor->isEnabled();
        if (cursor == surfaceItem)
        {
            return true;
        }
    }

    refusal = QStringLiteral("state item is outside the registered surface ancestry");
    return false;
}

bool itemContainsActiveFocus(QQuickItem *item, QQuickItem *surfaceItem)
{
    QQuickWindow *window = surfaceItem ? surfaceItem->window() : nullptr;
    for (QQuickItem *cursor = window ? window->activeFocusItem() : nullptr; cursor; cursor = cursor->parentItem())
    {
        if (cursor == item)
        {
            return true;
        }
    }
    return false;
}

} // namespace

SettingsControlRegistry::SettingsControlRegistry(QObject *parent) : QObject(parent) {}

bool SettingsControlRegistry::requireGuiThread(const char *operation) const
{
    QCoreApplication *application = QCoreApplication::instance();
    Q_ASSERT(application);
    Q_ASSERT(thread() == application->thread());
    Q_ASSERT(QThread::currentThread() == application->thread());

    if (!application || thread() != application->thread() || QThread::currentThread() != application->thread())
    {
        qWarning() << "settings control registry:" << operation << "refused outside the GUI thread";
        return false;
    }
    return true;
}

std::optional<quint64> SettingsControlRegistry::allocateGeneration()
{
    if (m_lastGeneration == std::numeric_limits<quint64>::max())
    {
        qWarning() << "settings control registry: process generation space exhausted";
        return std::nullopt;
    }
    return ++m_lastGeneration;
}

std::optional<quint64> SettingsControlRegistry::allocateObjectToken()
{
    if (m_lastObjectToken == std::numeric_limits<quint64>::max())
    {
        qWarning() << "settings control registry: object token space exhausted";
        return std::nullopt;
    }
    return ++m_lastObjectToken;
}

bool SettingsControlRegistry::isVisualDescendant(QQuickItem *item, QQuickItem *ancestor)
{
    return isVisualDescendantOf(item, ancestor);
}

std::optional<SettingsControls::Scalar> SettingsControlRegistry::scalarFromVariant(const QVariant &value)
{
    if (!value.isValid() || value.isNull() || value.typeId() == QMetaType::Nullptr)
    {
        return SettingsControls::Scalar{std::monostate{}};
    }

    switch (value.typeId())
    {
    case QMetaType::Bool:
        return SettingsControls::Scalar{value.toBool()};
    case QMetaType::Int:
        return SettingsControls::Scalar{static_cast<qint64>(value.toInt())};
    case QMetaType::LongLong:
        return SettingsControls::Scalar{value.toLongLong()};
    case QMetaType::Double:
        return SettingsControls::Scalar{value.toDouble()};
    case QMetaType::QString:
        return SettingsControls::Scalar{value.toString()};
    default:
        return std::nullopt;
    }
}

bool SettingsControlRegistry::descriptorPropertyIsScalar(QObject *object, const QByteArray &property,
                                                         QString &refusal) const
{
    if (!object || property.isEmpty() || object->metaObject()->indexOfProperty(property.constData()) < 0)
    {
        refusal = QStringLiteral("current property is missing");
        return false;
    }

    const auto scalar = scalarFromVariant(object->property(property.constData()));
    if (!scalar || !SettingsControls::scalarIsValid(*scalar))
    {
        refusal = QStringLiteral("current property is outside the closed scalar domain");
        return false;
    }
    return true;
}

bool SettingsControlRegistry::hitDescriptorsAreValid(QQuickItem *surfaceItem, const QList<HitDescriptor> &hits,
                                                     QString &refusal) const
{
    if (hits.isEmpty())
    {
        refusal = QStringLiteral("descriptor has no hit records");
        return false;
    }

    QSet<QString> roles;
    for (const auto &hit : hits)
    {
        if (hit.role.isEmpty() || !hit.item || !isVisualDescendant(hit.item, surfaceItem))
        {
            refusal = QStringLiteral("hit role is empty or its item is outside the surface");
            return false;
        }
        if (roles.contains(hit.role))
        {
            refusal = QStringLiteral("descriptor has duplicate hit role ") + hit.role;
            return false;
        }
        roles.insert(hit.role);
    }
    return true;
}

std::optional<quint64> SettingsControlRegistry::replaceScope(const ScopeDescriptor &descriptor)
{
    if (!requireGuiThread("replaceScope"))
    {
        return std::nullopt;
    }

    if (descriptor.surface.isEmpty() || !descriptor.lifetimeObject || !descriptor.surfaceItem ||
        !descriptor.geometryProvider || (descriptor.appletId && *descriptor.appletId < 0))
    {
        qWarning() << "settings control registry: malformed scope descriptor refused";
        return std::nullopt;
    }

    QList<quint64> retired;
    for (auto it = m_scopes.cbegin(); it != m_scopes.cend(); ++it)
    {
        const ScopeEntry &scope = it.value();
        const bool sameLifetime = scope.lifetimeObject == descriptor.lifetimeObject;
        const bool sameCompleteScope = scope.containmentId == descriptor.containmentId &&
                                       scope.surface == descriptor.surface && scope.appletId == descriptor.appletId;
        if (sameLifetime || sameCompleteScope)
        {
            retired.append(scope.generation);
        }
    }
    for (const quint64 generation : retired)
    {
        removeGenerationByToken(generation);
    }

    const auto generation = allocateGeneration();
    if (!generation)
    {
        return std::nullopt;
    }

    ScopeEntry scope;
    scope.generation = *generation;
    scope.containmentId = descriptor.containmentId;
    scope.surface = descriptor.surface;
    scope.appletId = descriptor.appletId;
    scope.lifetimeObject = descriptor.lifetimeObject;
    scope.surfaceItem = descriptor.surfaceItem;
    scope.geometryProvider = descriptor.geometryProvider;
    m_scopes.insert(*generation, scope);

    QSet<QObject *> watched{descriptor.lifetimeObject, descriptor.surfaceItem, descriptor.geometryProvider};
    for (QObject *object : watched)
    {
        connect(object, &QObject::destroyed, this, [this, token = *generation]() { removeGenerationByToken(token); });
    }
    return generation;
}

void SettingsControlRegistry::retireGeneration(quint64 generation)
{
    if (!requireGuiThread("retireGeneration"))
    {
        return;
    }
    removeGenerationByToken(generation);
}

void SettingsControlRegistry::removeGenerationByToken(quint64 generation)
{
    auto scopeIt = m_scopes.find(generation);
    if (scopeIt == m_scopes.end())
    {
        return;
    }

    const QList<quint64> controls = scopeIt->controlTokens.values();
    for (const quint64 token : controls)
    {
        removeControlByToken(token);
    }
    m_scopes.remove(generation);
}

std::optional<quint64> SettingsControlRegistry::registerControl(quint64 generation, const ControlDescriptor &descriptor)
{
    if (!requireGuiThread("registerControl"))
    {
        return std::nullopt;
    }

    auto scopeIt = m_scopes.find(generation);
    if (scopeIt == m_scopes.end())
    {
        qWarning() << "settings control registry: control registration used "
                      "retired generation"
                   << generation;
        return std::nullopt;
    }

    QString refusal;
    if (descriptor.auditIdentity.isEmpty() || descriptor.kind.isEmpty() ||
        (descriptor.instanceKey && descriptor.instanceKey->isEmpty()) || !descriptor.item ||
        !isVisualDescendant(descriptor.item, scopeIt->surfaceItem) ||
        !descriptorPropertyIsScalar(descriptor.item, descriptor.currentProperty, refusal) ||
        !hitDescriptorsAreValid(scopeIt->surfaceItem, descriptor.hits, refusal))
    {
        qWarning() << "settings control registry: malformed control descriptor refused:"
                   << (refusal.isEmpty() ? QStringLiteral("identity or item is invalid") : refusal);
        return std::nullopt;
    }

    for (const quint64 token : scopeIt->controlTokens)
    {
        const auto existing = m_controls.constFind(token);
        if (existing != m_controls.cend() &&
            std::tie(existing->auditIdentity, existing->kind, existing->instanceKey) ==
                std::tie(descriptor.auditIdentity, descriptor.kind, descriptor.instanceKey))
        {
            qWarning() << "settings control registry: duplicate complete control "
                          "identity refused for"
                       << descriptor.auditIdentity;
            return std::nullopt;
        }
    }

    std::optional<PopupEntry> popup;
    QMetaProperty popupOpenProperty;
    if (descriptor.popup)
    {
        const auto &candidate = *descriptor.popup;
        if (!candidate.stateObject || candidate.openProperty.isEmpty() || !candidate.item ||
            !isVisualDescendant(candidate.item, scopeIt->surfaceItem))
        {
            qWarning() << "settings control registry: malformed popup descriptor refused";
            return std::nullopt;
        }

        const int propertyIndex =
            candidate.stateObject->metaObject()->indexOfProperty(candidate.openProperty.constData());
        if (propertyIndex < 0)
        {
            qWarning() << "settings control registry: popup open property is missing";
            return std::nullopt;
        }
        popupOpenProperty = candidate.stateObject->metaObject()->property(propertyIndex);
        if (popupOpenProperty.metaType().id() != QMetaType::Bool || !popupOpenProperty.hasNotifySignal())
        {
            qWarning() << "settings control registry: popup open property must be "
                          "bool with a notify signal";
            return std::nullopt;
        }

        popup = PopupEntry{candidate.stateObject, candidate.openProperty, candidate.item, std::nullopt};
    }

    const auto token = allocateObjectToken();
    if (!token)
    {
        return std::nullopt;
    }

    ControlEntry entry;
    entry.token = *token;
    entry.scopeGeneration = generation;
    entry.auditIdentity = descriptor.auditIdentity;
    entry.kind = descriptor.kind;
    entry.instanceKey = descriptor.instanceKey;
    entry.item = descriptor.item;
    entry.currentProperty = descriptor.currentProperty;
    for (const auto &hit : descriptor.hits)
    {
        entry.hits.append(HitEntry{hit.role, hit.item});
    }
    entry.popup = popup;
    m_controls.insert(*token, entry);
    scopeIt->controlTokens.insert(*token);

    QSet<QObject *> watched{descriptor.item};
    for (const auto &hit : descriptor.hits)
    {
        watched.insert(hit.item);
    }
    if (descriptor.popup)
    {
        watched.insert(descriptor.popup->stateObject);
        watched.insert(descriptor.popup->item);
    }
    for (QObject *object : watched)
    {
        connect(object, &QObject::destroyed, this, [this, token = *token]() { removeControlByToken(token); });
    }

    if (descriptor.popup)
    {
        m_popupTokensByStateObject.insert(descriptor.popup->stateObject, *token);
        const int slotIndex = metaObject()->indexOfSlot("onPopupOpenPropertyChanged()");
        Q_ASSERT(slotIndex >= 0);
        const QMetaObject::Connection connection = QObject::connect(
            descriptor.popup->stateObject, popupOpenProperty.notifySignal(), this, metaObject()->method(slotIndex));
        if (!connection)
        {
            qWarning() << "settings control registry: popup notify connection failed";
            removeControlByToken(*token);
            return std::nullopt;
        }
        updatePopupGeneration(*token);
        if (!m_controls.contains(*token))
        {
            return std::nullopt;
        }
    }
    return token;
}

void SettingsControlRegistry::removeControlByToken(quint64 controlToken)
{
    auto controlIt = m_controls.find(controlToken);
    if (controlIt == m_controls.end())
    {
        return;
    }

    if (controlIt->popup && controlIt->popup->stateObject)
    {
        m_popupTokensByStateObject.remove(controlIt->popup->stateObject, controlToken);
    }
    for (const auto &row : controlIt->rows)
    {
        m_rowParents.remove(row.token);
    }
    auto scopeIt = m_scopes.find(controlIt->scopeGeneration);
    if (scopeIt != m_scopes.end())
    {
        scopeIt->controlTokens.remove(controlToken);
    }
    m_controls.erase(controlIt);
}

bool SettingsControlRegistry::registerPopupRow(quint64 controlToken, const PopupRowDescriptor &descriptor)
{
    if (!requireGuiThread("registerPopupRow"))
    {
        return false;
    }

    auto controlIt = m_controls.find(controlToken);
    if (controlIt == m_controls.end() || !controlIt->popup)
    {
        qWarning() << "settings control registry: popup row registration used "
                      "unknown or non-popup control token";
        return false;
    }
    auto scopeIt = m_scopes.find(controlIt->scopeGeneration);
    if (scopeIt == m_scopes.end())
    {
        qWarning() << "settings control registry: popup row registration used a "
                      "retired scope";
        return false;
    }

    QString refusal;
    if (descriptor.auditIdentity.isEmpty() || descriptor.kind.isEmpty() ||
        (descriptor.instanceKey && descriptor.instanceKey->isEmpty()) || descriptor.visualIndex < 0 ||
        !descriptor.item || !SettingsControls::scalarIsValid(descriptor.value) ||
        !isVisualDescendant(descriptor.item, scopeIt->surfaceItem) ||
        !descriptorPropertyIsScalar(descriptor.item, descriptor.currentProperty, refusal) ||
        !hitDescriptorsAreValid(scopeIt->surfaceItem, descriptor.hits, refusal))
    {
        qWarning() << "settings control registry: malformed popup row descriptor refused:"
                   << (refusal.isEmpty() ? QStringLiteral("identity, value, index, or item is invalid") : refusal);
        return false;
    }

    for (const auto &row : controlIt->rows)
    {
        if (row.visualIndex == descriptor.visualIndex ||
            std::tie(row.auditIdentity, row.kind, row.instanceKey) ==
                std::tie(descriptor.auditIdentity, descriptor.kind, descriptor.instanceKey))
        {
            qWarning() << "settings control registry: duplicate popup row identity "
                          "or visual index refused for"
                       << descriptor.auditIdentity;
            return false;
        }
    }

    const auto token = allocateObjectToken();
    if (!token)
    {
        return false;
    }

    PopupRowEntry row;
    row.token = *token;
    row.auditIdentity = descriptor.auditIdentity;
    row.kind = descriptor.kind;
    row.instanceKey = descriptor.instanceKey;
    row.visualIndex = descriptor.visualIndex;
    row.value = descriptor.value;
    row.item = descriptor.item;
    row.currentProperty = descriptor.currentProperty;
    for (const auto &hit : descriptor.hits)
    {
        row.hits.append(HitEntry{hit.role, hit.item});
    }
    controlIt->rows.append(row);
    m_rowParents.insert(*token, controlToken);

    QSet<QObject *> watched{descriptor.item};
    for (const auto &hit : descriptor.hits)
    {
        watched.insert(hit.item);
    }
    for (QObject *object : watched)
    {
        connect(object, &QObject::destroyed, this, [this, token = *token]() { removeRowByToken(token); });
    }
    return true;
}

void SettingsControlRegistry::removeRowByToken(quint64 rowToken)
{
    const auto parentIt = m_rowParents.find(rowToken);
    if (parentIt == m_rowParents.end())
    {
        return;
    }
    const quint64 controlToken = parentIt.value();
    m_rowParents.erase(parentIt);

    auto controlIt = m_controls.find(controlToken);
    if (controlIt == m_controls.end())
    {
        return;
    }
    for (auto rowIt = controlIt->rows.begin(); rowIt != controlIt->rows.end(); ++rowIt)
    {
        if (rowIt->token == rowToken)
        {
            controlIt->rows.erase(rowIt);
            break;
        }
    }
}

void SettingsControlRegistry::onPopupOpenPropertyChanged()
{
    QObject *stateObject = sender();
    const QList<quint64> tokens = m_popupTokensByStateObject.values(stateObject);
    for (const quint64 token : tokens)
    {
        updatePopupGeneration(token);
    }
}

void SettingsControlRegistry::updatePopupGeneration(quint64 controlToken)
{
    auto controlIt = m_controls.find(controlToken);
    if (controlIt == m_controls.end() || !controlIt->popup || !controlIt->popup->stateObject)
    {
        return;
    }

    const QVariant value = controlIt->popup->stateObject->property(controlIt->popup->openProperty.constData());
    if (value.typeId() != QMetaType::Bool)
    {
        qWarning() << "settings control registry: live popup open state is no "
                      "longer bool; control refused at query";
        return;
    }

    if (!value.toBool())
    {
        controlIt->popup->generation.reset();
    }
    else if (!controlIt->popup->generation)
    {
        const auto generation = allocateGeneration();
        if (!generation)
        {
            removeControlByToken(controlToken);
            return;
        }
        controlIt->popup->generation = generation;
    }
}

std::optional<SettingsControls::HitRecord>
SettingsControlRegistry::snapshotHit(const ScopeEntry &scope, const HitEntry &entry, const QRectF &surfaceGeometry,
                                     bool surfaceMapped, QString &refusal) const
{
    QQuickItem *item = entry.item;
    QQuickItem *surfaceItem = scope.surfaceItem;
    if (!item || !surfaceItem || entry.role.isEmpty())
    {
        refusal = QStringLiteral("destroyed or malformed live hit entry");
        return std::nullopt;
    }
    if (!inspectTransformsToSurface(item, surfaceItem, refusal))
    {
        return std::nullopt;
    }

    const QRectF localRect(0.0, 0.0, item->width(), item->height());
    if (!SettingsControls::rectIsFiniteAndPositive(localRect))
    {
        refusal = QStringLiteral("hit item has non-finite, zero, or negative geometry");
        return std::nullopt;
    }

    bool ok = false;
    const QTransform toSurface = item->itemTransform(surfaceItem, &ok);
    if (!ok || !transformPreservesAxisAlignedRectangles(toSurface))
    {
        refusal = QStringLiteral("hit transform cannot describe one axis-aligned rectangle");
        return std::nullopt;
    }

    QRectF global = toSurface.mapRect(localRect);
    global.translate(surfaceGeometry.topLeft());
    if (!SettingsControls::rectIsFiniteAndPositive(global))
    {
        refusal = QStringLiteral("mapped global hit geometry is invalid");
        return std::nullopt;
    }

    QRectF clipped = global.intersected(surfaceGeometry);
    for (QQuickItem *cursor = item; cursor; cursor = cursor->parentItem())
    {
        if (cursor->clip())
        {
            const QRectF localClip(0.0, 0.0, cursor->width(), cursor->height());
            if (!finiteRectCoordinates(localClip) || localClip.width() < 0.0 || localClip.height() < 0.0)
            {
                refusal = QStringLiteral("clipping ancestor has non-finite or negative geometry");
                return std::nullopt;
            }
            if (localClip.isEmpty())
            {
                clipped = {};
                break;
            }

            bool clipOk = false;
            const QTransform clipTransform = cursor->itemTransform(surfaceItem, &clipOk);
            if (!clipOk || !transformPreservesAxisAlignedRectangles(clipTransform))
            {
                refusal = QStringLiteral("clipping ancestor transform cannot describe one rectangle");
                return std::nullopt;
            }
            QRectF globalClip = clipTransform.mapRect(localClip);
            globalClip.translate(surfaceGeometry.topLeft());
            if (!SettingsControls::rectIsFiniteAndPositive(globalClip))
            {
                refusal = QStringLiteral("mapped clipping geometry is invalid");
                return std::nullopt;
            }
            clipped = clipped.intersected(globalClip);
        }
        if (cursor == surfaceItem)
        {
            break;
        }
    }

    bool visible = false;
    bool enabled = false;
    if (!effectiveItemState(item, surfaceItem, visible, enabled, refusal))
    {
        return std::nullopt;
    }

    SettingsControls::HitRecord hit;
    hit.role = entry.role;
    hit.globalGeometry = global;
    hit.mapped = surfaceMapped && visible && !clipped.isEmpty();
    if (hit.mapped)
    {
        hit.clippedGeometry = clipped;
    }
    return hit;
}

std::optional<SettingsControls::StateRecord>
SettingsControlRegistry::snapshotState(const ScopeEntry &scope, QQuickItem *item, const QByteArray &currentProperty,
                                       const QList<HitEntry> &hits, const QRectF &surfaceGeometry, bool surfaceMapped,
                                       QString &refusal) const
{
    if (!item || !scope.surfaceItem)
    {
        refusal = QStringLiteral("state item or surface was destroyed");
        return std::nullopt;
    }

    bool visible = false;
    bool enabled = false;
    if (!effectiveItemState(item, scope.surfaceItem, visible, enabled, refusal))
    {
        return std::nullopt;
    }

    const auto current = scalarFromVariant(item->property(currentProperty.constData()));
    if (!current || !SettingsControls::scalarIsValid(*current))
    {
        refusal = QStringLiteral("live current value is outside the closed scalar domain");
        return std::nullopt;
    }

    SettingsControls::StateRecord state;
    state.visible = visible && surfaceMapped;
    state.enabled = enabled;
    state.focused = itemContainsActiveFocus(item, scope.surfaceItem);
    state.current = *current;
    for (const auto &hit : hits)
    {
        const auto record = snapshotHit(scope, hit, surfaceGeometry, surfaceMapped, refusal);
        if (!record)
        {
            return std::nullopt;
        }
        state.hits.append(*record);
    }
    return state;
}

std::optional<SettingsControls::ControlRecord>
SettingsControlRegistry::snapshotControl(ScopeEntry &scope, ControlEntry &entry, const QRectF &surfaceGeometry,
                                         bool surfaceMapped, QString &refusal)
{
    const auto state =
        snapshotState(scope, entry.item, entry.currentProperty, entry.hits, surfaceGeometry, surfaceMapped, refusal);
    if (!state)
    {
        return std::nullopt;
    }

    SettingsControls::ControlRecord record;
    record.containmentId = scope.containmentId;
    record.surface = scope.surface;
    record.loadGeneration = scope.generation;
    record.appletId = scope.appletId;
    record.auditIdentity = entry.auditIdentity;
    record.kind = entry.kind;
    record.instanceKey = entry.instanceKey;
    record.state = *state;

    if (!entry.popup)
    {
        return record;
    }
    if (!entry.popup->stateObject || !entry.popup->item)
    {
        refusal = QStringLiteral("popup object was destroyed without control cleanup");
        return std::nullopt;
    }

    const QVariant openValue = entry.popup->stateObject->property(entry.popup->openProperty.constData());
    if (openValue.typeId() != QMetaType::Bool)
    {
        refusal = QStringLiteral("live popup open value is no longer bool");
        return std::nullopt;
    }
    const bool open = openValue.toBool();
    if (open != entry.popup->generation.has_value())
    {
        updatePopupGeneration(entry.token);
    }
    if (open != entry.popup->generation.has_value())
    {
        refusal = QStringLiteral("popup generation could not follow its live open state");
        return std::nullopt;
    }

    SettingsControls::PopupRecord popup;
    popup.open = open;
    popup.generation = entry.popup->generation;
    const auto popupHit = snapshotHit(scope, HitEntry{QStringLiteral("popup"), entry.popup->item}, surfaceGeometry,
                                      surfaceMapped, refusal);
    if (!popupHit)
    {
        return std::nullopt;
    }
    popup.mapped = open && popupHit->mapped;

    for (const auto &row : entry.rows)
    {
        const auto rowState =
            snapshotState(scope, row.item, row.currentProperty, row.hits, surfaceGeometry, surfaceMapped, refusal);
        if (!rowState)
        {
            return std::nullopt;
        }
        popup.rows.append(SettingsControls::PopupRowRecord{row.auditIdentity, row.kind, row.instanceKey,
                                                           row.visualIndex, row.value, *rowState});
    }
    record.popup = popup;
    return record;
}

QString SettingsControlRegistry::viewSettingsControlsData(uint containmentId)
{
    if (!requireGuiThread("viewSettingsControlsData"))
    {
        return QStringLiteral("[]");
    }

    QList<SettingsControls::ControlRecord> records;
    for (auto scopeIt = m_scopes.begin(); scopeIt != m_scopes.end(); ++scopeIt)
    {
        ScopeEntry &scope = scopeIt.value();
        if (scope.containmentId != containmentId)
        {
            continue;
        }
        if (!scope.surfaceItem || !scope.geometryProvider || !scope.lifetimeObject)
        {
            qWarning() << "settings control registry: destroyed scope entry remained "
                          "live; refusing view"
                       << containmentId;
            return QStringLiteral("[]");
        }

        const std::optional<QRectF> surfaceGeometry = scope.geometryProvider->settingsControlSurfaceGeometry();
        if (!surfaceGeometry || !SettingsControls::rectIsFiniteAndPositive(*surfaceGeometry))
        {
            qWarning() << "settings control registry: authoritative surface geometry "
                          "is unavailable or invalid; refusing view"
                       << containmentId << "surface" << scope.surface;
            return QStringLiteral("[]");
        }

        QQuickWindow *window = scope.surfaceItem->window();
        const bool surfaceMapped = window && window->isVisible();
        for (const quint64 token : scope.controlTokens)
        {
            auto controlIt = m_controls.find(token);
            if (controlIt == m_controls.end())
            {
                qWarning() << "settings control registry: scope references missing "
                              "control token; refusing view"
                           << containmentId;
                return QStringLiteral("[]");
            }

            QString refusal;
            const auto record = snapshotControl(scope, controlIt.value(), *surfaceGeometry, surfaceMapped, refusal);
            if (!record)
            {
                qWarning() << "settings control registry: malformed live entry refused "
                              "complete view"
                           << containmentId << ":" << refusal;
                return QStringLiteral("[]");
            }
            records.append(*record);
        }
    }

    const auto result = SettingsControls::serializeControlRecords(records);
    if (!result.accepted())
    {
        qWarning() << "settings control registry: value snapshot refused complete view" << containmentId << ":"
                   << result.refusal;
        return QStringLiteral("[]");
    }
    return result.data;
}

} // namespace Latte
