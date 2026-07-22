# Dock identity, placement, and replication hardening

Status: independent Duplicate Dock and explicit Create Linked Dock implemented;
bounded placement and same-edge stacking slices remain separate work
(2026-07-22)

This record defines the ownership model used to investigate dock duplication,
screen-group cloning, edit mode, placement, and same-edge stacking. It separates
confirmed causes from hypotheses and records which repairs have landed.

## User-facing terms and identity diagram

The user-facing operations have three distinct jobs:

- **New Dock** creates a dock from a selected template.
- **Duplicate Dock** takes one snapshot of the visible source and creates one
  independent dock. It does not retain a source pointer or a dock-level
  synchronization relationship.
- **Create Linked Dock…** creates a synchronized copy on a selected active
  output and edge. Applet content is linked while placement, visibility,
  appearance, removal, and edit presentation remain member-local.

The internal design calls the synchronized relation a **replica relationship**.
`ClonedView`, `isCloned`, and `isClonedFrom` are legacy compatibility and
migration terms for that relation, not additional user-facing operations.

```text
layout file
  |
  +-- persistent containment 41 --------------------------+
  |     persistent dock id: 41                            |
  |     logical dock id: 41                               |
  |     relationship: linked root                         |
  |                                                       |
  |     runtime OriginalView #A, output DP-1               |
  |       owns Positioner, Effects, Visibility, QML layout|
  |                                                       |
  |     persistent containment 57                         |
  |       persistent dock id: 57                          |
  |       logical dock id: 41                             |
  |       clone source id: 41                             |
  |       link placement: explicit target                 |
  |       runtime linked member #B, output DP-2           |
  |       legacy runtime type: ClonedView                 |
  |       owns placement, edit, managers and QML layout   |
  |                                                       |
  +-- persistent containment 63                            |
        persistent dock id: 63                            |
        logical dock id: 63                               |
        relationship: independent Duplicate Dock         |
        runtime OriginalView #C, output DP-1               |
        owns its runtime managers and QML layout           |

Corona
  +-- Layout::GenericLayout owns containment-to-view maps
  +-- ViewSettingsFactory owns one reusable edit-chrome ensemble
  +-- output-edge stack coordinator: missing
```

`runtime OriginalView #A` is descriptive only. C0 (the atomic dock-system
observability snapshot) exposes an opaque process-local runtime token for this
instance, while the persistent and logical identities remain containment based.
Edit-chrome retargeting carries its own process-local request generation because
that is the boundary with deferred work.

## Identity and ownership map

| Entity | Unique or shared | Lifetime owner | Persistence key | Duplication | Output or orientation change | Edit mode | Cleanup |
|---|---|---|---|---|---|---|---|
| Logical dock | One independent containment, or one original plus its explicit linked replicas | Original persistent containment | Original containment ID | Duplicate Dock creates one new independent logical dock | Identity stays stable | Selects the intended edit target | Ends when the original and its explicit replicas are removed |
| Persistent dock record | Unique per containment | Layout configuration | Containment ID | All containment and applet IDs are regenerated | Screen, edge, alignment, and length state update on the same record | Supplies menu and edit target identity | Removed with the containment configuration |
| Containment | Unique per visible original or clone | Plasma Corona and layout | Containment ID | Must be newly imported with remapped IDs | Persists placement and QML configuration | Own `userConfiguring` is the per-view edit presentation state | Destroyed by layout removal; no runtime view may retain it |
| Runtime dock view | Unique QObject and QWindow per active containment | `GenericLayout` containment-to-view map | Not persisted | Constructed fresh | Remaps or recreates its layer surface | Holds only a non-owning pointer to shared edit chrome | Disconnects, unregisters, and deletes all child managers |
| Per-output view | Every linked member is one separate containment and runtime view | `OriginalView` owns relationship membership; layout owns runtime registration | Member containment ID plus `isClonedFrom` and `linkPlacement` | Duplicate Dock clears this relationship | Derived members follow screen-group output policy; explicit members own output and edge | Derived members edit the root; explicit members edit themselves | Member unregisters from the root before destruction |
| Linked relationship | Shared logical group, explicit and acyclic | Root persistent dock | Member `isClonedFrom=<root id>` | Create Linked extends the direct-root group; Duplicate never copies it | `linkPlacement` names derived versus member-local placement ownership | Menu wording and edit targeting use the same runtime role | Root removal destroys members; member removal unregisters without dangling entries |
| Applet layout | Unique QML object and applet IDs per containment | Containment runtime | Applet groups under containment ID | Deep-copied and ID-remapped | Local geometry recalculates for that view | Its containment controls edit presentation | Destroyed with containment and view |
| Geometry controller | Unique `Positioner` per runtime view | Runtime `View` QObject parent | Derived, not persisted | Constructed fresh | Consumes that view's output and owns monotonic relocation generations | Edit thickness may change local placement | QObject parent destruction cancels timers and callbacks |
| Applet sizing | Unique `AutoSize` and effective icon state per containment | Containment QML tree | Configured values only | Config copied; effective values recomputed | Consumes the same view's solved primary length | Recomputes only for local edit geometry | QML tree destruction |
| Edit-mode controller | One reusable chrome ensemble, one current owner, one requested generation | `ViewSettingsFactory` under Corona | Not persisted | Never copied | Retarget is one cancelable generation | Exactly one effective owner; clone requests resolve explicitly | Cancels deferred work and clears the old containment before rebinding |
| Stacking controller | Missing in the current tree | Required owner: layout or Corona placement domain | Required key: output, edge, stable rank | New duplicate gets a stable independent rank | Re-solves membership atomically | Routes activation without merging edit identity | Removes membership before view destruction |
| Configuration object | Unique mutable map per containment; applet configuration is synchronized explicitly | Containment | Containment configuration group | Deep-copied into fresh groups for Duplicate Dock | Explicit members keep containment placement and appearance local | Menu and edit relationship are read from runtime identity | Destroyed with containment; signal contexts are the receiving view |

