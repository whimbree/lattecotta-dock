/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// EX-12 ColorizerDecisionCore (docs/tracking/QML_EXTRACTION_PLAN.md). Every case was
// derived BY HAND from reading the Qt5 body
// (f0ad7b23:containment/package/contents/ui/colorizer/Manager.qml:60
// applyTheme, :159 scheme) - no table was generated from the extracted
// implementation, so a transcription mistake in the core fails here instead
// of certifying itself. The named desk cases carry the executable
// expectations of the 2026-07-14..15 color investigations (5c06b497,
// 79ca3360, 1f835402) so future palette work can consult a test instead of
// re-deriving the tree.

#include <QtTest>

#include "units/colorizerdecision.h"

using namespace Latte::Containment;
using namespace Latte::Containment::ColorizerDecision;

Q_DECLARE_METATYPE(ThemeSource)
Q_DECLARE_METATYPE(SchemeFile)

namespace {

//! an env sitting in the theme branch: accelerated, themeExtended present,
//! no window tracking, nothing forcing plasma style
ColorizerEnv themedEnv()
{
    ColorizerEnv env;
    env.themeExtendedExists = true;
    return env;
}

//! an env sitting in the window branch: tracker on, both current-screen
//! schemes resolvable
ColorizerEnv trackedEnv()
{
    ColorizerEnv env = themedEnv();
    env.windowsTrackerEnabled = true;
    env.selectedActiveWindowSchemeExists = true;
    env.currentScreenActiveWindowSchemeExists = true;
    env.touchingWindowSchemeExists = true;
    return env;
}

}

class ColorizerDecisionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    //! the applyTheme tree
    void themeSource_softwareRenderingForcesPlasmaDefault();
    void themeSource_settingsTable_data();
    void themeSource_settingsTable();
    void themeSource_smartTable_data();
    void themeSource_smartTable();
    void themeSource_plasmaStyleOverrides_data();
    void themeSource_plasmaStyleOverrides();
    void themeSource_windowBranch_data();
    void themeSource_windowBranch();
    void themeSource_touchingBranchPrecedence();
    void themeSource_noThemeExtendedFallsToPlasmaDefault();

    //! the applyTheme === plasmaTheme identity algebra
    void identity_truthTable_data();
    void identity_truthTable();

    //! mustBeShown
    void mustBeShown_matrix_data();
    void mustBeShown_matrix();

    //! the scheme file selection
    void schemeFile_matrix_data();
    void schemeFile_matrix();

    //! the edit-mode text color override
    void textColor_overrideThresholds_data();
    void textColor_overrideThresholds();

    //! named desk cases
    void named_lightThemeColorsDeskScenario();
    void named_darkColorsRealConfigFlip();
    void named_activeWindowSchemeArrivalFlipsTheSource();
};

void ColorizerDecisionTest::themeSource_softwareRenderingForcesPlasmaDefault()
{
    // the tree's very first exit: software rendering pins the plasma
    // default even when a window scheme is available and Dark is configured
    ColorizerEnv env = trackedEnv();
    env.graphicsSystemAccelerated = false;
    env.themeColors = Types::DarkThemeColors;
    env.windowColors = Types::ActiveWindowColors;

    QCOMPARE(chooseThemeSource(env), ThemeSource::PlasmaDefault);
}

void ColorizerDecisionTest::themeSource_settingsTable_data()
{
    QTest::addColumn<int>("themeColors");
    QTest::addColumn<bool>("plasmaThemeIsLight");
    QTest::addColumn<bool>("layoutExists");
    QTest::addColumn<ThemeSource>("expected");

    QTest::newRow("plasma setting falls to default")
            << int(Types::PlasmaThemeColors) << true << true << ThemeSource::PlasmaDefault;
    QTest::newRow("dark setting picks dark theme")
            << int(Types::DarkThemeColors) << true << true << ThemeSource::DarkTheme;
    QTest::newRow("light setting picks light theme")
            << int(Types::LightThemeColors) << false << true << ThemeSource::LightTheme;
    QTest::newRow("reverse on light theme picks dark")
            << int(Types::ReverseThemeColors) << true << true << ThemeSource::DarkTheme;
    QTest::newRow("reverse on dark theme picks light")
            << int(Types::ReverseThemeColors) << false << true << ThemeSource::LightTheme;
    QTest::newRow("layout setting with a layout picks the layout scheme")
            << int(Types::LayoutThemeColors) << true << true << ThemeSource::LayoutScheme;
    QTest::newRow("layout setting without a layout falls to default")
            << int(Types::LayoutThemeColors) << true << false << ThemeSource::PlasmaDefault;
}

