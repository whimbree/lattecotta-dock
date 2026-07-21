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

## Where we are (2026-07-17)

The port is a daily driver on my real config. Phases 0-7 are
substantially done, Phase 11 (Nix packaging) shipped early, and the
Phase 10 QML extraction initiative is COMPLETE (all 25 units, see the
ledger in docs/tracking/QML_EXTRACTION_PLAN.md). Phase 4 is now WAYLAND-ONLY
(X11 backend removed 2026-07-17). Session three (2026-07-17) also
landed, via the feature-branch PR flow: the accessibility quartet's
code half (dock-window keyboard focus mode, Accessible.* rollout, the
maintained e2e nested-vehicle suite with a state-vs-render geometry
guard), the Phase 9 color-group audit, the CaptSilver P3b transplants,
and the Phase 8 bottom-dock surface-drift fix. Phase 8 remains the
open PHASE (shutdown/latency/lock-unlock/stranding threads are armed
and fire naturally). Phase 12 (upstream prep) stays parked under the
maintained-continuation framing.

High-priority remaining work below Phase 11, in rough order of
user-facing pain (each is a checklist item in its phase section with
the full context; this list is the map, not the territory):

1. Phase 8: session shutdown/logout handling as one deliberate
   sequence, including unloading the Corona's dependents in explicit
   dependency order (today's exit path is ad hoc; this is the
   crash-on-logout class).
2. Phase 8: startup latency (caught live: the dock takes noticeably
   long to appear at login) and the startup retry-exhaustion deadlock.
3. Phase 8: dock visibility across screen lock/unlock (observed live:
   docks can fail to come back).
4. Phase 8: multi-screen cloned-view applet-order sync (explicit sync
   instead of racing writes; prerequisite for the Replicate Dock
   continuation feature).
5. Phase 7: edit-mode entry/exit detection research (the subsystem
   that took a reference fork 8+ attempts; ours works but the
   detection contract was never nailed down) plus the drag-reorder
   jitter, double-click widget add, and position-aware drop insertion
   polish items.
6. Phase 7: icons stuck behind the close-button overlay (investigate
   root cause; visible in daily use occasionally).
7. Phase 4: struts/exclusive-zone reservation via layer-shell and
   kde_output_order_v1 primary-output detection (today's struts work
   but ride an older mechanism; the output-order protocol is what
   Plasma itself uses).
8. Phase 8: full settings-window control audit against Qt5 semantics
   (the Tasks-config lesson says half-wired controls hide silently).
9. DONE 2026-07-17: the e2e GUI harness is a maintained nested-vehicle
   suite (tests/e2e, desk-independent kwin_wayland --virtual, D-Bus
   assertions + KWin ScreenShot2 pixel goldens + a state-vs-render
   geometry guard). The local gates stay CI-portable by design; the
   hosted microvm pipeline is the only remaining piece.
8a. DONE 2026-07-17 (PR #1): Phase 4 X11 removal - the port is
    Wayland-only, main() refuses non-wayland platforms, build-check is
    single-tree. One decision still mine: retire vs keep byPassWM.
9a. Phase 10 accessibility/automation quartet: CODE HALF DONE 2026-07-17
    (D-Bus exposure, the dock-window keyboard focus mode, Accessible.*
    rollout, deterministic e2e conversion). REMAINING: the Orca
    screen-reader acceptance pass (my ears) and the two filed
    keyboard-focus follow-ups.
10. Phase 10 extraction/tail: the EX live-verification recipes
    (EX-14/15/17) run in the vehicle now; WindowId hardening done
    session two. REMAINING: the known-bug-list sweep and EX-12/EX-19
    (desk work), the two stuck-overlay / uninvoked-config-view findings.
11. DONE 2026-07-17: Phase 9 color-group audit (all ~160 theme reads
    classified across 44 files, three items ticked, two fixes landed).
12. Phases 2/3: the mechanical tail (QStringLiteral wrapping, C-cast
    conversion, QDBusInterface modernization, one KWindowEffects
    signature) - low priority, no user-facing behavior.

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
      Commits: c221f6e1 (`docs/reference/TESTING.md`)
- [x] Decide the test-infrastructure shape while it can still grow
      with the code: coverage-ratchet baseline (fails on regression),
      headless QML interaction-test harness skeleton. The
      e2e/screenshot harness needs a runnable dock and stays in
      Phase 10
      Commits: c221f6e1 (decision recorded in `docs/reference/TESTING.md`;
      harness/ratchet code lands with the Phase 2 compile milestone)
- [x] Editor code intelligence: clangd resolves the whole Qt6/KF6 tree
      with zero false diagnostics straight from `nix develop`. Root
      cause was no compile DB (clangd parsed files standalone -> every
      Qt include "file not found", cascading into hundreds of bogus
      undeclared-identifier errors). Fix has three parts: emit
      `build/compile_commands.json` unconditionally
      (`CMAKE_EXPORT_COMPILE_COMMANDS ON`, a side artifact only - build
      graph proven identical OFF->ON, binaries byte-identical); a
      repo-root `.clangd` pointing at `build/`; and a `--query-driver`
      glob so clangd runs the nix g++ wrapper to recover the Qt/KF6
      umbrella and libstdc++ include paths nix injects via
      `NIX_CFLAGS_COMPILE` (never recorded in the DB). Verified headless
      with `clangd --check`: layoutmanager.cpp 21->0, effects.cpp 21->0,
      justifysplitters.h 5->0 real diagnostics. Contributor flow in
      `docs/reference/clangd-setup.md`; devShell ships clangd; `.vscode/` config
      committed.
      Commits: <filled at merge>

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

### Phase 4: Window-system backends (Wayland-only; X11 removed 2026-07-17)

Scope decision, third revision (2026-07-17): X11 support is REMOVED.
The middle position ("best-effort: must compile, never blocks",
recorded below for history) is retired. What changed:

- KDE set the end date. "Going all-in on a Wayland future"
  (blogs.kde.org, 2025-11-26): Plasma 6.7 is the FINAL release with
  an X11 session; Plasma 6.8 (expected October 2026) ships
  Wayland-only; the 6.7 X11 session is supported only into early
  2027. This machine's pinned substrate (Plasma 6.7.3) is already
  the last X11-session Plasma there will ever be.
- A dock is a Plasma-session component. When the session itself can
  only be Wayland, an X11 windowing backend has no user left - it is
  dead code with a real cost: every gate builds the WITH_X11=OFF
  variant twice-over, every wm-adjacent reader pays the #ifdef tax,
  and none of it has ever been live-verified here.
- The original keep-X11 justification was upstream-mergeable value;
  the maintained-continuation decision (2026-07-15) already retired
  upstream mergeability as a planning constraint.
- Both reference forks removed X11 long ago (latte-dock-qt6's
  1cef7fe7 confirms theirs).
- X11 APPLICATIONS are unaffected: they run under Xwayland and reach
  the tasks model through plasma-window-management like any other
  window (KDE's announcement makes the same distinction). Nothing in
  window TRACKING changes; only the X11 windowing BACKEND goes.

Historical scope note (superseded): Wayland was always the primary
target and the only one verified live; X11 was kept compiling under
HAVE_X11 as a best-effort port with breakage tracked but never
blocking.

#### X11 removal checklist (added 2026-07-17)

- [x] Remove `XWindowInterface` (app/wm/xwindowinterface.*) and the
      corona's HAVE_X11 interface selection in lattecorona.cpp; the
      Wayland interface becomes the only backend, unconditionally
      Commits: 20a3c2506
- [x] Refuse to start on any non-wayland platform at the main()
      boundary (added during execution, my direction): default-deny
      with the offscreen QPA as the one named harness exception;
      driven four ways (xcb refuses with the platform named, minimal
      refuses, offscreen passes, wayland is the live dock)
      Commits: 1ddb4140f
- [x] Strip the remaining HAVE_X11 conditional sites, deleting the
      X11 arm and unconditionalizing the Wayland arm - audit each
      site so no Wayland behavior secretly rides an #else:
      app/infoview.cpp, app/primaryoutputwatcher.cpp (the xcb RandR
      native event filter), app/view/effects.cpp, app/view/view.cpp,
      app/plasma/extended/theme.cpp, app/tools/commontools.cpp,
      app/settings/settingsdialog/settingsdialog.cpp,
      declarativeimports/core/quickwindowsystem.cpp
      Commits: 9b41dd3ae (ifdef arms), 24e54b7d8 (the ~40
      permanently-false isPlatformX11 runtime branches, per-site
      audit in the body), 019c3aed3 (Effects visual-mask machinery
      collapse - updateMask/forceMaskRedraw/maskCombinedRegion and
      the caller-less subtracted/united region cluster)
- [x] Remove WITH_X11 from the build system: top-level CMakeLists
      option + X11/XCB find_package calls, app/config-latte.h.cmake
      and declarativeimports/core/config-latte-lib.h.cmake defines,
      app/CMakeLists.txt + app/wm/CMakeLists.txt conditionals
      Commits: 3f857fbff (wm/CMakeLists block in 20a3c2506)
- [x] Drop the X11 dependency set from flake.nix buildInputs (libx11,
      libsm, libice, libxcb, libxcb-util, libxrandr) and package.nix;
      kwindowsystem STAYS (KX11Extras was the only X11-path consumer,
      the framework itself is platform-neutral) - update its comment
      Commits: 3f857fbff
- [x] Collapse the both-variants gate discipline: build-check.sh
      builds one tree (drop build-no-x11), gate-all.sh comment,
      CLAUDE.md references ("both WITH_X11 variants" in the C++
      standard-raise process and build notes), docs/reference/TESTING.md if it
      names the variant pair
      Commits: 3f857fbff
- [x] Post-removal audit for textual X11 survivors: KX11Extras,
      QNativeInterface::QX11Application, isPlatformX11-style
      branches, xcb includes, stray WId comments. WindowId's
      QByteArray design STAYS AS IS (uuid strings on Wayland; the
      decimal-string X11 arm simply never occurs anymore - the type
      does not narrow, per the newtype hardening pass; fromX11WId
      stays with its windowinfowraptest pin as the design record).
      Result: only main.cpp's refusal-path comments mention xcb, by
      design; byPassWM found and deliberately NOT removed (next item)
      Commits: (audit finding record, this docs commit)
- [x] Follow-up sweep from the PR #1 independent review (non-blocking
      leftovers, isPlatformWayland-SHAPED branches the isPlatformX11
      sweep did not reach): subconfigview.cpp trackedWindowId() still
      falls back to WindowId::fromX11WId on !isPlatformWayland (dead:
      main() refuses those platforms; offscreen harness runs take the
      fallback harmlessly), and subwindow.cpp's visible-hack timer
      block re-derives an X11 id on forcedShown. Fold both into the
      wayland-id-only shape
      Commits: ed9416a21 (ff-merge preserves it)
- [ ] View-side activity-stop reshow hack: assess with live evidence,
      do not blind-delete (filed from the id-sweep review). Latte::View
      carries a parallel same-named cluster (view.cpp
      showHiddenViewFromActivityStopping + its timers + View::forcedShown
      -> visibilitymanager republishes frame extents). The SubWindow twin
      was X11-only, but the View comment claims the compositor clears
      frame extents on activity close - possibly TRUE on wayland KWin;
      needs an activity-close reproduction before any removal
      Commits:
- [ ] DECISION OWED (mine): the byPassWM layout setting. Found by the
      audit: Qt::BypassWindowManagerHint is X11-only machinery, but
      byPassWM is a Qt5-faithful, user-persisted layout setting with
      a config-UI surface (BehaviorConfig.qml) and ~36 code
      references. Retiring it means a config migration and a settings
      page change, which is a product decision, not an audit sweep -
      per the Qt5-faithful rule it stays until I decide. The
      visibilitymanager's bypasswm-on-X11 frame-extents fold already
      landed (24e54b7d8); what remains is the property chain, the
      setting and the UI
      Commits:
- [x] Survivor sweep (2026-07-18): a second whole-tree grep for the
      X11 vocabulary after the first removal wave, classified in
      docs/tracking/x11-cleanup-audit.md. Executed the DEAD/stale removals:
      the X11-only PlasmaCoreThumbnail preview element (deleted, the
      ToolTipInstance thumbnail ternary collapsed to PipeWire), the
      dead NETWM + orphaned KWindowSystem includes the layer-shell
      cutover left in the two SubWindow helpers, and the stale `// X11`
      label over theme.cpp's live KWindowSystem include. Build-system
      removal reconfirmed complete (no HAVE_X11/WITH_X11 define, no
      find_package(X11|XCB), no X11 nix buildInput survives)
      Commits: 4c84c1bad, 67bd25638, 2f78a7d7e
- [ ] AWAIT SIGN-OFF (survivor-sweep proposals, docs/tracking/x11-cleanup-audit.md):
      D2 the aboutApplication keep-above X11-id no-op (a Qt WId fed
      through fromX11WId never resolves in windowFor() on wayland, so
      the keep-above silently does nothing - known-defects D19; stub
      or wire through the surface, do not blind-delete the intent);
      D3 the windowColorScheme else arm (X11 decimal parse, dead in
      production, collapse to the wayland branch - retires the last
      non-test fromX11WId parse caller); D5 the dead netwm.h include
      in the vendored plasmoid/plugin/backend.h (verify against
      current upstream plasma-desktop before removing, to keep the
      vendored diff clean); D1 strip the WindowId X11 parse surface
      (gated on D2/D3 first); S1 the WindowId QByteArray-vs-QUuid
      substrate (a separate change, downstream of D1, touches the QML
      ArrayBuffer boundary). None applied this pass
      Commits:
- [x] README + docs register update (timeless: "Wayland-only,
      matching Plasma 6.8+'s Wayland-exclusive direction"), CLAUDE.md
      Plan section phase list wording
      Commits: (this docs commit; CLAUDE.md wording landed 03eba5618)

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
- [x] Bind `kde_output_order_v1` for primary-output detection (Plasma
      6 KWin no longer advertises `kde_primary_output_v1`), treat the
      first output in the order as primary. VERIFIED 2026-07-16: the
      order protocol is bound (WaylandOutputOrder in
      primaryoutputwatcher.cpp) and consumed by both screen pools;
      across a dozen live restarts this session every view landed on
      its recorded output (DP-2 x4 edges + DP-3 bottom + clones,
      dumpwins-checked each time).
      Commits: 958ec6aa (kde_primary_output_v1 stays bound as the
      fallback for older sessions)
- [x] Implement struts/exclusive-zone reservation via
      `zwlr_layer_shell_v1`'s `exclusive_zone` through LayerShellQt
      directly (`PlasmaShellSurface`'s `PanelBehavior` is deprecated
      and ignored by KWin, reserves nothing). Reserve the zone equal to
      *current visible* panel thickness, not a max-possible-expansion
      value (over-reservation bug latte-dock-ng hit).
      LIVE-VERIFIED AND HARDENED 2026-07-16: the mechanism was right
      but a trigger gap silently defeated it - canSetStrut() reads
      isOffScreen(), true throughout startup, and isOffScreen was the
      only strut input with no re-trigger, so AlwaysVisible docks
      could sit reserving NOTHING (protocol log: zone published then
      reset to 0; KWin maximize area confirmed). Fixed 538abc8ec
      (isOffScreenChanged -> coalesced strut update; KWin clientArea
      verified full reservations on every edge post-startup). Dodge
      modes correctly reserve zero (real config checked). The
      REMAINING startup-stranding defect this hunt exposed is filed
      as its own Phase 8 item below.
      Commits: 8e9a3dc6 (mapping layer + unit test), fb1302f5 (zone
      rides the dock's own layer surface), 538abc8ec (off-screen
      re-trigger + loud remove-reason logging)
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
      handler with conditional acceptance. See docs/archive/ng-upstream-audit.md finding A.
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
      combos, so if a combo gains an option the transcription must gain
      it too)
- [ ] Complete the real settings-control and runtime-behavior acceptance in
      `docs/tracking/settings-surface-completion-plan.md`. The work is split into
      one-PR source inventory, read-only registry, driver, component-family,
      page, migration, runtime-core, observability, and e2e matrix units, with
      explicit dependencies. Approval currently covers only the evidence-first
      and scaffold sequence. D29 (task-icon middle click appears to execute
      left-click behavior) must capture its exact row, stored action, event
      recipient, request, and independent effect before any fix is selected.
      D30 (Behavior mouse actions expose fixed booleans instead of full choices)
      remains code-grounded but has no approved action expansion.
      D56 (pure-launcher task wheel uses inherited asymmetric activation) is
      accepted as Qt5-faithful and needs a permanent regression guard. D24 (TypeSelection
      Dock/Panel presets write two dead keys) stays an independent appearance/
      schema cleanup. Any Qt5 divergence requires explicit maintainer sign-off.
      D31 (valid Justify splitter moves reset after restart) was fixed by PR #73
      and is not owned by this workstream.
      Commits:
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
      is recorded in docs/reference/live-only.md for live verification)
