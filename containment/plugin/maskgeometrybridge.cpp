/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "maskgeometrybridge.h"

// local
#include "units/maskgeometry.h"

// Qt
#include <QDebug>

// C++
#include <cmath>
#include <optional>
#include <type_traits>
#include <variant>

namespace Latte {
namespace Containment {

namespace {

//! the effects protocol's sentinel rects (Effects::setInputMask,
//! app/view/effects.cpp) - they exist only at this boundary
const QRect kAcceptInputEverywhere(0, 0, -1, -1);
const QRect kAcceptInputNowhere(-1, -1, 1, 1);

//! Plasmoid.location arrives as an int; anything outside the Plasma
//! enum is a caller bug, refused loudly by the invokables
std::optional<Plasma::Types::Location> toLocation(int location)
{
    if (location < Plasma::Types::Floating || location > Plasma::Types::RightEdge) {
        return std::nullopt;
    }

    return static_cast<Plasma::Types::Location>(location);
}

}

MaskGeometryBridge::MaskGeometryBridge(QObject *parent)
    : QObject(parent)
{
}

QRect MaskGeometryBridge::localGeometryFor(int location, bool behaveAsPlasmaPanel,
                                           qreal rootWidth, qreal rootHeight,
                                           int viewWidth, int viewHeight,
                                           QRect effectsRect,
                                           qreal totalsThickness, qreal screenEdgeMargin)
{
    //! zero sizes are the documented warmup protocol and pass through
    //! (Effects clears the mask for degenerate rects); NEGATIVE
    //! dimensions and thicknesses have no producer - arriving here with
    //! one is a shell bug, never something to normalize silently
    const bool geometryIsValid = rootWidth >= 0 && rootHeight >= 0
            && viewWidth >= 0 && viewHeight >= 0
            && totalsThickness >= 0 && screenEdgeMargin >= 0;
    const std::optional<Plasma::Types::Location> loc = toLocation(location);

    if (!geometryIsValid || !loc) {
        qCritical() << "MaskGeometryBridge.localGeometryFor: invalid inputs, location" << location
                    << "root" << rootWidth << "x" << rootHeight
                    << "view" << viewWidth << "x" << viewHeight
                    << "totalsThickness" << totalsThickness
                    << "screenEdgeMargin" << screenEdgeMargin
                    << "- refusing; returning an empty local geometry";
        return QRect();
    }

    MaskGeometry::LocalGeometryInputs in;
    in.location = *loc;
    in.behaveAsPlasmaPanel = behaveAsPlasmaPanel;
    in.rootSize = QSizeF(rootWidth, rootHeight);
    in.viewSize = QSize(viewWidth, viewHeight);
    in.effectsRect = effectsRect;
    in.totalsThickness = totalsThickness;
    in.screenEdgeMargin = screenEdgeMargin;

    //! toRect() is exactly the conversion the QML rect -> QRect
    //! Q_PROPERTY assignment performed before the extraction
    return MaskGeometry::computeLocalGeometry(in).toRect();
}

QRect MaskGeometryBridge::stableJustifyDockOccupancyFor(
    int location,
    qreal rootWidth, qreal rootHeight,
    int viewWidth, int viewHeight,
    qreal stablePrimaryLength,
    qreal totalsThickness,
    qreal configuredScreenEdgeMargin)
{
    const std::optional<Plasma::Types::Location> loc = toLocation(location);
    const bool isEdge = loc
        && (*loc == Plasma::Types::TopEdge
            || *loc == Plasma::Types::BottomEdge
            || *loc == Plasma::Types::LeftEdge
            || *loc == Plasma::Types::RightEdge);
    const bool finite = std::isfinite(rootWidth)
        && std::isfinite(rootHeight)
        && std::isfinite(stablePrimaryLength)
        && std::isfinite(totalsThickness)
        && std::isfinite(configuredScreenEdgeMargin);
    const bool horizontal = isEdge
        && (*loc == Plasma::Types::TopEdge
            || *loc == Plasma::Types::BottomEdge);
    const qreal owningPrimaryLength = horizontal
        ? rootWidth : rootHeight;
    const bool geometryIsValid = finite
        && rootWidth >= 0 && rootHeight >= 0
        && viewWidth >= 0 && viewHeight >= 0
        && stablePrimaryLength >= 0
        && stablePrimaryLength <= owningPrimaryLength
        && totalsThickness >= 0
        && configuredScreenEdgeMargin >= 0;

    if (!isEdge || !geometryIsValid) {
        qCritical()
            << "MaskGeometryBridge.stableJustifyDockOccupancyFor:"
               " invalid stable occupancy"
            << "location" << location
            << "root" << rootWidth << "x" << rootHeight
            << "view" << viewWidth << "x" << viewHeight
            << "stablePrimaryLength" << stablePrimaryLength
            << "totalsThickness" << totalsThickness
            << "configuredScreenEdgeMargin"
            << configuredScreenEdgeMargin
            << "- refusing; returning an empty occupied geometry";
        return {};
    }

    const MaskGeometry::StableJustifyDockOccupancyInputs in{
        .location = *loc,
        .rootSize = QSizeF(rootWidth, rootHeight),
        .viewSize = QSize(viewWidth, viewHeight),
        .stablePrimaryLength = stablePrimaryLength,
        .totalsThickness = totalsThickness,
        .configuredScreenEdgeMargin = configuredScreenEdgeMargin,
    };
    return MaskGeometry::computeStableJustifyDockOccupancy(in).toRect();
}

QRect MaskGeometryBridge::inputMaskFor(int location,
                                       bool compositingActive, bool behaveAsPlasmaPanel,
                                       bool isHidden, bool isSidebar,
                                       bool parabolicAnimating, bool floatingGapInputDisabled,
                                       qreal hiddenThickness, qreal zoomedForItemsThickness,
                                       qreal itemsScreenEdgeMargin, qreal totalsThickness,
                                       qreal maskScreenEdgeMargin,
                                       QRect localGeometry,
                                       qreal rootWidth, qreal rootHeight,
                                       int viewWidth, int viewHeight)
{
    const bool geometryIsValid = rootWidth >= 0 && rootHeight >= 0
            && viewWidth >= 0 && viewHeight >= 0
            && hiddenThickness >= 0 && zoomedForItemsThickness >= 0
            && itemsScreenEdgeMargin >= 0 && totalsThickness >= 0
            && maskScreenEdgeMargin >= 0;
    const std::optional<Plasma::Types::Location> loc = toLocation(location);

    if (!geometryIsValid || !loc) {
        qCritical() << "MaskGeometryBridge.inputMaskFor: invalid inputs, location" << location
                    << "root" << rootWidth << "x" << rootHeight
                    << "view" << viewWidth << "x" << viewHeight
                    << "thicknesses" << hiddenThickness << zoomedForItemsThickness
                    << itemsScreenEdgeMargin << totalsThickness << maskScreenEdgeMargin
                    << "- refusing; leaving input accepted everywhere";
        return kAcceptInputEverywhere;
    }

    if (behaveAsPlasmaPanel && !isHidden && !isSidebar) {
        qCritical() << "MaskGeometryBridge.inputMaskFor: visible panel input "
                       "belongs to FloatingTransition; refusing legacy write";
        return kAcceptInputNowhere;
    }

    MaskGeometry::InputMaskInputs in;
    in.location = *loc;
    in.compositingActive = compositingActive;
    in.isHidden = isHidden;
    in.isSidebar = isSidebar;
    in.parabolicAnimating = parabolicAnimating;
    in.floatingGapInputDisabled = floatingGapInputDisabled;
    in.hiddenThickness = hiddenThickness;
    in.zoomedForItemsThickness = zoomedForItemsThickness;
    in.itemsScreenEdgeMargin = itemsScreenEdgeMargin;
    in.totalsThickness = totalsThickness;
    in.maskScreenEdgeMargin = maskScreenEdgeMargin;
    in.localGeometry = localGeometry;
    in.rootSize = QSizeF(rootWidth, rootHeight);
    in.viewSize = QSize(viewWidth, viewHeight);

    const MaskGeometry::InputMaskDecision decision = MaskGeometry::computeInputMask(in);

    //! exhaustive by construction: a new InputMaskDecision alternative
    //! fails the static_assert at compile time instead of misdispatching
    return std::visit([](const auto &alternative) {
        using T = std::decay_t<decltype(alternative)>;
        if constexpr (std::is_same_v<T, MaskGeometry::AcceptAllInput>) {
            return kAcceptInputEverywhere;
        } else if constexpr (std::is_same_v<T, MaskGeometry::AcceptInputWithin>) {
            //! toRect() matches the pre-extraction QML rect -> QRect
            //! Q_PROPERTY conversion
            return alternative.rect.toRect();
        } else {
            static_assert(std::is_same_v<T, MaskGeometry::AcceptNoInput>);
            return kAcceptInputNowhere;
        }
    }, decision);
}

}
}