void ColorizerDecisionTest::themeSource_settingsTable()
{
    QFETCH(int, themeColors);
    QFETCH(bool, plasmaThemeIsLight);
    QFETCH(bool, layoutExists);
    QFETCH(ThemeSource, expected);

    ColorizerEnv env = themedEnv();
    env.themeColors = static_cast<Types::ThemeColorsGroup>(themeColors);
    env.plasmaThemeIsLight = plasmaThemeIsLight;
    env.layoutExists = layoutExists;

    QCOMPARE(chooseThemeSource(env), expected);
}

void ColorizerDecisionTest::themeSource_smartTable_data()
{
    QTest::addColumn<bool>("busyBackground");
    QTest::addColumn<double>("brightness");
    QTest::addColumn<double>("opacity");
    QTest::addColumn<ThemeSource>("expected");

    // non-busy: contrast against the measured background brightness
    QTest::newRow("bright background wants the light theme")
            << false << 200.0 << 0.5 << ThemeSource::LightTheme;
    QTest::newRow("dark background wants the dark theme")
            << false << 50.0 << 0.5 << ThemeSource::DarkTheme;
    QTest::newRow("tracker not loaded (-1000) reads as dark")
            << false << -1000.0 << 0.5 << ThemeSource::DarkTheme;
    QTest::newRow("threshold is strictly above 127.5")
            << false << 127.5 << 0.5 << ThemeSource::DarkTheme;

    // busy: the stored-opacity bands of the Qt5 body
    QTest::newRow("busy under 0.35 contrasts bright background with light")
            << true << 200.0 << 0.20 << ThemeSource::LightTheme;
    QTest::newRow("busy under 0.35 contrasts dark background with dark")
            << true << 50.0 << 0.20 << ThemeSource::DarkTheme;
    QTest::newRow("busy band lower bound 0.35 goes dark")
            << true << 200.0 << 0.35 << ThemeSource::DarkTheme;
    QTest::newRow("busy mid band goes dark regardless of brightness")
            << true << 200.0 << 0.50 << ThemeSource::DarkTheme;
    QTest::newRow("busy band upper bound 0.70 still goes dark")
            << true << 200.0 << 0.70 << ThemeSource::DarkTheme;
    QTest::newRow("busy above 0.70 falls back to plasma default")
            << true << 200.0 << 0.71 << ThemeSource::PlasmaDefault;
}

void ColorizerDecisionTest::themeSource_smartTable()
{
    QFETCH(bool, busyBackground);
    QFETCH(double, brightness);
    QFETCH(double, opacity);
    QFETCH(ThemeSource, expected);

    ColorizerEnv env = themedEnv();
    env.themeColors = Types::SmartThemeColors;
    env.forcePanelForBusyBackground = busyBackground;
    env.currentBackgroundBrightness = brightness;
    env.backgroundStoredOpacity = opacity;

    QCOMPARE(chooseThemeSource(env), expected);
}

void ColorizerDecisionTest::themeSource_plasmaStyleOverrides_data()
{
    QTest::addColumn<bool>("popupsCombo");            // userShowPanelBackground && plasmaBackgroundForPopups && hasExpandedApplet
    QTest::addColumn<bool>("busyVertical");           // plasmaStyleBusyForTouchingBusyVerticalView
    QTest::addColumn<bool>("smartSolidCombo");        // Smart + NoneWindowColors + forceSolidPanel
    QTest::addColumn<int>("themeColors");
    QTest::addColumn<ThemeSource>("expected");

    QTest::newRow("expanded popup with plasma background forces plasma style")
            << true << false << false << int(Types::DarkThemeColors) << ThemeSource::PlasmaDefault;
    QTest::newRow("busy touching vertical view forces plasma style")
            << false << true << false << int(Types::DarkThemeColors) << ThemeSource::PlasmaDefault;
    QTest::newRow("smart plus solid panel without window colors forces plasma style")
            << false << false << true << int(Types::SmartThemeColors) << ThemeSource::PlasmaDefault;
    QTest::newRow("solid panel alone does not override an explicit dark setting")
            << false << false << true << int(Types::DarkThemeColors) << ThemeSource::DarkTheme;
}

