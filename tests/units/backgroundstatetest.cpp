/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// EX-13 ViewTypeAndBackgroundPredicates (docs/tracking/QML_EXTRACTION_PLAN.md): the
// Panel-vs-Dock decision chain and the background-state predicate cluster
// (solid/transparent/busy/blur/shadows), plus the effects-area rect and the
// per-edge background padding math, as pure functions.
//
// Case derivation method (per docs/reference/TESTING.md): every expectation below was
// hand-derived by reading the QML bodies being replaced (containment
// main.qml:58-91/93/126-153/240-274, background/MultiLayered.qml:56-125/
// 463-498 at HEAD) side by side with their Qt5 ancestors at f0ad7b23 -
// the bodies are token-identical between the two, so one table pins both.
// Nothing here was generated from the header under test.

#include <QtTest>

// C++
#include <limits>
#include <type_traits>

#include "../../containment/plugin/units/backgroundstate.h"
#include "../../containment/plugin/backgroundstateresolver.h"

using namespace Latte::BackgroundState;

// The invalid states the type discipline eliminates (step-2.5 law), proven
// at compile time: inside the core a view type is one of exactly two named
// states - the QML boundary's int (where a garbage 7 was representable)
// survives only in the wrapper, which refuses out-of-domain ints loudly.
static_assert(!std::is_convertible_v<ViewType, int>,
              "enum class: a ViewType cannot silently decay to a raw int");
static_assert(!std::is_convertible_v<int, ViewType>,
              "enum class: a raw int cannot silently become a ViewType");

// the env structs ride through QTest data tables by value
Q_DECLARE_METATYPE(SolidPanelEnv)
Q_DECLARE_METATYPE(TransparentPanelEnv)
Q_DECLARE_METATYPE(BusyBackgroundEnv)
Q_DECLARE_METATYPE(PanelShadowsEnv)
Q_DECLARE_METATYPE(EffectsAreaEnv)
Q_DECLARE_METATYPE(EdgePaddingEnv)

class BackgroundStateTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // main.qml:71-91
    void viewTypeInQuestion_decisionTable_data();
    void viewTypeInQuestion_decisionTable();
    void viewTypeInQuestion_theThrowawayConfusionCase();

    // main.qml:58-69
    void viewType_floatingInternalGapForcesDock();
    void viewType_passesQuestionThroughOtherwise();

    // main.qml:126-135
    void solidPanel_decisionTable_data();
    void solidPanel_decisionTable();

    // main.qml:137-144
    void transparentPanel_decisionTable_data();
    void transparentPanel_decisionTable();

    // main.qml:146-153 (normalBusyForTouchingBusyVerticalView folded in)
    void busyBackground_decisionTable_data();
    void busyBackground_decisionTable();

    // main.qml:93
    void blur_truthTable();

    // main.qml:240-274
    void panelShadows_decisionTable_data();
    void panelShadows_decisionTable();
    void panelShadows_theDeadTransparencyClause();
    void panelShadows_popupBackgroundWinsOverDisabledConfig();
    void panelShadows_forcedNoShadowsBeatsPopupBackground();

    // MultiLayered.qml:463-498
    void effectsArea_decisionTable_data();
    void effectsArea_decisionTable();

    // MultiLayered.qml dock background length and centered placement
    void dockBackground_keepsCompleteVisualInsideSpan();
    void centeredDockOffset_staysInsideView();

    // MultiLayered.qml:56-125
    void edgePadding_decisionTable_data();
    void edgePadding_decisionTable();

    // the QML-facing wrapper's boundary refusals and enum mapping
    void wrapper_mapsCoreViewTypeOntoLatteTypesInts();
    void wrapper_refusesOutOfDomainViewTypeInt();
    void wrapper_classifiesAlignmentAndColorEnums();
    void wrapper_refusesMalformedIndicatorCornerMargin();
    void wrapper_effectsAreaMatchesCore();
};

// a panel-worthy environment: every clause of the Panel branch satisfied,
// individual rows flip one input at a time
static ViewTypeInQuestionEnv panelWorthyEnv()
{
    return ViewTypeInQuestionEnv{
        .viewAndVisibilityArePresent = true,
        .customShadowedRectangleIsEnabled = false,
        .alignmentIsJustify = true,
        .minLengthPercent = 0.0,
        .maxLengthPercent = 100.0,
        .backgroundIsGreaterThanItemThickness = true,
        .parabolicMaxZoom = 1.0,
    };
}

