# X11 cleanup audit (post-removal survivor sweep)

The X11 windowing backend was removed across session three (PR #1 and its
follow-ups: `20a3c2506` removed `XWindowInterface`, `1ddb4140f` made `main()`
refuse every non-wayland platform, `9b41dd3ae`/`24e54b7d8`/`019c3aed3` stripped
the conditional arms and the visual-mask machinery, `3f857fbff` removed
`WITH_X11` from the build system, `ed9416a21` folded the two id-fallback
leftovers the isPlatformX11 sweep did not reach). See the Phase 4 section of
`docs/PORTING_PLAN.md` for the rationale and the removal checklist.

This audit is the survivor sweep: a whole-tree grep for the X11 vocabulary
(`X11`, `xcb`, `XCB`, `HAVE_X11`, `WITH_X11`, `isPlatformX11`, `X11Extras`,
`KX11Extras`, `WindowThumbnail`, `NETWM`, `_NET_`, `rootWindow`, `fromX11WId`,
`toX11WId`, `parseX11WindowId`, numeric `WId`, `byPassWM`,
`BypassWindowManager`) across `app/`, `plasmoid/`, `shell/`, `containment/`,
`indicators/`, `declarativeimports/`, every CMake file and every
`*.cpp`/`*.h`/`*.qml`, classifying each hit and executing the safe removals.

## End-state model (what the sweep confirmed)

The build-system removal is complete: no `HAVE_X11`/`WITH_X11` define survives
in any `*.h.cmake`, no `find_package(X11|XCB)` or xcb pkg-config call survives
in any CMake file, and no X11 buildInput survives in `flake.nix`/`package.nix`.

The window-id model after the removal:

- `WindowId` (`app/wm/windowid.h`) is a neutral wrapper newtype. Its storage is
  a `QByteArray`. Construction is only through named factories
  (`fromWaylandUuid`, `fromX11WId`), so no `QByteArray`/`char*` converts in by
  accident.
- The live wayland payload is `PlasmaWindow::uuid()` bytes, wrapped at the
  window-manager boundary. Every `uuid()` call in `app/` (all in
  `waylandinterface.cpp`) feeds `WindowId::fromWaylandUuid(...)` immediately;
  nothing carries the raw uuid bytes onward in parallel (see the WHY-BOTH
  check below).
- The X11 decimal-string payload is now produced only by `fromX11WId`, which
  still has two callers in `lattecorona.cpp` (proposals D2 and D3). It is no
  longer produced anywhere else.

## Classified inventory

Classification buckets: (A) DEAD/ORPHANED, remove; (B) KEEP, load-bearing
boundary discipline still reachable; (C) COMMENT explaining a live wayland
divergence (keep) or stale (prune/reword); (D) NOT-WANTED, live but X11-shaped,
propose only.

