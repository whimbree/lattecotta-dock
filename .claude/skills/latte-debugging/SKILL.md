---
name: latte-debugging
description: Use when the latte-dock port crashes, hangs, spins CPU, or misbehaves and you need the actual root cause, not a guard that hides it.
---

# Debugging the latte-dock port

This skill is the crash/hang/misbehavior playbook. Related skills:
latte-live-verification (driving the dock: restart, fakepointer, screenshots),
latte-plasma6-defect-families (recognizing recurring bug classes),
latte-build-env (building and staging).

## The discipline (non-negotiable, from CLAUDE.md)

- Never guard away a degenerate value. A zero-size window, a null
  containment, an index of -1 are symptoms. `if (x.isEmpty()) return;` at
  the point of use is a bandaid; find what produced the bad value and fix
  it there. If a guard really is the right layer (a genuine optional),
  say why in a comment.
- Never silently swallow a failure. No quiet early-returns, empty catches,
  or clamps. If something cannot proceed, qWarning()/qCritical() with
  enough context to act on.
- Instrument, drive, read, remove. When the code alone does not explain
  the failure, add temporary logging that prints the actual values at the
  actual moment, reproduce, read the log, then delete the instrumentation.
- Verify causation by isolating one variable. Revert the suspect alone and
  retest, or read the actual loaded state of the running process, before
  "fixing" a guessed cause. Two wrong guesses cost more than one
  measurement.
- "It stopped crashing" is not "it is fixed" if the fix is downstream of
  the real defect. Confirm the fix against the running dock (logs, pixels,
  driven interaction), never just a green build.

## Read the state before instrumenting

The dock now answers most "what is it doing right now" questions over
D-Bus with zero code changes - check the pull surface BEFORE writing
instrumentation (docs/dbus-interface-reference.md has every method
and recipe):

- Is it up / how far into startup: `lifecycleState`, then `viewsData`
  per-view `inStartup`/`isOffScreen` (the startup-stranding bits).
- Invisible or dead-input dock: `viewsData` `isHidden`, `maskRect`,
  `inputRegionRects`, `publishedStruts` - the mask questions that used
  to need screenshots.
- Dodge/visibility logic: `trackerData` (activeWindowTouching and
  friends) against what you believe the windows are doing.
- Colorizer decisions: `colorizerData`; applet/task model state:
  `viewAppletsData` / `viewTasksData`.

Instrumentation is for the step AFTER these reads localize the
question to code the surface cannot see. And per observability-first
(CLAUDE.md): if your instrumentation printed state a future test will
want to assert on, that state probably deserves a readback on the
surface instead of a one-off log line.

## Crash capture

KCrash is useless here: it has been observed crashing while handling a
crash (crashRecursionCounter = 2), losing the core. Yama ptrace scope
blocks `gdb -p` on a running dock. So gdb must launch the dock as its
child, via the run wrapper:

```
cd ~/Projects/latte-dock
LATTE_RUN_WRAPPER="gdb -batch -x $PWD/scripts/tools/gdb-batch-cmds --args" \
  scripts/restart-staged.sh -d > /tmp/dock-gdb.log 2>&1 &
```

`scripts/tools/gdb-batch-cmds` runs the dock and, on any fault, prints
`bt 50` plus `thread apply all bt 12`. Notes:

- Use an absolute path for the batch file (the wrapper is word-split and
  gdb resolves relative paths against its cwd).
- `-d` makes the dock print debug messages; the log doubles as the
  forensics record.
- Keep the redirect. restart-staged.sh detaches the dock with setsid, but
  stdout/stderr are inherited, so the redirect captures everything.
- If a core does exist (crash outside the wrapper): `coredumpctl list`
  then `coredumpctl gdb <ID>`.
- Never rebuild C++ while a dock runs from build/bin; the running dock
  executes a deleted binary and crashes confusingly. Stop it first.

## Hang capture (100% CPU, D-Bus timeouts, frozen log)

Same wrapper, same launch. When the dock hangs, from another shell:

```
pkill -INT -x latte-dock
```

gdb intercepts the SIGINT, regains control, and the batch file prints the
live stack of every thread. That is a snapshot of the exact spot the event
loop is starved in.

