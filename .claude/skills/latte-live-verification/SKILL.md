---
name: latte-live-verification
description: Use when launching, restarting, driving, screenshotting, or verifying the staged latte-dock live on the Wayland session, including edit mode, pointer injection, window geometry dumps, and config round-trip checks.
---

# Latte live verification

How to drive the staged dock on the real Wayland session and prove a change
works. For crash analysis beyond the gdb wrapper here, see latte-debugging.
For the build/stage environment itself, see latte-build-env.

## Golden rule

Never claim a UI change works without driving it live and reading pixels
(screenshot) or true geometry (dumpwins). A green build proves nothing about
runtime behavior. "It compiles" and "the QML gate passes" are preconditions,
not verification.

## 1. Restarting the dock

ONLY restart via the script. Bare `pkill` leaves SIGSTOPped orphans (state T)
that never see SIGTERM, and the next launch silently stacks a second instance;
three docks fighting over layer surfaces has actually happened.

Standard invocation (throwaway config, gdb wrapper, log file):

```bash
cd ~/Projects/latte-dock
LATTE_RUN_WRAPPER="gdb -batch -x $PWD/scripts/tools/gdb-batch-cmds --args" \
  scripts/restart-staged.sh -d > "$SCRATCH/latte.log" 2>&1 &
```

where `$SCRATCH` is your session scratchpad directory. Facts to know:

- The script TERM+CONTs old instances, escalates to KILL, refuses to start
  while any instance survives, and launches via setsid with stdin closed.
- It restages QML on every run. QML/package changes need NO rebuild, just a
  restart. C++ changes (app/, declarativeimports/) need
  `nix develop -c cmake --build build --target latte-dock` first.
- WARNING: stop the dock BEFORE rebuilding. A running dock executes a
  deleted binary and crashes confusingly mid-session.
- The gdb wrapper (scripts/tools/gdb-batch-cmds) prints a 50-frame backtrace
  plus all threads on any crash. KCrash self-destructs in this environment,
  so without the wrapper a crash yields nothing. For a hang: send SIGINT to
  the latte-dock pid from another shell; gdb dumps the live stacks.
