/*
    SPDX-FileCopyrightText: 2018 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef COLORIZERDECISION_H
#define COLORIZERDECISION_H

// local
#include "../types.h"

// Qt
#include <QtGlobal>

namespace Latte {
namespace Containment {

//! The colorizer's theme/scheme selection decision tree (EX-12 in
//! docs/tracking/QML_EXTRACTION_PLAN.md), extracted from the containment's
//! colorizer/Manager.qml applyTheme, mustBeShown, scheme and textColor
//! bodies. The Qt5 body (f0ad7b23) is the spec: every branch and its
//! ordering are carried verbatim, including one deliberately preserved
//! oddity documented at its site. Rendering (ColorOverlay, layer
//! effects) stays in QML; this core only DECIDES.
//!
//! The tree in QML picked between live QObjects (SchemeColors instances)
//! and compared them by identity. Here the choice is an enum naming the
//! source; the thin ColorizerDecider wrapper maps enum -> object. The
//! identity comparisons become the explicit algebra in
//! appliedSchemeIsPlasmaTheme(), proven against the object providers:
//!
//!   - Theme::darkTheme() returns the SAME instance as defaultTheme()
//!     exactly when the plasma theme is dark, lightTheme() exactly when
//!     it is light (app/plasma/extended/theme.cpp: default vs reversed
//!     scheme selection on m_isLightTheme).
//!   - Window-tracker schemes and layout schemes come from the wm
//!     Schemes instance map (app/wm/tracker/schemes.cpp
//!     schemeForFile/schemeForWindow) - never the Theme's own
//!     defaultTheme instance, so they never alias it.
namespace ColorizerDecision {

//! which live object the colorizer applies as its palette
enum class ThemeSource {
    PlasmaDefault,                   //!< themeExtended.defaultTheme (Manager.qml's plasmaTheme)
    SelectedActiveWindowScheme,      //!< selectedWindowsTracker.activeWindowScheme
    CurrentScreenActiveWindowScheme, //!< windowsTracker.currentScreen.activeWindowScheme
    TouchingWindowScheme,            //!< windowsTracker.currentScreen.touchingWindowScheme
    DarkTheme,                       //!< themeExtended.darkTheme
    LightTheme,                      //!< themeExtended.lightTheme
    LayoutScheme,                    //!< latteView.layout.scheme
};

//! whose schemeFile the colorizer publishes to latte-aware applets.
//! KdeGlobalsFallback is a named state (the literal "kdeglobals" when no
//! themeExtended exists yet), not an empty-string default.
enum class SchemeFile {
    AppliedScheme,   //!< the chosen ThemeSource object's schemeFile
    LightThemeFile,
    DarkThemeFile,
    DefaultThemeFile,
    KdeGlobalsFallback,
};

//! one snapshot of everything the tree reads, as plain values. The
//! wrapper assembles it from its QML-bound inputs; *Exists fields are
//! resolved-pointer facts, never guesses.
struct ColorizerEnv {
    //! settings (the plugin's real Q_ENUM types - no mirror enums)
    Types::ThemeColorsGroup themeColors{Types::PlasmaThemeColors};
    Types::WindowColorsGroup windowColors{Types::NoneWindowColors};

    //! environment
    bool graphicsSystemAccelerated{true};
    bool compositingActive{true};

    //! plasma theme facts
    bool themeExtendedExists{false};
    bool plasmaThemeIsLight{false};

    //! view and window-tracker facts. The currentScreen fields are only
    //! read behind windowsTrackerEnabled, mirroring the QML guard.
    bool windowsTrackerEnabled{false};
    bool selectedActiveWindowSchemeExists{false};
    bool currentScreenActiveWindowSchemeExists{false};
    bool touchingWindowSchemeExists{false};
    bool existsWindowTouching{false};
    bool existsWindowTouchingEdge{false};
    bool activeWindowTouching{false};
    bool activeWindowTouchingEdge{false};
    bool layoutExists{false};

    //! panel and edit-mode state
    bool plasmaBackgroundForPopups{false};
    bool hasExpandedApplet{false};
    bool userShowPanelBackground{false};
    bool plasmaStyleBusyForTouchingBusyVerticalView{false};
    bool forceSolidPanel{false};
    bool forcePanelForBusyBackground{false};
    bool inConfigureAppletsMode{false};
    bool editModeTextColorIsBright{false};

    //! measured background; -1000 is the tracker-not-loaded value the
    //! QML always carried (reads as "dark" through the 127.5 threshold)
    double currentBackgroundBrightness{-1000.0};
    //! the view's stored background opacity, 0..1
    double backgroundStoredOpacity{1.0};
};

//! Chooses which scheme object the colorizer applies - Manager.qml's
//! applyTheme binding, branch for branch.
inline ThemeSource chooseThemeSource(const ColorizerEnv &env)
{
    if (!env.graphicsSystemAccelerated) {
        return ThemeSource::PlasmaDefault;
    }

    if (env.windowsTrackerEnabled && !(env.plasmaBackgroundForPopups && env.hasExpandedApplet)) {
        if (env.windowColors == Types::ActiveWindowColors && env.selectedActiveWindowSchemeExists) {
            return ThemeSource::SelectedActiveWindowScheme;
        }

        if (env.windowColors == Types::TouchingWindowColors && env.touchingWindowSchemeExists) {
            //! we must track touching windows and when they are not active
            //! the active window scheme is used for convenience
            //!
            //! PRESERVED ODDITY (Qt5 verbatim): the JS condition was
            //!   existsWindowTouching || existsWindowTouchingEdge && !(...) && ...
            //! where && binds tighter than ||, so existsWindowTouching ALONE
            //! selects the active-window scheme regardless of the
            //! not-active clauses the comment above describes. Qt5 shipped
            //! this precedence and it has never been reported as a defect;
            //! it is pinned bit-for-bit by the touchingBranchPrecedence
            //! test case. Regrouping it is a behavior change, not a
            //! cleanup - decide it deliberately, with live evidence.
            if (env.existsWindowTouching
                    || (env.existsWindowTouchingEdge
                        && !(env.activeWindowTouching || env.activeWindowTouchingEdge)
                        && env.currentScreenActiveWindowSchemeExists)) {
                return ThemeSource::CurrentScreenActiveWindowScheme;
            }

            return ThemeSource::TouchingWindowScheme;
        }
    }

    if (env.themeExtendedExists) {
        if ((env.userShowPanelBackground && env.plasmaBackgroundForPopups && env.hasExpandedApplet) /*for expanded popups when it is enabled*/
                || env.plasmaStyleBusyForTouchingBusyVerticalView
                || (env.themeColors == Types::SmartThemeColors /*for Smart theming that Windows colors are not used and the user wants solidness at some cases*/
                    && env.windowColors == Types::NoneWindowColors
                    && env.forceSolidPanel)) {
            /* plasma style*/
            return ThemeSource::PlasmaDefault;
        }

        if (env.themeColors == Types::DarkThemeColors) {
            return ThemeSource::DarkTheme;
        } else if (env.themeColors == Types::LightThemeColors) {
            return ThemeSource::LightTheme;
        } else if (env.themeColors == Types::ReverseThemeColors) {
            return env.plasmaThemeIsLight ? ThemeSource::DarkTheme : ThemeSource::LightTheme;
        } else if (env.themeColors == Types::LayoutThemeColors && env.layoutExists) {
            return ThemeSource::LayoutScheme;
        }

        if (env.themeColors == Types::SmartThemeColors) {
            //! Smart Colors Case
            const bool backgroundIsBright = env.currentBackgroundBrightness > 127.5;

            if (!env.forcePanelForBusyBackground) {
                //! simple case that not a busy background is applied
                return backgroundIsBright ? ThemeSource::LightTheme : ThemeSource::DarkTheme;
            } else {
                //! Smart + Busy background case. Qt5 also computed a
                //! themeContrastedBackground candidate here; it was dead
                //! (assigned, never read) in Qt5 and the port alike, so it
                //! is not carried over.
                if (env.backgroundStoredOpacity < 0.35) {
                    //! textColor should be better to provide the needed contrast
                    return backgroundIsBright ? ThemeSource::LightTheme : ThemeSource::DarkTheme;
                } else if (env.backgroundStoredOpacity >= 0.35 && env.backgroundStoredOpacity <= 0.70) {
                    //! provide a dark case scenario at all cases
                    return ThemeSource::DarkTheme;
                } else {
                    //! default plasma theme should be better for panel transparency > 70
                    return ThemeSource::PlasmaDefault;
                }
            }
        }
    }

    return ThemeSource::PlasmaDefault;
}

