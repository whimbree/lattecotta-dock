# Panel issues + panel test matrix (plan)

Source: `docs/prompts/panel-issues.md` (Bree's raw report, 2026-07-17).
Confirmed with Bree before farm-out. This is a CHECKLIST, same discipline as
docs/multi-distro-ci-plan.md: every fix lands AT ROOT, WITH its regression
guard and recorded driving evidence (CLAUDE.md definition of done), never a
downstream bandaid.

Scope note: these are PANEL-mode issues (behaveAsPlasmaPanel), first seen on a
TOP panel. A bottom DOCK was NOT affected by #2 (dock vs panel mode differ);
bottom-panel and the two side edges are UNVERIFIED and must be checked. Every
fix is validated on ALL FOUR edges (top/bottom/left/right), dock vs panel where
the mode matters.

## Issue 1 - floating gap grows the panel instead of offsetting it
APPEARS: with the floating gap (screenEdgeMargin) enabled on a TOP panel, no
gap appears between the screen edge and the panel; instead the panel gets
taller (edit-mode blueprint expands downward), eating screen space.
IMPACT: the floating-panel look is broken on top; you just lose real estate.
GROUNDING: the gap is `screenEdgeMargin` / `screenEdgeMarginEnabled`
(containment/package/contents/ui/main.qml:263), routed through
abilities/Metrics.qml (`margin.screenEdge`, `mask.screenEdge` at ~33/78/80)
and BindingsExternal.qml (pushes `screenEdgeMargin` onto the window,
~106/114). HYPOTHESIS: for a behaveAsPlasmaPanel surface a true gap must be
realized as a layer-shell EDGE OFFSET (the compositor positions the surface
away from the edge), not folded into surface thickness/mask; the margin is
being absorbed into the panel geometry instead of translated into an offset.
NEXT ACTION: trace screenEdgeMargin -> window / wlr-layer-shell geometry for
behaveAsPlasmaPanel per edge; find where the gap becomes thickness; fix at
origin. Guard: a state/geometry assertion (and a render scene) that a
top/bottom/left/right panel with the gap leaves a REAL gap and does not grow.

## Issue 2 - system-tray applet popup opens on top of the panel, not under it
APPEARS: on a TOP panel, clicking a systray icon (e.g. volume) slides the
popup in OVER the panel, covering the icon, so clicking the icon again cannot
close it - you must click outside.
IMPACT: click-to-toggle-closed is broken; stock Plasma anchors the popup flush
under the bar so the icon stays live.
CONFIRMED (Bree): seen on a top PANEL; a bottom DOCK was fine; bottom panel +
sides unverified.
GROUNDING: expanded applet popups are PlasmaCore.Dialog surfaces; the
position/anchor relative to the panel edge/thickness is wrong. Likely coupled
to Issue 1 (if effective panel thickness/gap is miscomputed the popup anchor
inherits it) - investigate together.
NEXT ACTION: find where the applet-popup dialog position is computed against
the panel edge; anchor it OUTSIDE the panel band (below on top, above on
bottom, beside on sides), leaving the icon uncovered and clickable. Guard: an
e2e recipe that opens a systray popup and asserts the icon remains
hittable/toggles closed, on each edge.

## Issue 3 - edit-mode tooltip eats the click on "Rearrange..."
APPEARS: in edit mode with little space, the tooltip over the "Rearrange..."
button intercepts the pointer; the click never reaches the button.
IMPACT: cannot reliably invoke the control; you fight the tooltip.
GROUNDING: the "tooltip grabs pointer events" class. ConfigOverlay.qml already
carries comments about preferring an in-Dialog label over a popup hint and
about hover-driven resize moving buttons under the cursor (~451-488) - a
known-delicate area. The rearrange toggle lives in
shell/package/contents/configuration/CanvasConfiguration.qml
(rearrangeToggleRect). The hint item is a pointer-grabbing MouseArea/ToolTip
sitting above the button.
NEXT ACTION: make the hint non-interactive (HoverHandler / pointer-transparent
/ z below the button) so clicks fall through to "Rearrange...". Guard: a
click-through test that a click at the button rect reaches the button even
with the tooltip shown.
ROOT CAUSE + FIX (Job B, branch panel-fix-editmode-tooltip): the three header
buttons share shell/.../configuration/canvas/controls/Button.qml; the real
click target (invisible opacity-0 PlasmaComponents.Button) carried an attached
QQC2.ToolTip. On Wayland that ToolTip is a SEPARATE popup surface at the
cursor; in the click-through edit-mode overlay it lands over the button and
takes the press. Same defect family already retired in ConfigOverlay.qml and
CanvasConfiguration.qml. Fixed by removing the QQC2.ToolTip (and the unused
QtQuick.Controls import) and rendering the hint IN-WINDOW as a pointer-
transparent Rectangle+Label (still shown, dwell-gated). Guards: source scan
scripts/qml-tooltip-rules.sh (ctest qmltooltiprules) bans the attached ToolTip
on the overlay click targets; behavioral tests/qml/tst_buttonhintclickthrough.qml
drives the real control (hint is an in-window rectangle carrying no input
handler; press reaches the button). See
docs/agent-logs/2026-07-17-panel-fix-editmode-tooltip.md.
OWED: (1) desk-check the tight top-panel case with real hands - the offscreen
engine cannot reproduce the Wayland separate-surface grab. (2) SIBLING same
class not yet fixed: the max-length ruler
shell/.../configuration/canvas/maxlength/Ruler.qml (~317-323) still uses the
invisible-button + attached QQC2.ToolTip pattern on its drag handle in the same
overlay; owed an in-window-hint migration (left out of qmltooltiprules scope
until done).

## Issue 4 - real panel test coverage on all four sides (SEPARATE subagent)
Bree: full depth, as its OWN isolated subagent (keep it out of orchestrator
context). Panels currently have thin coverage (tst_maskgeometry,
backgroundstatetest behavesAsPlasmaPanel cases). Build the full matrix for
behaveAsPlasmaPanel on top/bottom/left/right: UNIT (geometry/margin/mask math),
SMOKE (loads/settles), RENDER (sceneprobe panel scenes per edge, with/without
floating gap), REGRESSION (encode the corrected behavior of 1-3 as guards), and
E2E (popup toggle, edit-mode click-through, autohide/struts per edge). Assert
STATE via D-Bus/geometry where possible; pixels only where pixels are the thing.

## Farm-out plan
- **Job A - panel geometry (issues 1 + 2), Opus worktree.** Coupled edge-
  geometry domain: floating gap becomes a real offset; popup anchors outside
  the band. Root-cause + fix at origin + targeted guards; verify nested vehicle
  on all 4 edges (dock vs panel where it matters); record desk-checks owed.
- **Job B - edit-mode click-through (issue 3), Opus worktree.** Independent
  (ConfigOverlay/CanvasConfiguration). Fix + click-through guard.
- **Job C - panel test MATRIX (issue 4), Opus worktree, SEQUENCED AFTER A+B
  land.** Runs against corrected master so its render goldens and regression
  guards capture the RIGHT behavior (no golden churn, no xfail gymnastics).
  Separate/isolated per Bree. This is the deliberate ordering, not a delay for
  its own sake.

Each job: branch off master, fix/build with recorded evidence, gate-all on the
branch head (these are CODE changes - real gate, not the docs-only shortcut),
push branch, report back. Orchestrator runs the independent lean Opus review
and lands each THROUGH GitHub as a Merged PR (rebase branch, push it, keep PR
open, ff-merge, push master while open).
