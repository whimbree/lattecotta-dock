/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef MASKGEOMETRYBRIDGE_H
#define MASKGEOMETRYBRIDGE_H

// Qt
#include <QObject>
#include <QRect>

namespace Latte {
namespace Containment {

//! Thin QML shell over the MaskGeometry pure core (EX-10 in
//! docs/tracking/QML_EXTRACTION_PLAN.md, units/maskgeometry.h). The containment's
//! VisibilityManager.qml keeps its signal plumbing and update gates and
//! asks here for the rects; this boundary maps the core's decision
//! alternatives onto the effects protocol's sentinel rects
//! (Qt.rect(0,0,-1,-1) = accept everywhere, Qt.rect(-1,-1,1,1) = accept
//! nowhere - see Effects::setInputMask, app/view/effects.cpp).
//!
//! Arguments are read at call time on purpose: bound properties would
//! refresh on their own notify schedule relative to the signal handlers
//! that trigger updates, and a stale-by-one-update mask is the
//! invisible-dock / dead-input bug class.
//! not final: qmlRegisterType instantiates through a QQmlElement subclass
class MaskGeometryBridge : public QObject
{
    Q_OBJECT

public:
    explicit MaskGeometryBridge(QObject *parent = nullptr);

public Q_SLOTS:
    //! The view-local geometry of the dock's visible body
    //! (updateMaskArea's rect half): per-edge selection of the effects
    //! length axis and the clean applet thickness, clamped into the view
    //! window. Refuses negative dimensions and unknown locations loudly,
    //! returning an empty rect (the consumer's documented clear path).
    Q_INVOKABLE QRect localGeometryFor(int location, bool behaveAsPlasmaPanel,
                                       qreal rootWidth, qreal rootHeight,
                                       int viewWidth, int viewHeight,
                                       QRect effectsRect,
                                       qreal totalsThickness, qreal screenEdgeMargin);

    //! The input-region rect (updateInputGeometry): which part of the
    //! window accepts pointer input for the current visibility state,
    //! with the core's decision alternatives mapped onto the effects
    //! protocol's sentinel rects at this boundary. Refuses negative
    //! dimensions/thicknesses and unknown locations loudly, degrading to
    //! accept-everywhere so a refused call never bricks input.
    Q_INVOKABLE QRect inputMaskFor(int location,
                                   bool compositingActive, bool behaveAsPlasmaPanel,
                                   bool isHidden, bool isSidebar,
                                   bool parabolicAnimating, bool floatingGapInputDisabled,
                                   qreal hiddenThickness, qreal zoomedForItemsThickness,
                                   qreal itemsScreenEdgeMargin, qreal totalsThickness,
                                   qreal maskScreenEdgeMargin,
                                   QRect localGeometry,
                                   qreal rootWidth, qreal rootHeight,
                                   int viewWidth, int viewHeight);
};

}
}

#endif
