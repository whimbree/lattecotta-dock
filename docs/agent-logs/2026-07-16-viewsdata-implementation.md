# viewsData + setViewEditMode implementation log (2026-07-16)

Step 1 of docs/dbus-observability-interface.md: the `viewsData()` JSON
read surface and the `setViewEditMode(u, b)` coarse action on
org.kde.LatteDock.

## Method

Read the interface doc in full first, then walked the mechanics before
writing anything:

- Adaptor generation: app/CMakeLists.txt:42-43 runs qt_add_dbus_adaptor
  over app/dbus/org.kde.LatteDock.xml against Latte::Corona, so plain
  public methods/slots on Corona are the whole wiring
  (lifecycleState/viewAppletsOrder are the in-tree pattern).
- Getter inventory against the doc's field list: everything existed
  except Positioner::inStartup (private m_inStartup, the
  startup-stranding diagnostic bit) and
  VisibilityManager::publishedStruts (private m_publishedStruts, the
  rect actually sent to the WM). Both added as plain read-only getters,
  no parallel caches.
- Edit-mode entry: containmentactions/contextmenu/menu.cpp:104 shows
  "Edit Dock..." emits containment()->configureRequested(containment()),
  which lands in View::showConfigurationInterface - a TOGGLE.
  View::showSettingsWindow() (view.cpp:616) is the enter-only wrapper
  around the identical ensemble (mustBeShown + configuration interface +
  applyActivitiesToWindows), already used by the global shortcut and by
  ClonedView redirecting to its original. setViewEditMode(id, true)
  calls that: same path as Edit Dock, idempotent instead of toggling.
- Edit-mode exit: the chrome's close button is
  shell/package/contents/configuration/LatteDockConfiguration.qml:635
  `onClicked: viewConfig.hideConfigWindow()`, i.e.
  PrimaryConfigView::hideConfigWindow() (primaryconfigview.cpp:181),
  which owns the session-end semantics (endConfiguringSession +
  cancelDeferredShow + close/hide + canvas/secondary teardown).
  setViewEditMode(id, false) calls exactly that on the factory's
  singleton, after checking the view is actually in edit mode and the
  chrome is parented to it (it is a shared singleton; hiding it while
  parented to another view would end THAT view's session).
- Serializer home: new app/dbusreports.h/.cpp per the doc's
  implementation notes. Split so the ratchet gets a pure core: the
  header carries ViewRecord (a value snapshot of every reported field),
  the four enum-name switches and the JSON serialization, all pure and
  inline; only dbusreports.cpp touches live View objects (collect*
  functions). tests/units/dbusreportstest.cpp exercises the pure layer
  without a corona; the collectors are three-line field reads exercised
  by the running dock.

## Decisions worth recording

- inputRegionRects is an array of rects in the doc but Effects only
  exposes one QRect inputMask today; serialized as a 0-or-1 element
  array (invalid/empty rect means "no input restriction published",
  matching Effects::setInputMask's clear semantics) so multi-rect input
  regions can land later without a schema break.
- containmentId serialized before any other field reads happen;
  collectViewRecord asserts its preconditions (view, positioner,
  visibility, effects non-null) - these come from
  Synchronizer::currentViews(), not from outside, so Q_ASSERT is the
  right layer, not a runtime refusal.
- view->layout() is legitimately null mid-move (moveViewToLayout);
  reported as an empty layout name with a comment, not guarded silently.
- alignment comes back through View's int property surface; the cast to
  Types::Alignment is safe because View stores Types::Alignment
  internally (m_alignment).
- setViewEditMode(false) refuses (qWarning) when the view is not in
  edit mode or the chrome singleton is parented elsewhere; it does NOT
  require isVisible(), because a close request racing the chrome's
  deferred show (showAfter interval) must still cancel it -
  hideConfigWindow's cancelDeferredShow handles exactly that.

## Dead ends

- None structural. One detail worth noting: the doc's field name is
  "maskRect" but sits next to "inputRegionRects"; kept the doc's names
  verbatim so the e2e conversions can be written from the doc alone.

## Environment finding (a real causal chain, worth keeping)

build-check.sh kept failing with "QtCore/qobject.h: No such file or
directory" style errors while direct `nix develop -c cmake --build`
runs of the same dirs succeeded. First hypothesis (sandbox flake under
load) was WRONG - disproved by unsandboxed foreground runs failing the
same way. The actual chain, measured not guessed:

1. Every failing include is a module-root include (QtCore/qobject.h,
   LayerShellQt/window.h). CMake's explicit -isystem flags only carry
   the per-module dirs (.../include/QtCore); the include ROOTS
   (.../include) come from the nix gcc wrapper's NIX_CFLAGS_COMPILE.
   In the shell build-check gets when it re-execs itself
   (`exec nix develop "$repo" -c "$0"`) from this agent harness's
   shell, those wrapper flags do not take effect, so every module-root
   include fails "not found". Launching the script already inside the
   devShell (`nix develop -c ./scripts/build-check.sh`) passes end to
   end, repeatably. I did not run build-check from an interactive
   shell myself; the trigger is specific to the harness-spawned
   re-exec and needs a check from a real terminal to characterize
   further.
2. That broken-env configure also runs the UNGUARDED
   `try_compile(LATTE_LAYERSHELL_HAS_SET_SCREEN ...)` (top-level
   CMakeLists ~114), which re-executes on EVERY configure - a probe
   failure is silently recorded as "feature absent", the global
   add_definitions flips, every compile command changes, and the whole
   tree rebuilds (ninja -d explain: "command line changed"). One
   CMakeConfigureLog.yaml carried both outcomes for the identical
   probe command: exitCode 0 (my shell) and exitCode 1
   (LayerShellQt/window.h not found, build-check's re-exec shell).
   Worth considering: guarding the probe (if(NOT DEFINED ...)) or
   failing the configure loudly when the probe errors for a reason
   other than the symbol being absent - a silently flipped feature
   define that also masks env breakage is exactly the failure class
   CLAUDE.md's silent-swallow rule names. Left for the orchestrator:
   it is a build-system change outside this task's scope.

## Gates (all green)

- Fresh build dir in the worktree (nix develop -c cmake -B build -S .
  -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo), cmake --build build: OK.
- `nix develop -c ./scripts/build-check.sh`: OK end to end - both
  WITH_X11 variants build, ctest 48/48 passed, coverage-ratchet OK
  (48 entries, 28 unit headers paired). See the environment finding
  below for why the bare `./scripts/build-check.sh` self-re-exec form
  fails from this harness (not a source defect).
- scripts/qmllint-gate.sh: OK (155 finding files, baseline matched -
  no QML touched).
- Ratchet bookkeeping: baseline 47 -> 48 with dbusreportstest;
  app/dbusreports.h added to tests/units/app-subtree-units.list.
