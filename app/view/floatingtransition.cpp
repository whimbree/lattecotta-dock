/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "floatingtransition.h"

#include <QDebug>

#include <cmath>

namespace Latte::ViewPart {

FloatingTransition::FloatingTransition(QObject *parent, QPropertyAnimation *animation)
    : QObject(parent),
      m_animation(animation ? animation : new QPropertyAnimation)
{
    m_animation->setParent(this);
    m_animation->setTargetObject(this);
    m_animation->setPropertyName("floatingness");

    connect(m_animation, &QPropertyAnimation::stateChanged, this,
            [this]() {
                Q_EMIT runningChanged();
            });
    connect(m_animation, &QPropertyAnimation::finished, this, [this]() {
        setFloatingness(m_target == Target::Floated ? 1.0 : 0.0);
        setPhase(Phase::Resting);
    });
}

FloatingTransition::~FloatingTransition()
{
    m_animation->disconnect(this);
    m_animation->stop();
    delete m_animation;
    m_animation = nullptr;
}

qreal FloatingTransition::floatingness() const
{
    return m_floatingness;
}

FloatingTransition::Target FloatingTransition::target() const
{
    return m_target;
}

FloatingTransition::Phase FloatingTransition::phase() const
{
    return m_phase;
}

bool FloatingTransition::running() const
{
    return m_animation->state() == QAbstractAnimation::Running;
}

int FloatingTransition::animationDuration() const
{
    return m_animationDuration;
}

void FloatingTransition::setAnimationDuration(int duration)
{
    if (duration < 0) {
        qCritical() << "FloatingTransition refused a negative animation duration:"
                    << duration;
        return;
    }

    if (m_animationDuration == duration) {
        return;
    }

    m_animationDuration = duration;
    Q_EMIT animationDurationChanged();
}

bool FloatingTransition::hasGeometry() const
{
    return m_geometry.has_value();
}

QRect FloatingTransition::stableCanvasGeometry() const
{
    return hasGeometry() ? m_geometry->envelope.value : QRect{};
}

QRect FloatingTransition::attachedGeometry() const
{
    return hasGeometry() ? m_geometry->attached.value : QRect{};
}

QRect FloatingTransition::floatedGeometry() const
{
    return hasGeometry() ? m_geometry->floated.value : QRect{};
}

QRect FloatingTransition::stableTriggerGeometry() const
{
    return hasGeometry() ? m_geometry->trigger.value : QRect{};
}

QRect FloatingTransition::appletMeasurementBounds() const
{
    return hasGeometry() ? m_geometry->appletMeasurementBounds.value : QRect{};
}

int FloatingTransition::primaryAxisStart() const
{
    return hasGeometry() ? m_geometry->primaryAxisSpan.start : 0;
}

int FloatingTransition::primaryAxisLength() const
{
    return hasGeometry() ? m_geometry->primaryAxisSpan.length : 0;
}

int FloatingTransition::stableReservationDepth() const
{
    return hasGeometry() ? m_geometry->reservationDepth : 0;
}

QRectF FloatingTransition::currentVisibleGeometry() const
{
    return hasGeometry() ? m_geometry->visibleMask(m_floatingness).value : QRectF{};
}

QRectF FloatingTransition::fittsBridgeGeometry() const
{
    return hasGeometry() ? m_geometry->fittsBridge(m_floatingness).value : QRectF{};
}

QPointF FloatingTransition::contentTranslation() const
{
    return hasGeometry() ? m_geometry->contentTranslation(m_floatingness) : QPointF{};
}

quint64 FloatingTransition::geometryRevision() const
{
    return m_geometryRevision;
}

bool FloatingTransition::configureGeometry(
    const FloatingPanelGeometry::Inputs &inputs)
{
    const auto solution = FloatingPanelGeometry::solve(inputs);
    if (!solution.has_value()) {
        qCritical() << "FloatingTransition refused invalid stable panel geometry"
                    << inputs.outputGeometry << inputs.primaryAxisSpan.start
                    << inputs.primaryAxisSpan.length << inputs.panelDepth
                    << inputs.floatingGap;
        return false;
    }

    const bool unchanged = hasGeometry()
        && m_geometry->attached.value == solution->attached.value
        && m_geometry->floated.value == solution->floated.value
        && m_geometry->envelope.value == solution->envelope.value
        && m_geometry->trigger.value == solution->trigger.value
        && m_geometry->appletMeasurementBounds.value
            == solution->appletMeasurementBounds.value
        && m_geometry->primaryAxisSpan == solution->primaryAxisSpan
        && m_geometry->reservationDepth == solution->reservationDepth;
    if (unchanged) {
        return true;
    }

    m_geometry = solution;
    ++m_geometryRevision;
    Q_EMIT stableGeometryChanged();
    Q_EMIT currentGeometryChanged();
    return true;
}

void FloatingTransition::clearGeometry()
{
    if (!hasGeometry()) {
        return;
    }

    m_geometry.reset();
    ++m_geometryRevision;
    Q_EMIT stableGeometryChanged();
    Q_EMIT currentGeometryChanged();
}

void FloatingTransition::requestAttached()
{
    requestTarget(Target::Attached);
}

void FloatingTransition::requestFloated()
{
    requestTarget(Target::Floated);
}

void FloatingTransition::requestTarget(Target target)
{
    const qreal endpoint = target == Target::Floated ? 1.0 : 0.0;
    if (m_target == target
        && (running() || qFuzzyIsNull(m_floatingness - endpoint))) {
        return;
    }

    if (m_target != target) {
        m_target = target;
        Q_EMIT targetChanged();
    }

    if (qFuzzyIsNull(m_floatingness - endpoint)) {
        m_animation->stop();
        setFloatingness(endpoint);
        setPhase(Phase::Resting);
        return;
    }

    m_animation->stop();
    setPhase(target == Target::Floated ? Phase::Floating : Phase::Attaching);

    if (m_animationDuration == 0) {
        setFloatingness(endpoint);
        setPhase(Phase::Resting);
        return;
    }

    m_animation->setStartValue(m_floatingness);
    m_animation->setEndValue(endpoint);
    m_animation->setDuration(m_animationDuration);
    m_animation->setEasingCurve(target == Target::Floated ? QEasingCurve::OutQuad
                                                          : QEasingCurve::InQuad);
    m_animation->start();
}

void FloatingTransition::setFloatingness(qreal floatingness)
{
    if (!std::isfinite(floatingness) || floatingness < 0.0 || floatingness > 1.0) {
        qCritical() << "FloatingTransition refused out-of-range floatingness:"
                    << floatingness;
        return;
    }

    if (qFuzzyIsNull(m_floatingness - floatingness)) {
        return;
    }

    m_floatingness = floatingness;
    Q_EMIT floatingnessChanged();
    Q_EMIT currentGeometryChanged();
}

void FloatingTransition::setPhase(Phase phase)
{
    if (m_phase == phase) {
        return;
    }

    m_phase = phase;
    Q_EMIT phaseChanged();
}

} // namespace Latte::ViewPart
