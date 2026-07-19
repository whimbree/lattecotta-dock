# 2026-07-17 e2e promotion - running ledger

## FINAL STATE (unit complete)

Suite: scripts/run-e2e.sh, nested vehicle DEFAULT (--live escape
hatch), 9 recipes, 9/9 GREEN in one continuous vehicle run:
000-smoke, 010-wheel-desktops (EX-15 r2), 020-wheel-task-cycle
(EX-15 r3), 030-wheel-ruler-maxlength (EX-15 r4),
040-preview-tooltip-text (EX-17, pixel golden), 050-drag-reorder-
launchers (EX-14 r1), duplicate-view-idremap, parabolic-hover-preview,
settings-window-onscreen (kglobalaccel path works nested - KWin
provides org.kde.kglobalaccel on the vehicle bus).

Defects found: NONE in the dock's own behavior - every failing recipe
traced to either the vehicle's input-delivery quirks (findings 5),
harness assumptions (findings 2-4, 7) or KConfig default-deletion
semantics (030's commit). One REAL finding filed for Phase 8
root-cause: the window-x drift (finding 6) - the dock genuinely
renders shifted from its reported geometry in the vehicle; whether
the desk drifts too is a desk-owed check in
docs/reference/manual-flake-removal-testing.md, alongside the audio-badge
repeatable recipe (no pipewire in the vehicle) and the desk-side
consecutive-desktops-wheel cross-check.

Gates at close: full nested suite 9/9; sceneprobe gate 13/13 after
the shared-lib extraction; full ctest green (see closing commit).

Branch: e2e-promotion (worktree /tmp/latte-wt-e2e).
Task: promote the nested-compositor vehicle (session-two proof, see
session-handoff "NESTED-MATRIX VEHICLE PROVEN") into the maintained
suite (stabilization quartet item c + adoption plan P4), then implement
and RUN the owed EX-14/15/17 extraction-ledger recipes as e2e recipes.

## Plan

1. Extract the nested-kwin bring-up/teardown from
   tests/sceneprobe/run_in_kwin.sh into scripts/lib-nested-kwin.sh
   (configurable size/socket/bus capture); run_in_kwin.sh sources it,
   sceneprobe gate behavior stays identical (gate re-run proves it).
2. scripts/run-e2e.sh grows a nested mode (default): private
   kwin_wayland --virtual 1600x1000, ONE shared bus (kwin + dock +
   probes - the v2 separate-bus lesson), throwaway config copy,
   staged dock via run-staged.sh; --live keeps the desk path.
   tests/e2e/lib.sh carries the recipe helpers (D-Bus call/poll,
   dock stop/start, window dump, ScreenShot2 capture).
3. tests/e2e/000-smoke.sh: dock running, views settle, clean SIGTERM.
4. EX-15 recipes 2-4 (desktops wheel / task-cycle wheel / ruler wheel),
   EX-17 preview text (needs >1 nested virtual desktop), EX-14 recipe 1
   (drag-reorder with >=3 pinned launchers, order survives restart).
5. Ledger updates (EX-14/15/17 pending sections, extraction plan
   executed notes, PORTING_PLAN P4/quartet), README line.

## Verified while building (evidence, newest last)

- Build: RelWithDebInfo configure+build green in this worktree
  (495/495 targets).
- Vehicle mechanics proven interactively before writing the scripts:
  one shared bus (kwin+dock+probes), ScreenShot2 raw-over-fd capture
  (KWIN_SCREENSHOT_NO_PERMISSION_CHECKS verified present in the pinned
  6.7.3 screenshot plugin), fakepointer injection, transient-KWin-script
  readback (QT_FORCE_STDERR_LOGGING routes it into the vehicle log -
  NixOS Qt otherwise journals it), kglobalaccel provided by kwin itself
  on the nested bus, kactivitymanagerd dbus-activation needs
  WAYLAND_DISPLAY preseeded or Corona::load() waits forever.
- Suite green 4/4 after promotion (smoke, duplicate-view, parabolic
  hover, settings-window), no leftover state, desk untouched.

## Findings (non-defect, vehicle behavior)

1. Nested-kwin teardown race: kwin/kded flush config on SIGTERM after
   the leader is reaped; cleanup now waits the whole process group out
   and self-checks dir reappearance (fix commit on lib-nested-kwin.sh).
2. lifecycleState "running" precedes view creation; empty viewsData
   must read as still-starting (fix commit on tests/e2e/lib.sh).
3. Startup zoom animation keeps geometry moving after inStartup
   clears; e2e_wait_settled now also requires two identical
   consecutive viewsData reads.
4. The centered strip drifts with the clock text width run to run;
   pointer math must be recomputed from viewsData per dock start.
5. NESTED INPUT-DELIVERY LIMITATION (instrumented root cause, not a
   dock defect): the nested kwin stops delivering pointer/wheel to the
   dock's layer surface after a virtual-desktop switch. Evidence:
   repeated wheel deliveries fine while no switch happens (3 in a row,
   QML-side probe), the first real switch kills delivery, motion does
   not restore it, dock input regions stay intact (viewsData), and
   the WM-layer probe shows correct m_desktops/m_currentDesktop with
   the activated signal received. A dock restart resets delivery;
   010-wheel-desktops works around it and documents it in its header.
   An axis event racing its own surface-enter is also dropped: recipes
   settle the pointer (move outside, move in, wait) before wheeling.
6. WINDOW-X DRIFT (real, visual, filed as a plan finding): the bottom
   dock's layer surface sits at x=-20/-44/-74 in some sessions while
   viewsData's absolute/local pair implies x=0, and the ICONS REALLY
   RENDER SHIFTED (same-rect screenshot crops from a drifted and a
   non-drifted run differ by exactly the drift; evidence preserved in
   the ledger narrative). The drift value changes when the clock text
   re-measures (minute tick), and the on-demand sidebar's same-sized
   window drifts independently, which makes the compositor window dump
   ambiguous as a correction source. Recipes that need exact icon
   positions calibrate from PIXELS at action time (050 locates the
   spotify icon's green disc on the icon row); e2e_view_window_x
   remains as a best-effort correction for coarser targets. Whether
   the desk session drifts too is an open Phase 8 question - a 20px
   off-center dock on a 2560 screen would be easy to miss by eye.
7. Wrong even-slot model under zoomLevel=0: with the zoom disabled the
   auto-sized bar keeps trailing space inside the tasks applet, so
   applet-width/task-count no longer matches the rendered pitch. The
   drag recipe keeps the default zoom instead.
8. Drag semantics observed live (matches EX-14's decideTasksDragMove):
   the model reorders WHILE the drag crosses neighbors; the release
   point's slot decides the final index, so a swap-with-next means
   releasing at the neighbor's rest center, not past it.
