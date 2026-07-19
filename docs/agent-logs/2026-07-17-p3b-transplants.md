# P3b transplant wave - running ledger (2026-07-17)

Worktree: /tmp/latte-wt-p3b, branch p3b-transplants. Method per
docs/reference/captsilver-testability-adoption.md P3b: premise checked against OUR
code first, raised past the fork's cases, negative-tested. Reference:
~/Projects/latte-dock-qt6 at origin/main (read via git show; local
checkout sits at 9003f33a).

## Candidate-by-candidate

### shortcutstest - ADOPTED (f8bf1444c)

Fork: tests/shortcutstest.cpp at origin/main. Premises verified against
app/shortcuts/shortcutstracker.cpp and modifiertracker.cpp (both in
lattedock-core, upstream-shaped, identical parse logic). Raised:
data-driven badge table adds the branches the fork skipped (non-Meta
two-token uppercase, bare token F5, bare lowercase letter uppercased,
entry-19 boundary), multi-widget applet registry case, first-entry-
missing basedOnPosition row. Isolation defect in the FORK'S version
found while transplanting: their test left the session bus live, so
ShortcutsTracker's constructor (clearAllAppletShortcuts -> KGlobalAccel)
registered a throwaway component in the desktop's real kglobalacceld on
every run. Ours neuters DBUS_SESSION_BUS_ADDRESS and points
XDG_CONFIG_HOME at a temp dir BEFORE QGuiApplication (custom main, same
pattern as screenpooltest / askdestroysignalorderingtest).
ModifierTracker coverage stays idle-state only: KModifierKeyInfo key
events cannot be injected headless (same wall the fork hit).

### storagetest - ADOPTED (defect found + fixed first)

DEFECT (fixed in 9178957fd, fix(layouts)): Storage::updateView wrote
maxLength at the containment-group level while view() reads it from the
[General] subgroup - a max-length edit routed through view data (Views
dialog, inactive/storage-backed views via viewscontroller.cpp) landed on
a dead key and was lost. Inherited from upstream latte-dock (still live
on their master, verified via invent.kde.org raw fetch). Fork parallel:
their b48903ec is the same fix; their round-trip test caught it.

Test (this commit): drives the real Storage::self() singleton through
lattedock-core over temp .latte fixtures. Raised past the fork: view()
defaults table for unset keys, updateView non-Latte refusal (no
scribbling over foreign containments), clean-layout negative for the
errors/warnings scanners, plugins() containment-id filter made
observable (foreign containment carries its own applet; fork only
asserted rowCount >= 1), exportTemplate additionally pins
isPreferredForShortcuts and the LayoutSettings clearing. Dropped their
importContainmentsCopiesGroups: importContainments is PRIVATE in our
tree (fork widened it); its observable effect is pinned through
newView's inactive-layout branch instead - visibility not widened just
for a test. Learned mid-run: the warnings scanner counts ANY non-Latte
containment reachable from no view as orphaned, so the clean-layout
case needs a fixture without the foreign desktop containment.

### universalsettingstest - ADOPTED (b2292ecc7)

Adapted to our shapes: 2-arg ctor (their tree added a DI corona
parameter), layoutsMemoryUsage stays private here so not reached.
Raised: a dedicated case pins OUR deliberate divergence that
inConfigureAppletsMode is transient (never persisted, stale entries
deleted on load) - the fork's round trip asserts the opposite for
their tree. Custom main() isolates XDG_CONFIG_HOME (ctor resolves
kwinrc, load() resolves the autostart dir); load() cases seed
userConfiguredAutostart so first-run autostart enablement
(environment-dependent desktop-file copy) never runs.

### layoutmanagertest - ADOPTED (c81196d31)

Option lists, setOption dispatch, anchor properties, masquerade index
encoding. Their scheduled-destruction cases NOT carried: our
setAppletInScheduledDestruction refuses loudly without a container
(71b0d75a two-phase parking), pinned in layoutmanagerparkingtest;
adopting their blind-tracking assertions would pin the opposite.

### appletremovaltest - SKIPPED

Their removeAppletItem-on-destroyed()-removes-now assertion contradicts
our parking architecture: we PARK the container for the undo window
(the askdestroy contract + parkingLifecycle pin the full timeline).
Nothing to transplant that is not already covered better.

### importerlogictest - ADOPTED (defect found + fixed first)

DEFECT (fixed in 6aac18f32, fix(layouts)): nameOfConfigFile stripped
".latterc" via lastIndexOf + remove(index, 8); on Qt 6 a negative pos
wraps to the END, so extensionless import names lost their last
character ("Plasma" -> "Plasm", "A" -> ""). Proven with a standalone
probe against pinned qtbase 6.11.1. Reached from the settings dialog's
v1-config import. Fork parallel: their endsWith/chop rewrite.
Test (73ca496fb): raised with data-driven archive classification (+2
rows), split XDG_DATA_HOME/XDG_DATA_DIRS so systemShellDataPath's
last-entry pick is observable, missing-LayoutSettings default row.
uniqueLayoutName cases not carried (importerregressiontest is deeper).

### wmtoolstest - ADOPTED (46f88fd9a)

tasktools URL->AppData parsing + SchemeColors .colors parser. Raised:
data-driven skipTaskbar with the non-boolean row, by-NAME
possibleSchemeFile resolution (exact + whitespace/dash-simplified)
through a planted XDG_DATA_HOME - the fork only hit the absolute-path
branch. Their icon-non-null assertion dropped: QIcon::fromTheme
resolves against whatever themes the environment ships; would flake in
a bare CI image. XDG_DATA_HOME temp, XDG_DATA_DIRS host (KService/
KDesktopFile behavior stays production-shaped).