- Default config home is a throwaway (build/_runconfig). Pass
  `--user-config` as the FIRST argument only when you deliberately want the
  user's real ~/.config (`scripts/start-dock.sh` is the daily-driver
  shorthand for exactly that). Layout files live at
  build/_runconfig/latte/*.layout.latte in the throwaway case.
  `LATTE_CONFIG_HOME=<dir>` points the throwaway elsewhere and
  `BUILD=<dir>` runs an alternate build tree (both used by the nested
  harnesses).
- Validate QML before launching: `nix develop -c scripts/qml-compile-gate.sh`.

To STOP the dock without restarting (end of a session, leave the machine
clean), there is no stop-only flag; run the script's own kill sequence:

```bash
pkill -TERM -x latte-dock; pkill -CONT -x latte-dock
for i in $(seq 1 50); do pgrep -x latte-dock >/dev/null || break; sleep 0.2; done
pgrep -ax latte-dock && echo "STILL ALIVE - escalate: pkill -KILL -x latte-dock"

## 2. Entering edit mode

```bash
busctl --user call org.kde.kglobalaccel /component/lattedock \
  org.kde.kglobalaccel.Component invokeShortcut s "show view settings"
```

Known traps, all observed live:

- The FIRST invoke after a restart often races kglobalaccel registration and
  does nothing. Check the dock log for `#primaryconfigview#`; if absent,
  invoke again.
- If settings are already open, another invoke CYCLES to the NEXT view's
  settings (multi-dock layouts). Two rapid invokes toggle open then closed.
- The settings window closes on focus loss within seconds of anything else
  taking focus. Capture your screenshot about 2 seconds after the invoke,
  before touching anything else.

Robust open sequence: invoke once, `sleep 2`, then
`grep -q '#primaryconfigview#' "$SCRATCH/latte.log"`; if it is absent,
invoke once more and sleep again.

## 3. True window geometry: dumpwins

```bash
scripts/tools/dumpwins.sh
```

Prints one `DUMPWIN|class|caption|x,y WxH|output|layer=N` line per KWin
window via a transient KWin script (output read back from the kwin journal).
Docks are layer=3, menus layer=6. This is the ONLY source of truth for
layer-shell placement; do not infer placement from QML properties or
QWindow geometry, which lie on Wayland. Use it to verify screen moves,
edit-chrome placement, and popup positions. Related defect background:
latte-plasma6-defect-families.

## 3b. Removing views headlessly: the undo-window trap

`busctl --user call org.kde.lattedock /Latte org.kde.LatteDock
removeView u <id>` rides the SAME libplasma undo machinery as widget
removal: the containment stays alive (and in the layout file) until
the Panel Removed notification closes or the 60s fallback timer
fires. Restarting the dock inside that window RESURRECTS the
containment. After removeView, wait until the `[Containments][<id>]`
group is gone from the layout file before restarting:

```bash
until ! grep -q "^\[Containments\]\[13\]$" "$layoutfile"; do sleep 5; done
```

Always verify the id against the layout file (lastScreen/location/
plugin) before calling removeView - a mis-parsed id once deleted the
wrong dock (the d6d57e61-era incident note).

## 4. Pointer injection: fakepointer

Source: scripts/tools/fakepointer.c. Usage:

```bash
fakepointer move <x> <y>
fakepointer click <x> <y>        # left click at absolute position
fakepointer rightclick <x> <y>
fakepointer drag <x1> <y1> <x2> <y2>   # press, 24-step glide, release
fakepointer key <keysym> [down|up|press]   # inject a key (default press = down+up)
```

Coordinates are GLOBAL compositor logical coordinates. drag is how
configure-applets reordering is verified headlessly; note synthetic
drags are grab-point sensitive, so confirm the grab landed by checking
appletOrder actually changed rather than assuming.

`key` names the key by its XKB keysym (`Escape`, `Up`, `Return`, `space`,
or a numeric literal like `0xff1b`), sent through `keyboard_keysym` so the
COMPOSITOR maps it against its own keymap - no fragile scancode. It needs
fake_input interface v6. A standalone `key` cannot inject into a
pointer-button-held drag (the button releases when the tool process exits),
so it is used to cancel PERSISTENT modes: the acceptance test
`tests/e2e/080-key-escape-cancels-move.sh` drives KWin's keyboard
interactive window-move and shows Escape aborts+restores while Return
commits. `down`/`up` hold or release a key (a modifier), but for the same
reason cannot span two invocations across a drag.

When hovering DOCK ITEMS, GLIDE - move in small steps (~8px with short
sleeps) - never jump straight to an icon's rest coordinates. The
parabolic zoom shifts every icon while any of them is hovered, so a
jump lands beside the shifted icon and the enter event never fires:
this produced hours of phantom "hover didn't engage" flakiness and one
false content-bug lead (2026-07-15) before it was understood. A real
mouse always glides, which is why desk repros disagree with jumped
synthetic ones.

Build (inside the devshell, i.e. under `nix develop`):

```bash
xml=$(pkg-config --variable=pkgdatadir plasma-wayland-protocols)/fake-input.xml
# the devshell may not carry plasma-wayland-protocols in pkg-config;
# fall back to locating the xml in the store:
#   ls /nix/store/*plasma-wayland-protocols*/share/plasma-wayland-protocols/fake-input.xml
wayland-scanner client-header "$xml" fake-input-client-protocol.h
wayland-scanner private-code  "$xml" fake-input-protocol.c
cc -O2 -I. -o fakepointer fakepointer.c fake-input-protocol.c \
   $(pkg-config --cflags --libs wayland-client xkbcommon)
