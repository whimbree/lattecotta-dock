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
    "editMode": false, "inConfigureAppletsMode": false
  }
  ```
  This single call replaces the pixel-peeping used across the whole
  stabilization history: dumpwins cross-checks, mask/input-region
  debug windows, the struts hunt's KWin clientArea probes, the
  lock/unlock "is it actually visible" question, and the
  startup-stranding watchdog's state dump (inStartup/isOffScreen are
  exactly the stranded bits).
- `viewAppletsData(u containmentId) -> s` (JSON array). Per applet:
  id, plugin, index in layout, geometry within the view, expanded
  state, inScheduledDestruction, lockedZoom, colorizingBlocked.
  Supersedes viewAppletsOrder for rich asserts; viewAppletsOrder
  stays (cheap, stable, already used).
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
   LANDED 2026-07-16 (455ec42ac, 138364d7f, bc7582cd3, 0b5e2d9f7):
   serializer in app/dbusreports.h/.cpp, pure layer pinned by
   tests/units/dbusreportstest.cpp; enter wires through
   View::showSettingsWindow (the Edit Dock ensemble, enter-only),
   exit through PrimaryConfigView::hideConfigWindow (the chrome's
   close button).
2. viewAppletsData + activateTaskAt (order/task asserts).
3. trackerData + setViewVisibilityMode (visibility-mode e2e).
4. viewTasksData + colorizerData + layoutsData.
