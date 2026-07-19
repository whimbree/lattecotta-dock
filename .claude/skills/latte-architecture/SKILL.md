---
name: latte-architecture
description: Subsystem map and key data flows for navigating the latte-dock Plasma 6 codebase.
---

# Latte architecture map

Where things live and how data moves between them. Use this to find the
right file before touching anything; the sibling skills cover how to work
once you are there (latte-build-env to build, latte-live-verification to
prove changes, latte-debugging for root cause, latte-plasma6-defect-families
for recurring bug classes, latte-fork-sync for reference-fork review).

## Top-level map

- `app/` - the C++ shell binary. `app/lattecorona.cpp` is the Corona
  (application root, D-Bus surface, screen wiring). Key subtrees:
  `app/screenpool.cpp` (screen id persistence), `app/view/` (one View per
  dock window plus its managers), `app/wm/` (window-system backends and
  the layer-shell layer), `app/layout/` and `app/layouts/` (running layouts
  vs layout storage/sync), `app/settings/` (the global Latte settings
  dialogs: settingsdialog, viewsdialog, detailsdialog, screensdialog),
  `app/shortcuts/` (global shortcuts), `app/plasma/extended/` (theme,
  panel background cache, screen geometries).
- `containment/` - the dock containment. `containment/package/contents/ui/`
  is the QML (main.qml root, `abilities/`, `background/`, `layouts/`,
  `applet/`, `editmode/`), `containment/plugin/` is its private C++ QML
  plugin, most importantly `layoutmanager.cpp` (applet order persistence).
- `plasmoid/` - the Latte Tasks applet. `plasmoid/package/contents/ui/` is
  the QML; `plasmoid/plugin/` is the C++ backend VENDORED from
  plasma-desktop's taskmanager applet (see the provenance headers in
  `plasmoid/plugin/backend.h` and friends; Plasma 6 ships no public
  replacement). At fork-sync time diff against upstream plasma-desktop.
- `declarativeimports/` - the `org.kde.latte.*` QML modules: `core`
  (LatteCore types/services), `components` (shared controls, e.g.
  `declarativeimports/components/ComboBox.qml`), `abilities` (shared
  ability bases used by containment and plasmoid).
- `shell/` - the per-view settings window QML package. The pages are
  `shell/package/contents/configuration/pages/`: AppearanceConfig,
  BehaviorConfig, EffectsConfig, TasksConfig.
- `indicators/` - indicator packages: `default`, `org.kde.latte.plasma`,
  `org.kde.latte.plasmatabstyle`.
- `scripts/` - build/run/verification tooling (`gate-all.sh` runs every
  merge gate and stamps the validated HEAD for the pre-push hook in
  `scripts/git-hooks/`; `build-check.sh`, `run-staged.sh` the env core,
  `restart-staged.sh` the desk lifecycle, `start-dock.sh` the
  daily-driver shorthand, `qml-compile-gate.sh`, `qmllint-gate.sh`,
  `qml-interaction-tests.sh`, `sceneprobe-gate.sh`, `run-e2e.sh`,
  `coverage-ratchet.sh`, `lib-qml-env.sh`). latte-build-env and
  latte-live-verification document how to use these.
- `tests/` - the test tree, tiered: `units/` (sanitized pure-core
  suites via latte_add_unit_test: ASan+UBSan+QT_FORCE_ASSERTS),
  `contracts/` (pins of exact Qt/KF6/libplasma behaviors), behavioral
  suites at `tests/*.cpp` (real object graphs headless), `qml/`
  (offscreen qmltests against the staged tree), `sceneprobe/` (the
  real-pixel scene harness: probe binary, comparator, scenes with
  committed goldens), `e2e/` (live-session shell recipes), plus the
  committed `ratchet-baseline` and `qmllint-baseline`.
- `app/dbusreports.{h,cpp}` - the D-Bus observability serializers
  (viewsData and friends); Corona methods are one-line delegates.
  Usage: docs/reference/dbus-interface-reference.md; design:
  docs/reference/dbus-observability-interface.md.