```

The `xkbcommon` link is for the `key` verb's `xkb_keysym_from_name`; a
build missing it fails on that symbol.

KWin gates org_kde_kwin_fake_input PER CLIENT. The binary needs a desktop
file in ~/.local/share/applications whose Exec is the ABSOLUTE path of the
binary, matching exactly, plus the interface declaration; then rebuild the
sycoca:

```ini
[Desktop Entry]
Type=Application
Name=fakepointer
Exec=/absolute/path/to/fakepointer
NoDisplay=true
X-KDE-Wayland-Interfaces=org_kde_kwin_fake_input
```

```bash
kbuildsycoca6
```

WARNING: if the binary moves, the desktop file's Exec must be updated and
kbuildsycoca6 re-run, or the registry silently stops listing fake_input and
fakepointer reports "compositor does not expose org_kde_kwin_fake_input".
The canonical installed location is $HOME/.local/bin/fakepointer with
~/.local/share/applications/fakepointer.desktop pointing at it (verified
working 2026-07-12). Desktop files do not expand variables, so the Exec
line must carry the expanded absolute path. If the binary is missing,
rebuild from scripts/tools/fakepointer.c into ~/.local/bin per the recipe
above, fix Exec, re-run kbuildsycoca6, and confirm with a
`fakepointer move` call.

One known quirk: a synthetic click on a control that shrinks the input mask
mid-click (the rearrange toggle) once lost its release event, leaving the
button stuck pressed. Not reproduced with a real mouse; if a click seems to
do nothing twice in a row, suspect this.

## 5. Screenshots

```bash
spectacle -b -n -o "$SCRATCH/shot.png"
```

Then Read the png. The image is the WHOLE virtual desktop at 1:1 compositor
logical pixels, so a pixel coordinate in the screenshot IS the global
coordinate to feed fakepointer.

WARNING (2026-07-15, cost three mis-clicks): spectacle itself appears
as a TASK ICON in the dock while it captures, shifting every applet
right of it by roughly an icon width. Never locate-then-click across a
spectacle run: re-locate in the same capture you click from, or key
positions off dumpwins window geometry instead of pixels. Related
click-eater: STALE CHROME POPUP WINDOWS (the Type combo's Dock/Panel
popup, the secondary advanced config window) can persist across
sessions at wrong geometry and swallow clicks aimed at things under
them - three rearrange-toggle clicks in a row once landed on a stuck
popup. If clicks at verified coordinates do nothing, dumpwins and look
for small stale latte windows (layer 6 especially) overlapping the
target before doubting the coordinates.

WARNING: a ~14KB all-white png means the screen locked or the display slept,
NOT a broken tool. Handle it:

```bash
kscreen-doctor --dpms on          # wake the display first
loginctl unlock-session           # authorized for verification runs
# ... capture ...
loginctl lock-session             # re-lock when done
```

The session auto-locks on a timer, so expect to repeat this unless an
inhibitor is running (section 9).

## 6. Config round-trip verification

Toggling a settings control must write into the layout file:

```bash
grep -n someKey "$HOME/Projects/latte-dock/build/_runconfig/latte/My Layout.layout.latte"
# toggle the control (fakepointer click), then grep again
```

WARNING: KConfig DELETES keys whose value returns to the default. A key
disappearing after toggling back to the default is NORMAL, not a lost write.
Grep before AND after each toggle and reason about defaults, not just
presence.

## 7. D-Bus surface for deterministic driving (PREFER THIS)

The interface is now the complete observability surface - full method
list, signatures, JSON field lists and recipes live in
docs/dbus-interface-reference.md. The habits that replace older
sections of this skill:

- Poll `lifecycleState` for "running" instead of sleeping after a
  restart, then poll `viewsData` until no view reports
  `inStartup: true`.
- Read `viewsData` for hidden-state, mask rects, input regions,
  struts and edit-mode state instead of screenshots or dumpwins
  (dumpwins remains the truth for NON-dock windows and layer/stacking
  questions).
- `setViewEditMode ub <cid> true` replaces the kglobalaccel
  invoke-and-race dance in section 2 for entering edit mode
  programmatically (the invoke path stays for testing the shortcut
  itself).
- `setViewVisibilityMode us <cid> <name>` replaces stop-dock,
  kwriteconfig6, restart for visibility-mode flips (it persists).
- `trackerData u <cid>` answers the dodge questions (touching,
  maximized, active) that used to need window acrobatics plus eyes.

Interface name is `org.kde.LatteDock` (capital L, capital D) even though the
bus name is lowercase `org.kde.lattedock`. Example:

```bash
busctl --user call org.kde.lattedock /Latte org.kde.LatteDock addView us 0 "Default Panel"
```

## 7b. The nested vehicle: live verification WITHOUT the desk

Most recipes that only assert on STATE now run against a nested
compositor, fully isolated from the real session - the dock, pointer
injection (no desktop-file allowlist needed there), a real client
window for dodge tests, and the whole D-Bus surface on a private bus.
The EX-10 visibility matrix ran this way end to end. Shape and traps
(dock needs its own dbus-run-session or KDBusService kills it;
kwin and probes must share ONE bus or KWin scripting silently no-ops;
`-d` or near-silence): latte-debugging's "Isolated reproduction"
section and the session-handoff nested recipes. Live verification on
the real session stays fully sanctioned whenever a recipe needs it
(feel, focus interplay with a human, real-mouse semantics, DPMS, or
anything the nested vehicle cannot carry) - the vehicle exists to
free the desk, not to forbid it. When both can serve, prefer the
vehicle; when the desk is in active use, prefer it harder.

## 8. Confound control: DPMS and autolock

Kill the blank-screen/autolock confounds for the whole session with
`kde-inhibit --power --screenSaver sleep infinity &` (dies with the
session, nothing to undo). Verify it registered:

```bash
busctl --user call org.freedesktop.PowerManagement.Inhibit \
  /org/freedesktop/PowerManagement/Inhibit \
  org.freedesktop.PowerManagement.Inhibit HasInhibit
