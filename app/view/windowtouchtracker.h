/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef WINDOWTOUCHTRACKER_H
#define WINDOWTOUCHTRACKER_H

#include <QAbstractItemModel>
#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

namespace Latte::ViewPart {

class FloatingTransition;

class WindowTouchTracker final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QAbstractItemModel *model READ model WRITE setModel
                   NOTIFY modelChanged)
    Q_PROPERTY(int touchingWindowCount READ touchingWindowCount
                   NOTIFY touchingWindowCountChanged)
    Q_PROPERTY(QString geometryRoleTypeName READ geometryRoleTypeName
                   NOTIFY geometryRoleTypeNameChanged)

public:
    static constexpr int EvaluationDelayMs = 10;

    explicit WindowTouchTracker(FloatingTransition *transition,
                                QObject *parent = nullptr);
    ~WindowTouchTracker() override;

    [[nodiscard]] QAbstractItemModel *model() const;
    void setModel(QAbstractItemModel *model);

    [[nodiscard]] int touchingWindowCount() const;
    [[nodiscard]] QString geometryRoleTypeName() const;

Q_SIGNALS:
    void modelChanged();
    void touchingWindowCountChanged();
    void geometryRoleTypeNameChanged();

private:
    void connectModel(QAbstractItemModel *model);
    void disconnectModel();
    void scheduleEvaluation();
    void evaluateCurrentState();
    void resetForUnavailableModel(const char *reason);
    void setTouchingWindowCount(int count);
    void setGeometryRoleTypeName(const QString &typeName);

    QPointer<FloatingTransition> m_transition;
    QPointer<QAbstractItemModel> m_model;
    QObject *m_registeredModelIdentity{nullptr};
    QList<QMetaObject::Connection> m_modelConnections;
    QTimer m_evaluationTimer;
    int m_touchingWindowCount{0};
    QString m_geometryRoleTypeName;
};

} // namespace Latte::ViewPart

#endif
