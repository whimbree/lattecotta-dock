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

- [ ] A1 CMake option `-DLATTE_SANITIZE=ON` (default OFF) that compiles the
      dock and its libs (app/, containment/plugin/, declarativeimports/) with
      `-fsanitize=address,undefined -fno-sanitize-recover=all` + link, and
      `QT_FORCE_ASSERTS`, so OUR Q_ASSERTs go live and OUR-code UB aborts with
      a stack. Factor the flag set shared with `latte_add_unit_test` so there
      is one source of truth. The normal build MUST stay unchanged when OFF
      (blast radius: verify gate-all green with the option absent). Commits:
- [ ] A2 A build-asan tree + a launch mode: a script (or `LATTE_RUN_WRAPPER`
      analogue) to run the sanitized dock in the NESTED VEHICLE only (never
      the real session), with the ASan/UBSan options set to abort-and-log.
      Prove it catches a known UB (inject a temporary one, confirm the abort +
      stack, remove it). Commits:
- [ ] A3 Wire the sanitized dock into a driven gate: run the e2e /
      sceneprobe nested scenarios against the build-asan dock so UB in
      integration paths fails CI (halt_on_error, first-finding abort).
      Sequenced after A1/A2. Commits:

## Prong B - boundary invariants make lib-boundary UB structurally impossible

The codebase's own law (CLAUDE.md "assert invariants... make invalid states
unrepresentable... qCritical-and-refuse at API boundaries"), currently
under-enforced. At every index / enum / pointer boundary: `Q_ASSERT` the
precondition (live in the sanitized/test build) AND a runtime `qCritical`
-and-refuse/repair for the release dock (where Q_ASSERT is stripped). An
out-of-range value must never REACH `insert()` - caught at test time by forced
asserts, refused/repaired at runtime in the field.

- [ ] B1 JUSTIFY SPLITTER FIX (the first boundary-invariant case, and a live
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
      forced-assert against the old code). Commits:
- [ ] B2 Sweep the obvious index/enum/pointer boundaries the sanitized dock
      surfaces once it runs (A2/A3), converting each into a Q_ASSERT + runtime
      refuse. Ongoing, evidence-driven from Prong A findings. Commits:

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