void ColorizerDecisionTest::themeSource_plasmaStyleOverrides()
{
    QFETCH(bool, popupsCombo);
    QFETCH(bool, busyVertical);
    QFETCH(bool, smartSolidCombo);
    QFETCH(int, themeColors);
    QFETCH(ThemeSource, expected);

    ColorizerEnv env = themedEnv();
    env.themeColors = static_cast<Types::ThemeColorsGroup>(themeColors);
    env.plasmaThemeIsLight = true;
    env.currentBackgroundBrightness = 200.0; // smart would pick Light if the override missed

    if (popupsCombo) {
        env.userShowPanelBackground = true;
        env.plasmaBackgroundForPopups = true;
        env.hasExpandedApplet = true;
    }
    env.plasmaStyleBusyForTouchingBusyVerticalView = busyVertical;
    if (smartSolidCombo) {
        env.windowColors = Types::NoneWindowColors;
        env.forceSolidPanel = true;
    }

    QCOMPARE(chooseThemeSource(env), expected);
}

void ColorizerDecisionTest::themeSource_windowBranch_data()
{
    QTest::addColumn<int>("windowColors");
    QTest::addColumn<bool>("trackerEnabled");
    QTest::addColumn<bool>("selectedSchemeExists");
    QTest::addColumn<bool>("touchingSchemeExists");
    QTest::addColumn<bool>("popupOverride");          // plasmaBackgroundForPopups && hasExpandedApplet
    QTest::addColumn<ThemeSource>("expected");

    QTest::newRow("active window colors with a scheme win over the theme setting")
            << int(Types::ActiveWindowColors) << true << true << true << false
            << ThemeSource::SelectedActiveWindowScheme;
    QTest::newRow("active window colors without a scheme fall to the theme branch")
            << int(Types::ActiveWindowColors) << true << false << true << false
            << ThemeSource::DarkTheme;
    QTest::newRow("no tracker means no window branch at all")
            << int(Types::ActiveWindowColors) << false << true << true << false
            << ThemeSource::DarkTheme;
    QTest::newRow("an expanded plasma-background popup suspends the window branch")
            << int(Types::ActiveWindowColors) << true << true << true << true
            << ThemeSource::DarkTheme;
    QTest::newRow("touching colors without a touching scheme fall to the theme branch")
            << int(Types::TouchingWindowColors) << true << true << false << false
            << ThemeSource::DarkTheme;
    QTest::newRow("none window colors ignore available schemes")
            << int(Types::NoneWindowColors) << true << true << true << false
            << ThemeSource::DarkTheme;
}

void ColorizerDecisionTest::themeSource_windowBranch()
{
    QFETCH(int, windowColors);
    QFETCH(bool, trackerEnabled);
    QFETCH(bool, selectedSchemeExists);
    QFETCH(bool, touchingSchemeExists);
    QFETCH(bool, popupOverride);
    QFETCH(ThemeSource, expected);

    ColorizerEnv env = trackedEnv();
    env.themeColors = Types::DarkThemeColors; // the theme-branch fallthrough marker
    env.plasmaThemeIsLight = true;
    env.windowColors = static_cast<Types::WindowColorsGroup>(windowColors);
    env.windowsTrackerEnabled = trackerEnabled;
    env.selectedActiveWindowSchemeExists = selectedSchemeExists;
    env.touchingWindowSchemeExists = touchingSchemeExists;
    if (popupOverride) {
        env.plasmaBackgroundForPopups = true;
        env.hasExpandedApplet = true;
        // userShowPanelBackground stays false so the theme branch's own
        // popup override cannot mask the window-branch suspension
    }

    QCOMPARE(chooseThemeSource(env), expected);
}

void ColorizerDecisionTest::themeSource_touchingBranchPrecedence()
{
    // PRESERVED ODDITY PIN (see the core's comment): Qt5's condition is
    //   existsWindowTouching || existsWindowTouchingEdge && !(active...) && schemeExists
    // with && binding tighter, so a touching window ALONE selects the
    // current-screen ACTIVE window scheme even when the active window is
    // the one touching. Changing this grouping is a behavior change and
    // must fail this test first.
    ColorizerEnv env = trackedEnv();
    env.windowColors = Types::TouchingWindowColors;

    env.existsWindowTouching = true;
    env.activeWindowTouching = true; // does NOT matter under the shipped precedence
    QCOMPARE(chooseThemeSource(env), ThemeSource::CurrentScreenActiveWindowScheme);

    // edge-only touch honors the not-active clauses
    env.existsWindowTouching = false;
    env.existsWindowTouchingEdge = true;
    env.activeWindowTouching = false;
    env.activeWindowTouchingEdge = false;
    QCOMPARE(chooseThemeSource(env), ThemeSource::CurrentScreenActiveWindowScheme);

    // ... and yields the touching scheme when the active window is touching
    env.activeWindowTouchingEdge = true;
    QCOMPARE(chooseThemeSource(env), ThemeSource::TouchingWindowScheme);

    // ... or when the active-window scheme cannot be resolved
    env.activeWindowTouchingEdge = false;
    env.currentScreenActiveWindowSchemeExists = false;
    QCOMPARE(chooseThemeSource(env), ThemeSource::TouchingWindowScheme);

    // no touch at all still publishes the touching scheme (the branch was
    // entered on scheme availability alone)
    env.existsWindowTouchingEdge = false;
    QCOMPARE(chooseThemeSource(env), ThemeSource::TouchingWindowScheme);
}

