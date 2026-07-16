# QML extraction plan

Planning artifact for moving behavioral logic out of QML into
strongly-typed, unit-testable C++. Written 2026-07-15 against HEAD
5e1c2b12 by the strong-model planning session named in
docs/prompts/qml-extraction-planning-prompt.md; every commit hash, file
path, and line range cited below was verified with git/grep during that
session. This plan executes across a model transition: specs tagged
`delegate-safe` are written to be executable cold by a weaker model;
specs tagged `strong-model-only` must land inside the remaining
strong-model window or be deferred, never delegated.

Posture (decided in CLAUDE.md as of ce94bb1d, not relitigated here):
maintained continuation. Upstream mergeability is not a constraint.
Non-negotiable: small bisectable commits, Qt5 behavioral fidelity per
extraction (the Qt5 source is in our own history at tag f0ad7b23,
v0.10.8: `git show f0ad7b23:<path>`), re-implementation with
understanding. CaptSilver/latte-dock-qt6 (reviewed through 81384003) is
a blueprint of WHAT to extract and WHICH invariants to pin, never a
source to paste.

## Completeness ledger

Kept current as sections land. A PENDING spec names its scope in one
line; DONE means the full spec is written to the section-C template.

Inventory (section A):
- [x] containment/ - 62 QML files classified
- [x] plasmoid/ - 36 QML files classified
- [x] shell/ - 28 QML files classified
- [x] indicators/ - 9 QML files classified
- [x] declarativeimports/ - 104 QML files classified
- [x] JS logic files addendum - 11 files

Ranking (section B): [x] done.

Per-unit specs (section C), in rank order:
- [x] EX-01 PreviewSwitchEngine - preview adoption/debounce/LRU decision core
- [x] EX-02 ParabolicRouter - neighbor scale-stack propagation chains
- [x] EX-03 ParabolicMathCore - the zoom curve math
- [x] EX-04 AutoSizeEngine - iconSize shrink/grow feedback loop
- [x] EX-05 FillLengthDistributor - Justify/fill two-pass space distribution
- [x] EX-06 VisibleIndexEngine - visible-index math + separator neighbor walks
- [x] EX-07 StorageIdRemapper - layout-file id remapping (capt blueprint)
- [x] EX-08 ScreenGeometryCalculator - available screen rect/region (capt blueprint)
- [x] EX-09 PositionerGeometry - view sizing/placement math (capt blueprint)
- [x] EX-10 MaskInputGeometry - visibility mask + input region rect math
- [x] EX-11 LauncherListOps - launcher order algebra, registries, stored-list parsing
- [x] EX-12 ColorizerDecisionCore - applyTheme/scheme selection tree
- [x] EX-13 ViewTypeAndBackgroundPredicates - Panel-vs-Dock chain + background states
- [x] EX-14 DropEventClassifier - drag mime classification + insert index
- [x] EX-15 WheelAccumulator - wheel delta accumulation/threshold semantics
- [x] EX-16 GroupWindowCycler - next/previous/minimize target selection
- [x] EX-17 TooltipTextComposer - preview title/subtext string transforms
- [x] EX-18 LengthOffsetClamp - maxLength/offset mutual clamp (dedup)
- [x] EX-19 ColorLuminance - shared brightness/luminance helpers (dedup)
- [x] EX-20 BadgeMath - badge parsing, proportion, arc geometry
- [x] EX-21 ScrollOverflowMath - scrollable list overflow/autoscroll math
- [x] EX-22 ActivitySetAlgebra - activity set filtering (capt blueprint)
- [x] EX-23 WindowTrackingPredicates - window predicate + extra-view-hints pass (capt blueprint)
- [x] EX-24 IconSourceClassifier - icon source classification (capt blueprint)
- [x] EX-25 PanelBackgroundScan - panel background scanline math (capt blueprint)

Section D (coverage + ratchet): [x] done.
Section E (waves): [x] done.
Section F (risks + non-goals): [x] done.
Executive summary: [x] done.
PORTING_PLAN cross-reference item: [x] done (Phase 10, QML extraction initiative item).

## Method

- Extraction shape (the capt shape, proven in our own tree by
  d12baff2's `declarativeimports/core/dialog.cpp` seam and by
  `containment/plugin/layoutmanager.cpp`): a pure core - plain value
  structs in, plain values out, no QObject/scenegraph/binding
  dependencies - plus, where QML must call it, a thin registered
  wrapper in the subsystem's existing C++ plugin. Tests include the
  core header directly; the existing qmltest/contract harness keeps
  driving the real shipped QML for the thin-shell wiring.
- Cutover per unit, never two live copies: the commit that lands the
  C++ core also switches the QML call site to it and deletes the QML
  body. Bisectability is the rollback story; a revert of one commit
  restores the QML logic wholesale.
