# Justify splitter position UB (Prong B1 of the UB-catching initiative)

Branch: `panel-fix-justify-splitter-ub` (off master `fec9a6515`).
Scope: the first BOUNDARY-INVARIANT case - make lib-boundary UB structurally
impossible at the justify-splitter insert sites, with a sanitized guard.

## Root cause (confirmed)

Justify alignment carves the panel into three zones with two splitter markers
placed 1-based: a splitter at position `p` is inserted at 0-based index `p-1`.
`containment/plugin/layoutmanager.cpp` fed the stored positions straight to
`QList::insert` in two places:

- `updateOrder()` (was 294-296): `nextorder.insert(m_splitterPosition-1, ...)`
- `restore()` (was 381-389): `appletIdsOrder.insert(splitterPosition-1, -1)`
  under a `splitterPosition!=-1 && splitterPosition2!=-1` gate.

Bree's top dock carried `splitterPosition=0` (with `alignmentUpgraded=true` -
migrated from an older Latte alignment scheme). `0` passes the `!=-1` gate, so
`insert(0-1, ...) = insert(-1, ...)`: a NEGATIVE QList index. RelWithDebInfo
strips `Q_ASSERT` (NDEBUG), so the dock did not crash - it executed the UB
silently and inconsistently (Bree saw both splitters adjacent one load, the
trailing one dropped and applets collapsed to the center the next).

Nothing ACTIVELY produces 0 now: the schema default is `-1`
(`containment/package/contents/config/main.xml:157-159`) and the compute path
`save()` only ever emits `>= 1` (`pos1 = startChilds+1`,
`pos2 = startChilds+mainChilds+2`, all counts `>= 0`). So `0` is a stale value,
not a live producer. The QML upgrader `upgrader_v010_alignment` (main.qml:709)
only sets `alignment` from `panelPosition`, never a splitter position, so it is
not the producer either. Conclusion: NO active producer; the repair heals the
stale value.

### Live reproduction (nested vehicle)

Injected `alignment=10` (Justify) + `splitterPosition=0` + `splitterPosition2=2`
into a throwaway copy of the staged config's top containment and booted the
nested vehicle. The dock log showed the restore walk producing
`QList(0, -1, 3, 4, 6, 29, 122)` for `appletOrder=3;4;6;29;122` with
`splitterPosition=0` - the exact negative-insert garbage (a bare `0` from the
out-of-bounds write, a single `-1` at index 1, no trailing `-1`). This is the
UB, live, matching Bree's symptom.

## The fix (root + boundary invariant)

New single-owner pure core `containment/plugin/units/justifysplitters.h`:

- `positionsAreValid(first, second, appletCount)`: `first >= 1 &&
  second >= first+1 && second <= appletCount+2` (the exact range the compute
  path emits; derives `first <= appletCount+1`).
- `centeredPositions(appletCount) = {1, appletCount+2}` - the natural centered
  layout (every applet in the main zone, one splitter before all, one after).
- `repairPositions(...)` - pure; returns the in-range pair, repairing an
  out-of-range pair to centered, with a `wasRepaired` flag.
- `insertSplitters(order, first, second, splitterId)` - THE ONLY path that
  reaches `QList::insert` with a splitter index. It repairs first, then
  `Q_ASSERT(positionsAreValid(...))` on the post-repair positions (the
  test-build tripwire, live under QT_FORCE_ASSERTS), then inserts. So a
  negative/overrun index can never get there.

`layoutmanager.cpp` both call sites now route through `insertSplitters` and emit
a loud `qWarning` when they hand it an invalid pair (the release-dock
"refuse-to-UB, repair-and-say-so"). The centered repair is byte-identical to the
old `restore()` `-1` else-branch (`insert(0,-1)` then append), so valid configs
are unchanged (Qt5-faithful) and only corrupt/stale pairs change behavior -
always toward the safe centered layout.

Heal-in-place: `restore()` gained a trailing `saveOptions()` after
`initSaveConnections()`. `save()` already recomputes valid positions from the
real placement, but it runs before the save-connections exist, so its
`splitterPositionChanged` never reached `saveOptions`; the explicit call
persists the corrected value (writes only keys that differ, so a healthy config
produces no writes). CAVEAT: whether that write reaches the on-disk
`.layout.latte` depends on the containment config-write path the class header
(layoutmanager.h:37-41) already flags as finicky; the REPAIR runs on every load
regardless, so the user-facing bug is fixed even if a given config never heals
to disk.

