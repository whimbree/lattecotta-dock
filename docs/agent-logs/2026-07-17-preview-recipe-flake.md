# parabolic-hover-preview recipe flake - root cause and fix (2026-07-17)

Ledger for the investigation into `tests/e2e/parabolic-hover-preview.sh`
failing intermittently with "no preview dialog mapped after gliding the
tasks region".

## Verdict

RECIPE BUG (driving artifact), not a dock bug and not live-only. The
preview pipeline and the dialog mapping both work correctly in the nested
vehicle; the recipe drove the hover in a way that fires the trigger only
~60% of the time. Fixed by re-gliding onto the icon until the dialog maps,
with the assertion still requiring a genuine layer=6 dialog.

## Characterization (flaky, not deterministic)

Contra the "fails deterministically" report, the recipe is genuinely
FLAKY and the flake rate depends on the DRIVING PATTERN, not the config.
Measured on Bree's multi-dock "My Layout" throwaway config (konsole is a
grouped, window-owning task at index 4 of 7; the other six are windowless
launchers), 1600x1000 vehicle, previewsDelay default 650ms:

- original recipe (glide to endx, jump BACK to konsole): ~2/6 pass
- smooth glide, stop ON konsole: ~4-5/6 pass
- slow glide (0.08s between steps): 0/6 pass
- direct warp onto konsole + micro-nudge: 0/6 pass
- enter konsole vertically from outside at rest: 0/8 pass
- settle on left neighbor, then step onto konsole: 0/8 pass
- re-glide onto konsole until it maps (the fix): 12/12 pass

So only a horizontal glide that CROSSES the icon triggers it at all, and
even that only ~60% per attempt. The differentiator between a pass and a
fail is purely the timing of the boundary crossing against the zoom
animation - the pointer lands at exactly (876,956) on konsole every single
time (confirmed via `workspace.cursorPos`), pass or fail.

## Where the truth is (instrumented, then removed)

Chain probed with temporary `console.warn("ZZPROBE ...")` in
TaskMouseArea.qml (onEntered, hoveredTimer.onTriggered), TaskItem.qml
(showPreviewWindow), and main.qml (windowsPreviewDlg.show, settle timer),
read from the dock log (needs QT_FORCE_STDERR_LOGGING=1 - NixOS Qt block-
buffers stderr to a file, so the dock was SIGTERM'd to flush before
reading). All instrumentation reverted before committing.

1. Landing: honest. Cursor ends at (876,956) = konsole's rest centre every
   run. Not a geometry problem.
2. Dialog mapping: NOT a screencast/live-only problem. PipeWire is
   unavailable in the vehicle ("Failed to connect to PipeWire"), but the
   previews dialog still maps as a layer=6 window with the icon fallback -
   which is what the recipe asserts. When it maps it appears within ~500ms
   and stays; there is no map-then-unmap race (polled the full ~4s window).
3. Trigger: the preview shows only through
   `TaskMouseArea.onEntered` -> `hoveredTimer` (interval
   max(150, previewsDelay)=650ms) -> `TaskItem.showPreviewWindow()` ->
   `windowsPreviewDlg.show()`. On a PASS the whole chain fires, with
   `onEntered` logging `hoverEnabled=true`. On every FAIL pattern the chain
   never starts: `onEntered` is never emitted.

## Root cause

`TaskMouseArea.hoverEnabled` is `taskItem.visible && !inAnimation &&
!isStartup && !root.taskInAnimation && !inBouncingAnimation && !isSeparator`.
A MouseArea emits `onEntered` only while `hoverEnabled` is true. The
parabolic ZOOM is an animation, so while the dock is animating,
`hoverEnabled` is false on every task and no `onEntered` fires. This is
Qt5-faithful and deliberate (Michail's design: do not process hover
mid-animation, which would jitter the zoom).

A synthetic fakepointer warps the cursor in discrete steps. When a single
glide crosses konsole's boundary, whether `hoverEnabled` is true at that
instant is a race with the zoom animation the same motion is driving. If
the crossing lands on a settled frame, `onEntered` fires and the preview
shows; if it lands mid-animation, the enter is swallowed. Crucially, once
the warped pointer comes to REST inside the icon, no further boundary
crossing ever occurs, so the enter never gets a second chance - the preview
then never maps for the rest of the dwell (confirmed: fails stay failed
across a 4s poll). The task highlight seen on failing runs comes from
`parabolicAreaContainsMouse` (a separate path), which is why the icon looks
hovered while no preview appears.

This does not reproduce for a real user: a physical mouse emits
continuous, high-frequency motion, so after the zoom settles a fresh motion
event re-enters the icon and fires the trigger. Bree runs previews live
daily; they are reliable. The flake is an injection artifact.

## Fix

`tests/e2e/parabolic-hover-preview.sh`: repeat the glide-onto-konsole
gesture (up to 12 attempts), leaving the dock between attempts so the zoom
resets to rest, and breaking as soon as a layer=6 dialog is observed. Each
attempt is a genuine hover gesture; the final assertion still requires a
real mapped dialog, so the retry makes the DRIVE reliable without weakening
the check. The glide now also starts a few icon slots to the side of
konsole (whichever side has room) and ends ON konsole, instead of
overshooting and jumping backward.

Considered and rejected: touching the dock's `hoverEnabled` gate. It is
correct, Qt5-faithful behaviour that prevents mid-animation hover jitter
and is not user-visible-broken; changing it to satisfy a synthetic driver
would be a real regression risk for no live benefit.

## Evidence

- 12/12 consecutive PASS with the fix (single dock session each).
- ~60% single-shot pass before the fix (measured across patterns above).
- Instrumented chain trace confirming onEntered/hoveredTimer/showPreviewWindow
  fire on pass and never fire on fail.
