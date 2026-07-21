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
  release CI matrix plus native packages per format. All five local recipe
  formats exist. The Debian-family, shared RPM, Arch, and Void formats have
  fresh-environment install evidence; Gentoo has a built GPKG, a Portage-owned
  manifest, package/source reinstall evidence, and installed-artifact nested-
  runtime proof. F6 (the package-artifact CI task) remains pending outside the
  completed recipe scope. No hosted CI, official package or repository, release,
  tag, artifact upload, package publication, sponsorship, or distribution
  endorsement exists.
- [Edit-mode settings audit](edit-mode-settings-audit-plan.md) - the completed
  wiring/readback ledger. It proves expected keys and reflected values, but is
  not end-to-end control or runtime-behavior completion evidence.
- [Settings surface completion](settings-surface-completion-plan.md) - approval
  is limited to SC-F1 (the per-view source inventory and evidence ledger),
  SC-F2 (the source-to-ledger coverage gate), SC-O1 (the read-only
  settings-control D-Bus registry), SC-D1 (the pointer and keyboard control
  drivers), SC-D2 (the popup and lifecycle driver helpers), SC-C1 (the ComboBox
  and ComboBoxButton family), SC-C2 (the slider and numeric-field family), SC-C3
  (the text-entry family), SC-C4 (the checkable and grouped-button family), SC-C5
  (the color-control and dialog family), SC-W1 (the launcher-wheel regression
  guard), SC-T3 (the D29 narrow dispatch readback), SC-T5 (the D29 permanent
  runtime-effect acceptance), SC-CW1 (the D57 ConfigOverlay wheel-threshold
  reproduction), and SC-WT1 (the D58 tracker-enablement root fix and regression).
  SC-T1 (the D29 middle-click evidence capture), SC-T2 (the D29 disposition), and
  SC-B1 (the D30 current-contract investigation) are complete evidence records.
  Per-view pages come first; global Qt Widgets settings follow under the same
  C1-C9 (nine-property control completion) contract. D29 (task-icon middle click
  appears to execute left-click behavior) is accepted as Qt5-faithful
  configuration-scope behavior; SC-T4 (the D29 root fix) is not applicable. D30
  (Behavior mouse actions expose fixed booleans instead of full choices) is an
  inherited contract whose evidence favors retain-and-clarify, but SC-B2 (the
  D30 product decision and sign-off gate) remains pending with no action
  expansion approved. D57 (ConfigOverlay wheel threshold accepts nonnegative
  decrease deltas) is open and reproduced provisionally by local SC-CW1; SC-CW2
  (the D57 signed decrease-threshold fix and regression promotion) remains
  unapproved. D58 (close-only and minimize-toggle settings do not enable window
  tracking) is confirmed and open for its narrow root fix. D56 (pure-launcher
  task wheel uses inherited asymmetric activation) is accepted as Qt5-faithful
  with a permanent regression guard. D24 (TypeSelection Dock/Panel presets write
  two dead keys) is an independent small cleanup. D31 (valid Justify splitter
  moves reset after restart) is already fixed by PR #73 and is outside this plan.
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
