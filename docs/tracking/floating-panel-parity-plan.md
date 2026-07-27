# Floating-panel parity plan

Approved direction: Option 1, stable per-view surface with internal visual
transition, 2026-07-24.

Reference model:
[`../reference/plasma-floating-panel-parity.md`](../reference/plasma-floating-panel-parity.md).

This checklist replaces physical layer-surface motion with one stable geometry
model. Each slice lands through its own GitHub PR, receives an independent
read-only review, and passes the canonical gate before merge.

## Non-negotiable contract

- The view QWindow, layer-shell perpendicular margin, resting applet
  measurements and primary-axis span, trigger geometry, and normal reservation
  depth stay fixed while
  `floatingness` changes.
- Independent views own independent transition state.
- Multiple separated partial views on one output edge remain supported.
- Same-edge reservation depth is the maximum eligible depth, not a sum.
- No inward lanes, ranks, or dock-stack feature are introduced.
- Activation and Fitts input use exact per-view regions. One owner may manage
  multiple regions, but the regions are not widened into a continuous strip.
- Output membership uses Latte output identity plus edge, never physical
  adjacency.

## FP-1: output-edge maximum reservation authority

FP-1 means the output-edge maximum reservation authority.

- [x] Replace one positive reservation surface per view with one coordinator
      per Latte output identity and edge.
      Commits: 0a4407f30, e8adfb96e
- [x] Make contribution add, update, migration, and removal atomic. A view
      cannot remain under an old output or edge.
      Commits: 63497b3ac, 1f82307da
- [x] Publish the deepest eligible attached thickness. Never add depths.
      Commits: 0a4407f30, e8adfb96e
- [x] Preserve visual surfaces at zone -1 and preserve the exact per-view
      occupied geometry used by Latte's own placement solver.
      Commits: e8adfb96e
- [x] Extend the atomic D-Bus snapshot with group membership, contributors,
      selected depth, publisher state, and generation.
      Commits: 7d452e789, 27519ddb5, 9e8907870, ae529c166, 266f11d0f,
      a1035aabf
- [x] Pin order independence, deepest-member removal, last-member teardown,
      cross-output isolation, visibility changes, output moves, and restart.
      Commits: 0a4407f30, 21b7c604c, cb353022f, 3f70b7224, 9e8907870,
      ae529c166, 21ea8c61e, cdb9c6d20

## FP-2: stable canvas and transition controller

FP-2 means the stable canvas and transition controller.

- [x] Add pure geometry types for the attached rectangle, floated rectangle,
      stable envelope, trigger, visible mask, and edge-reaching input bridge.
      Assert valid output-contained geometry at the boundary.
      Commits: 1f4f6206b, 853e6e359, d5057d091, 8ef41a45d, 44a5ea89d
      Evidence: sanitizer-backed `floatingpanelgeometrytest` covers all four
      edges, offset outputs, partial spans, fractional progress, integer
      overflow, and the mandatory inward trigger pixel.
- [x] Add one per-view C++ transition controller with qreal progress, explicit
      target and phase enums, current-value reversal, and one animation owner.
      Commits: 0231c91ba, 2a225df16
      Evidence: `floatingtransitiontest` covers both directions, current-value
      reversal, twenty alternating targets, full-duration consistency, cubic
      easing, eligibility, and one animation owner.
- [x] Make Positioner solve one stable QWindow envelope on every edge. Remove
      floatingness from physical QWindow placement and layer-shell margins.
      Commits: 149d2ea38, 65ab2eab1
      Evidence: the panel path fails before QWindow mutation, applies one
      solved envelope once, retains zero layer-shell edge margin, and keeps
      ordinary hiding as displacement of that envelope.
- [x] Keep resting applet and automatic-size geometry independent of
      transition progress. Internal content may translate with the visible
      background, but it must not refit or resize.
      Commits: e0abc3d51, 73ad81186
      Evidence: `sourceguardtest` pins fixed applet measurement bounds,
      configured partial span, target-independent background thickness, and
      removal of the physical floating-gap slide animations. The QML compile
      gate passes 130 of 130 package files and the touched qmllint baseline
      strictly decreases.
