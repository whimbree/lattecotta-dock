# Theming / colorization pipeline audit - inventory (2026-08-11, read-only scout)

Scope: colorizer subsystem, plasma theme integration, panel background painting,
reverse/smart color modes, per-applet colorizing, indicator coloring, badge
coloring, window color-scheme tracking. Prior audits NOT re-done: the 2026-07-17
color-group audit (all 160 Kirigami.Theme/palette property reads,
docs/agent-logs/2026-07-17-color-group-audit.md) and the 2026-08-10
settings-wiring audit (config-page control -> key writes).

Verdicts: OK (consumer quoted) / DEAD / BROKEN / SUSPECT. Evidence is file:line
in the working tree at b27e133c4.

## Part 1 - colorizer core (audited first-hand)

| Surface | Key/property | Consumer | Verdict |
|---|---|---|---|
| themeColors (containment main.xml:139, enum PlasmaTheme/Reverse/Smart/Dark/Light/Layout) | root.themeColors main.qml:200 | ColorizerDecider input Manager.qml:129 -> chooseThemeSource colorizerdecision.h:113-195 (all six enum values handled incl. Reverse at :162-163) | OK |
| windowColors (main.xml:151, None/Active/Touching) | root.windowColors main.qml:201 | Manager.qml:130 -> decider; applyingWindowColors Manager.qml:73-76 | OK statically; Wayland scheme-source chain in Part 2 |
| SmartThemeColors background tracking | Manager.qml:22 Loader active on SmartThemeColors -> LatteApp.BackgroundTracker :154-158; currentBackgroundBrightness :71 -> decider | OK |
| colorizerManager published names | all 8 `colorizerManager.*` references tree-wide (plasmaTheme, mustBeShown, backgroundColor, applyTheme, outlineColor, originalLightTextColor, backgroundIsBusy, applyColor) exist on Manager.qml | OK (grep-verified exhaustively) |
| focusGlowColor | Qt5 Manager.qml:31 defined it; port dropped it | DEAD-in-Qt5-too: zero in-tree consumers at b474adadf~1 and at HEAD. Only risk: third-party KNS indicator packages reading colorPalette.focusGlowColor would now get undefined. Note-only. |
| Qt5 parity of decision tree | colorizerdecision.h vs Qt5 Manager.qml applyTheme/mustBeShown/scheme (git show b474adadf~1) | OK - branch-for-branch verbatim incl. the preserved || precedence oddity (:129-136) and the dead themeContrastedBackground candidate dropped with a comment (:176-179); pinned by colorizerdecisiontest |
| layout scheme source | LayoutThemeColors -> latteView.layout.scheme Manager.qml:124 | OK: CentralLayout Q_PROPERTY scheme (app/layout/centrallayout.h:38, SchemeColors*) |
| layout textColor | Manager.qml:67,91 | OK: AbstractLayout Q_PROPERTY textColor (app/layout/abstractlayout.h:60) |
| latteView.colorizer C++ bridge | BindingsExternal.qml:190-196 writes latteView.colorizer = colorizerManager | OK: View Q_PROPERTY (app/view/view.h:114); consumed by colorizerData D-Bus readback (app/dbusreports.cpp:628-660) |
| panelOutline (main.xml:107) | root.panelOutline main.qml:292 -> outline Loader MultiLayered.qml:678-687 (borderColor colorizerManager.outlineColor, width themeExtended.outlineWidth) + Manager.qml:51-61 | OK |
| editBackgroundOpacity (main.xml:27) | main.qml:1023-1036 (edit-mode visual opacity, wheel-adjusted) | OK |

## Part 2 - panel background / effects chain (MultiLayered five-layer stack, first-hand + agent A)

