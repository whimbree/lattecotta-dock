# Session handoff

Rolling handoff for the next session to pick up without re-deriving context.
Last updated 2026-07-16 (stabilization-execution session, in progress).
PHASE 8 IS OPEN - read its section in docs/PORTING_PLAN.md first; every item
is current there, several sections below are now RESOLVED and kept only as
archaeology.

## 2026-07-16: stabilization execution session TWO (running record)

Driving docs/prompts/stabilization-execution-prompt.md over the open
items (8, 9 remainder, 10). Session start: fixed session one's plan
drift (2bba6cb8b - P2 checkbox was never ticked, and the plan/design
doc carried the agents' pre-rebase hashes; master hashes now recorded,
agent logs got mapping notes; merge protocol lesson: resolve hashes
AFTER the rebase, at tick time). FOUR worktree agents dispatched in
parallel (ledgers in docs/agent-logs/, 2026-07-16-*): D-Bus
observability steps 2-4 (dbus-steps-2-4), CaptSilver P3 behavioral
tests (p3-behavioral-tests), sceneprobe follow-up scenes
(sceneprobe-followup-scenes), layershell try_compile guard + WindowId
newtype (layershell-guard-windowid). Merge serially with full gates
as they finish; busctl smokes for the D-Bus steps run live at merge.
Orchestrator meanwhile works the extraction-ledger live-verification
tail on the throwaway profile. scripts/run-e2e.sh found UNTRACKED
from session one - fold into the e2e-conversion work (quartet item c)
when the D-Bus steps merge.

MID-SESSION DIRECTION (from me, while agents ran): (1) sceneprobe
must WORK with a GPU but never REQUIRE one - dgpu device dispatch
becomes a documented optional extra, lavapipe stays the only CI tier
(adoption plan updated, agent instructed); (2) CaptSilver's testing
quality is the floor, not the ceiling - every adopted test gets
raised to this tree's bar (standing rule added to the adoption plan,
all four agents instructed).

LIVE TAIL, first pass (throwaway, run 1): EX-20 D-Bus badge arms all
PASS (count 7 / 9,999+ cap / clear / loud refusal in log 3x / progress
ring 50% right-half clockwise / full disc / count 42 via busctl emit -
gdbus is absent, `busctl --user emit` with sa{sv} works). EX-11 item 1
PASS (zero QML errors from the cutover files, zero launcher-record
warnings, cold start). INTERRUPTED: I was at the desk mid-run and
hand-added bottom docks to the displaced session at 20:58 (containment
14, removed, then 16 - the log's Storage::newView trail; every caller
is a user action, no auto-creation defect). Pointer-heavy recipes
would fight the desk, so --user-config was restored immediately;
the remaining matrix (EX-10, EX-11 2-6, EX-12, EX-14, EX-15, EX-17,
EX-19, EX-20 items 4-5) waits for an idle desk. THROWAWAY STATE: view
14 stripped (pre-strip backup at
build/_runconfig/latte/My Layout.layout.latte.pre-session2.bak), view
16 (bottom/primary/alwaysVisible) added by hand at the desk, and
containment 1 carries visibility=8 (SidebarOnDemand) from earlier
type-combo testing - craft the layout deliberately before the EX-10
matrix instead of trusting it.

DEFECT UNDER INVESTIGATION (root-cause in progress): ~180x
"Tools.colorBrightness: invalid color from QML" Criticals per
throwaway run, bursting at every view creation; ZERO on the real
config. Ruled out: scheme files missing groups (throwaway kdeglobals
and the generated default.colors/reversed.colors all carry complete
WM + Colors:Window groups); Storage::newView auto-creation. Config
difference spotted: throwaway containment 1 has themeColors=0
(numeric) vs real DarkThemeColors (name) - not yet proven relevant.
REPRO IS HEADLESS NOW: the staged dock runs inside
tests/sceneprobe/run_in_kwin.sh with its own dbus
(`nix develop -c tests/sceneprobe/run_in_kwin.sh dbus-run-session --
env LATTE_CONFIG_HOME=<copy> timeout 45 scripts/run-staged.sh -d`) -
80 criticals reproduced isolated from the live session. Two traps
banked: the dock inherits the caller's session bus and exits
instantly on the KDBusService unique name (wrap in dbus-run-session);
without -d the message handler swallows everything. TEMPORARY
instrumentation lives in declarativeimports/core/tools.cpp +
CMakeLists (V4 stack trace on invalid color, Qt6::QmlPrivate) built
in build-probe/ (BUILD=build-probe, never build/ - the running dock's
binary stays untouched); REMOVE both before committing anything
there. This nested-dock recipe is ALSO the P4 e2e vehicle proof.

## 2026-07-16: stabilization execution session ONE (record)

Drove docs/prompts/stabilization-execution-prompt.md top to bottom.

