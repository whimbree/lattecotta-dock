# latte-dock (Plasma 6 / Qt6 port)

## What this is

A from-scratch Plasma 6/Qt6 port of upstream KDE latte-dock
(`invent.kde.org/plasma/latte-dock`), forked to `whimbree/latte-dock`
on GitHub with full original history intact - not derived from either
community fork below.

## End goal

Two things, explicitly, not in tension: a production-grade daily-driver
dock, and a learning vehicle for the user (new to Qt6/KF6 and its
moving parts). Eventual upstream contribution to KDE is the aspiration
- keep history clean and commits reviewable-shaped from the start
because of this, not as an afterthought before submitting.

Explain Qt6/KF6/QML concepts as they come up rather than just applying
fixes silently - this is explicitly wanted, not a nice-to-have.

## Why this repo exists instead of adopting an existing fork

Two community Plasma6 ports already exist:
`~/Projects/latte-dock-ng` (ruizhi-lab, Wayland-only, history reset by
its maintainer) and `~/Projects/latte-dock-qt6` (CaptSilver, also
Wayland-only despite some leftover dead `isPlatformX11()` branches in
the code - its own commit `1cef7fe7` confirms the X11 backend was
actually removed - real original history preserved). Both were
evaluated in depth
(build + run + live crash debugging, not just reading commit messages)
before deciding to start fresh - see
`~/Projects/latte-dock-ng/docs/fork-comparison-journal.md` for the full
writeup. Summary: both forks have real, live, user-facing bugs found
through actual testing (not just static analysis), and latte-dock-ng's
reset history makes it a bad base for something meant to eventually go
back upstream. Both are kept as **reference material** - read their
diffs for a given subsystem, take what's actually correct, discard the
rest, write new commits.

## Plan

`docs/PORTING_PLAN.md` has the real detail - it's the product of
reading every commit body (not just subjects) across both reference
forks' full port-work ranges (406 commits in latte-dock-ng, 194 in
latte-dock-qt6), cross-referenced against live testing of both. 13
phases (0-12): build environment/testing ground rules -> build system
-> mechanical Qt6 conversion -> KF6 migration -> window-system
backends (Wayland primary, X11 best-effort: must compile, never
blocks, author runs Wayland only) ->
QML controls/rendering -> task manager -> widget
management/drag-drop/edit-mode -> layout/shutdown/multi-screen ->
theming polish -> stabilization -> Nix packaging -> upstream prep.

Phase 7 (widget management/drag-drop/edit-mode) and Phase 8 (shutdown/
multi-screen) carry the most specific gotchas - both reference forks
needed many repeated fix attempts there (edit-mode detection alone took
8+ tries in one fork). Read those sections before touching either
subsystem, not just the phase list here.

**The plan is a checklist, not prose to read once.** Every task is a
`- [ ]` with a `Commits:` line. When a task lands, tick it and fill in
the commit hash(es) - that's the traceability mechanism for the whole
port: any checklist item names its commit(s), any commit can be traced
back to the task it was for. 127 atomic items across the 13 phases as
of the last plan revision. Keep this
in sync as work happens - don't let it drift into "mostly done, some
stale checkboxes."

## Working agreements

- Behavior is Qt5-faithful: when the port and Qt5 Latte disagree on how
  something behaves (defaults, semantics, what a control adjusts, what
  is drawn where), Qt5 Latte is the spec unless a platform constraint
  genuinely forces a change - and then the deviation gets a comment
  saying what forced it. Both reference forks reinterpreted behaviors
  in ways users notice (e.g. edit-mode grid opacity rewired from
  editBackgroundOpacity to panelTransparency); read the Qt5 source
  before accepting a fork's version of any behavior.
- Never add Co-Authored-By or other AI attribution to commits (global
  preference, not specific to this repo).
- No em-dashes, no AI-sounding marketing-style phrasing in docs, commit
  messages, or code comments - write plainly, like a programmer.
- Push to origin (whimbree/latte-dock) after each big chunk of landed,
  verified work - do not let long sessions accumulate dozens of unpushed
  commits.
- Prefer new commits over amending, except when explicitly asked (e.g.
  cleaning up history before opening a PR).
