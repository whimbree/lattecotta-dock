# Dock duplication and replication: semantics and the Replicate feature

Design plan agreed 2026-07-12 (user + assistant discussion). Status:
DESIGN ONLY - no implementation yet. Prerequisites are tracked at the
bottom; two of them landed the same day.

## The three mechanisms that exist today

**Duplicate Dock** (context menu, and D-Bus `duplicateView(u)`): a
one-time snapshot copy. The new containment gets the original's config
at that moment and is fully independent afterwards. Changes to the
original (pins, applets, settings) do NOT propagate. As of 9a6f8fb8 the
copy is order-faithful; before that fix every dock's stored order was
wiped at startup, so duplicates came out scrambled.

**Screens-group clones** ("On All Screens" / "On All Secondary
Screens" in the settings screen selector): live replication, welded to
multi-screen. The `OriginalView`/`ClonedView` machinery spawns one
clone per OTHER screen, persists them with `isClonedFrom=<original>`,
and live-syncs containment and applet configuration original -> clone
(pinned tasks appearing on the copy is expected behavior here). Clones
cannot coexist with the original on the same screen and their
placement is derived, not chosen.

**Launcher groups** (Tasks page > Launchers: Unique / Layout /
Global): live, BIDIRECTIONAL sharing of just the pinned-launcher list
across every tasks applet in the group, orthogonal to dock identity.
This already answers "my pins everywhere" today, including across
independent duplicates - set both docks' tasks to Layout Group.

User-visible documentation of the duplicate-vs-clone distinction
(one-time copy vs live projection) should land wherever end-user docs
end up; until then this file is the reference.

## The feature: a first-class "Replicate Dock"

Add a sibling action to Duplicate Dock that creates a LIVE-MIRRORED
view of a dock, user-placed (screen and edge chosen by the user, not
derived from a screens-group), coexisting with the original anywhere
including the same screen.

Implementation shape: a `ClonedView` created with a user-chosen
placement instead of a screens-group-derived one. The sync machinery
already generalizes (`setNextLocationForClones` iterates a clone
list; clones persist in the layout file), so the work is placement,
keying, lifecycle and UX - not new sync plumbing.

## Design decisions (settled in discussion, to be confirmed at
## implementation time)

1. **Clone keying.** Current machinery assumes one clone per screen
   id. Replicas must be keyed by identity (clone id), with screen a
   property rather than the key. This is the main structural change.
2. **What syncs.** Content only: containment applet tree + applet
   configs (what screens-group clones already mirror). Placement
   (screen, edge, alignment, visibility mode) stays per-replica, so a
   bottom-dock original can have a right-edge replica.
3. **Editability.** A replica's settings window opens the ORIGINAL's
   content settings (with a visible hint), and only placement controls
   act on the replica itself. Blocking edits entirely reads as broken;
   silently editing the replica would break the mirror contract.
4. **Lifecycle.** Deleting the original offers: delete replicas with
   it, or DETACH them (turning each into an independent duplicate).
   Detach is cheap - drop `isClonedFrom` - and resolves the orphan
   question with an affordance instead of a policy.
5. **Sequencing.** This is an upstream-plus feature (beyond even Latte
   git master), appropriate under the maintained-continuation framing,
   and it rides the clone sync machinery - so it lands only after the
   remaining Phase 8 cloned-view item (structuralSyncReady deferred
   order sync) is fixed and the screens-group clone path is verified
   live on the desk setup.

## Prerequisites and their status

- [x] Applet config sync establishes at all (c3d15966, 2026-07-12) -
      without this every clone silently degraded to a stale snapshot.
- [x] Applet order persists and duplicates are order-faithful
      (9a6f8fb8, 2026-07-12) - replicas would have inherited the same
      wipe.
- [ ] Phase 8: cloned-view applet-order sync gap (deferred sync when
      structuralSyncReady becomes true late) - the known remaining
      clone-machinery defect.
- [ ] Live verification of screens-group clones on the dual-monitor
      desk setup (the "On All Screens" path has not been exercised in
      the port yet).
- [ ] UX: context menu gains "Replicate Dock" next to "Duplicate
      Dock"; the Add Dock/Panel submenu is already the discoverable
      home for both.