- Qt5 fidelity per unit: each spec names the f0ad7b23 file the C++
  must match. Where the port already fixed a Qt5-era defect (e.g.
  ad9b823f's loop termination), the spec says which behavior wins and
  why; everything else matches Qt5 exactly, tested by cases derived
  from reading the Qt5 body at execution time.
- No bandaids carried over: where the QML logic "works" via a polling
  timer, silent early-return, or value-hiding clamp, the spec flags it
  as a defect to fix during extraction. The known inventory of
  assessed silent guards is in docs/session-handoff.md (the 2026-07-15
  loops/degenerate-indexes sweep); specs reference it rather than
  re-litigating each guard.

## A. QML logic inventory

Classification taxonomy: geometry-math / state-machine / ordering /
model-transform / event-routing / pure-presentation. Size is the
extractable behavioral-logic volume: S under 40 lines, M 40-150, L
over 150. Verdict BEHAVIORAL means extraction candidate (section B/C
decides extract vs pin-in-place); PRESENTATIONAL means leave in QML.
File classifications were produced by four read-only inventory
subagent sweeps over every file; function names quoted here were
re-verified by grep in the main session wherever a spec cites them.
Line numbers appear only in section C, individually verified.

### containment/ (62 files, 13159 lines)

Behavioral files:

| File (containment/package/contents/ui/) | Lines | Categories | Size |
| --- | --- | --- | --- |
| main.qml | 1232 | geometry-math, state-machine, event-routing, ordering | L |
| VisibilityManager.qml | 661 | geometry-math, state-machine | L |
| BindingsExternal.qml | 399 | geometry-math, model-transform | L (borderline; most bindings are passthrough) |
| DragDropArea.qml | 206 | event-routing, state-machine | M |
| applet/AppletItem.qml | 1122 | geometry-math, state-machine, ordering, event-routing | L |
| applet/ItemWrapper.qml | 742 | geometry-math, state-machine | L |
| applet/ParabolicArea.qml | 244 | geometry-math, event-routing | L |
| applet/EventsSink.qml | 203 | geometry-math, event-routing | M (borderline) |
| applet/HiddenSpacer.qml | 72 | geometry-math | S (borderline) |
| applet/IndicatorLevel.qml | 77 | geometry-math, event-routing | S (borderline) |
| applet/ShortcutBadge.qml | 102 | model-transform | S (borderline) |
| applet/communicator/Actions.qml | 56 | event-routing | S (borderline) |
| abilities/AutoSize.qml | 258 | geometry-math, state-machine, event-routing | L |
| abilities/Metrics.qml | 103 | geometry-math | M |
| abilities/ParabolicEffect.qml | 38 | geometry-math | S (borderline) |
| abilities/Layouter.qml | 72 | state-machine, event-routing | S |
| abilities/Indexer.qml | 64 | ordering, model-transform | M |
| abilities/Animations.qml | 53 | state-machine | S (borderline) |
| abilities/PositionShortcuts.qml | 52 | event-routing | S |
| abilities/Indicators.qml | 125 | model-transform | M (borderline; capability probing) |
| abilities/privates/IndexerPrivate.qml | 312 | ordering, model-transform | L |
| abilities/privates/LayouterPrivate.qml | 440 | geometry-math, ordering | L |
| abilities/privates/layouter/AppletsContainer.qml | 214 | model-transform, ordering | L |
| abilities/privates/MetricsPrivate.qml | 144 | geometry-math | M |
| abilities/privates/ParabolicEffectPrivate.qml | 158 | state-machine, event-routing | L |
| abilities/privates/AnimationsPrivate.qml | 64 | model-transform | S |
| abilities/privates/LaunchersPrivate.qml | 113 | model-transform | M (borderline; duplicated 3-layout scans) |
| abilities/privates/MyViewPrivate.qml | 95 | model-transform, state-machine | M (borderline) |
| abilities/privates/PositionShortcutsPrivate.qml | 78 | model-transform | S (borderline) |
| abilities/privates/ThinTooltipPrivate.qml | 56 | model-transform | S (borderline) |
| background/MultiLayered.qml | 951 | geometry-math, state-machine | L (the ~300-line states block is presentational) |
| background/types/Paddings.qml | 40 | geometry-math | S (borderline) |
| background/types/Shadows.qml | 49 | geometry-math | S (borderline) |
| background/types/Totals.qml | 51 | geometry-math | S (borderline) |
| colorizer/Manager.qml | 206 | state-machine, model-transform | L |
| colorizer/CustomBackground.qml | 237 | geometry-math | M |
| editmode/ConfigOverlay.qml | 564 | geometry-math, state-machine, event-routing, ordering | L |
| layouts/LayoutsContainer.qml | 537 | geometry-math, state-machine, event-routing | L |
| layouts/EnvironmentActions.qml | 360 | geometry-math, event-routing | L |
| layouts/ParabolicEdgeSpacer.qml | 122 | geometry-math, event-routing | M |
| layouts/loaders/Tasks.qml | 102 | ordering, model-transform | M |

Presentational (leave in QML): abilities/Launchers.qml (thin proxy to
layoutsManager.syncedLaunchers), abilities/MyView.qml, abilities/Debug.qml,
abilities/ThinTooltip.qml, abilities/UserRequests.qml,
abilities/privates/IndicatorsPrivate.qml (mirror bindings),
abilities/privates/metrics/Fraction.qml, applet/PaddingsInConfigureApplets.qml,
applet/TitleTooltipParent.qml, applet/EventsSinkOriginArea.qml,
applet/colorizer/Applet.qml, applet/communicator/LatteBridge.qml,
applet/communicator/Engine.qml (logic lives in AppletIdentifier.js),
background/BackgroundProperties.qml, colorizer/KirigamiShadowedRectangle.qml,
colorizer/NormalRectangle.qml, debugger/DebugWindow.qml (861 lines of
read-only diagnostics display), debugger/Tag.qml, layouts/AppletsContainer.qml,
Upgrader.qml (one-shot v0.10 config migration), ContextMenuLayer.qml.

### plasmoid/ (36 files, 9858 lines)

Behavioral files:

| File (plasmoid/package/contents/ui/) | Lines | Categories | Size |
| --- | --- | --- | --- |
| main.qml | 1698 | state-machine, model-transform, ordering, geometry-math, event-routing | L |
| TasksExtendedManager.qml | 404 | model-transform, ordering, state-machine | L |
| PulseAudio.qml | 127 | model-transform, geometry-math | M |
| ContextMenu.qml | 909 | model-transform, event-routing, ordering | L |
| task/TaskItem.qml | 996 | state-machine, model-transform, geometry-math, event-routing | L |
| task/TaskIcon.qml | 593 | state-machine, pure-presentation | M (borderline; effect gating) |
| task/TaskMouseArea.qml | 375 | event-routing, state-machine | L |
| task/SubWindows.qml | 310 | model-transform, ordering, state-machine | L |
| task/AudioStream.qml | 147 | event-routing, geometry-math | M |
| task/ProgressOverlay.qml | 114 | model-transform | S (borderline) |
| task/animations/RealRemovalAnimation.qml | 226 | state-machine, ordering, event-routing | M |
| task/animations/ShowWindowAnimation.qml | 201 | state-machine, ordering | M |
| task/animations/RemoveWindowFromGroupAnimation.qml | 147 | state-machine, geometry-math | S |
| task/animations/NewWindowAnimation.qml | 104 | state-machine | S |
| task/animations/LauncherAnimation.qml | 113 | state-machine | S |
| taskslayout/ScrollableList.qml | 382 | geometry-math, ordering, state-machine | M |
| taskslayout/MouseHandler.qml | 264 | event-routing, ordering, model-transform | M |
| previews/ToolTipInstance.qml | 524 | model-transform, geometry-math, event-routing | L |
| previews/ToolTipDelegate2.qml | 234 | model-transform, geometry-math, event-routing | M |
| previews/ToolTipWindowMouseArea.qml | 51 | event-routing | S |
| abilities/Launchers.qml | 404 | model-transform, ordering, event-routing, state-machine | L |
| abilities/launchers/Validator.qml | 137 | ordering, model-transform | M |
| abilities/launchers/Syncer.qml | 109 | event-routing, state-machine | S |

Presentational (leave in QML): task/animations/launcher/BounceAnimation.qml,
task/animations/newwindow/BounceAnimation.qml,
task/animations/ClickedAnimation.qml, taskslayout/ScrollEdgeShadows.qml,
taskslayout/ScrollOpacityMask.qml, taskslayout/ScrollPositioner.qml,
previews/PipeWireThumbnail.qml, previews/PlasmaCoreThumbnail.qml,
AppletAbilities.qml, config/ConfigAppearance.qml (index-value combo
mapping is UI-local), config/ConfigInteraction.qml, config/ConfigPanel.qml,
and plasmoid/package/contents/config/config.qml.

### shell/ (28 files, 7720 lines)

Behavioral files:

| File (shell/package/contents/) | Lines | Categories | Size |
| --- | --- | --- | --- |
| applet/CompactApplet.qml | 444 | state-machine, geometry-math, event-routing | L |
| configuration/CanvasConfiguration.qml | 190 | geometry-math, event-routing | M |
| configuration/LatteDockConfiguration.qml | 656 | geometry-math, model-transform, state-machine, event-routing | L |
| configuration/canvas/HeaderSettings.qml | 170 | geometry-math | M |
| configuration/canvas/maxlength/RulerMouseArea.qml | 77 | geometry-math, event-routing | M |
| configuration/canvas/maxlength/Ruler.qml | 324 | geometry-math | M |
| configuration/pages/AppearanceConfig.qml | 1232 | geometry-math, model-transform, event-routing | L |
| configuration/pages/BehaviorConfig.qml | 937 | model-transform, ordering, event-routing | M |
| controls/CustomIndicatorButton.qml | 215 | model-transform, event-routing, state-machine | L |
| controls/CustomVisibilityModeButton.qml | 129 | model-transform, ordering, event-routing | M |
| controls/DragCorner.qml | 167 | geometry-math, event-routing | M |
| controls/IndicatorConfigUiManager.qml | 135 | state-machine, ordering, event-routing | M |
| controls/TypeSelection.qml | 136 | model-transform, event-routing | M |
| views/AppletDelegate.qml | 229 | event-routing | S |
| views/Panel.qml | 128 | state-machine, event-routing | M |
| views/WidgetExplorer.qml | 537 | model-transform, event-routing, state-machine | L |

Presentational (leave in QML): configuration/config.qml,
configuration/LatteDockSecondaryConfiguration.qml,
configuration/canvas/SettingsOverlay.qml,
configuration/canvas/controls/Button.qml, GraphicIcon.qml,
RearrangeIcon.qml, StickIcon.qml, configuration/pages/EffectsConfig.qml
(control-to-config plumbing), configuration/pages/TasksConfig.qml
(control-to-config plumbing; the Plasma 6 config-access route it uses
is pinned by 32df5b47), controls/InnerShadow.qml,
explorer/AppletAlternatives.qml (the 56549d73 package-local copy;
deliberately kept a minimal-diff mirror of plasma-desktop's file),
views/InfoView.qml.

### indicators/ (9 files, 1513 lines)

Behavioral: default/package/ui/main.qml (318 lines; geometry-math,
state-machine; L - mask thickness math, W3C luminance color selection,
line-style grow/shrink animation state machine),
org.kde.latte.plasma/package/ui/FrontLayer.qml (267 lines;
geometry-math, event-routing; M - clicked-animation radius math,
per-edge press-coordinate conversion),
org.kde.latte.plasma/package/ui/main.qml (145 lines; model-transform,
geometry-math; M - progress clip math, SVG prefix arrays).

Presentational: default/package/config/config.qml (percent-conversion
plumbing; carries the 33fa17d7 latteIndicator alias),
org.kde.latte.plasma/package/config/config.qml,
org.kde.latte.plasma/package/ui/AppletBackLayer.qml,
org.kde.latte.plasma/package/ui/TaskBackLayer.qml,
org.kde.latte.plasmatabstyle/package/ui/BackLayer.qml,
org.kde.latte.plasmatabstyle/package/ui/main.qml.

### declarativeimports/ (104 files, 7671 lines)

declarativeimports/core is C++ only (no QML). Behavioral files:

| File (declarativeimports/) | Lines | Categories | Size |
| --- | --- | --- | --- |
| components/ComboBox.qml | 476 | model-transform, event-routing, geometry-math | M |
| components/BadgeText.qml | 179 | geometry-math, event-routing | M |
| components/Slider.qml | 127 | geometry-math | S |
| components/SpriteRectangle.qml | 113 | geometry-math | M |
| components/GlowPoint.qml | 344 | geometry-math | S |
| components/ComboBoxButton.qml | 157 | event-routing | S |
| components/TextField.qml | 129 | state-machine, geometry-math | S |
| components/IndicatorItem.qml | 138 | state-machine | S |
| components/ShadowedItem.qml | 50 | geometry-math (delegates to code/EffectMath.js) | S |
| abilities/bridge/PositionShortcuts.qml | 53 | event-routing | S |
| abilities/bridge/ParabolicEffect.qml | 42 | event-routing | S |
| abilities/bridge/Launchers.qml | 37 | event-routing | S |
| abilities/client/Indexer.qml | 243 | ordering, model-transform | L |
| abilities/client/ParabolicEffect.qml | 201 | event-routing, state-machine | M |
| abilities/client/indicators/LatteIndicator.qml | 307 | geometry-math, state-machine | M |
| abilities/client/AppletAbilities.qml | 152 | model-transform, ordering | S |
| abilities/client/PositionShortcuts.qml | 69 | ordering | S |
| abilities/client/UserRequests.qml | 48 | event-routing | S |
| abilities/client/Requirements.qml | 62 | event-routing | S |
| abilities/definition/ParabolicEffect.qml | 82 | geometry-math, ordering | M (the parabolic math core) |
| abilities/definition/animations/Tracker.qml | 26 | model-transform | S |
| abilities/host/ThinTooltip.qml | 135 | state-machine, event-routing | M |
| abilities/host/Containment.qml | 46 | ordering | S |
| abilities/items/BasicItem.qml | 444 | ordering, state-machine, geometry-math, event-routing | L |
| abilities/items/basicitem/ParabolicItem.qml | 285 | geometry-math, state-machine, event-routing | L |
| abilities/items/basicitem/ParabolicEventsArea.qml | 225 | event-routing, geometry-math, ordering | L |
| abilities/items/IndicatorObject.qml | 119 | state-machine | S |
| abilities/items/basicitem/HiddenSpacer.qml | 102 | geometry-math, state-machine | S |
| abilities/items/basicitem/ShortcutBadge.qml | 88 | ordering | S |
| abilities/items/basicitem/IndicatorLevel.qml | 54 | event-routing, geometry-math | S |

Presentational (leave in QML), 74 files: components/HeaderSwitch.qml,
ItemDelegate.qml, ExternalShadow.qml, AddItem.qml, ScrollArea.qml,
SpinBox.qml, Header.qml, SubHeader.qml, Label.qml, CheckBox.qml,
CheckBoxesColumn.qml, Switch.qml, ToolTip.qml, AddingArea.qml, all six
components/private/ files, abilities/bridge/BridgeItem.qml, Indexer.qml,
Animations.qml, MyView.qml, ThinTooltip.qml, abilities/client/
Animations.qml, MyView.qml, Metrics.qml, Indicators.qml, ThinTooltip.qml,
Containment.qml, Debug.qml, Environment.qml, all three
appletabilities/Container*Bindings.qml, indicators/LatteConfiguration.qml,
all 27 abilities/definition/ interface files except the two behavioral
ones above, all abilities/host/ publicApi surfaces except ThinTooltip
and Containment, abilities/items/basicitem/SeparatorItem.qml,
TitleTooltipParent.qml, RestoreAnimation.qml,
abilities/items/IndicatorLevel.qml, indicators/LevelOptions.qml.

### JS logic files addendum (11 files, 1206 lines)

The .qml sweeps exclude imported .js libraries; they are part of the
same extraction surface:

- containment/package/contents/code/autosize.js (58) - shrinkStep/
  growStep math for EX-04.
- plasmoid/package/contents/code/layout.js (193) - plasmoid layout
  helpers.
- plasmoid/package/contents/code/tools.js (121) - task helper
  predicates.
- plasmoid/package/contents/code/TaskActions.js (56) - task action
  token dispatch tables.
- plasmoid/package/contents/code/activitiesTools.js (357) - launcher
  activity migration helpers.
- containment/package/contents/code/AppletIdentifier.js (304) -
  applet-specific icon discovery heuristics.
- three copies of ColorizerTools.js (34+34+28: declarativeimports/
  components/code/, containment/package/contents/code/,
  plasmoid/package/contents/code/) - the luminance math EX-19
  deduplicates.
- declarativeimports/components/code/EffectMath.js (10) - shadow blur
  curve.
- containment/package/contents/code/MathTools.js (11).

## B. Hot-spot ranking

Three axes. Bug-density counts verified fix commits whose diffs touch
the unit's logic (hashes cited; counted over f0ad7b23..HEAD with
`git log --grep='^fix' --name-only`). Testability-gain estimates how
much currently-unpinnable behavior becomes table-testable.
Feel-risk orders live-verification weight; high feel-risk sequences
later within its wave and gets a mandatory live recipe.

1. Preview adoption/anchoring pipeline (plasmoid main.qml previews
   block, TaskItem preview functions). Bug density 15+, the densest in
   the tree, all 2026-07-13..15: c6eeeb20, 4f96acb8, 4b533b8d,
   54ed1974, 0913bbee, 235753b8, d56a26aa, f1edd103, d619ae08,
   15558f40, e6c5ae76, c622da1b, d98bff98, 77aac4b4, df747ebf. Ten
   line-level invariants currently pinned only by grep
   (scripts/preview-contract-rules.sh, b4f5621c). Testability-gain:
   highest. Feel-risk: highest (hover feel, measured in ms).
   -> EX-01, strong-model-only.
2. Parabolic zoom engine (definition/ParabolicEffect.qml math;
   propagation chains in ParabolicEventsArea/ParabolicArea/
   ParabolicEdgeSpacer/ParabolicEffectPrivate). Direct fix density low
   in this port (the zoom math itself is untouched since fork), but
   the propagation index arithmetic is the exact class the
   loops/degenerate-values sweep hunted, the glide-vs-jump
   verification hazard cost hours of phantom flakiness (2026-07-15,
   recorded in latte-live-verification), and every hover bug transits
   this code. Feel-risk: maximum of the whole plan.
   -> EX-02 (router, strong-model-only) + EX-03 (math, delegate-safe).
3. AutoSize feedback loop (abilities/AutoSize.qml + code/autosize.js).
   Density: ad9b823f (infinite loop, 100% CPU hang, inherited
   upstream defect from 747d4870-era code). Already partially pinned
   by tests/qml/tst_autosize.qml. Pure math + bounded history: very
   high testability. -> EX-04, delegate-safe.
4. Fill/Justify length distribution (LayouterPrivate.qml). Zero direct
   fixes in our tree, but latte-dock-ng fixed a dock collapse in the
   same inherited algorithm (ng 30637c1cd) and the two-pass
   distribution is exactly the shape unit tests eat. -> EX-05,
   delegate-safe.
5. Visible-index and separator-neighbor ordering (IndexerPrivate.qml,
   client Indexer.qml twin, AppletItem/BasicItem neighbor walks).
   The 2026-07-15 loops sweep verified all these while-loops terminate
   (clean negatives, recorded in session-handoff) but nothing pins the
   RESULTS; the twins have already drifted apart structurally.
   -> EX-06, delegate-safe.
6. Storage id remapping (app/layouts/storage.cpp, C++). Density:
   fa02b887 (containments destroyed during template import; its
   liveness filter is a self-admitted band-aid with the deleter still
   unidentified), plus the whole duplicate-flow saga rode this path
   (e412889d investigation). capt extracted this exact unit
   (73f64383). -> EX-07, delegate-safe.
7. Available screen geometry (app/lattecorona.cpp, C++). Density:
   1b932ed9 (settings window overflow; our fix deliberately DIVERGES
   from upstream d30143f7 by accepting self-origin updates - the
   extraction must preserve that deviation). capt blueprint:
   screengeometrycalculator (with tests). -> EX-08, delegate-safe.
8. Positioner geometry (app/view/positioner.cpp, C++). Density: 3
   fixes (793faad2 moveToScreen remap, c5bdc239 late screen id,
   1607d022 family). capt blueprint: 4a829185. Our architecture
   note: on Wayland much placement authority moved to layer-shell
   anchors (app/wm/waylandlayershell.cpp), so the pure math matters
   mainly for X11, masks, and the canvas/edit chrome rects.
   -> EX-09, delegate-safe.
9. Visibility mask + input geometry (containment
   VisibilityManager.qml). Related family: the canvas input-region
   work (3d714d63, dbe5a03b) lives in C++ already; the QML half
   computes the dock's own mask/input rects. Errors here are
   invisible-dock / dead-input bugs. -> EX-10, delegate-safe with a
   heavy live recipe.
10. Launcher ordering complex (abilities/Launchers.qml,
    launchers/Validator.qml, TasksExtendedManager.qml). Density:
    d6d57e61 (stale synced-launcher clients crash); the Validator's
    upwardIsBetter -1-splice heuristic is in the assessed-guards
    inventory. Pure list algebra throughout. -> EX-11, delegate-safe.
11. Colorizer decision tree (colorizer/Manager.qml). The color complex
    (1f835402, 5c06b497, 79ca3360) was effects- and measurement-side,
    but every one of those investigations had to re-derive this
    QML decision tree to reason about expected behavior. -> EX-12,
    delegate-safe.
12. viewType/background predicate chain (containment main.qml,
    MultiLayered.qml). Density: 38e60eb9, f5a5f44c, d72ee0cd (the
    edit-mode background family) plus the recurring throwaway-layout
    confusion (viewType=1 rendering full-width background, mistaken
    for a regression twice in session-handoff). -> EX-13,
    delegate-safe.
13. Drop classification and insert index (DragDropArea.qml,
    MouseHandler.qml). Density: b474adad (the DropArea dead-handler
    trap was found here). -> EX-14, delegate-safe.
14. Wheel semantics (AudioStream.qml, TaskMouseArea.qml,
    EnvironmentActions.qml, RulerMouseArea.qml). Density: 299a241b
    (audio wheel matched to plasma-pa exactly, hand-verified).
    -> EX-15, delegate-safe.
15. Remaining pure-transform tail, all delegate-safe: EX-16 group
    cycling (SubWindows.qml + loaders/Tasks.qml), EX-17 preview
    title/subtext transforms (ToolTipInstance.qml), EX-18
    maxLength/offset clamp dedup (RulerMouseArea.qml vs
    AppearanceConfig.qml, two live copies of the same math), EX-19
    luminance dedup (five copies counted), EX-20 badge math, EX-21
    scroll overflow math, and the four remaining capt C++ blueprints
    EX-22..EX-25.

De-prioritized (high visibility, low extraction value): purely-drawing
QML (the states blocks, gradients, shadows), the ability
bridge/host/definition relay layers (property plumbing, no logic), the
settings pages' control-to-config plumbing (single-loader doctrine
already pinned by 32df5b47/c3d15966 fixes and their tests).