- `app/wm/windowid.h` - the WindowId newtype (explicit construction,
  optional-returning X11 parse, byte-wise container semantics).
- `docs/` - split into `tracking/` (live plans and registries) and
  `reference/` (stable how-tos and research). `tracking/ROADMAP.md` is
  the index; `tracking/PORTING_PLAN.md` is the master checklist (every
  task has a Commits: line, keep it ticked), `tracking/session-handoff.md`
  the rolling handoff, `tracking/known-defects.md` the flat defect
  registry; `reference/dbus-interface-reference.md` is the D-Bus how-to,
  `reference/captsilver-testability-adoption.md` the test-infrastructure
  adoption plan, plus research docs
  (`reference/taskmanager-integration-research.md`,
  `reference/dock-replication-design.md`,
  `reference/ng-upstream-audit.md`).

## The View family (app/view/)

`view.cpp` is the dock window itself (a PlasmaQuick ContainmentView).
Notable: the `containmentChanged` wiring in the constructor (~line 129),
`View::moveToScreen()` (remaps the window to another QScreen),
and the `KDE_COLOR_SCHEME_PATH` property pin (~line 303) that keeps the
view on Latte's color scheme. Each View owns manager objects:

- `positioner.cpp` - geometry sync and screen following.
  `setScreenToFollow()` binds the view to a QScreen;
  `immediateSyncGeometry()` recomputes geometry, keying its free-region
  math on `fixedScreen` availability (primary vs explicit screen id).
  Relocation (moving a dock to another edge/screen/layout) is a state
  machine: `setNextLocation()` stores the target, the view hides, and
  the `hidingForRelocationFinished` signal applies the move (including
  `setNextLocationForClones` fan-out for original views).
- `visibilitymanager.cpp` - dodge/auto-hide modes and mask handling.
- `parabolic.cpp`, `effects.cpp` - parabolic zoom plumbing and
  background/mask effects.
- `containmentinterface.cpp` - the C++ side of the containment bridge:
  per-applet metadata (`m_appletData`), the Plasma 6 applet config access
  (`appletObj->property("configuration")` cast to KConfigPropertyMap; the
  `configuration` Q_PROPERTY on Plasma::Applet is CONSTANT), the
  `latteTasksModel`, and the 'org.kde.sync' delayed applet-configuration
  retry logic (config sync storms live here).
- `originalview.cpp` / `clonedview.cpp` - the clone/replication split.
  `isClonedFrom` is persisted in the containment config
  (`app/layouts/storage.cpp` reads/writes it, null sentinel
  `ISCLONEDNULL`); `genericlayout.cpp` resolves clones back to their
  original at load. Design and prerequisites for the replicate feature:
  `docs/reference/dock-replication-design.md`.
- `app/view/settings/` - the per-view chrome windows, all deriving from
  `subconfigview.cpp` (shared base: `showAfter()` deferred show, the
  `viewconnections` list tying window lifetime to the view). See the
  settings-window section below.
- `app/view/indicator/indicator.cpp` (ViewPart::Indicator) - loads the
  indicator package; its `configuration` property is a KConfigPropertyMap
  backed by a KConfigLoader built from the package's config XML.

## Layer-shell layer (app/wm/)

`waylandlayershell.cpp` is the single place that talks to
LayerShellQt. Four entry points:

- `configureView()` - full setup for a dock window; must run before the
  window is first shown (a live QWindow cannot adopt layer-shell).
- `updateAnchoring()` - edge/alignment changes on an already-configured
  dock; deliberately narrower than configureView.
- `applyFixedGeometry()` - places chrome windows (settings, chooser,
  widget explorer) at an absolute rect via anchors plus margins.
- `applyCanvasPlacement()` - places the edit-mode canvas overlay along a
  dock edge.