Real example: the iconSize=78 startup hang. Log froze on "Updating
visibility mode :: AlwaysVisible", main thread at 100%. SIGINT under the
wrapper showed a QML bound-signal cascade that never returned; the root
cause was an infinite JS do-while in the autosize code whose exit
condition (`!== 16` while stepping by 8) could step past its floor, fed by
a degenerate unsized-window input on Wayland. Fix commit ad9b823f.

## CPU measurement trap

`ps %cpu` is a LIFETIME average. Right after a busy startup it reads high
for minutes even when the dock is idle. Measure a delta instead:

```
pid=$(pgrep -x latte-dock)
a=$(awk '{print $14+$15}' /proc/$pid/stat); sleep 3
b=$(awk '{print $14+$15}' /proc/$pid/stat); echo $((b-a)) ticks
```

0 ticks over 3 seconds means idle, regardless of what ps claims.

## Log forensics

Dock logs are colorized; ANSI escapes make grep treat them as binary.
Always `grep -a`.

Message filtering truth (post-2e87b99ec): WITHOUT `-d` the production
handler prints ONLY Critical and Fatal; Debug/Info/Warning are
swallowed. A production log with zero warnings proves nothing about
warnings - restart with `-d` before concluding anything from absence.
(Before that fix even Criticals were swallowed, which cost a full
investigation leg chasing a "config-specific" defect that fired
everywhere silently.)

Known benign noise. Do not chase any of these:

- KWindowShadow warnings (~56 per run).
- "Attempting to set another interceptor" (MultiLayered.qml already has a
  Behavior on opacity at its root; a second is ignored).
- Connections deprecation warnings from third-party applet QML.
- digitalclock Tooltip.qml "text" of null (third-party internal noise).
- "Tools.colorBrightness/colorLumina/isLight: invalid color" Critical
  BURSTS at view creation: the Kirigami attached theme serves
  default-invalid colors on the first creation-time binding
  evaluation and every consumer self-corrects on its change notify
  (mechanism documented at declarativeimports/core/tools.cpp). A
  STEADY stream at idle is NOT this and deserves a fresh hunt.

One line that has burned people twice:
`PLASMA SCREEN GEOMETRIES, CLEARED SCREEN :: X` is Latte's own strut
bookkeeping (app/plasma/extended/screengeometries.cpp:256). It fires when
an active screen simply has no space-reserving docks left on it, benignly
a short while after every dock start (observed around the 20s mark; the
figure is empirical, no timer in the file pins it) and on relocations. It is NOT an output
removal. For real monitor hotplug ground truth, run a kernel-level logger
alongside the session:

```
udevadm monitor --kernel --subsystem-match=drm > /tmp/drm-flap.log 2>&1 &
```

Zero lines there means zero physical flaps, whatever the dock log implies.

## QML TypeError semantics (load-bearing in this codebase)

- "Cannot read property X of undefined": the RECEIVER lacks the property,
  or the wrong object is in scope. First suspect: a QQC2 control property
  shadowing a context property or root id (the `indicator` checkbox
  family; see latte-plasma6-defect-families). Log the receiver object
  itself to see what is actually in scope.
- "Cannot read property X of null": the property EXISTS and its value is
  null. Different bug class: an async dependency not ready yet, or a
  destroyed object still bound.
- "Cannot assign to read-only property" THROWS, and the throw ABORTS the
  rest of that JS handler. Every statement after the assignment silently
  never runs. If a handler seems to half-execute, look above the dead code
  for a throwing assignment.

## Instrumentation pattern

1. Pick a unique greppable tag: past sessions used HANGDBG, ORDERDBG,
   INDDBG. One tag per investigation.
2. Add `qDebug() << "TAG:" << actualValues;` (C++) or
   `console.log("TAG:", values)` (QML) at the exact suspect points. Print
   the receiver object itself when scope is in question.
3. QML edits need NO rebuild: scripts/restart-staged.sh restages packages
   on every run. C++ edits need a cmake rebuild WITH THE DOCK STOPPED
   first.
4. Drive the failure, `grep -a TAG` the log, read the real values.
5. REMOVE all instrumentation before committing. The tree must carry none
   of it; that is why the tag must be greppable.

## Isolated reproduction: the nested vehicle

When the failure reproduces in the dock itself, do NOT iterate against
the live desk session. The staged dock runs fully isolated inside a
nested compositor with a private bus:

