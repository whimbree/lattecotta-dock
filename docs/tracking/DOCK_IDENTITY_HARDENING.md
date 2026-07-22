# Dock identity, placement, and replication hardening

Status: independent Duplicate Dock and explicit Create Linked Dock implemented;
linked runtime lifecycle and sizing ownership hardened; bounded placement and
same-edge stacking slices remain separate work
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
| Logical dock | One independent containment, or one original plus its linked members | Original persistent containment | Original containment ID | Duplicate Dock creates one new independent logical dock | Identity stays stable | Selects the intended edit target | Ends when the original and all linked members are removed |
| Persistent dock record | Unique per containment | Layout configuration | Containment ID | All containment and applet IDs are regenerated | Screen, edge, alignment, and length state update on the same record | Supplies menu and edit target identity | Removed with the containment configuration |
| Containment | Unique per visible original or clone | Plasma Corona and layout | Containment ID | Must be newly imported with remapped IDs | Persists placement and QML configuration | Own `userConfiguring` is the per-view edit presentation state | Destroyed by layout removal; no runtime view may retain it |
| Runtime dock view | Unique QObject and QWindow per active containment | `GenericLayout` containment-to-view map | Not persisted | Constructed fresh | Remaps or recreates its layer surface | Holds only a non-owning pointer to shared edit chrome | Disconnects, unregisters, and deletes all child managers |
| Per-output view | Every linked member is one separate containment and runtime view | `OriginalView` owns relationship membership; layout owns runtime registration | Member containment ID plus `isClonedFrom` and `linkPlacement` | Duplicate Dock clears this relationship | Derived members follow screen-group output policy; explicit members own output and edge | Derived members edit the root; explicit members edit themselves | Member unregisters from the root before destruction |
| Linked relationship | Shared logical group, explicit and acyclic | Root persistent dock | Member `isClonedFrom=<root id>` | Create Linked extends the direct-root group; Duplicate never copies it | `linkPlacement` names derived versus member-local placement ownership | Menu wording and edit targeting use the same runtime role | Explicit root removal is refused while members persist; member removal unregisters without dangling entries; runtime unload never deletes persistence |
| Applet layout | Unique QML object, applet ID, and config group per containment; one explicit content projection per relationship | Root view coordinates structural mutations; each containment owns its projection and per-view geometry keys | Applet groups under containment ID | Deep-copied and ID-remapped; linked import clears per-view `length` | Local geometry and applet length recalculate for that view | Its containment controls edit presentation | Root transaction removes every projection; Undo recreates member-local identities and shared config while keeping local geometry excluded; containment teardown destroys the remainder |
| Geometry controller | Unique `Positioner` per runtime view | Runtime `View` QObject parent | Derived, not persisted | Constructed fresh | Consumes that view's output and owns monotonic relocation generations | Edit thickness may change local placement | QObject parent destruction cancels timers and callbacks |
| Applet sizing | Unique `AutoSize`, effective icon state, and applet `length` per containment | Containment QML tree | Configured dock values plus per-applet local `length` | Independent Duplicate copies the snapshot; linked creation clears local `length`; effective values recompute | Consumes the same view's solved primary length and orientation | Recomputes only for local edit geometry | QML tree destruction |
| Edit-mode controller | One reusable chrome ensemble, one current owner, one requested generation | `ViewSettingsFactory` under Corona | Not persisted | Never copied | Retarget is one cancelable generation | Exactly one effective owner; clone requests resolve explicitly | Cancels deferred work and clears the old containment before rebinding |
| Stacking controller | Missing in the current tree | Required owner: layout or Corona placement domain | Required key: output, edge, stable rank | New duplicate gets a stable independent rank | Re-solves membership atomically without assuming adjacent outputs | Owns one activation mechanism with the exact eligible region set, which may be disjoint | Removes membership before view destruction |
| Configuration object | Unique mutable map per containment; an explicit policy selects linked applet keys | Containment owns storage; relationship root owns shared mutation routing; each view owns applet geometry keys | Containment configuration group | Deep-copied into fresh groups for Duplicate Dock; linked import removes per-view keys | Explicit members keep containment placement, appearance, and applet length local | Menu and edit relationship are read from runtime identity | Destroyed with containment; Undo and reconnect restore shared values without overwriting local keys; signal contexts are the receiving view |

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