- This file and `docs/PORTING_PLAN.md` are committed, but both are live
  documents - update them as direction changes or new information comes
  in (e.g. from watching the reference forks' ongoing work) rather than
  treating either as fixed once written.

### Failures and root cause

- Never silently swallow a failure. A quiet early-return, an empty
  catch, a `?:` that hides a null, a clamp that papers over a bad value -
  all worse than the failure itself, because they turn a loud, findable
  bug into a silent, wrong-behaviour one that surfaces somewhere far away.
  If something cannot proceed, say so: `qWarning()`/`qCritical()` with
  enough context to act on, or fail loudly. Silence is not error handling.
- A degenerate value is a symptom, not a thing to guard away. A zero-size
  window, a null containment, an empty action list, an index of -1 - none
  of these are normal states to clamp back into range. Ask what produced
  it and why, and fix it there. `if (size.isEmpty()) return;` at the point
  of use is a bandaid; the bug is whatever handed you an empty size.
- Trace every failure to its root cause before writing a fix. "It stopped
  crashing" is not "it is fixed" if the fix is a guard downstream of the
  real defect. Prefer the fix at the origin. If a guard is genuinely the
  right layer (e.g. a real optional that is legitimately absent), say why
  in a comment, so it reads as a deliberate contract and not a patch over
  something not understood.
- When you cannot see why from the code, instrument and reproduce - add
  temporary logging that prints the actual values at the actual moment,
  drive the failure, read it, then remove the instrumentation. Guessing
  and clamping is how bandaids accrete.

### Regression discipline

- Know a change's blast radius before making it, especially environment,
  launcher, build and plugin/module-resolution changes. Adding a directory
  to `QML2_IMPORT_PATH`, `QT_PLUGIN_PATH`, `XDG_DATA_DIRS` or the like is
  never "narrow": it can shadow ANY same-named module or plugin the process
  already resolves. A broad append of the system Qt6 QML tree once replaced
  the dock's right-click menu with the stock task menu, because Latte's
  `import org.kde.taskmanager` then resolved from the system copy instead of
  Latte's pinned one. If you must expose extra modules/plugins, allow-list
  the specific leaves (or symlink them into a private tree), never add a
  shared root that also carries components we ship our own copies of.
- Verify causation, do not assume it. When something regresses, isolate the
  one variable and prove it - revert it alone and retest, or read the actual
  loaded state - before "fixing" the suspected cause. Two wrong guesses in a
  row cost more than one measurement. The launcher QML-path change and the
  QT_PLUGIN_PATH gap were distinct faults; only reverting and re-reading the
  running process's env told them apart.
- After a fix, confirm it against the real running artifact (logs, the live
  dock), not just "it builds". A green build says nothing about whether the
  menu came back.

### Stub tracking

Anything stubbed to keep a phase moving - a function returning a
placeholder, a feature deferred to a later phase, a config page that
renders but isn't wired up - gets marked two independent, greppable
ways so it can't quietly get forgotten once a phase "builds fine":

- Commit subject prefixed `stub:` - its own conventional-commit type
  alongside feat/fix/docs/test/build/ci/chore. `git log --oneline |
  grep '^[a-f0-9]* stub:'` (or `git log --grep '^stub:'`) finds every
  one across the whole history.
- A `// STUB:` comment (or `# STUB:` in QML/CMake) at the exact
  location in code, stating what's missing and which phase should
  finish it. `grep -rn 'STUB:'` finds every currently-live stub
  without needing git history at all - this is the one that matters
  if someone's just reading the code, not archaeology-ing the log.
- The commit body says why it's stubbed now instead of done right, and
  what "done" actually looks like - not just "TODO fix this."

Never stub something silently under a `fix:`/`feat:` message. Case in
point, inherited from upstream rather than introduced by either fork:
the original latte-dock's Tasks config page rendered but never actually
applied its settings - a genuinely half-finished upstream feature that
was never marked as a stub anywhere. It went unnoticed across every
fork for a long time until latte-dock-ng's `9faccabda fix: hide
unfinished Tasks configuration tab` found it and hid it rather than
shipping something that silently did nothing (the right call at the
time). Mark it as a stub in the first place and this kind of silent gap
doesn't happen. (Since fixed properly upstream in latte-dock-ng's
`eabf7c89a`/`94f87ba66`/`ed0afd054` - see Phase 5/6 in
`docs/PORTING_PLAN.md` for the actual Plasma 6 config-access pattern
that fix uncovered, which is worth carrying into this port directly.)

## Reference fork sync status

Both reference forks are active projects, not frozen snapshots -
latte-dock-ng in particular is moving fast. Track the last commit
actually reviewed for each, so a future "check for updates" is a diff
from here, not a re-read of the whole history:

- **latte-dock-ng** (`~/Projects/latte-dock-ng`, remote `origin` ->
  `ruizhi-lab/latte-dock-ng`): reviewed through `59e04b8b7` (2026-07-04).
  Check for new work:
  `cd ~/Projects/latte-dock-ng && git fetch origin && git log --oneline 59e04b8b7..origin/main`
  If that shows anything, read full bodies (not just subjects) via
  `git log --format="%n=== %h %s ===%n%b" 59e04b8b7..origin/main`
  before deciding what's worth folding in, then update this hash.
- **latte-dock-qt6** (`~/Projects/latte-dock-qt6`, remote `origin` ->
  `CaptSilver/latte-dock-qt6`): reviewed through `9003f33a` (also the
  end of the port-work range named in the original comparison request).
  Less active than latte-dock-ng so far; same check pattern applies if
  it picks back up:
  `cd ~/Projects/latte-dock-qt6 && git fetch origin && git log --oneline 9003f33a..origin/main`
- **plasma-desktop** (KDE upstream, not a fork): the vendored
  task-manager backend in `plasmoid/plugin/` (backend, smartlauncher*)
  derives from `applets/taskmanager/` there, which keeps evolving
  inside KDE's C++ applet. At every sync pass, also diff upstream's
  files against our copies and port their fixes:
  `curl -s "https://invent.kde.org/api/v4/projects/plasma%2Fplasma-desktop/repository/files/applets%2Ftaskmanager%2Fbackend.cpp/raw?ref=master" | diff - plasmoid/plugin/backend.cpp`
  (ours carries deliberate extensions - KWin D-Bus watcher,
  showAudioStreamOsd - so expect a stable base diff; look for NEW
  upstream hunks. See docs/taskmanager-integration-research.md for the
  vendor-vs-integrate analysis and the decision record.)

## Current status

Phase 0 (build environment + testing ground rules) not yet started.
`CMakeLists.txt` at HEAD is still Qt5 5.15.0 / KF5 5.88.0, X11
required unconditionally.
