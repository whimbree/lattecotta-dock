# Catching UB everywhere (plan)

Written 2026-07-18, after a real bug (justify panel: the second splitter
vanished) turned out to be UNDEFINED BEHAVIOR - `QList::insert(-1, ...)` in
`containment/plugin/layoutmanager.cpp` from a stale `splitterPosition=0` - that
ran silently in the field. Bree's direction: UB is inexcusable and must be
TREATED AS A BUG - catchable and interrogable everywhere.

CHECKLIST, same discipline as the other plan docs.

## Why the UB slipped through (the gap)

- Sanitizers + forced asserts are wired ONLY onto unit-test targets:
  `tests/units/CMakeLists.txt:14` applies `-fsanitize=address,undefined
  -fno-sanitize-recover=all` via `latte_add_unit_test`, and `:32` sets
  `QT_FORCE_ASSERTS` on the TEST binaries only.
- The DOCK binary (app/, containment/, declarativeimports/) gets NEITHER. It
  is RelWithDebInfo -> NDEBUG -> every Q_ASSERT stripped, no -fsanitize. So
  `insert(-1)` executed silently.
- Second gap: NO unit test exercises the justify-splitter placement path, so
  even the one place UB would be caught (a sanitized test) never ran it.

## The uncomfortable nuance (so we build the right thing)

Even a fully sanitized DOCK would likely NOT catch this exact UB: the bad
index is handed to Qt's `QList::insert`, and the out-of-bounds happens INSIDE
uninstrumented system Qt (release, its own Q_ASSERT stripped). ASan/UBSan
instrument OUR translation units, not Qt. So "we passed a library a bad value"
is a class whose reliable catch is OUR boundary check BEFORE the call, not a
sanitizer AFTER it. UB-catching therefore needs TWO prongs.

## Prong A - a sanitized dock build, runnable and gated

- [x] A1 CMake option `-DLATTE_SANITIZE=ON` (default OFF) that compiles the
      dock and its libs (app/, containment/plugin/, declarativeimports/) with
      `-fsanitize=address,undefined -fno-sanitize-recover=all` + link, and
      `QT_FORCE_ASSERTS`, so OUR Q_ASSERTs go live and OUR-code UB aborts with
      a stack. Factor the flag set shared with `latte_add_unit_test` so there
      is one source of truth. The normal build MUST stay unchanged when OFF
      (blast radius: verify gate-all green with the option absent).
      Commits: (ub-sanitize-build branch; final hashes at merge)
      Top-level CMakeLists defines LATTE_SANITIZE_FLAGS once (shared with
      tests/units/CMakeLists.txt) and, when LATTE_SANITIZE=ON, adds them as
      compile+link options plus QT_FORCE_ASSERTS at directory scope. OFF is
      byte-unchanged (block skipped); proven by gate-all green with OFF. See
      docs/agent-logs/2026-07-18-ub-sanitize-build.md.
- [x] A2 A build-asan tree + a launch mode: a script (or `LATTE_RUN_WRAPPER`
      analogue) to run the sanitized dock in the NESTED VEHICLE only (never
      the real session), with the ASan/UBSan options set to abort-and-log.
      Prove it catches a known UB (inject a temporary one, confirm the abort +
      stack, remove it).
      Commits: (ub-sanitize-build branch; final hashes at merge)
      build-asan tree (-DLATTE_SANITIZE=ON -DBUILD_TESTING=OFF); nested launcher
      scripts/run-asan-dock.sh (BUILD=build-asan -> run-e2e vehicle) and a LIVE
      launcher for Bree's manual desk testing scripts/start-dock-sanitized.sh
      (build-asan against --user-config, detect_leaks=0, log_path, suppressions
      scripts/asan/{asan,ubsan}.supp). Proven: sanitized dock runs clean in the
      vehicle (0 sanitizer output at startup) and ABORTS with a symbolized stack
      on an injected heap OOB (app/main.cpp:159, since removed). Ledger:
      docs/agent-logs/2026-07-18-ub-sanitize-build.md.
