/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef BACKGROUNDSTATE_H
#define BACKGROUNDSTATE_H

// Qt
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QtGlobal>

// C++
#include <algorithm>
#include <cmath>

namespace Latte {

//! The Panel-vs-Dock decision chain and the background-state predicate
//! cluster (EX-13 in docs/tracking/QML_EXTRACTION_PLAN.md), extracted from the
//! containment's main.qml and background/MultiLayered.qml. This chain is
//! the root decision every geometry consumer branches on; the QML shells
//! keep their property bindings (whose split exists to break binding
//! loops - see the resolveViewType note) and ask here per predicate.
//!
//! Every body is lifted from its QML ancestor, token-identical between
//! HEAD and Qt5 f0ad7b23; the line references name the QML being replaced.
//! Null-view/null-tracker guards stay at the QML boundary as guarded
//! argument reads: QML evaluates call arguments eagerly, so the
//! short-circuit protection the old && chains provided must happen before
//! the call. "The view is not ready yet" is an explicit env state here,
//! never a guard hidden in the shell.
namespace BackgroundState {

//! how the view carries its background: a Dock window with a mask, or a
//! window that behaves like a plasma panel. Exactly these two states -
//! the QML boundary's int survives only in the wrapper, which refuses
//! out-of-domain values loudly.
enum class ViewType {
    Dock = 0,
    Panel
};

//! inputs of the Panel-vs-Dock question (main.qml:71-91)
struct ViewTypeInQuestionEnv {
    bool viewAndVisibilityArePresent = false;
    bool customShadowedRectangleIsEnabled = false;
    bool alignmentIsJustify = false;
    qreal minLengthPercent = 0.0;
    qreal maxLengthPercent = 0.0;
    bool backgroundIsGreaterThanItemThickness = false;
    qreal parabolicMaxZoom = 1.0;
};

//! The view type as chosen before considering the floating internal gap
//! enforcement (main.qml viewTypeInQuestion): a view behaves as a plasma
//! panel only when its layout cannot slide around (Justify alignment or a
//! static min==max length), its background is at least as thick as the
//! item row, and parabolic zoom is off. The custom shadowed rectangle
//! always needs dock mode (it draws its own chrome).
inline ViewType resolveViewTypeInQuestion(const ViewTypeInQuestionEnv &env)
{
    if (!env.viewAndVisibilityArePresent) {
        return ViewType::Dock;
    }

    if (env.customShadowedRectangleIsEnabled) {
        return ViewType::Dock;
    }

    //! exact equality on both comparisons, as the QML === did: the zoom
    //! config is exactly 1.0 when zoom is disabled, and the length
    //! percentages compare as the stored config values
    const bool staticLayout = (env.minLengthPercent == env.maxLengthPercent);

    if ((env.alignmentIsJustify || staticLayout)
            && env.backgroundIsGreaterThanItemThickness
            && (env.parabolicMaxZoom == 1.0)) {
        return ViewType::Panel;
    }

    return ViewType::Dock;
}

//! The effective view type (main.qml viewType): the in-question result,
//! overridden back to Dock while a floating view requests the client-side
//! internal floating gap. Takes the in-question RESULT as input - the QML
//! keeps two properties because the split breaks binding loops
//! (background geometry reads viewType while viewTypeInQuestion reads
//! background thickness), and this function mirrors that seam instead of
//! recomputing the question.
inline ViewType resolveViewType(bool viewAndVisibilityArePresent,
                                ViewType viewTypeInQuestion,
                                bool screenEdgeMarginEnabled,
                                bool floatingInternalGapIsForced)
{
    if (!viewAndVisibilityArePresent) {
        return ViewType::Dock;
    }

    if (screenEdgeMarginEnabled && floatingInternalGapIsForced) {
        //! dont use when floating views are requesting internal floating
        //! gap which is in client side
        return ViewType::Dock;
    }

    return viewTypeInQuestion;
}

//! inputs of the forced-solid predicate (main.qml:126-135)
struct SolidPanelEnv {
    bool viewAndVisibilityArePresent = false;
    bool compositingActive = false;
    bool inConfigureAppletsMode = false;
    bool userShowPanelBackground = false;
    bool solidBackgroundForMaximizedIsEnabled = false;
    bool hasExpandedApplet = false;
    bool plasmaBackgroundForPopupsIsEnabled = false;
    bool existsWindowTouching = false;
    bool existsWindowTouchingEdge = false;
};

//! main.qml forceSolidPanel: the background is forced fully opaque when a
//! window touches the view and the solid-for-maximized option is on (an
//! expanded applet vetoes that arm unless popups get the plasma
//! background, in which case the popup itself forces solidness), and
//! unconditionally without compositing.
inline bool isSolidPanelForced(const SolidPanelEnv &env)
{
    const bool windowIsTouching = env.existsWindowTouching || env.existsWindowTouchingEdge;

    return (env.viewAndVisibilityArePresent
            && env.compositingActive
            && !env.inConfigureAppletsMode
            && env.userShowPanelBackground
            && ((env.solidBackgroundForMaximizedIsEnabled
                 && !(env.hasExpandedApplet && !env.plasmaBackgroundForPopupsIsEnabled)
                 && windowIsTouching)
                || (env.hasExpandedApplet && env.plasmaBackgroundForPopupsIsEnabled)))
           || !env.compositingActive;
}

//! inputs of the forced-transparency predicate (main.qml:137-144)
struct TransparentPanelEnv {
    bool backgroundOnlyOnMaximized = false;
    bool viewAndVisibilityArePresent = false;
    bool compositingActive = false;
    bool inConfigureAppletsMode = false;
    bool solidPanelIsForced = false;
    bool existsWindowTouching = false;
    bool existsWindowTouchingEdge = false;
    bool windowColorsFollowActiveWindow = false;
    bool selectedTrackerExistsWindowActive = false;
};

//! main.qml forceTransparentPanel: with "background only on maximized"
//! the background goes transparent while nothing touches the view -
//! unless solidness is forced, or active-window colorizing has an active
//! window whose colors need the backdrop.
inline bool isTransparentPanelForced(const TransparentPanelEnv &env)
{
    return env.backgroundOnlyOnMaximized
           && env.viewAndVisibilityArePresent
           && env.compositingActive
           && !env.inConfigureAppletsMode
           && !env.solidPanelIsForced
           && !(env.existsWindowTouching || env.existsWindowTouchingEdge)
           && !(env.windowColorsFollowActiveWindow && env.selectedTrackerExistsWindowActive);
}

//! inputs of the busy-background predicate (main.qml:146-153)
struct BusyBackgroundEnv {
    bool userShowPanelBackground = false;
    bool viewIsTouchingBusyVerticalView = false;
    bool backgroundOnlyOnMaximized = false;
    bool transparentPanelIsForced = false;
    bool colorizerBackgroundIsBusy = false;
    bool themeColorsAreSmart = false;
};

//! main.qml forcePanelForBusyBackground: a transparent background is
//! re-forced visible when the underlying desktop is too busy for smart
//! theme colors, or when the view touches a vertical view in busy state
//! (main.qml's normalBusyForTouchingBusyVerticalView, folded in here -
//! this predicate was its only consumer tree-wide).
inline bool isPanelForcedForBusyBackground(const BusyBackgroundEnv &env)
{
    const bool busyVerticalViewIsTouching = env.viewIsTouchingBusyVerticalView
            && env.backgroundOnlyOnMaximized;

    return env.userShowPanelBackground
           && (busyVerticalViewIsTouching
               || (env.transparentPanelIsForced
                   && env.colorizerBackgroundIsBusy
                   && env.themeColorsAreSmart));
}

//! main.qml blurEnabled: behind-the-panel blur follows the config except
//! while the background is forced transparent - unless the busy state
//! re-forces a visible background, which brings the blur back with it.
inline bool isBlurActive(bool blurIsEnabledInConfig,
                         bool transparentPanelIsForced,
                         bool busyBackgroundPanelIsForced)
{
    return blurIsEnabledInConfig && (!transparentPanelIsForced || busyBackgroundPanelIsForced);
}

//! inputs of the panel-shadows predicate (main.qml:240-274)
struct PanelShadowsEnv {
    bool userShowPanelBackground = false;
    bool inConfigureAppletsMode = false;
    bool panelShadowsAreEnabledInConfig = false;
    //! main.qml disablePanelShadowMaximized (config option AND compositing),
    //! which stays a QML property - BindingsExternal also consumes it
    bool disablePanelShadowsForMaximizedIsActive = false;
    bool activeWindowIsMaximized = false;
    bool blurIsActive = false;
    //! unit-interval opacity (0..1); see the dead-clause note below
    qreal backgroundCurrentOpacity = 0.0;
    bool busyBackgroundPanelIsForced = false;
    bool backgroundOnlyOnMaximized = false;
    bool transparentPanelIsForced = false;
    bool hasExpandedApplet = false;
    bool plasmaBackgroundForPopupsIsEnabled = false;
};

//! main.qml panelShadowsActive, branch for branch. Notable pinned
//! behaviors (tests name them): configure mode follows the config
//! verbatim before anything else; a maximized active window with
//! disable-for-maximized suppresses everything below it INCLUDING the
//! popup-background branch, but only while the config bit is on (the
//! forcedNoShadows arm requires it); and the popup-background branch
//! ignores the config entirely.
inline bool arePanelShadowsActive(const PanelShadowsEnv &env)
{
    if (!env.userShowPanelBackground) {
        return false;
    }

    if (env.inConfigureAppletsMode) {
        return env.panelShadowsAreEnabledInConfig;
    }

    const bool forcedNoShadows = env.panelShadowsAreEnabledInConfig
            && env.disablePanelShadowsForMaximizedIsActive
            && env.activeWindowIsMaximized;

    if (forcedNoShadows) {
        return false;
    }

    //! THE INHERITED DEAD CLAUSE, kept bit-faithful (EX-13 log; the
    //! Qt5-fidelity agreement): currentOpacity is a unit-interval opacity
    //! in Qt5 f0ad7b23 and here alike, so the > 20 comparison can never
    //! fire and transparencyCheck degenerates to blurIsActive. The
    //! ancestor's own comment ("greater than 10%") disagrees with both
    //! its code and its units. Fixing it changes live shadow behavior for
    //! the busy-background state, so it is a deliberate decision for its
    //! own commit, not something to smuggle into an extraction;
    //! panelShadows_theDeadTransparencyClause pins the degeneracy.
    const bool transparencyCheck = env.blurIsActive
            || (!env.blurIsActive && env.backgroundCurrentOpacity > 20.0);

    //! Draw shadows for isBusy state only when current background opacity
    //! is greater than 10%
    if (env.panelShadowsAreEnabledInConfig && env.busyBackgroundPanelIsForced && transparencyCheck) {
        return true;
    }

    //! the ancestor re-tested !forcedNoShadows in this condition; provably
    //! redundant after the early return above, so it is dropped here
    if ((env.panelShadowsAreEnabledInConfig && !env.backgroundOnlyOnMaximized)
            || (env.panelShadowsAreEnabledInConfig && env.backgroundOnlyOnMaximized
                && !env.transparentPanelIsForced)) {
        return true;
    }

    if (env.hasExpandedApplet && env.plasmaBackgroundForPopupsIsEnabled) {
        return true;
    }

    return false;
}

//! inputs of the effects-area computation (MultiLayered.qml:463-498)
struct EffectsAreaEnv {
    bool compositingActive = false;
    bool viewIsHidden = false;
    bool behavesAsPlasmaPanel = false;
    //! the background's top-left mapped into the containment root item
    QPointF backgroundOriginInRoot;
    QSizeF backgroundSize;
};

//! the 1x1 offscreen rect View::effects accepts as a valid hide mask
inline constexpr QRectF hiddenEffectsMask{-1.0, -1.0, 1.0, 1.0};

//! MultiLayered.qml invUpdateEffectsArea, minus the latteView.effects.rect
//! assignment (the shell keeps the write and its 11ms coalescing timer).
//! Without compositing the rect maps the background verbatim even while
//! hidden - NOCOMPOSITING mode is a special case and the Effects Area is
//! also used for different calculations for View::mask().
inline QRectF resolveEffectsArea(const EffectsAreaEnv &env)
{
    if (!env.compositingActive) {
        return QRectF(env.backgroundOriginInRoot, env.backgroundSize);
    }

    if (env.viewIsHidden) {
        return hiddenEffectsMask;
    }

    if (env.behavesAsPlasmaPanel) {
        //! plasma-panel windows hug the background: effects anchor at the
        //! window origin instead of the mapped background position
        return QRectF(QPointF(0.0, 0.0), env.backgroundSize);
    }

    return QRectF(env.backgroundOriginInRoot, env.backgroundSize);
}

//! A dock's rounded solid background grows with the applet row. The configured
//! maximum belongs to the stable-layout solver, not this transient presentation
//! path. Keep the complete painted visual, including its length-axis shadow
//! margins, inside the output-owned canvas.
inline constexpr qreal fitDockBackgroundLength(qreal requestedBackgroundLength,
                                               qreal owningCanvasLength,
                                               qreal shadowMarginsLength)
{
    Q_ASSERT(requestedBackgroundLength >= 0.0);
    Q_ASSERT(owningCanvasLength >= 0.0);
    Q_ASSERT(shadowMarginsLength >= 0.0);

    const qreal maximumBackgroundLength = std::max(qreal{0},
                                                   owningCanvasLength - shadowMarginsLength);
    return std::min(requestedBackgroundLength, maximumBackgroundLength);
}

//! Keep a centered dock's configured and parabolic offset inside its actual
//! view. When the complete visual fills the view, no offset is possible;
//! shorter visuals retain the full symmetric movement available around them.
inline constexpr qreal fitCenteredDockOffset(qreal requestedOffset,
                                             qreal visualLength,
                                             qreal viewPrimaryLength)
{
    Q_ASSERT(visualLength >= 0.0);
    Q_ASSERT(viewPrimaryLength >= visualLength);

    const qreal maximumOffset = (viewPrimaryLength - visualLength) / 2.0;
    return std::clamp(requestedOffset, -maximumOffset, maximumOffset);
}

//! inputs of one edge's background padding (MultiLayered.qml:56-125; one
//! formula, four call sites differing only in which border/margins they
//! read and which axis the edge lies on)
struct EdgePaddingEnv {
    bool borderIsPresent = false;
    //! true when this edge sits at the ends of the view's length axis
    //! (top/bottom of a vertical view, left/right of a horizontal one) -
    //! those ends carry the roundness treatment
    bool paddingLiesOnLengthAxis = false;
    bool customRadiusIsEnabled = false;
    qreal customRadius = 0.0;
    qreal themePadding = 0.0;
    qreal solidBackgroundPadding = 0.0;
    //! metrics.margin.length, already applied around items
    qreal appliedLengthMargin = 0.0;
    //! indicators.info.backgroundCornerMargin - third-party indicator
    //! input; the wrapper refuses malformed values before they get here
    qreal backgroundCornerMarginFactor = 1.0;
};

//! MultiLayered.qml paddings.top/bottom/left/right: across the thickness
//! axis the padding is the larger of the theme's and the svg's; along the
//! length axis it is the background roundness beyond the already-applied
//! item margins, scaled by how much of the corner the indicator wants
//! kept clear.
inline qreal resolveEdgePadding(const EdgePaddingEnv &env)
{
    //! theme margins, radii and factors are non-negative by construction
    //! (config gates enablement on >= 0; the wrapper refuses the
    //! indicator factor) - a violation is a caller bug, not an input
    Q_ASSERT(env.backgroundCornerMarginFactor >= 0.0);
    Q_ASSERT(env.appliedLengthMargin >= 0.0);

    if (!env.borderIsPresent) {
        return 0.0;
    }

    const qreal themeExpected = std::max(env.themePadding, env.solidBackgroundPadding);

    if (!env.paddingLiesOnLengthAxis) {
        //! the custom radius only shapes the length-axis corners
        return themeExpected;
    }

    const qreal roundness = env.customRadiusIsEnabled ? env.customRadius : themeExpected;
    //! remove from roundness padding the applied margins
    const qreal roundnessBeyondMargins = std::max<qreal>(0.0, roundness - env.appliedLengthMargin);

    return roundnessBeyondMargins * env.backgroundCornerMarginFactor;
}

}
}

#endif
