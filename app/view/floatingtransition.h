/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef FLOATINGTRANSITION_H
#define FLOATINGTRANSITION_H

#include "floatingpanelgeometry.h"
#include "floatingpopuppresentation.h"

#include <QAbstractAnimation>
#include <QEasingCurve>
#include <QMargins>
#include <QObject>
#include <QPointF>
#include <QPropertyAnimation>
#include <QRect>
#include <QRectF>

#include <optional>

namespace Latte::ViewPart {

class Positioner;

class FloatingTransition final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal floatingness READ floatingness WRITE setFloatingness
                   NOTIFY floatingnessChanged)
    Q_PROPERTY(Target target READ target NOTIFY targetChanged)
    Q_PROPERTY(bool attachmentTargeted READ attachmentTargeted
                   NOTIFY targetChanged)
    Q_PROPERTY(bool floatingAppletPopupsPreferred
                   READ floatingAppletPopupsPreferred NOTIFY targetChanged)
    Q_PROPERTY(Phase phase READ phase NOTIFY phaseChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool floatingPanelEligible READ floatingPanelEligible
                   NOTIFY floatingPanelEligibleChanged)
    Q_PROPERTY(bool attachOnWindowTouchConfigured
                   READ attachOnWindowTouchConfigured
                   NOTIFY attachOnWindowTouchConfiguredChanged)
    Q_PROPERTY(bool attachmentWaitsForPointerExitConfigured
                   READ attachmentWaitsForPointerExitConfigured
                   NOTIFY attachmentWaitsForPointerExitConfiguredChanged)
    Q_PROPERTY(bool pointerInsideView
                   READ pointerInsideView
                   NOTIFY pointerInsideViewChanged)
    Q_PROPERTY(bool attachmentDeferredByPointer
                   READ attachmentDeferredByPointer
                   NOTIFY attachmentDeferredByPointerChanged)
    Q_PROPERTY(bool dockGapHideRequested READ dockGapHideRequested
                   NOTIFY dockGapHideRequestedChanged)
    Q_PROPERTY(int touchingWindowCount READ touchingWindowCount
                   NOTIFY touchingWindowCountChanged)
    Q_PROPERTY(int animationDuration READ animationDuration WRITE setAnimationDuration
                   NOTIFY animationDurationChanged)

    Q_PROPERTY(bool hasGeometry READ hasGeometry NOTIFY stableGeometryChanged)
    Q_PROPERTY(QRect stableCanvasGeometry READ stableCanvasGeometry
                   NOTIFY stableGeometryChanged)
    Q_PROPERTY(QRect attachedGeometry READ attachedGeometry NOTIFY stableGeometryChanged)
    Q_PROPERTY(QRect floatedGeometry READ floatedGeometry NOTIFY stableGeometryChanged)
    Q_PROPERTY(QRect stableTriggerGeometry READ stableTriggerGeometry
                   NOTIFY stableGeometryChanged)
    Q_PROPERTY(QRect appletMeasurementBounds READ appletMeasurementBounds
                   NOTIFY stableGeometryChanged)
    Q_PROPERTY(int primaryAxisStart READ primaryAxisStart NOTIFY stableGeometryChanged)
    Q_PROPERTY(int primaryAxisLength READ primaryAxisLength NOTIFY stableGeometryChanged)
    Q_PROPERTY(int stableReservationDepth READ stableReservationDepth
                   NOTIFY stableGeometryChanged)

    Q_PROPERTY(QRectF currentVisibleGeometry READ currentVisibleGeometry
                   NOTIFY currentGeometryChanged)
    Q_PROPERTY(QRectF fittsBridgeGeometry READ fittsBridgeGeometry
                   NOTIFY currentGeometryChanged)
    Q_PROPERTY(QPointF contentTranslation READ contentTranslation
                   NOTIFY currentGeometryChanged)
    Q_PROPERTY(QRect currentPaintMaskGeometry READ currentPaintMaskGeometry
                   NOTIFY currentGeometryChanged)
    Q_PROPERTY(QRect currentInputBridgeGeometry READ currentInputBridgeGeometry
                   NOTIFY currentGeometryChanged)

