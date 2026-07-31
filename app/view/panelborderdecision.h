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

[[nodiscard]] inline bool doesPresentationFillOutputPrimaryAxis(
    const QRect &presentation,
    const QRect &viewGeometry,
    const QRect &outputGeometry,
    FloatingPanelGeometry::Edge edge)
{
    //! No presentation exists until QML publishes its first effects rectangle
    //! during startup. That absence has no output endpoints. The caller owns
    //! the surface and output authorities and must never provide invalid
    //! geometry once a presentation exists.
    if (!presentation.isValid()) {
        return false;
    }
    Q_ASSERT(viewGeometry.isValid());
    Q_ASSERT(outputGeometry.isValid());

    //! Effects geometry is local to the view. A Dock normally owns an
    //! output-sized masked canvas, while a Panel's QWindow is only its own
    //! configured span. Compare translated paint to the assigned output so a
    //! partial Panel filling its canvas cannot impersonate output coverage.

    const qint64 presentationStart =
        FloatingPanelGeometry::isHorizontal(edge)
        ? qint64(viewGeometry.x()) + presentation.x()
        : qint64(viewGeometry.y()) + presentation.y();
    const qint64 presentationEnd = presentationStart
        + (FloatingPanelGeometry::isHorizontal(edge)
            ? presentation.width() : presentation.height());
    const qint64 outputStart =
        FloatingPanelGeometry::isHorizontal(edge)
        ? outputGeometry.x() : outputGeometry.y();
    const qint64 outputEnd = outputStart
        + (FloatingPanelGeometry::isHorizontal(edge)
            ? outputGeometry.width() : outputGeometry.height());
    return presentationStart == outputStart
        && presentationEnd == outputEnd;
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
