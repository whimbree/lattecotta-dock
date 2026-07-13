# Session handoff

Rolling handoff for the next session to pick up without re-deriving context.
Last updated 2026-07-12 (evening). PHASE 8 IS OPEN - read its section in
docs/PORTING_PLAN.md first; every item is current there, several sections
below are now RESOLVED and kept only as archaeology.

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
- Edit-mode chrome trilogy (user-reported: blueprint flash+vanish,
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
  (pre-existing per user recollection).
- Round two, user-driven: the rearrange input mask was carved from a
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
  with the Configure entry available ALWAYS (owner decision): the root
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
- Round six, autonomous (d619ae08, d98bff98, 69baabf0 + docs): the
  user's minimal preview recipe (hover System Monitor, glide to
  Firefox, preview ~370px left) root-caused past the deferred re-show:
  the timer path skipped preparePreviewWindow so content and anchor
  could interleave across tasks. show() now binds visualParent and
  content atomically at map time and cancels stale pendings; verified
  with glide at realistic rates AND a fresh-map screenshot with the
  preview dead-center (2395 vs icon 2394). Effect-source layering then
  landed (69baabf0): startup warnings 45 -> 7, ZERO accrual over 95s
  idle and over hover churn; residuals are first-frame bursts (~28
  applet-shadow, known lower-risk class). USER-VISIBLE: task
  hover-brighten/click effects render for the first time since the
  port - they were invalid-sampler no-ops before. If the user asks
  why icons suddenly brighten on hover, that is restored Qt5 behavior,
  not new. Badged-icon effect rendering still unverified (no badge app
  was running). Next in queue: hover-modal inconsistency in rearrange
  mode, residual ~40px preview offset during zoom dwell (live vs
  resting rect, refine d98bff98), then the latency items.
- Round nine (8f821310): 77aac4b4 user-confirmed with a real mouse.
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
  the user re-reproduced against d619ae08 with the same recipe. Full
  instrumentation of QML show() plus every C++ position push caught
  it: the dialog maps with the PREVIOUS task's content width, the base
  recenters on the resize, then libplasma's stale-position re-send
  reverts it - and the old stored-position counter-measure was armed
  only by the mainItem-resize hook, which fired for every intermediate
  stop of the sweep but not the final one. Ordering-dependent
  correctness, which is why three fixes passed synthetic retests and
  kept failing under the user's hand. 77aac4b4 removes the stored
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
  ToolTipDialog/KlipperPopup), not user hover.
- Round four, decisions and mapping (e70bccf7, e85e18d8, 98b7419e):
  parabolic zoom disabled for ALL of edit mode by owner decision
  (deliberate Qt5 deviation, comment at the site). The missing 'Applet
  Settings' menu entry got its architecture mapped: Qt5's
  ViewPart::ContextMenu was removed in the port (d3538eee), the canvas
  ContextMenuLayer replacement has correct composition but dead
  applet-under-cursor resolution (direct-children method lookup misses
  Plasma 6 wrappers; cross-window fallback; use-before-null-check at
  :229), and dock-window right-clicks reach the containment-only
  containmentactions plugin. Owner decision: configure entry belongs in
  the menu ALWAYS (matches Qt5/plasmashell). Also filed: Edit Dock
  wrong-view targeting (stale singleton chrome shows before retarget,
  observed in the 20:53 trace and twice by the user), edit-mode
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
it - use the real user layout, interactive), the with-user settings
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
- Multi-monitor edit chrome fixed and user-verified (d670c97a): canvas,
  settings window and widget explorer pin to the edited view's output.
  The secondary advanced-mode window is NOT yet covered.
- Debugging kit that made all of this work: LATTE_RUN_WRAPPER gdb batch
  script (catches every crash with full backtraces; KCrash self-destructs
  here), fakepointer move/click/rightclick, the KWin dumpwins scripting
  one-liner for true window geometries, WAYLAND_DEBUG=1 for protocol-level
  truth, and the user at the desk as the reproduction engine.

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
driven, read, then removed; tree carries none of it). The user's symptom
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