//! Whether the chosen source resolves to the very SchemeColors instance
//! Manager.qml calls plasmaTheme - the algebra behind every
//! `applyTheme === plasmaTheme` identity comparison in the QML (provider
//! proof in the namespace comment above).
inline bool appliedSchemeIsPlasmaTheme(ThemeSource source, const ColorizerEnv &env)
{
    switch (source) {
    case ThemeSource::PlasmaDefault:
        return true;
    case ThemeSource::DarkTheme:
        //! Theme::darkTheme() IS the defaultTheme instance when the theme is dark
        return !env.plasmaThemeIsLight;
    case ThemeSource::LightTheme:
        //! Theme::lightTheme() IS the defaultTheme instance when the theme is light
        return env.plasmaThemeIsLight;
    case ThemeSource::SelectedActiveWindowScheme:
    case ThemeSource::CurrentScreenActiveWindowScheme:
    case ThemeSource::TouchingWindowScheme:
    case ThemeSource::LayoutScheme:
        //! wm-tracker and layout schemes never alias the Theme's default
        //! instance (separate provider maps)
        return false;
    }

    Q_UNREACHABLE_RETURN(false);
}

//! Whether the colorizer must repaint at all - Manager.qml's mustBeShown.
//! appliedThemeExists is the wrapper's resolved-pointer fact: a chosen
//! source whose live object is null must not claim to be shown.
inline bool mustBeShown(const ColorizerEnv &env, ThemeSource source, bool appliedThemeExists)
{
    return (appliedThemeExists && !appliedSchemeIsPlasmaTheme(source, env))
            || (env.inConfigureAppletsMode && env.themeColors == Types::SmartThemeColors);
}

