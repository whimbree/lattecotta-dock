# Dock identity, placement, and replication hardening

Status: D77 (dock duplication retains clone lineage and edit ownership) identity
and lifecycle slice implemented; placement and stacking slices pending
(2026-07-21)

This record defines the ownership model used to investigate dock duplication,
screen-group cloning, edit mode, placement, and same-edge stacking. It separates
confirmed causes from hypotheses and records which repairs have landed.

## Terms and identity diagram

```text
layout file
  |
  +-- persistent containment 41 --------------------------+
  |     persistent dock id: 41                            |
  |     logical dock id: 41                               |
  |     relationship: screen-group original               |
  |                                                       |
  |     runtime OriginalView #A, output DP-1               |
  |       owns Positioner, Effects, Visibility, QML layout|
  |                                                       |
  |     persistent containment 57                         |
  |       persistent dock id: 57                          |
  |       logical dock id: 41                             |
  |       clone source id: 41                             |
  |       runtime ClonedView #B, output DP-2               |
  |       owns its runtime managers and QML layout         |
  |                                                       |
  +-- persistent containment 63                            |
        persistent dock id: 63                            |
        logical dock id: 63                               |
        relationship: independent duplicate              |
        runtime OriginalView #C, output DP-1               |
        owns its runtime managers and QML layout           |

Corona
  +-- Layout::GenericLayout owns containment-to-view maps
  +-- ViewSettingsFactory owns one reusable edit-chrome ensemble
  +-- output-edge stack coordinator: missing
```

`runtime OriginalView #A` is descriptive only. This slice adds no runtime view
identifier or dock-system snapshot. Edit-chrome retargeting carries its own
process-local request generation because that is the boundary with deferred
work. Broader runtime identity diagnostics remain a separate observability
decision.

## Identity and ownership map

| Entity | Unique or shared | Lifetime owner | Persistence key | Duplication | Output or orientation change | Edit mode | Cleanup |
|---|---|---|---|---|---|---|---|
| Logical dock | One independent containment, or one original plus its explicit screen-group clones | Original persistent containment | Original containment ID | Independent duplication creates a new logical dock | Identity stays stable | Selects the intended edit target | Ends when the original and its explicit clones are removed |
| Persistent dock record | Unique per containment | Layout configuration | Containment ID | All containment and applet IDs are regenerated | Screen, edge, alignment, and length state update on the same record | Supplies menu and edit target identity | Removed with the containment configuration |
| Containment | Unique per visible original or clone | Plasma Corona and layout | Containment ID | Must be newly imported with remapped IDs | Persists placement and QML configuration | Own `userConfiguring` is the per-view edit presentation state | Destroyed by layout removal; no runtime view may retain it |
| Runtime dock view | Unique QObject and QWindow per active containment | `GenericLayout` containment-to-view map | Not persisted | Constructed fresh | Remaps or recreates its layer surface | Holds only a non-owning pointer to shared edit chrome | Disconnects, unregisters, and deletes all child managers |
| Per-output view | A screen-group clone is one separate containment and runtime view | `OriginalView` owns clone membership; layout owns runtime registration | Clone containment ID plus `isClonedFrom` | Independent duplication must not create this relationship | Screen-group policy derives output placement; runtime geometry remains local | Clone edit requests intentionally resolve to the original | Clone must unregister from the original before destruction |
| Clone relationship | Shared logical group, explicit and acyclic | Original persistent dock | Clone record's `isClonedFrom` | Copied only when duplicating a complete logical group, never a single independent dock | Source remains valid and placement policy is explicit | Menu wording and edit targeting use the same relation | Source removal destroys or detaches all clones without dangling members |
| Applet layout | Unique QML object and applet IDs per containment | Containment runtime | Applet groups under containment ID | Deep-copied and ID-remapped | Local geometry recalculates for that view | Its containment controls edit presentation | Destroyed with containment and view |
| Geometry controller | Unique `Positioner` per runtime view | Runtime `View` QObject parent | Derived, not persisted | Constructed fresh | Consumes that view's output plus explicit peer-footprint solution | Edit thickness may change local placement | QObject parent destruction disconnects all registrations |
| Applet sizing | Unique `AutoSize` and effective icon state per containment | Containment QML tree | Configured values only | Config copied; effective values recomputed | Consumes the same view's solved primary length | Recomputes only for local edit geometry | QML tree destruction |
| Edit-mode controller | One reusable chrome ensemble, one current owner, one requested generation | `ViewSettingsFactory` under Corona | Not persisted | Never copied | Retarget is one cancelable generation | Exactly one effective owner; clone requests resolve explicitly | Cancels deferred work and clears the old containment before rebinding |
| Stacking controller | Missing in the current tree | Required owner: layout or Corona placement domain | Required key: output, edge, stable rank | New duplicate gets a stable independent rank | Re-solves membership atomically | Routes activation without merging edit identity | Removes membership before view destruction |
| Configuration object | Unique mutable map per containment; selected screen-clone values are synchronized | Containment | Containment configuration group | Deep-copied for independent duplication | Placement values must pass one normalization boundary | Menu and edit relationship are read from runtime identity | Destroyed with containment; signal contexts must be the receiving view |

## Confirmed causes

### Independent duplication can preserve clone ancestry (fixed)

Before the repair, the D-Bus `duplicateView()` action accepted a `ClonedView`,
although the clone's context menu hid Duplicate Dock. `ClonedView::data()`
recorded the original containment in `isClonedFrom`. Storage first remapped an
orphaned source to `-1`, then `Storage::newView()` overlaid the captured
`Data::View` and wrote the old `isClonedFrom` value back. The result was another
live clone rather than an independent duplicate. Runtime capability checks now
refuse duplication and the other original-only actions for clone roles.

