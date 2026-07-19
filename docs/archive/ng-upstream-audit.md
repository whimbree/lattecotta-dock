# Upstream ng commit audit

**Relationship (important):** our port is an independent Plasma 6/Qt6 port
forked from **latte-dock proper** (mvourlakos' original); every P6 change here
was driven by us. ng is a *separate* independent port of the same original, of
**uncertain quality** ("was it all done right?" is open). ng is NOT upstream to
us and we did not fork from it. So this is not a catch-up/gap audit — a change
being absent here is not a deficiency. The job is to mine ng's commits for
genuinely good fixes worth adopting into our port, judging each on its own
merit and checking whether ng actually got it right before taking it.

**Upstream at audit time:** `origin/main` @ `e94a9ce95` (v1.2.24, 2026-07-05).
439 commits since the 2026-05-03 history reset ("Latte Dock NG maintained by
Ruizhi"). 249 are substantive (fix / feat / build / ui); test-only, release,
chore, docs, ci and pure refactor are excluded per the agreed scope.

**Verdicts (merit-based, not gap-based):**
- **HAVE** — our port already achieves this (independently); nothing to do.
- **ADOPT** — ng's change is a genuine improvement/fix we should take (and looks correct).
- **SKIP** — not worth taking: an ng-specific design choice, superseded by our
  approach, irrelevant to us, or where ng's fix looks wrong/dubious.
- **N/A** — test-only / infra / does not apply.
- **CHECK** — evaluate on merit (and verify ng got it right) before deciding.
- **GAP** — a whole feature we don't have (bigger than a single fix; e.g. separator widgets).

Earlier rows use "PORT"; read it as "ADOPT". A verdict is a judgement on whether
the *idea* is worth adopting, not whether we are "missing" it.

Our port paths mirror ng's (`plasmoid/package/contents/ui/...`, `app/view/...`),
so "HAVE" means the specific fix is present in our file, not just that the file
exists. Where our port deliberately took latte-dock-qt6's QML instead of ng's,
that is called out.

**Progress: 249 / 249 audited — COMPLETE.**

Rough tally across all 249: ~95 CHECK · ~30 SKIP · ~33 HAVE · ~23 ADOPT-candidate
· ~24 N/A · 4 GAP. Most commits are ng-did-it-differently (expected). The value is
the small set of real bugs we plausibly share, listed below.

### Top ADOPT — confirmed or high-confidence bugs in our port

These are ranked; the top two are the ones to act on first.

- **A. Middle-click is dead (`Qt.MidButton` undefined in Qt6)** — `613ddcc3b`,
  `871c4322d`. **Confirmed**, not speculative: `Qt.MidButton` was removed in Qt6
  QML, so every `mouse.button === Qt.MidButton` in our port is always false. Task
  middle-click actions (`TaskMouseArea.qml:19/155/188`), the middle-click click
  animation (`ClickedAnimation.qml:35`), and the EnvironmentActions middle path
  (`44/53`) are all inert. This **overturns our deliberate-retention note**
  (`PORTING_PLAN.md:636-643`). ng's fix: rename to `Qt.MiddleButton` where QML must
  handle it, but make `EnvironmentActions` accept **only** `Qt.LeftButton` so
  empty-area middle-click propagates to the C++ `ContextMenuLayerQuickItem` handler
  (this is what avoids the double-handling the plan feared). Also flips the
  `middleClickAction` default to Close. **Fix and update PORTING_PLAN and known-defects.**
- **B. ~~Duplicate widget creation from the Widget Explorer~~ (`1b07df291`) —
  VERIFIED N/A 2026-07-08.** Does not apply: our `View::event()` Drop case only
  forwards (no `handlePlasmoidDrop`), so the QML `onDrop`→`processMimeData` is the
  sole creation path. If a double-add is ever seen, chase the explorer
  double-click debounce (`c70988a3f`) instead.
- **C. Shutdown/exit crash on the duplicate-instance path — FIXED 2026-07-08
  (commit d45c7a38).** The real cause was ordering, not `qGuiApp->exit()`:
  `main.cpp` built the `Corona` (and its KSvg singletons) *before* the
  `KDBusService(Unique)` guard, so a duplicate launch built the theme stack then
  crashed tearing a static `KSvg::Svg` down at exit. Fixed by claiming
  uniqueness first and `return 0`ing before the Corona when not the owner.
  (ng's `2437a92ad`/`a9c200fe2` were the pointers; our fix is our own, matched to
  our startup structure.)
- **D. inNormalState binding loop** (`73d982f0b`) — **reproduced in our log**
  (`Binding loop detected for inNormalState`, VisibilityManager.qml:32); imperative recompute.
- **E. Reorder jitter** (`c4e7bcb62` tasks + `924a8ac41`/`cf6aa1ec0` applets) —
  journal-flagged. We have no drag-reorder hysteresis; ng adds a 12px Manhattan
  dead-zone + higher resistance. Pairs with `84e7c9d10` and the parabolic-jitter
  debounce (`c917f7936`/`a118b91dc`, our `parabolic.cpp` has no switch-interval guard).
- **F. Task icons don't refresh on icon-theme change** (`ef2989ec2`) — GAP: no
  `forceRefreshTaskIconSource()` / `QPixmapCache::clear()` in our port; icons go
  stale until restart.

### Also worth taking (lower risk / smaller)
- Widget-explorer double-click add debounce (`c70988a3f`); audio-badge stuck
  highlight (`2d130fed6`); indicator panel-contrast theming
  (`adde24b14`/`f559f521b`/`74a2f9ef2`); context-menu More Places null-guard
  (`af6a90767`); EnvironmentActions null-tracker guard (`3a1aeaf53`); indicator
  user-package override (`7ce95f470`); `KDE_COLOR_SCHEME_PATH` pin (`9fe135422`);
  dbus `setWatchMode` (`603a9871c`); `grabToImage` targetSize (`cbb2f8199`);
  position-aware drop insertion (`735525810`) + text-heavy applet sizing
  (`544479586`); `LatteCore.Dialog`→`AppletPopup` (`29a515f59`).

### Needs a live check (ng bled here; our simpler code is unverified)
- **Edit-mode detection + middle-button-in-edit-mode**: ng spent ~24 commits on
  2026-07-03 (the two edit-mode clusters) fighting reliable `userConfiguring`
  detection and a QML input-blocking overlay. We use the simple direct binding
  (`Plasmoid.userConfiguring`) and a C++ input region — deliberately NOT ng's
  polling stack — but the sheer thrash is a strong signal to verify our edit-mode
  entry/exit and drag-to-reorder actually work live (`618ed8f50` cluster,
  `565ffed9e` middle-button cluster, `b12cce721`/`480a3ba84` detection).
- **Containment→plasmoid wheel bridge for audio-badge volume** (`b6693e7c4`) — the
  Phase 8 item deferred as live-only (docs/reference/live-only.md); ng's is the
  reference implementation.
- **Tasks config tab actually applying** (`eabf7c89a`/`ed0afd054`/`2b0963186`/
  `94f87ba66`) — dual `KConfigPropertyMap` loaders make writes invisible; verify
  our exposed Tasks tab takes effect, or hide it like `9faccabda`.
- **Parabolic wave smoothness** (`0deca9e18`) — always-visible synchronous MouseArea;
  we look close already.

### Feature GAPs (whole features we lack)
- **Separator widgets** (`org.kde.latte.separator`) — no `separator/` package.
- **KNS download** (`knscompat`) — "Get New Widgets/Indicators" from store.kde.org.
- **Auto-pin on drag into launcher zone** (`8410b0400`/`4386dfd9d`) — drag a running
  task into the pinned area to pin it; our qt6-derived drag lacks it.
- **Live task-icon refresh on icon-theme change** (`ef2989ec2`, see F above).

### HAVE-by-convergence (nice confirmations)
Several ng fixes we independently already carry: the LayerShellQt `setScreen` CMake
guard (`1f96a06b0`), the clean cancellable-shutdown lambda (`2d5d4408e`), all the
Qt5→Qt6 handler-binding conversions (`fe4e1154b`/`80bb966c8`/`319232fc4`/`a34009b93`/…),
`itemForApplet()` for P6 applet resolution, and `MultiEffect`/`internalAction()`.

### Recurring theme worth noting
ng **twice removed** view/surface recreate paths (`1b424bae9`, `dc5fa3b0c`) and a
redundant `ls->setScreen` (`911dd1a59`) — corroborates our own decision to drop
the hotplug surface-recreate. Our port still calls `recreateView`; worth auditing.

## Audit log

<!-- Append batches below. Format per row:
| short-hash | date | subject | VERDICT | note | -->

| commit | date | subject | verdict | note |
|--------|------|---------|---------|------|
| 990987d93 | 2026-05-03 | feat(ui): app-name hover tooltip fallback | PORT? | Our `task/TaskItem.qml:46` is still ng's exact pre-fix line `isWindow ? model.display : model.AppName`; missing the AppName-first fallback + `thinTooltipActive`/`fallbackTooltipText`. Optional dock-style UX (show app name, not window title). |
| af6a90767 | 2026-05-03 | fix(contextmenu): harden More Places callback | PORT | Our `ContextMenu.qml:320` still has ng's pre-fix inline `backend.showAllPlaces.connect(...)` unguarded. ng moved it to a guarded `Connections` property (prevents null deref / double-connect). Minor robustness. |
| 2d130fed6 | 2026-05-03 | fix(audio): prevent stuck mute badge highlight | PORT | Our `task/AudioStream.qml` still uses the old `audioBadgeMouseArea` MouseArea; ng migrated to `HoverHandler`+`TapHandler` to fix a stuck mute-badge highlight. Real bug fix. |
| 5f0245539 | 2026-05-04 | fix(tasks): restore hover highlight, clear stuck highlight after click | CHECK | Our port lacks `visualContainsMouse` / `clearParabolicFromExternalPosition` entirely — different parabolic-hover impl (qt6-derived). Verify live whether our tasks show the stuck-highlight-after-click bug this fixes. |
| adde24b14 | 2026-05-04 | fix(indicators): panel-contrast task state colors | PORT | Indicator theming cluster (with f559f521b, 74a2f9ef2). Our port has no `textColorSafe`/`backgroundColorSafe`/`panelPalette`/`oppositeToBackgroundColor` anywhere. Indicators likely use wrong contrast on custom panel colors. |
| 8410b0400 | 2026-05-04 | tasks: allow non-pinned into pinned area via auto-pin | CHECK | `schedulePromoteToLauncherAndMove`/`pendingPinnedSource` absent. Drag feature (promote a running task to a launcher by dragging into the pinned zone). Our drag is qt6-derived; confirm the behavior exists another way or is missing. |
| f559f521b | 2026-05-05 | fix(indicators/default): Header color set for contrast | PORT | Part of the indicator-theming cluster above; same absence. |
| cb89df82a | 2026-05-05 | fix(communicator): detect plasmoid root for P6 bridge | CHECK | `appletDefaultRootItem` bridge-attachment logic absent from our communicator QML. Verify our Latte↔applet bridge attaches correctly on Plasma 6 (indicators/abilities depend on it). |
| 74a2f9ef2 | 2026-05-05 | fix(indicators): Latte panel palette for contrast | PORT | Indicator-theming cluster; adds `panelPalette` through colorizer/MyView/LatteBridge. Absent in our port. |
| 7ce95f470 | 2026-05-05 | fix(indicator/factory): user-local packages override system | PORT | `app/indicator/factory.cpp` has no user-local-override precedence. Small, self-contained C++ fix; lets a user's own indicator package win over a system one. |
| 133043754 | 2026-05-05 | fix(tasks): keep icon visible when only audio badge shown | CHECK | No matching markers in our `task/TaskIcon.qml`. Verify the icon doesn't vanish when a task shows only its audio badge. |
| b0fc8c5f2 | 2026-05-05 | fix(communicator): avoid binding loop in appletDefaultRootItem | CHECK | Follow-up to cb89df82a; same communicator bridge our port doesn't have this form of. |
| 32fcb6886 | 2026-05-05 | fix(plasmoid): use Plasmoid.internalAction() for P6 | HAVE | Present: `main.qml:254` uses `Plasmoid.internalAction("configure")`. |
| 66fe4899e | 2026-05-05 | fix(tasks): badge masking Qt5 ShaderEffect → Qt6 MultiEffect | HAVE | Our `task/TaskIcon.qml` already uses `MultiEffect`; we ported to Qt6 effects independently. |
| f3fd517bc | 2026-05-05 | build: install fallback org.kde.plasma.private.taskmanager | CHECK | ng ships a `compat/qml` fallback for private taskmanager QML that some P6 packagings lack. We have no `compat/qml`; our nix deps (plasma-desktop) provide the module and our packaged dock showed no such error. Adopt only if a target distro lacks it. |
| b27fdbf30 | 2026-05-06 | fix(communicator): break binding loop on appletContainsLatteBridge | CHECK | Communicator-bridge cluster (cb89df82a/b0fc8c5f2). Our port doesn't use this bridge form; verify our Latte↔applet bridge has no analogous loop. |
| d71a73634 | 2026-05-06 | fix(tasks): drag-over parabolic wave behaves like hover | CHECK | Parabolic-drag cluster (needs `clearParabolicFromExternalPosition`, which we lack). Verify our drag-over zoom feels right; our parabolic impl is qt6-derived. |
| cc62a0955 | 2026-05-06 | fix(tooltip): PipeWire thumbnail on Wayland, avoid null-texture crash | HAVE~ | We have `previews/PipeWireThumbnail.5.2x.qml` and load them on Wayland. Verify our version has the null-texture/ready guard, but the mechanism is present. |
| d89f8cf1e | 2026-05-06 | fix(tooltip): suppress broken Wayland thumbnail loader | HAVE~ | Same pipewire thumbnail path exists in our previews. CHECK the specific "suppress broken loader" guard. |
| 73bc81b4e | 2026-05-06 | fix(tasks): grouped-task left click reliably activates a window | CHECK | `isActivatableChild` absent. Grouped-click reliability is exactly the kind of thing to verify live (our grouped tasks enumerate — does left-click pick a window reliably?). Candidate ADOPT if we have the bug. |
| 9bde530e3 | 2026-05-07 | fix(viewsdialog): widen dropdowns, align button text | CHECK | Settings dialog cosmetic (C++ `custommenuitemwidget` sizeHint). Low priority; verify our views dialog dropdowns aren't clipped. |
| d6cbc7167 | 2026-05-07 | fix(view): null-guard ContainmentInterface::setLayoutManager | CHECK | Our `containmentinterface.cpp:557` has the equal-return guard; confirm it also tolerates a null manager like ng's added guard. Tiny, cheap ADOPT if not. |
| 6f9a65579 | 2026-05-07 | fix(backgroundcache): wallpaper fallback for unconfigured screens | CHECK | Minor C++ resilience (use a known wallpaper when a screen has no config). Low priority. |
| 9fe135422 | 2026-05-07 | fix(view): pin KDE_COLOR_SCHEME_PATH on view windows | ADOPT? | Absent in our `app/`. Small C++; makes the dock honor the active color scheme reliably. Cheap and safe — verify our dock always picks up scheme changes, adopt if not. |
| b755f3694 | 2026-05-07 | fix(qml): port widget explorer + edit-dock dialog to P6 | CHECK | Edit-mode/explorer cluster (the subsystem I just fixed the positioning of). Our `components/` are only partly ported (Slider has KSvg, ItemDelegate doesn't). Worth a pass alongside the explorer work. |
| 1c96f2b4c | 2026-05-07 | fix(containment): defer applet insertion until graphic object ready | CHECK | Widget-add reliability (retry-timer until the applet's graphic object exists). Our widget-instantiation test loaded applets 2/3/4 fine, so our path may already be safe — verify, esp. the drag-drop add. |
| 3f0ced11a | 2026-05-07 | fix(audiobadge): fixed text colour for cross-view consistency | SKIP | Minor cosmetic; ties into the audio-badge theming cluster we handle differently. |
| 010269d4d | 2026-05-09 | fix: stabilize dock geometry + settings UI across edge changes | HAVE~ | Overlaps my config-view work: our `primaryconfigview.cpp:351` already has the `size.isEmpty()` bail + statusChanged re-sync. ng also hardened `subconfigview.cpp` — confirm ours has the equivalent there. |
| d811ae6ac | 2026-05-09 | fix: stabilize indicator config theming | CHECK | Indicator-config cluster (Switch.qml SystemPalette, config.qml). Tied to the indicator-theming ADOPTs above; evaluate together. |
| 546ec39d0 | 2026-05-09 | fix(tasks): remove invalid MultiEffect anchoring in group-remove anim | CHECK | Our `RemoveWindowFromGroupAnimation.qml:97` has a `MultiEffect`; check it doesn't carry the invalid anchoring ng removed (would log warnings / misrender the shadow). |
| 523f64bdd | 2026-05-10 | fix(containment): stabilize external widget applets | CHECK | Directly relevant to hosting non-Latte widgets (the clock I test-added). Adds `isExternalPlasmaApplet` detection + z-order. Verify external widgets render and layer correctly in our dock. |
| b75d4a37c | 2026-05-10 | feat: sort widget applets around tasks (drag) | CHECK | `isSortDragging`/`appletSortDragHandler` absent. Drag-to-reorder widgets among tasks. Our drag is qt6-derived; verify reorder works, else consider. |
| a877f7cae | 2026-05-11 | feat: refine dock behavior and widget interactions | CHECK | Broad 170-line change across `lattecorona.cpp`/`genericlayout.cpp`/`syncedlaunchers.cpp`. Too coarse to verdict from the diff; revisit if a specific behavior is off. |
| 603a9871c | 2026-05-11 | fix(dbus): silence deprecated serviceOwnerChanged watcher | ADOPT? | Our `abstractwindowinterface.cpp` builds `QDBusServiceWatcher`s but doesn't `setWatchMode(...)`. ng sets it explicitly to kill a deprecation warning. One-line, safe. |
| 46eee1df2 | 2026-05-12 | feat: add modern dock style | SKIP | New optional style + dual `lib/qml`+`lib/qt6/qml` install. Style is opt-in; the dual-install trick is minor (CHECK only if our QML gets shadowed by stale user installs). |
| 21ee748c0 | 2026-05-14 | fix(separators): stabilize task widget boundaries | CHECK | Large (`containmentinterface.cpp` +804, new `separator/` subdir, `isSeparatorPluginId`). Separator-widget support absent in our `app/`. Verify separators work in our dock. |
| 7c23cd64c | 2026-05-17 | fix: unify tooltip contrast to QQC2.ToolTip w/ palette | CHECK | Config-UI tooltip contrast (`safeTextColor`/`safeBackgroundColor`). Theming cluster; absent. Cosmetic but easy. |
| 54a652e1c | 2026-05-17 | feat: transparent edit mode w/ applet hover tooltips | CHECK | Edit-mode UX in `containment/…/main.qml` (`hoverTooltip`). We have `containment/` but not this. Evaluate with the edit-mode work. |
| f5f037893 | 2026-05-17 | fix: transparent paddings/opacity in edit mode | CHECK | Edit-mode visual polish; pairs with 54a652e1c. |
| 1b424bae9 | 2026-05-17 | fix: revert C++ forceWaylandSurfaceRefresh, fix brace | N/A | An ng *revert* — they tried a forced Wayland surface refresh and backed it out. Notable: same shape as the hotplug surface-recreate we just dropped; corroborates that path is fiddly. |
| 069cf81cf | 2026-05-22 | fix: widget right-click shows applet actions (alt/remove) | CHECK | Edit-mode: right-clicking a widget should offer Alternatives/Remove. C++ `findApplet` walk. Verify our widget context menu offers these (ties to your reported menu issues). |
| 172a36de0 | 2026-05-22 | fix: auto-detect parallel build jobs | N/A | ng's `install.sh`; irrelevant to our nix build. |
| e51685b6c | 2026-05-22 | fix: correct Arch package name plasma-framework→libplasma | N/A | ng's distro INSTALLATION doc. |
| 5c86637c5 | 2026-05-23 | fix: cross-distro build compatibility | N/A | Mostly INSTALLATION.md + a `find_package(KWayland)`; distro-specific, our nix build is unaffected. |
| 28bcf991d | 2026-05-23 | fix: cleanup + restore wheel task cycling | CHECK | `activateWheelTask` absent, but we have a `taskScrollAction` config (ScrollTasks default). Verify scroll-over-task cycling works in our dock. |
| 1df8d5e32 | 2026-05-23 | fix: editBackgroundOpacity only in edit mode | SKIP | Part of ng's edit-mode opacity redesign (below). |
| 61dcbcfe1 | 2026-05-24 | fix: simplify editBackgroundOpacity binding | SKIP | ng opacity-redesign cluster. |
| 984319fb1 | 2026-05-24 | fix: replace edit canvas grid with dock panel opacity control | SKIP | ng **design choice**: they dropped the edit-mode grid for a panel-opacity control. Not a bug fix; our port keeps the original edit canvas. Adopt only if we want ng's UX. |
| 9d798ea04 | 2026-05-24 | fix: blur behavior for panel transparency control | SKIP | Same opacity-redesign cluster. |
| 015b523e7 | 2026-05-27 | fix: cleanup config options, restore ReverseThemeColors | SKIP | ng-specific config surface cleanup; not applicable to our config set. |
| 64253c628 | 2026-05-27 | fix: unify panel colors across original and cloned views | HAVE | We have `emitContainmentConfigProperties`/`initializationCompleted` in `clonedview.cpp`/`containmentinterface.cpp` — same cloned-view color unification. |
| 0b2090c56 | 2026-05-27 | fix: audio badge follows panel contrast (not hardcoded dark) | CHECK | Audio-badge theming cluster; our `AudioStream.qml` lacks the `Kirigami.Theme.colorSet`/`coloredView` contrast. Evaluate with the indicator/audio theming ADOPTs. |
| 4fef2af1f | 2026-05-27 | fix: Layout Custom Colors loads configured scheme file | CHECK | `centrallayout.cpp` scheme-file load. Verify our "Layout" theme-colors mode actually loads a custom scheme. |
| 947bb2b66 | 2026-05-27 | fix: resolve three pre-existing QML reference warnings | CHECK | Small QML null/undefined guards. Cheap ADOPT if our QML logs the same warnings. |
| 6cb0c7e51 | 2026-05-29 | fix: separator line length matches app icon separators | GAP | **Our port has no `separator/` package at all** — the whole separator-widget feature (org.kde.latte.separator) is absent. This + 21ee748c0/d17b93871/770631543 are moot until we decide whether to port separators. Original Latte had them. |
| d17b93871 | 2026-05-29 | fix: separator plasmoid container sizing | GAP | Separator feature — absent (see above). |
| 770631543 | 2026-05-29 | fix: separator line gap 2px→4px classic | GAP | Separator feature — absent. |
| a5552d16b | 2026-05-29 | fix: symmetric thickness margin modern mode | SKIP | ng modern-dock-style tuning; not our style. |
| b17524722 | 2026-05-29 | fix: bump modern mode min thickness margin | SKIP | Same modern-style tuning. |
| f6cf174c7 | 2026-05-30 | fix: remove dead Plasma indicator tab, fix Install actions | SKIP | ng-specific indicator KNS/store install action + removing their Plasma indicator metadata. |
| 6e598bd01 | 2026-05-30 | fix: visibility mode bugs, UI checked state, sidebar | CHECK | We have the visibility infra (`setIsBelowLayer`/`isSidebar` in `visibilitymanager.cpp`) but not ng's `m_originalMode` edit-mode handling. Verify edit-mode & sidebar visibility behave (ties to the DodgeActive/AlwaysVisible work). |
| d769239fe | 2026-05-30 | ui: persistent checked highlight for config buttons | SKIP | Config-dialog button styling; cosmetic. |
| dc5fa3b0c | 2026-05-30 | fix: remove unnecessary recreateView + debug log | CHECK | ng removed a `recreateView`; **our port still calls `recreateView`** (view.cpp/primaryconfigview/genericlayout). 3rd signal (with 1b424bae9) that view-recreate on Wayland is trouble — worth auditing whether our recreateView paths are needed/safe. |
| 315057933 | 2026-05-30 | fix: config persistence via missing sync() + visibility not overwritten | ADOPT? | **Our settings dialogs have no explicit `.sync()`** (settingsdialog/layoutscontroller/detailsdialog). ng added them to fix lost config. Verify our settings persist across restart; cheap ADOPT if not. |
| 0b8e35460 | 2026-05-31 | feat: GitHub release workflow, CPack RPM/DEB/pacman | N/A | ng CI/packaging; our nix build is separate. |
| 8631fcf69 | 2026-05-31 | fix: scrollbar for Edit Dock advanced settings | SKIP | Config-dialog UI convenience. |
| 542b717b9 | 2026-05-31 | feat: lower Plasma min to 6.5.0, Mageia support | HAVE | Our `CMakeLists.txt:18` already sets `PLASMA_MIN_VERSION 6.5.0`; Mageia N/A. |
| cbb4eb9f3 | 2026-06-01 | fix: panel-resize ghosting/jagged edges at high opacity | CHECK | `effects.h` `effectiveBackgroundOpacity` + update on change. Verify our dock doesn't ghost during resize at high opacity. |
| 29a515f59 | 2026-06-02 | fix: LatteCore.Dialog → PlasmaCore.AppletPopup for P6 popups | ADOPT? | **Our `shell/…/CompactApplet.qml` still uses `LatteCore.Dialog`.** ng moved applet config popups to `AppletPopup`. Relevant to the applet-config popup issues (blank/sizing). Big change (new AboutPlugin/AppletConfiguration); evaluate as the fix for P6 popup compat. |
| 1173af2dd | 2026-06-03 | fix: applet config dialog blank window + popup sizing | CHECK | Pairs with 29a515f59. We have `internalAction("configure")` in CompactApplet but verify applet config windows aren't blank / mis-sized. |
| d1aa231b7 | 2026-06-03 | fix: restore popup resize for Application Menu | SKIP | Narrow app-menu popup tweak. |
| cce2a546f | 2026-06-03 | fix: remove recursive setVisible(false) in hideEvent (stack overflow) | HAVE | Our `primaryconfigview.cpp:465` hideEvent has no recursive `setVisible(false)` (different mode-change logic). We don't have this crash. |
| 5b1c8d821 | 2026-06-05 | fix: proper struts for AlwaysVisible via LayerShellQt | HAVE | We have `setViewStruts` (waylandinterface/xwindowinterface) + LayerShellQt exclusive-zone struts — our own AlwaysVisible strut impl. |
| 9cab741c2 | 2026-06-06 | fix: respect background radius in Modern dock style | SKIP | Modern-style config. |
| a4c6e77f6 | 2026-06-06 | fix: allow disabling shadows in Modern dock style | SKIP | Modern-style config. |
| a505038b2 | 2026-06-07 | fix: All Corners button for Classic and Modern | SKIP | Modern-style/background-corners config. |
| 30a1bd247 | 2026-06-07 | fix: disable Radius slider when All Corners off | SKIP | Config-UI enable state. |
| 8b6bc030b | 2026-06-07 | fix: widget explorer follows system theme all schemes | CHECK | Directly related to my widget-explorer left-edge fix. ng also sets `PlasmaShellSurface::Role::AppletPopup` on the explorer. Verify our explorer themes correctly + evaluate the AppletPopup role vs our layer-shell approach. |
| a118b91dc | 2026-06-07 | fix: debounce parabolic nullifier to stop zoom flicker | CHECK | `m_parabolicItemNullifier.setInterval(80)`. Verify our parabolic zoom doesn't flicker moving between icons; small debounce is a cheap ADOPT if it does. |
| 74e58dcbb | 2026-06-07 | fix: clip main layout so zoomed items don't overlap adjacent | CHECK | One-liner `clip: true`. Not in our containment main.qml. Verify zoomed icons don't bleed into adjacent layouts. |
| e9a579ac1 | 2026-06-07 | fix: edit-mode tooltip stability + ruler theme | CHECK | Edit-mode polish cluster. |
| 3c7c1e03c | 2026-06-07 | fix: zoomed items block clicks on adjacent icons | CHECK | ng raises launcher/tray `z:20` so zoom doesn't eat clicks. Verify our zoomed tasks don't block adjacent launcher/tray clicks. |
| 917022cf5 | 2026-06-07 | fix: unify widget explorer title/button theme | CHECK | Widget-explorer theming cluster (with 8b6bc030b); tied to the explorer fix. |
| cf5a78d28 | 2026-06-08 | fix: make Get New Widgets popup menu visible | CHECK | KNS "Get New Widgets" popup; related to the KNS gap below. |
| 97b701a70 | 2026-06-08 | fix: current panel thickness for struts + ghost window on correct screen | CHECK | Strut correctness + associating the strut ghost window with the right screen (multi-screen). Verify our strut sits on the correct output. |
| d3ca82919 | 2026-06-08 | fix: restore native KNS download via SharedQmlEngine | GAP | 483-line `knscompat.cpp` — we have **no `knscompat`**. "Get New Widgets/Indicators" downloads from store.kde.org likely don't work in our port. Decide if we want KNS download at all. |
| 433dc683a | 2026-06-09 | fix: null-safe guards for patched DrawerHandle/HandleButton | SKIP | Guards for ng's patched Kirigami drawer QML copies; ng-specific compat shims. |
| 8fabb8cc9 | 2026-06-09 | fix: explicit QML runtime deps in packaging | HAVE | Our `package.nix` already lists `kirigami`/`knewstuff` (and the wider QML set). |
| 90c9a0ca7 | 2026-06-09 | fix: opacity instead of visible for applet container | CHECK | Edit-mode applet-container visibility via opacity binding. Verify our applet show/hide in edit mode. |
| db748f0dd | 2026-06-09 | fix: full panel thickness for classic-style struts | CHECK | Strut-thickness cluster; verify our classic strut reserves the full thickness. |
| c917f7936 | 2026-06-09 | fix: prevent parabolic zoom jitter cursor between icons | ADOPT? | Our `parabolic.cpp` has `m_currentParabolicItem` switching but **no `MIN_SWITCH_INTERVAL`/`lastSwitchTimer` debounce** ng added. The journal flagged zoom/reorder jitter — this is a strong candidate. Verify + adopt the switch-interval guard. |
| c70988a3f | 2026-06-09 | fix: debounce double-click add in Widget Explorer | ADOPT? | No `addDebounceTimer` in our `shell/` explorer. The journal flagged **double-click crashes** in the explorer — this 400ms debounce prevents double-add/flash. Strong candidate. |
| 13628d9e5 | 2026-06-10 | fix: suppress cfg_ non-existent property warnings | CHECK | Applet-config `cfg_` key guard. Cheap; verify our applet config logs these. |
| c2c7bbed6 | 2026-06-11 | fix(systray): native icon colors + wheel handling | CHECK | Systray-in-dock colorizing exclusion (`appletBlocksColorizing`/`isSystray`). Verify a system tray widget renders/scrolls right in our dock. |
| c57458dd5 | 2026-06-12 | fix(widget-explorer): separator double-click flash-and-remove | CHECK | Separator + double-click debounce; separator feature is a GAP for us anyway. |
| 240f44593 | 2026-06-12 | fix(drag-drop): drag widgets from Widget Explorer to dock | HAVE~ | Our `view.cpp` already handles the `text/x-plasmoidservicename` drop. Verify the full drag-from-explorer path works (this is the gesture I couldn't test headlessly). |
| 735525810 | 2026-06-12 | fix(drag-drop): position-aware insertion + wave for drops | ADOPT? | `_latte_pendingInsertionIndex` absent. Our drops likely land at the dock end, not at the cursor. Good UX fix for drag-to-add; evaluate. |
| e557fcdf3 | 2026-06-12 | fix(widget-explorer): double-click to add separator applets | CHECK | Separator GAP; moot until separators exist. |
| 0e7d8e065 | 2026-06-12 | fix(widget-explorer): place new applets before systray/tasks | CHECK | Insertion-order fix (don't dump new widgets at the dock end). Pairs with 735525810. |
| 95b1f7cd8 | 2026-06-12 | fix: suppress benign Qt/KIO diagnostics + KIO teardown | CHECK | We have a `qInstallMessageHandler` in `main.cpp`; can add ng's specific noise filters if our logs are noisy. Low value. |
| 544479586 | 2026-06-12 | fix(widget): multi-cell slot for text-heavy external applets | ADOPT? | `externalAppletForcedSlots`/`externalAppletNaturalWidth` absent. Text-heavy widgets (clock, etc. — the one I test-added) may be squashed into one icon cell. Verify external widget sizing; good candidate. |
| be72dd5f4 | 2026-06-12 | fix: suppress KDE framework property override warnings | CHECK | More log-noise filters; pairs with 95b1f7cd8. Cosmetic. |
| 88ee2b8ab | 2026-06-12 | fix(systray): drag-drop reorder without breaking layout | CHECK | Systray reorder + sort-drag z-order. Verify systray widget behaves in our dock. |
| 078c1e1ef | 2026-06-12 | fix(popup): Wayland destroying applet popup on open | CHECK | Applet-popup lifetime on Wayland (block-hiding on NeedsAttention). Verify applet popups survive open in our dock. |
| c3db3ded0 | 2026-06-12 | fix(widget): show external C++ plasmoids requesting fillWidth | HAVE~ | Our `containment/…/AppletItem.qml`/`ItemWrapper.qml`/`LayouterPrivate.qml` already have `minAutoFillLength`/`maxAutoFillLength`. Verify fillWidth widgets expand. |
| ccc86c428 | 2026-06-13 | fix(wheel): pass wheel to all external applets | CHECK | `resolveAppletQuickItem` wheel routing. Verify scrolling over any widget (not just systray) reaches it. |
| f7dcc58cb | 2026-06-13 | fix(audio): no false muted icon when no stream | HAVE~ | Our `TaskItem.qml:215` has `hasAudioStream`; verify the muted icon is gated on it. |
| 73d982f0b | 2026-06-13 | fix(visibility): eliminate binding loop on inNormalState | ADOPT | **Confirmed: our dock logs `Binding loop detected for inNormalState` (VisibilityManager.qml:32).** ng replaced the binding with an imperative `recomputeInNormalState()`. Real, reproduced-in-our-build bug. Take it. |
| ea760f68f | 2026-06-13 | fix(cache): auto-clear stale QML disk cache on version change | CHECK | `autoClearQmlCacheOnVersionChange` absent. Avoids stale QML after upgrades; nice robustness. Evaluate. |
| 00fe319d7 | 2026-06-13 | fix(install): never purge user config during update | N/A | ng `install.sh`; our nix install doesn't do this. |
| 9ff5e6a5f | 2026-06-13 | fix(applet): defer container creation for new applets | CHECK | Applet-creation-timing cluster (with 1c96f2b4c). Verify no stale initial state when adding widgets. |
| dfafd07af | 2026-06-13 | fix(audio): prime PulseAudio SinkModel at startup | CHECK | Audio-priming cluster (4 commits: +00ae44348/7556d052a/b7dab7a90) to kill a false muted icon before PA is ready. We have a PulseAudio `Instantiator` but maybe not sink priming. Verify no false muted icon at startup; adopt the priming if so. |
| 00ae44348 | 2026-06-13 | fix(audio): drive SinkModel with Instantiator to prime PA | CHECK | Same audio-priming cluster. |
| 7556d052a | 2026-06-13 | fix(audio): force PreferredDevice initial sink read | CHECK | Same audio-priming cluster (`paFixTimer`). |
| b7dab7a90 | 2026-06-13 | fix(audio): back off paFixTimer to 30s after priming | CHECK | Same audio-priming cluster; the debounce tail. |
| c8875ee28 | 2026-06-13 | feat(applet): always show Remove in applet context menu | CHECK | Our `CompactApplet.qml` has no `closeApplet`/Remove wiring. Ties to your reported widget right-click issues; verify Remove is offered on widgets. |
| 43f86bd09 | 2026-06-13 | fix(shell): propagate Layout min/max to applet popup | CHECK | Applet-popup sizing; pairs with 29a515f59. |
| f14778efe | 2026-06-13 | fix(systray): reclaim dock space on applet removal | CHECK | Verify removing a widget immediately frees its dock space. |
| 7b8d9e0ff | 2026-06-13 | fix(layout): remove systray from default insertion boundary | CHECK | Insertion-order tweak; pairs with 0e7d8e065. |
| 70eced15e | 2026-06-13 | fix(wheel): restore containment scroll over latte plasmoid | CHECK | Scroll-action routing; verify scroll over the tasks area triggers containment scroll actions. |
| ad0d2c2e5 | 2026-06-14 | fix(widget): exclude analog clock from text-applet slot sizing | CHECK | Special-cases `digitalclock` sizing (the widget I test-added). Pairs with 544479586. |
| 3a2ce268a | 2026-06-14 | fix(cast): replace C-style enum cast in visibilitymanager | SKIP | Cleanup; also confirms ng's default visibility is DodgeActive (same as ours). |
| beff425d9 | 2026-06-15 | fix: audit compliance and shutdown stability | HAVE~ | We have `m_lastContainmentStatus` in `contextmenulayerquickitem`. Verify the shutdown-stability bits; relates to the KSvg exit crash note in this doc's peers. |
| ab3092e66 | 2026-06-15 | fix: reduce AlwaysVisible ghost window visibility | CHECK | We have the ghost window (`screenedgeghostwindow.cpp`). ng sets an alpha buffer to hide it; verify ours isn't visible. |
| a66c24145 | 2026-06-15 | fix: eliminate ghost window artifact during mode switches | CHECK | Ghost-window artifact on Dodge↔AlwaysVisible switch (exactly the modes I toggled). We have the ghost window; verify no flash on mode switch. |
| 30637c1cd | 2026-06-16 | fix: prevent dock collapse when adding fillWidth applets | HAVE~ | We have `inNormalFillCalculationsState` in our Layouter. Verify adding a fillWidth widget doesn't collapse the dock. |
| 2146e5564 | 2026-06-16 | fix: quit() in commitDataRequest to prevent logout hang | CHECK | Our `main.cpp:261` sets `RestartNever` on `commitDataRequest` but ng additionally forces `quit()` to avoid a logout hang. Verify our dock doesn't hang logout; cheap ADOPT if it does. |
| a9c200fe2 | 2026-06-16 | fix: align shutdown with Plasma panel pattern, fix double-free | ADOPT? | ng's `flushDelete` (deleteLater + `sendPostedEvents(DeferredDelete)`) for ordered teardown is **absent in our `main.cpp`**. This is a strong candidate for our **KSvg static-destructor exit crash** (see d45c7a38) — proper deferred-delete flush at shutdown. Evaluate. |
| 911dd1a59 | 2026-06-16 | fix: remove redundant LayerShellQt::Window::setScreen() | CHECK | Our `waylandlayershell.cpp:168` still calls `ls->setScreen()` (plus `window->setScreen()` at 166). ng found the LayerShellQt one redundant/harmful. Given our hotplug findings, worth revisiting whether that call helps or hurts. |
| f6ca45a75 | 2026-06-16 | fix: install indicator package structure for KF6 | HAVE | We have `app/packageplugins/indicator/indicatorpackage.cpp` — the indicator packagestructure plugin. |
| e787da77e | 2026-06-17 | fix: quiet compiler warnings | SKIP | Warning cleanup. |
| 0b29b63cb | 2026-06-17 | fix: silence remaining build warnings | SKIP | Warning cleanup. |
| d26645a48 | 2026-06-18 | fix: avoid panel overlap for edge-aligned docks | CHECK | New `viewgeometryhelpers.h` (132 lines) we don't have; positions left/right edge docks to avoid overlapping a Plasma panel. Verify our edge-aligned docks don't overlap panels. |
| 4ec686ce2 | 2026-06-18 | fix: clean Latte runtime warnings | SKIP | Runtime log-noise filter. |
| fb7f2f5de | 2026-06-18 | fix: restore widget undo position | HAVE~ | Our `containment/plugin/layoutmanager.cpp` has scheduled-destruction handling. Verify widget remove→undo restores the original index. |
| 174314979 | 2026-06-18 | fix: stabilize applet popup sizing | CHECK | Applet-popup sizing (CompactApplet/PulseAudio). Pairs with 29a515f59/43f86bd09. |
| a53e0adc3 | 2026-06-18 | fix: keep launcher applet popups resizable | CHECK | Applet-popup resize; same popup cluster. |
| a4abdcd01 | 2026-06-18 | fix: harden KNS compatibility overrides | SKIP | Extends ng's KNS compat (which we don't have — see d3ca82919 GAP). |
| 63d575107 | 2026-06-18 | build: document relaxed warning flags | N/A | ng build-flag doc. |
| c6e123401 | 2026-06-18 | fix: keep dock geometry aligned with edge changes | CHECK | Edge-relocation cluster via `viewgeometryhelpers.h` (absent in our port). Pairs with d26645a48/17b3b9519. |
| c85324068 | 2026-06-18 | feat: expose launcher pinning API | SKIP | ng-specific D-Bus `hasLauncher`/pinning API for external launcher sync; not in our design. |
| 17b3b9519 | 2026-06-18 | fix: stabilize dock edge relocation | CHECK | Same `viewgeometryhelpers` edge-relocation cluster. Verify our dock re-places cleanly on edge/alignment change. |
| 8a86e2215 | 2026-06-18 | feat: add dock items alignment option | CHECK | We have dock `alignment` (containment config:8). ng adds a *separate* items-alignment (Left/Center/Justify within the dock). Verify whether we expose that. |
| 9c12a79aa | 2026-06-18 | fix: harden cross-distro install packaging | N/A | CPACK distro deps + KNS; our nix build is separate. |
| f469d0737 | 2026-06-18 | fix: translate items alignment controls | N/A | po translations. |
| 9cbab5920 | 2026-06-18 | build: share CMake target resolution helpers | N/A | ng CMake refactor. |
| 1585c3b21 | 2026-06-18 | build: share imported target resolution helper | N/A | ng CMake refactor. |
| a6b6768f5 | 2026-06-18 | build: move CMake target helpers into module | N/A | ng CMake refactor. |
| 4db3cfdf5 | 2026-06-18 | build: move low-risk CMake config into modules | N/A | ng CMake refactor. |
| 09faa7d09 | 2026-06-18 | fix: allow isolated KNS compat user QML root | SKIP | KNS compat env (GAP — no KNS in our port). |
| c42d3355d | 2026-06-18 | fix: reject relative KNS compat user QML root | SKIP | KNS compat hardening (GAP). |
| a7e7091f2 | 2026-06-18 | fix: harden KNS compat override paths | SKIP | KNS compat path hardening (GAP — no KNS in our port). |
| 8e9dd600e | 2026-06-18 | fix: find Qt CoreTools before KDE install dirs | N/A | ng CMake find-order for their autotests. |
| c25b877b0 | 2026-06-18 | fix: smooth task icon zoom | ADOPT? | One-liner: ng uncomments `roundToIconSize: false` on the task icon so hover-zoom scales smoothly on current Kirigami. Ours is still commented (`TaskIcon.qml:80`). Cheap, matches the parabolic-jitter cluster. Verify Kirigami version behaves, then uncomment. |
| 8a1106784 | 2026-06-18 | feat: prefer GPU Qt Quick rendering | CHECK | ng calls `QQuickWindow::setGraphicsApi(OpenGL)` unless `QT_QUICK_BACKEND`/`QSG_RHI_BACKEND`/`QT_OPENGL` is set, to avoid RHI picking software rendering. We don't set it (`main.cpp` has no `setGraphicsApi`). Low priority; adopt if the dock ever renders via software on some GPU. |
| 3c773a7a5 | 2026-06-19 | fix: stabilize get new widgets dialog | SKIP | KNS compat versioning + drawer QML (GAP — no KNS). |
| 0fbf9d000 | 2026-06-19 | fix: translate widget explorer labels | N/A | ng shell WidgetExplorer i18n contexts (ng's own shell view). |
| 8f0ad2581 | 2026-06-19 | feat: add Plasma launcher dock action | SKIP | ng-specific kicker/launcher `.desktop` action + `launcherhelper`; not in our design. |
| 338a92c81 | 2026-06-19 | fix: stabilize widget explorer external dialogs | CHECK | Two parts: (a) keep Add Widgets alive while an external widget dialog is open — relevant to our explorer; (b) synthesize a Wayland env for KNS from a sparse dev shell — GAP (KNS). Our `widgetexplorerview.cpp` differs. Verify our explorer survives opening an external dialog; the KNS half is N/A. |
| 7f91f3888 | 2026-06-19 | fix: harden install cleanup paths | N/A | ng install/uninstall shell scripts. |
| ef2989ec2 | 2026-06-19 | fix: refresh task icons on icon theme changes | GAP | ng adds `forceRefreshTaskIconSource()` (clear source + `Qt.callLater` rebind) in TaskIcon and `QIcon::setThemeName` + `QPixmapCache::clear()` in `environment.cpp` so task icons repaint live on an icon-theme switch. We have **neither** (`TaskIcon.qml` has no forceRefresh; `environment.cpp` has no setThemeName/QPixmapCache). Real behavior gap: our task icons likely go stale until restart on icon-theme change. Worth adopting. |
| 0f3d89d3c | 2026-06-19 | fix: scale audio badges with task zoom | N/A~ | Part of ng's audio-badge rework: `parabolicZoom`/`maximumBadgeSize` props so the badge grows with hover-zoom. Our `AudioStream.qml` has none of these props (different design). Only relevant if we adopt ng's audio-badge structure wholesale. |
| 2d5d4408e | 2026-06-21 | fix: handle cancellable session shutdown | HAVE | We already carry ng's **final** clean pattern: a `disableSessionManagement` lambda on both `commitDataRequest`/`saveStateRequest` that only sets `RestartNever` and never quits during a still-cancellable logout (`main.cpp:259-262`). This commit is ng deleting the very polling/`isShuttingDown` hack we also don't have. Minor: ng also sets `Qt::AA_DisableSessionManager` which we don't — cheap to add but not required. |
| 53918b5eb | 2026-06-21 | fix: compact Modern background shadow | SKIP | Cosmetic: match the default Modern background shadow to 6px so reserved margins agree. ng-specific default tuning. |
| 86c0605d3 | 2026-06-22 | fix: quit Latte after committed shutdown without blockers | N/A | Adds `isPlasmaShutdownServiceActive()` (`org.kde.Shutdown` DBus probe) into ng's shutdown **poller** so it quits once shutdown is committed and no blocking windows remain. We don't have that poller (see 2d5d4408e — we use the clean lambda path), so this bolt-on doesn't apply. If our logout ever hangs, revisit. |
| 97403c4d3 | 2026-06-22 | fix: bind Wayland struts to target screen | N/A~ | ng binds struts to the right output in their `setExtendedStrut` path. Our struts are LayerShell exclusive zones anchored per-screen in `waylandlayershell.cpp` (no `setExtendedStrut`), a different mechanism. Verify multi-screen struts land on the correct output, but there's no direct code to take. |
| 8054a8cad | 2026-06-23 | fix: sync all-screen applet edits | CHECK | We have `clonedview.cpp`/`containmentinterface`. ng syncs applet edits from the primary containment to cloned per-screen views. Verify editing an applet on a multi-screen "all screens" dock reflects on the clones. |
| 2437a92ad | 2026-06-23 | fix: duplicate instance exits cleanly without QGuiApp exit | ADOPT? | **Direct candidate for our KSvg exit crash.** Our duplicate-instance guard calls `qGuiApp->exit(); return 0;` (`main.cpp:193,211,229,247,278`) — tearing down Qt globals that were never fully initialised, exactly the static-destructor teardown that segfaults (fixed in d45c7a38). ng drops the `qGuiApp->exit()` for a plain `return 0` and defers `SharedQmlEngine` creation until after the guard. Evaluate replacing our `qGuiApp->exit()` with a bare `return 0` on the dup path. |
| e29b68a2b | 2026-06-24 | fix: avoid Plasma panel overlap for vertical docks on multi-screen Wayland | CHECK | Uses `viewgeometryhelpers.h` (absent in our port) to keep a vertical dock off a Plasma panel on a second screen. Same edge-overlap cluster as d26645a48/c6e123401. Verify our vertical docks don't overlap panels on multi-screen. |
| 8bc9c0e10 | 2026-06-25 | fix: sync widget hide/show across all screens during removal and undo | CHECK | Multi-screen clone follow-up (clonedview + layoutmanager): a widget removed/undone on the primary must hide/show on clones. Pairs with 8054a8cad. Verify on multi-screen. |
| 14c98b9cc | 2026-06-25 | fix: middle-click close active window not working on empty dock areas | HAVE~ | The core fix — only accept the button event when a containment-action plugin is registered for the trigger, else let it propagate to the MouseArea below — is already ours (`contextmenulayerquickitem.cpp:156` sets accepted from `containmentActions().contains(...)`). ng additionally wires a `closeActiveWindowEnabled` property + lastActiveWindow close. Verify middle-click on empty dock closes the active window; the propagation half is HAVE. |
| a72c7e582 | 2026-06-25 | fix: scroll-down minimize not working for ScrollToggleMinimized action | CHECK | ng fixed a wrong-direction call in their `main.qml` z:10000 wheel handler (down-scroll called activate instead of minimize). Our `main.qml:175` handles `ScrollToggleMinimized` but via different code (no `rootWheelActivateTask`). Verify scrolling down over a task minimizes it. |
| 8e7cbbd66 | 2026-06-26 | fix: use private QML path for KNS compat to avoid crashing systemsettings | SKIP | KNS compat QML path (GAP — no KNS). |
| d5eebf9c6 | 2026-06-26 | fix: clean up KNS compat overrides in uninstall.sh | SKIP | KNS compat uninstall cleanup (GAP). |
| 84e7c9d10 | 2026-06-27 | fix: improve drag-and-drop icon reordering stability and visual feedback | ADOPT? | Reorder-jitter cluster (journal flagged reorder jitter + "icon stuck" bug). ng recursively collects `TaskItem` descendants (`collectTaskDescendants`) as a gap-detection fallback when `childAtPos` returns null between delegates, always applies the before/after-center direction check, removes launcher/task segregation, and adds drag opacity feedback. Our `MouseHandler.qml` uses plain `childAtPos` (line 142), no recursive fallback; `AppletItem.qml` has no `insertAtSideLayoutEdge`. Our reorder base is qt6-derived; evaluate this against it. |
| 4386dfd9d | 2026-06-27 | fix: restore auto-pin when dragging non-pinned tasks into launcher area | CHECK | Re-adds `schedulePromoteToLauncherAndMove` (auto-pin a running task by dragging it between pinned launchers) that 84e7c9d10 removed with the segregation guard. Part of the ng auto-pin feature (ties to 8410b0400); our drag is qt6-derived and likely lacks auto-pin. Feature choice — decide if we want drag-to-pin. |
| 7e918a4f6 | 2026-06-27 | fix: scope KNS compat QML import path to latte-dock engines only | SKIP | KNS-specific slice of 7d027c9b6 (GAP — no KNS). |
| 7d027c9b6 | 2026-06-27 | fix: scope all QML and plugin import paths to latte-dock engines only | CHECK | Real bug class: ng was exporting `QML_IMPORT_PATH`/`QT_QML_IMPORT_PATH` via `qputenv`, which **leaks into child processes and can crash other Qt apps**; they switched to per-engine `addImportPath()`. Our `main.cpp` has an `ensureUserLocalQmlImportPaths()` — verify it does NOT set those env vars globally (grep found no `addImportPath`, so confirm how ours injects paths and whether it leaks). |
| 1f96a06b0 | 2026-06-27 | fix: guard LayerShellQt::Window::setScreen with CMake feature detection | HAVE | We already have this exact guard: `CMakeLists.txt:108` `try_compile(LATTE_LAYERSHELL_HAS_SET_SCREEN ...)` + `cmake/CheckLayerShellSetScreen.cpp`, and `waylandlayershell.cpp:167` `#ifdef`s the `ls->setScreen()` call. |
| b34d363a1 | 2026-06-27 | fix(widget): increase digital clock width cap for long date formats | CHECK | Clock-width cluster (b34d363a1/38d5e8fff/29056b643/66c431045). ng special-cases clock applets in `CompactApplet.qml` to widen for long date strings. Ours uses a generic `Layout.maximumWidth` from the applet (`CompactApplet.qml:215`). Verify a digital clock with a long date format isn't clipped in our dock. |
| e1806ce3f | 2026-06-28 | fix: guard int property bindings against undefined from bridge host in MyView | CHECK | `declarativeimports/abilities/client/MyView.qml`: guard int bindings against `undefined` coming from the Latte↔applet bridge host. Ties to the communicator-bridge cluster (cb89df82a/b0fc8c5f2). Verify our MyView doesn't emit `undefined`-to-int warnings/breakage. |
| 38d5e8fff | 2026-06-28 | fix(widget): broaden clock detection and increase width cap for third-party clocks | CHECK | Clock-width cluster; extends detection to non-KDE clocks. Same generic-Layout question as b34d363a1. |
| 29056b643 | 2026-06-28 | fix(widget): add signal-driven slot width update for clock format changes | CHECK | Clock-width cluster; recompute slot width when the clock's format changes at runtime. |
| 66c431045 | 2026-06-28 | fix(widget): add signal-driven width update and height cap for clock widgets | CHECK | Clock-width cluster; adds a height cap too. Evaluate the whole cluster together if any clock-clipping is observed. |
| d092fd00d | 2026-06-29 | fix: deferred initial applet order sync for cloned views | CHECK | Multi-screen clone follow-up (`clonedview.cpp`): defer the initial applet-order sync so clones don't restore before the primary is ready. Verify cloned docks show applets in the right order on startup. |
| 2c7ca6877 | 2026-06-29 | fix: unkillable process and invisible dock on startup | CHECK | Two halves. (a) Unkillable-on-Ctrl-C: ng replaced `KSignalHandler::watchSignal(SIGINT)` with a self-pipe handler — **N/A for us**, our `main.cpp:429` uses a plain `std::signal(SIGINT, ...)`, never the signalfd path that missed signals. (b) Invisible-dock-on-startup: `layoutmanager.cpp` retries the applet restore up to 60 times while warming up (`m_restoreRetryCount < 60`). Verify our dock never comes up empty/invisible on a cold start; adopt a restore-retry if it does. |
| f4ab344ee | 2026-06-30 | fix: use allScreens tracker for ScrollToggleMinimized minimize/maximize | CHECK | ng switched the ScrollToggleMinimized min/max target to `windowsTracker.allScreens.lastActiveWindow`. Ours uses `selectedWindowsTracker.lastActiveWindow` (`EnvironmentActions.qml:158`). Verify scroll-to-minimize/maximize acts on the right window in multi-screen; may be a per-screen-vs-all-screens gap. |
| 956581cbc | 2026-06-30 | fix: prevent text-heavy clocks from occupying excessive dock space | CHECK | Clock-width cluster tail (`CompactApplet.qml`). Same generic-`Layout.maximumWidth` question as b34d363a1 etc. Evaluate the whole clock cluster together only if a clock is seen eating dock space. |
| fe4e1154b | 2026-07-01 | fix: convert Qt5-style signal handlers to Qt6 handler-binding syntax in drop areas | HAVE | ng was fixing dead `function onDragEnter(...)` direct-handlers that silently don't connect in Qt6. Our `DragDropArea.qml` (92/143/173) and `MouseHandler.qml` (106/127/205) already use the correct `onDragEnter: (event) => {}` arrow-binding form. We never had the dead-handler bug. |
| cb6bae98c | 2026-07-01 | fix: convert onDragMove to Qt6 handler-binding syntax in ToolTipDelegate2 | HAVE | Same as above; our `ToolTipDelegate2.qml:89` already uses `onDragMove: (event) => {}`. |
| ef78e238c | 2026-07-01 | feat: increase zoom-on-hover slider maximum from 225% to 300% | SKIP | Pure preference-range bump in the appearance config. Trivial to adopt if we want a bigger max zoom; no correctness value. |
| 80bb966c8 | 2026-07-02 | fix: convert widget's drag-and-drop handlers to binding form | HAVE | Same Qt6 handler-binding class; our `DragDropArea.qml` already uses the arrow form. |
| 871c4322d | 2026-07-02 | fix: restore tasks menu | CHECK (priority) | Two halves. (a) Resolve the Latte tasks applet item via `AppletQuickItem::itemForApplet()` — **HAVE**, our `containmentinterface.cpp` uses `itemForApplet` at all 13 sites. (b) `Qt.MidButton` → `Qt.MiddleButton` in `TaskMouseArea.qml`/`EnvironmentActions.qml`/`ClickedAnimation.qml`. **This directly conflicts with our deliberate choice** (`PORTING_PLAN.md:636-643`): we kept `Qt.MidButton` at the EnvironmentActions sites because the rename caused C++/QML **double-handling** of the same middle-click in ng. ng has now reversed and renamed. Open question ng's move raises: is `Qt.MidButton` even defined in our Qt6 QML, or is it `undefined` (making every `mouse.button === Qt.MidButton` false and silently killing middle-click)? **Live-test middle-click on our dock** (close window / new instance); if it works, our retention stands and ng may be re-introducing double-handling; if it doesn't, adopt the rename. Ties to 14c98b9cc. |
| 9faccabda | 2026-07-02 | fix: hide unfinished Tasks configuration tab | CHECK | ng judged the original author's Tasks config page as non-applying (UI renders, changes don't take effect) and hid it (`model: 0`). Our `LatteDockConfiguration.qml:477` still exposes it (`... latteTasksModel : 0`). Ties to the tasks-config note (now D10 in known-defects). Decide whether our Tasks config tab actually applies changes; if not, hide it the same way. |
| c4e7bcb62 | 2026-07-03 | fix: reduce task drag reorder jitter with hysteresis and higher resistance | ADOPT? | Reorder-jitter cluster (journal-flagged). ng raises `resistanceDelay` 450→600 and adds Manhattan-distance hysteresis (`minReorderDistance: 12`, `lastReorderIndex/X/Y`) so the insertion indicator stops flickering between adjacent slots. Our `TaskItem.qml:114` still has `resistanceDelay: 450` and **no** hysteresis in `MouseHandler.qml`. Strong candidate; pairs with 84e7c9d10 and the parabolic-jitter candidates. |
| cbb2f8199 | 2026-07-03 | fix: pass targetSize to grabToImage and suppress Qt internal drag warning | CHECK | Qt6 `grabToImage` expects an explicit `targetSize`. Our `TaskItem.qml:328` calls `grabToImage((result) => {...})` with **no** size, so the drag image may be mis-sized. ng passes `Qt.size(taskItem.width, taskItem.height)`. Small, safe fix worth taking (the warning-suppression half is cosmetic). |
| 2dafff37e | 2026-07-03 | fix: allow pointer events on dock during edit mode for drag-to-reorder | CHECK | ng sets `Qt::WindowTransparentForInput` on the edit-mode canvas config view so pointer events reach applet icons for reorder. We solve the same problem differently — a computed input region (`canvasInputRegion()` in `waylandlayershell.cpp`). Verify drag-to-reorder works while in edit mode on our port; no code to copy if it does. |
| 062eb1b41 | 2026-07-03 | feat: enable sort-drag for all applets in edit mode | CHECK | `AppletItem.qml`: allow any applet (not just tasks) to be sort-dragged in edit mode. Verify we can drag-reorder non-task widgets in edit mode. |
| 9a5e3b889 | 2026-07-03 | fix: cancel in-progress sort-drags when exiting edit mode | CHECK | `main.qml`: abort a live sort-drag if edit mode ends mid-drag (avoids a stuck/half-committed drag). Verify leaving edit mode during a drag doesn't strand an applet. |
| 4348980c0 | 2026-07-03 | fix: disable parabolic animation in edit mode and ignore task drag-move events | CHECK | Two bits: ignore task drag-move (our `DragDropArea.qml:146` already `return`s when `dragInfo.isTask` — HAVE); and disable the parabolic zoom animation while in edit mode (`ParabolicEffect.qml`). Verify our parabolic zoom is quiet in edit mode; adopt the guard if it still animates. |
| 924a8ac41 | 2026-07-03 | fix: reduce applet sort-drag jitter with hysteresis and higher resistance | ADOPT? | Applet analog of c4e7bcb62 (reorder-jitter cluster). Adds spatial hysteresis (`sortDragMinDistance: 12`, `sortDragLastX/Y`), bumps commit cooldown 90→180ms and center dead-zone 0.18→0.30 in `AppletItem.qml`. Our `AppletItem.qml` has **no** `shouldDelaySortCommit`/sort-drag hysteresis at all. Strong candidate together with c4e7bcb62. |
| cf6aa1ec0 | 2026-07-03 | fix: further increase sort-drag resistance to reduce jitter | ADOPT? | Same-day follow-up tuning to 924a8ac41 (more resistance). Take with 924a8ac41 as one change if adopting the applet-reorder hysteresis. |
| 1b07df291 | 2026-07-03 | fix: prevent double widget creation on Widget Explorer drop via mime split | N/A (verified 2026-07-08) | ng found `text/x-plasmoidservicename` drops handled by BOTH a C++ `View::event()`→`handlePlasmoidDrop()` AND the QML `onDrop`, double-creating. **Does not apply to us:** our `View::event()` `QEvent::Drop` case (`view.cpp:1557`) only `setContainsDrag(false)` and forwards - we have **no** `handlePlasmoidDrop`, so the sole applet-creation path is the QML `onDrop`→`processMimeData` (`DragDropArea.qml:198`). (My earlier note misread `view.cpp:1390`, which is the `mimeContainsPlasmoid` *check* helper, not a creation path.) If a double-add is ever reproduced, chase the explorer double-click debounce (`c70988a3f`) instead. |
| ec839b7eb | 2026-07-03 | fix: block mouse interactions on dock during edit mode except drag-to-reorder | N/A~ | Edit-mode input-blocking **overlay cluster** (ec839b7eb, 61a7e40a8, 79b0ecaf5, 1a0fc7dff, b93902dc7, 0de8f4ece — six same-day iterations of a QML MouseArea overlay in `containment/main.qml` that blocks clicks but punches through for task-plasmoid drag). Our port blocks edit-mode input at the C++ layer via a computed input region (`canvasInputRegion()` in `waylandlayershell.cpp`), a different mechanism, so these don't port directly. That ng needed six tries corroborates our C++ approach. Verify our edit-mode input-blocking + drag punch-through behaves; no code to copy. |
| 61a7e40a8 | 2026-07-03 | fix: add MouseArea overlay to block all non-drag interactions in edit mode | N/A~ | Same overlay cluster (see ec839b7eb). |
| 79b0ecaf5 | 2026-07-03 | fix: block task clicks and context menu in edit mode while allowing drag | N/A~ | Same overlay cluster (see ec839b7eb). |
| 1a0fc7dff | 2026-07-03 | fix: restore overlay with task-plasmoid punch-through for edit mode | N/A~ | Same overlay cluster (see ec839b7eb). |
| b93902dc7 | 2026-07-03 | fix: explicitly set mouse.accepted=false for task plasmoid punch-through | N/A~ | Same overlay cluster (see ec839b7eb). |
| b12cce721 | 2026-07-03 | fix: add containment fallback for plasmoid inEditMode detection | CHECK | **Corroborates the edit-mode-entry open question** (now noted in docs/reference/live-only.md as D-Bus-drivable). ng found `plasmoid.userConfiguring` alone unreliable when the bridge isn't connected yet, so added `plasmoid.containment.userConfiguring` as a fallback. Our `plasmoid/main.qml:193` uses exactly ng's **pre-fix** form: `latteInEditMode || Plasmoid.userConfiguring`. Verify our plasmoid reliably detects edit mode on entry; adopt the containment fallback if not. |
| 480a3ba84 | 2026-07-03 | fix: use explicit signal connection for containment edit mode in plasmoid | CHECK | Final form of the inEditMode-reliability fix: track `containmentUserConfiguring` via an explicit `onUserConfiguringChanged` Connections handler because the plain binding "may not reliably update in QML". Take this together with b12cce721 if our edit-mode detection proves flaky. |
| 0de8f4ece | 2026-07-03 | fix: use global overlay to block all mouse events in edit mode | N/A~ | Culmination of the overlay cluster (see ec839b7eb). |
| 618ed8f50 | 2026-07-03 | fix: use polling timer for reliable plasmoid edit-mode detection | SKIP (flag) | **Edit-mode-detection saga** (618ed8f50, 1cfb574ad, eccc3ea7b, 4be9bfb5c, 546c8b109, 6c9bbc145, 831c400c1 — seven more same-day iterations on top of b12cce721/480a3ba84). ng ended up on a **polling-timer stack** to detect edit mode because bindings/signals kept failing. Our port deliberately uses the simple direct binding (`containment/main.qml:112` `editMode: Plasmoid.userConfiguring`; `plasmoid/main.qml:193`), per the plan. We do NOT want ng's polling stack — but the sheer number of attempts here is a **strong flag** that our simple binding needs a careful live check (does edit mode reliably register on entry, on the plasmoid AND the containment?). |
| 1cfb574ad | 2026-07-03 | fix: check plasmoid.containment.userConfiguring directly in handlers | CHECK | Edit-mode-detection saga (see 618ed8f50): read `containment.userConfiguring` inside the task handlers rather than trusting the cached property. Worth mirroring if our task click-guards misfire in edit mode. |
| eccc3ea7b | 2026-07-03 | fix: add dedicated polling timer inside TaskMouseArea for edit mode | SKIP | Edit-mode-detection saga; per-handler polling timer. Same reason as 618ed8f50 (we avoid the polling approach). |
| 4be9bfb5c | 2026-07-03 | fix: propagate edit mode via direct property assignment to plasmoid | CHECK | Edit-mode-detection saga; push edit-mode state containment→plasmoid by assignment. Relevant if our bridge-based `latteInEditMode` lags. |
| 546c8b109 | 2026-07-03 | fix: poll containment plasmoid.userConfiguring for reliable editMode | SKIP | Edit-mode-detection saga; polling approach we avoid. |
| 565ffed9e | 2026-07-03 | fix: swallow middle-button events at View level during edit mode | CHECK | **Middle-button-in-edit-mode cluster** (565ffed9e, a6fbd43a0, 62a0e6e7c). ng intercepts middle-button events in `View::event()` during edit mode so a middle-click doesn't fire a task action while you're reordering. Our `view.cpp:1566` already handles `MouseButtonPress` and has `userConfiguringChanged` wiring at :195 — verify we actually suppress task middle-click actions in edit mode; adopt the swallow if not. Ties to the `Qt.MidButton` question (871c4322d). |
| a6fbd43a0 | 2026-07-03 | fix: intercept middle-button events in View::event during edit mode | CHECK | Same middle-button-in-edit-mode cluster (see 565ffed9e). |
| 6c9bbc145 | 2026-07-03 | fix: timer only runs during edit mode, entry via C++ signal | SKIP | Edit-mode-detection saga; scoping the poll timer. Avoided (polling stack). |
| 831c400c1 | 2026-07-03 | fix: restore editMode binding for entry detection | SKIP | Edit-mode-detection saga; ng re-adding a binding alongside the timer. Avoided. |
| 62a0e6e7c | 2026-07-03 | fix: expand middle-button interception to cover all event types | CHECK | Middle-button-in-edit-mode cluster (see 565ffed9e); broadens the interception beyond press to all event types. |
| 8594b8a59 | 2026-07-03 | fix: convert DragDropArea handlers to Qt6 binding syntax | HAVE | Our `DragDropArea.qml` already uses the arrow-binding form (see fe4e1154b/80bb966c8). |
| 319232fc4 | 2026-07-03 | fix: convert TaskMouseArea handlers to Qt6 binding syntax | HAVE | Our `TaskMouseArea.qml` already uses correct Qt6 forms: `function onPressed(mouse)` inside `Connections` (61-62) and `onPressed: (mouse) => {}` direct handlers (142/176/239). |
| a34009b93 | 2026-07-03 | fix: convert EnvironmentActions MouseArea handlers to Qt6 binding syntax | HAVE | `EnvironmentActions.qml` already uses arrow-binding handlers (52/58/75). |
| 64e583df0 | 2026-07-03 | fix: convert AudioStream MouseArea handlers to Qt6 binding syntax | HAVE | Our `AudioStream.qml:95` already uses `onWheel: (wheel) => {}`. |
| 129ffb9d9 | 2026-07-03 | fix: convert ComboBox MouseArea onPositionChanged to Qt6 binding syntax | HAVE | `declarativeimports/components/ComboBox.qml` already uses arrow-binding handlers (120/138/368). |
| 00f88fed4 | 2026-07-03 | fix: convert CanvasConfiguration MouseArea onWheel to Qt6 binding syntax | HAVE | `CanvasConfiguration.qml:134` already uses `onWheel: (wheel) => {}`. |
| 9ba6fc017 | 2026-07-03 | fix: convert containment main.qml MouseArea handlers to Qt6 binding syntax | HAVE | Our `containment/main.qml` has no stale `function on...` direct handlers. |
| 3a1aeaf53 | 2026-07-04 | fix: add LSP config and guard EnvironmentActions against null trackers | CHECK | `.clangd` is N/A. The real fix: guard `EnvironmentActions` against a null `windowsTracker`/`lastActiveWindow`. Our `EnvironmentActions.qml:149` dereferences `selectedWindowsTracker.lastActiveWindow.isValid` with **no null guard** — if `lastActiveWindow` is null this throws. Small robustness fix worth taking. |
| f0cbbac77 | 2026-07-03 | fix: add compat shims for KIconThemes, KGuiAddons, KCoreAddons, KArchive | N/A | ng ships header/`compat/` shims to build against older KF6 releases. Our nix build pins proper KF6 versions, so we don't need them. |
| 1f6e0148e | 2026-07-03 | fix: refresh default.nix metadata | N/A | ng's own `default.nix`. Their packaging, separate from our flake. |
| f69312a2c | 2026-07-03 | feat: add thin flake.nix wrapping default.nix | N/A | ng's own `flake.nix` (the ng NixOS packaging, which we maintain separately in the ng repo, not here). |
| 33390d702 | 2026-07-03 | feat: add NixOS build verification to the Docker pipeline | N/A | ng CI (Docker + verify scripts). |
| b6693e7c4 | 2026-07-04 | fix: route containment wheel events to audio badge for volume adjustment | CHECK (priority) | **This is the Phase 8 containment→plasmoid wheel bridge deferred as live-only (docs/reference/live-only.md).** ng routes wheel events from the containment through to the task's audio badge so scrolling over a playing task changes its volume (`containment/main.qml` + `plasmoid/main.qml` + `TaskIcon.qml`). Our AudioStream badge (`TaskIcon.qml:308`) has its own `onWheel` (`AudioStream.qml:95`), but whether the containment actually delivers the wheel into it is the open Phase 8 question. Use this as the reference implementation; live-test scroll-over-badge volume. |
| b5f4a2649 | 2026-07-04 | feat: add volume level bar and percentage indicator on audio badge | CHECK | Optional UX feat: draw a volume-level bar + percentage on the audio badge (`AudioStream.qml`). Our audio badge is a mute/stream indicator with no level bar. Adopt only if we want the richer badge; pairs with b6693e7c4 (the wheel-to-volume interaction it visualizes). |
| 3d7cd3a6a | 2026-07-04 | fix: keep icon always visible and fix volume bar fill rendering | CHECK | Audio-badge cluster follow-up (with fcbb6ecc4, a dup): keep the task icon visible when the badge shows and fix the volume-bar fill. The icon-visibility half ties to 133043754. Relevant only if we adopt the ng volume-bar badge (b5f4a2649). |
| 94f87ba66 | 2026-07-04 | fix: populate latteTasksModel and enable Tasks config tab | CHECK | ng **re-enables** the Tasks config tab it had hidden in 9faccabda, now that `latteTasksModel` is populated (`containmentinterface.cpp` + `LatteDockConfiguration.qml`). Our port already exposes the tab (`LatteDockConfiguration.qml:477`) — the open question is whether ours is actually populated/wired (see eabf7c89a). |
| 67fa82e90 | 2026-07-04 | fix: route containment wheel events to audio badge for volume adjustment | CHECK | Same as b6693e7c4 (rebased/dup on the PR-merge day). Phase 8 wheel bridge — see b6693e7c4. |
| ad5978526 | 2026-07-04 | feat: add volume level bar and percentage indicator on audio badge | CHECK | Same as b5f4a2649 (dup). See b5f4a2649. |
| fcbb6ecc4 | 2026-07-04 | fix: keep icon always visible and fix volume bar fill rendering | CHECK | Same as 3d7cd3a6a (dup). See 3d7cd3a6a. |
| ed0afd054 | 2026-07-04 | fix: provide plasmoid config via KConfigLoader and set dynamic property | CHECK | Tasks-config plumbing (pairs eabf7c89a/2b0963186): expose the plasmoid's real config via `KConfigLoader` so the config UI and the plasmoid QML read/write the same store. Relevant if our Tasks config tab doesn't apply changes. |
| 40daf331e | 2026-07-04 | fix: use lib.cleanSource in default.nix to exclude build artifacts | N/A | ng's own `default.nix`. |
| ee727cfd3 | 2026-07-04 | Merge PR #26: Fix NixOS/nixpkgs build + first-class Nix packaging | N/A | ng packaging merge (KF6 compat shims + their Nix packaging). Separate from our flake. |
| eabf7c89a | 2026-07-04 | fix: wire Tasks config tab options to actual functionality | CHECK (priority) | Root-causes why the Tasks config tab's combos/checkboxes don't apply: `appletConfiguration()` created a **separate** `ConfigPropertyMap` instead of returning the same `KConfigPropertyMap` the plasmoid QML binds to, so writes through one loader are invisible to the other (independent KConfigLoader caches). Our Tasks tab is exposed (`LatteDockConfiguration.qml:477`) but may have this exact dual-loader disconnect. If our Tasks config doesn't take effect, this is the fix. Pairs with ed0afd054/2b0963186. |
| 2b0963186 | 2026-07-04 | fix: defer QQmlContext access to configurationForAppletVisualIndex | CHECK | P6 config plumbing (pairs eabf7c89a): defer `QQmlContext` access to avoid a null/early read. Take with the Tasks-config wiring if adopting it. |
| 0deca9e18 | 2026-07-04 | fix: make widget ParabolicArea MouseArea always visible for smooth wave animation | CHECK | **Ties to the parabolic-smoothness question** (docs/reference/live-only.md; always-visible synchronous MouseArea vs queued). ng keeps the widget `ParabolicArea` MouseArea always visible so `onPositionChanged` delivers synchronous parabolic updates (C++ still arbitrates enter/exit with a 150ms lock), avoiding a `QueuedConnection` round-trip that made the wave stutter. Our `ParabolicArea.qml:29` is already `visible: appletItem.parabolicEffectIsSupported` (close to always-visible) and uses `acceptedButtons: Qt.NoButton`-style hover tracking. Verify our widget wave is smooth; ng confirms this is the correct pattern. |
| 613ddcc3b | 2026-07-05 | fix: restore middle-click behavior and migrate to Plasma 6 APIs | ADOPT (priority — confirmed bug) | **The definitive answer to the `Qt.MidButton` question, and a confirmed live bug in our port.** ng's own message: *"Replace deprecated `Qt.MidButton` with `Qt.MiddleButton` in QML (removed in Qt 6)."* `Qt.MidButton` is **undefined** in Qt6 QML, so every `mouse.button === Qt.MidButton` in our port is always false: **task middle-click actions are dead** (`TaskMouseArea.qml:19/155/188` accepts `Qt.LeftButton \| undefined \| Qt.RightButton` = no middle button; `ClickedAnimation.qml:35` never fires). This overturns our `PORTING_PLAN.md:636-643` deliberate-retention note — that reasoning was based on ng's earlier state and is now wrong. ng's correct fix is asymmetric: rename to `Qt.MiddleButton` where QML must handle it (TaskMouseArea), but have `EnvironmentActions` accept **only** `Qt.LeftButton` so empty-area middle-click propagates to the C++ `ContextMenuLayerQuickItem` handler (avoids the double-handling our plan feared). Also flips `middleClickAction` default to Close. Adopt; update PORTING_PLAN and known-defects. Ties to 871c4322d and 14c98b9cc. |
| 5a807aac7 | 2026-07-05 | fix: suppress expected configuration-syncing warnings during startup | SKIP | Log-noise suppression during the Tasks-config sync. Cosmetic. |
| f0f65f4e3 | 2026-07-05 | fix: remove duplicate parabolic zoom scaling from audio badge size | N/A~ | Tail of ng's audio-badge parabolic rework (`AudioStream.qml`): they double-applied parabolic zoom to the badge (their 0f3d89d3c) and here remove the duplicate. Only relevant if we adopt ng's audio-badge structure (we don't share it). |
