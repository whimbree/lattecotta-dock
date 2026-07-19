# Accessible.* rollout (2026-07-17, branch accessible-rollout)

The accessibility quartet's third item (Phase 10 "Full AT-SPI support"
in docs/tracking/PORTING_PLAN.md): Accessible attached properties across the
dock's interactive QML, per the gap list in
docs/agent-logs/2026-07-16-a11y-surface-inventory.md. Base already
carried the keyboard focus mode (the P0 gate); this rollout makes the
focused entry's Accessible state track it and gives every covered
surface role/name/description and a press action forwarding to the
same handler the click uses.

Branch commits (worktree hashes - re-resolve at merge):

- 8e709d26a feat(tasks): compose what a screen reader says about a
  task's badges and group
- daa29cc2f feat(plasmoid): tasks and the audio badge announce
  themselves to screen readers
- 39c518da1 feat(containment): applet containers announce their applet
  to screen readers
- 347e98e7d feat(components): HeaderSwitch and ComboBoxButton announce
  their labels to screen readers
- 8dd9064a1 feat(shell,containment): edit-mode chrome announces its
  function to screen readers
- e23ab2657 feat(shell): widget-explorer cards announce and add their
  widget for screen readers
- (docs commit follows this file)

## Surfaces covered

- Task items (TaskItem.qml): role Button, name = thin-tooltip fields
  (title / app name), description = the new composer (launcher kind,
  group window count, audio/progress/info badge values - the SHOWN
  state TaskIcon draws), press = activateTask(), focused =
  isKeyboardFocused, separators ignored.
- Audio badge (AudioStream.qml): the checkable Mute control, same
  msgid as the context menu item, press/toggle = toggleMuted() behind
  the same enabled gate as the click.
- Applet containers (AppletItem.qml): role Button, name = plasmoid
  title, press = toggleExpanded() (factored once; also serves both
  Meta+number handlers and neutral-area clicks), focused =
  isKeyboardFocused; pruned when no applet / separator / spacer /
  hidden / splitter / tasks-plasmoid container (its items announce
  individually).
- Settings custom controls: HeaderSwitch's switch ghost is the
  accessible checkbox (header text as name, tooltip as description,
  live checked, press/toggle raise item.pressed()); its text ghost and
  the visual Switch are pruned. ComboBoxButton names both halves from
  the visible buttonText (the checkable variant blanks the real text
  to " ").
- Edit-mode chrome: canvas controls/Button.qml's real click target
  (the invisible tooltip button) announces text/tooltip/checked and
  replays the pressedChanged cycle on AT press; ConfigOverlay's four
  handle ToolButtons reuse their sizing TextMetrics hints as names.
- Widget explorer cards (AppletDelegate.qml): role Button, model
  name/description, focused mirrors GridView currentItem, press =
  addWidget() (shared with the tap).

## Deferred, with reasons

- Group previews dialog (ToolTipInstance close/player buttons): the
  dialog is Qt.WindowDoesNotAcceptFocus in both modes, so nothing in
  it can receive AT focus today; labeling it now has no user-visible
  effect and the needed strings would have to move to C++ first (see
  the ratchet note below). Lands with the inventory's P1 previews
  keyboard/focus rework.
- Per-instance labels for template controls on the config pages
  (Slider/ComboBox/SpinBox next to Labels): roles come free from the
  QQC2 templates; associating each instance with its visible label is
  a page-by-page pass across pages/*.qml - the inventory's P3 tab-order
  item, not this rollout.
- ScrollArea (wheel-only MouseArea wrapper): nothing focusable to
  annotate; keyboard equivalents are the keyboard-navigation item.
- WidgetExplorer GridView Keys.onReturn: keyboard item (the AT press
  action this rollout added covers the screen-reader path).
- ConfigOverlay's rearrange MouseArea: single coordinate-hit-testing
  MouseArea with no per-applet items to focus; the inventory's P2
  keyboard-rearrange design owns it.
- Full TaskItem offscreen instantiation for a direct attached-property
  pin: needs the whole abilities graph + tasksModel delegate context +
  smart-launcher backend; judged not feasible at sane cost. The
  composed values are pinned through the shipped singleton instead
  (tst_taskaccessible.qml) and the attached wiring is walked in the
  Orca desk script.

## Pins landed (all offscreen, in the qml-interaction suite)

- tests/qml/tst_taskaccessible.qml: task description composition and
  the Mute label through the SHIPPED LatteTasks.TooltipTextComposer -
  the exact call TaskItem's binding performs. Twin C++ vectors run
  sanitized in tests/units/tooltiptexttest.cpp (plan-level token
  table + wrapper strings + malformed-bag refusals).
- tests/qml/tst_accessiblecontrols.qml: HeaderSwitch ghost checkbox
  (role/name/description/live checked) and press/toggle raising
  item.pressed(); ComboBoxButton naming both halves.
- tests/qml/tst_addwidgetsaccessible.qml: the REAL AppletDelegate in a
  GridView harness - role/name/description, focus mirror both ways,
  press action = one addApplet() + refresh, same as a tap.

## Mechanisms worth remembering (found the hard way)

- The qmllint ratchet counts EVERY context-chain read as unqualified -
  including i18n() calls and any read of an outside id from inside a
  Loader sourceComponent's inline component. Consequences that shaped
  this rollout: every new user-visible string lives in C++
  (TooltipTextComposer), the owning TaskItem threads to the audio
  badge through typed properties bound where ids are local, and the
  sourceComponent boundary is crossed with a document-scope
  Qt.binding in onLoaded. The gate caught the one violation attempt
  (+1 on TaskIcon) immediately.
- Qt's native AT press on a QQC2 button emits clicked() ONLY
  (QQuickAbstractButtonPrivate::accessiblePressAction -> trigger(),
  read from the pinned qtdeclarative 6.11.1 sources, extracted to the
  session scratchpad). Ghost buttons whose consumers listen to
  pressed/pressedChanged therefore NEED explicit
  Accessible.onPressAction handlers; declaring one also suppresses
  the native path (attached handlers win in
  QAccessibleQuickItem::doAction), so there is no double fire. Buttons
  wired through onClicked (ConfigOverlay handles, ComboBoxButton's
  main button) need no handler.
- Delegate required-properties mode switches OFF bare context-injected
  role reads. Adding `required property var model` to a delegate that
  still reads bare `decoration`/`index` silently breaks those reads -
  they must be converted in the same change (AppletDelegate commit).
- Attached Accessible properties ARE readable and their action signals
  emittable from another object via `id.Accessible.x` in a qmltest -
  that is what makes offscreen pinning of this surface possible.

## Gate state at branch head

Full qml-interaction suite 220 passing (5 new tests across 3 files),
qmlcompilegate green, qmllint ratchet green with two baseline shrinks
landed in their commits (AppletItem 158 -> 155, AppletDelegate
39 -> 33; total curated 6020 -> 6011), tooltiptexttest green
sanitized. gate-all.sh run after the docs commit - exit code is the
verdict, stamp on the branch head. Not pushed (per task).

## What acceptance still needs

The Orca desk script (docs/reference/manual-flake-removal-testing.md, "Orca
screen-reader pass") - announcement is a runtime pipeline only real
ears can accept. The plan item stays unticked until that pass and the
deferred surfaces land.
