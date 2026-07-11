# Session handoff

Rolling handoff for the next session to pick up without re-deriving context.
Last updated 2026-07-11 (early morning). Edit-mode layout work is COMMITTED
and live-verified (see "Edit mode layout" below). A staged dock is running
against the THROWAWAY config (build/_runconfig), NOT --user-config: the real
user layout currently triggers a startup hang (see "iconSize startup hang"
below).

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
4. System widgets (system monitor, battery, bluetooth, networkmanagement,
   kdeconnect, dictionary) fail "module not installed": their private QML
   modules are not on the launcher's QML2_IMPORT_PATH. WARNING: a broad append
   of the system Qt6 QML tree fixed them once but shadowed org.kde.taskmanager
   and replaced the dock context menu with the stock one. Allow-list leaf
   modules (symlink specific dirs into a private root), never the shared root.
5. plasma_applet_dict also needs QtWebEngine; recheck after 4.