## Confirmed causes

### Duplicate Dock retained source relationship fields (fixed)

The creation path always copied a stored template, but its captured `Data::View`
also carried relationship state. Two concrete paths violated the independent
snapshot contract:

1. Duplicating an ordinary All Screens original copied
   `screensGroup=AllScreensGroup`. The imported containment was a fresh
   `OriginalView`, but `OriginalView::syncClonesToScreens()` immediately created
   another persistent containment with `isClonedFrom=<new original id>`.
   `ClonedView` then retained `m_originalView` and installed live configuration
   synchronization connections. The result was a second linked ensemble, not
   one independent dock.
2. Duplicating a visible linked replica captured its legacy `isClonedFrom`
   source. Storage first orphaned the unmapped source, then the captured
   `Data::View` overlay wrote the old source ID back. Earlier D77 work avoided
   the fault by refusing Duplicate at replica action boundaries, but that also
   removed the promised operation from an ordinary visible dock.

Duplicate Dock now clears `isClonedFrom` and normalizes `screensGroup` to
`SingleScreenGroup` before import. It is available from either relationship
role and creates exactly one fresh containment with remapped applet IDs. Export,
move, and remove remain owned by the relationship original. Existing persisted
linked layouts are not rewritten and remain linked during migration.

Launcher groups remain orthogonal. A copied Tasks applet can still carry an
explicit Layout or Global launcher-group choice, just as any two independent
docks can opt into that shared launcher list. That opt-in content service is not
a dock replica relationship.

### Explicit linked placement was indistinguishable from derived fanout (fixed)

The legacy relation had one persisted marker, `isClonedFrom`, and every reader
interpreted it as disposable screen-group fanout. Reusing that marker alone for
a user-placed linked dock caused screen reconciliation and startup cleanup to
move or delete the new member. It also gave containment-level configuration and
edit targeting the broader legacy synchronization scope.

Each linked member now carries a typed placement policy. `ScreenGroupDerived`
retains existing behavior. `ExplicitTarget` gives the member local output,
edge, alignment, visibility, appearance, removal, and edit presentation while
the direct root coordinates applet membership, order, and settings. Startup
loads roots before members and only regenerates derived records. The relation
graph is validated as direct-root and acyclic before D-Bus observation.

### Programmatic applet creation bypassed linked synchronization (fixed)

The linked relation listened for `ContainmentInterface::appletCreated`, but the
successful plug-in-ID creation path never emitted it. A new applet therefore
appeared only in the addressed containment. The external creation boundary now
announces one relationship event, while coordinator fanout uses a local-only
creation path to prevent feedback. Every member receives the same plug-in
multiset with fresh applet IDs.

### Rapid relocation had two unowned completion paths (fixed)

A previous reveal animation could clear `inRelocationHiding` after a newer hide
claimed it, stranding the new screen or edge request. Delayed 100 ms completion
callbacks also carried no request identity and could publish completion for a
newer move. The next hide now stops the old reveal before claiming ownership,
and every move carries a monotonic generation. Superseded callbacks are
discarded. `geometrySettled` becomes true only after the newest generation is
applied and the view's screen, geometry, validation, and reveal queues drain.

