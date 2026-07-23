# org.kde.LatteDock observability interface design

Written 2026-07-16 for the Phase 10 observability-first requirement:
"design the additions as one reviewed interface, not accreted
one-offs." This is that review. The safety rules from the plan item
and CLAUDE.md bind every entry: read surfaces expose STATE, never
execution; mutating surfaces stay coarse-grained user actions or sit
behind an explicit debug gate; nothing exposes secrets or other
applications' content (window titles ARE other applications' content -
see the tasks section). D-Bus is the pull surface; per-frame data and
high-rate traces stay in categorized logging.

## What already exists (the seed)

Landed before or during the 2026-07-16 stabilization session:

- Actions (coarse, user-equivalent): quitApplication, addView,
  removeView, duplicateView, createLinkedView, setViewPlacement,
  moveViewToLayout, exportViewTemplate,
  switchToLayout, importLayoutFile, showSettingsWindow,
  toggleHiddenState, setAutostart, updateDockItemBadge,
  activateLauncherMenu, setBackgroundFromBroadcast(+enable),
  windowColorScheme.
- Reads: contextMenuData(u), viewTemplatesData(),
  lifecycleState() -> startup/running/quit-requested/unloaded,
  viewAppletsOrder(u containmentId) -> as.

## Design decisions

1. **JSON strings for structured reads.** The existing interface
   marshals lists as QStringList with ";;" and "**" joiners
   (contextMenuData) - workable for one consumer, hostile to tests.
   New structured reads return a single JSON string (type "s"),
   documented here. JSON is self-describing, diffable in test
   failures, and adding a field never breaks an existing consumer.
   No a{sv} dicts: busctl-side ergonomics and test code both read
   JSON more cheaply.
2. **Containment id is the stable key.** Every view read keys on the
   containment id (uint), same as every existing action. Clones
   carry isClonedFrom.
3. **Reads never construct.** A query for a missing id returns an
   empty result and logs a qWarning (established by viewAppletsOrder);
   it never creates views, never touches config.
4. **No pixels, no window content.** Anything that would need a
   screenshot stays out; the sceneprobe/e2e harness owns pixels.
5. **The debug gate.** Fine-grained mutations useful only for tests
   (e.g. forcing a visibility state) require the dock to run with
   LATTE_DEBUG_DBUS=1 (env, checked once at startup, logged loudly).
   `reloadView` uses this gate. The environment value is read once while
   Corona is constructed and enablement is logged at warning severity.

## Read surface (new)

- `viewsData() -> s` (JSON array; the workhorse). Per view:
  ```json
  {
    "containmentId": 1, "layout": "My Layout",
    "isCloned": false, "isClonedFrom": -1,
    "type": "dock|panel", "screen": "DP-2", "onPrimary": true,
    "edge": "bottom|top|left|right",
    "alignment": "center|left|right|top|bottom|justify",
    "visibilityMode": "alwaysVisible|autoHide|dodgeActive|...",
    "isHidden": false, "inStartup": false, "isOffScreen": false,
    "absoluteGeometry": [x, y, w, h],
    "localGeometry": [x, y, w, h],
    "screenGeometry": [x, y, w, h],
    "strutsThickness": 88, "publishedStruts": [x, y, w, h],
    "maskRect": [x, y, w, h], "inputRegionRects": [[...], ...],
    "appliedInputRegionRects": [[...], ...],
    "editMode": false, "inConfigureAppletsMode": false
  }
  ```
  This single call replaces the pixel-peeping used across the whole
  stabilization history: dumpwins cross-checks, mask/input-region
  debug windows, the struts hunt's KWin clientArea probes, the
  lock/unlock "is it actually visible" question, and the
  startup-stranding watchdog's state dump (inStartup/isOffScreen are
  exactly the stranded bits).
  `inConfigureAppletsMode` is the effective per-view value, matching QML's
  `editMode && universalSettings.inConfigureAppletsMode`, not the raw global
  toggle. A global rearrange session therefore does not report unrelated docks
  as configuring applets.