### Applet structural mutations lacked one relationship owner (fixed)

The linked relation listened for `ContainmentInterface::appletCreated`, but the
successful plug-in-ID creation path never emitted it. Other add, drop, remove,
and reorder paths entered below the runtime `View` boundary or assumed member
applets had matching positions. A mutation could therefore update only the
addressed containment, route a member removal through its independent Plasma
transaction, or associate the wrong applet after an order change.

Every structural action now enters through the addressed runtime `View` and is
translated to the direct root. The root owns one transaction and projects the
result to every member through stable applet-ID maps. Programmatic order changes
publish the same order signal as pointer-driven changes. Member projection IDs
remain fresh and unrelated to root IDs.

Plasma Undo exposed a second ownership error. Mirroring the root applet's
scheduled-destruction flag left the member applet configuration persistent, so
shutdown inside the Undo window resurrected that projection. The root now owns
the only reversible Plasma transaction. Member projections are retired
immediately, then recreated with fresh local IDs and copied root configuration
if Undo reverses the transaction. Restart during the window preserves the
removal in every containment.

### Runtime teardown owned persistent relationship cleanup (fixed)

`OriginalView::~OriginalView()` called `cleanClones()`. That was safe only
under the legacy assumption that every clone was disposable screen-group
fanout. Runtime destruction also occurs during indicator reload, layout unload,
and output loss, so the destructor could remove persistent explicit members as
if the logical relationship had been deleted. Root recreation separately
replaced only the root runtime. Every surviving `ClonedView` then held a null
root `QPointer` and could not synchronize or edit coherently.

Runtime destruction no longer mutates persistent relationship membership.
Screen-group-derived records are retired only through the named output-policy
path. Recreating a root collects its complete live group, queues members before
the root for orderly unregistration, constructs the root before members, and
reconciles derived fanout only after the complete relationship has rebound.
The diagnostic driver is gated by `LATTE_DEBUG_DBUS=1`; `dockSystemData` proves
that runtime identities rotate while persistent containment IDs do not.

### Layouts-dialog Copy retained linked lineage (fixed)

Copy exported only the selected containment, but its clipboard record retained
`isClonedFrom` and `ExplicitTarget`. ID remapping could then leave the pasted
member pointing to no root or to an unrelated dock with the same numeric ID in
the destination layout. The destination relationship graph could fail startup
validation after restart.

Copy now converts every record through `toIndependentSnapshot()` before
clipboard publication. This is the same relation-breaking policy as Duplicate
Dock. The value operation clears both move-transaction flags as well as linked
lineage. Cut remains distinct because it preserves origin identity only for
the checked move transaction.

### Late layout-move refusal never completed relocation (fixed)

The positioner hid a dock before calling a void layout-move transaction. A root
could gain an explicit member during that animation. The final persistent guard
then refused the now-partial move correctly, but no `layoutChanged` signal
cleared the pending target, leaving the dock hidden and geometry unsettled.

The manager now returns a checked success value and refuses before unassigning
the source containment. On failure the positioner clears pending layout,
screen, edge, alignment, and screen-group state, then completes the same
generation through the ordinary delayed reveal path.

### Output eligibility consumed transient runtime screen state (fixed)

When an output disconnected, Qt temporarily reported the surviving primary
screen through the still-live window and Plasma containment. `validViewsMap()`
read that runtime-derived value, reclassified the explicit member as eligible
on primary, and kept its stale surface alive. Reading through the layout-aware
Storage overload did not fix the defect because that overload deliberately
overlaid the live containment screen onto the persistent record.