- [x] Expose all stable and current transition geometry through the atomic
      D-Bus snapshot.
      Commits: e25ee1f0c, 054e4826e, 0436dd90c
      Evidence: schema 5 reports fraction-preserving stable and current
      geometry, controller identity and generation, and both physical-churn
      counters. Exact serializer and mutation tests fail closed on incomplete,
      inconsistent, off-output, wrong-edge, rounded, or aliased state.
- [x] Pin both reversal directions, rapid alternating targets, duration
      consistency, and no geometry/configure storm.
      Commits: 03861cdc3, 0dabbb516, b122ef88c, 34af636d7, 29f992ef0,
      e1504a097
      Evidence: `floatingtransitiontest`, `layershellmappingtest`, and
      `sourceguardtest` pass. Recipe 071 configures a partial Justify panel,
      samples both qreal directions, then drives eight rapid reversals while
      requiring byte-identical stable geometry and zero deltas from
      `transitionGeometryRevision`,
      `surfaceGeometryPublicationRevision` and
      `layerShellConfigureRequestRevision`. The schema-integrated nested-KWin
      run passes with an 88 px maximum-depth reservation and both qreal
      midpoints observed.

## FP-3: internal presentation, input, effects, and popups

FP-3 means internal presentation, input, effects, and popup ownership.

- [x] Move the visible background between attached and floated rectangles
      inside the stable canvas.
      Commits: 3bd2ce525, 48e1f9b39
- [x] Derive blur, contrast, borders, corners, and shadow offsets from the
      same visible shape.
      Commits: 3bd2ce525, 48e1f9b39, 19f3effd7
- [x] Keep the screen-edge border absent only at exact attached progress zero.
      Keep floating corners for every nonzero progress.
      Commits: 3bd2ce525, 48e1f9b39, 15d7dda7e
- [x] Extend the input mask from the exact visible span through the gap to the
      screen edge and remap pointer and wheel events into the containment.
      Commits: 3bd2ce525, 44e6d5907
- [x] Anchor popups from the visible mask and publish the supported
      floating-applet hint.
      Commits: 228252623, 15d7dda7e
- [x] Remove the physical `slideOffset` floating-panel animation, dynamic
      floating-gap strut depth, and stale real-offset comments and tests.
      Commits: ca388f82e
- [x] Add render and interaction coverage for all four edges, fractional
      progress, asymmetric shadows, partial spans, and popup toggle behavior.
      Commits: 3bd2ce525, 44e6d5907, 48e1f9b39, 228252623, ca388f82e,
      ab880f653, 15d7dda7e, 19f3effd7, 19cb727e0
      Evidence: the pure presentation, input, damage-handshake, border,
      shadow, popup, transition, D-Bus, source-route, and lifecycle tests pass.
      The pairing ratchet reports 116 CTest entries and 45 paired headers.
      The canonical gate passed at reviewed branch head `a7c941db1`, including
      all four nested sanitizer recipes, and GitHub merged PR #122 after the
      required independent follow-up review returned `MERGE`.

## FP-4: stable window-touch trigger and end-to-end acceptance

FP-4 means the stable window-touch trigger and end-to-end acceptance.

FP-4A means the direct window-touch runtime and single-client nested
acceptance. FP-4B means multi-output and separated-span topology acceptance.
FP-4C means deterministic operation-storm acceptance. PR #124 landed FP-4A,
and PR #126 landed FP-4B.

