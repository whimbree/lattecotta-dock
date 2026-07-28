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

bool FloatingTransition::attachmentTargeted() const
{
    return m_target == Target::Attached;
}

bool FloatingTransition::floatingAppletPopupsPreferred() const
{
    return m_target == Target::Floated;
}

int FloatingTransition::displayHintsWithFloatingPreference(
    int currentHints,
    int floatingHint,
    bool floatingPanelConfigured) const
{
    return FloatingPopupPresentation::
        displayHintsWithFloatingPreference(
            currentHints,
            floatingHint,
            floatingPanelConfigured
                && floatingAppletPopupsPreferred());
}

FloatingTransition::Phase FloatingTransition::phase() const
{
    return m_phase;
}

bool FloatingTransition::running() const
{
    return m_animation->state() == QAbstractAnimation::Running;
}

bool FloatingTransition::floatingPanelEligible() const
{
    return m_floatingPanelEligible;
}

bool FloatingTransition::attachOnWindowTouchConfigured() const
{
    return m_attachOnWindowTouchConfigured;
}

bool FloatingTransition::attachmentWaitsForPointerExitConfigured() const
{
    return m_attachmentWaitsForPointerExitConfigured;
}

bool FloatingTransition::pointerInsideView() const
{
    return m_pointerInsideView;
}

bool FloatingTransition::attachmentDeferredByPointer() const
{
    return m_attachmentDeferredByPointer;
}

bool FloatingTransition::dockGapHideRequested() const
{
    return m_dockGapHideRequested;
}