Output eligibility now reads a pending explicit placement transaction first
and otherwise reads the containment's persisted KConfig placement directly.
It never consults the transient QWindow or Plasma screen assignment. A removed
member output parks that runtime without deleting the persistent relationship.
Reconnect adds roots before members and reconstructs only records eligible on
currently active outputs. Runtime recreation repeats the same eligibility check
after its delay so an intervening disconnect cannot resurrect a stale surface.

### Reconnected members lacked an authoritative applet projection (fixed)

A member rebuilt after output reconnect began with the stale applet subtree
captured before it went offline. Root changes made while that member had no
runtime had no recipient, and startup readiness signals could arrive before
AppletQuickItem configuration maps existed. No later event guaranteed a full
structural and configuration comparison.

`ClonedView` now pauses member-originated feedback behind a reconciliation
barrier. Once both endpoint inventories and configuration maps are ready, it
prunes stale member applets, creates missing projections with fresh IDs, copies
shared configuration, and reapplies order, locked-zoom, and coloring lists.
Delayed configuration readiness emits the same progress signal, so completion
is event-driven rather than polled or silently abandoned.

### Applet-local length was mirrored across orientations (fixed)

The exact linked sizing reproducer kept `configuredIconSize`,
`effectiveIconSize`, and `availablePrimaryLength` stable, but the first
different state was the Tasks applet's dynamic `length` setting. ConfigOverlay
writes that key from the local resize handle using height for vertical views
and width for horizontal views. Linked signal forwarding and full KConfig
reconciliation treated it as ordinary shared content, so one orientation could
overwrite the other view's applet length and force its internal icon fit.

One compile-time policy now classifies `length` as per-view applet geometry.
Linked template import clears it, both live signal directions ignore it, Undo
restoration excludes it, and full reconnect reconciliation preserves the
target's local value or absence. Launcher settings and other content keys still
synchronize. Independent Duplicate Dock keeps the captured value because its
new containment owns an unrelated snapshot.

### Cross-layout moves could split a linked relationship (fixed)

The legacy layout move transaction can move an original plus derived All
Screens projections, but it has no transaction for independently persistent
explicit members. Moving a root or explicit member alone could leave a
cross-layout `isClonedFrom` reference. The layouts dialog added a second race:
Cut and Paste span multiple UI events, so a root could gain an explicit member
after Cut. Destination import then succeeded before source removal refused,
silently degrading Move into Copy.

The persistent `ViewsTable` now owns the move predicate. Runtime actions,
context menus, the layouts dialog, and `Layouts::Manager` all consult it. Save
re-reads and revalidates the current origin graph before importing any
destination, so a stale transaction cancels atomically with a visible warning.
Derived-only groups keep their legacy coordinated move.

### Persisted relationship graphs reached runtime partially validated (fixed)

Startup previously classified relationship records while constructing views.
A missing root, member-to-member edge, cycle, duplicate persistent ID, or
invalid placement policy could reach a null root cast or leave a plausible
partial runtime group. Storage now validates the complete table as one direct,
acyclic graph before creating any relationship runtime object. A malformed
graph is refused with the concrete validation error instead of being partially
loaded.

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

Single-layout Undo had the reverse fault: the forward tombstone deleted the
only complete persistent subtree, while Plasma revived only its in-memory
objects and partial groups. Saving those objects could not reconstruct linked
ownership, placement, subcontainments, and applet configuration exactly. The
layout now captures the owned subtree before removal, refuses a reversible
removal if that snapshot fails, and replaces Plasma's partial groups from the
snapshot on Undo. The containment ID and complete relationship record survive
Undo, while restart inside the window still observes the tombstone.

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

The required coordinator is keyed by Latte output identity and edge, not by
monitor adjacency. Portrait, landscape, overlapping-coordinate, fully touching,
partially touching, and separated output arrangements do not change stack
membership. KWin supplies each output's geometry; Latte must solve only within
the selected output rectangle. Activation ownership is one coordinator-owned
mechanism with one or more exact eligible regions. It must not widen two
separated partial-width docks into one continuous activation surface.

### Alignment changes bypass the placement invariant

