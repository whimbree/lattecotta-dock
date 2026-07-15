# Session handoff

Rolling handoff for the next session to pick up without re-deriving context.
Last updated 2026-07-15. PHASE 8 IS OPEN - read its section in
docs/PORTING_PLAN.md first; every item is current there, several sections
below are now RESOLVED and kept only as archaeology.

## 2026-07-15: headless silent-Qt6-break sweep (worktree session)

- Full-tree sweep of the silent mechanical break families, each against
  its landed exemplar (Array.isArray, removed Qt enums, int pointer
  caches, string window ids, QQC2 shadowing, destroyed()-handler casts,
  QVariant list reads). Findings and all clean negatives are itemized at
  the end of Phase 10 in the plan; three fixes landed (f5ff449b wm
  numeric id tests, 2d28cda1 indicator style/glow buttons, 75780c64
  containmentDestroyed freed-memory edge read) plus contract-suite
  growth (98986641: destroyed()-demotion C++ contract, AbstractButton
  indicator shadowing, Qt.MidButton removal, int fraction truncation)
  and a windowinfowrap regression test.
- Everything verified headlessly only: build-check green on both
  WITH_X11 variants, all 8 ctest entries pass. THREE LIVE CHECKS
  PENDING, marked in their plan items: activeWindowMaximized engages
  with a maximized window touching a dock (also: a focused dialog keeps
  activeWindowTouching), the four indicator-config style/glow buttons
  apply on press, and dock deletion still slides out on its own edge.
- Session tooling note: the bare `qml` tool in the devShell runs
  offscreen probes silently (no console.log output reaches the
  terminal); use qmltestrunner through scripts/qml-interaction-tests.sh
  for headless QML probing instead - contract-style TestCases double as
  the probe and the pin.
- Session tooling note: build-check.sh's self-reexec into nix develop
  only triggers when cmake is absent from PATH; a host cmake without
  ninja fails the configure. Run it as `nix develop -c
  ./scripts/build-check.sh` from a bare worktree.
## 2026-07-15: headless sweep - loops, degenerate indexes, lifetime hazards

Systematic sweep of the ad9b823f/46dc83c5/fa02b887/52c2987b/d6d57e61
families instead of waiting for the next coredump. Eleven commits; the
Phase 10 sweep entry in the plan lists them all with mechanisms. Fully
headless session: nothing was driven live, so several fixes carry
"noted for the Phase 10 sweep" markers for live confirmation (activities
delegate with a stale activity assignment, config-view slide-out racing
dock removal, recreate-during-removal, sort persistence round-trip,
dock/panel type flip after applet removal).

Clean negatives from the sweep (checked, no defect, so the next sweep
does not re-derive them):

- QML while-loops all terminate: head/tail scans in AppletItem/BasicItem
  strictly advance past finite lists, ContextMenu's eliding loop bottoms
  out at empty text vs a non-negative limit, restoreLaunchers always
  splices two off per pass, wheel-delta loops step by fixed 120.
- C++ while(!empty) teardown loops all remove before/while iterating
  (corona unload relies on libplasma removing containments on destroyed
  - upstream-shared contract, comment already in place).
- All first()/takeFirst()/last() sites are isEmpty-guarded.
- QMutableListIterator removal loops (alternatives objects) only
  pointer-compare captured pointers, never dereference - the d6d57e61
  discipline holds there, same for the applet destroyed() handlers in
  containmentinterface (remove-by-identity only).
- tasktools/servicesFromCmdLine indexOf chains all guard or use
  QString mid/lastIndexOf semantics safely.

Guard inventory - silent guards found in the swept paths, each assessed,
NOT blanket-fixed (per CLAUDE.md a guard is a contract or a bandaid):

- templatesmanager templateName(): the .view.latte else-arm calls
  remove(ext,...) unguarded; ext=-1 would chop the last 11 chars.
  Unreachable today - both callers filter *.layout.latte / *.view.latte
  dir entries. Contract-by-construction; left as is, would need a loud
  warning if a third caller ever appears.
- importer nameOfConfigFile()/abstractlayout layoutName(): same shape,
  remove(-1,n) truncates one char off a non-.latterc name. Feeder is
  the import file dialog (filters .latterc); cosmetic wrong name at
  worst. Left as is.
- corona windowColorScheme(): no '-' in the dbus payload makes both
  halves the whole string. Tolerant parse of our own dbus signal;
  harmless, left as is.
- waylandinterface switchToNext/PreviousVirtualDesktop(): unknown
  current desktop resolves to position -1 which behaves as
  before-first (next goes to first, previous wraps/returns). Sane
  degraded behavior, left as is.
- tasksmodel add/move/restore guards read 'plasmoid && !contains' so a
  null plasmoid FALLS THROUGH: addTask would null-deref three lines
  later. Unreachable (the only caller is behind an 'else if (ai)'),
  but the guard shape is misleading - candidate cleanup, not a bug
  today.
- originalview removeClone(): '!cloned->layout()' arm drops the clone
  from m_clones without slideOut/removeView - silent leak path if a
  clone ever loses its layout while registered. Origin not proven
  headlessly; left, watch item.
- launchers Validator upwardIsBetter(): goal.indexOf(val) can be -1
  when current is not a permutation of goal, making splice insert
  before-last; the function is a direction heuristic so the answer is
  just suboptimal, no corruption. Left as is.
- AutoSize updateIconSize() maxLength<=0 early-return: deliberate
  contract, commented in place since ad9b823f (re-run wired via
  onMaxLengthChanged).
- fa02b887's liveness filter in Storage::importLayoutFile: still a
  band-aid by its own admission (deleter unidentified); NOT
  headless-drivable (needs a live corona), so the importerregression
  extension was assessed and skipped rather than faked.

Session tooling notes: the worktree build needed its own cmake
configure (nix develop -c cmake -B build -S . -G Ninja
-DCMAKE_BUILD_TYPE=RelWithDebInfo) before any target built; ctest picks
up the new indicatorfactoryremovaltest without extra wiring. KDirWatch
deletion delivery works offscreen and the whole factory suite runs in
~1.3s, so it is safe for the default ctest pass.

## 2026-07-12 late evening: comic hover crash + edit-mode chrome trilogy

- Comic hover SIGSEGV root-caused and fixed (9ea29eaa): parabolic zoom
  crosses comic's switchWidth/switchHeight, Plasma 6 AppletQuickItem
  swaps representations by itself, and CompactApplet's Qt5-style
  representation stealing (parent capture, cross-window popup
  reparenting) turned each swap into a scenegraph use-after-free.
  Fixed on the upstream plasma-desktop Plasma 6 pattern (plasmoidItem
  property, isLatteAppletContainer marker walk, clean release of the
  outgoing representation, popup nodes returned to the dock window
  before teardown). Isolation acquitted the clicked-effect MultiEffect
  and both applet layer sites first. NOTE: mid-investigation a careless
  marker insertion split the two-line isMarginsAreaSeparator expression
  and turned every applet into a margins-area separator (no zoom, no
  clicks, layout in pieces) while the QML gate stayed green - the gate
  cannot catch semantics, only compilation. Anchor multi-line
  expressions in full when editing.
- Edit-mode chrome trilogy (caught live: blueprint flash+vanish,
  rearrange dying instantly, dock stuck in edit visuals) root-caused as
  THREE stacked latent defects, none introduced today: stale persisted
  inConfigureAppletsMode (fb621102), chrome focus-grab racing its own
  assembly, and hideEvent running session-end work on transient hides
  (both 4a8ac480). Today's dozens of crash-repro kills armed the stale
  persistence repeatedly, which is why it LOOKED like a fresh
  regression. Full loop verified live including a headless drag
  reorder; fakepointer learned drag (c1ee9e2b) and lives at
  ~/.local/bin/fakepointer.
- Comic now survives hover but EXPANDS from zoom alone (popup opens
  with no click) - behavior question vs Qt5 filed in the plan.
- Still open, filed: startup 'No QSGTexture' warnings (48/run, source
  unidentified, family 7 by fingerprint), default indicator main.qml:92
  dead binding, right-click menu missing 'Applet Settings'
  (pre-existing per my recollection).
- Round two, desk-driven: the rearrange input mask was carved from a
  once-sampled, wrong rect (outer Button item stretched to 2544 wide
  after chrome retargeting) - a full-width stripe ate hover and drags
  across every applet's middle. Fixed by re-carving on the rect's
  notify signal and mapping the interactive chip (8be2b388). The
  'un-toggle exits edit mode' report did NOT reproduce with the
  settings pinned (focus-loss close excluded) - retest with a real
  mouse. Two new items filed: the outer-Button width stretch
  (mechanism unidentified, now inert) and the vertical-dock canvas
  header rendering off-surface (left dock rearrange unusable, chip at
  y=-552). The settings pin (sticker) button is the way to keep chrome
  alive during headless driving; fakepointer clicks never hold real
  focus.