`addJustifySplittersInMainLayout()` was already safe (it maps
`splitterPosition < 1` to index `-1` = "first position", no insert), so it was
left unchanged.

## Guard (the template)

`tests/units/justifysplitterstest.cpp`, registered via `latte_add_unit_test`
(ASan+UBSan + QT_FORCE_ASSERTS, `-fno-sanitize-recover`), baseline bumped 83->84.
It pins: validity classification across boundaries, centered repair, valid
pass-through, exact placement for valid pairs, the exact zone splits `save()`
emits (start/main/end round trip), and Bree's live case (0, 2). The tripwire
property was VERIFIED empirically: with `insertSplitters`'s repair reverted to
the raw `insert(first-1)`, the test ABORTS with SIGABRT (exit 134) under the
sanitizers; with the repair in place it PASSES. Same abort confirmed on a
standalone `QList<int>::insert(-1, ...)` probe under QT_FORCE_ASSERTS.

## Fork / Qt5 comparison

The 1-based scheme is faithful and identical across upstream Qt5 Latte and both
reference forks - and none of them validate it:
- latte-dock-ng: `layoutmanager.cpp:441-442` (updateOrder), `1051-1052` (restore)
- latte-dock-qt6: `layoutmanager.cpp:275-276`, `363-364`
- our pre-fix tree: `294-296`, `383-384`
All carry the same unchecked `insert(splitterPosition-1, ...)` and the same
`!=-1` gate that lets `0` through into the negative insert. The fix ADDS
validation/repair at the boundary while keeping the scheme and valid-config
behavior; a justified divergence under the maintained-continuation framing.

## Nested-vehicle verification + a finding

- CONFIRMED live: the UB reproduces (see Live reproduction above); the dock
  loads the bad config, keeps all 5 applets with sane geometry, and does not
  crash - exactly the "silent misbehavior" signature.
- BLOCKED (fresh-fix load): the nested vehicle could not exercise the FRESH
  containment plugin. `/proc/<nested-dock>/maps` showed
  `liblattecontainmentplugin.so` (and the core/tasks plugins) loading from
  `/nix/store/...latte-dock-0.10.77/...`, the SYSTEM-INSTALLED packaged
  latte-dock, NOT the worktree stage.
  `/run/current-system/sw/share/plasma/plasmoids/org.kde.latte.containment` is a
  symlink into that store path. `run-staged.sh` puts the staged `share` tree
  FIRST in `XDG_DATA_DIRS`, so this is a KPackage/ksycoca resolution shadow (the
  vehicle uses a fresh private cache and the plasmoid lookup resolves to the
  system-registered package), not an XDG-ordering bug and not a fix defect. This
  is a nested-vehicle LIMITATION for containment-plugin changes worth a separate
  look (out of scope for B1; the HARD CONSTRAINTS keep this agent out of the
  build/harness). The fix is proven by the sanitized guard instead, and
  gate-all's `layoutmanagertest`/`layoutmanagerparkingtest` compile and run the
  modified `layoutmanager.cpp` in a real Qt context.

## Desk-check OWED to Bree (exact steps)

The definitive live-fix confirmation (the nested vehicle can't load the fresh
containment plugin per the shadow above), to run on the real desk after this
lands and the local build is restaged:

1. Rebuild + restage + start the dock on the real session
   (`scripts/start-dock.sh`) so the FRESH containment plugin loads (confirm via
   `/proc/$(pgrep -x latte-dock)/maps | grep liblattecontainmentplugin.so`
   pointing at the build tree, not `/nix/store/...latte-dock-0.10.77`).
2. Make a top panel, set alignment to Justify (Center-Right-Left justify).
3. Confirm TWO splitters render and applets distribute to both edges (drag a
   couple of applets to the left and right edges); the center zone holds the
   rest.
4. Close edit mode - the layout stays justified, both splitters present, applets
   stay where they were put.
5. Restart the dock and confirm the justify layout survives (splitters back in
   the same zones, no collapse-to-center, no missing trailing splitter).
6. For the exact migrated-config repro: if any real containment still carries
   `splitterPosition=0` with `alignment=10`, confirm it now loads justified with
   both splitters instead of the collapsed/one-missing symptom.