SESSION CLOSE: all three worktree branches merged serially with full
gates and pushed; final master state: 54 ctest entries, ratchet OK,
qmllint baseline matched, build-check both variants OK, sceneprobe
gate 9/9 PASS on merged master, real dock running --user-config,
worktrees pruned. The sceneprobe harness is LIVE: pure-CPU lavapipe
rendering under nested kwin_wayland, goldens blessed bit-exact
({0,0} tier, no tolerances needed - text-free scenes + pinned Mesa
beat upstream's variance), gate self-test armed. build-check.sh's
re-exec now demands the pinned /nix/store toolchain (the system
cmake 4.1 trap two agents hit independently). Remaining from the
CaptSilver adoption: P3 behavioral tests, P4 e2e pixel assertions,
follow-up scenes (parabolic_zoom with a pinned font, colorizer
stack, monochromatic icons, indicator glow), and cross-machine
golden verification once a second machine exists.

MERGES COMPLETE (late session): P2 contract transplants merged (52
ctest entries; includes a REAL live-bug fix - the alternatives swap
invoked createApplet with a QPoint that cannot resolve against Plasma
6's QRectF signature, so picking an alternative destroyed the applet
and created nothing; fix verified signature-correct against the
pinned header, ONE live "Show Alternatives" sanity pass still wanted
at the desk). viewsData + setViewEditMode merged (53 ctest entries)
and LIVE-VERIFIED over D-Bus on the throwaway: viewsData returns full
per-view state (geometry, struts, visibility, inStartup/isOffScreen -
the stranding diagnostics are now pull-queryable), and the
setViewEditMode enter/exit round-trip asserted editMode true->false
with zero pixels. Both agent worktrees pruned. Sceneprobe transplant
agent still running at write time. Two agent findings worth acting
on later: build-check.sh's devshell re-exec guard trusts any cmake
in PATH (system cmake 4.1 defeats it), and the unguarded
LATTE_LAYERSHELL_HAS_SET_SCREEN try_compile re-runs every configure
and can silently flip the define after one broken-env probe.

LATE-SESSION STATE: priority items 1-7 done or flagged (details in
the numbered entries below); the settings desk-walk, the DPMS
lock/unlock arm, one real logout cycle, and the reorder/stuck-icons
real-mouse checks are the four things waiting on my hands. Item 8's
D-Bus interface design is WRITTEN (docs/dbus-observability-interface.md)
and item 9 became the CaptSilver adoption plan
(docs/captsilver-testability-adoption.md). FOUR worktree/read agents
were dispatched in parallel at session end (logs in docs/agent-logs/,
2026-07-16-*): a11y surface inventory (read-only baseline for the
keyboard/AT-SPI quartet items), viewsData+setViewEditMode
implementation, sceneprobe P1 transplant (lavapipe-only), and P2
contract-pin transplants. Their branches merge serially with full
gates; if this session ends before the merges, the worktree branches
and their logs carry everything needed to finish.

1. DONE - Session shutdown teardown (priority item 1): 525f556c6
   (~Corona explicit dependency-order deletes; the deleteLater pile was
   a silent no-op - Qt contract pinned in quitteardowncontractstest),
   e02d1bcde (plasmashell's quit pattern: AA_DisableSessionManager,
   setQuitLockEnabled(false), SIGTERM through the clean quit path; ng's
   ksmserver poller and D-Bus shutdown-check deliberately NOT adopted -
   plasmashell has neither, reasons in the commit body and plan item),
   9d183984e (lifecycleState D-Bus readback, the observability half),
   adcc68357 (ratchet baseline). All gates green (46 ctest, ratchet,
   qmllint 155/6026 matched, both WITH_X11 variants). Live-verified on
   the throwaway: SIGTERM -> full quit trail -> 5 views unloaded ->
   clean exit ~1.5s, no core; teardown log shows the destructor
   completing ("Latte Corona - deleted..."). FLAGGED FOR MY HANDS: one
   real logout/login cycle (kills any driving session by definition) -
   check journal for the SIGTERM line + clean exit afterwards.
   Note for future probes: lifecycleState is the pull-query for "is
   the dock up" - poll it instead of sleeping after restart-staged.

2. DONE - Startup retry deadlock + latency (priority item 2): the
   deadlock is ng-only machinery (their layoutmanager.cpp retry loop;
   ours is upstream-shaped, no retries) - closed not-applicable with
   live evidence (all views' restore() logs show complete applet sets,
   recorded==produced order), plus 9df0732f9 making restore()'s three
   silent drop paths loud. Latency: the local measurement record
   stands (3.7-4.5s, floor owned by upstream sync applet loading).
   LOGIN FINDING flagged for my hands:
   ~/.config/autostart/org.kde.latte-dock.desktop points at the
   PACKAGED latte-dock-ng fork binary - login starts NG, not this
   port; the autostart story needs my decision (plan item has the
   detail), and no reboot/login has happened since June 25 anyway.

5/6. DONE with a big catch - Phase 7 research + Phase 4 layer-shell
   (priority items 5-6, partially): edit-mode detection contract
   pinned (87417a0c7, userconfiguringcontracttest - the notification
   is reliable, ng chased ghosts); reorder research verdict NO CHANGE
   NEEDED (agent log docs/agent-logs/2026-07-16-reorder-research.md:
   ours == qt6 fork == upstream placeholder design; ng's jitter was
   their own live-item feedback loop); click-add and drop-insertion
   audited as already-correct (masqueraded index, TapHandler, Qt5
   end-append); 'aplet' typo fixed (c97c6bb38 + baseline shrink).
   THE CATCH: the Phase 4 struts check found AlwaysVisible docks
   reserving NOTHING - root-caused to the isOffScreen trigger gap,
   fixed 538abc8ec and verified via KWin clientArea; and a DEEPER
   intermittent startup-stranding defect (inStartup stuck true
   forever, first-run-after-rebuild timing) is filed as a new Phase 8
   item with a 15s watchdog + breadcrumbs armed in the containment.
   kde_output_order_v1 item verified done and ticked. STILL OPEN in
   the Phase 7 cluster: stuck-icons z=100 suspect (task delegates
   never reset z after reorder - agent finding), dead
   target.animating guard removal, boundary-insertion owner call.

4. DONE - Cloned-view sync gap (priority item 4): e3fdcae78
   (apply-or-defer + retry for all three manually-synced properties),
   f7561df37 (viewAppletsOrder D-Bus readback). Live-verified: clone
   spawned on DP-3 via AllScreensGroup, per-position plugin identity
   match. Replication-design prerequisites both discharged - the
   Replicate Dock continuation feature is now unblocked. All gates
   green (46 ctest, ratchet, build-check both variants).

3. PARTIAL - Lock/unlock visibility (priority item 3): one clean
   throwaway cycle (lock 40s, unlock): dock survived, five views
   mapped before AND after, no ~20s exit, no invisibility. The
   missing ingredient is an OUTPUT-OFF event (DPMS), which a short
   lock never produces - the DPMS-cycle arm is deferred to a
   coordinated/idle moment (I was at the desk and the lock already
   interrupted me once). Both mystery symptoms stay armed with the
   new quit trail + lifecycleState readback. THROWAWAY LAYOUT STATE:
   it now carries MY extra duplicated bottom dock on DP-2 (view 14,
   I made it by hand at 18:09 while poking the throwaway mid-repro)
   - expect SIX views next -d run, or remove view 14 first.

## MODEL-TRANSITION PRIORITY STACK (2026-07-15 evening, read this first)

A strong model (Fable 5) is available for only a few more days, then
development continues on a weaker model. The remaining strong-model
sessions are budgeted, in order:

1. DONE this session: the preview-pipeline contract gate (b4f5621c,
   scripts/preview-contract-rules.sh, ctest previewcontractrules) pins the
   ten line-level invariants from the hover-lag excavation, negative-tested
   against three injected regressions. The most delicate machinery in the
   tree now defends itself.
2. DONE 2026-07-15 evening (the backlog-flush session, results below in
   its own entry): iconSize hang and duplicate-dock crash were already
   dead (annotated), jitter fixes had all landed as commits, Show
   Alternatives root-caused one layer deeper and FIXED (56549d73), the
   dead GHNS widget-download entry (desk report mid-session) FIXED
   (ab3caae1), quit-reason logging landed for the lock/unlock exit
   (ee0f1ba3), add-panel crash did not reproduce (7 adds, 3 templates;
   breadcrumbs silent since 07-11 - watch item).
3. DONE 2026-07-15 (this session): the QML extraction plan is written,
   committed and pushed - docs/QML_EXTRACTION_PLAN.md (bab18b2c,
   2fb1bd27, f017854c, e554bf04 + closing commit), with the Phase 10
   cross-reference item in docs/PORTING_PLAN.md. All 239 package QML
   files classified, 25 units specced with delegation tags, completeness
   ledger CLEAR (every section done). Pin-in-place verdicts recorded for
   CompactApplet chain, ContextMenu, ConfigOverlay drag, and friends.
4. DONE (2026-07-15 night + late night, two execution sessions; the
   strong-model shortlist is COMPLETE): capt sync EMPTY both sessions
   (origin/main still at 81384003). First session landed Wave 0
   (353fd783, 70d38a3e, 8ccad784, 66a41253), EX-01 (03cf0289,
   2f23f9bd), EX-03 (c3c04e49), EX-02's design+core (0613c2ae,
   ee66b07c). Second session (this one) finished EX-02: 1c94a7ef
   (core reshaped to the emission plan + DeadStop, driven by three
   NEW recorded dead-position cases - loader-inactive lockZoom
   applets kill live stacks but pass clear broadcasts; the spec's
   DESIGN REVISION bullet has the full reasoning), 504e95e5
   (containment cutover: deciders deleted, routing in
   ParabolicEffectPrivate, spacers on direct absorb calls, bridge
   re-entry via host.routeFromIndex), faa89fd1 (plasmoid cutover:
   decider + sltTrack watchers deleted, exports from the route
   result; definition's applyParabolicEffect body deleted). Live
   glide matrix ran BEFORE each cutover merged: preview-dialog
   anchor byte-identical at a fixed parked pointer (2366,1529
   274x204 across pre/containment/plasmoid runs, 1px on the last
   with the desk in active use), mid/rest screenshots match, log
   signatures identical in counts. STEP 2.5 also DONE and pushed
   (5bd40951): ASan+UBSan on all tests/units targets
   (latte_add_unit_test, negative-tested, four cores green), C++20
   raised from 17 (KDECompilerSettings 6.0.0 does NOT set 20 -
   verified, both WITH_X11 variants build-checked), qmllint ratchet
   gate (qmllintgate ctest entry, tests/qmllint-baseline 170 files /
   6923 curated warnings, negative-tested x2, strict-on-touch binds
   cutovers from HERE on), Method + section D + TESTING.md +
   CLAUDE.md amendments, STEP-2.5 ledger entry. NEXT SESSION STARTS
   AT: Wave 2 (delegate-safe C++-only capt ports; plan section E),
   then Wave 3 with EX-17 as calibration - waves are UNGATED, the
   same prompt continues from the ledger. Session notes: dock is
   back on --user-config (verified XDG_CONFIG_HOME + 6 views); the
   glide matrix script lives at the scratchpad's glide-matrix.sh
   pattern (fixed waypoints + park + dumpwins preview readout - the
   preview-anchor position is the precise parabolic-geometry
   equality instrument); one glide run captured its screenshots
   before the docks finished mapping under desk load - re-run warm
   rather than trusting a first pass when Bree is at the machine.
   POST-CUT ADDITIONS (same night, at my direction, all pushed):
   jq joined the devshell and the qmllint gate parses --json category
   ids (9f62ce01 - the text grep silently undercounted: qmllint's
   fix-suggestion lines carry no trailing newline, baseline corrected
   to 7098); the step-2.5 law then applied RETROACTIVELY to the four
   landed units (0119f6df cores: variant Action + optional sentinels
   in ParabolicRouter, optionals replacing kNoTask in
   PreviewSwitchEngine, itemsCount assert in ParabolicMath, EX-22
   reviewed clean - eliminated states named per ledger entry;
   365286e2 QML: plugins.qmltypes for org.kde.latte.core clears
   unresolved-type tree-wide with the regen recipe in
   declarativeimports/core/CMakeLists, implicit handlers +
   own-property qualification fixed, bridge+client parabolic
   abilities at ZERO, baseline 164 files / 6614, context-chain
   residues NAMED as structural exceptions in the plan's section D -
   zeroing them is the per-subtree injection endgame, not a per-unit
   retrofit). Glide matrix re-verified after the QML edits: two
   consecutive runs agree and match the pre-cutover anchor; the
   observed deltas were desk-environment (window-content height,
   one-run task-set variance), root-caused in the 365286e2 body.

Also done this session: CLAUDE.md reframed as maintained continuation
(ce94bb1d) - upstream mergeability is no longer a planning constraint;
bisectable small commits stay, with their honest justification.

## 2026-07-15 midday: resizable persistent popups LANDED (d12baff2..c3026dea)

- The full continuation feature from the plan: edge-drag resize on applet
  popups (vendored libplasma WindowResizeHandler in Latte::Quick, plus a
  resizeStarted() signal), popupWidth/popupHeight persisted in the applet's
  config group with plasmashell's exact keys, save only after a real user
  resize (deliberate deviation, commented), "Reset Popup Size" context-menu
  entry that re-sizes a pinned popup live, customPopupSize branch in the
  CompactApplet sizing chain with Layout.minimum still enforced, re-anchor
  suppression during the grab with a single re-anchor on release. Verified
  live on the throwaway layout with fakepointer: grow, persist, reopen at
  size, reset-in-place. qml gates + 41 qml tests + 21 contracts + 14 C++
  tests all green.
- Hard-won findings, all encoded in code comments and commit bodies:
  - KConfigWatcher CANNOT deliver for layout files: kconfig's change
    notification embeds the file name in a DBus object path and layout
    names carry spaces ("My Layout.layout.latte"). dbus-monitor showed no
    ConfigChanged even for notify-flagged writes. The reset wake-up is an
    in-process dynamic-property bump on the applet instead.
  - KConfigGroup::parent() never goes invalid: the root group is named
    "<default>" and stays its own parent. A path-building walk on
    isValid() alone hung the dock at startup twice (caught with the gdb
    wrapper's live stacks); the code that needed it was removed with the
    KConfigWatcher approach.
  - End-of-resize-grab has no protocol signal. Enter (KWin re-enters the
    surface when the grab ends) + Hide + a 2s resize-quiet backstop timer.
    MouseMove/MouseButtonRelease are NOT usable: events queued between
    startSystemResize() and the grab engaging arrive a beat later and
    ended the session in the same millisecond it started.
  - The systemtray plasmoid hosts its OWN libplasma AppletPopup (its QML
    instantiates PlasmaCore.AppletPopup), which persists popupWidth on
    every hide with zero interaction - so systray always carries the keys
    and always shows the reset entry. Upstream-inherited, harmless.
  - My real layout's digitalclock already carried popupWidth=560/
    popupHeight=400 (pre-dating this feature, likely plasmashell-era or
    from the systray-noticed save path); the feature now HONORS them, so
    the real clock popup opens 560x400 on first run. Reset is one
    right-click away if that reads wrong at the desk.
- Settle-gated hover chrome: the profiling prerequisite RAN and cleared
  the suspect, so the 50ms dwell gate was NOT built (the plan item holds
  the numbers: engage frames max 8ms on a 4.1ms baseline over 1377
  frames; the only stalls were the previews dialog's first show, already
  behind the 150ms hoveredTimer, and startup). If hover lag is still
  felt, profile the previews first-show and swap backpressure next.
- Rebuild-cost pass (autonomous, 2026-07-15 afternoon): strace-measured
  the adoption stall - 23k stat()/adoption, 96% ENOENT, dominated by
  KSvg probing the nonexistent translucent/colors theme variant per Svg
  instance. Landed 056f7e15 (XDG_DATA_DIRS allow-list: 273 devshell
  closure entries -> 18 deliberate; blast-radius verified: theme,
  icons, systray, context menu, previews all intact) and f1edd103
  (delegate cache -> depth-4 LRU, eviction via TaskItem
  Component.onDestruction contract; four-task tour builds once,
  second tour fully cache-served). Honest numbers in both commit
  bodies: the trim barely moved the adoption syscall count (probe
  count dominates, not dir count) - the stall killer is the LRU.
  Remaining levers live in the plan item: ksvg negative-caching
  (upstream) and async incubation.
- Seventh layer (window stuck at the previous task's size, reported as
  "1 spotify + 3 blank"): probed the adoption pipeline end to end and
  it was CORRECT - the failure was the previews host's declarative size
  bindings vs the base dialog's imperative mainItem stamps; a late echo
  of the old window size after a content SHRINK left the binding
  dormant forever. Fixed d56a26aa with imperative size enforcement in
  the host's change handlers (revert any write disagreeing with the
  active delegate's natural size; equality check ends the recursion in
  one step). Reproduced the stuck 1096 pre-fix with the exact bounce
  recipe, then verified 274->1096->274->1096 tracking post-fix. This
  echo family is the same one CompactApplet's sizing chain comments
  document - any future mainItem whose size derives from swappable
  content needs the imperative-enforcement shape, not plain bindings.
- Sixth layer (content corruption after the cache, reported within
  minutes): DelegateModel SILENTLY resets its root when its model swaps,
  and the previews DelegateModel swaps models by design (isGroup flips
  between 1 and tasksModel); equal-value rootIndex reassignment emits no
  change, so the correct root was never re-applied - revived groups
  showed one window, fresh delegates flashed the previous/unrelated
  content, and the 'Could not find window id 0' screencast error was a
  transient instance built before the isGroup binding existed. Fixed
  235753b8: rootIndex assigned LAST plus a refresh token the
  DelegateModel binding references. VERIFICATION LESSON, hard-won: test
  hovers with GLIDES (small pointer steps), never coordinate jumps -
  jumps land beside parabolic-shifted icons, miss the enter entirely,
  and produced hours of phantom 'flaky engagement' plus one false
  content-bug lead today.
- Fourth and fifth layers: the debounce shipped with a regression
  (deferred switches skipped show(), whose first act cancels the hide
  countdown every task exit arms - scrubbing hid the previews under the
  pointer; fixed 54ed1974 by cancelling in the defer branch), and
  hover-open flip-backs still paid the full rebuild per adoption -
  fixed 0913bbee with a two-slot delegate cache (active + previous task
  kept alive, visibility-flipped; parked bindings stay live, pipewire
  streams stay warm while the dialog shows; revive = rootIndex refresh
  only). Verified: konsole 4-group -> firefox -> konsole revives all
  four thumbnails with the binding set skipped. KNOWN GHOST, filed not
  guarded: the flaky first-synthetic-engagement after a restart can
  build a delegate against a half-ready engagement (one misplaced
  1440,425 popup and a transiently single-width group seen ONLY in
  that state, never in controlled cycles; the misplaced-popup family
  predates the cache - same artifact at -7843,425 this morning
  pre-feature). If a wrongly-built cached delegate is ever reproduced
  with a real mouse, the fix belongs in the engagement path, not as a
  cache guard.
- Third layer of the same report (still chugged "moving sufficiently
  quick"): adopting a task into the previews dialog rebuilds the whole
  delegate tree on the GUI thread - 100-400ms per switch measured with
  an event-loop lag probe, 859ms cache-cold, dominated by KSvg disk
  lookups (gdb mid-stall stack: SvgItem::componentComplete ->
  QStandardPaths::locate). Fixed in 4b533b8d with a trailing-edge burst
  debounce keyed on request cadence: sweeps coalesce to one rebuild at
  rest (measured: 2 adoptions instead of 7 on a 9-crossing 140ms
  sweep, zero mid-sweep stalls). NEXT LEVER if wanted: bring the
  per-adoption rebuild cost itself down - KSvg lookup storms during
  ToolTipInstance creation, or instance reuse across tasks.
- Follow-up on the same desk report, second layer: after the remap fix
  the residual "trigger and move on" lag was the wayland thumbnail
  streams - every preview is a kwin screencast and negotiation measured
  270-500ms per window (STREAM-PROBE), leaving a blank box exactly at
  hover-trigger crossing speed. Fixed in 4f96acb8 by extending the
  existing minimized/X11 icon fallback to the warm-up window (strict
  === false so X11's property-less item never matches). Probe-verified:
  icon in the same pass as the switch, streams ready ~300ms, icon
  yields the same millisecond. previewsDelay=300 now set in my real
  layout (was the 650 default). A stream keep-alive pool for recently
  hovered tasks is the natural next step if revisit warm-ups still
  bother; not built.
- The hover stutter itself was then described precisely at the desk and
  root-caused to something else entirely: preview task switches UNMAPPED
  and REMAPPED the dialog per icon crossing (wire-logged: attach(nil),
  231ms hole, fresh configure round each time) - a workaround from 07-13
  02:44 whose premise died at 09:27 the same day when the dialog gained
  live wayland re-anchoring. Fixed in c6eeeb20: switches now re-anchor
  the mapped window via set_position; verified on the wire (zero nil
  attaches across a stepped sweep) and by screenshot. The other half of
  the report ("half a second to show up") is the previewsDelay default
  (650ms, Qt5-faithful, user-tunable) plus the ~21-32ms first-show
  build; deliberately untouched.
- Colorfulness auto-exemption fixed same day it shipped (5c06b497): LCD
  subpixel fringes made the digital clock's white text measure 49-84%
  "saturated", so its colorizer switched off ~8s after load and palette
  changes never reached it again (caught at the desk under
  LightThemeColors). Judgment now area-averages the grab to 12x12 cells
  first - fringe pairs cancel within a stroke-width cell, solid icon
  color survives. Full measurement trail in the commit body.
- The throwaway 3-dock layout's bottom view is viewType=1 (Panel) +
  alignment=10 (Justify) + panelSize=100 - the full-width background it
  draws is the correct rendering for that config, not a regression
  (checked against the enums and my real layout's viewType=0 pill, which
  rendered correctly the same morning). Almost certainly left flipped by
  an earlier session's Dock/Panel type-combo testing.
- WATCH: one real-config restart brought the dock up with edit mode /
  the primary config view already open, uninvoked (log:
  "#primaryconfigview# initialization started" right at boot,
  10:52:44). Closed cleanly via its Close button; the immediately
  preceding real-config restart the same morning did not do this. If it
  recurs, suspect persisted config-view state from the prior session
  that ended mid-drive.

## 2026-07-15: tests-first coverage batch + class-A stranding sweep (worktree, headless)

- Regression and contract coverage landed for the recent lifecycle fixes
  that had none: tst_compactapplet.qml (3b37750b: 437d9a0c popup sizing
  chain + 1aa5238c release contract against the real shipped file),
  askdestroysignalorderingtest (5f94159c: libplasma undo-window signal
  ordering, plain vs containment-type), representationswitchtest
  (9a1195dc: AppletQuickItem inline-switch unwiring/never-re-shows/
  detach-to-null contracts), layoutmanagerparkingtest (f269e457: the
  71b0d75a parking state machine end to end, real plugin sources + real
  applets), tst_plasmaindicatorgroupsvg.qml + tst_loadergatecontracts.qml
  (2b800846: the 841c2ca4 null-svg guard and its ordering premise; the
  KSvg crash itself is unpinnable - it is a SIGSEGV, documented in both
  files). Full ctest is 13 entries, all green, plus the gate.
- Sweep of the two class-A shapes (conditional-anchors + size bindings,
  once-sampled geometry): full per-site verdict inventory is in the new
  Phase 10 sweep item in the plan. One fix landed (eca51ae0): the
  reassert remedy applied to plasmoid main.qml's three same-shape
  siblings (barLine, belower, shadowsSvgItem) and mouseHandler's
  reassert extended to location changes. Once-sampled geometry came back
  a clean negative.
- Session tooling note: constructing libplasma object graphs offscreen
  WORKS and is cheap - the recipe (concrete Corona subclass as parent,
  qrc applet path :/qt/qml/plasma/applet/<pluginid>/ instead of KPackage,
  LATTE_QML_MODULE_PATH -> QML2_IMPORT_PATH, DBUS_SESSION_BUS_ADDRESS
  neutered so askDestroy's KNotification never reaches the desk, and
  applet->config() before anything calls setDestroyed) lives in the two
  contract tests; reuse it rather than re-deriving.
- Session tooling note: moc's lexer silently stops at a raw string
  literal in a .cpp - every Q_OBJECT class after it loses its metaobject
  and the failure surfaces as an undefined vtable at link. Plain
  concatenated literals in test files.
- LIVE CHECKS PENDING from this session: the three new plasmoid
  reasserts under a real perpendicular flip (repro recipe in the
  eca51ae0 commit body), and the 71b0d75a undo arm still wants its one
  live Undo-click confirmation (unchanged from that item).
## 2026-07-15: headless wayland-platform/multi-screen session (worktree)

- Three commits, all headless-only, LIVE VERIFICATION PENDING flags in
  their plan items: 87749d93 (layershellmappingtest now pins the
  ec5d2316 exclusive-zone rules and the 793faad2/d670c97a cross-screen
  remap cycle on real QWindows - two offscreen outputs come from the
  offscreen platform's JSON configfile, written by a custom main),
  377aad57 (applet-created dialogs - the comic full-size viewer -
  inherit their view's screen via a Corona app-wide Show filter; the
  comic dialog QML was extracted from the pinned plugin binary, the
  libplasma behavior read from the pinned 6.6.5 tarball), 08511ffd
  (deliberate chrome hides cancel both deferred-show mechanisms; the
  stuck-overlay family traces to closed sessions whose showAfter timer
  or waiting-for-size flag mapped chrome windows afterwards).
- FINDING that redirected a fix mid-flight: on the pinned Qt 6.11 a
  QQuickWindow-derived window declared inside an Item gets NO
  transientParent (the Qt 5 declared-inside-an-item magic lives only in
  QQuickWindowQmlImpl now); the QObject resource-child parent survives.
  The first draft of the corona filter keyed on transientParent and the
  new contract test (tst_transientwindowcontracts) failed it
  immediately - both halves are now pinned (appletwindowparentingtest
  is the C++ half). If any future fix wants "the window's view", walk
  the QObject parent chain, not transientParent.
- SECOND catch in the same fix, worth its own trap note: on a QWindow*,
  window->parent() is QWindow::parent() - the window-parent overload,
  null for QML-declared dialogs - and it converts silently to QObject*,
  so a QObject-chain walk written with it compiles and matches nothing.
  Use window->QObject::parent(). The walk now lives in
  Latte::visualHostWindowOf (tools/commontools) with its own headless
  test, df63fe9e.
- Clean negative worth keeping: QQC2 popups on the pinned Qt 6.11 are
  in-scene items (popupType 0, probed offscreen through a scratch
  qmltestrunner case) - popup-as-native-window theories about the
  stuck overlays are dead; what lingered were the chrome windows
  themselves.
- Live queue for the next session with a compositor: comic zoom viewer
  lands on the dock's screen; stuck-overlay recipes (close chrome
  within the 200ms secondary interval, layout switch with chrome open)
  leave no mapped chrome window; enumerate what the 1096x527/1096x204
  layer-6 windows actually were.

## 2026-07-15: headless silent-Qt6-break sweep (worktree session)

- Full-tree sweep of the silent mechanical break families, each against
  its landed exemplar (Array.isArray, removed Qt enums, int pointer
  caches, string window ids, QQC2 shadowing, destroyed()-handler casts,
  QVariant list reads). Findings and all clean negatives are itemized at
  the end of Phase 10 in the plan; three fixes landed (f5ff449b wm
  numeric id tests, 2d28cda1 indicator style/glow buttons, 75780c64
  containmentDestroyed freed-memory edge read) plus contract-suite
  growth (98986641: destroyed()-demotion C++ contract, AbstractButton
  indicator shadowing, Qt.MidButton removal, int fraction truncation)
  and a windowinfowrap regression test.
- Everything verified headlessly only: build-check green on both
  WITH_X11 variants, all 8 ctest entries pass. THREE LIVE CHECKS
  PENDING, marked in their plan items: activeWindowMaximized engages
  with a maximized window touching a dock (also: a focused dialog keeps
  activeWindowTouching), the four indicator-config style/glow buttons
  apply on press, and dock deletion still slides out on its own edge.
- Session tooling note: the bare `qml` tool in the devShell runs
  offscreen probes silently (no console.log output reaches the
  terminal); use qmltestrunner through scripts/qml-interaction-tests.sh
  for headless QML probing instead - contract-style TestCases double as
  the probe and the pin.
- Session tooling note: build-check.sh's self-reexec into nix develop
  only triggers when cmake is absent from PATH; a host cmake without
  ninja fails the configure. Run it as `nix develop -c
  ./scripts/build-check.sh` from a bare worktree.
## 2026-07-15: headless sweep - loops, degenerate indexes, lifetime hazards

Systematic sweep of the ad9b823f/46dc83c5/fa02b887/52c2987b/d6d57e61
families instead of waiting for the next coredump. Eleven commits; the
Phase 10 sweep entry in the plan lists them all with mechanisms. Fully
headless session: nothing was driven live, so several fixes carry
"noted for the Phase 10 sweep" markers for live confirmation (activities
delegate with a stale activity assignment, config-view slide-out racing
dock removal, recreate-during-removal, sort persistence round-trip,
dock/panel type flip after applet removal).

Clean negatives from the sweep (checked, no defect, so the next sweep
does not re-derive them):

- QML while-loops all terminate: head/tail scans in AppletItem/BasicItem
  strictly advance past finite lists, ContextMenu's eliding loop bottoms
  out at empty text vs a non-negative limit, restoreLaunchers always
  splices two off per pass, wheel-delta loops step by fixed 120.
- C++ while(!empty) teardown loops all remove before/while iterating
  (corona unload relies on libplasma removing containments on destroyed
  - upstream-shared contract, comment already in place).
- All first()/takeFirst()/last() sites are isEmpty-guarded.
- QMutableListIterator removal loops (alternatives objects) only
  pointer-compare captured pointers, never dereference - the d6d57e61
  discipline holds there, same for the applet destroyed() handlers in
  containmentinterface (remove-by-identity only).
- tasktools/servicesFromCmdLine indexOf chains all guard or use
  QString mid/lastIndexOf semantics safely.

Guard inventory - silent guards found in the swept paths, each assessed,
NOT blanket-fixed (per CLAUDE.md a guard is a contract or a bandaid):

- templatesmanager templateName(): the .view.latte else-arm calls
  remove(ext,...) unguarded; ext=-1 would chop the last 11 chars.
  Unreachable today - both callers filter *.layout.latte / *.view.latte
  dir entries. Contract-by-construction; left as is, would need a loud
  warning if a third caller ever appears.
- importer nameOfConfigFile()/abstractlayout layoutName(): same shape,
  remove(-1,n) truncates one char off a non-.latterc name. Feeder is
  the import file dialog (filters .latterc); cosmetic wrong name at
  worst. Left as is.
- corona windowColorScheme(): no '-' in the dbus payload makes both
  halves the whole string. Tolerant parse of our own dbus signal;
  harmless, left as is.
- waylandinterface switchToNext/PreviousVirtualDesktop(): unknown
  current desktop resolves to position -1 which behaves as
  before-first (next goes to first, previous wraps/returns). Sane
  degraded behavior, left as is.
- tasksmodel add/move/restore guards read 'plasmoid && !contains' so a
  null plasmoid FALLS THROUGH: addTask would null-deref three lines
  later. Unreachable (the only caller is behind an 'else if (ai)'),
  but the guard shape is misleading - candidate cleanup, not a bug
  today.
- originalview removeClone(): '!cloned->layout()' arm drops the clone
  from m_clones without slideOut/removeView - silent leak path if a
  clone ever loses its layout while registered. Origin not proven
  headlessly; left, watch item.
- launchers Validator upwardIsBetter(): goal.indexOf(val) can be -1
  when current is not a permutation of goal, making splice insert
  before-last; the function is a direction heuristic so the answer is
  just suboptimal, no corruption. Left as is.
- AutoSize updateIconSize() maxLength<=0 early-return: deliberate
  contract, commented in place since ad9b823f (re-run wired via
  onMaxLengthChanged).
- fa02b887's liveness filter in Storage::importLayoutFile: still a
  band-aid by its own admission (deleter unidentified); NOT
  headless-drivable (needs a live corona), so the importerregression
  extension was assessed and skipped rather than faked.

Session tooling notes: the worktree build needed its own cmake
configure (nix develop -c cmake -B build -S . -G Ninja
-DCMAKE_BUILD_TYPE=RelWithDebInfo) before any target built; ctest picks
up the new indicatorfactoryremovaltest without extra wiring. KDirWatch
deletion delivery works offscreen and the whole factory suite runs in
~1.3s, so it is safe for the default ctest pass.
## 2026-07-15: headless effect-contract sweep (agent worktree, no dock runs)

Everything in this section was done WITHOUT launching the dock; all
mechanisms are pinned from the flake-pinned Qt/libplasma sources and
headless probes, and the visual/warning halves are queued for the next
live session (each fix's commit body says exactly what to look at).

- The churn 't1 ... (QQuickItem*)' warning is ROOT-CAUSED (5f8c10be):
  the (QQuickItem*) suffix means the sampler variant holds a NULL item;
  libplasma nulls the expander's compactRepresentation on the inline
  switch to full rep (appletquickitem.cpp:384), and CompactApplet's
  click flash (alwaysRunToEnd) kept sampling it. Gate now includes the
  representation. LIVE CHECK: click an applet, hover-zoom it across
  switchWidth, confirm zero t1 warnings.
- MECHANISM CORRECTION for family 7, earned from qquickmultieffect.cpp
  + qgfxsourceproxy.cpp and pinned in tst_multieffectcontracts.qml:
  Qt6 MultiEffect DOES auto-wrap plain Item sources in an internal
  ShaderEffectSource (hasProxySource). The real trap is that the proxy
  decides direct-vs-wrapped at polish time and NEVER repolishes when
  the source's layer.enabled flips, in either direction - so a source
  whose layer drops while any effect node can still preprocess it
  (opacity-0 nodes still preprocess; only visible:false removes the
  node) strands a dead provider. That refined rule produced: the
  colorizer visible gate (230774d0) and the forceMonochromaticIcons
  layer hold (1932db32). Masks are NOT proxied at all (maskSrc is set
  raw), so plain-item masks stay outright invalid - unchanged rule.
- Sibling-copy shadows finished off in tasks (b634ef67): ParabolicItem
  task shadow, basicitem ShortcutBadge (missed twin of c7c46226), and
  the remove-from-group ghost (shadow+desaturate collapsed into one
  layer effect; ShadowedItem carries saturation riders, contract in
  tst_shadoweditem). Forced monochromatic icons restored to
  ColorOverlay semantics at both sites (1932db32, 1f835402 family).
  Drag-greying badge effect gated visible (2c726d4b).
- Startup 'No QSGTexture' first-frame bursts EXPLAINED, no action:
  MultiEffect's internal shadow/blur chain is layered BlurItems
  sampling each other; first frames after a window maps warm the chain
  level by level. Bounded, never accrues at idle. Plan item updated.
- Tests: qmleffectrules ctest (autoPaddingEnabled only ever literal
  false in shipped QML, negative case verified),
  tst_multieffectcontracts.qml (autoPadding default, per-side
  paddingRect via itemRect, proxy behavior both directions),
  tst_shadoweditem rider test. All 7 ctest entries green.
- Tooling note: the offscreen platform never starts a scenegraph
  render loop - QSG_INFO prints nothing and TestCase.grabImage returns
  blank white for everything. Sampler warnings and texture content are
  NOT headlessly observable; polish-level state (hasProxySource,
  itemRect) is. Do not write pixel assertions into offscreen tests.
- Open, filed in the plan: the window-preview thumbnail shadow is
  still a sibling ShadowedItem whose proxy layer-grabs the live
  pipewire item every frame (PipeWireSourceItem is not a texture
  provider) - the e88af680 crash-class question needs a live gdb
  reproduction attempt before touching it.

## 2026-07-12 late evening: comic hover crash + edit-mode chrome trilogy

- Comic hover SIGSEGV root-caused and fixed (9ea29eaa): parabolic zoom
  crosses comic's switchWidth/switchHeight, Plasma 6 AppletQuickItem
  swaps representations by itself, and CompactApplet's Qt5-style
  representation stealing (parent capture, cross-window popup
  reparenting) turned each swap into a scenegraph use-after-free.
  Fixed on the upstream plasma-desktop Plasma 6 pattern (plasmoidItem
  property, isLatteAppletContainer marker walk, clean release of the
  outgoing representation, popup nodes returned to the dock window
  before teardown). Isolation acquitted the clicked-effect MultiEffect
  and both applet layer sites first. NOTE: mid-investigation a careless
  marker insertion split the two-line isMarginsAreaSeparator expression
  and turned every applet into a margins-area separator (no zoom, no
  clicks, layout in pieces) while the QML gate stayed green - the gate
  cannot catch semantics, only compilation. Anchor multi-line
  expressions in full when editing.
- Edit-mode chrome trilogy (caught live: blueprint flash+vanish,
  rearrange dying instantly, dock stuck in edit visuals) root-caused as
  THREE stacked latent defects, none introduced today: stale persisted
  inConfigureAppletsMode (fb621102), chrome focus-grab racing its own
  assembly, and hideEvent running session-end work on transient hides
  (both 4a8ac480). Today's dozens of crash-repro kills armed the stale
  persistence repeatedly, which is why it LOOKED like a fresh
  regression. Full loop verified live including a headless drag
  reorder; fakepointer learned drag (c1ee9e2b) and lives at
  ~/.local/bin/fakepointer.
- Comic now survives hover but EXPANDS from zoom alone (popup opens
  with no click) - behavior question vs Qt5 filed in the plan.
- Still open, filed: startup 'No QSGTexture' warnings (48/run, source
  unidentified, family 7 by fingerprint), default indicator main.qml:92
  dead binding, right-click menu missing 'Applet Settings'
  (pre-existing per my recollection).
- Round two, desk-driven: the rearrange input mask was carved from a
  once-sampled, wrong rect (outer Button item stretched to 2544 wide
  after chrome retargeting) - a full-width stripe ate hover and drags
  across every applet's middle. Fixed by re-carving on the rect's
  notify signal and mapping the interactive chip (8be2b388). The
  'un-toggle exits edit mode' report did NOT reproduce with the
  settings pinned (focus-loss close excluded) - retest with a real
  mouse. Two new items filed: the outer-Button width stretch
  (mechanism unidentified, now inert) and the vertical-dock canvas
  header rendering off-surface (left dock rearrange unusable, chip at
  y=-552). The settings pin (sticker) button is the way to keep chrome
  alive during headless driving; fakepointer clicks never hold real
  focus.
- Round five, into the small hours of 2026-07-13 (36160c46, afefa442,
  04d8000c and their docs commits): rearrange drag drift fixed (int
  lastX/lastY truncating wayland's fractional pointer coords - the
  int-vs-fractional audit hint is in the plan; fakepointer drag learned
  multi-waypoint zigzags to prove it). Edit Dock wrong-view targeting
  fixed (stale per-view pointer to the chrome singleton; show path now
  requires parentView()==this and the recreation catch-up releases the
  previous owner). Vertical-dock rearrange fixed via the canvas content
  reload on retarget (cross-view binding stranding class, mechanism
  demonstrated on the header's frozen y). Applet context menus restored
  with the Configure entry available ALWAYS (my call): the root
  was Panel.qml's viewLayout discovery missing that Plasma 6
  containment roots ARE ContainmentItems, plus three layered defects
  above it - see the plan item for the full chain. Default indicator
  dead binding fixed. QSGTexture warning sources identified by config
  bisection (applet-shadow first frames + TaskIcon's three unlayered
  MultiEffects, which accrue during idle runtime - fix shape filed).
  STILL OPEN, in priority order: TaskIcon/CompactApplet effect-source
  layering (73da8400 completion), hover-modal inconsistency and
  mispositioning (#recipe in plan), edit-mode first-open latency,
  startup latency (measure without the gdb wrapper first), containment
  menu corner-placement watch item, left canvas geometry flip-flop
  watch item.
- Round six, autonomous (d619ae08, d98bff98, 69baabf0 + docs): my minimal
  preview recipe (hover System Monitor, glide to
  Firefox, preview ~370px left) root-caused past the deferred re-show:
  the timer path skipped preparePreviewWindow so content and anchor
  could interleave across tasks. show() now binds visualParent and
  content atomically at map time and cancels stale pendings; verified
  with glide at realistic rates AND a fresh-map screenshot with the
  preview dead-center (2395 vs icon 2394). Effect-source layering then
  landed (69baabf0): startup warnings 45 -> 7, ZERO accrual over 95s
  idle and over hover churn; residuals are first-frame bursts (~28
  applet-shadow, known lower-risk class). VISIBLE CHANGE: task
  hover-brighten/click effects render for the first time since the
  port - they were invalid-sampler no-ops before. The sudden
  hover brightening is restored Qt5 behavior, not a new effect. Badged-icon effect rendering still unverified (no badge app
  was running). Next in queue: hover-modal inconsistency in rearrange
  mode, residual ~40px preview offset during zoom dwell (live vs
  resting rect, refine d98bff98), then the latency items.
- Round nineteen (2026-07-14): the Latte->Plasma indicator switch
  crash root-caused and fixed (841c2ca4). The gdb harness earned its
  keep: two runs, identical stacks, and a register dump (rdi=0)
  proving SvgItem::setSvg received a NULL, not a dangling pointer -
  KSvg 6 dropped Qt5's if (m_svg) guard in updateDevicePixelRatio()
  (still unguarded at ksvg master; upstream report candidate, noted
  in the plan item). The null was plasma FrontLayer's svg:
  resources.svgs[0] evaluating before the QML->C++->QML
  setSvgImagePaths round trip lands; fix gates the group-arrow
  Loader on the svg existing (deterministic: Loader active binding
  subscribes before any arrow exists, connections fire in
  establishment order). Verified live: switch, round trips, cold
  start with plasma persisted, frames render; my indicator
  setting restored to Latte afterwards. METHOD note: the crash
  always killed the process before KConfig synced, so the persisted
  type stayed latte - which both kept the repro armed across runs
  and hid the cold-start arm of the same race until tested
  deliberately.
- Round twenty (2026-07-14): the child-env leak fixed for its worst
  arm (00a6766c). KIO source reading (v6.27.0) settled the mechanism:
  systemd >= 250 means every dock-launched app is a transient service
  whose Environment= is a verbatim copy of the dock's env - no KIO
  hook to scrub. QT_PLUGIN_PATH now never enters the dock's env:
  run-staged hands the two dirs over as LATTE_EXTRA_PLUGIN_PATHS and
  main.cpp addLibraryPath()s them (process-local, inert var for
  children; the codebase already had this hygiene pattern for
  QT_QPA_PLATFORM). Canaries all green: right-click menu, zero
  KWindowShadow failures, and a konsole spawned via the task menu
  verified clean by reading its transient unit's /proc environ.
  Residuals (QML2_IMPORT_PATH per-engine env reads, stage-first
  XDG_DATA_DIRS, empty QT_QPA_PLATFORMTHEME degrading dev-child
  theming) documented in the plan item as accepted with reasons.
  FIELD NOTE: dock task icons ACTIVATE existing windows - to test a
  real spawn use the context menu's Start New Instance and find the
  child via systemctl --user list-units 'app-*' (process names are
  nix-wrapped, pgrep -x misses them).
- Round twenty-one (2026-07-14 evening): the colorizer check became a
  three-layer excavation (1f835402). The filed double-draw suspicion
  was real but MASKED: applet colorizing has been a silent no-op since
  the port because MultiEffect.colorization multiplies the target
  color by the source's gray level (read multieffect.frag:84 at the
  pin) - dark text colorized towards a light scheme comes out dark
  again. Both reference forks shipped that substitute for Qt5's
  ColorOverlay and lost the feature without noticing. Fix:
  Qt5Compat.GraphicalEffects ColorOverlay (same-pin package added to
  flake LATTE_QML_MODULE_PATH + package.nix) with the shadow as
  layer.effect (the sibling ShadowedItem ghost-double-struck the
  colorized text, same class as c7c46226). METHOD notes worth keeping:
  a lime Rectangle proves a subtree paints; a raw ShaderEffectSource
  proves the texture pipeline; forced garish colors beat subtle real
  ones for pixel truth; and check WHAT CONFIG the previous probe left
  behind - my own From Window=Active dropdown click made applyColor
  follow the active window's #232629 scheme 17s into every run, which
  cost three probe cycles to un-confuse (that tracking WORKS despite
  the missing-KWin-script warning, incidentally). NEW plan item filed:
  LatteComponents.ComboBox popups render collapsed rows with invisible
  text (found while driving the Appearance dropdowns headlessly).
  Unexplained leftover: during two mid-session probe runs the dock
  window mapped but painted nothing for ~2 minutes; not reproduced
  under the final code (four clean sub-30s starts), so only noted.
  My config fully restored (no colorizing keys, shadows on) and
  verified rendering unchanged.
- Round twenty-two (2026-07-14 evening, verification only): the
  comic/WebEngine free-run under threaded is GONE on HEAD. First
  measurement was a false negative - the throwaway layout's active
  copy had LOST its comic applet during earlier repairs (grep the
  layout for plugin= before trusting a no-repro; with-comic.bak
  restored and left active). With the comic present: settled idle
  0.2-1.6%, QSGRenderThreads ~1%, over minutes. No code change made;
  plan item closed with the plausible absorbers named (dialog rework,
  kwindowsystem wayland plugin arrival) and a bisect pointer if it
  returns. Also observed: the 3-dock throwaway burns 40-100% CPU
  MAIN-thread for ~60-90s after start (render threads quiet) -
  layout/applet settling, not a render loop; noted in the item, no
  new plan entry.
- Round twenty-three (2026-07-14 night, desk-driven): the ComboBox
  collapsed-popup defect confirmed by hand and fixed (a302d742). Headless
  qmltestrunner probes did the whole bisect without touching the live
  dock: Array.isArray(control.model) is always false on Qt6 (model
  reads back as QVariantList), so all role lookups took the
  model[role] branch - undefined for array models. The pin's actual
  semantics, measured: arrays put roles on modelData only, ListModels
  on model[role] only. Fix resolves from whichever exists; regression
  test covers the three model kinds the config pages use. TWO traps
  banked: the interaction-test harness reuses _qmlstage, so component
  edits need a restage before the test sees them (a probe cycle died
  to the old file); and the ListModel case is what proved the obvious
  modelData-only fix wrong - test all model kinds, not the broken one.
- Round twenty-four (2026-07-14 night, desk-driven): the intermittent
  settings-window overflow root-caused and fixed (1b932ed9) watching
  the repro live on screen as it ran. Method: a geometry-sampling loop
  (dumpwins every 300-400ms) around cold restarts vs warm reopens -
  warm was 12/12 correct, cold mapped +99px too tall (93px past the
  screen top) and STAYED wrong. The 99px equals the dock's own
  reserved thickness; corona integrates the view's own reserved area
  ~14s post-start (measured), the warmed chrome computed
  availableScreenGeometry before that, and upstream d30143f7's
  self-origin exclusion in updateAvailableScreenGeometry dropped the
  correction forever. Fix accepts self-origin updates (commented
  deviation). Verified across three cold runs after the fix: stale
  mapping lasts <1s then snaps correct; confirmed on screen: no overflow.
  RESIDUAL filed in the plan item (Phase 8 family): the <1s stale
  flash exists because corona's rect itself is late - the real lever
  is why reserved-area integration lags ~10s behind first paint.
- Round twenty-five (2026-07-14 late night): the reference-fork sync
  pass (d67e635a, df1c14da). Full record lives in the Phase 10 SYNC
  PASS plan item; the headline: one real fold from ng
  (alternativeshelper _plasma_graphicObject -> itemForApplet; "Show
  Alternatives" had been silently dead), qt6 woke up with a 54-commit
  testability campaign with nothing foldable verbatim, and their one
  behavior fix repairs a mapping mistake we never made - our
  layershellmappingtest comment already documents rejecting their
  layerFor(WindowsGoBelow)=Bottom. LESSON re-earned mid-pass: I
  drafted the same four-site explicit-mode change their fix makes,
  then found our own test file documenting why it is unnecessary
  here and REVERTED before commit - check what this tree already
  decided before folding a fork's fix for the fork's own bug. Also:
  add-via-drag ticked (hand-verified; ownership single by
  construction), missing "Show Alternatives" MENU ENTRY filed (the
  restored handler is unreachable until it exists), fakepointer
  needs no extension for drag-and-drop (drag is
  press/waypoints/release), GUI-CI candidate tests banked in the
  Phase 10 e2e item pending the planned microvm.
- Round twenty-six (2026-07-14, spilling past midnight): applet popup
  sizing fixed to the Plasma 6 contract (437d9a0c, landed before this
  round was written up). CompactApplet.qml now mirrors libplasma
  v6.6.5 appletpopup.cpp LayoutChangedProxy: the full representation's
  implicit size is the live base, Layout.preferred* only an override,
  Layout.minimum* enforced. Verified live on the volume applet
  (260x152 clipped -> 260x491 correct; the width sits at plasma-pa's
  own 252px minimum). IMPORTANT non-bug: plasmashell renders the same
  applet wider because of MY persisted custom size there
  (popupWidth=457 under the systemtray group in its appletsrc), not a
  different default - do not chase that delta again. The follow-up
  regression sweep (reopen volume + calendar, native-res screenshots)
  was attempted headlessly and ABORTED: I was at the desk actively
  using the machine, so the real mouse and fakepointer fought over
  hover/focus, and a NEW trap surfaced - spectacle's own task icon
  joins the dock during capture and shifts applet positions ~50px, so
  any locate-then-click that spans a spectacle run mis-targets (one
  click landed on Dictionary instead of the volume applet this way).
  Sweep evidence stands anyway, from my own hands: calendar and
  dictionary popups open SIMULTANEOUSLY on this build during the
  session, both rendered and positioned correctly off the dock. Two
  new bugs filed in the Phase 10 stabilization list with full
  investigation state: widget removal leaves a multi-second ghost
  slot in rearrange mode (C++ removal already immediate per 33830b2c,
  lag is downstream - repro ONLY on the throwaway layout), and
  edit-mode background opacity is not WYSIWYG (~half opacity at
  Opacity=100% in plain edit mode; blueprint vs real-background chain
  mapped, decisive next step is reading the Qt5 original main.qml
  from this repo's own history - CLAUDE.md names this exact area as a
  fork trap).
- Round twenty-seven (2026-07-15 small hours, desk-driven, I was at
  the machine and confirmed fixes live): both round twenty-six bug
  filings root-caused and fixed the same night. Edit-mode opacity
  (38e60eb9): the blueprint drew OVER the dock background; Qt5's
  composite is wallpaper > grid > background > applets, and my
  persisted editBackgroundOpacity=0.5 was exactly the "half" wash -
  the fix is child reorder in DragDropArea, the opacity chain itself
  was already byte-identical to Qt5. Removal ghost (71b0d75a):
  libplasma askDestroy() guards its immediate appletRemoved with
  !isContainment() and the systemtray IS a containment
  (CustomEmbedded), so its only appletRemoved comes from inside
  ~Applet when the undo window ends - measured 59.8s ghost pre-fix
  (destroyedChanged at click+159ms, appletRemoved at +59.8s = the
  libplasma 60s fallback timer), first-frame collapse post-fix,
  hand-confirmed ("it instantly deleted this time"). Fix restores
  Qt5's two-phase parking keyed to destroyedChanged, with undo
  restore-in-place and a duplicate-container guard; the undo arm
  still wants one live Undo click (popup auto-hides faster than a
  headless locate-then-click loop). METHOD notes: read the pinned
  libplasma tarball straight out of /nix/store (no checkout needed);
  the spectacle task icon shifts dock applet positions DURING
  capture, so never locate-then-click across a spectacle run; the
  rearrange-toggle window position varies between edit sessions -
  read it from dumpwins (it is its own 133x77 latte window), not
  from a previous session's coordinates. THROWAWAY layout note: the
  active copy was restored from with-comic.bak twice during the
  repro; it ends the session WITH its systray back. My real dock was
  NOT restarted back to --user-config yet at write time - do that
  (or I do it by hand) before daily-driving resumes.
- Round twenty-eight (2026-07-15 late night, desk-driven, I reported
  and co-drove throughout): the "comic stopped rendering" and "dock
  bar is white in dark mode" reports both dissolved into precise
  causes, none the feared commit regression. Comic = THREE findings:
  the black-disc icon was the 1f835402 colorizer flattening a
  full-color icon under Dark Colors (my palette flip-back proved it
  live; colorizer SCOPE defect filed - Qt5 rules need reading); the
  dead hover was the comic applet's own provider list with xkcd
  UNCHECKED (I found it in its settings; checking it fixed hover on
  the spot) - near-certainly reverted by the two with-comic.bak
  restores during the removal-ghost repro, so BEWARE: re-activating
  a layout backup silently reverts APPLET config too; and the probe
  runs surfaced a real latent defect, fixed (1aa5238c): the 9ea29eaa
  release path hid unwired full representations while libplasma's
  inline representation switch (applet grown past
  switchWidth/switchHeight) re-uses the cached item without ever
  re-showing it - the comic's full rep sat parent=null visible=false
  after startup churn, measured. Release now detaches to a null
  parent, visibility untouched, upstream-symmetric. White bar =
  build/_runconfig has NO kdeglobals/plasmarc, so every throwaway
  run resolves the default LIGHT scheme; the dark-bar memory is from
  --user-config runs (decisive test queued: restart --user-config
  and look; seed the throwaway with kdeglobals regardless). Dark
  Colors NOT darkening the background strengthens the dead
  background-arm suspicion in that item. NEW items filed:
  applet-created dialogs (comic's full-size viewer) open on the
  wrong screen (screenshot evidence, Phase 8 family), and a
  background color picker as a continuation feature (Qt5 has none).
  Session state: throwaway layout active with the comic working
  (xkcd checked), my real dock still not restored to --user-config;
  the removal-ghost undo arm still wants one live Undo click.
- Round twenty-nine (2026-07-15, deep into the night, desk-driven with
  me duplicating docks live while the probes ran): the duplicated-dock
  overlap fixed (e412889d). The long road mattered: config was proven
  clean, restore() proven correct, plain duplicateView proven clean,
  screen-move proven clean - the trigger is ONLY the live formFactor
  flip, and the defect is mouseHandler's width/height bindings dying
  in the tasks plasmoid while every input updates around them
  (measured: vertical=false, icList=592x88, mask=88, mouseHandler
  frozen 88x592). Fix is the afefa442-class binding reassert on
  orientation change; the destroyer itself is an open watch item in
  the plan entry. METHOD kit built along the way and worth reusing:
  a repeating containment-side geometry probe (per-layout kids with
  scene mapping, wrapper geometry, per-applet formFactor and Layout
  hints) plus a tasks-side value probe, and a fully headless repro
  recipe - dbus duplicateView, chrome cycling by canvas-window
  geometry, screen dropdown and edge-button coordinates for the
  pinned settings window. removeView/duplicateView over dbus went
  incident-free this time (ids verified against the layout file
  before every removeView). ALSO seen and filed nothing new: the
  1096x527 layer-6 chrome window on DP-3 appears when the settings
  chrome opens while Advanced is on and can linger - matches the
  Phase 8 'secondary advanced-mode window not covered' note; fold it
  into that work. Session state at close: throwaway active with the
  user's own re-made clone at DP-3 bottom (healthy), all my test
  clones removed, tree clean and pushed; my real dock still awaits
  the --user-config restart, which doubles as the dark-bar
  discriminator; the undo-click verification on widget removal
  remains queued.
- Round thirty (2026-07-15 ~01:00-02:30, autonomous drive, I am
  remote-monitoring): the defect-class initiative started. All 314
  post-fork commits classified into eleven defect classes (recorded
  in the round-twenty-nine-adjacent conversation and the wave
  charters); tests/contracts stood up (fadd9012) - the suite that
  pins libplasma/KSvg/Qt behaviors Latte relies on so pin bumps fail
  in ctest instead of misbehaving in the dock; seeded with the Qt6
  QVariantList round-trip contract. THREE WAVE-1 SUBAGENTS running
  in isolated worktrees at write time: mechanical Qt5->Qt6 breaks
  (+int-truncation audit), effect/texture-source contract sweep, and
  loops/degenerate-values sweep - each also writes regression tests
  for landed fixes and contract tests for its class; none may touch
  the live dock. Merges and live verification happen serially after
  they report. MEANWHILE the color complex CLOSED (79ca3360): the
  dark-bar report, the Dark Colors "dead background", and the black
  silhouette were ALL the throwaway's missing kdeglobals degrading
  themeExtended's variant resolution - proven by a live Dark Colors
  flip on the real config (background darkens, applets flatten
  LIGHT, tasks stay full color, all Qt5-faithful per the Qt5 source;
  palette restored after). run-staged.sh now seeds the throwaway
  config with the session kdeglobals. Colorizer scope item closed as
  not-a-bug. TRAPS learned: removeView rides the SAME 60s undo
  window as widget removal - restarting inside the window resurrects
  the containment (verify the config group is gone before
  restarting); stuck chrome popups (Type combo, secondary advanced
  window) linger across sessions and EAT CLICKS aimed at the
  rearrange toggle - filed as a new chrome-lifecycle item. The undo
  click on widget removal remains for hand verification (chrome
  fighting made it unreliable headlessly). Session state: real dock
  back on --user-config, palette default, tree clean and pushed.
- Round thirty-one (2026-07-15 ~03:30, autonomous): WAVE 1 of the
  defect-class initiative is MERGED (300e9477 mechanical breaks,
  74586980 loops/degenerate values, 17ed3bc9 effect sources - each
  agent's own docs record its full findings above/below this entry).
  Net: 17 fix commits, 6 test commits across the three branches; the
  contract suite went 1 -> 14 tests; new ctest entries
  windowinfowraptest, destroyedtypedemotiontest,
  indicatorfactoryremovaltest, qmleffectrules; both new
  bug-reproducing tests were verified to catch their original bugs
  with the fixes reverted. HEADLINE MECHANISM CORRECTION from the
  effect agent, now pinned in contracts: Qt 6.11 MultiEffect DOES
  auto-wrap plain sources; the real traps are the source proxy never
  repolishing when layer.enabled flips, and opacity-0 effect nodes
  still preprocessing - update the family-7 mental model. The t1
  warning root cause: null-variant source from libplasma's inline
  representation switch feeding the clicked flash (gated now).
  WAVE 2 RUNNING at write time: lifecycle/representation regression
  and contract tests (A+C) and wayland/multiscreen prep including
  the wrong-screen dialog and stuck-chrome-popup items (E+F).
  CONSOLIDATED LIVE-VERIFICATION LIST for the desk (in rough order):
  removal Undo click restores the slot; dodge-maximized engages with
  a maximized touching window; the four default-indicator
  style/glow config buttons apply; dock removal slides out on its
  own edge; t1 warning gone on the click+hover-churn recipe;
  task/badge/ghost shadow visuals unchanged; forced-monochromatic
  task icons render; duplicated-dock flip drive-through (e412889d
  regression check). The dock was STOPPED for the merged build-check
  at write time - if this handoff is read cold, check the dock is
  running before anything else.
- SESSION CLOSE STATE (2026-07-14 night): everything committed and
  pushed; working tree clean; my dock runs the latest build
  under the gdb wrapper with --user-config, config fully restored
  (Latte indicator, no colorizing keys, shadows on). The 2026-07-13
  queue is CLEARED: indicator switch crash fixed (841c2ca4), child
  QT_PLUGIN_PATH leak fixed with residuals filed (00a6766c),
  colorizer restored end-to-end (1f835402), comic/WebEngine free-run
  verified gone (docs only). The throwaway 3-dock layout has its
  comic applet back (with-comic.bak made active again). OPEN, in
  rough priority: the settings-window-overflows-screen-top report
  (needs my repro - which monitor/mode; plan item filed),
  the LatteComponents.ComboBox collapsed-popup defect (plan item
  with symptoms), startup latency phase 2, and the Phase 8 backlog.
- SESSION CLOSE STATE (2026-07-13 night): everything committed and
  pushed through 1a49f118; working tree clean; the dock runs the
  latest build with --user-config (my REAL ~/.config, single
  bottom dock - their Jul 10 layout). NOTE: plain restart-staged.sh
  without --user-config loads the THROWAWAY 3-dock test layout from
  build/_runconfig (which contains two comic/WebEngine applets that
  free-run under the threaded loop - see that plan item). Apps
  launched from the dock inherit its env; --user-config keeps their
  profiles correct (the spotify lesson), but QT_PLUGIN_PATH /
  QML2_IMPORT_PATH / stage-first XDG_DATA_DIRS still leak to
  children - filed as a dev-harness item... actually not yet filed:
  NEXT SESSION should file or fix it (child-env scrubbing).
  KWin state restored: fade loaded, SlideInTime default, all probe
  scripts unloaded. NEXT UP, in order: the Latte->Plasma indicator
  switch CRASH (plan item with repro recipe, use the gdb harness),
  colorizer double-draw, comic/WebEngine spin, the corona
  double-windowAdded curiosity (mixed single/double adds per popup
  open - benign for the slide but unexplained).
- Round eighteen, the slide saga SOLVED (1f8770fd, confirmed live: 'it slides
  in now!!'): the final layer was PlasmaQuick::Dialog deriving its
  own slide hint from location INSIDE its first-expose handler, which
  on the threaded loop blocks until the mapping frame is committed -
  the one moment no external re-assert can reach. Floating location
  = unset every open. Fix: pin location to the dock edge for
  AppletPopup dialogs in updatePopUpEnabledBorders; the base then
  slides it natively (and hides the dock-facing border, Qt5 look).
  LESSON worth its weight: when a base class acts inside a
  frame-blocking call, the ONLY winning move is changing the inputs
  its own computation reads, not racing its output. The double
  windowAdded lead was a red herring (mixed single/double adds,
  orthogonal). Our Dialog updateSlideEffect machinery stays as
  reinforcement for surface-recreation cases.
- Round seventeen, the popup slide saga (347f413a, f630d2ad; slide-in
  STILL OPEN, full evidence chain + leads in the plan item): the goal is
  plasma-parity popup animations - popup must emerge from
  behind the dock like plasmashell's from their panel. Banked along
  the way: the staged env was missing kwindowsystem's wayland plugin
  ENTIRELY (dialog shadows had silently never worked - allow-listed
  the exact linked package's plugin dir in run-staged), and applet
  popups now carry the appletpopup role (slide-OUT works,
  hand-verified). Slide-in remains eaten by the scale effect; the
  hottest lead is a DOUBLE windowAdded per open seen by the KWin
  probe. HARD-WON METHODOLOGY NOTES: popupWindow=false is CORRECT
  for appletpopup-role windows (wasted a probe cycle); client-side
  slide offsets computed pre-map are garbage that make the effect
  clip the animation invisible - always send -1; KWin effect
  slow-motion (SlideInTime=1500) + selective effect unloading are
  cheap discriminators for 'which effect animated that'; a KWin
  windowAdded script probe beats theorizing about classification;
  and READ upstream sources at the pinned tag before hypothesizing
  about them (three ordering theories died to source reading).
  Also new: audio-badge volume wheel now matches plasma-pa exactly
  (299a241b, hand-verified) and fakepointer has a scroll subcommand.
  QUEUED NEXT: the Latte->Plasma indicator switch crash (gdb harness
  item in the plan), the colorizer double-draw check, comic/WebEngine
  free-run under threaded.
- Round sixteen, threaded render loop (28fdca5f): the bar was set -
  'behave like plasma where possible' - and reported jittery
  per-task volume scroll plus a chugging calendar popup. Both traced
  toward the basic render loop: plasmashell runs threaded here (8
  QSGRenderThreads), latte ran basic (a stabilization-era default
  whose own commit said revisit after the effect audit). Measured:
  calendar interaction under basic = 417-610ms polish-dominated
  frames serialized on the gui thread; under threaded = p50 5ms, p99
  6ms, max 41ms, zero over 100ms. All historical crash recipes clean
  on threaded (the effect audit removed the churn that raced).
  CAVEAT filed: comic (WebEngine) applets free-run under threaded at
  ~20%/instance - bisected precisely (comic-hosting docks spun,
  comic-free layouts idle at 0.1%); basic stays as env override.
  OPEN: the volume-scroll jitter needs a retest under threaded, and
  if it persists, compare our per-task wheel path against plasma's
  volume applet (wheel accumulation/debounce) - fakepointer has no
  wheel support yet, add a scroll subcommand for headless driving.
- Round fifteen, corona init profiled (docs only): the 2.4s corona
  estimate was wrong - it is ~1.0s; the real pre-paint cost is the
  first view's 2.6s create-to-map pipeline (QQmlTypeLoader floor plus
  one-time inits gdb-sampled inside applet instantiation: ICU
  calendar data, notification-service DBus registrations). Tried an
  off-thread ICU pre-warm (QLocale::toString) - measured a no-op
  (identical stall pattern; Qt uses its own CLDR tables, the applet's
  ICU entry is elsewhere) and REMOVED it instead of shipping it.
  Startup verdict: 3.7-4.5s to first painted dock, remainder owned by
  upstream synchronous applet loading - local work here is done.
- Round fourteen, chrome warmup (fd8cbc45): first Edit Dock 7.3s ->
  1.5s. The corona builds the settings/canvas ensemble hidden 8s
  after synchronizer init. TWO traps documented for posterity:
  PrimaryConfigView's constructor showed AND started a configuring
  session via setParentView() (warmup attempt one flash-showed and
  self-closed through the focus machinery - constructor gained
  showOnCreation=false); and setParentView() early-returns on an
  unchanged parent, so the factory path in
  View::showConfigurationInterface never showed the warmed-hidden
  ensemble - first click was a SILENT no-op (explicit
  showConfigWindow() there now, gated parentView()==this). Also
  learned: qDebug through a redirected stdout FILE is block-buffered
  - a 'silent' log during idle means unflushed buffer, not a hung
  process; check D-Bus responsiveness before assuming deadlock.
  Watch item: first open on a non-warmed view (retarget from
  never-shown state) was not driven headlessly.
- Round thirteen, staggered startup (70fe5390): initContainments now
  queues one view creation per event loop pass. First dock visible at
  exec+3.7s (was 4.5s), all three at 6.3s (was 5.3s) - the promised
  ~2s did not materialize because ~2.4s of corona init precedes any
  view creation; the stagger only removes waiting behind sibling
  views. Verified: geometries identical, previews/menu/edit-chrome
  all work, ctest green (appstreamtest failure pre-existing without
  the change). If the slower last-dock feels worse live, the revert
  is exactly one commit. NEXT startup lever if wanted: profile the
  ~2.4s corona init (theme mask parsing was one visible chunk).
- Round twelve-c, ghosts finished (c7c46226): the per-side rect fix
  was not enough - it re-reproduced by hand on the clock. The sibling
  shadow pattern (ShadowedItem Loader sampling a still-visible
  original) draws content twice and trusts MultiEffect to place its
  padded copy pixel-exactly; it lands a few px off, double-striking
  text applets while icons just look slightly bold (which is how my
  full-screen visual pass missed it - RULE: verify text applets at
  NATIVE resolution, imagemagick via 'nix run nixpkgs#imagemagick'
  since the host has no crop tool). ItemWrapper and ShortcutBadge
  shadows are now layer.effect - double-draw impossible by
  construction. Colorizer's shadow keeps the sibling arrangement
  (samples wrapper while the colorizer MultiEffect draws) and is a
  filed suspect: in colorizing mode it likely draws an UNCOLORIZED
  wrapper copy over the colorized one; its comment assumes
  ShadowedItem draws only the shadow, which is false - MultiEffect
  always draws source content plus shadow. Idle CPU still 0.1%.
- Round twelve-b, the ghost regression (6c7001ce): e3376405's static
  paddingRect used Qt.rect(pad,pad,2p,2p) assuming width/height were
  totals; Qt's updateSourcePadding() defines them as PER-SIDE extras
  (left/top/right/bottom), and the shader's centerOffset scales with
  (x - width), so the asymmetric rect drew a smaller offset ghost of
  every applet inside itself. Caught by eye within minutes of the
  build going live. Fixed to Qt.rect(pad,pad,pad,pad); the test now
  pins all four sides equal. LESSON: MultiEffect verification needs a
  VISUAL pass, not just CPU numbers and green tests - and when Qt
  docs are vague, read the Qt source before shipping (the source also
  revealed autoPadding pads 256px per side around every applet, an
  extra reason the static rect is right). Storm numbers unchanged
  after the correction: 0.1% idle, shadows on.
- Round twelve, the storm KILLED (e3376405): three probe builds
  bisected it to MultiEffect's autoPaddingEnabled - it recomputes
  padding and re-dirties the effect every frame, so every
  shadow-carrying window rendered empty frames forever. ShadowedItem
  now uses a static paddingRect (blur + max offset per side; output
  identical). 18.2% idle CPU -> 0.1% with shadows fully on. LESSON:
  QSG_RENDER_TIMING is the sharp tool for who-renders (named both
  spinning windows and showed the frames were EMPTY - polish=0 sync=0
  render=0 - which pointed at a dirty-loop, not real work). RULE: no
  autoPaddingEnabled on MultiEffect anywhere in this tree; compute
  static padding (the new tst_shadoweditem contract enforces it for
  ShadowedItem). Startup and cold edit-open unchanged after (their
  costs are real work); the win is idle CPU, power, and scheduling
  headroom.
- Round eleven, the storm (docs only, evidence in the plan's TOP
  PRIORITY item): while measuring edit-open latency (cold 7.3s, warm
  0.5s), found the idle dock burning 18.2% CPU on the main thread -
  ~19,500 failing statx/s, flat, even with all docks dodge-hidden -
  and 1.2% with applet shadows disabled. The scenegraph renders every
  frame forever (idle gdb samples in QSGBatchRenderer), and each
  frame re-resolves two nonexistent theme colors files across a
  273-entry NixOS XDG_DATA_DIRS. The eternal-frame requester is in
  the applet-shadow path and is THE next thing to hunt - it taxes
  every latency number measured tonight. Full evidence chain and
  attack plan in the plan item. Practical field notes for that hunt:
  ptrace_scope=1 means gdb must be the parent (batch file with
  multiple continue/bt rounds works, SIGINT from outside); strace as
  parent slows the dock enough that dodge-reveal stops working, so
  measure idle-only under strace; QSG_VISUALIZE tints hidden layer
  surfaces black over the whole screen - useless and user-visible.
  The edit-open cold cost itself is chrome instantiation + Kirigami
  theme cascade (stack captured), but re-measure after the storm fix;
  pre-creation options and risks are filed in the latency item.
- Round ten (c622da1b): the ~40px zoom-dwell preview offset closed by
  DELETING d98bff98's resting-anchor machinery and restoring Qt5's
  live anchor (tooltipVisualParent). The resting anchor only ever
  existed because the dialog could not reposition after map; with the
  recompute-fresh Dialog the live anchor is strictly correct and 87
  lines of settle-delayer machinery went away. Dwell preview center
  matches the zoomed icon center exactly now. The whole popup saga
  (parking, interleave, dwell offset, rearrange modal) is CLOSED -
  one wayland root (mapped popups cannot be repositioned by
  QWindow-level calls) wearing four costumes. When a workaround's
  reason disappears, delete the workaround - do not refine it.
- Round nine (8f821310): 77aac4b4 confirmed with a real mouse.
  The rearrange hover-modal inconsistency then fell to the same class:
  ConfigOverlay's tooltip was a stock PlasmaCore.Dialog with a
  visualParent binding hopping between hovered applets while mapped -
  the window parked at the first hover's spot with only its width
  following (reproduced live at 2122 while hovering an applet at
  3290). One-line class swap to LatteCore.Dialog plus edge, comment at
  the site. When a mapped popup's anchor must move on wayland, it must
  be a Latte::Quick::Dialog - PlasmaCore.Dialog cannot do it. Grep for
  other PlasmaCore.Dialog uses with mutable visualParent if a similar
  'appears in the wrong place / does not appear' report shows up.
- Round eight, the previews bug's real ending (77aac4b4, dbe5a03b):
  re-reproduced by hand against d619ae08 with the same recipe. Full
  instrumentation of QML show() plus every C++ position push caught
  it: the dialog maps with the PREVIOUS task's content width, the base
  recenters on the resize, then libplasma's stale-position re-send
  reverts it - and the old stored-position counter-measure was armed
  only by the mainItem-resize hook, which fired for every intermediate
  stop of the sweep but not the final one. Ordering-dependent
  correctness, which is why three fixes passed synthetic retests and
  kept failing under my hand. 77aac4b4 removes the stored
  state entirely: recompute the anchored target fresh from current
  anchor + pending content size after every Move/Expose/Show and push
  it; any ordering self-heals next event. Clean-build verified, recipe
  plus bidirectional sweeps, idle-silent. Also fixed: the
  layershellmappingtest had been failing to COMPILE since the
  canvasInputRegion dockStrip change (dbe5a03b) - run ctest, not just
  the build, after C++ signature changes. Remaining preview polish:
  the ~40px zoom-dwell offset (separate filed item).
- Round seven, latency (37acf9ca): startup measured honestly (KWin
  window poller, no wrapper, no -d; the -d run matched within 50ms so
  -d timings are trustworthy). Restart was ~9.4s: ~4.1s launcher
  (nix develop 3.0s dominating) + 4.5s binary to first dock window,
  5.3s to all three. Launcher fixed: restart-staged sources a cached
  print-dev-env snapshot (build/_devshell.env, auto-refreshed on flake
  changes), warm path measured 0.57s. Binary phase attributed by a
  mid-stall gdb interrupt (ptrace_scope=1 on this host - attach is
  blocked, run gdb as the parent and SIGINT the dock, the repo's
  gdb-batch-cmds documents this): main thread parked in
  QQmlTypeLoader waits under PlasmaQuick's synchronous per-applet
  itemForApplet, three views built sequentially in one synchronizer
  callback (synchronizer.cpp:694). Filed lever: stagger view creation
  per event-loop cycle (earlier first dock, total unchanged) - NOT
  implemented, view creation order is Phase 8 territory. Corrected a
  wrong suspicion: restaging does NOT invalidate the QML disk cache
  (cmake --install preserves mtimes; 235/237 entries reused), so
  edit-mode first-open cost is in-engine type instantiation - warm-up,
  not qmlcachegen, is the candidate fix. Benign oddity explained:
  latte-dock windows 4/5 appear ~18.5s post-start in every run (lazy
  ToolTipDialog/KlipperPopup), not pointer hover.
- Round four, decisions and mapping (e70bccf7, e85e18d8, 98b7419e):
  parabolic zoom disabled for ALL of edit mode as a deliberate call
  (deliberate Qt5 deviation, comment at the site). The missing 'Applet
  Settings' menu entry got its architecture mapped: Qt5's
  ViewPart::ContextMenu was removed in the port (d3538eee), the canvas
  ContextMenuLayer replacement has correct composition but dead
  applet-under-cursor resolution (direct-children method lookup misses
  Plasma 6 wrappers; cross-window fallback; use-before-null-check at
  :229), and dock-window right-clicks reach the containment-only
  containmentactions plugin. Decided: configure entry belongs in
  the menu ALWAYS (matches Qt5/plasmashell). Also filed: Edit Dock
  wrong-view targeting (stale singleton chrome shows before retarget,
  observed in the 20:53 trace and twice by hand), edit-mode
  first-open latency (cold QML compile suspect), startup latency
  (measure without the gdb wrapper and -d before optimizing). Left
  dock rearrange remains dead pending the off-surface header item.
- Round three, the design-level one (3d714d63): plain edit mode
  blocked ALL widget interaction because the canvas owned its whole
  surface. Qt5/X11 stacked the edited dock above the canvas; wayland
  cannot restack same-layer surfaces, so the canvas input region now
  excludes the dock's absoluteGeometry permanently. Verified: full
  context menu on a task in plain edit mode, wheel tooltip confined to
  the blueprint margin. The reason this evening felt like whack-a-mole
  is now on record: five independent latent defects (persistence,
  focus race, transient-hide session kill, stale mask rect, canvas
  input ownership) stacked on the same user-visible surface; each fix
  was real and verified, the last one was architectural. Remaining in
  this area, filed with recipes: popup mispositioning during fast
  pointer movement (parabolic anchoring race), vertical-dock canvas
  header off-surface, outer-Button stretch mechanism, missing 'Applet
  Settings' context-menu entry (pre-existing).

## 2026-07-12 evening: skill library authored, tested, committed

- .claude/skills/ now holds seven skills (latte-architecture, latte-build-env,
  latte-conventions, latte-debugging, latte-fork-sync, latte-live-verification,
  latte-plasma6-defect-families). They are the distilled operating knowledge of
  this port; new sessions should lean on them instead of re-deriving.
- Tested before committing, four ways: (1) three independent accuracy audits
  covering roughly 240 objective claims (every cited path, symbol, line number,
  commit hash and quoted metric checked against the repo; zero wrong or stale
  claims, a handful of imprecisions found and fixed); (2) executable checks
  (qml-compile-gate 0 of 128 failures, all four cmake targets confirmed, the
  plasma-desktop curl diff and both fork range commands run verbatim);
  (3) a live smoke test of the whole latte-live-verification recipe (restart
  under the gdb wrapper, three docks mapped at layer=3 in dumpwins, D-Bus
  surface introspected, edit mode opened on first invoke, spectacle capture,
  clean stop; machine left dock-free as found); (4) six Sonnet-class usability
  probes run against realistic scenarios with only the skills as guidance -
  five clean passes, one wrong "stale binary" diagnosis that led to a new
  staleness-check paragraph in latte-build-env (plugin sources link into their
  own .so, never mtime-compare against build/bin/latte-dock; trust the no-op
  build's "no work to do").
- Environment change: fakepointer now lives at ~/.local/bin/fakepointer
  (was a transient prior-session scratch path); fakepointer.desktop Exec
  updated, sycoca rebuilt, injection verified working.
- Fork-sync pass is DUE: latte-dock-ng has ~10 unreviewed commits past
  59e04b8b7 (including real fixes: middle-click restore 613ddcc3b, duplicate
  parabolic zoom removal f0f65f4e3) and latte-dock-qt6 has 54 past 9003f33a
  (large test/refactor push). CLAUDE.md's "less active" note on qt6 is stale.
  Run the latte-fork-sync skill end to end next session.

## 2026-07-12 sweep (all committed, ad9b823f..1aa97b47)

- iconSize=78 startup hang FIXED (ad9b823f): the autosize shrink loop's
  '!== 16' exit stepped past its floor for any size not 16 mod 8, spinning
  forever once wayland's unsized-window start made the length limit
  unreachable. THE USER'S REAL LAYOUT STARTS AGAIN (verified in the
  throwaway config; --user-config is safe for it now).
- Applet private QML modules FIXED (4c9f3bc7): owning packages joined the
  flake's pinned LATTE_QML_MODULE_PATH; nine failing modules -> zero, the
  context menu survived (shadowing regression check), widget explorer
  previews all render.
- Comic Strip churn crash: NO LONGER REPRODUCES - the churn vector WAS the
  missing private module. CompactApplet hardening stays filed, not
  crash-backed (2edb1796).
- QQC2 'indicator' property shadowing FIXED (33fa17d7): indicator config
  checkboxes read AND wrote the control's own check-glyph object, silently
  dead since the QQC1->QQC2 port. Watch for this class anywhere a context
  property collides with a QQC2 control property name.
- Edit-chrome canvas-follow FIXED, two stacked causes (1607d022 surface
  remap in the LS helpers, c5bdc239 Containment::screenChanged resync).
- KDE_COLOR_SCHEME_PATH pinned per view window (a774ee55, ng's 9fe135422
  mechanism); Kirigami colorSet half deliberately deferred until a live
  divergence reproduction exists.
- Applet config sync FIXED AT THE ORIGIN (c3d15966): it had NEVER
  established for any applet (dead indexOfProperty probe on
  AppletQuickItem); now reads the CONSTANT configuration Q_PROPERTY off
  the Applet. org.kde.sync storms: gone. The handoff's duplicate-dock +
  add-widget crash NO LONGER REPRODUCES (driven end to end under gdb).
- Vendored backend: provenance stamps in plasmoid/plugin, plasma-desktop
  added as a sync-diff target in CLAUDE.md, Phase 12 outreach reframed for
  the maintained-continuation reality (docs/taskmanager-integration-research.md
  has the vendor-vs-integrate analysis; ng's narrowing is the counter-example
  and will not be repeated).

Still open from this sweep (all filed in the plan): duplicated-dock applet
ORDER retest on a rearranged layout (default-order layouts cannot exhibit
it - use my real layout, interactive), the at-the-desk settings
control walk (TaskMouseArea's 9 TaskAction values especially), folder-view
calling corona.isScreenUiReady (shim vs noise decision),
BindingsExternal.qml:281 localGeometry-of-null startup transient, and the
day-one leftovers (lock/unlock visibility, session shutdown pattern,
startup retry deadlock, wheel bypass list, context-menu .so naming,
RightButton re-assert).

Session tooling notes: 'CLEARED SCREEN' in the dock log is strut
bookkeeping, NOT an output removal - use a kernel drm hotplug logger
(udevadm monitor --kernel --subsystem-match=drm) for flap ground truth.
kde-inhibit --power --screenSaver held DPMS/autolock off for the whole
session (dies with the session, nothing to undo). The kglobalaccel
'show view settings' invoke races registration right after a restart -
invoke once, check for '#primaryconfigview#' in the log, invoke again if
absent; each further invoke CYCLES to the next view's settings.

Phase 8 day-one results (all commits on master, plan items ticked):
- The recurring crash family is dead: Qt6 MultiEffect does not auto-wrap
  plain Items as texture sources; three effect sites had invalid samplers
  since the port (73da8400), two more effects were dead and removed
  (df747ebf, e88af680), render loop defaults to basic for debuggability
  (c7200e3d). Regression caveat: re-adding the broken Comic Strip applet
  STILL crashes the churn recipe, and that crash (twice now) landed exactly
  on a 'CLEARED SCREEN :: DP-3' output removal - the portrait monitor
  flaps, and it MUST be eliminated as a variable before trusting further
  verdicts. CompactApplet churn hardening is filed and NOT implemented.
- Multi-monitor edit chrome fixed and hand-verified (d670c97a): canvas,
  settings window and widget explorer pin to the edited view's output.
  The secondary advanced-mode window is NOT yet covered.
- Debugging kit that made all of this work: LATTE_RUN_WRAPPER gdb batch
  script (catches every crash with full backtraces; KCrash self-destructs
  here), fakepointer move/click/rightclick, the KWin dumpwins scripting
  one-liner for true window geometries, WAYLAND_DEBUG=1 for protocol-level
  truth, and by-hand desk driving as the reproduction engine.

## What landed this session (all committed, all live-verified with screenshots)

Edit mode blueprint, done the Qt5-faithful way (the dock draws the grid in its
own window, since two wlr-layer surfaces cannot stack dock > grid > wallpaper):

- d68b8e8d feat(containment): draw the blueprint inside the containment, between
  the dock background and the layouts container, driven by root.editMode.
- d72ee0cd fix(containment): size the grid to latteView.editThickness at the
  screen edge. The dock window is taller than the edit area (parabolic zoom
  headroom: 384px window vs 146px editThickness here), so filling the window
  drew a huge grid far above the dock.
- 608a509e fix(settings): stop CanvasConfiguration.qml drawing its own grid.
  The canvas window stacks in front of the dock; its opaque copy hid every
  applet. Its imageTiler stays (geometry for the wheel MouseArea, opacity value
  for SettingsOverlay text contrast) but no longer renders.
- f5a5f44c fix(editmode): restore Qt5 editBackgroundOpacity semantics. The port
  had rewired the grid opacity to panelTransparency (the dock's persistent
  background setting) with an invented 0.5 floor, so at the theme default it was
  fully opaque and hid the white panel bar. Now: editBackgroundOpacity (default
  0.2), solid while rearranging widgets and without compositing, wheel steps it
  0.1 with no floor.

Wayland window previews (were blank, now show real thumbnails):

- c25cb3e1 fix(plasmoid): port previews to Plasma 6 kpipewire. The 5.26 rung
  kept the kpipewire 5 contract (item created enabled:false, C++ flips it);
  kpipewire 6 does not, so opacity stayed 0. Replaced the whole dead 5.24/5.25/
  5.26 version ladder with plasma-desktop 6's PipeWireThumbnail.qml shape.
  Also winId int -> var in ToolTipWindowMouseArea (wayland ids are QString
  UUIDs; same defect class as 5a77d2da).

Crash + infrastructure:

- 46dc83c5 fix(app): Factory::removeIndicatorRecords called removeAt(-1) for
  built-in indicators (never in the custom lists), a hard SIGSEGV on Qt6 (Qt5
  tolerated it). Fires when the install tree is restaged under a running dock.
  Three coredumps today traced here.
- f24b7d89 build: scripts/restart-staged.sh. Orphaned docks end up SIGSTOPped
  and ignore SIGTERM, so bare pkill left them alive and stacked instances (three
  docks were fighting over layer surfaces at one point). The script TERM+CONT,
  escalates to KILL, refuses to stack, launches via setsid with stdin closed.
- c49db4cc build: scripts/tools/fakepointer.c, wayland pointer injection for
  live verification (see below).
- fcedce40 docs: Qt5-faithful behavior agreement recorded in CLAUDE.md.

## 2026-07-15 evening: backlog flush (this session's record)

- Show Alternatives (56549d73): the entry was never missing - Qt5
  gates it on isUserConfiguring, so it shows only in edit mode; the
  real defect was appletalternativesui resolving through the package
  fallback to plasma-desktop's file, which imports org.kde.plasma.shell
  and cannot load outside plasmashell. Latte ships its own copy now.
  Full chain verified live (dialog opens filtered, Digital Clock
  replaced the Analog Clock in place and persisted). ng has the same
  latent break.
- Widget explorer GHNS entry (ab3caae1): ng's adopted QML routed 'Get
  New'-labelled clicks to an invokable this port never implemented -
  silent TypeError since 0aa7ffb6. Plain model.trigger() restored, ng's
  external-dialog detour machinery deleted (its reason - ng's system-Qt
  Kirigami break - does not exist on the pinned stack). KNS dialog
  verified rendering live. Known noise: one Kirigami OverlayDrawer
  TypeError from inside the KNS dialog, harmless.
- Quit-reason trail (ee0f1ba3) for the lock/unlock exit item: SIGINT
  now via KSignalHandler with a log line (the old raw std::signal
  lambda called qGuiApp->exit() in signal context; std::signal(SIGKILL)
  was uncatchable dead code), quitApplication and onAboutToQuit log
  themselves. SIGINT trail verified live on an unwrapped run. Synthetic
  lock/unlock repro NOT attempted (I was at the desk using the machine;
  locking it was not on). The next natural occurrence names its
  trigger: signal line = external kill / --replace; quitApplication
  line = settings/dbus/import; onAboutToQuit alone = the
  /MainApplication D-Bus quit() arm. NOTE: under the gdb wrapper SIGINT
  never reaches the process (gdb dumps stacks and exits UNcleanly), so
  a wrapped clean quit with no quitApplication line = the D-Bus arm.
- Add-panel crash: no reproduction (7 addView calls across Default
  Panel/Default Dock/Empty Panel on the throwaway, rapid-fire
  included); fa02b887's guard and breadcrumb have been armed through
  four days of heavy driving with zero firings. Core 3933991 is
  useless now (binary rebuilt many times since; the fresh-core
  analysis was already extracted into fa02b887's message). Stays a
  watch item: the next occurrence names the deleter in the log.
- Verification tooling lesson re-earned twice: context menus open at
  slightly different offsets per invocation - never click from a
  previous capture's coordinates; capture the menu you are about to
  click. One stale-coordinate click reopened the widget explorer
  instead of Edit Dock.
- Session end state: all three fixes committed and pushed, tree clean,
  real dock running --user-config under the gdb wrapper, throwaway
  layout restored from its pre-repro backup (the two test clocks and
  the digital-clock swap reverted with it).

## RESOLVED (landed 4411d724, 77aac4b4, c622da1b; saga closed by round ten) - Hover preview jitter: ROOT CAUSE CONFIRMED LIVE, fix proposed, awaiting go

Reproduced with fakepointer + temporary QML logging (instrumentation added,
driven, read, then removed; tree carries none of it). My symptom
(preview disappears and reappears in a different spot when nudging the cursor)
is the visible edge of a parabolic-zoom feedback loop that runs even with a
COMPLETELY STATIONARY pointer:

1. Hover a task, zoom animates, preview shows. While anything animates, the
   view receives a stream of MouseMove events at frame rate with an unchanged
   pointer position (~83 events/s measured; Qt6 frame-synchronous synthetic
   hover delivery, exact producer still to be pinned down in one rebuild).
2. app/view/parabolic.cpp onEvent maps each event's windowPos into the CURRENT
   parabolic item's coordinates and queues parabolicMove(item-local x,y) via
   Qt::QueuedConnection. The item is moving/scaling, so the item-local x keeps
   drifting (logged mx 76 -> 42 with the cursor pinned at one spot).
3. ParabolicEventsArea.qml onParabolicMove feeds that drifting x into
   calculateParabolicScales, which re-centers the zoom, which shifts the
   layout under the cursor, which produces more synthetic moves. The system
   oscillates instead of converging (one storm self-sustained for 33s; another
   walked the hover across tasks 4 -> 3 -> 2 -> 0 with zero pointer input,
   KWin raising each task's window via highlightWindows on the way).
4. Preview symptom: each boundary crossing fires exited/entered, show()
   re-anchors the popup to the new task (content + size + anchor all change);
   when the churn drops the cursor into a gap for >300ms, hidePreviewWinTimer
   fires forcePreviewsHiding and the preview vanishes, then re-hover shows it
   at the new anchor. That is exactly "disappears and reappears elsewhere".
5. Dialog layer makes it worse but is NOT the driver: with the Qt.ToolTip flag,
   every parabolic frame emits anchoredTooltipPositionChanged ->
   Dialog::updateGeometry() -> setPosition(popupPosition(visualParent, size())),
   racing PlasmaQuick's own syncToMainItemSize repositioning. Logged the window
   position flapping between two values (e.g. x=959 computed from the stale
   QWindow::size() vs x=681 from the new mainItem size) every ~10ms while the
   on-screen popup stayed at the first mapped position.

IMPLEMENTED (go given), iterated live at the desk across four rounds,
currently UNCOMMITTED in the working tree awaiting their final verdict:

1. Parabolic::onEvent drops MouseMove-derived updates whose window position
   has not changed (kills the stationary feedback loop; confirmed live: "it no
   longer jitters"). Honest caveat: the guard's drop path was never observed
   firing post-fix; the original storm conditions did not fully reproduce.
2. Dialog::updateGeometry() positions with the size the dialog is about to
   have (mainItem + frame margins) instead of the stale QWindow::size().
3. Dialog anchor-follow is pinned: reposition once per (anchor, size) change
   instead of on every parabolic wiggle of the anchor item; plus one explicit
   placement on visualParent change (covers same-sized content switches) and
   an explicit reposition whenever the mainItem resizes (base class proved
   unreliable there on wayland: a placement computed for the previous content
   width survived a resize, popup sat half a width off).
4. The previews dialog opts out of the applets-layout clamp
   (respectsAppletsLayoutGeometry: false): the clamp shoved wide previews for
   tasks near the dock ends sideways off their icon.
5. Previews anchor to a RESTING midpoint (my own suggestion): every task
   carries an invisible anchor whose center is sampled while the row is fully
   at rest and frozen during zoom AND during the restore animation
   (3*animationTime + margin; unfreezing right when currentParabolicItem
   clears sampled mid-restore midpoints, reproduced by hand with quick
   hover-away-and-back). TaskItem.qml _previewsVisualParent.

6. THE ACTUAL WAYLAND ROOT CAUSE of every "popup stuck at the previous
   task's spot" symptom, found with WAYLAND_DEBUG=1: QWindow::setPosition()
   and setGeometry() position parts are NO-OPs for a visible wayland window
   (client position stays frozen at the show-time value forever), AND the
   base PlasmaQuick::Dialog re-sends that frozen QWindow::position() to the
   plasmashell surface on every QEvent::Move and on expose
   (libplasma dialog.cpp event handler + updateVisibility()), overriding any
   correct placement right after it is made. Fix: updateGeometry() sends the
   anchored position through PlasmaShellWaylandIntegration::setPosition()
   (public PlasmaQuick API, works on visible surfaces; required generating
   qwayland-plasma-shell.h in declarativeimports/core the way app/ already
   generates other protocols), remembers it, and Dialog::event() re-asserts
   it AFTER the base class handles Move/Expose so ours is always the last
   request the compositor sees; cleared on Hide so show-time placement stays
   the base class's. Protocol-log verified: every stale re-send is now
   followed by the anchored position, last word ours.

The PORTING_PLAN Phase 7 "always-visible MouseArea, synchronous parabolic
calc" note (latte-dock-ng 0deca9e18) remains the structural long-term shape.
Confirmed by hand: no jitter within an icon; the final cross-icon slide test on
the (now multi-monitor) setup is still awaiting their verdict, then the
whole set can be committed in reviewable pieces.

## Multi-monitor: edit-mode windows use stale/wrong screen geometry (NEW, Phase 8)

I added a second monitor (portrait 1440x2560 at (0,0), landscape 2560x1440
at (1440,447)) and entered edit mode on the portrait dock. The settings
window landed at (2031,7) sized 529x1287: exactly the values computed for
the OLD single-screen 2560x1440 layout (x = 2560-529, y = 1440-146-1287),
i.e. floating in dead space above the landscape screen. Edit-mode window
geometry (PrimaryConfigView::syncGeometry inputs: screenGeometry,
m_availableScreenGeometry, canvasGeometry) must re-derive from the edited
view's CURRENT screen after output hotplug, and the layer-surface output
assignment (QWindow::setScreen at SubConfigView construction) must be
re-checked against where margins are computed (applyFixedGeometry margins
are relative to the passed screenGeometry and only correct when the surface
sits on that same output). This is the first concrete Phase 8 multi-screen
work item; do it WITH the dual-monitor setup live, per the plan's warning
that both reference forks fought multi-screen repeatedly.

## RESOLVED (no longer reproduces after c3d15966, driven end to end under gdb) - Duplicate-dock + add-widget crash (NEW, open, no core)

I duplicated the dock, then added a widget to it; latte crashed and
KCrash itself crashed while handling it (log: 'KCrash: appFilePath points
to nullptr!', crashRecursionCounter = 2), so there is NO core. The log
fingerprint: at duplication time a storm of
'org.kde.sync ... configuration syncing was not established, configuration
object was missing!' (app/view/containmentinterface.cpp:1034) for every
applet id of the clone; the same failure again for the newly added
widget's id right before the crash, then CompactApplet.qml binding errors
and death. Points straight at the cloned-view config-sync machinery, same
territory as the Phase 8 'cloned-view applet-order sync' item. To get a
backtrace next time: reproduce under
LATTE_RUN_WRAPPER='gdb -batch -ex run -ex bt --args' via restart-staged.sh.

## Dock exits ~20s after screen lock/unlock cycles (open; quit-reason trail landed ee0f1ba3, next occurrence names its trigger)

Twice observed: a screen remove/re-add cycle (lock or dpms), handled cleanly
by the positioner, then ~22s later 'Latte Corona - unload: containments' with
NO logged trigger, i.e. a clean app quit. Only three programmatic quit paths
exist (dbus quitApplication, settings dialog, importFullConfiguration) and
none tie to screens, so suspect an external SIGTERM or dbus call. Next step:
log the quit reason (KCrash/KSignalHandler or a SIGTERM handler qWarning) and
check what else runs at unlock. Before 72deaa8c these exits looked like
crashes because teardown itself segfaulted (~Theme null schemes).

## Add-panel crash (open watch item; 2026-07-15: no repro in 7 tries, core stale, fa02b887 breadcrumbs armed and silent since 07-11)

'Add default panel' crashed: Storage::importLayoutFile iterated
corona()->importLayout() results and one containment pointer was ALREADY
FREED (first bytes 0x1) when isLatteContainment() copied its metadata
(kcoreaddons KPluginMetaData copy ctor SEGV). Something destroys a containment
synchronously during template import. NOT deterministic: a headless
reproduction (busctl org.kde.LatteDock addView us 0 "Default Panel") imported
cleanly. Core: coredumpctl 3933991 (02:06). The crashed attempt left a
half-imported left-edge panel in the layout (cleaned). Candidate suspects to
instrument next: GenericLayout::addContainment side effects during
importLayout, subcontainment (systemtray) handling, and the icontasks
package-load failure ('Could not find required file mainscript') seen during
imports.

## Edit mode layout (my request, COMMITTED, live-verified)

The request: rearrange button to the LEFT, settings panel to the RIGHT, the
Maximum Length ruler aligned with the top of the blueprint. Root causes found
with a KWin-script window dump (see verification loop below):

- The canvas config window was pushed 88px off the screen edge by the dock's
  own exclusive zone (measured: canvas at y=1206 instead of 1294 for
  editThickness=146), so ruler + button floated above the blueprint.
  Layer-shell zone 0 means "respect other surfaces' struts"; an overlay must
  opt out with zone -1.
- The settings window used LayerShell::setUnanchored, so the compositor
  centered it mid-screen (measured at (1015,380)), nowhere near the dock.

Landed as:
- ec5d2316 fix(wm): applyCanvasPlacement and applyFixedGeometry now
  setExclusiveZone(-1) so both land exactly where computed.
- 0d92f007 feat(settings): settings window pinned right via
  applyFixedGeometry (wayland setUnanchored had it compositor-centered
  mid-screen); horizontal docks use the right-end placement in both modes.
- 374461cb feat(shell): ruler thicknessMargin 0 for horizontal docks (flush
  with the blueprint inner edge), rearrange button anchored left just below
  (bottom dock) / above (top dock) the ruler. Vertical docks unchanged.
- 5e873329 fix(settings): completes the cf05d856 STUB. In configure-applets
  mode the whole canvas was click-through, so unclicking the rearrange
  toggle fell through to the dock, the settings window lost focus and edit
  mode closed entirely (caught live). updateInputRegion now reads the
  QML-published rearrangeToggleRect and keeps that rect interactive.

All verified live with KWin-script geometry dumps + the screenshot loop:
canvas at (0,1294) 2560x146 == the blueprint band, settings at (2031,7)
flush right with its bottom on the blueprint top, ruler line is the grid's
top boundary, toggle at the left under it, and the rearrange toggle
round-trip keeps edit mode open (confirmed working by hand).

One quirk seen only with fakepointer, not reproduced with a real mouse:
the click that ENTERS rearrange mode shrinks the input mask mid-click, and
the synthetic release seemed to get lost once, leaving the Button stuck
pressed so the next synthetic click was a no-op. If a real-mouse report of
"first unclick needs two clicks" ever comes in, start there.

## RESOLVED (fixed ad9b823f, plan item ticked; --user-config safe again) - iconSize startup hang (NEW, root cause bisected, code fix pending)

The port hangs at startup at 100% CPU (main thread, event loop starved, dbus
times out) when the layout contains iconSize=78. Bisected live: my layout
in a throwaway config hangs; same layout minus iconSize=78 starts; iconSize=64
starts; 78 alone re-adds the hang. gdb backtrace (child run, since yama
blocks attach): a QML bound-signal handler triggered from the
Q_EMIT visibilityChanged() in View::setContainment (view.cpp:180 area) never
returns, i.e. a binding/handler cascade that never settles. Suspect surface:
viewTypeInQuestion / behaveAsPlasmaPanel / background.isGreaterThanItemThickness
flip-flopping once the icon size crosses a threshold between 64 and 78. The
log always freezes on the same line: 'Updating visibility mode ::
AlwaysVisible' right before indicator QML would load.

IMPORTANT: my REAL layout (~/.config/latte/"My Layout.layout.latte")
acquired iconSize=78 at 22:20:35 on 07-10, written when a staged dock was
killed while edit mode was open. Until the loop is fixed in code (preferred;
CLAUDE.md failure rules: fix the origin, do not clamp), --user-config runs
will hang. Do not silently edit my config; ask, or fix the code first.

## The live-verification loop (the big infrastructure win, use it every time)

Do NOT claim a UI change works without driving it and reading pixels. Yesterday's
"blank screenshot, tool is broken" conclusion was wrong; the tooling works.

- Restart the dock ONLY via scripts/restart-staged.sh --user-config -d (never
  bare pkill + relaunch, per the SIGSTOP trap above). It restages QML every run.
- Enter edit mode: busctl --user call org.kde.kglobalaccel /component/lattedock
  org.kde.kglobalaccel.Component invokeShortcut s "show view settings"
- Move/click the pointer without a human: scripts/tools/fakepointer.c
  (move|click <x> <y>) via org_kde_kwin_fake_input. KWin gates that interface
  per client, so it needs a desktop file in ~/.local/share/applications with a
  matching absolute Exec and X-KDE-Wayland-Interfaces=org_kde_kwin_fake_input,
  then kbuildsycoca6. A built binary + fakepointer.desktop are already set up.
- Capture: spectacle -b -n [-p for cursor] -o <file>, then Read the png.
- A ~14KB all-white png means the screen is LOCKED (loginctl show-session
  <wayland session> -p LockedHint) or the display slept, NOT a spectacle flake.
  Standing decision: loginctl unlock-session is fine for verification; re-lock
  with loginctl lock-session when done. Run kscreen-doctor --dpms on before
  capturing. The session auto-locks on a timer, so expect to unlock repeatedly.
- Confirm a capture landed inside edit mode by checking the png mtime against the
  dock log's "#primaryconfigview#" init line. The settings window closes on focus
  loss and edit mode dies within seconds of typing elsewhere, so capture
  ~2s after the invoke.
- Screenshots and the fakepointer binary go under $CLAUDE_JOB_DIR/tmp, not /tmp.

## Known traps

- Never rebuild (nix develop -c cmake --build build --target latte-dock) while a
  dock runs from build/bin; the running dock then executes a deleted binary and
  crashes confusingly. Stop it first.
- MultiLayered.qml already has a Behavior on opacity at its root; a second one
  logs "Attempting to set another interceptor" and is ignored. Those and the
  ~56 KWindowShadow warnings are pre-existing noise; do not chase them.
- QML/package changes need no rebuild (restart-staged.sh restages). Only C++
  changes (app/, declarativeimports/) need the cmake build.
- Validate QML with nix develop -c scripts/qml-compile-gate.sh before launching.

## Backlog (root-caused where noted, not started)

1. Edit mode step 2 (grow dock to editThickness) is filed in PORTING_PLAN Phase
   7 but may be largely moot: the mask already exposes the full window in edit
   mode and the window is taller than editThickness. Evaluate what actually
   remains (struts, input region, slide-in animation) before wiring anything.
2. UX: when the rearrange toggle is centered, open the settings window to the
   right instead of also centered (app/view/settings/primaryconfigview.cpp).
3. Dock invisible after a screen lock/unlock cycle, and a dock started under a
   locked screen stalls for minutes before "Adding View". Filed in PORTING_PLAN
   Phase 8. Not root-caused.
4. RESOLVED by 4c9f3bc7 (see the 2026-07-12 sweep): owning packages joined the
   flake's pinned LATTE_QML_MODULE_PATH; nine failing modules went to zero with
   the context menu intact. Kept for the warning's sake: a broad append of the
   system Qt6 QML tree fixed them once but shadowed org.kde.taskmanager and
   replaced the dock context menu with the stock one. Same-pin package roots
   only, never a foreign shared root.
5. plasma_applet_dict also needs QtWebEngine; recheck after 4.