- `dockSystemData() -> s` (compact JSON object, schema version 2; added by C0
  (the atomic dock-system observability snapshot)). This is the relational
  all-docks read. One synchronous call captures the global configuration mode
  and every current dock, then serializes docks in ascending
  `persistentDockId` order. `snapshotSequence` is a process-local monotonic
  decimal string; it identifies calls, not state changes.
  ```json
  {
    "schemaVersion": 2,
    "snapshotSequence": "14",
    "globalConfigureAppletsMode": true,
    "stacking": {
      "available": false,
      "reason": "No runtime authority models same-edge stack order or accumulated offsets."
    },
    "views": [{
      "runtimeViewId": "9",
      "persistentDockId": 17,
      "logicalDockId": 4,
      "originalDockId": 4,
      "relationship": "linkedMember",
      "linkPlacement": "explicitTarget",
      "screensGroup": "single",
      "linkedDockIds": [],
      "layout": "My Layout",
      "screenId": 2,
      "screen": "DP-2",
      "onPrimary": false,
      "type": "dock",
      "edge": "left",
      "orientation": "vertical",
      "alignment": "top",
      "maximumLengthRatio": 0.8,
      "offsetRatio": 0.0,
      "configuredIconSize": 64,
      "effectiveIconSize": 52,
      "availablePrimaryLength": 900,
      "normalThickness": 72,
      "maximumNormalThickness": 96,
      "windowGeometry": [x, y, w, h],
      "absoluteGeometry": [x, y, w, h],
      "localGeometry": [x, y, w, h],
      "screenGeometry": [x, y, w, h],
      "canvasGeometry": [x, y, w, h],
      "effectsRect": [x, y, w, h],
      "appletsLayoutGeometry": [x, y, w, h],
      "maskRect": [x, y, w, h],
      "inputMask": [x, y, w, h],
      "appliedInputMask": [x, y, w, h],
      "strutsThickness": 48,
      "publishedStruts": [x, y, w, h],
      "visibilityMode": "dodgeActive",
      "isHidden": false,
      "inStartup": false,
      "isOffScreen": false,
      "inRelocationAnimation": false,
      "inRelocationShowing": false,
      "geometrySettled": true,
      "relocationGeneration": "7",
      "appliedRelocationGeneration": "7",
      "inDelete": false,
      "inReadyState": true,
      "editMode": true,
      "effectiveConfigureAppletsMode": true,
      "settingsWindowShown": true,
      "objects": {
        "view": "object-9",
        "containment": "object-10",
        "configuration": "object-11",
        "layout": "object-12",
        "layoutController": "object-13",
        "geometryController": "object-14",
        "editController": "object-15",
        "configWindow": "object-16"
      }
    }]
  }
  ```
  `persistentDockId` is the Plasma containment id. `logicalDockId` is the
  direct root containment id for a linked member and otherwise equals the
  persistent id. `originalDockId` is null except on linked members.
  Independent duplication carries no durable relation to its source. The
  relationship values are `independent`, `linkedRoot`, and `linkedMember`.
  Linked roots carry their canonically sorted direct member ids in
  `linkedDockIds`; other records carry an empty array.

  A linked member's `linkPlacement` is `screenGroupDerived` for automatic
  output fanout or `explicitTarget` for the first-class Create Linked action.
  The field is null on independent docks and linked roots. A derived member's
  output assignment belongs to the root screen-group policy. An explicitly
  placed member owns its output, edge, alignment, visibility, appearance, edit
  presentation, and removal while applet membership and settings remain linked.

  `runtimeViewId` and every `objects` value are opaque process-local identities.
  They are assigned monotonically on first observation, remain stable for the
  QObject's lifetime, and never expose an address. Null controller tokens mean
  that authority is not live. The configuration authority is required, so a
  null configuration token or `configuredIconSize` is a logged defect rather
  than an ordinary startup state. The configuration window is legitimately
  null while closed; QML controllers and their `effectiveIconSize` or
  `availablePrimaryLength` values may be null during startup or teardown.
  `configuredIconSize` comes from the containment's live configuration map;
  `effectiveIconSize` comes from the live Metrics object;
  `availablePrimaryLength` is the layouter's current logical-pixel
  `contentsMaxLength`, the post-chrome applet span the autosizer consumes.
  It is smaller than raw containment `maxLength` when the background owns
  primary-axis end padding or shadow margins.
  `screensGroup` is always a string in a valid response. A derived member
  reports its root's screen-group policy. An explicitly placed member reports
  its local `single` policy. Whole-graph validation requires every linked
  member to point directly to a present linked root. Missing targets,
  independent targets, duplicate persistent ids, member chains, and cycles
  invalidate the complete query. The collector logs every lineage input at
  critical severity and returns an empty D-Bus string, never a smaller but
  plausible JSON snapshot. The visibility, startup, relocation, deletion, and
  ready fields come directly from View, Positioner, and VisibilityManager.
  Placement generations are process-local decimal strings. A delayed completion
  may apply only when its captured generation still equals
  `relocationGeneration`; `appliedRelocationGeneration` records the last accepted
  completion. `geometrySettled` additionally requires equal generations, no
  pending relocation or reveal, no slide, screen agreement, and drained screen,
  geometry, and validation timers. This is the convergence authority for tests,
  not an inference from a momentarily unchanged rectangle.

  The object identities name, respectively, the View window, Plasma
  containment, live KConfig property map, GenericLayout, QML layout manager,
  Positioner, containment root that owns edit state, and shared configuration
  window. Equal tokens prove shared ownership or accidental cross-dock reuse.
  `effectiveConfigureAppletsMode` is derived from the same per-view expression
  as QML. The raw global bit appears only once at the snapshot root.

  Every geometry array is `[x,y,w,h]` in Qt logical pixels, before any output
  device-pixel scaling. `windowGeometry`, `absoluteGeometry`, `screenGeometry`,
  `canvasGeometry`, and `publishedStruts` use virtual-desktop coordinates.
  `localGeometry`, `effectsRect`, `appletsLayoutGeometry`, `maskRect`,
  `inputMask`, and `appliedInputMask` use dock-window-local coordinates.
  `normalThickness`, `maximumNormalThickness`, and `strutsThickness` use the
  same logical-pixel unit.

  `stacking.available` is deliberately false. The current runtime has no
  explicit same-edge stack-order or accumulated-offset model. Canonical array
  order is serialization order only and must never be treated as layer-shell
  stack order. A later stack model requires a new authoritative runtime record,
  not inference from containment ids, pointers, or creation order.