- [x] Fix D25 (task icons stay stale after icon-theme changes): forward real
      icon-theme transitions to a shared icon component, then synchronously
      rebind each stable QIcon source so task and tooltip preview rasters refresh
      without an empty rendered interval.
      Commits: 8423fab40 (fix and focused render regression), 6765b2320
      (coverage ratchet). Evidence: the named fixture changes from red to blue
      with its source signal and cache key unchanged; a nameless pixmap icon
      remains green. The coverage ratchet reports 94 ctest entries and 31 paired
      unit headers.

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
      disappears" confirmation is drivable in the nested vehicle: drive
      the removal and assert the applet set via the viewAppletsData
      readback)
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
      this is the item to reopen. Headless coverage note: this is
      drivable now with no GUI CI vm - the nested-vehicle e2e suite
      (scripts/run-e2e.sh, default desk-independent kwin_wayland
      --virtual) injects fakepointer drag against a real client surface
      with no live desk, and 050-drag-reorder-launchers.sh already drives
      fakepointer drag-reorder there. An explorer->dock drop recipe just
      needs writing against that vehicle; it is not written yet.
      Commits: (no port-side change needed)
- [x] Position-aware drop insertion. VERIFIED PRESENT BY CONSTRUCTION
      2026-07-16: the intended insertion index is carried explicitly -
      DragDropArea's onDrop reads dndSpacerIndex() and encodes it into
      the x,y handed to processMimeData via indexToMasquearadedPoint
      (base -23456), and onAppletAdded decodes it back through
      isMasqueradedIndex -> addAppletItem(applet, index). Plasma's own
      geometryHint is only trusted when no spacer index exists. The
      0ms-singleShot prescription addressed ng's C++ drop handler
      shape; our drop path touches no QML items beyond the
      already-parented spacer, so there is nothing to defer.
      Commits: (mechanism landed across the drag-drop port; no new
      change needed - this entry records the audit)
- [x] **Widget add via click**: RESOLVED by 0aa7ffb6's adopted
      explorer QML, audited 2026-07-16: AppletDelegate uses exactly
      the prescribed TapHandler.onTapped coexisting with the sibling
      DragArea (no MouseArea.onDoubleClicked, no debounce band-aid).
      Single-click add matches current plasmashell's explorer
      semantics. Landing position is Qt5-faithful: WidgetExplorer's
      addApplet -> createApplet carries the (-1,-1) no-position
      sentinel at the pin (containment.h:208), which our
      addAppletItem(x,y) routes to end-of-main-layout append, same as
      Qt5. Found and fixed while auditing: the 'aplet' typo in
      pluginNameForApplet's fourth fallback (c97c6bb38).
      Commits: 0aa7ffb6 (mechanism), c97c6bb38 (typo)
- [ ] **Default insertion position**: implement boundary-applet
      detection (system tray, the Latte tasks plasmoid) once, shared by
      both the drag and double-click add paths, so new widgets land
      just before the boundary rather than at the absolute end.
      ANALYSIS 2026-07-16: this is an ng UX deviation, NOT Qt5
      behavior - Qt5 Latte appended positionless adds at the absolute
      end, and this port currently matches Qt5 exactly (verified: the
      (-1,-1) sentinel path falls through to end-append). Implementing
      it is a deliberate behavior deviation under the
      maintained-continuation framing - MY CALL to make; if wanted,
      the shared hook is addAppletItem's end-append fallback in
      containment/plugin/layoutmanager.cpp:1300 plus the index==count
      arm of the index overload.
      Commits:
- [x] **Edit-mode entry/exit detection** - RESEARCHED AND PINNED
      2026-07-16. Verdict: the notification is RELIABLE on Qt6/Plasma6
      and no polling or overlay machinery is needed. At the pinned
      libplasma 6.6.5, Applet::setUserConfiguring is a synchronous
      equal-value-guarded setter with a plain NOTIFY (read from
      applet.cpp); our detection is one property with one writer
      (PrimaryConfigView -> setUserConfiguring, containment QML binds
      Plasmoid.userConfiguring at main.qml:99, View re-broadcasts the
      containment signal as inEditModeChanged). ng's 8 attempts were
      chasing bugs that live elsewhere - the same traps this port
      already root-caused as fb621102 (stale PERSISTED
      inConfigureAppletsMode restoring edit visuals at boot) and
      4a8ac480 (chrome focus-grab racing its own mapping; hideEvent
      running session-end work on transient hides). The contract is
      pinned in tests/contracts/userconfiguringcontracttest.cpp so a
      libplasma bump that defers or compresses the signal fails in
      ctest before it breaks detection.
      Commits: 87417a0c7 (contract pin; no product code change needed)
- [x] **Drag-to-reorder jitter**: RESEARCHED 2026-07-16 (full record:
      docs/agent-logs/2026-07-16-reorder-research.md). Verdict: NO
      CHANGE NEEDED. The qt6 fork feels clean because it left
      upstream Qt5's placeholder design alone, and our port runs the
      SAME algorithm (line-for-line press/move/release logic;
      midpoint swap test; the placeholder itself is the hysteresis -
      after a swap the pointer sits inside it and childAt
      self-suppresses; swaps are instant, no animation window). Ours
      is strictly better in the two spots the trees differ:
      fractional drag coordinates (their int lastX/lastY still has
      the wayland truncation drift we fixed) and the repositionable
      LatteCore.Dialog handle tooltip. ng's jitter is their own
      live-item feedback loop (insertBefore on the DRAGGED item,
      16ms poll) suppressed by stacked dead zones/hysteresis/
      cooldowns - never import those knobs; if reorder ever jitters
      here, the bug is something re-reading geometry it just changed.
      Commits: (no change; the research and its two follow-up fixes
      are recorded under the stuck-icons item below)
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
- [x] Investigate and fix whatever causes **icons to get stuck behind
      other elements** after repeated drag-reordering. ROOT-CAUSE
      CANDIDATE FOUND AND FIXED 2026-07-16: the task-reorder path
      raises the dragged delegate to z=100 and NO tree (upstream, ng,
      qt6, us pre-fix) ever reset it - repeated reorders left several
      delegates at z=100 permanently, stacking over lower-z chrome.
      The applet path always reset (currentApplet.z = 1); the task
      path now restores z=0 at Drag.onDragFinished. Also removed the
      inert target.animating guard (property never existed on
      Latte's ListView; comment at the site records the alternative
      if a moveDisplaced decision freeze is ever wanted). LIVE CHECK
      PENDING at the desk: repeated real-mouse reorders, then check
      nothing stacks over the close-button overlay.
      Commits: 480ae30e3
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
docs/tracking/session-handoff.md for the measurements and
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

- [x] Bottom-dock layer surface drifts left of its reported geometry
      (filed 2026-07-17 from the e2e promotion unit, evidence in
      docs/agent-logs/2026-07-17-e2e-promotion.md finding 6; root-caused,
      fixed and verified 2026-07-17, ledger in
      docs/agent-logs/2026-07-17-phase8-surface-drift.md). ROOT CAUSE: a
      masked dock (behaveAsDockWithMask) keeps a window that spans the
      whole screen along its length axis and realises Left/Center/Right
      INTERNALLY through its mask; the layer-shell backend anchored a
      Center dock to a SINGLE edge and leaned on the compositor to centre
      it, but wlr-layer-shell centres a single-edge-anchored surface
      inside the region OTHER docks' exclusive zones leave free, not the
      whole output. A bottom dock beside a left dock (48px zone) and a
      right dock (88px zone) landed (48-88)/2 = -20px off the screen
      centre its own full-screen geometry math assumed (the -20/-44/-74
      spread and the re-anchoring on any re-commit were the side docks'
      zones committing asynchronously during startup). Vertical docks did
      not drift: their window is sized to the AVAILABLE region, so the
      compositor's centring is an identity. FIX: anchorsFor() takes a
      windowSpansScreenLength flag; a horizontal masked dock anchors BOTH
      length edges for every alignment, so the compositor pins it corner
      to corner across the full screen and never re-centres it
      (View::windowSpansScreenLength() = !behaveAsPlasmaPanel() &&
      horizontal; vertical docks and sized panels stay single-edge; the
      layershellmappingtest pins both branches). ISOLATION PROOF: a
      single bottom dock with NO side docks sat at x=0 drift 0; restoring
      the side docks brought the drift back. DESK-VS-VEHICLE: real, not a
      vehicle artifact, but config-dependent - the drift is purely
      (leftZone-rightZone)/2, so Bree's current no-side-dock desk layout
      shows 0 (verified live: both desk docks render exactly at their
      reported origin) while her side-dock "My Layout" drifts, the
      "easy to miss by eye" case the item predicted. GUARD:
      tests/e2e/060-geometry-agreement.sh now PASSES and its
      # e2e-expect: fail marker is removed - a permanent standing guard.
      The e2e recipes' pixel calibration (050) and e2e_view_window_x
      stay as best-effort correction but no longer compensate for a live
      bug. Also the run-e2e suite-flakiness root: with the surface no
      longer re-anchoring mid-suite the pointer/keyboard-nav recipes run
      stable back-to-back.
      Commits: (to fill post-rebase)

- [x] Maximize-length repaint: "maximize panel length in presence of
      maximized windows" grows a masked dock to full width while a
      maximized window is present and shrinks it back on un-maximize; on
      Qt6 wayland the just-vacated edge pixels rendered as a lighter
      frosted/semi-transparent band at the former extent (caught live on
      a real top dock). ROOT CAUSE: Qt6's wayland backend couples
      QWindow::mask() to each frame's submitted damage - a shrinking mask
      clips the vacated region's transparent repaint out of submitted
      damage, so the compositor keeps compositing stale panel pixels
      there. Qt5/X11 shape masks did not clip damage; platform-forced Qt6
      deviation, no upstream precedent (the same mask/damage coupling the
      empty-mask setInputMask comment already documents). FIX: a pure core
      (app/view/inputmaskflush.h, Latte::ViewPart::InputMaskFlush) owns the
      setMask-region decision - keep the UNION of applied+band across a
      shrink so the vacated edges stay inside the mask, collapse back to
      the band once it settles; Effects routes setInputMask through
      applyInputMaskToWindow() + a 100ms coalescing timer, keeps
      m_appliedInputMask, and reports it as viewsData's
      appliedInputRegionRects. VERIFICATION: inputmaskflushtest (sanitized,
      forced-assert) passes and is a real tripwire - a naive setMask(band)
      SIGABRTs on the shrink invariant result.contains(applied); the live
      union-then-collapse was captured in the nested vehicle with temporary
      instrumentation over a deliberate ruler-driven band shrink (the
      vehicle cannot flip existsWindowMaximized, so the identical maxLength
      path is driven instead) and guarded by
      tests/e2e/070-maximize-length-mask.sh. SCOPED after PR #24 review (F2):
      the input mask is the ONLY input driver, so the union-hold governed every
      shrink - including the autohide/dodge HIDE, where it held the full former
      band as the input mask while the dock was hidden (over-capturing input
      across the vacated body, measured in the vehicle). Root-caused to
      over-generalizing and scoped to LENGTH-axis shrinks
      (InputMaskFlush::windowMaskFor takes the dock's length axis; a thickness
      shrink to the reveal strip is applied directly), keeping maximize-length
      and zoom-out held while the HIDE no longer over-captures (re-verified in
      the vehicle: hidden dock applies its reveal strip directly). The "no
      frosted band" pixel confirmation on the real feature is a desk-check owed
      to Bree - steps in docs/agent-logs/2026-07-18-maximize-length-repaint.md.
      Commits: (to fill post-rebase)

- [x] D32 (Always Visible floating docks fail to track maximized windows when
      hiding the floating gap): the `View::WindowsTracker` enabled binding
      read nonexistent `root.screenEdgeMarginsEnabled` instead of the declared
      singular `screenEdgeMarginEnabled`. With no independent applet tracking
      requester, Always Visible left tracking disabled, cleared
      `existsWindowMaximized`, and made both maximize-length and hide-gap
      behavior inert. The typo came from upstream Qt5 commit
      `79705e9753edc45cfceccd432da86acbab6ae9b8` and survives in both reference
      forks; selecting the declared property restores the intended behavior.
      A marker-scoped source guard requires the singular arm and rejects the
      plural spelling; restoring the typo makes it fail. QML compile, qmllint,
      and the complete fast gate pass.
      Commits: 54572f495, 33c72b34e

