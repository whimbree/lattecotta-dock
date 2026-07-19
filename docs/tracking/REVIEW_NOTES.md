# Human review notes

Things done during the autonomous port that a human should look at when
convenient. Each entry says what was done, why, and what specifically to
check. Nothing here is known-broken - these are judgment calls, transcribed
data, or work that can only be finished/verified with a live session or a
decision the driver shouldn't make alone.

## Open

### Dock blank-after-display-churn: could NOT reproduce as a monitor-sleep bug
Initially suspected a display-event repaint bug (dock keeps its strut but stops
painting). Retested 2026-07-08: a plain monitor sleep/wake does NOT do this. On
this hardware `kscreen-doctor --dpms off/on` (single and 3x rapid) leaves the
QScreen intact, so `reconsiderScreen()` early-returns without a
`setScreenToFollow()`, the surface is never touched, and the dock stays visible
through every cycle. The blank I saw earlier was almost certainly self-inflicted
test churn (rapid DPMS + killing kscreenlocker_greet + unlock loops), not a
user-facing sleep bug. If a real "dock vanishes" is ever reported, the thing to
chase is the lock-screen/greeter path, not DPMS.

A separate, real scenario is genuine output hotplug (unplug/replug an external
monitor, laptop dock/undock): that DOES destroy+recreate the QScreen and call
`setScreenToFollow()`, and whether the layer surface re-maps cleanly there is a
fair question. It could not be tested here: KWin refuses to disable the only
output ("Disabling all outputs through configuration changes is not allowed"),
and there is no second monitor to unplug. A guarded platform-surface recreate in
`setScreenToFollow()` was prototyped and then dropped (commit reverted) because
it was unverifiable on this single-monitor box and this setup never hits the
case. If multi-monitor support is exercised later and the dock is seen to go
blank on hotplug, reintroduce a recreate (destroy()+show()) in
`setScreenToFollow()`, gated to a visible past-startup view.

### Widget instantiation via config verified OK (no qt6-style crash)
2026-07-08: injected `org.kde.plasma.digitalclock` and a second
`org.kde.latte.plasmoid` (the Latte Tasks widget that crashed latte-dock-qt6 on
add) straight into a layout's applet list and restarted. Both loaded cleanly -
log `applets found :: 3 : QList(2, 3, 4)`, no KCrash, dock survived. So the
containment's widget-load path is sound, including the risky widget. NOT covered
by this test: the drag-from-explorer / tap-to-add gesture itself (can't drive
the pointer headlessly) - still needs a human to confirm the DnD handler.


### KSvg static-destructor crash on the single-instance early-exit path - FIXED 2026-07-08
When a second latte-dock started while another instance already owned the
`org.kde.lattedock` D-Bus name, ours did its init then exited, and segfaulted
during static teardown: `__cxa_finalize` -> `_dl_call_fini` ->
`~QThreadDataDestroyer` -> a static `KSvg::Svg` destructor ->
`KSvg::SvgPrivate::eraseRenderer()`. Backtrace captured from a core dump on
2026-07-07. Root cause: `main.cpp` constructed `Latte::Corona` (which builds the
theme/KSvg singletons) BEFORE `KDBusService(KDBusService::Unique)`, so a
duplicate launch built the full theme stack and only then found it was not
unique and exited, tearing a live static `KSvg::Svg` down at `__cxa_finalize`.
Fixed by moving the `KDBusService(Unique)` guard ahead of the Corona and
`return 0`ing when `!service.isRegistered()`, so the duplicate path exits before
any KSvg object exists (commit d45c7a38, build-verified). **Human test:** launch
a second instance while one runs (or with a different Latte owning the D-Bus
name) and confirm it exits cleanly with no KCrash.

### Packaging (flake packages.default) follow-ups
The nix package builds, wraps, and passes the KWin enumeration gate (verified
live 2026-07-07: the wrapped binary authorized only by its own installed
`.desktop` enumerates all windows). Two loose ends, neither blocking:
- Runtime warning `Could not find plugin plasma5support/dataengine/
  plasma_engine_mpris2`. That dataengine is not present anywhere in the store,
  so the tasks media/audio badge and the Spotify media-control overlay have no
  backend in the packaged build. Identify which kdePackages input ships the
  mpris2 dataengine (or the Plasma 6 replacement API) and add it, or wire the
  plasmoid to the current media API. Enumeration and the dock itself are
  unaffected.
