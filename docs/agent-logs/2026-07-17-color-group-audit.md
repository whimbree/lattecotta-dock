# Phase 9 color-group audit - 2026-07-17

Working ledger for the three Phase 9 audit checklist items in
docs/PORTING_PLAN.md: (1) every Plasma/Kirigami color-group property
read in shipped QML checked against the theme object genuinely in
scope, (2) the Header-color-group question for panel-contrast chrome,
(3) reads of properties Plasma 6 removed from the containment graphic
objects. Branch: color-group-audit.

## Method and scope

- Enumerated with grep over containment/package, plasmoid/package,
  shell/package, declarativeimports, plus indicators/ (also shipped
  QML): 160 matching lines across 44 files, every non-comment line
  classified below.
- Qt5 spec source: this repo's own history; f79594dd0 (2024-12-18) is
  the last upstream commit, every per-site comparison reads the Qt5
  original from there.
- API pin verified against the store's libplasma 6.7.3 (headers of
  Plasma::Applet, Plasma::Containment, PlasmoidItem/AppletQuickItem;
  corebindingsplugin.qmltypes for the QML exports; the source tarball
  realized from its drv for setExpanded semantics).
- Kirigami platform theme in the latte process: BOTH plugins get
  probed (org.kde.desktop.so from qqc2-desktop-style and
  KirigamiPlasmaStyle.so from libplasma are mapped in the live dock's
  process), selection goes by style name, and this is a plain KDE
  QApplication with org.kde.desktop, so Kirigami.Theme resolves to the
  kdeglobals application scheme through KColorScheme with full color
  set support (Window/View/Button/Header/Complementary). All verdicts
  below assume that; a live probe would pin it (see owed checks).

## Facts the verdicts rest on

- Plasma 5's `theme` context property (Plasma::Theme, the plasma
  desktop theme's colors) has NO Plasma 6 QML equivalent:
  org.kde.plasma.core at the pin exports only Action, ActionGroup,
  Applet, Containment, EdgeEventForwarder, Types. The port's
  substitutes are Kirigami.Theme (application scheme) and
  themeExtended.defaultTheme (a SchemeColors regenerated FROM the
  plasma theme - the faithful stand-in, available where the context
  reaches: containment tree via Interfaces, config windows via
  SubConfigView::init).
- Kirigami.Theme (attached) carries every property name this codebase
  reads (textColor, backgroundColor, highlightColor,
  highlightedTextColor, focusColor, hoverColor, linkColor,
  positive/neutral/negativeTextColor, defaultFont, smallFont), so the
  reads-undefined defect class cannot arise from Kirigami.Theme sites;
  it can only arise from custom palette objects, which were each
  verified:
  - SchemeColors (applyTheme/plasmaTheme/window schemes): all 14 read
    properties exist (app/wm/schemecolors.h), including the button*
    and inactive* members Kirigami.Theme lacks.
  - colorizerManager as a palette (LatteBridge.colorPalette,
    AppletItem/EnvironmentActions indicator palettes): Manager.qml
    publishes every consumed name including buttonFocusColor.
  - IndicatorObject/client-MyView colorPalette fallback is the
    Kirigami.Theme attached object as a VALUE; the shipped default
    indicator handles the polymorphism explicitly
    (`buttonFocusColor !== undefined ? ... : focusColor`,
    indicators/default/package/ui/main.qml:39-41).
  - TaskIcon's iconColorsLoader.item is LatteCore.IconItem:
    backgroundColor/glowColor are real Q_PROPERTYs (iconitem.h).
  - latteView.layout.textColor (abstractlayout.h) and .scheme
    (centrallayout.h, SchemeColors*) exist.
- No live bare `theme.` reads remain anywhere in shipped QML. The two
  grep hits in declarativeimports/components/ComboBox.qml (119, 279
  with 280-281) are inside commented-out blocks (line comment and the
  /* T.TextField */ block at 261-304). Dead code, not defects.

## Item 1 - per-site verdicts (all 160 lines classified)

### CORRECT: font-only reads (colorSet-independent, property exists)

