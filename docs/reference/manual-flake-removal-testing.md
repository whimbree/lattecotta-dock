# Manual testing checklist (my hands required)

Everything below needs a real human at the desk - a real mouse, a real
login, or a judgment call only I can make. Collected 2026-07-16 at the
end of the stabilization execution session; each entry names the plan
item or commit it verifies so results can be recorded there. Tick and
annotate here as they happen, then fold the verdicts back into
docs/tracking/PORTING_PLAN.md.

Ground rules while testing: the dock should be running the staged
build with --user-config (scripts/restart-staged.sh --user-config);
watch the -d log or journal when a recipe says so; `busctl --user call
org.kde.lattedock /Latte org.kde.LatteDock viewsData` is the state
readback for almost everything (geometry, struts, editMode, isHidden,
inStartup).

## Session lifecycle

- [ ] **Real logout/login cycle** (session-shutdown item, e02d1bcde).
      Log out normally, log back in, then check the previous session's
      journal: `journalctl --user -b 0 | grep -E 'quitting on
      SIGTERM|lifecycle phase'`. Expect the SIGTERM line, the
      quit-requested -> unloaded trail, and NO coredump
      (`coredumpctl list --since today`). This is the one check that
      kills any driving session by definition.
- [ ] **Autostart decision** (startup-latency item). Login currently
      starts the PACKAGED latte-dock-ng fork binary
      (~/.config/autostart/org.kde.latte-dock.desktop) - not this
      port. Decide: point the entry at the packaged build from
      package.nix, or keep dev-session-only startup. After deciding,
      measure a real login (journal timestamps + poll lifecycleState
      for "running").
- [ ] **DPMS / long lock cycle** (lock/unlock item). Either
      `kscreen-doctor --dpms off` then on, or lock and walk away past
      the display-sleep timer. Afterwards: docks visible on both
      screens? `viewsData` sane (isHidden false, correct geometry)?
      Any "STARTUP STRANDED" or unexplained quit trail in the log
      ~20s after unlock? The short-lock arm was verified clean
      2026-07-16; the output-off arm is what remains.
- [ ] **Startup-stranding watch** (new Phase 8 item, 538abc8ec). If
      the dock ever comes up looking normal but windows maximize
      UNDER it, or slide-in never played: grep the log for "STARTUP
      STRANDED" and "startup:" breadcrumbs and paste the lines into
      the plan item - they name the dying hop. No deliberate action
      needed otherwise; the watchdog fires on its own.

## Edit mode / reorder (real mouse, throwaway layout is fine)

- [ ] **Repeated task drag-reorder, then z-order check** (480ae30e3).
      Reorder tasks back and forth a dozen times with the real mouse,
      then enter rearrange mode and confirm no task icon draws OVER
      the close-button overlay or other chrome (the old z=100 leak).
- [ ] **Drag-reorder feel** (research verdict: no change needed).
      While at it, confirm reorder does not jitter under slow and
      fast drags. If it ever jitters, do NOT tune constants - the
      finding goes to the reorder research log
      (docs/agent-logs/2026-07-16-reorder-research.md): something is
      re-reading geometry it just changed.
- [ ] **Show Alternatives swap** (b-lineage createApplet QRectF fix).
      Edit mode -> right-click an applet -> Show Alternatives -> pick
      a different applet. The replacement must APPEAR (the old bug
      destroyed without creating). Once forward and once back.
- [ ] **Widget removal Undo click** (71b0d75a, long-standing). Remove
      a widget, click Undo in the notification within 60s, confirm the
      widget returns to its slot.
- [ ] **Perpendicular flip reasserts** (eca51ae0). Flip a dock between
      horizontal and vertical edges in the settings and back; barLine,
      belower, shadowsSvgItem and the mouse handler must all track
      (no frozen 88px-wide input band, no stale background strips).
- [ ] **Duplicated-dock flip drive-through** (e412889d regression
      check). Duplicate a dock, flip the duplicate's edge
      perpendicular, hover across its tasks - zoom and clicks must
      work across the full row.

## Visibility / window tracking (real windows needed)

- [ ] **Dodge-maximized engages** (windowtracking sweep). Set a dock
      to Dodge Maximized, maximize a window touching it - dock hides;
      unmaximize - dock returns. Also: a focused dialog over a
      touching window keeps activeWindowTouching engaged.
- [ ] **Dock removal slides out on its own edge** (indicator sweep
      pending check). Remove a dock (undo-window aware!) and watch the
      exit slide use the dock's own edge, not a default.

## Settings walk (the Tasks-config lesson)

