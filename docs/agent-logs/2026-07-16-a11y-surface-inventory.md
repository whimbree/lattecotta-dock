# Accessibility and keyboard surface inventory (2026-07-16)

Baseline inventory for the Phase 10 accessibility requirements
(docs/PORTING_PLAN.md, "Accessibility, keyboard and automation
requirements"). Read-only research pass: no code changed. Method:
grepped the shipped packages (containment/package, plasmoid/package,
shell/package, declarativeimports) plus app/settings and app/shortcuts
for interaction handlers (MouseArea, TapHandler, onClicked, onWheel,
drag), keyboard markers (Keys.on*, activeFocusOnTab, KeyNavigation,
focus: true, Shortcut, QAction/kglobalaccel), and accessibility markers
(Accessible.*, QAccessible); then read each interactive file around the
hits. The AT-SPI bridge question was answered from the pinned qtbase
6.11.0 source tarball in /nix/store (extracted to the session
scratchpad, not into the repo). tests/ skipped per scope.

Dead ends, so the next pass does not repeat them: there is no
QAccessible usage anywhere in app/ C++ (grep clean); the only
`focus: true` in the whole dock QML is
containment/package/contents/ui/editmode/ConfigOverlay.qml:29 and it is
inert (no Keys handler attached anywhere in containment/ or the
plasmoid task QML); qtbase's installed mkspecs/headers do not carry the
bridge-enable logic, only the source tarball
(qtbase-everywhere-src-6.11.0.tar.xz) does.

## 1. Interactive surface inventory

### Dock window proper (layer-shell, normally focus-refusing)

- Task items: plasmoid/package/contents/ui/task/TaskItem.qml with all
  input in task/TaskMouseArea.qml. Left/middle/right buttons
  (acceptedButtons line 19), hover with parabolic zoom and previews
  (onEntered line 65), press/release dispatch (142/176: right opens
  context menu at 169-171, middle runs middleClickAction at 190, left
  runs leftClickAction previews/present-windows/cycle at 209-221),
  wheel (250: task activation cycling or manual list scrolling).
  Drag-reorder of tasks/launchers lives in
  taskslayout/MouseHandler.qml:263 (drop area + drag). Keyboard: none
  in the item itself; Meta+number global shortcuts reach tasks from
  outside (see section 3).
- Applet items: containment/package/contents/ui/applet/AppletItem.qml.
  Hover via applet/ParabolicArea.qml:24 (parabolic + thin tooltip),
  wheel forwarding at AppletItem.qml:667; actual applet input goes to
  the hosted plasmoid. Keyboard: none at the Latte layer.
- Empty-area actions:
  containment/package/contents/ui/layouts/EnvironmentActions.qml.
  Middle-click closes active window (58), press+move drags the active
  window (64/81), double-click maximizes (98), wheel runs the
  configured scroll action or pager (116). Pointer-only.
- Task context menu: plasmoid/package/contents/ui/ContextMenu.qml:23 is
  PlasmaExtras.Menu, i.e. a native QMenu. Once open it has full arrow
  key/Enter navigation and QWidget-level AT-SPI for free. Opening it is
  pointer-only (right-click in TaskMouseArea.qml:169; no Menu key or
  Shift+F10 handling anywhere).
- Containment/applet context menu (right-click on the dock window):
  containment/package/contents/ui/ContextMenuLayer.qml wraps
  app/declarativeimports/contextmenulayerquickitem.cpp; a QMenu built
  in mousePressEvent (162) and shown with popup() (362). Same story:
  keyboard-fine once open, pointer-only to open.
- Group previews (windows preview dialog):
  plasmoid/package/contents/ui/main.qml:363-366, windowsPreviewDlg is a
  PlasmaCore.Dialog with Qt.WindowDoesNotAcceptFocus in both popup and
  tooltip modes, so nothing inside is keyboard-reachable by
  construction. Contents: previews/ToolTipInstance.qml close ToolButton
  (139-145), player prev/play/next ToolButtons (376-403), and
  previews/ToolTipWindowMouseArea.qml (left activates, middle closes,
  right opens the context menu, hover highlights the window, 25-49).
- Badges on tasks: task/AudioStream.qml:91-96 click toggles mute plus a
  wheel volume handler; ProgressOverlay/BadgeText are display-only.
  ShortcutBadge (containment applet/ShortcutBadge.qml and
  declarativeimports basicitem/ShortcutBadge.qml) is display-only,
  driven by holding Meta.
- Thin tooltip (abilities/ThinTooltip): display-only.
- Debug window (containment debugger/DebugWindow.qml): dev-gated,
  out of scope for this inventory.

### Edit mode

- Configure-applets overlay:
  containment/package/contents/ui/editmode/ConfigOverlay.qml is ONE
  MouseArea (19) doing coordinate hit-testing over all applets: hover
  picks currentApplet (107), press+drag rearranges (181), wheel (233).
  There are no per-applet Items to focus. Its floating tooltip hosts
  real PlasmaComponents.ToolButtons: configure (496-501), painting
  color-picker (line ~555), parabolic lock/unlock (~565), remove
  (~575). The `focus: true` at line 29 has no Keys handler; it does
  nothing today.
- Canvas chrome (the edit-mode window):
  shell/package/contents/configuration/canvas/SettingsOverlay.qml
  composes HeaderSettings.qml (three hand-rolled buttons at 68-146:
  stick-top, rearrange toggle, stick-bottom) and
  maxlength/Ruler.qml + RulerMouseArea.qml (max-length adjustment is
  wheel-only, RulerMouseArea.qml:33-39; no drag handles in this port).
  The canvas buttons are controls/Button.qml: a drawn Rectangle plus an
  invisible PlasmaComponents.Button overlay firing on pressedChanged
  (110-119) - clickable chip, no text/name exposed at the interactive
  element, no keyboard reachability in practice.
- CanvasConfiguration.qml:126-159 editBackMouseArea: wheel adjusts
  editBackgroundOpacity. Pointer-only. Note canvasconfigview.cpp:213
  masks the canvas input region down to the chrome, which any future
  keyboard-focus scheme has to respect.

### Settings windows (QML, layer-shell, DO take keyboard focus)

All inherit SubConfigView which sets layer-shell
KeyboardInteractivityOnDemand (app/view/settings/subconfigview.cpp:330)
and strips WindowDoesNotAcceptFocus (176).

- Primary config ensemble:
  shell/package/contents/configuration/LatteDockConfiguration.qml. Escape
  closes (169-173, forceActiveFocus at 175). Chrome: pin ToolButton
  (213), PlasmaComponents.TabBar with TabButtons (291-345), actions
  ComboBoxButton (487), remove/close Buttons (613-635). Pages
  (pages/BehaviorConfig, AppearanceConfig, EffectsConfig, TasksConfig)
  are built from QQC2-template-based Latte components (see section 2),
  so space/arrows work once a control is focused; but the pages also
  carry pointer-only bits: hover-swap value labels
  (AppearanceConfig.qml:170, 635, 682), the shadow color picker
  MouseArea (EffectsConfig.qml:250), and wheel-adjust ScrollArea
  wrappers (declarativeimports/components/ScrollArea.qml is a bare
  MouseArea).
- Secondary config window:
  LatteDockSecondaryConfiguration.qml:52-58, Escape closes, same
  control family.
- Widget explorer: shell/package/contents/views/WidgetExplorer.qml in
  app/view/settings/widgetexplorerview.cpp (SubConfigView subclass, so
  focusable; requestActivate at 163). Search field grabs focus
  (376-399), GridView is activeFocusOnTab with a highlight (449-461).
  Delegates (views/AppletDelegate.qml): TapHandler tap adds the applet
  (53-59), DragArea drags it to the dock (34), remove-instances and
  uninstall ToolButtons appear on the current item (150, 158). No
  Keys.onReturn on the GridView, so keyboard can reach and move the
  highlight but cannot add a widget.
- Applet alternatives:
  shell/package/contents/explorer/AppletAlternatives.qml. The best
  surface in the tree: Escape/Return/Enter Shortcuts (60-71), focused
  ListView with highlight (112-127), delegates are
  PlasmaComponents3.ItemDelegate with Accessible.name/description
  (134-135). Hosted in a PlasmaCore.Dialog with
  hideOnWindowDeactivate (40).
- Applet popups: shell/package/contents/applet/CompactApplet.qml
  dialog: mainItem focus: true, Keys.onEscapePressed collapses
  (288-294), focus handed to the applet fullRepresentation (308-311).
  Reachable from the keyboard via Meta+number (activateEntry toggles
  expandable applets) and the KWin Meta forward for the launcher.
- Info view: app/infoview.cpp:167 sets KeyboardInteractivityNone;
  shell/package/contents/views/InfoView.qml is logo + message label,
  nothing interactive. Fine as-is.

### QWidget dialogs (app/settings/)

settingsdialog, viewsdialog, detailsdialog, actionsdialog,
exporttemplatedialog, screensdialog: classic QDialog + .ui trees.
Dense QAction shortcut coverage: Ctrl+S apply / Ctrl+L reset
(settingsdialog.cpp:81-82), file menu Ctrl+Alt+I/E, Ctrl+Alt+S,
Ctrl+Q (134-156); layouts tab Ctrl+Tab switch, Ctrl+A activities,
Ctrl+N/D new/duplicate, Delete, Ctrl+B, Ctrl+R, Ctrl+P, Ctrl+T,
Ctrl+I/E import/export (tablayoutshandler.cpp:120-257); views dialog
mirrors the family plus cut/copy/paste
(viewshandler.cpp:72-195, viewscontroller.cpp:105-113). QWidget gives
these keyboard focus and AT-SPI for free; the open work there is an
Orca verification pass (custom-painted table delegates included), not
new plumbing.

## 2. Existing accessibility state

Five Accessible usages in the entire tree, all inherited from copied
plasma-desktop code or the components library:

- declarativeimports/components/Label.qml:30-31 (role StaticText, name
  from text; activeFocusOnTab false at 17)
- shell/package/contents/views/WidgetExplorer.qml:357 (Accessible.name
  on the get-new-widgets button)
- shell/package/contents/explorer/AppletAlternatives.qml:134-135

No QAccessible / QAccessibleInterface anywhere in C++. Nothing on task
items, applet items, badges, previews, edit mode, or the config pages.

What comes free vs not: the QWidget dialogs get full AT-SPI from Qt's
widget bridge. The QML config windows get whatever the
QQC2-template-based controls provide (T.Slider, T.ComboBox,
T.CheckDelegate, PlasmaComponents CheckBox/Switch/SpinBox/TextField
carry template focus policy, key handling and accessible roles;
Slider/ComboBox even have focus visual states,
components/Slider.qml:83, ComboBox.qml:351). The hand-rolled pieces do
NOT: ComboBoxButton (Rectangle root), HeaderSwitch (Item root whose
real Switch is covered by an invisible ghost Button firing on
pressedChanged, HeaderSwitch.qml:103-121), the canvas
controls/Button.qml chip, ScrollArea, AddItem. The layer-shell dock
window itself renders plain QQuickItems with zero Accessible attached
properties, so its AT-SPI subtree is effectively empty.

AT-SPI bridge activation (from the pinned qtbase 6.11.0 sources,
src/gui/accessible/linux/dbusconnection.cpp:99-103 and
qspiaccessiblebridge.cpp:36-47): the bridge lives in QtGui and enables
per-process when org.a11y.Status reports ScreenReaderEnabled or
IsEnabled, or when QT_LINUX_ACCESSIBILITY_ALWAYS_ON is set. Nothing is
plasmashell-specific; our binary gets the bridge the moment Orca runs.
QT_LINUX_ACCESSIBILITY_ALWAYS_ON is the switch for Orca-less testing
(e.g. asserting on the tree from e2e). The real constraint for focus
EVENTS is window activation: a layer-shell surface with
KeyboardInteractivityNone never has Qt focus, so nothing in the dock
window can emit AT-SPI focus changes today.

## 3. Existing keyboard map

Global (kglobalaccel, app/shortcuts/globalshortcuts.cpp):

- Meta+` show dock/panel (86-92)
- Meta+A cycle dock settings windows (95-101)
- show layouts editor: registered, no default binding (104-111)
- Meta+1..9 activate entries 1-9 (116-129); Meta+0,Z,X,C,V,B,N,M,
  comma, period for entries 10-19 (132-144). Verified: this is the
  "Meta+number already works" family the plan names. It routes through
  ContainmentInterface (activateEntry / activatePlasmaTask) and also
  toggles expandable applet popups, so it is the one existing keyboard
  path INTO dock content.
- Meta+Ctrl+same keys: new instance for entries 1-19 (147-170)
- Meta press-and-hold: shortcut badges + show views (172-189,
  modifiertracker); badge text mirrors kglobalshortcutsrc via
  shortcutstracker.cpp:112.
- KWin-forwarded Meta: D-Bus org.kde.lattedock activateLauncherMenu
  (app/dbus/org.kde.LatteDock.xml:4, lattecorona.cpp:1084) opens the
  application launcher applet.

In-window keys: Escape in both config windows
(LatteDockConfiguration.qml:169, LatteDockSecondaryConfiguration.qml:54),
Escape in applet popups (CompactApplet.qml:290), Escape/Return/Enter in
AppletAlternatives (60-71), tab/arrow/space behavior inherited by the
QQC2-based controls, GridView tab stop in WidgetExplorer (454), full
QAction sets in the QWidget dialogs (section 1).

Pointer-only today: every interaction on the dock window itself (task
click/middle/wheel/drag-reorder, opening either context menu, empty
area actions), the whole previews dialog, all of edit mode (rearrange,
ruler, canvas buttons, applet handle tooltip buttons, background
opacity wheel), widget-explorer add (tap/drag only), the config pages'
hover-labels and color picker, and drag-and-drop everywhere.

## 4. Gap list, prioritized

P0 - foundations, everything else depends on them:

- Focus mode for the dock window. app/view/view.cpp:802-841 flips the
  window to focusable only on AcceptingInputStatus;
  app/wm/waylandlayershell.cpp:203-209 is the switch. Keyboard
  navigation of dock items needs a deliberate enter/leave keyboard-nav
  mode (a global shortcut that sets focus policy OnDemand, focuses the
  first item, Escape leaves), because AT-SPI focus events require real
  Qt focus. Design it once, together with the D-Bus
  enter/exit-edit-mode trigger the observability item already files.
- Task/applet traversal + roles. TaskItem.qml and AppletItem.qml gain
  Accessible.role (Button), Accessible.name (app name + window count
  for tasks, applet title for applets), Accessible.description carrying
  badge state (AudioStream/Progress/BadgeText values), and
  Accessible.onPressAction. Focus order comes from the existing
  Indexer ability (containment abilities/Indexer.qml) - arrow keys move
  along the visual index, Enter activates, Menu/Shift+F10 calls the
  already-existing showContextMenu()/createContextMenu paths
  (TaskMouseArea.qml:169, plasmoid main.qml:1588). Both menus are
  QMenu-backed, so they are keyboard/AT-SPI complete once openable.

P1 - previews dialog: drop Qt.WindowDoesNotAcceptFocus (plasmoid
main.qml:365-366) when opened via keyboard (or always in popup mode),
arrow between ToolTipInstances, Enter = activate, Delete = close
window; the close/player ToolButtons are already focusable types.
Announce window titles per instance.

P2 - edit mode: the ConfigOverlay single-MouseArea design
(ConfigOverlay.qml:19) has no items to focus; keyboard rearrange is
cleanest as focus-per-applet in the blueprint plus move-left/right
keys, not as synthetic pointer math. Canvas chrome: replace the
invisible-ghost-button pattern (controls/Button.qml:110-119,
HeaderSettings.qml:68-146) with real named buttons; ruler gains
+/- key equivalents beside the wheel (RulerMouseArea.qml:33) and an
Accessible value announcement. The input-region mask
(canvasconfigview.cpp:213) must widen while keyboard mode is active.

P3 - config windows (best baseline, cheapest wins): define tab order
on the pages, add Accessible.name to ComboBoxButton/HeaderSwitch (and
fix HeaderSwitch so the ghost button carries the header text as its
accessible name), keyboard equivalents for the hover-value labels
(AppearanceConfig.qml:170/635/682) and the shadow color picker
(EffectsConfig.qml:250), verify the TabBar arrows and visible focus on
every custom control. Same treatment for indicator config pages loaded
into the ensemble.

P4 - explorers: WidgetExplorer GridView gains Keys.onReturn -> addApplet
and per-delegate Accessible.name/description (model has both);
AppletAlternatives only needs a role check, it is otherwise the model
to copy.

P5 - QWidget dialogs: no new plumbing expected; schedule the Orca pass
and check the custom table delegates announce their cell text.

Cross-cutting: pin per-surface keys with offscreen qmltests where
feasible (plan requirement), land the shortcut map in end-user docs,
and use QT_LINUX_ACCESSIBILITY_ALWAYS_ON + the AT-SPI tree as the
deterministic e2e assertion surface the plan's third item wants.
