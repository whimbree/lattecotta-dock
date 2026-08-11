# Fork-sync pass report (2026-08-11)

Periodic reference-fork and plasma-desktop review per the CLAUDE.md sync
section. Read-only scout output, verdicts verified against this tree before
recording. Reviewed-through hashes updated in CLAUDE.md by the same session.

## 1. latte-dock-ng (ruizhi-lab)

New tip reviewed through: `a48c121d8` (previous `456154efb`; ~100 commits,
v1.2.25 through v1.2.38, full bodies read). Their `docs/` carried no new
analysis since the last sync (README/INSTALLATION/CHANGELOG only).

### Verdicts (grouped)

| ng commit(s) | Subject | Verdict | Reason |
|---|---|---|---|
| `1d051010c` (one hunk) | tasktools servicesFromCmdLine recursion guard | FOLD | Real infinite recursion, present verbatim in this tree (evidence below) |
| `9ee17a9da` | Include KPluginFactory in indicator package plugin | FOLD (low) | The indicator package plugin has the same transitive-include fragility (evidence below) |
| `db325cc4d` | Enable windows tracking for Cycle And Minimize | SKIP, already present | The BindingsExternal enabled binding already carries the exact condition |
| `606a1d22e` | Keep appmenu submenus open on Wayland (NeedsAttention) | SKIP + note | View::statusChanged already treats NeedsAttention like RequiresAttention (block-hiding only); see the idempotence note below |
| `07f254a18`, `a39f841ed` | AutoSize infinite loop (step-8 misalignment) | SKIP, immune | The AutoSize core here is the rewritten AutoSizeEngine; no 8px do-while exists |
| `8d345ab0f`, `f0c7fde15`, `fc852de4c`, `1500fc9f3` | Animation speed remap + shadow ghosting | SKIP, reject (speed) / immune (ghosting) | Speed mapping 0.75/1.0/1.15 and the button pairing are verbatim Qt5 Michail code in this tree; ng's remap is a reinterpretation and Qt5 is the spec. The shadow here is a layer effect, not a sibling MultiEffect copy |
| `1d31141a2`..`a7bfe1edd`, `591fed125` | Parabolic oscillation chain (5 attempts + test) | SKIP, solved differently | app/view/parabolic.cpp carries the 1ms nullifier plus stationary-pointer MouseMove dedup covering exactly their premise |
| `b42115115`, `cd155c705`, `b15b7d18b`, `5b3f54fa2` | Bounce-with-parabolic feature + its stuck-zoom fix | SKIP | The feature deviates from Qt5 (removes invkClearZoom); LauncherAnimation.qml:66 keeps Qt5's clear-zoom-on-launch, so their fix repairs a regression only their feature created |
| `64bdc60f7`, `429c242e8`, `ca689131b` | Widget-zoom-disable option and plumbing | SKIP | New non-Qt5 option, their-parabolic-specific |
| `79004a8ae`, `0ea1fe3e8` | Restore minimized group member before presenting | INVESTIGATE | The WindowCycler pins Qt5 requestActivate-only semantics; needs a live repro of KWin Wayland unminimize-on-activate on this stack |
| `ccbfe7608` | Skip phantom group entries cycling backwards | INVESTIGATE | No phantom filter in _snapshotGroupWindows/WindowCycler (deliberate Qt5 pinning); the trigger (surfaceless daemon toplevels, e.g. ghostty) is real-world |
| `ac30d8dce`, `9a0f91eed`, `c3d33d7cb`, `58a72b0c8` | Kicker cascading submenus stuck open (Plasma 6.6) | INVESTIGATE | Plasma-6.6 focus behavior; needs a live check with a kicker-style applet on this pin. Their event-driven pointer-window tracker is the reference if it reproduces |
| `8b3442167` | AllSecondaryScreens relocation ping-pong | INVESTIGATE (Phase 8) | Positioner::reconsiderScreen() case-1 primary enforcement has no inRelocationAnimation() guard (the state exists at app/view/positioner.h:47) |
| `5c0a6c2c5` | Applet loss on AllScreens->SingleScreen (destroy cascade) | INVESTIGATE (Phase 8) | removeClone -> GenericLayout::removeView -> destroyContainment -> containment->destroy(); whether the cascade can reach the original containment needs a multi-screen repro |
| `b16b22648` | Effects/shadows apply immediately in edit mode | INVESTIGATE (low) | The drawEffects binding has no !editMode gate (good), but their m_view->update() frame-commit finding may apply; live check: toggle blur in edit mode |
| `51b9de4aa` | Unmap views on session shutdown (KWin closeWaylandWindows) | SKIP + Phase 8 note | Their views are xdg_toplevels; these are layer-shell surfaces, which KWin's toplevel-close wait does not cover. Phase 8 logout checks should still verify no lingering xdg_toplevels (config windows) block logout |
| `0578527e1`, `d66a048ca` | Async D-Bus init, CPU/memory perf pass | SKIP, perf reference | initKWinInterface does blocking QDBusInterface introspection at startup (1-5ms class); optional perf item, not a defect |
| `f183f7356`, `dc4acad2c`, `cf799f68a` | Blur regression pair | SKIP | They broke then re-fixed their own blur gating; the gating here is upstream-shaped |
| `a2b305b18`..`452fa2d82`, `8d26e1e38`, `8d5f5e193` | Separator crash/jitter/wheel chain (14 commits) | SKIP | All against their C++ containment layoutmanager port; this tree keeps the upstream QML manager |
| `212984dcf`, `2bc11956f`, `44c5de34e` | Autostart phase-2 + retry + self-heal | SKIP (note) | Cold-boot compositor race mitigation; main.cpp here has deliberate autostart handling with journal-visible refusal, and NixOS systemd session ordering differs |
| `4a732a00c`, `324116dd7`, `fe626dd19`, `06e233a36` | i<10000 caps, LevelOptions reentry guards, snapshot loops | SKIP | Bandaid-cap style contrary to the root-cause discipline; concrete claims checked: LevelOptions mutual handlers converge, Corona::unload differs |
| `4674a4e2f`, `9c474bf88`, `d36b85ebf`, `9f3c0a32f` | Norm-violation campaign + regression + revert | SKIP | Their campaign introduced the layout-unload regression they then reverted; cautionary tale only |
| `70103ece0`, `e35cedd22`, `015df7fcf` | GCC15/Clang warning fixes | SKIP, verified clean | Their modeIsChanged() recursion typo: correct here (layoutscontroller.cpp:178 delegates to the model); their unused foundActive: not present in updateHints here |
| Plasma 6.3 / Debian 13 / Kirigami compat cluster | Old-platform compat | SKIP | This tree pins Plasma 6.5+ via Nix; no knscompat, no ecm_install_icons |
| deb/rpm/CI packaging cluster | Distro packaging | SKIP | Packaging here is Nix (Phase 11) plus the native-packaging legs already landed |
| `fbc47607a` | Filter cosmetic SVG warnings | SKIP | Log filtering conflicts with observability-first |
| releases/merges/chores | Housekeeping | SKIP | Their tree only |

