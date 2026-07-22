# org.kde.LatteDock D-Bus reference (usage)

The practical companion to [the observability interface design doc](dbus-observability-interface.md)
(which carries the design rationale, the safety rules and the
rejected-with-reasons list). This file is the how-to: every method on
the bus, its real signature, and a copy-paste invocation. Field lists
below were captured from live introspection on 2026-07-16; if this file
and the running dock ever disagree, `busctl --user introspect
org.kde.lattedock /Latte org.kde.LatteDock` is the truth and this file
has drifted.

## Addressing

- Bus name: `org.kde.lattedock` (lowercase)
- Object path: `/Latte`
- Interface: `org.kde.LatteDock` (capital L, capital D)

```bash
call() { busctl --user call org.kde.lattedock /Latte org.kde.LatteDock "$@"; }
```

All structured reads return ONE JSON string (`s`). Views key on the
containment id (uint) everywhere. Reads never construct anything: an
unknown id returns `"[]"` or `"{}"` plus a qWarning in the dock log
(visible with `-d`, and Criticals always). Actions are coarse user
actions - the same code paths the UI drives.

Signals are not part of the surface; poll `lifecycleState` for
liveness and re-read after acting.

## Liveness

```bash
call lifecycleState                    # s "startup"|"running"|"quit-requested"|"unloaded"
```

Poll this instead of sleeping after a restart. `viewsData`'s
`inStartup` per view tells you when views have settled.

## Read surface

### viewsData - the workhorse

```bash
call viewsData                         # s: JSON array, one object per view
```

Per view: `containmentId`, `layout`, `isCloned`, `isClonedFrom`,
`type` (dock|panel), `screen`, `onPrimary`, `edge`, `alignment`,
`visibilityMode` (alwaysVisible|autoHide|dodgeActive|dodgeMaximized|
dodgeAllWindows|windowsGoBelow|windowsCanCover|windowsAlwaysCover|
sidebarOnDemand|sidebarAutoHide|normalWindow), `isHidden`,
`inStartup`, `isOffScreen`, `absoluteGeometry`, `localGeometry`,
`screenGeometry` (all `[x,y,w,h]`), `strutsThickness`,
`publishedStruts`, `maskRect`, `inputRegionRects` (array of rects),
`appliedInputRegionRects` (array of rects: the region actually handed
to `QWindow::setMask`, kept wide across a length shrink and collapsing
back to `inputRegionRects` once the band settles - differs only
mid-shrink), `editMode`, `inConfigureAppletsMode`, `keyboardNavigation`.
`inConfigureAppletsMode` is effective for that view: it is true only when the
view is in edit mode and the global rearrange toggle is on.

This replaces pixel-peeping for dock state: hidden-or-not, where the
input region really is, whether startup stranded (`inStartup` stuck
true with `isOffScreen`), what the compositor was told to reserve.

### dockSystemData - atomic all-docks topology

```bash
call dockSystemData                    # s: compact schema-versioned JSON object
```

Top level: `schemaVersion` (currently 1), `snapshotSequence` (decimal string,
process-local monotonic call identity), `globalConfigureAppletsMode`,
`stacking`, and `views`. The complete view list is captured synchronously and
serialized in ascending `persistentDockId` order, so repeated snapshots do not
inherit `QHash` traversal order.

Per dock:

- Identity and relationship: `runtimeViewId` (decimal string),
  `persistentDockId`, `logicalDockId`, `originalDockId` (number or null),
  `relationship` (`single|screensGroupOriginal|screensGroupClone`),
  `screensGroup` (`single|allScreens|allSecondaryScreens`), and
  `cloneDockIds`. `persistentDockId` is the containment id. A clone's logical
  id and original id name its original containment. Every other dock's logical
  id equals its persistent id. Duplication creates no lasting relation to its
  source. Under the current behavior, a duplicated all-screens policy produces
  a new screens-group original and its own clones; duplicating a single-screen
  dock produces a `single` record.
  `screensGroup` is always a string in a valid response. Before serialization,
  every clone must point directly to a present screens-group original. Missing
  targets, clone chains, cycles, duplicate persistent ids, and standalone
  targets invalidate the whole query rather than producing partial JSON.
- Placement: `layout`, `screenId`, `screen`, `onPrimary`, `type`, `edge`,
  `orientation`, `alignment`, `maximumLengthRatio`, and `offsetRatio`.