//! Chooses whose schemeFile the colorizer publishes - Manager.qml's
//! scheme binding, branch for branch.
inline SchemeFile chooseSchemeFile(const ColorizerEnv &env, ThemeSource source, bool appliedThemeExists)
{
    const bool appliedIsPlasma = appliedSchemeIsPlasmaTheme(source, env);

    if (env.inConfigureAppletsMode && (env.themeColors == Types::SmartThemeColors)) {
        if (!env.compositingActive && !appliedIsPlasma) {
            return SchemeFile::AppliedScheme;
        }

        //! in edit mode (that is shown the edit visual without opacity)
        //! take care the applets that need a proper color scheme to paint themselves
        if (env.editModeTextColorIsBright == env.plasmaThemeIsLight) {
            //! the edit text's brightness matches the theme's, so contrast
            //! needs the REVERSED variant. Qt5 spelled this
            //! `darkTheme === defaultTheme ? lightTheme : darkTheme`; the
            //! default IS the dark variant exactly when the theme is dark
            //! (the identity algebra above), which is this ternary.
            return env.plasmaThemeIsLight ? SchemeFile::DarkThemeFile : SchemeFile::LightThemeFile;
        } else {
            return SchemeFile::DefaultThemeFile;
        }
    }

    if (appliedIsPlasma || !mustBeShown(env, source, appliedThemeExists)) {
        return env.themeExtendedExists ? SchemeFile::DefaultThemeFile : SchemeFile::KdeGlobalsFallback;
    }

    return SchemeFile::AppliedScheme;
}

//! Whether edit mode overrides the palette's text color with the layout's
//! own - Manager.qml's textColor early branch (configure mode, compositing,
//! stored opacity under 0.40, Smart colors, a layout to take it from).
inline bool useLayoutTextColor(const ColorizerEnv &env)
{
    return env.layoutExists
            && env.inConfigureAppletsMode
            && env.compositingActive
            && env.backgroundStoredOpacity < 0.40
            && (env.themeColors == Types::SmartThemeColors);
}

}
}
}

#endif