### FOLD details with this-tree evidence

F1. tasktools infinite recursion (ng `1d051010c`): app/wm/tasktools.cpp:512
computes `firstSpace = cmdLine.indexOf(' ')`; lines 552-560 recurse via
`servicesFromCmdLine(_cmdLine.mid(firstSpace + 1), ...)` when the command
line is in TryIgnoreRuntimes. A single-token command line (no space,
firstSpace == -1) that is itself an ignored runtime recurses on the identical
string forever: stack exhaustion. The correct fix is a terminal condition
(nothing left to strip when no separator exists), not a cap.

F2. Explicit KPluginFactory include (ng `9ee17a9da`):
app/packageplugins/indicator/indicatorpackage.cpp uses
K_PLUGIN_CLASS_WITH_JSON (line 34) but includes only KPackage/PackageLoader;
the macro's header arrives transitively, the exact fragility that shipped
ng's indicator plugin without embedded metadata on Qt 6.8 moc. One-line
hardening matching the standard KDE plugin pattern.

### Already-present confirmations (no action)

- `db325cc4d`: BindingsExternal.qml windows-tracker enabled binding already
  contains the ScrollToggleMinimized condition.
- `606a1d22e`: View::statusChanged (app/view/view.cpp:1544) handles
  NeedsAttention and RequiresAttention identically, block-hiding only.
- Shadow ghosting: ItemWrapper.qml:449-466 uses a layer effect with an
  explicit comment, immune to the sibling-MultiEffect ghosting class.

### Idempotence note (filed with the appmenu continuation item)

statusChanged calls applyPanelFocusPolicy() on every containment status
change, which unconditionally runs setFlags, initViewFlags,
LS::setFocusPolicy and requestUpdate. If those are not value-idempotent,
entering NeedsAttention while an applet menu is open could churn the layer
surface (ng's appmenu bug class). Verify idempotence when the appmenu
sibling repo lands.

## 2. latte-dock-qt6 (CaptSilver)

No new commits; origin/main still `81384003` (fetch confirmed). Nothing to
evaluate.

## 3. plasma-desktop (vendored task-manager backend)

All six vendored files diffed against master. smartlauncher* and backend.h:
no upstream commits since 2026-07-14. backend.cpp carried three:

| upstream commit | Subject | Verdict | Reason |
|---|---|---|---|
| `da33d713` | Remove unused non-trivial variable | FOLD | The vendored copy has the dead `const QString resource = ...` at plasmoid/plugin/backend.cpp:474; identical hunk applies cleanly, keeps the vendor diff cheap |
| `8cdade3d` | Consolidate Meta+1-9 shortcuts into Backend | SKIP, N/A | Lands in upstream's setupShortcuts/dispatchActivateTaskAtIndex block, which the vendored copy does not carry; Latte owns Meta+N via its own global-shortcuts system |
| `606724cd` | Prevent detaching in range-based for-loop | SKIP, N/A | Both hunks are in functions absent from the vendored copy |

## Prioritized fold-in list

1. fix(wm): tasktools servicesFromCmdLine infinite recursion on a spaceless
   ignored-runtime command line (credit ng `1d051010c`); register as a
   defect in docs/tracking/known-defects.md.
2. chore(plasmoid): port plasma-desktop `da33d713` (drop the unused
   `resource` local in backend.cpp:474).
3. build(packageplugins): explicit KPluginFactory include in
   indicatorpackage.cpp (credit ng `9ee17a9da`).
4. Plan items to file (no code): the two WindowCycler INVESTIGATE repros,
   the kicker cascading-submenu check, the two Phase 8 multi-screen items
   (reconsiderScreen relocation guard, clone-removal destroy cascade), the
   edit-mode effects frame-commit check, the logout lingering-xdg_toplevel
   check, and the appmenu idempotence note above.