| Location | What it is | Reachable on a wayland-only build? | Bucket |
|---|---|---|---|
| `plasmoid/.../previews/PlasmaCoreThumbnail.qml` (whole file) | X11-only `PlasmaCore.WindowThumbnail` element | No: only reference was the else arm of the `isPlatformWayland` ternary at `ToolTipInstance.qml:197`; `main()` refuses non-wayland, offscreen has no compositor thumbnail either | A - REMOVED (`4c84c1bad`) |
| `ToolTipInstance.qml:197` | `isPlatformWayland ? PipeWire : PlasmaCore` thumbnail source ternary | else arm dead per above | A - collapsed to `PipeWireThumbnail.qml` (`4c84c1bad`) |
| `ToolTipInstance.qml:261,270-272` | `//X11 case` label + "X11 thumbnail item has no hasThumbnail" rationale | Stale after the collapse (no X11 thumbnail item remains); conditions themselves stay wayland-valid | C - reworded (`4c84c1bad`) |
| `app/view/helpers/floatinggapwindow.cpp:20-21` | `// X11` + `#include <NETWM>` (and orphaned `#include <KWindowSystem>`) | No: no NET*/KWindowSystem symbol used in the file or its header; SubWindow X11 flags path removed | A - REMOVED (`67bd25638`) |
| `app/view/helpers/screenedgeghostwindow.cpp:24-25` | Same dead `// X11`/NETWM + orphaned KWindowSystem block | No, same as above | A - REMOVED (`67bd25638`) |
| `app/plasma/extended/theme.cpp:29` | `// X11` label over `#include <KWindowSystem>` | Include is LIVE (line 51 `isPlatformWayland()`); label is stale (KWindowSystem is platform-neutral) | C - relabelled `// KDE` (`2f78a7d7e`) |
| `app/wm/windowid.h` `fromX11WId`/`toX11WId`/`parseX11WindowId` | The X11 decimal-string parse surface of the WindowId newtype | LIVE: two callers in `lattecorona.cpp` + unit tests | D - proposal D1 |
| `app/lattecorona.cpp:882` | `setKeepAbove(fromX11WId(aboutDialog->winId()), true)` | LIVE but a silent no-op on wayland (a Qt `WId` never matches a PlasmaWindow uuid in `windowFor()`) | D - proposal D2 (defect) |
| `app/lattecorona.cpp:1099-1111` | `windowColorScheme` else arm: decimal parse + `fromX11WId` | Only when `!isPlatformWayland` (production refuses; offscreen has no windows to color) | D - proposal D3 |
| `app/view/*` `byPassWM` / `byPassX11WM` / `Qt::BypassWindowManagerHint` (~36 refs) | User-persisted layout setting with a config-UI surface | LIVE and user-facing | D - proposal D4 (already an OPEN plan item) |
| `plasmoid/plugin/backend.h:26` | `#include <netwm.h>` in the vendored task-manager backend | No NET* symbol used, but the file tracks upstream plasma-desktop | D - proposal D5 (verify vs upstream) |
| `app/view/view.cpp:1400` `showHiddenViewFromActivityStopping` + `m_visibleHackTimer1/2` + `View::forcedShown` | KWin activity-stop reshow hack | Possibly wayland-live (comment claims the compositor clears frame extents on activity close) | D - deferred; existing OPEN plan item, needs an activity-close repro before any removal |
| `app/wm/windowid.h` `WindowId` type + `fromWaylandUuid` | The neutral wrapper and its wayland factory | LIVE - the whole window-tracking path | B - keep |
| `app/main.cpp:131,140-141,653-655` | xcb/X11 mentions in the refusal-path diagnostics | LIVE by design (the refusal message names what is unsupported) | B/C - keep |
| Positive `isPlatformWayland()` guards (subconfigview, subwindow, waylandlayershell, theme, ...) | Guards protecting wayland protocol calls | LIVE - `isPlatformWayland()` is false on the offscreen harness by design | B - keep |
| Explanatory `// on X11 ...` / `// Qt5/X11 got this by ...` comments across `app/`, `containment/`, `declarativeimports/`, `shell/`, `tests/` | History notes on why a wayland path looks the way it does | n/a - comments | C - keep (explain a live divergence) |

Counts: A = 3 removals (one file deleted, one ternary collapsed, two include
blocks stripped); C-executed = 2 rewords (theme.cpp label, ToolTipInstance
stale comments); D-proposed = 5 (D1-D5) plus 1 substrate proposal (S1) plus 1
deferred existing plan item (the activity-stop hack); B/C-keep = the wayland
guards, the refusal-path diagnostics, and the explanatory comment class.

## The WHY-BOTH check (result: no redundancy)

The question: is a raw uuid `QByteArray` ever stored or passed alongside a
`WindowId` for the same window, which would be a consolidation target?

Result: no. Every `uuid()` call in `app/` lives in `waylandinterface.cpp` and
immediately wraps into `WindowId::fromWaylandUuid(...)`; only the `WindowId`
flows onward. No `QByteArray` member holds a window id in parallel with a
`WindowId` (the only `QByteArray` window-id-shaped members are the newtype's own
storage and unrelated temp-file paths). The single `.bytes()` escape-hatch
consumer outside tests is `lastactivewindow.cpp:405`, which ships the bytes into
the QML `winId` QVariant property - a boundary export, not parallel storage. The
model is exactly "one neutral wrapper, wayland payload wrapped at the boundary,
nothing duplicated."

