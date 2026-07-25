/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef FLOATINGTRANSITION_H
#define FLOATINGTRANSITION_H

#include "floatingpanelgeometry.h"

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

class FloatingTransition final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal floatingness READ floatingness WRITE setFloatingness
                   NOTIFY floatingnessChanged)
    Q_PROPERTY(Target target READ target NOTIFY targetChanged)
    Q_PROPERTY(Phase phase READ phase NOTIFY phaseChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool eligible READ eligible WRITE setEligible NOTIFY eligibleChanged)
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
    [[nodiscard]] Phase phase() const;
    [[nodiscard]] bool running() const;
    [[nodiscard]] bool eligible() const;
    void setEligible(bool eligible);

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

    Q_INVOKABLE void requestAttached();
    Q_INVOKABLE void requestFloated();

Q_SIGNALS:
    void floatingnessChanged();
    void targetChanged();
    void phaseChanged();
    void runningChanged();
    void eligibleChanged();
    void animationDurationChanged();
    void stableGeometryChanged();
    void currentGeometryChanged();

private:
    void requestTarget(Target target);
    void setFloatingness(qreal floatingness);
    void setPhase(Phase phase);

    qreal m_floatingness{1.0};
    Target m_target{Target::Floated};
    Phase m_phase{Phase::Resting};
    bool m_eligible{false};
    int m_animationDuration{0};
    quint64 m_geometryRevision{0};
    std::optional<FloatingPanelGeometry::Solution> m_geometry;
    QPropertyAnimation *m_animation{nullptr};
};

} // namespace Latte::ViewPart

#endif