`LengthOffsetClamp` expresses the correct length and offset limits, but only the
Maximum and Offset controls call it. Changing Center to Start or End writes the
new alignment without normalizing a negative center offset. The QML background
and applet layout then apply a negative edge margin and render past the output.
Startup and external configuration writes can bypass the same invariant.

### Partial-footprint peer resizing remains a separate hypothesis

The exact linked reproducer did not change the remote view's dock-level
`configuredIconSize`, `effectiveIconSize`, or `availablePrimaryLength`. Its
first divergent value was the applet-local `length` described above. This rules
out a shared AutoSize instance or global icon cache for that reproduction.

A separate reservation discrepancy remains plausible for independent
perpendicular docks. Layer shell publishes an edge-wide scalar exclusive zone,
while Latte's internal available-rectangle model applies an alignment-aware
partial footprint. Static reading suggests one dock's alignment could change a
neighbor's solved length without changing compositor work area, but that path
has not yet been demonstrated by the exact independent-dock nested matrix. It
remains a hypothesis for the stacking and placement slice, not a confirmed
cause of the linked sizing defect.

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

## Shared-state audit

No duplicate was found sharing a containment QObject, applet QObject, runtime
View, Positioner, AutoSize object, or mutable KConfig group with its source.
The confirmed violations were copied relationship fields and mutations routed
through the wrong authority:

| State or identity | Intended ownership | Confirmed violation | Status |
|---|---|---|---|
| `screensGroup` and `isClonedFrom` | Relationship policy on the new persistent record | Duplicate Dock retained both fields and therefore created or rejoined a linked group | Fixed; one const snapshot transformation clears the relation |
| Persistent containment ID | Unique per dock record | No shared ID found; every import remaps it | Regression-pinned |
| Applet ID | Unique per containment | Member-to-root routing used positional coincidence after reorder | Fixed; explicit ID maps route every mutation |
| Runtime view identity | Unique per live QObject | No copied runtime ID found; the opaque diagnostic token is process-local | Regression-pinned |
| Mutable containment configuration | Unique per containment | No shared KConfig object found; blanket linked mirroring gave applet-local `length` root-owned relationship semantics | Fixed through the compile-time shared-versus-local key policy on import, live signals, Undo, and reconciliation |
| Applet removal transaction | One logical content transaction at the relationship root | Scheduled-destruction state was mirrored into member-local transactions and persistence | Fixed; root owns Undo, members own disposable projections |
| Edit chrome | Intentionally one reusable factory-owned ensemble | Unversioned retarget callbacks and inconsistent linked target resolution left stale edit owners | Fixed; one cancelable generation and one authoritative relationship target |
| Geometry and sizing controllers | Unique per runtime view | No shared cache found; stale relocation callbacks crossed generations, transient screen state overrode persisted placement, and applet `length` crossed orientations | Relocation, output authority, and linked applet length fixed; independent reservation and stacking slices remain open |
| Runtime relationship membership | Persistent records outlive runtime views; live members require a live root generation | Root destruction deleted persistent members, while root-only recreation stranded surviving member pointers | Fixed; teardown is nonpersistent, recreation replaces the complete eligible group root-first |
| Cross-layout move transaction | One independent dock or a coordinated derived-only group | Explicit roots and members could move alone; stale Cut could import before later source refusal | Fixed; one persistent predicate is revalidated before any destination import |
| Layouts-dialog clipboard | Independent snapshots for Copy; origin identity only for Cut | Copy retained a linked member's root ID while importing only that member | Fixed; Copy normalizes through `toIndependentSnapshot()` before publication |
| Relocation completion | Every started generation either commits or cancels and reveals | Late manager refusal returned no result, so a hidden dock retained its pending layout | Fixed; checked refusal clears the full request and schedules normal completion |
| Same-edge rank and activation regions | Shared only through a future output-edge coordinator | No authority exists, so every surface independently claims the same edge | Open |

## Deterministic replay results