- Sizing: `configuredIconSize`, `effectiveIconSize`,
  `availablePrimaryLength`, `normalThickness`, and `maximumNormalThickness`.
  `effectiveIconSize` and `availablePrimaryLength` are null while their live
  QML authorities are not constructed. A null `configuredIconSize` instead
  means the required containment configuration map or its `iconSize` entry is
  missing; collection logs that defect. `availablePrimaryLength` is the
  containment root's current logical-pixel `maxLength`, not the configured
  ratio.
- Geometry: `windowGeometry`, `absoluteGeometry`, `localGeometry`,
  `screenGeometry`, `canvasGeometry`, `effectsRect`,
  `appletsLayoutGeometry`, `maskRect`, `inputMask`, `appliedInputMask`,
  `strutsThickness`, and `publishedStruts`. Every rectangle is `[x,y,w,h]` in
  Qt logical pixels. `windowGeometry`, `absoluteGeometry`, `screenGeometry`,
  `canvasGeometry`, and `publishedStruts` use virtual-desktop coordinates.
  `localGeometry`, `effectsRect`, `appletsLayoutGeometry`, `maskRect`,
  `inputMask`, and `appliedInputMask` use dock-window-local coordinates.
  Thickness values are logical pixels as well.
- Runtime state: `visibilityMode`, `isHidden`, `inStartup`, `isOffScreen`,
  `inRelocationAnimation`, `inDelete`, and `inReadyState`.
- Edit state: `editMode`, `effectiveConfigureAppletsMode`, and
  `settingsWindowShown`. The effective field is true only for an edited view
  while the top-level global toggle is true.
- `objects`: opaque `object-N` identities for `view`, `containment`,
  `configuration`, `layout`, `layoutController`, `geometryController`,
  `editController`, and `configWindow`. Null means that authority is not live.
  The configuration authority is required and its absence is logged as a
  defect; the configuration window is legitimately absent while closed, and
  QML controllers may be absent during startup or teardown. Tokens remain
  stable for a QObject's lifetime and are useful only within the current
  process. They are not memory addresses.

`stacking.available` is currently false and its `reason` explains the missing
capability. No runtime authority models same-edge stack order or accumulated
offsets yet. Do not interpret canonical `views` array order as physical stack
order.

An internal lineage-invariant failure logs every relationship input at critical
severity and returns an empty D-Bus string. It never returns a smaller but
otherwise plausible `views` array.

Example relationship checks:

```bash
state=$(call dockSystemData)
# jq examples after extracting the D-Bus string payload:
jq '.views | map({persistentDockId,logicalDockId,relationship,cloneDockIds})' <<<"$state"
jq '[.views[] | select(.effectiveConfigureAppletsMode)] | length' <<<"$state"
```

### Per-view reads (all take the containment id)