Pin-in-place verdicts (behavioral in the inventory, but extraction is
the wrong tool; each becomes a test-only task, not a backlog unit):

- shell CompactApplet.qml popup sizing/representation chain. Fix
  density is real (437d9a0c, 1aa5238c, 9ea29eaa, 5f8c10be, d12baff2)
  but every fix was about matching libplasma's live binding/parenting
  contracts, which is inherently scenegraph-coupled; the chain is
  already pinned by tests/qml/tst_compactapplet.qml (3b37750b) driving
  the real shipped file. Extracting the arithmetic would leave the
  risk (the wiring) in QML and fight the existing pin. Task: extend
  tst_compactapplet when the chain changes; nothing to extract.
- plasmoid ContextMenu.qml (909 lines). Menu assembly against live
  PlasmaExtras.Menu/TasksModel APIs; the one algorithmic piece (the
  eliding while-loop) was verified terminating in the loops sweep. The
  practical hazards here have been API-contract ones (52c2987b menu
  teardown, d67e635a/56549d73 alternatives chain), each now fixed at
  its origin. Task: qmltest contract for loadDynamicLaunchActions
  section assembly if churn resumes.
- editmode/ConfigOverlay.qml drag/reorder. Binding-entangled
  (hoveredItem hit-testing, live reparenting, input-mask re-carve
  8be2b388); recent fixes (36160c46, 8f821310) are stable and
  live-verified. Extraction would need a designed seam for the
  drag session state; flagged design-first in section F, not forced.
- TaskItem slotPublishGeometries. Geometry clamp math feeding
  libtaskmanager; depends on live item mapping (mapToGlobal) per
  frame. Task: add invariant assertions to a qmltest against the real
  TaskItem (bounds containment, hidden-view collapse).
- components/ComboBox.qml role resolution. Already fixed and
  regression-tested (a302d742 covers the three model kinds).
- LatteDockConfiguration.qml window size negotiation. Chrome-only,
  stable since 1b932ed9 fixed the C++ availability side.
- class-A stranded-binding reasserts (e412889d, eca51ae0 and the
  eca51ae0-family reassert functions in plasmoid main.qml). These are
  QML binding lifecycle countermeasures, not extractable logic; the
  open question (what destroys the bindings) is a filed watch item in
  the plan. Extraction note: EX-units that absorb the values these
  bindings feed (EX-10 mask geometry) reduce the surface, which is the
  real fix direction.

## C. Per-unit extraction specs

Written in rank order. Every line range below was verified by grep at
HEAD 5e1c2b12 (bab18b2c for the plan itself); Qt5 anchors were verified
against `git show f0ad7b23:<path>`. Where a Qt5 anchor is given as a
function name without lines, the executor reads that function at
execution time; a spec never carries an unverified line number.