void BackgroundStateTest::viewTypeInQuestion_decisionTable_data()
{
    QTest::addColumn<bool>("present");
    QTest::addColumn<bool>("customShadowedRect");
    QTest::addColumn<bool>("justify");
    QTest::addColumn<double>("minLength");
    QTest::addColumn<double>("maxLength");
    QTest::addColumn<bool>("greaterThanItem");
    QTest::addColumn<double>("maxZoom");
    QTest::addColumn<bool>("expectPanel");

    //             present shadRect justify  min     max   greater  zoom   panel?
    QTest::newRow("startup without view is a Dock even when panel-worthy")
            << false << false << true << 0.0 << 100.0 << true << 1.0 << false;
    QTest::newRow("custom shadowed rectangle forces Dock over a panel-worthy rest")
            << true << true << true << 0.0 << 100.0 << true << 1.0 << false;
    QTest::newRow("Justify + thick background + no zoom is a Panel")
            << true << false << true << 0.0 << 100.0 << true << 1.0 << true;
    QTest::newRow("static layout (min==max) qualifies without Justify")
            << true << false << false << 50.0 << 50.0 << true << 1.0 << true;
    QTest::newRow("zoom enabled kills the Panel")
            << true << false << true << 0.0 << 100.0 << true << 1.4 << false;
    QTest::newRow("zoom barely above 1 kills the Panel (exact equality, as QML ===)")
            << true << false << true << 0.0 << 100.0 << true << 1.0000001 << false;
    QTest::newRow("background thinner than the item row is a Dock")
            << true << false << true << 0.0 << 100.0 << false << 1.0 << false;
    QTest::newRow("non-Justify non-static alignment is a Dock")
            << true << false << false << 20.0 << 100.0 << true << 1.0 << false;
}

void BackgroundStateTest::viewTypeInQuestion_decisionTable()
{
    QFETCH(bool, present);
    QFETCH(bool, customShadowedRect);
    QFETCH(bool, justify);
    QFETCH(double, minLength);
    QFETCH(double, maxLength);
    QFETCH(bool, greaterThanItem);
    QFETCH(double, maxZoom);
    QFETCH(bool, expectPanel);

    const ViewTypeInQuestionEnv env{
        .viewAndVisibilityArePresent = present,
        .customShadowedRectangleIsEnabled = customShadowedRect,
        .alignmentIsJustify = justify,
        .minLengthPercent = minLength,
        .maxLengthPercent = maxLength,
        .backgroundIsGreaterThanItemThickness = greaterThanItem,
        .parabolicMaxZoom = maxZoom,
    };

    QCOMPARE(resolveViewTypeInQuestion(env),
             expectPanel ? ViewType::Panel : ViewType::Dock);
}

void BackgroundStateTest::viewTypeInQuestion_theThrowawayConfusionCase()
{
    // The named case from the spec: the throwaway layout (alignment=10 ==
    // Justify, panelSize=100 making the background thicker than the item
    // row, zoom 1.0) renders a full-width Panel background. That rendering
    // was twice mistaken for a regression during live debugging
    // (session-handoff record); this row pins that it is CORRECT.
    ViewTypeInQuestionEnv throwaway = panelWorthyEnv();
    QCOMPARE(resolveViewTypeInQuestion(throwaway), ViewType::Panel);
}

void BackgroundStateTest::viewType_floatingInternalGapForcesDock()
{
    // floating views requesting the client-side internal gap must not
    // behave as plasma panels (main.qml:63-66) - both flags are needed
    QCOMPARE(resolveViewType(true, ViewType::Panel, true, true), ViewType::Dock);
    QCOMPARE(resolveViewType(true, ViewType::Panel, true, false), ViewType::Panel);
    QCOMPARE(resolveViewType(true, ViewType::Panel, false, true), ViewType::Panel);
}

void BackgroundStateTest::viewType_passesQuestionThroughOtherwise()
{
    QCOMPARE(resolveViewType(true, ViewType::Dock, false, false), ViewType::Dock);
    QCOMPARE(resolveViewType(true, ViewType::Panel, false, false), ViewType::Panel);
    // startup without a view resolves Dock regardless of the question
    QCOMPARE(resolveViewType(false, ViewType::Panel, false, false), ViewType::Dock);
}