### Removed containments remained persistent (fixed)

Plasma keeps a removed containment alive during its 60-second Undo window.
MultipleLayouts synchronization copied that transient QObject back into the
layout file, while a missing notification service could prevent libplasma's
`KNotification::closed` cleanup from ever committing the destruction. Storage
now treats destroyed containments and their owned subcontainments as persistence
tombstones. GenericLayout owns a same-duration fallback transaction that Undo
or final destruction cancels, so runtime and persistent retirement agree.

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

1. Make relationship creation explicit. Duplicate Dock accepts either relation
   role, clears both dock-relationship fields, and creates remapped containment
   and applet IDs. Screen-group construction sets a derived relationship;
   Create Linked Dock… sets the explicit user-placed policy. Both point
   directly to the same validated root model.
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
7. Replace blanket replica configuration mirroring with explicit content and
   placement projections. Existing screen-group replicas may retain derived
   placement as a named policy; independent duplicates share no dock-owned
   mutable state.
8. Keep runtime identity diagnostics in C0 (the atomic dock-system observability
   snapshot). Relationship changes preserve its fail-closed graph, per-view
   ownership contract, and requested-versus-applied relocation generations.

## Implementation and verification slices

1. [x] Relationship capability policy, independent Duplicate Dock from original
   and replica sources through both live-view and layouts-dialog entry paths,
   edit targeting, lifecycle cleanup, exact Wayland identity, owner-scoped
   registrations, and persistent context-menu identity.
2. [ ] Placement normalization and bounded visible geometry across all edges and
   semantic alignments.
3. [ ] Same-edge stack solving, persistence, layer-shell application, reserved
   space, and activation routing.
4. [x] Multi-view nested-KWin fixtures and deterministic operation replay across
   duplication, movement, orientation, alignment, editing, destruction, and
   reload.
5. [x] Explicit linked relationship creation, member-local placement and edit
   ownership, applet synchronization, persistent restoration, and removal.

The completed slices are covered by sanitizer-backed pure-core tests for action
policy, retarget request generations, exact title matching, and ignored-window
ownership, plus a production source contract and successful focused production
compiles. Nested KWin drove the edit-retarget cancel race within 178 ms and
observed both views through the old timeout. A dual-output nested run created an
All Screens original and linked replica, duplicated from each, observed exactly
one independent containment per call with disjoint applet IDs, and verified all
four identities after restart. A visibility-mode change propagated from the
original to its linked replica but bypassed both duplicates, proving the
production synchronization connection retained only its intended members. The
first cold review found D95 (layouts-dialog Duplicate preserves linked
relationship state) and D96 (Duplicate settings inventory still claims linked
exclusion). Their corrections pass the focused data-type, source-contract, and
settings-inventory tests. The mandatory second cold review found D97
(independent snapshot test ignores transient view fields), not a production
fault: persistence equality deliberately omitted five transient fields from the
preservation check. Direct assertions now cover those fields. The final
canonical repository gate passed at exact pre-merge head
`defaa0c7ad1a0e376937bf07f035430ecc977407` with 104/104 CTest entries,
coverage and QML ratchets, 13 scene probes, three sanitizer recipes, and the
deterministic output matrix. GitHub rebased the validated source and test tree
through `b6ba7ab15`; the documentation-only merge tail `8f2c3073d` changes no
validated source or test content. The explicit-linked acceptance then created
two direct-root members, including one on an already occupied edge and one on a
separated portrait output, kept every runtime and applet identity disjoint,
proved local placement, visibility, sizing, and edit behavior, and restored the
same relationship after restart. Seed 127934575 replayed 28 placements, seven
edit transitions, two independent duplicates, removal, replacement, and reload;
the settled geometry and sizing snapshot remained unchanged after two seconds.

Each slice requires a failing regression first, pure-core ASan and UBSan tests
where a value model can carry the invariant, nested-KWin state and render
agreement for compositor behavior, and an independent cold diff review before
merge.

## Open questions requiring runtime evidence

- Linked members have no detach action yet. Removing the root removes the
  relationship; a relationship-aware root-removal choice dialog also remains
  future UX work.
- Which same-edge reservation policy best matches intended daily-driver
  behavior for mixed visibility modes. The stack still requires deterministic
  physical order under every policy.
- Whether partial dock-to-dock corner avoidance should remain under Wayland or
  be replaced by the compositor's edge-wide work-area contract. The current
  split cannot remain implicit.