- [ ] **Every control on every page against Qt5 feel** (Phase 8 audit
      item; the automatable half is done and recorded in the item).
      Drive each control and watch the dock respond; anything that
      renders but changes nothing gets filed with the control name
      and page. Check especially: the four default-indicator
      style/glow buttons apply on press (2d28cda1 pending live
      check), and both Behavior tabs end to end.

## Visual / effects spot checks

- [ ] **t1 sampler warning gone** (5f8c10be). Click an applet, then
      hover-zoom it across its switchWidth; zero "t1 ...
      (QQuickItem*)" warnings in the log.
- [ ] **Task/badge/ghost shadow visuals unchanged; forced-monochromatic
      icons render** (effect-sweep pending checks). Native-resolution
      screenshots if anything looks doubled or bold (the c7c46226
      lesson: verify text applets at native res).
- [ ] **Meta+number badges** (extraction ledger EX tail; named as
      needing my hands). Hold Meta - number badges appear over tasks;
      Meta+3 activates the third entry.
- [ ] **Real file drags** (ledger tail). Drag a file from Dolphin over
      a task (hover-activates the window) and onto a launcher-capable
      applet; drop onto the tasks area offers add-launcher behavior.

## Multi-screen chrome (dual-monitor)

- [ ] **Comic full-size viewer lands on the dock's screen**
      (377aad57 live check). Open the comic applet's full-size view
      from a DP-3 dock; the dialog must open on DP-3.
- [ ] **Stuck-overlay recipes leave no mapped chrome** (08511ffd).
      Close the edit chrome within ~200ms of opening; switch layouts
      with chrome open; then `scripts/tools/dumpwins.sh` shows no
      lingering latte chrome windows (the 1096x527 layer-6 family).

## Session two additions (2026-07-16, post-merge desk checks)

- [ ] **WM surface pass after the WindowId newtype flip** (plan item
      "WindowId newtype hardening", commits 46ce2dfbb..4ebadede9).
      One normal desk session paying attention to: active-window
      tracking (dock colorizer/dodge following the focused window),
      dodge modes reacting to real window moves, window color-scheme
      following, and a subwindow (dialog) closing+reopening still
      tracked. Anything that stops following windows points at the
      wm/ conversion.
- [ ] **Meta+number and press-and-hold badges through the FIXED
      shortcuts host** (a3d2afc7c). The old check verified fallbacks;
      the real host is alive for the first time since the port. Hold
      Meta: number badges must appear over tasks AND applets;
      Meta+3 activates entry 3; Meta+Ctrl+3 opens a new instance.
      If badges do not appear, grep the log for "shortcuts host" -
      the discovery now warns loudly instead of dying silently.

## Keyboard focus mode (2026-07-17, needs a real keyboard)

The nested vehicle proved the C++ lifecycle end to end
(tests/e2e/keyboard-navigation-mode.sh: enter/exit readback, unknown-id
refusal, focus-loss self-exit); what it cannot drive is real key events
into the focused window. All of these against the staged dock:

- [ ] **Meta+Alt+D toggle** (plan keyboard item, worktree commit
      a5759f19b). Press once: dock takes focus, position badges appear
      (the Meta-hold view), the first item lights its indicator hover
      state. Press again: everything reverts. `viewsData`'s
      keyboardNavigation is the readback if the visuals are ambiguous.
- [ ] **Traversal keys** (1fbe25b45). Arrows step along the row
      (Up/Left back, Down/Right forward), no wrap at either edge,
      Home/End jump; the highlight and the badges must always agree
      with what Enter would activate (they share the shortcut-index
      match).
- [ ] **Activation**. Enter/Return/Space on a task = left click
      semantics (activate/minimize toggle, group cycling); on an
      expandable applet = popup toggle. Activating a real window must
      ALSO exit the mode by itself (the window takes focus - watch
      keyboardNavigation flip false).
- [ ] **Escape** exits, dock returns focus-refusing (type into the
      previously focused app to confirm nothing is stuck).
- [ ] **Auto-hide interplay**. With auto-hide on and the dock hidden,
      Meta+Alt+D must reveal it (block-hiding mustBeShown) and exit
      must re-hide it.
- [ ] **Fullscreen safety, the defect class the exits guard**. Enter
      the mode, then click into a fullscreen app: the dock must give
      the keyboard back instantly (mode exits on focus loss). If any
      app ever stops receiving keys after using the mode, that is the
      P0 regression - viewsData keyboardNavigation stuck true.
- [ ] **RTL judgment call** (ledger leftover). The handler does not
      wire LayoutMirroring: with an RTL layout the arrows keep visual
      left/right semantics. Decide whether that is correct or arrows
      should follow reading direction, and record the decision in the
      plan.

