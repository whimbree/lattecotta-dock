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
- [ ] **P2 Render-golden bridge (small set).** Blocks the render-golden half of
      the visual-only scenarios (section 7). Wire lavapipe + fixed wallpaper +
      cursor-park. Add `e2e_assert_golden <cell> <verb> <phase>` (crop ->
      `latte-imgdiff` at the tier tolerance -> save actual/expected/diff on
      mismatch) and the golden-equality helper the abort backbone calls. Add
      `--bless`. Acceptance (observes a rejection): self-test that a deliberately
      WRONG golden FAILS the compare and a MISSING golden FAILS on the lavapipe
      tier (mirror sceneprobe `selftest-bad`/`selftest-blank`). Depends on P0.
      Commits:
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

These 78 abort cells are the CONFIG-MATRIX abort coverage. The section 7
adversarial abort target matrix ADDS ~30 novel residue-path scenarios on top
(each on one representative cell, all novel by class), lifting the abort total to
~108 and the grand total to ~228, and splitting the two richest abort legs (add,
applet-reorder) into config-matrix + adversarial-target chunks (section 8's
C-A1b, C-A2b).

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

#### Adversarial abort target matrix (grounded in the drop code)

The polite aborts A1-A6 already carry (Escape, release-at-origin) exercise the
drop code's EARLY-OUT paths, which are by construction the least fragile arms in
the machinery. The dangerous residue lives where the code TRIES to honor a
malformed-but-plausible drop: it computes a target index, mutates layout state,
and only then discovers the drop was nonsensical (the `insert(-1)` class). This
matrix enumerates those targets, each anchored to the exact handler it stresses
and the residue it would strand. Targets are NOVEL by class (each is a distinct
residue path); per HC1 each runs on its family's single highest-value
representative cell (justify on a VERTICAL edge, where `insertAtCoordinates`'
three-zone distance fallback and the length-axis reorder math are busiest), not
multiplied across the 128-cell config matrix. A family's config-matrix abort
cells (section 6) stay as-is; these targets ADD to that family's novel count.

**Shared code map (the handlers every target below stresses):**
- **CONTAINMENT ADD DND:** `DragDropArea.qml` onDragEnter/Move/Leave/Drop ->
  `LayoutManager::insertAtCoordinates` (layoutmanager.cpp:1121) ->
  `insertAtLayoutCoordinates` (821). insertAtCoordinates NEVER refuses: when the
  point hovers no layout it falls through to a nearest-distance heuristic
  (1151-1187) that places the spacer by min tail/head distance to
  start/main/end. There is no "off-target, refuse" arm in the C++; the only
  refusal is the DropClassifier ladder in QML (`containmentDropAction`,
  dropclassifier.h:243).
- **THE SPACER (G3):** `dndSpacerIndex()` (layoutmanager.cpp:1000) reports the
  spacer's slot, -1 when it is parented to root/containment (clean). It is set
  live by insertAtCoordinates and cleared by onDragLeave (DragDropArea.qml:152),
  the 100ms `clearInfoTimer` (70-86), and the edit-exit rescue (main.qml:403).
  onDrop does NOT reparent the spacer (only `dndSpacer.opacity = 0`, line 201),
  so a completed drop leaves it parented until the timer. FINDING:
  dndSpacerIndex line 1002 dereferences `m_dndSpacer` with no null guard; the G3
  readback surface must guard it or a query issued before any drag has created
  the spacer crashes the dock.
- **THE MASQUERADE:** onDrop reads dndSpacerIndex, encodes it to a fake negative
  point (`indexToMasquearadedPoint`, 601), hands that to `processMimeData`;
  `onAppletAdded` (main.qml:547) decodes it and calls `addAppletItem(applet,
  index)` (1211), which guards `index < 0` (early return, applet left unplaced)
  and appends when `index >= count`. A stale spacer index between drop and
  applet-added lands the applet at the wrong slot.
- **APPLET REORDER:** `ConfigOverlay.qml` MouseArea onPressed/PositionChanged/
  Released. onReleased (201) ALWAYS commits (insertBefore(placeHolder,
  currentApplet), z=1, save); there is NO abort branch. Press lifts the applet
  to `root` at z=900 (194-198); only onReleased restores it. The MouseArea has
  no `acceptedButtons` (LeftButton only) and no `Keys`/Escape handler.
- **TASK REORDER:** `MouseHandler.qml` DropArea. `MoveTaskReorder` mutates
  `tasksModel.move` LIVE during onDragMove (184) and sets `dragSource.z = 100`
  (180); onDrop for an internal move is `LeaveUnchanged`/ignore
  (dropclassifier.h:263) and reverts NOTHING. The 200ms `ignoreItemTimer` (50)
  is the only anti-jitter.
- **CROSS-SCREEN:** `positioner.cpp` relocation is hide -> set
  m_nextScreen/m_nextScreenName (890) -> screenChanged confirm (835) ->
  hidingForRelocationFinished apply (877). Those pending-move members are the
  split-brain state, and the wayland `confirmedgeometry` shortcut (843) skips the
  geometry-confirm gate.