- [x] A3 Wire the sanitized dock into a driven gate: run the e2e /
      sceneprobe nested scenarios against the build-asan dock so UB in
      integration paths fails CI (halt_on_error, first-finding abort).
      Sequenced after A1/A2. Commits: (ub-a3-sanitized-gate branch; final
      hashes at merge)
      NOTE (2026-07-18): the containment-plugin-shadow-in-vehicle risk this
      item flagged is fixed at the harness layer by PR #23 (master 326aba06d,
      scripts/lib-qml-env.sh): NIXPKGS_QT6_QML_IMPORT_PATH / NIXPKGS_QML_SEARCH_PATHS
      leaked the SYSTEM-INSTALLED packaged latte-dock, so the STAGED dock (and
      any nested run sourcing lib-qml-env.sh) loaded the packaged containment
      plugin, not the build-under-test - A3's sanitized dock would have run the
      packaged binary and caught no UB. The fix strips only the packaged
      latte-dock leaf. A3 still confirms the build-asan dock's plugin resolves to
      build-asan (asserts /proc/<dock>/maps in the vehicle, same check that
      caught this live - see tests/e2e/070-asan-binary-shadow.sh below).
      Non-blocking nit filed from PR #23 review: lib-qml-env.sh:40 grep -vE
      returns exit 1 if a var is ALL latte-dock entries (unreachable in the
      documented env - the vars always carry KDE frameworks); when this file is
      next touched, wrap the grep `|| true` so the all-filtered case is tolerated
      (grep-exit-1 there is a legit "everything filtered" signal, not a masked
      failure).
      DONE: scripts/asan-e2e-gate.sh builds build-asan, seeds a hermetic
      default-layout config (shared scripts/lib-e2e-seed.sh, extracted from
      ci/build-and-gate.sh), and drives the sanitized dock through the no-input
      e2e core (000-smoke, 060-geometry-agreement, plus the new shadow assertion
      tests/e2e/070-asan-binary-shadow.sh) with ASan/UBSan abort-on-first;
      wired as gate-all.sh's final stage (exit code = verdict). The shadow
      assertion reads /proc/<dock>/maps and asserts the executable AND the
      containment plugin resolve under build-asan and libasan is mapped
      (E2E_EXPECT_ASAN=1 from run-asan-dock.sh) - so a shadowed uninstrumented
      binary (PR #23 class) fails instead of silently catching no UB. Proven:
      GREEN exit 0 at 3/3 clean; RED exit 2 with a symbolized stack at
      layoutmanager.cpp on an injected LayoutManager-ctor OOB (view-creation
      path, removed after). The gate caught two REAL vptr UBs on its first run
      (see B2). Note: the "sceneprobe against sanitized" leg is deferred - the
      probe binary lives under BUILD_TESTING (absent in build-asan) and GCC's
      shared ASan will not let an uninstrumented probe dlopen sanitized plugins,
      so it needs its own -DLATTE_SANITIZE -DBUILD_TESTING tree; the unit +
      sceneprobe suites are already sanitized in the normal gate via
      latte_add_unit_test / the LATTE_SANITIZE directory scope, and the DOCK
      e2e path is where the motivating integration-path UB actually lives.
      Ledger: docs/agent-logs/2026-07-18-a3-sanitized-gate.md.

## Prong B - boundary invariants make lib-boundary UB structurally impossible

The codebase's own law (CLAUDE.md "assert invariants... make invalid states
unrepresentable... qCritical-and-refuse at API boundaries"), currently
under-enforced. At every index / enum / pointer boundary: `Q_ASSERT` the
precondition (live in the sanitized/test build) AND a runtime `qCritical`
-and-refuse/repair for the release dock (where Q_ASSERT is stripped). An
out-of-range value must never REACH `insert()` - caught at test time by forced
asserts, refused/repaired at runtime in the field.

- [x] B1 JUSTIFY SPLITTER FIX (the first boundary-invariant case, and a live
      user bug). Root cause: `splitterPosition=0` (stale, migrated;
      `alignmentUpgraded=true`) -> `layoutmanager.cpp:294-296`
      `insert(splitterPosition-1)` = `insert(-1)` = negative index = UB in the
      running dock (asserts stripped), inconsistent (looked like 2 adjacent,
      then 1 missing). Fix: validate/repair splitter positions before use -
      a position `< 1` or `>` applet count is invalid; recompute a sane value
      from the actual layout and re-save; NEVER insert a negative index. Add
      Q_ASSERT preconditions at the insert sites + a runtime qCritical-refuse/
      repair. Must HEAL Bree's existing `splitterPosition=0` config in place.
      Guard: a sanitized layout-manager unit test feeding a bad splitterPosition
      that asserts two correctly-placed splitters (would abort under ASan/
      forced-assert against the old code). DONE: single-owner pure core
      `containment/plugin/units/justifysplitters.h` (validate + centered repair
      + post-repair Q_ASSERT) is the only path reaching `QList::insert` for a
      splitter index; both call sites (updateOrder, restore) route through it
      and warn loudly on a bad pair; restore() heals via a trailing
      saveOptions(). Guard `tests/units/justifysplitterstest.cpp` PASSES on the
      fix and ABORTS (SIGABRT) on the reverted raw insert (proven). Nested
      vehicle reproduced the live UB (`QList(0, -1, 3, 4, 6, 29, 122)` from
      splitterPosition=0). Full detail + the nested-vehicle
      packaged-plugin-shadow finding + the desk-check owed:
      docs/agent-logs/2026-07-18-justify-splitter-ub.md. Commits: (branch
      panel-fix-justify-splitter-ub head 33a28f196; post-rebase hash at merge)
- [ ] B2 Sweep the obvious index/enum/pointer boundaries the sanitized dock
      surfaces once it runs (A2/A3), converting each into a Q_ASSERT + runtime
      refuse. Ongoing, evidence-driven from Prong A findings. Commits:
      FIRST FINDINGS (A3's driven gate, 2026-07-18): two UBSan -fsanitize=vptr
      aborts on the sanitized dock's clean-shutdown path, same root cause - a
      static_cast downcast in a destroyed(QObject*) slot reads the object's
      decayed (now-QObject) vptr = UB. app/layout/genericlayout.cpp:790
      (Containment) and app/layouts/syncedlaunchers.cpp:65 (QQuickItem); both use
      the pointer only for identity (indexOf / take / removeAll), so fixed with
      reinterpret_cast (vptr-clean, no vtable access) + a comment at each site.
      Fixed on the ub-a3-sanitized-gate branch so the gate is green; final hashes
      at merge. Not a boundary-refuse case (no external bad input) - a cast-idiom
      correction - but recorded here as Prong A-surfaced UB. Remaining B2 index/
      enum boundaries: still to sweep as the gate exercises more paths.

## Farm-out (parallel Opus worktree agents)
- Agent 1 = Prong A1+A2 (the LATTE_SANITIZE build + nested run + known-UB
  proof). Build-system blast radius: normal build stays green.
- Agent 2 = Prong B1 (justify fix + boundary invariant + sanitized test).
  Independent files (layoutmanager.cpp + a test) - parallel-safe with Agent 1.
- Then: A3 (gate wiring, after A1/A2) - orchestrator or a follow-up agent.

## HARD CONSTRAINTS for every agent (learned the hard way 2026-07-18)
- NEVER drive the real Wayland session. Nested vehicle ONLY. Visual/real-theme
  checks = desk-checks OWED to Bree with exact steps, never an agent on the desk.
- NEVER rebuild the binary Bree's live dock runs from (main worktree build/).
  Agents build in their OWN worktree tree - safe. Never touch main build/.
- Merge via `gh pr merge --rebase` (CLAUDE.md merge-mechanics) so PRs show MERGED.
