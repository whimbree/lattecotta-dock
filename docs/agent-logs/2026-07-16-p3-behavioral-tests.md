# P3 behavioral-test transplants from latte-dock-qt6 (2026-07-16)

Task: docs/captsilver-testability-adoption.md P3 - behavioral tests
over the lattedock-core object library, transplanted from the
reference fork (read at origin/main via `git show`; the local checkout
sits at 9003f33a and these tests exist only at origin/main), each
candidate's premise verified against THIS tree before adopting.
Method follows the P2 ledger
(docs/agent-logs/2026-07-16-p2-contract-transplants.md): per-candidate
verdicts, adapt to our mechanisms instead of pinning the fork's,
fixes land BEFORE negative tests, negative-test every contract.

Candidate order (from the adoption plan): screenpooltest and
visibilityrevealtest first (Phase 8 subjects), then abstractlayouttest,
activitiesinfotest, syncedlaunchersclienttest, lastactivewindowtest,
smartlauncheritemtest, tasksbackendtest, datatypestest,
settingsmodelstest. storageroundtriptest and templatesnametest are
SKIPPED per the plan (mirror-logic tests; docs/TESTING.md rejects
reimplementation-testing).

## Defects exposed by the premise sweep

The fork's e997ac93 ("Fix latent correctness defects in data, screen,
settings and view layers") fixed six latent defects. Checked all six
sites in our tree before adopting any test that pins them:

1. app/screenpool.cpp removeScreens(): `return` on the first absent id
   instead of `continue`, silently leaving later obsolete screens
   mapped. LIVE in our tree (caller: the Screens dialog removal,
   app/settings/screensdialog/screenshandler.cpp:125).
2. app/settings/settingsdialog/layoutscontroller.cpp:179
   `return m_model-modeIsChanged();` - missing `>`: pointer arithmetic
   plus unqualified self-call, infinite recursion if ever invoked.
   Dead code (no callers anywhere in the tree), inherited from
   upstream 6f58d09d0. LIVE TRAP in our tree.
3. app/view/visibilitymanager.cpp updateSidebarState():
   `m_isSidebar == cursidebarstate;` - comparison instead of
   assignment; isSidebar() reports false forever. Consumers:
   global shortcuts, widget explorer and config view positioning,
   the QML mask geometry. LIVE in our tree.
4. app/data/appletdata.h `bool isSelected;` uninitialised in the
   default ctor while the copy/move ctors and operator== read it.
   LIVE in our tree.
5. app/view/containmentinterface.cpp
   updateContainmentConfigProperty(): already correct in our tree
   (guarded early return present). NOT AFFECTED.
6. app/settings/settingsdialog/layoutsmodel.cpp currentData(): already
   returns by value in our tree. NOT AFFECTED.

Further defects found by later premise checks (fork parallels named
in each fix commit):

7. app/layout/abstractlayout.cpp layoutName(): stripped the extension
   with remove(lastIndexOf, 13) - QString::remove counts a negative
   position from the end, so any non-.layout.latte name came back one
   character short. LIVE (importer feeds arbitrary names).
8. app/data/viewdata.cpp View::operator QString(): three composition
   bugs - dead combined move-marker branch (single-flag checks first),
   doubled Primary/Explicit token (unconditional second append), and
   "Cloned from:" + int as POINTER arithmetic. LIVE (debug/report
   output).
9. app/settings/{exporttemplatedialog/appletsmodel,
   screensdialog/screensmodel, detailsdialog/colorsmodel}.cpp data():
   guarded only row >= rowCount(), letting the invalid QModelIndex's
   row()==-1 through to the GenericTable uint indexer - SEGV at index
   4294967295, reproduced under gdb. LIVE at a Qt API boundary.

## Quality bar (relayed from Bree, 2026-07-16)

The fork's tests are the floor, not the ceiling. For every transplant:
cover the edge cases the fork skipped (boundaries, degenerate inputs,
ordering/idempotency); assert observable end-to-end behavior, never
construction or internal state; use QTest data tables where the input
space is enumerable; pin the WHY when a case encodes a non-obvious
contract; keep the step-2.5 advantages (ASan+UBSan, QT_FORCE_ASSERTS,
type discipline) doing work; redesign weak shapes instead of copying
them (the P2 weak-substring lesson). The first two transplants were
strengthened retroactively under this bar - see their entries.

## Per-candidate verification and verdicts

### 1. screenpooltest - ADOPTED, and it pins a live-bug fix

Premise checks against our tree, all green: Latte::ScreenPool carries
the exact API the test drives (config-injected ctor, load(),
NOSCREENID=-1, hasScreenId, insertScreenMapping(connector),
removeScreens(ScreensTable), FIRSTSCREENID=10);
PlasmaExtended::ScreenPool self-loads from the hardcoded plasmashellrc
in its ctor with the 0=primary/-1=not-found id semantics;
Data::Screen has the (id, serialized) ctor. The removeScreens premise
was NOT green: our loop carried the early return the fork's e997ac93
fixed (defect 1 above). Fixed at the origin first (return -> continue,
contract comment added), then adopted the test. Adaptation: the fork
compiles fifteen sources into the test binary; ours links the
lattedock-core object library like every other behavioral test.