```bash
call viewAppletsData u 1               # s: JSON array per applet, in visual order
#   id, plugin, index, geometry, isExpanded, inScheduledDestruction,
#   lockedZoom, colorizingBlocked, colorizerActive, colorizerReason, z
#   colorizerActive (D21): true when the colorizer scheme is pushed into this
#   applet's own colour group; colorizerReason names the decision - applied /
#   notEngaged / splitter / selfColored / userBlocked / inlineFull.
#   colorizingBlocked stays the user opt-out list ONLY (narrower than the above)
#   id is the stable Plasma INSTANCE id: two applets of the same plugin
#   are distinguishable by id and their order is unambiguous (G1)
#   z is the delegate STACKING order (G2): 0 at rest, 900 while ConfigOverlay
#   drags it; an applet left >=900 is stranded over the edit chrome (residue)
call viewTasksData u 1                 # s: JSON array per task entry (tasks views; else [])
#   index, appletId, appId, launcherUrl, isLauncher, isGrouped,
#   childCount, isActive, isMinimized, demandsAttention, badge,
#   geometry   - NO window titles, by design (appId is app identity)
#   index+appId are the window-task ORDER readback (G4): index is the
#   model row (moves on reorder), appId the stable per-window identity;
#   track a window by appId, read its index before/after a task drag
call viewSettingsControlsData u 1      # s: JSON array of registered settings controls for view 1
#   SC-O1 (the read-only settings-control D-Bus registry) currently registers
#   only its test fixture. Production views therefore return [] until a later
#   component/page unit registers its own controls.
#   Per control: containmentId (number), surface, loadGeneration (decimal
#   string), appletId (number|null), auditIdentity, kind, instanceKey
#   (string|null), hits, visible, enabled, focused, current, popup (object|null).
#   A hit: role, globalGeometry [x,y,width,height] with QRectF precision,
#   clippedGeometry ([...] or null), mapped. mapped=false plus null clippedGeometry
#   is fully clipped/unmapped, not a zero-size target.
#   current is exactly null/bool/integer/double/string. No object/array coercion.
#   Popup: open, mapped, generation (decimal string|null), rows. Each row carries
#   auditIdentity, kind, instanceKey, visualIndex, stable value, hits, visible,
#   enabled, focused, current. Locate rows by stable value, never label/index.
#   Exactly one row may have each compact serialized JSON value per popup:
#   duplicate nulls and integer 1 versus double 1.0 therefore conflict.
#   Integer row values are limited to [-9007199254740991,9007199254740991],
#   the range JSON/IEEE-754 consumers preserve exactly as locator identity.
#   Generations are process-local monotonic identities. Re-query immediately
#   before input and reject a changed/missing load or popup generation.
#   A duplicate or malformed registration poisons its exact scope. The complete
#   containment remains [] until that scope is validly replaced or its owner is
#   destroyed. Any malformed live entry also refuses the complete view as [].
call taskMiddleClickDispatchData u 1   # s: JSON object - latest task-icon middle-click dispatch
#   rowIdentity, rowKind (launcher|task), configuredAction,
#   dispatchedOperation, sequence. The D29 pair (task-icon middle click appears
#   to execute left-click behavior) is:
#   launcher + newInstance + requestActivate, then
#   task + newInstance + requestNewInstance. sequence increases process-wide;
#   "{}" means no middle click has been recorded since this dock started.
#   Every current tasks applet participates. Any malformed nonempty candidate
#   or duplicate sequence refuses the aggregate as "{}"; an applet remains
#   queryable through Plasma's removal undo window until actual destruction.
#   Read-only latest event: no setter, no history, no titles or window content.
call trackerData u 1                   # s: JSON object - the windows-tracker facts
#   enabled, activeWindowTouching, activeWindowTouchingEdge,
#   activeWindowMaximized, existsWindowTouching,
#   existsWindowTouchingEdge, existsWindowMaximized,
#   existsWindowActive, lastActiveWindowPresent,
#   lastActiveWindowAppName
call colorizerData u 1                 # s: JSON object - the colorizer decision in force
#   colorizerPresent, enabled, themeColorsMode, windowColorsMode,
#   mustBeShown, applyingWindowColors, backgroundIsBusy,
#   currentBackgroundBrightness (-1000 = no measurement), scheme,
#   applyColor, textColor, backgroundColor ("#rrggbb", "" if unresolved),
#   applyColorBrightness, backgroundColorBrightness (0..255) - the resolved
#   foreground/background so a D21 contrast test can assert at the state level
call viewConfigData u 1                # s: JSON object - the settings-panel config VALUES
#   { containmentId, config:{<every General-group key> -> value},
#     view:{byPassWM, isPreferredForShortcuts, visibilityTimerShow,
#           visibilityTimerHide, visibilityEnableKWinEdges,
#           visibilityRaiseOnDesktop, visibilityRaiseOnActivity,
#           indicatorPresent, indicatorEnabled, indicatorType,
#           indicatorCustomType, inAdvancedModeForEditSettings,
#           settingsWindowScaleWidth, settingsWindowScaleHeight} }
#   config = the containment config map, read IN-PROCESS (a key at its
#   default reads as its default, never "missing" - the KConfig
#   default-deletion caveat). view = the live C++-property half whose
#   controls write a View/Visibility/Indicator property, not a config key.
#   The edit-mode settings audit diffs config before/after driving a
#   control (right-key check) and reads view for the P3 reflect-state leg.
call appletConfigData u 1 4            # s: JSON object - one child applet's config VALUES
#   { containmentId, appletId, plugin, config:{<applet keys> -> value} }
#   The D10-class surface: the Tasks page writes tasks.plasmoid.
#   configuration.* (appletId from viewAppletsData). "{}" if the view or
#   the applet id is unknown (warned).
call viewAppletsOrder u 1              # as: applet INSTANCE ids in visual order (cheap, stable);
#   justify-splitter sentinels (-10) excluded, so it agrees with
#   viewAppletsData's order on justify views (G1)
call viewDropMarkerIndex u 1           # i: live drop-marker (dndSpacer) visual insert index while
#   a drag hovers the view, -1 when no marker is live (parked at rest / after a
#   drop or abort). Index 0 = leading position (a live marker); only negative =
#   no marker. The G3 insert(-1) observability an add/reorder abort asserts (-1)
call contextMenuData u 1               # as: legacy ;;/** joined list (pre-JSON, kept)
```