## Executed removals (bucket A / C)

- `4c84c1bad` - `refactor(previews): drop the dead X11 window-thumbnail preview
  path`. Deletes `PlasmaCoreThumbnail.qml`, collapses the `ToolTipInstance.qml`
  thumbnail ternary to `"PipeWireThumbnail.qml"`, rewords the two stale in-file
  comments that named the removed X11 thumbnail item. Dead-caller evidence: a
  tree-wide grep for `PlasmaCoreThumbnail`/`WindowThumbnail` found only the
  ternary and the file's own declaration; no CMake/qrc/metadata lists it.
- `67bd25638` - `chore(view): drop the dead X11 window-flags includes from the
  SubWindow helpers`. Removes the `NETWM` (X11-only) and orphaned
  `KWindowSystem` includes from `floatinggapwindow.cpp` and
  `screenedgeghostwindow.cpp`. Dead evidence: a case-insensitive grep for
  net/kwindowsystem/kx11/xcb/isPlatform across each `.cpp` and `.h` matches only
  the include lines.
- `2f78a7d7e` - `chore(theme): relabel the stale X11 header over the
  KWindowSystem include`. The include is live (`isPlatformWayland()`); only the
  misleading `// X11` label is corrected to `// KDE`.

## Recommended plan and sequence (plain English, 2026-07-19)

The port is Wayland-only now (it refuses to start on old X11). Five X11-era
leftovers remain. Each is written plainly with its tracking codeword in brackets
so the plan is both readable and traceable. (The `byPassWM` setting [D4] is NOT
in this list: it is a live user-facing setting tracked as its own open plan item,
not dead-code cleanup.)

The five leftovers:

1. **Dead color-scheme fallback branch [D3].** One spot picks a window's colors
   and has an "if not on Wayland, parse the id the old X11 way" branch that can
   never run. Removing it is safe, and it deletes the last real (non-test) use of
   the old X11 id-parsing.
2. **The About dialog's broken "keep on top" [D2].** The Help -> About dialog
   should float above other windows; it asks using an old X11 window id that
   Wayland can never match, so the request silently does nothing and the dialog
   can end up buried. A real (minor) bug. Do not leave it a SILENT nothing: mark
   it a clear stub now (findable, deferred to the window-surface work), and wire
   it up properly later.
3. **Trim the window-identity wrapper [D1].** A small type represents "which
   window this is" and can build that identity two ways: the Wayland way (live)
   and the old X11 way (dead). Once #1 and #2 are gone, nothing uses the X11 way,
   so delete those few methods - but KEEP the wrapper itself. Its value is that
   it cannot be built wrong (you cannot accidentally hand it the wrong kind of
   string); that safety is worth keeping with only the Wayland way left.
4. **(Optional, maybe never) Store the id more strictly [S1].** The wrapper
   stores the identity as raw bytes; Qt has a dedicated unique-id type
   (`QUuid`) it could use instead - more self-documenting, harder to misuse. It
   fixes nothing and is the riskiest of the five: it changes what the UI layer
   sees across the C++/UI bridge, changes how the id sorts as a lookup key, needs
   its tests rewritten, and only works if the Wayland id is genuinely a proper
   UUID (unconfirmed). Lowest value: do it last or skip it, and only after
   confirming the id's shape.
5. **Leftover unused X11 include [D5].** A file kept in sync with upstream KDE
   still includes an old X11 header nothing uses. Trivial to drop, but do it
   while syncing that file against upstream so it does not create diff noise.

Order of work:

- **Soon, one small PR:** remove the dead color-scheme branch [D3] and mark the
  About-dialog keep-on-top a stub [D2]. Low risk; turns a silent bug into a
  tracked one.