void ColorizerDecisionTest::themeSource_noThemeExtendedFallsToPlasmaDefault()
{
    ColorizerEnv env; // themeExtendedExists false
    env.themeColors = Types::DarkThemeColors;
    QCOMPARE(chooseThemeSource(env), ThemeSource::PlasmaDefault);

    // the window branch does not need themeExtended
    ColorizerEnv tracked = trackedEnv();
    tracked.themeExtendedExists = false;
    tracked.windowColors = Types::ActiveWindowColors;
    QCOMPARE(chooseThemeSource(tracked), ThemeSource::SelectedActiveWindowScheme);
}

void ColorizerDecisionTest::identity_truthTable_data()
{
    QTest::addColumn<ThemeSource>("source");
    QTest::addColumn<bool>("plasmaThemeIsLight");
    QTest::addColumn<bool>("expected");

    // provider algebra (app/plasma/extended/theme.cpp 180-187): darkTheme
    // IS the default instance iff the theme is dark, lightTheme iff light
    QTest::newRow("plasma default is itself") << ThemeSource::PlasmaDefault << true << true;
    QTest::newRow("plasma default is itself on dark") << ThemeSource::PlasmaDefault << false << true;
    QTest::newRow("dark aliases the default on a dark theme") << ThemeSource::DarkTheme << false << true;
    QTest::newRow("dark is the reversed scheme on a light theme") << ThemeSource::DarkTheme << true << false;
    QTest::newRow("light aliases the default on a light theme") << ThemeSource::LightTheme << true << true;
    QTest::newRow("light is the reversed scheme on a dark theme") << ThemeSource::LightTheme << false << false;
    QTest::newRow("selected window schemes never alias") << ThemeSource::SelectedActiveWindowScheme << true << false;
    QTest::newRow("current screen window schemes never alias") << ThemeSource::CurrentScreenActiveWindowScheme << false << false;
    QTest::newRow("touching window schemes never alias") << ThemeSource::TouchingWindowScheme << true << false;
    QTest::newRow("layout schemes never alias") << ThemeSource::LayoutScheme << false << false;
}

void ColorizerDecisionTest::identity_truthTable()
{
    QFETCH(ThemeSource, source);
    QFETCH(bool, plasmaThemeIsLight);
    QFETCH(bool, expected);

    ColorizerEnv env;
    env.plasmaThemeIsLight = plasmaThemeIsLight;

    QCOMPARE(appliedSchemeIsPlasmaTheme(source, env), expected);
}

void ColorizerDecisionTest::mustBeShown_matrix_data()
{
    QTest::addColumn<ThemeSource>("source");
    QTest::addColumn<bool>("plasmaThemeIsLight");
    QTest::addColumn<bool>("appliedExists");
    QTest::addColumn<bool>("editMode");
    QTest::addColumn<bool>("smart");
    QTest::addColumn<bool>("expected");

    QTest::newRow("a non-default applied scheme must be shown")
            << ThemeSource::DarkTheme << true << true << false << false << true;
    QTest::newRow("an aliased dark-on-dark scheme is already painted by plasma")
            << ThemeSource::DarkTheme << false << true << false << false << false;
    QTest::newRow("a null applied scheme cannot claim to be shown")
            << ThemeSource::DarkTheme << true << false << false << false << false;
    QTest::newRow("plasma default never needs the colorizer")
            << ThemeSource::PlasmaDefault << true << true << false << false << false;
    QTest::newRow("edit mode with smart colors shows regardless of the scheme")
            << ThemeSource::PlasmaDefault << true << false << true << true << true;
    QTest::newRow("edit mode without smart colors adds nothing")
            << ThemeSource::PlasmaDefault << true << true << true << false << false;
}

