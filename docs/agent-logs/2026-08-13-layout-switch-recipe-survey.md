# Layout-switching e2e recipe survey (2026-08-13, read-only scout)

Harness map for the queued multi-layout/activity recipe wave, gathered before
that wave was deferred. Everything below is verified against the tree at the
survey time; re-verify entry points before building on it.

## Recipe API surface (harness/src/latte_harness/recipe.py)

- Mutators: `call_or_fail(fail_message, *args)` is the helper for
  `switchToLayout` / `moveViewToLayout` / `importLayoutFile` calls.
- Readbacks: `read_json(method, *args)` for `layoutsData` (no typed Layout
  model exists; View/Applet/Task/DockSystemData are the typed ones).
- Lifecycle: `dock_start(timeout)` / `dock_stop()` (nested-only), fail/run/
  run_with_cleanup conventions, `DbusUnavailableError` as the not-yet signal.
- Env contract: E2E_CONFIG_HOME, E2E_LAYOUT, E2E_RT etc. exported by the
  runner.

## D-Bus methods (none currently exercised by any recipe)

- `switchToLayout s "<name>"` (lattecorona.cpp:1269; a path arg imports),
  `importLayoutFile ss <path> <suggestedName>` (:1278),
  `moveViewToLayout us <cid> <layout>` (:1981), `layoutsData` (JSON:
  memoryUsage, layouts[] with name/isActive/activities/viewsCount;
  serializeLayoutRecord at app/dbusreports.h:1397-1421). Signatures
  documented at docs/reference/dbus-interface-reference.md:594-597.
- Zero harness/e2e callers today; the only hits are C++ and the fp4c
  operation-model fixtures. This is the coverage gap the wave closes.

## Seeding and layout selection

- In-recipe seeding pattern (111-colorizer-content-policy.py:106-126 is the
  model): stop the dock FIRST (clean SIGTERM flushes current config - the
  030/110/032 ordering rule), delete stale `*.layout.latte` under
  `$E2E_CONFIG_HOME/latte/`, copy the fixture layout in, write
  `[UniversalSettings] singleModeLayoutName=<Name>` and `memoryUsage=0`
  into `$E2E_CONFIG_HOME/lattedockrc`, restart, assert, restore
  (ConfigHomeSnapshot).
- Templates ship at shell/package/contents/templates/ (Default/Empty/
  Extended/Plasma/Unity .layout.latte).
- The runner picks E2E_LAYOUT from the sole layout file, or
  `singleModeLayoutName` when several exist (e2e_runner.py:499-543). There
  is no lastUsedLayout key; single-mode selection is singleModeLayoutName.
- Closest structural template for restart-and-assert:
  tests/e2e/034-tasks-config-apply.py.

## Runner wiring

- Discovery is automatic: any executable top-level tests/e2e/*.py|*.sh.
  Markers: `# e2e-mode: nested-only`, `# e2e-expect: fail|status N`.
- gate-all does NOT run the full recipe set; the always-on merge-gate
  recipes are hardcoded in scripts/asan-e2e-gate.sh:61-62 (000-smoke,
  001-dbus-readback-schema, presentation-coverage-selftest,
  060-geometry-agreement, 070-asan-binary-shadow). A new layout recipe
  joins run-e2e.sh sweeps automatically but the always-on gate only by
  editing that array.

## Activities in the nested vehicle

- The vehicle is only kwin_wayland --virtual under dbus-run-session; no
  explicit kactivitymanagerd. It IS dbus-activated on demand (seed.py:224
  preseeds WAYLAND_DISPLAY into the activation environment precisely so the
  activities consumer reaches Running - without it the dock hangs with zero
  views). Consequence: exactly ONE default activity exists; single-mode
  switching (memoryUsage=0) is fully drivable, per-activity
  MultipleLayouts assignment can only see one activity id
  (switchToLayoutInMultipleModeBasedOnActivities,
  app/layouts/synchronizer.cpp:754). Multi-activity coverage needs a
  second activity created via the kactivities D-Bus API inside the
  session - feasibility unprobed.

## Determinism conventions

- No run-twice or retry machinery in the runner; determinism lives inside
  recipes: bounded injection-retry loops (092-task-reorder.py:134),
  sustained-sampling assertions (111:151-178), the stop-before-seeding
  ordering rule, and refusal-aware polling on DbusUnavailableError.