Conventions used by all specs:
- "Core" means a pure C++ class/namespace: value structs in, values
  out, no QObject, no QQuickItem, no KConfig, no timers, no clock
  reads (time arrives as a parameter). Cores live next to their
  consumers: `containment/plugin/units/`, `plasmoid/plugin/units/`
  (new subdirectories, Latte-authored provenance headers so the
  plasma-desktop vendor-sync diff stays clean), `declarativeimports/
  core/units/`, or the app subtree for C++-to-C++ extractions
  (capt's placement, e.g. `app/layouts/`).
- "Shell" means the thin QML-facing wrapper (registered QObject or
  attached call sites) that feeds the core and applies its outputs.
  QML keeps ownership of timers, bindings, and scenegraph items; the
  core owns decisions and math.
- Tests go in `tests/units/<unit>test.cpp` (new ctest entries wired
  like the existing flat tests in tests/CMakeLists.txt), plus qmltest
  contract coverage where the QML shell wiring itself is the risk.
- Every unit's cutover commit deletes the QML logic body it replaces
  in the same commit (single-copy rule); rollback is one revert.

### EX-01 PreviewSwitchEngine [strong-model-only]

- Header: `plasmoid/plugin/units/previewswitchengine.h`
- Responsibility: the window-previews dialog's decision core - switch
  vs defer vs settle-adopt, hide-countdown arming/cancelling, and the
  parked-delegate LRU accounting - as one testable state machine.
- Source (verified): plasmoid/package/contents/ui/main.qml:440-476
  `materializeDelegateFor` (LRU: revive parked, build on miss, park +
  evict-oldest), 482-490 `dropCachedDelegateFor`, 518-544
  `shouldDeferSwitch` (burst debounce keyed on request cadence),
  566-631 `windowsPreviewDlg.hide`/`show`, 640-653
  `previewSwitchSettleTimer`, 656-673 `hidePreviewWinTimer`
  (composite contains-mouse test); plasmoid/package/contents/ui/task/
  TaskItem.qml:472-491 `showPreviewWindow`, 499-576
  `preparePreviewWindow` (cache-hit rootIndex refresh vs
  fresh-instance binding cluster).
- Extract-vs-pin: this is the one unit where the pin already exists
  and is still not enough. scripts/preview-contract-rules.sh
  (b4f5621c) pins ten line-level invariants by grep - it defends line
  ORDER, not semantics, and it broke new ground precisely because the
  logic is unhosted by any test harness. Fifteen fix commits in three
  days (section B, rank 1) is the strongest extraction signal in the
  tree. GATE INTERACTION, stated per the planning contract: the
  extraction deliberately migrates gate rules 1-4 (defer-before-
  prepare ordering, defer-cancels-hide, settle-adopts-directly,
  settle-interval-below-threshold) into behavioral C++ tests with an
  injected clock, and REWRITES those gate rules to instead assert the
  QML shell delegates to the engine (grep for the engine call). Rules
  that pin QML-side mechanics that stay in QML (rootIndex assigned
  after isGroup with the refresh-token bump, thumbnail icon fallback
  strictness, imperative size enforcement) remain in the gate
  untouched. No commit may leave an invariant unpinned in both
  places; each migration commit moves rule and test together.
- Interface / DI seam:
  - `struct SwitchRequest { int taskId; qint64 nowMs; bool dialogVisible; }`
  - `enum class SwitchDecision { ShowNow, Defer }` from
    `decide(SwitchRequest)`; deferred state carries the pending task;
    `settle(qint64 nowMs)` returns the task to adopt (the settle
    timer stays a QML Timer armed with the engine's interval).
  - `enum class HideDecision { StartCountdown, CancelCountdown, HideNow }`
    from `hoverChanged(HoverSnapshot)` where HoverSnapshot is the
    composite the hide timer currently reads (dialog contains-mouse,
    task contains-mouse, drag active).
  - LRU: `enum class Materialize { Revive, Build, BuildEvicting }`,
    `materialize(taskId) -> { Materialize kind; int evictTaskId; }`,
    `drop(taskId)`, capacity injected (4, from f1edd103).
  - The engine never touches the DelegateModel or the dialog; QML
    applies decisions. Timer intervals are engine constants exposed
    read-only so QML cannot drift from the tested thresholds
    (today: settle interval vs switchBurstThreshold, gate rule 4).
- capt cross-reference: none - capt has no preview-pipeline
  extraction; its tasks work is backend-side. This unit is ours alone.
- Test plan (tests/units/previewswitchenginetest.cpp): burst-boundary
  table (crossing cadence just under/over the threshold; the 120ms vs
  140ms degeneration bug from 4b533b8d's first cut as a named case);
  defer path cancels an armed hide countdown (54ed1974's regression);
  settle adopts the LAST hovered task and never re-enters the burst
  check (4b533b8d); hide-countdown composite (any contains-mouse
  cancels; drag blocks hide); LRU: revive keeps warm entries, build
  at capacity evicts the oldest, drop removes without eviction
  side-effects (f1edd103's Component.onDestruction contract);
  interleavings: defer -> exit-all -> settle must not show over
  nothing (d56a26aa family). Tests precede the extraction seam:
  written against the spec tables first, the engine implemented to
  green them.
- Qt5-fidelity: Qt5 main.qml:366-421 (hide at :366, show at :384,
  verified) is the ancestor - immediate show/hide with previewsDelay
  hover gating and no debounce/LRU (those are port-era responses to
  measured Qt6/Wayland rebuild costs: 100-400ms per adoption,
  4b533b8d body). Fidelity target is Qt5's OBSERVABLE behavior -
  previews follow the hovered task, previewsDelay honored, hide on
  exit - with the port's performance machinery preserved exactly as
  the fix commits measured it. Deviations are already documented in
  c6eeeb20..f1edd103 bodies; the spec adds none.
- Live verification (mandatory, feel-bearing): fakepointer GLIDE
  sweeps (~8px steps) across 4+ tasks at slow/medium/fast rates -
  never coordinate jumps (parabolic shift makes jumps miss enters;
  lesson recorded in latte-live-verification). Verify: single
  adoption at rest per sweep (event-loop lag probe or log counter),
  dialog never hides mid-scrub under the pointer, konsole 4-window
  group -> firefox -> konsole revives all four thumbnails
  (0913bbee's recipe), preview centered on the hovered icon
  (screenshot), zero attach(nil) on the wire across a stepped sweep
  (c6eeeb20's check, WAYLAND_DEBUG counter).
- Delegation tag: strong-model-only. The decisions being extracted
  are exactly the ones that took seven measured excavation layers to
  get right; encoding them wrong but plausibly is cheap for a weaker
  model and expensive to detect.
- Risk + rollback: highest-feel unit in the plan. Land as (1) core +
  tests, (2) QML cutover + gate migration - two commits, each
  revertable alone; the gate keeps the old rules until (2) so there
  is no unpinned window.

### EX-02 ParabolicRouter [strong-model-only]

- Header: `declarativeimports/core/units/parabolicrouter.h` (serves
  both containment and plasmoid sides through org.kde.latte.core).
- Responsibility: replace the recursive neighbor-to-neighbor
  scale-propagation signal chains with one computed per-item scale
  assignment for the whole row.
- Source (verified): containment/package/contents/ui/applet/
  ParabolicArea.qml:167-222 `sltUpdateItemScale` (slice/splice of the
  neighbour scales array, clear-request propagation up/down, bridge
  forwarding), 139-155 `calculateParabolicScales`;
  declarativeimports/abilities/items/basicitem/
  ParabolicEventsArea.qml:126-152 `calculateParabolicScales`, 154-204
  `updateScale`/propagation twins; containment/package/contents/ui/
  layouts/ParabolicEdgeSpacer.qml (55-110 per the inventory,
  accept/clear by tail/head + alignment); containment/package/
  contents/ui/abilities/privates/ParabolicEffectPrivate.qml (restore
  zoom state machine).
- Extract-vs-pin: the propagation is duplicated in two structurally
  drifted twins (containment applets vs plasmoid tasks), rides on
  index arithmetic of exactly the class the loops/degenerate-values
  sweep hunted, and crosses the applet bridge (clientsBridges) where
  sub-index handoff is hand-maintained. Pinning in place would need
  the full matrix of (edge spacers x separators x hidden x bridges)
  driven through real QML - strictly harder than testing a pure
  assignment function. Extraction also removes the per-hop signal
  latency structure, but BEHAVIOR must stay identical (see fidelity).
- Interface / DI seam: `struct RowItem { int index; bool isSeparator;
  bool isHidden; int subItemCount; }` (sub-items model bridge
  clients); `assignScales(rowItems, currentIndex, currentItemScales)
  -> QVector<float>` full-row assignment including edge-spacer
  absorption; a `clearAssignment(rowItems)` counterpart. QML shells
  (both twins) feed their row snapshot and apply the returned vector
  to zoomScale properties; the existing signals survive only as the
  application mechanism inside each shell, not as inter-item routing.
  DESIGN-FIRST note: how the bridge clients receive their slice
  (today: forwarded signals into the applet's internal indexer) needs
  a designed handoff before coding; this is half the reason for the
  strong-model tag.
- capt cross-reference: none. capt did not touch parabolic
  propagation.
- Test plan: equality harness first - an offscreen qmltest drives the
  EXISTING chain over a synthetic row (no live dock; the recipe for
  constructing ability object graphs offscreen is in
  tests/contracts, per session-handoff's tooling note) and records
  the resulting scale vectors for a case table (pointer at first/mid/
  last item, separators adjacent, hidden runs, spacer edges, bridge
  client mid-row); the C++ core must reproduce those vectors exactly.
  Then unit tests own the table and the qmltest shrinks to shell
  wiring.
- Qt5-fidelity: the chain shape is Qt5-inherited; fidelity = equal
  scale assignments for equal inputs, which the equality harness
  makes mechanical. Read f0ad7b23's ParabolicArea equivalents when
  building the case table so Qt5-era cases (no margins-area
  separators, e.g.) are represented.
- Live verification (mandatory): glide sweeps along a mixed row
  (tasks + separator + applets + edge), verifying visually smooth
  zoom with no popping neighbors; before/after screenshots at
  identical pointer positions must match within antialiasing noise;
  a fast zigzag glide (36160c46's drag-drift recipe adapted) checks
  direct-rendering engagement still triggers on rapid index jumps.
- Delegation tag: strong-model-only (maximum feel risk + the bridge
  handoff design). If the strong-model window closes before this
  lands: DEFER with this marker intact; do not delegate.
- Risk + rollback: feel regression of the signature interaction of
  the whole dock. Land core+harness first (no behavior change), then
  one cutover commit per twin (containment, plasmoid), each
  independently revertable.

### EX-03 ParabolicMathCore [delegate-safe]

- Header: `declarativeimports/core/units/parabolicmath.h`
- Responsibility: the zoom curve itself - per-item scale from pointer
  position - as pure functions.
- Source (verified): declarativeimports/abilities/definition/
  ParabolicEffect.qml:37-66 `applyParabolicEffect` (left/right scale
  stacks from mouse percentage, RTL swap), 68-76 `scaleForItem`,
  78-81 `scaleLinear`.
- Extract-vs-pin: S-size pure math, but it feeds EX-02 and is
  duplicated nowhere; extracting it first gives EX-02 a tested
  foundation and the equality harness a reference. Cheap, safe,
  foundational.
- Interface: `computeScales(double mousePosPercent, int itemIndex,
  int itemsCount, double zoomFactor) -> { QVector<double> left,
  right; }` plus `scaleForItem`/`scaleLinear` equivalents. The QML
  definition file keeps its signal emissions and calls the core for
  numbers only.
- capt cross-reference: none.
- Test plan (tests/units/parabolicmathtest.cpp): equivalence table
  against the current QML implementation (drive the QML function
  offscreen via qmltestrunner once, bake the numbers into the table
  with a comment naming the generation method); boundary cases:
  zoom=1 (all ones), single item, pointer at 0/50/100 percent, RTL
  swap symmetry (left(p) == mirror(right(1-p))).
- Qt5-fidelity: f0ad7b23 declarativeimports/abilities/definition/
  ParabolicEffect.qml:36 `applyParabolicEffect` and :67
  `linearEffect` (verified; the port renamed linearEffect to
  scaleForItem/scaleLinear - the executor confirms numeric
  equivalence of the renamed pair against the Qt5 body before
  writing the table).
- Live verification: one glide pass on the real dock after cutover;
  zoomed-row screenshot at a fixed pointer position matches
  pre-cutover capture.
- Delegation tag: delegate-safe. Pure math, equivalence-tested, no
  design decisions.
- Risk + rollback: minimal; one commit, one revert.

### EX-04 AutoSizeEngine [delegate-safe]

- Header: `containment/plugin/units/autosizeengine.h`
- Responsibility: the automatic icon-size search - shrink/grow step
  selection, oscillation detection, prediction history - as a pure
  step function.
- Source (verified): containment/package/contents/ui/abilities/
  AutoSize.qml:134-151 `producesEndlessLoop`, 154-242
  `updateIconSize`, 117-132 history ring (`clearHistory`/
  `addPrediction`), plus containment/package/contents/code/
  autosize.js (58 lines, `shrinkStep`/`growStep`; port-era file,
  absent at f0ad7b23 - verified).
- Extract-vs-pin: tests/qml/tst_autosize.qml already pins pieces
  against the shipped QML, and the maxLength<=0 early-return carries
  its deliberate-contract comment (ad9b823f). But the core is a
  feedback loop whose failure mode is a 100% CPU hang (the ad9b823f
  incident: latent upstream loop-termination defect armed by
  Wayland's late geometry), and loops that can hang the GUI thread
  belong in C++ where the full input space is table-testable.
  tst_autosize.qml remains as the shell pin.
- Interface: `struct AutoSizeInput { int layoutLength; int maxLength;
  int currentIconSize; int maxIconSize; double zoomFactor; bool
  normalState; }` -> `struct AutoSizeStep { int nextIconSize; bool
  found; }` via `step(input, History &history)`; History is a value
  type with the bounded ring (historyMinSize/historyMaxSize
  semantics preserved).
- capt cross-reference: none (capt's appletzoomsizetest is adjacent
  but backend-side).
- Test plan: the ad9b823f regression as a named case (iconSize=78
  layout from its commit body: termination for ANY icon size -
  property-style loop over sizes 16..256 asserting the search
  terminates and result is within bounds); oscillation detector
  truth table (history[1] vs prediction per producesEndlessLoop);
  asymmetric shrink/grow limits (the comments in AutoSize.qml warn
  about the oscillation the asymmetry prevents - encode as a test
  that a grow immediately after a shrink at the boundary is
  rejected); maxLength<=0 remains OUTSIDE the core (the QML contract
  comment and its onMaxLengthChanged re-run stay; core asserts
  maxLength>0 via Q_ASSERT so misuse is loud).
- Qt5-fidelity: f0ad7b23 containment/package/contents/ui/abilities/
  AutoSize.qml:151 `updateIconSize`, :131 `producesEndlessLoop`
  (verified). DEVIATION, named: ad9b823f's termination guarantee wins
  over the Qt5 body (Qt5 carries the latent infinite loop); each
  divergent branch cites ad9b823f in a comment.
- Live verification: restart the throwaway 3-dock layout with
  iconSize=78 (the ad9b823f recipe): all docks map, process idles;
  resize maxLength live via the ruler and watch the size settle
  without oscillation.
- Delegation tag: delegate-safe. The spec's tables are exhaustive
  and the hang failure mode is caught by the termination test.
- Risk + rollback: low; one core commit + one cutover commit.

### EX-05 FillLengthDistributor [delegate-safe]

- Header: `containment/plugin/units/filldistributor.h`
- Responsibility: the two-pass distribution of free length among
  fill applets (Justify two-sided and one-pool variants).
- Source (verified): containment/package/contents/ui/abilities/
  privates/LayouterPrivate.qml:74-146 `computeStep1ForLayout`,
  151-239 `computeStep2ForLayout`, 273-368
  `updateFillAppletsWithTwoSteps`, 370-415
  `updateFillAppletsWithOneStep`, 418-439 dispatcher.
- Extract-vs-pin: pure arithmetic over per-applet constraint records,
  currently executed by mutating live QML items mid-pass (the
  functions read item.metrics and assign item lengths as they go).
  Unit-testing in place requires a full ability graph; extracted, it
  is a table function. latte-dock-ng fixed a dock collapse in this
  inherited algorithm (ng 30637c1cd) - we carry the same shape.
- Interface: `struct FillItem { int minLength; int prefLength; int
  maxLength; bool isFill; }` (record -1 sentinels as std::optional
  at the boundary); `distribute(QVector<FillItem>, int
  availableSpace, Alignment alignment, SplitterInfo splitters) ->
  QVector<int>` assigned lengths. The QML layouter keeps gathering
  inputs from live items and applying results; the passes move.
- capt cross-reference: none directly; capt's
  editmodehandleactiontest and viewmodels work are elsewhere.
- Test plan: sum(assigned) <= availableSpace always; no assignment
  below min or above max; the ng 30637c1cd collapse scenario
  (fillWidth applet added to a constrained row - read ng's commit
  body for the exact trigger and encode it); all-fill,
  no-fill, single-item, zero-space, Justify halving with odd totals
  (rounding), splitter max-size participation
  (maxJustifySplitterSize semantics from the sibling
  AppletsContainer counters).
- Qt5-fidelity: f0ad7b23 containment/package/contents/ui/abilities/
  privates/LayouterPrivate.qml:74 and :151 (verified, same function
  names) - the executor diffs port-vs-Qt5 bodies first; any port-era
  drift found is a finding to record, not silently normalize.
- Live verification: Justify layout with two fill applets +
  fixed-size neighbors; add/remove an applet in edit mode and
  screenshot-verify no collapse and no overflow (the ng bug's
  symptom).
- Delegation tag: delegate-safe.
- Risk + rollback: low-medium (edit-mode add/remove paths exercise
  it); single cutover commit.

### EX-06 VisibleIndexEngine [delegate-safe]

- Header: `declarativeimports/core/units/visibleindex.h` (one core
  for both the containment and applet-side twins).
- Responsibility: visible-index mapping and separator/hidden
  neighbor predicates over an item row, including multi-item applet
  expansion.
- Source (verified): containment/package/contents/ui/abilities/
  privates/IndexerPrivate.qml:255-277 `visibleItemsBeforeCount`,
  279-289 `visibleIndex`, 291-311 `visibleIndexBelongsAtApplet`
  (plus the collector Bindings 23-253 per the inventory);
  declarativeimports/abilities/client/Indexer.qml (the drifted twin:
  firstTailItemIsSeparator/lastHeadItemIsSeparator/
  firstVisibleItemIndex/lastVisibleItemIndex/visibleIndex);
  containment/package/contents/ui/applet/AppletItem.qml:221-245
  `tailAppletIsSeparator`, 247-271 `headAppletIsSeparator`
  (while-loop neighbor walks), 486-532 `checkIndex`;
  declarativeimports/abilities/items/BasicItem.qml neighbor-walk
  twins (178-260 per the inventory).
- Extract-vs-pin: four sites hand-maintain the same ordering
  semantics; the 2026-07-15 loops sweep proved the walks terminate
  but nothing pins their RESULTS, and the twins have drifted
  structurally already. One tested core ends the drift class.
- Interface: `struct RowEntry { int index; bool isSeparator; bool
  isHidden; bool isMarginsSeparator; int subItemCount; }`;
  functions: `visibleIndexOf(entries, actualIndex)`,
  `entriesBefore(entries, actualIndex)`, `neighborIsSeparator(
  entries, actualIndex, Direction)`, `firstVisible/lastVisible`,
  `belongsAtEntry(entries, entryIndex, int visibleIndex)`. The QML
  collector Bindings keep gathering RowEntry lists (they read live
  children); all arithmetic moves.
- capt cross-reference: none for this unit.
- Test plan: separator runs at head/tail/middle; hidden interleaved
  with separators; multi-item applets (subItemCount 0/1/n) spanning
  the queried index; empty row; single separator-only row; the
  margins-area parity predicate (AppletItem `inMarginsArea`,
  inventory: parity count of preceding margin separators) as its own
  case family; cross-check containment and client twins give
  identical answers for identical inputs (the drift regression
  test).
- Qt5-fidelity: f0ad7b23 IndexerPrivate.qml:249/:273/:285 (verified
  same functions). Diff port-vs-Qt5 first, record drift findings.
- Live verification: shortcut badges (Meta+number ordering) on a
  row containing separators + a systray; badge numbers must match
  visible positions (PositionShortcuts consumes visibleIndex).
- Delegation tag: delegate-safe.
- Risk + rollback: low; per-twin cutover commits (containment,
  client) so a regression bisects to one side.

### EX-07 StorageIdRemapper [delegate-safe]

- Header: `app/layouts/storageidremapper.h` (capt's placement).
- Responsibility: the id-assignment math of layout import/duplicate -
  which containment/applet ids get remapped to what - lifted out of
  Storage::newUniqueIdsFile's KConfig surgery.
- Source (verified in OUR tree): app/layouts/storage.cpp:269
  `Storage::availableId`, :327 `Storage::newUniqueIdsFile`. This is
  a C++-to-pure-C++ extraction; no QML changes.
- Extract-vs-pin: the duplicate/import flow is the spine of
  newView/duplicateView/template import and its only current test
  coverage is importerregressiontest (name collision suffixes, not id
  math). fa02b887's liveness filter in importLayoutFile is a
  self-admitted band-aid (deleter unidentified, noted in
  session-handoff's guard inventory); a tested remap core is the
  prerequisite for root-causing that properly instead of guarding.
- Interface: capt's shape verbatim as a starting point (verified at
  capt 81384003, app/layouts/storageidremapper.h): `IdRemapInput {
  usedIds, containmentIds, appletIds }` -> `IdRemap { QHash old->new;
  mapped() passthrough for unknown keys }`; constants
  CONTAINMENTIDBASE=12, APPLETIDBASE=40, MAXID=32000 exhaustion cap.
  OUR DIVERGENCES from capt, mandatory: (1) our storage.cpp also
  rewrites stringly id LISTS (appletOrder, lockedZoomApplets,
  userBlocksColorizingApplets - named in latte-architecture) and the
  isClonedFrom clone reference (ISCLONEDNULL sentinel) - the remap
  application helper must cover list-valued keys and the clone
  field, which capt's input struct does not model; (2) exhaustion
  returning "" must FAIL LOUDLY at the call site (qCritical + abort
  the import), not write empty ids - per the no-silent-failure rule.
- capt cross-reference: capt 73f64383 "Extract storage id-remap
  assignment into a testable unit" (+ e3fc02e1 routing commit).
- Test plan: port capt's 11 verified test slots
  (storageidremappertest.cpp at 81384003): availableId first-gap /
  exhaustion; remap keeps high free ids, reassigns low ids, reassigns
  colliding used id, containments-before-applets distinct ranges,
  applet allocation skips containment assignments, the "PROBLEM
  APPEARED" 2-cycle fix does not corrupt the non-cycle case, empty id
  gets fresh id, mapped() passthrough, order-key unknown token. ADD
  ours: list-valued key rewriting (appletOrder round-trip), clone
  reference remap, exhaustion is loud.
- Qt5-fidelity: f0ad7b23 app/layouts/storage.cpp:269/:327 (verified;
  Qt5 uses literal base 12 at :406). The algorithm is Qt5-inherited;
  diff our current body against Qt5 first and record drift.
- Live verification: dbus duplicateView on the throwaway layout
  (recipe in latte-live-verification section 7 plus the undo-window
  trap in 3b), then verify the new containment's ids in the layout
  file are collision-free and appletOrder references the new ids.
- Delegation tag: delegate-safe (capt blueprint + case list + no
  QML).
- Risk + rollback: low; behavior-preserving refactor commit + test
  commit.

### EX-08 ScreenGeometryCalculator [delegate-safe]

- Header: `app/screengeometrycalculator.h` (capt's placement).
- Responsibility: availableScreenRect/availableScreenRegion math -
  the screen area left free of docks - as a pure function over view
  footprints.
- Source (verified in OUR tree): app/lattecorona.cpp:594
  `availableScreenRegionWithCriteria`, :798
  `availableScreenRectWithCriteria`.
- Extract-vs-pin: this math fed the settings-window overflow saga
  (1b932ed9: the +99px cold-start overflow) and the Phase 8
  reserved-area-lag residual filed there; it runs against live View
  pointers today so every test needs a corona. capt proved the
  footprint-snapshot seam works.
- Interface: capt's shape (verified at capt 81384003,
  app/screengeometrycalculator.h): `ViewFootprint { location,
  formFactor, alignment, visibilityMode, hasVisibility, isOffScreen,
  behaveAsPlasmaPanel, normalThickness, screenEdgeMargin, maxLength,
  offset, geometry }`; `availableRect(startRect, screenGeometry,
  footprints, ignoreModes, ignoreEdges, desktopUse)` and
  `availableRegion(...)`. OUR DIVERGENCE, mandatory: 1b932ed9
  deliberately accepts SELF-ORIGIN updates (upstream d30143f7
  excluded the origin view; our fix reverted that exclusion so a
  view's own reserved thickness corrects its chrome) - the
  extraction must preserve our semantics, and the divergence gets a
  test case named for 1b932ed9 plus the code comment carried over.
- capt cross-reference: capt screengeometrycalculator.{h,cpp} +
  screengeometrycalculatortest.cpp (11 verified slots).
- Test plan: port capt's slots: empty footprints returns startRect;
  bottom/top/left edge non-panel reserve thickness; top+bottom
  accumulate; panel desktopUse uses screenEdgeMargin; ignored
  visibility mode skipped; ignored edge skipped;
  normal-window+None auto-blacklisted; offscreen desktopUse skipped;
  region subtracts footprint. ADD: the 1b932ed9 self-origin case and
  a multi-dock same-edge case (our real layout shape).
- Qt5-fidelity: f0ad7b23 app/lattecorona.cpp:537
  `availableScreenRegion` (verified) and its rect sibling; note Qt5
  predates the WithCriteria split - fidelity is behavioral (the
  reserved area Qt5 computed), executor reads both bodies.
- Live verification: cold restart with --user-config; settings
  window opens fully on-screen (the 1b932ed9 regression check);
  dumpwins confirms geometry.
- Delegation tag: delegate-safe.
- Risk + rollback: low-medium (chrome placement consumes this);
  refactor + tests, one revert.

### EX-09 PositionerGeometry [delegate-safe]

- Header: `app/view/positionergeometry.h` (capt's placement).
- Responsibility: the pure sizing/placement math of a dock view -
  window position, window size, maximum normal geometry, canvas
  rect, slide edge, forced borders - lifted from Positioner's live
  View reads.
- Source (verified in OUR tree): app/view/positioner.cpp:307
  `slideLocation`, :686 `validateTopBottomBorders`, :772
  `updatePosition`, :903 `resizeWindow`.
- Extract-vs-pin: three landed fixes in this file (793faad2 remap on
  relocation, c5bdc239 late screen id, the 1607d022 family) were all
  about WHEN geometry runs, not the math - and the math is exactly
  what nobody can currently test without a compositor. Extracting the
  math separates "compute the rect" (table-testable) from "apply it
  on Wayland" (layer-shell, stays put).
- Interface: capt's shape (verified at capt 81384003,
  app/view/positionergeometry.h, 339 lines): `ViewGeometryInputs`
  struct (location, formFactor, alignment, behaveAsPlasmaPanel,
  thicknesses, margins, maxLength, offset, slideOffset...) feeding
  `dockPosition`, `windowSize`, `maximumNormalGeometry`,
  `canvasGeometry`, `slideEdge`, `forcedBorders`. OUR DIVERGENCE,
  mandatory and architectural: on Wayland the dock surface position
  is owned by layer-shell anchors (app/wm/waylandlayershell.cpp
  configureView/updateAnchoring - the invariants documented there
  must not be fought); our Positioner's computed positions feed
  masks, X11, the canvas overlay, and availability math. The spec's
  adapter section must map each pure function to its ACTUAL consumer
  in our tree first (grep the call sites at execution) and drop any
  capt function whose consumer does not exist here - "not consumed
  on our architecture" is a recorded verdict, not dead code to carry.
- capt cross-reference: capt 4a829185 (+ sourceguardtest guards) and
  positionergeometrytest.cpp - 28 verified slots covering all six
  function families.
- Test plan: port the 28 capt slots (dockPosition per edge for
  panel/non-panel and per alignment incl. the screenEdgeMargin/
  slideOffset case; windowSize horizontal/vertical panel/non-panel +
  degenerate clamp; maximumNormalGeometry left/right/bottom +
  multi-monitor offset; canvasGeometry four edges; slideEdge four
  edges + Floating->None; forcedBorders four cases). Where our math
  already diverges from capt's port (they ported from the same Qt5
  ancestor), the Qt5 body at f0ad7b23 arbitrates.
- Qt5-fidelity: f0ad7b23 app/view/positioner.cpp:300 slideLocation
  (verified) and the updatePosition/resizeWindow ancestors (executor
  reads the Qt5 bodies; our layershellmappingtest already pins the
  port-era layer mapping decisions and must stay green).
- Live verification: the layershellmappingtest + a real-config
  restart with dumpwins geometry comparison before/after; canvas
  overlay placement in edit mode on both monitors.
- Delegation tag: delegate-safe.
- Risk + rollback: medium (touches the most Wayland-sensitive file);
  strictly behavior-preserving refactor, one commit per function
  family if the diff grows.

### EX-10 MaskInputGeometry [delegate-safe, heavy live recipe]

- Header: `containment/plugin/units/maskgeometry.h`
- Responsibility: the dock's visibility mask rect and input-region
  rect per edge/state (hidden, normal, floating-gap, sidebar) as
  pure per-edge tables.
- Source (verified): containment/package/contents/ui/
  VisibilityManager.qml:250-322 `updateMaskArea`, 324-404
  `updateInputGeometry`.
- Extract-vs-pin: mask errors are the invisible-dock / dead-input
  class - user-catastrophic and pixel-invisible in screenshots
  until interacted with. The math is per-edge rect selection with
  qBound normalization, fully table-testable. The qBound clamps get
  the no-bandaid assessment during extraction: each is either a
  documented geometry normalization (comment carried over) or a
  symptom hiding a bad producer (finding to file).
- Interface: `struct MaskInputs { Plasma::Types::Location location;
  bool inNormalState; bool isHidden; bool behaveAsPlasmaPanel; bool
  isFloatingGapWindowEnabled; int screenEdgeMargin; int
  normalThickness; int maxThickness; QRect viewLocalGeometry; ... }`
  -> `struct MaskResult { QRect maskRect; QRect inputRect; }` via
  two functions mirroring the QML pair. The exact field list is
  derived from the two function bodies at execution (they read a
  bounded set of metrics/visibility properties; enumerate, do not
  approximate).
- capt cross-reference: none (capt's visibilityrevealtest is
  backend-side).
- Test plan: per-edge x per-state matrix (4 edges x {normal, hidden,
  sliding, floating-gap, sidebar}); mask==input equivalences where
  the QML computes them equal; degenerate window sizes rejected
  loudly (a zero-size input is family-5 async timing - assert, do
  not clamp, per the AutoSize contract precedent).
- Qt5-fidelity: f0ad7b23 containment/package/contents/ui/
  VisibilityManager.qml:250 `updateMaskArea` (verified, same
  name/line). The input-geometry half is port-era (Wayland input
  regions); its spec source is the current body plus the C++
  consumers in app/view/visibilitymanager.cpp.
- Live verification (mandatory, extensive): for each visibility mode
  (dodge-active, dodge-maximized, auto-hide, windows-go-below):
  drive a window against the dock, verify reveal/hide; probe input
  edges with fakepointer clicks just inside/outside the mask rect
  (clicks outside must fall through to the desktop); floating-gap
  hover reveal; dumpwins geometry cross-check. This is the heaviest
  live matrix in the delegate-safe set - budget it.
- Delegation tag: delegate-safe (tables are mechanical), but the
  live matrix is mandatory before the cutover commit merges.
- Risk + rollback: medium-high consequence, low likelihood given the
  matrix; single cutover commit.

### EX-11 LauncherListOps [delegate-safe]

- Header: `plasmoid/plugin/units/launcherlistops.h`
- Responsibility: launcher list algebra - stored-record parsing,
  separator name allocation, order reconciliation planning, and the
  waiting/to-be-added/to-be-removed/to-be-moved registries - as pure
  list operations.
- Source (verified): plasmoid/package/contents/ui/abilities/
  Launchers.qml:282-316 `currentStoredLauncherList` (activity-tagged
  record parsing), 59-87 separator predicates +
  `freeAvailableSeparatorName` allocation loop;
  plasmoid/package/contents/ui/abilities/launchers/Validator.qml:
  50-71 `upwardIsBetter`, 73-136 the reconciliation loop;
  plasmoid/package/contents/ui/TasksExtendedManager.qml (404 lines
  of parallel array registries; verified anchor 264-324
  `addLauncherToBeMoved`/`moveLauncherToCorrectPos` family).
- Extract-vs-pin: d6d57e61 (stale synced-launcher clients) lived in
  this complex; the guard inventory flags Validator's
  `upwardIsBetter` -1-splice as a heuristic that silently degrades
  to suboptimal moves. Registries with substring-equality and
  hand-rolled splices are the exact drift class typed containers
  end. The reconciliation REDESIGN: replace the move-one-then-
  restart loop with a computed move plan `plan(current, goal) ->
  ordered list of (from,to) moves`, which fixes the -1-splice at
  origin (the planner never needs the direction heuristic).
- Interface: pure functions over QStringList/QVector value types:
  `parseStoredRecords(QStringList) -> QVector<LauncherRecord {url,
  activities}>`, `freeSeparatorName(existing) -> QString`,
  `movePlan(current, goal) -> QVector<Move>`; a `Registry<T>` value
  type replacing the four parallel-array registries with one tested
  container (add/remove/exists/count semantics preserved per
  registry, including the substring-equality quirk of
  waiting-launchers - which gets a test documenting WHY substring,
  or a finding if the why cannot be established from Qt5).
- capt cross-reference: capt syncedlaunchersclienttest and
  smartlauncheritemtest exist (backend-side); read for case ideas,
  not structure.
- Test plan: parse round-trip (records with/without activity tags,
  malformed tails as loud errors); separator allocation fills gaps
  and never collides; movePlan property tests (applying the plan to
  `current` yields `goal` for permutations, insertions, removals;
  plan length minimal for adjacent swaps); registry semantics per
  the current QML behavior (equality mode explicit).
- Qt5-fidelity: f0ad7b23 plasmoid/package/contents/ui/abilities/
  launchers/Validator.qml:50 `upwardIsBetter` (verified) and the
  Qt5 TasksExtendedManager equivalent (executor locates it -
  the Qt5 file may sit at a different path; find it with
  `git show f0ad7b23:plasmoid/package/contents/ui/ --name-only`
  class searches, record the actual ancestor).
- Live verification: drag-reorder launchers in edit mode
  (fakepointer drag, confirm via appletOrder/launcher list
  round-trip in the layout file per latte-live-verification section
  6); pin/unpin a launcher on two activities and switch activities.
- Delegation tag: delegate-safe.
- Risk + rollback: medium (launcher order is user-sacred data;
  family-6 KConfig default-deletion amplifies mistakes - the tests
  must include a non-default-order round-trip). Registry cutover and
  planner cutover land as separate commits.

### EX-12 ColorizerDecisionCore [delegate-safe]

- Header: `containment/plugin/units/colorizerdecision.h`
- Responsibility: the colorizer's theme/scheme selection decision
  tree (which scheme file and text-color class apply for a given
  environment state) as a pure decision function.
- Source (verified): containment/package/contents/ui/colorizer/
  Manager.qml:68-136 `applyTheme` decision tree, 167-199 `scheme`
  selection (edit-mode contrast logic).
- Extract-vs-pin: every color investigation of the 2026-07-14..15
  complex (1f835402 ColorOverlay semantics, 5c06b497 fringe
  measurement, 79ca3360 kdeglobals resolution) had to re-derive this
  tree by hand to state expected behavior. The tree itself has not
  been the defect yet - extraction here is prophylactic
  testability, which is why it ranks below the geometry units. The
  rendering side (ColorOverlay, layer effects) STAYS in QML - it is
  pinned by qml-effect-rules.sh and the family-7 contracts and is
  explicitly out of scope.
- Interface: `struct ColorizerEnv { bool colorizerEnabled; int
  themeColorsSetting; int windowColorsSetting; bool busyBackground;
  double backgroundOpacity; bool editMode; bool touchingWindowScheme
  ... }` -> `struct ColorizerChoice { SchemeSource source; QString
  schemeFile; bool brightText; }`. Field list enumerated from the
  two bodies at execution.
- capt cross-reference: capt schemesmodeltest is settings-side;
  none for this tree.
- Test plan: decision table covering the setting enums x busy x
  opacity thresholds x edit mode; the LightThemeColors desk scenario
  (5c06b497's context: colorizer active under light scheme) and the
  Dark Colors real-config flip (79ca3360's verification) as named
  cases so future palette work has executable expectations.
- Qt5-fidelity: f0ad7b23 containment/package/contents/ui/colorizer/
  Manager.qml:60 `applyTheme`, :159 `scheme` (verified). CLAUDE.md
  names edit-mode color behavior as a fork trap; the Qt5 body is
  the spec, not either fork's.
- Live verification: flip Dark/Light Colors on the real config with
  colorizing enabled; background and applet text track per the Qt5
  rules (the 79ca3360 session recorded the expected behavior);
  restore palette after (the probe-config lesson from 1f835402's
  session).
- Delegation tag: delegate-safe.
- Risk + rollback: low; the actuators stay in QML.

### EX-13 ViewTypeAndBackgroundPredicates [delegate-safe]

- Header: `containment/plugin/units/backgroundstate.h`
- Responsibility: the Panel-vs-Dock decision chain and the
  background-state predicate cluster (solid/transparent/shadows) as
  one pure state resolver.
- Source (verified): containment/package/contents/ui/main.qml:58-69
  `viewType`, 71-91 `viewTypeInQuestion`, 240-274
  `panelShadowsActive`, plus the forceSolidPanel/
  forceTransparentPanel/forcePanelForBusyBackground cluster
  (126-149 per the inventory); containment/package/contents/ui/
  background/MultiLayered.qml:463-498 `invUpdateEffectsArea` (the
  effects-rect computation), 56-125 paddings math.
- Extract-vs-pin: the chain is the root decision every geometry
  consumer branches on (latte-architecture: "trace it before
  changing geometry code") and it has confused live debugging twice
  (the throwaway viewType=1 full-width background mistaken for a
  regression, recorded in session-handoff). The edit-mode background
  family (38e60eb9, f5a5f44c, d72ee0cd) was adjacent stacking/
  semantics, fixed in QML - the predicates that DECIDE remain
  untested. Extract the predicates; the layer stack and its states
  block stay QML.
- Interface: `struct BackgroundEnv { int viewTypeSetting; bool
  byPassWM; ... }` -> resolved `{ ViewType effective; bool
  solidForced; bool transparencyForced; bool shadowsActive; }`, and
  a separate `effectsRect(inputs) -> QRect` for the
  invUpdateEffectsArea math. Enumerate fields from the bodies.
- capt cross-reference: capt panelbackgroundtest covers the C++
  scanline side (our EX-25), not these predicates.
- Test plan: decision table: viewType setting x custom shadow x
  Justify/static x thickness-vs-panelSize x zoom==1 (the
  viewTypeInQuestion clauses); the throwaway confusion case
  (viewType=1 + alignment=10 + panelSize=100 => full-width Panel
  rendering is CORRECT) as a named case; effectsRect for
  compositing/no-compositing/hidden.
- Qt5-fidelity: the Qt5 main.qml ancestor (executor greps
  `viewType`/`panelShadowsActive` in f0ad7b23's containment
  main.qml; function-level anchor, lines unverified here).
- Live verification: flip Dock/Panel type in the chooser on the
  throwaway; background mode changes match Qt5 screenshots
  (blueprint order per 38e60eb9 stays).
- Delegation tag: delegate-safe.
- Risk + rollback: low-medium (everything reads these); cutover in
  one commit, revert restores bindings.

### EX-14 DropEventClassifier [delegate-safe]

- Header: `declarativeimports/core/units/dropclassifier.h` (both
  containment and plasmoid consume it).
- Responsibility: drag/drop mime classification (task vs separator
  vs launcher-list vs plasmoid vs file urls) and the drop insert
  index math, as pure functions over a mime snapshot.
- Source (verified): containment/package/contents/ui/
  DragDropArea.qml:92-141 `onDragEnter` classification decisions;
  plasmoid/package/contents/ui/taskslayout/MouseHandler.qml:70-91
  `isDroppingSeparator`/`isDroppingOnlyLaunchers`/`isMovingTask`,
  127-195 `onDragMove` (insert index + ignored-item suppression).
- Extract-vs-pin: mime-format string matching duplicated across two
  files with subtly different accept sets; b474adad found the
  DropArea dead-handler trap here (a `function onDrop` member that
  silently never connects on Qt6) - the QML shells keep arrow-syntax
  handlers, the decisions move where they cannot be silently
  disconnected.
- Interface: `struct MimeSnapshot { QStringList formats; QByteArray
  taskData; QList<QUrl> urls; QString sourceId; }` ->
  `DropKind classify(snapshot)`; `int insertIndexAt(rowEntries,
  QPointF pos, Qt::Orientation)` sharing RowEntry with EX-06.
- capt cross-reference: capt alternativescreateapplettest is
  adjacent flow, not this logic.
- Test plan: classification truth table per mime-format combination
  (including the masqueraded-index plasmoid drop from containment
  main.qml's onAppletAdded, latte-architecture's drop routing);
  insertIndexAt across separators/hidden; launcher-vs-task
  suppression (the ignoredItem rule in onDragMove).
- Qt5-fidelity: Qt5 ancestors of both files at f0ad7b23
  (function-level; executor reads both onDragEnter/onDragMove
  bodies).
- Live verification: fakepointer drag of a task icon to reorder; a
  file-url drop onto a task (opens with the app) and onto empty
  dock area (launcher pin offer), per Qt5 behavior.
- Delegation tag: delegate-safe.
- Risk + rollback: low-medium (drop paths are user-visible but
  recipes exist); one cutover commit per consumer.

### EX-15 WheelAccumulator [delegate-safe]

- Header: `declarativeimports/core/units/wheelaccumulator.h`
- Responsibility: wheel delta accumulation with per-site step,
  threshold, and direction-reversal semantics - one tested
  accumulator replacing four hand-rolled copies.
- Source (verified): plasmoid/package/contents/ui/task/
  AudioStream.qml:111-142 (120-unit step accumulator, residue reset
  on direction reversal - the 299a241b semantics, hand-verified
  against plasma-pa); plasmoid/package/contents/ui/task/
  TaskMouseArea.qml:239-331 `onWheel` (angle decomposition,
  parallel-scroll detection); containment/package/contents/ui/
  layouts/EnvironmentActions.qml:107-173 `onWheel` (scroll-action
  dispatch thresholds); shell/package/contents/configuration/canvas/
  maxlength/RulerMouseArea.qml:25-33 `onWheel` (step trigger).
- Extract-vs-pin: 299a241b's body records exact accumulation
  semantics matched to plasma-pa; nothing pins them. Four sites,
  three behaviors (accumulate-and-step, threshold-fire,
  threshold-with-axis-priority). One parameterized accumulator with
  per-site tests ends the class.
- Interface: `class WheelAccumulator { int add(QPoint angleDelta);
  }` returning fired step count (sign-carrying), constructed with
  {stepSize, axisPriority, resetOnReversal}; pure, no Qt event
  types beyond QPoint.
- capt cross-reference: none.
- Test plan: per-site parameter table; 299a241b's plasma-pa cases
  (fractional deltas from high-resolution wheels accumulate to one
  step at 120; reversal drops residue); TaskMouseArea's
  parallel-axis selection; ruler's +-6 step semantics
  (RulerMouseArea.qml:36-76 consumes the fired steps - that clamp
  math belongs to EX-18, keep the seam clean).
- Qt5-fidelity: Qt5 sites predate high-resolution wheel handling in
  places; 299a241b's matched-to-plasma-pa semantics WIN where they
  differ (deviation already documented in that commit body).
- Live verification: fakepointer scroll subcommand (added in the
  round-seventeen session) over a task's audio badge: volume steps
  match plasma-pa's applet on the same hardware.
- Delegation tag: delegate-safe.
- Risk + rollback: low; per-site cutover commits.

### EX-16 GroupWindowCycler [delegate-safe]

- Header: `plasmoid/plugin/units/windowcycler.h`
- Responsibility: target selection for next/previous/minimize over a
  task group's window list (wraparound, last-active fallback), and
  the containment-side task cycle twin.
- Source (verified): plasmoid/package/contents/ui/task/
  SubWindows.qml:165-205 `activateNextTask`, 209-250
  `activatePreviousTask`, 254-301 `minimizeTask`; containment/
  package/contents/ui/layouts/loaders/Tasks.qml:57-100
  `activateNextPrevTask` (group-parent expansion + wraparound).
- Extract-vs-pin: forward/reverse cycles are hand-mirrored copies
  (the classic drift shape); the fallback ladder (active ->
  lastActive -> first non-minimized) is a selection function over a
  snapshot - pure by construction. Wayland window ids are string
  UUIDs (8e8cdf31/e9710e95): the core takes opaque QString ids,
  never ints (the family the windowinfowrap tests pin).
- Interface: `struct GroupWindow { QString winId; bool isActive;
  bool isMinimized; }`; `selectNext(windows, lastActiveId)`,
  `selectPrevious(...)`, `selectMinimizeTarget(...)` -> index or
  -1-free `std::optional<int>`.
- capt cross-reference: capt lastactivewindowtest covers the
  backend's last-active bookkeeping; read its cases for the fallback
  ladder expectations.
- Test plan: wraparound both directions; active mid-list/at-ends;
  no active + lastActive present/absent/stale; all minimized;
  single window; empty list returns nullopt (caller warns - the
  tasksmodel null-plasmoid misleading-guard note in the handoff's
  guard inventory applies to the caller side).
- Qt5-fidelity: Qt5 ancestors of both files (function-level anchors;
  executor reads f0ad7b23 bodies of SubWindows.qml and the
  containment tasks loader).
- Live verification: wheel-cycle over a konsole group (per the
  configured wheel action); minimize-toggle click behavior on a
  group.
- Delegation tag: delegate-safe.
- Risk + rollback: low; one cutover commit.

### EX-17 TooltipTextComposer [delegate-safe]

- Header: `plasmoid/plugin/units/tooltiptext.h`
- Responsibility: preview tooltip title/subtext derivation - KWin
  `<n>` counter stripping, app-name suffix removal, desktop/activity
  subtext assembly - as pure string functions.
- Source (verified): plasmoid/package/contents/ui/previews/
  ToolTipInstance.qml:419-461 `generateTitle` (regex chain), 463-523
  `generateSubText`; plasmoid/package/contents/ui/task/
  TaskItem.qml:579-624 `generateSubText` (near-twin).
- Extract-vs-pin: two near-twin generateSubText bodies; regex chains
  are cheap to table-test and annoying to drive through live
  previews. Lowest-risk extraction in the plan - a good first unit
  for the post-transition executor to calibrate on.
- Interface: `QString composeTitle(QString display, QString appName)`;
  `QString composeSubText(TaskEnv { genericName, virtualDesktops,
  activityNames, onAllDesktops, onAllActivities, currentActivity })`.
- capt cross-reference: none.
- Test plan: KWin counter forms (`title <2>`, nested angle
  brackets, no counter); app-name suffix present/absent/case
  variants; subtext matrix (all-desktops x activities x current
  activity filtering); empty inputs; twin-equivalence (both call
  sites produce identical output for identical inputs after
  cutover).
- Qt5-fidelity: f0ad7b23 plasmoid/package/contents/ui/task/
  TaskItem.qml:537 `generateSubText` (verified); the ToolTipInstance
  ancestor at its Qt5 path (executor locates; the previews dir was
  reshaped in the port).
- Live verification: hover a task with two windows on different
  desktops; title/subtext match pre-cutover screenshots.
- Delegation tag: delegate-safe.
- Risk + rollback: minimal; one commit.

### EX-18 LengthOffsetClamp [delegate-safe]

- Header: `app/settings/lengthoffsetclamp.h` (consumed by shell
  config QML through an existing settings-side registration point;
  executor picks the registration seam that already reaches these
  pages - universalSettings or the config view context - and
  records it).
- Responsibility: the mutual maxLength/minLength/offset clamp that
  keeps a dock on-screen for Center/Justify vs edge alignments -
  one implementation, currently two.
- Source (verified): shell/package/contents/configuration/canvas/
  maxlength/RulerMouseArea.qml:36-76 `updateMaxLength` (the
  canonical copy per the inventory); shell/package/contents/
  configuration/pages/AppearanceConfig.qml:272-310
  `updateMaxLength`, 460-537 offsetSlider from/to/update cluster
  (the duplicate).
- Extract-vs-pin: live duplicated math that must agree (the ruler
  and the slider edit the same keys); divergence here is a
  config-corruption class. Dedup-by-extraction is the whole point;
  pinning two copies separately would cement the duplication.
- Interface: `struct LengthState { double maxLength, minLength,
  offset; }` (percent units as the QML uses);
  `clampMaxLength(state, step, Alignment) -> LengthState`,
  `clampOffset(state, newOffset, Alignment) -> LengthState`.
- capt cross-reference: capt configcontrolstest exists
  settings-side; read for shape only.
- Test plan: Center/Justify off-screen correction (max+offset
  overflow pulls offset back); edge alignments (offset bounds
  differ); min<=max invariant under crossed updates; the 30..100
  maxLength floor/ceiling; idempotence (clamping a clamped state is
  identity).
- Qt5-fidelity: Qt5 ancestors of both files (function-level;
  executor diffs both Qt5 bodies - if Qt5 also duplicated, equal
  behavior wins; if they differed, that IS the finding to resolve
  before dedup).
- Live verification: drag the ruler and the slider to extremes on
  the throwaway; config round-trip grep (section 6 of
  latte-live-verification) shows clamped values, dock stays fully
  on-screen (dumpwins).
- Delegation tag: delegate-safe.
- Risk + rollback: low; one commit.

### EX-19 ColorLuminance [delegate-safe]

- Header: `declarativeimports/core/units/colortools.h`, exposed to
  QML as a LatteCore singleton function set.
- Responsibility: one luminance/brightness implementation replacing
  five copies.
- Source (verified copies): declarativeimports/components/code/
  ColorizerTools.js, containment/package/contents/code/
  ColorizerTools.js, plasmoid/package/contents/code/
  ColorizerTools.js (34/34/28 lines); indicators/default/package/ui/
  main.qml:71-79 `colorBrightness`; declarativeimports/abilities/
  client/indicators/LatteIndicator.qml:63-71 twin (plus the inline
  brightness math in shell LatteDockConfiguration per the
  inventory).
- Extract-vs-pin: five copies of the W3C formula is pure drift
  surface; indicator packages are user-installable so the QML-facing
  singleton must stay available to third-party indicator QML too
  (register in LatteCore, which indicators already import).
- Interface: `double luminance(QColor)`, `bool isLight(QColor,
  threshold=127.5)`, `QColor darker/lighterBy(QColor, factor)` -
  match the JS semantics exactly first; improvements are out of
  scope.
- capt cross-reference: none.
- Test plan: formula table (black/white/mid greys/primaries match
  the JS output to double precision - generate reference values by
  executing the current JS once, method noted in the test);
  threshold boundary; the three JS copies diff-checked for
  divergence BEFORE dedup (a divergence is a finding, one copy may
  be fixing something).
- Qt5-fidelity: the JS files are Qt5-inherited; f0ad7b23 carries the
  same copies (executor confirms per-copy).
- Live verification: default indicator dot colors on light vs dark
  tasks unchanged (screenshot pair).
- Delegation tag: delegate-safe.
- Risk + rollback: minimal; per-consumer cutover commits.

### EX-20 BadgeMath [delegate-safe]

- Header: `plasmoid/plugin/units/badgemath.h`
- Responsibility: badge identifier parsing, badge record update
  semantics, progress-proportion math, and the arc geometry the
  badge canvas draws.
- Source (verified): plasmoid/package/contents/ui/main.qml:
  1467-1478 `getBadger` (identifier tail parse + lookup), 1480-1497
  `updateBadge`; plasmoid/package/contents/ui/task/
  ProgressOverlay.qml:93-103 `proportion`;
  declarativeimports/components/BadgeText.qml:91-106 `onPaint` arc
  sweep (+36-50 sizing math per the inventory).
- Extract-vs-pin: parsing and proportion are pure; the Canvas
  drawing stays QML (presentational), consuming computed
  {startRadian, sweepRadian, clampedText}. The >9999 clamp and
  locale formatting are user-visible semantics worth pinning.
- Interface: `parseBadgeIdentifier(QString) -> {launcherUrl}`;
  `applyBadge(records, identifier, value) -> records'`;
  `double proportion(int value, int max, bool isCount)`;
  `arcFor(double proportion) -> {startRadian, sweepRadian}`;
  `QString badgeLabel(int value, QLocale)` (9999+ clamp).
- capt cross-reference: capt smartlauncheritemtest covers backend
  badge sources; read for value-range cases.
- Test plan: identifier forms (with/without tail, malformed - loud);
  record update (append vs mutate); proportion 0/1/overflow/negative
  (negative is a producer bug: assert, per no-bandaid); arc
  quarter/half/full sweeps; label clamp at 9999/10000 and locale
  grouping.
- Qt5-fidelity: Qt5 ancestors (function-level anchors in the Qt5
  plasmoid main.qml and BadgeText).
- Live verification: a download in a badge-publishing app (or
  qdbus-published unity counter) renders count and progress ring as
  before.
- Delegation tag: delegate-safe.
- Risk + rollback: minimal.

### EX-21 ScrollOverflowMath [delegate-safe]

- Header: `plasmoid/plugin/units/scrollmath.h`
- Responsibility: task-row overflow scrolling math: clamped
  positions, scroll-into-view distance, autoscroll trigger zones.
- Source (verified): plasmoid/package/contents/ui/taskslayout/
  ScrollableList.qml:84-107 `increasePos`/`decreasePos`/WithStep,
  109-134 `focusOn` (off-screen distance), 136-171 `autoScrollFor`
  (trigger zones + block conditions), plus the
  onContentsExtraSpaceChanged clamp (173-187 per the inventory).
- Extract-vs-pin: bounded-scroll clamp math with orientation
  branches; the df747ebf scroll-fade crash was adjacent (effects),
  but the math itself is untested and feeds drag-autoscroll (feel
  when dragging tasks in overflow).
- Interface: `struct ScrollEnv { int contentLength; int viewport;
  double currentPos; bool vertical; }`; `clampPos`, `stepPos`,
  `focusDelta(itemRect)`, `autoScrollDecision(pointerPos,
  triggerZone, blocked) -> {none|step +-}`.
- capt cross-reference: none.
- Test plan: no-overflow identity; clamp at both ends; focusOn for
  fully/partially off-screen items both directions; trigger-zone
  boundaries; blocked conditions (dragging edge cases from the
  body).
- Qt5-fidelity: Qt5 ScrollableList ancestor (function-level).
- Live verification: shrink maxLength until tasks overflow; wheel
  and drag-autoscroll behave as pre-cutover (glide the drag).
- Delegation tag: delegate-safe.
- Risk + rollback: low.

### EX-22 ActivitySetAlgebra [delegate-safe]

- Header: `app/layouts/activitysetalgebra.h` (capt's placement).
- Responsibility: free/free-running/valid activity list filtering
  out of Synchronizer.
- Source (verified in OUR tree): app/layouts/synchronizer.cpp:150
  `freeActivities`, :166 `freeRunningActivities`, :179
  `validActivities`.
- Extract-vs-pin: trivial set algebra glued to live managers today;
  capt's header (verified at 81384003) is three inline functions -
  the cheapest capt port, useful as the delegate wave's warm-up.
- Interface: capt's verbatim: `freeActivities(all, assigned)`
  (removeAll semantics), `freeRunningActivities(running, assigned)`
  (order-preserving), `validActivities(layoutActivities, all)`.
- capt cross-reference: capt 941bb7fb; activitysetalgebratest (7
  slots, verified: removeAll duplicate semantics, order
  preservation, empty cases).
- Test plan: port the 7 slots; add our synchronizer's actual call
  patterns (synchronizer.cpp:789/:795 consumers) as integration
  assertions.
- Qt5-fidelity: Qt5 synchronizer ancestor (executor greps
  f0ad7b23:app/layouts/synchronizer.cpp for the same functions).
- Live verification: not required (no feel surface); layout
  switching smoke via dbus switchToLayout suffices.
- Delegation tag: delegate-safe.
- Risk + rollback: minimal.

### EX-23 WindowTrackingPredicates + ExtraViewHints [delegate-safe]

- Headers: `app/wm/windowtrackingpredicates.h`,
  `app/wm/tracker/extraviewhints.h` (capt's placements; one spec,
  two headers, two commits).
- Responsibility: (a) the window-state predicates behind
  activeWindowTouching/dodge decisions; (b) the
  horizontal-touching-busy-vertical bucket pass.
- Source (verified in OUR tree): app/wm/tracker/
  windowstracker.cpp:684 `Windows::intersects` and the adjacent
  isActive/isActiveInViewScreen/isMaximizedInViewScreen family; the
  extra-view-hints computation lives in the same file (executor
  locates the exact loop; capt's 5b58c0a3 diff shows the ancestor
  shape).
- Extract-vs-pin: dodge visibility decisions are family-5 timing
  clients (screen geometry arriving late); predicates with pinned
  truth tables make "dodge did not engage" bugs attributable to
  inputs vs logic in one test run. The pending live checks from the
  silent-break sweep (activeWindowMaximized with a touching
  maximized window) become named cases.
- Interface: capt's predicates verbatim (WindowInfoWrap + QRect in,
  bool out; whitelist-wins hasBlockedTracking composition) and
  capt's TrackedViewGeometry/bucket pass with the hasEdgeTouch
  callback seam.
- capt cross-reference: capt 9fba82c8 (7 test slots incl.
  whitelist-wins and scaled-screen intersects) and 5b58c0a3 (7
  slots: same-screen bucketing, wrong-location pairing,
  disabled/not-tracking skips, callback gating). Also capt c94676b9
  (iterate hashes without copying keys) - fold that micro-fix in if
  our loop shape matches.
- Test plan: port both slot sets; add the dodge-maximized live-check
  scenario as a table case (maximized window intersecting view
  screen, not minimized/shaded).
- Qt5-fidelity: Qt5 windowstracker ancestor (function-level).
- Live verification: the standing consolidated list item: dodge
  engages with a maximized touching window; a focused dialog keeps
  activeWindowTouching.
- Delegation tag: delegate-safe.
- Risk + rollback: low; behavior-preserving.

### EX-24 IconSourceClassifier [delegate-safe]

- Header: `declarativeimports/core/units/iconsourceclassifier.h`
  (capt used declarativeimports/core/ too).
- Responsibility: the IconItem setSource if/else ladder as a
  classification enum + the last-valid-source-name filter.
- Source (verified in OUR tree): declarativeimports/core/
  iconitem.cpp exists; executor anchors the setSource ladder lines
  at execution.
- Extract-vs-pin: IconItem is shared rendering infrastructure (task
  icons, applet fallbacks); misclassification bugs render as wrong/
  missing icons with no error. capt's classifier is small and
  proven.
- Interface: capt's verbatim (verified at 81384003): `sourceName`
  (QIcon::name precedence), `classify -> {LocalFile, SvgOrIconName,
  Icon, Image, Clear}`, `isFilteredSourceName` (empty or
  application-x-executable), `ResolvedIcon::isValid`.
- capt cross-reference: capt ad74a34a + ed0afd054 routing;
  iconsourceclassifiertest (13 slots incl. the QImage-variant
  empty-name case and the isValid truth table).
- Test plan: port the 13 slots; verify against OUR iconitem.cpp
  ladder first - if our port diverged from capt's ancestor, our
  body + Qt5 arbitrate.
- Qt5-fidelity: f0ad7b23's iconitem.cpp ancestor (executor greps
  setSource there).
- Live verification: task icons, applet icons, and a local-file
  icon path in a launcher all render post-cutover (screenshot).
- Delegation tag: delegate-safe.
- Risk + rollback: minimal.

### EX-25 PanelBackgroundScan [delegate-safe]

- Header: `app/plasma/extended/panelbackgroundscan.h` (capt's
  placement).
- Responsibility: the pixel-scanline math of panel background
  analysis - max opacity, mask roundness, shadow band metrics -
  lifted from panelbackground.cpp's image plumbing.
- Source (verified in OUR tree): app/plasma/extended/
  panelbackground.cpp exists; executor anchors the scanline loops
  at execution (capt 15a317ff's diff maps the ancestor: 400 lines
  reduced to 25 in their panelbackground.cpp).
- Extract-vs-pin: pure pixel math over QImage rows, feeding
  theme-derived roundness/opacity that background drawing consumes;
  currently only testable by loading real theme SVGs. capt's test
  images (constructed QImages) prove the seam.
- Interface: capt's shape: functions over QImage + geometry returning
  {maxOpacity, roundness, shadow band size/color}.
- capt cross-reference: capt 15a317ff; panelbackgroundscantest (17
  slots: opaque/transparent/half-alpha opacity, stepped/square/
  mirrored corners, zigzag/monotonic shadow ramps, single-row
  no-crash cases, band spans, max-alpha color pick, no-opaque
  invalid).
- Test plan: port the 17 slots with constructed QImages.
- Qt5-fidelity: f0ad7b23's panelbackground.cpp ancestor
  (function-level; roundness semantics must match what our
  MultiLayered padding math consumes - cross-check with EX-13's
  paddings tests).
- Live verification: theme switch on the real config; background
  roundness/shadows unchanged (screenshot pair).
- Delegation tag: delegate-safe.
- Risk + rollback: minimal.

## D. Coverage and test infrastructure

Extend the existing harness; re-invent nothing. What exists today
(inventoried at HEAD): C++ ctest targets flat in tests/ (10
ecm_add_test entries) plus tests/contracts (the pin-the-frameworks
suite, fadd9012) and tests/qml (6 qmltest files driving the real
shipped QML); the gates qmlcompilegate, qmlinteraction, qmleffectrules,
previewcontractrules, qmlcontracts; scripts/build-check.sh as the local
CI surrogate; docs/TESTING.md's honest-coverage standard (real
assertions, no swallowed throws, no construction-only credit, honest
mocks, deterministic headless) with docs/testing/live-only.md for what
genuinely cannot be tested headlessly.

How new units plug in:

- Pure cores get `tests/units/<name>test.cpp`, one ctest entry each,
  wired in tests/CMakeLists.txt exactly like the existing flat tests.
  Cores are headers (+ .cpp where non-inline); tests include them
  directly, no QML engine, no corona.
- QML shell wiring keeps its coverage in tests/qml and the gates. A
  cutover commit may not reduce qmltest coverage: where a qmltest
  asserted QML-computed values, it now asserts the shell delegates
  and applies (the tst_autosize.qml precedent for EX-04).
- The honest-coverage standard applies verbatim: a unit test that
  instantiates a core and asserts nothing earns nothing; reference
  tables generated from the current implementation must name their
  generation method in a comment (EX-03's and EX-19's tables).

Coverage ratchet (design; adopt in Wave 0):

- `scripts/coverage-ratchet.sh`, run by build-check.sh after ctest.
  Two honest numbers, no instrumentation dependency:
  1. Unit-pairing: every header under the four units/ directories and
     the named capt-placement headers must have a matching
     tests/units/*test.cpp registered in ctest. Unpaired header =
     fail. This is the structural guarantee a weaker model cannot
     quietly skip tests.
  2. Test-count floor: `ctest -N` entry count >= the recorded
     baseline in `tests/ratchet-baseline` (a committed integer + the
     entry list for diffability). Any commit may raise the baseline;
     only a deliberate commit editing the file may lower it, which
     makes silent coverage loss un-mergeable.
- Line/branch coverage is deliberately NOT the ratchet metric: the
  devShell has no pinned gcovr/llvm-cov wiring today and percentage
  metrics invite the gaming docs/TESTING.md bans. The script's
  numbers are CI-portable as-is (pure shell over cmake/ctest
  output), so a future CI job runs the same script unchanged - the
  no-rework requirement.
- The preview contract gate and effect/qml gates stay in force; EX-01
  is the only unit allowed to EDIT preview-contract-rules.sh, under
  the migration rule its spec states.

## E. Sequencing into waves

Worktree-per-agent, merge to master serially, prune - the established
workflow (the Wave 1 defect-class initiative ran exactly this shape).
Per-wave done = tests green (full ctest) + ratchet advanced +
live-verified per the specs in the wave + Qt5-fidelity confirmed +
PORTING_PLAN ticked + pushed.

THE TRANSITION RULE GOVERNS ORDER: strong-model-only units are
scheduled inside the remaining strong-model window or explicitly
deferred with their do-not-delegate markers; everything else is the
post-transition backlog, each unit self-contained.

- Wave 0 (strong-model window, small, serial): scaffolding.
  tests/units/ directory + first CMake wiring, the four units/
  directories with a README stating the pure-core rules,
  scripts/coverage-ratchet.sh + tests/ratchet-baseline,
  EX-22 ActivitySetAlgebra as the proving unit (smallest spec,
  exercises the whole pipeline end to end). One session.
- Wave 1 (strong-model window, serial, feel-bearing): EX-01
  PreviewSwitchEngine, then EX-03 ParabolicMathCore, then EX-02
  ParabolicRouter design + implementation. EX-03 precedes EX-02
  because the router consumes the math core. If the window closes
  mid-wave: EX-02 defers with its do-not-delegate marker; EX-01
  half-done rolls back to its last green commit (its two-commit
  split is designed for exactly this).
- Wave 2 (post-transition, parallel worktrees, C++-only - no QML
  risk): EX-07 StorageIdRemapper, EX-08 ScreenGeometryCalculator,
  EX-09 PositionerGeometry, EX-23 predicates/hints, EX-24
  IconSourceClassifier, EX-25 PanelBackgroundScan. Two to three
  agents; these touch disjoint files and merge cleanly. This wave
  first: it builds the delegate's muscle on specs with capt case
  lists and zero feel exposure.
- Wave 3 (post-transition, parallel worktrees, QML math cores):
  EX-17 TooltipTextComposer (calibration unit, run it first), EX-04
  AutoSizeEngine, EX-05 FillLengthDistributor, EX-06
  VisibleIndexEngine, EX-19 ColorLuminance, EX-16 GroupWindowCycler,
  EX-15 WheelAccumulator. Disjoint by subsystem; EX-06 before any
  future EX-14 work (shared RowEntry type).
- Wave 4 (post-transition, serial-ish, live-verification heavy):
  EX-10 MaskInputGeometry (its live matrix wants the desk), EX-11
  LauncherListOps (user-sacred data, config round-trips), EX-13
  ViewTypeAndBackgroundPredicates, EX-12 ColorizerDecisionCore,
  EX-14 DropEventClassifier, EX-18 LengthOffsetClamp, EX-20
  BadgeMath, EX-21 ScrollOverflowMath. Sequenced later per feel-risk
  policy; each unit still delegate-safe from its spec, but merges
  gate on the live recipes actually run and recorded.

Shared-type note (the "shared enums first" rule): the plan
deliberately introduces only ONE shared new type family (RowEntry,
EX-06/EX-14) and otherwise reuses Plasma::Types/Latte::Types via
coretypes.h as capt did. If execution discovers a second shared type
wanting to exist, it lands as its own tiny commit before its
consumers, never inside a unit commit.

## F. Risk register and non-goals

Risks, with mitigations bound to mechanisms already in the tree:

1. Feel regression in preview/parabolic (EX-01/02/03). Mitigation:
   glide-only live recipes in every feel spec (the jump-vs-glide
   lesson is codified in latte-live-verification); before/after
   screenshot pairs at fixed pointer positions; two-commit cutovers
   so a revert is surgical; Wave 1 lands while the strong model can
   still judge "feels wrong" against measured baselines.
2. The preview contract gate and EX-01 fighting each other.
   Mitigation: the migration rule in EX-01's spec - rule and test
   move together, no unpinned window; gate edits allowed only in
   EX-01 commits.
3. Binding-entangled logic forced into a bad seam. Flagged
   design-first, extract-second, and NOT in the backlog: ConfigOverlay
   drag/reorder, ItemWrapper's zoom/length binding chains,
   BindingsExternal's passthrough clusters, the class-A reassert
   sites (e412889d/eca51ae0 - their real fix is the filed
   binding-destroyer watch item), CompactApplet's representation
   wiring. Any future attempt at these starts with a design note in
   this file, not code.
4. Config data loss via family-6 (KConfig default deletion) during
   EX-11/EX-18. Mitigation: non-default-value round-trip tests
   mandated in both specs; config round-trip greps in the live
   recipes.
5. Weak-model execution drift (specs misread, tests thinned).
   Mitigation: the ratchet's unit-pairing check (structural, not
   judgment); specs carry verified line anchors and named test
   cases so "done" is checkable by diff; Wave 2 ordering gives the
   delegate capt case lists to port before it must derive any;
   PORTING_PLAN cross-item keeps per-unit Commits: traceability.
6. Silent divergence from Qt5 during re-implementation. Mitigation:
   per-spec f0ad7b23 anchors; the executor diffs port-vs-Qt5 BEFORE
   extracting (three specs already flag known-drift questions);
   deviations get named commits and code comments per CLAUDE.md.
7. Vendored-tree contamination (plasmoid/plugin). Mitigation: new
   files only under plasmoid/plugin/units/ with Latte provenance
   headers; the fork-sync diff recipe in CLAUDE.md stays clean
   because vendored files are not edited by extraction commits.

Non-goals, explicit:

- Presentational QML stays QML: the 100+ PRESENTATIONAL files in
  section A, all states/anchors/gradients blocks, the ability
  bridge/host/definition relay layers, DebugWindow.
- No ocean-boiling: files classified BEHAVIORAL but not in the
  backlog (EventsSink, HiddenSpacer, Metrics chains, ItemWrapper,
  TaskIcon effect gating, animation ScriptAction state brackets,
  settings model builders, WidgetExplorer, chrome sizing) are
  deliberate leave-in-place verdicts at this plan's date; revisit
  only with new bug-density evidence.
- No feature work rides extraction commits. Continuation features
  stay in their own PORTING_PLAN section.
- No test-infrastructure rewrite: the harness is extended, not
  replaced; the ratchet is a new script, not a new framework.
- X11-specific behavior is preserved-but-untested beyond compile
  (project policy: X11 must compile, never blocks).

## Executive summary

Twenty-five units: 24 delegate-safe, 1 strong-model-only pair-plus-one
(EX-01 previews, EX-02 parabolic router with EX-03 math underneath).
Everything else a weaker model can execute cold from section C.

Top five extractions by leverage: EX-01 PreviewSwitchEngine (15 fix
commits in three days; converts grep-pinned invariants into behavioral
tests), EX-02/03 parabolic engine (the dock's signature feel, twin
drift ended), EX-04 AutoSizeEngine (hang-class feedback loop),
EX-05 FillLengthDistributor (collapse-class arithmetic), EX-07
StorageIdRemapper (user-data spine, capt-blueprinted).

Wave order: 0 scaffolding+ratchet -> 1 feel-bearing (strong model) ->
2 C++-only capt ports -> 3 QML math cores -> 4 live-heavy tail.

Strong-model-window shortlist (must land before the transition, in
order): Wave 0 (scaffolding, ratchet, EX-22 proving unit), EX-01,
EX-03, EX-02. If time runs out inside the list, everything after the
cut defers with do-not-delegate markers; nothing on it is delegated.

Single biggest risk: feel regression in the preview/parabolic
cutovers - the two units where correctness lives partly in
milliseconds and pixels. The mitigation is structural: they land
first, inside the strong-model window, with glide-based live recipes
and surgical two-commit rollbacks; every other unit is pure math with
tables.

