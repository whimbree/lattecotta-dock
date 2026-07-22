# Dock duplication and linked-dock design

Design direction settled 2026-07-21. Status: Duplicate Dock independence is
implemented by D77 (dock duplication retains clone lineage and edit ownership).
Create Linked Dock… remains design-only.

## User-facing operations

The interface names three operations by what they do:

1. **New Dock** creates a dock from a selected template.
2. **Duplicate Dock** creates one independent snapshot of the visible source.
3. **Create Linked Dock…** will create a synchronized copy with explicit
   placement. This action is not implemented yet.

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

D77 enforces that boundary before import by setting legacy `isClonedFrom` to
`-1` and normalizing `screensGroup` to `SingleScreenGroup`. This second field is
load-bearing: copying `AllScreensGroup` from an ordinary original used to make
`OriginalView::syncClonesToScreens()` create another persisted replica and
install `ClonedView` synchronization immediately after the template copy.
Copying from a visible replica had a second failure mode because the captured
`Data::View` could restore its old `isClonedFrom` after Storage orphaned the
unmapped source. Refusing the replica action avoided that fault but did not
satisfy the Duplicate Dock contract; the operation now works from both roles.

The copied Tasks configuration can still explicitly select Unique, Layout, or
Global launcher groups. Layout and Global groups are independent content
services that intentionally synchronize pinned launchers across otherwise
independent docks. They do not create a dock replica relationship, retain a dock
source pointer, or merge edit and placement ownership.

## Existing linked docks

The current On All Screens and On All Secondary Screens settings create linked
screen-group members. The original and every linked member have distinct
containment, applet, runtime, output, placement, geometry, and edit-registration
identities. The relationship itself is one explicit logical group:

- the original owns relationship membership;
- each persisted linked member carries legacy
  `isClonedFrom=<original containment id>`;
- each runtime legacy `ClonedView` holds a guarded pointer to the original;
- content and selected configuration properties synchronize through explicit
  connections;
- output and runtime geometry remain local to each member;
- edit requests resolve to one authoritative relationship target.

Existing persisted linked layouts remain linked during migration. D77 does not
rewrite their records or detach their members.

## Create Linked Dock… target

Create Linked Dock… will expose the linked relationship independently of the
screen-group selector. It creates a synchronized copy that can be placed by an
explicit screen and edge choice, including on the same screen as the original.

The target implementation must satisfy these rules:

1. **Relationship identity.** One explicit relationship identity owns an
   original and its linked members. A member is keyed by persistent member
   identity, not by screen ID. Screen is a property because more than one
   linked member may eventually occupy the same output.
2. **Fresh per-member identity.** Every linked member receives fresh persistent
   containment and applet IDs, a fresh runtime view, fresh output and placement
   state, fresh geometry controllers, and a distinct edit registration.
3. **Synchronization scope.** Content synchronization is explicit. Placement,
   output, edge, alignment, visibility policy, runtime geometry, and transient
   edit presentation are not blanket-copied through the relationship.
4. **One relationship policy.** Menu capabilities, edit targeting, lifecycle,
   persistence, and migration derive from the same relation. No surface infers
   relationship semantics from window title, pointer ancestry, or presentation
   wording.
5. **Editability.** A linked member opens the relationship's content settings
   with visible relationship context. Placement controls act on the selected
   member. The shared edit chrome still has exactly one effective owner and one
   cancelable pending target.
6. **Lifecycle.** Removing the original offers relationship-aware choices such
   as removing linked members or detaching them. Detach clears the relation and
   leaves fresh independent identities intact.
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
- [x] Duplicate Dock normalizes relationship state and works from original and
      linked-member sources (D77, 2026-07-21). The dual-output nested acceptance
      created exactly one independent dock per invocation with disjoint applet
      IDs, observed a visibility-mode change propagate only inside the existing
      relationship, and kept identities stable across restart.
- [ ] Add the Create Linked Dock… action and placement flow.
- [ ] Replace screen-ID-keyed membership assumptions with persistent member
      identity where same-output linked placement requires it.
- [ ] Add explicit relationship schema and migration fixtures before retiring
      legacy clone terminology from internal APIs.
