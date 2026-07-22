# Dock duplication and linked-dock design

Design direction settled 2026-07-21 and implemented 2026-07-22. Duplicate Dock
is an independent snapshot. Create Linked Dock… is the explicit synchronized
operation.

## User-facing operations

The interface names three operations by what they do:

1. **New Dock** creates a dock from a selected template.
2. **Duplicate Dock** creates one independent snapshot of the visible source.
3. **Create Linked Dock…** creates a synchronized copy on a selected active
   output and edge, with member-local placement.

Duplicate, Replicate, and Clone must not appear as three neighboring actions.
Those words read as synonyms without internal context. The synchronized action
therefore says Create Linked Dock…, while the internal architecture may use
the precisely defined term **replica relationship**.

`ClonedView`, `isCloned`, and `isClonedFrom` are legacy compatibility and
migration terms. They continue to represent existing linked layouts on disk and
in source while migration is incremental, but they are not user-facing action
names.

## Duplicate Dock contract

Duplicate Dock is a one-time snapshot. The new dock receives:

- a fresh logical dock identity;
- a fresh persistent containment ID;
- fresh containment and applet IDs;
- a fresh runtime `OriginalView` and manager ensemble;
- its own edit ownership and configuration groups;
- copied configuration values at the instant of duplication.

It retains no dock source pointer and no dock-level synchronization connection.
The source may be either an original or any visible member of an existing
replica relationship. Both entry paths produce exactly one independent dock.

D77 enforces that boundary through the const
`Data::View::toIndependentSnapshot()` transformation before import. The
transformation sets legacy `isClonedFrom` to `-1` and normalizes `screensGroup`
to `SingleScreenGroup`, while leaving the source value and copied configuration
unchanged. Both the live dock action and the layouts dialog call the same
boundary. The second field is load-bearing: copying `AllScreensGroup` from an
ordinary original used to make `OriginalView::syncClonesToScreens()` create
another persisted replica and install `ClonedView` synchronization immediately
after the template copy. Copying from a visible replica had a second failure
mode because the captured `Data::View` could restore its old `isClonedFrom`
after Storage orphaned the unmapped source. Refusing the replica action avoided
that fault but did not satisfy the Duplicate Dock contract; the operation now
works from both roles and both Duplicate interfaces.

The copied Tasks configuration can still explicitly select Unique, Layout, or
Global launcher groups. Layout and Global groups are independent content
services that intentionally synchronize pinned launchers across otherwise
independent docks. They do not create a dock replica relationship, retain a dock
source pointer, or merge edit and placement ownership.

## Linked docks

On All Screens and On All Secondary Screens create derived linked members.
Create Linked Dock… creates explicitly placed linked members. The root and
every linked member have distinct
containment, applet, runtime, output, placement, geometry, and edit-registration
identities. The relationship itself is one explicit logical group:

- the original owns relationship membership;
- each persisted linked member carries `isClonedFrom=<root containment id>`
  and a typed `linkPlacement` policy;
- each runtime legacy `ClonedView` holds a guarded pointer to the original;
- every applet instance and configuration group remains containment-local;
- the root is the single structural transaction coordinator for applet add,
  drop, remove, reorder, and Undo, regardless of which member originated the
  action;
- applet membership, order, and shared settings synchronize as explicit
  projections with stable root-to-member identity maps, never by equal local
  IDs or positional coincidence;
- applet `length` remains per-view because ConfigOverlay writes it from the
  local orientation and resize handle. Linked template import, live signals,
  Undo restoration, and reconnect reconciliation all exclude that key;
- output, edge, alignment, visibility, appearance, edit presentation, and
  runtime geometry remain local to an explicitly placed member;
- derived members retain the legacy root-owned screen-group placement and edit
  target;
- menu wording and edit behavior derive from the same runtime relationship
  role.

Existing persisted linked layouts remain linked during migration. D77 does not
rewrite their records or detach their members.

## Create Linked Dock… contract

Create Linked Dock… exposes the linked relationship independently of the
screen-group selector. It can target any active output and edge, including an
edge already occupied by another dock or panel. Occupied edges are valid input;
physical stacking is a separate placement-coordinator responsibility.

The target implementation must satisfy these rules:

1. **Relationship identity.** The persistent root containment is the logical
   relationship identity. Every member points directly to it. A member is keyed
   by persistent containment identity, never by screen ID, so multiple members
   can occupy the same output and edge.
2. **Fresh per-member identity.** Every linked member receives fresh persistent
   containment and applet IDs, a fresh runtime view, fresh output and placement
   state, fresh geometry controllers, and a distinct edit registration.
   Layouts-dialog Copy also produces independent snapshots; Cut alone preserves
   origin identity for its checked move transaction.
3. **Synchronization scope.** Content synchronization is explicit. Placement,
   output, edge, alignment, visibility policy, runtime geometry, applet-local
   length, and transient edit presentation are not blanket-copied through the
   relationship.
4. **One relationship policy.** Menu capabilities, edit targeting, lifecycle,
   persistence, and migration derive from the same relation. No surface infers
   relationship semantics from window title, pointer ancestry, or presentation
   wording.