// a maximized-solid environment: the touch arm satisfied, rows flip inputs
static SolidPanelEnv touchSolidEnv()
{
    return SolidPanelEnv{
        .viewAndVisibilityArePresent = true,
        .compositingActive = true,
        .inConfigureAppletsMode = false,
        .userShowPanelBackground = true,
        .solidBackgroundForMaximizedIsEnabled = true,
        .hasExpandedApplet = false,
        .plasmaBackgroundForPopupsIsEnabled = false,
        .existsWindowTouching = true,
        .existsWindowTouchingEdge = false,
    };
}

void BackgroundStateTest::solidPanel_decisionTable_data()
{
    QTest::addColumn<SolidPanelEnv>("env");
    QTest::addColumn<bool>("expected");

    QTest::newRow("no compositing forces solid unconditionally")
            << SolidPanelEnv{.compositingActive = false} << true;
    QTest::newRow("no compositing forces solid even without a view")
            << SolidPanelEnv{.viewAndVisibilityArePresent = false, .compositingActive = false} << true;
    QTest::newRow("touching window with solid-for-maximized forces solid")
            << touchSolidEnv() << true;
    {
        auto env = touchSolidEnv();
        env.existsWindowTouching = false;
        env.existsWindowTouchingEdge = true;
        QTest::newRow("edge-touching window qualifies exactly like touching") << env << true;
    }
    {
        auto env = touchSolidEnv();
        env.existsWindowTouching = false;
        QTest::newRow("no touching window, no solid") << env << false;
    }
    {
        auto env = touchSolidEnv();
        env.hasExpandedApplet = true;
        QTest::newRow("expanded applet without plasma popup background vetoes the touch arm")
                << env << false;
    }
    {
        auto env = touchSolidEnv();
        env.hasExpandedApplet = true;
        env.plasmaBackgroundForPopupsIsEnabled = true;
        env.existsWindowTouching = false;
        QTest::newRow("expanded applet with plasma popup background forces solid without touch")
                << env << true;
    }
    {
        auto env = touchSolidEnv();
        env.inConfigureAppletsMode = true;
        QTest::newRow("configure-applets mode suspends forced solid") << env << false;
    }
    {
        auto env = touchSolidEnv();
        env.userShowPanelBackground = false;
        QTest::newRow("hidden panel background never forces solid") << env << false;
    }
    {
        auto env = touchSolidEnv();
        env.viewAndVisibilityArePresent = false;
        QTest::newRow("no view yet, compositing on: not solid") << env << false;
    }
    {
        auto env = touchSolidEnv();
        env.solidBackgroundForMaximizedIsEnabled = false;
        QTest::newRow("solid-for-maximized disabled ignores touching windows") << env << false;
    }
}

void BackgroundStateTest::solidPanel_decisionTable()
{
    QFETCH(SolidPanelEnv, env);
    QFETCH(bool, expected);
    QCOMPARE(isSolidPanelForced(env), expected);
}

static TransparentPanelEnv transparencyEnv()
{
    return TransparentPanelEnv{
        .backgroundOnlyOnMaximized = true,
        .viewAndVisibilityArePresent = true,
        .compositingActive = true,
        .inConfigureAppletsMode = false,
        .solidPanelIsForced = false,
        .existsWindowTouching = false,
        .existsWindowTouchingEdge = false,
        .windowColorsFollowActiveWindow = false,
        .selectedTrackerExistsWindowActive = false,
    };
}

void BackgroundStateTest::transparentPanel_decisionTable_data()
{
    QTest::addColumn<TransparentPanelEnv>("env");
    QTest::addColumn<bool>("expected");

    QTest::newRow("background-only-on-maximized with nothing touching is transparent")
            << transparencyEnv() << true;
    {
        auto env = transparencyEnv();
        env.backgroundOnlyOnMaximized = false;
        QTest::newRow("feature disabled: never forced transparent") << env << false;
    }
    {
        auto env = transparencyEnv();
        env.solidPanelIsForced = true;
        QTest::newRow("forced solid wins over forced transparent") << env << false;
    }
    {
        auto env = transparencyEnv();
        env.existsWindowTouching = true;
        QTest::newRow("a touching window suspends transparency") << env << false;
    }
    {
        auto env = transparencyEnv();
        env.existsWindowTouchingEdge = true;
        QTest::newRow("an edge-touching window suspends transparency") << env << false;
    }
    {
        auto env = transparencyEnv();
        env.windowColorsFollowActiveWindow = true;
        env.selectedTrackerExistsWindowActive = true;
        QTest::newRow("active-window colors with an active window suspend transparency")
                << env << false;
    }
    {
        auto env = transparencyEnv();
        env.windowColorsFollowActiveWindow = true;
        QTest::newRow("active-window colors without an active window stay transparent")
                << env << true;
    }
    {
        auto env = transparencyEnv();
        env.inConfigureAppletsMode = true;
        QTest::newRow("configure-applets mode suspends transparency") << env << false;
    }
    {
        auto env = transparencyEnv();
        env.viewAndVisibilityArePresent = false;
        QTest::newRow("no view yet: not transparent") << env << false;
    }
    {
        auto env = transparencyEnv();
        env.compositingActive = false;
        QTest::newRow("no compositing: not transparent") << env << false;
    }
}

