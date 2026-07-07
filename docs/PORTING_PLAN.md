# Plasma 6 / Qt6 porting plan

This fork starts from upstream KDE latte-dock's full history (last real
commit before this port: Qt5 5.15.0 / KF5 5.88.0, X11 required
unconditionally). The goal is a clean, upstream-mergeable Plasma 6/Qt6
port, not a history-reset rewrite.

Two independent community ports already exist and are used as
**reference material, not as a base**: `~/Projects/latte-dock-ng`
(ruizhi-lab, Wayland-only, 406 commits of port work, history reset by
its maintainer) and `~/Projects/latte-dock-qt6` (CaptSilver, also
Wayland-only despite some leftover dead `isPlatformX11()` branches -
its own `1cef7fe7` commit confirms the X11 backend was actually
removed - 194 commits of port work, real original history preserved).
Neither fork's git history or commits are inherited here. This plan is
the product of reading every commit message (not just subjects) in
both forks' port-work ranges, cross-referenced against live testing of
both (build, run, drive real interactions, capture real crash
backtraces) - not just archaeology. See
`~/Projects/latte-dock-ng/docs/fork-comparison-journal.md` for the fork
comparison writeup and live-testing findings.

Where a fork's approach to a subsystem is correct, it informs how we
reimplement it. Where it isn't (both forks have real, live bugs -
tracked below, and a few subsystems clearly took many repeated fix
attempts to reach their current state in at least one fork), do it
properly instead, even if that means more upfront research before
writing code.

## How to use this checklist

Every task below is a checkbox: `- [ ]`. Each has a `Commits:` line.
When a task lands, tick the box and fill in the commit hash(es) that
implemented it (short hash is fine, e.g. `a1b2c3d`) - this is the
traceability mechanism for the whole port: given any checklist item,
you can find exactly which commit(s) did it, and given any commit, this
doc says which task it was for. If a task ends up split across more
commits than expected, or one commit covers two tasks, just list the
hash(es) under each - don't force a 1:1 mapping that isn't real.

If a task gets stubbed rather than fully done, tick it anyway but note
that in the commit line (e.g. `Commits: a1b2c3d (stub - see
STUB comment in tasksbackend.cpp)`) so it's clear from this doc alone,
without cross-referencing git log, that follow-up work is owed.

## Known bugs in the reference forks (don't reintroduce these)

- **latte-dock-ng**: widget removal from the dock doesn't work (menu
  item present, action doesn't function). Drag-reorder jitter, fought
  four separate times by its maintainer via escalating
  resistance/hysteresis tuning, still present per live testing.
  Repeated drag-reordering can leave an icon stuck behind other
  elements visually (not yet root-caused).
- **latte-dock-qt6**: adding the task-manager widget crashes, both via
  drag and via double-click (two different trigger paths - one
  confirmed backtrace shows a null `QMimeData` during a Wayland drag
  operation inside Qt/KDeclarative framework code, triggered from
  `AppletDelegate.qml`'s drag handling; root cause for the
  double-click-only path, which shares no drag code, not found).
  Widget explorer window is also mispositioned, undecorated, and has
  severe input-hover lag - a pattern of rough newer UI surfaces, not
  an isolated bug.
- **Both forks**: drag-reorder jitter, edit-mode entry/exit detection,
  and session shutdown/logout hangs each consumed a large number of
  iterative band-aid commits in at least one fork before reaching
  (arguably still imperfect) stability. Treat these three subsystems as
  places to research the actual root cause before writing the first
  line of code, not places to start tuning constants.

## Stub tracking

