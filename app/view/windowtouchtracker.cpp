/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "windowtouchtracker.h"

#include "floatingtransition.h"
#include "windowtouchstate.h"

#include <QDebug>
#include <QMetaType>
#include <QModelIndex>
#include <QVariant>

#include <optional>
#include <utility>
#include <vector>

namespace Latte::ViewPart {

namespace {

struct WindowTouchRoles
{
    int isWindow{-1};
    int isHidden{-1};
    int isMinimized{-1};
    int geometry{-1};
};

[[nodiscard]] std::optional<int> uniqueRoleNamed(
    const QHash<int, QByteArray> &roles,
    const QByteArray &name)
{
    std::optional<int> match;

    for (auto role = roles.cbegin(); role != roles.cend(); ++role) {
        if (role.value() != name) {
            continue;
        }

        if (match.has_value()) {
            qCritical() << "WindowTouchTracker refused duplicate model role"
                        << name << "in" << roles;
            return std::nullopt;
        }

        match = role.key();
    }

    if (!match.has_value()) {
        qCritical() << "WindowTouchTracker requires model role" << name
                    << "but the model exposes" << roles;
    }

    return match;
}

[[nodiscard]] std::optional<WindowTouchRoles> resolveWindowTouchRoles(
    const QAbstractItemModel *model)
{
    const QHash<int, QByteArray> roleNames = model->roleNames();
    const auto isWindow = uniqueRoleNamed(
        roleNames, QByteArrayLiteral("IsWindow"));
    const auto isHidden = uniqueRoleNamed(
        roleNames, QByteArrayLiteral("IsHidden"));
    const auto isMinimized = uniqueRoleNamed(
        roleNames, QByteArrayLiteral("IsMinimized"));
    const auto geometry = uniqueRoleNamed(
        roleNames, QByteArrayLiteral("Geometry"));

    if (!isWindow || !isHidden || !isMinimized || !geometry) {
        return std::nullopt;
    }

    return WindowTouchRoles{
        *isWindow,
        *isHidden,
        *isMinimized,
        *geometry,
    };
}

[[nodiscard]] std::optional<bool> exactBool(
    const QVariant &value,
    const QByteArray &roleName,
    int row)
{
    if (value.metaType().id() != QMetaType::Bool) {
        qCritical() << "WindowTouchTracker requires bool role"
                    << roleName << "at row" << row
                    << "but received" << value.metaType().name();
        return std::nullopt;
    }

    return value.toBool();
}

[[nodiscard]] std::optional<QRect> exactRect(
    const QVariant &value,
    int row)
{
    if (value.metaType().id() != QMetaType::QRect) {
        qCritical() << "WindowTouchTracker requires QRect Geometry at row"
                    << row << "but received" << value.metaType().name();
        return std::nullopt;
    }

    return value.toRect();
}

[[nodiscard]] std::optional<std::vector<WindowTouchCandidate>>
collectWindowTouchCandidates(
    const QAbstractItemModel *model,
    const WindowTouchRoles &roles)
{
    const int rowCount = model->rowCount();
    if (rowCount < 0) {
        qCritical() << "WindowTouchTracker refused a negative model row count"
                    << rowCount;
        return std::nullopt;
    }

    std::vector<WindowTouchCandidate> candidates;
    candidates.reserve(static_cast<std::size_t>(rowCount));

    for (int row = 0; row < rowCount; ++row) {
        const QModelIndex index = model->index(row, 0);
        if (!index.isValid() || index.model() != model) {
            qCritical() << "WindowTouchTracker could not address model row"
                        << row;
            return std::nullopt;
        }

        const auto isWindow = exactBool(
            model->data(index, roles.isWindow),
            QByteArrayLiteral("IsWindow"), row);
        if (!isWindow) {
            return std::nullopt;
        }
        if (!*isWindow) {
            continue;
        }

        const auto isHidden = exactBool(
            model->data(index, roles.isHidden),
            QByteArrayLiteral("IsHidden"), row);
        const auto isMinimized = exactBool(
            model->data(index, roles.isMinimized),
            QByteArrayLiteral("IsMinimized"), row);
        const auto geometry = exactRect(
            model->data(index, roles.geometry), row);
        if (!isHidden || !isMinimized || !geometry) {
            return std::nullopt;
        }

        if (!*isHidden && !*isMinimized && !geometry->isValid()) {
            qCritical() << "WindowTouchTracker refused invalid visible-window"
                           " Geometry at row"
                        << row << *geometry;
            return std::nullopt;
        }

        candidates.push_back(WindowTouchCandidate{
            *geometry,
            true,
            *isHidden,
            *isMinimized,
        });
    }

    return candidates;
}

} // namespace

WindowTouchTracker::WindowTouchTracker(
    FloatingTransition *transition,
    QObject *parent)
    : QObject(parent),
      m_transition(transition)
{
    if (!m_transition) {
        qFatal("WindowTouchTracker requires a FloatingTransition");
    }

    m_evaluationTimer.setSingleShot(true);
    m_evaluationTimer.setInterval(EvaluationDelayMs);
    m_evaluationTimer.setTimerType(Qt::PreciseTimer);

    connect(&m_evaluationTimer, &QTimer::timeout,
            this, &WindowTouchTracker::evaluateCurrentState);
    connect(m_transition, &FloatingTransition::stableGeometryChanged,
            this, [this]() {
                if (!m_transition || !m_transition->hasGeometry()) {
                    m_evaluationTimer.stop();
                    setTouchingWindowCount(0);
                    return;
                }

                scheduleEvaluation();
            });
    connect(m_transition, &QObject::destroyed, this, [this]() {
        m_evaluationTimer.stop();
        m_transition.clear();
        setTouchingWindowCount(0);
    });
}

WindowTouchTracker::~WindowTouchTracker()
{
    m_evaluationTimer.stop();
    disconnectModel();
}

QAbstractItemModel *WindowTouchTracker::model() const
{
    return m_model;
}

void WindowTouchTracker::setModel(QAbstractItemModel *model)
{
    if (m_registeredModelIdentity == model) {
        return;
    }

    disconnectModel();
    m_model = model;
    m_registeredModelIdentity = model;
    setGeometryRoleTypeName(QString{});

    if (model) {
        connectModel(model);
        scheduleEvaluation();
    } else {
        resetForUnavailableModel("the model was cleared");
    }

    Q_EMIT modelChanged();
}

int WindowTouchTracker::touchingWindowCount() const
{
    return m_touchingWindowCount;
}

QString WindowTouchTracker::geometryRoleTypeName() const
{
    return m_geometryRoleTypeName;
}

void WindowTouchTracker::connectModel(QAbstractItemModel *model)
{
    const auto schedule = [this]() {
        scheduleEvaluation();
    };

    m_modelConnections = {
        connect(model, &QAbstractItemModel::rowsInserted,
                this, schedule),
        connect(model, &QAbstractItemModel::rowsRemoved,
                this, schedule),
        connect(model, &QAbstractItemModel::rowsMoved,
                this, schedule),
        connect(model, &QAbstractItemModel::dataChanged,
                this, schedule),
        connect(model, &QAbstractItemModel::modelReset,
                this, schedule),
        connect(model, &QAbstractItemModel::layoutChanged,
                this, schedule),
        connect(model, &QObject::destroyed, this,
                [this, model]() {
                    if (m_registeredModelIdentity != model) {
                        return;
                    }

                    disconnectModel();
                    m_model.clear();
                    setGeometryRoleTypeName(QString{});
                    resetForUnavailableModel(
                        "the observed model was destroyed");
                    Q_EMIT modelChanged();
                }),
    };
}

void WindowTouchTracker::disconnectModel()
{
    for (const auto &connection : std::as_const(m_modelConnections)) {
        disconnect(connection);
    }
    m_modelConnections.clear();
    m_registeredModelIdentity = nullptr;
}

void WindowTouchTracker::scheduleEvaluation()
{
    if (!m_model || !m_transition) {
        resetForUnavailableModel(
            !m_model ? "no model is available"
                     : "the transition controller was destroyed");
        return;
    }

    if (!m_evaluationTimer.isActive()) {
        m_evaluationTimer.start();
    }
}

void WindowTouchTracker::evaluateCurrentState()
{
    if (!m_model || !m_transition) {
        resetForUnavailableModel(
            !m_model ? "the model disappeared before evaluation"
                     : "the transition controller disappeared before evaluation");
        return;
    }

    if (!m_transition->hasGeometry()) {
        setTouchingWindowCount(0);
        return;
    }

    const auto trigger = StableWindowTouchTrigger::fromGeometry(
        m_transition->stableTriggerGeometry());
    if (!trigger) {
        qCritical() << "WindowTouchTracker refused invalid stable trigger"
                    << m_transition->stableTriggerGeometry();
        setTouchingWindowCount(0);
        return;
    }

    const auto roles = resolveWindowTouchRoles(m_model);
    if (!roles) {
        setGeometryRoleTypeName(QString{});
        setTouchingWindowCount(0);
        return;
    }

    const auto candidates = collectWindowTouchCandidates(m_model, *roles);
    if (!candidates) {
        setGeometryRoleTypeName(QString{});
        setTouchingWindowCount(0);
        return;
    }

    setGeometryRoleTypeName(
        candidates->empty() ? QString{} : QStringLiteral("QRect"));

    setTouchingWindowCount(
        countWindowsTouchingTrigger(*trigger, *candidates));
}

void WindowTouchTracker::resetForUnavailableModel(const char *reason)
{
    m_evaluationTimer.stop();
    if (m_touchingWindowCount != 0) {
        qWarning() << "WindowTouchTracker reset its count because" << reason;
    }
    setTouchingWindowCount(0);
}

void WindowTouchTracker::setTouchingWindowCount(int count)
{
    Q_ASSERT(count >= 0);

    if (m_touchingWindowCount == count) {
        return;
    }

    m_touchingWindowCount = count;
    Q_EMIT touchingWindowCountChanged();
}

void WindowTouchTracker::setGeometryRoleTypeName(const QString &typeName)
{
    if (m_geometryRoleTypeName == typeName) {
        return;
    }

    m_geometryRoleTypeName = typeName;
    Q_EMIT geometryRoleTypeNameChanged();
}

} // namespace Latte::ViewPart
