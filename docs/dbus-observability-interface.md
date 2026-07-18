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
  removeView, duplicateView, moveViewToLayout, exportViewTemplate,
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
   Nothing in this document's action list needs it so far; the gate
   is specified now so a future need does not improvise.

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
- `viewAppletsData(u containmentId) -> s` (JSON array, in visual order).
  Per applet: id, plugin, index in layout, geometry within the view,
  expanded state, inScheduledDestruction, lockedZoom, colorizingBlocked.
  The per-applet `id` is the stable Plasma instance id, so two applets
  of the SAME plugin are distinguishable by id and their order is
  unambiguous (the G1 disambiguation readback,
  docs/e2e-interaction-test-plan.md - retires the same-plugin ambiguity
  in the F2/F3/A1/A2 add/reorder/abort scenarios). Supersedes
  viewAppletsOrder for rich asserts.
- `viewAppletsOrder(u containmentId) -> as`: the cheap companion to
  viewAppletsData - the same applets' instance ids (NOT plugin strings)
  in visual order. Justify-splitter sentinels (JUSTIFYSPLITTERID = -10,
  layout artifacts that own no applet) are EXCLUDED, the same line
  viewAppletsData draws, so the two order readbacks agree on justify
  views. Cheap, stable, already used.
- `viewDropMarkerIndex(u containmentId) -> i` (added 2026-07-18 with the
  e2e interaction suite, C-I4/P4). The live drop-marker readback (the G3
  gap, docs/e2e-interaction-test-plan.md): the drag placeholder spacer's
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
- `trackerData(u containmentId) -> s` (JSON): the windows-tracker
  facts per view - activeWindowTouching, activeWindowMaximized,
  existsWindowTouching, existsWindowMaximized, lastActiveWindow
  present + its appId (no title), enabled state. This is the readback
  for the dodge/visibility live checks (the "maximized window
  touching" checks the handoff lists as pending).
- `colorizerData(u containmentId) -> s` (JSON): the colorizer
  decision in force - mode, exempted or not, measured saturation
  bucket, active scheme file basename. Replaces the palette-flip
  screenshot bisections of rounds twenty-one/twenty-eight.
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

## Action surface (new, all coarse user actions)

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
  applet context menu triggers: Applet::destroy() on the applet with
  that instance id. It rides the libplasma UNDO WINDOW exactly as
  removeView does: destroy() marks the applet destroyed() and holds the
  object alive while the "Widget Removed" undo notification is open
  (~60s libplasma fallback timer), so the applet does NOT vanish from
  the readback immediately - it lingers with inScheduledDestruction=true
  and leaves viewAppletsData/viewAppletsOrder only once the window ends
  (a restart inside the window resurrects it, same trap as removeView).
  Two refusals, both loud with NO applet removed (reads-never-construct
  extended to this mutator): a containment id with no view, and an
  applet id that names no applet on the view. Readback: the applet's
  inScheduledDestruction field in viewAppletsData flips to true.

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