### 2. visibilityrevealtest - ADOPTED, extended over the full enum

Our tree already carries the static
VisibilityManager::revealsOnScreenEdge helper (landed with cf05d8564,
the layer-shell placement port) gating
WaylandInterface::setActiveEdge - identical semantics to the fork's.
Nothing pinned it. Adopted the fork's eight assertions and extended
with the four enum values our Types::Visibility also carries
(WindowsAlwaysCover, SidebarOnDemand, SidebarAutoHide, NormalWindow),
all non-revealing, verified against updateKWinEdgesSupport() which
creates edge ghosts only for the four reveal modes plus
WindowsCanCover's layer mechanism.

### 3. abstractlayouttest - ADOPTED, extended, and it pins a live-bug fix

Premise check found defect: our layoutName() still stripped the
extension with remove(lastIndexOf(ext), 13) - QString::remove counts
a negative position from the end, so any non-.layout.latte name came
back one character short (the fork's 443ac361 names the same
mechanism). Reachable from the importer's arbitrary file names. Fixed
at the origin first (endsWith/chop, comment naming the
negative-position semantics), then adopted the test. Extended: the
layoutName table grows 4 -> 9 rows (empty path, directory path,
extension-in-the-middle, double extension chopped once, bare
extension) and a new settersEmitOnceAndOnlyOnRealChange case pins the
guarded-setter signals persistence rides on (init() wires every
change signal to saveConfig).

### 4. activitiesinfotest - ADOPTED, extended

Our tree already carries the identical activitiesinfo helper (the
KActivities 6 replacement reading org.kde.ActivityManager) - the
premise held with no code change. Extended: negative and
first-past-the-end boundary rows in the state table, Starting added
to the exclusion case (their case never proved a starting activity
stays out of the running set), and the empty-records row. The live
DBus list() query stays live-verified per the helper's own header.

### 5. syncedlaunchersclienttest - ADOPTED, redesigned

Premise checks: our SyncedLaunchers already carries the d6d57e61
identity-removal fix (the comment names the live crash), and
tests/contracts/destroyedtypedemotiontest.cpp already pins the Qt
demotion contract the fork's test mostly re-proves. The fork's shape
is weak for us twice over: it mirrors the handler's pattern on a
plain QList (their SyncedLaunchers cannot link headlessly; ours can,
plain QObject parent in lattedock-core), and a mirror keeps passing
when production regresses. Redesigned: recording probe clients
(QQuickItem subclass exposing addSyncedLauncher) registered through
addAbilityClient, real addLauncher() broadcasts observed end to end -
group delivery, layout/group filtering incl. the GlobalLaunchers
cross-layout semantics, destroyed-client removal, explicit
unregister. The negative test reproduced the production failure
shape: reintroducing cast-then-remove segfaulted the suite on the
first post-destruction broadcast.

### 6. lastactivewindowtest - ADOPTED, adapted to our WindowId, extended

Premise checks: our AbstractWindowInterface pure-virtual set matches
the fork's mock exactly; MAXHISTORY/PREFHISTORY identical (22/14);
forwarders invokable. One real divergence: WindowId is QByteArray in
this port (Qt6 removed QVariant comparison operators;
windowinfowrap.h carries the contract) where the fork kept QVariant -
ids adapted, and the initial-state assertion reshaped because a Qt6
QVariant wrapping a null QByteArray is not null. Removal is driven
through the real WM-signal -> Windows-tracker -> LastActiveWindow
chain instead of the fork's invoke-private-slot-by-name. New cases:
removal precision for older history entries and no
currentWinIdChanged churn on same-window updates.

### 7+8. smartlauncheritemtest / tasksbackendtest - ADOPTED, extended
(the vendored-backend pins; one tight-group commit)

Premise checks: our plasmoid/plugin copies carry the full API both
tests drive (backend.h matches; smartlauncheritem is the vendored
Kai Uwe Broulik code with our KWin window-view watcher extension on
the backend). Compiled from the plugin sources like the fork does -
they live in the tasks QML plugin, not lattedock-core. These serve
the plasma-desktop sync pass as behavioral tripwires. Extensions:
launcherUrlChanged signal discipline (item); the Type=Link .desktop
refusal row, remote-url refusal, no-Categories empty row, and ALL
THREE generateMimeData keys - the fork never pinned the
taskbuttonitem entry Latte's own drop handling reads (backend). The
badge mutation path (Unity LauncherAPI DBus) stays live-verified.

### 9. datatypestest - ADOPTED, adapted to our contracts, redesigned case

Premise sweep found live defects (three in one function): see defects
6-8 below, fixed first in their own commits. Divergences from the
fork recorded in the test header: the missing-id table lookup pins
OUR c117e598e contract (loud qCritical + invalid record + write
discard - a new assertion proves the discard) and deliberately does
NOT exercise the by-index overloads out of range (that would freeze a
silent clamp our failure rules ban; out-of-range index stays a caller
bug). The poison-buffer isSelected case was REDESIGNED: reading the
poisoned bool lvalue is UB and gcc at -O2 folded the fork's
comparison to true even with the initializer removed (raw byte 0xFF,
comparison still passed - caught because the negative test failed to
fail); the case now asserts the raw byte through unsigned char*,
which detects deterministically. Extra row: geometry-less screen
serialization (the plain-name plasmashellrc shape).