int FloatingTransition::touchingWindowCount() const
{
    return m_touchingWindowCount;
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

QRect FloatingTransition::currentPaintMaskGeometry() const
{
    return hasGeometry() ? m_geometry->paintMask(m_floatingness).value : QRect{};
}

QRect FloatingTransition::currentInputBridgeGeometry() const
{
    return hasGeometry() ? m_geometry->inputBridge(m_floatingness).value : QRect{};
}

QMargins FloatingTransition::currentShadowPaddingOffsets() const
{
    return hasGeometry() ? m_geometry->shadowPaddingOffsets(m_floatingness)
                         : QMargins{};
}

bool FloatingTransition::screenEdgeBorderVisible() const
{
    return hasGeometry() && m_geometry->screenEdgeBorderVisible(m_floatingness);
}

bool FloatingTransition::floatingCornersVisible() const
{
    return hasGeometry() && m_geometry->floatingCornersVisible(m_floatingness);
}

FloatingPanelGeometry::InputDisposition FloatingTransition::classifyInput(
    const QPointF &position) const
{
    return hasGeometry()
        ? m_geometry->classifyInput(m_floatingness, position)
        : FloatingPanelGeometry::InputDisposition::ConsumeWithoutForwarding;
}

QPointF FloatingTransition::positionAdjustedForVisibleMask(
    const QPointF &position) const
{
    return hasGeometry()
        ? m_geometry->positionAdjustedForVisibleMask(m_floatingness, position)
        : position;
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

    configureGeometry(*solution);
    return true;
}

void FloatingTransition::configureGeometry(
    const FloatingPanelGeometry::Solution &solution)
{
    const bool unchanged = hasGeometry()
        && m_geometry->attached.value == solution.attached.value
        && m_geometry->floated.value == solution.floated.value
        && m_geometry->envelope.value == solution.envelope.value
        && m_geometry->trigger.value == solution.trigger.value
        && m_geometry->appletMeasurementBounds.value
            == solution.appletMeasurementBounds.value
        && m_geometry->primaryAxisSpan == solution.primaryAxisSpan
        && m_geometry->reservationDepth == solution.reservationDepth;
    if (unchanged) {
        return;
    }

    m_geometry = solution;
    ++m_geometryRevision;
    Q_EMIT stableGeometryChanged();
    Q_EMIT currentGeometryChanged();
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

void FloatingTransition::reconcileTargetPolicy(
    bool floatingPanelEligible,
    bool attachOnWindowTouchConfigured,
    bool attachmentWaitsForPointerExitConfigured,
    bool pointerInsideView,
    int touchingWindowCount,
    bool dockGapHideRequested)
{
    if (touchingWindowCount < 0) {
        qCritical() << "FloatingTransition refused a negative touching-window"
                       " count:"
                    << touchingWindowCount;
        return;
    }
    if (dockGapHideRequested
        && (floatingPanelEligible
            || !attachOnWindowTouchConfigured
            || touchingWindowCount <= 0)) {
        qCritical()
            << "FloatingTransition refused an inconsistent Dock gap-hide"
               " request";
        return;
    }

    const bool eligibilityDidChange =
        m_floatingPanelEligible != floatingPanelEligible;
    const bool configurationDidChange =
        m_attachOnWindowTouchConfigured != attachOnWindowTouchConfigured;
    const bool waitConfigurationDidChange =
        m_attachmentWaitsForPointerExitConfigured
        != attachmentWaitsForPointerExitConfigured;
    const bool pointerInsideDidChange =
        m_pointerInsideView != pointerInsideView;
    const bool deferredBefore = m_attachmentDeferredByPointer;
    const bool dockGapHideRequestDidChange =
        m_dockGapHideRequested != dockGapHideRequested;
    const bool countDidChange =
        m_touchingWindowCount != touchingWindowCount;

    //! Commit all policy inputs before target/phase signals are emitted.
    //! Every observer therefore sees one complete equation, even when QML
    //! eligibility and task-model rows change in the same event-loop turn.
    m_floatingPanelEligible = floatingPanelEligible;
    m_attachOnWindowTouchConfigured = attachOnWindowTouchConfigured;
    m_attachmentWaitsForPointerExitConfigured =
        attachmentWaitsForPointerExitConfigured;
    m_pointerInsideView = pointerInsideView;
    m_dockGapHideRequested = dockGapHideRequested;
    m_touchingWindowCount = touchingWindowCount;

    const bool panelAttachmentRequested =
        m_floatingPanelEligible
        && m_attachOnWindowTouchConfigured
        && m_touchingWindowCount > 0;
    const bool attachmentRequested =
        panelAttachmentRequested || m_dockGapHideRequested;
    //! Pointer entry is not a detach request. It defers only an attachment
    //! that begins while the pointer is already inside this view.
    const bool attachmentAlreadyTargeted =
        m_target == Target::Attached;
    m_attachmentDeferredByPointer =
        attachmentRequested
        && m_attachmentWaitsForPointerExitConfigured
        && m_pointerInsideView
        && !attachmentAlreadyTargeted;
    const bool deferralDidChange =
        deferredBefore != m_attachmentDeferredByPointer;
    //! The scalar presentation belongs to this controller for both view
    //! types. A Dock deliberately owns no FloatingPanelGeometry; its QML
    //! layout consumes floatingness while its stable QWindow stays unchanged.
    const bool shouldAttach =
        attachmentRequested
        && !m_attachmentDeferredByPointer;
    requestTarget(shouldAttach ? Target::Attached : Target::Floated);

    if (eligibilityDidChange) {
        Q_EMIT floatingPanelEligibleChanged();
    }
    if (configurationDidChange) {
        Q_EMIT attachOnWindowTouchConfiguredChanged();
    }
    if (waitConfigurationDidChange) {
        Q_EMIT attachmentWaitsForPointerExitConfiguredChanged();
    }
    if (pointerInsideDidChange) {
        Q_EMIT pointerInsideViewChanged();
    }
    if (deferralDidChange) {
        Q_EMIT attachmentDeferredByPointerChanged();
    }
    if (dockGapHideRequestDidChange) {
        Q_EMIT dockGapHideRequestedChanged();
    }
    if (countDidChange) {
        Q_EMIT touchingWindowCountChanged();
    }
}

void FloatingTransition::requestTarget(Target target)
{
    const qreal endpoint = target == Target::Floated ? 1.0 : 0.0;
    if (m_target == target
        && (running() || m_floatingness == endpoint)) {
        return;
    }

    if (m_target != target) {
        m_target = target;
        Q_EMIT targetChanged();
    }

    if (m_floatingness == endpoint) {
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
    m_animation->setEasingCurve(target == Target::Floated ? QEasingCurve::OutCubic
                                                          : QEasingCurve::InCubic);
    m_animation->start();
}

void FloatingTransition::setFloatingness(qreal floatingness)
{
    if (!std::isfinite(floatingness) || floatingness < 0.0 || floatingness > 1.0) {
        qCritical() << "FloatingTransition refused out-of-range floatingness:"
                    << floatingness;
        return;
    }

    const bool endpoint = floatingness == 0.0 || floatingness == 1.0;
    if (m_floatingness == floatingness
        || (!endpoint && qFuzzyIsNull(m_floatingness - floatingness))) {
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
