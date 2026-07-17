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

## Per-candidate verification and verdicts

(filled in as work lands)

## Negative tests

(actual outcomes recorded as they run)

## Gates

(recorded when run)

## Commits

(final list; the merge to master will rebase these hashes)
