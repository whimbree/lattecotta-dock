# P2 contract transplants from latte-dock-qt6 (2026-07-16)

Task: docs/reference/captsilver-testability-adoption.md P2 - transplant the six
Plasma 6 silent-failure contract pins from the reference fork
(read at origin/main via `git show`; the local checkout sits at
9003f33a and all seven test files exist only at origin/main), each
verified against THIS tree's actual code before adopting.

## Per-candidate verification and verdicts

### 1. alternativescreateapplettest - ADOPTED, and it caught a live bug

Verified the premise against the pin first:
`plasmoidplugin.qmltypes` in the flake-pinned libplasma 6.6.5
(`/nix/store/kk0g33...-libplasma-6.6.5/lib/qt-6/qml/org/kde/plasma/plasmoid/`)
declares `createApplet(plugin: QString, args: QVariantList, geom: QRectF)`.

Then checked our callers of createApplet (app/, containment/):

- `containment/plugin/layoutmanager.cpp` invokes `createAppletItem`,
  a Latte-owned main.qml function - not the Plasma method, unaffected.
- `app/view/containmentinterface.cpp:624` calls the C++
  `Plasma::Containment::createApplet(pluginId)` directly - typed at
  compile time, unaffected.
- `app/alternativeshelper.cpp:85` HAD THE TRAP: it invoked the QML
  ContainmentItem's `createApplet` with `Q_ARG(QPoint, newPos)`.
  invokeMethod resolves by exact argument types, so the QPoint call
  fails to resolve, returns false (return value was not checked), and
  the swap silently no-ops - after `m_applet->destroy()` already ran.
  Net effect: picking an alternative destroys the applet and never
  creates the replacement. AlternativesHelper is live code
  (lattecorona.cpp:1008, the "Show Alternatives" dialog).

Fixed the caller (QRectF with a zero size to keep the Qt5
position-only semantics; invokeMethod result now checked and reported
loudly per the failures-and-root-cause rules), then adopted the test,
adapted:

- The fork reads the qmltypes via QLibraryInfo::path(QmlImportsPath)
  and QSKIPs when absent. In our nix env that path is Qt's own qml
  dir and libplasma's is a different store path, so the QSKIP would
  fire forever and the pin would silently never run - exactly the
  silence CLAUDE.md bans. Instead CMake derives the qmltypes path
  from the imported Plasma::Plasma library location and FATAL_ERRORs
  at configure when missing; the test has no skip path.