### 10. settingsmodelstest - ADOPTED, extended, and it found a crash

The premise run SEGFAULTed: both models' data() guarded only
row >= rowCount(), letting the invalid index's row()==-1 through to
the GenericTable uint indexer (index 4294967295, crash inside
Data::Generic's copy ctor, reproduced under gdb). Defect 9 below;
fixed in all three models sharing the guard (colorsmodel too) before
the test landed. Extension: the screens-model bad-index/role
rejection twin (fork only covered the applets model's).

### Extra: sourceguardtest - two cases adopted (fix pins)

Not on the candidate list, but the two one-token fixes (defects 2 and
3) have no headless behavioral repro - the classes need the full
View/Corona/settings graph. Adopted exactly the two relevant cases
from the fork's sourceguardtest (brace-matched body, whitespace
stripped, positive AND negative token assertions); the rest of their
file pins their delegation-helper architecture and stays not-adopted
per the plan.

### SKIPPED: storageroundtriptest, templatesnametest

Mirror-logic tests, per the adoption plan and docs/TESTING.md's
reimplementation-testing ban. No further evaluation needed.

## Strengthening pass under the quality bar

- screenpooltest gained five cases after its initial landing: the
  \":\"-prefixed garbage-connector refusal, insert idempotency for a
  known connector, load() skipping reserved/non-numeric keys, removal
  persisting to disk read back via a fresh non-shared KConfig, and
  the plasma pool's id 0 <-> primary fallback pair.
- visibilityrevealtest already covered its entire input space (the
  full Visibility enum) at first landing; left as is.

## Negative tests

Actual outcomes, not plans; every fix was committed before its
negative test so a git checkout restores the fixed state:

- screenpooltest: reintroduced the removeScreens early return alone ->
  lattePool_removeScreensSkipsAbsentIdAndRemovesPresent FAILED on
  '!pool.hasScreenId(11)'; restored -> green. Process note: the first
  attempt used a sed that clobbered two unrelated `continue`
  statements in the same file and broke the build, and ctest then
  reported a stale PASSING binary - the lesson is to make
  negative-test edits with an exact-match edit, never a pattern
  substitution, and to confirm the build succeeded before reading the
  test result.
- visibilityrevealtest: removed AutoHide from the predicate ->
  revealsOnScreenEdge_revealingModes FAILED naming the AutoHide check;
  restored -> green.
- screenpooltest strengthening: removed the startsWith(\":\") guard in
  insertScreenMapping alone ->
  lattePool_insertRefusesGarbageColonConnector FAILED; restored ->
  green.
- abstractlayouttest: reintroduced the remove(lastIndexOf, 13) parser
  form alone -> three rows FAILED (non-layout kept, no extension kept,
  extension inside); restored -> green.
- activitiesinfotest: loosened the running filter to '!= Stopped'
  alone -> runningKeepsOnlyRunning FAILED (sizes differ); restored ->
  green.
- syncedlaunchersclienttest: reintroduced the cast-then-remove
  destroyed() handler alone -> the suite died with Exception: SegFault
  on the first broadcast after a client destruction (the exact
  production failure shape from 2026-07-13); restored -> green.
- lastactivewindowtest: dropped the updateInformationFromHistory()
  call from windowRemoved alone ->
  removingOlderHistoryEntryKeepsCurrent AND historyPrunesToBounds
  FAILED; restored -> green.
- smartlauncheritemtest: dropped the launcherUrl identity guard alone
  -> launcherUrlChangeEmitsOnceAndIsGuarded FAILED; restored -> green.
- tasksbackendtest: removed the taskbuttonitem mime insert alone ->
  generateMimeDataCarriesAllDragKeys FAILED; restored -> green.
- datatypestest: removed the isSelected initializer alone -> the
  fork's bool-comparison form did NOT fail (gcc -O2 folded the UB
  read; raw byte was 0xFF and the compare still passed) - the case
  was redesigned to assert the raw byte and then FAILED as required;
  restored -> green. The pre-fix tree also failed
  view_operatorQStringMarkers on the dead combined-marker branch and
  the doubled Primary token, which is how defect 8 was found.
- settingsmodelstest: reverted the applets-model row guard alone ->
  the suite died with SEGFAULT (the production failure shape);
  restored -> green.
- sourceguardtest: reintroduced the '==' typo and the missing '>' ->
  both cases FAILED naming their messages; restored -> green.

The datatypestest lesson generalizes: a negative test that does NOT
fail is a finding about the test, not busywork - here it exposed that
the transplanted assertion form was unable to detect the very defect
it claims to pin at this optimization level.

## Gates

(recorded when run)

## Commits

(final list; the merge to master will rebase these hashes)