### Global reads

```bash
call layoutsData                       # s: JSON array per loaded layout
#   name, isActive, memoryUsage, activities, viewsCount
call screensData                       # s: JSON array per known output
#   id (Latte screen id), name (connector), geometry [x,y,w,h],
#   isActive (connector connected now), isPrimary. The screen<->output
#   topology; viewsData.screen reports the connector a view sits ON,
#   this reports the whole mapping (which id is which output, which is
#   primary). Used by the multi-output e2e vehicle to discover the
#   secondary output and verify a per-screen pin resolved.
call viewTemplatesData                 # as: name,id pairs flattened
```

## Action surface

```bash
call setViewEditMode ub 1 true         # enter Edit Dock for view 1 (false = close chrome)
call setViewVisibilityMode us 1 "dodgeActive"   # the settings combo; names as viewsData reports
call setViewKeyboardNavigation ub 1 true        # enter/leave keyboard navigation (what Meta+Alt+D
                                                # toggles); readback: viewsData keyboardNavigation.
                                                # false is always safe: exits are idempotent
call setViewConfiguringApplets ub 1 true        # enter/leave the applet-REARRANGE sub-mode (what the
                                                # settings header rearrange button toggles); the ConfigOverlay
                                                # drag machinery needs it. Refused loudly if view 1 is NOT in
                                                # edit mode first (open it with setViewEditMode). Readback:
                                                # effective viewsData inConfigureAppletsMode, or dockSystemData's
                                                # global and per-view fields. false is always safe
call activateTaskAt ui 1 3             # what Meta+3 does on view 1: 1-BASED visual entry
                                       # index (badges' numbering); active task toggles
                                       # (minimize), launcher-only entries launch
call showSettingsWindow i 1
call addView us 0 "Default Dock"       # 0 = first current layout; template names from viewTemplatesData
call addApplet us 1 "org.kde.plasma.marginsseparator"  # add an installed plasmoid to view 1,
                                       # end-appended (the deterministic sibling of a widget-explorer
                                       # drop). Refused loudly with NO applet if the containment id has
                                       # no view or the plugin id is not installed. Readback:
                                       # viewAppletsData / viewAppletsOrder grow by one
call removeApplet uu 1 42               # remove applet instance 42 from view 1 - the coarse
                                       # "Remove this Widget". UNDO WINDOW (same as removeView):
                                       # the applet lingers with inScheduledDestruction=true until
                                       # the notification closes or ~60s; a restart inside the
                                       # window resurrects it. Refused loudly with NO removal if the
                                       # containment id has no view or the applet id names no applet.
                                       # Readback: viewAppletsData's inScheduledDestruction flips
call showWidgetExplorer u 1            # open view 1's widget explorer (the "Add Widgets..."
                                       # context menu action). Exists as the drag SOURCE for the
                                       # e2e DND driver: the explorer's AppletDelegate offers the
                                       # text/x-plasmoidservicename mime. Refused loudly with no
                                       # window if the containment id has no view. No readback of
                                       # its own; the window appears in the compositor dump
call duplicateView u 1
call removeView u 1                    # UNDO WINDOW: containment survives until the
                                       # notification closes or ~60s; restarting inside
                                       # that window resurrects it (see live-verification skill)
call moveViewToLayout us 1 "Other Layout"
call exportViewTemplate u 1
call switchToLayout s "My Layout"
call importLayoutFile ss "/path/file.layout.latte" ""
call updateDockItemBadge ss "org.kde.konsole" "7"   # "" clears; unparseable clears + warns
call setAutostart b true
call toggleHiddenState sssi ...        # legacy plasma-compat signature
call activateLauncherMenu
call quitApplication
```

## Recipes

Wait for a healthy dock (restart scripts + tests use this shape):

```bash
until [ "$(call lifecycleState | awk '{print $2}')" = '"running"' ]; do sleep 1; done
until ! call viewsData | grep -q 'inStartup\\":true'; do sleep 1; done
```

Extract ids from the escaped JSON busctl prints:

```bash
call viewsData | grep -oE 'containmentId\\?":[0-9]+' | grep -oE '[0-9]+'
```

Pretty-print a payload (the reply is a quoted, escaped string):

```bash
call viewsData | sed 's/^s "//; s/"$//; s/\\"/"/g' | jq .
```

Query settings controls with `qdbus6`, whose string reply pipes directly to
`jq`:

```bash
view=1
qdbus6 org.kde.lattedock /Latte \
  org.kde.LatteDock.viewSettingsControlsData "$view" | jq .
```