void BackgroundStateTest::transparentPanel_decisionTable()
{
    QFETCH(TransparentPanelEnv, env);
    QFETCH(bool, expected);
    QCOMPARE(isTransparentPanelForced(env), expected);
}

void BackgroundStateTest::busyBackground_decisionTable_data()
{
    QTest::addColumn<BusyBackgroundEnv>("env");
    QTest::addColumn<bool>("expected");

    QTest::newRow("touching busy vertical view with background-only-on-maximized")
            << BusyBackgroundEnv{.userShowPanelBackground = true,
                                 .viewIsTouchingBusyVerticalView = true,
                                 .backgroundOnlyOnMaximized = true} << true;
    QTest::newRow("touching busy vertical view without background-only-on-maximized")
            << BusyBackgroundEnv{.userShowPanelBackground = true,
                                 .viewIsTouchingBusyVerticalView = true,
                                 .backgroundOnlyOnMaximized = false} << false;
    QTest::newRow("transparent + busy colorizer + smart theme colors")
            << BusyBackgroundEnv{.userShowPanelBackground = true,
                                 .transparentPanelIsForced = true,
                                 .colorizerBackgroundIsBusy = true,
                                 .themeColorsAreSmart = true} << true;
    QTest::newRow("busy colorizer without smart theme colors")
            << BusyBackgroundEnv{.userShowPanelBackground = true,
                                 .transparentPanelIsForced = true,
                                 .colorizerBackgroundIsBusy = true,
                                 .themeColorsAreSmart = false} << false;
    QTest::newRow("busy colorizer without forced transparency")
            << BusyBackgroundEnv{.userShowPanelBackground = true,
                                 .transparentPanelIsForced = false,
                                 .colorizerBackgroundIsBusy = true,
                                 .themeColorsAreSmart = true} << false;
    QTest::newRow("hidden panel background gates both arms")
            << BusyBackgroundEnv{.userShowPanelBackground = false,
                                 .viewIsTouchingBusyVerticalView = true,
                                 .backgroundOnlyOnMaximized = true,
                                 .transparentPanelIsForced = true,
                                 .colorizerBackgroundIsBusy = true,
                                 .themeColorsAreSmart = true} << false;
}

void BackgroundStateTest::busyBackground_decisionTable()
{
    QFETCH(BusyBackgroundEnv, env);
    QFETCH(bool, expected);
    QCOMPARE(isPanelForcedForBusyBackground(env), expected);
}

void BackgroundStateTest::blur_truthTable()
{
    // blurEnabled = config && (!transparent || busy)  (main.qml:93)
    QCOMPARE(isBlurActive(true, false, false), true);
    QCOMPARE(isBlurActive(true, true, false), false); // transparent panels drop blur
    QCOMPARE(isBlurActive(true, true, true), true);   // unless the busy state re-forces it
    QCOMPARE(isBlurActive(true, false, true), true);
    QCOMPARE(isBlurActive(false, false, false), false);
    QCOMPARE(isBlurActive(false, true, true), false);
}

static PanelShadowsEnv regularShadowsEnv()
{
    return PanelShadowsEnv{
        .userShowPanelBackground = true,
        .inConfigureAppletsMode = false,
        .panelShadowsAreEnabledInConfig = true,
        .disablePanelShadowsForMaximizedIsActive = false,
        .activeWindowIsMaximized = false,
        .blurIsActive = false,
        .backgroundCurrentOpacity = 0.7,
        .busyBackgroundPanelIsForced = false,
        .backgroundOnlyOnMaximized = false,
        .transparentPanelIsForced = false,
        .hasExpandedApplet = false,
        .plasmaBackgroundForPopupsIsEnabled = false,
    };
}

