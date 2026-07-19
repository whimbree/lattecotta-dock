# Roadmap

The one-screen index of the port's active plans and where each stands. Open the
linked doc for the detail; this page is the map, not the territory.

**Release target: v0.20.0** - the first tagged continuation release (upstream
stopped at v0.10.8). Its gate is the multi-distro CI matrix going green across
every target, at which point VERSION bumps to 0.20.0 and the tag lands.

## Plans

- [Porting plan](PORTING_PLAN.md) - the master 13-phase checklist, every task
  naming its commit. Phases 0-7 substantially done; Phase 4 is Wayland-only (the
  X11 backend was removed); Phase 8 (layout, shutdown, multi-screen) still has
  open items.
- [Multi-distro CI and native packaging](multi-distro-ci-plan.md) - the v0.20.0
  release CI matrix plus native packages per format. Arch is proven end to end;
  the other distro legs and the release tag are pending.
- [Edit-mode settings audit](edit-mode-settings-audit-plan.md) - a driven audit
  proving every edit-mode control keeps its contract (applies, right key, stays
  in sync). CL-0 (harness), CL-1 (length), and wave-2 (CL-2/3/4/6: appearance,
  behavior, effects, chrome) landed; CL-5 (tasks page and the D10 wire-up) is in
  flight; follow-ups cover the control 56-90 readback gaps and the D24
  disposition.
- [E2e interaction tests](e2e-interaction-test-plan.md) - the nested-vehicle
  interaction matrix. The interaction DRIVER layer (C-I chunks) landed; the
  scenario and abort chunks (C-S/C-A) and C-I5 (moveViewToScreen) are pending; a
  multi-monitor hardware-seam roadmap is queued for that plan.
- [UB catching](ub-catching-plan.md) - the sanitized dock build and boundary
  invariants. Prong A (the sanitized build and its driven ASan+UBSan gate) and B1
  (the justify-splitter pure core) are done; B2 (the boundary-invariant sweep) is
  ongoing.
- [X11 cleanup](x11-cleanup-audit.md) - the Wayland-only survivor sweep. The
  dead-code removals landed; the sign-off-gated proposal sequence is pending.
- [QML extraction](QML_EXTRACTION_PLAN.md) - COMPLETE. All 25 pure-core
  extraction units executed and merged; the doc is kept as the ledger of record.
- [Panel issues](panel-issues-plan.md) - panel-mode (behaveAsPlasmaPanel)
  defects: the floating-gap geometry, the systray popup anchor, the edit-mode
  tooltip that eats the click, and four-edge test coverage. Catalogued with
  root-cause groundings; fixes pending.

## Registries and state

- [Known defects](known-defects.md) - the flat found-bugs registry (D1-D24), one
  line per bug with its status (OPEN / FIXED / ACCEPTED / SUSPECTED). Scan here
  for what is known broken.
- [Session handoff](session-handoff.md) - the rolling session state. Read first
  every session; it is the single source of where things stand.

## Elsewhere in docs/

- [`../reference/`](../reference/) - stable how-tos and research: the D-Bus
  interface reference and observability design, the testing standard, the
  dock-replication design, clangd setup, the task-manager integration research,
  the ng-upstream audit, flake-removal testing, the CaptSilver testability
  adoption, and the live-only test registry.
- [`../prompts/`](../prompts/) - the orchestrator prompt and the per-workstream
  execution and planning prompts.
