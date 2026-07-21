/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef SETTINGSCONTROLREGISTRY_H
#define SETTINGSCONTROLREGISTRY_H

#include "settingscontrolrecords.h"

#include <QByteArray>
#include <QHash>
#include <QMultiHash>
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QRectF>
#include <QSet>

#include <optional>

namespace Latte
{

//! Supplies the logical global bounds of one settings surface. Implementations
//! must read the placement authority used to position that surface. The
//! registry deliberately does not infer a Wayland global position from
//! QWindow::position().
class SettingsControlSurfaceGeometryProvider : public QObject
{
  public:
    using QObject::QObject;
    ~SettingsControlSurfaceGeometryProvider() override = default;

    virtual std::optional<QRectF> settingsControlSurfaceGeometry() const = 0;
};

class SettingsControlRegistry : public QObject
{
    Q_OBJECT

  public:
    struct ScopeDescriptor
    {
        uint containmentId{0};
        QString surface;
        std::optional<qint64> appletId;
        QObject *lifetimeObject{nullptr};
        QQuickItem *surfaceItem{nullptr};
        SettingsControlSurfaceGeometryProvider *geometryProvider{nullptr};
    };

    struct HitDescriptor
    {
        QString role;
        QQuickItem *item{nullptr};
    };

    struct PopupDescriptor
    {
        QObject *stateObject{nullptr};
        QByteArray openProperty;
        QQuickItem *item{nullptr};
    };

    struct ControlDescriptor
    {
        QString auditIdentity;
        QString kind;
        std::optional<QString> instanceKey;
        QQuickItem *item{nullptr};
        QByteArray currentProperty;
        QList<HitDescriptor> hits;
        std::optional<PopupDescriptor> popup;
    };

    struct PopupRowDescriptor
    {
        QString auditIdentity;
        QString kind;
        std::optional<QString> instanceKey;
        int visualIndex{-1};
        SettingsControls::Scalar value;
        QQuickItem *item{nullptr};
        QByteArray currentProperty;
        QList<HitDescriptor> hits;
    };

    explicit SettingsControlRegistry(QObject *parent = nullptr);

    //! A valid replacement retires every previous generation for the same
    //! lifetime object or complete surface scope before allocating a newer id.
    std::optional<quint64> replaceScope(const ScopeDescriptor &descriptor);
    void retireGeneration(quint64 generation);

    std::optional<quint64> registerControl(quint64 generation, const ControlDescriptor &descriptor);
    bool registerPopupRow(quint64 controlToken, const PopupRowDescriptor &descriptor);

    //! Known-view validation belongs to Corona. A registry with no matching
    //! generation or no controls returns [] quietly.
    QString viewSettingsControlsData(uint containmentId);

  private Q_SLOTS:
    void onPopupOpenPropertyChanged();

  private:
    struct HitEntry
    {
        QString role;
        QPointer<QQuickItem> item;
    };

    struct ScopeEntry
    {
        quint64 generation{0};
        uint containmentId{0};
        QString surface;
        std::optional<qint64> appletId;
        QPointer<QObject> lifetimeObject;
        QPointer<QQuickItem> surfaceItem;
        QPointer<SettingsControlSurfaceGeometryProvider> geometryProvider;
        QSet<quint64> controlTokens;
    };

    struct PopupEntry
    {
        QPointer<QObject> stateObject;
        QByteArray openProperty;
        QPointer<QQuickItem> item;
        std::optional<quint64> generation;
    };

    struct PopupRowEntry
    {
        quint64 token{0};
        QString auditIdentity;
        QString kind;
        std::optional<QString> instanceKey;
        int visualIndex{-1};
        SettingsControls::Scalar value;
        QPointer<QQuickItem> item;
        QByteArray currentProperty;
        QList<HitEntry> hits;
    };

    struct ControlEntry
    {
        quint64 token{0};
        quint64 scopeGeneration{0};
        QString auditIdentity;
        QString kind;
        std::optional<QString> instanceKey;
        QPointer<QQuickItem> item;
        QByteArray currentProperty;
        QList<HitEntry> hits;
        std::optional<PopupEntry> popup;
        QList<PopupRowEntry> rows;
    };

    bool requireGuiThread(const char *operation) const;
    std::optional<quint64> allocateGeneration();
    std::optional<quint64> allocateObjectToken();
    void removeGenerationByToken(quint64 generation);
    void removeControlByToken(quint64 controlToken);
    void removeRowByToken(quint64 rowToken);
    void updatePopupGeneration(quint64 controlToken);

    bool descriptorPropertyIsScalar(QObject *object, const QByteArray &property, QString &refusal) const;
    bool hitDescriptorsAreValid(QQuickItem *surfaceItem, const QList<HitDescriptor> &hits, QString &refusal) const;
    static std::optional<SettingsControls::Scalar> scalarFromVariant(const QVariant &value);
    static bool isVisualDescendant(QQuickItem *item, QQuickItem *ancestor);

    std::optional<SettingsControls::HitRecord> snapshotHit(const ScopeEntry &scope, const HitEntry &entry,
                                                           const QRectF &surfaceGeometry, bool surfaceMapped,
                                                           QString &refusal) const;
    std::optional<SettingsControls::StateRecord> snapshotState(const ScopeEntry &scope, QQuickItem *item,
                                                               const QByteArray &currentProperty,
                                                               const QList<HitEntry> &hits,
                                                               const QRectF &surfaceGeometry, bool surfaceMapped,
                                                               QString &refusal) const;
    std::optional<SettingsControls::ControlRecord> snapshotControl(ScopeEntry &scope, ControlEntry &entry,
                                                                   const QRectF &surfaceGeometry, bool surfaceMapped,
                                                                   QString &refusal);

    quint64 m_lastGeneration{0};
    quint64 m_lastObjectToken{0};
    QHash<quint64, ScopeEntry> m_scopes;
    QHash<quint64, ControlEntry> m_controls;
    QHash<quint64, quint64> m_rowParents;
    QMultiHash<QObject *, quint64> m_popupTokensByStateObject;
};

} // namespace Latte

#endif
