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

- [ ] Add pure geometry types for the attached rectangle, floated rectangle,
      stable envelope, trigger, visible mask, and edge-reaching input bridge.
      Assert valid output-contained geometry at the boundary.
      Commits:
- [ ] Add one per-view C++ transition controller with qreal progress, explicit
      target and phase enums, current-value reversal, and one animation owner.
      Commits:
- [ ] Make Positioner solve one stable QWindow envelope on every edge. Remove
      floatingness from physical QWindow placement and layer-shell margins.
      Commits:
- [ ] Keep resting applet and automatic-size geometry independent of
      transition progress. Internal content may translate with the visible
      background, but it must not refit or resize.
      Commits:
- [ ] Expose all stable and current transition geometry through the atomic
      D-Bus snapshot.
      Commits:
- [ ] Pin both reversal directions, rapid alternating targets, duration
      consistency, and no geometry/configure storm.
      Commits:

## FP-3: internal presentation, input, effects, and popups

FP-3 means internal presentation, input, effects, and popup ownership.

- [ ] Move the visible background between attached and floated rectangles
      inside the stable canvas.
      Commits:
- [ ] Derive blur, contrast, borders, corners, and shadow offsets from the
      same visible shape.
      Commits:
- [ ] Keep the screen-edge border absent only at exact attached progress zero.
      Keep floating corners for every nonzero progress.
      Commits:
- [ ] Extend the input mask from the exact visible span through the gap to the
      screen edge and remap pointer and wheel events into the containment.
      Commits:
- [ ] Anchor popups from the visible mask and publish the supported
      floating-applet hint.
      Commits:
- [ ] Remove the physical `slideOffset` floating-panel animation, dynamic
      floating-gap strut depth, and stale real-offset comments and tests.
      Commits:
- [ ] Add render and interaction coverage for all four edges, fractional
      progress, asymmetric shadows, partial spans, and popup toggle behavior.
      Commits:

## FP-4: stable window-touch trigger and end-to-end acceptance

FP-4 means the stable window-touch trigger and end-to-end acceptance.

- [ ] Use current-desktop/current-activity window state, exclude hidden and
      minimized windows, and allow a spanning window to affect more than one
      output.
      Commits:
- [ ] Intersect against the stable per-view trigger with one logical pixel of
      inward overlap. Do not use the moving visible rectangle.
      Commits:
- [ ] Deliver the direct interaction path with a bounded 10 ms debounce
      instead of the generic 150 ms window-change coalescer.
      Commits:
- [ ] Initially enable attachment only for eligible floating Always Visible
      panels. Preserve distinct visibility-mode semantics.
      Commits:
- [ ] Drive drag-in, drag-out, mid-flight reverse, Escape cancel, and committed
      maximize against real nested-KWin client frames.
      Commits:
- [ ] Drive portrait, landscape, disconnected, partially touching, fully
      touching, spanning-window, and same-edge separated-span fixtures.
      Commits:
- [ ] Preserve replay logs for a deterministic operation storm covering
      duplicate, linked view, output move, edge, orientation, alignment, edit
      mode, destruction, recreation, and reload.
      Commits:
- [ ] Run that operation storm with a fixed seed in nested KWin. Assert through
      D-Bus after convergence that identities are unique where required,
      transition geometry is stable, reservation membership has no stale or
      orphan group, edit participants are exact, and restart reproduces the
      settled state. Fail on any divergence; keep the replay log only as the
      reproduction artifact.
      Commits:

## Definition of done

- [x] PR #116, the geometry and reservation baseline, is merged first.
      Commits: 6f6c33d9a
- [x] D160 (same-edge maximum reservation depth was described as implemented)
      exposed the missing authority; FP-1 implements it.
- [ ] D172 (floating panel attachment moves the surface and reservation instead
      of presentation) is fixed by FP-1 through FP-4.
- [ ] D151 (nested hover preview did not exercise parabolic expansion) and
      D152 (linked portrait dock overflowed with automatic sizing off) remain
      independent unless a shared root is proved.
- [ ] Every asserted state is available through D-Bus. Update the adaptor XML,
      atomic serializer and exact schema test, D-Bus design document, and D-Bus
      usage reference in the same source commit.
- [x] The README describes the stable floating-panel behavior in timeless
      terms.
- [ ] The full canonical gate passes after the final source commit.
- [ ] Land FP-1, FP-2, FP-3, and FP-4 serially through the orchestrator review,
      correction, rereview where required, rebase, canonical-gate, and GitHub
      rebase-merge flow. Each final code diff must have an independent
      mergeable verdict.
- [ ] Nested-KWin acceptance passes without touching the real desktop session.
- [ ] Final real-layout acceptance checks visual feel, pointer-edge behavior,
      popup placement, and multi-output composition before release sign-off.