- Build log: `cmake -E touch: failed to update <out>//nix/store/<out>/share/
  icons/hicolor` (doubled path). Harmless (icon cache index), but it points at
  an install rule using an absolute path under DESTDIR. Worth a look when
  touching the icon install.

### Enumeration ("missing running apps") root cause: KWin permission gate (dev build only)
Not a code bug. KWin gates `org_kde_plasma_window_management` (libtaskmanager's
window source) behind a `.desktop` match: the canonicalized first token of an
installed desktop file's `Exec=` must equal the client's invocation path
(argv[0] / cmdline[0], NOT `/proc/PID/exe`), and that service must list the
interface in `X-KDE-Wayland-Interfaces`. Our
`app/org.kde.latte-dock.desktop.cmake` already declares it correctly.
- Normal install works: `Exec=$PREFIX/bin/latte-dock`, and the process is
  launched from that path so argv[0] matches.
- Nix-wrapped install works too: `makeBinaryWrapper` keeps
  `argv[0]=$out/bin/latte-dock` (the wrapper), which equals the `.desktop`
  `Exec`, even though `/proc/exe` is the `.latte-dock-wrapped` binary.
  (Verified empirically 2026-07-07; an earlier note here wrongly said nix
  installs are gated.)
- Only fails for the DEV BUILD run straight from `build/bin/` (no installed
  `.desktop` has an `Exec` matching that build-tree path). Dev workaround: a
  throwaway `.desktop` in `~/.local/share/applications` with
  `Exec=<repo>/build/bin/latte-dock` plus the interface line, then
  `kbuildsycoca6`.
Full write-up and correction in the fork-comparison journal, 2026-07-07
entries. **No source change needed**; flagged so nobody re-hunts this as a
code defect.

### Tasks click-action completeness test uses a transcribed "offered" set
`tests/qml/tst_taskactions.qml` guards the enum/handler contract for task
click actions (the plan's "config offers 9, handler handles 3" regression
class). The set of enum values each click combo *offers* is transcribed into
the test from the config UIs, with a source reference on each list:

- left click: `shell/.../configuration/pages/TasksConfig.qml` leftClickAction combo (3 values)
- middle / modifier click: same file, middle/modifierClickAction combos (6 values, index == enum)
- wheel: same file, wheelAction combo (3 values)
- the plasmoid's own `ConfigInteraction.qml` middle-click combo offers a 4-value subset

**Review:** confirm the transcribed lists still match the combo `model:`
arrays. If a combo gains an option, the transcription here must gain it too -
the test only catches a *handler* that fell behind the offered set, not a
*test* that fell behind the UI. A nice future hardening would be to drive the
real combo components and read their `model` arrays directly, but those config
pages have a large ambient-context surface that isn't cheaply mockable yet.

### In-plasmoid wheel routing: audited, no fix needed, one part deferred to Phase 8
Plan item (Phase 6): "Route wheel events to badges/sub-regions inside the
tasks plasmoid explicitly ... DropArea blocks wheel event delivery in Qt6".

Audit result - the current structure is already correct, so no routing code
was added:

- **Sub-region routing (badge vs task) is correct by z-order.** The audio
  badge's `MouseArea` lives inside the icon's `Flow` at `z:10`
  (`declarativeimports/abilities/items/BasicItem.qml:285`); `TaskMouseArea` is
  a plain child at default `z:0`. So a wheel over the badge hits the badge
  handler (volume) and a wheel anywhere else on the task falls through the
  visual-only icon items to `TaskMouseArea` (cycle). No explicit
  hit-test-and-route is needed.
- **The DropArea does not block the tasks' wheel.** `MouseHandler` (which
  holds the `preventStealing` `DropArea`) is declared *before* `ScrollableList`
  in `plasmoid/.../ui/main.qml`, so the task list stacks above it; wheel over a
  task reaches `TaskMouseArea`, not the DropArea beneath.

**Deferred to Phase 8 (live verification):** whether the containment actually
delivers wheel events *into* the tasks plasmoid depends on the Phase 8
containment wheel bridge, which does not exist yet. Once it lands, verify on a
live session that (a) scrolling over a task cycles/activates and (b) scrolling
over an audio badge on a task changes that app's volume. This is a live-only
check (needs a real audio-playing window on a task); it is recorded in
`docs/reference/live-only.md`.

