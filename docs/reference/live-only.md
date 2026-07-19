# Live-only test registry

Per `docs/reference/TESTING.md`, a unit that genuinely cannot be verified headlessly
gets an entry here stating *why*, and becomes a target of live verification
(per-phase live testing, and the nested-vehicle e2e suite
`scripts/run-e2e.sh`). The registry exists so each gap is recorded instead of
papered over with a dishonest test. The bar for "genuinely" tightened once
that suite existed: needing a real compositor or a scriptable pointer gesture
is no longer live-only, so the entries below are narrowed to what truly needs
the real desk or a human judgment.

## Entries

### Wheel-to-volume on a task's audio badge
- **What:** scrolling over the audio badge on a task icon changes that
  application's playback volume.
- **Why live-only:** the badge only shows on a task bound to a client actually
  playing through the real PulseAudio/PipeWire graph, and the assertion is that
  stream's volume moving. The nested vehicle has real client windows but no real
  audio streams bound to tasks, so this case needs the real desk. The wheel path
  itself is NOT the gap: fakepointer has a `scroll` verb and the wheel reaching
  the tasks plasmoid is already proven by `tests/e2e/020-wheel-task-cycle.sh`
  (below); the missing piece is a task bound to a live audio stream.
- **Verify at:** a live session with a real audio-playing client.

The task-cycle and empty-area wheel behaviors this entry used to cover are no
longer live-only: `020-wheel-task-cycle.sh` (nested-only) drives a fakepointer
scroll over a grouped task icon and asserts the window cycles A -> B -> A on
KWin's activeWindow, `010-wheel-desktops.sh` asserts a wheel over empty dock
area switches the virtual desktop, and `030-wheel-ruler-maxlength.sh` asserts an
edit-mode ruler wheel steps maxLength. The wheel reaching the tasks plasmoid is
driven and asserted in the vehicle, so only the real-audio badge remains here.

### Phase 7 feel judgments: reorder jitter and hover-zoom smoothness
- **What:** drag-to-reorder stays smooth with no jitter or stuck icons;
  parabolic hover-zoom feels smooth, not stuttery.
- **Why live-only:** these are perceptual judgments about motion feel. The
  underlying math lives in unit-tested pure cores (the parabolic engine, drag
  classification) and the gestures are driven in the vehicle (below), but "does
  it feel smooth" is a human sign-off, not a headless assertion.
- **Verify at:** a live session, watching a real reorder and a real hover sweep.

The rest of the Phase 7 subsystem this entry used to claim as live-only is not.
Wayland pointer input IS scriptable (`scripts/tools/fakepointer.c`:
move/click/rightclick/drag/glide/scroll via `org_kde_kwin_fake_input`), and the
nested vehicle drives it against a real client surface:
- drag-to-reorder is driven by `050-drag-reorder-launchers.sh` (nested-only
  fakepointer drag, asserted on the `viewTasksData` launcher order flipping);
- hover-zoom and previews by `040-preview-tooltip-text.sh` and
  `parabolic-hover-preview.sh` (fakepointer glide onto the target icon);
- edit-mode entry/exit is drivable over D-Bus (`setViewEditMode`, read back as
  the `editMode` field of `viewsData`), and `030-wheel-ruler-maxlength.sh`
  already operates in edit mode.

Widget removal, widget-add-by-drag (explorer -> dock), widget-add-by-tap, and
the Latte Tasks widget-add crash case are recipe gaps, not infra gaps: the
vehicle can drive the gesture and assert the applet set via the `viewAppletsData`
readback, and the sanitized e2e gate (`scripts/asan-e2e-gate.sh`) would abort on
an add-crash; the recipes are just not written yet (the explorer -> dock drop is
tracked in PORTING_PLAN Phase 7).
