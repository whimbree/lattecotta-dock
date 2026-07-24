/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "backgroundstateresolver.h"

// local
#include "types.h"
#include <plugin/lattetypes.h>

// Qt
#include <QDebug>

// C++
#include <cmath>

namespace Latte {
namespace Containment {

//! the core's closed two-state enum and the QML-facing Latte::Types ints
//! must never drift apart silently
static_assert(static_cast<int>(BackgroundState::ViewType::Dock) == Latte::Types::DockView,
              "BackgroundState::ViewType::Dock must map onto Latte::Types::DockView");
static_assert(static_cast<int>(BackgroundState::ViewType::Panel) == Latte::Types::PanelView,
              "BackgroundState::ViewType::Panel must map onto Latte::Types::PanelView");

static int toQmlViewTypeInt(BackgroundState::ViewType type)
{
    return static_cast<int>(type);
}

BackgroundStateResolver::BackgroundStateResolver(QObject *parent)
    : QObject(parent)
{
}

int BackgroundStateResolver::viewTypeInQuestion(bool viewAndVisibilityArePresent,
                                                bool customShadowedRectangleIsEnabled,
                                                int alignment,
                                                double minLengthPercent,
                                                double maxLengthPercent,
                                                bool backgroundIsGreaterThanItemThickness,
                                                double parabolicMaxZoom) const
{
    const BackgroundState::ViewTypeInQuestionEnv env{
        .viewAndVisibilityArePresent = viewAndVisibilityArePresent,
        .customShadowedRectangleIsEnabled = customShadowedRectangleIsEnabled,
        .alignmentIsJustify = (alignment == Latte::Types::Justify),
        .minLengthPercent = minLengthPercent,
        .maxLengthPercent = maxLengthPercent,
        .backgroundIsGreaterThanItemThickness = backgroundIsGreaterThanItemThickness,
        .parabolicMaxZoom = parabolicMaxZoom,
    };

    return toQmlViewTypeInt(BackgroundState::resolveViewTypeInQuestion(env));
}

int BackgroundStateResolver::viewType(bool viewAndVisibilityArePresent,
                                      int viewTypeInQuestion,
                                      bool screenEdgeMarginEnabled,
                                      bool floatingInternalGapIsForced) const
{
    //! the in-question value round-trips through a QML int property; a
    //! value outside the two-state domain is a shell bug, refused loudly
    //! and resolved to the Dock default rather than propagated
    if (viewTypeInQuestion != Latte::Types::DockView
            && viewTypeInQuestion != Latte::Types::PanelView) {
        qCritical() << "BackgroundStateResolver.viewType: viewTypeInQuestion" << viewTypeInQuestion
                    << "is out of domain {DockView=0, PanelView=1} - resolving to DockView";
        return Latte::Types::DockView;
    }

    const auto inQuestion = static_cast<BackgroundState::ViewType>(viewTypeInQuestion);

    return toQmlViewTypeInt(BackgroundState::resolveViewType(viewAndVisibilityArePresent,
                                                             inQuestion,
                                                             screenEdgeMarginEnabled,
                                                             floatingInternalGapIsForced));
}

bool BackgroundStateResolver::solidPanelForced(bool viewAndVisibilityArePresent,
                                               bool compositingActive,
                                               bool inConfigureAppletsMode,
                                               bool userShowPanelBackground,
                                               bool solidBackgroundForMaximizedIsEnabled,
                                               bool hasExpandedApplet,
                                               bool plasmaBackgroundForPopupsIsEnabled,
                                               bool existsWindowTouching,
                                               bool existsWindowTouchingEdge) const
{
    const BackgroundState::SolidPanelEnv env{
        .viewAndVisibilityArePresent = viewAndVisibilityArePresent,
        .compositingActive = compositingActive,
        .inConfigureAppletsMode = inConfigureAppletsMode,
        .userShowPanelBackground = userShowPanelBackground,
        .solidBackgroundForMaximizedIsEnabled = solidBackgroundForMaximizedIsEnabled,
        .hasExpandedApplet = hasExpandedApplet,
        .plasmaBackgroundForPopupsIsEnabled = plasmaBackgroundForPopupsIsEnabled,
        .existsWindowTouching = existsWindowTouching,
        .existsWindowTouchingEdge = existsWindowTouchingEdge,
    };

    return BackgroundState::isSolidPanelForced(env);
}

bool BackgroundStateResolver::transparentPanelForced(bool backgroundOnlyOnMaximized,
                                                     bool viewAndVisibilityArePresent,
                                                     bool compositingActive,
                                                     bool inConfigureAppletsMode,
                                                     bool solidPanelIsForced,
                                                     bool existsWindowTouching,
                                                     bool existsWindowTouchingEdge,
                                                     int windowColors,
                                                     bool selectedTrackerExistsWindowActive) const
{
    const BackgroundState::TransparentPanelEnv env{
        .backgroundOnlyOnMaximized = backgroundOnlyOnMaximized,
        .viewAndVisibilityArePresent = viewAndVisibilityArePresent,
        .compositingActive = compositingActive,
        .inConfigureAppletsMode = inConfigureAppletsMode,
        .solidPanelIsForced = solidPanelIsForced,
        .existsWindowTouching = existsWindowTouching,
        .existsWindowTouchingEdge = existsWindowTouchingEdge,
        .windowColorsFollowActiveWindow = (windowColors == Types::ActiveWindowColors),
        .selectedTrackerExistsWindowActive = selectedTrackerExistsWindowActive,
    };

    return BackgroundState::isTransparentPanelForced(env);
}

bool BackgroundStateResolver::panelForcedForBusyBackground(bool userShowPanelBackground,
                                                           bool viewIsTouchingBusyVerticalView,
                                                           bool backgroundOnlyOnMaximized,
                                                           bool transparentPanelIsForced,
                                                           bool colorizerBackgroundIsBusy,
                                                           int themeColors) const
{
    const BackgroundState::BusyBackgroundEnv env{
        .userShowPanelBackground = userShowPanelBackground,
        .viewIsTouchingBusyVerticalView = viewIsTouchingBusyVerticalView,
        .backgroundOnlyOnMaximized = backgroundOnlyOnMaximized,
        .transparentPanelIsForced = transparentPanelIsForced,
        .colorizerBackgroundIsBusy = colorizerBackgroundIsBusy,
        .themeColorsAreSmart = (themeColors == Types::SmartThemeColors),
    };

    return BackgroundState::isPanelForcedForBusyBackground(env);
}

bool BackgroundStateResolver::blurActive(bool blurIsEnabledInConfig,
                                         bool transparentPanelIsForced,
                                         bool busyBackgroundPanelIsForced) const
{
    return BackgroundState::isBlurActive(blurIsEnabledInConfig,
                                         transparentPanelIsForced,
                                         busyBackgroundPanelIsForced);
}

bool BackgroundStateResolver::panelShadowsActive(bool userShowPanelBackground,
                                                 bool inConfigureAppletsMode,
                                                 bool panelShadowsAreEnabledInConfig,
                                                 bool disablePanelShadowsForMaximizedIsActive,
                                                 bool activeWindowIsMaximized,
                                                 bool blurIsActive,
                                                 double backgroundCurrentOpacity,
                                                 bool busyBackgroundPanelIsForced,
                                                 bool backgroundOnlyOnMaximized,
                                                 bool transparentPanelIsForced,
                                                 bool hasExpandedApplet,
                                                 bool plasmaBackgroundForPopupsIsEnabled) const
{
    const BackgroundState::PanelShadowsEnv env{
        .userShowPanelBackground = userShowPanelBackground,
        .inConfigureAppletsMode = inConfigureAppletsMode,
        .panelShadowsAreEnabledInConfig = panelShadowsAreEnabledInConfig,
        .disablePanelShadowsForMaximizedIsActive = disablePanelShadowsForMaximizedIsActive,
        .activeWindowIsMaximized = activeWindowIsMaximized,
        .blurIsActive = blurIsActive,
        .backgroundCurrentOpacity = backgroundCurrentOpacity,
        .busyBackgroundPanelIsForced = busyBackgroundPanelIsForced,
        .backgroundOnlyOnMaximized = backgroundOnlyOnMaximized,
        .transparentPanelIsForced = transparentPanelIsForced,
        .hasExpandedApplet = hasExpandedApplet,
        .plasmaBackgroundForPopupsIsEnabled = plasmaBackgroundForPopupsIsEnabled,
    };

    return BackgroundState::arePanelShadowsActive(env);
}

QRectF BackgroundStateResolver::effectsArea(bool compositingActive,
                                            bool viewIsHidden,
                                            bool behavesAsPlasmaPanel,
                                            double backgroundOriginXInRoot,
                                            double backgroundOriginYInRoot,
                                            double backgroundWidth,
                                            double backgroundHeight) const
{
    const BackgroundState::EffectsAreaEnv env{
        .compositingActive = compositingActive,
        .viewIsHidden = viewIsHidden,
        .behavesAsPlasmaPanel = behavesAsPlasmaPanel,
        .backgroundOriginInRoot = QPointF(backgroundOriginXInRoot, backgroundOriginYInRoot),
        .backgroundSize = QSizeF(backgroundWidth, backgroundHeight),
    };

    return BackgroundState::resolveEffectsArea(env);
}

double BackgroundStateResolver::dockBackgroundLength(double requestedBackgroundLength,
                                                      double owningCanvasLength,
                                                      double shadowMarginsLength) const
{
    if (!std::isfinite(requestedBackgroundLength)
            || !std::isfinite(owningCanvasLength)
            || !std::isfinite(shadowMarginsLength)
            || requestedBackgroundLength < 0.0
            || owningCanvasLength < 0.0
            || shadowMarginsLength < 0.0) {
        qCritical() << "BackgroundStateResolver.dockBackgroundLength: invalid geometry"
                    << requestedBackgroundLength << owningCanvasLength
                    << shadowMarginsLength;
        return 0.0;
    }

    return BackgroundState::fitDockBackgroundLength(requestedBackgroundLength,
                                                     owningCanvasLength,
                                                     shadowMarginsLength);
}

double BackgroundStateResolver::centeredDockOffset(double requestedOffset,
                                                    double visualLength,
                                                    double viewPrimaryLength) const
{
    if (!std::isfinite(requestedOffset)
            || !std::isfinite(visualLength)
            || !std::isfinite(viewPrimaryLength)
            || visualLength < 0.0
            || viewPrimaryLength < visualLength) {
        qCritical() << "BackgroundStateResolver.centeredDockOffset: invalid geometry"
                    << requestedOffset << visualLength << viewPrimaryLength;
        return 0.0;
    }

    return BackgroundState::fitCenteredDockOffset(requestedOffset,
                                                   visualLength,
                                                   viewPrimaryLength);
}

double BackgroundStateResolver::edgePadding(bool borderIsPresent,
                                            bool paddingLiesOnLengthAxis,
                                            bool customRadiusIsEnabled,
                                            double customRadius,
                                            double themePadding,
                                            double solidBackgroundPadding,
                                            double appliedLengthMargin,
                                            double backgroundCornerMarginFactor) const
{
    //! third-party indicator packages hand backgroundCornerMargin over
    //! (indicators.info reads it off the loaded indicator item); a
    //! negative or non-finite factor is refused loudly and the documented
    //! IndicatorInfo default of 1.0 applies - a padding must never go
    //! negative or NaN, it feeds view geometry
    double factor = backgroundCornerMarginFactor;
    if (!std::isfinite(factor) || factor < 0.0) {
        qCritical() << "BackgroundStateResolver.edgePadding: indicator backgroundCornerMargin"
                    << backgroundCornerMarginFactor
                    << "is malformed - using the IndicatorInfo default 1.0";
        factor = 1.0;
    }

    const BackgroundState::EdgePaddingEnv env{
        .borderIsPresent = borderIsPresent,
        .paddingLiesOnLengthAxis = paddingLiesOnLengthAxis,
        .customRadiusIsEnabled = customRadiusIsEnabled,
        .customRadius = customRadius,
        .themePadding = themePadding,
        .solidBackgroundPadding = solidBackgroundPadding,
        .appliedLengthMargin = appliedLengthMargin,
        .backgroundCornerMarginFactor = factor,
    };

    return BackgroundState::resolveEdgePadding(env);
}

}
}