void ColorizerDecisionTest::mustBeShown_matrix()
{
    QFETCH(ThemeSource, source);
    QFETCH(bool, plasmaThemeIsLight);
    QFETCH(bool, appliedExists);
    QFETCH(bool, editMode);
    QFETCH(bool, smart);
    QFETCH(bool, expected);

    ColorizerEnv env = themedEnv();
    env.plasmaThemeIsLight = plasmaThemeIsLight;
    env.inConfigureAppletsMode = editMode;
    env.themeColors = smart ? Types::SmartThemeColors : Types::PlasmaThemeColors;

    QCOMPARE(mustBeShown(env, source, appliedExists), expected);
}

void ColorizerDecisionTest::schemeFile_matrix_data()
{
    QTest::addColumn<bool>("editMode");
    QTest::addColumn<bool>("smart");
    QTest::addColumn<bool>("compositing");
    QTest::addColumn<bool>("editTextIsBright");
    QTest::addColumn<bool>("plasmaThemeIsLight");
    QTest::addColumn<bool>("themeExtendedExists");
    QTest::addColumn<ThemeSource>("source");
    QTest::addColumn<bool>("appliedExists");
    QTest::addColumn<SchemeFile>("expected");

    // edit mode + smart, the applet-repaint contrast logic (Qt5 :159-178)
    QTest::newRow("edit no-compositing publishes the applied scheme")
            << true << true << false << true << true << true
            << ThemeSource::DarkTheme << true << SchemeFile::AppliedScheme;
    QTest::newRow("edit no-compositing with an aliased scheme falls through to contrast")
            << true << true << false << true << true << true
            << ThemeSource::LightTheme << true << SchemeFile::DarkThemeFile;
    QTest::newRow("edit contrast: bright text on light theme wants the dark file")
            << true << true << true << true << true << true
            << ThemeSource::DarkTheme << true << SchemeFile::DarkThemeFile;
    QTest::newRow("edit contrast: dark text on dark theme wants the light file")
            << true << true << true << false << false << true
            << ThemeSource::DarkTheme << true << SchemeFile::LightThemeFile;
    QTest::newRow("edit contrast: mismatched brightness keeps the default file")
            << true << true << true << true << false << true
            << ThemeSource::DarkTheme << true << SchemeFile::DefaultThemeFile;

    // outside edit mode (or without smart): the mustBeShown gate
    QTest::newRow("plasma default publishes the default theme file")
            << false << false << true << false << true << true
            << ThemeSource::PlasmaDefault << true << SchemeFile::DefaultThemeFile;
    QTest::newRow("no themeExtended yet falls back to the kdeglobals literal")
            << false << false << true << false << true << false
            << ThemeSource::PlasmaDefault << false << SchemeFile::KdeGlobalsFallback;
    QTest::newRow("a shown non-default scheme publishes its own file")
            << false << false << true << false << true << true
            << ThemeSource::DarkTheme << true << SchemeFile::AppliedScheme;
    QTest::newRow("an aliased scheme publishes the default file")
            << false << false << true << false << false << true
            << ThemeSource::DarkTheme << true << SchemeFile::DefaultThemeFile;
    QTest::newRow("a null applied scheme publishes the default file")
            << false << false << true << false << true << true
            << ThemeSource::DarkTheme << false << SchemeFile::DefaultThemeFile;
    QTest::newRow("edit mode without smart takes the normal path")
            << true << false << true << false << true << true
            << ThemeSource::DarkTheme << true << SchemeFile::AppliedScheme;
}

void ColorizerDecisionTest::schemeFile_matrix()
{
    QFETCH(bool, editMode);
    QFETCH(bool, smart);
    QFETCH(bool, compositing);
    QFETCH(bool, editTextIsBright);
    QFETCH(bool, plasmaThemeIsLight);
    QFETCH(bool, themeExtendedExists);
    QFETCH(ThemeSource, source);
    QFETCH(bool, appliedExists);
    QFETCH(SchemeFile, expected);

    ColorizerEnv env;
    env.inConfigureAppletsMode = editMode;
    env.themeColors = smart ? Types::SmartThemeColors : Types::PlasmaThemeColors;
    env.compositingActive = compositing;
    env.editModeTextColorIsBright = editTextIsBright;
    env.plasmaThemeIsLight = plasmaThemeIsLight;
    env.themeExtendedExists = themeExtendedExists;

    QCOMPARE(chooseSchemeFile(env, source, appliedExists), expected);
}