ConfigPanel.qml:146; BindingsExternal.qml:83-84; AppletAlternatives.qml:208-209;
ToolTipDelegate2.qml:104; ContextMenu.qml:130; CompactApplet.qml:231;
CanvasConfiguration.qml:190; SpinBox.qml:17; LatteDockConfiguration.qml:67;
AppearanceConfig.qml:28; ToolTipInstance.qml:372; Ruler.qml:27,199,200,207,
208,215,225,226,288,289,295,305,306 (the file's whole set - all thickness
metrics off defaultFont.pixelSize, no color reads); TypeSelection.qml:23;
EffectsConfig.qml:31; Header.qml:24; TextField.qml:17; AppletDelegate.qml:221;
ConfigAppearance.qml:293; indicators default+plasma config.qml font lines.
Verdict: CORRECT (36 lines).

### CORRECT: explicit colorSet + inherit:false (correct by construction)

- WidgetExplorer.qml:29-32 (root: Complementary when the plasma theme
  is dark, else Window - the deliberate contrast fix for the plasma
  `dialogs/background` FrameSvg, commented in the file; themeExtended
  IS in scope: WidgetExplorerView inherits SubConfigView::init which
  sets it), plus 342-345 (View), 352-355/367-370 (Button), 378-381
  (View), 404-407 (Button).
- AppletDelegate.qml:125-128, 160-163 (Button/Complementary; the
  `typeof themeExtended` guard is redundant but harmless).
- ItemDelegate.qml:90-91 with the textColor read at 95: Kirigami View
  set replacing Qt5's `theme.viewTextColor` - group-faithful mapping.
- ComboBoxButton.qml:136-139 (Button, or Window when the button is
  transparent - matches the surface actually drawn).