- Round five, into the small hours of 2026-07-13 (36160c46, afefa442,
  04d8000c and their docs commits): rearrange drag drift fixed (int
  lastX/lastY truncating wayland's fractional pointer coords - the
  int-vs-fractional audit hint is in the plan; fakepointer drag learned
  multi-waypoint zigzags to prove it). Edit Dock wrong-view targeting
  fixed (stale per-view pointer to the chrome singleton; show path now
  requires parentView()==this and the recreation catch-up releases the
  previous owner). Vertical-dock rearrange fixed via the canvas content
  reload on retarget (cross-view binding stranding class, mechanism
  demonstrated on the header's frozen y). Applet context menus restored
  with the Configure entry available ALWAYS (my call): the root
  was Panel.qml's viewLayout discovery missing that Plasma 6
  containment roots ARE ContainmentItems, plus three layered defects
  above it - see the plan item for the full chain. Default indicator
  dead binding fixed. QSGTexture warning sources identified by config
  bisection (applet-shadow first frames + TaskIcon's three unlayered
  MultiEffects, which accrue during idle runtime - fix shape filed).
  STILL OPEN, in priority order: TaskIcon/CompactApplet effect-source
  layering (73da8400 completion), hover-modal inconsistency and
  mispositioning (#recipe in plan), edit-mode first-open latency,
  startup latency (measure without the gdb wrapper first), containment
  menu corner-placement watch item, left canvas geometry flip-flop
  watch item.
- Round six, autonomous (d619ae08, d98bff98, 69baabf0 + docs): my minimal
  preview recipe (hover System Monitor, glide to
  Firefox, preview ~370px left) root-caused past the deferred re-show:
  the timer path skipped preparePreviewWindow so content and anchor
  could interleave across tasks. show() now binds visualParent and
  content atomically at map time and cancels stale pendings; verified
  with glide at realistic rates AND a fresh-map screenshot with the
  preview dead-center (2395 vs icon 2394). Effect-source layering then
  landed (69baabf0): startup warnings 45 -> 7, ZERO accrual over 95s
  idle and over hover churn; residuals are first-frame bursts (~28
  applet-shadow, known lower-risk class). VISIBLE CHANGE: task
  hover-brighten/click effects render for the first time since the
  port - they were invalid-sampler no-ops before. The sudden
  hover brightening is restored Qt5 behavior, not a new effect. Badged-icon effect rendering still unverified (no badge app
  was running). Next in queue: hover-modal inconsistency in rearrange
  mode, residual ~40px preview offset during zoom dwell (live vs
  resting rect, refine d98bff98), then the latency items.
- Round nineteen (2026-07-14): the Latte->Plasma indicator switch
  crash root-caused and fixed (841c2ca4). The gdb harness earned its
  keep: two runs, identical stacks, and a register dump (rdi=0)
  proving SvgItem::setSvg received a NULL, not a dangling pointer -
  KSvg 6 dropped Qt5's if (m_svg) guard in updateDevicePixelRatio()
  (still unguarded at ksvg master; upstream report candidate, noted
  in the plan item). The null was plasma FrontLayer's svg:
  resources.svgs[0] evaluating before the QML->C++->QML
  setSvgImagePaths round trip lands; fix gates the group-arrow
  Loader on the svg existing (deterministic: Loader active binding
  subscribes before any arrow exists, connections fire in
  establishment order). Verified live: switch, round trips, cold
  start with plasma persisted, frames render; my indicator
  setting restored to Latte afterwards. METHOD note: the crash
  always killed the process before KConfig synced, so the persisted
  type stayed latte - which both kept the repro armed across runs
  and hid the cold-start arm of the same race until tested
  deliberately.
- Round twenty (2026-07-14): the child-env leak fixed for its worst
  arm (00a6766c). KIO source reading (v6.27.0) settled the mechanism:
  systemd >= 250 means every dock-launched app is a transient service
  whose Environment= is a verbatim copy of the dock's env - no KIO
  hook to scrub. QT_PLUGIN_PATH now never enters the dock's env:
  run-staged hands the two dirs over as LATTE_EXTRA_PLUGIN_PATHS and
  main.cpp addLibraryPath()s them (process-local, inert var for
  children; the codebase already had this hygiene pattern for
  QT_QPA_PLATFORM). Canaries all green: right-click menu, zero
  KWindowShadow failures, and a konsole spawned via the task menu
  verified clean by reading its transient unit's /proc environ.
  Residuals (QML2_IMPORT_PATH per-engine env reads, stage-first
  XDG_DATA_DIRS, empty QT_QPA_PLATFORMTHEME degrading dev-child
  theming) documented in the plan item as accepted with reasons.
  FIELD NOTE: dock task icons ACTIVATE existing windows - to test a
  real spawn use the context menu's Start New Instance and find the
  child via systemctl --user list-units 'app-*' (process names are
  nix-wrapped, pgrep -x misses them).
- Round twenty-one (2026-07-14 evening): the colorizer check became a
  three-layer excavation (1f835402). The filed double-draw suspicion
  was real but MASKED: applet colorizing has been a silent no-op since
  the port because MultiEffect.colorization multiplies the target
  color by the source's gray level (read multieffect.frag:84 at the
  pin) - dark text colorized towards a light scheme comes out dark
  again. Both reference forks shipped that substitute for Qt5's
  ColorOverlay and lost the feature without noticing. Fix:
  Qt5Compat.GraphicalEffects ColorOverlay (same-pin package added to
  flake LATTE_QML_MODULE_PATH + package.nix) with the shadow as
  layer.effect (the sibling ShadowedItem ghost-double-struck the
  colorized text, same class as c7c46226). METHOD notes worth keeping:
  a lime Rectangle proves a subtree paints; a raw ShaderEffectSource
  proves the texture pipeline; forced garish colors beat subtle real
  ones for pixel truth; and check WHAT CONFIG the previous probe left
  behind - my own From Window=Active dropdown click made applyColor
  follow the active window's #232629 scheme 17s into every run, which
  cost three probe cycles to un-confuse (that tracking WORKS despite
  the missing-KWin-script warning, incidentally). NEW plan item filed:
  LatteComponents.ComboBox popups render collapsed rows with invisible
  text (found while driving the Appearance dropdowns headlessly).
  Unexplained leftover: during two mid-session probe runs the dock
  window mapped but painted nothing for ~2 minutes; not reproduced
  under the final code (four clean sub-30s starts), so only noted.
  My config fully restored (no colorizing keys, shadows on) and
  verified rendering unchanged.
- Round twenty-two (2026-07-14 evening, verification only): the
  comic/WebEngine free-run under threaded is GONE on HEAD. First
  measurement was a false negative - the throwaway layout's active
  copy had LOST its comic applet during earlier repairs (grep the
  layout for plugin= before trusting a no-repro; with-comic.bak
  restored and left active). With the comic present: settled idle
  0.2-1.6%, QSGRenderThreads ~1%, over minutes. No code change made;
  plan item closed with the plausible absorbers named (dialog rework,
  kwindowsystem wayland plugin arrival) and a bisect pointer if it
  returns. Also observed: the 3-dock throwaway burns 40-100% CPU
  MAIN-thread for ~60-90s after start (render threads quiet) -
  layout/applet settling, not a render loop; noted in the item, no
  new plan entry.
- Round twenty-three (2026-07-14 night, desk-driven): the ComboBox
  collapsed-popup defect confirmed by hand and fixed (a302d742). Headless
  qmltestrunner probes did the whole bisect without touching the live
  dock: Array.isArray(control.model) is always false on Qt6 (model
  reads back as QVariantList), so all role lookups took the
  model[role] branch - undefined for array models. The pin's actual
  semantics, measured: arrays put roles on modelData only, ListModels
  on model[role] only. Fix resolves from whichever exists; regression
  test covers the three model kinds the config pages use. TWO traps
  banked: the interaction-test harness reuses _qmlstage, so component
  edits need a restage before the test sees them (a probe cycle died
  to the old file); and the ListModel case is what proved the obvious
  modelData-only fix wrong - test all model kinds, not the broken one.
- Round twenty-four (2026-07-14 night, desk-driven): the intermittent
  settings-window overflow root-caused and fixed (1b932ed9) watching
  the repro live on screen as it ran. Method: a geometry-sampling loop
  (dumpwins every 300-400ms) around cold restarts vs warm reopens -
  warm was 12/12 correct, cold mapped +99px too tall (93px past the
  screen top) and STAYED wrong. The 99px equals the dock's own
  reserved thickness; corona integrates the view's own reserved area
  ~14s post-start (measured), the warmed chrome computed
  availableScreenGeometry before that, and upstream d30143f7's
  self-origin exclusion in updateAvailableScreenGeometry dropped the
  correction forever. Fix accepts self-origin updates (commented
  deviation). Verified across three cold runs after the fix: stale
  mapping lasts <1s then snaps correct; confirmed on screen: no overflow.
  RESIDUAL filed in the plan item (Phase 8 family): the <1s stale
  flash exists because corona's rect itself is late - the real lever
  is why reserved-area integration lags ~10s behind first paint.
- Round twenty-five (2026-07-14 late night): the reference-fork sync
  pass (d67e635a, df1c14da). Full record lives in the Phase 10 SYNC
  PASS plan item; the headline: one real fold from ng
  (alternativeshelper _plasma_graphicObject -> itemForApplet; "Show
  Alternatives" had been silently dead), qt6 woke up with a 54-commit
  testability campaign with nothing foldable verbatim, and their one
  behavior fix repairs a mapping mistake we never made - our
  layershellmappingtest comment already documents rejecting their
  layerFor(WindowsGoBelow)=Bottom. LESSON re-earned mid-pass: I
  drafted the same four-site explicit-mode change their fix makes,
  then found our own test file documenting why it is unnecessary
  here and REVERTED before commit - check what this tree already
  decided before folding a fork's fix for the fork's own bug. Also:
  add-via-drag ticked (hand-verified; ownership single by
  construction), missing "Show Alternatives" MENU ENTRY filed (the
  restored handler is unreachable until it exists), fakepointer
  needs no extension for drag-and-drop (drag is
  press/waypoints/release), GUI-CI candidate tests banked in the
  Phase 10 e2e item pending the planned microvm.
- Round twenty-six (2026-07-14, spilling past midnight): applet popup
  sizing fixed to the Plasma 6 contract (437d9a0c, landed before this
  round was written up). CompactApplet.qml now mirrors libplasma
  v6.6.5 appletpopup.cpp LayoutChangedProxy: the full representation's
  implicit size is the live base, Layout.preferred* only an override,
  Layout.minimum* enforced. Verified live on the volume applet
  (260x152 clipped -> 260x491 correct; the width sits at plasma-pa's
  own 252px minimum). IMPORTANT non-bug: plasmashell renders the same
  applet wider because of MY persisted custom size there
  (popupWidth=457 under the systemtray group in its appletsrc), not a
  different default - do not chase that delta again. The follow-up
  regression sweep (reopen volume + calendar, native-res screenshots)
  was attempted headlessly and ABORTED: I was at the desk actively
  using the machine, so the real mouse and fakepointer fought over
  hover/focus, and a NEW trap surfaced - spectacle's own task icon
  joins the dock during capture and shifts applet positions ~50px, so
  any locate-then-click that spans a spectacle run mis-targets (one
  click landed on Dictionary instead of the volume applet this way).
  Sweep evidence stands anyway, from my own hands: calendar and
  dictionary popups open SIMULTANEOUSLY on this build during the
  session, both rendered and positioned correctly off the dock. Two
  new bugs filed in the Phase 10 stabilization list with full
  investigation state: widget removal leaves a multi-second ghost
  slot in rearrange mode (C++ removal already immediate per 33830b2c,
  lag is downstream - repro ONLY on the throwaway layout), and
  edit-mode background opacity is not WYSIWYG (~half opacity at
  Opacity=100% in plain edit mode; blueprint vs real-background chain
  mapped, decisive next step is reading the Qt5 original main.qml
  from this repo's own history - CLAUDE.md names this exact area as a
  fork trap).
- Round twenty-seven (2026-07-15 small hours, desk-driven, I was at
  the machine and confirmed fixes live): both round twenty-six bug
  filings root-caused and fixed the same night. Edit-mode opacity
  (38e60eb9): the blueprint drew OVER the dock background; Qt5's
  composite is wallpaper > grid > background > applets, and my
  persisted editBackgroundOpacity=0.5 was exactly the "half" wash -
  the fix is child reorder in DragDropArea, the opacity chain itself
  was already byte-identical to Qt5. Removal ghost (71b0d75a):
  libplasma askDestroy() guards its immediate appletRemoved with
  !isContainment() and the systemtray IS a containment
  (CustomEmbedded), so its only appletRemoved comes from inside
  ~Applet when the undo window ends - measured 59.8s ghost pre-fix
  (destroyedChanged at click+159ms, appletRemoved at +59.8s = the
  libplasma 60s fallback timer), first-frame collapse post-fix,
  hand-confirmed ("it instantly deleted this time"). Fix restores
  Qt5's two-phase parking keyed to destroyedChanged, with undo
  restore-in-place and a duplicate-container guard; the undo arm
  still wants one live Undo click (popup auto-hides faster than a
  headless locate-then-click loop). METHOD notes: read the pinned
  libplasma tarball straight out of /nix/store (no checkout needed);
  the spectacle task icon shifts dock applet positions DURING
  capture, so never locate-then-click across a spectacle run; the
  rearrange-toggle window position varies between edit sessions -
  read it from dumpwins (it is its own 133x77 latte window), not
  from a previous session's coordinates. THROWAWAY layout note: the
  active copy was restored from with-comic.bak twice during the
  repro; it ends the session WITH its systray back. My real dock was
  NOT restarted back to --user-config yet at write time - do that
  (or I do it by hand) before daily-driving resumes.
- Round twenty-eight (2026-07-15 late night, desk-driven, I reported
  and co-drove throughout): the "comic stopped rendering" and "dock
  bar is white in dark mode" reports both dissolved into precise
  causes, none the feared commit regression. Comic = THREE findings:
  the black-disc icon was the 1f835402 colorizer flattening a
  full-color icon under Dark Colors (my palette flip-back proved it
  live; colorizer SCOPE defect filed - Qt5 rules need reading); the
  dead hover was the comic applet's own provider list with xkcd
  UNCHECKED (I found it in its settings; checking it fixed hover on
  the spot) - near-certainly reverted by the two with-comic.bak
  restores during the removal-ghost repro, so BEWARE: re-activating
  a layout backup silently reverts APPLET config too; and the probe
  runs surfaced a real latent defect, fixed (1aa5238c): the 9ea29eaa
  release path hid unwired full representations while libplasma's
  inline representation switch (applet grown past
  switchWidth/switchHeight) re-uses the cached item without ever
  re-showing it - the comic's full rep sat parent=null visible=false
  after startup churn, measured. Release now detaches to a null
  parent, visibility untouched, upstream-symmetric. White bar =
  build/_runconfig has NO kdeglobals/plasmarc, so every throwaway
  run resolves the default LIGHT scheme; the dark-bar memory is from
  --user-config runs (decisive test queued: restart --user-config
  and look; seed the throwaway with kdeglobals regardless). Dark
  Colors NOT darkening the background strengthens the dead
  background-arm suspicion in that item. NEW items filed:
  applet-created dialogs (comic's full-size viewer) open on the
  wrong screen (screenshot evidence, Phase 8 family), and a
  background color picker as a continuation feature (Qt5 has none).
  Session state: throwaway layout active with the comic working
  (xkcd checked), my real dock still not restored to --user-config;
  the removal-ghost undo arm still wants one live Undo click.
- Round twenty-nine (2026-07-15, deep into the night, desk-driven with
  me duplicating docks live while the probes ran): the duplicated-dock
  overlap fixed (e412889d). The long road mattered: config was proven
  clean, restore() proven correct, plain duplicateView proven clean,
  screen-move proven clean - the trigger is ONLY the live formFactor
  flip, and the defect is mouseHandler's width/height bindings dying
  in the tasks plasmoid while every input updates around them
  (measured: vertical=false, icList=592x88, mask=88, mouseHandler
  frozen 88x592). Fix is the afefa442-class binding reassert on
  orientation change; the destroyer itself is an open watch item in
  the plan entry. METHOD kit built along the way and worth reusing:
  a repeating containment-side geometry probe (per-layout kids with
  scene mapping, wrapper geometry, per-applet formFactor and Layout
  hints) plus a tasks-side value probe, and a fully headless repro
  recipe - dbus duplicateView, chrome cycling by canvas-window
  geometry, screen dropdown and edge-button coordinates for the
  pinned settings window. removeView/duplicateView over dbus went
  incident-free this time (ids verified against the layout file
  before every removeView). ALSO seen and filed nothing new: the
  1096x527 layer-6 chrome window on DP-3 appears when the settings
  chrome opens while Advanced is on and can linger - matches the
  Phase 8 'secondary advanced-mode window not covered' note; fold it
  into that work. Session state at close: throwaway active with the
  user's own re-made clone at DP-3 bottom (healthy), all my test
  clones removed, tree clean and pushed; my real dock still awaits
  the --user-config restart, which doubles as the dark-bar
  discriminator; the undo-click verification on widget removal
  remains queued.
- Round thirty (2026-07-15 ~01:00-02:30, autonomous drive, I am
  remote-monitoring): the defect-class initiative started. All 314
  post-fork commits classified into eleven defect classes (recorded
  in the round-twenty-nine-adjacent conversation and the wave
  charters); tests/contracts stood up (fadd9012) - the suite that
  pins libplasma/KSvg/Qt behaviors Latte relies on so pin bumps fail
  in ctest instead of misbehaving in the dock; seeded with the Qt6
  QVariantList round-trip contract. THREE WAVE-1 SUBAGENTS running
  in isolated worktrees at write time: mechanical Qt5->Qt6 breaks
  (+int-truncation audit), effect/texture-source contract sweep, and
  loops/degenerate-values sweep - each also writes regression tests
  for landed fixes and contract tests for its class; none may touch
  the live dock. Merges and live verification happen serially after
  they report. MEANWHILE the color complex CLOSED (79ca3360): the
  dark-bar report, the Dark Colors "dead background", and the black
  silhouette were ALL the throwaway's missing kdeglobals degrading
  themeExtended's variant resolution - proven by a live Dark Colors
  flip on the real config (background darkens, applets flatten
  LIGHT, tasks stay full color, all Qt5-faithful per the Qt5 source;
  palette restored after). run-staged.sh now seeds the throwaway
  config with the session kdeglobals. Colorizer scope item closed as
  not-a-bug. TRAPS learned: removeView rides the SAME 60s undo
  window as widget removal - restarting inside the window resurrects
  the containment (verify the config group is gone before
  restarting); stuck chrome popups (Type combo, secondary advanced
  window) linger across sessions and EAT CLICKS aimed at the
  rearrange toggle - filed as a new chrome-lifecycle item. The undo
  click on widget removal remains for hand verification (chrome
  fighting made it unreliable headlessly). Session state: real dock
  back on --user-config, palette default, tree clean and pushed.
- SESSION CLOSE STATE (2026-07-14 night): everything committed and
  pushed; working tree clean; my dock runs the latest build
  under the gdb wrapper with --user-config, config fully restored
  (Latte indicator, no colorizing keys, shadows on). The 2026-07-13
  queue is CLEARED: indicator switch crash fixed (841c2ca4), child
  QT_PLUGIN_PATH leak fixed with residuals filed (00a6766c),
  colorizer restored end-to-end (1f835402), comic/WebEngine free-run
  verified gone (docs only). The throwaway 3-dock layout has its
  comic applet back (with-comic.bak made active again). OPEN, in
  rough priority: the settings-window-overflows-screen-top report
  (needs my repro - which monitor/mode; plan item filed),
  the LatteComponents.ComboBox collapsed-popup defect (plan item
  with symptoms), startup latency phase 2, and the Phase 8 backlog.
- SESSION CLOSE STATE (2026-07-13 night): everything committed and
  pushed through 1a49f118; working tree clean; the dock runs the
  latest build with --user-config (my REAL ~/.config, single
  bottom dock - their Jul 10 layout). NOTE: plain restart-staged.sh
  without --user-config loads the THROWAWAY 3-dock test layout from
  build/_runconfig (which contains two comic/WebEngine applets that
  free-run under the threaded loop - see that plan item). Apps
  launched from the dock inherit its env; --user-config keeps their
  profiles correct (the spotify lesson), but QT_PLUGIN_PATH /
  QML2_IMPORT_PATH / stage-first XDG_DATA_DIRS still leak to
  children - filed as a dev-harness item... actually not yet filed:
  NEXT SESSION should file or fix it (child-env scrubbing).
  KWin state restored: fade loaded, SlideInTime default, all probe
  scripts unloaded. NEXT UP, in order: the Latte->Plasma indicator
  switch CRASH (plan item with repro recipe, use the gdb harness),
  colorizer double-draw, comic/WebEngine spin, the corona
  double-windowAdded curiosity (mixed single/double adds per popup
  open - benign for the slide but unexplained).
- Round eighteen, the slide saga SOLVED (1f8770fd, confirmed live: 'it slides
  in now!!'): the final layer was PlasmaQuick::Dialog deriving its
  own slide hint from location INSIDE its first-expose handler, which
  on the threaded loop blocks until the mapping frame is committed -
  the one moment no external re-assert can reach. Floating location
  = unset every open. Fix: pin location to the dock edge for
  AppletPopup dialogs in updatePopUpEnabledBorders; the base then
  slides it natively (and hides the dock-facing border, Qt5 look).
  LESSON worth its weight: when a base class acts inside a
  frame-blocking call, the ONLY winning move is changing the inputs
  its own computation reads, not racing its output. The double
  windowAdded lead was a red herring (mixed single/double adds,
  orthogonal). Our Dialog updateSlideEffect machinery stays as
  reinforcement for surface-recreation cases.
- Round seventeen, the popup slide saga (347f413a, f630d2ad; slide-in
  STILL OPEN, full evidence chain + leads in the plan item): the goal is
  plasma-parity popup animations - popup must emerge from
  behind the dock like plasmashell's from their panel. Banked along
  the way: the staged env was missing kwindowsystem's wayland plugin
  ENTIRELY (dialog shadows had silently never worked - allow-listed
  the exact linked package's plugin dir in run-staged), and applet
  popups now carry the appletpopup role (slide-OUT works,
  hand-verified). Slide-in remains eaten by the scale effect; the
  hottest lead is a DOUBLE windowAdded per open seen by the KWin
  probe. HARD-WON METHODOLOGY NOTES: popupWindow=false is CORRECT
  for appletpopup-role windows (wasted a probe cycle); client-side
  slide offsets computed pre-map are garbage that make the effect
  clip the animation invisible - always send -1; KWin effect
  slow-motion (SlideInTime=1500) + selective effect unloading are
  cheap discriminators for 'which effect animated that'; a KWin
  windowAdded script probe beats theorizing about classification;
  and READ upstream sources at the pinned tag before hypothesizing
  about them (three ordering theories died to source reading).
  Also new: audio-badge volume wheel now matches plasma-pa exactly
  (299a241b, hand-verified) and fakepointer has a scroll subcommand.
  QUEUED NEXT: the Latte->Plasma indicator switch crash (gdb harness
  item in the plan), the colorizer double-draw check, comic/WebEngine
  free-run under threaded.
- Round sixteen, threaded render loop (28fdca5f): the bar was set -
  'behave like plasma where possible' - and reported jittery
  per-task volume scroll plus a chugging calendar popup. Both traced
  toward the basic render loop: plasmashell runs threaded here (8
  QSGRenderThreads), latte ran basic (a stabilization-era default
  whose own commit said revisit after the effect audit). Measured:
  calendar interaction under basic = 417-610ms polish-dominated
  frames serialized on the gui thread; under threaded = p50 5ms, p99
  6ms, max 41ms, zero over 100ms. All historical crash recipes clean
  on threaded (the effect audit removed the churn that raced).
  CAVEAT filed: comic (WebEngine) applets free-run under threaded at
  ~20%/instance - bisected precisely (comic-hosting docks spun,
  comic-free layouts idle at 0.1%); basic stays as env override.
  OPEN: the volume-scroll jitter needs a retest under threaded, and
  if it persists, compare our per-task wheel path against plasma's
  volume applet (wheel accumulation/debounce) - fakepointer has no
  wheel support yet, add a scroll subcommand for headless driving.
- Round fifteen, corona init profiled (docs only): the 2.4s corona
  estimate was wrong - it is ~1.0s; the real pre-paint cost is the
  first view's 2.6s create-to-map pipeline (QQmlTypeLoader floor plus
  one-time inits gdb-sampled inside applet instantiation: ICU
  calendar data, notification-service DBus registrations). Tried an
  off-thread ICU pre-warm (QLocale::toString) - measured a no-op
  (identical stall pattern; Qt uses its own CLDR tables, the applet's
  ICU entry is elsewhere) and REMOVED it instead of shipping it.
  Startup verdict: 3.7-4.5s to first painted dock, remainder owned by
  upstream synchronous applet loading - local work here is done.
- Round fourteen, chrome warmup (fd8cbc45): first Edit Dock 7.3s ->
  1.5s. The corona builds the settings/canvas ensemble hidden 8s
  after synchronizer init. TWO traps documented for posterity:
  PrimaryConfigView's constructor showed AND started a configuring
  session via setParentView() (warmup attempt one flash-showed and
  self-closed through the focus machinery - constructor gained
  showOnCreation=false); and setParentView() early-returns on an
  unchanged parent, so the factory path in
  View::showConfigurationInterface never showed the warmed-hidden
  ensemble - first click was a SILENT no-op (explicit
  showConfigWindow() there now, gated parentView()==this). Also
  learned: qDebug through a redirected stdout FILE is block-buffered
  - a 'silent' log during idle means unflushed buffer, not a hung
  process; check D-Bus responsiveness before assuming deadlock.
  Watch item: first open on a non-warmed view (retarget from
  never-shown state) was not driven headlessly.
- Round thirteen, staggered startup (70fe5390): initContainments now
  queues one view creation per event loop pass. First dock visible at
  exec+3.7s (was 4.5s), all three at 6.3s (was 5.3s) - the promised
  ~2s did not materialize because ~2.4s of corona init precedes any
  view creation; the stagger only removes waiting behind sibling
  views. Verified: geometries identical, previews/menu/edit-chrome
  all work, ctest green (appstreamtest failure pre-existing without
  the change). If the slower last-dock feels worse live, the revert
  is exactly one commit. NEXT startup lever if wanted: profile the
  ~2.4s corona init (theme mask parsing was one visible chunk).
- Round twelve-c, ghosts finished (c7c46226): the per-side rect fix
  was not enough - it re-reproduced by hand on the clock. The sibling
  shadow pattern (ShadowedItem Loader sampling a still-visible
  original) draws content twice and trusts MultiEffect to place its
  padded copy pixel-exactly; it lands a few px off, double-striking
  text applets while icons just look slightly bold (which is how my
  full-screen visual pass missed it - RULE: verify text applets at
  NATIVE resolution, imagemagick via 'nix run nixpkgs#imagemagick'
  since the host has no crop tool). ItemWrapper and ShortcutBadge
  shadows are now layer.effect - double-draw impossible by
  construction. Colorizer's shadow keeps the sibling arrangement
  (samples wrapper while the colorizer MultiEffect draws) and is a
  filed suspect: in colorizing mode it likely draws an UNCOLORIZED
  wrapper copy over the colorized one; its comment assumes
  ShadowedItem draws only the shadow, which is false - MultiEffect
  always draws source content plus shadow. Idle CPU still 0.1%.
- Round twelve-b, the ghost regression (6c7001ce): e3376405's static
  paddingRect used Qt.rect(pad,pad,2p,2p) assuming width/height were
  totals; Qt's updateSourcePadding() defines them as PER-SIDE extras
  (left/top/right/bottom), and the shader's centerOffset scales with
  (x - width), so the asymmetric rect drew a smaller offset ghost of
  every applet inside itself. Caught by eye within minutes of the
  build going live. Fixed to Qt.rect(pad,pad,pad,pad); the test now
  pins all four sides equal. LESSON: MultiEffect verification needs a
  VISUAL pass, not just CPU numbers and green tests - and when Qt
  docs are vague, read the Qt source before shipping (the source also
  revealed autoPadding pads 256px per side around every applet, an
  extra reason the static rect is right). Storm numbers unchanged
  after the correction: 0.1% idle, shadows on.
- Round twelve, the storm KILLED (e3376405): three probe builds
  bisected it to MultiEffect's autoPaddingEnabled - it recomputes
  padding and re-dirties the effect every frame, so every
  shadow-carrying window rendered empty frames forever. ShadowedItem
  now uses a static paddingRect (blur + max offset per side; output
  identical). 18.2% idle CPU -> 0.1% with shadows fully on. LESSON:
  QSG_RENDER_TIMING is the sharp tool for who-renders (named both
  spinning windows and showed the frames were EMPTY - polish=0 sync=0
  render=0 - which pointed at a dirty-loop, not real work). RULE: no
  autoPaddingEnabled on MultiEffect anywhere in this tree; compute
  static padding (the new tst_shadoweditem contract enforces it for
  ShadowedItem). Startup and cold edit-open unchanged after (their
  costs are real work); the win is idle CPU, power, and scheduling
  headroom.
- Round eleven, the storm (docs only, evidence in the plan's TOP
  PRIORITY item): while measuring edit-open latency (cold 7.3s, warm
  0.5s), found the idle dock burning 18.2% CPU on the main thread -
  ~19,500 failing statx/s, flat, even with all docks dodge-hidden -
  and 1.2% with applet shadows disabled. The scenegraph renders every
  frame forever (idle gdb samples in QSGBatchRenderer), and each
  frame re-resolves two nonexistent theme colors files across a
  273-entry NixOS XDG_DATA_DIRS. The eternal-frame requester is in
  the applet-shadow path and is THE next thing to hunt - it taxes
  every latency number measured tonight. Full evidence chain and
  attack plan in the plan item. Practical field notes for that hunt:
  ptrace_scope=1 means gdb must be the parent (batch file with
  multiple continue/bt rounds works, SIGINT from outside); strace as
  parent slows the dock enough that dodge-reveal stops working, so
  measure idle-only under strace; QSG_VISUALIZE tints hidden layer
  surfaces black over the whole screen - useless and user-visible.
  The edit-open cold cost itself is chrome instantiation + Kirigami
  theme cascade (stack captured), but re-measure after the storm fix;
  pre-creation options and risks are filed in the latency item.
- Round ten (c622da1b): the ~40px zoom-dwell preview offset closed by
  DELETING d98bff98's resting-anchor machinery and restoring Qt5's
  live anchor (tooltipVisualParent). The resting anchor only ever
  existed because the dialog could not reposition after map; with the
  recompute-fresh Dialog the live anchor is strictly correct and 87
  lines of settle-delayer machinery went away. Dwell preview center
  matches the zoomed icon center exactly now. The whole popup saga
  (parking, interleave, dwell offset, rearrange modal) is CLOSED -
  one wayland root (mapped popups cannot be repositioned by
  QWindow-level calls) wearing four costumes. When a workaround's
  reason disappears, delete the workaround - do not refine it.
- Round nine (8f821310): 77aac4b4 confirmed with a real mouse.
  The rearrange hover-modal inconsistency then fell to the same class:
  ConfigOverlay's tooltip was a stock PlasmaCore.Dialog with a
  visualParent binding hopping between hovered applets while mapped -
  the window parked at the first hover's spot with only its width
  following (reproduced live at 2122 while hovering an applet at
  3290). One-line class swap to LatteCore.Dialog plus edge, comment at
  the site. When a mapped popup's anchor must move on wayland, it must
  be a Latte::Quick::Dialog - PlasmaCore.Dialog cannot do it. Grep for
  other PlasmaCore.Dialog uses with mutable visualParent if a similar
  'appears in the wrong place / does not appear' report shows up.
- Round eight, the previews bug's real ending (77aac4b4, dbe5a03b):
  re-reproduced by hand against d619ae08 with the same recipe. Full
  instrumentation of QML show() plus every C++ position push caught
  it: the dialog maps with the PREVIOUS task's content width, the base
  recenters on the resize, then libplasma's stale-position re-send
  reverts it - and the old stored-position counter-measure was armed
  only by the mainItem-resize hook, which fired for every intermediate
  stop of the sweep but not the final one. Ordering-dependent
  correctness, which is why three fixes passed synthetic retests and
  kept failing under my hand. 77aac4b4 removes the stored
  state entirely: recompute the anchored target fresh from current
  anchor + pending content size after every Move/Expose/Show and push
  it; any ordering self-heals next event. Clean-build verified, recipe
  plus bidirectional sweeps, idle-silent. Also fixed: the
  layershellmappingtest had been failing to COMPILE since the
  canvasInputRegion dockStrip change (dbe5a03b) - run ctest, not just
  the build, after C++ signature changes. Remaining preview polish:
  the ~40px zoom-dwell offset (separate filed item).
- Round seven, latency (37acf9ca): startup measured honestly (KWin
  window poller, no wrapper, no -d; the -d run matched within 50ms so
  -d timings are trustworthy). Restart was ~9.4s: ~4.1s launcher
  (nix develop 3.0s dominating) + 4.5s binary to first dock window,
  5.3s to all three. Launcher fixed: restart-staged sources a cached
  print-dev-env snapshot (build/_devshell.env, auto-refreshed on flake
  changes), warm path measured 0.57s. Binary phase attributed by a
  mid-stall gdb interrupt (ptrace_scope=1 on this host - attach is
  blocked, run gdb as the parent and SIGINT the dock, the repo's
  gdb-batch-cmds documents this): main thread parked in
  QQmlTypeLoader waits under PlasmaQuick's synchronous per-applet
  itemForApplet, three views built sequentially in one synchronizer
  callback (synchronizer.cpp:694). Filed lever: stagger view creation
  per event-loop cycle (earlier first dock, total unchanged) - NOT
  implemented, view creation order is Phase 8 territory. Corrected a
  wrong suspicion: restaging does NOT invalidate the QML disk cache
  (cmake --install preserves mtimes; 235/237 entries reused), so
  edit-mode first-open cost is in-engine type instantiation - warm-up,
  not qmlcachegen, is the candidate fix. Benign oddity explained:
  latte-dock windows 4/5 appear ~18.5s post-start in every run (lazy
  ToolTipDialog/KlipperPopup), not pointer hover.
- Round four, decisions and mapping (e70bccf7, e85e18d8, 98b7419e):
  parabolic zoom disabled for ALL of edit mode as a deliberate call
  (deliberate Qt5 deviation, comment at the site). The missing 'Applet
  Settings' menu entry got its architecture mapped: Qt5's
  ViewPart::ContextMenu was removed in the port (d3538eee), the canvas
  ContextMenuLayer replacement has correct composition but dead
  applet-under-cursor resolution (direct-children method lookup misses
  Plasma 6 wrappers; cross-window fallback; use-before-null-check at
  :229), and dock-window right-clicks reach the containment-only
  containmentactions plugin. Decided: configure entry belongs in
  the menu ALWAYS (matches Qt5/plasmashell). Also filed: Edit Dock
  wrong-view targeting (stale singleton chrome shows before retarget,
  observed in the 20:53 trace and twice by hand), edit-mode
  first-open latency (cold QML compile suspect), startup latency
  (measure without the gdb wrapper and -d before optimizing). Left
  dock rearrange remains dead pending the off-surface header item.
- Round three, the design-level one (3d714d63): plain edit mode
  blocked ALL widget interaction because the canvas owned its whole
  surface. Qt5/X11 stacked the edited dock above the canvas; wayland
  cannot restack same-layer surfaces, so the canvas input region now
  excludes the dock's absoluteGeometry permanently. Verified: full
  context menu on a task in plain edit mode, wheel tooltip confined to
  the blueprint margin. The reason this evening felt like whack-a-mole
  is now on record: five independent latent defects (persistence,
  focus race, transient-hide session kill, stale mask rect, canvas
  input ownership) stacked on the same user-visible surface; each fix
  was real and verified, the last one was architectural. Remaining in
  this area, filed with recipes: popup mispositioning during fast
  pointer movement (parabolic anchoring race), vertical-dock canvas
  header off-surface, outer-Button stretch mechanism, missing 'Applet
  Settings' context-menu entry (pre-existing).

## 2026-07-12 evening: skill library authored, tested, committed

- .claude/skills/ now holds seven skills (latte-architecture, latte-build-env,
  latte-conventions, latte-debugging, latte-fork-sync, latte-live-verification,
  latte-plasma6-defect-families). They are the distilled operating knowledge of
  this port; new sessions should lean on them instead of re-deriving.
- Tested before committing, four ways: (1) three independent accuracy audits
  covering roughly 240 objective claims (every cited path, symbol, line number,
  commit hash and quoted metric checked against the repo; zero wrong or stale
  claims, a handful of imprecisions found and fixed); (2) executable checks
  (qml-compile-gate 0 of 128 failures, all four cmake targets confirmed, the
  plasma-desktop curl diff and both fork range commands run verbatim);
  (3) a live smoke test of the whole latte-live-verification recipe (restart
  under the gdb wrapper, three docks mapped at layer=3 in dumpwins, D-Bus
  surface introspected, edit mode opened on first invoke, spectacle capture,
  clean stop; machine left dock-free as found); (4) six Sonnet-class usability
  probes run against realistic scenarios with only the skills as guidance -
  five clean passes, one wrong "stale binary" diagnosis that led to a new
  staleness-check paragraph in latte-build-env (plugin sources link into their
  own .so, never mtime-compare against build/bin/latte-dock; trust the no-op
  build's "no work to do").
- Environment change: fakepointer now lives at ~/.local/bin/fakepointer
  (was a transient prior-session scratch path); fakepointer.desktop Exec
  updated, sycoca rebuilt, injection verified working.
- Fork-sync pass is DUE: latte-dock-ng has ~10 unreviewed commits past
  59e04b8b7 (including real fixes: middle-click restore 613ddcc3b, duplicate
  parabolic zoom removal f0f65f4e3) and latte-dock-qt6 has 54 past 9003f33a
  (large test/refactor push). CLAUDE.md's "less active" note on qt6 is stale.
  Run the latte-fork-sync skill end to end next session.

## 2026-07-12 sweep (all committed, ad9b823f..1aa97b47)

- iconSize=78 startup hang FIXED (ad9b823f): the autosize shrink loop's
  '!== 16' exit stepped past its floor for any size not 16 mod 8, spinning
  forever once wayland's unsized-window start made the length limit
  unreachable. THE USER'S REAL LAYOUT STARTS AGAIN (verified in the
  throwaway config; --user-config is safe for it now).
- Applet private QML modules FIXED (4c9f3bc7): owning packages joined the
  flake's pinned LATTE_QML_MODULE_PATH; nine failing modules -> zero, the
  context menu survived (shadowing regression check), widget explorer
  previews all render.
- Comic Strip churn crash: NO LONGER REPRODUCES - the churn vector WAS the
  missing private module. CompactApplet hardening stays filed, not
  crash-backed (2edb1796).
- QQC2 'indicator' property shadowing FIXED (33fa17d7): indicator config
  checkboxes read AND wrote the control's own check-glyph object, silently
  dead since the QQC1->QQC2 port. Watch for this class anywhere a context
  property collides with a QQC2 control property name.
- Edit-chrome canvas-follow FIXED, two stacked causes (1607d022 surface
  remap in the LS helpers, c5bdc239 Containment::screenChanged resync).
- KDE_COLOR_SCHEME_PATH pinned per view window (a774ee55, ng's 9fe135422
  mechanism); Kirigami colorSet half deliberately deferred until a live
  divergence reproduction exists.
- Applet config sync FIXED AT THE ORIGIN (c3d15966): it had NEVER
  established for any applet (dead indexOfProperty probe on
  AppletQuickItem); now reads the CONSTANT configuration Q_PROPERTY off
  the Applet. org.kde.sync storms: gone. The handoff's duplicate-dock +
  add-widget crash NO LONGER REPRODUCES (driven end to end under gdb).
- Vendored backend: provenance stamps in plasmoid/plugin, plasma-desktop
  added as a sync-diff target in CLAUDE.md, Phase 12 outreach reframed for
  the maintained-continuation reality (docs/taskmanager-integration-research.md
  has the vendor-vs-integrate analysis; ng's narrowing is the counter-example
  and will not be repeated).

Still open from this sweep (all filed in the plan): duplicated-dock applet
ORDER retest on a rearranged layout (default-order layouts cannot exhibit
it - use my real layout, interactive), the at-the-desk settings
control walk (TaskMouseArea's 9 TaskAction values especially), folder-view
calling corona.isScreenUiReady (shim vs noise decision),
BindingsExternal.qml:281 localGeometry-of-null startup transient, and the
day-one leftovers (lock/unlock visibility, session shutdown pattern,
startup retry deadlock, wheel bypass list, context-menu .so naming,
RightButton re-assert).

Session tooling notes: 'CLEARED SCREEN' in the dock log is strut
bookkeeping, NOT an output removal - use a kernel drm hotplug logger
(udevadm monitor --kernel --subsystem-match=drm) for flap ground truth.
kde-inhibit --power --screenSaver held DPMS/autolock off for the whole
session (dies with the session, nothing to undo). The kglobalaccel
'show view settings' invoke races registration right after a restart -
invoke once, check for '#primaryconfigview#' in the log, invoke again if
absent; each further invoke CYCLES to the next view's settings.

Phase 8 day-one results (all commits on master, plan items ticked):
- The recurring crash family is dead: Qt6 MultiEffect does not auto-wrap
  plain Items as texture sources; three effect sites had invalid samplers
  since the port (73da8400), two more effects were dead and removed
  (df747ebf, e88af680), render loop defaults to basic for debuggability
  (c7200e3d). Regression caveat: re-adding the broken Comic Strip applet
  STILL crashes the churn recipe, and that crash (twice now) landed exactly
  on a 'CLEARED SCREEN :: DP-3' output removal - the portrait monitor
  flaps, and it MUST be eliminated as a variable before trusting further
  verdicts. CompactApplet churn hardening is filed and NOT implemented.
- Multi-monitor edit chrome fixed and hand-verified (d670c97a): canvas,
  settings window and widget explorer pin to the edited view's output.
  The secondary advanced-mode window is NOT yet covered.
- Debugging kit that made all of this work: LATTE_RUN_WRAPPER gdb batch
  script (catches every crash with full backtraces; KCrash self-destructs
  here), fakepointer move/click/rightclick, the KWin dumpwins scripting
  one-liner for true window geometries, WAYLAND_DEBUG=1 for protocol-level
  truth, and by-hand desk driving as the reproduction engine.

## What landed this session (all committed, all live-verified with screenshots)

Edit mode blueprint, done the Qt5-faithful way (the dock draws the grid in its
own window, since two wlr-layer surfaces cannot stack dock > grid > wallpaper):

- d68b8e8d feat(containment): draw the blueprint inside the containment, between
  the dock background and the layouts container, driven by root.editMode.
- d72ee0cd fix(containment): size the grid to latteView.editThickness at the
  screen edge. The dock window is taller than the edit area (parabolic zoom
  headroom: 384px window vs 146px editThickness here), so filling the window
  drew a huge grid far above the dock.
- 608a509e fix(settings): stop CanvasConfiguration.qml drawing its own grid.
  The canvas window stacks in front of the dock; its opaque copy hid every
  applet. Its imageTiler stays (geometry for the wheel MouseArea, opacity value
  for SettingsOverlay text contrast) but no longer renders.
- f5a5f44c fix(editmode): restore Qt5 editBackgroundOpacity semantics. The port
  had rewired the grid opacity to panelTransparency (the dock's persistent
  background setting) with an invented 0.5 floor, so at the theme default it was
  fully opaque and hid the white panel bar. Now: editBackgroundOpacity (default
  0.2), solid while rearranging widgets and without compositing, wheel steps it
  0.1 with no floor.

Wayland window previews (were blank, now show real thumbnails):

- c25cb3e1 fix(plasmoid): port previews to Plasma 6 kpipewire. The 5.26 rung
  kept the kpipewire 5 contract (item created enabled:false, C++ flips it);
  kpipewire 6 does not, so opacity stayed 0. Replaced the whole dead 5.24/5.25/
  5.26 version ladder with plasma-desktop 6's PipeWireThumbnail.qml shape.
  Also winId int -> var in ToolTipWindowMouseArea (wayland ids are QString
  UUIDs; same defect class as 5a77d2da).

Crash + infrastructure:

- 46dc83c5 fix(app): Factory::removeIndicatorRecords called removeAt(-1) for
  built-in indicators (never in the custom lists), a hard SIGSEGV on Qt6 (Qt5
  tolerated it). Fires when the install tree is restaged under a running dock.
  Three coredumps today traced here.
- f24b7d89 build: scripts/restart-staged.sh. Orphaned docks end up SIGSTOPped
  and ignore SIGTERM, so bare pkill left them alive and stacked instances (three
  docks were fighting over layer surfaces at one point). The script TERM+CONT,
  escalates to KILL, refuses to stack, launches via setsid with stdin closed.
- c49db4cc build: scripts/tools/fakepointer.c, wayland pointer injection for
  live verification (see below).
- fcedce40 docs: Qt5-faithful behavior agreement recorded in CLAUDE.md.

## Hover preview jitter: ROOT CAUSE CONFIRMED LIVE, fix proposed, awaiting go

Reproduced with fakepointer + temporary QML logging (instrumentation added,
driven, read, then removed; tree carries none of it). My symptom
(preview disappears and reappears in a different spot when nudging the cursor)
is the visible edge of a parabolic-zoom feedback loop that runs even with a
COMPLETELY STATIONARY pointer:

1. Hover a task, zoom animates, preview shows. While anything animates, the
   view receives a stream of MouseMove events at frame rate with an unchanged
   pointer position (~83 events/s measured; Qt6 frame-synchronous synthetic
   hover delivery, exact producer still to be pinned down in one rebuild).
2. app/view/parabolic.cpp onEvent maps each event's windowPos into the CURRENT
   parabolic item's coordinates and queues parabolicMove(item-local x,y) via
   Qt::QueuedConnection. The item is moving/scaling, so the item-local x keeps
   drifting (logged mx 76 -> 42 with the cursor pinned at one spot).
3. ParabolicEventsArea.qml onParabolicMove feeds that drifting x into
   calculateParabolicScales, which re-centers the zoom, which shifts the
   layout under the cursor, which produces more synthetic moves. The system
   oscillates instead of converging (one storm self-sustained for 33s; another
   walked the hover across tasks 4 -> 3 -> 2 -> 0 with zero pointer input,
   KWin raising each task's window via highlightWindows on the way).
4. Preview symptom: each boundary crossing fires exited/entered, show()
   re-anchors the popup to the new task (content + size + anchor all change);
   when the churn drops the cursor into a gap for >300ms, hidePreviewWinTimer
   fires forcePreviewsHiding and the preview vanishes, then re-hover shows it
   at the new anchor. That is exactly "disappears and reappears elsewhere".
5. Dialog layer makes it worse but is NOT the driver: with the Qt.ToolTip flag,
   every parabolic frame emits anchoredTooltipPositionChanged ->
   Dialog::updateGeometry() -> setPosition(popupPosition(visualParent, size())),
   racing PlasmaQuick's own syncToMainItemSize repositioning. Logged the window
   position flapping between two values (e.g. x=959 computed from the stale
   QWindow::size() vs x=681 from the new mainItem size) every ~10ms while the
   on-screen popup stayed at the first mapped position.

IMPLEMENTED (go given), iterated live at the desk across four rounds,
currently UNCOMMITTED in the working tree awaiting their final verdict:

1. Parabolic::onEvent drops MouseMove-derived updates whose window position
   has not changed (kills the stationary feedback loop; confirmed live: "it no
   longer jitters"). Honest caveat: the guard's drop path was never observed
   firing post-fix; the original storm conditions did not fully reproduce.
2. Dialog::updateGeometry() positions with the size the dialog is about to
   have (mainItem + frame margins) instead of the stale QWindow::size().
3. Dialog anchor-follow is pinned: reposition once per (anchor, size) change
   instead of on every parabolic wiggle of the anchor item; plus one explicit
   placement on visualParent change (covers same-sized content switches) and
   an explicit reposition whenever the mainItem resizes (base class proved
   unreliable there on wayland: a placement computed for the previous content
   width survived a resize, popup sat half a width off).
4. The previews dialog opts out of the applets-layout clamp
   (respectsAppletsLayoutGeometry: false): the clamp shoved wide previews for
   tasks near the dock ends sideways off their icon.
5. Previews anchor to a RESTING midpoint (my own suggestion): every task
   carries an invisible anchor whose center is sampled while the row is fully
   at rest and frozen during zoom AND during the restore animation
   (3*animationTime + margin; unfreezing right when currentParabolicItem
   clears sampled mid-restore midpoints, reproduced by hand with quick
   hover-away-and-back). TaskItem.qml _previewsVisualParent.

6. THE ACTUAL WAYLAND ROOT CAUSE of every "popup stuck at the previous
   task's spot" symptom, found with WAYLAND_DEBUG=1: QWindow::setPosition()
   and setGeometry() position parts are NO-OPs for a visible wayland window
   (client position stays frozen at the show-time value forever), AND the
   base PlasmaQuick::Dialog re-sends that frozen QWindow::position() to the
   plasmashell surface on every QEvent::Move and on expose
   (libplasma dialog.cpp event handler + updateVisibility()), overriding any
   correct placement right after it is made. Fix: updateGeometry() sends the
   anchored position through PlasmaShellWaylandIntegration::setPosition()
   (public PlasmaQuick API, works on visible surfaces; required generating
   qwayland-plasma-shell.h in declarativeimports/core the way app/ already
   generates other protocols), remembers it, and Dialog::event() re-asserts
   it AFTER the base class handles Move/Expose so ours is always the last
   request the compositor sees; cleared on Hide so show-time placement stays
   the base class's. Protocol-log verified: every stale re-send is now
   followed by the anchored position, last word ours.

The PORTING_PLAN Phase 7 "always-visible MouseArea, synchronous parabolic
calc" note (latte-dock-ng 0deca9e18) remains the structural long-term shape.
Confirmed by hand: no jitter within an icon; the final cross-icon slide test on
the (now multi-monitor) setup is still awaiting their verdict, then the
whole set can be committed in reviewable pieces.

## Multi-monitor: edit-mode windows use stale/wrong screen geometry (NEW, Phase 8)

I added a second monitor (portrait 1440x2560 at (0,0), landscape 2560x1440
at (1440,447)) and entered edit mode on the portrait dock. The settings
window landed at (2031,7) sized 529x1287: exactly the values computed for
the OLD single-screen 2560x1440 layout (x = 2560-529, y = 1440-146-1287),
i.e. floating in dead space above the landscape screen. Edit-mode window
geometry (PrimaryConfigView::syncGeometry inputs: screenGeometry,
m_availableScreenGeometry, canvasGeometry) must re-derive from the edited
view's CURRENT screen after output hotplug, and the layer-surface output
assignment (QWindow::setScreen at SubConfigView construction) must be
re-checked against where margins are computed (applyFixedGeometry margins
are relative to the passed screenGeometry and only correct when the surface
sits on that same output). This is the first concrete Phase 8 multi-screen
work item; do it WITH the dual-monitor setup live, per the plan's warning
that both reference forks fought multi-screen repeatedly.

## Duplicate-dock + add-widget crash (NEW, open, no core)

I duplicated the dock, then added a widget to it; latte crashed and
KCrash itself crashed while handling it (log: 'KCrash: appFilePath points
to nullptr!', crashRecursionCounter = 2), so there is NO core. The log
fingerprint: at duplication time a storm of
'org.kde.sync ... configuration syncing was not established, configuration
object was missing!' (app/view/containmentinterface.cpp:1034) for every
applet id of the clone; the same failure again for the newly added
widget's id right before the crash, then CompactApplet.qml binding errors
and death. Points straight at the cloned-view config-sync machinery, same
territory as the Phase 8 'cloned-view applet-order sync' item. To get a
backtrace next time: reproduce under
LATTE_RUN_WRAPPER='gdb -batch -ex run -ex bt --args' via restart-staged.sh.

## Dock exits ~20s after screen lock/unlock cycles (open, now well-scoped)

Twice observed: a screen remove/re-add cycle (lock or dpms), handled cleanly
by the positioner, then ~22s later 'Latte Corona - unload: containments' with
NO logged trigger, i.e. a clean app quit. Only three programmatic quit paths
exist (dbus quitApplication, settings dialog, importFullConfiguration) and
none tie to screens, so suspect an external SIGTERM or dbus call. Next step:
log the quit reason (KCrash/KSignalHandler or a SIGTERM handler qWarning) and
check what else runs at unlock. Before 72deaa8c these exits looked like
crashes because teardown itself segfaulted (~Theme null schemes).

## Add-panel crash (open, core retained)

'Add default panel' crashed: Storage::importLayoutFile iterated
corona()->importLayout() results and one containment pointer was ALREADY
FREED (first bytes 0x1) when isLatteContainment() copied its metadata
(kcoreaddons KPluginMetaData copy ctor SEGV). Something destroys a containment
synchronously during template import. NOT deterministic: a headless
reproduction (busctl org.kde.LatteDock addView us 0 "Default Panel") imported
cleanly. Core: coredumpctl 3933991 (02:06). The crashed attempt left a
half-imported left-edge panel in the layout (cleaned). Candidate suspects to
instrument next: GenericLayout::addContainment side effects during
importLayout, subcontainment (systemtray) handling, and the icontasks
package-load failure ('Could not find required file mainscript') seen during
imports.

## Edit mode layout (my request, COMMITTED, live-verified)

The request: rearrange button to the LEFT, settings panel to the RIGHT, the
Maximum Length ruler aligned with the top of the blueprint. Root causes found
with a KWin-script window dump (see verification loop below):

- The canvas config window was pushed 88px off the screen edge by the dock's
  own exclusive zone (measured: canvas at y=1206 instead of 1294 for
  editThickness=146), so ruler + button floated above the blueprint.
  Layer-shell zone 0 means "respect other surfaces' struts"; an overlay must
  opt out with zone -1.
- The settings window used LayerShell::setUnanchored, so the compositor
  centered it mid-screen (measured at (1015,380)), nowhere near the dock.

Landed as:
- ec5d2316 fix(wm): applyCanvasPlacement and applyFixedGeometry now
  setExclusiveZone(-1) so both land exactly where computed.
- 0d92f007 feat(settings): settings window pinned right via
  applyFixedGeometry (wayland setUnanchored had it compositor-centered
  mid-screen); horizontal docks use the right-end placement in both modes.
- 374461cb feat(shell): ruler thicknessMargin 0 for horizontal docks (flush
  with the blueprint inner edge), rearrange button anchored left just below
  (bottom dock) / above (top dock) the ruler. Vertical docks unchanged.
- 5e873329 fix(settings): completes the cf05d856 STUB. In configure-applets
  mode the whole canvas was click-through, so unclicking the rearrange
  toggle fell through to the dock, the settings window lost focus and edit
  mode closed entirely (caught live). updateInputRegion now reads the
  QML-published rearrangeToggleRect and keeps that rect interactive.

All verified live with KWin-script geometry dumps + the screenshot loop:
canvas at (0,1294) 2560x146 == the blueprint band, settings at (2031,7)
flush right with its bottom on the blueprint top, ruler line is the grid's
top boundary, toggle at the left under it, and the rearrange toggle
round-trip keeps edit mode open (confirmed working by hand).

One quirk seen only with fakepointer, not reproduced with a real mouse:
the click that ENTERS rearrange mode shrinks the input mask mid-click, and
the synthetic release seemed to get lost once, leaving the Button stuck
pressed so the next synthetic click was a no-op. If a real-mouse report of
"first unclick needs two clicks" ever comes in, start there.

## iconSize startup hang (NEW, root cause bisected, code fix pending)

The port hangs at startup at 100% CPU (main thread, event loop starved, dbus
times out) when the layout contains iconSize=78. Bisected live: my layout
in a throwaway config hangs; same layout minus iconSize=78 starts; iconSize=64
starts; 78 alone re-adds the hang. gdb backtrace (child run, since yama
blocks attach): a QML bound-signal handler triggered from the
Q_EMIT visibilityChanged() in View::setContainment (view.cpp:180 area) never
returns, i.e. a binding/handler cascade that never settles. Suspect surface:
viewTypeInQuestion / behaveAsPlasmaPanel / background.isGreaterThanItemThickness
flip-flopping once the icon size crosses a threshold between 64 and 78. The
log always freezes on the same line: 'Updating visibility mode ::
AlwaysVisible' right before indicator QML would load.

IMPORTANT: my REAL layout (~/.config/latte/"My Layout.layout.latte")
acquired iconSize=78 at 22:20:35 on 07-10, written when a staged dock was
killed while edit mode was open. Until the loop is fixed in code (preferred;
CLAUDE.md failure rules: fix the origin, do not clamp), --user-config runs
will hang. Do not silently edit my config; ask, or fix the code first.

## The live-verification loop (the big infrastructure win, use it every time)

Do NOT claim a UI change works without driving it and reading pixels. Yesterday's
"blank screenshot, tool is broken" conclusion was wrong; the tooling works.

- Restart the dock ONLY via scripts/restart-staged.sh --user-config -d (never
  bare pkill + relaunch, per the SIGSTOP trap above). It restages QML every run.
- Enter edit mode: busctl --user call org.kde.kglobalaccel /component/lattedock
  org.kde.kglobalaccel.Component invokeShortcut s "show view settings"
- Move/click the pointer without a human: scripts/tools/fakepointer.c
  (move|click <x> <y>) via org_kde_kwin_fake_input. KWin gates that interface
  per client, so it needs a desktop file in ~/.local/share/applications with a
  matching absolute Exec and X-KDE-Wayland-Interfaces=org_kde_kwin_fake_input,
  then kbuildsycoca6. A built binary + fakepointer.desktop are already set up.
- Capture: spectacle -b -n [-p for cursor] -o <file>, then Read the png.
- A ~14KB all-white png means the screen is LOCKED (loginctl show-session
  <wayland session> -p LockedHint) or the display slept, NOT a spectacle flake.
  Standing decision: loginctl unlock-session is fine for verification; re-lock
  with loginctl lock-session when done. Run kscreen-doctor --dpms on before
  capturing. The session auto-locks on a timer, so expect to unlock repeatedly.
- Confirm a capture landed inside edit mode by checking the png mtime against the
  dock log's "#primaryconfigview#" init line. The settings window closes on focus
  loss and edit mode dies within seconds of typing elsewhere, so capture
  ~2s after the invoke.
- Screenshots and the fakepointer binary go under $CLAUDE_JOB_DIR/tmp, not /tmp.

## Known traps

- Never rebuild (nix develop -c cmake --build build --target latte-dock) while a
  dock runs from build/bin; the running dock then executes a deleted binary and
  crashes confusingly. Stop it first.
- MultiLayered.qml already has a Behavior on opacity at its root; a second one
  logs "Attempting to set another interceptor" and is ignored. Those and the
  ~56 KWindowShadow warnings are pre-existing noise; do not chase them.
- QML/package changes need no rebuild (restart-staged.sh restages). Only C++
  changes (app/, declarativeimports/) need the cmake build.
- Validate QML with nix develop -c scripts/qml-compile-gate.sh before launching.

## Backlog (root-caused where noted, not started)

1. Edit mode step 2 (grow dock to editThickness) is filed in PORTING_PLAN Phase
   7 but may be largely moot: the mask already exposes the full window in edit
   mode and the window is taller than editThickness. Evaluate what actually
   remains (struts, input region, slide-in animation) before wiring anything.
2. UX: when the rearrange toggle is centered, open the settings window to the
   right instead of also centered (app/view/settings/primaryconfigview.cpp).
3. Dock invisible after a screen lock/unlock cycle, and a dock started under a
   locked screen stalls for minutes before "Adding View". Filed in PORTING_PLAN
   Phase 8. Not root-caused.
4. RESOLVED by 4c9f3bc7 (see the 2026-07-12 sweep): owning packages joined the
   flake's pinned LATTE_QML_MODULE_PATH; nine failing modules went to zero with
   the context menu intact. Kept for the warning's sake: a broad append of the
   system Qt6 QML tree fixed them once but shadowed org.kde.taskmanager and
   replaced the dock context menu with the stock one. Same-pin package roots
   only, never a foreign shared root.
5. plasma_applet_dict also needs QtWebEngine; recheck after 4.
