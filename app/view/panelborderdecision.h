/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANELBORDERDECISION_H
#define PANELBORDERDECISION_H

#include "floatingpanelgeometry.h"

#include <KSvg/FrameSvg>

namespace Latte::ViewPart::PanelBorderDecision {

enum class Alignment {
    Start,
    Center,
    End,
    Justify,
};

struct Inputs {
    FloatingPanelGeometry::Edge edge{FloatingPanelGeometry::Edge::Bottom};
    Alignment alignment{Alignment::Center};
    bool configuredFloatingPresentation{false};
    bool screenEdgeBorderVisible{false};
    bool floatingCornersVisible{false};
    bool primaryAxisFillsOutput{false};
    bool screenEdgeMarginEnabled{false};
    bool backgroundAllCorners{false};
    bool forcePrimaryStartBorder{false};
    bool forcePrimaryEndBorder{false};
    qreal maxLength{1.0};
    qreal offset{0.0};
};

[[nodiscard]] inline bool doesPresentationFillPrimaryAxis(
    const QRect &presentation,
    const QSize &canvas,
    FloatingPanelGeometry::Edge edge)
{
    //! The Dock effects rectangle is empty during normal startup warmup. It
    //! has no presented endpoints until QML publishes the first background.
    if (!presentation.isValid() || canvas.isEmpty()) {
        return false;
    }

    const QRect canvasBounds(QPoint(0, 0), canvas);
    return FloatingPanelGeometry::isHorizontal(edge)
        ? presentation.left() == canvasBounds.left()
            && presentation.right() == canvasBounds.right()
        : presentation.top() == canvasBounds.top()
            && presentation.bottom() == canvasBounds.bottom();
}

[[nodiscard]] inline KSvg::FrameSvg::EnabledBorders enabledBorders(
    const Inputs &inputs)
{
    using Border = KSvg::FrameSvg;

    // Every floating shape owns all four corners. Gate the geometric predicate
    // with view configuration because flush panels also rest at progress 1.
    if (inputs.configuredFloatingPresentation
        && inputs.floatingCornersVisible) {
        return Border::AllBorders;
    }

    Border::EnabledBorders borders = Border::AllBorders;

    const bool hidePhysicalEdge =
        (inputs.configuredFloatingPresentation
         && !inputs.screenEdgeBorderVisible)
        || (!inputs.configuredFloatingPresentation
            && !inputs.screenEdgeMarginEnabled
            && !inputs.backgroundAllCorners);
    if (hidePhysicalEdge) {
        switch (inputs.edge) {
        case FloatingPanelGeometry::Edge::Top:
            borders &= ~Border::TopBorder;
            break;
        case FloatingPanelGeometry::Edge::Right:
            borders &= ~Border::RightBorder;
            break;
        case FloatingPanelGeometry::Edge::Bottom:
            borders &= ~Border::BottomBorder;
            break;
        case FloatingPanelGeometry::Edge::Left:
            borders &= ~Border::LeftBorder;
            break;
        }
    }

    const bool attachedFloatingPresentation =
        inputs.configuredFloatingPresentation
        && !inputs.floatingCornersVisible;
    if (inputs.backgroundAllCorners
        && !attachedFloatingPresentation) {
        return borders;
    }

    const bool vertical = inputs.edge == FloatingPanelGeometry::Edge::Left
        || inputs.edge == FloatingPanelGeometry::Edge::Right;
    if (vertical) {
        if (inputs.primaryAxisFillsOutput
            || (inputs.maxLength == 1.0
                && inputs.alignment == Alignment::Justify)) {
            if (!inputs.forcePrimaryStartBorder) {
                borders &= ~Border::TopBorder;
            }
            if (!inputs.forcePrimaryEndBorder) {
                borders &= ~Border::BottomBorder;
            }
        }

        if (inputs.alignment == Alignment::Start && inputs.offset == 0.0
            && !inputs.forcePrimaryStartBorder) {
            borders &= ~Border::TopBorder;
        }
        if (inputs.alignment == Alignment::End && inputs.offset == 0.0
            && !inputs.forcePrimaryEndBorder) {
            borders &= ~Border::BottomBorder;
        }
    } else {
        if (inputs.primaryAxisFillsOutput
            || (inputs.maxLength == 1.0
                && inputs.alignment == Alignment::Justify)) {
            borders &= ~Border::LeftBorder;
            borders &= ~Border::RightBorder;
        }
        if (inputs.alignment == Alignment::Start && inputs.offset == 0.0) {
            borders &= ~Border::LeftBorder;
        }
        if (inputs.alignment == Alignment::End && inputs.offset == 0.0) {
            borders &= ~Border::RightBorder;
        }
    }

    return borders;
}

} // namespace Latte::ViewPart::PanelBorderDecision

#endif