| Scenario | Before | After linked-identity repair |
|---|---|---|
| Duplicate an All Screens source | A fresh root immediately produced another linked ensemble | Exactly one independent containment with fresh applet IDs |
| Duplicate from a linked member | Relationship lineage could be retained or the action was hidden | Exactly one independent containment; the source group remains unchanged |
| Rapid output, edge, and alignment changes | An older reveal could clear a newer move and strand geometry | Seed 127934575 completed 28 placements and seven edit transitions; two settled snapshots remained byte-equivalent for geometry and sizing |
| Disconnect and reconnect a member output after root changes | Runtime screen fallback kept the member mapped to primary; offline applet changes had no guaranteed replay | The member runtime parks, its persistent record remains, and reconnect restores exact shared applet content while preserving local `length` |
| Recreate a linked root runtime | Only the root rotated, leaving member root pointers stale | Root and all live members receive fresh runtime IDs root-first; the unrelated Duplicate and every persistent containment ID stay unchanged |
| Disconnect the root output while members target another output | Members could outlive the missing runtime coordinator or teardown could remove persistence | The complete linked runtime group parks; every containment record remains; reconnect rebuilds a fresh complete generation |
| Copy a linked member between layouts | Paste could preserve a missing or unrelated numeric root reference | The pasted dock is an independent snapshot with no relationship lineage |
| Reject a move after its hide animation starts | No failure result cleared the pending layout, so the dock remained hidden | Refusal occurs before unassignment; all pending placement state clears and the same generation reveals normally |
| Remove and recreate a member | Persistent tombstones could be overwritten or never committed | The removed member leaves runtime and disk, replacement receives a fresh identity, and reload restores only intended records |
| Remove a linked applet, then restart inside Undo | A member projection could resurrect | Root and every member remain removed in runtime and persistence |
| Invoke real dock and applet Undo | No executable relationship-aware coverage | Applet projection, full dock subtree, direct-root relationship, and reload all recover deterministically |

Same-edge physical composition and final alignment bounds are deliberately not
listed as passing. Their independent coordinator and normalization slices have
not landed yet.

## Minimal architectural repair

1. Make relationship creation explicit. Duplicate Dock accepts either relation
   role, clears both dock-relationship fields, and creates remapped containment
   and applet IDs. Screen-group construction sets a derived relationship;
   Create Linked Dock… sets the explicit user-placed policy. Both point
   directly to the same validated root model.
2. Route linked applet structure through the direct root while retaining unique
   per-containment applet identities and configuration groups. Validate the
   complete persisted relation before constructing any runtime member.
3. Give each deferred edit-chrome retarget a monotonic process-local request
   generation. Delayed work carries both the target and request generation and
   is rejected when superseded.
4. Make edit-chrome ownership a single transition with one cancelable pending
   target. Ending the old containment's configuring session precedes every
   rebind.
5. Route every placement mutation through one normalization transaction for
   output, edge, semantic alignment, minimum and maximum length, and offset.
6. Add one output-edge stack coordinator with stable persisted ranks,
   cumulative insets, activation ownership over an exact region set, and a
   single reservation policy.
7. Keep compositor work-area reservation separate from Latte's partial
   dock-to-dock avoidance footprint. Autosizing consumes only the final solved
   geometry for its own view.
8. Replace blanket replica configuration mirroring with explicit shared-content
   and per-view geometry projections. Applet `length` is local across both
   explicit and screen-group-derived members. Independent duplicates share no
   dock-owned mutable state.