See `CLAUDE.md` for the convention (`stub:` commit type + `// STUB:`
code comment, both explaining what's missing and why). Applies from
Phase 1 onward - this port will legitimately need stubs to keep phases
moving (e.g. task manager backend before Phase 6 lands), and they must
never be silent.

## Phases

Sequenced the way both reference forks actually did it, since that
reflects real dependency constraints (QML controls can't be ported
before the build produces a binary), not because we're following
their commits.

Verification cadence: from the first runnable milestone (end of Phase
5) onward, a phase isn't done until its subsystem has been driven in a
live Plasma 6 Wayland session. Phase 10 is the full final sweep, not
the first time anything gets tested by hand - both forks' histories
show what deferring all live testing to the end produces.

### Phase 0: Build environment and testing ground rules

Phase 1's milestone ("cmake configures cleanly against Qt6/KF6") is
untestable without a Qt6/KF6 toolchain to configure against, and a
testing standard adopted during stabilization can't shape any test
written before it. Both belong before the first porting commit, not
in Phases 10-11 where they originally sat.

- [x] Minimal reproducible Qt6/KF6 toolchain: a Nix devShell (or the
      Docker image, matching the verification pattern already built
      for latte-dock-ng) with the full Phase 1 dependency list
      available to `find_package`. Just enough to build - the
      polished packaging (flake outputs, overlay, NixOS module) stays
      in Phase 11
      Commits: 14980059 (flake devShell, nixos-unstable pin, Qt 6.11.1)
- [x] One-command build check (script or make target) runnable
      per-commit once Phase 2's compile milestone lands - cheap
      insurance across a 100+-item port. Once Phase 4 introduces the
      `HAVE_X11` option it must build both ON and OFF, since the
      author only runs Wayland and an untested `#ifdef` path rots in
      weeks otherwise
      Commits: 6c0134bc (`scripts/build-check.sh`; expected to fail
      until the Phase 1/2 milestones - that failing run is the Phase 1
      work loop)
- [x] Write the "honest coverage" testing standard doc now (no test
      that doesn't assert something real/observable), modeled on
      latte-dock-qt6's documented standard (`5fcaa9f1`/`c903921d` in
      its history), which explicitly bans gaming the metric
      Commits: c221f6e1 (`docs/TESTING.md`)
- [x] Decide the test-infrastructure shape while it can still grow
      with the code: coverage-ratchet baseline (fails on regression),
      headless QML interaction-test harness skeleton. The
      e2e/screenshot harness needs a runnable dock and stays in
      Phase 10
      Commits: c221f6e1 (decision recorded in `docs/TESTING.md`;
      harness/ratchet code lands with the Phase 2 compile milestone)

### Phase 1: Build system migration

- [x] Migrate top-level `find_package(Qt5 ...)` to
      `find_package(Qt6 ...)`, bump `QT_MIN_VERSION` to 6.6.0; bump
      the `find_package(ECM ...)` floor to match the chosen KF6
      minimum
      Commits: b41667ec
- [x] Migrate `find_package(KF5 ...)` umbrella to individual
      `find_package(KF6Xxx ...)` calls (KF6 dropped the single-umbrella
      component list on some distros); decide `KF6_MIN_VERSION`
      deliberately - latte-dock-ng uses 6.0.0 (broader compatibility
      target), latte-dock-qt6 uses 6.5.0 (narrower)
      Commits: b41667ec (deviation: kept the `find_package(KF6 ...)`
      umbrella - current KDE style, works on nixpkgs. Chose
      KF6_MIN_VERSION 6.5.0 / PLASMA_MIN_VERSION 6.5.0, the oldest a
      reference fork demonstrably built against. Discovery: the KF6
      Declarative/QuickAddons libraries no longer exist as of KF 6.27,
      link targets moved to KF6::ConfigQml now - see Phase 3)
- [x] Add `find_package` for de-umbrella'd-from-Plasma frameworks:
      `libplasma`, `PlasmaQuick`, `PlasmaActivities`,
      `PlasmaActivitiesStats`, `KSvg`, `KWayland`, `LayerShellQt`,
      `KSysGuard`
      Commits: b41667ec (PlasmaActivitiesStats deliberately skipped -
      nothing in the tree uses KActivities::Stats; KSvg via the KF6
      umbrella's Svg component)
- [x] Add `find_package(KF6KirigamiPlatform)` - transitively required
      by `Plasma::Plasma`'s link interface, configure fails without it
      even though nothing calls it directly
      Commits: b41667ec
- [x] Add `find_package(KF6Service)` and `find_package(LibNotificationManager)`
      - needed by the tasks plugin for job/launcher data
      Commits: b41667ec
- [x] Keep X11 as an optional, best-effort build path instead of
      dropping it (scope decision, see Phase 4 intro): remove
      `find_package(Qt5 X11Extras)` (the module is gone in Qt6 -
      native handles come from `QNativeInterface::QX11Application`),
      add `find_package(KF6WindowSystem)`'s X11 side
      (`KX11Extras`/`KWindowInfo`/NETWM moved there from
      KWindowSystem core), keep `HAVE_X11` a real `option()`
      defaulting ON but never REQUIRED. Both reference forks went
      Wayland-only, so there is no reference port for this path -
      expect it to be the mechanical Qt5->Qt6 work upstream would
      have done, with no fork to crib from
      Commits: b41667ec (`WITH_X11` option -> `HAVE_X11`; both ON and
      OFF variants configure from a fresh tree)
- [x] `qt5_add_dbus_adaptor` -> `qt_add_dbus_adaptor`
      Commits: b41667ec
- [x] Add a CMake `try_compile` feature-detection guard for
      `LayerShellQt::Window::setScreen(QScreen*)` (added in
      LayerShellQt 6.6, later removed upstream - a real build
      regression latte-dock-ng hit after depending on it
      unconditionally); fall back to plain `QWindow::setScreen()` when
      absent
      Commits: b41667ec (detected available on the current pin)
- [x] Milestone: `cmake` configures cleanly against Qt6/KF6, even if
      nothing compiles yet
      Commits: b41667ec (Qt 6.11.1 / KF 6.27.0 / libplasma 6.7.2,
      WITH_X11 ON and OFF)

### Phase 2: Mechanical Qt5->Qt6 source conversions

Should not require design decisions - if a change here needs one, it
belongs in a later phase instead.

- [x] `QString::SkipEmptyParts` -> `Qt::SkipEmptyParts`
      Commits: e9710e95
- [x] `foreach` -> range-for (verify no mutate-during-iteration/detach
      semantics changed per site)
      Commits: d7801e30 (all six sites verified read-only)
- [x] `QMouseEvent`/`QWheelEvent::pos()` -> `position().toPoint()`
      Commits: e9710e95 (mouse sites; the one wheel site was rebuilt on
      the Qt6 QWheelEvent constructor in 0c4525d9)
- [x] `QRegExp` -> `QRegularExpression` - **not always behavior-
      identical**: latte-dock-qt6 needed a dedicated regression test
      after this subtly changed the " - [0-9]+" copy-suffix match in
      `Importer::uniqueLayoutName`. Verify each nontrivial pattern,
      don't assume equivalence
      Commits: 6baf2603 (all five sites are the same copy-suffix
      pattern, converted to the form latte-dock-qt6 regression-tested;
      the pinning unit test landed with the harness:
      tests/importerregressiontest.cpp)
- [x] Drop the removed `QDesktopWidget` include and its usages
      Commits: 16f47bf1 (include only, there were no usages)
- [x] `emit`/`signals`/`slots` -> `Q_EMIT`/`Q_SIGNALS`/`Q_SLOTS` (KDE
      compiler settings strictness)
      Commits: 46484d25
- [ ] Wrap bare string literals in `QStringLiteral`/`QLatin1String`
      (`QT_NO_CAST_FROM_ASCII`/`_BYTEARRAY` - latte-dock-ng wrapped
      ~720 sites doing this). Sequencing freedom: if this churn is
      blocking the compile milestone, `remove_definitions()` the two
      flags temporarily with a stub-tracked follow-up and land the
      wrap as its own pass - it has no interaction with anything else
      in this phase
      Commits:
- [ ] C-style casts -> `static_cast<>()`
      Commits:
- [x] `qrand` -> `QRandomGenerator`
      Commits: 478efa54 (single site, infoview)
- [x] `QButtonGroup::idToggled` rename (from the removed
      `buttonToggled`/similar Qt5 signal)
      Commits: 1ae47bd6
- [x] `ManagedTextureNode` -> `QSGSimpleTextureNode`
      Commits: ef7e26f9 (single site, IconItem; adds setOwnsTexture)
- [ ] `QDBusInterface` -> `QDBusMessage`/`QDBusConnection::call()`
      throughout - Qt 6.8+ deprecated the `serviceOwnerChanged` signal
      `QDBusInterface` connects to internally, otherwise every D-Bus
      call site prints a deprecation warning
      Commits:
- [x] Milestone: compiles (a lot still functionally broken/stubbed -
      mark those stubs per the convention in `CLAUDE.md`)
      Commits: e9710e95 (full tree, both WITH_X11 variants, via
      `scripts/build-check.sh`; open stubs: wayland skipTaskBar,
      infoview setOnActivities, activity stopping, QStringLiteral
      compiler flags still relaxed)

### Phase 3: KF5->KF6 framework API migration

- [x] `KActivities` includes -> `PlasmaActivities` (C++ namespace stays
      `KActivities`)
      Commits: 97900be8
- [x] Replace removed `KActivities::Info::State`/
      `Consumer::runningActivities()` with a helper that reads activity
      state directly via `org.kde.ActivityManager` D-Bus
      (`ListActivitiesWithInformation`), mapped onto a locally-defined
      `Activity::State` enum. Without this every activity reads as
      "running" (stopped layouts load, scroll-cycling includes dead
      activities, settings dialog can't bold the real one)
      Commits: 97900be8 (`app/data/activitiesinfo.{h,cpp}`), 6b53a5ee
      (scroll-cycling consumer). Discovery recorded there: Plasma 6
      also removed activity start/stop entirely, see Phase 8 stub
- [x] `ConfigPropertyMap` -> `KConfigPropertyMap` (header:
      `KConfigQml/KConfigPropertyMap`)
      Commits: 1990d64c, 0c4525d9 (canonical `<KConfigPropertyMap>`
      include; the KF6::ConfigQml link target landed in b41667ec)
- [x] `Plasma::Svg`/`FrameSvg`/`Theme` -> `KSvg::Svg`/`FrameSvg`/`ImageSet`
      Commits: ef7e26f9, 785ab8f7 (Svg/FrameSvg fully; Plasma::Theme
      itself survives in libplasma 6 and stays - only its ColorGroup
      enum died, replaced by KSvg::Svg::ColorSet)
- [x] `QmlObjectSharedEngine` -> `PlasmaQuick::SharedQmlEngine`;
      `QuickViewSharedEngine` -> `PlasmaQuick`
      Commits: 6baf2603, 16f47bf1, 0c4525d9; e9710e95 handles
      engine() now returning std::shared_ptr
- [x] `KMimeTypeTrader`/`KServiceTypeTrader` -> `KApplicationTrader`
      Commits: 6b53a5ee (all nine trader-language queries in the
      vendored tasktools became lambda filters)
- [ ] `KWindowEffects` signature change: WId -> `QWindow*` on KF6.
      Every `enableBlurBehind`/`enableBackgroundContrast` call passes
      `m_view->winId()` today (`view/effects.cpp`,
      `view/settings/primaryconfigview.cpp`,
      `view/settings/widgetexplorerview.cpp`) - pass the window
      object itself
      Commits: 0c4525d9
- [x] Audit the WId-based `KWindowSystem` calls KF6 moved to
      `KX11Extras`: on the X11 path they become
      `KX11Extras::setOnActivities`/`compositingActive`/etc. behind
      `HAVE_X11`; the Wayland path needs its own answer per call site
      - `setOnActivities(winId(), ...)` in `infoview.cpp` needs a
      Wayland-side replacement or an explicit decision to skip it
      there, and `compositingActive()` in `visibilitymanager.cpp` is
      always true under Wayland (fold the conditional away on that
      branch, don't fake-port it)
      Commits: 478efa54, 0c4525d9 (Latte::compositingActive() helper),
      1ae47bd6; infoview's wayland side is a marked stub
- [x] KPackage metadata pass: add
      `"X-Plasma-API-Minimum-Version": "6.0"` to every package
      `metadata.json` (shell, containment, plasmoid, all three
      indicators) - Plasma 6 treats a QML package without it as a
      Plasma 5 leftover and refuses to load it, so nothing renders no
      matter how correct the code is (latte-dock-ng's port added
      exactly this key). Clean out the KF5-era `ServiceTypes` arrays
      while there; `X-Plasma-MainScript` is ignored on Plasma 6 (main
      file location is fixed by convention, and `ui/main.qml` already
      matches it)
      Commits: 7a1e63ae
- [x] `KNS3` -> `KNSWidgets`
      Commits: 0835b22c
- [x] `Plasma::Package` -> `KPackage`
      Commits: 6baf2603, 1ae47bd6, 6b53a5ee (includes, Applet::kPackage
      -> pluginMetaData, addFile/DirectoryDefinition signatures)
- [x] Switch every plain-JSON `KPluginMetaData(QString)` load site to
      `KPluginMetaData::fromJsonFile()` - the bare single-string
      constructor **resolves its argument as a loadable plugin library
      on KF6**, not a parsed JSON file the way it did on KF5. Silent
      failure mode: latte-dock-qt6 lost every indicator's running/
      active dot this way with zero error message
      Commits: 7a1e63ae (both sites were the indicator factory)
- [x] Rename the KNS install-structure key from the KF5-era
      `KPackageType` to KF6's `KPackageStructure` - without this,
      "Get New Indicators..." can't reach any provider
      (`ConfigFileError`, store unreachable)
      Commits: 7a1e63ae
- [x] `#include <Plasma>` -> `<Plasma/Plasma>`; link `KF6::Svg`,
      `KF6::Package`, `KF6::IconWidgets`
      Commits: ef7e26f9, 1990d64c, 785ab8f7, 1ae47bd6
- [x] Add `compat/` shim headers for the nixpkgs-only include-path
      issue (already solved once for latte-dock-ng, carry the solution
      in from day one): `KIconThemes/KIconLoader`,
      `KIconThemes/KIconEffect`, `KGuiAddons/KIconUtils`,
      `KCoreAddons/KSignalHandler`, `KArchive/{KTar,KZip,
      KArchiveEntry,KArchiveDirectory}`, `KConfigQml/KConfigPropertyMap`
      (`__has_include` fallback chain: unprefixed spelling, then
      lowercase real header, then `include_next`)
      Commits: resolved without shims - every framework-prefixed
      include was canonicalized to the plain CamelCase form
      (1990d64c, ef7e26f9, 6baf2603 and friends), which is upstream
      KDE style and needs no include-path tricks; proven by the whole
      port building on nixpkgs from day one

### Phase 4: Window-system backends (Wayland primary, X11 best-effort)

Scope decision (revised from the original Wayland-only plan, on
explicit user request): Wayland is the primary target and the only
one verified live - the author does not run X11. X11 stays as a
best-effort port: it must keep compiling under `HAVE_X11=ON`, gets
the mechanical Qt6/KF6 migration done properly (there is real
upstream-mergeable value here - upstream never dropped X11, both
community forks did), but X11-specific breakage is tracked as a known
issue, never a milestone blocker, and no phase waits on X11 behavior
being right. Opportunistic smoke tests (Xephyr/nested session) are
welcome but not required.

- [x] Keep the `AbstractWindowInterface` split; port
      `XWindowInterface` to Qt6/KF6 mechanically: `QX11Info` ->
      `qGuiApp->nativeInterface<QNativeInterface::QX11Application>()`,
      `KWindowSystem`/`KWindowInfo`/`NETWinInfo`/NETWM includes ->
      `KX11Extras` equivalents, all behind `HAVE_X11`
      Commits: e9710e95 (the file is also excluded from WITH_X11=OFF
      builds and the corona falls back to the wayland interface with
      a warning)
- [x] Port (not strip) the remaining X11-conditional code:
      the xcb RandR native event filter in `PrimaryOutputWatcher`
      (Qt6 changed `QAbstractNativeEventFilter::nativeEventFilter`'s
      result parameter from `long*` to `qintptr*`), X11 branches in
      `ScreenPool` and `GlobalShortcuts`, `QX11Info` in `tasktools`.
      Delete only what is genuinely dead on both paths
      Commits: 478efa54 (primaryoutputwatcher, screenpool's whole X11
      block was dead), 0c4525d9 (globalshortcuts), 6b53a5ee (tasktools
      QX11Info include was unused)
- [x] Move Wayland window tracking off the removed
      `PlasmaWindow::internalId()` to `uuid()`-based ids carried in
      the existing variant `WindowId`; update every id-validity check
      that assumed an integer so it handles both the X11 `WId` case
      and the Wayland string-uuid case (window tracker, sub-windows,
      config views, positioner, main/child window detection)
      Commits: 8e8cdf31, e9710e95 (WindowId became QByteArray rather
      than staying QVariant - Qt6 removed QVariant's comparison
      operators, so the variant design was unsalvageable; empty id =
      invalid on both platforms, X11 ids ride as decimal strings)
- [ ] Remove `View::surface()` (a `PlasmaShellSurface` accessor,
      always null under layer-shell) and its dead callers; gate auto-
      hide/dodge-reveal on `VisibilityManager::revealsOnScreenEdge()`
      instead of the dead surface-null check
      Commits:
- [ ] Bind `kde_output_order_v1` for primary-output detection (Plasma
      6 KWin no longer advertises `kde_primary_output_v1`), treat the
      first output in the order as primary. Without this the dock can
      silently land on the wrong monitor in multi-display setups
      Commits:
- [ ] Implement struts/exclusive-zone reservation via
      `zwlr_layer_shell_v1`'s `exclusive_zone` through LayerShellQt
      directly (`PlasmaShellSurface`'s `PanelBehavior` is deprecated
      and ignored by KWin, reserves nothing). Reserve the zone equal to
      *current visible* panel thickness, not a max-possible-expansion
      value (over-reservation bug latte-dock-ng hit)
      Commits:
- [ ] In multi-screen setups, call `setScreen()` on both the `QWindow`
      and the `LayerShellQt::Window` before configuring the surface, or
      struts land on the wrong monitor
      Commits:
- [ ] Keep the ghost-window's visual surface at ~1px while its
      exclusive zone reserves the full dock thickness (KWin renders
      compositor blur behind the visual surface area independently of
      the exclusive zone - without this a full-height blur bar becomes
      a persistent artifact during visibility/style-mode switches); use
      `LayerBackground`, not `LayerTop`, for the ghost-window layer
      Commits:
- [ ] Position config/edit-mode surfaces via layer-shell anchors +
      margins, never `QWindow::setPosition()` (ignored entirely by a
      wlr-layer-shell surface - missing this breaks the whole edit-mode
      subsystem)
      Commits:
- [ ] Re-anchor layer-shell surfaces on dock edge/alignment *change*,
      not just once at creation - anchoring only at startup leaves the
      surface welded in place if the dock later moves to a different
      edge
      Commits:
- [ ] Give the edit-mode canvas overlay its own anchor set, distinct
      from the dock's strut-reserving edge, or KWin kills the surface
      ("exclusive edge is not of the anchors") whenever the canvas is
      up while the dock is mid-move
      Commits:
- [ ] Center settings windows via layer-shell margins instead of
      anchoring them to the dock's edge (anchored, they stick to
      whichever edge the dock had when they *opened*, not the current
      one)
      Commits:
- [ ] Seed a legal initial size for any layer-shell surface whose
      anchors don't span an axis it has zero size on yet (a single-edge
      Center dock, or an as-yet-unsized edge helper) - the first
      `wl_surface` commit is otherwise protocol-rejected
      Commits:
- [ ] Wire window activation/peek through KWin D-Bus
      (`org.kde.KWin.Effect.WindowView1`, `org.kde.KWin.HighlightWindow`),
      tracked via a service watcher, **with a real fallback to plain
      window cycling** when the effect isn't available - both forks
      found the no-fallback version silently no-ops the click
      Commits:

### Phase 5: QML controls & rendering migration

- [ ] `PlasmaComponents 2.0` -> `3.0` everywhere on the load path (2.0
      is removed in Plasma 6 - an unresolved import hard-fails the
      *whole* QML load, not just the missing control)
      Commits:
- [ ] `PlasmaCore.FrameSvgItem`/`SvgItem`/`Svg` -> the `KSvg` module
      (`org.kde.ksvg`) - moved out of `org.kde.plasma.core`
      Commits:
- [ ] Drop `QtQuick.Controls.Styles.Plasma` and `QtQuick.Dialogs 1.x`
      imports
      Commits:
- [ ] Remove `ExclusiveGroup` usage everywhere - it's gone in Controls
      2, and in both forks' experience every button using one already
      had `checked` driven by an external binding (the group was dead
      weight); drive exclusivity from data directly
      Commits:
- [ ] `Slider.minimumValue`/`maximumValue` -> `from`/`to`; drop
      `tickmarksEnabled` (no Controls 2 equivalent)
      Commits:
- [ ] Collapse frameless/empty-title `GroupBox` wrappers to plain
      `ColumnLayout` (Controls 2 `GroupBox` has no `flat` property)
      Commits:
- [ ] Replace `tooltip`/`iconSource`/`iconName` properties (removed
      from Controls 2 `Button`/`ComboBox`/`ToolButton`) with
      `icon.name` plus the `QQC2.ToolTip` attached property
      (`text:`, `visible: hovered`)
      Commits:
- [ ] `TabBar.currentTab` -> `currentIndex`/`currentItem`
      Commits:
- [ ] Stop redeclaring `implicitWidth`/`implicitHeight` on custom
      controls (`final` on Qt6 base controls - both forks hit this as
      a *compile failure*, not a misbehavior, on
      `TextField`/`ExternalShadow`/`HeaderSwitch`); bind instead
      Commits:
- [ ] Detect a `ListModel` by probing for `.get()`, not
      `Array.isArray()` - a Qt6 sequence-type model needs the `.get()`
      branch
      Commits:
- [ ] Replace removed `PlasmaExtras.ScrollArea` with `QtQuick Controls
      ScrollView` everywhere it's used
      Commits:
- [ ] Port context menus from `PlasmaComponents` `ContextMenu`/
      `MenuItem` (removed) to `org.kde.plasma.extras`'s `Menu`/
      `MenuItem`; hold each item's `Connections` and submenu as named
      properties since `QMenuItem` has no default property anymore
      Commits:
- [ ] Replace bare `theme`/`units` global context properties (removed)
      with explicit `Kirigami.Theme`/`Kirigami.Units`
      Commits:
- [ ] Replace removed `ColorScope` accessors with `Kirigami`/`KSvg`
      theming directly
      Commits:
- [ ] Build a `ShadowedItem`-style wrapper component (`MultiEffect`
      preconfigured as a drop shadow, normalizing the old pixel radius
      into `MultiEffect`'s 0..1 `shadowBlur`) to replace
      `QtGraphicalEffects` drop shadows - **verify the QML type
      actually resolves and renders at runtime early**; latte-dock-qt6
      has an unresolved `LatteComponents.ShadowedItem is not a type`
      bug with exactly this shape of component, never root-caused this
      session
      Commits:
- [ ] Port glow/saturation/colorize/brightness effects (task icon
      states, tooltip masks) from `QtGraphicalEffects` to
      `QtQuick.Effects`'s `MultiEffect`
      Commits:
- [ ] Port scroll-edge shadows, scroll-opacity masks, glow-point
      corner/band gradients to `QtQuick.Shapes` gradients
      Commits:
- [ ] Rewrite icon badge masking (info/progress cutouts) using
      `MultiEffect`'s `maskEnabled`+`maskInverted` from the start - the
      legacy inline GLSL `ShaderEffect` approach **does not compile
      under Qt6 RHI** ("shaders must be preprocessed using qsb"), don't
      port the old shader path first
      Commits:
- [ ] Fix bridge/interface discovery to check the applet item itself
      first (not just its children) for the `latteBridge` property -
      `AppletQuickItem::itemForApplet()` returns the applet's QML root
      (the `PlasmoidItem`) directly on Plasma 6, one level higher than
      Plasma 5's wrapping. Without this, Latte-aware applets (the tasks
      plasmoid) never receive the bridge and silently run with zoom/
      abilities disabled
      Commits:
- [ ] Replace removed `applet.action(name)` calls with
      `Plasmoid.internalAction(name)` / `applet.plasmoid.internalAction(name)`
      - the removed method throws a `TypeError` that can abort an
      entire handler (both Configure and Remove broke this way in one
      fork)
      Commits:
- [ ] Build plasmoid configuration access correctly from the start:
      `AppletQuickItem` has **no static `configuration` property on
      Plasma 6** (latte-dock-ng discovered this after merging our PR,
      via its own `ed0afd054`/`94f87ba66`/`eabf7c89a`). Build a
      `KDeclarative::ConfigPropertyMap` from the applet's
      `contents/config/main.xml` via `KConfigLoader`, attach it as a
      **dynamic** property on the `AppletQuickItem`, and read it back
      via `property()` directly rather than
      `indexOfProperty() >= 0` (a static-property check that's
      meaningless for a dynamic property). When C++ needs the *same*
      config object QML bindings (`plasmoid.configuration.*`) observe -
      not a second, independently-caching one - resolve it through
      `QQmlContext::contextProperty("plasmoid")` from the actual QML
      context, not by constructing a fresh loader. Two independent
      loaders over the same config file cache separately: a write
      through one is invisible to the other, which is exactly how
      latte-dock-ng's Tasks config tab silently did nothing for a long
      time despite looking wired up
      Commits:
- [ ] Defer any `QQmlContext`/`QQmlEngine` access performed from applet-
      configuration code (e.g. an `appletConfiguration()`-style
      accessor) until the config page actually opens, not during early
      containment initialization - latte-dock-ng's `2b0963186` found
      that early access here had unrelated side effects (regressions in
      wheel-scroll task minimize and middle-click close, in a completely
      different subsystem). If a config-population path needs the
      `AppletQuickItem` before the QML engine has created it (e.g. in
      `onAppletAdded()`), defer via a short retry timer
      (latte-dock-ng used ~1-2s `QTimer::singleShot`, retried until the
      graphic object is available) rather than assuming it exists
      synchronously
      Commits:
- [ ] Read dock location changes through `containment.plasmoid`
      (`locationChanged` and `location` moved to the backing applet -
      the containment graphic object no longer emits it itself)
      Commits:
- [ ] Handle the `appletAdded` signal's new `QRectF` geometry hint
      (was `(int x, int y)`) - detect the shape (`typeof x === "object"`)
      or move position logic to C++ entirely; a naive handler assuming
      the old signature silently defaults to (0,0), landing new widgets
      at the wrong end of the dock with no error
      Commits:
- [ ] Audit `Containment.onAppletAdded`/`onAppletRemoved` and other
      signal handlers for whether they need explicit
      `function(applet, rect)` parameters instead of Qt6's deprecated
      implicit parameter injection - verify each one against a real
      running dock, this is inconsistent across signal types
      Commits:
- [ ] Convert every `DropArea` `onDragEnter`/`onDragMove`/`onDrop`
      handler from `function onDrop(event) {...}` form (does **not**
      connect to the signal in Qt6, silently becomes a dead method - both
      forks independently found this) to the arrow-function/binding
      form: `onDrop: (event) => {...}`
      Commits:
- [ ] Audit every `when:`-gated `Binding` for whether it needs
      `restoreMode: Binding.RestoreNone` - Qt6's default changed to
      `RestoreBindingOrValue`, so any transient-gated binding (drag/
      reorder, relocation, edit-mode transition, screen-gap animation,
      zoom-size hold, indexer visible-index state) now snaps back to
      its declared default instead of holding its last value. Both
      forks hit this *repeatedly*, in different subsystems, discovered
      piecemeal - do this as one deliberate audit pass, not as bugs
      come in
      Commits:
- [ ] Contract-test each `Qt.MidButton`/`Qt.MiddleButton` site
      individually rather than blanket-renaming - latte-dock-ng found
      at least one site where keeping the *old* `Qt.MidButton` name was
      required (the new name caused C++/QML double-handling of the same
      event)
      Commits:
- [ ] Replace the `KWindowSystem` creatable-QML-element usage in the
      widget explorer with the KF6 singleton form
      Commits:
- [ ] Rebuild `AppletDelegate` (widget explorer entry) around the
      current upstream Plasma 6 layout (instance-count badge, remove/
      uninstall buttons) - both forks did a substantial rewrite here,
      not an incremental patch; load its context menus from
      `org.kde.plasma.extras`
      Commits:
- [ ] Milestone: **first runnable dock** - starts under a live
      Plasma 6 Wayland session and renders a panel with at least one
      non-tasks applet (tasks needs Phase 6). From this point the
      per-phase verification cadence from the top of this section
      applies: drive each phase's subsystem in a running session
      before ticking its last box
      Commits:

### Phase 6: Task manager subsystem

`org.kde.plasma.private.taskmanager`'s `Backend`/`SmartLauncherItem`
QML types were removed when Plasma 6 folded the Task Manager applet
into C++. There is no drop-in replacement to import.

- [ ] **Decision**: full vendor (copy the jump-list/places/recent-
      document menu actions, process/desktop-file helpers, drag mime/
      url helpers into Latte's own `org.kde.latte.private.tasks` module
      - latte-dock-qt6's approach, self-contained but more surface area)
      vs. a documented compat/fallback shim depending on the system
      module when present (latte-dock-ng's approach, smaller footprint,
      depends on an increasingly-undocumented Plasma internal) -
      recorded decision:
      Commits:
- [ ] If compat-shim route chosen: wire the fallback into the actual
      CMake install target (not just a convenience shell script) -
      latte-dock-ng shipped a gap here where a direct `cmake --install`
      (e.g. a distro ebuild) skipped the fallback entirely and the dock
      came up completely empty with an unhelpful "module ... is not
      installed" error
      Commits:
- [ ] Vendor `SmartLauncherItem`/launcher badge+progress into the tasks
      plugin using the Unity launcher D-Bus API regardless of the
      decision above - neither fork found a reusable Plasma 6 QML
      module for this anymore
      Commits:
- [ ] Wire `activateWindowView`, `windowsHovered`,
      `cancelHighlightWindows` to the KWin D-Bus interfaces from Phase
      4, with the fallback behavior always present
      Commits:
- [ ] Decide and implement grouped-task window preview thumbnails:
      either suppress them on Wayland entirely, or use
      `org.kde.pipewire`'s `PipeWireThumbnail` (unversioned import in
      Plasma 6) with a real fallback to the legacy path - verify the
      `SIGSEGV`-under-`DodgeActive` crash class (stale texture pointer,
      hit in latte-dock-ng) doesn't recur before shipping either choice
      Commits:
- [ ] Grouped-task click activation: fall back to real window cycling
      (`activateNextTask()`) unconditionally rather than assuming a
      window-effect-based path is reliable; skip phantom toplevels
      (headless daemon processes with no real window surface) when
      cycling
      Commits:
- [ ] Audit every enum-driven click/action handler (left-click, middle-
      click, modifier-click, scroll actions, etc.) for completeness
      against the full enum, not just the common cases - latte-dock-ng
      shipped a Tasks config UI offering all 9 `TaskAction` values while
      `TaskMouseArea.qml` actually only handled a subset per click type
      (3 of 9 for left-click, 5 of 9 for middle-click and modifier-
      click); picking an unhandled action from the UI silently fell
      through to a default instead of erroring. Write a completeness
      test per enum/handler pair rather than trusting the handler was
      updated in lockstep with the enum
      Commits:
- [ ] Route wheel events to badges/sub-regions *inside* the tasks
      plasmoid explicitly, not just per-applet: `DragDrop.DropArea`
      blocks wheel event delivery in Qt6, so even though the
      containment-level wheel bridge (Phase 8) already passes wheel
      events through to external applets, anything living *inside*
      Latte's own tasks plasmoid (e.g. an audio-stream badge on a task
      icon) still needs its own explicit hit-test-and-route step (e.g.
      `mapFromItem` from containment -> plasmoid -> badge) since the
      tasks plasmoid itself isn't in the external-applet passthrough
      path
      Commits:

### Phase 7: Widget management, drag-and-drop, edit mode

This phase took the most repeated fix attempts to reach its current
(still imperfect) state in both forks. Research each bolded item below
before implementing, not just before merging.

- [ ] **Widget removal** (the confirmed latte-dock-ng bug): destroy the
      applet container directly in `appletRemoved`, don't defer/park-
      and-wait for a follow-up signal - Plasma 6's
      `Containment::appletRemoved` fires with the applet **already**
      marked `destroyed()` (Plasma 5 delivered it before `destroyed()`
      was set), so a "wait for a follow-up to finish the job" pattern
      waits forever and the widget is never actually removed
      Commits:
- [ ] **Widget add via drag** from the Widget Explorer: decide the
      C++-vs-QML drop-handling ownership split explicitly and keep it
      exclusive per mime type (e.g. Widget-Explorer-originated drops
      identified by `text/x-plasmoidservicename` go through C++ only;
      file/URL drops via `text/uri-list` go through QML `DragDropArea`)
      - Qt6/Wayland's `View::event()` can intercept drag events before
      Kirigami's `DropArea` QML ever sees them, and letting both paths
      partially handle overlapping cases caused a double-widget-
      creation bug in latte-dock-ng
      Commits:
- [ ] Position-aware drop insertion: defer QML item access via a 0ms
      singleShot timer (immediate access breaks the Wayland event
      chain), and carry the intended insertion index as an explicit
      property rather than trusting the position Plasma's own signal
      hands back
      Commits:
- [ ] **Widget add via double-click**: use `TapHandler.onTapped`
      (coexists cleanly with a sibling `DragArea`) rather than
      `MouseArea.onDoubleClicked` (one fork found it could race against
      widget-explorer's own add/remove toggle, needing a debounce timer
      band-aid)
      Commits:
- [ ] **Default insertion position**: implement boundary-applet
      detection (system tray, the Latte tasks plasmoid) once, shared by
      both the drag and double-click add paths, so new widgets land
      just before the boundary rather than at the absolute end
      Commits:
- [ ] **Edit-mode entry/exit detection** - research first: determine
      whether `plasmoid.userConfiguring`/
      `plasmoid.containment.userConfiguring`'s QML change notification
      is actually unreliable on Qt6/Plasma6, and why, before
      implementing anything. Latte-dock-ng went through at least 8
      distinct attempts here (direct property binding, containment
      fallback, explicit signal connections, global block-everything
      overlay, punch-through overlay, polling timers at several
      different intervals in different components, C++-level
      middle-button interception, a timer finally scoped to active
      edit mode only) without landing on something clean. If polling
      turns out to be genuinely necessary, start from whichever of
      those iterations is closest to final, not from scratch
      Commits:
- [ ] **Drag-to-reorder jitter**: read latte-dock-qt6's actual reorder-
      handling source (not just its commit log) to understand why it
      works cleanly there before implementing, since it's the one
      subsystem observed directly (via live testing) to work better in
      one fork than the other. Latte-dock-ng fought this four times
      (escalating cooldown/dead-zone/hysteresis tuning) and it's still
      jittery per direct testing - don't start from that approach
      Commits:
- [ ] Related smoothness finding (parabolic hover-zoom, not drag-
      reorder, but the same class of problem and a real pattern worth
      copying): keep an item's parabolic-zoom `MouseArea` **always
      visible** and compute scale synchronously in its own
      `onPositionChanged`, rather than hiding the `MouseArea` once an
      item becomes the "current" parabolic item and routing updates
      through a C++ signal with `Qt::QueuedConnection`. Latte-dock-ng
      found this exact difference between widget icons (originally
      routed through the C++ queued path, visibly stuttery) and task
      icons (always-synchronous, smooth) in its `0deca9e18` - the
      queued/hidden-MouseArea approach compounds with any lock/
      debounce timer (its `MIN_SWITCH_INTERVAL_MS`/nullifier timers) to
      produce visible stutter. Apply "always-visible MouseArea, local
      synchronous calculation" as the default pattern for any hover-
      driven visual effect, not just where it was found
      Commits:
- [ ] Investigate and fix whatever causes **icons to get stuck behind
      other elements** after repeated drag-reordering (new bug found
      via live testing in latte-dock-ng, not yet root-caused in either
      fork - likely a z-order or reparenting mistake during/after the
      reorder animation)
      Commits:
- [ ] If any edit-mode interaction needs to be blocked, put the guard
      inside the specific handler that matters (e.g. an explicit
      `!editMode` check in the click/context-menu handler) rather than
      a blanket occlusion overlay - Qt6 Quick's pointer-handler delivery
      (`DragHandler`, used for applet sort-drag) bypasses `MouseArea`-
      based overlays entirely, so an overlay meant to block everything
      except drag-to-reorder does not actually block a
      `DragHandler`-driven drag no matter how it's layered
      Commits:

### Phase 8: Layout/config persistence, session shutdown, multi-screen

- [ ] Implement session shutdown/logout handling as one deliberate
      pattern rather than iterating - latte-dock-ng's end state (worth
      adopting as the *starting* implementation, not rediscovering):
      `setQuitLockEnabled(false)` (a `KJob` can otherwise silently
      suppress `QCoreApplication::quit()` during logout); a 5-second
      poller fallback for shutdown detection, checked only after the
      confirmation phase (Wayland has no XSMP session management, so
      `commitDataRequest` may simply never fire); the synchronous D-Bus
      shutdown-check call's timeout reduced from the default 25s to
      ~1s (25s can block the main thread through compositor teardown);
      async-signal-safe `SIGINT`/`SIGTERM` handling via a self-pipe
      (not relying on `signalfd`, which can silently miss signals on
      some platforms)
      Commits:
