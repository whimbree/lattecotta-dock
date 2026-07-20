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
  in sync). COMPLETE - CL-0 (harness), CL-1 (length), wave-2 (CL-2/3/4/6:
  appearance, behavior, effects, chrome), and CL-5 (tasks page; D10 resolved as
  "applies", no wire-up needed) all landed. Small follow-ups remain: the control
  56 / 90 readback gaps and the D24 dead-keys disposition.
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

- [Known defects](known-defects.md) - the flat found-bugs registry, one
  line per bug with its status (OPEN / FIXED / ACCEPTED / SUSPECTED). Scan here
  for what is known broken.
- [Session handoff](session-handoff.md) - the rolling session state. Read first
  every session; it is the single source of where things stand.

## Elsewhere in docs/

- [`../reference/`](../reference/) - stable how-tos and research: the D-Bus
  interface reference and observability design, the testing standard, the
  dock-replication design, clangd setup, the task-manager integration research,
  flake-removal testing, and the live-only test registry.
- [`../prompts/`](../prompts/) - the orchestrator prompt and the multi-distro CI
  execution and orchestrator prompts.
- [`../archive/`](../archive/) - finished and superseded docs, kept for the
  record; see the Archived / completed section below.

## Archived / completed

Finished or superseded docs, moved to [`../archive/`](../archive/) so the record
is preserved but off the active index. One line each on what it was:

- [ng-upstream audit](../archive/ng-upstream-audit.md) - the complete 249/249
  merit audit of latte-dock-ng's commits. Every genuinely-open ADOPT (adopt-this-
  fix) item is now tracked in the porting plan or the known-defects registry.
- [CaptSilver testability adoption](../archive/captsilver-testability-adoption.md) -
  the P1-P5 (five-wave) plan for adopting CaptSilver's testability campaign; the
  adopted waves have landed.
- [QML extraction execution prompt](../archive/qml-extraction-execution-prompt.md) -
  the per-unit session contract that drove the now-complete QML extraction
  initiative.
- [QML extraction planning prompt](../archive/qml-extraction-planning-prompt.md) -
  the planning-session contract that produced the QML extraction plan.
- [Stabilization execution prompt](../archive/stabilization-execution-prompt.md) -
  the stabilization session contract (re-runnable priority list, merge protocol);
  its work has all landed.
- [Stabilization short prompt](../archive/stabilization-short.md) - the short
  kickoff form of the stabilization session contract.
