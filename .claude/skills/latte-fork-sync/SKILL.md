---
name: latte-fork-sync
description: The periodic pass reviewing latte-dock-ng, latte-dock-qt6 and plasma-desktop upstream for work worth folding into the latte-dock Plasma 6 port.
---

# Reference-fork and upstream sync pass

This skill runs the periodic check of the three external sources this
port watches: two community Plasma 6 forks kept as reference material,
and KDE's plasma-desktop, which owns the upstream copy of the vendored
task-manager backend.

The canonical last-reviewed hashes live in CLAUDE.md, section
"Reference fork sync status". Always take the hashes from there at the
start of a pass. Do not trust hashes written in this file or anywhere
else; they go stale. The hashes cited below were correct when this
skill was written and are examples only.

## The three sync targets

### 1. latte-dock-ng (ruizhi-lab)

Local checkout: `~/Projects/latte-dock-ng`, remote `origin` points at
`ruizhi-lab/latte-dock-ng`. This fork moves fast. With LASTHASH taken
from CLAUDE.md (at this skill's last touch: `456154efb`):

```
cd ~/Projects/latte-dock-ng && git fetch origin && git log --oneline LASTHASH..origin/main
```

If that shows anything, read FULL commit bodies, not just subjects,
before deciding anything:

```
git log --format="%n=== %h %s ===%n%b" LASTHASH..origin/main
```

### 2. latte-dock-qt6 (CaptSilver)

Local checkout: `~/Projects/latte-dock-qt6`, remote `origin` points at
`CaptSilver/latte-dock-qt6`. It woke up in July 2026 with a testability campaign. Same pattern, with
LASTHASH from CLAUDE.md (at this skill's last touch: `81384003`):

```
cd ~/Projects/latte-dock-qt6 && git fetch origin && git log --oneline LASTHASH..origin/main
```

Read full bodies with the same format string if anything shows.

### 3. plasma-desktop (KDE upstream, not a fork)

The vendored task-manager backend in `plasmoid/plugin/` (backend.cpp,
backend.h, smartlauncherbackend, smartlauncheritem) derives from
`applets/taskmanager/` in plasma-desktop, which keeps evolving inside
KDE's C++ applet. At every pass, diff upstream's files against our
copies. The command for backend.cpp, from CLAUDE.md:

```
curl -s "https://invent.kde.org/api/v4/projects/plasma%2Fplasma-desktop/repository/files/applets%2Ftaskmanager%2Fbackend.cpp/raw?ref=master" | diff - plasmoid/plugin/backend.cpp
```

Repeat for backend.h, smartlauncherbackend.{h,cpp} and
smartlauncheritem.{h,cpp} by swapping the URL-encoded filename. Our
copies carry deliberate extensions (the KWin WindowView/HighlightWindow
QDBusServiceWatcher wiring and showAudioStreamOsd), so a stable base
diff is expected and normal. What you are looking for is NEW upstream
hunks that were not in the last pass: upstream bug fixes and behavior
changes worth porting.

## How to evaluate what you find

The forks are reference material, never a merge source. Never merge or
cherry-pick from them. Read their diffs, take what is actually correct,
discard the rest, and write NEW commits in this repo (per CLAUDE.md: new commits over amending, and Qt5 Latte is the behavior spec).

Before accepting any fork commit that changes behavior (defaults,
semantics, what a control adjusts), read the Qt5 Latte source first.
Both forks reinterpreted behaviors in ways users notice; a plausible
commit message is not evidence the change is Qt5-faithful.

Calibration examples from past passes (hashes are in latte-dock-ng
unless noted; verify before citing them onward):

- ng `010269d4d` found the same ComboBox pressed-handler defect we had
  already fixed independently. Fork findings can confirm a diagnosis
  even when we take none of their code.
- ng `eabf7c89a` wired up the Tasks config tab, a real upstream gap
  worth fixing, but did it with dynamic-property shims and retry
  timers where the direct applet chain is cleaner. Adopt the problem
  they found, not necessarily their solution.
- ng narrowed their vendored backend and dropped smart-launcher badges,
  leaving dangling QML references (a deleted SmartLauncherItem still
  instantiated, a connect to a removed slot that aborts the handler
  and silently kills the code after it).
  docs/reference/taskmanager-integration-research.md itemizes the damage. The
  decision on record is that this project will NOT repeat ng-style
  narrowing; treat any ng commit in that direction as rejected by
  default.

Many fork findings map onto defect families this port has already
catalogued; check the latte-plasma6-defect-families skill before
treating a fork fix as a new discovery.

## Vendored code sync specifics

The provenance headers at the top of the `plasmoid/plugin/` files name
the origin (plasma-desktop, Eike Hein's backend) and the adoption
commit in this repo. Keep those headers accurate. When porting an
upstream hunk, land it as a new commit here that says which upstream
change it ports, and keep the port line-for-line close to upstream
where our extensions allow, so the next pass's diff stays cheap. The
diffability against upstream is the whole reason the vendor was kept in
upstream's idiom (option A in
docs/reference/taskmanager-integration-research.md); do not restyle it.

## Process checklist

1. Read CLAUDE.md "Reference fork sync status" and note the recorded
   hashes for both forks.
2. Run the two fork checks (fetch, then `git log --oneline
   LASTHASH..origin/main`). For any new range, read full bodies with
   the documented format string.
3. Run the plasma-desktop diffs against `plasmoid/plugin/` and separate
   the known base diff (our extensions) from new upstream hunks.
4. Classify every finding, explicitly, one of three ways:
   - Fold in now: write a new commit in this repo (never a
     cherry-pick), crediting the source commit in the body.
   - File for later: add a checklist item to docs/tracking/PORTING_PLAN.md in
     the right phase, with a pointer to the source commit.
   - Reject: record a one-line note saying what it was and why it was
     rejected, so the next pass does not re-evaluate it from scratch.
5. Update the last-reviewed hashes in CLAUDE.md to the new tips you
   actually read through.
6. Record the pass (date, ranges reviewed, classification of each
   finding) in docs/tracking/session-handoff.md.

## Warning: worktree state vs recorded hash

The fork worktrees may sit on a different checkout than the
last-reviewed hash in CLAUDE.md. At the time of writing,
`~/Projects/latte-dock-ng` HEAD was `c705aa7e7` while the recorded
last-reviewed hash was `59e04b8b7`. Always compute the review range
against the RECORDED hash from CLAUDE.md, not against whatever HEAD
happens to be. And when you read files in a fork checkout to document
a finding, cite which checkout (commit) you actually read, the way
docs/reference/taskmanager-integration-research.md cites "read at ng checkout
c705aa7e7". A file citation without the checkout hash is not
reproducible once the worktree moves.

## Test-infrastructure findings route to the adoption plan

CaptSilver's testability campaign has its own standing plan:
docs/archive/captsilver-testability-adoption.md (P1-P5 waves, what is adopted
vs skipped, and the hard VM-only constraint). Fork commits about test
infrastructure are evaluated against THAT document, not folded ad hoc.
Two standing rules recorded there: the fork's tests are the quality
floor, never the ceiling (every adoption gets raised to this tree's
bar), and anything adopted must run in CI under a plain VM - GPU
support stays optional, never required.