- Added a structural check pinning OUR caller: alternativeshelper.cpp
  must pass Q_ARG(QRectF, must not pass Q_ARG(QPoint to createApplet,
  and must check the invoke result.

Negative-tested: see the negative-test section below.

### 2. indicatormetadatatest - ADOPTED, plus dead-trap removal

app/indicator/factory.cpp's live paths already load metadata via
`KPluginMetaData::fromJsonFile()` (lines 121, 348; fixed in
7a1e63ae3 "port: KPackage metadata, KNS structure key, plugin JSON
loading"), so the running dock avoids the trap. BUT the file still
carried `Factory::metadataAreValid(QString &file)` (factory.cpp:252)
using the bare `KPluginMetaData(file)` ctor - on KF6 that ctor
resolves its argument as a plugin library, so the function returns
false for every metadata.json. Grepped the whole tree: no callers -
dead code carrying a live trap for whoever calls it next. Removed the
overload (factory.h:50 + factory.cpp) instead of testing around it.

Adopted the fork's three checks (behavioral fromJsonFile validity for
all three shipped indicator packages, the bare-ctor-is-invalid KF6
signature pin, and the structural factory.cpp scan). The structural
scan would have FAILED on our tree before the dead-overload removal -
its regex flags exactly the bare-ctor-from-a-variable form line 255
had.

### 3. indicatorsknsrctest - ADOPTED

app/latte-indicators.knsrc already carries
`KPackageStructure=Latte/Indicator` and no dead `KPackageType` key
(also 7a1e63ae3), but nothing pinned it - a future edit or a
copy-paste from an old KF5 knsrc would silently empty the "Get New
Indicators" dialog (KNSCore EngineBase fails init with
ConfigFileError when Uncompress=kpackage has no structure).
Adopted near-verbatim; the path comes from CMake as in the fork.
app/latte-layouts.knsrc uses `Uncompress=archive`, which needs no
structure key, so the indicators file is the only one at risk.

### 4. contextmenudefaulttest - ADOPTED, adapted to OUR mechanism

The right-click menu works live in this tree, but WHY it works is
different from the fork, so their view.cpp check would pin the wrong
thing:

- The fork installs the default RightButton action in View. We
  re-assert it in `GenericLayout::addContainment`
  (app/layout/genericlayout.cpp:748-750), because Latte rewrites its
  per-layout file with only [Containments] on save, serializing the
  [ActionPlugins] binding away - the genericlayout comment carries
  the full story.
- shell/package/contents/defaults maps
  `RightButton;NoModifier=org.kde.latte.contextmenu` (under
  [Panel][ContainmentActions]; the fork's shell carries a different
  group layout, the substring pin holds for both).
- containmentactions/contextmenu/CMakeLists.txt already renames the
  plugin to org.kde.latte.contextmenu.so with a comment on the KF6
  filename-derived plugin id - known, but unpinned before this test.

Adopted with four checks: the behavioral eventToString trigger pin
(right-click really produces "RightButton;NoModifier" at our Plasma
pin), the shipped-defaults mapping, a structural pin on the
genericlayout re-assert, and the plugin id chain (built .so basename
== metadata KPlugin.Id == the id the defaults name). The .so path
comes from `$<TARGET_FILE:plasma_containmentactions_lattecontextmenu>`
so a rename in either place fails the test, not the user's
right-click.

### 5. editmodehandleactiontest - ADOPTED

containment/package/contents/ui/editmode/ConfigOverlay.qml already
resolves everything through `applet.plasmoid.internalAction(...)`
(lines 428-430, 502, 579) and has no `applet.action(` call;
containment main.qml uses `Plasmoid.internalAction` as well. Nothing
pinned it. Adopted the fork's structural test (ban on the removed
Plasma 5 `applet.action()` API, require the internalAction resolution
for both "remove" and "configure").

### 6. appletzoomsizetest + bindingrestoremodetest - split verdict

Our tree already carries `restoreMode: Binding.RestoreNone` in 23
shipped QML files. A brace-matching scan over every shipped QML file
found 108 when-gated Binding elements, 0 missing restoreMode (no
`Binding on prop` elements shipped at all). No existing test pinned
either the Qt semantics or the source rule.

- appletzoomsizetest (the behavioral half): ADOPTED as
  tests/contracts/tst_bindingrestorecontracts.qml - it pins upstream
  Qt semantics, which is exactly what tests/contracts is for, and the
  qmltest form runs under the existing qmlcontracts entry (no new
  ctest entry). Same two scenarios as the fork's C++ test: RestoreNone
  keeps the captured value when the gate closes; the Qt6 default
  resets it to the declared value (documents the trap).
- bindingrestoremodetest (the source-scan half): EXTENDED-GATE.
  The fork's per-file minimum-count table is brittle (their comment
  admits the per-file triage) and covers 13 files where we ship 23.
  Since our tree is at ZERO violations, a blanket rule is both
  stronger and simpler: scripts/qml-effect-rules.sh gains rule 2 -
  every when-gated Binding element in shipped QML must declare an
  explicit restoreMode. Zero-baseline ratchet, same genre as the
  existing autoPaddingEnabled rule.

## Negative tests

Each new contract was negative-tested by temporarily breaking the
premise, watching the test fail, and restoring. Actual outcomes, not
the plan:

- alternativescreateapplettest: reverted the Q_ARG(QRectF back to
  Q_ARG(QPoint in alternativeshelper.cpp -> callerPassesRectfGeometry
  FAILED; restored -> green. (The qmltypes half was exercised only
  positively - the pinned path is compiled in, so faking a QPoint
  signature would mean doctoring a store path; the parse-and-compare
  logic is the same code the caller check goes through.)
- indicatormetadatatest, first attempt replaced the fromJsonFile call
  with the bare ctor -> failed on the must-contain-fromJsonFile check;
  redone properly by ADDING a bare `KPluginMetaData legacy(path);`
  alongside the fromJsonFile call -> factoryUsesFromJsonFile FAILED on
  the bare-ctor regex, naming the match; restored -> green.
- indicatorsknsrctest: renamed KPackageStructure to KPackageType in
  the knsrc -> BOTH checks FAILED; restored -> green.
- contextmenudefaulttest: the first negative test PASSED when it
  should not have - renaming the setContainmentActions call and its
  trigger string left the test green, because the original three
  independent substring checks were also satisfied by the lookup line
  NEXT to the re-assert call. Tightened the check to a
  whitespace-tolerant regex over the exact two-argument call form;
  re-ran the negative test (plugin id gutted inside the call) ->
  layoutReassertsDefaultRightButtonAction FAILED; restored -> green.
  The weak-substring lesson is recorded in the test comment.
- editmodehandleactiontest: changed the click-handler
  internalAction("remove") to action("remove") in ConfigOverlay.qml ->
  handleDoesNotUseRemovedAppletActionApi FAILED; restored -> green.
- tst_bindingrestorecontracts.qml: flipped the RestoreNone component
  to RestoreBindingOrValue -> test_restoreNoneKeepsCapturedValueWhenGateCloses
  FAILED; restored -> green.
- qml-effect-rules rule 2: deleted a restoreMode line from
  LayoutsContainer.qml -> gate FAILED naming file:line
  (LayoutsContainer.qml:39); restored -> green.

Process scar worth recording: the first negative-test round restored
files with `git checkout` while the alternativeshelper/factory fixes
were still UNCOMMITTED, wiping them; they were reapplied and committed
before the remaining rounds. Fixes land before their negative tests
from now on.

## Gates

- fresh cmake configure + full build (RelWithDebInfo, worktree): OK
- full ctest green, 52 entries; tests/ratchet-baseline updated with
  each new C++ contract test in the same commit (47 -> 52 over five
  commits, each intermediate state consistent)
- scripts/coverage-ratchet.sh: OK
- scripts/qmllint-gate.sh: OK (155 finding files, baseline matched -
  new test QML is outside its scope by design)
- nix develop -c ./scripts/build-check.sh --fresh (both WITH_X11
  variants + full ctest + ratchet): OK

Environment scar, worth escalating separately: build-check.sh only
re-execs into the devshell when cmake is NOT in PATH. With a system
cmake 4.1 in /run/current-system that guard is defeated - the script
ran with the system toolchain, reconfigured build/ against the wrong
environment and died on qtbase umbrella headers (the devshell's
NIX_CFLAGS_COMPILE was missing). Recovered with
`nix develop -c ./scripts/build-check.sh --fresh`. The guard should
probably check for the PINNED toolchain (e.g. the flake's cmake store
path or IN_NIX_SHELL), not for any cmake.

## Commits

- b241909a5 fix(app): pass the QRectF geometry hint Plasma 6
  createApplet expects
- 0e2be5871 chore(app): drop the dead bare-ctor
  metadataAreValid(QString) overload
- c23faa895 test(contracts): pin Plasma 6's createApplet QRectF
  geometry contract
- a5fbb8048 test(contracts): pin KF6 indicator metadata loading
  through fromJsonFile
- 1537fd83e test(contracts): pin the KF6 KPackageStructure key in
  the indicators knsrc
- f93961316 test(contracts): pin the right-click context menu wiring
  chain
- 7969c43a7 test(contracts): pin the edit-mode handle on
  plasmoid.internalAction
- bd47c11c6 test: pin Qt6 Binding restoreMode semantics and enforce
  the freeze rule
- (this commit) docs: the P2 transplant log

## Verdict summary

1. alternativescreateapplettest: ADOPTED + caller FIX (live bug)
2. indicatormetadatatest: ADOPTED + dead-trap removal
3. indicatorsknsrctest: ADOPTED (code already correct, now pinned)
4. contextmenudefaulttest: ADOPTED, adapted to our GenericLayout
   mechanism
5. editmodehandleactiontest: ADOPTED (code already correct, now
   pinned)
6. appletzoomsizetest: ADOPTED as tst_bindingrestorecontracts.qml;
   bindingrestoremodetest: EXTENDED-GATE (qml-effect-rules.sh rule 2)

## Post-merge note (orchestrator)

The serial merge rebased this branch onto master, so the hashes above
are the worktree's, not master's. Landed as: 5925b167f, 7fc338cd5,
9ee6f7ad5, 22e6bb63d, 885c1318e, 53839863b, 9f1672434, b21825ed4
(same subjects, same order).