- ComboBox.qml:248-249 with 253 (Button set for the label; down state
  reads highlightedTextColor); 457-458 with 460-461 (View set for the
  popup frame, border derived from the same set's textColor).
Verdict: CORRECT (34 lines).

### CORRECT-substituted: inherited Window set, Qt5 pairing preserved

Qt5 read `theme.*` (plasma theme) at these sites; the port's recorded
bulk conversion (b474adadf) substituted Kirigami.Theme. In every site
below BOTH halves of the visible contrast pair (what is drawn under
vs what is drawn over) come from the same substituted scheme, so no
invisible-UI risk was introduced; the deviation from Qt5 is the SOURCE
scheme (application scheme instead of plasma theme) and is uniform.

- LatteDockConfiguration.qml:100-101 (bC/tC feed
  transparentBackgroundColor/borderColor), 256/261/264 (Advanced
  toggle text derived from textColor brightness), 381 (settings body
  background - paired with every text color in the window; Qt5 drew
  the same Rectangle from theme.backgroundColor).
- CanvasConfiguration.qml:72 (icon variant chosen by textColor
  brightness; Qt5 same logic off plasma theme).
- EffectsConfig.qml:170 (shadow "Theme Color" preview - mirrors
  MyViewPrivate:28 so the preview matches the applied value), 246
  (swatch border), 493 (tab underline).
- ConfigOverlay.qml:310 (edit-mode handle plate at 0.35 opacity,
  paired with the Kirigami.Icon on top of it).
- AddingArea.qml:17,25,47 and AddItem.qml:25,31,41-42 (drop areas
  paint their own background and title from one scheme).
- Header.qml:16; Label.qml:25-26; ToolTip.qml:32; TextField.qml:41;
  ItemDelegate.qml:54,105,125; MobileCursor.qml:29,39 (LatteComponents
  used in config windows; upstream Plasma 6 components pair KSvg
  surfaces with Kirigami.Theme colors the same way).
- ToolTipInstance.qml:329 (frosted strip under player controls),
  416 (active task line, focusColor).
- TaskIcon.qml:47,50 (preview highlight rect).
- ProgressOverlay.qml:73,76,79,85 (badge draws its own
  background/text from one scheme) and 88 (Qt5 read
  theme.buttonFocusColor; Kirigami.Theme has no button focus member,
  the port reads the Window set's focusColor - DecorationFocus rarely
  differs between sets; same deviation at BadgeText.qml:54,
  AppletItem.qml:59, DragCorner.qml:17 first branch,
  LatteIndicator.qml:33).
- AudioStream.qml:63 (badge background; borderColor comes from
  root.lightTextColor, same scheme).
- BadgeText.qml:46,52,53,54 (defaults; internally paired).
- plasmoid main.qml:102-103 (themeTextColor/themeBackgroundColor feed
  lightTextColor for badge borders - brightness pick between two
  colors of one scheme).
- ShortcutBadge.qml:32,35 (badge border pick; badge itself paired).
- MyViewPrivate.qml:28 (ThemeColorShadow option: shadow color from
  textColor; shadows have no contrast pairing to break).
- DragCorner.qml:17 (canvas config window corner).
- AppletDelegate.qml:109 (running/recent badge - inherits the
  explorer root's Complementary-or-Window set, so it follows the
  surface fix; positiveTextColor exists on Kirigami.Theme).
- AppletAlternatives.qml:188 - see SUSPECT below.
- indicators/org.kde.latte.plasma FrontLayer.qml:77 (clicked flash,
  Qt5 theme.highlightColor parity).
- colorizer/Manager.qml:32,34-35,74,83,86-87,89-98 Kirigami branches:
  fallbacks that only evaluate when themeExtended is null; every
  fallback property exists; the primary branches read SchemeColors
  members that all exist (see facts). The 86/87 ternary cannot deref
  null: applyTheme is null only when plasmaTheme is also null (both
  require themeExtended), making the identity comparison true and the
  guarded branch taken.
- MyView.qml (client):50 and definition/MyView.qml:50 (colorPalette
  fallback objects); IndicatorObject.qml:108 (same).
- VisibilityManager.qml:163 (Connections target guarded).
- MyViewPrivate.qml backgroundStoredOpacity reads guard
  themeExtendedBackground properly.
Verdict: CORRECT-substituted (77 lines), deviation recorded here and
in the plan; no behavior changed (Qt5-faithful rule: the substitution
is the port's recorded baseline, and re-pointing these at
themeExtended.defaultTheme would be a behavior change needing live
evidence of a real mixed-theme defect first).

### SUSPECT - needs a live mixed-theme desk check (no change made)

The pairing-breaking scenario: a fixed-color dark plasma theme (panel
background SVG stays dark) plus a light application scheme (or vice
versa). Qt5 read the plasma theme at these sites so its chrome always
followed the panel; the port's Kirigami reads follow the application
scheme instead. With matching themes (my real config) the values
coincide; with mixed themes these can approach the forks'
invisible-indicator scenario, through valid-but-wrong colors rather
than undefined reads:

1. LatteIndicator.qml:31,33,36,99 - the built-in FALLBACK indicator's
   dot colors, drawn directly over the panel. The shipped default
   indicator package is NOT affected (it reads indicator.colorPalette,
   which routes to the colorizer palette / plasma-theme SchemeColors
   via the bridge). Candidate fix if the desk check shows it: read
   indicator.colorPalette here too, with the polymorphism guard the
   default package already uses.
2. TaskIcon.qml:28,29 - iconBackgroundColor/glowColor fallbacks handed
   to indicators when needsIconColors and the icon-color extraction is
   off; drawn over the panel.
3. AppletAlternatives.qml:188 - list text over the plasma
   `dialogs/background` SVG, the exact surface class WidgetExplorer
   fixed with its Complementary root set. This engine has NO
   themeExtended in scope (lattecorona only injects
   alternativesHelper), so the WidgetExplorer treatment needs plumbing
   first. Qt5 had no color reads here (PC2 defaults followed the
   plasma theme).

Desk recipe for all three: switch kdeglobals to a light scheme while
keeping a fixed dark plasma theme, then check (1) fallback-indicator
dots visible, 2) icon-colored indicators visible, (3) alternatives
dialog text readable.

### DEFECT - fixed this session