void ColorizerDecisionTest::textColor_overrideThresholds_data()
{
    QTest::addColumn<bool>("layoutExists");
    QTest::addColumn<bool>("editMode");
    QTest::addColumn<bool>("compositing");
    QTest::addColumn<double>("opacity");
    QTest::addColumn<bool>("smart");
    QTest::addColumn<bool>("expected");

    QTest::newRow("all conditions met under the 0.40 threshold")
            << true << true << true << 0.39 << true << true;
    QTest::newRow("threshold is strictly below 0.40")
            << true << true << true << 0.40 << true << false;
    QTest::newRow("no layout, no override")
            << false << true << true << 0.20 << true << false;
    QTest::newRow("outside edit mode, no override")
            << true << false << true << 0.20 << true << false;
    QTest::newRow("without compositing, no override")
            << true << true << false << 0.20 << true << false;
    QTest::newRow("without smart colors, no override")
            << true << true << true << 0.20 << false << false;
}

void ColorizerDecisionTest::textColor_overrideThresholds()
{
    QFETCH(bool, layoutExists);
    QFETCH(bool, editMode);
    QFETCH(bool, compositing);
    QFETCH(double, opacity);
    QFETCH(bool, smart);
    QFETCH(bool, expected);

    ColorizerEnv env = themedEnv();
    env.layoutExists = layoutExists;
    env.inConfigureAppletsMode = editMode;
    env.compositingActive = compositing;
    env.backgroundStoredOpacity = opacity;
    env.themeColors = smart ? Types::SmartThemeColors : Types::DarkThemeColors;

    QCOMPARE(useLayoutTextColor(env), expected);
}

void ColorizerDecisionTest::named_lightThemeColorsDeskScenario()
{
    // 5c06b497's context: LightThemeColors on a DARK session theme - the
    // light variant is the reversed scheme, so the colorizer is active and
    // paints the clock with the light scheme's dark text at load
    ColorizerEnv env = themedEnv();
    env.themeColors = Types::LightThemeColors;
    env.plasmaThemeIsLight = false;

    const ThemeSource source = chooseThemeSource(env);
    QCOMPARE(source, ThemeSource::LightTheme);
    QVERIFY(!appliedSchemeIsPlasmaTheme(source, env));
    QVERIFY(mustBeShown(env, source, true));
}

void ColorizerDecisionTest::named_darkColorsRealConfigFlip()
{
    // 79ca3360's verification: Dark Colors on my real (light) session
    // darkens the background and text goes light - the dark variant is the
    // reversed scheme and the colorizer must repaint
    ColorizerEnv env = themedEnv();
    env.themeColors = Types::DarkThemeColors;
    env.plasmaThemeIsLight = true;

    ThemeSource source = chooseThemeSource(env);
    QCOMPARE(source, ThemeSource::DarkTheme);
    QVERIFY(mustBeShown(env, source, true));
    QCOMPARE(chooseSchemeFile(env, source, true), SchemeFile::AppliedScheme);

    // the same setting on a DARK session aliases the default instance:
    // plasma already paints those colors, the colorizer stays out
    env.plasmaThemeIsLight = false;
    source = chooseThemeSource(env);
    QCOMPARE(source, ThemeSource::DarkTheme);
    QVERIFY(!mustBeShown(env, source, true));
    QCOMPARE(chooseSchemeFile(env, source, true), SchemeFile::DefaultThemeFile);
}

void ColorizerDecisionTest::named_activeWindowSchemeArrivalFlipsTheSource()
{
    // 1f835402's probe-run lesson: with From Window active colors, the
    // tracker coming alive seventeen seconds in flipped applyColor - the
    // scheme's arrival alone moves the source from the theme branch to the
    // window branch
    ColorizerEnv env = themedEnv();
    env.themeColors = Types::DarkThemeColors;
    env.plasmaThemeIsLight = true;
    env.windowColors = Types::ActiveWindowColors;
    env.windowsTrackerEnabled = true;
    env.selectedActiveWindowSchemeExists = false;

    QCOMPARE(chooseThemeSource(env), ThemeSource::DarkTheme);

    env.selectedActiveWindowSchemeExists = true;
    QCOMPARE(chooseThemeSource(env), ThemeSource::SelectedActiveWindowScheme);
}

QTEST_GUILESS_MAIN(ColorizerDecisionTest)

#include "colorizerdecisiontest.moc"