- `viewAppletsData(u containmentId) -> s` (JSON array, in visual order).
  Per applet: id, plugin, index in layout, geometry within the view,
  expanded state, inScheduledDestruction, lockedZoom, colorizingBlocked,
  colorizerActive + colorizerReason (D21, added 2026-07-18: whether the
  colorizer scheme is EFFECTIVELY pushed into the applet's own colour
  group, and the single winning reason - applied / notEngaged / splitter
  / selfColored / userBlocked / inlineFull; colorizingBlocked
  stays the narrower user-opt-out list),
  z (stacking order). The per-applet `id` is the stable Plasma instance
  id, so two applets of the SAME plugin are distinguishable by id and
  their order is unambiguous (the G1 disambiguation readback,
  docs/tracking/e2e-interaction-test-plan.md - retires the same-plugin ambiguity
  in the F2/F3/A1/A2 add/reorder/abort scenarios). The `z` field (added
  2026-07-18 with the applet-reorder driver, C-I7/P6) is the G2 stacking
  readback: the applet's AppletItem delegate z, 0 at the layout default
  and lifted to 900 by ConfigOverlay while a reorder drags it. An abort
  that strands the dragged applet over the edit chrome (the 480ae30e3
  "icons stuck over chrome" residue class, e.g. an edit-exit mid-drag)
  shows here as a delegate left at z>=900, so the residue is queryable
  instead of golden-only (D2 in docs/tracking/known-defects.md was confirmed live
  through exactly this field). Supersedes viewAppletsOrder for rich
  asserts.
- `viewAppletsOrder(u containmentId) -> as`: the cheap companion to
  viewAppletsData - the same applets' instance ids (NOT plugin strings)
  in visual order. Justify-splitter sentinels (JUSTIFYSPLITTERID = -10,
  layout artifacts that own no applet) are EXCLUDED, the same line
  viewAppletsData draws, so the two order readbacks agree on justify
  views. Cheap, stable, already used.