9. Keep runtime identity diagnostics in C0 (the atomic dock-system observability
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
   ownership, root-coordinated applet synchronization from every member,
   validated startup restoration, reversible removal, restart tombstones,
   per-view applet sizing keys, output lifecycle, and whole-group runtime
   recreation.

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
The first explicit-linked cold review then found mutation routing below the
relationship boundary, positional applet mapping, incomplete startup graph
validation, untested real Undo, missing current copyright lines, and stale
D-Bus prose. The corrections route structural mutations from every member,
publish programmatic order, validate the whole persisted graph before runtime,
and drive libplasma's real notification action. The expanded notification
recipe proves applet and dock Undo, reload after Undo, and shutdown during an
applet Undo window. The focused storage test proves that Undo replaces a partial
group with the exact relationship and applet snapshot. The first final gate
also caught a reusable widget-explorer delegate reaching past its injected page
contract to production's `latteView`; the corrected page boundary passes the
accessible press-action regression and shrinks the qmllint baseline.
The pre-rereview canonical gate passed on the tree now represented by
`65b27ec43` after history cleanup, with 104/104 CTest entries,
5,831 curated full-stage QML warnings, 13 render probes, three nested sanitizer
recipes, and the complete output-matrix fixture.
The final cold rereview found that linked-root teardown did not own one
reversible transaction, persisted graph validation accepted noncanonical IDs
and incompatible explicit placement roles, and the relationship-aware applet
removal wrapper did not preserve source visibility. Root removal is now refused
while explicit persistent members remain, with derived All Screens teardown
unchanged. Value and KConfig fixtures cover the expanded graph domain, and the
menu wrapper copies visibility. A group-wide root-removal transaction remains
open lifecycle work.
The post-review canonical gate found D114 (linked-source removal controls
raised the QML warning baseline). The shell-provided `latteView` and `i18n`
boundary is now explicit around the complete touched binding block. This
removes all five introduced warnings plus three inherited warnings from that
block. The final gate passed at exact source and test head
`8e703bb83694db8cbf072561dfc0ad6cb87f90d2` with 104/104 CTest entries,
5,828 curated warnings across 234 eligible QML files, all 13 render probes,
three nested ASan/UBSan recipes, and the complete output-matrix fixture.

The post-review lifecycle extension then disconnected an explicit member's
separated portrait output, changed root applet structure while the member was
offline, and required exact shared configuration convergence after reconnect.
It recreated the root through the debug-gated production reload path and
observed fresh runtime identities for the complete linked group while the
independent Duplicate retained its runtime identity. It moved the root onto the
portrait output, disconnected that output, observed only the independent dock
remain live, then reconnected and recovered the complete persistent group. The
Tasks applet's local `length` remained stable through root alignment, shared
launcher edits, output reconnect, and root runtime recreation. The removal and
Undo recipe, seed 127934575 operation storm, and independent Duplicate recipe
also pass after these lifecycle changes.

Each slice requires a failing regression first, pure-core ASan and UBSan tests
where a value model can carry the invariant, nested-KWin state and render
agreement for compositor behavior, and an independent cold diff review before
merge.

## Open questions requiring runtime evidence

- Linked members have no detach action yet. Root removal is refused while
  explicit members remain because one-containment Plasma Undo cannot restore
  the complete relationship. A group-wide transaction and root-removal choice
  dialog remain future lifecycle work.
- Which same-edge reservation policy best matches intended daily-driver
  behavior for mixed visibility modes. The stack still requires deterministic
  physical order under every policy.
- Whether partial dock-to-dock corner avoidance should remain under Wayland or
  be replaced by the compositor's edge-wide work-area contract. The current
  split cannot remain implicit.

## Current release severity

- **Release blocker:** missing same-edge stack order, cumulative inset,
  reservation, and exact activation-region ownership. Supported layouts can
  overlap or reserve space nondeterministically.
- **Release blocker:** Start and End alignment can leave the rendered rectangle
  outside its selected output because alignment bypasses placement
  normalization.
- **Beta blocker:** the partial-footprint and layer-shell reservation
  disagreement may resize an independent perpendicular dock after a peer
  alignment change. Static ownership is inconsistent, but the independent-dock
  executable matrix still has to confirm or reject this separate hypothesis.
- **Known issue:** linked members have no Detach action or relationship-aware
  root-removal choice dialog. Linked roots remain protected until their
  explicit members are removed. Enabling root removal without one group-wide
  transaction is a release blocker for that future milestone.
- **Cosmetic:** legacy `ClonedView`, `isCloned`, and `isClonedFrom` names remain
  in internal and persisted compatibility APIs. User-facing actions use
  Duplicate Dock and Create Linked Dock… consistently.
