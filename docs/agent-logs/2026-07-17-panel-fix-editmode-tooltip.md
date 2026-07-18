# Panel fix: edit-mode tooltip eats the click on "Rearrange..." (2026-07-17)

Job B of docs/panel-issues-plan.md (issue 3). Worktree, branch
`panel-fix-editmode-tooltip`. Hashes here are pre-rebase; the merge
rewrites them.

## Symptom

In edit mode with little space, the tooltip over the "Rearrange..."
button intercepts the pointer and the click never reaches the button
(Bree's report, top panel). Same for the two Stick-on-Top/Bottom
buttons, which share the control.

## Root cause

The three settings-header buttons are the shell control
`shell/package/contents/configuration/canvas/controls/Button.qml`.
The real click target is an invisible (opacity 0) `PlasmaComponents.Button`
(`tooltipBtn`) filling the chip, and it carried an attached
`QQC2.ToolTip`:

    QQC2.ToolTip.text: button.tooltip
    QQC2.ToolTip.visible: hovered && button.tooltip.length > 0

This is the "a per-control QQC2.ToolTip maps its OWN surface at the
cursor" defect family (latte-plasma6-defect-families). On Wayland the
ToolTip is a separate popup surface positioned at the cursor. The
header lives in the edit-mode CANVAS OVERLAY, which CanvasConfigView
draws over the dock with a click-through input mask carved to leave
only the toggle's rect live. When space is tight the tooltip surface
lands directly over the button, and being a separate surface it takes
the pointer: the press hits the tooltip, not the button beneath it.

The identical class was already retired TWICE in sibling edit-mode
chrome, both with in-window hints and both carrying a "don't re-add a
QQC2.ToolTip here" comment measured live:
- `containment/.../editmode/ConfigOverlay.qml` (the ~20Hz edit-handle
  flicker that also ate clicks)
- `shell/.../configuration/CanvasConfiguration.qml` (the wheel/opacity
  hint chip)
Button.qml was simply never migrated with them.

## Fix (at root)

Remove the attached `QQC2.ToolTip` (and the now-unused
`QtQuick.Controls` import) and render the hint IN-WINDOW as a plain
`Rectangle` + `Label` inside the same window, shown after Plasma's
tooltip dwell while the button is hovered. A Rectangle/Label subtree
carries no input handler, so it is pointer-transparent: it still SHOWS
the hint but the press falls straight through to `tooltipBtn` beneath
it. This mirrors the wheel-hint chip in CanvasConfiguration.qml
(same dwell-timer guard so brief hover bounces during input-mask
re-carve never flash it). Colours come from `Kirigami.Theme` (qualified
access, so the qmllint curated count is unchanged).

Not a hide: the hint remains visible, per the requirement. The chip
anchors just past the button chip toward the header interior; the
header rotates for vertical docks, so button-local "below" tracks the
correct visual side automatically.

## Guards

1. Source scan `scripts/qml-tooltip-rules.sh`, ctest `qmltooltiprules`
   (registered in tests/CMakeLists.txt, added to
   tests/coverage/ratchet-baseline, count 82 -> 83). Fails if the
   edit-mode overlay click-target controls (Button.qml + ConfigOverlay
   handle buttons) re-grow an attached QQC2/Controls.ToolTip. This pins
   the actual Wayland-surface mechanism the offscreen engine cannot
   reproduce. Negative-tested: passes clean, fails when a
   `QQC2.ToolTip.text:` line is re-added, passes after restore.
2. Behavioral qmltest `tests/qml/tst_buttonhintclickthrough.qml` drives
   the REAL Button control offscreen and pins the observable half:
   the hint is an in-window `QQuickRectangle` (not a popup), its subtree
   carries no MouseArea/AbstractButton (structurally pointer-transparent),
   and a press at the button centre reaches the button. Catches the
   other regression direction: removing the in-window hint makes
   `findChild("buttonHintChip")` null and the test fails.

## Verification

- `scripts/qml-tooltip-rules.sh`: OK; negative-tested (re-added tooltip
  -> exit 1; restored -> exit 0).
- qml interaction harness: `ButtonHintClickThrough` 3/3 PASS against the
  real Button.qml; suite Totals 225 passed, 0 failed. The
  latteView/settingsRoot ReferenceErrors logged are the button's colour
  bindings (C++ context properties absent offscreen) and touch neither
  geometry nor input.
- qmllint ratchet: OK, baseline matched (Button.qml curated count
  unchanged at 16 - removed the QQC2 import, added only qualified
  access).
- gate-all.sh: <fill at commit time>

## Owed

- DESK-CHECK (real hands, top panel): with a top PANEL sized so edit
  mode is tight, enter edit mode, hover "Rearrange..." to raise the
  hint, and click it - confirm the click toggles configure-applets mode
  with the hint shown. Not driven live here: verifying the fix needs a
  restage+restart of the shared session, which would disrupt the other
  in-flight worktree (Job A) / the daily driver. The offscreen suite
  cannot reproduce the Wayland separate-surface grab, so this is the
  only faithful check of the grab being gone.
- SIBLING (same class, not fixed in this scope): the max-length ruler
  `shell/.../configuration/canvas/maxlength/Ruler.qml` (~317-323) still
  carries the same invisible-button + attached `QQC2.ToolTip` pattern
  on its drag handle, in the same canvas overlay. Owed migration to an
  in-window hint; intentionally left out of the qmltooltiprules scope
  until migrated (noted in the rule's header). Filed to
  docs/panel-issues-plan.md issue 3.
