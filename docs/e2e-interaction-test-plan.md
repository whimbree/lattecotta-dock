# E2E interaction test suite plan

Planning artifact, written 2026-07-18. The port has a mature HEADLESS
state-assertion stack (the `tests/e2e` nested-vehicle suite: kwin_wayland
`--virtual` + a private bus + fakepointer + the `org.kde.LatteDock` D-Bus
surface) and a mature deterministic RENDER gate (`tests/sceneprobe`: offscreen
Vulkan/lavapipe QML render + golden compare with the Phase C device/tier
split). Today those two stacks barely overlap: the e2e recipes assert on state
and only one recipe (`040-preview-tooltip-text.sh`) ships a pixel golden, and
sceneprobe renders isolated QML components, not the live dock mid-interaction.

This plan designs a COMPREHENSIVE interaction-level e2e suite that drives the
real widget/layout machinery headlessly in the nested vehicle. It covers the
committed interaction verbs (edit mode, add widget, rearrange applets, rearrange
tasks, move a view across screens, remove) AND, as a first-class equal
dimension, the ABORT / negative path of each verb, across the meaningful
cross-product of dock/panel x four edges x four alignments x single/dual output.

This is the PLAN. No scenario or infra is implemented here. It is the checklist
the implementation swarm executes, reviewed by the orchestrator and Bree before
any implementation fires.

## 0. Governing constraints (HC1-HC4)

Four hard constraints from Bree (2026-07-18) govern this plan and sit ABOVE the
original brief where they conflict. The plan is not done unless it visibly
honors all four; they are the acceptance criteria for the plan itself.

- **HC1 - Mark NOVEL vs COVERAGE-ONLY cells (anti-bloat).** The full
  cross-product is huge and most cells re-run a path another cell already
  covers. Every cell (or cell group) in the enumerated matrix is LABELED NOVEL
  (exercises genuinely distinct code: vertical-edge layout math, justify
  distribution, cross-screen reassignment, the abort/residue paths) or
  COVERAGE-ONLY (a redundant re-run in a different cosmetic config).
  Implementation effort and golden maintenance concentrate on NOVEL cells;
  coverage-only cells are cheap parametrized repeats or explicitly deferred. The
  novel-cell count is reported SEPARATELY from the total (section 6).

- **HC2 - Render golden ONLY where no readback exists** (this SUPERSEDES the
  original "VERIFICATION = BOTH, FULLY / a render golden per scenario"
  instruction; that instruction is not followed). Assert via D-Bus STATE
  READBACK wherever a readback can express the property (deterministic, cheap,
  non-flaky). Use a RENDER GOLDEN ONLY for properties with NO queryable
  readback: genuinely visual facts (pixel placement of edit chrome, justify
  pixel distribution, a stale frosted band, ghost-overlay presence). CRUCIAL
  corollary: when tempted to reach for a golden, FIRST ask whether a readback
  SHOULD exist - a missing readback is a signal to ADD one (observability
  first), not to golden around it. Net effect: the readback-gap list (new D-Bus
  surface to add, section 9) grows, and the render-golden set stays small,
  deliberate, and justified case-by-case (section 7 justifies every golden).

- **HC3 - Every driver's acceptance test must prove it OBSERVES A REJECTION.**
  Each infra driver (multi-output vehicle, widget-explorer DND driver,
  task-reorder driver, applet-reorder driver, the parametrized harness) ships a
  self-test / acceptance test that demonstrates the driver can OBSERVE A
  NEGATIVE OUTCOME - a rejected drop, a refused reorder, an aborted move - and
  report it AS a rejection, not merely show a green happy-path. A driver whose
  acceptance test only proves the success path is untrustworthy: it cannot be
  relied on to assert "the abort left no residue" because it would report
  success regardless. This is the tripwire principle (the sceneprobe gate's
  own good-passes/bad-fails/blank-fails self-test, and run-e2e's XFAIL/XPASS
  machinery) applied to the drivers themselves, and it is what makes the abort
  column (HC4) real rather than decorative. Each infra-prereq item states this
  in its definition of done (section 5).

- **HC4 - Abort / cancel is a first-class interaction verb.** The abort column
  crosses the whole matrix; each abort scenario asserts return-to-baseline with
  no residue; it is the highest-value class (the `insert(-1)`/PR-#20 residue
  lives there); it gets its own chunk family; it lands EARLY, never last
  (section 8). Rationale in section 1.

## 1. Scope and honored decisions

- **VERIFICATION is READBACK-FIRST, GOLDEN-ONLY-WHERE-BLIND** (HC2). See section
  3 for the model and section 7 for every golden's case-by-case justification.
  The golden path, where used, reuses Phase C's device/tier decoupling
  (`tests/sceneprobe/imagecompare.h` `GoldenTier`, `tests/sceneprobe/imgdiff_main.cpp`):
  bit-exact on the NixOS tier, bounded-delta on the tolerance tier.

- **ABORT / NEGATIVE PATH IS A FIRST-CLASS VERB DIMENSION** (HC4). The committed
  verbs are all happy paths, and happy paths are BY DEFINITION the ones that
  already work. The path that historically breaks, and that none of the
  committed verbs cover, is CANCEL / ABORT MID-INTERACTION: the committed action
  completes, but the ABORTED action strands stale intermediate state. This
  project's own recent bugs are all abort/residue-class: the `justifysplitters`
  `insert(-1)` UB was a HALF-APPLIED insertion index; PR #20's edit-mode overlay
  ATE CLICKS AFTER EXIT; "vacated the old output but never claimed the new one"
  is the classic aborted cross-screen-move split-brain strand. So abort is a
  matrix COLUMN, and each abort asserts RETURN-TO-BASELINE WITH NO RESIDUE.

- **MATRIX = FULL, DUAL-DISPLAY, but LABELED NOVEL vs COVERAGE** (HC1). The
  universe is viewType {dock, panel} x edge {top, bottom, left, right} x
  alignment {left, center, right, justify} x display {single, dual} = 128 config
  cells, each run for both the COMMITTED and the ABORT outcome of every
  applicable verb. Section 6 labels each cell group and gives novel vs
  coverage-only counts. Dual-display exercises cross-screen moves, per-screen
  layouts, and stranding.

- **This plan is REVIEWED** by the orchestrator and Bree before implementation.

Non-goals: this suite does not replace live-desk verification for things the
vehicle cannot carry (real kglobalaccel feel, Orca, human focus interplay, real
multi-GPU). It does not test D-Bus refusal semantics of the read surface
(covered by `dbusreportstest`) or pure-core math (covered by the unit tier). It
drives the same coarse user-action code paths the UI drives, never internal APIs.

## 2. Ground truth: what exists today

Read before implementing; do not re-derive.