### Phase 7 needs a hands-on session - most of it can't be verified without you
Phase 7 (widget management, drag-and-drop, edit mode) is ~90% interactive, and
its verification fundamentally needs gestures I can't perform autonomously on
wayland (dragging a widget from the explorer, tap-to-add, drag-to-reorder,
entering/leaving edit mode) - there is no `xdotool` equivalent for wayland
drag-drop, so unlocking the screen wouldn't help either. What I did headlessly,
and what still needs you:

**Done in code (deterministic, build-verified):**
- Widget removal (`33830b2c`): `LayoutManager::removeAppletItem` finalizes the
  destroy immediately instead of parking for a follow-up signal that never
  comes on Plasma 6. This is straight C++ logic and matches the plan's exact
  prescription. **Human test:** in edit mode, remove a widget from the dock and
  confirm it actually disappears.

**Adopted the working fork's code, pending your test:**
- Widget explorer add path (`0aa7ffb6`): moved `AppletDelegate.qml` + `WidgetExplorer.qml` from the
  crashing latte-dock-qt6 versions we inherited to latte-dock-ng's
  confirmed-working ones (TapHandler for tap-add, fuller upstream delegate
  layout). `WidgetExplorer.qml` imports `org.kde.plasma.private.shell` so the
  compile gate can't check it, and add is a drag/tap gesture. **Human test:**
  open Add Widgets in edit mode - does the explorer open cleanly (not
  mispositioned/undecorated), and can you add a widget both by dragging and by
  tapping, *including the Latte Tasks widget* (the one that crashed the other
  fork), without a crash or a double-add?

**Researched, base is right, needs live verification (not reimplemented):**
- Drag-to-reorder: our containment QML is already latte-dock-qt6's, which the
  plan identifies as the fork whose reorder works *better* (read its source,
  don't start from ng's four-times-fought tuning). So we are on the
  recommended base already. **Human test:** drag-reorder widgets repeatedly and
  watch for jitter and for the "icon stuck behind other elements" bug.
- Edit-mode entry/exit: we use the direct `Plasmoid.userConfiguring` binding
  (qt6's approach), not ng's polling-timer stack. The plan says ng found the
  notification unreliable on Qt6/Plasma6 across 8 attempts; whether *we* need a
  fallback is a live question. **Human test:** enter and leave edit mode
  several ways (context menu, shortcut) and confirm the dock reliably knows.
- Parabolic hover-zoom smoothness (always-visible synchronous MouseArea vs
  queued): the pattern is perceptual; can only be judged live.

If any of the human tests above fails, that's the signal to do the deeper
per-item work the plan describes - I did not want to blind-swap more
interactive subsystems I can't verify, or tick items the plan's own cadence
says aren't done until driven live.

### Middle-click was dead (`Qt.MidButton` undefined in Qt6) - FIXED 2026-07-08
Found during the ng upstream audit (docs/archive/ng-upstream-audit.md finding A) and
fixed the same day. `Qt.MidButton` was removed from the QML Qt enum in Qt6, so
it evaluated to `undefined`; every `mouse.button === Qt.MidButton` was always
false. Consequences before the fix: task middle-click actions (close / new
instance / paste, whatever `middleClickAction` is set to) never fired, the
middle-click ClickedAnimation never played, and the empty-area
close-active-window did nothing. Renamed all sites to `Qt.MiddleButton`:
`plasmoid/.../task/TaskMouseArea.qml` (3), `.../animations/ClickedAnimation.qml`
(1), `containment/.../layouts/EnvironmentActions.qml` (2). EnvironmentActions now
accepts the middle button only when `closeActiveWindowEnabled` is on, so with the
feature off the middle-click falls through to the containment's own middle-click
action instead of being swallowed (this is the "double-handling" the old
PORTING_PLAN note wrongly tried to avoid by keeping the broken enum). **Human
test:** middle-click a task (should run the configured middle-click action);
enable "close active window on middle-click" and middle-click an empty dock area
(should close the active window); with that setting off, a configured containment
middle-click action should still fire. Not bundled: ng also flips the
`middleClickAction` default from NewInstance(2) to Close(1) - left our default
alone (it is a UX preference, not part of the bug).

## Resolved
(none yet)