public:
    enum class Target {
        Attached,
        Floated,
    };
    Q_ENUM(Target)

    enum class Phase {
        Resting,
        Attaching,
        Floating,
    };
    Q_ENUM(Phase)

    explicit FloatingTransition(QObject *parent = nullptr,
                                QPropertyAnimation *animation = nullptr);
    ~FloatingTransition() override;

    [[nodiscard]] qreal floatingness() const;
    [[nodiscard]] Target target() const;
    [[nodiscard]] bool attachmentTargeted() const;
    [[nodiscard]] bool floatingAppletPopupsPreferred() const;
    [[nodiscard]] Phase phase() const;
    [[nodiscard]] bool running() const;
    [[nodiscard]] bool floatingPanelEligible() const;
    [[nodiscard]] bool attachOnWindowTouchConfigured() const;
    [[nodiscard]] bool attachmentWaitsForPointerExitConfigured() const;
    [[nodiscard]] bool pointerInsideView() const;
    [[nodiscard]] bool attachmentDeferredByPointer() const;
    [[nodiscard]] bool dockGapHideRequested() const;
    [[nodiscard]] int touchingWindowCount() const;

    [[nodiscard]] int animationDuration() const;
    void setAnimationDuration(int duration);

    [[nodiscard]] bool hasGeometry() const;
    [[nodiscard]] QRect stableCanvasGeometry() const;
    [[nodiscard]] QRect attachedGeometry() const;
    [[nodiscard]] QRect floatedGeometry() const;
    [[nodiscard]] QRect stableTriggerGeometry() const;
    [[nodiscard]] QRect appletMeasurementBounds() const;
    [[nodiscard]] int primaryAxisStart() const;
    [[nodiscard]] int primaryAxisLength() const;
    [[nodiscard]] int stableReservationDepth() const;

    [[nodiscard]] QRectF currentVisibleGeometry() const;
    [[nodiscard]] QRectF fittsBridgeGeometry() const;
    [[nodiscard]] QPointF contentTranslation() const;
    [[nodiscard]] QRect currentPaintMaskGeometry() const;
    [[nodiscard]] QRect currentInputBridgeGeometry() const;
    [[nodiscard]] QMargins currentShadowPaddingOffsets() const;
    [[nodiscard]] bool screenEdgeBorderVisible() const;
    [[nodiscard]] bool floatingCornersVisible() const;
    [[nodiscard]] FloatingPanelGeometry::InputDisposition classifyInput(
        const QPointF &position) const;
    [[nodiscard]] QPointF positionAdjustedForVisibleMask(
        const QPointF &position) const;

    [[nodiscard]] quint64 geometryRevision() const;
    [[nodiscard]] bool configureGeometry(
        const FloatingPanelGeometry::Inputs &inputs);
    void configureGeometry(const FloatingPanelGeometry::Solution &solution);
    void clearGeometry();

    Q_INVOKABLE void reconcileTargetPolicy(
        bool floatingPanelEligible,
        bool attachOnWindowTouchConfigured,
        bool attachmentWaitsForPointerExitConfigured,
        bool pointerInsideView,
        int touchingWindowCount,
        bool dockGapHideRequested);
    Q_INVOKABLE int displayHintsWithFloatingPreference(
        int currentHints,
        int floatingHint,
        bool floatingPanelConfigured) const;

Q_SIGNALS:
    void floatingnessChanged();
    void targetChanged();
    void phaseChanged();
    void runningChanged();
    void floatingPanelEligibleChanged();
    void attachOnWindowTouchConfiguredChanged();
    void attachmentWaitsForPointerExitConfiguredChanged();
    void pointerInsideViewChanged();
    void attachmentDeferredByPointerChanged();
    void dockGapHideRequestedChanged();
    void touchingWindowCountChanged();
    void animationDurationChanged();
    void stableGeometryChanged();
    void currentGeometryChanged();

private:
    friend class Positioner;

    [[nodiscard]] bool installGeometryWithoutNotification(
        const std::optional<FloatingPanelGeometry::Solution> &geometry);
    void publishInstalledGeometryChange();
    void requestTarget(Target target);
    void setFloatingness(qreal floatingness);
    void setPhase(Phase phase);

    qreal m_floatingness{1.0};
    Target m_target{Target::Floated};
    Phase m_phase{Phase::Resting};
    bool m_floatingPanelEligible{false};
    bool m_attachOnWindowTouchConfigured{false};
    bool m_attachmentWaitsForPointerExitConfigured{false};
    bool m_pointerInsideView{false};
    bool m_attachmentDeferredByPointer{false};
    bool m_dockGapHideRequested{false};
    int m_touchingWindowCount{0};
    int m_animationDuration{0};
    quint64 m_geometryRevision{0};
    std::optional<FloatingPanelGeometry::Solution> m_geometry;
    QPropertyAnimation *m_animation{nullptr};
};

} // namespace Latte::ViewPart

#endif