Generation values are decimal strings on purpose. JSON numbers cannot preserve
all 64-bit identities through IEEE-754 consumers. A driver stores the complete
identity and observed generation, then re-queries immediately before input. A
missing record or changed generation is stale and must abort rather than fall
back to another instance:

```bash
view=1
surface='primary-settings'
audit='appearance.zoom'
kind='slider'
instance='main'
applet_json=null

snapshot="$(qdbus6 org.kde.lattedock /Latte \
  org.kde.LatteDock.viewSettingsControlsData "$view")"
load_generation="$(jq -er --arg surface "$surface" --arg audit "$audit" \
  --arg kind "$kind" --arg instance "$instance" --argjson applet "$applet_json" \
  '.[] | select(.surface == $surface and .appletId == $applet
                and .auditIdentity == $audit and .kind == $kind
                and .instanceKey == $instance) | .loadGeneration' \
  <<<"$snapshot")"

fresh="$(qdbus6 org.kde.lattedock /Latte \
  org.kde.LatteDock.viewSettingsControlsData "$view")"
jq -e --arg surface "$surface" --arg audit "$audit" --arg kind "$kind" \
  --arg instance "$instance" --argjson applet "$applet_json" \
  --arg generation "$load_generation" \
  '.[] | select(.surface == $surface and .appletId == $applet
                and .auditIdentity == $audit and .kind == $kind
                and .instanceKey == $instance
                and .loadGeneration == $generation)' \
  <<<"$fresh" >/dev/null || { printf '%s\n' 'stale settings control' >&2; exit 1; }
```

Popup input adds the same check for `.popup.open`, `.popup.mapped`, and the
observed string `.popup.generation`. Row resolution uses `.popup.rows[] |
select(.value == $stable_value)`; labels and `visualIndex` are not locators.
Registration guarantees exactly one row with the locator's compact serialized
JSON bytes inside that popup. Integer locators outside the exact interoperable
range `[-9007199254740991, 9007199254740991]` are refused.

Isolated testing: the whole surface works identically inside the
nested-kwin vehicle (private bus) - see the session-handoff nested
recipes and tests/e2e. Remember the dock exits instantly if another
instance owns the name on the same bus (KDBusService unique), which
is why nested runs wrap the dock in `dbus-run-session`.

## Refusal semantics (what "wrong input" does)

- Unknown containment id: empty JSON (`"[]"`/`"{}"`) + qWarning.
- `viewSettingsControlsData` on a known view with no registered controls: `"[]"`
  without a warning. An unknown view warns. Any malformed live scope, control,
  hit, popup, or row refuses the whole requested view as `"[]"` with a warning;
  no valid-looking subset is returned. A duplicate or malformed control or
  popup-row registration immediately poisons its complete load generation and
  retains an exact-scope tombstone. Any other surviving scope in the same
  containment is also hidden. Unrelated replacement does not clear the
  tombstone; valid replacement of that exact scope or destruction of its owner
  does. Further registrations through the old generation or control tokens warn
  and refuse.
- `taskMiddleClickDispatchData` before any middle-click event: `"{}"` without
  a warning. Empty maps are legitimate no-event candidates. Any malformed or
  unknown nonempty candidate, out-of-scope candidate, or duplicate sequence
  across the requested containment is refused as one aggregate `"{}"` with a
  qWarning instead of being skipped in favor of an older plausible event. A
  startup-transient tasks applet with no quick item is unavailable rather than
  malformed and is skipped with a warning.
- `setViewVisibilityMode` with a name viewsData never reports:
  refused with a qWarning naming the expectation; nothing changes.
- `activateTaskAt` that cannot resolve an entry: qWarning
  "could not activate entry N"; no crash, no fallback.
- `updateDockItemBadge` with an unparseable value: badge clears and
  a Critical names the value and identifier (Criticals reach the log
  even without `-d`).

## Maintenance rule

New surfaces land in three places in the same change: the adaptor XML
(app/dbus/org.kde.LatteDock.xml), the design doc's decision record,
and this reference with a real invocation. The serializers live in
app/dbusreports.{h,cpp}; their pure layers are pinned by
tests/units/dbusreportstest.cpp - extend the test with any new field. The
settings-control registry is the documented exception: its value serializer is
`app/settingscontrolrecords.h` with the paired sanitized
`settingscontrolrecordstest`; its QObject/QQuickItem collector is
`app/settingscontrolregistry.{h,cpp}` with `settingscontrolregistrytest`.