## Orca screen-reader pass (2026-07-17, needs my ears)

The Accessible.* rollout (branch accessible-rollout; ledger
docs/agent-logs/2026-07-17-accessible-rollout.md) pinned every
role/name/description/press-action offscreen, but announcement is a
runtime pipeline (Qt bridge -> AT-SPI bus -> Orca heuristics) that
only real ears can accept. Setup: start Orca (`orca` in a terminal or
the a11y KCM); Qt's bridge enables itself per-process the moment
org.a11y reports a screen reader - no dock restart needed if Orca was
up first, otherwise restart the staged dock. For Orca-less debugging,
`QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1` on the dock plus `accerciser` (or
busctl on the a11y bus) shows the raw tree.

- [ ] **Task announcement.** Meta+Alt+D, arrow along the row. Each
      task must announce: title (windows) or app name (launchers),
      "button", and the state description - a pinned launcher says
      "launcher", a group says "N windows", a task playing sound says
      "playing audio" (or "audio muted"), a download in flight says
      "NN% complete", an unread-count badge says "N notifications".
      The announcement must follow the arrows in step with the visual
      indicator highlight (Accessible.focused mirrors the same index).
- [ ] **Task activation by AT.** Orca's default action (or Enter) on a
      focused task must do exactly what a left click does - both paths
      run activateTask().
- [ ] **Applet announcement.** Arrow onto a plain applet: its plasmoid
      title plus "button"; the default action toggles its popup (the
      Meta+number body). The tasks area itself must NOT announce as
      one blob - its container is pruned so individual tasks speak.
- [ ] **Audio badge.** On a task with sound, flat review (or object
      navigation) must find a "Mute" check box whose checked state
      follows the real mute; toggling it from Orca must mute/unmute
      (same toggleMuted() as the click and the context menu item).
- [ ] **Settings custom controls.** Open dock settings: every
      HeaderSwitch header ("Background", "Indicators", ...) must read
      as a named check box with its tooltip as help text, and toggling
      from Orca must flip it. ComboBoxButton chips (e.g. the layouts
      actions chip) must announce their visible label, both the button
      and the dropdown half.
- [ ] **Edit-mode chrome.** In the canvas: "Rearrange and configure
      your widgets" and the two stick chips announce with their text
      and toggle state, and Orca-press toggles them (the replayed
      pressedChanged cycle). Hover an applet in configure mode: the
      handle buttons announce "Configure applet", "Enable painting for
      this applet", "Disable parabolic effect for this applet",
      "Remove applet".
- [ ] **Add widgets.** Open the widget explorer: each card announces
      name + description + "button"; the grid's current-item highlight
      and the announced focus move together; Orca's default action
      adds the widget exactly like a tap (recorded pin, verify live).
- [ ] **Known gaps to NOT report as regressions:** the group previews
      dialog is still focus-refusing and unannounced (deferred to the
      P1 previews keyboard rework); sliders and combos inside config
      pages carry roles but not per-instance labels yet (P3 page
      pass); Return in the widget explorer grid is the keyboard
      item's Keys wiring, not this rollout's.
## E2E promotion additions (2026-07-17, desk-owed because the vehicle cannot host them)

- [ ] **Audio-badge wheel as a REPEATABLE check** (EX-15 recipe 1).
      Verified live once (2026-07-16: spotify stream, pactl 74->84->79,
      one 5% plasma-pa step per detent), but the nested vehicle has no
      pipewire so no audio-stream task can exist there and the check
      cannot join tests/e2e/. Re-run at the desk whenever the wheel or
      audio-stream code moves: `fakepointer scroll <x> <y> <detents>
      <gap>` over a playing task's audio badge; one volume step per
      detent, reversal needs full travel.
- [ ] **Desk-side consecutive desktops-wheel** (010's vehicle
      limitation cross-check). In the vehicle, kwin stops delivering
      wheel to the dock after a desktop switch (not a dock defect -
      instrumented, see docs/agent-logs/2026-07-17-e2e-promotion.md).
      On the desk, flip scrollAction=Desktops on the throwaway and
      verify several wheel switches IN A ROW work with a real mouse;
      if they die after the first switch there too, the vehicle
      finding graduates into a real bug.
- [ ] **Dock placement drift check on the desk** (the Phase 8 drift
      item filed 2026-07-17). Measure once whether the desk dock's
      surface x matches viewsData's implied origin: run
      `scripts/tools/dumpwins.sh`, compare the bottom dock window's x
      against absoluteGeometry-localGeometry, and watch it across a
      clock minute tick. In the vehicle they disagree by 20-74px and
      the icons really shift.