### coretypesenumtest - ADOPTED AS popupplacementcontracttest (4d9134e6f)

Premise check reshaped it entirely: their EdgePosition enum does not
exist in our coretypes (their addition) - SKIPPED. PopupPlacement DOES
matter here (plasmoid ContextMenu.qml feeds the ints straight into
PlasmaExtras.Menu.placement), but instead of their three hardcoded
ints, ours parses the PINNED libplasma's plasmaextracomponents
qmltypes (located at configure time, missing = configure error) and
pins all nine members at their declared indexes. Landed as a contract
test per tests/contracts rules.

### commontoolstest - ADOPTED (e2d75c1ce; defect fixed first in aa2ab9555)

Reshaped: their color cases do not apply (colorBrightness/colorLumina
moved to the ColorTools EX-19 core here; colortoolstest pins it
sanitized). What remains: rect<->string round trip + format pin +
XDG resolvers. DEFECT (aa2ab9555, fix(tools)): stringToRect indexed
parts[1] blind; input arrives from user-editable persisted screen
records (ScreenPool::load -> Data::Screen::init), so a corrupted entry
was an out-of-range crash at startup. Loud boundary refusal per the
failures-and-root-cause law, pinned by seven malformed rows.

### coretoolstest - ADOPTED (e2d75c1ce, same commit)

Environment constants + extras.h helpers, compiling environment.cpp
directly. Color half skipped (same EX-19 reason). Their
frameworksVersion==PLASMA_VERSION assertion replaced by OUR contract:
plasma/version.h is gone in Plasma 6, frameworksVersion() returns
KCoreAddons::version() (documented in environment.cpp).

### generictoolstest - ADOPTED (6e03b32cf; defect fixed first in 9838e9cad)

The settings-dialog delegate toolkit. DEFECT (9838e9cad,
fix(settings)): all four icon helpers (remainedFromIcon, drawIcon,
remainedFromLayoutIcon, drawLayoutIcon) resolved the -1 thickMargin
sentinel into a local and then sized the icon from the RAW parameter -
height+2 instead of height-2 under defaults, oversizing the slot 4px
and overflowing the row 3px. Upstream-inherited (lowercase/capital
lookalike); fork parallel d3be9b71 fixed the same four sites. Raised:
the RTL flip case (setLayoutDirection honored by the slot geometry; no
fork case drove it); their default-margin equivalence case pins the
fix.

### panelbackgroundtest - ADOPTED (3f2ddd6f3)

Both trees fixed the Plasma 6 Wayland band the same way (our Panel.qml
keeps imagePath empty with the story in a comment). Two live-engine
cases document the legacy backgroundHints fallthrough; a source scan
keeps the shipped wrapper clean. Nothing else pinned Panel.qml.

### configcontrolstest - ADOPTED (758cada52)

Qt6 Controls-2 rules (final base members kill type LOADING, tooltip ->
ToolTip attached, TabBar currentIndex, ListModel get() probing) plus
scans of our declarativeimports/components CheckBox/TextField/
ItemDelegate, which carry the same fixed shapes as the fork's.
Semantics fragments compile against plain QtQuick.Controls (the rules
are Qt's; no Plasma import-path dependence).

### schemesmodeltest - ADOPTED (3b6dda215)

Details-dialog Schemes model over planted .colors fixtures. Premise
note: our possibleSchemeFile carries a BreezeLight fallback for empty
config homes; inside the temp dirs it also resolves to nothing, so
row 0 stays the empty-file SchemeColors both trees expect.

### viewmodelstest - ADOPTED (b16a730b2)

TasksModel (insert/remove ranges, count discipline, dedupe + null/
unknown refusals, survivor shift) + IndicatorPart::Info (nine bool
capabilities via a setter/getter/NOTIFY table, int/float, emit-only-
on-real-change). Bare AppletQuickItem has a public default ctor at
6.7.3; addTask's applet() connect degrades to the production-tolerated
warning.

### layoutsmodeltest - DEFERRED

Constructs a live Latte::Corona ("constructs fine offscreen" in THEIR
tree, which has the CoronaEngine DI seams). In our upstream-shaped
tree Corona construction spins the whole app: session D-Bus service
registration, activities consumer, layouts manager loading, screen
pool. That is a corona-test-harness investigation of its own, not a
transplant; rushing it would violate the quality floor. Adoptable
premise checked and already satisfied: their static_assert that
currentData() returns by value (dangling-ref regression) - ours
returns const Data::Layout by value.

### viewsmodeltest - DEFERRED

Same reason: needs the live-Corona harness. Both filed as the P3b
tail in the adoption doc.

## Wave summary

14 suites adopted (ctest 66 -> 80 entries), 2 skipped
(appletremovaltest: contradicts our parking contract; coretypesenum's
EdgePosition half: their-tree enum), 2 deferred (layouts/views models:
live-Corona harness needed). Four inherited defects found and fixed at
the origin, each landing before the test that pins it:

1. 9178957fd fix(layouts): updateView wrote maxLength at the
   containment level; view() reads [General] - max-length edits
   through view data were lost. Upstream still carries it.
2. 6aac18f32 fix(layouts): nameOfConfigFile's remove(-1, 8) wraps to
   the string END on Qt 6 - extensionless import names lost their last
   character (proven with a qtbase-6.11.1 probe).
3. aa2ab9555 fix(tools): stringToRect out-of-range crash on corrupted
   persisted screen records - now a loud boundary refusal.
4. 9838e9cad fix(settings): four icon helpers sized delegate icons
   from the raw -1 thickMargin sentinel - oversized/clipped icons
   under default margins.

Gates at the end of the wave: full ctest green (80 entries),
coverage-ratchet OK. Not pushed (worktree branch; merge + gate-all +
re-pin hashes happen at integration).
