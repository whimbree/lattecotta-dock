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
explicit decision): Wayland is the primary target and the only
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
- [x] Remove `View::surface()` (a `PlasmaShellSurface` accessor,
      always null under layer-shell) and its dead callers; gate auto-
      hide/dodge-reveal on `VisibilityManager::revealsOnScreenEdge()`
      instead of the dead surface-null check
      Commits: cf05d856 (the whole plasma-shell surface path is gone
      from every Latte window, not just the View)
- [ ] Bind `kde_output_order_v1` for primary-output detection (Plasma
      6 KWin no longer advertises `kde_primary_output_v1`), treat the
      first output in the order as primary. Without this the dock can
      silently land on the wrong monitor in multi-display setups
      Commits: 958ec6aa (kde_primary_output_v1 stays bound as the
      fallback for older sessions)
- [ ] Implement struts/exclusive-zone reservation via
      `zwlr_layer_shell_v1`'s `exclusive_zone` through LayerShellQt
      directly (`PlasmaShellSurface`'s `PanelBehavior` is deprecated
      and ignored by KWin, reserves nothing). Reserve the zone equal to
      *current visible* panel thickness, not a max-possible-expansion
      value (over-reservation bug latte-dock-ng hit)
      Commits: 8e9a3dc6 (mapping layer + unit test), fb1302f5 (the
      zone rides the dock's own layer surface; the rect handed to
      setViewStruts is strutsThickness, the visible thickness)
- [x] In multi-screen setups, call `setScreen()` on both the `QWindow`
      and the `LayerShellQt::Window` before configuring the surface, or
      struts land on the wrong monitor
      Commits: 8e9a3dc6 (updateAnchoring() sets both; the
      LayerShellQt::Window call sits behind the Phase 1 cmake probe
      because the method came and went upstream)
- [x] Keep the ghost-window's visual surface at ~1px while its
      exclusive zone reserves the full dock thickness (KWin renders
      compositor blur behind the visual surface area independently of
      the exclusive zone - without this a full-height blur bar becomes
      a persistent artifact during visibility/style-mode switches); use
      `LayerBackground`, not `LayerTop`, for the ghost-window layer
      Commits: fb1302f5 (deviation: the internal strut ghost window is
      deleted outright instead of tuned - the exclusive zone rides the
      dock's own layer surface, so no helper surface exists for KWin
      to paint a blur bar behind; this item's constraints applied only
      to the ng design that kept a ghost)
- [x] Position config/edit-mode surfaces via layer-shell anchors +
      margins, never `QWindow::setPosition()` (ignored entirely by a
      wlr-layer-shell surface - missing this breaks the whole edit-mode
      subsystem)
      Commits: cf05d856 (SubConfigView configures the layer surface
      from initParentView(), before first show)
- [x] Re-anchor layer-shell surfaces on dock edge/alignment *change*,
      not just once at creation - anchoring only at startup leaves the
      surface welded in place if the dock later moves to a different
      edge
      Commits: a2e65f90 (locationChanged/alignmentChanged connect to
      the narrower updateAnchoring() path so the stacking layer and
      focus policy survive the re-anchor)
- [x] Give the edit-mode canvas overlay its own anchor set, distinct
      from the dock's strut-reserving edge, or KWin kills the surface
      ("exclusive edge is not of the anchors") whenever the canvas is
      up while the dock is mid-move
      Commits: 8e9a3dc6 (canvasPlacement mapping + the
      exclusive-edge-is-always-anchored unit test), cf05d856
      (applyCanvasPlacement clears the exclusive edge and carves the
      configure-applets click-through input region; the interactive
      chrome rect was stubbed there and completed in 5e873329),
      ec5d2316 (zone -1 so the overlay is not pushed off the edge by
      the dock's own strut)
- [x] Center settings windows via layer-shell margins instead of
      anchoring them to the dock's edge (anchored, they stick to
      whichever edge the dock had when they *opened*, not the current
      one)
      Commits: cf05d856 (deviation: the settings windows drop their
      anchors entirely and let the compositor centre them - simpler
      than margin math and equally clear of the dock's current edge),
      superseded for the primary window by 0d92f007 (pinned to the
      right end; compositor-centering put it mid-screen over the
      widgets being edited)
- [x] Seed a legal initial size for any layer-shell surface whose
      anchors don't span an axis it has zero size on yet (a single-edge
      Center dock, or an as-yet-unsized edge helper) - the first
      `wl_surface` commit is otherwise protocol-rejected
      Commits: 8e9a3dc6 (seededLayerSize, unit-tested; re-run safely
      on every re-anchor because sized windows pass through untouched)
- [x] Wire window activation/peek through KWin D-Bus
      (`org.kde.KWin.Effect.WindowView1`, `org.kde.KWin.HighlightWindow`),
      tracked via a service watcher, **with a real fallback to plain
      window cycling** when the effect isn't available - both forks
      found the no-fallback version silently no-ops the click
      Commits: folded into Phase 6 - the only invocation site is the
      tasks-plasmoid backend, which does not exist here until the
      Phase 6 vendor-vs-shim decision (latte-dock-qt6 implements this
      inside its vendored plasma-desktop Backend); Phase 6 already
      carries the wiring item and the cycling-fallback item

### Phase 5: QML controls & rendering migration

- [x] `PlasmaComponents 2.0` -> `3.0` everywhere on the load path (2.0
      is removed in Plasma 6 - an unresolved import hard-fails the
      *whole* QML load, not just the missing control)
      Commits: b474adad (plus 523e04a4
      for the three indicator packages the compile gate caught still
      on Plasma 5 QML)
- [x] `PlasmaCore.FrameSvgItem`/`SvgItem`/`Svg` -> the `KSvg` module
      (`org.kde.ksvg`) - moved out of `org.kde.plasma.core`
      Commits: b474adad, 523e04a4
- [x] Drop `QtQuick.Controls.Styles.Plasma` and `QtQuick.Dialogs 1.x`
      imports
      Commits: b474adad (EffectsConfig moved to the
      Qt6 QtQuick.Dialogs, which is a different, live module)
- [x] Remove `ExclusiveGroup` usage everywhere - it's gone in Controls
      2, and in both forks' experience every button using one already
      had `checked` driven by an external binding (the group was dead
      weight); drive exclusivity from data directly
      Commits: b474adad
- [x] `Slider.minimumValue`/`maximumValue` -> `from`/`to`; drop
      `tickmarksEnabled` (no Controls 2 equivalent)
      Commits: b474adad
- [x] Collapse frameless/empty-title `GroupBox` wrappers to plain
      `ColumnLayout` (Controls 2 `GroupBox` has no `flat` property)
      Commits: b474adad
- [x] Replace `tooltip`/`iconSource`/`iconName` properties (removed
      from Controls 2 `Button`/`ComboBox`/`ToolButton`) with
      `icon.name` plus the `QQC2.ToolTip` attached property
      (`text:`, `visible: hovered`)
      Commits: b474adad
- [x] `TabBar.currentTab` -> `currentIndex`/`currentItem`
      Commits: b474adad
- [x] Stop redeclaring `implicitWidth`/`implicitHeight` on custom
      controls (`final` on Qt6 base controls - both forks hit this as
      a *compile failure*, not a misbehavior, on
      `TextField`/`ExternalShadow`/`HeaderSwitch`); bind instead
      Commits: b474adad
- [x] Detect a `ListModel` by probing for `.get()`, not
      `Array.isArray()` - a Qt6 sequence-type model needs the `.get()`
      branch
      Commits: b474adad (ComboBox probes typeof
      model.get === "function")
- [x] Replace removed `PlasmaExtras.ScrollArea` with `QtQuick Controls
      ScrollView` everywhere it's used
      Commits: b474adad
- [x] Port context menus from `PlasmaComponents` `ContextMenu`/
      `MenuItem` (removed) to `org.kde.plasma.extras`'s `Menu`/
      `MenuItem`; hold each item's `Connections` and submenu as named
      properties since `QMenuItem` has no default property anymore
      Commits: b474adad
- [x] Replace bare `theme`/`units` global context properties (removed)
      with explicit `Kirigami.Theme`/`Kirigami.Units`
      Commits: b474adad
- [x] Replace removed `ColorScope` accessors with `Kirigami`/`KSvg`
      theming directly
      Commits: b474adad
