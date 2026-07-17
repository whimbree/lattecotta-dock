# Testing standard

Adopted at Phase 0 of the port (see `docs/PORTING_PLAN.md`), before any
test exists, so it can shape every test written rather than being
retrofitted during stabilization. Modeled on latte-dock-qt6's documented
honest-coverage standard (`5fcaa9f1`/`c903921d` in its history), which
explicitly bans gaming the metric.

## The standard

A test earns credit for a unit of code only if all of these hold:

1. **Real assertions.** Every claimed unit must assert an observable
   effect: a return value, a property change, a signal emission, or a
   mock side-effect. Execute-but-assert-nothing does not count.
2. **No swallowed throws.** No `try {} catch {}` (or `safe()` wrapper)
   that lets a test pass while the unit's body actually failed. If a
   unit throws on a missing global, mock the global or don't claim the
   unit.
3. **No construction-only credit.** An object that gets instantiated but
   can't be retained or driven does not count as covered, even if its
   `Component.onCompleted` fired during teardown.
4. **Honest mocks.** Mocks are shaped like the real object - the
   properties and methods the unit actually reads - never a catch-all
   that silently absorbs every access.
5. **Deterministic and headless.** Passes offscreen, stable across runs,
   no dependence on the developer's live session.

A unit that genuinely cannot meet 1-4 headlessly is **live-only**: it
gets an entry in a live-only registry (`docs/testing/live-only.md`,
created with its first entry) stating *why*, and becomes the target of
live verification (per-phase live testing from the first runnable
milestone, and the Phase 10 e2e harness). The registry exists so the gap
is recorded instead of papered over with a dishonest test. Latte's QML
is full of units that dereference a live containment, `Plasmoid`
attached objects, or a real compositor - latte-dock-qt6's registry shows
how large this class is; expect the same here.

## Pure cores (the extraction initiative's step-2.5 law)

Pure cores (`units/` headers and their capt placements) are written to
the project C++ standard (C++20, pinned at the top-level CMakeLists)
with invalid states designed out via the type system - std::optional,
enum class, strong typedefs, bounds-checked access, no
lifetime-escaping lambdas - and the eliminated invalid states named in
the unit's ledger entry at landing. Their tests run under ASan+UBSan
(`latte_add_unit_test` in tests/units/CMakeLists.txt; a `.cpp` core is
compiled into the sanitized test binary, never linked from an
unsanitized object library). A sanitizer trip is a real bug to fix at
origin, never suppress. QML-side, `tests/coverage/qmllint-gate.sh` ratchets
five curated warning categories against `tests/coverage/qmllint-baseline`
(exact match; the baseline only shrinks), and every QML file a cutover
commit touches leaves at zero curated warnings. Full-strict QML is the
asymptotic state the extraction converges to, not a mandate on
inherited files; structural exceptions are named in the extraction
plan's section D (the baseline file is regenerated wholesale). The
fences: this law binds the new cores and their tests only - the live
dock and packaged runtime are never sanitized, and no repo-wide
modernization of inherited code rides on it.

## Infrastructure decision (Phase 0)

Adopt latte-dock-qt6's three-piece shape, adapted rather than copied:

- **C++ behavioral tests** (`tests/`, ctest, ran by
  `scripts/build-check.sh`) - link the real compiled application code
  through the `lattedock-core` object library instead of mocking or
  re-building it, redirect XDG paths at throwaway temp dirs, run
  offscreen. First occupant: the `Importer::uniqueLayoutName`
  regression test pinning the QRegExp -> QRegularExpression
  copy-suffix behavior. (This piece firmed up at the Phase 2 compile
  milestone.)
- **Headless QML checks** (landed at the Phase 5 milestone, two ctest
  entries):
  - `qmlcompilegate` (`scripts/qml-compile-gate.sh`) stages a cmake
    install and compiles every package QML file in a real engine via
    `Qt.createComponent` - type resolution and property-assignment
    existence for lazy, interaction-only components included. Skips
    (and reports) files needing the running app's private module or
    plasma-workspace's private shell module.
  - `qmlinteraction` (`scripts/qml-interaction-tests.sh`) runs
    `tests/qml/tst_*.qml` through qmltestrunner against the staged
    Latte modules, so module registration and type resolution are part
    of every test. First occupant: `tst_shadoweditem.qml`.
  Both source `scripts/lib-qml-env.sh`, which assembles the import
  path from the devShell's pinned module set - the user profile's
  QML2_IMPORT_PATH and the engine's ambient defaults resolve modules
  from foreign Qt builds on this host and must not leak in.