- **EDIT OVERLAY MASK:** `inputmaskflush.h` holds the window mask at the union of
  bands during a LENGTH-axis shrink and collapses on a settle timer in Effects;
  `needsSettleCollapse` (97) gates the collapse. The edit overlay leaves only the
  toggle rects live (PR #20).

**T1 WRONG-PLACE-IN-DOCK**
- **T1a onto an OCCUPIED applet** (insert-at-occupied): drop squarely on another
  applet's center. Stresses insertAtLayoutCoordinates' midpoint branch (879-884)
  and its degenerate `width()>1`/`height()>1` guard - a hovered item caught
  mid-collapse (width<=1) always routes `insertAfter` regardless of side.
  Residue: applet on the wrong side of the occupant, an off-by-one in
  `viewAppletsOrder`. Assert: appletOrder + applet-id order (G1) byte-identical
  to baseline on abort.
- **T1b on a JUSTIFY SPLITTER or exactly ON a zone boundary** (the
  insert(-1)/off-by-one territory): drop at the start/main or main/end seam.
  Stresses insertAtCoordinates' justify branch (1131-1145, start then end then
  main) and save()'s splitter recompute (666-668) feeding
  `JustifySplitters::insertSplitters` (justifysplitters.h:94). Residue: a stale
  splitterPosition/splitterPosition2 persisted; on the next restore
  `repairPositions` logs "out of range ... repairing to centered"
  (layoutmanager.cpp:398), a visible migration the abort must NOT provoke.
  Assert: splitterPosition/splitterPosition2 in config byte-unchanged, NO repair
  qWarning in the log.
- **T1c PAST THE LAST SLOT** (overflow/clamp): drop beyond the final applet.
  Stresses the distance fallback (1169-1187, tail-vs-head) and addAppletItem's
  `index >= count` append (1235). Residue: applet at a phantom trailing index or
  a gap. Assert: order + count identical.
- **T1d a TASK dropped OUT of the Tasks applet onto a containment neighbor:** the
  task drag carries the taskbutton mime; classifyContainmentDrag sets isTask
  (dropclassifier.h:148), containmentDropAction returns Ignore (246), and onDrop
  leaves the spacer as-is (177-180, "Qt5 returned early here"). Residue: the
  spacer strands in the neighbor's layout until the 100ms timer; a re-enter that
  stops the timer lingers it. Assert: dndSpacerIndex()==-1 (G3) after settle;
  appletOrder unchanged.
- **T1e an applet dropped INTO the SYSTRAY interior:** the systray is its own
  containment; its DropArea consumes the drop and Latte's onDrop never fires
  (cross-target, see T3). Residue would be a Latte spacer opened with no matching
  onDrop to close it. Assert: G3 clean, Latte appletOrder unchanged.
- **T1f onto the EDIT CHROME** (ruler, Add-Widgets button, settings window): the
  settings window is a separate KWin surface (a foreign-surface drop, T2b); the
  in-canvas ruler/toggle carry no DropArea. Assert: the drop is a no-op (order
  unchanged, no spacer opened).