void BackgroundStateTest::panelShadows_decisionTable_data()
{
    QTest::addColumn<PanelShadowsEnv>("env");
    QTest::addColumn<bool>("expected");

    QTest::newRow("plain enabled shadows draw")
            << regularShadowsEnv() << true;
    {
        auto env = regularShadowsEnv();
        env.userShowPanelBackground = false;
        QTest::newRow("hidden panel background disables shadows before anything else")
                << env << false;
    }
    {
        auto env = regularShadowsEnv();
        env.inConfigureAppletsMode = true;
        QTest::newRow("configure-applets mode follows the config verbatim: enabled")
                << env << true;
    }
    {
        auto env = regularShadowsEnv();
        env.inConfigureAppletsMode = true;
        env.panelShadowsAreEnabledInConfig = false;
        // even states that would force shadows below (popup background) are
        // not consulted in configure mode
        env.hasExpandedApplet = true;
        env.plasmaBackgroundForPopupsIsEnabled = true;
        QTest::newRow("configure-applets mode follows the config verbatim: disabled")
                << env << false;
    }
    {
        auto env = regularShadowsEnv();
        env.disablePanelShadowsForMaximizedIsActive = true;
        env.activeWindowIsMaximized = true;
        QTest::newRow("maximized active window with disable-for-maximized kills shadows")
                << env << false;
    }
    {
        auto env = regularShadowsEnv();
        env.disablePanelShadowsForMaximizedIsActive = true;
        QTest::newRow("disable-for-maximized without a maximized window draws normally")
                << env << true;
    }
    {
        auto env = regularShadowsEnv();
        env.busyBackgroundPanelIsForced = true;
        env.blurIsActive = true;
        env.backgroundOnlyOnMaximized = true;
        env.transparentPanelIsForced = true;
        QTest::newRow("busy background with blur draws shadows even while transparent")
                << env << true;
    }
    {
        auto env = regularShadowsEnv();
        env.panelShadowsAreEnabledInConfig = false;
        QTest::newRow("shadows disabled in config: none") << env << false;
    }
    {
        auto env = regularShadowsEnv();
        env.backgroundOnlyOnMaximized = true;
        QTest::newRow("background-only-on-maximized, currently opaque: shadows draw")
                << env << true;
    }
    {
        auto env = regularShadowsEnv();
        env.backgroundOnlyOnMaximized = true;
        env.transparentPanelIsForced = true;
        QTest::newRow("background-only-on-maximized, currently transparent: no shadows")
                << env << false;
    }
    {
        auto env = regularShadowsEnv();
        env.panelShadowsAreEnabledInConfig = false;
        env.hasExpandedApplet = true;
        env.plasmaBackgroundForPopupsIsEnabled = true;
        QTest::newRow("expanded applet with plasma popup background draws shadows")
                << env << true;
    }
}

void BackgroundStateTest::panelShadows_decisionTable()
{
    QFETCH(PanelShadowsEnv, env);
    QFETCH(bool, expected);
    QCOMPARE(arePanelShadowsActive(env), expected);
}

void BackgroundStateTest::panelShadows_theDeadTransparencyClause()
{
    // THE INHERITED DEAD CLAUSE, pinned deliberately (agent log EX-13):
    // the ancestor's transparencyCheck compares background.currentOpacity
    // against 20, but currentOpacity is a unit-interval opacity (0..1;
    // backgroundStoredOpacity = panelTransparency/100) in Qt5 f0ad7b23 and
    // here alike, so the clause can never fire and transparencyCheck
    // degenerates to blurEnabled. Kept bit-faithful per the Qt5-fidelity
    // agreement. This case documents the degeneracy: a busy background at
    // 90% opacity WITHOUT blur draws no shadows through the busy arm (and
    // with backgroundOnlyOnMaximized + transparency active, no arm at all).
    // If the clause is ever deliberately fixed to unit-interval units,
    // this row must flip in that commit - never silently.
    PanelShadowsEnv env = regularShadowsEnv();
    env.busyBackgroundPanelIsForced = true;
    env.blurIsActive = false;
    env.backgroundCurrentOpacity = 0.9;
    env.backgroundOnlyOnMaximized = true;
    env.transparentPanelIsForced = true;

    QCOMPARE(arePanelShadowsActive(env), false);
}

void BackgroundStateTest::panelShadows_popupBackgroundWinsOverDisabledConfig()
{
    // the final branch ignores the panelShadows config entirely: an
    // expanded applet with plasma popup backgrounds draws shadows even
    // when the config says never (Qt5 quirk, pinned)
    PanelShadowsEnv env = regularShadowsEnv();
    env.panelShadowsAreEnabledInConfig = false;
    env.hasExpandedApplet = true;
    env.plasmaBackgroundForPopupsIsEnabled = true;

    QCOMPARE(arePanelShadowsActive(env), true);
}