- **Live-run script** - `scripts/run-staged.sh` starts the staged dock
  against the live Wayland session with a throwaway XDG_CONFIG_HOME;
  this is the vehicle for the per-phase live verification cadence.
- **Coverage ratchet** - fails the build check on regression below the
  recorded baseline. Baseline gets recorded once the harness produces
  its first honest number; ratchet thresholds only ever move up.
- **e2e harness** - `scripts/run-e2e.sh` runs the `tests/e2e/*.sh`
  recipes against the staged dock. The default mode is DESK-INDEPENDENT:
  a nested `kwin_wayland --virtual` vehicle with one private D-Bus
  session shared by kwin, the dock and every probe, a throwaway config
  copy, fakepointer injection and KWin ScreenShot2 capture (pixel
  assertions via `latte-imgdiff` against eye-verified goldens in
  `tests/e2e/goldens/`); `--live` runs against the real session for
  recipes that genuinely need it (`# e2e-mode:` markers select). The
  kwin bring-up/teardown is shared with the sceneprobe gate
  (`scripts/lib-nested-kwin.sh`); recipe helpers live in
  `tests/e2e/lib.sh`. Assertions are D-Bus state first (lifecycleState,
  viewsData and friends); pixels only where pixels are the thing under
  test. The ONE thing D-Bus state cannot see is state/render
  divergence - the dock believing the right geometry while the
  compositor draws it elsewhere - so `e2e_assert_geometry_agrees`
  (`060-geometry-agreement.sh`) compares each view's reported origin
  against its true rendered surface position as a standing guard for
  exactly that class. A recipe that reproduces a known-open bug carries
  a `# e2e-expect: fail` marker: the driver reports its failure as XFAIL
  (not counted against the suite) and its unexpected pass as XPASS (a
  hard failure - the guarded bug is fixed, so the marker comes off and
  the recipe becomes a real guard). `060` is XFAIL today against the
  Phase 8 drift below and will XPASS the moment it is fixed. Known
  xfail limitation (standard for the mechanism): an xfail recipe treats
  ANY failure as the expected one, so if `060` ever failed for an
  unrelated reason (its `e2e_wait_settled` timing out on infra
  breakage, say) that would still read XFAIL and mask a new bug - keep
  the marker on the narrowest possible recipe and drop it promptly once
  a guarded bug is fixed.
  Every recipe passes SOLO; the pointer-precision recipes
  (`050-drag-reorder`, `parabolic-hover-preview`) and the
  focus-grant-timed `keyboard-navigation-mode` can flake in a long
  back-to-back run because the bottom-dock surface-geometry drift filed
  in Phase 8 (surface renders offset from its reported geometry and
  re-anchors on clock-minute ticks) shifts icons out from under
  fakepointer and slows the layer-shell OnDemand focus grant. Until
  that root cause lands, run a suspected-flaky recipe on its own to
  separate a real regression from the drift; a per-recipe dock reseed
  was tried and made the pointer recipes WORSE (a freshly restarted
  surface is even less settled), so the suite keeps the shared-dock
  shape.

Enum/handler completeness tests (Phase 6: every UI-offered enum value
must have a handled branch, verified per enum/handler pair) are part of
the headless harness's job, not a separate mechanism.

## Gate discipline for agentic sessions (added 2026-07-16)

One runner, one verdict: `scripts/gate-all.sh` runs every merge gate
(build-check: full build + full ctest + coverage
ratchet, qmllint gate, sceneprobe gate) and its EXIT CODE is the only
verdict - success additionally prints `GATES: ALL OK @ <sha>` as the
last line and stamps that sha into `build/_gates-passed`. The
committed pre-push hook (`scripts/git-hooks/pre-push`, enabled with
`git config core.hooksPath scripts/git-hooks`) refuses to push a ref
whose code differs from the stamped sha; docs-only drift (docs/,
*.md) past a stamped ancestor is exempt so documentation ticks stay
cheap. Never scrape logs for gate success and never combine reading a
verdict with acting on it in one shell invocation: a failure branch
that exits 0 plus a tail read in the same breath as `git push` shipped
a broken master for 20 minutes on 2026-07-16.