1. containment main.qml:559-560 (item 3's find, see below):
   `Plasmoid.applets[i].expanded = false` threw a TypeError and closed
   nothing. Fixed at the hop's origin through
   extendedInterface.deactivateApplets() + contract pin. Commit
   aac3c9fa5.
2. MultiLayered.qml:509: bare `themeExtended.defaultTheme` deref in
   coloredView, the single unguarded themeExtended consumer; latent
   (short-circuit timing), now compares against
   colorizerManager.plasmaTheme (identical object when wired, defined
   when not). Commit ac8318f86.

## Item 2 - Header color group for panel-contrast chrome

Qualifying panel-contrast sites (chrome drawn over the dock surface):
the indicator palettes (default package via colorPalette; fallback
LatteIndicator directly), task badges (ProgressOverlay, AudioStream,
BadgeText, ShortcutBadge), TaskIcon icon-color fallbacks, edit-mode
chrome (ConfigOverlay handle, AddingArea/AddItem), applet highlight
(AppletItem/ItemWrapper), theme-colored item shadows (MyViewPrivate).

Verdict, per site and as a family: Header is NOT the right target for
this port, at any of them. Reasoning: Plasma 6's panels read Header
because plasmashell paints panel surfaces with Header colors. Latte
paints its own panel from plasma-theme SVGs (KSvg) or the colorizer's
palette, so chrome paired to that surface must follow the SAME source:
that is themeExtended.defaultTheme / the colorizer palette, which is
also exactly what Qt5 Latte did (`theme` = plasma theme). Reading
[Colors:Header] would pair panel chrome with a scheme the panel
background does not follow, wrong in a different way for every
mixed-theme setup. Qt5's behavior therefore does NOT agree with a
Header change anywhere, and per the working agreement no behavior was
changed. Where the SUSPECT sites above fail a live mixed-theme check,
the route is the colorizer palette / themeExtended.defaultTheme, not
Header. Checklist item closed as a recorded decision.

## Item 3 - properties removed from the containment graphic objects

Enumerated every `Plasmoid.*` (245+163+72+... occurrences, 17 distinct
names) and `plasmoid.*` (11 distinct names) property read in shipped
QML and checked each against the pinned 6.7.3 headers:

- EXIST on Plasma::Applet/Containment: configuration, location,
  formFactor, immutable, immutability, userConfiguring, status, id,
  title, busy, pluginName, constraintHints, containmentDisplayHints,
  backgroundHints (so BOTH known writes, containment main.qml:310 and
  plasmoid main.qml:215, are valid), applets, screen, screenGeometry
  (Containment); internalAction is a Q_INVOKABLE.
- Dialog properties (AppletAlternatives.qml:40, CompactApplet.qml:280
  backgroundHints; hideOnWindowDeactivate) exist on PlasmaCore.Dialog
  at the pin.
- CompactApplet.qml keeps the PlasmoidItem-vs-Applet split correct
  (toolTip*/expanded/toolTipItem/hideOnWindowDeactivate off the item,
  location/status/containmentDisplayHints off .plasmoid).
- Panel.qml:17-18 already documents the removed containment-item
  backgroundHints and carries the replacement logic.
- Plasmoid.action() exists only in a comment (ContextMenu.qml:874);
  the live code uses internalAction.
- THE find: `Plasmoid.applets[i].expanded = false` (containment
  main.qml:559-560). Containment::applets returns Plasma::Applet
  objects on Plasma 6 (graphic items are a KF6 TODO in libplasma) and
  `expanded` lives on the graphic item, so the first write threw a
  TypeError aborting the handler: entering configure mode has never
  closed open popups in this port. latte-dock-qt6 ships the identical
  dead loop (their main.qml:590-592). Fixed by routing through
  ContainmentInterface::deactivateApplets() (the C++ itemForApplet
  walk already used by the autohide collapse), with
  appletsexpandedpropertytest pinning both premises at the libplasma
  pin. Commit aac3c9fa5.

## Gates

- Build: clean configure + build in the worktree (RelWithDebInfo).
- ctest: 67/67 green after both fixes (includes the new
  appletsexpandedpropertytest).
- qmllint gate: OK, baseline matched (155 finding files, unchanged).

## Owed live/desk checks

1. Configure-mode collapse (aac3c9fa5): open an applet popup, enter
   edit/configure mode, popup must collapse, no TypeError in the log.
2. The three SUSPECT mixed-theme sites (recipe above).
3. Kirigami platform theme pin: confirm org.kde.desktop is the ACTIVE
   Kirigami platform plugin in the dock process (both .so files are
   mapped; a one-line qml probe of Kirigami.Theme.textColor against
   kdeglobals vs the plasma theme settles it).
4. Alternatives dialog (AppletAlternatives.qml): `Plasmoid.location`
   read through the attached type on a foreign AppletQuickItem
   reference; open "Show Alternatives" once and confirm the dialog
   places on the dock edge (exercises that attached resolution).