- [x] Build a `ShadowedItem`-style wrapper component (`MultiEffect`
      preconfigured as a drop shadow, normalizing the old pixel radius
      into `MultiEffect`'s 0..1 `shadowBlur`) to replace
      `QtGraphicalEffects` drop shadows - **verify the QML type
      actually resolves and renders at runtime early**; latte-dock-qt6
      has an unresolved `LatteComponents.ShadowedItem is not a type`
      bug with exactly this shape of component, never root-caused this
      session
      Commits: b474adad (the wrapper), 523e04a4
      (tests/qml/tst_shadoweditem.qml resolves the type through the
      real module import and pins the px->shadowBlur math - the
      resolution check the fork never had)
- [x] Port glow/saturation/colorize/brightness effects (task icon
      states, tooltip masks) from `QtGraphicalEffects` to
      `QtQuick.Effects`'s `MultiEffect`
      Commits: b474adad (containment/
      declarativeimports side; the tasks plasmoid's copies are Phase 6)
- [x] Port scroll-edge shadows, scroll-opacity masks, glow-point
      corner/band gradients to `QtQuick.Shapes` gradients
      Commits: b474adad
- [x] Rewrite icon badge masking (info/progress cutouts) using
      `MultiEffect`'s `maskEnabled`+`maskInverted` from the start - the
      legacy inline GLSL `ShaderEffect` approach **does not compile
      under Qt6 RHI** ("shaders must be preprocessed using qsb"), don't
      port the old shader path first
      Commits: b474adad (containment side; the tasks
      plasmoid's badges are Phase 6)
- [x] Fix bridge/interface discovery to check the applet item itself
      first (not just its children) for the `latteBridge` property -
      `AppletQuickItem::itemForApplet()` returns the applet's QML root
      (the `PlasmoidItem`) directly on Plasma 6, one level higher than
      Plasma 5's wrapping. Without this, Latte-aware applets (the tasks
      plasmoid) never receive the bridge and silently run with zoom/
      abilities disabled
      Commits: b474adad (AppletIdentifier.js probes the
      applet root first)
- [x] Replace removed `applet.action(name)` calls with
      `Plasmoid.internalAction(name)` / `applet.plasmoid.internalAction(name)`
      - the removed method throws a `TypeError` that can abort an
      entire handler (both Configure and Remove broke this way in one
      fork)
      Commits: b474adad
- [x] Build plasmoid configuration access correctly from the start:
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
      Commits: 33830b2c (deviation: the containment
      configuration resolves through the applet chain
      PlasmoidItem.plasmoid -> configuration - Plasma 6's Applet
      carries the map, so no second KConfigLoader is ever built and
      the single-loader rule holds by construction; the indicator
      package config kept its own KConfigLoader from Phase 3 since
      indicators are not applets)
- [x] Defer any `QQmlContext`/`QQmlEngine` access performed from applet-
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
      Commits: 33830b2c (appletConfiguration() is only
      called from config-page paths; nothing reads engine state during
      early containment init - verified against the live startup run)
- [x] Read dock location changes through `containment.plasmoid`
      (`locationChanged` and `location` moved to the backing applet -
      the containment graphic object no longer emits it itself)
      Commits: b474adad
- [x] Handle the `appletAdded` signal's new `QRectF` geometry hint
      (was `(int x, int y)`) - detect the shape (`typeof x === "object"`)
      or move position logic to C++ entirely; a naive handler assuming
      the old signature silently defaults to (0,0), landing new widgets
      at the wrong end of the dock with no error
      Commits: b474adad (main.qml reads
      geometryHint.x/y)
- [x] Audit `Containment.onAppletAdded`/`onAppletRemoved` and other
      signal handlers for whether they need explicit
      `function(applet, rect)` parameters instead of Qt6's deprecated
      implicit parameter injection - verify each one against a real
      running dock, this is inconsistent across signal types
      Commits: b474adad (explicit parameters
      everywhere; per-handler live verification continues through
      Phases 6-8 as each subsystem is driven)
- [x] Convert every `DropArea` `onDragEnter`/`onDragMove`/`onDrop`
      handler from `function onDrop(event) {...}` form (does **not**
      connect to the signal in Qt6, silently becomes a dead method - both
      forks independently found this) to the arrow-function/binding
      form: `onDrop: (event) => {...}`
      Commits: b474adad
- [x] Audit every `when:`-gated `Binding` for whether it needs
      `restoreMode: Binding.RestoreNone` - Qt6's default changed to
      `RestoreBindingOrValue`, so any transient-gated binding (drag/
      reorder, relocation, edit-mode transition, screen-gap animation,
      zoom-size hold, indexer visible-index state) now snaps back to
      its declared default instead of holding its last value. Both
      forks hit this *repeatedly*, in different subsystems, discovered
      piecemeal - do this as one deliberate audit pass, not as bugs
      come in
      Commits: b474adad (47 annotations),
      cbf198fd (the deliberate single pass over the remaining 55
      sites; the tasks plasmoid is excluded until Phase 6)
- [x] Contract-test each `Qt.MidButton`/`Qt.MiddleButton` site
      individually rather than blanket-renaming.
      CORRECTION 2026-07-08: the earlier "keep Qt.MidButton deliberately"
      decision was WRONG. `Qt.MidButton` was removed from the QML Qt enum in
      Qt6, so it reads as `undefined` and every `mouse.button === Qt.MidButton`
      comparison was always false - task middle-click actions, the middle-click
      click animation, and the empty-area close-active-window were all silently
      dead. All sites are now `Qt.MiddleButton` (TaskMouseArea, ClickedAnimation,
      EnvironmentActions). The "C++/QML double-handling" the old note feared is
      handled properly instead of by keeping a broken enum: EnvironmentActions
      accepts the middle button only while `closeActiveWindowEnabled` is on, so
      when it is off the middle-click falls through to the containment's own
      middle-click action rather than being swallowed. ng reached the same
      conclusion in 613ddcc3b ("Qt.MidButton ... removed in Qt 6"); ng delegates
      the empty-area close to a C++ handler we don't have, so we keep our QML
      handler with conditional acceptance. See docs/ng-upstream-audit.md finding A.
- [x] Replace the `KWindowSystem` creatable-QML-element usage in the
      widget explorer with the KF6 singleton form
      Commits: b474adad
- [x] Rebuild `AppletDelegate` (widget explorer entry) around the
      current upstream Plasma 6 layout (instance-count badge, remove/
      uninstall buttons) - both forks did a substantial rewrite here,
      not an incremental patch; load its context menus from
      `org.kde.plasma.extras`
      Commits: b474adad
- [x] Milestone: **first runnable dock** - starts under a live
      Plasma 6 Wayland session and renders a panel with at least one
      non-tasks applet (tasks needs Phase 6). From this point the
      per-phase verification cadence from the top of this section
      applies: drive each phase's subsystem in a running session
      before ticking its last box
      Commits: 5059840e (protocol-verified: KWin
      configures the layer surface at 2560x256 on the bottom edge and
      buffers are attached; analogclock loads, the tasks plasmoid
      correctly waits for Phase 6; found and fixed a startup segfault
      in winIdFor() and three latteView-null QML errors)
      Resolved in follow-up live debugging: 7ca33972 (deferred startup
      slide-in), fb98037c (the missed itemForApplet conversion in
      View::init that kept latteView null in QML, and the Qt6 wayland
      QWindow::setMask damage-clipping behavior that froze the surface
      transparent). The dock visibly renders on the live session with
      working struts - screenshot-verified

### Phase 6: Task manager subsystem

`org.kde.plasma.private.taskmanager`'s `Backend`/`SmartLauncherItem`
QML types were removed when Plasma 6 folded the Task Manager applet
into C++. There is no drop-in replacement to import.

- [x] **Decision**: full vendor (copy the jump-list/places/recent-
      document menu actions, process/desktop-file helpers, drag mime/
      url helpers into Latte's own `org.kde.latte.private.tasks` module
      - latte-dock-qt6's approach, self-contained but more surface area)
      vs. a documented compat/fallback shim depending on the system
      module when present (latte-dock-ng's approach, smaller footprint,
      depends on an increasingly-undocumented Plasma internal) -
      recorded decision:
      Commits: 14c973b3 - decision: full vendor,
      latte-dock-qt6's approach. The compat shim depends on an
      increasingly undocumented Plasma internal and latte-dock-ng's
      variant shipped a real packaging gap (see next item)
- [x] If compat-shim route chosen: wire the fallback into the actual
      CMake install target (not just a convenience shell script) -
      latte-dock-ng shipped a gap here where a direct `cmake --install`
      (e.g. a distro ebuild) skipped the fallback entirely and the dock
      came up completely empty with an unhelpful "module ... is not
      installed" error
      Commits: not applicable - the vendor route was chosen, there is no
      fallback install step to wire
- [x] Vendor `SmartLauncherItem`/launcher badge+progress into the tasks
      plugin using the Unity launcher D-Bus API regardless of the
      decision above - neither fork found a reusable Plasma 6 QML
      module for this anymore
      Commits: 14c973b3 (smartlauncherbackend/item in plasmoid/plugin,
      Unity launcher D-Bus API, provenance preserved)
- [x] Wire `activateWindowView`, `windowsHovered`,
      `cancelHighlightWindows` to the KWin D-Bus effect interfaces
      (this item absorbed the deferred Phase 4 item; the full spec
      lives here now): `org.kde.KWin.Effect.WindowView1` for grouped-
      task window view, `org.kde.KWin.HighlightWindow` for hover peek,
      availability tracked via a `QDBusServiceWatcher` (register and
      unregister both), **with a real fallback to plain window cycling
      when the effect isn't available** - both forks found the
      no-fallback version silently no-ops the click
      Commits: 14c973b3 (vendored Backend carries the QDBusServiceWatcher on
      org.kde.KWin.Effect.WindowView1 for register and unregister;
      main.qml connects activateWindowView/windowsHovered to it and
      TaskMouseArea gates on backend.windowViewAvailable with the
      cycling path always reachable)
- [x] Decide and implement grouped-task window preview thumbnails:
      either suppress them on Wayland entirely, or use
      `org.kde.pipewire`'s `PipeWireThumbnail` (unversioned import in
      Plasma 6) with a real fallback to the legacy path - verify the
      `SIGSEGV`-under-`DodgeActive` crash class (stale texture pointer,
      hit in latte-dock-ng) doesn't recur before shipping either choice
      Commits: 14c973b3 - decision: org.kde.pipewire (unversioned, Plasma 6)
      through the newest version-ladder rung
      (PipeWireThumbnail.5.26.qml); the dead 5.24/5.25 rungs are
      compile-gate-skipped. The DodgeActive stale-texture crash class
      is a live-verification watch item for the Phase 10 sweep.
      c25cb3e1 - the 5.26 rung still carried the kpipewire 5 contract
      (enabled flipped from C++), which kpipewire 6 dropped: previews
      rendered as icon fallbacks at opacity 0. Whole ladder replaced
      with plasma-desktop 6's PipeWireThumbnail.qml shape, plus winId
      int->var in ToolTipWindowMouseArea (wayland ids are QString
      UUIDs). Verified live via scripts/tools/fakepointer.c hover loop:
      real thumbnails for unminimized windows, icon fallback for
      minimized ones (upstream behavior)
- [x] Grouped-task click activation: fall back to real window cycling
      (`activateNextTask()`) unconditionally rather than assuming a
      window-effect-based path is reliable; skip phantom toplevels
      (headless daemon processes with no real window surface) when
      cycling
      Commits: 14c973b3 (TaskMouseArea falls back to
      subWindows.activateNextTask() per click type when the effect
      path is unavailable)
- [x] Audit every enum-driven click/action handler (left-click, middle-
      click, modifier-click, scroll actions, etc.) for completeness
      against the full enum, not just the common cases - latte-dock-ng
      shipped a Tasks config UI offering all 9 `TaskAction` values while
      `TaskMouseArea.qml` actually only handled a subset per click type
      (3 of 9 for left-click, 5 of 9 for middle-click and modifier-
      click); picking an unhandled action from the UI silently fell
      through to a default instead of erroring. Write a completeness
      test per enum/handler pair rather than trusting the handler was
      updated in lockstep with the enum
      Commits: e9754810 (extracted the dispatch into
      code/TaskActions.js as the single source of truth; the middle/
      modifier chains collapse into one executeStandardAction executor;
      tst_taskactions.qml pins that every config-offered enum resolves
      to a real command. The offered set is transcribed from the config
      combos - see docs/REVIEW_NOTES.md for the review item)
- [x] Route wheel events to badges/sub-regions *inside* the tasks
      plasmoid explicitly, not just per-applet: `DragDrop.DropArea`
      blocks wheel event delivery in Qt6, so even though the
      containment-level wheel bridge (Phase 8) already passes wheel
      events through to external applets, anything living *inside*
      Latte's own tasks plasmoid (e.g. an audio-stream badge on a task
      icon) still needs its own explicit hit-test-and-route step (e.g.
      `mapFromItem` from containment -> plasmoid -> badge) since the
      tasks plasmoid itself isn't in the external-applet passthrough
      path
      Commits: e9754810 (audit only - no fix needed. The badge
      MouseArea sits in the icon's z:10 Flow above the whole-task
      handler at z:0, so sub-region routing is correct by z-order; the
      preventStealing DropArea is stacked below the task list. The
      cross-boundary "does the containment deliver wheel into the
      plasmoid at all" question depends on the Phase 8 wheel bridge and
      is recorded in docs/testing/live-only.md for live verification)

### Phase 7: Widget management, drag-and-drop, edit mode

This phase took the most repeated fix attempts to reach its current
(still imperfect) state in both forks. Research each bolded item below
before implementing, not just before merging.

- [x] **Widget removal** (the confirmed latte-dock-ng bug): destroy the
      applet container directly in `appletRemoved`, don't defer/park-
      and-wait for a follow-up signal - Plasma 6's
      `Containment::appletRemoved` fires with the applet **already**
      marked `destroyed()` (Plasma 5 delivered it before `destroyed()`
      was set), so a "wait for a follow-up to finish the job" pattern
      waits forever and the widget is never actually removed
      Commits: 33830b2c (LayoutManager::removeAppletItem finalizes
      immediately via destroyAppletContainer instead of parking in
      m_appletsInScheduledDestruction; the deterministic C++ fix is in
      place and matches the prescription. Final "the widget visibly
      disappears" confirmation is a human click-test, queued in
      docs/REVIEW_NOTES.md)
- [x] **Widget add via drag** from the Widget Explorer: decide the
      C++-vs-QML drop-handling ownership split explicitly and keep it
      exclusive per mime type (e.g. Widget-Explorer-originated drops
      identified by `text/x-plasmoidservicename` go through C++ only;
      file/URL drops via `text/uri-list` go through QML `DragDropArea`)
      - Qt6/Wayland's `View::event()` can intercept drag events before
      Kirigami's `DropArea` QML ever sees them, and letting both paths
      partially handle overlapping cases caused a double-widget-
      creation bug in latte-dock-ng.
      RESOLVED WITHOUT NEW CODE, hand-verified working 2026-07-14:
      drag-from-Widget-Explorer adds widgets on the live dock. The
      ownership split exists by construction in this port - our
      View::event() only OBSERVES drag events (setContainsDrag for
      visibility) and sinks them onward; widget creation is owned
      solely by the Plasma 6 containment drop path, so there is no
      second C++ creation path to race (the ng bug came from adding
      one). No double-creation observed; if duplicates ever appear,
      this is the item to reopen. Headless coverage note: fakepointer
      drag can drive explorer->dock drops once a GUI CI vm exists.
      Commits: (no port-side change needed)
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

Qt5-faithful edit mode blueprint (decided live 2026-07-09, see
docs/session-handoff.md for the measurements and
reasoning): the blueprint grid must live inside the dock window, since
two wlr-layer surfaces cannot be interleaved (no dock > grid >
wallpaper stacking across the dock and CanvasConfigView surfaces).

- [x] Step 1: draw the blueprint inside the containment, between the
      dock background and the layouts container, driven by editMode
      Commits: d68b8e8d, d72ee0cd (fill-the-window was wrong: the
      window carries parabolic zoom headroom, so the grid must be
      sized to editThickness at the screen edge like Qt5's visual)
- [ ] Step 2: grow the dock to `latteView.editThickness` in edit mode
      (computed in app/view/view.cpp but consumed nowhere in QML - the
      port dropped that wiring); wire it through Metrics/
      MetricsPrivate/VisibilityManager. Expect iteration on mask,
      strut and input region. May be partly moot: the mask already
      exposes the full window in edit mode and the window is taller
      than editThickness, so evaluate what actually remains (struts,
      input region, slide animation) before wiring anything
      Commits:
- [x] Step 3: stop CanvasConfiguration.qml drawing its own grid so
      there is only one blueprint; the canvas keeps its other jobs
      (settings chrome, input carving in configure-applets mode).
      Done early, out of order: the canvas stacks in front of the
      dock, so its opaque grid hid every applet once step 1 landed
      Commits: 608a509e
- [x] UX: when the rearrange toggle is centered, open the settings
      window to the right side instead of also centered
      (app/view/settings/primaryconfigview.cpp). Grew deliberately
      into the full edit-chrome layout: settings window pinned right
      (wayland was compositor-centering it mid-screen), ruler flush
      with the blueprint top, rearrange toggle at the left end, canvas
      overlay no longer pushed off the edge by the dock's own strut
      Commits: ec5d2316 (exclusive-zone opt-out), 0d92f007 (settings
      window right), 374461cb (ruler + toggle), 5e873329 (toggle stays
      clickable in configure-applets mode, completes cf05d856 stub)

### Phase 8: Layout/config persistence, session shutdown, multi-screen

PHASE OPENED 2026-07-11 (go given), starting from live crashes on a
multi-view, multi-monitor setup.

- [x] Render-thread crash whenever an overflowing dock relayouts (enter
      edit mode, add a widget, right-click an applet in edit mode - all
      one backtrace: buildRenderLists SEGV during QSGRhiLayer::grab).
      The tasks scroll-fade MultiEffect was the vector: mask sampled
      from inside the grabbed subtree, and layer.enabled churning with
      contentsExceed. Deterministic gdb reproduction, fixed and
      verified with a 15-toggle gauntlet
      Commits: df747ebf (fix), ee745859 (fakepointer rightclick used to
      reproduce headlessly)
- [x] The rest of the crash family behind the scroll-fade fix: Qt6
      MultiEffect does not auto-wrap plain Items (Qt5 Colorize and
      DropShadow did), so the applet colorizer and both applet shadow
      variants had INVALID samplers since the port: effects rendered
      nothing (blank colorized applets, no applet shadows) and the
      invalid providers corrupted the scenegraph on representation
      churn (reproduces on the basic loop too, so corruption, not a
      race). Reliable reproducer was a broken Comic Strip applet whose
      representation flapped error/default on every focus change.
      Hand-verified stable after the fix set
      Commits: 73da8400 (providers), c7200e3d (basic render loop
      default, which made the corruption debuggable), df747ebf and
      e88af680 (the two dead-effect removals found on the way)
      FOLLOW-UP 2026-07-13 (28fdca5f): the basic loop was retired once
      the effect audit completed. plasmashell runs threaded on this
      system (8 QSGRenderThreads); the basic loop serialized every
      window's polish/sync/render on the gui thread and measured
      400-610ms frame hitches opening the clock's calendar popup
      (threaded: p50 5ms, p99 6ms, max 41ms, zero >100ms on the same
      interaction - my 'calendar chugs at low fps' report).
      All historical crash recipes ran clean on threaded before the
      flip. QSG_RENDER_LOOP=basic stays as a supported override.
- [x] WebEngine-backed applets (org.kde.plasma.comic) free-run under
      the threaded render loop: ~20% cpu per instance at idle,
      bisected on the 3-dock test layout - exactly the two
      comic-hosting docks spun (5.3k/4.9k unthrottled frames in ~25s)
      while the comic-free dock idled, and a comic-free layout idles
      at 0.1% under threaded.
      NOT REPRODUCIBLE on HEAD (2026-07-14, no code change for it):
      re-measured with the comic restored to the throwaway layout
      (the active copy had lost it - with-comic.bak swapped back in
      and left active, matching the handoff's description), settled
      idle is 0.2-1.6% total with QSGRenderThreads at ~1%, sustained
      over minutes. Changes landed between the round-16 measurement
      and now that plausibly absorbed it: the dialog/appletpopup
      rework (347f413a/f630d2ad/1f8770fd) and the kwindowsystem
      wayland plugin joining the staged env (KWindowShadow no longer
      fails per dialog). If it resurfaces, bisect across that range;
      QSG_RENDER_TIMING names the spinning window. SEPARATE
      observation while re-measuring, not filed as its own item: the
      3-dock throwaway burns 40-100% CPU for the first ~60-90s after
      start, MAIN thread (35%), render threads quiet (~1%) - applet/
      network settling on a heavy layout, not a render loop; the
      my single-dock layout shows nothing similar.
      Commits: (verification only, no change)
- [x] Harden CompactApplet against representation churn: plasma 6
      destroys/recreates compact representations at runtime (observed
      live: rep -> null -> new DefaultCompactRepresentation) while
      CompactApplet.qml keeps parent/anchors/size bindings on the old
      item and never detaches it; also audit its Qt5-style popupWindow
      fullRepresentation reparenting against Plasma 6 AppletQuickItem
      ownership (plasmashell moved applet popups to AppletPopup).
      REGRESSION-TESTED 2026-07-11 evening: re-adding the Comic Strip
      applet after the provider fixes still crashed the killer recipe
      (one 'Texture t1' then the buildRenderLists SEGV), so at least
      one invalid-provider path survives on the churn route. CAVEAT:
      that crash coincided with a 'CLEARED SCREEN :: DP-3' output
      removal (the portrait monitor flaps, second time a crash lands
      exactly on that event), so screen-removal teardown is a co-equal
      suspect and possibly contaminates earlier verdicts too.
      CAVEAT RESOLVED 2026-07-11 (late session): 'CLEARED SCREEN' is
      NOT an output removal. It is Latte's own strut bookkeeping
      (screengeometries.cpp:256), logged whenever an ACTIVE screen
      simply has no space-reserving docks left on it - it fires
      benignly ~20s after every dock start and on relocations
      (confirmed twice while a kernel drm hotplug logger recorded zero
      link events on DP-3). The crash coincidence therefore does not
      implicate screen-removal teardown; treat the churn crash as its
      own defect. For any future crash, get flap ground truth from
      'udevadm monitor --kernel --subsystem-match=drm' instead of this
      log line. Confound control active while establishing this:
      kde-inhibit --power --screenSaver, inhibition verified
      registered with PowerDevil.
      RETESTED CLEAN 2026-07-12 after 4c9f3bc7 (private QML modules
      pinned): Comic Strip now loads silently as a healthy applet -
      the error/default representation flapping WAS the missing
      org.kde.plasma.private.comic module, as this item suspected.
      The killer recipe no longer reproduces (16 focus flips, 4
      edit-mode cycles, 3 edit-mode right-clicks under the gdb
      wrapper: no 'Texture t1', no SEGV, zero drm flap events). The
      structural CompactApplet hardening below remains filed on its
      own observed evidence (rep -> null -> new seen live), but it no
      longer has a crash reproduction behind it; per the
      fix-with-reproduction discipline it waits for a live vector
      (any applet that legitimately swaps representations) rather
      than speculative hardening now.
      LIVE VECTOR FOUND AND FIXED 2026-07-12 (evening): a HEALTHY comic
      applet reproduces the SEGV on hover alone - parabolic zoom grows
      the item past comic's switchWidth/switchHeight (gridUnit*5), and
      Plasma 6 AppletQuickItem flips expanded=true and swaps
      representations by itself, no click involved. Isolation acquitted
      the MultiEffect and both applet layer sites (crash reproduced with
      all three disabled); the defect was Qt5-style representation
      stealing fighting AppletQuickItem ownership, exactly as this item
      suspected. Fixed on the upstream plasma-desktop Plasma 6 pattern
      (plasmoidItem property, marker-walk applet discovery, clean
      release of outgoing representations, popup nodes returned to the
      dock window before teardown). Follow-ups filed below: comic still
      EXPANDS on hover-zoom (behavior question vs Qt5), and the startup
      'No QSGTexture provided' warnings (48/run) proved to come from
      neither the clicked effect nor the shadow/colorizer layers -
      source still unidentified.
      Commits: 4c9f3bc7 (removed the original churn vector), 9ea29eaa
      (churn hardening, the actual fix)
- [ ] Comic/any switch-threshold applet EXPANDS from parabolic zoom
      alone: hovering grows the item past switchWidth/switchHeight and
      AppletQuickItem opens the popup with no click (observed live
      2026-07-12, survives cleanly since 9ea29eaa but the popup opening
      on hover is questionable UX). Decide the Qt5-faithful behavior:
      check how Qt5 AppletQuickItem's compactRepresentationCheck treated
      panel form factors vs size thresholds, and whether Latte should
      pin preferredRepresentation for zoom-resized applets.
      Commits:
- [x] 'No QSGTexture provided from updateSampledImage()' warnings:
      SOURCES IDENTIFIED 2026-07-13 by config bisection. Startup
      baseline ~45: disabling appletShadowsEnabled on all containments
      drops it to 9, so ~36 are the applet ShadowedItem sites (layered
      sources per 73da8400, so these are transient first-frame nulls,
      lower risk). The residual 9 match the task-icon count: TaskIcon
      .qml instantiates THREE MultiEffects per task (stateColorizer,
      hoveredImage, brightnessTaskEffect at lines ~339-368) sampling
      source: badgesLoader.active ? badgesLoader : taskIconItem with
      NO layer on either candidate - the verbatim family 7 anti-pattern
      73da8400 fixed elsewhere but missed here. Warnings also ACCRUE
      during idle runtime (9 -> 45 over minutes with Spotify playing:
      task icon refreshes re-trigger invalid sampling), so this is live
      corruption exposure on every icon update, not just startup. FIX
      SHAPE for its own session: layer the sampled sources per
      73da8400, minding df747ebf (the badgesLoader/taskIconItem ternary
      switching is itself churn) - and give CompactApplet's
      _clickedEffect the same treatment (still unlayered; earlier
      2026-07-12 note: it was NOT the source of these warnings, but it
      is the same latent class).
      FIXED 2026-07-13 exactly to that shape: one anyStateEffectShown
      property gates layer.enabled on taskIconItem/badgesLoader (split
      by badgesLoader.active, mirroring the effects' source ternary),
      effects get visible: opacity>0 so a faded effect cannot sample,
      CompactApplet's flash uses a when-gated Binding with
      RestoreBindingOrValue so the adopted rep is not left permanently
      layered (df747ebf). Verified live: startup 45 -> 7; all residual
      warnings are first-frame bursts within 17s of startup (initial
      map + one dodge re-show repainting applet shadows); ZERO accrual
      over 95s idle and zero across three hover glide passes. Side
      effect worth knowing: the task hover-brighten/colorize/click
      effects actually render now - they were silent no-ops before
      (invalid sampler drew nothing), so tasks visibly brighten on
      hover for the first time since the port. That is Qt5-faithful
      (restoring intended behavior), not a deviation. Badged-icon
      effect rendering unverified (no badge app running); visual-only
      risk if wrong. The ~28 applet-shadow first-frame nulls remain as
      the known lower-risk transient population (layered sources,
      family 7 note in 73da8400).
      Commits: 69baabf0
- [x] SIGSEGV dropping/pinning a launcher onto the tasks applet
      (reproduced by hand 2026-07-13 under the gdb wrapper, full trace at
      syncedlaunchers.cpp:87). Root cause: removeClientObject
      qobject_cast the dying QObject back to QQuickItem inside a
      destroyed() handler - the cast always returns null there because
      the QQuickItem part is already destructed, so client
      unregistration silently never happened and every destroyed tasks
      applet left a dangling pointer that the next launcher drop
      dereferenced. Removal now by pointer identity. Verified with
      temporary counters (clients 3 -> 2 on destruction, dying object
      printing as plain QObject). LESSON recorded: never qobject_cast
      to a derived type inside a destroyed() handler; grep for the
      pattern when touching lifetime code. INCIDENT NOTE: during
      verification a mis-parsed containment id fed removeView and
      deleted the throwaway layout's top dock; restored from
      with-comic.bak (state as of 2026-07-11 evening) and the orphan
      duplicate removed. Parse D-Bus ids with anchored matches and
      prefer non-destructive verification vectors.
      Commits: d6d57e61
- [x] Default indicator main.qml:92 'Unable to assign [undefined] to
      double', 3 per startup: the GlowPoint opacity block fell through
      undefined for not-yet-classified indicators; explicit opacity 0
      terminal return. Verified zero occurrences on a fresh start.
      Commits: 04d8000c
- [x] Right-click context menu on an applet offers no 'Applet Settings'
      entry (caught live 2026-07-12; pre-existing since the port).
      FIXED 2026-07-13: four stacked defects. The ROOT was one level
      deeper than the mapped architecture: Panel.qml resolved its
      viewLayout by scanning containment.children for the
      containmentViewLayout objectName, but Plasma 6 containment roots
      ARE ContainmentItem types so the handed item IS the root carrying
      the name itself - the scan always missed and the delegating
      appletContainsPos silently returned false forever (now warns
      loudly when unresolved). On top: the ContextMenuLayer's
      direct-children method lookup (now recursive, depth-capped), the
      null-deref-before-check fallback (reordered), foreign-window
      coordinate resolution (gated to window()==view), and no layer in
      the dock window at all (the containment now instantiates one at
      the bottom of the stacking order; tasks keep their own menus,
      empty areas keep the containment menu). Standing decision honored:
      the Configure entry is available ALWAYS. Verified live: clock
      right-click shows its section + Configure, activating it opened
      the Digital Clock Settings dialog, re-verified on the clean
      build. NOTE while testing: the containment-only menu placed once
      at the screen corner instead of the click (top dock) - watch for
      recurrence, may be popUpRelevantToGlobalPoint quirk for
      full-window rects.
      Commits: afefa442
- [x] Edit Dock opens the chrome for the WRONG VIEW (caught live
      three times 2026-07-12 night: right-click bottom dock -> chrome
      appeared on the left dock, later the top dock). ROOT CAUSE: the
      per-view m_primaryConfigView pointer to the shared chrome
      singleton went stale. showConfigurationInterface short-circuited
      on a non-null pointer without checking parentView()==this (the
      invariant settingsWindowIsShown already encodes), and the
      view-recreation catch-up path seeded stale pointers by stealing
      the singleton without releasing the previous owner (recreations
      fire on visibility-mode/byPassWM changes, so the state recurred).
      Fixed both ends; verified twice with the chrome deliberately
      parked on the top then the left dock: Edit Dock on the bottom
      dock opened the bottom chrome both times (dumpwins).
      Commits: 66114774
- [x] Edit mode first-open latency (caught live: first open slow,
      subsequent opens fast). Suspect: cold QML compilation of the
      chrome packages (settings pages, canvas) on first
      instantiation; the staged tree carries no qmlcachegen output and
      QML disk cache is invalidated by restaging. Options to evaluate
      in Phase 10: qmlcachegen for installed packages, chrome
      pre-instantiation at startup, measuring with QSG_INFO/QML_PROFILE.
      CORRECTION from the 2026-07-13 startup measurement: the disk
      cache is NOT invalidated by restaging (cmake --install preserves
      source mtimes; ~/.cache/lattedock/qmlcache showed 235/237 entries
      reused across two restarts), so the first-open cost is in-engine
      type instantiation, not bytecode compilation. The startup stack
      capture (see next item) makes chrome warm-up the plausible lever:
      first-open pays the same blocking QQmlTypeLoader walk the views
      pay at startup, and second opens are fast because the engine's
      type cache is warm. Candidate: async-warm the chrome component
      after startup idle. Measure the actual first-open duration before
      building anything.
      MEASURED 2026-07-13 (KWin window-count poller bracketing the
      Edit Dock click to chrome windows visible): COLD 7.3s, WARM
      0.5s on the same engine. The 6.8s delta is one-time chrome
      instantiation: the C++ views construct in ~0.4s, then a 4.8s
      near-silent stall before the windows map. A gdb interrupt
      mid-stall caught the main thread in a recursive Kirigami
      PlatformTheme::updateChildren cascade ending in KSvg
      ImageSet::filePath -> QStandardPaths::locate -> statx, i.e.
      theme propagation through the freshly built control tree, not
      pure QQmlTypeLoader work. IMPORTANT: measured while the idle
      render-loop storm (next item) was active and competing for the
      main thread - re-measure after that item lands before designing
      any warm-up. Pre-creation options and their risks:
      (a) create the chrome ensemble hidden at startup idle - full
      saving, but ViewSettingsFactory::primaryConfigView(view) calls
      setUserConfiguring(true) at creation (the dock would flash edit
      visuals) and the ensemble's focus/map behavior is the
      edit-trilogy minefield; needs a supervised session;
      (b) share the corona's QML engine with the config views so the
      startup type-graph warm-up carries over - architectural change.
      OPTION (a) LANDED 2026-07-13 (fd8cbc45): corona warms the
      ensemble 8s after synchronizer initializationFinished, via a
      showOnCreation=false PrimaryConfigView constructor (wires the
      parent through initParentView() only - no configuring session,
      no userConfiguring flash, no mapped window). Two live-found
      holes closed on the way: the constructor's setParentView()
      unconditionally showed AND started a configuring session (the
      first warmup attempt flash-showed and was closed by the focus
      machinery), and View::showConfigurationInterface relied on
      setParentView()'s show side effect, which early-returns on an
      unchanged parent - the first Edit Dock on the warmed view was a
      silent no-op until the factory path learned to show explicitly
      (gated to parentView()==this so retargets stay on the deferred
      slide-out swap). MEASURED: first open 7.3s -> 1.5s, warm reopen
      unchanged, warmup silent (window count stays 3, 0.1% idle).
      Residual 1.5s = deferred show intervals + mapping; good enough,
      option (b) not needed. Watch item: first open on a NON-warmed
      view goes through the standard retarget (deferred slide-out) -
      untouched code, but the never-shown-ensemble variant was not
      driven headlessly (dodge kept hiding the top dock); it will be
      hit naturally at the desk.
      Commits: fd8cbc45
- [x] TOP PRIORITY: idle render-loop + filesystem-stat storm in the
      applet-shadow path (discovered 2026-07-13 while measuring the
      edit-open latency).
      FIXED same day (e3376405): bisected live in three probe builds
      (ItemWrapper shadow site off -> 0.1%; the source layer alone ->
      0.1%; shadows on with autoPaddingEnabled=false -> 0.1%). ROOT:
      MultiEffect's autoPaddingEnabled recomputes padding and
      re-dirties the effect continuously - the eternal frame
      requester. ShadowedItem now carries a static paddingRect (blur
      extent + max offset per side, output identical). Verified 0.1%
      idle with shadows fully on (was 18.2%), tests extended with the
      padding contract. QSG_RENDER_TIMING named the spinning windows
      first (both shadow-enabled docks, back-to-back EMPTY frames,
      unthrottled because hidden layer surfaces get no vsync pacing).
      Startup/cold-edit-open re-measured after: unchanged - the
      storm's cost was idle CPU and power, not those paths. The KSvg
      negative-cache gap (nonexistent theme colors re-scanned across
      273 XDG_DATA_DIRS entries per query) remains as an upstream
      observation; with the frame loop gone the query rate is normal.
      Original evidence below for the record.
      THE NUMBERS: the idle dock burns 18.2% CPU, all of it on the
      MAIN thread (single-threaded 'basic' QtQuick render loop);
      with appletShadowsEnabled=false on ALL containments it drops to
      1.2%. A statx-filtered strace recorded 5.04M statx calls over
      264s - a FLAT ~19,500/s at idle, 99.8% ENOENT, persisting even
      while every dock was dodge-hidden. The calls are dominated by
      two files that exist NOWHERE in this environment
      (plasma/desktoptheme/default/colors and its translucent
      variant; Breeze uses colorscheme-following instead), resolved
      ~67x/second - frame rate - with each resolution scanning the
      session's enormous XDG_DATA_DIRS (273 entries in the dock
      process, 187 dirs actually probed; NixOS session env).
      plasma_theme_default.kcache is continuously rewritten. Idle
      gdb samples show the main thread inside
      QSGBatchRenderer::Updater/QSGRenderer::preprocess - the
      scenegraph re-renders every frame, forever.
      READING: something in the applet-shadow path (ItemWrapper's
      ShadowedItem Loader + _wrapperContainer layer, possibly the
      TaskIcon shadow sites too) requests a new frame every vsync;
      each frame's preprocess walks KSvg/FrameSvg nodes whose colors
      lookup misses everything and is not negatively cached, so the
      per-frame cost explodes under the wide XDG_DATA_DIRS. The
      frame-request loop is OURS; the stat amplification is
      environmental (libplasma/KSvg negative-cache gap, worth an
      upstream report once pinned).
      NEXT SESSION's attack plan: find the eternal frame requester.
      (1) narrow by config: shadows on ONE containment only, then on
      one applet; (2) QSG_RENDER_TIMING=1 names which window renders;
      (3) suspects to rule in/out: layered _wrapperContainer sampled
      by a sibling MultiEffect that also fills it (double-draw
      feedback), a flapping binding in the shadow chain (zoom/margin
      jitter), MultiEffect autoPadding oscillation;
      (4) QSG_VISUALIZE=changes was tried and is USELESS here - it
      tints the whole (dodge-hidden) layer surface and blackens the
      my screen; do not repeat.
      Also caught on the idle main thread: QtWebEngine performing a
      lazy Vulkan/RhiGpuInfo init ~20s after startup (some applet
      pulls QtWebEngine in) - one-time jank, separate small item.
      FOLLOW-UPS from the fix (both caught live ghost regressions,
      fixed same day): paddingRect components are PER-SIDE extras,
      not totals (6c7001ce, Qt updateSourcePadding() semantics), and
      the sibling shadow pattern (effect sampling a still-visible
      original) double-draws with a few px offset - ItemWrapper and
      ShortcutBadge shadows are layer.effect now (c7c46226).
      Commits: e3376405, 6c7001ce, c7c46226
- [x] Applet popup SLIDE-IN animation still missing (plasma parity;
      desk-driven session 2026-07-13 evening). The slide-OUT works
      (hand-verified) since f630d2ad; the open still animates with a
      generic quick fade (the scale effect - it persisted with KWin's
      fade effect unloaded, and it stayed quick while slidingpopups
      was configured to 1500ms, which is how it was identified as NOT
      slidingpopups). Target behavior: the popup must
      emerge FROM BEHIND the dock, exactly like plasmashell popups
      emerge from their panel (that is the slide offset/clip at the
      panel edge, free once the slide-in engages).
      SOLVED (1f8770fd), hand-verified 'it slides in now'. FINAL
      LAYER: PlasmaQuick::Dialog derives its OWN slide hint from its
      location property inside its first-expose handler, immediately
      before the call that renders and commits the mapping frame; on
      the threaded loop that call BLOCKS until the frame is out, so
      the location-derived hint at that instant is what KWin maps
      with and NOTHING outside the base's call stack can interleave.
      Latte's Floating location meant the base UNSET the slide every
      open - all four re-assert strategies lost by construction.
      Fix: AppletPopup dialogs pin location to the dock edge in
      updatePopUpEnabledBorders, so the base itself requests the
      correct slide in the only slot that decides (side effect,
      deliberate: dock-facing border hidden, the Qt5 attached look).
      EVIDENCE CHAIN so far (do not re-litigate these):
      (1) kwindowsystem wayland backend was missing entirely
      (347f413a) - KWindowEffects was a no-op, shadows also fixed;
      (2) the popup carried the Dock role; type AppletPopup fixed it,
      set_role(7) wire-confirmed, and popupWindow=false in KWin
      Window terms is CORRECT for appletpopup role (not a defect -
      that flag misled a whole probe cycle);
      (3) slide hints reach the wire with valid location, offset -1
      (client-computed offsets from pre-map geometry are garbage and
      make the effect clip the slide to an empty region - keep -1);
      (4) hints are asserted at SurfaceCreated, visibleChanged (after
      the base's location-derived UNSET in the same emission) and
      every position sync;
      (5) PlasmaShellWaylandIntegration re-applies the role itself on
      Qt's per-hide surface recreation (read v6.6.5 source; explicit
      re-apply is a cached no-op) but NOT panelBehavior - we re-apply
      that at SurfaceCreated.
      OPEN LEADS, in order: (a) the KWin probe saw the popup produce
      TWO windowAdded events within milliseconds on some opens (and
      one on others) - a mid-open re-map would cancel slidingpopups'
      in-animation and hand the second map to the scale effect;
      correlate with the xdg_surface destroy/recreate cycles and the
      client-side single visibleChanged; find what re-maps. (b) dump
      KWIN's view: run kwin with WAYLAND_DEBUG or add effect-side
      logging via a nested session to see whether slideOnShowHide()
      is populated at windowAdded. (c) compare a plasmashell popup's
      full wire stream against ours (kickoff via
      plasmashell activateLauncherMenu D-Bus refused to open
      headlessly; the panel calendar needs a real click).
      TOOLING from the session: KWin windowAdded classification
      probe (wininspect2.js pattern), fakepointer scroll, latte
      global-shortcut invocation for applet activation
      (kglobalaccel invokeShortcut 'activate entry N', flaky),
      KWin effect slow-motion (Effect-slidingpopups SlideInTime) and
      effect unloading as discriminators.
      Commits: f630d2ad (role + slide machinery, slide-out works)
- [x] Latte -> Plasma indicator style switch CRASHES the dock
      (caught live 2026-07-13 evening, twice). ROOT-CAUSED with the
      gdb harness (two identical stacks, rdi=0 at the fault):
      KSvg::SvgItem::setSvg(nullptr) null-derefs in
      updateDevicePixelRatio() - KSvg 6 dropped Qt5's if (m_svg)
      guard (verified plasma-framework v5.115.0 vs ksvg 6.27/master;
      upstream ksvg report candidate for Phase 12). The null came
      from org.kde.latte.plasma FrontLayer.qml binding svg:
      resources.svgs[0], which is empty until the QML->C++->QML
      setSvgImagePaths round trip lands - always after the new
      package's items incubate during the switch cascade. Fixed by
      gating the group-arrow Loader on the svg existing (ordering is
      deterministic: the Loader's active binding subscribes before
      any arrow exists, connections activate in establishment order).
      Verified: switch, round trips, cold start with plasma
      persisted, frames render.
      Commits: 841c2ca4
- [x] Staged launcher child-env leak (dock-launched apps inherited the
      dock's whole environment: KIO systemd runner copies it verbatim
      into the transient unit's Environment=, no per-job hook - read at
      kio v6.27.0). QT_PLUGIN_PATH fixed: run-staged.sh unsets it and
      hands the staged plugin tree + kwindowsystem leaf over as
      LATTE_EXTRA_PLUGIN_PATHS, which main.cpp feeds into process-local
      QCoreApplication::addLibraryPath() (both consumers -
      containmentactions lookup and the kwindowsystem backend - search
      libraryPaths()). Verified: right-click menu intact, zero
      KWindowShadow failures, dock-spawned konsole env clean of staging
      dirs. RESIDUAL, accepted for the dev harness (no clean seam),
      re-evaluate at Phase 11 packaging: QML2_IMPORT_PATH still leaks
      (read per-QQmlEngine from env; engines created in many places;
      qt.conf Qml2Imports takes a single dir) - bounded because child
      wrappers prepend their own paths; stage-first XDG_DATA_DIRS leaks
      (latte-only content, benign); QT_QPA_PLATFORMTHEME= (empty)
      leaks, so Qt apps launched from the DEV dock lose platform
      theming (production installs never set any of these).
      Commits: 00a6766c
- [x] Colorizer's shadow site (colorizer/Applet.qml) likely draws an
      UNCOLORIZED copy of the wrapper over the colorized applet while
      colorizing mode is active. CONFIRMED by reading, but it was the
      SMALLEST of three stacked defects: the colorizer itself has been
      a silent no-op since the port because MultiEffect.colorization
      is a luminance-preserving tint (multieffect.frag:84 multiplies
      the target color by source gray), not Qt5's ColorOverlay
      flat-color-through-alpha - both reference forks carry this.
      Peeled with probe builds (lime rect proved the subtree paints,
      forced red+brightness proved the sampler, a raw
      ShaderEffectSource proved the texture pipeline, and QML probes
      proved mustBeShown/applyColor were correct all along). Fixed
      with Qt5Compat ColorOverlay (same pin, added to flake+package)
      and the shadow as the colorizer's layer.effect (the sibling
      arrangement ghost-double-struck text, c7c46226 class).
      Commits: 1f835402
- [x] Settings window (edit chrome) reported overflowing the screen
      top, intermittently. CAUGHT LIVE with a geometry-sampling loop
      (watched on screen as the loop reproduced it): first open shortly after a dock
      restart mapped +99px too tall / 93px past the screen top and
      STAYED wrong; warm reopens always correct. The +99px is the
      dock's own reserved thickness: corona integrates this view's
      reserved screen area late after a restart (~14s post-start,
      measured), the warmed chrome computed availableScreenGeometry
      from the strut-less rect, and the correcting
      availableScreenRectChangedFrom(self) was dropped by upstream
      d30143f7's self-origin exclusion, so the wrong height persisted
      all session. Fixed by accepting self-origin updates (commented
      deviation; churn bounded by the 250ms debounce + syncGeometry
      no-change return). RESIDUAL, Phase 8 family: the window still
      maps at the stale size for ~1.2s on a cold open before
      snapping correct, because corona's rect itself is stale that
      early - the real lever is whatever makes the view's reserved
      area integrate ~10s after the view is already painted; fold
      into the Phase 8 screen-geometry work.
      Commits: 1b932ed9
- [x] LatteComponents.ComboBox popup renders its dropdown rows
      collapsed (~13px tall) with item text invisible - found while
      driving the Appearance page Palette / From Window dropdowns
      headlessly, then confirmed with a real mouse. ROOT CAUSE:
      the delegate's Array.isArray(control.model) branch is always
      false on Qt6 (the ComboBox hands the model back as a
      QVariantList even when a JS array was assigned), so all seven
      role lookups fell through to model[role], undefined for array
      models - textless rows that also collapse because the label
      drives the delegate height. Measured on the pin: array models
      carry roles on modelData only, ListModels on model[role] only
      (modelData undefined) - the delegate now resolves from whichever
      exists. tst_comboboxpopup.qml pins all three model kinds the
      config pages feed it (object arrays, ListModel, string arrays);
      the ListModel case caught the first fix attempt being wrong.
      Live-verified: Palette dropdown shows five readable rows.
      Commits: a302d742
- [ ] Startup latency (caught live: 'takes way longer than it
      should'). Measure BOTH dev and production-shaped starts before
      optimizing: dev runs pay for cmake --install restaging, the gdb
      wrapper and -d logging before the first frame; a clean timing
      needs a bare run-staged.sh without wrapper. Then profile the real
      offenders (layout load, QML compilation, screen wait).
      MEASURED 2026-07-13, no wrapper, no -d, KWin window poller for
      ground truth (~150ms granularity); the -d run matched within
      50ms so -d numbers are trustworthy. Perceived restart is
      ~9.4s, split into two distinct problems:
      (1) LAUNCHER ~4.1s before the binary even execs: kill-wait 0.6s
      + nix develop 3.0s (measured alone) + restaging ~0.5s. Lever:
      cache the dev-shell env (nix print-dev-env snapshot, invalidate
      on flake.lock change) instead of re-evaluating per restart -
      would cut ~3s of dev-loop latency, zero product risk. Production
      installs never pay this.
      (2) BINARY 4.5s exec -> first dock window, 5.3s -> all three
      docks. Attributed via log density + a mid-stall gdb interrupt
      (child-run gdb; ptrace_scope=1 blocks attach): ~0.6s libs+init
      to first log, ~0.9s corona init (theme mask parsing, screens,
      layout templates), then ~3s creating the three views
      SEQUENTIALLY in one synchronizer timer callback
      (synchronizer.cpp:694 -> initContainments -> addView each).
      The captured stack shows the main thread parked in
      QWaitCondition::wait inside QQmlTypeLoader::doLoad<CachedLoader>
      under PlasmaQuick AppletQuickItem::itemForApplet /
      ContainmentItem::init - upstream PlasmaQuick loads every
      applet's QML synchronously on the main thread, so the 'stall'
      windows in the log are the loader thread churning imports.
      Levers, in value order: (a) create views one per event-loop
      cycle instead of all in one callback - first dock maps ~2s
      earlier (perceived win), total unchanged, small and safe;
      (b) the synchronous per-applet load is upstream PlasmaQuick
      design - treat as the floor, do not fight it locally.
      Also explained: latte-dock windows 4 and 5 appearing ~18.5s
      after startup in BOTH runs are lazily created auxiliary popups
      (ToolTipDialog, KlipperPopup timing matches), benign.
      LEVER (a) LANDED 2026-07-13 (70fe5390): initContainments hands
      pending Latte containments to a queued one-per-turn chain.
      Measured honestly: first dock exec+3.7s (was 4.5s), all three
      6.3s (was 5.3s) - the estimate of '~2s earlier' was wrong
      because ~2.4s of the first-dock time is corona init BEFORE any
      view can start, so the stagger only removes the wait behind the
      other two views' construction. The total grows because
      interleaved turns frontload mapping/painting of earlier views.
      OWNER CALL if the trade feels wrong live: revert is one commit.
      Remaining startup levers: corona init ~2.4s (theme mask parsing,
      screens, layout templates - unprofiled), the launcher cache
      (landed, 37acf9ca) and the upstream synchronous-load floor.
      CORONA INIT PROFILED 2026-07-13: the ~2.4s estimate was wrong -
      corona init is only ~1.0s (one 0.35s gap at layout init). The
      real pre-first-paint chunk is the FIRST VIEW's create-to-map
      pipeline, ~2.6s, with three stalls (0.57/0.62/0.39s) that
      gdb-sample to (a) the QQmlTypeLoader floor and (b) one-time
      process inits buried in applet instantiation: ICU
      calendar/locale data (icu::Calendar::makeInstance under
      LikelySubtags::load, entered from deep applet code) and the
      notification/JobViewServer DBus registrations from the systray
      load. An off-thread ICU pre-warm via QLocale::toString was
      implemented, measured to change NOTHING (identical gap pattern
      - Qt formats dates from its own CLDR tables; the applet's ICU
      entry point is elsewhere), and REMOVED rather than shipped as
      cargo cult. Startup now stands at: launcher 0.6s + libs 0.4s +
      corona 1.0s + view1 2.6s = 3.7-4.5s to first painted dock
      (run-to-run noise +-0.4s), with the remainder owned by upstream
      PlasmaQuick's synchronous applet loading. Further local work
      here has poor cost/benefit; revisit only if upstream grows
      async applet loading.
      Commits: 37acf9ca (launcher cache), 70fe5390 (staggered views)
- [x] Edit-mode chrome lifecycle trilogy (caught live 2026-07-12
      evening: blueprint flashes at open then vanishes, rearrange mode
      dies moments after enabling with no blue frames and no dragging,
      closing the chrome leaves the dock stuck in edit visuals). Three
      stacked root causes, instrumented and convicted separately:
      (1) inConfigureAppletsMode was persisted, so any session killed
      mid-configure seeded a stale true that flashed on every next
      open; (2) the layer-shell chrome windows grab keyboard focus on
      map before isActive() lands, so the primary's focusOut hasFocus()
      check raced its own canvas mapping and the ensemble closed itself
      (1ms Show-to-hideEvent in the log); (3) hideEvent ran session-end
      work on transient hides (deferred show on view retargeting), so
      cycling docks killed the fresh session ~300ms in - chrome open,
      editMode false, blueprint and configure frames dead. NOT a
      regression from any single commit: the traps were latent and
      today's heavy crash-repro kill cycles armed (1) repeatedly.
      Verified live end to end including a headless drag reorder
      (appletOrder 3;4;6;11;29;122 -> 29;3;4;6;11;122, persisted
      across restart; fakepointer learned drag for this, c1ee9e2b).
      Commits: fb621102 (transient sub-mode), 4a8ac480 (family focus
      + session end on deliberate close)
- [x] Rearrange input mask carved from stale/wrong geometry
      (caught live 2026-07-12 late evening: wheel-opacity tooltip on
      every applet's middle band, only applet edges draggable, per-dock
      variance). The canvas carve sampled rearrangeToggleRect exactly
      once and the published rect mapped the outer Button item, which
      stretches to near-full row width after chrome retargeting
      (observed 2544 vs the 273 chip; origin unidentified, see next
      item). Mask now re-carves on the rect's notify signal and maps
      the interactive chip. Verified: carve reads QRegion(8,99 273x26)
      on the top dock, toggle cycles cleanly with the settings pinned.
      Commits: 8be2b388
- [x] Outer SettingsControls.Button item width stretches after chrome
      retargeting (2544 observed while the chip stayed 273). RESOLVED
      as part of the cross-view binding-stranding family: 9aeda562
      reloads the canvas content on every view retarget, so no canvas
      binding can carry stranded cross-view state (the Button's exact
      strand path was not individually traced; the header's was, see
      the vertical-dock item). If stranding ever reappears in
      NON-reloaded chrome, this is the family fingerprint.
      Commits: 8be2b388 (made it inert for the mask), 9aeda562
      (removed the class)
- [x] Plain edit mode blocked all widget interaction (caught live
      2026-07-12 night: wheel-opacity tooltip over every widget, no
      right-click, no hover config without entering rearrange). Design
      mismatch, not a point bug: Qt5/X11 stacked the edited dock ABOVE
      the canvas so the dock strip stayed interactive; wayland
      same-layer surfaces cannot restack, so the later-mapping canvas
      swallowed the strip. The canvas input region now always excludes
      the dock's absoluteGeometry (re-carved on absoluteGeometryChanged).
      Verified: full Latte context menu opens on a task in plain edit
      mode, wheel tooltip only on the blueprint margin.
      Commits: 3d714d63
- [x] Rearrange drag: cursor drifts away from the dragged widget over
      long back-and-forth drags (caught live 2026-07-12 night). The
      delta-tracked drag stored lastX/lastY as int while wayland
      delivers fractional pointer coordinates: every event truncated
      the stored position and injected the lost fraction into the
      applet position; reversals compound it. Now real. Verified with
      fakepointer's new multi-waypoint drag: a four-reversal net-zero
      zigzag (1600px travel, ~384 events, fractional steps) returned
      the applet to its exact slot. Same int-vs-fractional-wayland
      class as the WindowId int trap (8e8cdf31): audit other `property
      int` pointer/geometry caches when touching input code.
      Commits: 36160c46
- [x] Applet edit-tooltip modal (rearrange mode) and task hover
      previews: misposition during fast pointer movement AND the
      rearrange-mode applet hover modal appears INCONSISTENTLY
      (caught live 2026-07-12 night). PARTIALLY FIXED 2026-07-13
      (e6c5ae76): the parked-preview half is gone - the preview dialog
      updated its content per hovered task but a mapped wayland popup
      silently ignores visualParent changes, so fast sweeps left the
      window at the sweep origin (screenshot: Firefox preview over
      the clock, 380px off). e6c5ae76 unmapped before adopting a new
      task but hid and re-showed synchronously, which COALESCES: no
      real unmap, re-reproduced within minutes with a real mouse
      (synthetic fakepointer moves left ~150ms gaps that masked it -
      LESSON: verify input fixes at realistic event rates). 15558f40
      defers the re-show one event loop pass so the unmap commits.
      Re-reproduced by hand AGAINST 15558f40 too (recipe: hover the
      system monitor TASK, wait for its preview, glide to firefox -
      firefox thumbnails ~370px left). THIRD ROOT CAUSE (d619ae08,
      instrumented anchors proved every prepare's anchor CORRECT): the
      deferred remap's 1ms timer re-shows the pending task directly,
      bypassing preparePreviewWindow, so interleaved shows (task B's
      prepare between task A's hide and A's deferred re-show)
      resurrected one task's CONTENT at another task's ANCHOR, and a
      stale pending could re-show an old task after a newer one mapped
      (ping-pong). show() now cancels stale pendings when a newer task
      reaches the map point and assigns visualParent from the mapped
      taskItem itself (TaskItem exposes previewsVisualParent), making
      anchor and content atomic per task under any interleaving.
      Verified: my recipe at realistic event rates (fakepointer
      glide, 8ms steps) mapped the firefox preview centered on its
      logged anchor (2352 vs 2351.5), plus two adversarial overshoot
      zigzags and a clean-build pass, all correct. By hand it
      re-reproduced AGAINST d619ae08 as well though. FOURTH AND STRUCTURAL
      ROOT CAUSE (77aac4b4, instrumented both the QML show() path and
      every C++ position push): the dialog MAPS with the previous
      task's content size still in the mainItem (1112px, system
      monitor's strip), so the base places it at anchor-556; the
      content then shrinks to firefox's 278 and the base correctly
      recenters; 15ms later libplasma's stale-position re-send (frozen
      QWindow::position(), documented in dialog.cpp) reverts it for
      good. The old counter-measure STORED the last anchored position
      and was armed only by the mainItem-resize hook, which never
      fired for the final task of a sweep - intermediate stops in the
      SAME sweep won the race, which is why every slow retest passed
      while by-hand runs kept failing. Correctness depended on event
      ordering. 77aac4b4 stores nothing: after every Move/Expose/Show
      the target is recomputed from the CURRENT anchor and CURRENT
      pending content size and pushed to the plasmashell surface, so
      any ordering self-heals on the next event. Verified clean-build
      with the by-hand recipe and bidirectional multi-speed sweeps
      (window at 2213 where it parked at 1796), idle-silent.
      LESSON for the whole series: when a fix depends on which of two
      event handlers runs last, it is a patch, not a fix - remove the
      stored state instead of arming it in more places.
      USER-CONFIRMED with a real mouse 2026-07-13 morning.
      The rearrange-mode modal half was the SAME defect (8f821310):
      ConfigOverlay's tooltip was a stock PlasmaCore.Dialog whose
      visualParent hops between hovered applets while mapped -
      reproduced live: sweeping clock -> tasks -> comic strip left the
      window parked at the first applet (x=2122) with only its width
      tracking the hover, so far applets looked like the modal never
      appeared. Switched to Latte::Quick::Dialog (the 77aac4b4
      recompute-fresh machinery); verified live in both sweep
      directions (2114 over the clock, 3180 over the comic strip).
      The ~40px zoom-dwell offset closed too (c622da1b): the resting
      anchor was itself a port-era workaround for the
      cannot-reposition dialog; with 77aac4b4 the Qt5 anchor
      (tooltipVisualParent, live and zoom-tracking) is strictly
      correct, so it is restored verbatim and the whole
      restingCenter/settle-delayer machinery (d98bff98) is deleted.
      Verified: dwell preview center == zoomed icon center (2432 ==
      2432), sweeps and zigzags within a few px of the live icon.
      ITEM CLOSED - all three popup defects (parking, interleave,
      dwell offset) plus the rearrange modal trace to mapped-popup
      repositioning on wayland, all fixed by the recompute-fresh
      Dialog plus live anchors.
      Commits: e6c5ae76 (incomplete, coalescing), 15558f40 (deferred
      remap, still incomplete), d619ae08 (atomic anchor+content,
      still incomplete), 77aac4b4 (recompute-fresh positioning, the
      structural fix, confirmed by hand) + dbe5a03b (layershellmappingtest
      signature catch-up found by the same session's build) + 8f821310
      (rearrange modal, same class, confirmed by hand) + c622da1b (live
      anchor restored, resting machinery deleted)
- [x] Vertical (left/right) dock canvas header renders off-surface
      (rearrange chip at y=-552/-596, rearrange unusable on the left
      dock, caught live twice). MECHANISM DEMONSTRATED: the header's
      positional bindings branch on per-view context and each ternary
      branch captures different dependencies, so a binding evaluated
      mid-retarget through the old view's branch strands on a transient
      value with nothing left to wake it (header y frozen at -13 while
      sibling width re-evaluated to 1440). STRUCTURAL FIX: the canvas
      view reloads its QML content whenever it retargets to another
      view; fresh instantiation against settled context cannot strand.
      Verified live: after cycling across all three docks the left
      header matched its formulas exactly (y=707 on the 1352 canvas),
      the chip mapped on-surface at canvas-local (77,539), rearrange
      engaged with the blueprint down the strip, and a vertical drag
      reordered appletOrder 111..117 -> 112;113;114;111;115;116;117,
      persisted. NOTE the left canvas geometry flip-flop across runs
      (106x1440 at y425 vs 106x1352 at y513) is still unexplained;
      likely sibling docks' strut states at placement time - watch it
      during Phase 8 multi-screen work on Positioner::canvasGeometry.
      Commits: 9aeda562
- [x] iconSize=78 startup hang (bisected 2026-07-10, fixed 2026-07-12):
      the autosize shrink loop's termination was the equality
      nextIconSize !== 16 while stepping by 8, so any icon size not
      congruent to 16 mod 8 stepped past the floor forever; on
      Qt6/wayland the first updateIconSize() arrives before the window
      has geometry (maxLength 0, negative shrink limit) so the length
      condition could not exit either, starving the event loop at 100%
      CPU. Inherited from upstream 747d4870, latent on X11. Both loops
      now clamp and exit on inequality, and recalculation skips the
      unsized-window degenerate input (re-run by onMaxLengthChanged).
      Verified with the throwaway layout at 78 and my real
      layout, which starts again
      Commits: ad9b823f
- [x] Expose plasma applets' PRIVATE QML modules to the dock (hit live
      it live: 'module org.kde.private.desktopcontainment.folder is not
      installed'; ALL third-party applets error in the staged run while
      working under plasmashell). Likely the reason broken applets like
      Comic Strip churn representations at all. RESOLVED 2026-07-12 the
      NixOS-native way: instead of allow-listing leaves out of the
      foreign system tree, the owning packages joined the flake's own
      pinned LATTE_QML_MODULE_PATH (plasma-desktop, bluedevil, bluez-qt,
      plasma-nm, kdeconnect-kde, kdeplasma-addons, qtwebengine,
      powerdevil, print-manager) - same-pin whole-package roots cannot
      shadow with a foreign build, and staged Latte modules still win
      last. Verified on my full layout: nine distinct failing
      modules -> zero errors, and the right-click menu is still Latte's
      (the shadowing regression check)
      Commits: 4c9f3bc7
- [x] Duplicated docks: applets come out in a DIFFERENT ORDER than the
      original (observed live) - matches the ng-documented
      applet-order sync defect class. Note: live config mirroring
      (e.g. pinned tasks appearing on the copy) is NOT expected for
      duplicates, only for screens-group clones; document that
      distinction somewhere user-visible (moved to the replication
      design doc). ROOT-CAUSED AND FIXED 2026-07-12 with a synthetic
      reproduction (non-default appletOrder written into the throwaway
      layout, duplicateView driven over D-Bus): the defect was far
      bigger than duplication. save()'s collector read the applet id
      off the GRAPHIC item, which lost its 'id' property on Plasma 6,
      so every dock's stored order was wiped to default on EVERY
      startup - rearrangements never survived a restart, and
      duplicates inherited nothing for the (working) id remap to
      remap. After the fix all three docks persist real orders and a
      duplicate's appletOrder maps to exactly the original's plugin
      sequence
      Commits: 9a6f8fb8 (id chain), c3d15966 (config-sync
      prerequisite)
- [x] Verify duplicated/cloned docks actually establish applet config
      sync after the initial 'org.kde.sync ... was not established'
      storm (the 1s retry in ContainmentInterface should log 'delayed
      applet configuration was successful' per applet id; confirm none
      stay unestablished, since clone mirroring silently degrades).
      VERIFIED 2026-07-12 - and the finding was much bigger: sync
      NEVER established for ANY applet since the port.
      appletConfiguration() probed the AppletQuickItem for the static
      'configuration' property Plasma 6 removed, so it always returned
      null and the 1s retry could never succeed either (same dead
      probe). Fixed by reading the CONSTANT configuration Q_PROPERTY
      off the Applet itself (single-loader chain, like 32df5b47).
      Startup storms: six per dock -> zero. Duplicate Dock: zero
      failures. The duplicate-then-add-widget crash from the handoff
      (KCrash double-crash, no core) no longer reproduces - driven
      end to end under the gdb wrapper, Activity Pager added twice to
      a fresh clone, dock alive
      Commits: c3d15966
- [ ] Dock visibility across screen lock/unlock (observed live
      2026-07-10, not yet root-caused): a dock that lives through a
      kscreenlocker cycle can stay invisible after unlock even though
      the process is healthy and reacting to pointer events (an
      edit-mode mustBeShown revived it); a dock STARTED under a locked
      screen stalls for minutes before "Adding View" runs. Reproduce
      with loginctl lock-session/unlock-session and the screenshot
      loop, then trace the visibility/startup state machine
      Commits:
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
- [x] Edit-mode windows must re-derive geometry from the edited view's
      CURRENT screen after output hotplug (found live 2026-07-11 when a
      second monitor was added: the settings window kept the exact
      position/size computed for the old single-screen layout, landing
      in dead space between outputs). Also pair layer-surface output
      assignment with the screen the margins are computed against
      (LayerShell::applyFixedGeometry offsets are relative to the
      passed screenGeometry and only land right when the surface is on
      that output). See "Multi-monitor" section in
      docs/session-handoff.md for the measured evidence
      Commits: d670c97a (canvas, settings window and widget explorer are
      pinned to the edited view's output; hand-verified on the
      portrait+landscape setup), 7ac419d1 (secondary advanced-mode
      window, the last compositor-placed surface: applyFixedGeometry on
      the edited view's screen, verified sitting above the canvas band
      at the dock's start)
- [x] Settings window screen selector is broken on multi-monitor: the
      'On Primary Screen' combobox neither opens other screens nor
      applies a change (caught live while trying to move a dock to
      the other monitor's bottom edge). RESOLVED 2026-07-11: of the
      three suspected layers, none was the cause. Qt6 made
      QQC2 ComboBox.pressed read-only; the write in onGenericPressed
      threw a TypeError that aborted the handler BEFORE the
      popup.visible toggle, so no LatteComponents ComboBox popup could
      open anywhere in the settings (the screens model, the old-style
      Connections handlers and the layer-shell popup all work). Apply
      then exposed a second, independent defect: a mapped wlr-layer
      surface cannot change outputs (no protocol request exists), so
      QWindow::setScreen in setScreenToFollow left the relocated dock
      on the old output with the new screen's size. Both fixed and
      verified end to end with pointer injection: dock moved
      DP-2 -> DP-3 -> bottom edge from the settings UI alone
      Commits: 0474e20c (ComboBox pressed writes -> down),
      793faad2 (View::moveToScreen hide/retarget/show remap).
      USER-VERIFIED 2026-07-12: dock moved to the portrait monitor and
      back to the main one on the real desk setup
- [ ] Full settings-window control audit against Qt5 semantics (split
      from the screen-selector item; more controls look broken,
      driving both tabs by hand at the desk). PROGRESS
      2026-07-11 late session: the two biggest breakage classes fixed.
      (a) Every LatteComponents.ComboBox popup was unopenable (Qt6
      read-only pressed, commit 0474e20c under the selector item).
      (b) The whole Tasks page read tasks.configuration which Plasma 6
      removed - all 75 reads undefined, controls showed defaults and
      applied nothing; rewired through tasks.plasmoid.configuration
      and verified round-trip into the layout file (32df5b47).
      The indicator config TypeErrors are RESOLVED (33fa17d7): not a
      first-eval transient at all - QQC2 controls carry their own
      'indicator' property (the check glyph), which shadows the config
      API's context property inside control-scoped bindings AND
      onClicked handlers, so those checkboxes showed defaults and
      their toggles silently wrote to the wrong object. Qt5's QQC1
      controls had no such property, which is why upstream's bare name
      worked. Diagnosed by logging the failing evaluation (the scope
      object printed as CheckIndicator_QMLTYPE). WATCH FOR the same
      shadowing class anywhere a context property or root id collides
      with a QQC2 control property name.
      NEW FINDINGS filed from the module-fix fallout (applets now
      load far enough to hit real API gaps): (a) folder-view/desktop
      containment calls corona.isScreenUiReady which Latte::Corona
      does not implement (plasmashell ShellCorona API) - decide shim
      vs acceptable noise; (b) our own BindingsExternal.qml:281 reads
      'localGeometry' of null twice during startup; (c) digitalclock
      Tooltip.qml 'text' of null, third-party internal noise.
      Also still owed: the at-the-desk semantic walk of
      every control on both tabs against Qt5 (check especially
      TaskMouseArea handling all 9 TaskAction enum values - ng
      eabf7c89a found 3/9, 5/9, 5/9 handled for left/middle/modifier
      clicks - and ConfigInteraction.qml cfg_hoverAction hardcoded to
      NoneAction in ng before their fix)
      Commits: 32df5b47 (Tasks page config access)
- [x] Edit-mode canvas can stay on the previous output after a
      screen-only relocation with edit mode open (observed ONCE live:
      top dock DP-2 -> DP-3 left the canvas band at DP-2's top with
      the old 2560x146 size; the settings window followed correctly).
      RESOLVED 2026-07-12, two stacked defects found by making the
      reproduction deterministic (first move after a fresh start with
      chrome open): (a) a mapped layer surface cannot change outputs,
      so applyCanvasPlacement's setScreen was a no-op for the visible
      canvas while new margins/size still applied - fixed with a
      shared retargetScreen hide/retarget/show remap in the LS
      helpers, covering canvas, secondary chooser and widget explorer
      alike; (b) the positioner keys available-area on
      containment()->screen(), which lands asynchronously after the
      move - when all syncs ran before it landed, the canvas kept the
      previous screen's LENGTH (1264 on the 2560 portrait). Fixed by
      connecting Containment::screenChanged -> Positioner::syncGeometry.
      Verified: two consecutive fresh-start first-move relocations
      carry the full chrome ensemble to the target output
      Commits: 1607d022 (surface remap), c5bdc239 (screen-id resync)
- [x] Fix multi-screen palette divergence: pin
      `Kirigami.Theme.inherit: false` with an explicit `colorSet`, and
      set `KDE_COLOR_SCHEME_PATH` explicitly and early (in the `View`
      constructor) so every view starts from the same palette - each
      `QQuickWindow` can otherwise resolve its `KDEPlatformTheme`
      palette independently on Plasma 6/Wayland. DONE 2026-07-12: the
      KDE_COLOR_SCHEME_PATH window-property pin landed in View::init
      (the mechanism ng verified live as white audio badges, their
      9fe135422; plasmashell pins its panels the same way). The
      Kirigami colorSet half stays unapplied deliberately: no live
      divergence reproduction exists in this port yet, and pinning
      colorSets blind risks Qt5-unfaithful colors - if mixed-scheme
      divergence ever shows on the desk setup, that is the follow-up
      Commits: a774ee55
- [ ] Duplicate-dock + add-widget crash from the handoff: RESOLVED as
      a downstream of the config-sync fix (c3d15966) as far as the
      recipe drives - kept as a watch item until my real
      rearranged layout also survives duplication in the interactive
      session
      Commits: c3d15966
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
      still honest against the Phase 0 standard. A microvm is planned to
      provide a microvm for real-GUI CI; tests written meanwhile
      should assume that runtime (fakepointer already covers
      move/click/rightclick/drag/glide/scroll - drag doubles as
      drag-and-drop since it is press/waypoints/release).
      GUI-CI-candidate tests banked so far: widget-explorer->dock
      drag-add, AlternativesHelper applet swap (needs a live applet),
      the settings-window cold-open geometry loop.
      Commits:
- [x] SYNC PASS 2026-07-14 (record; hashes updated in CLAUDE.md):
      latte-dock-ng through 456154efb (10 commits): FOLDED
      alternativeshelper _plasma_graphicObject -> AppletQuickItem::
      itemForApplet (their 613ddcc3b spotted it; ours silently no-op'd
      "Show Alternatives"). SKIPPED: middleClickAction default flip
      NewInstance->Close (behavior deviation; Qt5 is the spec),
      audio-badge double-scaling fix (applies to their redesigned
      badge, ours keeps Qt5 relative sizing), Qt.MidButton and
      tasks-config migrations (already carried), startup warning
      gating (their code path). Their new unit-test suites (positioner
      math, effects borders, importer parsing) are candidate surfaces
      for our own tests, not foldable files.
      latte-dock-qt6 through 81384003 (54 commits): NOTHING foldable
      verbatim - it is a testability-refactor campaign (pure
      extractions, ICoronaHost/IViewFactory/IScreenInfo seams,
      coverage gates) on an architecture diverging from upstream
      shape. Their one behavior fix (81384003 top-layer) repairs
      their own layerFor(WindowsGoBelow)=Bottom mapping mistake; our
      mapping was already corrected and test-pinned. CANDIDATES noted
      for later: their StorageValidator (orphaned subcontainments,
      id-collision detection - we have real layout-corruption history,
      see the .polluted/.pre-repair backups) for Phase 8/11, and the
      Wayland windowFor id-index (perf) if window lookup ever profiles
      hot. plasma-desktop backend: current except yesterday's new
      task-cycling-shortcuts feature (overlaps Latte's own shortcut
      system - decide at Phase 12, not auto-port).
      Commits: (alternativeshelper fix in its own commit)
- [x] Applet popups are mis-sized (caught live 2026-07-14: the volume
      applet popup renders ~260px wide with wrapping tabs and clipped
      content; plasmashell sizes the same applet correctly).
      ROOT-CAUSE DIRECTION, from reading libplasma v6.6.5
      appletpopup.cpp: Plasma 6 inverted the popup sizing contract -
      LayoutChangedProxy takes the full representation's IMPLICIT size
      as the base, Layout.preferred* only as an override, and enforces
      Layout.minimum*. Our shell/package/contents/applet/
      CompactApplet.qml still ran the Qt5-era chain (preferred ->
      implicit -> live width -> font-metric fallback), so applets
      written to the new contract (plasma-pa) landed in the wrong
      branch. FIXED on the upstream proxy semantics: implicit size as
      the live base, Layout.preferred* as override, Layout.minimum*
      enforced. Verified live on the volume applet (260x152 clipped ->
      260x491 correct; the 260 width is the applet's own 252px minimum
      plus margins, and plasmashell's wider rendering of the same
      applet is a persisted custom size there - popupWidth=457 in its
      appletsrc - not a different default).
      Commits: 437d9a0c
- [ ] Applet context menu is MISSING the "Show Alternatives" entry
      (found while live-verifying the AlternativesHelper fix: the
      clock's menu shows Configure/Copy but no Alternatives, so the
      restored helper is unreachable from the UI). Sibling of the
      fixed "Applet Settings" missing-entry saga - check the
      contextmenu containmentactions plugin and the canvas
      ContextMenuLayer for where Qt5 injected it
      (appletAlternativesRequested wiring exists and is connected).
      Commits:
- [x] Widget removal leaves a ghost slot in rearrange mode (caught
      live 2026-07-14 with screenshots: deleting the System Tray in
      rearrange mode left its layout slot visible for multiple
      seconds). ROOT CAUSE, measured on the throwaway layout with
      probes and timestamped screenshots: libplasma keeps the deleted
      applet alive for undo, and askDestroy()'s immediate
      appletRemoved emit is guarded with !isContainment() - the
      systemtray applet IS a Plasma::Containment (CustomEmbedded), so
      its only appletRemoved arrives from inside ~Applet when the
      undo window ends (measured exactly 59.8s, the libplasma 60s
      deleteNotificationTimer; feels like seconds when the Widget
      Removed notification closes earlier). The one-phase
      removeAppletItem finalization therefore did nothing until then,
      while destroyedChanged(true) hid the content instantly, leaving
      the ghost slot. FIXED by restoring Qt5's two-phase parking
      keyed to the Plasma 6 signal timeline: AppletItem watches
      destroyedChanged (park on true = slot hides instantly via the
      existing isScheduledForDestruction binding, unpark on false =
      undo restores in place), removeAppletItem parks while
      destroyed() is set, physical cleanup rides the
      AppletItem.onAppletChanged self-destroy with the parking entry
      pruning itself by identity, and addAppletItem guards against
      duplicate containers after undo. Verified: post-fix slot gone
      by the first frame 1.4s after the click (was 59.8s),
      finalization at the 60s mark silent and persisted,
      hand-confirmed at the desk. WATCH ITEM: the undo arm
      (notification Undo click restores the slot in place) is
      implemented per the libplasma contract but still wants one live
      confirmation - the popup auto-hid before a headless click could
      land.
      Commits: 71b0d75a
- [x] Edit-mode background opacity is not WYSIWYG (caught live
      2026-07-14 with screenshots: Background -> Opacity at 100% in
      the settings chrome, but the dock in PLAIN edit mode renders
      its background at roughly HALF opacity). ROOT CAUSE: the
      candidate mechanism "the port draws the background and
      blueprint composed differently than Qt5" was right, inverted:
      the port drew the BLUEPRINT OVER the background. Qt5 composited
      wallpaper > grid > dock background > applets (the grid window
      stacked behind the whole dock window on X11); the port had the
      grid between background and applets, so the whitish grid tile
      washed the background at editBackgroundOpacity - the persisted
      0.5 in my config is exactly the observed "roughly half", and
      the suspected editBackgroundOpacity interaction was literally
      it multiplying in on top. The background opacity chain itself
      (solidBackground <- midOpacity <- backgroundStoredOpacity) is
      byte-identical to Qt5, no edit-mode arm anywhere - the child
      order inside DragDropArea was the whole defect. FIXED by
      moving editBlueprint below the background Item; comment at the
      site records the Qt5 composite. Verified in the throwaway:
      rearrange mode draws the solid grid behind the background
      band, plain edit leaves the background undimmed.
      Commits: 38e60eb9

- [ ] Dock background renders LIGHT/white while the palette is set to
      Dark Colors (reported 2026-07-15 with a screenshot, throwaway
      bottom dock in plain edit mode, Background Opacity 100%).
      NOT introduced tonight: the same light pill shows in captures
      from before the 2026-07-15 fixes (21:55 real dock, 22:18
      throwaway outside edit mode) - the blueprint stacking fix
      merely made the background fully visible in edit mode, which
      makes the color obvious now. TWO SUSPECTS, both pre-existing:
      (a) the staged dock resolves a LIGHT plasma theme while
      plasmashell on the same session renders its panel dark - an
      env/theming resolution gap (Phase 9 KDE_COLOR_SCHEME_PATH /
      Kirigami colorSet territory); (b) the Dark Colors BACKGROUND
      arm (Colorizer.CustomBackground / overlayedBackground
      midOpacity path) may be silently dead - same defect class as
      the applet colorizer no-op fixed in 1f835402, which only
      covered the applet foreground arm. MEASURED 2026-07-15:
      build/_runconfig (the throwaway XDG_CONFIG_HOME) contains NO
      kdeglobals and NO plasmarc, so every throwaway run resolves
      the default LIGHT color scheme - the white bar is structural
      to the throwaway environment, independent of code. The
      recollection of a dark bottom bar most plausibly comes from
      --user-config runs, which read the real dark kdeglobals.
      DECISIVE TEST: restart --user-config and look at the bar; if
      the real dock's bar is also white while plasmashell renders
      dark, THAT is the port defect to chase (a774ee55
      KDE_COLOR_SCHEME_PATH pinning territory). Regardless: seed
      build/_runconfig with a kdeglobals copy so throwaway runs
      match the real session visually.
      Commits:
- [ ] Applet colorizer applies too broadly: under a non-default
      palette it flattens FULL-COLOR applet icons to single-color
      silhouettes (confirmed live 2026-07-15: Dark Colors turned the
      comic's multicolor icon into a black disc; flipping the
      palette back restored it instantly). The 1f835402 restoration
      brought back Qt5's ColorOverlay mechanism but apparently not
      Qt5's applicability rules. READ THE QT5 SOURCE for which
      applets got colorized vs left full-color (per-applet blocking
      exists - userBlocksColorizingApplets - and Qt5 likely also had
      an automatic monochrome/low-color heuristic); restore the
      scope, not just the mechanism. CLAUDE.md fork-trap rule
      applies: Qt5 is the spec.
      Commits:
- [x] Comic Strip applet "not rendering" (reported 2026-07-15; two
      symptoms, RESOLVED as three separate causes, none the suspected
      commit regression). (1) The black-disc icon: the applet
      colorizer restored in 1f835402 flattens icon content to a
      single scheme color under a non-default palette - CONFIRMED by
      my flip test (Palette back to Plasma Theme Colors: the smiley
      returned instantly). The colorizer-scope defect stays open as
      its own item below. (2) The hover-to-comic display dead in and
      out of edit mode: the comic applet's own provider list had
      xkcd UNCHECKED - no comic selected, nothing to render. Found
      by me in the comic settings; hover works again the moment it
      was checked (xkcd renders). Near-certain cause of the
      "worked yesterday" confusion: the active throwaway layout was
      twice restored from with-comic.bak during the removal-ghost
      repro, and the backup predates the provider selection - the
      "regression" was a config file reverted by the repro
      infrastructure, a trap worth remembering whenever a layout
      backup gets re-activated. (3) A REAL latent defect found by
      the probes along the way and fixed: the 9ea29eaa release path
      hid unwired full representations, and libplasma's inline
      representation switch re-uses the cached item without ever
      re-showing it - any applet whose representations churn could
      end up with a permanently invisible full rep (the comic's sat
      at parent=null visible=false after startup churn, measured).
      Release now detaches to a null parent without touching
      visibility, upstream-symmetric.
      Commits: 1aa5238c
- [x] Duplicated dock renders its applets ON TOP OF EACH OTHER
      (caught live 2026-07-15 with screenshots, reproduced by hand
      twice and headlessly twice). NOT the duplicate machinery and
      NOT config corruption: the file stays consistent, restore()
      produces correct order, plain duplicateView assembles a clean
      dock, a screen-only move stays clean, and every restart heals.
      The trigger is the LIVE FORM-FACTOR FLIP (the clone is born on
      a free vertical edge, the user then moves it to a horizontal
      edge of the other screen). ROOT CAUSE, probe-measured through
      the whole chain: the tasks plasmoid's mouseHandler width/height
      bindings are DEAD after the flip - root.vertical updates
      (formFactorChanged fires, verified with a listener), icList
      re-lays (592x88), metrics stay correct (mask thickness 88), yet
      mouseHandler stays frozen at the old orientation's 88x592. The
      frozen size cascades: tasksWidth/Height -> Layout.preferred* ->
      ItemWrapper appletPreferredThickness -> a vertical-shaped tasks
      AppletItem (88px of reserved length) inside a horizontal row ->
      neighbors pack after the phantom slot while ~592px of task
      icons render over them. FIXED by reasserting the bindings on
      every orientation change (the afefa442 stranded-binding
      remedy); verified live: post-fix flip tracks (592x88), row
      renders correctly spaced, healthy docks unaffected. WATCH
      ITEM: the binding destroyer is unidentified (suspected
      anchors-system size takeover while old and new conditional
      anchors overlap during the transition) - if another dead-
      binding-after-flip surfaces elsewhere, hunt the mechanism
      itself, greppable via 'stranded-binding'.
      Commits: e412889d
- [ ] Applet-created dialogs open on the wrong screen (caught live
      2026-07-15 with a screenshot: the comic's full-size viewer -
      its "bigger" zoom button - opened on the portrait DP-3 while
      the dock lives on DP-2; the xkcd image rendered correctly, so
      this is placement only). The dialog is created by the applet's
      own QML, not by our code, so the fix surface is wherever our
      Dialog/window plumbing resolves screens for applet transients
      (visualParent/transientParent forwarding). Compare with
      plasmashell: where does the same button place its window
      there. Phase 8 multi-screen family.
      Commits:
- [x] HEADLESS SWEEP 2026-07-15: loops, degenerate indexes, iteration
      lifetime, delayed-lambda captures (the ad9b823f/46dc83c5/
      fa02b887/52c2987b/d6d57e61 families, swept systematically
      instead of waiting for the next coredump). Nine defects found
      by reading + proven mechanisms, all fixed at their origin:
      GenericTable operator[](QString) indexed m_list[-1] for missing
      ids with three reachable feeder sites in the activities
      delegate (c117e598); layouts model data() read the row before
      its rowExists guard - the sibling views model carries the
      corrected order with the old line still commented out
      (cbb37c95); layouts model currentData() returned a reference to
      a temporary (41a6918e); fifteen contextless
      QTimer::singleShot lambdas capturing raw pointers, incl. the
      config-view slide-out also needing a QPointer on the target
      view (d1773423); recreateView's delayed steps fetched via
      null-inserting QHash::operator[], could addView a destroyed
      containment, and wedged m_viewsToRecreate on abort (9abb3d25);
      the layouts sort persistence read a half-destructed QHeaderView
      inside its destroyed() handler (dedfc441); client UserRequests
      never disconnected its bridge connect - it re-CONNECTED from
      Component.onDestruction (708c6fe4); cleanupRecords spliced with
      a proven-negative index, deleting the last launcher record
      instead of the orphan - dead code today, caller commented out
      (8468f765); Factory's parallel id/name lists desynced for
      duplicate display names, making removal index past the names
      list, plus unbounded local-id re-appends per dirty event
      (5406a27b). Tests: indicatorfactoryremovaltest drives Factory
      removals through real KDirWatch deletions - built-in arm
      SIGSEGVs with the 46dc83c5 guard reverted, passes with it
      (0454ed36); tst_autosize.qml pins shrink/grow termination for
      every icon size 16-128 against the REAL stepping extracted to
      containment code/autosize.js, hang-verified against the
      747d4870 loop form (1fdcb2e0). Clean negatives and the guard
      inventory live in docs/session-handoff.md (2026-07-15 sweep
      section). fa02b887's import path was assessed and is NOT
      headless-drivable (Storage::importLayoutFile requires a live
      corona); its guard stays live-verified only.
      Commits: c117e598 cbb37c95 41a6918e d1773423 9abb3d25 dedfc441
      708c6fe4 8468f765 5406a27b 0454ed36 1fdcb2e0

### Phase 11: Nix packaging + Docker build verification

Directly reusable knowledge from this session, not new research -
confirmed transferable today when the same include-path fix was
applied to get latte-dock-qt6 building on Nix during live debugging.
The bare build toolchain moved to Phase 0; this phase is the
polished, distributable form of it.

- [ ] Decide the appstream component-id question: appstreamcli 1.x
      fails validation on org.kde.latte-dock's hyphenated rDNS id
      (cid-rdns-contains-hyphen, found when the Phase 5 QML staging
      briefly un-vacuumed ECM's appstreamtest). Either rename the
      component (touches desktop file, appdata, notifyrc) or record a
      validation override; the test is vacuous again until a real
      install writes build/install_manifest.txt
      Commits:

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

### Continuation features (beyond upstream parity)

Features beyond even Latte git master, appropriate under the
maintained-continuation framing. None of these start before their
prerequisites in the phases above are done.

- [ ] Resizable applet popups with per-applet persistence and reset
      (requested 2026-07-14; PREREQUISITE: the popup mis-sizing fix in
      the stabilization list - the feature sits on the corrected
      sizing contract). Design decided, from libplasma v6.6.5
      AppletPopup which implements exactly this for plasmashell:
      (a) interactive edge-drag resize on the popup dialog via a
      WindowResizeHandler equivalent (startSystemResize; KWin already
      cooperates for appletpopup-role windows, which ours are since
      f630d2ad) - scoped STRICTLY to type === AppletPopup so hover
      previews never become resizable;
      (b) persist popupWidth/popupHeight in the applet's own config
      group, the SAME keys plasmashell uses - lands in the layout
      file so it travels with profile export, and layouts moved
      between plasmashell and latte keep their sizes;
      (c) DELIBERATE deviation from upstream: save only after an
      actual user resize (upstream saves on every hide), so
      "has a custom size" stays meaningful and the reset entry can
      hide itself when there is nothing to reset - comment at the
      site;
      (d) "Reset Popup Size" entry in the applet context menu
      (contextmenu containmentactions plugin, next to Configure),
      visible only when the keys exist, deletes them and re-sizes
      live back to hint size;
      (e) suppress our recompute-fresh re-anchoring while a system
      resize is in flight, re-anchor once on release (the dialog
      repositions on size change and would fight the drag);
      (f) tests: qmltest pinning the sizing resolution
      (implicit/preferred/minimum permutations), GUI-CI candidates
      for the microvm: drag-resize round trip, persistence across
      restart, reset entry.
      Commits:
- [ ] Background color picker: let a dock's background color be set
      directly from Appearance (requested 2026-07-15 while chasing
      the light-background-under-dark-scheme item: "how else can I
      make the bottom bar dark?"). Qt5 Latte has NO such control -
      background color comes from the plasma theme / color scheme /
      colorizer palettes - so per the Qt5-faithful rule this is a
      continuation feature, not a bug. Design sketch: a color
      button next to the palette dropdown feeding the existing
      Colorizer.CustomBackground layer (the plumbing that already
      draws custom-colored backgrounds), persisted per-containment;
      prerequisite is the Dark Colors background arm actually
      working (the item in Phase 10).
      Commits:
- [ ] Replicate Dock: first-class live-mirrored views with user-chosen
      placement, riding the existing ClonedView sync machinery. Full
      design, settled decisions (keying, sync scope, editability,
      lifecycle/detach) and prerequisite tracking:
      docs/dock-replication-design.md. Blocked on the Phase 8
      cloned-view order-sync item and a live screens-group clone
      verification
      Commits:

### Phase 12: Upstream contribution prep

- [ ] Reality-check the upstream target **early, not here** (this box
      exists so the check isn't forgotten, but do it around Phase
      2-3): upstream latte-dock has had no real development since
      2022 (only po/docbook syncs since), so find out whether the
      invent.kde.org repo still accepts MRs and whether anyone
      reviews there. REFRAMED 2026-07-12 (my call: latte-dock
      upstream is dead for practical purposes, and ng's
      scope-narrowing decisions will NOT be repeated here): plan for
      the maintained-continuation outcome, with a latte-dock MR as
      the long-shot upside rather than the goal. The outreach that
      matters goes to the LIVING KDE repos and survives either way:
      pitch plasma-workspace on a public libtaskmanager API for the
      task-manager backend helpers (jump-list/places/recent-doc
      actions, smart-launcher badge tracking) - see
      docs/taskmanager-integration-research.md; the evidence is that
      three independent ports all vendor the same plasma-desktop
      file, and ng's attempt to shrink that vendor cost four end-user
      features plus silent breakage (itemized in the research doc).
      X11 support-level remains the other direction question if a
      latte-dock submission ever becomes real. Keep vendored-file
      provenance/SPDX clean so the code survives KDE review in any
      venue
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

Phases 0-2 done, Phase 3 nearly done, Phases 4, 5 and 6 done (88 of
128 items ticked). The dock runs on the live
Plasma 6 Wayland session with the task manager alive: real task icons
from libtaskmanager, the vendored backend (jump lists, KWin D-Bus
activation with cycling fallback, smart-launcher badges), pipewire
thumbnails, and struts that resize maximized windows -
screenshot-verified. The two behavior-audit items closed: the
click-action dispatch is now data-driven through code/TaskActions.js
with a completeness test, and the wheel-routing audit found the
existing z-ordering already correct (cross-boundary wheel delivery
deferred to the Phase 8 bridge). Next: Phase 7 - widget management,
drag-and-drop, edit mode.

The first runnable milestone (end of Phase 5) is reached and exceeded:
the dock creates its wlr-layer-shell surface, gets configured by KWin
on the bottom edge, and visibly renders panel, clock and task icons.
`scripts/run-staged.sh` reproduces the run (throwaway config home,
staged install, environment pinned via the flake's
LATTE_QML_MODULE_PATH).

Phase 4 summary: every Latte window is a wlr-layer-shell surface now
and the plasma-shell surface path is deleted. The mapping between
Latte's edge/alignment/visibility model and layer-shell state lives in
`app/wm/waylandlayershell.{h,cpp}` as pure unit-tested functions.
Struts are the dock surface's own exclusive zone (the internal ghost
window is gone), config windows centre by dropping anchors, the
edit-mode canvas overlays the dock with a carved input region, and
primary-output detection binds `kde_output_order_v1`. One deliberate
deviation from latte-dock-qt6: `WindowsGoBelow` maps to `LayerTop`
(the fork's `LayerBottom` mapping also dragged its front-layer default
down; the X11 backend gives the mode keep-above). The KWin D-Bus
activation/peek item folded into Phase 6 where its only invocation
site (the tasks backend) will exist.

Phase 5 summary: the whole QML load path (shell package, containment
package, Latte QML modules, the three indicator packages) is on
Plasma 6 / Qt6 Quick; the tasks plasmoid deliberately stays Plasma 5
until Phase 6 and fails its applet load cleanly. Testing grew two
ctest entries: a compile gate that loads all 91 package QML files in a
real engine (it caught the indicator packages the main sweep missed)
and the qmltestrunner interaction harness whose first occupant pins
ShadowedItem's type resolution and math. Live startup found and fixed
a winIdFor() segfault (window management not yet announced) and three
latteView-null QML errors.

Still open from Phases 2-3, all deliberately deferred hygiene: the
~720-site QStringLiteral wrap (compiler flags stubbed off),
C-style-cast cleanup, QDBusInterface -> QDBusMessage conversion. Open
stubs: wayland skipTaskBar for dialogs, per-activity placement for the
info window, the canvas interactive-chrome rect (the QML side now
exists in the fork's shape but wiring waits for edit-mode live testing
in Phase 7), activity stopping (Phase 8 decision).

From here on the per-phase verification cadence applies: each phase's
subsystem gets driven in the live session before its last box is
ticked. Next: Phase 6, the task manager subsystem (vendor-vs-shim
decision first).