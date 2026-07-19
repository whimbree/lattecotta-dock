# 2026-07-17 Phase 8 bottom-dock surface drift - running ledger

The plan item: "Bottom-dock layer surface drifts left of its reported
geometry" (docs/tracking/PORTING_PLAN.md). Reproduced as a first-class failing test,
tests/e2e/060-geometry-agreement.sh (was `# e2e-expect: fail`, XFAIL at
-44px).

## Root cause (proven)

A masked dock (behaveAsDockWithMask) keeps a window that spans the WHOLE
screen along its length axis - PositionerGeometry::windowSize gives a
horizontal masked dock the full screen width - and realises its
Left/Center/Right alignment INTERNALLY through its mask (the visible strip
sits inside the full-width window via localGeometry). The window never
moves.

The layer-shell backend anchored a Center dock to a SINGLE edge (bottom
only) and leaned on the compositor to centre it along the length axis. But
wlr-layer-shell centres a single-edge-anchored surface inside the region
left FREE by OTHER surfaces' exclusive zones, not the whole output. With a
left dock (48px zone) and a right dock (88px zone) present, the usable
horizontal band is [48, 1512], and a full-width (1600) surface centred in it
lands at 48 + (1464 - 1600)/2 = -20px. The dock's own geometry math
(PositionerGeometry::dockPosition, full-screen for horizontal docks) assumes
x=0. So absoluteGeometry, struts and input regions were self-consistent at
x=0 while the compositor drew the surface at -20 (icons render 20px off
where clicks land). The drift is exactly (leftZone - rightZone)/2,
independent of window width; it varies run to run because the side docks'
exclusive zones commit asynchronously during startup (so -20/-44/-74 as
different zones are or are not active when KWin recomputes the centring),
which is also why it "re-anchors" on any later re-commit (clock minute
tick).

Vertical docks do NOT drift: their window is sized to the AVAILABLE region
(reduced by top/bottom docks), so the compositor's usable-area centring is
an identity. Confirmed live (left/right docks agree, drift 0).

### Instrumented values (nested vehicle, Bree's real config, 1600x1000)

Temporary qCWarning in updateAnchoring/setExclusiveZone:

- bottom docks: anchors L=false R=false B=true, window QSize(1600, 384)
  (full screen width => masked dock), exclusive zone 88
- left dock: exclusive zone 48; right dock: exclusive zone 88
- viewsData view 16 (bottom center): abs [363,912,874,88] local
  [363,296,874,88] => reported window origin x = 363-363 = 0
- dumpwins bottom window: pos 24/-20/-44 x, size 1600x384 across runs
  (drifted), never matching the reported origin 0

### Variable isolation (the mandated one-variable proof)

Single bottom dock, NO side docks (a Default-template layout, alignment
center, maxLength 54): the bottom window sits at x=0, reported origin 0,
drift 0. Restore the side docks and the drift returns. The side docks'
exclusive zones ARE the variable.

### Fix-direction experiment

Temporarily anchoring the Center dock to BOTH length edges (left+right+
bottom): the full-width surface stays 1600 wide at x=0 (the compositor does
NOT shrink a both-edge-anchored surface to the usable band, and does NOT
recentre it). Drift -> 0. This is the fix.

## Fix

app/wm/waylandlayershell.cpp anchorsFor() takes a new
`windowSpansScreenLength` flag. When true (a horizontal masked dock), the
surface anchors BOTH length edges for every alignment - the compositor pins
it corner to corner across the full screen length, so its own full-screen
geometry math is honoured and no compositor centring happens. A sized panel
window (behaveAsPlasmaPanel) passes false and keeps the alignment-driven
near/far/both/single-edge anchoring. View::windowSpansScreenLength() returns
`!behaveAsPlasmaPanel() && formFactor()==Horizontal` (vertical masked docks
are sized to the available region, not the screen, and never drift, so they
stay single-edge). SubWindow edge strips (fraction-of-screen helpers) pass
false to keep their behaviour identical.

Panels (behaveAsPlasmaPanel Center) would still be centred by the compositor
inside the usable band; that is a distinct, unreported case (no horizontal
panel in the repro config - every horizontal surface measured full screen
width) and is left to a margin-based follow-up if it ever surfaces. The 060
guard would catch it.

## Desk-vs-vehicle determination

REAL, not a vehicle artifact - but config-dependent. The drift is a pure
function of side-dock exclusive-zone asymmetry (drift = (leftZone -
rightZone)/2), so it only manifests when a horizontal masked dock coexists
with side docks whose exclusive zones differ. The vehicle ran Bree's
multi-dock "My Layout" (left dock 48px, right dock 88px) and drifted -20px;
that is the same config the desk would drift on.

Live desk check (read-only, no restart, the --user-config dock left
untouched so nothing to restore): the desk was running a two-bottom-dock
layout with NO side docks. viewsData reported origin 0 (DP-3 portrait) and
1440 (DP-2 landscape); dumpwins showed the DP-3 dock at `0,2176 1440x384`
and the DP-2 dock at `1440,1481 2560x384` - both EXACTLY at their reported
origin, drift 0. Consistent with the root-cause formula (no side docks =>
zones 0 => drift 0), and it confirms the measurement method matches on the
real compositor. So Bree's current desk layout does not drift, but her
side-dock layouts do, off-centre by (leftZone-rightZone)/2 - exactly the
"easy to miss by eye" placement bug the plan item predicted.

## Before/after geometry-agreement

- Before: 060 XFAIL, view renders -44px (also seen -20/-74) off its reported
  origin.
- After: 060 PASS, all horizontal views drift 0 (bottom docks at x=0 =
  reported origin). Marker lines removed; 060 is a permanent standing guard.
