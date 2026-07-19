# A3 - the driven sanitized-dock gate (2026-07-18)

Prong A3 of the UB-catching initiative (docs/tracking/ub-catching-plan.md): wire the
sanitized dock (build-asan, -DLATTE_SANITIZE=ON, from A1/A2) into a DRIVEN gate
so OUR-code UB and tripped invariants in the LIVE integration paths abort the run
with a symbolized stack and a non-zero exit instead of running silently. A1/A2
built and could launch the sanitized dock; A3 makes it a gate CI runs.

Branch: `ub-a3-sanitized-gate` off master. Handoff to the orchestrator for the
independent review + `gh pr merge --rebase`; this agent does not merge.

## What landed

- `scripts/asan-e2e-gate.sh` - the self-contained driven gate. Builds build-asan
  (`-DLATTE_SANITIZE=ON -DBUILD_TESTING=OFF`), seeds a hermetic default-layout
  config, and drives the sanitized dock through a nested e2e recipe core with
  ASan/UBSan set to abort on the first finding. THE EXIT CODE IS THE VERDICT.
  Wired into `scripts/gate-all.sh` as its final stage, so a merge that introduces
  integration-path UB fails the merge gate.
- `tests/e2e/070-asan-binary-shadow.sh` - the shadow assertion (a hard A3
  deliverable, not optional). Reads the vehicle dock's own `/proc/<pid>/maps` and
  asserts the executable AND the containment plugin actually mapped into the
  running process resolve under `$E2E_BUILD`, never a packaged /nix/store copy
  (the PR #23 NIXPKGS_QT6_QML_IMPORT_PATH shadow class). Under `E2E_EXPECT_ASAN=1`
  (which `scripts/run-asan-dock.sh` now exports) it additionally asserts libasan
  is mapped - proof the run is really the sanitized build, not an uninstrumented
  shadow that would catch no UB and pass green. The universal (plugin-under-build)
  half is a standing guard in any nested run, sanitized or not.
- `scripts/lib-e2e-seed.sh` - the default-layout config seeder, EXTRACTED from
  `ci/build-and-gate.sh`'s inline `seed_config` into a shared lib (single source
  of truth) and consumed by both the container release gate and this NixOS
  sanitized gate. Carries the portable-glob fix below.
- `scripts/run-asan-dock.sh` - exports `E2E_EXPECT_ASAN=1` so the shadow recipe
  demands a genuinely sanitized binary.
- `app/layout/genericlayout.cpp`, `app/layouts/syncedlaunchers.cpp` - two REAL
  vptr UB fixes the gate surfaced on its first run (below). Landed first so the
  gate is green.

## Real UB the gate caught on its first run (the payoff)

Bringing the sanitized dock up + clean SIGTERM in the vehicle aborted TWICE
under UBSan `-fsanitize=vptr`, both the same root cause - a `static_cast`
downcast inside a `destroyed(QObject*)` slot, where every derived destructor has
already run and the object's dynamic type has decayed to `QObject`, so the
downcast reads the (now-QObject) vptr to validate itself = UB:

```
app/layout/genericlayout.cpp:790:40: runtime error: downcast of address ...
    which does not point to an object of type 'Containment'
    note: object is of type 'QObject' / vptr for 'QObject'
```
```
app/layouts/syncedlaunchers.cpp:65:25: runtime error: downcast of address ...
    which does not point to an object of type 'QQuickItem'
    note: object is of type 'QObject'
```

Both pointers are used ONLY for identity (`m_containments.indexOf`, the
Plasma::Containment*-keyed `m_latteViews`/`m_waitingLatteViews` `take`, and
`m_clients.removeAll`), never dereferenced through the vtable, so the fix is
`reinterpret_cast` (pure pointer identity, no vtable access - vptr-clean and
correct) with a comment at each site. syncedlaunchers already carried a comment
explaining the mid-destruction identity use; genericlayout is the same
destroyed()-handler demotion family as its own location()-reads-freed-memory
note. These are filed under B2 (Prong A findings) in the plan. NOT suppressed:
they are OUR code, and the initiative exists to end exactly this silent class.

The redundant `static_cast<Plasma::Containment*>(sender)` at genericlayout.cpp
838/841 are no-ops (`sender` is already a live, qobject_cast-checked
`Plasma::Containment*`) and read no decayed vptr, so they are left alone (one
root cause per commit).

## Shadow assertion evidence (070, run under the gate)

```
run-e2e: ---- 070-asan-binary-shadow.sh
  exe: .../build-asan/bin/latte-dock
  containment plugin: .../build-asan/_qmlstage/lib/qml/org/kde/latte/private/containment/liblattecontainmentplugin.so
  sanitized: libasan mapped (/nix/store/...-gcc-15.2.0-lib/lib/libasan.so.8.0.0)
run-e2e: PASS 070-asan-binary-shadow.sh
```