- **Right after, a tiny follow-up:** trim the window-identity wrapper to
  Wayland-only [D1], now that its callers are gone.
- **Optional and last, only if it earns it:** the stricter-id-type switch [S1],
  after confirming the Wayland id is really a UUID.
- **At the next upstream sync:** drop the leftover include [D5].

The detailed per-item analysis (exact files, blast radius, the two options for
[D1]) is in the Proposals section below.

## Proposals (await sign-off)

None of the following are applied in this pass. Each names its blast radius.

### D1 - Strip or collapse the WindowId X11 parse surface

`fromX11WId`, `toX11WId` and `parseX11WindowId` are NOT dead: `fromX11WId` has
two live callers in `lattecorona.cpp` (D2, D3) and the parse boundary is pinned
by `tests/units/windowidtest.cpp`. So this cannot be a blind removal; it is
gated on D2 and D3 first.

Once D2 and D3 are resolved, two options:

- (i) Keep `WindowId` as a uuid-only boundary type: drop `fromX11WId`,
  `toX11WId`, `parseX11WindowId`, keep the named-factory + no-accidental-convert
  discipline that earns the newtype. Recommended - the boundary discipline is
  worth keeping on its own merits even with one factory.
- (ii) Collapse the newtype back to a bare alias if it no longer earns itself.
  Not recommended: the named-factory discipline and container-key semantics
  still have value.

Blast radius: `windowid.h`; `tests/units/windowidtest.cpp` (its X11-parse cases
shrink to the wayland shape); `tests/windowinfowraptest.cpp` (uses `fromX11WId`
to build test ids - swap to `fromWaylandUuid`); and the two `lattecorona.cpp`
callers, which must be resolved by D2/D3 first.

### D2 - aboutApplication keep-above is a silent X11-shaped no-op (defect)

`lattecorona.cpp:882` runs, unconditionally:

```
m_wm->setKeepAbove(WindowSystem::WindowId::fromX11WId(aboutDialog->winId()), true);
```

`aboutDialog->winId()` is a Qt `WId`; `fromX11WId` wraps its decimal string into
a `WindowId`. On wayland `WaylandInterface::setKeepAbove` does
`windowFor(wid)`, which resolves a `WindowId` against `PlasmaWindow::uuid()`
values. A Qt `WId` decimal string can never equal a PlasmaWindow uuid, so
`windowFor()` returns null and the keep-above silently does nothing. This is the
class of silent-wrong-behaviour the failures-and-root-cause discipline warns
against: the about dialog does not actually get keep-above on wayland. It pairs
with the `skipTaskBar` STUB one line above (`waylandinterface.cpp:297`, already
a no-op with a Phase-4 surface-management note).

Proposal: do NOT delete the call (that drops the legitimate intent silently).
Either mark it a `// STUB` like `skipTaskBar`, deferring the real keep-above to
the PlasmaShellSurface/layer-shell surface-management work, or wire the about
dialog's keep-above through the wayland surface directly. Recorded as a defect
in `docs/known-defects.md`.

Blast radius: `lattecorona.cpp` `aboutApplication`; the wayland surface-request
path if wired for real. Small.

### D3 - windowColorScheme else arm is dead in production

`lattecorona.cpp:1099-1111`: the `else` of `if (isPlatformWayland())` parses a
decimal window-id string and calls `setColorSchemeForWindow(fromX11WId(...))`.
Production refuses non-wayland platforms, and the offscreen harness (the one
platform that reports `!isPlatformWayland`) has no real windows to color, so the
arm does nothing meaningful anywhere.

Proposal: collapse `windowColorScheme` to the wayland branch (drop the else and
its decimal parse). Note the behaviour change: the D-Bus method would no longer
attempt an X11-style decimal lookup on the offscreen platform (it never
succeeded there). Removing this arm also retires the last non-test caller of
`fromX11WId` that goes through the decimal parse, which is a precondition for
D1.