- `viewDropMarkerIndex(u containmentId) -> i` (added 2026-07-18 with the
  e2e interaction suite, C-I4/P4). The live drop-marker readback (the G3
  gap, docs/tracking/e2e-interaction-test-plan.md): the drag placeholder spacer's
  (dndSpacer) current visual insert index while a drag hovers the view,
  or -1 when no marker is live (the spacer parked off the layouts at
  rest and after any drop or abort). Index 0 is the LEADING insert
  position, a live marker - only a negative value means "no marker" (the
  DbusReports::dropMarkerIsLive predicate pins that boundary, pinned by
  dbusreportstest). This is the direct insert(-1) observability: an
  add/reorder ABORT asserts it reads -1 afterwards, so a cancelled or
  misdirected drag that stranded an orphan placeholder is queryable, not
  golden-only. -1 also for a bad containment id (warned in the log).
- `viewTasksData(u containmentId) -> s` (JSON array, tasks plasmoid
  views only). Per task item: index, appId/launcherUrl, isLauncher,
  isGrouped + childCount, isActive, isMinimized, demandsAttention,
  badge value, item geometry. NO window titles by default: titles are
  other applications' content. A test asserting on window identity
  uses appId; the Orca/AT-SPI tree is where human-readable names
  belong (and reading THAT is the at-spi harness's job, not D-Bus).
  G4: index + appId ARE the window-task ORDER readback (index the model
  row that moves on tasksModel.move, appId the stable identity that
  travels with its window across a reorder), so the F4/A3 window-task
  scenarios track a window by appId and compare its index before/after a
  drag - no new field, the pair was already reported (pinned by
  dbusreportstest windowTaskOrderReadbackTracksAppIdAcrossReorder).
  Launchers reorder through the identical path and additionally persist
  their order to the tasks-applet `launchers` config key.
- `viewSettingsControlsData(u containmentId) -> s` (JSON array, added by
  SC-O1 (the read-only settings-control D-Bus registry)). This is the per-view
  settings-control location and inspection surface. SC-O1 supplies the
  transport, registry, lifecycle, serializer, and a test-only fixture vertical
  slice. Production controls register only with their later component or page
  units, so a current production view legitimately reports `[]` until one of
  those units lands.

  The implementation is deliberately hybrid. `SettingsControlRegistry` is a
  Corona-owned QObject registry because QML objects, visual ancestry, focus,
  popup transitions, and destruction are live QObject/QQuickItem concerns.
  `settingscontrolrecords.h` is a value-only C++20 core that owns the closed
  scalar domain, validation, complete-identity ordering, popup-row ordering,
  exact serialized scalar locator identity, and compact JSON. Every registered
  QObject/QQuickItem must initially have the registry's GUI-thread affinity.
  Lifecycle and popup-notify connections use `Qt::AutoConnection`: cleanup is
  synchronous while that invariant holds, while an illegal later migration
  queues cleanup to the GUI thread and makes queries fail closed until cleanup
  or replacement. The registry owns and disconnects every connection when an
  entry is logically removed. A count-only internal diagnostics read reports
  generations, controls, rows, routes, owned connections, and invalid-scope
  tombstones without exposing identities or content through D-Bus. The registry
  outlives settings factories and views during Corona teardown.

  Every control carries numeric `containmentId`, stable nonempty `surface`, decimal
  string `loadGeneration`, nullable numeric `appletId`, exact nonempty
  `auditIdentity` and `kind`, nullable nonempty `instanceKey`, effective
  `visible`/`enabled`, descendant-aware `focused`, typed `current`, one or more
  `hits`, and nullable `popup`. The scalar domain is JSON null, bool, integer,
  double, or string only. Each hit has a stable `role`, fractional
  `[x,y,width,height]` `globalGeometry`, `mapped`, and nullable
  `clippedGeometry`. Null clipped geometry plus `mapped:false` is the explicit
  fully clipped or unmapped state.

  Popup records carry `open`, `mapped`, decimal string or null `generation`,
  and deterministic semantic `rows`. Rows carry exact audit identity and kind,
  nullable instance key, numeric `visualIndex`, stable scalar `value`, hit
  records, and the same visible/enabled/focused/current state. Stable row value,
  never a localized label or visual index, is the future locator identity. Each
  popup must contain exactly one row for each compact serialized JSON value.
  Values with identical wire bytes conflict, including two null values and
  integer `1` versus double `1.0`. Integer row locators are restricted to
  `[-9007199254740991, 9007199254740991]`, the exact IEEE-754/JSON interoperable
  integer range. Every row item and hit must be the popup item or its visual
  descendant. Pointer values, window ids, localized labels, setters, filters,
  registration, and execution are not exposed.

  Geometry is recomputed at every query from current visual ancestry and the
  registered surface placement authority. It is clipped through every
  `QQuickItem::clip` ancestor and the supplied global surface bounds. Wayland
  `QWindow::position()` is not treated as a global coordinate. Rotation,
  shear, projective or mirrored transforms, non-finite values, and invalid hit
  sizes cannot be represented by the one-rectangle contract and therefore
  refuse the complete requested aggregate as `[]` with a warning.

  Registry generations are positive and process-monotonic; callers cannot
  choose them. Replacing or retargeting a scope retires its previous generation
  before a newer one is allocated, even when the old QML object survives.
  Popup close/reopen similarly receives a newer generation. Lifetime-object
  destruction removes the whole scope, while control or row destruction
  removes only that entry. Destroyed handlers capture numeric tokens and popup
  routing identity before pointer decay and never cast a dying QObject.
  Duplicate or malformed control and popup-row registration poisons the whole
  affected load generation immediately. The registry keeps an exact-scope
  tombstone, so any surviving surface or applet scope in that containment still
  yields `[]`. Unrelated scope replacement cannot clear it. Only valid
  replacement of that exact containment/surface/applet scope or destruction of
  its captured owner identity clears it. Further registration with the old
  generation or control tokens warns and refuses. Unsupported current values
  and any malformed live entry also warn and refuse the complete query. No
  partial plausible array is returned. Unknown views warn and return `[]`;
  known views with no controls return `[]` quietly.
- `taskMiddleClickDispatchData(u containmentId) -> s` (JSON object, added
  2026-07-21 for SC-T3, the narrow dispatch readback for D29 (task-icon middle
  click appears to execute left-click behavior)).
  The newest task-icon middle-click decision across the view's tasks applets:
  `rowIdentity` (launcher URL without icon payload, with appId fallback),
  `rowKind` (`launcher|task`), `configuredAction`, `dispatchedOperation`, and
  process-monotonic `sequence`. The observed launcher/task pair reads
  `configuredAction:"newInstance"` in both cases, with
  `launcher`/`requestActivate` for the pure launcher and
  `task`/`requestNewInstance` for the resulting window row. `{}` is the
  legitimate no-event state before the first middle click. The sequence resets
  when the dock process restarts and provides ordering without retaining event
  history. `TaskMouseArea.onReleased` records at the production branch that
  selects the operation; the report does not infer dispatch from downstream
  model effects. Every current Latte Tasks applet in the requested containment
  participates in the aggregate. A malformed nonempty candidate, candidate
  outside that containment, or duplicate sequence anywhere in the candidate
  set refuses the complete aggregate as `{}` rather than returning an older
  plausible event. An empty backend map is a legitimate no-event candidate. A
  tasks applet whose quick item does not exist yet is a startup-transient
  unavailable candidate and is skipped with a warning. Plasma keeps applets in
  containment iteration during the removal undo window, so their last event
  remains queryable until actual destruction removes the applet. No setter,
  execution hook, window title, or application content is exposed.
- `trackerData(u containmentId) -> s` (JSON): the windows-tracker
  facts per view - activeWindowTouching, activeWindowMaximized,
  existsWindowTouching, existsWindowMaximized, lastActiveWindow
  present + its appId (no title), enabled state. This is the readback
  for the dodge/visibility live checks (the "maximized window
  touching" checks the handoff lists as pending).
- `colorizerData(u containmentId) -> s` (JSON): the colorizer
  decision in force - mode, exempted or not, measured saturation
  bucket, active scheme file basename, and (D21, added 2026-07-18) the
  resolved applyColor/textColor/backgroundColor plus their brightnesses,
  so an applet-contrast test asserts foreground-vs-background at the
  state level. Replaces the palette-flip screenshot bisections of rounds
  twenty-one/twenty-eight.
- `viewConfigData(u containmentId) -> s` (JSON object, added 2026-07-18
  for the edit-mode settings audit's CL-0 prerequisite,
  docs/tracking/edit-mode-settings-audit-plan.md). Two objects: `config`, the
  containment's settings-panel config VALUES (every key of the General
  group - maxLength/minLength/offset/alignment, iconSize/zoomLevel/
  panelSize/panelTransparency, the margins, the background toggles, the
  color enums - each key -> its current value); and `view`, the live
  C++-property half whose settings controls write a View/Visibility/
  Indicator property instead of a config key (byPassWM,
  isPreferredForShortcuts, visibility timerShow/timerHide/enableKWinEdges/
  raiseOnDesktop/raiseOnActivity, indicator enabled/type/customType) plus
  the universalSettings edit-settings state (inAdvancedModeForEditSettings,
  settingsWindowScaleWidth/settingsWindowScaleHeight) the CL-6 chrome audit
  reads for the advanced-mode toggle (control 7) and the drag-corner window
  scales (control 8) - these live on UniversalSettings (global / per-screen)
  with no containment config key, so the Corona resolves them and merges
  them onto the view object.
  READ FROM THE IN-PROCESS KConfigPropertyMap (the same map the settings
  pages write through plasmoid.configuration), NOT the on-disk file: the
  file drops a key whose value returned to its default, but the map keeps
  it, so a config-snapshot diff cannot mistake "written to default" for
  "deleted" (the KConfig default-deletion caveat). The `config` object is
  what the audit's Tier B check diffs before/after driving a control to
  prove it wrote its own key with no stray side effect; the `view` object
  is the P3 (reflects-current-state) readback for the C++-property
  controls. Coarse read of the user's own dock config: no execution, no
  secrets, no other applications' content - it carries only setting
  values, dedicated (Bree's decision) rather than grown into viewsData so
  that call stays lean. A dedicated method also keeps the audit's
  full-config diff surface separate from the geometry/visibility facts
  viewsData reports.
- `appletConfigData(u containmentId, u appletId) -> s` (JSON object,
  added 2026-07-18 alongside viewConfigData). One child applet's config
  VALUES keyed by containmentId, appletId and plugin, read the same
  in-process way. This is the D10-class surface: the Tasks settings page
  writes tasks.plasmoid.configuration.* (a DIFFERENT applet's config),
  and whether those writes reach the running tasks applet is the D10
  question CL-5 settles by diffing this readback. The applet id comes
  from viewAppletsData; an id that names no applet on the view is refused
  loudly ("{}" + a warning), never a silent empty answer a consumer could
  read as "the applet has no config".
- `layoutsData() -> s` (JSON): loaded layouts, memory mode, current
  activity per layout, view counts. Complements switchToLayout.
- `screensData() -> s` (JSON array): the ScreenPool id<->connector
  mapping - per output its Latte screen id, connector name, geometry
  ([x,y,w,h]), isActive (the connector is currently connected) and
  isPrimary. The pull-queryable screen<->output topology the
  multi-output e2e vehicle (C-I2/P1, O7) discovers the secondary
  output from and pins a per-screen view assignment against; before it
  existed that mapping was only reachable by scraping ScreenPool's
  qDebug, which the observability-first rule forbids.
- Existing lifecycleState() gains nothing; it stays the tiny liveness
  probe (poll for "running").

## Action surface

`reloadView(u containmentId)` is the one debug-gated lifecycle action. It
requires `LATTE_DEBUG_DBUS=1` at process startup and otherwise refuses loudly.
The action recreates the addressed runtime through the custom-indicator reload
path. Recreating a relationship root replaces every live linked runtime, builds
the root first, and rebinds each member without changing persistent containment
identity or configuration. `dockSystemData` exposes the changed
`runtimeViewId` tokens. An output that becomes inactive during the delayed
replacement is revalidated from persisted placement and is not remapped or
recreated on another output.

The remaining actions are coarse user actions:

- `setViewEditMode(u containmentId, b editing)` - what Edit Dock and
  its Close button do. The e2e replacement for the kglobalaccel
  invoke + coordinate-click dance (which races registration and
  cycles views - both documented traps).
- `activateTaskAt(u containmentId, i index)` - what a left-click on a
  task does (same code path). Index, not window id: coarse, and
  matches Meta+number semantics.
- `activateEntryAt(u containmentId, i index)` under debug gate? NO -
  rejected: activateTaskAt covers the test need; per-entry submenu
  drives stay pointer/AT-SPI work.
- `setViewVisibilityMode(u containmentId, s mode)` - what the
  settings combo does; needed by the lock/unlock and struts e2e
  recipes (flipping AlwaysVisible/dodge without config-file surgery
  and dock restarts, which is how this session did it).
- `setViewKeyboardNavigation(u containmentId, b navigating)` (added
  2026-07-17 with the keyboard focus mode) - the same
  View::enter/exitKeyboardNavigation the Meta+Alt+D global shortcut
  drives, targeted at one view; the test surface for the mode, since
  kglobalaccel invokes race registration inside the nested vehicle.
  Readback: viewsData()'s keyboardNavigation field.
- `setViewConfiguringApplets(u containmentId, b configuring)` (added
  2026-07-18 with the applet-reorder driver, C-I7/P6) - flips the same
  global inConfigureAppletsMode rearrange sub-mode the settings header's
  rearrange button toggles (HeaderSettings.qml). It is the driving
  surface for the applet-reorder e2e family: the ConfigOverlay drag
  machinery is live only when a view is BOTH in edit mode AND this
  sub-mode, and the sub-mode is transient (deleted on config load, never
  persisted), so no config seed can reach it. Entering is REFUSED loudly
  when the target view is not already in edit mode (rearrange only runs
  inside an open edit session; the boundary refusal is also the action's
  observes-a-rejection self-test); exiting is always honored. Readback:
  viewsData()'s effective per-view inConfigureAppletsMode field and
  dockSystemData()'s top-level raw global plus per-view effective fields.
- `addApplet(u containmentId, s pluginId)` (added 2026-07-18 with the
  e2e interaction suite, C-I3/P3) - the deterministic sibling of a
  widget-explorer drop: validate that pluginId names an INSTALLED
  plasmoid, then Plasma::Containment::createApplet, end-appended
  (Qt5-faithful). It is the add-path the F2 scenarios drive instead of
  the real explorer DND (that stays its own driver, C-I9/P8). Two
  refusals, both loud with NO applet created (reads-never-construct
  extended to this mutator): a containment id with no view, and a
  plugin id that names no installed plasmoid. Readback:
  viewAppletsData/viewAppletsOrder grow by one, the new applet at the
  absolute end.
- `removeApplet(u containmentId, u appletId)` (added 2026-07-18 with the
  e2e interaction suite, C-I4/P4) - the coarse "Remove this Widget" the
  applet context menu triggers. The addressed runtime view is the mutation
  boundary. Independent views ask that applet to begin Plasma's reversible
  destruction transaction. A linked member first translates its local applet
  identity to the relationship root, which owns the one transaction and undo
  notification for the group. The root applet remains observable with
  `inScheduledDestruction=true`; member projections are retired immediately so
  their persistence tombstones are durable. Undo keeps the root applet identity
  and recreates each member projection with a fresh member-local identity and a
  copy of the root configuration. A restart during the Undo window preserves
  removal in every containment instead of resurrecting a member projection.
  Two refusals, both loud with NO applet removed (reads-never-construct
  extended to this mutator): a containment id with no view, and an
  applet id that names no applet on the addressed view. Readback:
  `viewAppletsData` shows the root transaction and the member projection set.
- `showWidgetExplorer(u containmentId)` (added 2026-07-18 with the e2e
  interaction suite, C-I9/P8) - the coarse "Add Widgets..." the
  containment context menu triggers (Menu::requestWidgetExplorer ->
  Containment::showAddWidgetsInterface -> View::showWidgetExplorer),
  exposed as a public View::openWidgetExplorer entry so no synthetic
  right-click is needed. Its only reason to exist is a DRIVING SURFACE:
  the explorer window carries the AppletDelegate DragArea offering the
  text/x-plasmoidservicename mime, the real Wayland drag source the e2e
  DND driver (dnd-lib.sh) presses on and glides onto the dock. A bad
  containment id (no view) is refused loudly with no window shown
  (reads-never-construct extended to this action). No readback of its
  own: the explorer window shows in the compositor dump (e2e_dumpwins,
  resourceClass latte-dock, its own caption), and a drop's effect is
  observed through viewAppletsData (count) and viewDropMarkerIndex (the
  live spacer, G3).

Rejected for now, with reasons recorded: setAppletsOrder over D-Bus
(reorder is a drag interaction; tests that need order changes drive
the UI or edit config while stopped - a writable order API invites
sync corruption exactly where the clone machinery is delicate);
anything returning window titles or icons (content); screenshot
methods (pixels are the harness's job); per-applet config get/set
(KConfig is already externally readable, and a write API would race
the in-process KConfigPropertyMap caches).

## Implementation notes

- One C++ home: a small Latte::DbusReports helper (app/) that walks
  corona -> layouts -> views and serializes; Corona methods stay
  one-liners delegating to it. Keeps lattecorona.cpp from growing
  another thousand lines.
- The settings-control read is the deliberate hybrid exception to the
  DbusReports collector shape. QObject/QQuickItem lifecycle lives in
  `app/settingscontrolregistry.{h,cpp}`; the value snapshot and serializer live
  in `app/settingscontrolrecords.h`. Corona still follows the same thin
  delegation rule after validating that the requested containment has a current
  view.
- Every JSON field reads EXISTING state objects (View, Positioner,
  VisibilityManager, ContainmentInterface, trackers). If a field has
  no clean getter yet, add the getter, never a parallel cache.
- The XML grows in one commit per surface group (views, applets,
  tasks, tracker/colorizer, actions), each with its adaptor regen
  and a busctl smoke in the commit body.
- tests/e2e/ converts to these as they land (quartet item c): each
  converted check names the D-Bus call that replaced its
  screenshot/sleep.

## Sequencing

1. viewsData (unblocks most e2e conversions and the stranding
   diagnosis) + setViewEditMode.
   LANDED 2026-07-16 (fdfdf5b00, 07e91e456, dd3046c03, 77a9586cc -
   post-rebase master hashes):
   serializer in app/dbusreports.h/.cpp, pure layer pinned by
   tests/units/dbusreportstest.cpp; enter wires through
   View::showSettingsWindow (the Edit Dock ensemble, enter-only),
   exit through PrimaryConfigView::hideConfigWindow (the chrome's
   close button).
2. viewAppletsData + activateTaskAt (order/task asserts).
   LANDED 2026-07-16 session two.
3. trackerData + setViewVisibilityMode (visibility-mode e2e).
   LANDED 2026-07-16 session two.
4. viewTasksData + colorizerData + layoutsData.
   LANDED 2026-07-16 session two.
   Steps 2-4 commits (post-rebase master hashes): 2390e7220..bb3d8c53f
   (11 commits; per-surface list and the owed-then-run busctl smokes in
   docs/agent-logs/2026-07-16-dbus-steps-2-4.md).

## Schema additions recorded at implementation (deliberate, 2026-07-16)

- trackerData also carries activeWindowTouchingEdge,
  existsWindowTouchingEdge and existsWindowActive - the tracker's
  companion facts; asserting on touching without touching-edge kept
  producing half-blind e2e checks.
- viewTasksData records carry appletId, so a multi-tasks-applet view
  keys records unambiguously.
- trackerData names the last-active-window identity field
  lastActiveWindowAppName: LastActiveWindow exposes appName, not a
  stable appId, and appName is application identity, not window
  content - still NO titles anywhere.
- viewsData also carries keyboardNavigation (2026-07-17): whether the
  view is in keyboard-navigation mode, i.e. temporarily accepting
  keyboard focus. The exit paths are asserted through this field
  (a stuck true means a dock stuck focusable - the exact defect the
  mode's design guards against).
- screensData (2026-07-18, C-I2/P1): the ScreenPool id<->connector
  mapping as its own read, so the multi-output vehicle DISCOVERS which
  connector is the secondary (isActive && !isPrimary) rather than
  hardcoding a name the compositor may reassign, and VERIFIES a pinned
  view's id resolved to the intended output. viewsData.screen already
  reported the connector NAME a view sits on; screensData adds the
  topology (all outputs, their ids, and which is primary) that
  view-level field cannot express.