- [ ] Unload the Corona's dependents in explicit dependency order
      (`unload` before `setParent(nullptr)` before delete) rather than
      ad-hoc deletion, to avoid a double-free when deferred-delete
      events later reference already-gone objects; detach any shared
      QML engine from `QApplication` before app teardown so it isn't
      deleted twice (once by Qt's child-deletion, once by its own
      shared_ptr)
      Commits:
- [ ] Fix the startup retry-exhaustion deadlock in
      `LayoutManager::restore()`: gate `shouldRetry` explicitly on
      retry count rather than letting it stay permanently true when
      max retries are exhausted but `expectedAppletCount > 0` - without
      this the dock never starts its "restored" timer and sits
      positioned off-screen (-9999,-9999) forever with no visible error
      Commits:
- [ ] Guard any code that reads a window/activity/audio tracker object
      during early startup with an explicit "is this tracker actually
      ready yet" property, rather than assuming it's non-null - both
      forks hit null-dereference-during-startup bugs of this shape
      (latte-dock-ng's `3a1aeaf53`: `EnvironmentActions` accessed
      `selectedWindowsTracker`/`lastActiveWindow` before the window
      tracker had initialized). A general instance of "don't assume an
      async-initialized dependency is ready just because your own
      component finished constructing"
      Commits:
