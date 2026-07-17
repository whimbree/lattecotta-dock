# Manual testing checklist (my hands required)

Everything below needs a real human at the desk - a real mouse, a real
login, or a judgment call only I can make. Collected 2026-07-16 at the
end of the stabilization execution session; each entry names the plan
item or commit it verifies so results can be recorded there. Tick and
annotate here as they happen, then fold the verdicts back into
docs/PORTING_PLAN.md.

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