5. **Editability.** An explicitly placed member opens its own edit presentation
   as Edit Linked Dock or Panel. Placement and containment settings act on the
   selected member. Applet changes flow through the root content coordinator.
   The shared edit chrome still has exactly one effective owner and one
   cancelable pending target.
6. **Lifecycle.** An explicitly placed member owns its own Remove action and
   unregisters from the root. Runtime removal immediately writes a persistence
   tombstone. Plasma Undo restores the same containment ID and its complete
   pre-removal subtree from a transaction snapshot. Root removal is temporarily
   refused while persistent explicit members remain because Plasma provides one
   containment transaction and cannot atomically restore the complete
   relationship. The settings and context-menu surfaces explain that linked
   members must be removed first. Derived All Screens members do not trigger
   this gate. A group-wide transaction, detach action, and root-removal choice
   dialog remain future lifecycle work. Destroying or recreating a runtime
   view never removes its persistent relationship record. Replacing a root
   runtime replaces and rebinds its complete live member group root-first.
   Output disconnect parks ineligible runtimes while preserving their records;
   reconnect reconstructs the root before members and reconciles the full
   applet projection.
7. **Migration.** Legacy `ClonedView` and `isClonedFrom` records load as linked
   relationships without changing behavior. Any schema migration must be
   explicit, reversible at the file boundary, and covered by real persisted
   fixtures.

## Prerequisites and status

- [x] Applet configuration synchronization establishes at all (`c3d15966`,
      2026-07-12).
- [x] Applet order persists and snapshot copies are order-faithful
      (`9a6f8fb8`, 2026-07-12).
- [x] Deferred structural synchronization retries when applet ID mapping becomes
      ready (`e3fdcae78`, 2026-07-16).
- [x] Existing screen-group relationships were verified live on two monitors
      with per-position applet identity readback (`f7561df37`, 2026-07-16).
- [x] Duplicate Dock normalizes relationship state through one const value
      transformation used by the live view and layouts dialog, and works from
      original and linked-member sources (D77, 2026-07-21). The dual-output nested acceptance
      created exactly one independent dock per invocation with disjoint applet
      IDs, observed a visibility-mode change propagate only inside the existing
      relationship, and kept identities stable across restart.
- [x] Add the Create Linked Dock… action and active-output/edge placement flow
      (`ea7a77f0e`, 2026-07-22).
- [x] Key relationship membership by persistent member identity and separate
      explicit placement from screen-group-derived placement
      (`6a9183fc6`, `fe1230670`, 2026-07-22).
- [x] Persist and validate the explicit placement policy while retaining the
      legacy default for existing layouts (`6a9183fc6`, `c53887f9b`,
      2026-07-22).
- [x] Synchronize applet creation without feedback and keep every applet
      instance identity disjoint (`148da3e1b`, 2026-07-22).
- [x] Route add, drop, remove, reorder, configuration, and Undo through the
      direct root from every member. Member projections retain separate applet
      identities; Undo may allocate a fresh member-local ID while copying the
      root configuration (`1457ab790`, `c9f74689c`, `5bcde4f40`, 2026-07-22).
- [x] Validate the complete persisted direct-root graph before runtime
      construction and refuse missing targets, chains, cycles, duplicate IDs,
      noncanonical containment IDs, and invalid placement roles (`683a17048`,
      `5b8bb9542`, 2026-07-22).
- [x] Persist removal tombstones before restart and restore Plasma Undo from an
      exact pre-removal snapshot in single-layout mode (`f1a76d7a4`,
      `e781b4d0b`, `c69ad6e86`, `98dcbf894`, 2026-07-22).
- [x] Cover exact creation, occupied-edge coexistence, local edit and placement,
      sizing isolation, removal, deterministic operation replay, and restart
      (`05bcb00c5`, `19ef4ff7d`, `5c90f9431`, 2026-07-22).
- [x] Refuse linked-root removal at every runtime and persistence boundary until
      a group-wide reversible transaction exists. Keep derived All Screens
      removal available and prove refusal changes no runtime, persisted, or
      notification state (`184370cdc`, 2026-07-22).
- [x] Keep runtime recreation and output availability separate from persistent
      relationship ownership. Replace complete live groups root-first, park
      members whose persisted output is inactive, and rebuild applet
      projections after reconnect (2026-07-22).
- [x] Define applet `length` as per-view geometry state and exclude it from
      linked template import, both live configuration directions, Undo restore,
      and full reconnect reconciliation. Shared launcher configuration remains
      synchronized (2026-07-22).
- [x] Refuse cross-layout moves for explicit relationships at the view, menu,
      layouts-dialog, manager, and final Cut/Paste transaction boundaries.
      Revalidate the current persisted origin immediately before import so a
      stale Cut cannot degrade into Copy (2026-07-22).
- [x] Normalize layouts-dialog Copy through the same independent-snapshot
      policy as Duplicate Dock. A copied member never carries a cross-layout
      root ID into Paste (2026-07-22).
- [x] Make the layout move transaction report success. A relationship that
      becomes ineligible during the hide animation is refused before source
      unassignment, then the positioner cancels every pending placement
      component and reveals the dock through normal settlement (2026-07-22).
- [ ] Add detach and relationship-aware root-removal choices.
- [ ] Retire legacy clone terminology from internal APIs in a dedicated
      migration after persisted compatibility no longer depends on it.