- [x] D27 (maximize transitions leave a stale floating-gap work area):
      preserve maximize as a synchronous semantic edge and publish its
      resulting layer-shell reservation without the geometry throttle. The
      original `windowChanged` timer restarted forever under KWin's maximize
      geometry stream; PR #61 fixed that starvation by holding the first
      150 ms deadline, but a live trace exposed two remaining delays. The
      discrete `PlasmaWindow::maximizedChanged` edge still shared the
      coalesced route, and `strutsThicknessChanged` fed the one-second
      `updateStrutsAfterTimer()` path intended for geometry feedback. The
      measured thickness changed `44 -> 26` 1.069 s before the new exclusive
      zone reached `setViewStruts()`. FIX: typed
      `WindowChangeDelivery::{Coalesced,Immediate}` routes maximize directly
      while preserving coalescing for noisy window properties; direct
      thickness publication updates the layer-shell zone synchronously while
      absolute/screen/off-screen geometry remains throttled. GUARDS:
      `windowchangedebouncetest` pins immediate same-window delivery,
      unrelated-pending ordering, and no late duplicate;
      `sourceguardtest` pins direct thickness versus exclusively throttled
      geometry and rejects parallel routes; both passed 20 consecutive runs.
      `tests/e2e/071-maximized-window-length.sh` drove one uniquely tagged
      active nested Wayland client through both transitions, measured 284 ms,
      and verified KWin's complete screen-derived 88 px work area.
      Two cleaned real-session Firefox runs measured 114 ms and exact
      `0,26 1440x2534` KWin frame geometry.
      Commits: 983685c00, f6d5271c4 (bounded coalescing, PR #61),
      393d1f2bf, 7d3269011, e61a70016, 11861e947 (synchronous
      maximize/exclusive-zone follow-up, PR #70)

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
      RESIDUAL EXPLAINED 2026-07-15 from the pinned qtdeclarative
      6.11.0 source: any MultiEffect with shadow/blur builds an
      internal chain of LAYERED BlurItem ShaderEffects (blurSrc1..n,
      qquickmultieffect.cpp updateBlurLevel), each sampling the
      previous level; on the first frames after a shadowed window maps
      or re-shows, downstream levels sample layers that have not
      produced a texture yet - one 'No QSGTexture provided' per level
      per effect. Internal to Qt's MultiEffect, bounded per map or
      re-show (never accrues at idle), not fixable from Latte QML
      without dropping shadows; accepted as benign, no action.
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
      persisted, frames render. The guard is pinned headlessly:
      tst_plasmaindicatorgroupsvg.qml drives the real FrontLayer.qml
      through empty -> populated -> empty resources.svgs (the crash
      itself cannot be a contract test, it is a SIGSEGV), and
      tst_loadergatecontracts.qml pins the Loader-deactivates-before-
      inner-bindings-see-null ordering the guard's comment claims.
      Commits: 841c2ca4, 2b800846 (guard + ordering tests)
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
      LOGIN FINDING 2026-07-16: the 'slow to appear at login'
      complaint cannot implicate this port on the current machine -
      ~/.config/autostart/org.kde.latte-dock.desktop Exec points at
      the PACKAGED latte-dock-ng binary (the ruizhi-lab fork), so
      login starts ng, not our build; and the current boot dates to
      June 25, so no recent login measured anything. NEEDS MY HANDS:
      decide the autostart story for the port (point the entry at the
      packaged build from package.nix, or keep dev-session-only
      startup), then measure a real login with the journal +
      lifecycleState polling. The lifecycleState D-Bus readback
      (9d183984e) is the measurement instrument: poll for "running".
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
- [x] D26 (VisibilityManager inNormalState binding-loop warning): keep
      `inNormalState` declarative and break the synchronous AutoSize feedback
      edge. Its true-edge handler called `updateIconSize()` while the
      tracker-count binding was still evaluating; selecting a target entered
      `inAutoSizeAnimation`, changed `needBothAxis`, and fed the binding's own
      source. `Qt.callLater(sizer.updateIconSize)` defers only that
      continuation; the execution-time normal-state check rejects stale work
      and duplicate calls coalesce. The focused production-QML test fails four
      assertions with the direct call restored and passes all five scenarios
      with the deferred continuation; qmlinteraction passes 232 cases, the QML
      compile gate compiles 129 files, and AutoSize's 24 curated qmllint
      warnings drop to zero.
      Commits: 4cc94a48f
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
- [x] D31 (valid Justify splitter moves reset after restart): persist both
      splitter positions through explicit KConfig keys. `saveOptions()` inserted
      new values under their live keys but emitted `valueChanged` through absent
      `m_option` entries, so both notifications used an empty key and the
      backing loader restored the old zones. The equality-guarded writer now
      inserts and publishes the same explicit key. `layoutmanagerparkingtest`
      moves positions `1,5 -> 2,5 -> 2,4`, reconstructs the fixture, restores
      start/main/end zone counts `1/1/2` while preserving applet order, and
      proves that a healthy `2,4` save emits no notification and remains
      byte-identical. This is distinct from D5 (Justify splitter negative-insert
      UB), which repairs invalid positions.
      Commits: 91eff7c46 (fix and regression coverage), 3170dd4f9
      (required source attribution)
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
      loop, then trace the visibility/startup state machine.
      REPRO ATTEMPT 2026-07-16 (throwaway, one cycle): loginctl
      lock-session 3, 40s hold, unlock - CLEAN. Dock survived, all
      five views stayed mapped (dumpwins before/after), no ~20s exit,
      no invisibility. IMPORTANT nuance: a short lock never blanks
      the outputs, and the historical fingerprint ("CLEARED SCREEN",
      remove/re-add) is an OUTPUT event - the failing ingredient is
      DPMS/output-off, not the locker itself. Next repro needs a
      kscreen-doctor --dpms off cycle or a long lock (past the
      display-sleep timer) - hostile while I am at the desk, so it
      waits for a coordinated or idle moment. The ~20s mystery exit
      remains armed with the full quit trail + lifecycleState; the
      next natural occurrence names its trigger. (Mid-repro note: the
      "uninvoked chrome + duplicate view" seen in the log at
      18:09:15-19 was MY OWN hands at the desk - ParabolicEventsArea
      first-hover warnings at 18:09:12 prove a real pointer - not an
      uninvoked-action defect; do not conflate with the round-27
      boot-time uninvoked config view, which had no human.)
      Commits:
- [x] Implement session shutdown/logout handling as one deliberate
      pattern rather than iterating. RESOLVED 2026-07-16 against the
      primary source (plasma-workspace 6.6.5 shell/main.cpp read at the
      pin), not ng's end state - ng's mechanisms were verified claim by
      claim and two of four were their own invention: plasmashell has
      NO ksmserver shutdown poller and NO synchronous D-Bus
      shutdown-check (a non-XSMP wayland client cannot block ksmserver;
      session teardown arrives as SIGTERM from systemd after the user
      confirms), so neither was adopted and the 25s->1s timeout note is
      moot. What landed: AA_DisableSessionManager (replaces the
      commitData/saveState RestartNever handlers, which could never
      fire on wayland), setQuitLockEnabled(false) (KJob's
      QEventLoopLocker could quit the dock when the last job finished),
      and SIGTERM routed through the clean quit path via KSignalHandler
      (self-piped) with its own quit-trail line - unhandled SIGTERM was
      immediate death mid-write, the config-corruption class
      (iconSize=78 was written by exactly such a kill). Verified live:
      SIGTERM -> full trail -> all five throwaway views unloaded ->
      clean exit ~1.5s, no core. LIVE CHECK PENDING (needs my hands,
      kills the session): one real logout/login cycle observing the
      journal for the SIGTERM line and a clean exit.
      Commits: e02d1bcde (quit pattern), 9d183984e (lifecycleState
      D-Bus readback: startup/running/quit-requested/unloaded,
      observability-first)
- [x] Unload the Corona's dependents in explicit dependency order.
      RESOLVED 2026-07-16: the destructor's deleteLater() pile was a
      silent no-op - Qt flushes DeferredDelete exactly once after the
      loop returns (execCleanup), and ~Corona runs after that, so real
      destruction fell to ~QObject's child pass in CONSTRUCTION order
      (screen/theme services before the layouts manager that still
      consumed them: the crash-on-logout class). ~Corona now deletes
      explicitly, consumers before services, wm last; onAboutToQuit
      keeps the live-app work (Qt 6.11 emits aboutToQuit inside
      exit(), so the app is fully live there - pinned in
      tests/contracts/quitteardowncontractstest.cpp along with the
      two deferred-delete facts). The shared-QML-engine double-free
      sub-item is NOT APPLICABLE at our pin: libplasma 6.6.5's
      SharedQmlEngine holds a parentless std::make_shared QQmlEngine
      (weak_ptr singleton, no qApp parent, nothing double-deletes) -
      that fix was for ng's stack, verified against the pinned source.
      Commits: 525f556c6 (+ adcc68357 ratchet baseline)
- [x] Fix the startup retry-exhaustion deadlock in
      `LayoutManager::restore()`. RESOLVED 2026-07-16 as NOT
      APPLICABLE, verified against both trees: shouldRetry/
      expectedAppletCount/m_restoreRetryCount are latte-dock-ng's own
      additions (their layoutmanager.cpp:984-1000, a 150ms retry loop
      because THEIR plasmoidApplets() could be empty at restore time);
      our tree runs upstream's shape - restore() once at containment
      completion, no retry machinery to deadlock. The underlying race
      was checked too: live logs show every view's restore() seeing
      the complete applet set (recorded order == produced order on
      all containments), because the C++ containment populates its
      applets before the containment QML completes. What we did have:
      restore()'s three SILENT drop paths (null graphic item skips in
      both alignment branches, stale appletOrder ids) - now loud with
      applet ids, so if the premise ever breaks the failure names
      itself instead of surfacing as a missing applet.
      Commits: 9df0732f9
- [ ] Startup-stranding: some cold/loaded startups leave
      root.inStartup TRUE forever on every view (filed 2026-07-16
      from the struts hunt). Consequences: startupFinished() never
      reaches the positioner, absoluteGeometry keeps the -9999
      startup coordinates, canSetStrut() stays false so AlwaysVisible
      docks reserve nothing, and the slide-in never runs - yet the
      docks LOOK correct because layer-shell placement rides anchors,
      not window position, which is what let this hide. Reproduced 5x
      today, every time on first-run-after-a-C++-rebuild timing;
      never on warm restarts (8+ clean). Localization so far: on the
      one stranded run with partial instrumentation, restore() ran on
      all six views ("applets found" logged) but the startup delayer
      never fired and the watchdog never armed - the chain dies
      between the C++ hasRestoredAppletsChanged emission (a plain 2s
      QTimer lambda, unconditional) and the QML Connections handler /
      Timer.start() taking effect. Armed instrumentation (538abc8ec):
      breadcrumbs at both fragile hops plus a 15s console.warn
      watchdog dumping the full state - the next natural occurrence
      names its dying hop. Suspects to check when it fires:
      Connections delivery under type-loader pressure, the QML Timer
      never being started vs never firing, and the animation-driver
      angle (frame-callback-starved window freezing a running
      SequentialAnimation). Fix at the origin once named; do NOT
      paper it with a retry timer.
      Commits: 538abc8ec (instrumentation + the struts-side
      re-trigger that heals non-stranded runs)
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
- [x] Fix multi-screen cloned-view applet-order sync. RESOLVED
      2026-07-16: in our tree the guard is implicit (translatability
      of original ids to cloned ids), and all THREE manually-synced
      properties (appletOrder, lockedZoomApplets,
      userBlocksColorizingApplets) dropped a not-yet-translatable sync
      silently with no replay. Handlers now apply-or-defer with a
      retry at every ids-hash growth point; order re-reads the
      original at apply time, payload syncs replay their last payload
      (std::optional - pending EMPTY list is valid and distinct).
      Live-verified on the throwaway over the real dual-monitor
      session: AllScreensGroup on containment 1 spawned the DP-3
      clone, applet order maps position-by-position (plugin identity
      checked all five positions) - this also discharges the
      dock-replication-design prerequisite "live verification of
      screens-group clones". The deferred path's live replay needs a
      runtime reorder with a clone mid-init; its org.kde.sync
      "deferred" log lines name it when it fires naturally.
      Observability: viewAppletsOrder(containmentId) D-Bus readback.
      Commits: e3fdcae78 (apply-or-defer), f7561df37 (readback)
- [x] Edit-mode windows must re-derive geometry from the edited view's
      CURRENT screen after output hotplug (found live 2026-07-11 when a
      second monitor was added: the settings window kept the exact
      position/size computed for the old single-screen layout, landing
      in dead space between outputs). Also pair layer-surface output
      assignment with the screen the margins are computed against
      (LayerShell::applyFixedGeometry offsets are relative to the
      passed screenGeometry and only land right when the surface is on
      that output). See "Multi-monitor" section in
      docs/tracking/session-handoff.md for the measured evidence
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
      AUDIT PROGRESS 2026-07-16 (the named sub-checks, done from
      code + pinned sources): (a) TaskMouseArea/TaskAction coverage
      is ALREADY the fixed shape here - the action->command mapping
      is a single source of truth (code/TaskActions.js, written
      explicitly against ng's 9-offered-3-handled bug) and
      tst_taskactions.qml pins that every value each config combo
      offers resolves to a real command; nothing to fix. (b)
      ConfigInteraction.qml cfg_hoverAction is properly aliased to
      the combo with a bidirectional enum<->index mapping - ng's
      hardcoded-NoneAction defect is not present. (c) The
      folder-view isScreenUiReady decision RESOLVED as a shim
      (b8a489c84): the missing invokable left the applet's isUiReady
      undefined-falsy forever, worse than noise. STILL OWED and
      needs my hands: the full at-the-desk semantic walk of every
      control on every page against Qt5 feel/behavior (the
      Tasks-config lesson - a control that renders but applies
      nothing - can only be caught by driving each one and watching
      the dock respond).
      Commits: 32df5b47 (Tasks page config access), b8a489c84
      (isScreenUiReady shim)
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

- [x] Audit every Plasma/Kirigami color-group property read against
      what object is genuinely guaranteed to be present at that call
      site, not what's true in the common case - reading a property
      that doesn't exist on whatever theme object is actually in scope
      evaluates to `undefined` in QML silently (no warning, no crash,
      just a wrong color - both forks hit this as literally invisible
      UI, e.g. black indicator dots on a dark panel)
      DONE 2026-07-17: all 160 theme-read lines across 44 shipped QML
      files classified, per-site inventory in
      docs/agent-logs/2026-07-17-color-group-audit.md. Every property
      name read off Kirigami.Theme exists on the attached type, so the
      reads-undefined class cannot arise there; every CUSTOM palette
      object was verified against its C++ type (SchemeColors carries
      all 14 consumed members incl. button*/inactive*; colorizer
      Manager publishes every name the bridge consumers read;
      LatteCore.IconItem has backgroundColor/glowColor; layout has
      textColor and scheme). No live bare `theme.` reads remain (the
      two ComboBox.qml grep hits are commented-out code). Verdicts: 36
      font-only CORRECT, 34 CORRECT by explicit colorSet+inherit:false,
      77 CORRECT-substituted (Qt5 `theme.*` uniformly replaced by
      Kirigami.Theme with contrast pairing preserved; source-scheme
      deviation recorded, no behavior changed), 3 site groups
      SUSPECT-needs-live-check on mixed themes (fallback
      LatteIndicator dots, TaskIcon icon-color fallbacks,
      AppletAlternatives text over the plasma dialog SVG - desk recipe
      in the ledger), 2 DEFECTs fixed (the configure-mode popup
      collapse loop, item 3 below, and the coloredView bare
      themeExtended deref - the one unguarded consumer, latent via
      short-circuit timing, now compared against the null-safe
      colorizerManager.plasmaTheme).
      Commits: b84c1f8ff, c2a0d718c
- [x] For panel-contrast elements specifically, read the `Header` color
      group (`[Colors:Header]` in kdeglobals) rather than whichever
      `Theme` object is nearest at hand (which usually resolves the
      *window* scheme) - needed for mixed-theme setups (dark panel +
      light window scheme or vice versa)
      CLOSED AS A RECORDED DECISION 2026-07-17 (no behavior change):
      Header is the wrong target for this port. Plasma 6 panels read
      Header because plasmashell PAINTS panel surfaces with Header
      colors; Latte paints its panel from plasma-theme SVGs or the
      colorizer palette, so panel chrome must pair with THAT source -
      which is themeExtended.defaultTheme / the colorizer palette,
      exactly Qt5's `theme` behavior. Reading [Colors:Header] would
      pair chrome with a scheme the panel background does not follow.
      Qualifying panel-contrast sites are inventoried in the ledger;
      the shipped default indicator already routes through
      indicator.colorPalette (colorizer/plasma palette) and is
      correct. If the ledger's SUSPECT sites fail a live mixed-theme
      check, the fix route is the palette bridge, not Header.
      Commits: (decision recorded here and in the 2026-07-17 ledger)
- [x] Audit for other properties assumed present on the containment
      graphic object that Plasma 6 actually removed rather than
      deprecated (e.g. `backgroundHints`, which silently takes the
      undefined branch of any comparison rather than erroring)
      DONE 2026-07-17: every `Plasmoid.*`/`plasmoid.*` property read in
      shipped QML (17 + 11 distinct names) checked against the pinned
      libplasma 6.7.3 headers - all exist on Applet/Containment at the
      pin, including backgroundHints (so both known writes are valid);
      Dialog.backgroundHints/hideOnWindowDeactivate exist on
      PlasmaCore.Dialog; CompactApplet's PlasmoidItem-vs-Applet split
      verified; Panel.qml's removed-backgroundHints replacement was
      already commented in place. ONE real hit:
      `Plasmoid.applets[i].expanded = false` in containment main.qml -
      Containment::applets returns Plasma::Applet objects on Plasma 6
      (graphic-item list is a KF6 TODO in libplasma) and `expanded`
      lives on the graphic item, so the write threw a TypeError that
      aborted the handler and entering configure mode never collapsed
      open popups (latte-dock-qt6 ships the identical dead loop).
      Rerouted through extendedInterface.deactivateApplets(), premises
      pinned by tests/contracts/appletsexpandedpropertytest.cpp.
      Owed at the desk: popup collapses on entering configure mode.
      Commits: b84c1f8ff

### Phase 10: Stabilization / verification

#### Accessibility, keyboard and automation requirements (added 2026-07-16)

These four are first-class requirements, not polish. They were added
after the extraction initiative completed, when live verification
showed how much of the dock can only be driven by a pointer today.

- [ ] Robust keyboard navigation for EVERYTHING: every interactive
      surface (dock items and their context menus, group previews,
      edit mode including the ruler and the applet handles, all
      settings windows and their pages, applet popups) must be
      reachable and operable from the keyboard alone. Audit
      surface-by-surface against a written shortcut map (extend the
      existing global-shortcuts family: Meta+number already works);
      the map lands in end-user docs and each surface's keys are
      pinned by an offscreen qmltest where feasible. Focus order and
      visible focus indicators are part of the requirement, not
      separate.
      BASELINE INVENTORY DONE 2026-07-16
      (docs/agent-logs/2026-07-16-a11y-surface-inventory.md): full
      per-surface map with file:line for every input path, the
      existing kglobalaccel family verified (Meta+`/A/1-9/0ZXCVBNM,.
      + Ctrl variants + press-and-hold badges), and the P0 gate
      identified - the dock window only takes focus on
      AcceptingInputStatus (view.cpp:802-841), so a keyboard-nav
      focus mode gates everything else.
      INVENTORY CORRECTION 2026-07-16 (session two, found via the new
      activateTaskAt D-Bus smoke): the shortcuts host was NEVER being
      found on Plasma 6 - identifyShortcutsHost kept the Plasma 5
      child-scan walk while the containment root itself now carries
      the containmentViewLayout objectName (the Panel.qml viewLayout
      class of tree change), so activateEntryAtIndex,
      newInstanceForEntryAtIndex, setShowAppletShortcutBadges and
      appletIdForIndex were all silently dead and Meta+number was
      being serviced by fallbacks. Fixed at origin with loud no-host
      warnings; verified in the nested vehicle (entry activation
      toggles the active task, the exact Meta+N semantics).
      RE-VERIFY at the desk: Meta+number entry mapping and the
      press-and-hold shortcut badges, both through the now-live host.
      OWED TEST LANDED 2026-07-17: tests/shortcutshosttest.cpp builds
      the containment graph through the real itemForApplet() path with
      the REAL abilities/PositionShortcuts.qml qrc-aliased in, and
      asserts the discovery walk plus all four invokable signatures
      through the shipped statics (findShortcutsHost /
      resolveShortcutsHostMethods, extracted for the pin). Trap found
      at introduction: the engine's .qmlc disk cache can serve a stale
      compilation of qrc QML and mask exactly the drift the pin
      catches - the test disables it; see the follow-up item below.
      P0 FOCUS MODE LANDED 2026-07-17 (the gate everything else
      depends on): View::enter/exit/toggleKeyboardNavigation flips the
      window to layer-shell OnDemand + focus-accepting, with three
      bulletproof exits (Escape, the shortcut toggle, focus loss via
      QWindow::activeChanged); Meta+Alt+D kglobalaccel toggle;
      setViewKeyboardNavigation D-Bus action + viewsData
      keyboardNavigation readback; QML traversal over the same
      1-based entry space Meta+<number> addresses (slot math in the
      VisibleIndex core: countVisibleSlots/steppedVisibleSlot),
      Enter/Return/Space activate through activateEntryAtIndex (the
      Meta+N path = the click path), focus highlight through the
      items' indicator hover state and position badges for
      orientation. Nested-vehicle evidence:
      tests/e2e/keyboard-navigation-mode.sh 8/8 ok including the
      focus-loss self-exit (ledger
      docs/agent-logs/2026-07-17-keyboard-focus-mode.md). Desk pass
      owed (real keyboard: Meta+Alt+D, arrows/Home/End, Enter,
      Escape, badge + highlight visuals) - filed in
      docs/reference/manual-flake-removal-testing.md.
      TWO FOLLOW-UPS from the merge review (neither is the
      stuck-focus defect class): (1) if the compositor silently
      denies requestActivate at mode entry, the mode FLAG stays true
      with no keys ever reaching the window - Escape cannot exit
      (no key delivery) and the activeChanged watcher never fires;
      recoverable via toggle/D-Bus only. Fix shape: a bounded
      activation-confirmation timeout that reverts the flag when
      isActive() never goes true. (2) setViewKeyboardNavigation has
      no cross-view exclusivity - two views can hold the mode at
      once over D-Bus while the shortcut toggle assumes one; decide
      the contract (exclusive enter, or make the toggle multi-view
      aware).
      Group previews are
      keyboard-unreachable BY CONSTRUCTION (WindowDoesNotAcceptFocus
      in both modes); edit-mode chrome buttons are hand-rolled
      ghost-button chips; QWidget settings dialogs are already dense
      with QAction shortcuts. Work order per the inventory's gap
      list.
      Commits: worktree keyboard-focus-mode 634ae2083 (view state
      machine), a5759f19b (Meta+Alt+D), 0c3fa2a70 (D-Bus surface),
      84884c246 (VisibleIndex slot math), 1fbe25b45 (QML traversal),
      1c7fe9871 (owed pin test), 920e84ff9 (e2e drive) - retick with
      post-rebase hashes at merge
- [ ] qmlc-cache audit for the qrc QML harnesses (filed 2026-07-17
      from the shortcutshosttest introduction): qrc URLs carry no
      timestamp, so the engine's on-disk .qmlc cache can serve a STALE
      compilation of qrc-embedded QML - a deliberately drifted
      signature still passed the new pin until the test set
      QML_DISABLE_DISK_CACHE=1 (negative-tested both ways). Audit the
      other qrc-riding QML tests (representationswitchtest,
      layoutmanagerparkingtest) for the same masking and decide
      whether the flag belongs in latte_add_unit_test/ecm harness
      environment wholesale.
      Commits:
- [ ] Observability first - everything instrumentable, D-Bus as the
      primary surface: any subsystem's runtime state must be cheaply
      inspectable from outside the process, and anything a test needs
      to drive gets a driving surface. Concretely: view geometry and
      visibility state queries, applet/task enumeration with ids and
      positions, badge state readback, tracker facts (touching/
      maximized/active per view), colorizer decisions in force, mask
      and input-region rects, action triggers (activate task N, open
      settings for view X, enter/exit edit mode for view X). The
      existing org.kde.lattedock interface is the seed
      (updateDockItemBadge, showSettingsWindow, duplicateView already
      exist); design the additions as one reviewed interface, not
      accreted one-offs. Rules: (a) if a live check in this plan
      needed pixel-peeping or pointer acrobatics to observe STATE (not
      to exercise input), that state gets a readback; (b) new
      subsystems ship their observability surface as part of the
      definition of done, not as a follow-up; (c) safety is a
      constraint - read surfaces expose state, never arbitrary
      execution; mutating surfaces stay coarse-grained user actions
      (the kind the UI already offers) or sit behind an explicit
      debug-mode gate; nothing exposes secrets or other apps' content.
      D-Bus is the default; where D-Bus fits badly (per-frame data,
      high-rate traces) categorized qCDebug logging or the debug
      window are acceptable alternates, but state that a test asserts
      on should be pull-queryable, not log-scraped.
      SEED LANDED 2026-07-16: lifecycleState() readback
      (startup/running/quit-requested/unloaded) shipped with the
      session-shutdown work (9d183984e) and viewAppletsOrder with the
      clone-sync work (f7561df37). THE REVIEWED INTERFACE DESIGN IS
      WRITTEN: docs/reference/dbus-observability-interface.md (read surfaces as
      JSON keyed on containment id, coarse actions, the debug gate,
      the rejected-with-reasons list, and the 4-step landing order -
      viewsData + setViewEditMode first). Implementation proceeds
      per that document. STEP 1 LANDED 2026-07-16: viewsData()
      (serializer in app/dbusreports.h/.cpp, pure layer pinned by
      dbusreportstest) and setViewEditMode(u,b) (enter via
      View::showSettingsWindow, exit via
      PrimaryConfigView::hideConfigWindow - the Edit Dock and
      close-button paths). Steps 2-4 remain.
      STEPS 2-4 LANDED 2026-07-16 (session two): all seven remaining
      surfaces (viewAppletsData, activateTaskAt, trackerData,
      setViewVisibilityMode, viewTasksData, colorizerData,
      layoutsData) with data-driven serializer tests, deliberate
      schema additions recorded in the design doc, busctl smokes run
      in the nested-kwin vehicle at merge. The design doc's sequencing
      section carries the per-step record.
      Commits: 9d183984e, f7561df37 (seeds); fdfdf5b00, 07e91e456,
      dd3046c03, 77a9586cc (step 1; the worktree merge rebased the
      hashes the agent log records); 2390e7220..bb3d8c53f (steps 2-4,
      11 commits, post-rebase)
- [ ] Convert nondeterministic e2e tests to deterministic ones: every
      screenshot-compare or sleep-and-hope check that is really about
      STATE moves to a deterministic D-Bus-driven or offscreen-qmltest
      assertion. A test keeps a live cursor ONLY when the thing under
      test IS pointer delivery (input masks, hover chains, drags) -
      those keep using fakepointer as today, with the glide rules from
      the live-verification skill.
      Commits:
- [ ] Full AT-SPI support: the dock must be a first-class citizen for
      assistive technology. Wire Qt's accessibility bridge end to end:
      Accessible.role/name/description on every interactive QML item
      (tasks announce their app name and window count, badges announce
      their value, edit-mode controls announce their function), correct
      focus events, and a real screen-reader pass with Orca as the
      acceptance test. This also multiplies the keyboard-navigation and
      D-Bus items: AT-SPI exposes an introspectable tree that
      deterministic e2e tests can assert against.
      BASELINE 2026-07-16 (same inventory as the keyboard item,
      docs/agent-logs/2026-07-16-a11y-surface-inventory.md): exactly
      five Accessible.* usages exist in the whole tree
      (components/Label, WidgetExplorer heading, AppletAlternatives),
      zero C++ QAccessible - the dock window's AT-SPI subtree is
      effectively empty. KEY MECHANISM, verified from the pinned
      qtbase 6.11 source (dbusconnection.cpp:99-103): the
      accessibility bridge enables per-process via the org.a11y bus
      flags or QT_LINUX_ACCESSIBILITY_ALWAYS_ON - no plasmashell
      dependency, and the env var is the switch for Orca-less AT-SPI
      e2e asserts.
      ACCESSIBLE ROLLOUT LANDED 2026-07-17 (branch accessible-rollout,
      6 feat commits; ledger
      docs/agent-logs/2026-07-17-accessible-rollout.md): role/name/
      description/press-action on task items (badge values through a
      new TooltipTextComposer accessible-description composer - core
      plan + i18n in the wrapper, the EX-17 split), the audio badge as
      the same checkable Mute the context menu offers, applet
      containers (activation factored once as toggleExpanded()),
      HeaderSwitch/ComboBoxButton, edit-mode chrome (canvas chips +
      ConfigOverlay handles reusing their TextMetrics hint strings),
      and widget-explorer cards (press action = the tap's addWidget()).
      Accessible.focused tracks the keyboard focus mode on tasks,
      applets and explorer cards. Pinned offscreen:
      tst_taskaccessible / tst_accessiblecontrols /
      tst_addwidgetsaccessible + sanitized composer vectors in
      tooltiptexttest. Mechanism notes that will bite again (QQC2
      native AT press emits clicked() only; required-properties mode
      kills bare role reads; the qmllint ratchet counts i18n() and
      inline-component outside-id reads) are in the ledger. STILL OPEN
      before ticking: the Orca desk pass
      (docs/reference/manual-flake-removal-testing.md "Orca screen-reader
      pass"), the merge review's one nit (canvas Button.qml wires an
      undocumented Accessible.onToggleAction duplicating onPressAction
      - functionally safe, each fire is an independent correct toggle,
      but the duplicate AT-SPI action surface wants a comment or
      removal), the previews dialog (deferred with the P1 focus rework),
      per-instance labels on the config pages' template controls (the
      P3 page pass), and C++-side focus-event verification against a
      live AT client.
      Commits:

- [x] WindowId newtype hardening (filed 2026-07-16 from the EX-23
      design review). 8e8cdf31 replaced upstream's WindowId = QVariant
      with QByteArray because Qt6 removed QVariant's relational
      operators (QMap<WindowId,...> keys and wid<=0 checks stopped
      compiling); right substrate, but stringly-typed by today's law:
      any QByteArray or bare char* converts implicitly into a
      WindowId parameter (QT_NO_CAST_FROM_BYTEARRAY is still stubbed
      out), and all six wid.toUInt() sites in xwindowinterface.cpp
      ignore the ok flag, so a malformed id silently becomes window 0
      (X11 is best-effort, which contains but does not excuse it).
      The fix: a newtype wrapper keeping QByteArray storage (zero
      conversion from KWayland uuid()) with explicit
      fromWaylandUuid()/fromX11WId() constructors and an
      std::optional<quint32>-returning toX11WId() so the silent-0
      parse becomes a loud absence at the boundary; the QML-facing
      winId property stays QVariant (Qt6 exposes raw QByteArray to
      QML as ArrayBuffer - the 8e8cdf31 boundary decision holds).
      Alternatives weighed and rejected in the review:
      std::variant<monostate,quint32,QByteArray> over-models a
      single-platform process and touches every call site harder;
      QString doubles memory and adds a conversion. Blast radius is
      the whole wm/ surface - do it as its own pass with the
      windowinfowraptest and EX-23 predicate tests as the safety net,
      not opportunistically inside another unit.
      LANDED 2026-07-16 (session two, own pass as required; ledger
      docs/agent-logs/2026-07-16-layershell-guard-windowid.md): the
      checked X11 parse retired two latent toInt() overflow bugs and
      the lattecorona D-Bus silent-0 (refusals are loud now); the
      wayland trackedWindowId re-resolve's stale numeric premise is
      recorded in place; windowidtest pins explicit-only construction
      (static_asserts), exhaustive malformed rows, window-0
      unproducibility, and QMap ordering against the old substrate.
      Same wave: the LATTE_LAYERSHELL_HAS_SET_SCREEN try_compile no
      longer silently flips on a broken configure (result cached on
      LayerShellQt_DIR; non-signature probe failures FATAL_ERROR;
      46ce2dfbb). STILL OWED at the desk: one wayland pass over the
      wm surface (active-window tracking, dodge, scheme following,
      subwindow uuid re-resolution after reshow) - in the manual
      list; and the deferred call on tightening the three
      trackedWindowId() lazy re-resolves to isEmpty().
      Commits: 46ce2dfbb, 4cdd13a72, 4ada8834a, 77e31bed4, 3c3f2d557,
      4ebadede9 (post-rebase)

- [ ] Verify against the full known-bug list at the top of this doc by
      actually driving each interaction (add/remove/drag/edit-mode/
      right-click/task-manager) in a running session - not by reading
      the code and concluding it looks right; both forks' history shows
      real, reproducible, user-facing bugs went unnoticed for a long
      time under exactly that kind of confidence
      Commits:
- [ ] CaptSilver testability adoption (REPLACES the microvm GUI-CI
      plan, my direction 2026-07-16; the full analysis is
      docs/archive/captsilver-testability-adoption.md and the study record is
      docs/agent-logs/2026-07-16-captsilver-testability-study.md).
      HARD CONSTRAINT: everything must run in CI under a plain
      VM - no real GPU. Sub-items, in adoption order:
      - [x] P1: the sceneprobe visual-regression harness,
            lavapipe-only (QQuickRenderControl + QRhiTextureRenderTarget
            readback under nested kwin_wayland --virtual; fixed-step
            animation driver; three failure gates; two-parameter
            comparator; per-scene tolerance overrides; explicit bless
            flow with gate self-test). Bless OUR goldens in the nix
            environment, pin fonts in scenes, write scenes for our own
            defect history (ShadowedItem padding, colorizer stack,
            monochromatic icons, BadgeEffect, MultiEffect permutations).
            LANDED 2026-07-16 (scripts/sceneprobe-gate.sh; execution
            record in docs/agent-logs/2026-07-16-sceneprobe-transplant.md):
            harness + gate + first scene set (selftests, shadoweditem
            with the measured static-paddingRect probeExpect, six
            MultiEffect permutations, badgeeffect), all text-free so no
            font pinning needed yet; 9 goldens blessed at the strict
            {0,0} tier after a two-run byte-identical determinism check.
            FOLLOW-UP SCENES LANDED 2026-07-16 (session two, agent
            ledger docs/agent-logs/2026-07-16-sceneprobe-followup-scenes.md):
            parabolic_zoom (text-free redesign - production parabolic
            content is icons, and a font file alone cannot pin
            rendering), applet_colorizer (the 1f835402 ColorOverlay
            stack), forced_monochromatic (the 1932db32 sites),
            indicator_glow (GlowPoint + Shapes gradient port). All
            four carry probeExpect invariants, defect-injection
            negative proof, and {0,0}-tier goldens after two-run
            byte-identical checks; 13/13 gate. Same wave: opt-in dgpu
            device mode restored (my direction - works WITH a GPU,
            never requires one; lavapipe stays the only CI tier),
            verified on the real RX 5700 XT. Cross-machine golden
            verification still owed once a second machine exists.
            Follow-up commits: 0ecc2ea2e, 7ea3ed156, b7d02dc11,
            af04acf35, 8d97c1e46.
            Commits: e0d5dcce9, e9f9d7734, 2c3547a18, c5372aba4
            (plus 27668839a, a build-check re-exec fix caught while
            running the gate suite for this item; the worktree merge
            rebased the hashes the agent log records)
      - [x] P2: the Plasma-6 silent-failure contract pins
            (createApplet QRectF, KPluginMetaData path ctor, knsrc
            KPackageStructure, no-ActionPlugins default, internalAction
            handle, Binding.restoreMode zoom collapse) - each verified
            against Qt5 semantics first.
            LANDED 2026-07-16 (verdicts, negative tests and the live
            createApplet bug it caught are in
            docs/agent-logs/2026-07-16-p2-contract-transplants.md;
            one live "Show Alternatives" sanity pass remains in
            docs/reference/manual-flake-removal-testing.md).
            Commits: 5925b167f, 7fc338cd5, 9ee6f7ad5, 22e6bb63d,
            885c1318e, 53839863b, 9f1672434, b21825ed4 (the worktree
            merge rebased the hashes the agent log records)
      - [x] P3: behavioral tests over lattedock-core (screenpool,
            visibility-reveal, abstractlayout, activitiesinfo,
            syncedlaunchers client demotion, lastactivewindow, the
            vendored plasmoid backend pins, data types, settings
            models).
            LANDED 2026-07-16 (session two; per-candidate verdicts,
            negative-test outcomes and the raised-quality record in
            docs/agent-logs/2026-07-16-p3-behavioral-tests.md): all
            ten candidates executed (syncedlaunchers REDESIGNED over
            the real broadcast surface, the rest adopted and
            extended past the fork's cases per the standing quality
            rule), mirror-logic pair skipped, ctest 54 -> 65. The
            premise checks found NINE live upstream-inherited
            defects, each fixed at origin in its own commit:
            screenpool removeScreens stopping at the first absent id
            (501c5b789), layoutName chopping the last char of
            non-layout names (1db82ff50), uninitialized
            Applet.isSelected (9b8964915), three View debug-string
            composition bugs incl. char*+int pointer arithmetic
            (c1e35a72f), negative-row SEGV in the settings table
            models (5828fe9ae), isSidebar() always false from == in
            place of = (29b5cea73), and layouts-controller
            modeIsChanged infinite recursion from a missing arrow
            (207a4ac55).
            Commits: 20124015c..2b6a94243 (22, post-rebase; the
            fixes above plus test/docs commits listed in the ledger)
      - [ ] P3b: the remaining transplant candidates (filed
            2026-07-17; the itemized list with skip verdicts lives in
            the adoption plan doc - ~14 suites over shared headers,
            shortcutstest and storagetest first).
            STATUS 2026-07-17 evening wave: 14 suites adopted
            (shortcuts, storage, universalsettings, layoutmanager,
            importerlogic, wmtools, popupplacement contract from
            coretypesenum, commontools, coretools, generictools,
            panelbackground, configcontrols, schemesmodel, viewmodels
            - ctest 66 -> 80 entries), 2 skipped with reasons in the
            adoption doc (appletremovaltest contradicts our parking
            contract; coretypesenum's EdgePosition is their-tree-only),
            2 DEFERRED (layoutsmodeltest, viewsmodeltest: both need a
            live Latte::Corona, which in this upstream-shaped tree
            means the whole app machinery - session D-Bus, activities,
            layout loading; needs its own harness investigation, the
            fork leans on its DI seams). FOUR inherited defects found
            by the wave and fixed at the origin, each landing before
            the test that pins it: updateView's dead-key maxLength
            write, nameOfConfigFile's Qt6 remove(-1,..) end-wrap chop,
            stringToRect's out-of-range crash on corrupted screen
            records, and the four icon helpers sizing from the raw -1
            thickMargin sentinel. Ledger:
            docs/agent-logs/2026-07-17-p3b-transplants.md.
            Commits: f8bf1444c..b16a730b2 on p3b-transplants
            (17 commits: 13 test + 4 fix; worktree hashes - re-pin
            at merge time if rebased)
      - [x] P4: e2e pixel assertions (latte-imgdiff + KWin ScreenShot2
            before/after under nested kwin, D-Bus-driven, seeded HOME);
            fakepointer keeps covering live pointer-delivery tests.
            GUI-CI-candidate tests already banked: widget-explorer
            drag-add, AlternativesHelper swap, settings-window
            cold-open geometry loop.
            LANDED 2026-07-17 (the e2e promotion unit; execution record
            in docs/agent-logs/2026-07-17-e2e-promotion.md): the nested
            vehicle is the maintained DEFAULT mode of
            scripts/run-e2e.sh (1600x1000 kwin_wayland --virtual, one
            private bus for kwin + dock + probes, throwaway config
            copy, --live escape hatch; kwin bring-up shared with the
            sceneprobe gate via scripts/lib-nested-kwin.sh).
            tests/e2e/lib.sh carries the recipe helpers incl.
            ScreenShot2 raw-over-fd capture; 040 is the first pixel
            assertion (eye-verified golden + latte-imgdiff). Suite at
            promotion: 9/9 recipes green in one vehicle run (smoke,
            EX-15 wheel trio, EX-17 preview text, EX-14 drag-reorder,
            plus the three pre-existing recipes now vehicle-capable).
            Commits: ffd1db8c6..27caf1202 on the e2e-promotion branch
            (11 commits; re-record post-rebase hashes at merge per the
            landing rule)
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
- [ ] Edit mode / primary config view opened UNINVOKED on one
      real-config dock start (2026-07-15 10:52: the log shows
      "#primaryconfigview# initialization started" right at boot with
      no shortcut invoked; closed cleanly via its Close button). Single
      occurrence; the immediately preceding real-config restart that
      same morning did not do this. Suspect persisted config-view or
      edit state left by the prior session, which ended mid-drive on a
      safeguard error. If it recurs: diff the layout/config files for
      edit-state keys before and after a clean close, and find who
      constructs the primaryconfigview at startup.
      Commits:
- [ ] Previews adoption rebuild cost (the one lever left from the
      2026-07-15 hover-lag excavation; everything else landed, see the
      c6eeeb20..d56a26aa series). Adopting a task into the previews
      dialog builds its delegate tree synchronously on the GUI thread:
      measured 100-400ms per switch, 859ms cache-cold, dominated by
      KSvg disk lookups during ToolTipInstance creation (gdb mid-stall:
      SvgItem::componentComplete -> ImageSet::filePath ->
      QStandardPaths::locate). The burst debounce and the two-slot
      cache bound HOW OFTEN it is paid (once per rested icon, free on
      flip-backs); this item is about the cost itself.
      2026-07-15 afternoon pass, strace-measured: one adoption issues
      23,255 stat() calls, 96% ENOENT, ~20k for plasma theme 'colors'
      files - KSvg probes the NONEXISTENT translucent/colors variant
      per Svg instance and negative lookups walk the whole
      XDG_DATA_DIRS every time. LANDED from that pass: 056f7e15 (staged
      runs allow-list XDG_DATA_DIRS - 273 inherited closure entries
      down to 18 deliberate ones; honest note: the adoption syscall
      count barely moved because probe COUNT dominates, but every
      negative walk the process ever does got shorter) and f1edd103
      (delegate cache generalized to a depth-4 LRU with an explicit
      TaskItem-onDestruction eviction contract - each task a hover
      session touches is built exactly once, revisits are free with
      warm streams). ASYNC INCUBATION LANDED (207a8084): each
      ToolTipInstance loads through an asynchronous Loader behind a
      placeholder shell sized by an estimate the first ready instance
      pins exactly - fresh-build GUI stalls measured down from
      100-400ms to 31-65ms, no dialog pulse, drag/hover reaches fixed
      for the wrapped shape. THE PER-RUN ~480ms COMPILE STALL IS ALSO
      GONE (45c15433): staging rm-rf'd and reinstalled the stage every
      restart, refreshing every QML file's mtime, and Qt's disk cache
      validates on timestamps - the dock recompiled its whole QML tree
      per run (239 qmlcache rewrites/restart). Checksum-sync staging
      keeps unchanged files' mtimes: 14 rewrites/restart, first
      adoption 484ms -> 77ms, every staged applet warms at startup.
      REMAINING: only the upstream lever - ksvg lacks negative caching
      in theme file discovery (the translucent/colors probe storm is
      upstream-reproducible; consider filing/patching ksvg). Steady
      state now: adoptions 30-170ms sliced and non-blocking, revisits
      free, sweeps coalesced.
      Commits: 056f7e15 (XDG allow-list), f1edd103 (LRU cache),
      207a8084 (async incubation), 45c15433 (mtime-preserving staging)
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
      tst_compactapplet.qml pins the resolution chain headlessly
      against the real shipped file (implicit tracked live including
      the 0x0-at-swap volume shape, preferred override, minimum
      enforcement) - the qmltest sketched in the resizable-popups
      continuation item, landed ahead of the feature.
      Commits: 437d9a0c, 3b37750b (sizing chain qmltest)
- [x] Applet context menu is MISSING the "Show Alternatives" entry
      (found while live-verifying the AlternativesHelper fix: the
      clock's menu shows Configure/Copy but no Alternatives, so the
      restored helper is unreachable from the UI). RESOLVED 2026-07-15
      in two parts. The entry was never missing: Qt5 gates it on
      isUserConfiguring (contextmenulayerquickitem.cpp:471 matches the
      Qt5 original line for line), so it only shows while the settings
      window is open - the filing above was a normal-mode right-click.
      The REAL defect was one layer down: clicking it did nothing
      because appletalternativesui had no local definition in
      Latte::Package and resolved through the org.kde.plasma.desktop
      fallback to plasma-desktop's Plasma 6 AppletAlternatives.qml,
      which imports org.kde.plasma.shell (registered only inside
      plasmashell's process) and fails to load. Latte now ships its
      own copy (two deviations, header comment in the file) plus the
      package file definition. Verified live end to end on the
      throwaway layout: entry in edit mode, dialog opens
      provides-filtered, picking Digital Clock replaces the Analog
      Clock and persists. latte-dock-ng carries this same latent break
      (helper restored, dialog QML unfixed) - fold candidate for them.
      Commits: 56549d73
- [x] Widget explorer "Get New..." > "Download New Plasma Widgets" did
      nothing (my desk report, 2026-07-15): ng's WidgetExplorer.qml,
      adopted wholesale in 0aa7ffb6, intercepts Get New-labelled
      entries into viewConfig.openGetNewWidgetsDialog() - an invokable
      this port never had (silent TypeError on click). ng only needs
      that detour to launch an external knewstuff-dialog6 around its
      system-Qt Kirigami break. Restored plain model.trigger() and
      deleted the detour machinery; the in-process KNSWidgets::Dialog
      opens fully rendered (verified live), explorer closes via
      upstream's shouldClose.
      Commits: ab3caae1
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
      land. The state machine is now pinned headlessly end to end:
      layoutmanagerparkingtest compiles the real plugin sources and
      drives park/unpark/re-init/prune/direct-finalize plus the loud
      no-container refusal with real applets (itemForApplet) and the
      real askDestroy chain; askdestroysignalorderingtest pins the
      libplasma signal timeline the machine keys on (immediate
      appletRemoved for plain applets, none until object death for
      containment-type, destroyedChanged(true) as the one instant
      signal).
      Commits: 71b0d75a, f269e457 (state machine test), 5f94159c
      (askDestroy ordering contract)
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

- [x] Dock background renders LIGHT/white while the palette is set to
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
      DECISIVE TEST RUN 2026-07-15 (--user-config restart, native
      crops of both surfaces side by side): the real dock's bar
      renders LIGHT and plasmashell's own panel on the same session
      renders LIGHT - they MATCH. Latte resolves the theme exactly
      as plasmashell does; the system's plasma desktop theme has a
      light panel surface while the application scheme is dark, so
      there is NO port defect in theme resolution. CLOSED
      2026-07-15 second pass: the "Dark Colors background arm dead"
      suspicion was ALSO the throwaway kdeglobals gap - on the real
      config the palette darkens the background correctly (proven
      live, palette restored afterwards; see the colorizer item
      below). run-staged.sh now seeds the throwaway config with the
      session kdeglobals so throwaway runs match the real session.
      The route to a permanently dark bar on a light plasma theme
      is the Dark Colors palette (works) or the filed color-picker
      continuation feature.
      Commits: 79ca3360
- [x] Applet colorizer applies too broadly - RESOLVED AS NOT A BUG,
      plus a harness fix. Qt5 source read end to end (Manager.qml
      applyTheme, applet/colorizer/Applet.qml): Qt5 runs the same
      ColorOverlay over EVERY non-blocked applet's whole wrapper
      whenever a non-default palette applies; flattening applet
      icons to a single scheme color IS the Qt5 Dark Colors
      behavior, tasks are exempt through the communicator indexer
      block, and the port matches. The BLACK disc and the
      non-darkening background were the throwaway environment:
      build/_runconfig had no kdeglobals, so themeExtended's
      dark/light variant resolution degraded and applyColor came
      out wrong. Proven live on the real config 2026-07-15: Dark
      Colors darkens the background at the stored opacity, text
      goes light, applet icons flatten to LIGHT silhouettes
      (Qt5-correct), task icons stay full color; palette restored
      to default afterwards. run-staged.sh now seeds the throwaway
      config with the session kdeglobals (copy, never a link).
      Commits: 79ca3360
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
      visibility, upstream-symmetric. Pinned from both sides now:
      tst_compactapplet.qml asserts our release detaches with
      visibility intact (via AppletQuickItem's own bare re-parent
      step) and our adopt re-shows, and representationswitchtest pins
      the upstream half - the inline switch re-parents the cached
      item without ever re-showing it, unwires the expander with
      nulls, and un-switches by detaching to a null parent.
      Commits: 1aa5238c, 3b37750b (release qmltest), 9a1195dc
      (representation-switch contract)
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
- [x] HEADLESS SWEEP 2026-07-15 (second pass): the two class-A
      stranding shapes, swept systematically across every shipped
      QML package. Shape 1, conditional anchors (per-edge/
      orientation ternaries) combined with width/height bindings on
      the same item (the e412889d shape) - every site inventoried
      with a verdict:
      FIXED (same file, same transition triggers as the proven
      strand): plasmoid main.qml barLine (the background band from
      the original live report), belower, shadowsSvgItem - each got
      the reassert remedy, and mouseHandler's existing reassert was
      extended to location changes (a bottom-to-top move rebinds the
      same conditional anchors with no verticalChanged). The
      destroyer is still the open e412889d watch item; the reasserts
      contain blast radius, they are not the root cause fix.
      ITEMIZED, NOT FIXED (same static shape, but the e412889d live
      verification is evidence AGAINST live stranding there: post-fix
      the flipped dock rendered a correctly spaced row, so these
      per-task/per-applet items tracked the flip; also one reassert
      per instance would multiply across every task): BasicItem.qml
      root (declarativeimports), ParabolicItem.qml inner item (which
      also carries upstream's bug-478 zoom-jiggle acknowledging this
      family), SeparatorItem.qml, basicitem/IndicatorLevel.qml,
      containment applet/IndicatorLevel.qml, plasmoid main.qml red
      zoomHelper debug rectangle (debug-only visual). If a flip ever
      strands one of these, apply the same reassert.
      SAFE BY MECHANISM (the suspected takeover needs an OPPOSING
      anchor pair - top+bottom or left+right - to form transiently so
      the anchors system controls the size; sites whose conditional
      anchors can never form one, or that a stronger remedy covers,
      cannot strand this way): canvas Ruler.qml + HeaderSettings.qml
      (center-line ternaries only, and 9aeda562 reloads the whole
      canvas content on retarget - a fresh instantiation cannot
      strand), HeaderSwitch.qml (left/horizontalCenter only, level
      fixed per instance), ComboBoxButton.qml (single side by RTL,
      process-constant), ShortcutBadge.qml both copies (left/top/
      centerIn only - centerInParent DOES flip live at iconSize 48,
      but no opposing pair can form), AnchorChanges-based state
      machines (FrontLayer clickedCenter/arrow) which use the anchors
      system's own transition mechanism rather than ternary rebinds.
      Shape 2, once-sampled geometry (the 8be2b388 shape) - CLEAN
      NEGATIVE: the only C++ site sampling QML-published geometry is
      canvasconfigview.cpp:200, already notify-connected by 8be2b388
      itself; MultiLayered's effects-area and VisibilityManager's
      mask computations are signal-retriggered recompute networks
      (x/y/size/margins/hidden all wired); the remaining imperative
      geometry writes are drag/animation transients that are
      correctly one-shot. One WATCH NOTE, not a defect: the comment
      at primaryconfigview.cpp syncGeometry's zero-size bail claims
      "rootObject's width/height changes (connected in init())" but
      the only size connects are SubConfigView's deferred-show retry
      and updateEffects; the resample network covers mode changes,
      screen changes and the post-show debounce, so no gap was
      provable headlessly - verify the comment against the code when
      next in that file.
      Commits: eca51ae0 (sibling reasserts)
- [ ] Settings-chrome popup windows linger as stuck overlays across
- [x] Settings-chrome popup windows linger as stuck overlays across
      sessions (observed repeatedly 2026-07-15: the Type combo's
      "Dock/Panel" popup survived its parent chrome closing and sat
      over the rearrange toggle EATING CLICKS - three toggle clicks
      in a row landed on the stale popup during headless driving;
      plus 1096x527 and 1096x204 layer-6 windows appearing when the
      chrome opens with Advanced enabled, parked at wrong geometry,
      persisting after close - almost certainly the SECONDARY
      advanced-mode config window, which the Phase 8 multi-monitor
      work note already flags as not covered). One investigation:
      enumerate the chrome's child windows/popups, their lifecycle
      on close/retarget, and their output+geometry handling; the
      d670c97a screen-pinning never covered them.
      ROOT CAUSE (source, headless worktree 2026-07-15): both of
      SubConfigView's deferred-show mechanisms survive close() -
      the showAfter() timer (primary 250ms / secondary 200ms /
      canvas 50ms) and the showWhenSized waiting-for-size flag that
      any later width/height change re-fires. A session closed
      inside those windows had its chrome map AFTERWARDS, outside
      any configuring session: unmanaged, unplaced, top layer,
      focus-taking - exactly the observed overlays. The "Dock/Panel
      popup" reading was a red herring: QQC2 popups on the pinned
      Qt 6.11 are in-scene items (popupType 0, probed offscreen),
      so what lingered were the chrome WINDOWS themselves, the
      secondary type chooser (which sits at the dock's start, over
      the rearrange toggle) among them. Fix: cancelDeferredShow()
      on every deliberate hide path; transient hides deliberately
      keep their deferred re-shows. LIVE VERIFICATION PENDING:
      re-drive the recipes (open chrome with Advanced enabled and
      close within 200ms; switch layouts with chrome open), then
      KWin-dump that no chrome window stays mapped and the
      rearrange toggle takes clicks. The window ENUMERATION half of
      this item (which windows exactly were 1096x527/1096x204) also
      needs the live pass.
      Commits: 08511ffd
- [x] Applet-created dialogs open on the wrong screen (caught live
      2026-07-15 with a screenshot: the comic's full-size viewer -
      its "bigger" zoom button - opened on the portrait DP-3 while
      the dock lives on DP-2; the xkcd image rendered correctly, so
      this is placement only). The dialog is created by the applet's
      own QML, not by our code, so the fix surface is wherever our
      Dialog/window plumbing resolves screens for applet transients
      (visualParent/transientParent forwarding). Compare with
      plasmashell: where does the same button place its window
      there. Phase 8 multi-screen family.
      ROOT CAUSE (source, headless worktree 2026-07-15): the viewer
      is a PlasmaCore.Dialog with flags Qt.Popup and NO visualParent
      (extracted from the pinned comic plugin); libplasma forwards
      screen/transient info only for visualParent'd dialogs (pinned
      libplasma 6.6.5 dialog.cpp), so QWindow::screen() stays the
      PRIMARY screen at the moment the applet's positionFullView
      centers on it, synchronously after show() and before any
      wl_surface.enter - the placement reaches KWin through the
      plasma-shell surface position in primary-output coordinates.
      plasmashell carries the same gap, masked by panels usually
      living on the primary. Fix: Corona app-wide filter forwards
      the hosting view's screen at Show delivery; the view resolves
      through the dialog's QObject parent chain because Qt 6 assigns
      NO transientParent to windows declared inside items (contract
      tests pin both halves - the first fix draft keyed on
      transientParent and the new contract test killed it).
      LIVE VERIFICATION PENDING: open the comic zoom viewer from
      the DP-2 dock and confirm it lands on DP-2 (and centered,
      not top-left cornered). FOLLOW-UP the same session
      (df63fe9e): the first walk read the shadowing
      QWindow::parent() and matched nothing; the resolution now
      lives in Latte::visualHostWindowOf with its own headless
      test (visualhostwindowtest).
      Commits: 377aad57, df63fe9e
- [x] layershellmappingtest extended past the pure mapping functions
      to the apply helpers (headless worktree 2026-07-15): the
      ec5d2316 exclusive-zone rules (docks reserve exactly their
      strut thickness via the setViewStruts flow and release with 0;
      overlay and fixed-position surfaces carry zone -1 and clear
      any stale exclusive edge) and the 793faad2/d670c97a retarget
      cycle (visible cross-screen placement = exactly one hide then
      one show through both applyFixedGeometry and
      applyCanvasPlacement; same-screen and hidden-window paths =
      zero visibility churn). Needed two outputs headlessly: the
      offscreen platform's JSON configfile provides them (custom
      main writes a landscape+portrait topology, initTestCase pins
      it loudly). LayerShellQt stores desired state client-side, so
      every setter reads back without a wayland connection. Two new
      contract tests also landed with the applet-dialog fix
      (appletwindowparentingtest, tst_transientwindowcontracts):
      Qt 6 gives windows declared inside items NO transientParent
      (the Qt 5 magic moved to QQuickWindowQmlImpl) but keeps the
      QObject resource-child parent - both halves keyed on by
      Corona's screen forwarding. All headless-verified green.
      Commits: 87749d93 (layer-shell apply-helper pins), 377aad57
      (parenting contract tests, in the fix commit)
- [x] SWEEP 2026-07-15 (headless, worktree): one pass over every
      silent-mechanical-Qt6-break pattern with a landed exemplar.
      Clean negatives recorded so the next sweep diffs instead of
      re-reading: Array.isArray (a302d742 family) - one live site,
      already fixed, no others, no instanceof Array anywhere; Qt.*
      enums in QML (81f0093e family) - all 35 distinct names in use
      exist in Qt6, Qt.MidButton survives only in comments; int-typed
      pointer/geometry caches (36160c46 family) - the one per-event
      accumulator was already fixed (ConfigOverlay), the remaining
      `property int` caches (TaskItem pressX/Y, EnvironmentActions
      lastPressX/Y, ParabolicEventsArea lastMouse*/lastParabolicPos,
      DragCorner initGlobal*/curGlobal*, AppletItem oldX/Y) are
      threshold-gated or one-shot with bounded sub-1px error and never
      accumulate, judged against the truncation contract pinned in
      98986641; C++ event paths use QPointF end to end (parabolic,
      eventssink) with only drag/drop and wheel positions rounding to
      QPoint, which is harmless there; QVariant::value<QList<...>>
      (33830b2c family) - LayoutManager's order/lockedZoomApplets/
      userBlocksColorizingApplets are real Q_PROPERTY QList<int>, exact
      metatype everywhere, the one element-wise helper is 33830b2c's
      own; QQC2 shadowing (33fa17d7 family) - of all injected context
      properties (indicator, dialog, viewConfig, latteView, plasmoid,
      universalSettings, layoutsManager, themeExtended,
      shortcutsEngine, primaryConfigView, containmentFromView,
      infoWindow, alternativesHelper) only `indicator` collides with a
      QQC2 control property name, and one missed site family was found
      (next item); destroyed() handlers (d6d57e61 family) - all
      remaining C++ handlers remove by identity or touch only captured
      live objects, except containmentDestroyed (item below). The
      settings-dialog teardown saves (headerView sort state, column
      widths read from the dying widget in its own destroyed()
      handler) are inherited upstream idiom that reads the still-live
      QObjectPrivate-based d-pointer, non-virtual calls only - noted,
      not touched.
      Commits: f5ff449b, 2d28cda1, 75780c64, 98986641 (findings also
      itemized below)
- [x] Wayland window ids defeated two numeric validity tests left from
      the WindowId->QByteArray port (8e8cdf31/e9710e95):
      WindowInfoWrap::isChildWindow() tested parentId.toInt()>0,
      always false for a wayland UUID, so a focused dialog never
      resolved to its main window in windowstracker's PASS 2 and
      activeWindowTouching missed the active-group case; and
      setActiveWindowMaximized tested maxWinId.toInt()>0, so
      activeWindowMaximized was constitutionally false on wayland
      (dodge-maximized's reaction and the containment's
      plasma-background-for-maximized binding both sit on it). Both
      now isEmpty() tests, equivalent on X11 where ids are decimal
      strings. windowinfowraptest pins parent-id semantics for both id
      shapes. LIVE VERIFICATION PENDING: a maximized window touching a
      dock must set activeWindowMaximized; a focused dialog of a
      touching window must keep activeWindowTouching.
      Commits: f5ff449b
- [x] Default indicator config: the four style/glow
      PlasmaComponents.Buttons wrote bare `indicator.configuration` in
      onPressedChanged. Qt6 moved `indicator` onto AbstractButton, so
      inside a Button scope the bare name resolves to the control's
      own null glyph item: the write throws, the handler aborts, and
      picking Line/Dots or On Active/All silently did nothing.
      33fa17d7's latteIndicator alias covered only the CheckBox sites
      (those threw at load where verification could see them; buttons
      only throw on press). All four writes now go through
      latteIndicator; the shadowing is pinned by
      test_abstractButtonShadowsOuterIndicatorName. LIVE VERIFICATION
      PENDING: press all four buttons in the indicator config and see
      the setting apply.
      Commits: 2d28cda1
- [x] GenericLayout::containmentDestroyed dereferenced the dying
      containment for the exit-slide edge: containment->location()
      inside its own destroyed() handler reads freed AppletPrivate
      (deleted in ~Applet, destroyed() emitted later from ~QObject).
      Hit on EVERY completed dock removal - the scheduled-destruction
      flow parks the view alive in m_waitingLatteViews until final
      destruction, so the take() succeeds and the deref runs; silent
      because the freed value usually still parses as the old edge.
      Positioner now caches the last known edge (seeded at init,
      follows locationChanged, which libplasma 6.6.5 emits at
      containment attach and on every change - verified in its
      source), and slideLocation() resolves its Floating default from
      the cache, which also removes the same hazard from ~Positioner.
      destroyedtypedemotiontest pins the demotion contract (cast null,
      QPointer null, metaobject demoted, identity survives). LIVE
      VERIFICATION PENDING: deleting a dock still slides it out on its
      own edge.
      Commits: 75780c64, 98986641 (contract test)
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
      inventory live in docs/tracking/session-handoff.md (2026-07-15 sweep
      section). fa02b887's import path was assessed and is NOT
      headless-drivable (Storage::importLayoutFile requires a live
      corona); its guard stays live-verified only.
      Commits: c117e598 cbb37c95 41a6918e d1773423 9abb3d25 dedfc441
      708c6fe4 8468f765 5406a27b 0454ed36 1fdcb2e0
- [x] 'ShaderEffect: Texture t1 is not assigned a valid texture
      provider (QQuickItem*)' during representation churn (observed
      live: an applet crossing switchWidth/switchHeight on hover).
      ROOT-CAUSED 2026-07-15 headlessly from the pinned sources: the
      (QQuickItem*) suffix means the sampler variant holds a NULL item
      (qsgrhishadereffectnode.cpp prints the live source's class name
      otherwise), and libplasma's compactRepresentationCheck NULLS the
      expander's compactRepresentation property on the inline switch
      to full (appletquickitem.cpp:384 at 6.6.5). CompactApplet's
      clicked flash kept sampling that property while running
      (alwaysRunToEnd), warning per material sync for the rest of the
      flash. Gate includes the representation now; visible:false
      removes the node before the next sync. Live re-verification of
      warning disappearance queued for the next live session.
      Commits: 5f8c10be
- [x] Colorizer disengage armed a dead-provider sampler: effect
      SourceProxies decide direct-vs-proxy at polish time and NEVER
      repolish when the source's layer.enabled flips (pinned by the
      new contract tests). ItemWrapper's provider layer drops at fade
      end while the ColorOverlay stayed in the scenegraph at opacity
      0, still preprocessing per repaint against the destroyed layer.
      Colorizer subtree now leaves the scenegraph at opacity 0.
      Commits: 230774d0
- [x] Sibling-copy shadow arrangements surviving in tasks after
      c7c46226 banned the class in the containment: ParabolicItem's
      task shadow, basicitem/ShortcutBadge (twin of the converted
      containment badge), RemoveWindowFromGroupAnimation's fly-away
      ghost (shadow + desaturate siblings, now one layer effect with
      saturation riding ShadowedItem). All layer.effect on
      settings-stable gates now. Visual confirmation (single-struck
      shadows on live task icons) queued for the next live session.
      Commits: b634ef67
- [x] Forced monochromatic icons (side painting) a near no-op since
      the port: both sites used MultiEffect.colorization (gray-level
      multiply) where Qt5 used ColorOverlay - identical mechanism to
      the applet colorizer defect 1f835402. Restored ColorOverlay at
      ParabolicItem and TaskIcon's badge branch; taskIconItem's
      provider layer is held on while the feature is enabled so the
      no-repolish proxy can never be stranded by hover churn. Visual
      confirmation needs forceMonochromaticIcons armed live.
      Commits: 1932db32
- [x] Drag-greying badge effect sampled at opacity 0: missed sibling
      of the 69baabf0 gates - with any badge showing, every badge
      repaint re-grabbed the source for an effect drawing nothing
      (e3376405 idle-cost family). visible: opacity > 0 now.
      Commits: 2c726d4b
- [x] Effect-contract enforcement made executable: qmleffectrules
      ctest greps shipped QML so autoPaddingEnabled can only ever be
      assigned literal false (negative case verified: a flipped
      ShadowedItem fails the scan with file:line), and
      tst_multieffectcontracts.qml pins autoPadding default TRUE,
      per-side paddingRect via itemRect, plain-source proxying, and
      the no-repolish-on-layer-flip gap in both directions. NOTE for
      future testing: the offscreen platform never starts a scenegraph
      render loop (QSG_INFO silent, grabs return blank), so sampler
      warnings and texture content are live-only; polish-level state
      (hasProxySource, itemRect) is the headless observable.
      Commits: 032c3d4d, ba57eb37
- [ ] Window-preview thumbnail shadow is still a sibling-copy
      ShadowedItem sampling previewThumbLoader.item (ToolTipInstance
      .qml:189, from c25cb3e1). Two open questions for a live
      session: kpipewire's PipeWireSourceItem is NOT a texture
      provider (no isTextureProvider override at 6.6.5), so the
      effect's SourceProxy layer-grabs the live video item every
      frame - the arrangement e88af680 refused to create for the
      player-controls mask because layer grabs of the churning
      pipewire item raced teardown; and converting to layer.effect
      would layer the pipewire item directly (same class). It has run
      live since c25cb3e1 without an attributed crash, so decide with
      a live reproduction attempt (preview sweeps + teardown churn
      under gdb) rather than headless guessing; the double-draw is
      invisible on video content.
      Commits:
- [x] QML extraction initiative: execute docs/tracking/QML_EXTRACTION_PLAN.md
      (25 units EX-01..EX-25, each with a full spec, delegation tag,
      Qt5 anchor, and wave assignment; the completeness ledger at the
      top of that file is the live per-unit tracker - tick units
      THERE, tick this item when the backlog is complete or formally
      closed). COMPLETE 2026-07-16: all 25 units executed and merged,
      45 ctest entries with 27 sanitized unit-header pairs, qmllint
      baseline only shrinking; the live-verification tail is recorded
      per unit in the ledger's executed notes, with the still-owed
      recipes named there. Three defects found and fixed at origin
      along the way: the wayland tooltip flash loop (19946c08), the
      dead D-Bus badge path (0faf8e45), and the desktop-name/typo/dead-
      path family the unit agents fixed inside their own commits. Strong-model-window shortlist, in order: Wave 0
      (tests/units scaffolding + coverage ratchet + EX-22 proving
      unit), EX-01 PreviewSwitchEngine, EX-03 ParabolicMathCore,
      EX-02 ParabolicRouter; anything not landed when the window
      closes defers with its do-not-delegate marker, never delegates.
      The remaining 21 delegate-safe units are the post-transition
      backlog (waves 2-4 in the plan's section E).
      Commits: bab18b2c, 2fb1bd27, f017854c, e554bf04, b70ccb55 (the
      plan document; execution commits get recorded per unit in the
      plan's ledger)

### Phase 11: Nix packaging + Docker build verification

- [x] RE-PIN after the 2026-07-16 23:33 system rebuild (INCIDENT, blocked
      all gates - full record in the handoff's 2026-07-17 entry).
      Executed 2026-07-17: the machine had moved AGAIN by session start
      (26.11.20260715/753cc8a, not the incident entry's e7a3ca8 -
      reading the live generation, not the notes, was the right call);
      re-pinned to exactly it (Plasma 6.6.5->6.7.3, Qt 6.11.0->6.11.1),
      fresh both-variant rebuild, full ctest (one contract repin owed
      to libplasma 6.7's askDestroy guard change, its own commit),
      sceneprobe 13/13 PASS against the COMMITTED goldens - the
      expected Mesa-move re-bless proved unnecessary, text-free
      lavapipe scenes render byte-identical on LLVM 21.1.8 (one new
      Qt-RHI validation VUID suppressed, traced to qrhivulkan.cpp:861).
      Nested vehicle verified end-to-end on the fresh pin (dock to
      lifecycleState running, 5 views settled, clean SIGTERM teardown
      trail, all over the private bus). Real dock restarted on the
      fresh build, 2 views settled == the 2 latte containments in the
      on-disk layout. gate-all.sh gained the pin-vs-system lockstep
      guard (exit 4 + re-pin recipe on mismatch, loud skip off-NixOS).
      The upgrade STORY stays Bree's open decision (manual list):
      nixos-upgrade.timer at 04:00 re-creates the drift daily; the
      guard makes it loud, the re-pin flow stays manual.
      Commits: c147fbbdb (re-pin), 24dc3ee39 (askDestroy contract
      repin), 250096280 (VUID suppression), fa2721d2f (lockstep
      guard), 70e1ae9aa (restart-staged packaged-wrapper sweep)
- [x] restart-staged.sh must kill the PACKAGED dock too (found live at
      the re-pin's real-dock restart, first login with
      programs.latte-dock.autostart=true): the module-autostarted
      package is a wrapQtAppsHook wrapper whose process comm is
      ".latte-dock-wra" (kernel 15-char truncation of
      .latte-dock-wrapped), so the pgrep/pkill -x latte-dock sweep
      never matched it, it kept the KDBusService unique name, and the
      staged dock exited silently while busctl answered from the old
      binary. Sweep now covers both comm shapes; verified live twice.
      Commits: 70e1ae9aa

Directly reusable knowledge from this session, not new research -
confirmed transferable today when the same include-path fix was
applied to get latte-dock-qt6 building on Nix during live debugging.
The bare build toolchain moved to Phase 0; this phase is the
polished, distributable form of it.

- [ ] Correct D59 (invalid standalone AppStream identity and stale library
      provider). Latte is a standalone executable, not an addon of Plasma
      Shell. The complete source contract is component type
      `desktop-application`, component ID `org.kde.latte-dock`, no `extends`,
      and a `provides` block containing only binary `latte-dock`; the launchable
      remains desktop ID `org.kde.latte-dock.desktop`. An exact `replaces`
      relationship names the old `org.kde.latte-dock.desktop` component ID
      shipped by upstream tag `v0.10.8` at `28f39d65d`, preserving
      software-center history without keeping it as a live alias. The old ID
      `org.kde.latte-dock.desktop` fails AppStream 1.1.3 with
      `cid-rdns-contains-hyphen` because its suffix makes `latte-dock` a
      non-final reverse-DNS segment. `liblatte2plugin.so` disappeared with the
      complete `liblatte2` tree in commit `507393933` in 2020, and private QML
      plugins are not public library providers.

      Validation must not depend on ECM's vacuous development-state
      `appstreamtest`, which reads only `build/install_manifest.txt`. A direct
      CTest always validates the configured
      `build/app/org.kde.latte-dock.appdata.xml` and asserts every identity,
      launchable, extension, and provider invariant. The installed-package gate
      separately requires package-owned metainfo and structurally enforces the
      same contract, including exact `replaces` contents, without an AppStream
      runtime dependency. The Nix package and development shell declare
      AppStream as a native test tool, and the pure package derivation must build
      with tests enabled. Debian and RPM builders archive current HEAD, so their
      duplicate source patches are removed. The tracked Gentoo and Void recipes
      remain pinned to older source with their patches unchanged until a second
      PR can reference this PR's final GitHub hash. The Void helper is distinct:
      it rewrites the staged recipe to exact current HEAD and must stage exactly
      that `_commit`, no patch, and matching archive metadata. No continuation
      package has been released, so no continuation compatibility alias or
      migration is added; `replaces` covers only the inherited upstream release
      identity.

      Branch implementation is complete at provisional commits `8468e54c6`,
      `34999aa56`, `6eb4406c1`, `a860385ef`, `a42843047`, `480c831aa`,
      `e00f4f6e9`, `2a1eb43d5`, and `5f893e43c`. Keep this item unchecked until
      the PR merges; replace these hashes with the final post-rebase commits and
      then mark it complete.
      Commits: 8468e54c6, 34999aa56, 6eb4406c1, a860385ef, a42843047,
      480c831aa, e00f4f6e9, 2a1eb43d5, 5f893e43c (provisional branch hashes;
      final post-rebase hashes required)

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
- [ ] mpris2 dataengine missing from the packaged build (media badge has
      no backend). Migrated 2026-07-19 from the deprecated human review
      notes (packaging follow-up). The packaged (flake `packages.default`)
      dock logs `Could not find plugin plasma5support/dataengine/
      plasma_engine_mpris2`, and that dataengine is not present anywhere
      in the store, so the tasks media/audio badge and the Spotify
      media-control overlay have NO backend in the packaged build (the
      dev/staged build is unaffected). Identify which kdePackages input
      ships the mpris2 dataengine (or its Plasma 6 replacement API) and add
      it, or wire the plasmoid to the current media API. Enumeration and
      the dock itself are unaffected. Not blocking.
      Commits:
- [ ] cmake icon-cache doubled-path install warning. Migrated 2026-07-19
      from the deprecated human review notes (packaging follow-up). The
      build log shows `cmake -E touch: failed to update <out>//nix/store/
      <out>/share/icons/hicolor` (doubled path). Harmless (it is only the
      icon cache index), but it points at an install rule using an absolute
      path under DESTDIR; worth a look when next touching the icon install.
      Commits:

### Continuation features (beyond upstream parity)

Features beyond even Latte git master, appropriate under the
maintained-continuation framing. None of these start before their
prerequisites in the phases above are done.

- [ ] MULTI-DISTRO CI MATRIX (v0.20.0 release pre-req, planned
      2026-07-17) - take the headless nested-kwin/lavapipe sceneprobe +
      tests/e2e harness to Arch/Fedora/Ubuntu-family Plasma 6 containers
      with per-distro golden validation (graduated rigor: bit-exact on
      the NixOS pin, bless-frozen on stable distros, invariant+tolerance
      on rolling). Green across the matrix gates the v0.20.0 tag (first
      tagged continuation release; upstream stopped at v0.10.8, tree is
      at 0.10.77). Full architecture, phased A-E checklist and open
      decisions in docs/tracking/multi-distro-ci-plan.md. Cross-ARCH (aarch64)
      golden verification (item 9) is PARKED there with its findings -
      the width-knob crashes lavapipe, and a local aarch64 guest needs
      binfmt or a real ARM box; the distro matrix is the higher-value
      portability signal. Commits:
- [x] Resizable applet popups with per-applet persistence and reset
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
      (implicit/preferred/minimum permutations) - LANDED ahead of the
      feature as tests/qml/tst_compactapplet.qml (3b37750b); still
      wanted here: GUI-CI candidates for the microvm: drag-resize
      round trip, persistence across restart, reset entry.
      LANDED 2026-07-15, all live-verified on the throwaway layout
      with fakepointer drags (grow by top-edge drag, persisted keys
      in the layout file, reopen at persisted size, reset shrinks a
      pinned popup live). Two design notes from the live drive:
      the cross-module reset wake-up is an in-process dynamic-property
      bump on the applet, NOT KConfigWatcher - kconfig's DBus change
      notification cannot address layout files with spaces in their
      names ('My Layout.layout.latte'), verified with dbus-monitor;
      and end-of-grab detection is Enter/Hide plus a settle timer,
      because mouse events queued before the compositor grab engages
      arrive late and killed the session instantly. The systemtray
      hosts its OWN libplasma AppletPopup which saves popupWidth on
      every hide (upstream semantics), so the systray always carries
      the keys and always shows the reset entry - upstream-inherited,
      noted in 22aa01d7's body. GUI-CI candidates remain wanted.
      Commits: d12baff2 (core feature), 22aa01d7 (reset menu entry),
      c3026dea (custom-size qmltests)
- [ ] Settle-gated hover chrome (requested 2026-07-15: create the
      expensive hover state only after the cursor has DWELLED on the
      same icon for 50ms; once created it tracks the cursor live
      exactly as today while it stays on that icon). CLARIFIED by
      the requester: the 50ms is dwell WITHIN the icon, not global
      cursor stillness - a stillness detector would delay chrome
      even when the pointer is clearly settled but micro-moving,
      which reads as lag; per-icon dwell is the classic
      tooltip-intent pattern. SCOPE DECIDED: gate the EXPENSIVE
      hover constructions only - brighten-effect layers (created per
      hover-engage since 69baabf0), title tooltips, first
      preview-window creation. The parabolic zoom is EXEMPT: the
      zoom wave following the pointer through a sweep is the
      signature feel, is construction-free, and gating it reads as
      lag. 50ms kept as one named constant. PREREQUISITE per the
      root-cause rules: one profiling pass confirming the reported
      hover lag IS chrome-creation cost (first-frame effect-layer
      allocation is the prime suspect from the documented warning
      bursts) before the gate is built, so it fixes rather than
      masks. Ordered AFTER the resizable-popups feature per the
      2026-07-15 direction.
      PROFILING PASS DONE 2026-07-15 (QSG_RENDER_TIMING on the real
      dock, hover-engage timestamps probed in TaskItem, a slow
      fakepointer sweep plus discrete hovers): the prime suspect is
      CLEARED. 1377 frames, baseline avg 4.1ms; across all 15
      hover-engages (including the first after a fresh restart) the
      worst frame within 200ms of an engage was 8ms - the brighten
      layer + effect nodes cost at most ~4ms once, not perceivable.
      The only real stalls in the run were the previews dialog's
      FIRST show (21ms and 32ms sync-dominated frames on the popup's
      own window), which already sits behind the existing >=150ms
      hoveredTimer, and two startup frames. Per this item's own
      prerequisite the 50ms dwell gate is NOT built: it would mask
      nothing and delay chrome for no measured win. If hover lag is
      still felt at the desk, the next suspects are the previews
      first-show cost (pre-warm or async-create the dialog) and
      compositor swap backpressure (14-18ms swap frames observed
      twice), not chrome creation. Item stays open only as the
      holder of that follow-up direction.
      Commits: none (gate not built by measurement)
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
- [ ] Scrollable group previews: when a grouped task holds more
      windows than the hover preview strip can fit on screen, the
      strip should scroll (wheel and drag) instead of clipping.
      Requested 2026-07-16 while watching a 10-window dolphin group's
      previews overflow. Qt5 Latte clipped the same way, so per the
      Qt5-faithful rule this is a continuation feature, not a bug.
      Likely shape: the ToolTipInstance row becomes a ListView with
      the EX-21 ScrollMath core driving overflow/clamp, mirroring the
      tasks-row scroller.
      Commits:
- [ ] Window-less pinned launcher can vanish from the row after a
      drag-reorder plus restart: reproduced once on the polluted
      throwaway layout (2026-07-16, launchers59 still listed dolphin
      first while the row rendered without it; opening a dolphin
      window materialized the task at the correct stored slot, so the
      slot survived). The layout carries TWO latte tasks applets, so
      isolation is needed before any fix (regression discipline:
      one variable, clean profile). File under Phase 8 launcher
      persistence follow-ups.
      Commits: none (finding, not yet isolated)
- [ ] Replicate Dock: first-class live-mirrored views with user-chosen
      placement, riding the existing ClonedView sync machinery. Full
      design, settled decisions (keying, sync scope, editability,
      lifecycle/detach) and prerequisite tracking:
      docs/reference/dock-replication-design.md. Blocked on the Phase 8
      cloned-view order-sync item and a live screens-group clone
      verification
      Commits:
- [ ] Ship the Latte separator applet in-tree (requested 2026-07-15
      while surveying what the repo actually ships: shell,
      containment, tasks plasmoid and three indicators - NO applets).
      org.kde.latte.separator was never part of latte-dock upstream
      either; it is one of psifidotos's companion plasmoids from a
      separate repo, yet the containment SPECIAL-CASES its plugin id
      today (AppletItem.qml isSeparator + latteStyleApplet arms, the
      export-template applets list, and the parabolic router treats
      isSeparator items as Transparent) - we recognize an applet we
      do not provide. SHAPE (decided 2026-07-15 after weighing
      in-tree vs submodule vs flake input): companion applets live
      in their OWN repos consumed as FLAKE INPUTS, never submodules
      and not in-tree - the dock never compiles their source, only
      stages their built packages, and the flake is this project's
      single pinning mechanism (a submodule is a second one that
      drifts against the lock; in-tree bloats the dock's scope and
      discards the applet's identity). Fork
      github.com/psifidotos/applet-latte-separator (the standalone
      upstream, verified public 2026-07-15) with full history
      preserved and port it there. TWO more sources to diff at
      execution: psifidotos's own GitHub Latte-Dock mirror carries
      an in-tree copy at applets/separator on its master (may have
      diverged from the standalone repo, and is the likely ancestry
      of ng's vendored copy), and latte-dock-ng's Qt6 port at
      v1.0.14 with their own fixes (6cb0c7e51 separator line length
      matched to app-icon separators, 21ee748c0 task-widget boundary
      stabilization) - fold per the fork-sync judgment rules, read
      individually, take what is correct. The applet repo
      carries its own thin gates (compile check + qmllint over its
      QML, the step-2.5 law from birth); THIS repo's work is the
      integration: the flake input, staging the applet package for
      the throwaway, and live verification. Done means: pinned
      input, applet drops into a dock via the widget explorer,
      isSeparator recognition and parabolic transparency verified
      live (glide across it), and the sibling spacer applet
      (org.kde.latte.spacer, upstream
      github.com/psifidotos/applet-latte-spacer, also id-recognized)
      gets an explicit same-shape-or-reject decision recorded here.
      This item establishes the applet-repo + flake-input + staging
      pipeline on a trivially small applet; the appmenu port below
      reuses it.
      Commits:
- [ ] Full port of applet-window-appmenu (decided 2026-07-15): ship
      a Latte-maintained global menu applet so we can support
      features Plasma's org.kde.plasma.appmenu does not or will not
      (upstream github.com/psifidotos/applet-window-appmenu, Qt5,
      dormant - the feature-rich one: per-window menu display modes
      and window-buttons-family integration that stock appmenu never
      grew; clone it as reference material next to the other
      reference checkouts when the spike starts). This is a REAL
      PORT, not a fold: the Qt5 applet leans on private appmenu
      plumbing, so the port stands on its own research spike first -
      (a) read the Qt5 source in full and inventory every private
      dependency (libdbusmenuqt, the appmenu KDED registrar
      protocol, KWindowSystem X11 winId paths - the wayland arm
      matters most here, same primary/best-effort split as Phase 4);
      (b) read Plasma 6's own org.kde.plasma.appmenu + kded appmenu
      module at the pin to learn the CURRENT registrar/dbusmenu
      surface (that pair is the living Qt6 reference for the
      protocol, the same way libplasma 6.6.5 was for popups);
      (c) decide vendor-vs-depend for dbusmenu parsing the way
      docs/reference/taskmanager-integration-research.md decided the tasks
      backend (record the decision the same way);
      (d) only then the applet port itself, IN ITS OWN REPO: fork
      github.com/psifidotos/applet-window-appmenu to whimbree with
      full history preserved (the same founding move as this repo),
      port there in cutover-sized commits with the step-2.5 law
      applied from birth (strict QML, typed cores for any extracted
      logic, asserts, its own thin compile+qmllint gates),
      Qt5-faithful to the ORIGINAL applet's behavior per the
      standing rule (its config options are the spec). The applet
      stays independently useful in plain plasmashell panels - that
      is half its audience and a design constraint, not an accident.
      THIS repo consumes it as a FLAKE INPUT (never a submodule:
      the dock only stages the built package, and the flake is the
      single pinning mechanism) and tracks here the integration
      work: input + staging + live verification in a dock.
      PREREQUISITES: the separator item above lands first (it
      proves the applet-repo + flake-input + staging pipeline on a
      trivially small applet); interim, verify stock
      org.kde.plasma.appmenu hosts correctly in a Latte dock (live
      check, currently UNVERIFIED - needs the appmenu KDED module
      running) so users have a working global menu while the port
      cooks and we have a behavior baseline to compare against.
      Commits:
- [ ] Lattecotta package rename (DELIBERATELY DEFERRED, decided
      2026-07-16 with the rebrand): eventually rename packages,
      binaries, installed files, icon names, QML import URIs and
      D-Bus names from latte / org.kde.latte to Lattecotta
      equivalents, so multiple latte-dock forks can be installed
      side by side without file conflicts - that side-by-side
      story is the whole point, not vanity naming. The 2026-07-16
      rebrand (repo URL, README, app icon artwork, CLAUDE.md)
      intentionally did NOT touch any of these: this is a
      big-blast-radius rename and doing it casually would break
      running installs, so it gets its own planned pass. Blast
      radius to design for before starting: config migration for
      existing installs (~/.config/latte, lattedockrc, the
      *.layout.latte files and the kwinrc window rules that name
      the binary); the third-party indicator API (indicators
      declare X-Latte-* metadata and import org.kde.latte.*, so
      published indicators break without a compat shim or a
      declared API break); every QML import URI we ship
      (org.kde.latte.core/components/private.*) plus the
      QML2_IMPORT_PATH staging scripts and the Nix
      module/overlay attribute names that mirror them; and the
      org.kde.lattedock D-Bus service name plus its .desktop
      autostart entry (anything that busctl-calls or
      DBusActivates the dock breaks, including our own scripts).
      Done means: new names installed, a migration path for my
      real config verified live, the staging/verification
      scripts updated in the same series, and a recorded
      decision per surface on shim-vs-clean-break.
      Icon decision (2026-07-16, FINAL): Varlesh's original icon
      set and logo stay permanently, through and beyond this
      rename. Three commissioned replacement rounds did not beat
      it, it is GPL and credited, and Lattecotta still starts
      with that L. This rename item covers names only, never the
      artwork. Three rounds of replacement candidates were
      commissioned and none beat it (they live in the
      logo-candidates*/ working dirs and the design agents'
      worktree branches, kept as archive, not as seed material).
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
      docs/reference/taskmanager-integration-research.md; the evidence is that
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

For the LIVE current status and the ranked high-priority list, read
"## Where we are (2026-07-17)" at the top of this file; the per-item
detail is in the phase sections. This section keeps the phase-by-phase
architectural summaries.

Position as of 2026-07-17: 183 of 233 checklist items ticked. Phases
0-7 are substantially done, Phase 4 is WAYLAND-ONLY (X11 backend
removed), Phase 11 (Nix packaging) shipped early, and the Phase 10 QML
extraction initiative is COMPLETE. Session three landed the
accessibility quartet's code half (keyboard focus mode, Accessible.*
rollout, the maintained e2e nested-vehicle suite), the Phase 9
color-group audit, the CaptSilver P3b transplants, and the Phase 8
bottom-dock surface-drift fix. Phase 8 is the open phase
(shutdown/latency/lock-unlock/stranding). Phase 12 (upstream prep)
stays parked under the maintained-continuation framing. The dock runs
on the live Plasma 6 Wayland session against my real config.

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
down; layershellmappingtest pins the corrected mapping). Since
2026-07-17 this is the ONLY window-system backend - the X11 backend
was removed and masked horizontal docks now anchor both length edges
so the compositor cannot re-centre them (the Phase 8 surface-drift
fix). The KWin D-Bus activation/peek item folded into Phase 6 where
its only invocation site (the tasks backend) exists.

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

The per-phase verification cadence still applies: each phase's
subsystem gets driven in the live session before its last box is
ticked, and from 2026-07-17 each feature lands as its own PR with an
independent review. The open work is the Phase 8 code threads plus
the desk-hands and continuation items in "Where we are" and the
manual list.