Blast radius: `lattecorona.cpp` `windowColorScheme`; the `windowColorScheme`
D-Bus contract (behaviour only changes on non-wayland, which production cannot
reach).

### D4 - byPassWM setting (already an OPEN plan item)

`byPassWM` / the `byPassX11WM` constructor parameter chain / `Qt::BypassWindow-
ManagerHint` is X11-only window-manager machinery, but `byPassWM` is a
Qt5-faithful, user-persisted layout setting with a config-UI surface (the "Can
be above fullscreen windows" checkbox in `BehaviorConfig.qml`, whose tooltip
literally names the `BypassWindowManagerHint` flag) and roughly 36 code
references. Retiring it is a config migration and a settings-page change - a
product decision, not an audit sweep, per the Qt5-faithful rule.

This is already the "DECISION OWED (mine): the byPassWM layout setting" item in
the Phase 4 checklist. No action here; recorded for completeness so the audit
inventory is whole.

Blast radius (for whenever the decision lands): `view.{h,cpp}` property chain,
`genericlayout.cpp` read, `visibilitymanager.cpp`, `primaryconfigview.cpp`,
`BehaviorConfig.qml`, `BindingsExternal.qml`, `containment/.../main.qml`, plus
a config migration for the persisted `byPassWM` entry.

### D5 - dead netwm.h include in the vendored backend

`plasmoid/plugin/backend.h:26` carries `#include <netwm.h>`, but no NET* symbol
is used in `backend.cpp` or `backend.h`. Unlike the two SubWindow helpers, this
file is VENDORED from KDE plasma-desktop's `applets/taskmanager/backend.*` and
is tracked against upstream (see the reference-fork sync section of CLAUDE.md).

Proposal: before removing, diff the current upstream `backend.h` at the tracked
ref. If upstream has already dropped `netwm.h` (plausible, KDE is going
wayland-only too), remove it here to stay aligned. If upstream still carries it,
either leave it (to keep the vendored diff clean) or remove it with a recorded
deviation. Not executed this pass because it touches the upstream-tracking
contract, which raises the blast radius above a plain dead-include removal.

Blast radius: one include line in a vendored file; the cost is future
fork-sync diff noise if upstream diverges.

### S1 - WindowId substrate: QByteArray-of-uuid vs QUuid (separate proposal)

Independent of the removals above. `WindowId`'s storage is a `QByteArray`.
A `QUuid` substrate would be more type-safe for the wayland payload, but the
switch is not free and must not be bundled with any X11 removal:

- QML ArrayBuffer boundary: `lastactivewindow.cpp:405` ships `m_currentWinId.
  bytes()` into the QML `winId` QVariant property (Qt6 exposes a `QByteArray`
  to QML as an ArrayBuffer, the `8e8cdf31` decision). QML consumers read this
  (`thumbnailSourceItem.winId`, `ToolTipWindowMouseArea.winId`,
  `ScreencastingRequest.uuid`). A `QUuid` substrate would need a defined
  conversion at this boundary and the QML side re-checked.
- Container keys: `QMap`/`QHash`/`QList` keyed on `WindowId` rely on byte-wise
  equality, ordering and hashing. `QUuid` has its own ordering and `qHash`; the
  newtype already encapsulates the substrate, so the keys move as a unit, but
  the semantics change (uuid ordering, not lexical byte ordering) and the
  container-key test in `windowidtest.cpp` would need updating.
- Coupling to D1: `QUuid` cannot hold the X11 decimal-string payload
  (`"77594625"` is not a uuid), so a `QUuid` substrate REQUIRES the X11 payload
  to be gone first (D1). This is why S1 is downstream of D1, not bundled with
  it.
- Persistence: none found - a scan of the `.bytes()` consumer and the config
  writers shows `WindowId` is runtime-only (the ignored/whitelisted sets and
  the scheme map are not persisted), so there is no on-disk migration.

Blast radius: `windowid.h`; the QML `winId` boundary in `lastactivewindow.*`;
every `WindowId`-keyed container; `windowidtest.cpp`. Do not start until D1
lands.
