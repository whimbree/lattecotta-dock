# org.kde.LatteDock D-Bus reference (usage)

The practical companion to [dbus-observability-interface.md](dbus-observability-interface.md)
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

This replaces pixel-peeping for dock state: hidden-or-not, where the
input region really is, whether startup stranded (`inStartup` stuck
true with `isOffScreen`), what the compositor was told to reserve.

### Per-view reads (all take the containment id)

```bash
call viewAppletsData u 1               # s: JSON array per applet, in visual order
#   id, plugin, index, geometry, isExpanded, inScheduledDestruction,
#   lockedZoom, colorizingBlocked
#   id is the stable Plasma INSTANCE id: two applets of the same plugin
#   are distinguishable by id and their order is unambiguous (G1)
call viewTasksData u 1                 # s: JSON array per task entry (tasks views; else [])
#   index, appletId, appId, launcherUrl, isLauncher, isGrouped,
#   childCount, isActive, isMinimized, demandsAttention, badge,
#   geometry   - NO window titles, by design (appId is app identity)
call trackerData u 1                   # s: JSON object - the windows-tracker facts
#   enabled, activeWindowTouching, activeWindowTouchingEdge,
#   activeWindowMaximized, existsWindowTouching,
#   existsWindowTouchingEdge, existsWindowMaximized,
#   existsWindowActive, lastActiveWindowPresent,
#   lastActiveWindowAppName
call colorizerData u 1                 # s: JSON object - the colorizer decision in force
#   colorizerPresent, enabled, themeColorsMode, windowColorsMode,
#   mustBeShown, applyingWindowColors, backgroundIsBusy,
#   currentBackgroundBrightness (-1000 = no measurement), scheme
call viewAppletsOrder u 1              # as: applet INSTANCE ids in visual order (cheap, stable);
#   justify-splitter sentinels (-10) excluded, so it agrees with
#   viewAppletsData's order on justify views (G1)
call contextMenuData u 1               # as: legacy ;;/** joined list (pre-JSON, kept)
```

### Global reads

```bash
call layoutsData                       # s: JSON array per loaded layout
#   name, isActive, memoryUsage, activities, viewsCount
call viewTemplatesData                 # as: name,id pairs flattened
```

## Action surface

```bash
call setViewEditMode ub 1 true         # enter Edit Dock for view 1 (false = close chrome)
call setViewVisibilityMode us 1 "dodgeActive"   # the settings combo; names as viewsData reports
call setViewKeyboardNavigation ub 1 true        # enter/leave keyboard navigation (what Meta+Alt+D
                                                # toggles); readback: viewsData keyboardNavigation.
                                                # false is always safe: exits are idempotent
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

Isolated testing: the whole surface works identically inside the
nested-kwin vehicle (private bus) - see the session-handoff nested
recipes and tests/e2e. Remember the dock exits instantly if another
instance owns the name on the same bus (KDBusService unique), which
is why nested runs wrap the dock in `dbus-run-session`.

## Refusal semantics (what "wrong input" does)

- Unknown containment id: empty JSON (`"[]"`/`"{}"`) + qWarning.
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
tests/units/dbusreportstest.cpp - extend the test with any new field.