Invariants (all documented in the file's comments, do not fight them):
margins are interpreted relative to the surface's own output, so the
surface must be on the output that `screenGeometry` describes;
the exclusive edge must be one of the anchors or the compositor kills
the surface ("exclusive edge is not of the anchors"); overlays set
exclusive zone -1 to opt out of other surfaces' zones (zone 0 gets them
pushed off-screen by the dock's own zone); and a mapped wlr-layer
surface is bound to the output it was created on, so `retargetScreen()`
must hide/re-show to move chrome across monitors.

`waylandinterface.cpp` is the window-tracking backend behind
`abstractwindowinterface.cpp` (wayland-only since the X11 backend and
`xwindowinterface.cpp` were removed 2026-07-17, Phase 4).

## Layout and storage (app/layout/, app/layouts/)

- `app/layout/genericlayout.cpp` - a running layout: owns its views,
  `addView()` materializes a View for a containment, `newView()` spawns
  from a template or view data, and `screenForContainment()` answers
  "which screen does this containment belong to", consulting
  `m_pendingContainmentUpdates` first so not-yet-saved moves win over
  stale config.
- `app/layouts/storage.cpp` - the config-file surgeon. `storedView()`
  serializes one containment (plus subcontainments) to a temp file;
  `newUniqueIdsFile()` rewrites all containment/applet ids in a layout
  file to ids free in the destination, including rewriting stringly
  id lists (`appletOrder`, `lockedZoomApplets`,
  `userBlocksColorizingApplets`); `importLayoutFile()` loads the
  massaged file into the corona; `newView()` chains them.
- `synchronizer.cpp` / `manager.cpp` - which layouts are active per
  activity, layout switching.

The duplicate flow end to end: D-Bus `duplicateView` (declared in
`app/dbus/org.kde.LatteDock.xml`) -> `Corona::duplicateView()` in
`lattecorona.cpp` -> `View::duplicateView()` -> `storedView()` temp
file -> `View::newView()` picks a free edge ->
`GenericLayout::newView()` -> `Storage::newView()` ->
`newUniqueIdsFile()` -> `importLayoutFile()` -> `addView()`.

## Containment QML (containment/package/contents/ui/)

`main.qml` is the root. The central decision chain is `viewType`
(feeding `behaveAsPlasmaPanel` vs `behaveAsDockWithMask`): panel mode
means the window is a plain opaque strip, dock mode means a full-size
window shaped by an input/paint mask. Many downstream behaviors branch
on this, so trace it before changing geometry code.

- `abilities/` - self-contained services the layouts and applets read:
  `Metrics.qml` (icon size, margins, thickness math), `AutoSize.qml`
  (the iconSize shrink/grow feedback loops; the comments warn about
  endless grow/shrink oscillation, respect the asymmetric limits),
  `ParabolicEffect.qml`, `Layouter.qml`, `Indexer.qml`, etc. Shared
  ability bases live in `declarativeimports/abilities`.
- `background/MultiLayered.qml` - the dock background stack;
  `isGreaterThanItemThickness` decides when the panel background is
  thick enough to swallow item margins.
- `layouts/` - `LayoutsContainer.qml` and `AppletsContainer.qml` hold
  the three applet rows (start/main/end for Justify).
- `editmode/` - edit-mode visuals inside the containment.

`containment/plugin/layoutmanager.cpp` (LayoutManager) is the applet
order brain: `restore()` rebuilds the QML applet items from the saved
`appletOrder`/`splitterPosition` options, `save()` writes them back,
`addAppletItem()` inserts drops. It resolves QML items to applet ids
through the PlasmaQuick::AppletQuickItem -> applet() chain, and its
`m_configuration` is the containment's own KConfigPropertyMap obtained
via `m_plasmoid->property("configuration")`.

## Config access chains (single-loader doctrine)

One KConfigLoader-backed map per config object; everyone shares it,
nobody constructs a second loader.

- Containment options inside containment QML: `Plasmoid.configuration`.
- Applet options from the settings window: the shell pages reach the
  applet's own map, e.g. `tasks.plasmoid.configuration` in
  `shell/package/contents/configuration/pages/TasksConfig.qml` (see the
  comment at its top; this is the Plasma 6 pattern the ng fork
  uncovered).
- C++ side: `applet->property("configuration")` cast to
  KConfigPropertyMap (`app/view/containmentinterface.cpp` does this in
  `appletConfiguration()`).
- Indicators: `app/view/settings/indicatoruimanager.cpp` injects an
  `indicator` context property into every indicator UI; its
  `configuration` is the KConfigLoader map owned by ViewPart::Indicator
  (`app/view/indicator/indicator.cpp`).

If a settings control writes but nothing changes, suspect a broken link
in one of these chains before anything else, and check
latte-plasma6-defect-families for the known failure shapes.

## Settings windows and edit mode

All in `app/view/settings/`, all subclassing SubConfigView.
`viewsettingsfactory.cpp` creates the primary config view and the widget
explorer; the secondary chooser and the canvas overlay are created by
PrimaryConfigView itself (see its showSecondaryWindow/showCanvasWindow
paths in `primaryconfigview.cpp`):

- `primaryconfigview.cpp` - the main per-dock settings window (hosts the
  shell/ package pages).
- `secondaryconfigview.cpp` - the small Dock/Panel chooser.
- `canvasconfigview.cpp` - the edit-mode canvas overlay drawn over the
  dock area; `updateInputRegion()` re-carves its input mask when the
  configure-applets mode flips or the surface resizes (a resize resets
  the surface mask).
- `widgetexplorerview.cpp` - the widget explorer sidebar.

Placement: primary/secondary/widget-explorer call
`LayerShell::applyFixedGeometry()`, the canvas calls
`applyCanvasPlacement()`; all inherit the output-remap behavior
described in the layer-shell section. Edit-mode driving and
verification recipes are in latte-live-verification.

## Where do I start for X

| Symptom / task | Entry point |
| --- | --- |
| Dock on wrong screen/edge, geometry off | `app/view/positioner.cpp` then `app/wm/waylandlayershell.cpp` |
| Settings control does nothing | `shell/package/contents/configuration/pages/*.qml`, then the config chains above; see latte-plasma6-defect-families |
| Applet order lost / drag-drop persistence | `containment/plugin/layoutmanager.cpp` |
| 'org.kde.sync' storms, per-applet config sync | `app/view/containmentinterface.cpp` |
| Multi-screen moves, monitor hotplug | `Positioner::setScreenToFollow()` + `View::moveToScreen()` + `retargetScreen()` in waylandlayershell |
| Edit chrome on wrong monitor / dead input | `app/view/settings/canvasconfigview.cpp` + `applyCanvasPlacement`/`applyFixedGeometry` |
| Tasks behavior (grouping, launchers, badges) | `plasmoid/package/contents/ui/` + vendored `plasmoid/plugin/backend.cpp` |
| Duplicate/clone/new dock broken | the duplicate flow above; `app/layouts/storage.cpp` id remapping |
| Icon size wrong, dock keeps resizing | `containment/package/contents/ui/abilities/AutoSize.qml` + `Metrics.qml` |
| Background/theming drawing | `containment/package/contents/ui/background/MultiLayered.qml`, `app/plasma/extended/theme.cpp` |
| Auto-hide / dodge misbehaving | `app/view/visibilitymanager.cpp` |
| Layout switching, activities | `app/layouts/synchronizer.cpp`, `app/layouts/manager.cpp` |
| Inspect/drive the dock from outside | `docs/reference/dbus-interface-reference.md`; serializers in `app/dbusreports.cpp` |
| Meta+number, entry activation, shortcut badges | `app/shortcuts/globalshortcuts.cpp` -> `app/view/containmentinterface.cpp` `identifyShortcutsHost()` (defect family 9 lives here) |