```
nix develop -c tests/sceneprobe/run_in_kwin.sh dbus-run-session -- \
  env LATTE_CONFIG_HOME=<config-copy> timeout 45 scripts/run-staged.sh -d
```

Three traps, all paid for: the dock inherits the caller's session bus
and EXITS INSTANTLY on the KDBusService unique name unless wrapped in
its own dbus-run-session; without `-d` it prints (almost) nothing; and
if probes need KWin scripting (window moves, maximize), kwin and the
probes must share ONE bus - wrap BOTH in the same dbus-run-session or
every org.kde.KWin call silently no-ops (the e2e nested wrapper in
tests/e2e has the correct shape once P4 lands; until then the handoff's
session-two entry carries it).

Instrumented binaries build in a SEPARATE tree and run with
`BUILD=<dir>`: `cmake -B build-probe -S . -G Ninja
-DCMAKE_BUILD_TYPE=RelWithDebInfo`, then
`BUILD=$PWD/build-probe scripts/run-staged.sh` inside the vehicle.
The live dock's build/bin is never touched, so no stop is needed and
no instrumented code ever reaches the real session.

QML-caller tracing: when a qCritical/qWarning boundary fires and the
question is WHICH QML site passed the bad value, a temporary
Qt6::QmlPrivate probe answers it in one run:
`qjsEngine(this)->handle()->stackTrace(4)` inside the C++ boundary
prints file:line:function for the calling binding (needs
`find_package(Qt6 COMPONENTS QmlPrivate)` + the target linking
Qt6::QmlPrivate, and an #ifdef so unit-test targets that compile the
same .cpp do not need the private headers). This named ten distinct
call sites in one run during the invalid-color hunt where static
reading had produced three wrong hypotheses. Remove before commit,
like all instrumentation.

## Environment confound control

- Long sessions: `kde-inhibit --power --screenSaver` holds off DPMS and
  autolock for its own lifetime (dies with the session, nothing to undo).
  Without it, screen sleep and lock inject output events mid-experiment
  that look exactly like the bugs you are hunting.
- Screenshot verification: run `kscreen-doctor --dpms on` first. A ~14KB
  all-white png means a locked or sleeping display, not a spectacle flake.
- See latte-live-verification for the full drive-and-capture loop.

## Intermittent reproductions

Do not fix against a bug you cannot summon. First make the reproduction
deterministic by synthesizing the triggering state. Example: the
applet-order defect could not show on a default-order layout, so a
non-default appletOrder was written into the throwaway layout by hand,
then duplicateView was driven over D-Bus; that turned "sometimes the copy
is scrambled" into a one-command reproduction. After the fix, re-run the
reproduction at least twice. Two of the case studies below had a second
cause hiding behind the first; one clean run proves nothing.

## Case studies (verified commits, for pattern recognition)

- ad9b823f: infinite loop from a modular-arithmetic exit condition
  (`!== 16` stepping by 8) plus a degenerate unsized-window input; fixed
  BOTH the loop exit and the degenerate input at its origin.
- 9a6f8fb8: applet ids silently read as 0 off the graphic item (Plasma 6
  removed the property), wiping every dock's persisted applet order on
  every startup. A silent-swallow defect: nothing ever warned.
- 33fa17d7: QQC2 `indicator` property shadowed the config context
  property; diagnosed by logging the receiver object, which printed as
  CheckIndicator_QMLTYPE instead of the config API.
- c3d15966: a dead probe (indexOfProperty for a property Plasma 6
  removed) made applet config sync silently inert for every applet since
  the port; the retry could never succeed because it used the same probe.
- 1607d022 + c5bdc239: one intermittent symptom (edit chrome stuck on the
  old screen), TWO stacked causes (mapped layer surfaces cannot change
  outputs; async screen id landing after all syncs ran). Fixing the first
  and stopping would have shipped the second.

## Checklist before calling anything fixed

- [ ] Root cause identified at its origin, not guarded downstream.
- [ ] Any remaining guard has a comment saying why it is the right layer.
- [ ] All instrumentation removed (`grep -rn 'YOURTAG'` is clean).
- [ ] Reproduction re-run at least twice against the running dock.
- [ ] Log checked with `grep -a`, benign-noise list not mistaken for
      evidence.
- [ ] If a checklist item in docs/PORTING_PLAN.md covers this, tick it
      and fill in the commit hash.
