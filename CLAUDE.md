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
latte-dock-qt6), cross-referenced against live testing of both. 12
phases: build system -> mechanical Qt6 conversion -> KF6 migration ->
Wayland backend -> QML controls/rendering -> task manager -> widget
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
back to the task it was for. 119 atomic items across the 12 phases as
of the last sync with upstream latte-dock-ng's ongoing work. Keep this
in sync as work happens - don't let it drift into "mostly done, some
stale checkboxes."

## Working agreements

- Never add Co-Authored-By or other AI attribution to commits (global
  preference, not specific to this repo).
- No em-dashes, no AI-sounding marketing-style phrasing in docs, commit
  messages, or code comments - write plainly, like a programmer.
- Prefer new commits over amending, except when explicitly asked (e.g.
  cleaning up history before opening a PR).
- This file and `docs/PORTING_PLAN.md` are committed, but both are live
  documents - update them as direction changes or new information comes
  in (e.g. from watching the reference forks' ongoing work) rather than
  treating either as fixed once written.

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

## Current status

Phase 1 (build system migration) not yet started. `CMakeLists.txt` at
HEAD is still Qt5 5.15.0 / KF5 5.88.0, X11 required unconditionally.