- [x] Use current-desktop/current-activity window state, exclude hidden and
      minimized windows, and allow a spanning window to affect more than one
      output.
      Commits: d0d499d50, 36e835fb9, fd445ee2f (PR #124)
- [x] Intersect against the stable per-view trigger with one logical pixel of
      inward translation. The complete stable envelope, including the floating
      gap, moves one logical pixel toward the workspace and clips to the
      output. Do not expand only the attached background or use the moving
      visible rectangle.
      Commits: d0d499d50 (PR #124), 8d4ac1e90
- [x] Deliver the direct interaction path with a bounded 10 ms debounce
      instead of the generic 150 ms window-change coalescer.
      Commits: d0d499d50 (PR #124)
- [x] Initially enable attachment only for eligible floating Always Visible
      panels. Preserve distinct visibility-mode semantics.
      Commits: d0d499d50, b552508e3, f4232ae54 (PR #124)
- [x] Drive drag-in, drag-out, mid-flight reverse, Escape cancel, and committed
      maximize against real nested-KWin client frames.
      Commits: f8396b5ed, d0d499d50 (PR #124), 559cb666b
- [x] Drive portrait, landscape, disconnected, partially touching, fully
      touching, spanning-window, and same-edge separated-span fixtures.
      Commits: 4daa80121, ad2a91c6f, 649fb79b4, 4ac5208b9,
      80e5d8fee, 3f6794861 (PR #126)
- [x] Preserve replay logs for a deterministic operation storm covering
      duplicate, linked view, output move, edge, orientation, alignment, edit
      mode, destruction, recreation, and reload.
      Commits: 0c5c33fa6
- [x] Run that operation storm with a fixed seed in nested KWin. Assert through
      D-Bus after convergence that identities are unique where required,
      transition geometry is stable, reservation membership has no stale or
      orphan group, edit participants are exact, and restart reproduces the
      settled state. Fail on any divergence; keep the replay log only as the
      reproduction artifact.
      Commits: 223ec413a, cef08bd1f, eab2e1f59, 1c8d9bf2d, 0c5c33fa6,
      3967011eb, c675458c6, e712cbf63, 0f214f012, 7973f68cd
- [x] Correct the post-merge live-drag parity findings. Give both Panels and
      Docks one tracker-owned per-view trigger, make eligible Docks consume the
      live count instead of committed maximize, and keep one attached-depth
      reservation throughout Dock presentation changes.
      Commits: 8d4ac1e90, 559cb666b
      Evidence: recipe 074 holds a real titlebar drag across and back out of
      both a Panel and a Dock trigger. Both state changes occur before button
      release while the QWindow, reservation, layer-shell state, tracker
      identity, and publication revisions remain byte-identical.
- [x] Make multi-output baseline capture transactional after structural
      validation.
      Commits: e47d5c1c0
      Evidence: recipe 073 passes exact separated-span activation,
      spanning-window fanout, maximum-depth reservations, restart persistence,
      and full-touching, partial-touching, and disconnected output topologies.

## Definition of done

- [x] PR #116, the geometry and reservation baseline, is merged first.
      Commits: 6f6c33d9a
- [x] D160 (same-edge maximum reservation depth was described as implemented)
      exposed the missing authority; FP-1 implements it.
- [x] D172 (floating panel attachment moves the surface and reservation instead
      of presentation) is fixed by FP-1 through FP-4.
      Commits: complete FP-1 through FP-4 per-stage commit lists above; final
      live-drag corrections 8d4ac1e90, 559cb666b
- [ ] D151 (nested hover preview did not exercise parabolic expansion) and
      D152 (linked portrait dock overflowed with automatic sizing off) remain
      independent unless a shared root is proved.
- [x] Every asserted state is available through D-Bus. Update the adaptor XML,
      atomic serializer and exact schema test, D-Bus design document, and D-Bus
      usage reference in the same source commit.
      Commits: 1b0a88eeb
- [x] The README describes the stable floating-panel behavior in timeless
      terms.
- [x] The full canonical gate passes after the final source commit.
      The replacement gate exited 0 at exact source head
      `15baaf03426c39e752e814de937681809c4c7e0c`, including all 124 CTest
      entries, QML and coverage ratchets, visual probes, the complete
      ASan/UBSan build, four nested-compositor recipes, package provenance
      controls, and matrix refusals.
- [x] Correct D226 (LayerShell output migration bypassed reservation-gated
      remapping) and D227 (layout mutation preceded destination-output
      preflight).
      Commits: 01d364d95, bd744dddc
- [x] Fix D228 (placement preflight promoted a hide-time QWindow observation
      to output ownership) and rerun the exact operation storm.
      Commit: 992f9df1c
      Evidence: all 76 operations and exact cleanup pass for seed 127934575 in
      nested KWin.
- [x] Obtain the required independent follow-up review after the D226, D227,
      and D228 corrections. The replacement canonical gate passed at
      `728285b39`. That review found D229 (cross-layout placement could report
      success after persistence failure). Commit `e57f8e929` partially fixed
      D229, and its canonical gate passed at `f66a404c0`. Silent dock loss
      after restart is a CRITICAL persistence finding, so the rule's critical
      exception required one fresh independent rereview. That rereview returned
      `DO NOT MERGE`: existing files did not require a writable parent, and
      absent paths did not require a searchable parent. Commit `1dc18737f`
      completes both branches with real filesystem coverage. Its replacement
      canonical gate passed at exact branch head
      `a1a154fd0843d64e4551d61fe559963532dee70a`. The next critical rereview
      returned `DO NOT MERGE`: KConfig reparses an existing file before
      replacement, but the classifier accepts a write-only file. The
      missing-parent and regular-file-parent absent cases were also untested.
      Commit `7d2db9f95` requires readable existing targets and completes the
      real-filesystem matrix. Its seven focused tests and exact 76-operation
      replay pass. The replacement canonical gate passed at exact branch head
      `b5ef76d74e13bcc13b64e0df7dc18fb5295b4354`. The fresh critical rereview
      returned `DO NOT MERGE`: KConfig canonicalizes existing paths and uses
      `QSaveFile` only for process-owned targets, but the classifier models
      neither branch. Commit `14c81dd18` classifies the canonical backend path
      and requires the process owner. Its real symlink case and compile-time
      ownership negative control are non-vacuous, and the focused tests and
      exact replay pass. The replacement canonical gate passed at exact branch
      head `73ffe196d1c3b9edf2a632d2d399a09aed46ef5a`; the fresh critical
      rereview returned `DO NOT MERGE`. A file-wide KConfig `[$i]`
      immutability marker passes filesystem preflight and makes the later
      sync refuse after mutation. Commits `3651b3a8b` and `1b0a88eeb` now
      provide a checksummed destination-first transaction, active-owner commit
      decision, startup rollback or roll-forward, immutable and held-lock
      refusal, semantic readback, and typed refusal propagation. Commit
      `7973f68cd` requires no pending transaction at every operation-storm
      checkpoint. The focused matrix and exact seed 127934575 replay pass in
      `linked-dock-operation-stress.seed-127934575.run-APzTUu`; the
      replacement canonical gate passed at exact branch head
      `311589122215a17c4a00ec1f1edf9dd117819eb9`. The fresh critical rereview
      returned `DO NOT MERGE` for D230 (endpoint directory entries were not
      durable before journal retirement), D231 (queued active-view moves could
      be recorded as committed), and D232 (the operation storm never invoked
      a cross-layout transaction). Commit `aa2744787` flushes the containing
      directory after each endpoint publication. Commit `0e2ec0810` gives
      every queued placement one exact terminal generation result. Commits
      `c68f4a974` and `1c3b86a85` expose transaction lifecycle generations and
      drive two real cross-layout moves. The replacement 78-operation replay
      passes with creation, commit, and retirement generations advancing
      exactly from 0 to 1 to 2. Commit `c2ef221ca` bounds cleanup of a
      crash-stopped nested seed. The replacement canonical gate passes at
      `103c9e4a9f7bd7d87f7ba523a71ff735b30fddc1`. Fresh critical rereview found
      D234 (first transaction-root publication was not durable) and D235
      (unanimated layout moves retained a delayed relocation completion).
      Commits `f4594042e` and `7b4cc6e98` correct both findings with exact
      durability injection and generation-preserving callback coverage. The new
      canonical gate passes at
      `15baaf03426c39e752e814de937681809c4c7e0c`. The final fresh critical
      rereview returned `MERGE` with no findings.
- [x] Land FP-1, FP-2, FP-3, and FP-4 serially through the orchestrator review,
      correction, rereview where required, rebase, canonical-gate, and GitHub
      rebase-merge flow. Each final code diff must have an independent
      mergeable verdict. PR #128 merged FP-4C and completed Option 1 at
      `4d52a1917` on `main`.
- [x] Nested-KWin acceptance passes without touching the real desktop session.
- [ ] Final real-layout acceptance checks visual feel, pointer-edge behavior,
      popup placement, and multi-output composition before release sign-off.
