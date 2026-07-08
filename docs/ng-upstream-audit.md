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

**Progress: 67 / 249 audited.**

**Emerging finding (after 13):** ng and our port solved the same original
independently, so most ng commits describe changes we simply did differently or
not at all — that is expected, not a gap. The useful output is the subset where
ng fixed a real bug we plausibly also have (candidate ADOPTs so far: audio-badge
stuck highlight, context-menu null-guard, indicator panel-contrast theming,
indicator user-package override) and the CHECKs where we must verify our own
behavior. Each ADOPT still needs a look at whether ng's implementation is
actually right before we take it.

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