- [ ] Fix multi-screen cloned-view applet-order sync: add an explicit
      "if this initialization completion made `structuralSyncReady()`
      become true, perform the deferred order sync now" path - a
      clone's containment can finish initializing and receive its
      first `appletDataCreated` signal before the sync guard is
      actually true, and nothing re-triggers the sync once both sides
      finish otherwise
      Commits:
- [ ] Fix multi-screen palette divergence: pin
      `Kirigami.Theme.inherit: false` with an explicit `colorSet`, and
      set `KDE_COLOR_SCHEME_PATH` explicitly and early (in the `View`
      constructor) so every view starts from the same palette - each
      `QQuickWindow` can otherwise resolve its `KDEPlatformTheme`
      palette independently on Plasma 6/Wayland
      Commits:
- [ ] Name the context-menu plugin's built `.so` to exactly match its
      `KPlugin::Id` (e.g. `org.kde.latte.contextmenu.so`) - KF6 derives
      a containmentactions plugin's id from its **file name**, not the
      metadata's embedded id, and Plasma's lookup-by-id silently fails
      otherwise
      Commits:
- [ ] Explicitly re-assert the default `RightButton` -> context-menu-
      plugin mapping whenever a containment is wired up, rather than
      trusting it to persist from saved layout config - Plasma 6 no
      longer restores a containment's configured mouse-action plugins
      from saved config on its own
      Commits:
- [ ] Implement a per-applet-type wheel-event bypass list (system
      tray, general external applets like volume/brightness/media) so
      they receive their own wheel events past the containment-level
      global wheel handler, while keeping the Latte tasks plasmoid's
      area excluded from that bypass (its area is where containment-
      level scroll actions are supposed to intercept)
      Commits:

### Phase 9: Theming, colorization, multi-monitor visual polish

- [ ] Audit every Plasma/Kirigami color-group property read against
      what object is genuinely guaranteed to be present at that call
      site, not what's true in the common case - reading a property
      that doesn't exist on whatever theme object is actually in scope
      evaluates to `undefined` in QML silently (no warning, no crash,
      just a wrong color - both forks hit this as literally invisible
      UI, e.g. black indicator dots on a dark panel)
      Commits:
- [ ] For panel-contrast elements specifically, read the `Header` color
      group (`[Colors:Header]` in kdeglobals) rather than whichever
      `Theme` object is nearest at hand (which usually resolves the
      *window* scheme) - needed for mixed-theme setups (dark panel +
      light window scheme or vice versa)
      Commits:
- [ ] Audit for other properties assumed present on the containment
      graphic object that Plasma 6 actually removed rather than
      deprecated (e.g. `backgroundHints`, which silently takes the
      undefined branch of any comparison rather than erroring)
      Commits:

### Phase 10: Stabilization / verification

- [ ] Verify against the full known-bug list at the top of this doc by
      actually driving each interaction (add/remove/drag/edit-mode/
      right-click/task-manager) in a running session - not by reading
      the code and concluding it looks right; both forks' history shows
      real, reproducible, user-facing bugs went unnoticed for a long
      time under exactly that kind of confidence
      Commits:
- [ ] Stand up the e2e harness deferred from Phase 0's
      test-infrastructure decision (real widget add/remove driven
      through KWin D-Bus with actual screenshot capture, modeled on
      latte-dock-qt6's) and verify the coverage ratchet baseline is
      still honest against the Phase 0 standard
      Commits:

### Phase 11: Nix packaging + Docker build verification

Directly reusable knowledge from this session, not new research -
confirmed transferable today when the same include-path fix was
applied to get latte-dock-qt6 building on Nix during live debugging.
The bare build toolchain moved to Phase 0; this phase is the
polished, distributable form of it.

- [ ] Write `default.nix` (Qt6/KF6 dependency list, matching Phase 1-3
      framework choices). Use `lib.cleanSource ./.` for `src`, not bare
      `./.` - a plain `./.` copies the *entire* working tree into the
      Nix store, including a stale `build/` directory (a `CMakeCache.txt`
      with host paths causes a CMake source-path mismatch inside the
      Nix build) and anything like a `.codegraph/` daemon socket file
      (Nix literally cannot copy a socket, hard build failure).
      `lib.cleanSource` filters via the repo's own `.gitignore`
      patterns, which already exclude both - latte-dock-ng hit this
      exact failure post-merge and fixed it in `40daf331e`
      Commits:
- [ ] Write `flake.nix` exposing `packages.default`, `overlays.default`,
      `nixosModules.default`
      Commits:
- [ ] Confirm the Phase 3 `compat/` include-path shims are sufficient
      for the Nix build (they should be, since they were written with
      this in mind, but verify)
      Commits:
- [ ] Add a `nixos` target to Docker-based build verification, matching
      the pattern already built for latte-dock-ng
      (`Dockerfile.nixos` + `verify-nix-nixos.sh`)
      Commits:

### Phase 12: Upstream contribution prep

- [ ] Reality-check the upstream target **early, not here** (this box
      exists so the check isn't forgotten, but do it around Phase
      2-3): upstream latte-dock has had no real development since
      2022 (only po/docbook syncs since), so find out whether the
      invent.kde.org repo still accepts MRs and whether anyone
      reviews there. Two upstream-facing decisions in this plan are
      big enough to sink an unannounced mega-MR regardless: X11
      demoted to a best-effort, author-untested path (softer than the
      forks' outright removal, but still a support-level change
      upstream would need to own) and the Phase 6 task manager
      vendoring. Raise the direction with KDE (an issue or draft MR
      describing this plan) before the work is done, not after. If upstream turns out to be dead for review purposes,
      the fallback is publishing this fork as a maintained
      continuation - every item below still pays off as code quality
      work either way
      Commits:
- [ ] Pass over the whole diff for KDE coding style compliance
      Commits:
- [ ] Verify REUSE/SPDX license header compliance across
      new/modified files
      Commits:
- [ ] Split the accumulated work into reviewable-sized chunks/merge
      requests rather than one enormous diff
      Commits:
- [ ] Submit via invent.kde.org (KDE's GitLab - the GitHub mirror is
      not where KDE reviews happen)
      Commits:

## Status

Phases 0-2 done, Phase 3 nearly done, Phase 4 started. The compile
milestone is reached: the full tree configures, compiles and links in
both WITH_X11 variants via `scripts/build-check.sh` (Qt 6.11.1 /
KF 6.27.0 / libplasma 6.7.2). 37 of 127 items ticked.

Still open from Phases 2-3, all deliberately deferred hygiene: the
~720-site QStringLiteral wrap (compiler flags stubbed off),
C-style-cast cleanup, QDBusInterface -> QDBusMessage conversion.
Everything else through the compile milestone is done, including the
KPackage metadata pass, the KNS structure key, fromJsonFile, and the
ctest harness with the uniqueLayoutName regression test.

Nothing has been run yet; the first runnable milestone is end of
Phase 5 (wayland backend + QML load path first).