# expect: b true
```

## 9. Worked example: verify a settings toggle end to end

```bash
cd ~/Projects/latte-dock
SCRATCH=/path/to/session/scratchpad

# 1. confounds off
kde-inhibit --power --screenSaver sleep infinity &

# 2. restart under the gdb wrapper into a log
LATTE_RUN_WRAPPER="gdb -batch -x $PWD/scripts/tools/gdb-batch-cmds --args" \
  scripts/restart-staged.sh -d > "$SCRATCH/latte.log" 2>&1 &
sleep 8   # wait for views; check the log for "Adding View"

# 3. open edit mode robustly (first invoke may race registration)
busctl --user call org.kde.kglobalaccel /component/lattedock \
  org.kde.kglobalaccel.Component invokeShortcut s "show view settings"
sleep 2
grep -q '#primaryconfigview#' "$SCRATCH/latte.log" || busctl --user call \
  org.kde.kglobalaccel /component/lattedock \
  org.kde.kglobalaccel.Component invokeShortcut s "show view settings"

# 4. where is everything, really
scripts/tools/dumpwins.sh          # settings window geometry, layer, output

# 5. grep the layout key state BEFORE
grep -n theKey "build/_runconfig/latte/My Layout.layout.latte" || echo absent

# 6. click the control (coordinates from dumpwins + a screenshot)
kscreen-doctor --dpms on
spectacle -b -n -o "$SCRATCH/before.png"   # Read it; find the control
/path/to/fakepointer click <x> <y>

# 7. verify all three ways
spectacle -b -n -o "$SCRATCH/after.png"    # Read it: did the UI change?
grep -n theKey "build/_runconfig/latte/My Layout.layout.latte" || echo absent
tail -50 "$SCRATCH/latte.log"              # no new warnings or backtrace
```

If any step disagrees with expectation, stop and root-cause it; see the
failure rules in CLAUDE.md and latte-debugging. For commit shape when the
fix lands, see CLAUDE.md (Commit shape and definition of done); for where the touched code lives, see
latte-architecture; for whether a reference fork already fixed it, see
latte-fork-sync.