void BackgroundStateTest::panelShadows_forcedNoShadowsBeatsPopupBackground()
{
    // asymmetry pinned: forcedNoShadows returns early, so with the config
    // ENABLED a maximized window suppresses even the popup-background
    // shadows - but with the config DISABLED forcedNoShadows cannot arm
    // (it requires the config bit) and the popup branch draws
    PanelShadowsEnv env = regularShadowsEnv();
    env.disablePanelShadowsForMaximizedIsActive = true;
    env.activeWindowIsMaximized = true;
    env.hasExpandedApplet = true;
    env.plasmaBackgroundForPopupsIsEnabled = true;

    env.panelShadowsAreEnabledInConfig = true;
    QCOMPARE(arePanelShadowsActive(env), false);

    env.panelShadowsAreEnabledInConfig = false;
    QCOMPARE(arePanelShadowsActive(env), true);
}

void BackgroundStateTest::effectsArea_decisionTable_data()
{
    QTest::addColumn<EffectsAreaEnv>("env");
    QTest::addColumn<QRectF>("expected");

    const QPointF origin(12.5, 3.0);
    const QSizeF size(800.0, 48.0);

    QTest::newRow("no compositing maps the background rect verbatim")
            << EffectsAreaEnv{.compositingActive = false,
                              .viewIsHidden = false,
                              .behavesAsPlasmaPanel = false,
                              .backgroundOriginInRoot = origin,
                              .backgroundSize = size}
            << QRectF(origin, size);
    QTest::newRow("no compositing ignores hidden: the rect doubles as View::mask input")
            << EffectsAreaEnv{.compositingActive = false,
                              .viewIsHidden = true,
                              .behavesAsPlasmaPanel = true,
                              .backgroundOriginInRoot = origin,
                              .backgroundSize = size}
            << QRectF(origin, size);
    QTest::newRow("hidden view reports the 1x1 offscreen hide mask")
            << EffectsAreaEnv{.compositingActive = true,
                              .viewIsHidden = true,
                              .behavesAsPlasmaPanel = false,
                              .backgroundOriginInRoot = origin,
                              .backgroundSize = size}
            << QRectF(-1.0, -1.0, 1.0, 1.0);
    QTest::newRow("plasma-panel mode anchors the effects rect at the window origin")
            << EffectsAreaEnv{.compositingActive = true,
                              .viewIsHidden = false,
                              .behavesAsPlasmaPanel = true,
                              .backgroundOriginInRoot = origin,
                              .backgroundSize = size}
            << QRectF(QPointF(0.0, 0.0), size);
    QTest::newRow("dock mode maps the background position into the view")
            << EffectsAreaEnv{.compositingActive = true,
                              .viewIsHidden = false,
                              .behavesAsPlasmaPanel = false,
                              .backgroundOriginInRoot = origin,
                              .backgroundSize = size}
            << QRectF(origin, size);
    QTest::newRow("degenerate zero size passes through unclamped (faithful)")
            << EffectsAreaEnv{.compositingActive = true,
                              .viewIsHidden = false,
                              .behavesAsPlasmaPanel = false,
                              .backgroundOriginInRoot = origin,
                              .backgroundSize = QSizeF(0.0, 0.0)}
            << QRectF(origin, QSizeF(0.0, 0.0));
}

void BackgroundStateTest::effectsArea_decisionTable()
{
    QFETCH(EffectsAreaEnv, env);
    QFETCH(QRectF, expected);
    QCOMPARE(resolveEffectsArea(env), expected);
}

void BackgroundStateTest::dockBackground_keepsCompleteVisualInsideSpan()
{
    //! Live D140 (zoomed side-dock chrome clipped at both ends) shape: the
    //! resting solid background requests 1230 px in a 1240 px primary span
    //! with 40 px of length-axis shadow. Hover requests 1307 px after the row
    //! grows, but the complete visual stays in that span.
    static_assert(fitDockBackgroundLength(1230.0, 1240.0, 40.0) == 1200.0);
    static_assert(fitDockBackgroundLength(1307.0, 1240.0, 40.0) == 1200.0);

    QCOMPARE(fitDockBackgroundLength(900.0, 1240.0, 40.0), 900.0);
    QCOMPARE(fitDockBackgroundLength(1307.0, 1240.0, 40.0), 1200.0);

    Latte::Containment::BackgroundStateResolver resolver;
    QCOMPARE(resolver.dockBackgroundLength(1307.0, 1240.0, 40.0), 1200.0);
}