| Key | Consumer | Verdict |
|---|---|---|
| useThemePanel | main.qml:232 (userShowPanelBackground) + MultiLayered.qml:49 opacity | OK |
| panelSize | MultiLayered.qml:235,244 visualThickness fraction | OK |
| panelTransparency | backgroundStoredOpacity MyViewPrivate.qml:23-24 (with -1 sentinel -> themeExtendedBackground.maxOpacity) -> overlayedBackground midOpacity MultiLayered.qml:642-650 + decider input | OK |
| backgroundRadius (>=0 sentinel) | customRadiusIsEnabled MultiLayered.qml:323, customRadius :325-333 | OK |
| backgroundShadowSize (>=0 sentinel) | customUserShadowIsEnabled MultiLayered.qml:321, customShadow :334-340 | OK |
| shadowColorType/shadowOpacity/shadowSize/shadowColor (applet shadows) | MyView.qml:48-51 itemShadow.* (MyViewPrivate.qml:20-35: Default->"080808", Theme->Kirigami.Theme.textColor [substituted read, in prior audit's CORRECT-substituted class], User->shadowColor) -> ShadowedItem layer effects ItemWrapper.qml:449-467, task shadows | OK |
| appletShadowsEnabled | MyView.qml:48 itemShadow.isEnabled -> _wrapperContainer.shadowIsEnabled ItemWrapper.qml:449-452 | OK |
| blurEnabled | main.qml:78 blurActive (core backgroundstate.h:206-211) -> BindingsExternal.qml:245-253 effects.drawEffects -> app/view/effects.cpp:829 KWindowEffects::enableBlurBehind(m_view,true,effectRegion), cleared :842 | OK (wiring static; blur draw = nested-vehicle check) |
| panelShadows (internal SVG) | main.qml:299-310 panelShadowsActive (core backgroundstate.h:239-288) -> MultiLayered.qml:399-403 hideShadow -> shadowsSvgItem opacity :393 | OK |
| panelShadows (external, behaveAsPlasmaPanel) | main.qml:97 drawShadowsExternal -> BindingsExternal.qml:255-261 -> effects.cpp:773-782 updateShadows -> PanelShadows::addWindow -> panelshadows.cpp:340 shadow->create() via KF6 KWindowShadow (Wayland org_kde_kwin_shadow; transplanted from plasma-workspace 4c3ace3d, cited panelshadows.cpp:10-13). Qt5's dual X11-pixmap/KWayland::Client impl replaced; no dead arm | OK (compositor draw = render check) |
| disablePanelShadowForMaximized | main.qml:96 -> arg main.qml:302 -> backgroundstate.h:249-251 forcedNoShadows; tracker kept awake BindingsExternal.qml:438 | OK |
| backgroundAllCorners | BindingsExternal.qml:208-219 -> effects.cpp:981 -> app/view/panelborderdecision.h:92,:113 | OK |
| backgroundOnlyOnMaximized / solidBackgroundForMaximized / plasmaBackgroundForPopups | cores use ALL params: backgroundstate.h:167,:194,:277-278 / :141 / :142,:144,:283 + colorizerdecision.h:119,:149 + MultiLayered.qml:468,:680; only unused-looking input is the DOCUMENTED dead transparency clause backgroundstate.h:257-267 (Qt5-faithful, test-pinned) | OK |
| shadows enum + shadowsUpgraded | Upgrader.qml:21-29 one-shot migration to appletShadowsEnabled (instantiated main.qml:899, Component.onCompleted); post-upgrade "shadows" has zero consumers - correct-by-design legacy | OK-legacy |
| background contrast | effects.cpp:831-837 enableBackgroundContrast, clear :843; updateBackgroundContrastValues :873-891 token-identical to Qt5 f0ad7b23; theme-driven via Plasma::Theme themeChanged effects.cpp:149-152; panelTransparency -1 sentinel forwarded BindingsExternal.qml:241 -> effects.cpp:882 | OK (draw = render check) |
| enabledBorders | effects.cpp:961-996 PanelBorderDecision, positioner-guarded :902-935 -> both FrameSvgItems MultiLayered.qml:390,:563 | OK |
| blur rect coverage | docks: MultiLayered.qml:543 effects.rect (11ms timer); panels (floating AND plain): positioner.cpp:1072-1082 -> Effects::applyFloatingPanelPresentation effects.cpp:527; hidden-dock ISHIDDENMASK refused effects.cpp:797 (matches Qt5 960186b14) | OK |
| DEVIATION (recorded) panel blur region | Qt5 f0ad7b23 blurred behaveAsPlasmaPanel windows regionless; port always computes region from visible shape + SVG mask because the panel window is now a larger stable canvas (48e1f9b39) - whole-window blur would bleed | OK-deliberate |
| theme-change refresh hack | MultiLayered.qml:490-498 double-toggles Plasmoid.configuration.panelShadows on themeChanged (Qt5-era FrameSvgItem-margins workaround). Cannot refresh margins via the border path (PanelBorderDecision inputs independent of shadows); KF6 KSvg FrameSvgItem re-resolves margins on theme change itself, so the premise is LIKELY STALE. Net config unchanged (churn, not corruption); side effect: external-shadow destroy/recreate churn on panels | SUSPECT (needs nested-vehicle theme-switch check comparing solidBackground.margins with hack disabled) |

## Part 3 - per-applet colorizing (first-hand)

| Surface | Consumer | Verdict |
|---|---|---|
| D21 approach B palette push | AppletItem.qml:862-877: _wrapper sets its OWN Kirigami.Theme.{textColor,backgroundColor,highlightColor,highlightedTextColor,positiveTextColor,neutralTextColor,negativeTextColor} from colorizerHost when colorizerPaletteActive (:102-104); Kirigami.Theme.inherit gate :870 | OK - DELIBERATE recorded divergence from Qt5 FBO ColorOverlay (comment at :892-899); e2e 110/111 + colorizerExemptionReason observability (:109-123, dbusreports.cpp:336-337) |
| Retired ColorOverlay path | appletColorizer AppletItem.qml:888-908 held at mustBeShown:false; applet/colorizer/Applet.qml inert (visible: opacity>0) | OK-inert by design (single rollback point) |
| userBlocksColorizingApplets (main.xml:18) | AppletItem.qml:91-92 indexOf(applet.plasmoid.id) - correct one-hop Plasma6 id read | OK read side (writer side: agent C) |
| lockedZoomApplets | AppletItem.qml:90 same one-hop pattern | OK read side |
| Latte-aware applet palette bridge | LatteBridge.qml:53 colorPalette = colorizerManager when !latteSideColoringEnabled; :62 applyPalette = mustBeShown | OK (client-side reads verified in prior color-group audit) |
| ShortcutBadge border | ShortcutBadge.qml:92 colorizerManager.originalLightTextColor | OK |
| Edit ruler palette | EnvironmentActions.qml:224 colorPalette: colorizerManager.applyTheme | OK |

## Part 4 - per-applet lists, palette bridge, indicators, badges (agent-verified, spot-checked)

| Surface | Consumer chain | Verdict |
|---|---|---|
| userBlocksColorizingApplets + lockedZoomApplets writers | edit-mode ConfigOverlay.qml:614,:625 fastLayoutManager.setOption(applet.plasmoid.id, "userBlocksColorizing"/"lockZoom"); store containment/plugin/layoutmanager.cpp setOption :726, saveOptions :700-711, restoreOptions :570-585 (";"-joined int lists, cleanupOptions :1211 prunes); one-hop plasmoid.id domain consistent across writer/reader/C++ (9a6f8fb85 fixed the shared persistence path) | OK (cheap nested-vehicle round-trip confirm optional) |
| userBlocks/lockZoom consumers | AppletItem.qml:88-92 -> IndicatorLevel.qml:57, AppletItem.qml:1001,:1003 (parabolic), :102-123 (colorizerPaletteActive + exemption reason, pinned by tests/sourceguardtest.cpp:167,2548); app mirror containmentinterface.cpp:660-683,:868-897 -> dbusreports.cpp:323-324, clonedview.cpp:92-93,:116-117 | OK |
| Palette bridge rename (Qt6-forced) | Qt5 published `palette` on LatteBridge + IndicatorObject; Qt6 Item has a FINAL `palette`, b474adadf renamed BOTH publishers and every in-tree reader to `colorPalette` (LatteBridge.qml:53,:62 <-> client MyView.qml:51). No severed pairs (grep-verified). COMPAT NOTE: third-party Qt5-era KNS indicators reading indicator.palette.textColor silently get Qt6 QQuickPalette (no textColor role) - unfixable without shipping the old name | OK + recorded compat note |
| Default indicator coloring | indicators/default/package/ui/main.qml; `indicator` is a DECLARED property on IndicatorItem.qml:9-10 root (not a context property) - no family-2 QQC2 shadowing exposure; isActiveColor guards Kirigami fallback's missing buttonFocusColor (main.qml:43-45); feed points AppletItem.qml:824 (colorPalette: colorizerHost.applyTheme) and TaskItem.qml:204 | OK |
| glow3D self-binding nit | indicators/default main.qml:110,:225 `glow3D: glow3D` on GlowPoint = self-referential (scope object resolves first), config key never reaches GlowPoint through it. BYTE-IDENTICAL in Qt5 (a3bdc89a0:105,:220); config default == GlowPoint default (true) so no visible change. Same pattern in unreachable client fallback LatteIndicator.qml:99,:214 | Qt5-faithful nit (check for Qt6 binding-loop warning in a render check; fix = `glow3D: indicator.configuration.glow3D` class change, product call) |
| infoBadgeProminentColorEnabled (plasmoid main.xml:136) | plasmoid main.qml:142 -> ProgressOverlay.qml:76,:85; writer TasksConfig.qml:101-104; containment twin (main.xml:339) is migration-source only, consumed by Upgrader.qml:63 (same as Qt5) | OK |
| Badge colors generally | Kirigami.Theme-sourced (ProgressOverlay :73; AudioStream.qml:89-91 + lightTextColor plasmoid main.qml:95-103) matching Qt5 global-theme reads; badges never consumed the colorizer bridge in Qt5 either; audio icon colorGroup KSvg.Svg.Button (AudioStream.qml:105). Mapping note: Qt5 theme.buttonFocusColor -> Kirigami.Theme.focusColor (ProgressOverlay.qml:88) | OK |
| plasmoid showShadows | plasmoid main.qml:913 myView.local.itemShadow.isEnabled | OK |
| colorizerManager.applyColor | Manager.qml:83; only render consumer is the INERT overlay (colorizer/Applet.qml:41) + D-Bus readback dbusreports.cpp:680 | OK-dormant (rollback-point machinery) |

## Part 5 - legacy/dead keys

| Key | Verdict |
|---|---|
| containment showGlow (main.xml:184) | DEAD-inherited: zero consumers at HEAD AND in Qt5 (a3bdc89a0: declaration + layout templates only; conceptual successor is the indicator package's glowEnabled from the 2019 re-architecture 0b90411b1, no migration ever written upstream). Shipped templates still write showGlow=false (e.g. shell/package/contents/templates/Default.layout.latte:61). Not a port regression; housekeeping candidate. |
| plasmoid showGlow / threeColorsWindows / dotsOnActive | DEAD: only the legacy standalone config page binds them (ConfigAppearance.qml:25-29); no runtime consumer. Dead in Qt5 too (consumers died in the 2019 indicators re-architecture, last refs removed through ac890e2a3). Matches the 2026-08-10 settings-audit finding; disposition pending there (remove-vs-wire). |
| shadows enum + shadowsUpgraded (containment main.xml:258,281) | OK-legacy: Upgrader.qml:21-29 one-shot migration to appletShadowsEnabled; post-upgrade zero consumers by design. |

## Part 6 - window color-scheme tracking + plasma theme extended (agent-verified, key claims re-verified first-hand)

| Surface | Consumer chain / evidence | Verdict |
|---|---|---|
| windowColors in-tree wiring | Manager.qml:73-76,:125-127 -> currentscreentracker.cpp:164-171, allscreenstracker.cpp:100-102 -> wm/tracker/windowstracker.cpp:1061-1075 (view), :1161 (layout) -> Schemes::schemeForWindow schemes.cpp:108-117 | OK |
| Per-window scheme ASSOCIATION on Wayland | The ONLY writer of a per-window scheme (Qt5 and now) is the external D-Bus method Corona::windowColorScheme (lattecorona.cpp:1221-1247, org.kde.LatteDock.xml:18), historically fed by the psifidotos "Window Colors" KWin script (not shipped). Wayland handler tags m_wm->activeWindow() after 200ms - byte-identical to pre-port. Without the feeder Active/Touching silently degrade to the kdeglobals default scheme (Qt5-faithful; the default DOES live-update via schemes.cpp:56-70 KDirWatch). This tree never read _KDE_NET_WM_COLOR_SCHEME on any platform (verified: pre-removal xwindowinterface.cpp has zero scheme code) | SUSPECT-ecosystem (in-port chain OK; Plasma 6 compat of the external script unverified - real-desktop check; nested D-Bus injection proves the in-port half) |
| X11 parse arm of windowColorScheme | lattecorona.cpp:1234-1246 unreachable (main.cpp:137 refuses non-wayland) | DEAD-known (already filed as x11-cleanup D3, PORTING_PLAN.md:551) |
| SchemeColors parser | schemecolors.cpp:240-269 reads WM/Colors:Selection/Colors:Window/Colors:Button (KConfig semantics unchanged in KF6); all 13 colors + schemeFile populated (schemecolors.h:20-36), all 14 consumed Manager.qml:85-114; missing keys -> invalid QColor mitigated by D14 guards + loud C++ boundary; unit-tested (tests/wmtoolstest.cpp, 46f88fd9a); Plasma 6 auto-accent path schemecolors.cpp:163-187 with BreezeLight fresh-home fallback (72deaa8ce) | OK |
| ThemeExtended construction | theme.cpp: original scheme = theme colors file or kdeglobals (:366-411); default scheme copied to QTemporaryDir with WM keys rewritten (:205-243); REVERSED scheme still generated for ReverseThemeColors (:245-322); isLightTheme lumina (:413-429); hasShadow KSvg corner-alpha scan (:334-364); per-edge backgrounds (:41-44,:324-332) consumed MultiLayered.qml:365-374; theme-change notify stays with Plasma::Theme (still emits themeChanged in libplasma 6.7.2), connected theme.cpp:62-63 | OK |
| RUNTIME scheme-change refresh | theme.cpp:180-182: `if (m_originalSchemePath == file && m_defaultScheme && m_reversedScheme) return;` swallows every refresh on the Plasma 6 auto-accent path, because possibleSchemeFile("kdeglobals") returns the CONSTANT path ~/.config/kdeglobals whenever kdeglobals carries [WM] activeBackground (schemecolors.cpp:176-185) - VERIFIED PRESENT in the real kdeglobals on this machine. The KDirWatch on kdeglobals (theme.cpp:395-407) fires but its re-invocation always early-returns; updateDefaultScheme/updateReversedScheme/loadThemeLightness never re-run; the temp-dir snapshots and isLightTheme stay stale until restart. Asymmetry: the wm-tracker default scheme watches kdeglobals DIRECTLY and does update -> half-updated dock after a runtime Light<->Dark switch. Qt5 had the same guard but only hit it under Plasma >= 5.25 auto-accent | BROKEN (refresh only; startup state correct). Statically decided; nested-vehicle check for visible evidence |
| PanelBackground SVG scan | panelbackground.cpp:309-327 KSvg::Svg + QImage; maxOpacity :117-128, paddings :130-142, roundness tri-path :144-233/:291-307, shadow bands :235-288; math in tested PanelBackgroundScan core (EX-25, 8fe5f2d8f); no STUB markers | OK |
| Bare KSvg::Svg theme resolution | Works via libplasma6 ThemePrivate mutating the process-global ImageSet in place (theme_p.cpp:99-100,:394); Latte constructs Plasma::Theme before any scan (lattecorona.cpp:120,:170) | OK (fragile-looking; comment nit) |
| KDE_COLOR_SCHEME_PATH pin coverage | ONLY View pinned (view.cpp:410-413; grep-verified single site). NOT pinned: SubConfigView family (primary/secondary/canvas config windows - render Kirigami.Theme-colored shell config QML), PlasmaQuick::ConfigView applet-config window (view.cpp:1235), InfoView. Same later-created-QQuickWindow palette-resolution mechanism as the original a774ee554 defect. SubWindow/ScreenSpaceReservation not relevant (no palette content); QWidget dialogs exempt | SUSPECT (needs live/nested palette check; pin is cheap) |
| themeExtended QML exposure | property-injection: view.cpp:570 _latte_themeExtended_object -> interfaces.cpp:114,:156 (LatteApp.Interfaces, registered lattecorona.cpp:2054) -> containment main.qml:1337-1360, alias :365; null-before-wire handled by consumers (prior audit) + view.cpp:574-587 updateInterfaces | OK |
| panelbackground.h shadowSizeChanged/shadowColorChanged | declared (:32,:38) never emitted - identical in Qt5 tree (checked at 8709818fa) | Qt5-inherited nit |

## Prioritized defect/finding list

1. BROKEN (file + fix): ThemeExtended never refreshes on a runtime global scheme
   change on the Plasma 6 kdeglobals auto-accent path. theme.cpp:180 path-equality
   early return + constant possibleSchemeFile result (schemecolors.cpp:176-185);
   the theme.cpp:395-407 KDirWatch refresh is always swallowed. Impact: all
   themeColors modes' palettes (default/dark/light/reversed), isLightTheme and
   dependent colorizer decisions stale until restart, while the wm-tracker
   kdeglobals scheme updates live (half-updated dock). Fix shape: make the
   watcher-driven path content-aware - on the kdeglobals branch re-run
   updateDefaultScheme/updateReversedScheme/loadThemeLightness + emit themeChanged
   even when the path is unchanged (keep the first-call null-schemes guard);
   simplest: a Theme::refreshOriginalScheme() that skips the path-equality check,
   called from the two KDirWatch lambdas. Nested-vehicle evidence: flip the scheme
   mid-run, read colorizerData / compare rendered panel colors.
2. SUSPECT (cheap fix + live check): config windows lack the KDE_COLOR_SCHEME_PATH
   pin (only View has it). Apply the same 4-line pin to SubConfigView (covers
   primary/secondary/canvas), the applet-config PlasmaQuick::ConfigView creation
   site (view.cpp:1235) and InfoView; verify with the a774ee554 palette scenario.
3. SUSPECT (stale-premise hack, decide-then-remove): MultiLayered.qml:490-498
   double-toggle of Plasmoid.configuration.panelShadows on themeChanged. KF6
   KSvg::FrameSvgItem re-resolves margins on theme change itself; the hack cannot
   refresh margins through the border path and causes config-write plus
   external-shadow destroy/recreate churn. Nested theme-switch check comparing
   solidBackground.margins with the hack disabled, then delete or re-comment.
4. INHERITED dead setting (file, low): default indicator "3D glow" config option
   never reaches GlowPoint - `glow3D: glow3D` self-binding (indicators/default
   package ui/main.qml:110,:225; byte-identical in Qt5 a3bdc89a0:105,:220; also in
   the unreachable client fallback LatteIndicator.qml:99,:214). GlowPoint default
   (true) masks it until a user unchecks the option. One-line fix per site
   (qualify the RHS); Qt5-faithful means filing it as an upstream-inherited defect
   with the fix as a deliberate behavior correction.
5. ECOSYSTEM decision (document or port): windowColors Active/Touching modes need
   the external Window Colors KWin script feeding the windowColorScheme D-Bus
   method to ever differ from the default scheme; Plasma 6 compatibility of that
   script unverified. Options: README/user-doc note, verify/port the script as a
   continuation item, or accept silent Qt5-faithful degrade (record it).
6. Notes (plan items / comment nits, not defects): (a) Manager.qml dropped Qt5's
   focusGlowColor - dead in-tree in Qt5 too; only third-party KNS indicators could
   read it; (b) same class: Qt5-era third-party indicators reading
   indicator.palette.* now silently get Qt6's QQuickPalette (the Qt6-forced
   palette->colorPalette rename, b474adadf) - compat note candidate for docs;
   (c) bare-KSvg::Svg global-ImageSet dependency deserves a comment in theme.cpp;
   (d) panelbackground.h declared-never-emitted shadow signals (Qt5-inherited);
   (e) ItemWrapper.qml:72-81 comment still states the pre-correction "MultiEffect
   does NOT auto-wrap" claim (corrected 2026-07-15 in the defect-families skill)
   and names MultiEffect for a ColorOverlay path - doc-rot on the dormant
   rollback leg; (f) containment showGlow + plasmoid
   showGlow/threeColorsWindows/dotsOnActive dead keys - fold into the pending
   settings-audit remove-vs-wire disposition.

## Owed live/nested checks

Nested-vehicle (no real desktop needed):
- N1. Runtime scheme flip (Light<->Dark in the nested env's kdeglobals): prove
  finding 1's stale defaultTheme/isLightTheme (colorizerData readback + render).
- N2. Theme-switch with the MultiLayered double-toggle hack disabled: compare
  solidBackground.margins (finding 3 removability).
- N3. Compositor draws whose WIRING is static-OK: blur region, background
  contrast, external KWindowShadow on a behaveAsPlasmaPanel dock.
- N4. windowColorScheme D-Bus injection + colorizerData readback (in-port half of
  finding 5).
- N5. glow3D binding-loop warning presence (finding 4).
- N6. userBlocksColorizingApplets/lockedZoomApplets config round-trip (toggle
  overlay buttons, restart, re-read) - cheap confirm of an already-OK chain.
- N7. Config-window palette divergence for finding 2 if reproducible nested;
  otherwise desk.

Real desktop (owed, carried or new):
- D1. Whether a Plasma 6-compatible Window Colors KWin script exists/works
  (finding 5's external half).
- D2. Finding 2's original scenario (later-created clone/second-monitor dock,
  open its config window, compare palettes) if N7 does not reproduce.
- Carried from the 2026-07-17 color-group audit ledger (still open): the three
  SUSPECT mixed-theme sites (fallback LatteIndicator dots, TaskIcon icon-color
  fallbacks, AppletAlternatives text over the plasma dialog SVG); confirm
  org.kde.desktop is the active Kirigami platform plugin; popup collapse on
  entering configure mode; open Show Alternatives once.

## Method note

Chains verified by three parallel read-only scouts (effects/blur/shadows;
window schemes/theme-extended; indicators/badges/per-applet) plus first-hand
audit of the colorizer core, panel background stack, and per-applet colorizing.
Load-bearing agent claims re-verified directly: theme.cpp:175-199 early return,
schemecolors.cpp:147-201 constant-path resolution, real kdeglobals [WM]
activeBackground presence, single KDE_COLOR_SCHEME_PATH pin site, colorPalette
rename with zero stale readers, colorizerManager property-reference
exhaustiveness, CentralLayout scheme Q_PROPERTY.
