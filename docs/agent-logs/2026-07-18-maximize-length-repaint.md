# 2026-07-18 maximize-length repaint (stale frosted band) - ledger

The bug: the panel option "maximize panel length in presence of maximized
windows" (`maximizeWhenMaximized`) grows a masked dock to full width while a
maximized window is present and shrinks it back on un-maximize. On Qt6 wayland
the just-vacated edge pixels render as a lighter frosted/semi-transparent band
at the FORMER extent - stale content the compositor keeps compositing. Caught
live on a real top dock.

## Root cause (proven, platform-forced)

Qt6's wayland backend couples `QWindow::mask()` to each frame's submitted
buffer damage: Qt restricts the damage it submits to the current mask region.
When the visible band SHRINKS, the scene graph repaints the vacated edge
transparent, but that damage falls OUTSIDE the new, smaller mask and is
dropped, so the compositor never learns those pixels changed and keeps
compositing the stale semi-transparent panel content there. Qt5/X11 shape
masks did not clip damage; this is a platform-forced Qt6 deviation with no
upstream precedent (the code and commit say so).

This is the same `QWindow::mask()`/damage coupling that the existing
`setInputMask` comment already documents for the empty-mask "whole surface
frozen at last content" case - the shrink case is its sibling.

## The fix

A pure core owns the "what region to hand `QWindow::setMask`" decision:
`app/view/inputmaskflush.h`, `Latte::ViewPart::InputMaskFlush`.

- `windowMaskFor(applied, band)`: a clear/degenerate band clears the mask; a
  first band with no prior applied mask is used as-is; otherwise the UNION of
  the applied region and the new band is kept. Growing collapses to the band on
  its own (a wider band already contains the old applied region, `united ==
  band`); a shrink stays wide (`united ==` the old, wider applied region) so the
  vacated edges stay inside the mask and their clearing damage is not clipped.
  It asserts its contract - `result.contains(applied)` - which `united()`
  satisfies by construction and a naive `return band` violates on a shrink.
- `needsSettleCollapse(applied, band)`: whether the applied mask is still wider
  than the band, i.e. the settle collapse is owed.

`Effects` (app/view/effects.cpp) routes `setInputMask` through
`applyInputMaskToWindow()`, keeps `m_appliedInputMask` (the region actually
handed to `setMask`), and arms a 100ms single-shot coalescing timer
(`m_inputMaskSettleTimer`, restarted on every band change) that, once the band
is quiet, narrows the window mask from the union back to the exact band so
steady-state hit-testing and libplasma popup anchoring read the real band.
`m_inputMask` still reports the logical band, so QML readback is unchanged.

Observability: `Effects::appliedInputMask()` and a new
`appliedInputRegionRects` array in `viewsData` report the applied window mask
so the union/collapse is queryable, not pixel-peeped (D-Bus reference +
observability docs updated; dbusreportstest pins the new key and its
empty-means-cleared convention).

## Verification

### Pure-core unit test + tripwire (SIGABRT)

`tests/units/inputmaskflushtest.cpp` (sanitized, ASan+UBSan, QT_FORCE_ASSERTS)
passes on the fix. It hand-derives every expected rect from the QRect union
geometry and pins the invariant as `shrinkKeepsUnionUntilSettle` /
`animatedShrinkNeverClipsVacatedEdges`.

Tripwire proven: reverting the core's `applied.united(band)` to a naive
`return band` (the shape both reference forks still ship) and running the
sanitized binary aborts on the FIRST shrink case:

```
QFATAL : InputMaskFlushTest::shrinkKeepsUnionNotBand() ASSERT: "result.contains(applied)"
         in file .../app/view/inputmaskflush.h, line 63
FAIL!  : InputMaskFlushTest::shrinkKeepsUnionNotBand() Received a fatal error.
Received signal 6 (SIGABRT)
```

Restored to `united()`, the binary is green again.

### Nested-vehicle integration (state + instrumentation)

Vehicle limitation found and recorded: `existsWindowMaximized` never flips in
the nested vehicle - this vehicle's kwin does not surface the plasma
window-management maximized state to Latte. A konsole cycled maximized (1464x824)
<-> normal (500x300) via KWin scripting left the dock's band unchanged at 641px,
measured. Live-writing `maxLength` to the layout file did not reload either (no
file watch), and pointer hover did not expand this config's band. So the literal
maximize-length feature is not drivable here.

The IDENTICAL band-shrink code path is the exact quantity `maximizeWhenMaximized`
overrides - `maxLength` - so I drove it through the edit-mode length ruler below
the applet extent (~54% here), where the band actually shrinks. On the
instrumented dock (temporary `qWarning` in `applyInputMaskToWindow` + the settle
lambda, since removed) each deliberate down-detent produced the union-then-collapse:

```
apply band=769x78 applied=874x78 settleArmed=true   [band shrank 874->769, applied HELD at the 874 union]
settle-collapse applied=769x78 band=769x78           [collapsed to the band ~105ms later]
apply band=641x56 applied=727x56 settleArmed=true   [727->641, applied held at the 727 union]
settle-collapse applied=641x56 band=641x56           [collapsed]
apply band=540x44 applied=641x44 settleArmed=true   [641->540, applied held at the 641 union]
settle-collapse applied=540x44 band=540x44           [collapsed]
```

The ~100ms union-hold is below D-Bus round-trip latency - rapid-sampling
`appliedInputRegionRects` right after a detent (8 samples/detent) never caught
`applied != input`, measured - so no automated D-Bus recipe can assert the
transient. The unit test is its tripwire.

Standing guard: `tests/e2e/070-maximize-length-mask.sh` drives the same
ruler-shrink and asserts per-view over D-Bus that after every shrink settles
the applied window mask has COLLAPSED back to the band (`applied == input`). A
settle that failed to fire would leave the applied mask stuck at the pre-shrink
union (`applied` wider than `input`) and fail. Passes on the clean fix; band
shrank 874 -> 769 -> 727 -> 641 -> 540 with the applied mask collapsed at
every step.

## Desk-check owed to Bree (real session, the "no frosted band" pixel confirm)

The nested vehicle cannot exercise the real maximize-length feature (see above),
so the visual confirmation is a desk-check on the live session:

1. A masked TOP or BOTTOM dock (a Latte dock, not a plasma panel), maxLength
   below full (e.g. 60%) so it is visibly shorter than the screen.
2. Its config: "Maximize panel length in presence of maximized windows" ON
   (Behavior / or `maximizeWhenMaximized=true` in the containment's General
   group).
3. Maximize a window on that dock's screen - the dock grows to full screen
   width.
4. Un-maximize it (restore) - the dock shrinks back to its ~60% band.
5. LOOK at the just-vacated edge regions (between the shrunken dock's ends and
   the screen edges), especially against a dark wallpaper: BEFORE the fix a
   lighter frosted/semi-transparent band lingers there at the former full-width
   extent; AFTER the fix those regions are clean (the vacated pixels clear).
   The band, if present, sits within ~100ms plus animation of the un-maximize.
6. Repeat the maximize/un-maximize cycle a few times; the artifact is most
   visible right after the shrink and on a busy/bright background behind the
   vacated edges.

Query while checking: `busctl --user call org.kde.lattedock /Latte
org.kde.LatteDock viewsData` and read the view's `appliedInputRegionRects` vs
`inputRegionRects` - they agree at rest and during the grow, and the applied
one is briefly wider during the shrink.