void BackgroundStateTest::centeredDockOffset_staysInsideView()
{
    static_assert(fitCenteredDockOffset(-34.0, 1240.0, 1240.0) == 0.0);
    static_assert(fitCenteredDockOffset(80.0, 1000.0, 1240.0) == 80.0);
    static_assert(fitCenteredDockOffset(160.0, 1000.0, 1240.0) == 120.0);

    QCOMPARE(fitCenteredDockOffset(-160.0, 1000.0, 1240.0), -120.0);

    Latte::Containment::BackgroundStateResolver resolver;
    QCOMPARE(resolver.centeredDockOffset(-34.0, 1240.0, 1240.0), 0.0);
}

void BackgroundStateTest::edgePadding_decisionTable_data()
{
    QTest::addColumn<EdgePaddingEnv>("env");
    QTest::addColumn<double>("expected");

    QTest::newRow("no border: no padding, whatever the theme says")
            << EdgePaddingEnv{.borderIsPresent = false,
                              .paddingLiesOnLengthAxis = true,
                              .customRadiusIsEnabled = true,
                              .customRadius = 12.0,
                              .themePadding = 8.0,
                              .solidBackgroundPadding = 6.0,
                              .appliedLengthMargin = 2.0,
                              .backgroundCornerMarginFactor = 1.0}
            << 0.0;
    QTest::newRow("thickness axis takes the larger of theme and svg paddings")
            << EdgePaddingEnv{.borderIsPresent = true,
                              .paddingLiesOnLengthAxis = false,
                              .customRadiusIsEnabled = false,
                              .customRadius = 0.0,
                              .themePadding = 8.0,
                              .solidBackgroundPadding = 6.0,
                              .appliedLengthMargin = 2.0,
                              .backgroundCornerMarginFactor = 0.5}
            << 8.0;
    QTest::newRow("thickness axis ignores the custom radius entirely")
            << EdgePaddingEnv{.borderIsPresent = true,
                              .paddingLiesOnLengthAxis = false,
                              .customRadiusIsEnabled = true,
                              .customRadius = 40.0,
                              .themePadding = 3.0,
                              .solidBackgroundPadding = 6.0,
                              .appliedLengthMargin = 2.0,
                              .backgroundCornerMarginFactor = 0.5}
            << 6.0;
    QTest::newRow("length axis: roundness beyond the applied margins, scaled by the indicator factor")
            << EdgePaddingEnv{.borderIsPresent = true,
                              .paddingLiesOnLengthAxis = true,
                              .customRadiusIsEnabled = false,
                              .customRadius = 0.0,
                              .themePadding = 10.0,
                              .solidBackgroundPadding = 4.0,
                              .appliedLengthMargin = 4.0,
                              .backgroundCornerMarginFactor = 0.5}
            << 3.0; // (max(10,4) - 4) * 0.5
    QTest::newRow("length axis with custom radius uses the radius, not the theme")
            << EdgePaddingEnv{.borderIsPresent = true,
                              .paddingLiesOnLengthAxis = true,
                              .customRadiusIsEnabled = true,
                              .customRadius = 12.0,
                              .themePadding = 30.0,
                              .solidBackgroundPadding = 30.0,
                              .appliedLengthMargin = 2.0,
                              .backgroundCornerMarginFactor = 1.0}
            << 10.0; // (12 - 2) * 1.0
    QTest::newRow("margins larger than the roundness clamp to zero, never negative")
            << EdgePaddingEnv{.borderIsPresent = true,
                              .paddingLiesOnLengthAxis = true,
                              .customRadiusIsEnabled = false,
                              .customRadius = 0.0,
                              .themePadding = 4.0,
                              .solidBackgroundPadding = 2.0,
                              .appliedLengthMargin = 9.0,
                              .backgroundCornerMarginFactor = 1.0}
            << 0.0;
    QTest::newRow("an indicator opting out (factor 0) removes the roundness padding")
            << EdgePaddingEnv{.borderIsPresent = true,
                              .paddingLiesOnLengthAxis = true,
                              .customRadiusIsEnabled = true,
                              .customRadius = 12.0,
                              .themePadding = 0.0,
                              .solidBackgroundPadding = 0.0,
                              .appliedLengthMargin = 0.0,
                              .backgroundCornerMarginFactor = 0.0}
            << 0.0;
}