IMPLEMENTED (user go given), iterated live with the user across four rounds,
currently UNCOMMITTED in the working tree awaiting their final verdict:

1. Parabolic::onEvent drops MouseMove-derived updates whose window position
   has not changed (kills the stationary feedback loop; user confirmed "it no
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
5. Previews anchor to a RESTING midpoint (user's own suggestion): every task
   carries an invisible anchor whose center is sampled while the row is fully
   at rest and frozen during zoom AND during the restore animation
   (3*animationTime + margin; unfreezing right when currentParabolicItem
   clears sampled mid-restore midpoints, reproduced by the user with quick
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
User confirmed no jitter within an icon; the final cross-icon slide test on
the (now multi-monitor) setup is still awaiting their verdict, then the
whole set can be committed in reviewable pieces.

## Multi-monitor: edit-mode windows use stale/wrong screen geometry (NEW, Phase 8)

User added a second monitor (portrait 1440x2560 at (0,0), landscape 2560x1440
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

User duplicated their dock, then added a widget to it; latte crashed and
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

User's 'add default panel' crashed: Storage::importLayoutFile iterated
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

## Edit mode layout (user-requested, COMMITTED, live-verified)

User request: rearrange button to the LEFT, settings panel to the RIGHT, the
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
  mode closed entirely (user-reported). updateInputRegion now reads the
  QML-published rearrangeToggleRect and keeps that rect interactive.

All verified live with KWin-script geometry dumps + the screenshot loop:
canvas at (0,1294) 2560x146 == the blueprint band, settings at (2031,7)
flush right with its bottom on the blueprint top, ruler line is the grid's
top boundary, toggle at the left under it, and the rearrange toggle
round-trip keeps edit mode open (user-confirmed working).

One quirk seen only with fakepointer, not reproduced with a real mouse:
the click that ENTERS rearrange mode shrinks the input mask mid-click, and
the synthetic release seemed to get lost once, leaving the Button stuck
pressed so the next synthetic click was a no-op. If a real-mouse report of
"first unclick needs two clicks" ever comes in, start there.

## iconSize startup hang (NEW, root cause bisected, code fix pending)

The port hangs at startup at 100% CPU (main thread, event loop starved, dbus
times out) when the layout contains iconSize=78. Bisected live: user layout
in a throwaway config hangs; same layout minus iconSize=78 starts; iconSize=64
starts; 78 alone re-adds the hang. gdb backtrace (child run, since yama
blocks attach): a QML bound-signal handler triggered from the
Q_EMIT visibilityChanged() in View::setContainment (view.cpp:180 area) never
returns, i.e. a binding/handler cascade that never settles. Suspect surface:
viewTypeInQuestion / behaveAsPlasmaPanel / background.isGreaterThanItemThickness
flip-flopping once the icon size crosses a threshold between 64 and 78. The
log always freezes on the same line: 'Updating visibility mode ::
AlwaysVisible' right before indicator QML would load.

IMPORTANT: the REAL user layout (~/.config/latte/"My Layout.layout.latte")
acquired iconSize=78 at 22:20:35 on 07-10, written when a staged dock was
killed while edit mode was open. Until the loop is fixed in code (preferred;
CLAUDE.md failure rules: fix the origin, do not clamp), --user-config runs
will hang. Do not silently edit the user's config; ask, or fix the code first.

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
  User gave standing consent to loginctl unlock-session for verification; re-lock
  with loginctl lock-session when done. Run kscreen-doctor --dpms on before
  capturing. The session auto-locks on a timer, so expect to unlock repeatedly.
- Confirm a capture landed inside edit mode by checking the png mtime against the
  dock log's "#primaryconfigview#" init line. The settings window closes on focus
  loss and edit mode dies within seconds of the user typing elsewhere, so capture
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
