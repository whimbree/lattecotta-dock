# Testing standard

Adopted at Phase 0 of the port (see `docs/tracking/PORTING_PLAN.md`), before any
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
gets an entry in a live-only registry (`docs/reference/live-only.md`,
created with its first entry) stating *why*, and becomes the target of
live verification (per-phase live testing from the first runnable
milestone, and the nested-vehicle e2e suite below). The registry exists
so the gap is recorded instead of papered over with a dishonest test.
The bar for "genuinely" tightened once the e2e tier existed: needing a
real compositor or a real pointer gesture (drag, tap, wheel, hover
glide) is no longer live-only, because the nested-vehicle e2e suite
(`scripts/run-e2e.sh`, a desk-independent `kwin_wayland --virtual` with
fakepointer injection) supplies both with no dependence on the
developer's session. True live-only is now what needs the real desk
itself: real kglobalaccel registration, real multi-screen, audio bound
to a task, a logout/login or lock/unlock cycle. Latte's QML still has
units that dereference a live containment or `Plasmoid` attached objects
in ways an offscreen engine cannot satisfy - latte-dock-qt6's registry
shows how large that class is - but the "needs a real compositor" slice
of it is now e2e-coverable rather than live-only.

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
(exact match; the baseline only shrinks - with ONE documented exception: an
irreducible per-feature warning that cannot be qualified away, e.g. an `i18nc`
label in a context-property-only file with no qualifiable access, may grow the
baseline by that minimum when the growth is justified in the commit body; the
D23 Reverse-row restore is the first such case, 242->243), and every QML file a
cutover commit touches leaves at zero curated warnings. Full-strict QML is the
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
  exactly that class. A known-open bug recipe should reserve one nonzero
  status and declare `# e2e-expect: status N`, where N is 1 through 255.
  Only status N becomes XFAIL; status 0 becomes XPASS, and every other
  nonzero status remains FAIL. Reserve N only after the exact known signature,
  shutdown, restoration, and residue checks complete, so infrastructure and
  cleanup failures cannot masquerade as the known bug.

  The legacy `# e2e-expect: fail` marker remains supported for recipes whose
  every nonzero exit represents the same open condition. It maps any nonzero
  status to XFAIL and therefore risks hiding an unrelated failure; prefer the
  exact-status form and keep any legacy marker on the narrowest possible
  recipe. With either marker, status 0 is XPASS until the marker is removed and
  the recipe becomes a normal guard. No marker retains ordinary status-0 PASS
  and nonzero FAIL behavior. Marker extraction is strict: exactly one nonempty
  declaration is allowed. Blank, duplicate, conflicting, malformed, zero, and
  out-of-range declarations fail before recipe execution. `060` used the legacy
  form against the Phase 8 surface drift; that drift is fixed (see below), so
  `060` now passes without a marker as a permanent standing guard.
  Every recipe passes SOLO and now back-to-back: the pointer-precision
  recipes (`050-drag-reorder`, `parabolic-hover-preview`) and the
  focus-grant-timed `keyboard-navigation-mode` used to flake in a long
  sequential run because the bottom-dock surface-geometry drift shifted
  icons out from under fakepointer and slowed the OnDemand focus grant
  as the surface re-anchored mid-suite. That drift is root-caused and
  fixed (a masked dock's surface now anchors both length edges instead
  of relying on compositor centring - app/wm/waylandlayershell.cpp
  anchorsFor, ledger docs/agent-logs/2026-07-17-phase8-surface-drift.md),
  so the surface no longer re-anchors and the suite is stable. A
  per-recipe dock reseed was tried during the investigation and made the
  pointer recipes WORSE (a freshly restarted surface is even less
  settled), so the suite keeps the shared-dock shape.

  `parabolic-hover-preview` additionally re-glides onto the target icon
  until the preview dialog maps (root-caused 2026-07-17, ledger
  docs/agent-logs/2026-07-17-preview-recipe-flake.md). The preview trigger
  is the task MouseArea's `onEntered`, and the parabolic layer only emits
  it while the dock is at REST - `hoverEnabled` gates off during the zoom
  animation (Qt5-faithful: mid-animation hover jitter is intentionally
  ignored). A single synthetic glide crosses the icon boundary at a moment
  that races that animation, so `onEntered` fires on only ~60% of attempts,
  and once the warped pointer rests inside the icon no further crossing
  re-fires it, so the dialog never maps. This is a synthetic-injection
  artifact, not a dock defect - a real, continuously-moving mouse re-nudges
  and previews are reliable live. The recipe mirrors that: it repeats the
  glide gesture (up to 12 tries, breaking as soon as the dialog maps) while
  keeping the assertion honest - a genuine layer=6 dialog must map. Proven
  12/12 back-to-back after the fix; ~60% single-shot before it.

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
