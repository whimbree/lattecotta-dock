# Testing standard

Adopted at Phase 0 of the port (see `docs/PORTING_PLAN.md`), before any
test exists, so it can shape every test written rather than being
retrofitted during stabilization. Modeled on latte-dock-qt6's documented
honest-coverage standard (`5fcaa9f1`/`c903921d` in its history), which
explicitly bans gaming the metric.

## The standard

A test earns credit for a unit of code only if all of these hold:

1. **Real assertions.** Every claimed unit must assert an observable
   effect: a return value, a property change, a signal emission, or a
   mock side-effect. Execute-but-assert-nothing does not count.
2. **No swallowed throws.** No `try {} catch {}` (or `safe()` wrapper)
   that lets a test pass while the unit's body actually failed. If a
   unit throws on a missing global, mock the global or don't claim the
   unit.
3. **No construction-only credit.** An object that gets instantiated but
   can't be retained or driven does not count as covered, even if its
   `Component.onCompleted` fired during teardown.
4. **Honest mocks.** Mocks are shaped like the real object - the
   properties and methods the unit actually reads - never a catch-all
   that silently absorbs every access.
5. **Deterministic and headless.** Passes offscreen, stable across runs,
   no dependence on the developer's live session.

A unit that genuinely cannot meet 1-4 headlessly is **live-only**: it
gets an entry in a live-only registry (`docs/testing/live-only.md`,
created with its first entry) stating *why*, and becomes the target of
live verification (per-phase live testing from the first runnable
milestone, and the Phase 10 e2e harness). The registry exists so the gap
is recorded instead of papered over with a dishonest test. Latte's QML
is full of units that dereference a live containment, `Plasmoid`
attached objects, or a real compositor - latte-dock-qt6's registry shows
how large this class is; expect the same here.

## Infrastructure decision (Phase 0)

Adopt latte-dock-qt6's three-piece shape, adapted rather than copied:

- **Headless QML interaction harness** - drives real package QML
  components offscreen with honest mock contexts. Lands together with
  the first code that compiles (end of Phase 2), grows with each phase.
- **Coverage ratchet** - fails the build check on regression below the
  recorded baseline. Baseline gets recorded once the harness produces
  its first honest number; ratchet thresholds only ever move up.
- **e2e harness** - real widget add/remove driven through KWin D-Bus
  with screenshot capture against a live dock. Needs a runnable,
  reasonably complete dock; lands in Phase 10 as planned.

Enum/handler completeness tests (Phase 6: every UI-offered enum value
must have a handled branch, verified per enum/handler pair) are part of
the headless harness's job, not a separate mechanism.