void BackgroundStateTest::edgePadding_decisionTable()
{
    QFETCH(EdgePaddingEnv, env);
    QFETCH(double, expected);
    QCOMPARE(resolveEdgePadding(env), expected);
}

void BackgroundStateTest::wrapper_mapsCoreViewTypeOntoLatteTypesInts()
{
    Latte::Containment::BackgroundStateResolver resolver;

    // panel-worthy inputs, alignment 10 == Latte::Types::Justify
    QCOMPARE(resolver.viewTypeInQuestion(true, false, 10, 0.0, 100.0, true, 1.0),
             1 /*Latte::Types::PanelView*/);
    // alignment 0 == Center, non-static lengths
    QCOMPARE(resolver.viewTypeInQuestion(true, false, 0, 0.0, 100.0, true, 1.0),
             0 /*Latte::Types::DockView*/);

    QCOMPARE(resolver.viewType(true, 1, false, false), 1);
    QCOMPARE(resolver.viewType(true, 1, true, true), 0);
}

void BackgroundStateTest::wrapper_refusesOutOfDomainViewTypeInt()
{
    Latte::Containment::BackgroundStateResolver resolver;

    // an int outside {DockView=0, PanelView=1} arriving from QML is a shell
    // bug: refused loudly, resolved to the Dock default
    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral("out of domain")));
    QCOMPARE(resolver.viewType(true, 7, false, false), 0);
}

void BackgroundStateTest::wrapper_classifiesAlignmentAndColorEnums()
{
    Latte::Containment::BackgroundStateResolver resolver;

    // windowColors 1 == ActiveWindowColors suspends transparency with an
    // active window; 2 == TouchingWindowColors does not
    QCOMPARE(resolver.transparentPanelForced(true, true, true, false, false,
                                             false, false, 1, true), false);
    QCOMPARE(resolver.transparentPanelForced(true, true, true, false, false,
                                             false, false, 2, true), true);

    // themeColors 2 == SmartThemeColors arms the busy arm; 0 == Plasma does not
    QCOMPARE(resolver.panelForcedForBusyBackground(true, false, false, true, true, 2), true);
    QCOMPARE(resolver.panelForcedForBusyBackground(true, false, false, true, true, 0), false);
}

void BackgroundStateTest::wrapper_refusesMalformedIndicatorCornerMargin()
{
    Latte::Containment::BackgroundStateResolver resolver;

    // backgroundCornerMargin arrives from third-party indicator packages -
    // genuinely external input. Malformed values (negative, NaN) are
    // refused loudly and the documented IndicatorInfo default 1.0 applies.
    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral("backgroundCornerMargin")));
    QCOMPARE(resolver.edgePadding(true, true, true, 12.0, 0.0, 0.0, 2.0, -3.0), 10.0);

    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral("backgroundCornerMargin")));
    QCOMPARE(resolver.edgePadding(true, true, true, 12.0, 0.0, 0.0, 2.0,
                                  std::numeric_limits<double>::quiet_NaN()), 10.0);

    // well-formed factors pass through untouched, including > 1
    QCOMPARE(resolver.edgePadding(true, true, true, 12.0, 0.0, 0.0, 2.0, 0.5), 5.0);
    QCOMPARE(resolver.edgePadding(true, true, true, 12.0, 0.0, 0.0, 2.0, 2.0), 20.0);
}

void BackgroundStateTest::wrapper_effectsAreaMatchesCore()
{
    Latte::Containment::BackgroundStateResolver resolver;

    QCOMPARE(resolver.effectsArea(true, true, false, 5.0, 6.0, 100.0, 40.0),
             QRectF(-1.0, -1.0, 1.0, 1.0));
    QCOMPARE(resolver.effectsArea(true, false, true, 5.0, 6.0, 100.0, 40.0),
             QRectF(0.0, 0.0, 100.0, 40.0));
    QCOMPARE(resolver.effectsArea(true, false, false, 5.0, 6.0, 100.0, 40.0),
             QRectF(5.0, 6.0, 100.0, 40.0));
    QCOMPARE(resolver.effectsArea(false, true, true, 5.0, 6.0, 100.0, 40.0),
             QRectF(5.0, 6.0, 100.0, 40.0));
}

QTEST_GUILESS_MAIN(BackgroundStateTest)
#include "backgroundstatetest.moc"