This explains order-dependent clone membership, Edit Original Dock wording, and
edit requests opening the original on another output when duplication is driven
through D-Bus or another path that bypasses the menu restriction.

### Deferred edit-chrome retargets are not cancelable (fixed)

`PrimaryConfigView::setParentView()` scheduled an unversioned 400 ms retarget.
An A to B to C sequence could run both callbacks. The first callback started
B's configuring session. The second rebound directly to C without ending B's
session, leaving more than one containment with `userConfiguring=true`. The
retarget is now cancelable and generation-checked, and every rebind ends the old
session first.

### Same-edge stacking has no ownership model

Multiple same-edge containments are persistable, but no stack ID, stable rank,
cumulative inset, activation routing, or reservation coordinator exists. Equal
screen and edge views originate in `QHash::values()` and have no deterministic
tie-break. Every layer surface independently requests the same anchor and its
own exclusive zone. Latte's internal available-rectangle model separately uses
the deepest footprint rather than a solved physical stack.

### Alignment changes bypass the placement invariant

`LengthOffsetClamp` expresses the correct length and offset limits, but only the
Maximum and Offset controls call it. Changing Center to Start or End writes the
new alignment without normalizing a negative center offset. The QML background
and applet layout then apply a negative edge margin and render past the output.
Startup and external configuration writes can bypass the same invariant.

### Cross-dock icon resizing is local autosizing after peer geometry changes

A bottom alignment change alters its partial internal footprint. A same-output
vertical dock receives the available-region invalidation, selects a shorter
rectangle, and resizes. Its own `AutoSize` instance then reduces its effective
icon size. No global icon cache or shared autosizer is involved.

The defect is the disagreement between two reservation models. Layer shell can
publish only an edge-wide scalar exclusive zone, while Latte internally applies
an alignment-aware partial footprint. A dock's alignment can therefore change a
neighbor's internal length without changing the compositor work area.

## Additional confirmed lifecycle hazards

- `ClonedView` destruction did not unregister itself from `OriginalView`'s raw
  clone list. Clone membership now uses `QPointer` and unregisters before
  teardown.
- `Positioner` added output-geometry signal connections on each output move
  without replacing the prior set. One owned connection now replaces the old
  subscription.
- Edit-chrome title matching used prefix matching, so a title for containment 1
  could match containment 10. Wayland lookup now requires exact app ID and
  title identity.
- The ignored-window registry was a global set without owner counts, so one
  owner could remove an entry still required by another owner. First-owner and
  last-owner transitions now govern tracking.
- D-Bus edit diagnostics report the global configure-applets flag for every
  view instead of each view's effective `editMode && configureApplets` state.

These faults share the session-wide theme of unversioned or unowned runtime
registrations. They do not prove one shared persistent object causes every
visible symptom.

## Minimal architectural repair

1. Make relationship creation explicit. Independent duplication accepts only
   original roles and creates remapped containment and applet IDs. Screen-group
   clone construction is the only path allowed to set `isClonedFrom`.
2. Give each deferred edit-chrome retarget a monotonic process-local request
   generation. Delayed work carries both the target and request generation and
   is rejected when superseded.
3. Make edit-chrome ownership a single transition with one cancelable pending
   target. Ending the old containment's configuring session precedes every
   rebind.
4. Route every placement mutation through one normalization transaction for
   output, edge, semantic alignment, minimum and maximum length, and offset.
5. Add one output-edge stack coordinator with stable persisted ranks,
   cumulative insets, activation ownership, and a single reservation policy.
6. Keep compositor work-area reservation separate from Latte's partial
   dock-to-dock avoidance footprint. Autosizing consumes only the final solved
   geometry for its own view.
7. Replace blanket clone configuration mirroring with explicit content and
   placement projections. Existing screen-group clones may retain derived
   placement as a named policy; independent duplicates share nothing mutable.
8. Keep broader runtime identity diagnostics in the observability track. This
   repair does not add a runtime view ID or dock-system D-Bus snapshot.

## Implementation and verification slices

1. [x] Relationship capability policy, independent duplication, edit targeting,
   lifecycle cleanup, exact Wayland identity, owner-scoped registrations, and
   persistent context-menu identity.
2. [ ] Placement normalization and bounded visible geometry across all edges and
   semantic alignments.
3. [ ] Same-edge stack solving, persistence, layer-shell application, reserved
   space, and activation routing.
4. [ ] Multi-view nested-KWin fixtures and deterministic operation replay across
   duplication, movement, orientation, alignment, editing, destruction, and
   reload.

The completed slice is covered by sanitizer-backed pure-core tests for action
policy, retarget request generations, exact title matching, and ignored-window
ownership, plus a production source contract and successful focused production
compiles. No live-desktop run, nested-KWin replay, or full repository gate is
claimed for this branch.

Each slice requires a failing regression first, pure-core ASan and UBSan tests
where a value model can carry the invariant, nested-KWin state and render
agreement for compositor behavior, and an independent cold diff review before
merge.

## Open questions requiring runtime evidence

- Whether the reported Duplicate Dock sequence entered through D-Bus, an older
  UI surface, or a screen-group operation. Every current action boundary now
  applies the same role policy, but the historical entry route remains unknown.
- Which same-edge reservation policy best matches intended daily-driver
  behavior for mixed visibility modes. The stack still requires deterministic
  physical order under every policy.
- Whether partial dock-to-dock corner avoidance should remain under Wayland or
  be replaced by the compositor's edge-wide work-area contract. The current
  split cannot remain implicit.
