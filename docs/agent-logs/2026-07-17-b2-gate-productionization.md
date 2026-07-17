# B2 gate productionization (2026-07-17)

Wire the Phase B2 headless gate stage of the multi-distro CI matrix so
`ci/build-and-gate.sh gate` runs the full headless gate (sceneprobe +
tests/e2e behavioral recipes) in-container on Arch, un-stubbed, and
distro-agnostic (per-distro paths come from the Containerfile ENV, never
hardcoded in the driver). Branch `multi-distro-ci-b2-gate`.

## What landed

- `ci/containers/Containerfile.arch`: add `imagemagick` (screenshot
  recipes), `konsole` (task-interaction recipes), `python` (recipe
  helpers - was only a transitive dep before, now declared); add
  `LATTE_QML_MODULE_PATH=/usr/lib/qt6/qml` and
  `LATTE_FAKEINPUT_PROTOCOL=/usr/share/plasma-wayland-protocols/fake-input.xml`
  to the ENV block; put `/usr/lib/qt6/bin` on PATH so the unversioned
  `qml` runtime resolves (Arch ships only `qml6` on the default PATH; the
  keyboard-nav recipe launches its focus-taker with `qml`).
- `ci/build-and-gate.sh`: replaced the `gate` stage `# STUB:` with the
  real wiring - assert the LATTE_QML_MODULE_PATH container contract, build
  fakepointer, run the sceneprobe render gate, seed a default layout
  config, run the environment-agnostic e2e subset. Fails loudly (nonzero)
  if any sub-gate fails. Two ctest entries that cannot pass off the NixOS
  pin are excluded from the matrix ctest (named, not silently dropped):
  `qmllintgate` (the qmllint ratchet structurally refuses any qmllint
  outside /nix/store) and `schemesmodeltest` (non-hermetic, the filed B2
  item).
- `scripts/run-staged.sh`: guard `$HOME` under set -u on line 85 (the
  always-run XDG_DATA_DIRS assembly), resolving it from the passwd db like
  the already-fixed `$USER` landmine (09b6e69bc).
- `scripts/lib-qml-env.sh` + `scripts/qml-interaction-tests.sh`: the
  staging-reuse guard hardcoded `lib/qml`; now `qml_env_setup` leaks the
  resolved `qmldir` (KDE_INSTALL_QMLDIR: lib/qml on nixpkgs, lib/qt6/qml on
  Arch) and the guard uses it, so it stops force-re-staging off nix.

## Traps hit and root-caused

1. **The gate died EXIT 143 during config seeding.** `nested_kwin_cleanup`
   (lib-nested-kwin.sh) ends with `wait $NESTED_KWIN_PID`, which returns
   the kwin's SIGTERM status (143) because teardown just killed it. The
   seed subshell inherited the driver's `set -e`, so that 143 from the
   EXIT trap took the whole driver down right after a clean seed. Every
   library caller runs without `-e` (run-e2e.sh uses `set -uo pipefail`);
   the seed subshell now does `set +e` and relies on explicit checks. Not
   a library bug - a set-e-vs-cleanup-contract mismatch. Traced with a
   standalone repro that printed "SUBSHELL END OK" then died in the trap.
2. **fakepointer on Arch.** Arch ships no `plasma-wayland-protocols.pc`, so
   `pkg-config --variable=pkgdatadir` fails. The driver tries pkg-config
   first, then falls back to `LATTE_FAKEINPUT_PROTOCOL` (Containerfile ENV)
   -> `/usr/share/plasma-wayland-protocols/fake-input.xml`.
3. **ctest went 74/82 -> 80/82** once the image exported
   LATTE_QML_MODULE_PATH: the 7 QML-tree-dependent tests from A2 now pass;
   only qmllintgate (nix-tier) and schemesmodeltest (non-hermetic) remain,
   both excluded from the matrix ctest with a tracked reason.

## Full e2e characterization (Arch container, nested vehicle)

Ran the FULL 11-recipe suite in-container. Result: 6 PASS, 5 FAIL. The 6
that pass assert pure dock state and are the gate's hard e2e set; the 5
that fail depend on integration a bare CI container does not provide.

| Recipe | Result | Root cause |
|---|---|---|
| 000-smoke | PASS | - |
| 010-wheel-desktops | PASS | fakepointer wheel over empty dock area; the recipe's own restart-between-directions handles the nested input-delivery limitation |
| 020-wheel-task-cycle | FAIL | konsole window app_id resolves to bare `konsole` in-container, recipe matches `org.kde.konsole.desktop` |
| 030-wheel-ruler-maxlength | PASS | fakepointer wheel over the edit-mode ruler |
| 040-preview-tooltip-text | FAIL | same konsole app_id mismatch (cannot locate the task icon) |
| 050-drag-reorder-launchers | FAIL | default-template seed carries launchers with empty app_ids; recipe needs >=3 real pinned launchers |
| 060-geometry-agreement | PASS | state-vs-render agreement guard |
| duplicate-view-idremap | FAIL | removeView removal never reaches the layout file even after the full 120s undo window (libplasma removal-flush divergence) |
| keyboard-navigation-mode | PASS | uses the `qml` focus-taker (needs /usr/lib/qt6/bin on PATH) |
| parabolic-hover-preview | FAIL | same konsole app_id mismatch |
| settings-window-onscreen | PASS | kglobalaccel-triggered settings window on-screen check |

The konsole cluster (020/040/parabolic) is ONE root cause: the
desktop-app-database resolution the dev's plasma-integrated nix env
provides is absent in a bare container. VERIFIED with a live vehicle
probe that the konsole windows DO map (kwin dump shows two
`org.kde.konsole` toplevels) and the dock DOES track konsole as a task -
`viewTasksData` carries a window task - so this is app_id RESOLUTION, not
konsole availability. The task's app_id came out `konsole` (bare) even
after a `kbuildsycoca6 --noincremental` into a cache shared with the dock.
050's two default launchers also have EMPTY app_ids - the same resolution
gap. konsole+imagemagick stay in the image so these recipes drop straight
in once the app_id/fixture items land.

duplicate-view is a separate, non-flake failure (confirmed solo, not under
concurrent load): after removeView the containment section persists in the
layout file past the full 120s undo window - a libplasma removal-flush
behavior to root-cause on Arch.

## Reproduce (host podman)

```
WT=<worktree>
podman build -t localhost/latte-ci-arch:latest -f "$WT/ci/containers/Containerfile.arch" "$WT/ci/containers"
# persistent build dir so re-runs are incremental:
mkdir -p /tmp/ci-build
podman run --rm --shm-size=1g -v "$WT:/src:ro" -v /tmp/ci-build:/build:rw,z \
    localhost/latte-ci-arch:latest bash /src/ci/build-and-gate.sh gate
```

Stages: build -> ctest (minus the two NixOS-tier entries) -> fakepointer
-> sceneprobe (13/13) -> seed default layout -> e2e agnostic subset (6/6).
Gate exits 0 green. To see the full 11-recipe characterization, run all
recipes directly:
`podman run ... bash /src/scripts/run-e2e.sh` with
`-e BUILD=/build -e E2E_CONFIG_BASE=/build/_seedconfig -e E2E_FAKEPOINTER=/build/_e2e-tools/fakepointer`.