- **Nested vehicle**: `scripts/run-e2e.sh` (driver, nested default + `--live`),
  `scripts/lib-nested-kwin.sh` (compositor bring-up/teardown, one private
  `dbus-run-session` bus shared by kwin+dock+probes), `tests/e2e/lib.sh` (the
  recipe helper library). The vehicle is `kwin_wayland --virtual --width 1600
  --height 1000`, a throwaway config copy, `KWIN_WAYLAND_NO_PERMISSION_CHECKS`
  for fakepointer, `KWIN_SCREENSHOT_NO_PERMISSION_CHECKS` for ScreenShot2.
- **Existing recipes** (`tests/e2e/*.sh`): `000-smoke`, `010-wheel-desktops`,
  `020-wheel-task-cycle`, `030-wheel-ruler-maxlength`, `040-preview-tooltip-text`
  (the one golden recipe today), `050-drag-reorder-launchers` (the closest DND
  example: fakepointer glide-drag of a launcher, `viewTasksData` readback,
  config persistence, survives restart), `060-geometry-agreement` (the
  state-vs-render guard - the readback of "does the compositor draw the dock
  where viewsData claims", `e2e_assert_geometry_agrees`), `keyboard-navigation-mode`,
  `settings-window-onscreen` (kglobalaccel edit chrome), `parabolic-hover-preview`,
  `duplicate-view-idremap`.
- **Helpers in `lib.sh`**: `e2e_call`/`e2e_json`, `e2e_wait_running`,
  `e2e_wait_settled` (existence + geometry-stopped-moving), `e2e_dock_start`/
  `e2e_dock_stop`, `e2e_kwin_js`/`e2e_dumpwins` (compositor window truth - a
  READBACK of every window's class/caption/geometry/output/layer),
  `e2e_screenshot` (KWin ScreenShot2 -> PNG, nested-only), `e2e_tasks_view`,
  `e2e_view_window_x`, `e2e_task_center`, `e2e_geometry_drift`,
  `e2e_assert_geometry_agrees`.
- **fakepointer** (`scripts/tools/fakepointer.c`): `move`, `click`,
  `rightclick`, `drag x1 y1 x2 y2 [x3 y3 ...]` (press, 24-step glide, release),
  `glide ...` (move-only), `scroll x y detents ms-gap`. NO key injection today:
  Escape-cancel aborts need a new `key` verb (prereq P9); release-over-nothing /
  drop-at-origin aborts are pointer-only.
- **Render golden infra**: `tests/sceneprobe/main.cpp` (offscreen Vulkan render,
  5 frames, `StepAnimationDriver` deterministic clock, readback, golden compare),
  `tests/sceneprobe/imagecompare.{h,cpp}` (`CompareTolerance`, `GoldenTier`
  {BitExact, Tolerance}, `parseGoldenTier`, `toleranceForTier`, `compareImages`,
  `amplifiedDiff`, `verdictLine`), `tests/sceneprobe/imgdiff_main.cpp`
  (`latte-imgdiff actual.png expected.png [--delta N] [--budget F] [--out
  diff.png]`), `scripts/sceneprobe-gate.sh` (nested kwin + lavapipe ICD +
  `SCENEPROBE_TIER` + `--bless`; SELF-TESTS good/bad/blank first - the HC3
  precedent).
- **D-Bus surface**: `docs/dbus-interface-reference.md`, adaptor XML
  `app/dbus/org.kde.LatteDock.xml`, serializers `app/dbusreports.{h,cpp}` pinned
  by `tests/units/dbusreportstest.cpp`. Section 9 inventories which methods each
  verb uses and which are MISSING.
- **Subsystems under test**: `app/view/` (`view.cpp`, `positioner.cpp`,
  `visibilitymanager.cpp`, `effects.cpp`, `settings/primaryconfigview.cpp`,
  `clonedview.cpp`), `containment/plugin/layoutmanager.cpp` +
  `containment/plugin/units/justifysplitters.h` (applet-order + splitter
  machinery + the `insert(-1)` UB history), `plasmoid/` (the Latte tasks applet).
- **Traps** (`latte-live-verification` skill): kglobalaccel edit-mode
  registration race (retry); settings window closes on focus loss; the removeView
  undo window (containment lingers until Panel-Removed notification or ~60s,
  restart resurrects); glide-not-jump (parabolic zoom shifts icons); ScreenShot2
  not spectacle in-vehicle (no task-icon trap); stale chrome popups swallowing
  clicks; synthetic click on a shrinking input mask losing its release (a real
  abort-residue lookalike); KConfig deletes keys whose value returns to default
  (an abort's byte-unchanged assertion must reason about defaults, not raw
  presence).

## 3. Verification model (readback-first)

Every scenario asserts against the post-drive dock, primarily by D-Bus/compositor
READBACK, with a render golden reserved for the visual-only residue.

**Committed scenarios** assert a STATE TRANSITION by readback: pull the exact
queryable fact the interaction changed (`viewAppletsOrder`, `viewTasksData`
order, `viewsData.editMode`, `viewsData.screen`, applet count, chrome-window
geometry via `e2e_dumpwins`, published struts) and compare to expected. A render
golden is added ONLY for a transition whose CORRECTNESS is a pixel fact no
readback expresses (section 7 names each).

**Abort scenarios** assert RETURN-TO-BASELINE WITH NO RESIDUE:

- `matrix_baseline_capture <cell>` snapshots the exact D-Bus/compositor fields
  the verb would touch (and, only for the visual-residue cases, a baseline crop).
- Drive the interaction, then ABORT it mid-flight.
- `matrix_assert_baseline_restored <cell>` re-reads the same fields and asserts
  BYTE-IDENTICAL to baseline (order unchanged, count unchanged, screen unchanged,
  struts unchanged, `editMode`/`inConfigureAppletsMode` false, config file
  byte-unchanged modulo KConfig default-key deletion). Where the residue is
  VISUAL-ONLY and no readback can express it (a ghost overlay whose state says
  "clean" - PR #20; a stale drop-marker band), it also re-shoots the crop and
  asserts equality to the baseline frame. No abort blesses a NEW golden; its
  expected image IS the clean baseline, so residue shows as a diff against a
  known-clean frame.

Why readback-first works here and where it does not: most residue HAS a readback
or should get one (a half-applied swap shows in `viewAppletsOrder`; a stale
insertion index should show in a drop-marker/spacer readback we ADD; a
stuck-over-chrome icon should show in a `z`/stacking readback we ADD; a
split-brain move shows in `viewsData.screen` + `publishedStruts`). The RESIDUAL
cases that legitimately need a golden are the ones where the dock's STATE is
already clean but the COMPOSITOR still shows something - exactly the Phase 8
state-vs-render divergence class - and PR #20's exited-overlay-still-drawing is
the canonical one. Those, and pure placement/distribution pixels (edit chrome
geometry inside the dock window, justify zone widths), are the whole golden set.

## 4. The golden strategy (small, deliberate, tier-reused)

Where a golden is genuinely required (section 7 enumerates them), it is a
LIVE-DOCK cropped screenshot compared through sceneprobe's comparator:

- **Determinism**: render through the same deterministic stack the sceneprobe
  gate uses - lavapipe ICD (`LATTE_VULKAN_LAVAPIPE_ICD`, `LP_NUM_THREADS=0`),
  pinned fonts (flake-frozen), a FIXED flat wallpaper (so wallpaper never
  contributes noise), cursor parked OFF the crop, and the shot taken only after
  `e2e_wait_settled` reports geometry stopped moving (the startup zoom animation
  is the known coordinate-drift source).
- **Crop** to the bounding rect of the view under test unioned with the
  edit-chrome window (from `viewsData`/`maskRect` + `e2e_dumpwins`). Smaller
  goldens are stable and localize failures.
- **Filename** mirrors sceneprobe device keying:
  `tests/e2e/goldens/<family>/<cell>.<verb>.<phase>.expected.lavapipe.png`;
  device always `lavapipe`. Abort scenarios reuse the committed family's
  `...before` golden as expected and bless NO new file.
- **Tier** reuses `GoldenTier`. The bridge maps tier to `latte-imgdiff` args:
  BitExact -> `--delta 0 --budget 0`; Tolerance -> `toleranceForTier`. Env is
  `SCENEPROBE_TIER` (already the multi-distro container ENV). If live-compositor
  shots prove too brittle for bit-exact even on NixOS, the interaction goldens
  gate at Tolerance while sceneprobe stays BitExact (open question O3).
- **Bless** mirrors `sceneprobe-gate.sh --bless`. A missing required golden is a
  hard failure on the lavapipe tier (mirror `main.cpp`).

Because HC2 shrinks the golden set to the visual-only residue and placement
facts, the total blessed golden corpus is small (section 7 sums it at roughly
three dozen), not one-per-scenario.

## 5. Infra prerequisites (dependency order)

Each is a chunk in section 8. Every item's definition of done includes the HC3
acceptance clause: its self-test must demonstrably OBSERVE A REJECTION, not only
a success.

- [x] **P0 Parametrized fixture generator + matrix harness + baseline
      backbone.** Blocks every scenario. (a) A generator
      (`tests/e2e/matrix/fixture.py`) that, given `(viewType, edge, alignment,
      display)`, derives a throwaway config from a proven-loadable seed with
      the view type, edge, alignment, and per-screen assignment
      (`screensGroup`/`onPrimary`/`explicitScreen`) set correctly. viewType is
      driven by the QML derivation (zoom-off + justify-or-static + thick
      background = panel; zoom-on = dock) since `viewsData.type` is the derived
      value, not a stored flag. (b) A harness (`scripts/run-matrix.sh` +
      `tests/e2e/matrix/matrix-lib.sh`): `matrix_scenario_commit` /
      `matrix_scenario_abort <cell> <verb>` stage, start, drive, assert, tear
      down - scenarios are FUNCTIONS parametrized by cell, never copy-paste;
      verbs are `matrix_verb_<name>_drive`/`_probe` pairs a scenario chunk
      defines. (c) The abort backbone: `matrix_baseline_capture` /
      `matrix_assert_baseline_restored` snapshot and diff the residue surface
      (view record, applet order, config bytes KConfig-default-aware, the
      verb's probe, and a baseline frame under `MATRIX_CAPTURE_FRAME=1` whose
      pixel compare is a P2 hook). Acceptance (observes a rejection): the
      `matrix-harness-selftest` recipe asserts the harness's OWN verdicts - a
      WRONG expected-readback reported as FAIL, abort RESIDUE (a leaked edit
      session) reported as FAIL, a malformed cell REFUSED, and a
      did-not-realize-as-declared fixture REFUSED - not a green pass. Reuses
      run-e2e's XPASS-is-failure discipline.
      Commits: (C-I1 branch, hash at merge)
- [ ] **P1 Multi-output nested vehicle.** Blocks all dual cells and F5/A4.
      Extend `lib-nested-kwin.sh` to pass `--output-count 2` (verified on the
      pinned kwin_wayland: `--virtual --output-count <count>`), discover the two
      output NAMES via a KWin script (`workspace.screens`), export
      `E2E_OUTPUTS`. Confirm `ScreenPool` maps those names so a fixture's
      per-screen assignment lands the view on the intended output (research: the
      name<->connector mapping in a headless virtual multi-output is unproven).
      Acceptance (observes a rejection): the vehicle's self-test asserts a view
      that FAILED to land on its assigned output is reported as a
      MIS-ASSIGNMENT (drive a fixture pinned to output B, force/observe it
      landing on A, prove the check goes red), and that a single-output run
      asked for output B reports "no such output" rather than silently passing.
      Commits:
- [x] **P2 Render-golden bridge (small set).** Blocks the render-golden half of
      the visual-only scenarios (section 7). Wire lavapipe + fixed wallpaper +
      cursor-park. Add `e2e_assert_golden <cell> <verb> <phase>` (crop ->
      `latte-imgdiff` at the tier tolerance -> save actual/expected/diff on
      mismatch) and the golden-equality helper the abort backbone calls. Add
      `--bless`. Acceptance (observes a rejection): self-test that a deliberately
      WRONG golden FAILS the compare and a MISSING golden FAILS on the lavapipe
      tier (mirror sceneprobe `selftest-bad`/`selftest-blank`). Depends on P0.
      Landed: the bridge is `tests/e2e/matrix/golden-bridge.sh` -
      `e2e_golden_compare` (tier-aware pixel compare), `e2e_screenshot_crop`
      (cursor-excluded deterministic shot), `matrix_view_crop_rect`, and
      `e2e_assert_golden` with `E2E_BLESS=1`. It REUSES the sceneprobe
      comparator: `latte-imgdiff` gains a `--tier` flag that resolves the
      delta/budget from `SCENEPROBE_TIER` through the SAME
      `parseGoldenTier`/`toleranceForTier` the render gate uses (the C++ tier
      table stays the one source of the numbers, no shell re-derivation). The
      abort backbone's stubbed frame-compare hook
      (`matrix_frame_equals_baseline`) now routes through
      `e2e_golden_compare`, and the baseline frames are captured cursor-free.
      Interaction goldens gate at Tolerance, sceneprobe stays BitExact (O3,
      Bree 2026-07-18): the vehicle dock is host-rendered, not lavapipe-forced.
      Acceptance is `tests/e2e/090-golden-bridge-selftest.sh`: off one settled
      crop it proves a match passes, a WRONG golden and a real diff beyond
      Tolerance are REJECTED (HC3), a missing golden hard-fails, an unknown tier
      is refused, and the SAME +1-shift pair flips verdict across
      tolerance/bitexact (proving the tier axis is consulted, not hardcoded).
      Commits: (C-I6 branch; final hashes at PR merge)
- [x] **P3 `addApplet` action + applet-id order readback.** Blocks F2/A1 and
      F6/A6 setup. Add COARSE `addApplet us <cid> <pluginId>` (Qt5-faithful
      end-append) in the three required places + `dbusreportstest`. ALSO add the
      readback HC2 demands here: an unambiguous applet-id order (extend
      `viewAppletsData`/`viewAppletsOrder` so two same-plugin applets are
      distinguishable by id, not just plugin string). Acceptance (observes a
      rejection): the action's test proves a bad containment id is REFUSED with
      a qWarning and no applet created (the existing read-surface refusal
      contract, extended to this mutator).
      Landed: `addApplet(u,s)` on the corona (loud refusal for a bad
      containment id AND a plugin that names no installed plasmoid; the latter
      made observable by making `ContainmentInterface::addApplet` return bool);
      `viewAppletsOrder` now reports the splitter-free applet-instance-id order
      via the pure `DbusReports::appletIdOrder` helper (G1); XML, design doc,
      usage reference, `dbusreportstest` (appletIdOrder), and the
      `tests/e2e/080-add-applet.sh` acceptance recipe (happy add + G1
      consistency + the HC3 bad-id rejection, PASS in the nested vehicle).
      Commits: (branch worktree-agent-a102bc9cfc620c8c0; final hashes at PR merge)
- [ ] **P4 `removeApplet` action + drop-marker/spacer readback.** Blocks F6/A1/A2.
      Add COARSE `removeApplet uu <cid> <appletId>` (invokes
      `LayoutManager::removeAppletItem`, the `33830b2c` finalize-immediately
      path), documenting the undo window. ALSO add the readback HC2's corollary
      demands for the add/reorder ABORT residue: expose whether a drop
      placeholder / `dndSpacerIndex` is currently LIVE (so a stranded insertion
      index is queryable, not golden-only) - this is the direct `insert(-1)`
      observability. Three places + `dbusreportstest`. Acceptance (observes a
      rejection): a bad applet id is REFUSED, no removal; and the drop-marker
      readback reports "marker live" during a drag and "clean" after, proven by
      driving both states.
      Commits:
- [ ] **P5 `moveViewToScreen` action.** Blocks F5/A4. Add COARSE
      `moveViewToScreen us <cid> <screenName>` (sets the same
      `screensGroup`/`onPrimary`/explicit-screen state the settings combo
      writes). Readback exists (`viewsData.screen`/`onPrimary`/`screenGeometry`/
      `publishedStruts`). Config-write+restart is rejected for the driving path:
      it is not an interaction and would defeat A4's split-brain check.
      Acceptance (observes a rejection): moving to a NON-EXISTENT output is
      REFUSED with a qWarning and the view stays put (readback unchanged),
      proven as a rejection.
      Commits:
- [ ] **P6 Applet-reorder driver (commit + abort).** Blocks F3/A2. Generalize
      `050` to APPLETS in rearrange mode; drag axis edge-driven (horizontal
      top/bottom, VERTICAL left/right - the buggy path). Abort via drop-at-origin
      or Escape (P9). Add a `z`/stacking readback to `viewAppletsData` (HC2
      corollary: the icons-stuck-behind-chrome residue, `480ae30e3`, becomes
      queryable instead of golden-only). Acceptance (observes a rejection): the
      driver's self-test proves it reports a REFUSED reorder (a drag that does
      not cross a neighbor leaves order unchanged - reported as "no reorder",
      NOT success) and an ABORTED reorder (order restored) AS negatives, using
      the `viewAppletsOrder` + z readbacks. Depends on P0.
      Commits:
- [ ] **P7 Task-reorder driver + window-task order readback (commit + abort).**
      Blocks F4/A3. Extend `050` launcher reorder into a reusable driver AND
      cover the DISTINCT window-task sub-model. Launcher order readback exists
      (`viewTasksData` `launcherUrl`, persists to the `launchers` key). Confirm
      window-task order is stably queryable (`viewTasksData` `index`/`appId`); if
      not, ADD it (HC2). Spawn N deterministic client windows for reorderable
      window tasks. Acceptance (observes a rejection): self-test reports a
      REFUSED task reorder (Escape mid-drag -> order unchanged) AS an abort, not
      a pass. Depends on P0.
      Commits:
- [ ] **P8 Widget-explorer DND driver (commit + non-drop abort).** Blocks F2 DND
      leg + A1 (deterministic add leg needs only P3). Open the explorer, locate
      a widget delegate (dumpwins + screenshot calibration, `050`-style), drag
      onto the view (commit) or release over a NON-drop zone (A1 abort). Respect
      the Phase 7 mime split (`text/x-plasmoidservicename` = C++ path only): a
      correct drop adds exactly ONE, a non-drop adds ZERO (the ng
      double-creation bug must not reproduce). Acceptance (observes a rejection):
      THIS DRIVER'S ACCEPTANCE TEST IS A REJECTION BY CONSTRUCTION - it must
      prove that a release over nothing yields ZERO applets and reports that AS a
      rejected drop (count unchanged), which is exactly the A1 assertion; a
      driver that cannot see the rejected drop cannot be trusted for A1.
      EXPECTATION (Bree, 2026-07-18): explorer DND IS doable in THIS nested
      vehicle (kwin_wayland + fakepointer press-glide-release + a live client
      surface in the same compositor) - the old Phase 7 "defer to a GUI CI vm"
      note is stale, it predates this infra. Implement real explorer->containment
      DND here. A degrade to xfail is a LAST RESORT only if PROVEN infeasible
      after honest effort, with the blocking root cause recorded - never an
      assumed skip. Depends on P0, maybe P1.
      Commits:
- [x] **P9 fakepointer key injection (`key <keysym>`).** Blocks the
      Escape-CANCEL sub-path of A2/A3/A4/A5 (release/drop-back aborts do not need
      it). `org_kde_kwin_fake_input` carries a keyboard axis; the `key` verb uses
      `keyboard_keysym` (interface v6) so the compositor maps the semantic XKB
      keysym against its OWN keymap (no fragile hardcoded scancode) - resolved
      from a name via libxkbcommon. Acceptance (observes a rejection):
      `tests/e2e/080-key-escape-cancels-move.sh` proves Escape actually CANCELS
      an in-flight drag by observing the cancel EFFECT - it drives KWin's
      keyboard interactive-move (the simplest in-flight drag a STANDALONE key
      verb can cancel, since a pointer-button-held drag releases when the tool
      exits), nudges the window with arrow keys, then shows Return COMMITS the
      move while Escape ABORTS it and restores the pre-move geometry. The A2/A3
      pointer-drag Escape sub-paths interleave the key within one drag client and
      land with P6/P7. Small, independent; landed early.
      Commits: <this PR - fakepointer key verb + 080 recipe>

## 6. The configuration matrix (labeled NOVEL vs COVERAGE-ONLY)

**Universe** = 2 viewType x 4 edge x 4 alignment x 2 display = **128 cells**,
each run for the COMMITTED and ABORT outcome of every applicable verb. Cell id:
`<vt>-<edge>-<align>-<display>`. A `2out` cell places the view under test on the
SECONDARY output (what makes a dual cell non-redundant).

Novelty collapse rules (HC1) - within a verb, these cells are COVERAGE-EQUIVALENT
(one NOVEL representative, the rest coverage-only parametrized repeats):

- `top` = `bottom` and `left` = `right` are anchor-cosmetic for MECHANIC verbs
  (add/rearrange/remove/their aborts); the ORIENTATION split
  horizontal-vs-vertical IS novel (distinct layout math). For the PLACEMENT verb
  (F1 edit chrome) each of the 4 edges is a distinct chrome geometry = novel.
- `left`/`center`/`right` alignment share one non-justify placement path
  (coverage) FOR A DOCK; `justify` is a distinct splitter path = NOVEL, never
  trimmed (it is the `insert(-1)` path). For F1 chrome on a DOCK, `center` is
  distinct from the rest (settings opens to the side) = novel.
- **PANEL-ALIGNMENT DEGENERACY** (grounded in the code, not a blind collapse).
  A Plasma panel (`behaveAsPlasmaPanel`, i.e. `viewType === PanelView`) is
  LENGTH-MAXIMIZED unconditionally: `main.qml:210` sets
  `const maximize = behaveAsPlasmaPanel || ...` and returns the full `width`/
  `height` for `maxLength` (and `minLength` at `main.qml:203-205` does the same
  under compositing), so a panel ALWAYS spans the whole edge. The left/center/
  right alignment control has nothing to position along an already-full-span
  edge, so for a panel those three are DEGENERATE (no effect), not merely
  coverage-equivalent. There is also NO panel maxLength<100% positioning case:
  the config slider exists but `maximize = behaveAsPlasmaPanel` overrides it, so
  a panel is never sub-full-length (that positioning is a DOCK behavior, and its
  cells are the dock-alignment cells, not a panel path). Consequently a PANEL
  carries only `{full-span (the single non-justify layout), justify (the
  splitter distribution within the full span)}` = TWO alignment modes, never
  four. `center` in any panel row below denotes the full-span layout. Panels are
  also degenerate on non-matrix axes the dock owns (no parabolic zoom, no
  floating length gap), but those are not matrix axes; alignment is the one
  matrix axis where the panel differs, and it collapses 4 -> 2. (Panel edit
  chrome still differs from dock chrome and is NOT degenerate - a panel has no
  parabolic headroom, so its `editThickness`/blueprint path is its own; F1 keeps
  a panel leg, just with 2 alignment modes.)
- `2out` per-view interaction (reorder/add/remove on a secondary view) is
  coverage of its `1out` twin EXCEPT for the SCREEN-OP verbs (F5/A4), where dual
  is the substance = novel.
- ABORT outcomes are novel by class (residue paths), minus their own
  cosmetic-edge repeats.

Meaningful count and novel split per verb x outcome:

| Verb | outcome | cells (meaningful) | NOVEL | coverage-only |
|---|---|---|---|---|
| F1 edit mode DOCK (single, 4 edge x 4 align) | commit | 16 | 8 (4 edge x {centered, not}) | 8 |
| F1 edit mode PANEL (single, 4 edge x {full-span, justify}) | commit | 8 | 4 (4 edge, panel spans full; no L/C/R) | 4 |
| F1 edit mode (dual chrome-on-2nd, 4 edge x 2 vt) | commit | 8 | 2 (dock+panel maps on 2nd) | 6 |
| A5 edit-mode no-op (4 edge x 2 vt center; +dual 2) | abort | 10 | 10 (PR #20 residue, all novel) | 0 |
| F2 add (single {c,j} x 4 edge x 2 vt; +dual 2) | commit | 18 | 8 (orient x {nonjust,just} x vt) | 10 |
| A1 add abort (same shape) | abort | 18 | 8 (residue paths; justify = insert(-1) analog) | 10 |
| F3 rearrange applets (single {c,j} x 4 edge x 2 vt; +dual 2) | commit | 18 | 8 (orient x {nonjust,just} x vt; vertical high-value) | 10 |
| A2 applet-reorder abort (same shape) | abort | 18 | 8 | 10 |
| F4 rearrange tasks (single center x 4 edge x 2 vt; +dual 2) | commit | 10 | 4 (orient x vt; launcher+window within) | 6 |
| A3 task-reorder abort (same shape) | abort | 10 | 4 | 6 |
| F5 move across screens (dual {c,j} x 4 edge x 2 vt) | commit | 16 | 16 (cross-screen reassignment, all novel) | 0 |
| A4 move abort / split-brain (same shape) | abort | 16 | 8 (split-brain check; edge-cosmetic collapses) | 8 |
| F6 widget remove (single {c,j} x 4 edge x 2 vt) | commit | 16 | 8 | 8 |
| F6 removeView commit (single, 2 edge x 2 vt) | commit | 4 | 2 | 2 |
| F6 removeView / stranding (dual, 2 edge x 2 vt) | commit | 4 | 2 | 2 |
| A6 remove-undo abort (single 4 + dual 2) | abort | 6 | 6 (undo+commit x 2 vt +dual) | 0 |

**Committed total = 118. Abort total = 78. Grand total = 196 scenarios.**
**NOVEL ~= 106. Coverage-only ~= 90.** (The panel-alignment collapse removed 8
degenerate panel F1 cells - 4 novel, 4 coverage - from the earlier 204/110/94.)
Implementation and golden effort target the ~106 novel; the ~90 coverage-only
are the same parametrized functions re-invoked with cosmetic params (near-free)
or explicitly deferred if the swarm runs short. justify and abort and
vertical-edge cells are never in the coverage-only bucket, and a panel never
carries four alignment sub-modes (only full-span + justify).

## 7. Scenario families (readback assertions; goldens justified case-by-case)

For each family: the readback that carries the assertion, and - only where a
readback cannot express the fact - the specific golden and its justification.

### F1 Edit mode
- [ ] **F1.** DRIVE: enter two ways (D-Bus `setViewEditMode ub <cid> true`;
      kglobalaccel `invokeShortcut "show view settings"`), exit `false`.
      READBACK: `viewsData.editMode`/`inConfigureAppletsMode` flip; the edit
      chrome (ruler, Add-Widgets, settings window) present at correct per-edge
      geometry and fully on-screen via `e2e_dumpwins` (chrome-window placement IS
      queryable - no golden needed for window x,y,w,h). GOLDEN (justified): the
      BLUEPRINT GRID is drawn INSIDE the dock window (Phase 7 step 1) and the
      ruler/toggle are QML items inside the config view - neither is a separate
      KWin window nor a `viewsData` field, so their pixel placement is
      visual-only. One golden per (edge x viewType) for the representative alignment
      (~8: the dock's centered case, where the toggle sits at the
      alignment-driven end and settings opens to the side; the panel's full-span
      case, since a panel has no L/C/R positioning per the PANEL-ALIGNMENT
      DEGENERACY rule), proving the grid renders at `editThickness` at the edge.
      READBACK-GAP flagged (O-gap): an
      edit-chrome sub-item geometry readback (ruler/toggle rects) would retire
      even these goldens. GOTCHAS: first kglobalaccel invoke races registration
      (retry); dock grows to `editThickness` (Phase 7 step 2 UNFINISHED - assert
      current mask/strut, mark the gap); panel chrome differs from dock (no
      parabolic headroom) and a panel carries only {full-span, justify}, never
      four alignment sub-modes.
      Commits:

### F2 Add a widget
- [ ] **F2.** DRIVE: deterministic `addApplet` (P3) and explorer DND (P8).
      READBACK: `viewAppletsData` length grows by one, new plugin in
      `viewAppletsOrder`, lands at the Qt5-faithful absolute end; DND adds
      exactly one (no ng double-create). No golden for the STATE. GOLDEN
      (justified, justify cells only): the JUSTIFY 3-zone pixel distribution
      after insertion is a visual fact `viewAppletsOrder` does not express (order
      is right, zone widths are the question) - one golden per justify
      orientation x vt (~4). GOTCHAS: mime split (`text/x-plasmoidservicename` =
      C++ only, `View::event()` observes only); `dndSpacerIndex` ->
      masqueraded point (base -23456); glide-not-jump.
      Commits:

### F3 Rearrange applets
- [ ] **F3.** DRIVE (P6): rearrange mode, drag an applet within the view.
      READBACK: `viewAppletsOrder` (+ applet-id order from P3) changed to
      expected; the stuck-over-chrome residue is caught by the `z`/stacking
      readback P6 adds (no golden for it). GOLDEN (justified, justify cells
      only): justify zone-width redistribution after a cross-splitter reorder
      (~4). **VERTICAL edges (left, right) are the historically buggy path,
      called out - highest-value committed cells.** GOTCHAS: `DragHandler`
      bypasses `MouseArea` overlays (guard inside the handler); z reset on
      `Drag.onDragFinished` (`480ae30e3`); Qt5 placeholder algorithm unchanged
      (no knobs).
      Commits:

### F4 Rearrange tasks
- [ ] **F4.** DRIVE (P7): drag a task within the Latte tasks applet (DISTINCT
      `ListView` sub-model). Cover launcher (persists to `launchers` key,
      survives restart) AND window-task (`viewTasksData` `index`/`appId`, needs
      real windows). READBACK: `viewTasksData` order changed; launcher config
      persistence + restart. No golden (order is fully queryable; z-residue via
      the added readback). GOTCHAS: glide not jump; approach from OUTSIDE the
      dock; z reset; zoom-on for the even-slot model (`050`).
      Commits:

### F5 Move a view across screens
- [ ] **F5.** DRIVE (P1,P5): `moveViewToScreen` A->B and back. READBACK, no
      golden: `viewsData.screen` A->B, `screenGeometry` = B's rect,
      `publishedStruts` now on B and GONE on A (vacate is queryable), no
      stranding (`inStartup` not stuck, `isOffScreen` false, geometry
      non-degenerate), and the render-lands-on-B fact is the existing
      `e2e_assert_geometry_agrees` guard (dumpwins vs reported - a readback of
      the compositor, not a golden). Justify: struts recompute against B.
      GOTCHAS: `CLEARED SCREEN` log is BENIGN (Phase 8); cloned-view applet-order
      sync (Phase 8 open); `ScreenPool` name mapping (P1 risk).
      Commits:

### F6 Remove
- [ ] **F6.** DRIVE: widget remove (`removeApplet`, P4); view remove
      (`removeView`, then WAIT for `[Containments][<cid>]` to leave the layout
      before any restart - undo trap). READBACK, no golden: applet count drops,
      plugin gone from `viewAppletsOrder`, view relayouts without stranding; view
      gone from `viewsData`, group gone from the layout, restart does not
      resurrect. The overflow-relayout CRASH (`df747ebf`) is witnessed by a clean
      `e2e_dock_stop` + no new backtrace, not a golden. GOTCHAS: finalize
      immediately (`33830b2c`); verify id against the layout first (skill 3b);
      justify recomputes splitters.
      Commits:

### ABORT families (highest-value; land early)
Each drives then CANCELS, then `matrix_assert_baseline_restored`: byte-identical
readback + (visual-only cases) crop equals the clean baseline.

- [ ] **A1 Widget-add abort.** DRIVE (P8): explorer drag, release over a
      NON-drop zone. READBACK: applet count and `viewAppletsOrder` byte-identical
      to baseline; the drop-marker/spacer readback (P4) reads CLEAN (no stranded
      `dndSpacerIndex`) - the direct `insert(-1)` observability, no golden.
      GOLDEN only if the spacer readback cannot fully express a half-rendered
      placeholder band (justify cells): crop equals baseline. This is the
      readback-gap-becomes-surface case in action.
      Commits:
- [ ] **A2 Applet-reorder abort.** DRIVE (P6; Escape sub-path P9): start a
      reorder, Escape (drag-CANCEL) or release at origin. READBACK:
      `viewAppletsOrder` UNCHANGED (watch a SUBTLE off-by-one, the half-applied
      swap), z back to baseline (no applet over chrome). GOLDEN: none (both
      residues are now readback). Run both sub-paths where P9 exists. Vertical
      edges highest-value.
      Commits:
- [ ] **A3 Task-reorder abort.** DRIVE (P7; Escape P9): start a task drag,
      Escape / release at origin. READBACK: `viewTasksData` order UNCHANGED, z
      back to baseline, `launchers` key byte-unchanged. Both sub-models. No
      golden.
      Commits:
- [ ] **A4 Cross-screen-move abort (split-brain).** DRIVE (P1,P5): begin a move
      toward B, drop BACK on the origin (or cancel mid-move). READBACK, the
      SPLIT-BRAIN catch stated as an assertion: `viewsData.screen` unchanged
      (still A), A's `publishedStruts` still reserved, and NO ghost view/struts
      on B (exactly one view, on A). Fully queryable - no golden. This is the
      "vacated but never claimed" strand turned into a readback assertion.
      Commits:
- [ ] **A5 Edit-mode no-op (PR #20 click-eating residue).** DRIVE: enter edit
      mode, exit WITHOUT changes. READBACK: `editMode`/`inConfigureAppletsMode`
      false; `inputRegionRects`/`maskRect` restored to the non-edit baseline;
      config byte-unchanged; and a BEHAVIORAL readback for click-eating - drive a
      `fakepointer click` at a point that was under the exited overlay and
      confirm it REACHES the applet beneath (a launcher activates / a task
      toggles, observed via `viewTasksData`/`activateTaskAt` effect), not
      swallowed. GOLDEN (justified - the one genuine visual-only residue): the
      exited overlay STILL DRAWING is a state-says-clean / pixels-show-overlay
      divergence no readback can express (the readback says clean, that IS the
      bug), so the post-exit crop must equal the baseline. The marquee golden of
      the suite. ~1 per (edge x vt) center (~8).
      Commits:
- [ ] **A6 Remove-undo abort.** DRIVE: `removeView`, then trigger UNDO within
      the window. READBACK, no golden: on undo the containment FULLY resurrects
      (back in `viewsData` with baseline geometry, applets intact, not a
      half-alive strand); counter-case, if it COMMITS, the group is FULLY gone
      (no `[Containments][<cid>]`, no orphan strut). Both branches asserted.
      GOTCHAS: 60s fallback; restart-inside-window resurrection; verify id first.
      Commits:

## 8. Chunk decomposition for the swarm

Ten infra chunks, twelve committed scenario chunks, six abort chunks: **28
chunks total.** Each is one Opus agent's single reviewable PR (branch, gate,
independent lean-Opus review, `gh pr merge --rebase`, re-resolve hashes, fetch).

### Infra chunks (each ships the HC3 observes-a-rejection acceptance test)
- [x] **C-I1 = P0** fixture generator + matrix harness + baseline backbone.
      `tests/e2e/matrix/fixture.py`, `tests/e2e/matrix/matrix-lib.sh`,
      `scripts/run-matrix.sh`, `tests/e2e/matrix-harness-selftest.sh` (HC3).
      Commits: (C-I1 branch, hash at merge)
- [ ] **C-I2 = P1** multi-output vehicle. Commits:
- [x] **C-I3 = P3** `addApplet` + applet-id order readback + XML + test. Commits: (branch worktree-agent-a102bc9cfc620c8c0; final hashes at PR merge)
- [ ] **C-I4 = P4** `removeApplet` + drop-marker/spacer readback + XML + test. Commits:
- [ ] **C-I5 = P5** `moveViewToScreen` + XML + test. Commits:
- [x] **C-I6 = P2** render-golden bridge (small set) + `--bless`.
      `tests/e2e/matrix/golden-bridge.sh` (`e2e_golden_compare`/
      `e2e_assert_golden`/`e2e_screenshot_crop`), `latte-imgdiff --tier`,
      `matrix_frame_equals_baseline` hook live, `tests/e2e/090-golden-bridge-selftest.sh` (HC3).
      Commits: (C-I6 branch; final hashes at PR merge)
- [ ] **C-I7 = P6** applet-reorder driver + `z`/stacking readback (commit+abort). Commits:
- [ ] **C-I8 = P7** task-reorder driver + window-task readback (commit+abort). Commits:
- [ ] **C-I9 = P8** widget-explorer DND driver (commit + non-drop abort). Commits:
- [x] **C-I10 = P9** fakepointer key injection. Commits: <this PR - `key <keysym>` verb (keyboard_keysym/xkbcommon) + tests/e2e/080-key-escape-cancels-move.sh>

### Committed scenario chunks
- [ ] **C-S1** F1 dock, single, 4 edge x 4 align (16; 8 novel) - a dock is not
      length-maximized, so all four alignments position distinctly. Commits:
- [ ] **C-S2** F1 panel, single, 4 edge x {full-span, justify} (8; 4 novel) -
      panel is length-maximized, so NO L/C/R alignment cells (PANEL-ALIGNMENT
      DEGENERACY). Commits:
- [ ] **C-S3** F1 dual chrome-on-2nd, dock+panel, 4 edge, center (8; 2 novel). Commits:
- [ ] **C-S4** F2 add, horizontal, dock+panel, {c,j}, single (8) + dual (2) = 10. Commits:
- [ ] **C-S5** F2 add, vertical, dock+panel, {c,j}, single (8). Commits:
- [ ] **C-S6** F3 applets, horizontal, dock+panel, {c,j}, single (8) + dual (2) = 10. Commits:
- [ ] **C-S7** F3 applets, VERTICAL (buggy path, own chunk), dock+panel, {c,j} (8). Commits:
- [ ] **C-S8** F4 tasks, 4 edge, dock+panel, center, single (8) + dual (2) = 10. Commits:
- [ ] **C-S9** F5 move, dock, 4 edge, {c,j}, dual (8). Commits:
- [ ] **C-S10** F5 move, panel, 4 edge, {c,j}, dual (8). Commits:
- [ ] **C-S11** F6 widget remove horizontal + removeView single (8+4 = 12). Commits:
- [ ] **C-S12** F6 widget remove vertical + dual strand (8+4 = 12). Commits:

### Abort scenario chunks (highest-value; interleave EARLY, never last)
- [ ] **C-A1** A1 add abort, dock+panel, 4 edge, {c,j}, single (16) + dual (2) = 18. Commits:
- [ ] **C-A2** A2 applet-reorder abort, dock+panel, 4 edge, {c,j} (16) + dual (2) = 18. Commits:
- [ ] **C-A3** A3 task-reorder abort, dock+panel, 4 edge, center (8) + dual (2) = 10. Commits:
- [ ] **C-A4** A4 move abort / split-brain, dock+panel, 4 edge, {c,j}, dual (16). Commits:
- [ ] **C-A5** A5 edit no-op / PR#20 guard, dock+panel, 4 edge, center (8) + dual (2) = 10. Commits:
- [ ] **C-A6** A6 remove-undo abort, dock+panel, 2 edge, center (4) + dual (2) = 6. Commits:

### Merge order and parallelism
**Gate on everything:** C-I1 (harness + baseline) and C-I6 (golden bridge) block
all eighteen scenario chunks.

**Per-family gates:** C-I3 -> C-S4,C-S5,C-A1. C-I4 -> C-S11,C-S12,C-A1(spacer),
C-A2(z). C-I5 -> C-S9,C-S10,C-A4. C-I7 -> C-S6,C-S7,C-A2. C-I8 -> C-S8,C-A3.
C-I9 -> DND leg of C-S4,C-S5,C-A1. C-I10 -> Escape sub-path of C-A2,C-A3,C-A5.
C-I2 -> all dual tails + F5/A4.

**Waves:**
- Wave 1 (<=4): C-I1, C-I3, C-I4, C-I5, C-I10.
- Wave 2 (<=4): C-I2, C-I6 (needs C-I1), C-I7 (needs C-I1), C-I8 (needs C-I1).
- Wave 3: C-I9 (needs C-I1, maybe C-I2), hardest.
- Wave 4+ (scenarios, <=4, gated by C-I1+C-I6 + family/dual/abort deps). **Abort
  chunks are scheduled here AS SOON AS their infra lands, INTERLEAVED WITH the
  committed chunks (HC4), not after.** C-A5 and C-A6 need only C-I1+C-I6
  (+C-I10 for A5's Escape variant; removeView exists for A6), so they are among
  the FIRST scenario chunks to merge. C-A2 lands with C-S6/C-S7 (C-I7); C-A1
  with C-S4/C-S5 (C-I9/C-I3/C-I4); C-A4 with C-S9/C-S10 (C-I5/C-I2); C-A3 with
  C-S8 (C-I8).

**Max concurrency = 4 agents** (the established cap). Nested kwin + lavapipe is
CPU-heavy and podman/CPU are shared: a gate/e2e timeout under concurrent load is
LOAD, not a defect - verify solo before calling a failure. Concurrency is in the
AUTHORING; merges stay serial (gate-stamp -> independent review -> rebase-merge).

## 9. D-Bus surface: what each verb uses, and the readback-gap list (grows per HC2)

Existing readbacks that carry assertions: `viewsData` (editMode,
inConfigureAppletsMode, screen, onPrimary, screenGeometry, publishedStruts,
strutsThickness, inputRegionRects, maskRect, isHidden, isOffScreen, inStartup),
`viewAppletsData`/`viewAppletsOrder`, `viewTasksData`, `e2e_dumpwins`
(chrome-window geometry, layer, output), `e2e_assert_geometry_agrees`
(render-vs-reported).

Existing actions used: `setViewEditMode`, `setViewKeyboardNavigation`,
`removeView`, `duplicateView`, `activateTaskAt`, kglobalaccel
`show view settings`.

**Readback-gap list to ADD (HC2 corollary - each closes a would-be golden and
lands in the three required places + `dbusreportstest`):**

- [x] G1 applet-id order in `viewAppletsData`/`viewAppletsOrder` (disambiguate
      same-plugin applets). Retires ambiguity in F2/F3/A1/A2. (In C-I3/P3.)
      `viewAppletsData` already carried the stable per-applet instance `id` in
      visual order; the gap was `viewAppletsOrder`, which interleaved
      justify-splitter sentinels (`JUSTIFYSPLITTERID = -10`) and was
      mislabeled "plugin ids" in the docs. Now both are splitter-free and
      agree, via the pure `DbusReports::appletIdOrder` helper (pinned by
      `dbusreportstest`); the e2e recipe asserts the two order readbacks match.
      Commits: (branch worktree-agent-a102bc9cfc620c8c0; final hashes at PR merge)
- [ ] G2 `z`/stacking order in `viewAppletsData` (and `viewTasksData`). Retires
      the stuck-over-chrome golden for F3/F4/A2/A3 (`480ae30e3` residue becomes
      queryable). (In C-I7/P6.) Commits:
- [ ] G3 live drop-marker / `dndSpacerIndex` state readback. Retires the
      half-inserted-placeholder golden for A1/A2 - the direct `insert(-1)`
      observability. (In C-I4/P4.) Commits:
- [ ] G4 window-task order readback confirmation/addition in `viewTasksData`
      (`index`/`appId` stable after reorder). For F4/A3 window sub-model. (In
      C-I8/P7.) Commits:
- [ ] G5 (candidate, flagged not committed) edit-chrome sub-item geometry
      (ruler/toggle rects). Would retire the F1 blueprint/toggle golden; larger
      surface, decide in review (O-gap). Commits:

**Mutating actions to ADD (coarse user actions, section 5):** `addApplet` (P3),
`removeApplet` (P4), `moveViewToScreen` (P5).

Goldens that SURVIVE HC2 (the only blessed images): F1 blueprint-grid render
(~8, edge x vt centered), F2/F3 justify pixel distribution (~8), A5 PR#20
ghost-overlay residue (~8-10), and any A1 justify placeholder band the G3
readback cannot fully express (<=4). Roughly three dozen goldens total, versus
one-per-scenario. Everything else asserts by readback.

## 10. Open questions / risks

- **O1 Full vs trimmed matrix (HC1).** 196 total, ~106 novel, ~90 coverage-only.
  Confirm the coverage-only cells are cheap parametrized repeats to include, or
  should they be DEFERRED (novel-only first, ~106)? justify/abort/vertical are
  never coverage-only.
- **O2 New mutating actions + growing readback surface (HC2).** `addApplet`,
  `removeApplet`, `moveViewToScreen`, plus G1-G4 readbacks. Each mirrors a real
  coarse user action / is observability-first. Confirm the surface growth is
  wanted over golden-around-it; confirm G5 (chrome sub-item geometry) in or out.
- **O3 Golden brittleness / merge tier.** Even the small live-dock golden set
  may not reach bit-exact (cursor, wallpaper, real-time animation vs sceneprobe's
  deterministic clock). Gate the interaction goldens at Tolerance while
  sceneprobe stays BitExact, or invest in a frozen live animation clock? The set
  is small (~3 dozen), so maintenance is bounded either way.
- **O4 Does the suite join `gate-all.sh`?** `run-e2e` is not in `gate-all`
  today. Recommend a fast tier-1 subset (one novel cell per verb incl. the
  highest-value aborts A2-vertical, A4-split-brain, A5) in `gate-all` as the
  per-commit gate, the full 196 as a periodic / pre-release gate in the
  multi-distro matrix. Confirm split + tier-1 membership.
- **O5 Widget-explorer DND: EXPECTED DOABLE (Bree directive, 2026-07-18), not an
  open question.** The Phase 7 "defer to a GUI CI vm" note is STALE - it predates
  the nested vehicle (kwin_wayland + fakepointer press-glide-release + a live
  client surface in ONE compositor), which is exactly the infra a Wayland
  explorer->containment DND needs. P8 implements REAL DND. A degrade to xfail is a
  last resort only if PROVEN infeasible after honest effort, with the blocking
  root cause recorded - never an assumed skip. C-I9's acceptance bar (HC3 rejected
  drop) requires the driver to actually drive DND, so xfail is not a shortcut.
- **O6 Window-task fixtures.** F4/A3 window sub-model needs real reorderable
  client windows. Confirm spawning N deterministic throwaway windows, or scope to
  launcher reorder only (window-task = live-desk).
- **O7 Multi-output view-assignment mapping.** `ScreenPool` name<->output under
  `--virtual --output-count 2` is unproven; P1 needs a discover-and-pin step
  before any dual scenario trusts placement. Biggest schedule risk in the
  dual/F5/A4 half.
- **O8 Abort input mechanics: Escape vs release.** Escape-cancel (drag CANCEL,
  needs P9) and release-at-origin / release-over-nothing (a no-op drop) exercise
  DIFFERENT code. Plan runs both where P9 exists. Confirm both wanted, or pick
  Escape as canonical (more residue-prone, better regression target).