- **T1g onto a STALE popup/tooltip window** (the click-eater surface): a
  QQC2.ToolTip / preview surface stranded over a live rect (the PR #20 class). A
  drop or click landing on it is eaten. Assert (behavioral, shared with A5): the
  drop/click reaches the item beneath.

**T2 OFF-THE-DOCK-ENTIRELY**
- **T2a release on EMPTY DESKTOP / at OFF-SCREEN coords:** pointer released
  outside every layout. For add DND, onDragLeave (DragDropArea.qml:152) should
  have fired on exit and cleaned the spacer; the adversarial case is a release
  WITHOUT a preceding leave (teleport out), which if still inside root lands in
  the distance fallback. Residue: an orphan spacer (G3 != -1) or a phantom
  insert. Assert: nothing added/removed, G3 clean, no gap.
- **T2b release on a FOREIGN WINDOW** (another app, the settings window): the
  drop crosses to a surface Latte does not own. Assert: appletOrder/count
  unchanged, spacer clean.
- **T2c the SPACER-CLEANUP stress (widget-add):** drag from the explorer, hover
  IN to SHOW the spacer (dndSpacerIndex>=0), then drag back OUT and release
  off-dock. Stresses whether onDragLeave/clearInfoTimer actually reset the spacer
  parent. An orphan spacer here is pure residue. Assert: dndSpacerIndex()==-1
  (G3), appletOrder byte-identical.

**T3 CROSS-TARGET / CROSS-VIEW**
- **T3a onto a DIFFERENT dock/view then abort:** start on view A, cross to view
  B's surface, release/Escape. Each view has an independent DragDropArea and its
  own m_dndSpacer; a spacer opened on B must clean on B. Assert: both views'
  appletOrder unchanged, both G3 clean.
- **T3b DUAL-DISPLAY move toward B, waypoint, drop BACK on A** (split-brain
  "vacated but never claimed"): stresses positioner's pending-move members
  (m_nextScreen/m_nextScreenName, positioner.cpp:890) and the wayland
  confirmedgeometry shortcut (843). Residue: struts vacated on A but never
  republished (A4's core). Assert: viewsData.screen still A, A's publishedStruts
  intact, no struts / ghost view on B.
- **T3c a TASK dragged out of Tasks onto the containment/systray and back** (the
  cross-target of the task sub-model, T1d/T1e for the tasks path).

**T4 TIMING / STATE-TRANSITION COMBOS** (driver DR-4)
- **T4a abort mid-drag DURING autohide HIDE/REVEAL:** fire a visibility
  hide/reveal mid-glide. Stresses the thickness-axis mask shrink (inputmaskflush,
  deliberately NOT held) racing the drag grab. Residue: a dropped drag or a
  spacer left in a hidden dock. Assert: on reveal, G3 clean, order unchanged.
- **T4b abort DURING MAXIMIZE-LENGTH or ZOOM-SETTLE:** spawn/maximize a client
  (maximize-length path) or unhover (zoom-out) mid-drag, then abort. Stresses
  `windowMaskFor`'s length-axis union hold (inputmaskflush.h:62-92) and
  `needsSettleCollapse` (97): an abort that interrupts the settle collapse leaves
  the WINDOW mask over-wide. Residue: inputRegionRects/maskRect wider than the
  settled band (input captured across the vacated band, the stale frosted band
  drawn). Assert: after settle, maskRect == the non-drag band (readback) AND the
  post-settle crop equals the clean baseline (the frosted-band golden, the one
  visual-only residue, shared with A5).
- **T4c EXIT EDIT-MODE mid-drag (applet reorder):** trigger `setViewEditMode
  false` while ConfigOverlay holds currentApplet at z=900 parented to root.
  onReleased never runs; onEditModeChanged (main.qml:387-405) rescues dndSpacer
  but NOT the ConfigOverlay placeHolder/currentApplet. Residue: an applet stuck
  at z=900 parented to root, DROPPED from appletOrder (save() walks only the
  three layouts, so the applet vanishes from the order). Assert: appletOrder
  count+order restored (applet back in a layout), z readback (G2) at baseline.
  HIGH-VALUE A2 target.
- **T4d DRAG SOURCE REMOVED mid-drag:** removeApplet the dragged applet mid-glide
  (a second path deletes it). Stresses currentApplet going null under
  ConfigOverlay and dragSource dying under MouseHandler. Residue: a dangling
  currentApplet, placeHolder/spacer never cleared. Assert: no crash,
  placeHolder/spacer clean, order reflects only the removal.
- **T4e SECOND DRAG before the first settles** (driver DR-5): start drag B before
  A's save/settle. Stresses re-entrancy of the single m_dndSpacer / single
  currentApplet. Assert: exactly one coherent order, one spacer, no double
  insert. (Advanced; desk-check fallback if headlessly infeasible, root cause
  recorded per HC3.)

**T5 DEGENERATE INPUT**
- **T5a ZERO-DELTA drag:** press and release without moving. The true no-op
  baseline: task reorder MUST NOT move (insertIndexAt inert), applet reorder
  onReleased returns the applet to origin. Assert: order + z + config identical.
- **T5b DIRECTION-REVERSAL JITTER** (out->back->out->release, driver DR-2):
  stresses the 200ms ignoreItemTimer (MouseHandler.qml:50) and SuppressRepeatTarget
  (dropclassifier.h:387). For task reorder, note the LIVE-move truth: a reversal
  that crosses a neighbor commits tasksModel.move immediately and does NOT revert
  on the way back unless the reverse re-crosses. Assert: final order matches the
  NET crossings; launchers key reflects only net moves.
- **T5c OFF-BY-ONE BOUNDARY COORDINATE:** release one pixel inside/outside a
  midpoint, or at distance 0. Stresses insertIndexAt's `ceil(distance/step)-1` =
  -1 at distance 0 (dropclassifier.h:316), which TasksModel::move rejects
  (inert, preserved not clamped). Assert: NO move to -1, order unchanged.
- **T5d TELEPORT vs GLIDE (parabolic shift):** a single-step teleport skips the
  intermediate childAt() sampling a 24-step glide hits. Stresses whether the
  reorder tracks the same target under both. Assert: same final order; on abort,
  baseline.
- **T5e RIGHT/MIDDLE-CLICK mid-drag:** the ConfigOverlay MouseArea accepts
  LeftButton only (no acceptedButtons); a right/middle press during the left-drag
  is not consumed here and may reach a context menu. Assert: the reorder still
  resolves on left-release with no residue, OR a context menu opens without
  stranding currentApplet at z=900.
- **T5f MODIFIER HELD (Ctrl/Shift copy-vs-move, driver DR-3):** hold Ctrl/Shift
  across the drag. Latte's paths do not distinguish a copy action
  (`event.proposedAction`, DragDropArea.qml:198). Assert: the abort leaves no
  residue regardless of modifier; a copy-modified add abort still adds ZERO.

**ESCAPE-vs-RELEASE, grounded** (supersedes the assumption in O8): the
ConfigOverlay applet-reorder MouseArea has NO Keys/Escape handler, so Escape
never reaches it. The A2 "Escape sub-path" is therefore NOT a given cancel:
Escape hits the view / KeyboardNavigationHandler and most likely EXITS edit mode
(T4c), which STRANDS the ConfigOverlay residue rather than cleanly cancelling.
The task-reorder MouseHandler is a real DragDrop drag the compositor CAN cancel
with Escape, but the LIVE `tasksModel.move` already applied is not reverted. Each
abort driver's HC3 acceptance must OBSERVE Escape's REAL effect on its path
(clean cancel, no-op, or edit-exit-with-residue), never assume "Escape cancels".

**Driver capabilities the abort matrix needs** (flagged for the named infra
chunk; these ADD to that chunk's definition of done, they do not change its
config-matrix scope - do not edit the infra tick lines to record them, they land
with the chunk's own work):
- **DR-1 drop-at-arbitrary-target release** (explicit x,y; over a named foreign
  window from dumpwins; a nested systray containment; off-screen coords) -> C-I9
  extends its release primitive, C-I1 resolves the target rect. Needed by
  T1a/b/c/d/e, T2a/b, T3a.
- **DR-2 reverse-jitter glide** (press, out->back->out waypoints, release) ->
  composes fakepointer's existing multi-waypoint drag/glide; a
  `matrix_reverse_jitter` wrapper in C-I7 (applets) / C-I8 (tasks). No new
  fakepointer verb.
- **DR-3 modifier-hold** (Ctrl/Shift across a drag) -> fakepointer has no
  modifier axis; extend the C-I10 keyboard axis with keydown/keyup (press-and-hold
  a modifier keysym). Needed by T5f.
- **DR-4 mid-drag state-transition hook** (run a D-Bus action or spawn/maximize a
  window BETWEEN glide waypoints) -> a `matrix_mid_drag_hook` seam in C-I1,
  driving existing actions (setViewEditMode for T4c, removeApplet for T4d, a
  maximized client for T4b, a visibility toggle for T4a). Needed by T4a-d.
- **DR-5 second-concurrent-drag** -> C-I7/C-I8; advanced, desk-check fallback if
  headlessly infeasible with the root cause recorded (HC3 still requires
  observing the negative outcome).
- **DR-6 escape-within-a-held-pointer-drag** (press + `key Escape` + release in
  ONE tool invocation) -> C-I7/C-I8 build on C-I10; the standalone 080 recipe
  proved a key cancels a KWin interactive move, NOT a MouseArea/DropArea drag
  with the button still held. Its acceptance must resolve the ESCAPE-vs-RELEASE
  finding above.

**Candidate readback** (per HC2, a missing readback is a signal to ADD one):
- **G6** (candidate, flagged not committed) relocation-pending state (positioner
  m_nextScreen/m_nextScreenName/m_nextScreenEdge) would make A4's mid-relocation
  split-brain queryable instead of timing-dependent; decide in review alongside
  G5.
- **G3 hardening:** the dndSpacerIndex null-guard finding above is a prerequisite
  for A1/A2 to query the spacer safely; it lands with G3 in C-I4/P4.

**Residue surface per abort (harness-hardening requirement, C-I1).** The
`matrix_probe_config` residue detector snapshots ONLY the layout file
(`$E2E_LAYOUT`), so an abort that strands residue OUTSIDE it false-passes green.
Each abort scenario must name the surface(s) its residue could land in, and any
that leaves the layout file MUST extend the snapshot. Three surfaces exist:
- **LAYOUT FILE** (covered): the containment config group carries `appletOrder`,
  `splitterPosition`/`splitterPosition2`, `lockedZoomApplets`/
  `userBlocksColorizingApplets`, and the view's screen assignment. A1/A2/A6
  residue is here.
- **VIEW RECORD** (covered by D-Bus readback, not a file): `editMode`,
  `inputRegionRects`/`maskRect`, `publishedStruts`, z-order (G2), dndSpacerIndex
  (G3). A2/A3/A5 runtime residue is here.
- **OFF-LAYOUT CONFIG (NOT covered - the gap):**
  - **`lattedockrc`** carries universal settings and the ScreenPool
    `[ScreenConnectors]` name<->connector map (screenpool.cpp:140-145). A4 and A6
    can strand a PHANTOM screen connector here on a split-brain move / a view
    removal; A5 can strand a stale `inConfigureAppletsMode`, which universalSettings
    persists via `saveConfig` on change (universalsettings.cpp:50). These three
    families MUST also snapshot `lattedockrc` in their baseline/assert.
  - **Another applet's config group** (a distinct group inside the layout file,
    NOT the containment's `appletOrder`): the tasks applet's `launchers` key
    (plasmoid config main.xml:21) for A3, and the stealing-applet launchers key
    for A1's add-launchers path. If `matrix_probe_config` reads only the
    containment's `appletOrder` key rather than the whole file, these are missed;
    A3/A1 MUST snapshot the tasks-applet config group via the
    `MATRIX_VOLATILE_EXTRA` seam.
  This is a HARNESS-HARDENING prereq on C-I1: extend `matrix_probe_config` to
  optionally snapshot `lattedockrc` and accept a `MATRIX_VOLATILE_EXTRA` list of
  additional config groups/files, so A3/A4/A5/A6 assert on the surface their
  residue actually lands in rather than on the layout file alone.

**Scenario growth:** each family's target list below is single-representative-cell
(justify + a vertical edge) per HC1, so the matrix adds roughly 30 novel
target scenarios (A1 ~7, A2 ~9, A3 ~5, A4 ~4, A5 ~4, A6 ~3) on top of the 78
config-matrix abort cells, lifting the abort total to ~108 and the grand total
to ~228. The two richest legs (add, applet-reorder) split into a config-matrix
chunk plus an adversarial-target chunk (C-A1b, C-A2b), taking the abort chunks
from six to eight and the total from 28 to 30 (section 8 re-balances the waves).

- [ ] **A1 Widget-add abort.** DRIVE (P8): explorer drag, release over a
      NON-drop zone. READBACK: applet count and `viewAppletsOrder` byte-identical
      to baseline; the drop-marker/spacer readback (P4) reads CLEAN (no stranded
      `dndSpacerIndex`) - the direct `insert(-1)` observability, no golden.
      GOLDEN only if the spacer readback cannot fully express a half-rendered
      placeholder band (justify cells): crop equals baseline. This is the
      readback-gap-becomes-surface case in action.
      ADVERSARIAL TARGETS (each on the justify + vertical-edge representative,
      per the matrix above): **T2c** the spacer-cleanup stress (hover in to open
      the spacer, drag back OUT, release off-dock) - the canonical orphan-spacer
      case, assert dndSpacerIndex()==-1; **T2a** teleport-out release with no
      preceding onDragLeave (the distance-fallback at insertAtCoordinates:1151
      must not phantom-insert); **T2b** release on the settings window / a
      foreign surface; **T1a** release on an OCCUPIED applet's center (the add
      never overwrites, count unchanged); **T1d** a TASK dragged out of Tasks
      onto this containment - containmentDropAction Ignore (dropclassifier.h:246)
      leaves the spacer as-is (DragDropArea.qml:177), assert G3 clean after the
      100ms timer, NOT stranded by a re-enter that stops the timer; **T1e** an
      applet released into the systray interior (cross-target, systray's own
      DropArea eats it, Latte spacer must still close); **T5f** a Ctrl/Shift-held
      copy-modified add abort still adds ZERO (no ng double-create even with a
      copy action). RESIDUE / NO-RESIDUE: appletOrder + applet-id order (G1)
      byte-identical, count unchanged, `dndSpacerIndex()==-1` (G3, null-guarded
      per the finding above), no repair qWarning, config byte-unchanged; the
      justify placeholder-band golden only where G3 cannot express the half-drawn
      spacer. RESIDUE SURFACE: layout file + G3 runtime for the plain add; the
      T1d/stealing-applet launcher-drop leg strands into the tasks-applet
      `launchers` group (addDroppedLaunchersInStealingApplet), so that leg's
      assert snapshots the tasks-applet config group via `MATRIX_VOLATILE_EXTRA`.
      DRIVER: DR-1 (arbitrary-target release, C-I9); split into C-A1b.
      Commits:
- [ ] **A2 Applet-reorder abort.** DRIVE (P6; Escape sub-path P9): start a
      reorder, Escape (drag-CANCEL) or release at origin. READBACK:
      `viewAppletsOrder` UNCHANGED (watch a SUBTLE off-by-one, the half-applied
      swap), z back to baseline (no applet over chrome). GOLDEN: none (both
      residues are now readback). Run both sub-paths where P9 exists. Vertical
      edges highest-value.
      ADVERSARIAL TARGETS (ConfigOverlay MouseArea; onReleased always commits and
      has no abort branch, so the residue is in the paths that BYPASS onReleased):
      **T4c** EXIT edit-mode mid-drag (the marquee A2 target) - setViewEditMode
      false while the applet is lifted to root at z=900, onEditModeChanged
      (main.qml:387) rescues dndSpacer but NOT the ConfigOverlay
      placeHolder/currentApplet, so the applet DROPS out of appletOrder (save()
      walks only the three layouts); assert appletOrder count+order restored and
      z (G2) at baseline; **T4d** the dragged applet REMOVED by another path
      mid-drag (currentApplet goes null) - assert no crash, placeHolder cleared,
      order reflects only the removal; **T1a** release on an OCCUPIED applet
      (insertAtLayoutCoordinates midpoint + the width<=1 degenerate-hovered
      guard, 879) - assert no off-by-one; **T1b** release exactly on a justify
      SPLITTER / zone boundary - assert splitterPosition/2 config byte-unchanged,
      NO "repairing to centered" qWarning (layoutmanager.cpp:398); **T1c** release
      PAST the last slot (distance fallback + index>=count append); **T5b**
      reverse-jitter (out->back->out->release); **T5e** right/middle-click
      mid-drag (MouseArea is LeftButton-only, no acceptedButtons) - assert the
      left-release still resolves and no applet strands at z=900; **T5f** modifier
      held; **T5a** zero-delta. ESCAPE FINDING: prove Escape's REAL effect here
      first - the MouseArea has no Keys handler, so Escape likely exits edit mode
      (= T4c residue), NOT a clean cancel; the driver's HC3 acceptance must
      distinguish. RESIDUE / NO-RESIDUE: `viewAppletsOrder` + applet-id order (G1)
      byte-identical, z (G2) baseline (no applet parented to root / over chrome),
      splitterPosition/2 byte-unchanged, config byte-unchanged. DRIVERS: DR-4
      (mid-drag hook, C-I1), DR-6 (escape-in-held-drag, C-I7+C-I10), DR-2
      (reverse-jitter, C-I7); split into C-A2b.
      Commits:
- [ ] **A3 Task-reorder abort.** DRIVE (P7; Escape P9): start a task drag,
      Escape / release at origin. READBACK: `viewTasksData` order UNCHANGED, z
      back to baseline, `launchers` key byte-unchanged. Both sub-models. No
      golden.
      LIVE-MOVE TRUTH (grounds this whole family): MouseHandler mutates
      `tasksModel.move` DURING onDragMove (MouseHandler.qml:184), not on drop;
      onDrop for an internal move is LeaveUnchanged/ignore (dropclassifier.h:263)
      and reverts NOTHING. So "order UNCHANGED" holds ONLY for a drag that never
      crossed a neighbor's midpoint. A CROSS-then-abort is the adversarial case:
      the move already committed (and the `launchers` key already rewrote), and
      neither Escape nor release restores it. The plan must assert the ACTUAL
      contract, not a wished revert.
      ADVERSARIAL TARGETS: **T5a** zero-delta / release-before-cross - the ONLY
      true no-op, assert order + launchers byte-unchanged; **T5b** reverse-jitter
      within the 200ms ignoreItemTimer window (MouseHandler.qml:50) - assert the
      final order equals the NET crossings and the launchers key reflects only
      net moves (a reversal that re-crosses back is a net-zero that MUST land
      byte-identical); **T5c** off-by-one / distance-0 boundary - insertIndexAt
      answers -1 (dropclassifier.h:316), TasksModel::move rejects it, assert NO
      move to -1; **T5d** teleport vs 24-step glide to the same target - same
      final order; **T4c/T4d** edit-exit or task-closed mid-drag; **T1d/T3c** a
      task dragged OUT of the Tasks applet onto the containment/systray and back
      (cross-target); window-task sub-model AND launcher sub-model both. RESIDUE
      / NO-RESIDUE: `viewTasksData` order (+ window-task order G4) matches the net
      crossings, z (G2) back to baseline (dragSource.z=100 reset on
      Drag.onDragFinished, 480ae30e3, must not stick), `launchers` key reflects
      only net moves. RESIDUE SURFACE: the `launchers` key is the TASKS-APPLET
      config group (plasmoid main.xml:21), NOT the containment `appletOrder` - the
      assertion MUST snapshot that group via `MATRIX_VOLATILE_EXTRA`, or a
      launcher-order strand on a cross-then-abort false-passes. ESCAPE FINDING: a
      DragDrop drag IS compositor-cancelable by
      Escape, but the already-applied move stays - the acceptance must observe
      that, not assume a revert. DRIVER: DR-2 (reverse-jitter, C-I8).
      Commits:
- [ ] **A4 Cross-screen-move abort (split-brain).** DRIVE (P1,P5): begin a move
      toward B, drop BACK on the origin (or cancel mid-move). READBACK, the
      SPLIT-BRAIN catch stated as an assertion: `viewsData.screen` unchanged
      (still A), A's `publishedStruts` still reserved, and NO ghost view/struts
      on B (exactly one view, on A). Fully queryable - no golden. This is the
      "vacated but never claimed" strand turned into a readback assertion.
      ADVERSARIAL TARGETS: **T3b** the waypoint-drop-back-on-A (glide toward B,
      waypoint over B's rect, release back on A) - stresses positioner's
      pending-move members (m_nextScreen/m_nextScreenName, positioner.cpp:890)
      and the wayland `confirmedgeometry` shortcut (843) that skips the
      geometry-confirm gate, so a screenChanged that fires mid-abort can finalize
      a move the drop-back was meant to cancel; assert m_nextScreen cleared (via G6 if committed,
      else via viewsData.screen + publishedStruts staying on A); **T4a** abort
      DURING the relocation HIDE (hidingForRelocationStarted has fired, the view
      is sliding out) - assert the view slides back IN on A, not stranded hidden
      (`isHidden`/`isOffScreen` false, `inStartup` not stuck); move to a
      NON-EXISTENT output must be REFUSED (P5 acceptance, view stays put); a
      cloned-view move (Phase 8 applet-order sync open) if a clone exists.
      RESIDUE / NO-RESIDUE: exactly one view, on A; `viewsData.screen`/`onPrimary`
      unchanged; A's `publishedStruts` intact and NONE on B; `screenGeometry` ==
      A's rect; `e2e_assert_geometry_agrees` (render lands where reported); no
      "CLEARED SCREEN"-driven stranding (that log is benign per Phase 8).
      RESIDUE SURFACE: a split-brain move can strand a PHANTOM entry in the
      ScreenPool `[ScreenConnectors]` map in `lattedockrc` (screenpool.cpp:140-145),
      which the layout-file snapshot MISSES - the assertion MUST also snapshot
      `lattedockrc`. READBACK-GAP: flag G6 (relocation-pending state) so the
      mid-relocation abort is queryable rather than timing-dependent. DRIVERS:
      DR-4 (mid-drag hook to abort during the hide, C-I1), P1/P5 vehicle + move
      action.
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
      ADVERSARIAL TARGETS: **T1g** exit edit mode with a STALE tooltip/preview
      surface still mapped over a formerly-live rect (the exact PR #20
      separate-surface click-eater) - drive a click at that point, assert it
      REACHES the applet beneath (behavioral readback), and the crop equals
      baseline; **T4b** exit edit mode DURING a length-axis mask shrink
      (maximize-length release or zoom-out settle) so the settle collapse races
      the exit - stresses `windowMaskFor`'s union hold (inputmaskflush.h:62) and
      `needsSettleCollapse` (97), residue is `inputRegionRects`/`maskRect` left
      wider than the settled band (input captured across the vacated band, the
      frosted band drawn); assert after settle maskRect == the non-edit band AND
      the crop equals baseline; **T4c** exit edit mode mid applet-drag (shares the
      A2 z=900 strand); a right/middle-click on the exiting overlay. RESIDUE /
      NO-RESIDUE: `editMode`/`inConfigureAppletsMode` false, `inputRegionRects` +
      `maskRect` == the non-edit baseline (NOT the held union), config
      byte-unchanged, the click-through behavioral readback reaches the applet,
      and the post-exit crop equals the clean baseline (the frosted-band /
      ghost-overlay golden). RESIDUE SURFACE: `inConfigureAppletsMode` is a
      UNIVERSAL setting that `saveConfig`s to `lattedockrc` on change
      (universalsettings.cpp:50), so an edit-mode abort that leaves it stale
      strands residue OUTSIDE the layout file - the assertion MUST also snapshot
      `lattedockrc`. DRIVER: DR-4 (fire the mask-shrink transition at edit-exit,
      C-I1); Escape variant needs C-I10.
      Commits:
- [ ] **A6 Remove-undo abort.** DRIVE: `removeView`, then trigger UNDO within
      the window. READBACK, no golden: on undo the containment FULLY resurrects
      (back in `viewsData` with baseline geometry, applets intact, not a
      half-alive strand); counter-case, if it COMMITS, the group is FULLY gone
      (no `[Containments][<cid>]`, no orphan strut). Both branches asserted.
      GOTCHAS: 60s fallback; restart-inside-window resurrection; verify id first.
      ADVERSARIAL TARGETS (the widget-remove undo, layoutmanager.cpp
      removeAppletItem:1333 + setAppletInScheduledDestruction:147): **T4d-class**
      remove an applet, then re-add (undo) it - libplasma re-emits appletAdded and
      the double-container guard (addAppletItem:1287-1302) must NOT create a
      second container; assert applet count and appletOrder resurrect exactly once
      (no duplicate id), and `appletsInScheduledDestruction` (the parking map) is
      EMPTY after both undo and commit; RESTART inside the undo window
      (resurrection trap) - assert the removed applet/view does NOT come back
      after a commit that a restart raced; UNDO AFTER an overflow-relayout
      (df747ebf class) - assert a clean relayout with no orphan strut; a justify
      remove-then-undo recomputes splitters without a repair qWarning
      (splitterPosition/2 byte-stable). RESIDUE / NO-RESIDUE: on undo, `viewsData`
      / `viewAppletsOrder` + count byte-identical to baseline, parking map empty,
      no orphan `[Containments][<cid>]` strut; on commit, the group is fully gone,
      parking map empty, restart does not resurrect. RESIDUE SURFACE: the
      widget-remove-undo residue is the layout file (covered) plus the runtime
      parking map (readback); but the removeView branch can leave an ORPHAN
      ScreenPool `[ScreenConnectors]` entry in `lattedockrc` (screenpool.cpp:140-145)
      for the removed view's screen - that branch MUST also snapshot `lattedockrc`.
      No new driver (removeView / removeApplet exist).
      Commits:

## 8. Chunk decomposition for the swarm

Ten infra chunks, twelve committed scenario chunks, eight abort chunks: **30
chunks total.** (The abort family grew from six to eight: the adversarial target
matrix in section 7 split the two richest legs, add and applet-reorder, into a
config-matrix chunk plus an adversarial-target chunk each - C-A1b, C-A2b - while
A3/A4/A5/A6 fold their targets into the existing chunk.) Each is one Opus
agent's single reviewable PR (branch, gate, independent lean-Opus review, `gh pr
merge --rebase`, re-resolve hashes, fetch).

### Infra chunks (each ships the HC3 observes-a-rejection acceptance test)
- [x] **C-I1 = P0** fixture generator + matrix harness + baseline backbone.
      `tests/e2e/matrix/fixture.py`, `tests/e2e/matrix/matrix-lib.sh`,
      `scripts/run-matrix.sh`, `tests/e2e/matrix-harness-selftest.sh` (HC3).
      Commits: (C-I1 branch, hash at merge)
- [ ] **C-I2 = P1** multi-output vehicle. Commits:
- [x] **C-I3 = P3** `addApplet` + applet-id order readback + XML + test. Commits: (branch worktree-agent-a102bc9cfc620c8c0; final hashes at PR merge)
- [ ] **C-I4 = P4** `removeApplet` + drop-marker/spacer readback + XML + test. Commits:
- [ ] **C-I5 = P5** `moveViewToScreen` + XML + test. Commits:
- [ ] **C-I6 = P2** render-golden bridge (small set) + `--bless`. Commits:
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
- [ ] **C-A1** A1 add abort (config-matrix leg), dock+panel, 4 edge, {c,j}, single (16) + dual (2) = 18. Commits:
- [ ] **C-A1b** A1 add abort (adversarial-target leg): T2c orphan-spacer, T2a/T2b off-dock/foreign, T1a occupied, T1d task-onto-containment spacer-strand, T1e systray interior, T5f copy-modifier (~7 on the justify+vertical representative). Needs C-I9 (DR-1) + C-I4 (G3 null-guard). Commits:
- [ ] **C-A2** A2 applet-reorder abort (config-matrix leg), dock+panel, 4 edge, {c,j} (16) + dual (2) = 18. Commits:
- [ ] **C-A2b** A2 applet-reorder abort (adversarial-target leg): T4c edit-exit-mid-drag z=900 strand (marquee), T4d source-removed, T1a/T1b/T1c occupied/splitter/overflow, T5b reverse-jitter, T5e right-click, T5f modifier (~9). Needs C-I7 + C-I1 (DR-4) + C-I10 (DR-6). Commits:
- [ ] **C-A3** A3 task-reorder abort, dock+panel, 4 edge, center (8) + dual (2) = 10; folds the live-move-truth targets T5a/T5b/T5c/T5d + T1d cross-target (~5). Needs C-I8 (DR-2). Commits:
- [ ] **C-A4** A4 move abort / split-brain, dock+panel, 4 edge, {c,j}, dual (16); folds T3b waypoint-drop-back + T4a abort-during-hide + nonexistent-output refusal (~4). Flags G6 relocation-pending readback. Needs C-I5 + C-I2 + C-I1 (DR-4). Commits:
- [ ] **C-A5** A5 edit no-op / PR#20 guard, dock+panel, 4 edge, center (8) + dual (2) = 10; folds T1g stale-tooltip-surface + T4b mask-shrink-settle-race (~4). Needs C-I1 (DR-4). Commits:
- [ ] **C-A6** A6 remove-undo abort, dock+panel, 2 edge, center (4) + dual (2) = 6; folds double-container-guard, restart-inside-window, overflow-relayout undo (~3). removeView/removeApplet exist. Commits:

### Merge order and parallelism
**Gate on everything:** C-I1 (harness + baseline) and C-I6 (golden bridge) block
all twenty scenario chunks.

**Per-family gates:** C-I3 -> C-S4,C-S5,C-A1. C-I4 -> C-S11,C-S12,C-A1(spacer),
C-A1b(G3 null-guard),C-A2(z). C-I5 -> C-S9,C-S10,C-A4. C-I7 -> C-S6,C-S7,C-A2,
C-A2b. C-I8 -> C-S8,C-A3. C-I9 -> DND leg of C-S4,C-S5,C-A1,C-A1b. C-I10 ->
Escape sub-path of C-A2,C-A2b,C-A3,C-A5. C-I2 -> all dual tails + F5/A4. The two
adversarial legs carry the heaviest driver deps: C-A1b needs DR-1 (C-I9) + the
G3 null-guard (C-I4); C-A2b needs DR-4 (C-I1 mid-drag hook) + DR-6 (C-I7+C-I10),
so it is the LAST abort chunk to unblock.

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
  C-S8 (C-I8). The adversarial legs trail their config-matrix twins by the
  driver deps: C-A1b after C-A1 once C-I9(DR-1)+C-I4(G3) are in; C-A2b after
  C-A2 once C-I1(DR-4)+C-I10(DR-6) are in - it is the last abort chunk, but still
  interleaved with the remaining committed chunks, never deferred past them.

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
- **O8 Abort input mechanics: Escape vs release (GROUNDED, see section 7's
  ESCAPE-vs-RELEASE finding).** Escape-cancel and release-at-origin /
  release-over-nothing exercise DIFFERENT code, and the code says Escape is NOT
  uniformly a cancel: the ConfigOverlay applet-reorder MouseArea has no Keys
  handler, so Escape there most likely EXITS edit mode (stranding the z=900
  residue, target T4c), while the task-reorder DragDrop drag IS
  compositor-cancelable but does not revert the already-applied `tasksModel.move`.
  So both input paths are wanted AND their acceptance must OBSERVE Escape's real
  effect per path, never assume cancel. The richer regression target is the
  malformed-drop matrix (T1-T5), not Escape alone. Confirm: run both, with the
  driver HC3 acceptance pinning Escape's actual per-path behavior.
