/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef BACKGROUNDSTATERESOLVER_H
#define BACKGROUNDSTATERESOLVER_H

// local
#include "units/backgroundstate.h"

// Qt
#include <QObject>
#include <QRectF>

namespace Latte {
namespace Containment {

//! Thin QML shell over the BackgroundState pure core (EX-13 in
//! docs/tracking/QML_EXTRACTION_PLAN.md, units/backgroundstate.h). Stateless: the
//! containment main.qml and background/MultiLayered.qml each instantiate
//! one and keep their per-predicate property bindings (the binding split
//! breaks loops the core must not re-tie; see the core's resolveViewType
//! note). The wrapper owns the QML boundary: enum-int classification
//! against the real Latte enums, and loud refusal of out-of-domain input.
//! not final: qmlRegisterType instantiates through a QQmlElement subclass
class BackgroundStateResolver : public QObject
{
    Q_OBJECT

public:
    explicit BackgroundStateResolver(QObject *parent = nullptr);

public Q_SLOTS:
    //! main.qml viewTypeInQuestion; alignment is the raw config int,
    //! classified against Latte::Types::Justify here
    Q_INVOKABLE int viewTypeInQuestion(bool viewAndVisibilityArePresent,
                                       bool customShadowedRectangleIsEnabled,
                                       int alignment,
                                       double minLengthPercent,
                                       double maxLengthPercent,
                                       bool backgroundIsGreaterThanItemThickness,
                                       double parabolicMaxZoom) const;

    //! main.qml viewType over the in-question result (a
    //! Latte::Types::ViewType int; anything else is refused loudly and
    //! resolves to the Dock default)
    Q_INVOKABLE int viewType(bool viewAndVisibilityArePresent,
                             int viewTypeInQuestion,
                             bool screenEdgeMarginEnabled,
                             bool floatingInternalGapIsForced) const;

    //! main.qml forceSolidPanel
    Q_INVOKABLE bool solidPanelForced(bool viewAndVisibilityArePresent,
                                      bool compositingActive,
                                      bool inConfigureAppletsMode,
                                      bool userShowPanelBackground,
                                      bool solidBackgroundForMaximizedIsEnabled,
                                      bool hasExpandedApplet,
                                      bool plasmaBackgroundForPopupsIsEnabled,
                                      bool existsWindowTouching,
                                      bool existsWindowTouchingEdge) const;

    //! main.qml forceTransparentPanel; windowColors is the raw config int,
    //! classified against Latte::Containment::Types::ActiveWindowColors
    Q_INVOKABLE bool transparentPanelForced(bool backgroundOnlyOnMaximized,
                                            bool viewAndVisibilityArePresent,
                                            bool compositingActive,
                                            bool inConfigureAppletsMode,
                                            bool solidPanelIsForced,
                                            bool existsWindowTouching,
                                            bool existsWindowTouchingEdge,
                                            int windowColors,
                                            bool selectedTrackerExistsWindowActive) const;

    //! main.qml forcePanelForBusyBackground (normalBusyForTouching-
    //! BusyVerticalView folded in); themeColors is the raw config int,
    //! classified against Latte::Containment::Types::SmartThemeColors
    Q_INVOKABLE bool panelForcedForBusyBackground(bool userShowPanelBackground,
                                                  bool viewIsTouchingBusyVerticalView,
                                                  bool backgroundOnlyOnMaximized,
                                                  bool transparentPanelIsForced,
                                                  bool colorizerBackgroundIsBusy,
                                                  int themeColors) const;

    //! main.qml blurEnabled
    Q_INVOKABLE bool blurActive(bool blurIsEnabledInConfig,
                                bool transparentPanelIsForced,
                                bool busyBackgroundPanelIsForced) const;

    //! main.qml panelShadowsActive
    Q_INVOKABLE bool panelShadowsActive(bool userShowPanelBackground,
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
                                        bool plasmaBackgroundForPopupsIsEnabled) const;

    //! MultiLayered.qml invUpdateEffectsArea's rect (the shell keeps the
    //! latteView.effects.rect write and the coalescing timer)
    Q_INVOKABLE QRectF effectsArea(bool compositingActive,
                                   bool viewIsHidden,
                                   bool behavesAsPlasmaPanel,
                                   double backgroundOriginXInRoot,
                                   double backgroundOriginYInRoot,
                                   double backgroundWidth,
                                   double backgroundHeight) const;

    //! MultiLayered.qml dock-mode primary-axis bounds. maxLength constrains the
    //! resting applet layout in QML; this helper keeps the transient background
    //! and its length-axis shadows inside the output-owned canvas. The centered
    //! offset stays inside that same canvas after parabolic movement is added.
    Q_INVOKABLE double dockBackgroundLength(double requestedBackgroundLength,
                                            double owningCanvasLength,
                                            double shadowMarginsLength) const;
    Q_INVOKABLE double centeredDockOffset(double requestedOffset,
                                          double visualLength,
                                          double viewPrimaryLength) const;

    //! MultiLayered.qml totals.visualThickness / visualMaxThickness. The
    //! QML boundary validates the live theme and configuration values before
    //! the pure core interpolates from the theme minimum to the item row.
    Q_INVOKABLE double visualThickness(double minimumThickness,
                                       double itemThickness,
                                       double sizeFraction) const;

    //! MultiLayered.qml paddings.top/bottom/left/right; the corner-margin
    //! factor arrives from third-party indicator packages and is refused
    //! loudly when malformed (negative or non-finite), falling back to
    //! the documented IndicatorInfo default of 1.0
    Q_INVOKABLE double edgePadding(bool borderIsPresent,
                                   bool paddingLiesOnLengthAxis,
                                   bool customRadiusIsEnabled,
                                   double customRadius,
                                   double themePadding,
                                   double solidBackgroundPadding,
                                   double appliedLengthMargin,
                                   double backgroundCornerMarginFactor) const;
};

}
}

#endif
