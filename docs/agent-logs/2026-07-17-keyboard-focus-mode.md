# Keyboard focus mode + shortcuts-host pin (2026-07-17)

Worktree branch `keyboard-focus-mode`. The accessibility quartet's P0
gate (docs/tracking/PORTING_PLAN.md, keyboard item; gap analysis in
docs/agent-logs/2026-07-16-a11y-surface-inventory.md) plus the owed
shortcuts-host pin test. NOTE: hashes below are worktree hashes; the
plan gets its final ones at merge.

## Design (as landed)

The dock window is focus-refusing by construction; the mode makes it
temporarily focusable and everything else rides existing machinery:

- **C++ state machine** (634ae2083, app/view/view.{h,cpp}):
  `keyboardNavigationIsActive` property with
  enter/exit/toggleKeyboardNavigation. Enter: block-hiding event (also
  reveals an auto-hidden dock via mustBeShown - the Meta press-and-hold
  mechanism), WindowDoesNotAcceptFocus dropped, layer-shell
  KeyboardInteractivityOnDemand, requestActivate(). statusChanged was
  restructured around one policy writer (applyKeyboardFocusPolicy) so
  the AcceptingInputStatus machine and the mode cannot fight; mode off
  reproduces the old three-branch behavior exactly. Exits are
  deliberately redundant: Escape (QML), the shortcut/D-Bus toggle, and
  QWindow::activeChanged - ANY focus loss converges the window back to
  focus-refusing, because a dock stuck focusable breaks fullscreen
  apps.
- **Global shortcut** (a5759f19b): Meta+Alt+D in the existing
  kglobalaccel family; strict toggle (exits from whichever view is
  navigating, enters on the isPreferredForShortcuts-then-sorted view,
  the activateEntry choice). Meta+Alt+P is plasmashell's panel-focus
  analog; D avoids the collision.
- **D-Bus surface** (0c3fa2a70): setViewKeyboardNavigation(u,b) coarse
  action + viewsData `keyboardNavigation` readback; XML + both docs +
  dbusreportstest per the maintenance triple.
- **Traversal core** (84884c246): VisibleIndex gains countVisibleSlots
  (max over per-entry upper bounds, NOT a sub-item sum - a trailing
  empty tasks applet owns its exact base slot one past the sum) and
  steppedVisibleSlot (clamped, no wrap; stale current clamps back in).
  Unit-pinned in visibleindextest.
- **QML traversal** (1fbe25b45): KeyboardNavigationHandler.qml in the
  containment takes collaborators as typed properties; arrows/Home/End
  step the same 1-based entry space Meta+number addresses;
  Enter/Return/Space call the shortcuts host's activateEntryAtIndex
  (the Meta+N path, which is a task's click path); Escape exits. Focus
  indicator: items publish isKeyboardFocused from the SAME
  shortcut-index match their activation handlers use and feed it into
  indicator.isHovered - the hover chrome IS the focus indicator; the
  position badges show while navigating (the Meta-hold view). One new
  shared ability property: PositionShortcuts.keyboardFocusedEntryIndex,
  client-mirrored like showPositionShortcutBadges.
- **Pin test** (1c7fe9871): the owed offscreen pin. The walk +
  signature resolution extracted to statics
  (ContainmentInterface::findShortcutsHost /
  resolveShortcutsHostMethods); real containment graph through
  itemForApplet with the REAL PositionShortcuts.qml qrc-aliased in;
  child-scan fallback and hostless refusal pinned with plain graphs.

## Evidence

- Nested vehicle (run_in_kwin.sh + dbus-run-session, private bus):
  tests/e2e/keyboard-navigation-mode.sh (920e84ff9) 8/8 ok on the
  final binary - lifecycle running, baseline false, unknown-id refusal
  with the dock alive, enter/exit readback true/false, focus-loss
  self-exit (instrumented run showed the full chain: requestActivate
  -> activeChanged(true) -> qml focus-taker maps ->
  activeChanged(false) -> exitKeyboardNavigation; instrumentation
  removed afterwards), idempotent exit.
- ctest: dbusreportstest, visibleindextest, shortcutshosttest green;
  qmlcompilegate, qmllintgate (baseline unchanged), qmleffectrules
  green. Full suite green at close (see final gate run).

## Traps hit and banked

1. **Forgot the dbus-run-session wrap on the first drive**: the driver
   reached the DESK session's real dock (read-only calls only, no
   harm). The private-bus wrap is not optional; the e2e script header
   says so.
2. **konsole as focus-taker**: cold start inside the nested session
   exceeded 30s and faked a "focus loss did not exit" failure. A
   minimal qml Window maps in about a second; that is the committed
   focus-taker.
3. **Default GL stalls the dock in "startup"** on the nested virtual
   output (own-vehicle attempt with a shared kwin bus); run_in_kwin's
   lavapipe vulkan env is the proven combination. The shared-bus
   variant (needed if a future drive wants kwin scripting) works once
   it copies that env - scripts kept in the session scratchpad, not
   committed; the committed e2e rides run_in_kwin.
4. **Stale .qmlc masks qrc QML drift**: a deliberately drifted
   signature PASSED the new pin until QML_DISABLE_DISK_CACHE=1;
   negative-tested both ways. Filed as a plan item to audit the other
   qrc harnesses (representationswitchtest, layoutmanagerparkingtest).

## Left for the desk pass (filed in docs/reference/manual-flake-removal-testing.md)

Real-keyboard traversal (Meta+Alt+D, arrows/Home/End, Enter/Space,
Escape), the badge + indicator visuals, auto-hide reveal on enter, and
the RTL arrow-direction judgment call (LayoutMirroring is not wired
into the handler; arrows follow visual left/right only for LTR).