The dock and the containment plugin both resolve under build-asan, and libasan
is mapped - the run is the sanitized binary, not a shadow. (`nm -D` on the plugin
shows 29 `__asan` + 8 `__ubsan` undefined refs: GCC's shared-ASan model, the
plugin resolves the runtime from the loaded executable.)

## Injected-UB proof (the gate catches UB, exit code is the verdict)

A temporary heap OOB read behind `#ifdef LATTE_UB_SELFTEST` was injected in the
containment plugin's `LayoutManager` constructor - a path the containment QML
runs at VIEW CREATION, which 000-smoke drives - with a `#define LATTE_UB_SELFTEST`
to activate it, build-asan rebuilt (plugin only), and the gate re-run:

```
containment/plugin/layoutmanager.cpp:55:78: runtime error: load of address ...
    with insufficient space for an object of type 'volatile int'
    #0 Latte::Containment::LayoutManager::LayoutManager(QObject*) containment/plugin/layoutmanager.cpp:55
    #1 QQmlPrivate::QQmlElement<Latte::Containment::LayoutManager>::QQmlElement()
    #2 QQmlPrivate::createInto<Latte::Containment::LayoutManager>(void*, void*)
    #3 QQmlType::create(void**, unsigned long) const
```

The sanitized dock ABORTED at view creation (frame #0 is OUR code, instantiated
by QML - a genuine integration path), never settled, so run-e2e failed and the
gate exited NON-ZERO (2). Both the `#ifdef` block and the `#define` were then
REMOVED (git shows no diff to layoutmanager.cpp), build-asan rebuilt clean, and
the gate re-run GREEN (exit 0, 3/3). No trace committed.

## The seeding rabbit hole and its real root cause (compgen)

The seed brings a dock up against an empty config once so it self-inits the "My
Layout" default (app/layouts/manager.cpp:86 `newLayout`, a synchronous
QFile::copy), which run-e2e then copies its throwaway from. Seeding looked
completely broken - the dock ran, `viewsData` settled, but the seeder always
reported "no layout" and failed, which sent this down a long false trail about
KConfig flushing the layout only at shutdown.

The layout file was on disk at +1s the whole time. The real cause: the nix
devShell's bash (5.3.9) is built WITHOUT programmable completion, so `compgen`
exits 127 ("command not found"); the seeder's `compgen -G ".../*.layout.latte"
>/dev/null 2>&1` swallowed that 127 and every check read as "no layout". Every
"flush only at shutdown / timing is variable" observation along the way was this
same lie - the file is written synchronously at first run (manager.cpp:86
QFile::copy) for the normal AND the sanitized dock; only a `find`-based probe
(not compgen) finally showed it present at +1s. Fixed with a portable, nullglob-
/nounset-safe loop-glob check (`_e2e_seed_has_layout`) in the shared lib - a
strict improvement for the container path too (it no longer depends on the distro
bash carrying compgen). The container gate had compgen, so its seed worked
before; this is why the failure was NixOS-only.

The gate still seeds with the normal build/ dock, not the sanitized one: the
seed is plain config DATA that gains nothing from ASan and only pays its startup
overhead (build/ seeds in ~2s). build-asan is reserved for the driven runs.

## Recipe core (why these three)

`000-smoke` (view creation, layout restore -> layoutmanager splitter placement,
mask geometry, clean SIGTERM), `060-geometry-agreement` (state-vs-render), and
`070-asan-binary-shadow`. Deliberately the NO-INPUT set: they exercise the
containment-plugin integration paths where the motivating insert(-1) UB lived,
without the fakepointer input-timing flake the wheel/edit recipes carry under
ASan slowdown. A deeper sweep with the full recipe set is a manual
`scripts/run-asan-dock.sh <recipes>` run, not the always-on merge gate.

## Gate cost / blast radius

- `scripts/asan-e2e-gate.sh` builds a SECOND out-of-tree sanitized tree
  (build-asan, gitignored via `build*/`). First run is a full sanitized build
  (~minutes); steady-state is incremental. In gate-all it runs last and reuses
  the build/ tree build-check already built for its 2s seed.
- OFF path unchanged: gate-all's other stages are untouched; the new stage is
  additive and its own exit code.

## Isolation / hard-constraint compliance

- Every dock run was in the nested vehicle. The only latte-dock on the real
  session throughout was Bree's desk dock (main-worktree `build/bin/latte-dock`);
  it was never rebuilt or killed. Nested docks were torn down by their own
  vehicle cleanup; no tree-wide pkill.
- This agent built ONLY in its own worktree (build-asan and build under
  `.claude/worktrees/agent-*/`), never the main worktree's build/.
