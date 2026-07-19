/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef MASKGEOMETRY_H
#define MASKGEOMETRY_H

// Qt
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QtGlobal>

// Plasma
#include <Plasma/Plasma>

// C++
#include <variant>

namespace Latte {

//! The dock's visibility mask and input-region rect math (EX-10 in
//! docs/tracking/QML_EXTRACTION_PLAN.md), extracted from the containment's
//! VisibilityManager.qml updateMaskArea/updateInputGeometry pair. Mask
//! errors are the invisible-dock / dead-input class: user-catastrophic
//! and invisible in screenshots until interacted with, which is why the
//! per-edge tables live here as pure functions under a full test matrix
//! (tests/units/maskgeometrytest.cpp) instead of inside a QML body.
//!
//! Provenance: the local-geometry half matches Qt5 f0ad7b23 verbatim;
//! the input half matches upstream 11f42978 (2022), which replaced Qt5's
//! clear-while-parabolic-animated with the zoomedForItems input band.
//! The QML shell keeps the signal plumbing, the update gates and the
//! actuating property assignments; the thin QML-facing wrapper is
//! containment/plugin/maskgeometrybridge.h.
namespace MaskGeometry {

//! Both rect computations end with the clamps inherited from Qt5
//! ("qBound = qMax(min, qMin(value, max))"): x/y bounded into
//! [0, viewSize], spans capped at viewSize. This is DOCUMENTED
//! NORMALIZATION, not a bandaid: the effects rect and the window size
//! arrive from different signal cascades, so a transiently larger
//! effects rect - or a still-zero window while the layouter warms up -
//! is a legitimate cross-subsystem state. Degenerate results are part
//! of the protocol: Effects::setInputMask (app/view/effects.cpp)
//! documents that warmup rects with zero size legitimately pass through
//! and clear the window mask. Only NEGATIVE dimensions are never-states
//! and assert.
inline QRectF clampIntoViewWindow(QRectF geometry, const QSize &viewSize)
{
    geometry.moveLeft(qBound<qreal>(0, geometry.x(), viewSize.width()));
    geometry.moveTop(qBound<qreal>(0, geometry.y(), viewSize.height()));
    geometry.setWidth(qMin<qreal>(geometry.width(), viewSize.width()));
    geometry.setHeight(qMin<qreal>(geometry.height(), viewSize.height()));
    return geometry;
}

struct LocalGeometryInputs {
    Plasma::Types::Location location{Plasma::Types::Floating};
    bool behaveAsPlasmaPanel{false};
    QSizeF rootSize;           //!< the containment root item size
    QSize viewSize;            //!< the view window size, the clamp bounds
    QRect effectsRect;         //!< latteView.effects.rect, the length-axis source
    qreal totalsThickness{0};  //!< metrics.totals.thickness, the clean applet thickness
    qreal screenEdgeMargin{0}; //!< metrics.mask.screenEdge, the floating gap
};

//! The view-local geometry of the dock's visible body (updateMaskArea's
//! rect half): what View::localGeometry is set to, driving
//! absoluteGeometry and the dodge modes. As a plasma panel the whole
//! window is the panel, so the full root rect applies UNCLAMPED (Qt5
//! keeps the clamps inside the dock branch). As a dock the shadows stay
//! outside: the length axis comes from the effects area and the
//! thickness axis is the clean applet thickness offset by the floating
//! screen-edge gap.
inline QRectF computeLocalGeometry(const LocalGeometryInputs &in)
{
    Q_ASSERT(in.rootSize.width() >= 0 && in.rootSize.height() >= 0);
    Q_ASSERT(in.viewSize.width() >= 0 && in.viewSize.height() >= 0);
    Q_ASSERT(in.totalsThickness >= 0);
    Q_ASSERT(in.screenEdgeMargin >= 0);

    const QRectF fullRootRect(0, 0, in.rootSize.width(), in.rootSize.height());

    if (in.behaveAsPlasmaPanel) {
        return fullRootRect;
    }

    QRectF geometry = fullRootRect;

    switch (in.location) {
    case Plasma::Types::TopEdge:
        geometry = QRectF(in.effectsRect.x(), in.screenEdgeMargin,
                          in.effectsRect.width(), in.totalsThickness);
        break;
    case Plasma::Types::BottomEdge:
        geometry = QRectF(in.effectsRect.x(),
                          in.rootSize.height() - in.totalsThickness - in.screenEdgeMargin,
                          in.effectsRect.width(), in.totalsThickness);
        break;
    case Plasma::Types::LeftEdge:
        geometry = QRectF(in.screenEdgeMargin, in.effectsRect.y(),
                          in.totalsThickness, in.effectsRect.height());
        break;
    case Plasma::Types::RightEdge:
        geometry = QRectF(in.rootSize.width() - in.totalsThickness - in.screenEdgeMargin,
                          in.effectsRect.y(),
                          in.totalsThickness, in.effectsRect.height());
        break;
    default:
        //! non-edge locations (Floating during very early startup) keep
        //! the full root rect - the Qt5 if/else-if fall-through, explicit
        //! here instead of silent
        break;
    }

    return clampIntoViewWindow(geometry, in.viewSize);
}

struct InputMaskInputs {
    Plasma::Types::Location location{Plasma::Types::Floating};
    bool compositingActive{true};
    bool behaveAsPlasmaPanel{false};
    bool isHidden{false};
    bool isSidebar{false};
    bool parabolicAnimating{false};       //!< animations.needBothAxis.count > 0
    bool floatingGapInputDisabled{false}; //!< root.hasFloatingGapInputEventsDisabled
    qreal hiddenThickness{0};             //!< metrics.mask.thickness.hidden
    qreal zoomedForItemsThickness{0};     //!< metrics.mask.thickness.zoomedForItems
    qreal itemsScreenEdgeMargin{0};       //!< metrics.margin.screenEdge
    qreal totalsThickness{0};             //!< metrics.totals.thickness
    qreal maskScreenEdgeMargin{0};        //!< metrics.mask.screenEdge
    QRect localGeometry;                  //!< latteView.localGeometry, the length-axis source
    QSizeF rootSize;                      //!< the containment root item size
    QSize viewSize;                       //!< the view window size, the clamp bounds
};

//! the window accepts input everywhere: no input mask is applied at all
//! (real panels and non-composited sessions)
struct AcceptAllInput {
    bool operator==(const AcceptAllInput &) const = default;
};

//! input is accepted only inside the rect; the boundary converts it to
//! the consumer's QRect exactly like the QML -> Q_PROPERTY assignment
//! did (QRectF::toRect)
struct AcceptInputWithin {
    QRectF rect;
    bool operator==(const AcceptInputWithin &) const = default;
};

//! no input is accepted anywhere (hidden sidebars: showing is on demand,
//! there is no reveal strip)
struct AcceptNoInput {
    bool operator==(const AcceptNoInput &) const = default;
};

//! The effects protocol encodes AcceptAllInput as Qt.rect(0,0,-1,-1) and
//! AcceptNoInput as Qt.rect(-1,-1,1,1). Those sentinel rects are
//! deliberately unrepresentable here: they exist only at the wrapper
//! boundary (maskgeometrybridge.cpp), mapped exhaustively from these
//! alternatives.
using InputMaskDecision = std::variant<AcceptAllInput, AcceptInputWithin, AcceptNoInput>;

//! selects how thick the band accepting input is, measured from the
//! screen edge side of the window
inline qreal selectInputThickness(const InputMaskInputs &in)
{
    if (in.isHidden) {
        //! the reveal strip
        return in.hiddenThickness;
    }

    if (in.floatingGapInputDisabled) {
        //! the floating gap accepts nothing, the band hugs the applets;
        //! zoomedForItems already contains margins.screenEdge
        //! (Metrics.qml), so the gap's share is removed while animated
        return in.parabolicAnimating ? in.zoomedForItemsThickness - in.itemsScreenEdgeMargin
                                     : in.totalsThickness;
    }

    return in.parabolicAnimating ? in.zoomedForItemsThickness
                                 : in.maskScreenEdgeMargin + in.totalsThickness;
}

//! The input-region decision (updateInputGeometry): which part of the
//! window accepts pointer input for the current visibility state. While
//! parabolic-animated the band widens to the full length axis at
//! zoomedForItems thickness - upstream 11f42978's workaround for faulty
//! ParabolicMouseArea onEntered() signals right after sglClearZoom
//! (Qt5-f0ad7b23 cleared the mask entirely instead, letting the whole
//! window take input during zoom).
inline InputMaskDecision computeInputMask(const InputMaskInputs &in)
{
    Q_ASSERT(in.rootSize.width() >= 0 && in.rootSize.height() >= 0);
    Q_ASSERT(in.viewSize.width() >= 0 && in.viewSize.height() >= 0);
    Q_ASSERT(in.hiddenThickness >= 0);
    Q_ASSERT(in.zoomedForItemsThickness >= 0);
    Q_ASSERT(in.itemsScreenEdgeMargin >= 0);
    Q_ASSERT(in.totalsThickness >= 0);
    Q_ASSERT(in.maskScreenEdgeMargin >= 0);

    if (!in.compositingActive) {
        return AcceptAllInput{};
    }

    if (in.behaveAsPlasmaPanel) {
        //! A panel window IS its visible band and accepts pointer input across
        //! all of it, so on Qt5/X11 it carried no window mask at all. Plasma 6
        //! forces a mask here: libplasma's PopupPlasmaWindow anchors an applet
        //! popup (systray volume, calendar, ...) to
        //! parentWindow->mask().boundingRect() so it opens OUTSIDE the panel
        //! band. A maskless panel collapses that bounding rect to a 1px strip
        //! at the window top, and the popup opens OVER the panel, covering the
        //! icon so a second click cannot toggle it closed. A masked dock
        //! already exposes its band through the branch below, which is why only
        //! panels broke. So expose the whole panel window as the mask: input is
        //! unchanged (the entire band still takes events) and the popup anchors
        //! to the real band edge. Platform-forced deviation from Qt5.
        return AcceptInputWithin{clampIntoViewWindow(
            QRectF(0, 0, in.rootSize.width(), in.rootSize.height()), in.viewSize)};
    }

    const qreal inputThickness = selectInputThickness(in);

    //! a floating dock with gap input disabled keeps the band on the
    //! applets by offsetting it away from the screen edge; hidden docks
    //! keep the reveal strip ON the edge
    const qreal subtractedScreenEdge =
            (in.floatingGapInputDisabled && !in.isHidden) ? in.maskScreenEdgeMargin : 0;

    QRectF inputGeometry(0, 0, in.rootSize.width(), in.rootSize.height());

    //! length axis from localGeometry - except while parabolic-animated,
    //! when the full root span applies (11f42978)
    switch (in.location) {
    case Plasma::Types::TopEdge:
        if (!in.parabolicAnimating) {
            inputGeometry.moveLeft(in.localGeometry.x());
            inputGeometry.setWidth(in.localGeometry.width());
        }
        inputGeometry.moveTop(subtractedScreenEdge);
        inputGeometry.setHeight(inputThickness);
        break;
    case Plasma::Types::BottomEdge:
        if (!in.parabolicAnimating) {
            inputGeometry.moveLeft(in.localGeometry.x());
            inputGeometry.setWidth(in.localGeometry.width());
        }
        inputGeometry.moveTop(in.rootSize.height() - inputThickness - subtractedScreenEdge);
        inputGeometry.setHeight(inputThickness);
        break;
    case Plasma::Types::LeftEdge:
        if (!in.parabolicAnimating) {
            inputGeometry.moveTop(in.localGeometry.y());
            inputGeometry.setHeight(in.localGeometry.height());
        }
        inputGeometry.moveLeft(subtractedScreenEdge);
        inputGeometry.setWidth(inputThickness);
        break;
    case Plasma::Types::RightEdge:
        if (!in.parabolicAnimating) {
            inputGeometry.moveTop(in.localGeometry.y());
            inputGeometry.setHeight(in.localGeometry.height());
        }
        inputGeometry.moveLeft(in.rootSize.width() - inputThickness - subtractedScreenEdge);
        inputGeometry.setWidth(inputThickness);
        break;
    default:
        //! non-edge locations keep the full root rect, the Qt5
        //! fall-through (same contract as computeLocalGeometry)
        break;
    }

    inputGeometry = clampIntoViewWindow(inputGeometry, in.viewSize);

    if (in.isSidebar && in.isHidden) {
        return AcceptNoInput{};
    }

    return AcceptInputWithin{inputGeometry};
}

}
}

#endif
