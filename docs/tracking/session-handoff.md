# Session handoff

Rolling handoff for the next session to pick up without re-deriving context.
Last updated 2026-07-22.

## 2026-07-22: PR #110 observability merged

PR #110 separates D76 (global applet-configure readback marked unrelated
docks active) from C0 (the atomic dock-system observability snapshot). Commit
`c11c77ed2` derives the effective configure state from each dock's edit mode and
the one global rearrange toggle. Commit `5591b66d7` adds the schema-versioned
`dockSystemData()` query and its deterministic value-layer tests. The public
contract and this traceability update remain a separate documentation commit.

The initial independent review returned MERGE AFTER FIXES. It found that
runtime tokens were assigned before sorting QHash-derived views, the lifetime
test did not force address reuse, GUI-thread affinity was implicit, live
collection did not route through tested seams, stacking keys were not pinned,
D76 and C0 shared one commit, public identity and coordinate-space semantics
were incomplete, and enum/relationship mappings lacked exhaustive coverage.

The rewrite sorts persistent containment ids before the first identity lookup,
uses a generation-checked QPointer registry with synchronous GUI-thread
retirement, reconstructs QObjects at an exact reused address in the unit test,
routes collection through pure ordering and constexpr lineage seams, refuses
malformed lineage loudly, pins the production routes with controlled-mutation
source guards, and defines stacking as an exact unavailable capability rather
than inventing an order. Geometry coordinate spaces, logical-pixel units,
nullable controller states, and the current screen-group duplication behavior
are explicit in the public reference.

A fresh Nix development build completed `lattedock-core`, `latte-dock`,
`dbusreportstest`, and `sourceguardtest`; both focused tests passed. The first
compile exposed two observational View getters that lacked const qualification;
their declarations and definitions are now const-correct. The canonical gate
then exited 0 at exact head
`cf1a85acad6342bb4c59bca0e86b41b3e4d00281`: 100/100 CTest entries, the
100-entry/32-header coverage ratchet, the 234-file and 5,832-warning qmllint
baseline, all 13 scene probes, 3/3 nested ASan/UBSan recipes, and the complete
output-matrix fixture passed.

The required cold follow-up review returned MERGE AFTER FIXES with five major
findings and no CRITICAL finding. D90 (malformed clone lineage yielded plausible
partial snapshots) is fixed by `30ecf6bfc` and pinned by `e853e196a`: one
whole-graph pass now rejects missing or standalone targets, duplicate ids,
clone chains, and cycles before the first runtime identity lookup, and the
D-Bus wrapper returns no partial JSON. D85 (runtime identity tests missed
retirement timing and thread affinity) is fixed by `85e59ee07`: a count-only
oracle proves synchronous retirement before address reuse, worker-thread cases
drive both foreign object and foreign caller rejection, and controlled source
mutations pin the direct connection and full affinity predicate. D86
(dock-system schema tests left most field types unchecked) is fixed by
`f9c5af8df`, which asserts every wire type and nullable state. D92
(const-touched View files omitted current copyright attribution) is fixed by
`cd478bb06`. D91 (C0 review defects lacked flat-registry and checklist
traceability) is fixed by this record, the Phase 10 checklist, and the flat
defect entries.

The `cf1a85aca` gate stamp was invalidated by these code corrections. The final
canonical gate exited 0 at exact pre-merge head
`11bd5f5dcfaf43bcc42f5eea5153bf2f42213448`; GitHub then rebased the reviewed
commits onto `main`, ending at `4fa619870`. Under the
review-severity rule, a second review with only major findings ends the review
sequence after those findings are fixed; only a CRITICAL correction requires
another cold review.

## 2026-07-21: D81 and D82 prerequisite corrections merged

PR #108 merged the standalone prerequisites discovered while gating C0 (the
atomic dock-system observability snapshot): D81 (installed-package audit
crossed its isolated package-root boundary and saw unrelated ancestor markers)
and D82 (TaskItem Connections syntax exceeded the curated Qt 6 lint ratchet).
Exact post-rebase `origin/main` head is
`06ba567fcb9bc48c36679a321563eaa5f22f11ae`.

The initial independent review returned MERGE AFTER FIXES with a major
test-meaning finding. The D81 live-root fixture invoked `--root /` from below
`/tmp`, so the production host-root ancestry walk correctly saw an unrelated
`/tmp/.git` and invalidated the claimed live-root evidence. Commit `29322fb93`
makes the fixture environment explicit: it defaults to `XDG_RUNTIME_DIR` or
`/var/tmp`, requires absolute writable marker-free ancestry, and leaves the
production root boundary unchanged. The earlier `bd620c89b` provider fix still
stops only isolated-root traversal at the package boundary.

The required second cold review returned MERGE AFTER FIXES with one major
test-meaning finding: the fixture preflight stopped before checking `/`, while
the production live-root traversal checks `/` before stopping. Commit
`06da33ae0` makes the shared preflight inspect every current ancestor including
the host root. Its predicate seam drives a root-only marker refusal without
creating a real `/.git`, `/CMakeLists.txt`, `/CMakeCache.txt`, or `/CMakeFiles`.

The committed fixture was driven by
`nix develop --command tests/installed-package-gate-selftest.sh`. It exited 0
with all 91 focused controls, including `live-root fixture preflight rejects a
source/build marker at host root`, `isolated package provenance stops at the
package root`, and `live roots retain host-absolute symlink semantics`. The
canonical fast gate then exited 0 and stamped exact executable head
`06da33ae0a71e7505b9800662956afbbbfc7110e`: 100/100 CTest entries passed, the
100-entry/32-header coverage ratchet passed, qmllint matched 5,832 curated
warnings across 234 files, all 13 scene probes passed, and the package matrix
passed. ASan e2e was intentionally skipped by `LATTE_GATE_FAST=1` and remains a
merge-time gate.

No additional cold review was required. The root-omission finding was major but
not critical, and the severity rule defines this completed second review as the
single follow-up rather than the start of another review loop. The rewritten
implementation commits are `7148a54d8`, `fcb71e8b4`, and `ff732466e` for D81,
and `728d69a62` for D82. The companion record commits are `0b1a85262`,
`3a5a28c74`, `bb4f0595a`, and `06ba567fc`.

## 2026-07-22: SC-O1 settings-control registry merged

PR #105 merged SC-O1 (the read-only settings-control D-Bus registry). Exact
`origin/main` head is `01b462b70208d9cb84c7bfe589247f3f9f3b2fa0`. The final
rewritten commits are:

- `288b273cdd44de69dcce12fa76768639473cb3c3` defines the value-only C++20
  records, aggregate validation, deterministic ordering, compact JSON
  serialization, and paired sanitized tests.
- `d8558ad7d41354c36bf7f53195f1878ab9fd7bb5` adds the Corona-owned runtime
  registry, read-only per-view D-Bus method, generated adaptor path, fixture
  vertical slice, lifecycle cleanup, and controlled-mutation source guard.
- `522ffa4031737f64d53c633d4baab9d25b939524` defines the public query contract,
  design, usage recipe, and initial completion tracking.
- `523c6f4687c2849e872c3ed250f656145c9263c8` fixes the initial review findings:
  wire-level popup locator uniqueness, descriptor thread affinity, owned
  lifecycle connections, popup routing cleanup and ancestry, fail-closed load
  generations, and missing Corona copyright lines.
- `3f7ea99796d7684e38379fb782ede10addfe4f19` bounds reviews and gates to
  invalidated engineering risks.
- `c51a125880137e46710f289bf964522c96046923` records D65-D70 from the initial
  review and its focused correction evidence.
- `036df9618b67fe01be6f26745cdb688c07482486` classifies review follow-up by
  finding severity and requires one second review after major findings.
- `015200981da01a22b82ffa95d9b946c25788714c` fixes the second review findings:
  cross-scope tombstones, migration-safe `Qt::AutoConnection` cleanup,
  JSON-safe integer locators, count-only cleanup diagnostics, and the associated
  stale-generation contracts.
- `a5b086d29382b5e5698979a8f66e22919350acc3` records D71-D75, the final query
  contract refinements, and completion of the required second review.
- `01b462b70208d9cb84c7bfe589247f3f9f3b2fa0` requires independent rereview of
  every CRITICAL fix and is the final merged main head.

The initial independent review found major ownership, lifecycle, invariant,
test-meaning, and traceability issues, so the severity rule required one second
independent review after correction. That second review returned MERGE AFTER
FIXES with five findings, all fixed by `015200981` and recorded by `a5b086d29`.
No finding in either review was classified CRITICAL, so the CRITICAL-fix
rereview exception did not require a third review.

The public method is
`viewSettingsControlsData(u containmentId) -> s`. Unknown views warn and return
`[]`; known views with no controls return `[]` quietly. No production QML page
or control registration and no runtime-effect readback expansion landed in
SC-O1. Setters, filters, production registration, execution, and additional
readbacks remain in dependent component, page, and oracle units. The test
fixture remains a dedicated QRC/QML tree outside SC-F2 (the source-to-ledger
coverage gate) production roots and loads in a real QQuickWindow/QQmlEngine
through the production registry API.

The canonical `scripts/gate-all.sh` run exited 0 and stamped exact pre-rebase
head `8c3191d914e90dd18ca3081f822f756616017af3`. That hash is retained only as
historical exact full-gate stamp evidence, not implementation traceability. Key
results were 100/100 CTest entries, 100 coverage-ratchet entries with 32 paired
unit headers, the 130-file QML compile gate, the 234-file and 5,832-warning
qmllint baseline, all 13 scene probes, and the full ASan e2e and matrix-fixture
gates. The pre-rebase and merged heads have the same tree,
`754304b009cb545203c3c8163168d8dcbdf6b875`, so the merged tree is the final
gated tree.

SC-O1 is complete and checked in the settings completion plan. The next approved
work is SC-D1 (the pointer and keyboard control drivers), which remains
unchecked. SC-A5 (the move-current-view-to-another-layout action) remains
approval-required, unapproved, and unchecked. No adjacent work was completed.

## 2026-07-21: D77 dock identity isolation corrected after initial review

D77 (dock duplication retains clone lineage and edit ownership) is fixed on
`fix/dock-identity-isolation` in draft PR #109. The implementation remains
limited to the first identity and lifecycle slice in
`docs/tracking/DOCK_IDENTITY_HARDENING.md`; placement normalization and same-edge
stacking remain separate open slices.

The rebased branch commits establish relationship capability policy
(`aad2f524f`), serialize settings-chrome ownership and linked-member edit
targeting (`5de3f98d3`), retire replica membership during destruction
(`bf6602737`), require exact Wayland window titles (`b72c6147c`), count
ignored-window owners (`103147995` plus caller contract `eb5b6d47b`), replace
stale output geometry subscriptions (`d819fbb91`), and preserve persistent menu
identity across runtime view recreation (`8082a504f`). The investigation and
ownership model began at `a92d3b454`.

The first independent review returned MERGE AFTER FIXES with one major finding:
entering B and exiting B before the 400 ms retarget expired left the pending
request alive because the shared chrome still had A as its parent. The review
also required direct action-boundary guards, missing copyright lines, and a
literal commit-message newline correction. Corrections cancel pending ownership
by target (`8828f1f2b`), apply the relationship policy at Corona and View
boundaries (`d3d0a170e`), and add the missing copyright attribution
(`f63e555e5`). The branch history was rewritten to repair the commit message.
Because the review found an ownership/lifecycle issue, one second independent
cold review is mandatory after the corrected branch is pushed.

The clarified Duplicate Dock contract then exposed a second confirmed gap. The
captured template copied `screensGroup=AllScreensGroup` from an ordinary source,
so the new original immediately created a persisted replica ensemble and
installed `ClonedView` synchronization. A linked-member source separately
carried legacy `isClonedFrom`. Refusing that source did not satisfy the operation
contract. `e2f8bd1d6` makes Duplicate Dock a relation-breaking snapshot from
either role: both relationship fields are normalized before import, the action
stays visible on linked-member menus, and export, move, and remove remain owned
by the original. The final cold review found that the layouts dialog had a
second Duplicate implementation which bypassed that normalization. Commit
`170c827ee` moves relationship breaking into the const
`Data::View::toIndependentSnapshot()` transformation and requires both entry
paths to call it. Existing persisted linked layouts are unchanged. Future Create
Linked Dock… is documented but not implemented.

Focused sanitizer-backed action-policy and retarget-generation tests pass. The
production source contract passes, and `latte-dock` plus the containment-actions
plugin compile. The nested edit cancellation recipe (`a8bbe1b7e`) completed the
B enter/exit driver in 178 ms, kept both containments out of edit mode through
the old timeout, and passed a later B round trip. The dual-output recipe in
`e2f8bd1d6` made All Screens original 1 and linked replica 12 each create exactly
one independent dock (13 and 14), found disjoint containment and applet IDs,
kept 1 -> 12 linked, propagated a visibility-mode change only inside 1 -> 12,
and preserved all four identities across restart. The branch now sits on C0
(the atomic dock-system observability snapshot), whose runtime tokens and
fail-closed relationship graph remain the diagnostic authority. No live desktop
run, placement normalization, or same-edge stack coordinator was added.

The first rebased canonical gate passed 103 of 104 CTest entries and stopped at
D93 (Duplicate submenu change left a stale settings-inventory identity). The
relationship-aware submenu had removed an unused QAction binding around its
conditional separator, while the exact source-site ledger retained the old
statement identity. The one structural exemption now follows the scanner's
direct `m_addViewMenu` receiver identity. No inventory rule or coverage scope
was weakened.

The second canonical run then passed all 104 CTest entries and stopped at D94
(dock identity tests were absent from the coverage ratchet). The branch had
registered four new targets without adding them to the exact target ledger.
Commit `5efe665c2` adds all four in sorted order; the focused ratchet passes with
104 CTest entries and 35 paired unit headers. The third canonical run passed at
`81c15c789` with all 104 CTest entries, 104 coverage targets and 35 paired unit
headers, the 234-file QML ratchet, 13 scene probes, three sanitizer recipes, and
the deterministic output matrix.

The mandatory cold review of that exact head returned MERGE AFTER FIXES. D95
(layouts-dialog Duplicate preserves linked relationship state) was the major
finding: the distinct settings-dialog path copied `Data::View` without severing
lineage. Commit `170c827ee` centralizes the const transformation, pins its value
semantics, and checks both callers. D96 (Duplicate settings inventory still
claims linked exclusion) corrected the stale semantic row in `a009f8875`.
`datatypestest`, `dockidentitycontracttest`, and `settingsinventorytest` pass on
the correction. The canonical gate also passes at `5f616abde` with 104/104
CTest entries, both ratchets, 13 scene probes, three sanitizer recipes, and the
output matrix. The mandated second cold review remains required before merge.

The baseline nested run also confirmed D83 (removed duplicate containment
survives the undo window in persistent layout state), which is not fixed by this
branch. Independent duplicate containment 12 had `IsClonedFrom: -1` and logged
runtime destruction at 21:31:13, but its persistent group remained after the
120-second poll. D83 stays OPEN pending instrumentation at the persistent
removal and layout-sync boundary.

## 2026-07-21: SC-F2 source-coverage gate merged

PR #103 merged SC-F2 (the source-to-ledger coverage gate). The mandatory final
remote rereview returned MERGE, and exact `origin/main` head is
`31f9bb3565f574c334124050bcd74d532a310fcc`. The final rewritten commits are:

- `7eda16ca0f9389414e89014980b4958074e66b47` adds the syntax-aware QML/C++
  scanner and its sanitized contracts.
- `6089fdea92fd8c574d1106ea7a9d943553881922` migrates the evidence ledger to
  schema 2 and enforces independent bidirectional coverage, dynamic QML action
  coverage, broader C++ grammar, canonical source ownership, and the schema
  refusal matrix.
- `14ecdf9dda6a618969af2491628be022f730ca24` records the coverage contract.
- `31f9bb3565f574c334124050bcd74d532a310fcc` records the post-gate handoff and
  is the final main head.

SC-F2 is complete and checked in the completion plan. The next approved work is
SC-O1 (the read-only settings-control D-Bus registry). This completion does not
approve SC-A5 (the move-current-view-to-another-layout action) or any adjacent
unchecked work.

The fixed source universe contains 58 files: 237 QML objects, 357 QML members,
57 model elements, 57 semantic QML calls, 16 C++ constructions, and 10 C++
action calls, for 734 candidates total. Mappings cover 271 provisional
affordances and 25 explicit exemptions through 16 exact representatives and
1,259 coverage relations. The 25th exemption is the shipped public `SpinBox`,
which has no live per-view settings instantiation. Source lines are diagnostics
only. Dynamic menu helper declarations are not calls; `Component.onCompleted`
and `Component.onDestruction` are candidates on every QML object; and C++ lambda
ancestry survives mutable/noexcept/attribute/trailing-return forms. Every
identity also requires coverage in its declared source file. The final
correction counts only accepted site and representative links and pins all
1,259 relations. Standalone `newSeparator` calls are statement-local creation
boundaries with a following-creation digest for stable disambiguation; the
factory construction and all separator calls map to the structural exemption.

The focused correction gates passed in
`/tmp/opencode/latte-sc-f2-rewrite-build`: 20 scanner checks ran under
ASan+UBSan, and 74 inventory checks passed including every refusal mutation.
The 97-entry/31-header coverage ratchet, the 130-file QML compile gate, the
234-file qmllint baseline, fixed JSON count checks, and implementation diff
checks also passed. The full canonical `scripts/gate-all.sh` run exited 0 and
stamped exact pre-rebase branch head
`f62001a230e370a226fc9a9f008d1dc8423bf28b` before the branch push. That hash is
retained only as historical gate evidence, not implementation traceability; the
final implementation hashes are listed above. The post-gate change was
docs-only, so no implementation or test contract changed after the stamp.

## 2026-07-21: SC-T5 exact-once runtime acceptance merged

PR #101 merged SC-T5 (the permanent runtime-effect acceptance for D29,
task-icon middle click appears to execute left-click behavior) into
`origin/main` at `91b24b694`. Final runtime-test commit `382268a92` supplies the
complete nested-only `023-task-middle-click-runtime.sh` recipe, final D64
(distro-gate fakepointer build omits the xkbcommon link dependency) record
commit `611824a68` keeps the separate CI defect open, and `91b24b694` carries
the reviewed evidence and tracking. The disposable fixture is a compiled Qt
Widgets executable that sets desktop-file name `org.kde.latte.sc-t5` before
creating its Wayland surface. KWin reports that exact resource class, while the
task model reports
`appId=org.kde.latte.sc-t5.desktop` and launcher URL
`applications:org.kde.latte.sc-t5.desktop`. No product behavior or D-Bus
surface changed. SC-T5 is complete. D29 remains ACCEPTED as Qt5-faithful
configuration-scope behavior.

The recipe seeds one pure launcher with `middleClickAction=2` (`NewInstance`)
and no fixture window. Exactly one status-0 fakepointer middle click produced
SC-T3 (the D29 narrow middle-click dispatch readback) JSON
`{"configuredAction":"newInstance","dispatchedOperation":"requestActivate","rowIdentity":"applications:org.kde.latte.sc-t5.desktop","rowKind":"launcher","sequence":1}`.
Independent KWin state changed from no matching window to one active
`org.kde.latte.sc-t5` window, while `viewTasksData` changed the stable identity
from one pure launcher to one active, ungrouped, unminimized window row. The
fixture process record matched its live `/proc/<pid>/stat` start time and
`/proc/<pid>/exe` before counting.

PR #101's initial independent review returned MERGE AFTER FIXES because phase
two could locate a single row, settle the pointer, and then accept a changed or
already grouped target before input. The merged target lookup requires exactly
one fixture KWin window, one validated PID/start-time/executable tuple, and one
active, unminimized, ungrouped task row with `childCount=0`. The same complete
state is queried again immediately after pointer settlement and captured before
the single click. A grouped or extra-process state cannot satisfy the
precondition. Independent rereview accepted the hardened exact-once contract
before the rebase merge.

The phase-two click produced
`{"configuredAction":"newInstance","dispatchedOperation":"requestNewInstance","rowIdentity":"applications:org.kde.latte.sc-t5.desktop","rowKind":"task","sequence":2}`.
The captured original active window became the sole inactive, unminimized old
window with the same internal id, resource class, and caption. Its exact
PID/start-time/executable tuple remained live. Exactly one distinct active,
unminimized fixture window and one new validated process tuple appeared, for
totals of two. The task retained its app id, launcher URL, applet id, and row
identity while becoming `isGrouped=true,childCount=2`. After settlement, the
sequence remained exactly two and the same old/new identity relation persisted.

The negative control restarts the dock with the actually offered
`middleClickAction=0` (`None`), whose documented process-local sequence resets
to no event. Exactly one group-row middle click returned
`{"configuredAction":"none","dispatchedOperation":"none","rowIdentity":"applications:org.kde.latte.sc-t5.desktop","rowKind":"task","sequence":1}`.
For a three-second settled interval, sequence stayed exactly one; both KWin
internal ids and each window's active/minimized state stayed exact; both
validated process tuples stayed exact; and task app id, launcher URL, applet id,
active/minimized state, grouping, and `childCount=2` stayed exact. Every pass
also queried absent containment `4294967295` and received `{}`; the one-view
hermetic seed supplied no second valid containment.

Five controlled probes went RED before removal. A process launched between
pointer settlement and the final pre-click snapshot produced two windows, two
processes, and a grouped task and was rejected before input. An interrupted
`mktemp` allocation failed without a backup-residue cleanup error. A status-0
no-input path produced no dispatch. A numeric process start-time mismatch was
rejected before counting or signaling, and cleanup refused raw-PID signaling.
A nonexistent D-Bus method failed immediately at the query boundary. Three
fresh full corrected nested runs then passed. The focused compatibility batch
passed `020-wheel-task-cycle.sh`, `021-launcher-wheel.sh`, and
`022-empty-area-window-actions.sh` 3/3. The full local build, Bash syntax,
ShellCheck, desktop-file validation, and diff checks passed.

The installed `~/.local/bin/fakepointer` predates the tracked `middleclick`
verb, so verification used the worktree-built binary without modifying the
live-session installation. This exposed D64 (distro-gate fakepointer build
omits the xkbcommon link dependency), recorded separately at final commit
`611824a68`. D64 remains OPEN, B2a (the D64 distro-gate fakepointer xkbcommon
link repair) remains unchecked and outside SC-T5 scope, and
`ci/build-and-gate.sh` remains unchanged.

The final canonical full gate exited 0 and stamped the exact pre-rebase PR tree.
GitHub's rebase merge produced final head `91b24b694` with the same tree
`320a562378aa0687fe75dca0183ff8182aa9de66`, so the merged code and tracking are
the full-gate equivalent. This docs-only finalization makes no new code-gate
claim.

## 2026-07-21: SC-T3 (the D29 narrow middle-click dispatch readback) merged

PR #99 merged SC-T3 (the D29 narrow middle-click dispatch readback) into
`origin/main` at `eb66895ca`. Final implementation and public API commit:
`e87deef58`. D60 (tasks QML type metadata omits accessibility composer methods)
record: `faceecd35`. Initial authoring and focused verification documentation:
`03bdcee11` and `668c40c22`. D61 (middle-click aggregate could expose an older
plausible event) and D62 (middle-click readback accepted inconsistent
action-operation pairs) fix: `bfd30f235`. README and public documentation:
`9d377b51a`. Initial production bridge guards and final collector semantic
guards: `e190d03b0` and `4dd51fdcd`. Hash refresh documentation: `6bcbc7f0f`
and `aa2f375a6`. D63 (task settings-inventory anchors did not follow
middle-click QML) anchor fix and tracking: `cd959cb3a` and `f2c2ba089`. Final
review documentation: `ca5753f62` and `eb66895ca`. D60 remains OPEN and its
repair remains separate.

`TaskMouseArea.onReleased` records at the production launcher/task dispatch
branch without changing its operation. The tasks backend retains one typed
event per tasks applet and assigns a process-monotonic sequence. The D-Bus
collector selects the newest event across the view only after every candidate
passes containment, shape, action-operation, and globally unique sequence
validation. Any malformed nonempty candidate or duplicate sequence refuses the
aggregate as `{}`. Empty maps remain legitimate no-event state; a missing
startup quick item remains loudly unavailable; an applet in Plasma's removal
undo window remains queryable until actual destruction. The new read-only
method is `taskMiddleClickDispatchData(u containmentId) -> s`. It returns only
`rowIdentity`, `rowKind`, `configuredAction`, `dispatchedOperation`, and
`sequence`, or `{}` before the first event. There is no setter, history,
arbitrary execution, action expansion, window title, or other application
content. SC-T5 (the D29 permanent runtime-effect acceptance) was intentionally
separate and had not started at that point.

Post-review focused application/tasks-plugin/test builds passed.
`tasksbackendtest`, sanitizer-backed `dbusreportstest`, and `sourceguardtest`
passed. Coverage includes all six offered task action-operation pairs and
launcher exceptions, malformed pair refusal, multiple applets, newest
selection, exact JSON, mixed no-event candidates, requested-containment scope,
malformed-plus-valid aggregate refusal, global 5/10/5 duplicate refusal, the
production QML recording branch/order, exact reporter forwarding and identity
fallback, reporter aliases, and the collector's plugin, quick-item, warning,
candidate, selector, refusal, and undo-window lifecycle semantics. The QML
compile gate passed 130 files, qmllint matched its 5,832-warning baseline, all
232 QML interaction checks passed, the coverage ratchet passed 96 entries and
31 paired headers, and XML validation plus generated adaptor compilation
passed. Regenerated tasks type metadata differs only by D60's two pre-existing
composer-method omissions. SC-T5 runtime-effect acceptance was not part of the
SC-T3 run.

The first canonical gate exposed D63 (task settings-inventory anchors did not
follow middle-click QML): SC-T3 shifted every `TaskMouseArea.qml` source anchor,
but the inventory retained pre-feature line numbers. Final commit `cd959cb3a`
moves all nine task-row anchors and the drag-and-drop exemption to their exact
handlers. Focused `settingsinventorytest` passes at 270 affordances and 21
exemptions. The final canonical full gate passed and stamped exact pre-rebase
head `2fd23a08e34a10eebeab11e7cbb02c919478b8d4`, whose tree matches final
tracking commit `f2c2ba089` after GitHub's rebase merge. This current-state
correction is docs-only after that gated code/tracking tree; it adds no code and
makes no new code-gate claim.

The QML type-dump comparison found D60. It is pre-existing and unrelated to
SC-T3; the new Backend property, signal, and method match regenerated metadata.
D60 was recorded at final commit `faceecd35` without fixing the unrelated drift.

## 2026-07-21: D57 ConfigOverlay wheel threshold reproduced

PR #96 landed SC-CW1 (the D57 ConfigOverlay wheel-threshold reproduction) at
final evidence commits `5ec57175f`, `aa6399b44`, `709c0946b`, and `9b0672cf9`.
PR #95 supplied its generic e2e prerequisites at `57bc03ce0`, `7f747f944`,
`fb3466223`, `ce424574a`, `cd6d317b2`, and `240476b9c`; it has no separate
settings-plan completion unit.

D57 (ConfigOverlay wheel threshold accepts nonnegative decrease deltas) is OPEN:
`ConfigOverlay.qml` decreases for `angle < 12` instead of `angle < -12`.
Both view axes produced +120:+8, -120:-8, +96:0, -96:-8, +90:-8, -90:-8, and
horizontal +/-120:-8. Explicit `axisstop` sends a zero fake-input axis; KWin
forwards it as `wl_pointer.axis_stop`, and Qt emits no `QWheelEvent` in this
isolated sequence. The post-stop +120 still increases length.

Status 57 requires the complete matrix, clean shutdown, byte-identical config
restoration, and zero fixture residue. Status 0 is XPASS; input, query, partial-
signature, shutdown, restoration, and residue failures remain FAIL. SC-CW2 (the
D57 signed decrease-threshold fix and regression promotion) remains unchecked,
approval-required, and unapproved. Merged SC-CW1 evidence does not authorize the
production fix.

## 2026-07-21: D58 tracker requester fix landed

PR #94 landed SC-WT1 (the D58 tracker-enablement root fix and regression), so
D58 (close-only and minimize-toggle settings do not enable window tracking) is
FIXED. Final commits are `15f026887` for the root fix, `91cfb2bac` for
initial tracking, `14da9e7ce` for e2e harness hardening, `0a796e1ec` for the
complete requester source guard, and `8c6b1c826` for review tracking.

The root was the requester OR-set in
`containment/package/contents/ui/BindingsExternal.qml`: it enabled the active-
window tracker for `dragActiveWindowEnabled` but not for
`closeActiveWindowEnabled` or `ScrollToggleMinimized`. The fix adds only those
two existing-contract dependencies, with no capability check, typed refusal,
action-choice UI, D30 product decision, or adjacent cleanup.

Review hardening removes false-zero window counts, checks every KWin and
fakepointer status, reserves effect retries for successful input, exposes
fixture termination failures, and proves final zero residue plus byte-identical
config restoration before PASS. The source guard now compares the complete
normalized requester expression, including all prior visibility, applet,
move/maximize, dynamic-background, and floating-gap arms.

Controlled probes proved the hardened verdicts: a false injector failed at the
first settlement move with status 1, and an OR-to-AND requester mutation failed
the exact-expression guard. Both temporary probes were removed. Three hardened
back-to-back nested runs passed the disabled, enabled, no-target, independent
close/minimize effect, final absence, and config-restore checks. Middle click
needed two status-0 effect retries in each run; wheel effects arrived first try.
QML compile passed 130/130, the 234-file qmllint baseline matched at 5,832
warnings, and focused `sourceguardtest` passed. The earlier five-test focused set
also remains green. The capability-check and typed-refusal follow-ups remain
separate from D58, as does SC-B2 (the D30 product decision and sign-off gate).

## 2026-07-21: D59 AppStream source and native recipes complete

D59 (invalid standalone AppStream identity and stale library provider) was
source-corrected by merged PR #91. Its final implementation commits are
`94f8dc1e5`, `c5adbb863`, `cb659d480`, `477cdf70a`, `7246b4222`, `5c51ef221`,
`696d383db`, `7463152e8`, and `625b6c2c0`; merged head is exact
`80451925475d7d5b0fb6d74f6b43d782c81dc4b5`. The configured metadata declares
desktop application `org.kde.latte-dock`, keeps desktop launchable
`org.kde.latte-dock.desktop`, has no Plasma Shell extension, and provides only
binary `latte-dock`. Upstream tag `v0.10.8` at `28f39d65d` shipped the old
component ID, so `replaces` preserves its software-center history without
retaining the invalid ID as a live alias.

The new `appstreammetadatatest` validates the configured metadata directly, so
ECM's absent-`install_manifest.txt` pass can no longer hide the source defect.
AppStream 1.1.3 changed from `cid-rdns-contains-hyphen` failure to successful
validation, and the coverage ratchet now records 96 CTest entries with 31 paired
unit headers. The installed-package gate requires package-owned metainfo and
parses its structure using the existing Perl dependency; its self-test passes
90 focused controls, including missing, unowned, wrong type, wrong ID, wrong
launchable, missing or malformed released-ID migration, forbidden extension,
and stale-library cases. No AppStream runtime dependency was added. AppStream is
an explicit native test dependency in both `package.nix` and the development
shell; a sandboxed `nix build .# --no-link` completed successfully.

PR #92 landed the mandatory recipe follow-up at final implementation commit
`dbba5ea48`; `ba32d824c`, `72796622b`, and `4eb2e3d67` record tracking,
independent acceptance, and the final Debian wording correction. Arch, Gentoo,
and Void now pin merged PR #91 head `804519254`; Gentoo and Void delete their
obsolete AppStream patches, while Arch had no patch. Debian and RPM already
carried no AppStream patch after PR #91.
The final contract is no per-distribution AppStream patch file or live recipe
reference anywhere in the tracked tree. The Void current-HEAD staging helper
also remains patch-free.

Arch's deterministic local archive is 4,587,878 bytes with SHA-256
`4bfda179309464052ca4805c5361478507f274a38239471b65b7fbb2b53542e6`. Gentoo
and Void consume the 4,593,948-byte GitHub archive with SHA-256
`6aad9e9b9192df1be6dcbfa114f8c8b522e9dcaa1dd38a6cb4a0359c621aaa0c`; the
Gentoo Manifest additionally records BLAKE2B
`4becd1ea52f28f6b60a31dd9114433a163a4a18e7eb4e15c5f2f9f465dca934c96ac1a5befcb770933d67f536a28475571bdf944204529d4a914c5090baba52c`
and SHA512
`6e307a94d47de2f90c7c71f7e42705854f8c9f042341d7d7b36dd4c62a47893f9ac306606a46da3b6f07dd12f701109a1bbcbc471f5645ee9f506b2d287c5e15`.
Both archives contain the corrected metadata exactly. Arch source verification,
`.SRCINFO` regeneration, namcap, Gentoo digest and syntax checks, Void xlint and
patch-free stage-only checks, and the tracked-tree patch scan pass. No package
version or revision changed because no continuation artifact has been released.

Five independent external lanes passed at exact pre-rebase PR #92 head
`45c0d27cbae8e23fdde621841a697a2c5ade59c6`. Every fresh install contained
exactly the `desktop-application` component, `org.kde.latte-dock` ID,
`org.kde.latte-dock.desktop` launchable and released-ID `replaces` relationship,
with no `extends`, public library, or stale provider. Every installed package
passed its native integrity checks and the full nested-Wayland package gate with
status-0 shutdown.

Arch reproduced source SHA-256
`4bfda179309464052ca4805c5361478507f274a38239471b65b7fbb2b53542e6`, built a
runtime package with SHA-256 prefix `766e5499`, and verified all 766 files without
a patch. Debian/KDE neon passed 94/94 selected tests including direct AppStream
validation, lintian reported no errors or warnings, 554 checksummed regular files
covered 769 entries, and the runtime DEB SHA-256 starts with `9d02e711`; no quilt
patch existed. Fedora and openSUSE both passed `rpmbuild -ba`, installation,
integrity, AppStream, and the full package gate without a Patch directive or
file; their runtime SHA-256 prefixes and manifest sizes are `6e79aa2b`/659 and
`fafe9e46`/642 respectively. Gentoo reproduced the Manifest, passed clean
`pkgcheck`, 92/92 selected tests and `qcheck` 761/761, produced 552 package-owned
entries and a GPKG with SHA-256 prefix `80a9a21d`, with no `PATCHES` entry or
file. Void passed xlint, fetch, build, repository indexing and `xbps-pkgdb`,
produced a 552-file XBPS artifact with SHA-256 prefix `1fbf7988`, and retained a
current-HEAD helper that stages zero patches.

The repository gate passed and stamped exact pre-rebase head `45c0d27cb`.
GitHub rewrote the recipe implementation to final commit `dbba5ea48` without
changing the validated tree.

The Phase 11 (Nix packaging and Docker build verification) item and D59 are
complete. F6 (the package-artifact CI task) remains open and out of scope.

README remains unchanged. This correction changes package metadata accuracy and
test enforcement, not a timeless product capability, public surface, phase
completion, or roadmap state.

## 2026-07-20: packaging wave B native recipes finalized

PR #85 landed F5 (the Void native package recipe) as post-rebase commits
`ea1bc5acb` and `ced7a48a5`. A fresh Void checkout built exact source head
`fbdc3a3150b5fe4dad788e189101e9989129fbb8`; xlint, XBPS package integrity,
desktop-file and AppStream validation, the 552-entry owned-file manifest, and
the installed-package gate through five mappings, nested-KWin startup, and clean
shutdown all passed. The fast repository gate passed 94/94 plus all 77 focused
installed-package controls at that same pre-rebase head `fbdc3a315`.

PR #86 landed F4 (the Gentoo native package recipe) as post-rebase commit
`22cb06836`. Portage resolved the test and no-test closures, the forced source
rebuild passed all 90 selected tests, rebuilt GPKG metadata recorded the exact
Vulkan subslot requirements, and pkgcheck and Manifest QA passed. The built GPKG
and package/source reinstall produced a 552-object Portage-owned manifest; the
installed-package gate passed exact mappings, nested-KWin startup, and clean
shutdown. The fast repository gate passed 94 CTest entries, qmllint, all 13 scene
probes, and matrix fixtures at pre-rebase head
`d38e12ed26a198b56b76773adae8ae8c5534713a`.

All five local recipe formats now exist. Wave A (the Debian-family, shared RPM,
and Arch formats) and Void have fresh/clean target install evidence as recorded
above and in their PRs. Gentoo has package/source reinstall evidence rather than
a pristine first-install environment, without reducing its built-package,
ownership, or installed-runtime proof. F6 (the package-artifact CI task) remains
pending and outside this recipe-finalization scope. No official package or
repository, package publication, hosted CI, release, tag, artifact upload,
sponsorship, or distribution endorsement exists. No settings-plan or defect
state changed.

## 2026-07-20: packaging wave A native recipes finalized

PRs #80, #81, and #82 landed F3 (the Arch native PKGBUILD and generated
`.SRCINFO`), F1 (the Debian-family native recipe), and F2 (the shared
Fedora/openSUSE RPM recipe). Their final post-rebase commits on `origin/main`
are:

- F1: `43131b00f`, `43ea0f17f`, `48bb23fb6`, `b2889ffbb`, `b344808a8`,
  `02770a57c`, `7cf2be7aa`, `efbefc500`, `99ed25c46`, `82fcc84b1`,
  `aeb47d001`, `07d94fe64`, `3d9d58342`, `d497d2f78`, `a04c47614`,
  `71fa379c2`.
- F2: `bb5e45f1f`, `17e1b10fd`, `3567e3c9e`, `4a10ccc0a`, `f1184c3d3`.
- F3: `59e9367ff`, `bfc62b3e0`, `85ed78b2c`.

The Debian-family acceptance used a minimal Ubuntu noble root carrying only
the signed KDE neon User repository. Its checked build passed 92/92 package
tests, the `nocheck` build skipped the test override, and lintian reported no
tags. A fresh root installed local package
`c1e9f343bc4bfb22c862f14d8475652d40587501b28044ce199081018141c779`;
`dpkg --audit` and `apt-get check` exited 0. The 768-entry `dpkg-query`
manifest has SHA-256
`43e4a2c3c13443545febb61f03fc495c7deb15f6ee9732640c3ecaefc035ff7e`.
The installed-package gate mapped `/usr/bin/latte-dock`, three Latte QML
plugins, and the containment-actions plugin from `/usr`, settled one view under
nested KWin, and shut down with status 0.

The Arch clean build produced main package
`2180c8f6a0630161d74cf8968627f3d216a4fc32ae6bca3d5a7e81d0afdbeebc`.
A fresh `pacman -U` install exited 0; its 766-entry `pacman -Qlq` manifest has
SHA-256 `9c8f4c0598cbecb3632a8a4ff64124a24f0b6b9bebfdc9d1a8119abca8e6d4d6`.
The installed-package gate verified the exact executable and four startup
plugin mappings, settled under nested KWin, and shut down with status 0.
Namcap reported zero errors, and `.SRCINFO` matched `makepkg --printsrcinfo`.

Fedora 43 and openSUSE Tumbleweed each produced source, runtime, debuginfo, and
populated debugsource RPMs from the reproducible source archive with SHA-256
`98baabfbb4eccde9ecb0c553583c02bf3a7f2acfa2e946a93db7d12a0441d875`.
Fresh roots installed runtime RPMs
`dbbb55482915ccd622cda4c01ed9697f0a9eec77355e79fad97823cf2ebec1f1`
(Fedora) and
`532e84569ad1c3de215f63d975f233031bebe55d94ba9a2cb739aeba2762ac3f`
(Tumbleweed). Both `rpm -V` checks and installed-package gates passed through
exact installed mappings, nested-KWin startup, and clean shutdown. Fedora's
659-entry `rpm -ql` manifest has SHA-256
`828111dd72e23787f9ad82133fd274fd98a660a96397e82956ab18a30a122459`;
Tumbleweed's 642-entry manifest has SHA-256
`806453d3bb3d30ad862d23d6203f4f688e35b7b6fd144ac1a5bb741853796bd0`.

The fast repository gates each passed 94/94 at pre-rebase heads `4c937adde`
(Debian/KDE neon), `90f610c59` (Arch), and `b9f9ed439` (Fedora/Tumbleweed).
Their post-rebase equivalents are `a04c47614`, `85ed78b2c`, and `f1184c3d3`;
F1's later `71fa379c2` follow-up changed only manpage aliases and passed its
focused documentation and lint checks. No hosted CI, official repository,
release, tag, upload, publication, sponsorship, or endorsement was created.
Gentoo and Void followed in PRs #85 and #86, with current evidence recorded
above. No settings or defect state changed, and no new defect was found or
filed.

## 2026-07-20: settings investigations disposed and focused follow-ups approved

`docs/tracking/settings-surface-completion-plan.md` now separates the per-view
and global settings initiative into one-PR source inventory, read-only registry,
driver, shared-component, page, migration, runtime-core, observability, and e2e
matrix units with explicit dependencies. Its C1-C9 (nine-property control
completion) contract still requires reachability, real operation, exact writes,
runtime effects, reflection, persistence, complete choices, lifecycle cleanup,
and accessibility.

Approval covers only the evidence-first and scaffold sequence: SC-F1 (the
per-view source inventory and evidence ledger), SC-F2 (the source-to-ledger
coverage gate), SC-O1 (the read-only settings-control D-Bus registry),
SC-D1 (the pointer and keyboard driver), SC-D2 (the popup and lifecycle helper),
SC-C1 through SC-C5 (the five shared-control component families), SC-W1 (the
launcher-wheel regression guard), and the focused D29, D57, and D58 follow-ups
below. Page behavior changes, action expansion, schemas, migrations, and
maintained-continuation divergences are not approved by the plan.

PR #88 landed SC-F1 at final commit `472711d11`. PR #89 landed SC-W1 at final
commits `d2fa8bbd1`, `3b6930851`, and `c61ce8502`. PR #103 subsequently landed
SC-F2 (the source-to-ledger coverage gate) at final commits `7eda16ca0`,
`6089fdea9`, `14ecdf9dd`, and `31f9bb356`. SC-T3 (the D29 narrow dispatch
readback) merged through PR #99 and is complete; PR #101 merged SC-T5 (the D29
permanent runtime-effect acceptance) at final implementation commit
`382268a92`. These completions do not widen any adjacent unit.

- D29 (task-icon middle click appears to execute left-click behavior) is
  ACCEPTED as Qt5-faithful configuration-scope behavior. At `5c2223a3e`, a
  physical middle click with default `middleClickAction=2` reached a pure Dolphin
  launcher, dispatched `activateLauncher` to `requestActivate`, and changed zero
  windows to one active window. The same click on that single-window row
  dispatched `newInstance` to `requestNewInstance`, changing one window to a
  two-child group. The sequence was reproduced twice; Qt5 and both forks retain
  the launcher exception; temporary instrumentation was removed. SC-T3 (the D29
  narrow dispatch readback) is complete through PR #99. SC-T5 (the D29 permanent
  runtime-effect acceptance) is complete through PR #101 at `382268a92`. SC-T4
  (the D29 root fix) is not applicable.
- D30 (Behavior mouse actions expose fixed booleans instead of full choices)
  remains OPEN only at SC-B2 (the product decision and sign-off gate). SC-B1
  (the D30 current-contract investigation) confirmed inherited booleans for
  left move/double-click maximize and middle close, a left single-click no-op,
  and the current wheel 0-4 behavior. Nested proof covered enabled, disabled,
  no-target, move/maximize/close, desktop/task wheel, activity refusal, and
  target history. The evidence favors retain-and-clarify, but SC-B2 remains
  pending and no expanded action model is approved.
- D57 (ConfigOverlay wheel threshold accepts nonnegative decrease deltas) is
  OPEN and reproduced. PR #96 landed SC-CW1 (the D57 ConfigOverlay
  wheel-threshold reproduction) at final commits `5ec57175f`, `aa6399b44`,
  `709c0946b`, and `9b0672cf9`. SC-CW2 (the D57 signed decrease-threshold fix and
  regression promotion) remains unchecked, approval-required, and unapproved.
- D58 (close-only and minimize-toggle settings do not enable window tracking) is
  FIXED by PR #94. SC-WT1 (the D58 tracker-enablement root fix and regression)
  landed at final commits `15f026887`, `91cfb2bac`, `14da9e7ce`, `0a796e1ec`,
  and `8c6b1c826`. Wayland close/minimize capability checks and typed-refusal APIs
  remain separate plan findings.
- D56 (pure-launcher task wheel uses inherited asymmetric activation) is
  ACCEPTED as Qt5-faithful. The initial disposable nested capture at `6765b2320`
  is now backed by the permanent SC-W1 (the D56 launcher-wheel regression guard)
  at `d2fa8bbd1`, `3b6930851`, and `c61ce8502`. Positive wheel activates the
  launcher through `TasksModel.requestActivate`, negative wheel no-ops, manual
  scrolling controls whether `ScrollNone` reaches the same no-overflow path, and
  the burst-limiter guard keeps pure-launcher classification stable. This is
  separate from D29.
- D24 (TypeSelection Dock/Panel presets write two dead keys) remains OPEN as
  SC-M1 (the D24 dead TypeSelection write cleanup), independent of D30.
  D31 (valid Justify splitter moves reset after restart) remains FIXED by PR #73
  and is outside the settings-completion ownership.

PR #90 landed SC-T1 (the D29 middle-click evidence capture), SC-T2 (the D29
disposition), and SC-B1 (the D30 current-contract investigation) at substantive
evidence commit `327e2e9af`. Commit `7a65f6130` only updated trace metadata, and
`35f46a981` only glossed investigation gate names; neither replaces the
implementation/evidence mapping.

## 2026-07-20: D25 (task icons stay stale after icon-theme changes) fixed

PR #76 landed the icon-theme refresh path on `origin/main`. `Kirigami.Icon`
cached the raster resolved from a stable task-model `QIcon` QVariant, so
`KIconLoader` theme-data updates did not reevaluate the source binding.
`Environment` now emits only for real theme transitions, and `ThemeAwareIcon`
retains the QVariant while synchronously clearing and rebinding its inherited
source. The primary task icon and both tooltip preview icons use the component.

The focused production-QML test changes a named fixture from red to blue while
its source signal and cache key remain unchanged, and keeps a nameless
pixmap-backed icon green. The full build, QML compile gate, and qmllint ratchet
pass. The coverage ratchet records 94 ctest entries and 31 paired unit headers.
Post-rebase commits: `8423fab40` (fix and render regression) and `6765b2320`
(coverage ratchet). latte-dock-ng commit `ef2989ec2` supplied the missing-refresh
idea; its global theme mutation, cache clearing, file watching, and deferred
rebind were not carried.

## 2026-07-20: D26 (VisibilityManager normal-state binding loop) and D31 (Justify splitter moves reset after restart)

Two normal-state and splitter defects landed on `origin/main`.

- PR #74 fixed D26 by deferring only the AutoSize continuation from the
  declarative `inNormalState` true edge. The production-QML negative control
  fails four assertions with the direct call restored; the fixed path passes
  all five focused scenarios, 232 qmlinteraction cases, the 129-file QML compile
  gate, and strict-on-touch qmllint with AutoSize reduced from 24 warnings to
  zero. Post-rebase commit: `4cc94a48f`.
- PR #73 fixed D31 by publishing each moved Justify splitter position through
  the same explicit key inserted into the live config map. The production
  `LayoutManager` fixture persists independent moves across complete
  `KConfigLoader` reconstruction, restores start/main/end zone counts `1/1/2`,
  preserves applet order, and keeps a healthy no-op byte-identical. The full
  gate passed at pre-merge `4f505ac5b`; GitHub rewrote that tree-identical head
  to post-rebase commits `91eff7c46` and `3170dd4f9`.

## 2026-07-20 SESSION: shared installed-package gate hardened (PR #72)

PR #72 landed F0 (the shared installed-package provenance and nested-runtime
gate) on `origin/main` at `468baf961` after rebasing onto `7a5314130`. Blockers
from independent reviews and follow-up cross-checks are fixed. Live `--root /`
checks require an explicit newline-delimited package manifest, while isolated
package extraction roots remain valid without one.
Every selected Latte artifact and file below the audited QML/data trees must be
owned by that manifest. All links in an isolated extraction resolve from its
package namespace rather than host `/`; selected targets remain inside their
artifact trees and their resolved host paths drive inspection, loading, launch,
and mapping identity. Live-root links retain host semantics. `find`, `readelf`,
`awk`, `/proc/<pid>/maps`, and process-environment producers publish results
only after successful completion.

The installed `latte-dock` target must be ELF, so check-only and runtime
`/proc/<pid>/exe` provenance use one contract. All five plugin slots require
their exact Qt IID and class; containment actions and indicator package
structure also require typed category metadata: containment service types are
an array with exact membership, while indicator package structure and parent
application are strings. A version-verified Qt 6 inspector is selected ahead of
any unsuffixed tool; each exact-format major-version probe is independently
bounded and a failed candidate falls through. Metadata inspection and
immediate-binding `dlopen` run under fixed timeouts. Normal dock startup must map
all three QML plugins and the containment-actions plugin from the installed root.
The indicator package structure is intentionally not a startup mapping: it is
used while opening or installing indicator packages, so its applicable runtime
proof is bounded metadata validation plus `dlopen`. Literal absolute
RUNPATH/RPATH entries are rejected for isolated roots because the host loader
cannot apply package-root namespace semantics; `$ORIGIN`-relative entries and
contained live-root absolute entries remain valid.

Dock shutdown and cleanup target the complete `setsid` process group with
bounded TERM/KILL escalation, including the leader-exits/descendant-survives
case. Polling distinguishes absent groups from `pgrep` operational failures and
counts zombie-only groups as stopped, so no failure path enters an unbounded
wait. Optional mapping registration returns success explicitly, so the
startup-inactive indicator cannot terminate the runtime under `set -e`.
Validation now preflights `env`; unused `sort` and self-test `grep` requirements
are removed.

Positive acceptance was driven in the cached Arch package environment with:
`podman run --rm --security-opt label=disable -v "$PWD:/src:ro"
localhost/latte-ci-arch:latest bash -lc 'cmake -S /src -B /build -G Ninja
-DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF && cmake --build /build
--parallel "$(nproc)" && BUILD=/build
/src/tests/installed-package-gate-runtime-test.sh'`. The successful command
mounted exact pre-merge source head `3ee077529` read-only and exited 0. Its
post-rebase tree-equivalent is `10b4c4565`. The executed code head was
pre-merge `31b768e5a`, post-rebase tree-equivalent `3fb92a05a`;
`3ee077529` added only tracking records after the final code fixes.

The Release payload installed under a fresh `/tmp` root, the dock settled under
nested KWin, `/proc` reported the exact installed executable and four startup
plugin mappings, forbidden ambient variables were absent, SIGTERM produced
status 0, the process group disappeared, nested cleanup completed, and both
installed-package PASS lines were reached. The focused self-test passed 77
controls, including stale same-prefix ownership,
partial producers, non-ELF launchers, absolute-link namespaces, deterministic
Qt 6 tool selection, wrong-but-valid plugin metadata, polling failures, a real
unreaped zombie, selected-artifact tree and manifest containment, coherent ELF
search-path semantics, optional mapping continuation, bounded exact version
parsing, typed plugin categories, and a TERM-ignoring surviving descendant.
The canonical full gate exited 0 at pre-merge head `9baf22ac7`, whose
post-rebase tree-equivalent is merged main head `468baf961`: 94/94 ctests and
all 77 focused installed-package controls passed. The runtime, gate, and merged
heads differ only by tracking documentation after the final code fix.

The install-assertion idea is credited to latte-dock-ng's
`docker/verify-install.sh` at exact commit
`9c12a79aaf9350e73059da5b293c931218419c05`; the nested-runtime implementation
is original. Post-rebase review follow-up commits: `02153ed63`, `9fe8ddd1d`,
`3b025df03`, `40ad5a245`, `ebcda72fa`, `29a5e4ce6`, `c329eb138`, `98f4ff797`,
`009c406dc`, `3fb92a05a`.

## 2026-07-20 SESSION: D27 (stale maximize work area) latency follow-up

The D27 (maximize transitions leave a stale floating-gap work area) follow-up
landed through PR #70. PR #61 fixed
unbounded `windowChanged` starvation by preserving the first 150 ms deadline,
but its records incorrectly described an immediate maximize route. A direct
trace found two remaining delays: `PlasmaWindow::maximizedChanged` still shared
the coalescer, and the resulting `strutsThicknessChanged` waited behind the
one-second geometry throttle. The measured thickness changed `44 -> 26` at
`1784527923148`; `setViewStruts()` received it at `1784527924217`, 1.069 s
later.

The follow-up adds typed immediate/coalesced window-change delivery, routes
maximize edges immediately, and publishes discrete strut-thickness changes
directly while retaining the throttle for absolute/screen/off-screen geometry
churn. `windowchangedebouncetest` and `sourceguardtest` passed 20 repetitions.
Negative controls rejected the old coalesced maximize route, a parallel direct
geometry route, and a second throttled thickness route. The strict
`tests/e2e/071-maximized-window-length.sh` acceptance drove one uniquely tagged
active Wayland Konsole, correlated both tracker facts through two maximize
cycles, measured a 284 ms reservation update, and verified positive frame
geometry covering KWin's exact screen-derived 88 px work area. Two cleaned
real-session Firefox runs both measured 114 ms with exact `0,26 1440x2534` KWin
frame geometry. Temporary instrumentation is removed. The full gate passed at
pre-merge `29a7b63bf`, including 93/93 ctest, sceneprobe 13/13, and the
sanitized nested dock; GitHub rewrote that tree-identical head to `11861e947`.

## 2026-07-20: D32 (disabled Always Visible floating-gap tracking)

PR #71 fixed an inherited one-character tracker-enablement defect found while
hardening the D27 (maximize transitions leave a stale floating-gap work area)
nested acceptance. `BindingsExternal.qml` read nonexistent
`root.screenEdgeMarginsEnabled`; the declared property is singular. A hermetic
Always Visible layout therefore reported `trackerData.enabled=false` even with
`hideFloatingGapForMaximized=true`, while a richer real layout could mask the
bug through an unrelated applet tracking requester. The fix selects the
declared property. A marker-scoped source guard fails when the plural spelling
is restored; QML compile, qmllint, and the complete fast gate passed. Merged as
PR #71 (`54572f495` + `33c72b34e`).

## 2026-07-19 SESSION: D27 + D28 panel bugs fixed and delivered live

Two live panel-mode bugs on the real top panel were investigated in the same
session. D28 was fixed and delivered; D27's first fix bounded one delay, with
the remaining end-to-end latency resolved by the 2026-07-20 follow-up above:

- **D27 (window-change starvation, PR #61)** - the first investigation found
  that `AbstractWindowInterface::considerWindowChanged` re-armed its 150 ms
  timer on every `geometryChanged` event from KWin's maximize animation. PR #61
  bounded that stream by preserving the first deadline. It did not route
  maximize synchronously or cover the later exclusive-zone publication delay;
  the 2026-07-20 entry above records the end-to-end follow-up.

- **D28 (show-desktop white icon on light panels, PR #60)** - the show-desktop
  applet rendered a white icon on a light panel because the
  `IconColorfulness` probe judged the Breeze `user-desktop` icon as colorful
  and gated the colorizer off. A deeper root-cause pass confirmed the probe
  and its wrapper target are correct; the icon itself is intrinsically
  multicolored. The in-tree root-cause-aware fix is a policy override in
  `AppletItem.qml` that forces `org.kde.plasma.showdesktop` to follow the
  panel colorizer scheme. E2E guard in
  `tests/e2e/110-colorizer-applet-contrast.sh`. Merged via PR #60.

Both PR #61 and the D28 fix are on main and were delivered live. PR #61 bounded
window-state starvation, and the dark show-desktop icon rendered correctly;
the D27 floating-gap latency required the 2026-07-20 follow-up above.

## 2026-07-18 (previous state)

Trunk renamed master->main; the e2e interaction DRIVER
layer is fully landed - C-I7/C-I8/C-I9 merged as PRs #37/#38/#39; the edit-mode
settings audit harness CL-0 merged as PR #40; and a second merge wave cleared:
CL-1 length cluster (PR #43 - D15 ACCEPTED, D16 slider-desync + D17 Justify
stranding FIXED), the X11 survivor sweep (PR #42 - dead-code removals, D19 filed,
proposals D1/D2/D3/D5/S1 awaiting sign-off in docs/tracking/x11-cleanup-audit.md), and the
D20 normal-mode menu guard (PR #44 - the write-path emptying was disproven).
D20-class menu emptying was also fixed live on the real dock by restoring the
contextMenuActionsAlwaysShown key. The Light/Layout colorizer contrast fix
LANDED as PR #46 (D21/D22/D23 FIXED via approach B - push the resolved scheme
into applets' Kirigami.Theme color groups, retire the FBO overlay; a recorded
Qt5 divergence; new per-applet colorizerActive/colorizerReason readbacks) and was
DELIVERED live to the real dock (verified: the Light top panel resolves dark fg
#202326 on light bg #fcfcfc, all applets colorizerActive=applied).

LANDED: wave-2 audit clusters all merged - CL-2 (appearance, PR #53), CL-3
(behavior, #51), CL-4 (effects, #52), CL-6 (chrome, #50). All controls P2/P3
green; the S-b palette-index, S-c duration-sense, and S-d combo-round-trip
suspicions are CLEARED; the edit-background wheel is confirmed Qt5-faithful (it
writes editBackgroundOpacity, not the panelTransparency both forks wrongly rewired
to); S-a is CONFIRMED and filed as D24 (TypeSelection writes two dead keys
solidPanel/colorizeTransparentPanels - disposition pending Bree: remove or accept,
harmless either way). CL-6 added an advanced-mode + drag-corner-scale readback to
viewConfigData. Wave-2 follow-ups (all small): control 56 (maximizeWhenMaximized)
is unassigned between CL-1 and CL-2; control 90's per-plugin indicator sub-options
need an [Indicator][<pluginId>] readback for their sub-audit; CL-6's destructive
add/remove/close control-level live drives are deferred; the D24 disposition.

NEXT for a fresh session - drive it with docs/prompts/orchestrator-prompt.md (the
reusable farm/review/merge workflow + subagent templates):
1. The edit-mode settings-audit epic is COMPLETE (CL-0..CL-6 all landed; CL-5
   settled D10 as "applies" - the port already wires the Tasks config, no wire-up
   needed). Remaining tail: the control 56 / 90 readback gaps and the D24
   dead-keys disposition (remove or accept).
2. X11 hygiene, best-practice sequence (docs/tracking/x11-cleanup-audit.md has the plain-
   English plan): one small PR = D3 (collapse the dead windowColorScheme else-arm)
   + D2 (mark the D19 keep-above an explicit `// STUB`); THEN a deliberate isolated
   later pass for D1 (strip the WindowId X11 parse surface -> uuid-only) and S1
   (QByteArray-uuid -> QUuid substrate, LAST, its own driven-verified PR, optional);
   D5 at the next fork-sync.
3. The e2e scenario/abort track (C-S*/C-A* in docs/tracking/e2e-interaction-test-plan.md)
   and the deferred nits.

Note: AGENTS DEVELOP IN THE NESTED VEHICLE, never the real session (a WIP-build
restart broke the live dock this session); the ORCHESTRATOR does controlled live
deliver/verify. See the live-system consent memory.)

## 2026-07-18 WAVE: e2e driver layer complete + settings-audit harness (CL-0)

The e2e interaction plan's DRIVER layer is fully on main. The three interaction
drivers landed each as its own reviewed PR through `gh pr merge --rebase`
(server-side rebase, MERGED not closed, shas rewritten at merge):
- **#37 C-I8/P7** task-reorder driver + G4 window-task readback + the `dragkey`
  fakepointer verb (DR-6 escape-in-held-drag). D1 (aborted task-reorder does not
  revert) resolved ACCEPTED, Qt5-faithful live-move.
- **#38 C-I9/P8** widget-explorer real cross-surface Wayland DnD driver +
  `showWidgetExplorer` coarse action. Found D18 (explorer-drag enter/leave
  flicker, OPEN, correctness unaffected).
- **#39 C-I7/P6** applet-reorder driver + G2 delegate-`z` readback +
  `setViewConfiguringApplets` action. Confirmed D2 live (ConfigOverlay z=900
  strand on edit-exit mid-drag; was SUSPECTED, now OPEN).

Merge-queue lesson banked: serial `gh pr merge --rebase` moves main under every
other in-flight branch, so each following PR re-rebases onto the new tip before
its asan-at-merge runs (asan must validate the exact head that lands). #38/#39
auto-merged their overlapping docs both-sides; #40 had real code overlap in
dbusreports.cpp/lattecorona.cpp (disjoint methods, kept both).

**CL-0 (#40) landed** - the edit-mode settings audit harness:
`viewConfigData(u)` / `appletConfigData(u,u)` in-process config readbacks (the
KConfig default-deletion trap avoided by reading the live KConfigPropertyMap),
the config-snapshot-diff core (tests/units/configsnapshotdiff.h), the shell
audit library (tests/e2e/audit/audit-lib.sh with the P1/P2/P3/P4 assert
helpers + settings-window drive helpers), and the QML-handler wiring harness
(tests/settingswiringharnesstest.cpp). HC3 proven: a wrong-key, a stray coupled
write (D15 shape) and a no-op (D10 shape) each report FAIL through a real QML
handler. Three non-blocking review nits filed as AU-0e/0f/0g in the audit plan.

IN FLIGHT (farmed, code-only PRs; orchestrator owns plan ticks + defect filing):
- **CL-1 length cluster** - the three seed defects. D15 ACCEPTED (keep the
  coupling, Bree's call); D16 FIX (Max/Min sliders get the offset slider's
  re-syncing Binding{} pattern so the handle re-tracks config after a drag);
  D17 FIX (extend the clamp core's Alignment enum to represent Justify
  distinctly and skip the minLength floor for Justify in both clamp functions +
  bridge + handlers + new core tests).
- **X11-ism cleanup audit** - inventory the whole tree for surviving X11-isms
  now that main() refuses non-Wayland, execute the dead-code removals (seed: the
  X11-only WindowThumbnail element reachable only through a dead ternary arm;
  app/wm/windowid.h's dead X11 parse surface), and PROPOSE (sign-off-gated) the
  windowid.h newtype simplification and the QByteArray-uuid vs QUuid substrate
  question. Primary deliverable docs/tracking/x11-cleanup-audit.md.

NEXT after CL-1: the wave-2 audit clusters {CL-2 appearance, CL-3 behavior,
CL-4 effects, CL-6 chrome} in parallel, then CL-5 tasks/D10 wire-up. The e2e
scenario + abort chunks (C-S*/C-A*) are the other open track once C-I5
(moveViewToScreen) lands.

## 2026-07-18 X11 survivor sweep (own PR, in flight)

A second whole-tree grep for the X11 vocabulary after the first removal wave,
classified in docs/tracking/x11-cleanup-audit.md. Executed the DEAD/stale removals only:
deleted the X11-only PlasmaCoreThumbnail preview element (ToolTipInstance
thumbnail ternary collapsed to PipeWire), dropped the dead NETWM + orphaned
KWindowSystem includes from the two SubWindow helpers, relabelled the stale
`// X11` header over theme.cpp's live KWindowSystem include. Build-system X11
removal reconfirmed complete. Surfaced (NOT applied) as sign-off proposals:
D2 the aboutApplication keep-above X11-id no-op (known-defects D19), D3 the
windowColorScheme else arm, D5 the vendored backend.h netwm.h include, D1 the
WindowId X11 parse surface, S1 the QByteArray-vs-QUuid substrate. The two
still-open Phase-4 checklist items (the View activity-stop reshow hack and the
byPassWM decision) are unchanged - both need live evidence / a product decision.

## 2026-07-18 D14: startup invalid-color qCriticals fixed at the source (own PR)

The startup burst of `Tools.colorBrightness: invalid color from QML` qCriticals
(known-defects D14) is FIXED. ROOT confirmed in the nested vehicle: Kirigami's
attached PlatformTheme (and the colorizer/colorPalette chain it feeds) serves a
default-constructed invalid QColor on the FIRST evaluation of a creation-time
binding, before its palette resolves; the change notify recomputes the real
color a beat later. The C++ boundary guard in tools.cpp is correct and UNTOUCHED
(comment reworded only); the fix guards each of the 13 LatteCore.Tools
brightness/isLight call sites on color validity
(`COLOR.valid ? Tools.f(COLOR) : <fallback>`). Nested-vehicle evidence (3 runs
against the real config copied read-only): baseline 80 invalid qCriticals (all
spec=0); after 0, with temporary per-site attribution summing to exactly 80 (no
site missed); settled brightness values identical before/after (37.445 / 239.815
/ true). Method banked: with Qt private QML headers absent in this nixpkgs
qtdeclarative, the old V4-backtrace caller trace is not available; instead a
behavior-preserving QML wrapper (`f(C.valid ? C : (console.warn("tag"), C))`)
tagged each site while still passing the invalid color through so the C++
counter kept the baseline - and a per-site `console.warn` in the guard's else
branch gave the attribution. The throwaway probe recipe was tests/e2e/
zzz-d14-probe.sh (removed). Colors readback used temporary `onXChanged`
console.warn loggers in Manager (removed).

## 2026-07-18 SWARM: C-I7/P6 applet-reorder driver + G2 z readback

The applet-reorder infra chunk of the e2e interaction matrix
(docs/tracking/e2e-interaction-test-plan.md). Landed on its own branch/PR:
- The reusable driver `tests/e2e/matrix/applet-reorder-driver.sh` - enter/exit
  rearrange, applet-id-order + delegate-z readbacks, and a glide reorder with
  commit/origin/noop/jitter/escape/occupied/overflow modes plus an
  arbitrary-target primitive (the justify-splitter seam). It is what F3/A2
  (C-S6/C-S7/C-A2/C-A2b) drive.
- The rearrange sub-mode (inConfigureAppletsMode) is transient with no config
  path, so a NEW coarse D-Bus action `setViewConfiguringApplets(u,b)` is the
  driving surface (flips the HeaderSettings rearrange toggle; refuses when the
  view is not in edit mode).
- G2: a `z` field on each applet in `viewAppletsData` (the AppletItem DELEGATE
  z, found via an `isInternalViewSplitter` parent-walk; the delegate, not the
  inner quick item, is what ConfigOverlay lifts to 900). Makes the
  stuck-over-chrome residue queryable.
- DR-6: a new fakepointer `dragkey <keysym> ...` verb (taps a key while the
  pointer button is held, one connection) - the escape-in-held-drag primitive.
- HC3 acceptance `tests/e2e/100-applet-reorder.sh` PASSES both axes. It also
  CONFIRMED D2 live: the DR-6 escape exits edit mode and STRANDS the applet at
  z=900 (G2 queried it), so D2 is now OPEN, not SUSPECTED. The T4c edit-exit
  strand fix (rescue the ConfigOverlay currentApplet in main.qml
  onEditModeChanged) is the C-A2b marquee, out of this chunk's scope.

## 2026-07-18 SIDE TRACK: clangd editor code intelligence (own PR)

Made clangd resolve the whole Qt6/KF6 tree with zero false diagnostics from
`nix develop`, so a contributor opening the repo in VSCode (or any LSP editor)
gets correct code intelligence out of the box instead of a flood of bogus
"QHash/QObject file not found" errors. Root cause: no compile DB existed, so
clangd parsed every file standalone. Three-part fix: `CMAKE_EXPORT_COMPILE_COMMANDS ON`
(emits build/compile_commands.json, a side artifact only - build graph proven
identical OFF->ON); repo-root `.clangd` (CompilationDatabase: build); and a
hash-tolerant `--query-driver=/nix/store/*/bin/*gcc,/nix/store/*/bin/*g++` glob
so clangd runs the nix g++ wrapper to recover the Qt/KF6 umbrella + libstdc++
include paths nix injects via NIX_CFLAGS_COMPILE (never recorded in the DB, and
the reason the DB alone is not enough on nix). devShell now ships clangd
(clang-tools - no gcc/clang++ driver, so no toolchain shadowing). `.vscode/`
committed; contributor flow in docs/reference/clangd-setup.md. Headless proof via
`clangd --check`: layoutmanager.cpp 21->0, effects.cpp 21->0, justifysplitters.h
5->0 real diagnostics. Filed as a Phase 0 plan item.

## 2026-07-18 SESSION: panel fixes, UB-catching, dev-loop shadow fix

Pivoted from the multi-distro mission (Phase C landed as #18; Phase D+ still
standing, section below) to a batch of panel bugs Bree hit on the real dock,
plus a UB-catching initiative. All landed as MERGED PRs via gh pr merge --rebase
(so the local panel-fix-* branches are stale - their work is on master under
rebased shas, not the branch shas).

LANDED (master 326aba06d):
- #19 floating gap becomes a real edge offset + systray popup anchors UNDER the
  panel (maskgeometry).
- #20 edit-mode header hint renders in-window so it stops eating the Rearrange
  click (+ #743d11e2e guard test).
- #21 Prong A: -DLATTE_SANITIZE=ON dock build (ASan+UBSan+forced-asserts on the
  dock/libs, not just unit tests) + scripts/run-asan-dock.sh (nested) +
  scripts/start-dock-sanitized.sh (LIVE, for Bree's pre-release manual testing).
- #22 Prong B1: justify-splitter pure core containment/plugin/units/
  justifysplitters.h - the ONLY splitter-insert path; repairs out-of-range
  positions (killed a splitterPosition=0 -> QList::insert(-1) UB); sanitized
  guard proven a real tripwire.
- #23 DEV-LOOP SHADOW FIX: the staged dev dock was silently loading the
  SYSTEM-INSTALLED packaged latte-dock's containment plugin, not the worktree
  build - Bree's machine enables programs.latte-dock.enable=true ->
  environment.systemPackages -> the session Qt6 wrapper seeds
  NIXPKGS_QT6_QML_IMPORT_PATH with packaged 0.10.77 QML. Every containment/plugin
  change "landed but never ran" on the dev dock, invisible until /proc/<dock>/
  maps was read. Fix: scripts/lib-qml-env.sh strips ONLY the packaged latte-dock
  leaf (grep -vE '/nix/store/[^/]*-latte-dock-'), keeping frameworks the QML
  gates need (a wholesale unset broke qmlcontracts - caught by the gate). PROVEN:
  /proc/maps flipped packaged->staged; #19/#22 fixes now visibly active on Bree's
  real dock (Bree confirmed "dock is looking better").

ALSO LANDED (merged since):
- #25 A3 (ub-catching): sanitized dock wired into a DRIVEN gate (build-asan +
  halt_on_error + /proc/maps shadow-assertion + injected-UB proof, now gate-all's
  final stage). CAUGHT TWO REAL vptr UBs on first run (decayed-vptr static_cast in
  destroyed() slots, genericlayout.cpp + syncedlaunchers.cpp, fixed via
  reinterpret_cast, filed under B2). Commits ddb766df1/f8505a543/d6fc8cd9e.
- #26 clangd: CMAKE_EXPORT_COMPILE_COMMANDS + .clangd + nix --query-driver +
  devShell clang-tools + .vscode + docs/reference/clangd-setup.md. Contributor gets
  zero-false-diagnostic VSCode clangd out of the box (proven clangd --check
  21->0). In-session harness clangd still noisy (it invokes without --query-driver;
  cosmetic, VSCode has it).
- #24 maximize-length repaint: inputmaskflush.h union-then-collapse core, SCOPED
  to LENGTH-axis shrinks only (F2 caught autohide/dodge HIDE over-capture - a
  hidden dock held its full body as input for 100ms; fixed by keying the hold to
  the length axis so thickness/hide applies the strip directly). New
  appliedInputRegionRects D-Bus readback. Commits 2b80a9411/79f8f1379/c05f844c2/
  98008f70f. DESK-CHECK OWED to Bree: (a) maximize-length un-maximize shows no
  frosted band at the former extent; (b) autohide/dodge hide + zoom-out settle
  show no click-swallow / spurious reveal (drive it on the live dock once the
  swarm quiesces; the fix is on master but Bree's running dock predates it).
  TWO NITS FILED (re-review, non-blocking): inputmaskflush.h:64 the axis test keys
  off the held `applied` region not the previous logical band, so a
  maximize-length settle still pending + an autohide HIDE within 100ms
  misclassifies the hide as a length shrink (sub-100ms race, exotic combo, still
  strictly better than pre-fix; hardening = classify against the previous logical
  band); and inputmaskflushtest.cpp:210 rests on the reveal strip preserving the
  band length EXACTLY (1px shorter flips lengthShrank true) - worth a comment.

E2E INTERACTION SUITE (docs/tracking/e2e-interaction-test-plan.md, on master): 196
scenarios (~106 novel), abort first-class, readback-first (goldens only where no
readback exists), full-dual-display matrix, tier-1-in-gate-all + full-periodic.
Both HC gates passed at review (HC1 panel-alignment degeneracy, HC3 rejection
acceptance per driver). Adversarial abort matrix (PR #31) enriched A1-A6 with
code-grounded wrong-place/off-dock/timing scenarios (T1-T5 targets, DR-1..DR-6
driver needs) and found real bugs (D1-D3 in known-defects). E2E INFRA LAYER
COMPLETE (all merged): C-I1 harness backbone (#30) + residue-detector hardening
across every strand surface incl lattedockrc (#35); C-I2 multi-output vehicle +
screensData readback, O7 retired (#34); C-I3 addApplet+G1 (#29); C-I4
removeApplet+G3 drop-marker (#32); C-I6 golden bridge at Tolerance tier (#33);
C-I10 fakepointer key verb (#28). REMAINING SWARM: infra C-I5 (moveViewToScreen),
C-I7 (applet-reorder + z), C-I8 (task-reorder), C-I9 (widget-explorer DND, EXPECTED
DOABLE in the vehicle per Bree); then scenario chunks C-S1..C-S12 + abort chunks
C-A1..C-A8 (30 chunks total). Widget-explorer DND is not deferred - the old "GUI CI
vm" note was stale.

GATE SPLIT (2026-07-18): LATTE_GATE_FAST=1 skips the ~30-min asan-e2e-gate on a
swarm BRANCH gate (it exceeds a subagent's per-tool budget); the orchestrator runs
asan-e2e-gate at MERGE time on the rebased head before merging a dock-code PR.

DEFECTS REGISTRY (docs/tracking/known-defects.md, 2026-07-18): the flat found-bugs index
D1-D14; CLAUDE.md now requires filing there + PERIODIC governing-doc review. VOICE
(2026-07-18): impersonal, no first-person pronouns in committed content. D14 OPEN:
46 invalid-color qCriticals at startup (the colorBrightness guard is correct; a QML
site hands it an invalid QColor before the theme resolves) - fix in flight.

OPEN: Job C (panel matrix) SUBSUMED by the e2e suite - reconcile/close; UB Prong B2
(boundary sweep, seeded by A3's two vptr finds); the splitterPosition write-back
caveat (small follow-up); the maximize-length desk-check owed to Bree (dock is on
the fixed build now); the residue-detector nits (N1 unquoted glob in
matrix_surface_list, N2 unescaped MATRIX_VOLATILE_EXTRA regex) + the #28 fakepointer
nits + the #33/#34 first-person commit-body wording, to fold in at the next touch /
pre-PR cleanup.

## UB-catching initiative (parallel track, docs/tracking/ub-catching-plan.md)

Prong A COMPLETE with A3 (branch `ub-a3-sanitized-gate`, PR open, awaiting
review), building on the #21/#22 A1/A2/B1 landings above. A3 wires the sanitized
dock into a DRIVEN gate: `scripts/asan-e2e-gate.sh` seeds a hermetic config
(shared `scripts/lib-e2e-seed.sh`, extracted from ci/build-and-gate.sh) and
drives the sanitized dock through the no-input e2e core - 000-smoke, 060, and the
new shadow assertion `tests/e2e/070-asan-binary-shadow.sh` that proves from
/proc/<dock>/maps that the binary + containment plugin under test are build-asan
and libasan is mapped - wired as gate-all.sh's final stage. It caught two REAL
vptr UBs on its first run (destroyed()-slot decayed-vptr downcasts in
genericlayout.cpp + syncedlaunchers.cpp, fixed via reinterpret_cast). NEXT on
this track: Prong B2 (sweep the index/enum/pointer boundaries the gate surfaces),
and optionally add the wheel/edit recipes or a sanitized sceneprobe leg. Ledger:
docs/agent-logs/2026-07-18-a3-sanitized-gate.md.

## SIBLING BRANCH IN FLIGHT (2026-07-18)

`panel-fix-maximize-length-repaint` (PR open, awaiting independent review +
`gh pr merge --rebase`): fixes the Phase 8 maximize-length repaint (stale
frosted band when a masked dock shrinks off its maximized-window full width on
Qt6 wayland). New pure core `app/view/inputmaskflush.h` (keep the union across
a mask shrink, collapse once settled), `Effects` coalescing timer, a new
`viewsData` field `appliedInputRegionRects`, `inputmaskflushtest` (SIGABRT
tripwire on a naive setMask(band)), and `tests/e2e/070-maximize-length-mask.sh`.
Ledger + desk-check steps: `docs/agent-logs/2026-07-18-maximize-length-repaint.md`.
Plan item filed under Phase 8. DESK-CHECK owed to Bree: the "no frosted band"
pixel confirmation on the real feature (the vehicle cannot flip
existsWindowMaximized).

## NEXT SESSION ENTRY POINT (2026-07-17)

Invoke `docs/prompts/multi-distro-ci-orchestrator-prompt.md`. The session
drives as an ORCHESTRATOR: it owns the shared scaffold + serial merges and
FARMS chunks to capable Opus worktree subagents, merging each returned branch
through independent lean Opus review -> merge.
STATE @ master 1a01d0dee: PHASE A/B COMPLETE. All EIGHT distro legs build
565/565 and render sceneprobe 13/13 in-container: Arch, Debian (PR #11),
openSUSE (#12), Fedora (#14), KDE neon (#15), Void (#16), Gentoo (#17), plus
the NixOS bit-exact dev tier. B2 gate stage productionized (#13). ZERO port
source changes were needed across all of Wave A+B - every issue was a
per-distro dep-name/split or image-env trap, so the port is genuinely
distro-portable. Sceneprobe tiers: BIT-EXACT = Arch/Debian/openSUSE/Void/
Gentoo (LLVM 22, and Void's LLVM 21.1.7); TOLERANCE Δ=2 = Fedora (LLVM 21.1.8)
+ neon (LLVM 20) - so bit-exactness tracks the exact Mesa+LLVM build, not the
LLVM major. NEXT: Phase C (decouple render DEVICE from golden TIER in
tests/sceneprobe/main.cpp + imagecompare.h; wire bit-exact NixOS / bless-
frozen stable / invariant+tolerance rolling - this is a REAL code change, so a
real gate-all, not the --no-verify shortcut), then Phase D (CI workflow -
collect DECISION 6 runner + 2 cadence), Phase E (v0.20.0 - DECISION 5),
Phase F (packaging swarm deb/rpm/arch/gentoo/void - DECISION 7/8).

MERGE MECHANIC (Bree's direction, learned the hard way this session): land
each PR THROUGH GitHub as MERGED, never manual-close. Per leg after review:
rebase branch onto master (fix plan-tick hashes to post-rebase), push the
rebased branch to its PR, KEEP THE PR OPEN, ff-merge master, push master while
the PR is open -> GitHub auto-marks it MERGED (its head is in the base push).
Re-gate ONLY if the rebase changed PORT CODE or review asked for code changes;
a docs+Containerfile-only leg reuses the agent's green gate via --no-verify
(Bree-authorized for that scenario - port code unchanged & already gated).
CAUTION: the gate stamp (build/_gates-passed) is per-worktree and does NOT
travel with a merge; the cross-worktree nix flake race can stamp a foreign
sha (verify stamp==HEAD). OPEN: PRs #12/#13/#14 are CLOSED (mergedAt=null) from
an early batch-and-manual-close mistake - GitHub will NOT retroactively flip a
manually-closed PR to Merged (confirmed), their commits are on master under
rebased shas; Bree's call whether to accept Closed-with-comment. The execution
prompt (multi-distro-ci-execution-prompt.md) still holds the standing mission.

## 2026-07-17 multi-distro CI: PHASE A/B COMPLETE - 8 DISTROS (master 1a01d0dee)

Wave B (neon #15, void #16, gentoo #17) landed after Wave A + B2, each as a
proper MERGED PR via the correct process above. Highlights: neon needed 5
deb dev-names that diverge from BOTH Arch and Debian (KSvg = kf6-ksvg-dev, no
libkf6svg-dev); Void had no prior docker reference (first full xbps table)
and split the ASan/UBSan runtime into libsanitizer-devel; Gentoo is the
binhost leg - base gentoo/stage3:amd64-systemd (KDE binpkgs carry USE=systemd)
+ two mandatory source rebuilds (Mesa +video_cards_lavapipe for the ICD, kwin
+lock so kwin_wayland accepts --no-lockscreen), split-usr kwin at both
/usr/bin and /usr/sbin. Cross-cutting for Phase C/D: qmllintgate is a
NixOS-tier ratchet the distro gate excludes; Qt QML host tools sit off default
PATH on Fedora/openSUSE/Debian/neon/Void/Gentoo (bindir added to each image
PATH; a qtpaths6-aware gate script is a filed follow-up). Ledgers:
docs/agent-logs/2026-07-17-{neon,void,gentoo}-leg.md.

## 2026-07-17 multi-distro CI: WAVE A + B2 LANDED (master f4db9b558)

The orchestrator dispatched B2 + three Wave A legs in parallel (4-agent
cap), reviewed each with an independent cold-context lean Opus agent (all
MERGE), and landed them as one rebased batch behind a single gate-all
(GATES: ALL OK @ f4db9b558, exit 0). Contents, all bisectable on master:
- B2 gate productionization (PR #13): ci/build-and-gate.sh gate stage
  un-stubbed, distro-agnostic (fakepointer XML via pkg-config->ENV,
  LATTE_QML_MODULE_PATH asserted as a container contract, seed a default
  layout in a nested kwin, sceneprobe + e2e). Runs full green on Arch:
  build -> ctest 80/80 -> fakepointer -> sceneprobe 13/13 -> seed -> e2e
  6/6. Real defect fixed: EXIT 143 from the errexit-unsafe nested-kwin
  cleanup, `set +e` scoped to the seed subshell with explicit checks kept.
  Also landed the PR #9/#10 nits (run-staged $HOME guard, qml-interaction
  QMLDIR guard). e2e hard set = 6 recipes; 5 deferred+filed (konsole app_id
  resolves bare in a bare container -> 020/040/parabolic; empty launcher
  app_ids -> 050; duplicate-view-idremap = a libplasma removeView
  layout-flush divergence, a REAL bug to root-cause on Arch).
- Debian testing (PR #11): builds 565/565, sceneprobe bit-exact. Found +
  fixed a real libplasma VERSION-SKEW: askdestroysignalorderingtest was
  pinned to the 6.7 widened-guard contract, but the 6.5 floor + Debian's
  6.6.5 need the pre-6.7 shape - now gated on #if PLASMA_VERSION >= 6.7.0
  (NixOS 6.7.3 branch unchanged, merge gate untouched).
- openSUSE Tumbleweed (PR #12): builds 565/565, sceneprobe bit-exact, zero
  source change. Trap: qt6-base-private-devel split; Qt QML tooling off PATH.
- Fedora 43 (PR #14): builds 565/565, sceneprobe renders but Δ=2 TOLERANCE
  (LLVM 21.1.8) - the first tolerance-tier leg, the graduated-rigor proof.
  Traps: qt6-qtbase-private-devel + libasan/libubsan splits (build deps).
Cross-cutting for Phase C/D + Wave B: qmllintgate is a NixOS-tier ratchet
(distro gate excludes it); Qt QML host tools live off default PATH on
Fedora/openSUSE/Debian (bindir added to image PATH; a qtpaths6-aware gate
script is a follow-up); all distro sceneprobe on LLVM 22 stayed bit-exact,
LLVM 21 (Fedora) is Δ=2 -> Phase C must decouple render DEVICE from golden
TIER. Ledgers: docs/agent-logs/2026-07-17-{b2-gate-productionization,
debian-leg,opensuse-leg,fedora-leg}.md.

## 2026-07-17 multi-distro CI Phase B2: GATE STAGE PRODUCTIONIZED (branch multi-distro-ci-b2-gate)

ci/build-and-gate.sh's gate stage is un-stubbed and runs the full headless
gate in-container end-to-end GREEN (exit 0): build -> ctest (80/80, minus
qmllintgate + schemesmodeltest which cannot pass off the nix pin) ->
fakepointer -> sceneprobe (13/13) -> seed default layout -> e2e
environment-agnostic subset (6/6). Distro-agnostic: all per-distro paths
come from the Containerfile ENV (LATTE_QML_MODULE_PATH, LATTE_FAKEINPUT_
PROTOCOL) not the driver. Image gained imagemagick/konsole/python and
/usr/lib/qt6/bin on PATH (the `qml` runtime). Both PR-review nits folded
in ($HOME guard in run-staged.sh:85; qml-interaction reuse-guard uses
KDE_INSTALL_QMLDIR).

Two real traps root-caused: (1) the gate died EXIT 143 during seeding -
nested_kwin_cleanup ends with `wait $NESTED_KWIN_PID` (the kwin's 143) and
the seed subshell inherited the driver's set -e; the library is
errexit-unsafe by design, so the seed subshell runs `set +e`. (2)
LATTE_QML_MODULE_PATH in the image dropped the A2 ctest failures 8 -> 2.

FULL e2e characterization (docs/agent-logs/2026-07-17-b2-gate-
productionization.md): 6 PASS (the gate's hard set), 5 FAIL, all filed in
the plan B2 with root causes - the konsole cluster (020/040/parabolic) is
one app-db-resolution gap (konsole window app_id resolves to bare
"konsole" not "org.kde.konsole.desktop" in a bare container; the dock DOES
track the window, so it is resolution not availability), 050 needs a
launcher fixture (default template seeds empty-app_id launchers), and
duplicate-view is a libplasma removal-flush divergence (removeView never
reaches the layout file past the 120s undo window - confirmed not a load
flake). These are the remaining B2 work; the gate is green on the agnostic
core now. HANDOFF: branch pushed, gate-all green, awaiting orchestrator
review + ff-merge.

## 2026-07-17 multi-distro CI Phase B2: e2e VEHICLE RUNS IN-CONTAINER (branch multi-distro-ci-phase-b2)

000-smoke PASSES in the Arch container - the full nested e2e vehicle
works headless in podman: dock reaches lifecycleState running in ~2s,
kactivitymanagerd D-Bus-activates on the private bus (org.kde.
ActivityManager + org.kde.runners.activities), 1 view settles, clean
SIGTERM, relaunch settles. THE blocker was a real defect: run-staged.sh
referenced $USER (for an /etc/profiles/per-user/$USER XDG_DATA_DIRS
entry) under set -u, and a bare container doesn't export USER, so it
aborted "USER: unbound variable" before launching the dock. Fixed
09b6e69bc with ${USER:-$(id -un)}; the nix path entries are absent and
harmlessly skipped off NixOS. gate-all green @ 09b6e69bc (USER is set on
nix, so the resolved value is unchanged there).

Proven-by-hand recipe (scratchpad e2e-smoke-check.sh), to wire into
build-and-gate.sh's gate stage next: compile fakepointer (Arch has no
plasma-wayland-protocols.pc - resolve fake-input.xml at
/usr/share/plasma-wayland-protocols/), export LATTE_QML_MODULE_PATH=
/usr/lib/qt6/qml (belongs in each Containerfile ENV), seed E2E_CONFIG_BASE
via one run-staged self-init run in a nested kwin (dock writes a default
My Layout.layout.latte), then run-e2e.sh. STILL TO DO for a repeatable
gate: wire that into build-and-gate.sh (un-stub the gate stage), add
imagemagick to the image (screenshot recipes), run the FULL e2e suite to
characterize (only 000-smoke run so far). Landing PR #10 = the $USER fix +
this B2 proof; the driver wiring is the next chunk.

## 2026-07-17 multi-distro CI Phase B: ARCH RENDER HARNESS IN-CONTAINER (branch multi-distro-ci-phase-b)

Depth-first on Arch through Phase B (prove the riskiest unknown - nested
kwin + lavapipe in a container - before writing more distro legs). Branch
multi-distro-ci-phase-b off master bfb0fe5f2:
- B1 DONE: nested kwin_wayland comes up headless in podman. Blocker fixed:
  Arch's kwin_wayland has cap_sys_nice=ep and podman's default cap set
  lacks CAP_SYS_NICE, so execve failed EPERM; the image strips the cap
  (setcap -r, needs libcap). Also added rsync+perl for QML staging.
  Commit 79a8008f0.
- B3 DONE: all 13 sceneprobe scenes render + PASS in the Arch container,
  bit-exact against the nix lavapipe goldens (Arch llvmpipe 22.1.8 @
  256-bit matches nix Mesa). Enabler = a real portability fix (18aac31b0):
  the QML harness hardcoded nix's KDE_INSTALL_QMLDIR spelling (lib/qml);
  Arch uses lib/qt6/qml, so staged org.kde.latte.* modules were unfindable
  and 3 scenes failed "module not installed". Fix: top CMakeLists emits
  KDE_INSTALL_QMLDIR to build/latte-qmldir.txt, lib-qml-env.sh reads it.
  (Same defect class as the qmltypes qt-6/qml fix in PR #8.)
- Two set -e / ordering traps hit and fixed along the way (in the commit
  body): a `[[ -d ]] &&` as the function's last statement aborts the gate
  under set -e; qml_env_setup runs BEFORE qml_env_stage so the staged dir
  cannot be probed at setup time (hence the emitted-file approach).

B2 (e2e vehicle in-container) NOT STARTED - it's the next chunk: needs
python3 + imagemagick in the image, a seed layout config, and
kactivitymanagerd D-Bus activation in the container. schemesmodeltest
non-hermeticity (XDG_DATA_DIRS) also still open. Deferred to keep this PR
the clean "render harness runs in-container" unit.

MERGED via PR #9 (ff, master at 3de11200e; lean Opus review MERGE, one
nit filed under B2: qml-interaction-tests.sh:18 hardcodes lib/qml in its
staging-reuse guard, same class). NEXT: B2 (e2e vehicle in-container) to
finish the Arch end-to-end story - risky unknown is kactivitymanagerd
D-Bus activation in the container (the dock waits on the activities
consumer reaching Running).

## 2026-07-17 multi-distro CI Phase A: ARCH LEG PROVEN + MERGED (PR #8)

Started executing docs/prompts/multi-distro-ci-execution-prompt.md. Phase
A checkpoint on ONE distro (Arch) before scaling, per the prompt. Landed
via PR #8 (MERGED, ff, master now at cdb76af1e; lean Opus review returned
MERGE with two nits, both fixed on-branch: ead2518f3 re-resolve qmltypes
cache each configure, cdb76af1e author voice on the PR #26 refs). Branch
multi-distro-ci-phase-a off master bdd4f3e82:
- 00400f16c fix(tests): the two qmltypes contract lookups hardcoded
  nixpkgs' "qt-6/qml" subdir spelling, so configure aborted FATAL on
  Arch (which uses "qt6/qml"). Now a helper searches both spellings from
  libplasma's own libdir; verified the nix pin still resolves its
  qt-6/qml store paths. This is a REAL portability defect that blocked
  configuring the test suite on any non-nix distro.
- d68ad64c0 build(ci): ci/containers/Containerfile.arch (deps-only
  cacheable Arch layer) + ci/build-and-gate.sh (distro-agnostic
  build/test/gate driver; gate stage is a marked # STUB: for Phase B).

RESULT: the port builds clean (565/565) in the Arch container against Qt
6.11.1 / KF6 6.28 / Plasma 6.7.3; 74/82 offscreen ctest pass. The 8
failures are ALL harness-env, not port/build defects: 7 need the staged
QML tree + LATTE_QML_MODULE_PATH the nix devShell exports
(qmllint/qmlcompile/qmlcontracts/qmlinteraction gates, shortcutshost,
layoutmanagerparking, representationswitch); schemesmodeltest is
non-hermetic (reads ambient XDG_DATA_DIRS=/usr/share, finds real Breeze
schemes not its fixtures). Both classes are Phase B in-container
env-staging (filed in the plan's B2 item).

USEFUL PRIOR ART (my own latte-dock-ng PR #26, flagged mid-session): the
ng fork's docker/ pipeline has per-distro Dockerfiles for
arch/fedora/opensuse/ubuntu/debian/gentoo/mageia/nixos - a ready dep-NAME
matrix to adapt for the remaining Phase A legs (ours is a superset; ng
omits libksysguard/kpipewire/etc. and all render-gate deps). Also
package-arch.sh + verify-ebuild-gentoo.sh for Phase F, and verify-
install.sh's install-assertion pattern for "gate the installed package".
Its build is build+install-verify ONLY (no render gates) - our headless
tier is all-new. The PR's KF6 compat-shim finding (nixpkgs split-tree
include breakage) does NOT affect merged-tree distros (Arch/Fedora/
Debian), so our container legs are unaffected. NOTE: ng bakes in China
USTC mirrors (USE_MIRRORS=true default) + a Mageia leg - neither wanted.

NEXT: run gate-all.sh on the branch (the tests/CMakeLists.txt + ci/ files
are CODE, need the stamp before push) -> push -> PR + lean Opus review ->
ff-merge. Then Phase B (wire in-container nested-kwin + lavapipe, resolve
the two env-staging classes above) OR widen Phase A to fedora/opensuse/
neon/debian/gentoo/void using the ng dep matrix.

## 2026-07-17 NEW DIRECTION: multi-distro CI matrix for v0.20.0

Planned, not started. The next big initiative (Bree's call): take the
existing headless harness (nested kwin_wayland + lavapipe sceneprobe,
tests/e2e nested-vehicle recipes) to Arch/Fedora/Ubuntu-family Plasma 6
CONTAINERS with per-distro golden validation, as a fully automated
GitHub Actions matrix. Green across the matrix is the release gate for
v0.20.0 (first tagged continuation release; upstream stopped at v0.10.8,
tree is at CMakeLists VERSION 0.10.77; no GitHub Actions exist yet, only
the inherited .kde-ci.yml). Full architecture + phased A-E checklist +
six open DECISIONs in docs/tracking/multi-distro-ci-plan.md; pointer bullet in
PORTING_PLAN's Continuation-features section. podman (the project's
container runtime, not docker) is on the host, so Phase A (containerize +
build per distro) can prototype locally right now.

Key design call banked: cross-environment rendering is NOT bit-exact, so
goldens use graduated rigor per tier - bit-exact on the NixOS pin,
bless-frozen on pinned-tag stable distros, invariant+tolerance on rolling
(Arch). The probe already supports all three (imagecompare.h
checkInvariants + CompareTolerance; per-scene probeTolerance).

CROSS-ARCH (aarch64) golden verification is PARKED (findings in the plan
doc): forcing LP_NATIVE_VECTOR_WIDTH=128 crashes lavapipe on this x86
build (never set it), a local aarch64 guest needs 264 local aarch64
builds (blocked without binfmt or a real ARM box), and cross-arch won't
bit-match x86 anyway. The distro matrix is the higher-value path.

## 2026-07-17 SESSION FOUR: CaptSilver attribution audit (item 11)

## 2026-07-17 SESSION FOUR: CaptSilver attribution audit (item 11)

Drove the stabilization prompt's one remaining OPEN code-side thread,
item 11 (comprehensive CaptSilver attribution audit). Everything else on
the open list needs Bree's hands (Orca, cross-machine goldens, desk
checks) and is not code work. Branch captsilver-attribution-audit off
master 8bb23ee04, three commits:
- f2658c6d6 SPDX lines on 14 DERIVED files. Pass 3's full-tree scan found
  more than the four the external review named: activitysetalgebra.h
  (capt's namespace + signatures verbatim), iconsourceclassifier,
  panelbackgroundscan, windowtrackingpredicates, extraviewhints, and all
  five sceneprobe files. backend.{h,cpp} deliberately gets none (Eike
  Hein's code; only capt's vendoring decision was followed).
- 90b6d52e9 expanded every "capt" shorthand to full David Goree /
  latte-dock-qt6 / URL / per-file commit / source path / what-taken; the
  plan doc's Posture paragraph now defines "capt" once for the whole file.
- da933136d README credit split out for David Goree with the concrete
  contributions (sceneprobe harness, TESTING.md model, seven EX units,
  transplanted suites).
Verification pass clean: every DERIVED file has his line; the five that
name him without one are divergence/contrast notes (capt bugs we avoided:
WindowsGoBelow->LayerBottom, unresolved ShadowedItem) plus the colortools
data tags - all correctly exempt. Ledger
docs/agent-logs/2026-07-17-captsilver-attribution-audit.md. gate-all run
for the stamp (comment/SPDX/doc-only, no code moved). Opened as PR #7;
lean independent Sonnet review returned MERGE (no blockers; two cosmetic
nits, one fixed on-branch as 8949b41e2 + re-gate, one left as internal-log
prose). REMAINING: ff-merge on the MERGE verdict, master push, prune.

Note: master carried a pre-existing uncommitted CLAUDE.md edit (a
"Current status" meta-comment removal) from before this session - left
untouched, not part of this PR.

## 2026-07-17 POST-CLOSE ADDENDUM (master at e089d3d6d)

Two more features landed after the session-three close below, both via
the PR flow: PR #4 the e2e geometry-agreement guard (state-vs-render
check + the `# e2e-expect: fail` xfail marker; `060` was XFAIL against
the Phase 8 drift), and PR #5 the PHASE 8 SURFACE DRIFT FIX itself
(Opus agent, independent Sonnet review). Root cause: a masked dock
keeps a full-screen-width window and realises alignment via its mask,
but a Center dock was anchored to a SINGLE edge, so wlr-layer-shell
re-centred it inside the band other docks' exclusive zones leave free -
drift = (leftZone-rightZone)/2, -20px with Bree's 48/88 side docks.
Fix: horizontal masked docks anchor BOTH length edges (3de174290);
the review caught a gap (reanchor not wired to behaveAsPlasmaPanelChanged,
so a Panel->Dock toggle at Center alignment could strand the old
anchor) - fixed on the branch (e089d3d6d) before merge. `060` is now a
permanent hard-passing guard (marker removed). Verified live on the
desk: both bottom docks render at drift 0 on the canonical build, views
healthy. The desk currently has no side docks so it was already drift 0
(the fix's benefit shows with side docks); no regression. One live-read
gotcha banked: the SubWindow edge-helper strip renders 1px wide in its
normal ISHIDDENMASK state, so a startup dumpwins can catch it at full
width mid-transition - not a defect. parabolic-hover-preview was flaky in
the vehicle (~60% single-shot); root-caused 2026-07-17 as a synthetic-
injection artifact, NOT a dock bug - the preview trigger (task MouseArea
onEntered) is suppressed while the zoom animation runs (hoverEnabled gates
off, Qt5-faithful), so a single fakepointer glide races the animation and
fires the trigger only ~60% of the time, while a real continuously-moving
mouse always re-enters and previews are reliable live. Fixed by re-gliding
onto the icon until the dialog maps (assertion still requires a real
layer=6 dialog); 12/12 back-to-back. Ledger
docs/agent-logs/2026-07-17-preview-recipe-flake.md, TESTING.md updated.
Real dock runs the merged e089d3d6d build.

## 2026-07-17 SESSION THREE CLOSE (master at ed62ce333, all pushed)

The whole open list from the stabilization prompt is landed. Master
moved e2db89a25 -> ed62ce333 over the session, every feature through
the branch -> gate -> lean Sonnet review -> merge flow. In order:
re-pin (item 0), X11 removal (PR #1, the ONLY GitHub-rebase-merge -
it rewrote hashes and cost a re-resolution, which is why ff-merge is
now the documented default), restart-sweep fix, color-group audit,
P3b (14 suites, ctest to 81), wayland-id sweep, keyboard focus mode
(the a11y P0 gate, ctest to 82), Accessible.* rollout, e2e vehicle
promotion + EX-14/15/17 (PR #2), and the CaptSilver attribution +
process rules (PR #3). Real dock verified running on the final build
(2 views, keyboardNavigation readback false by default).

PROCESS additions this session, all in CLAUDE.md now: feature ->
branch -> real GitHub PR (`nix run nixpkgs#gh --`) -> lean Sonnet
review (diff-read only, no independent rebuild - the branch gate
stamp proves gates) -> ff-merge (NOT GitHub rebase-merge, which
rewrites shas). Transplanted code cites the exact fork commit
(81384003 for CaptSilver) plus the original author's SPDX line when
derived.

OWED, needs Bree's hands (none block the code): the Meta+Alt+D
keyboard-nav desk pass + Orca screen-reader pass
(docs/reference/manual-flake-removal-testing.md), the byPassWM retire/keep
decision (Phase 4 item), the Phase 8 bottom-dock surface-drift
root-cause (it is the e2e suite's sequence-flakiness source - recipes
pass solo), the two keyboard-focus follow-ups (denied-activation flag
staleness, cross-view exclusivity), the nixos-upgrade-timer story,
and the e2e audio-badge recipe (needs pipewire). Full desk checklist
in docs/reference/manual-flake-removal-testing.md.

## 2026-07-17: stabilization execution session THREE (running record)

ACCESSIBLE ROLLOUT LANDED (branch accessible-rollout, 6 feat commits +
docs, worktree hashes 8e709d26a..HEAD - retick at merge): the
accessibility quartet's third item. Accessible role/name/description +
press-action-forwarding-to-the-click-handler on: task items (badge
values via a new TooltipTextComposer accessible-description composer,
core+wrapper per the EX-17 split), the audio badge (checkable Mute,
context-menu msgid), applet containers (toggleExpanded() factored
once for Meta+number / neutral-area / AT press), HeaderSwitch +
ComboBoxButton ghosts, edit-mode canvas chips + ConfigOverlay handle
buttons (names reuse the TextMetrics hint strings), widget-explorer
cards (press = the tap's addWidget(); delegate moved to
required-properties mode). Accessible.focused mirrors the keyboard
focus mode. Pins: tst_taskaccessible, tst_accessiblecontrols,
tst_addwidgetsaccessible + sanitized composer vectors. Two qmllint
baseline shrinks landed (AppletItem 158->155, AppletDelegate 39->33).
Deferred with reasons (ledger
docs/agent-logs/2026-07-17-accessible-rollout.md): previews dialog
(P1 focus rework), config-page per-control labels (P3), ScrollArea /
Keys.onReturn (keyboard item). Desk pass owed: the Orca script in
docs/reference/manual-flake-removal-testing.md. Plan item stays UNTICKED until
the Orca pass. NOT pushed.

KEYBOARD FOCUS MODE LANDED (branch keyboard-focus-mode, 7 commits,
worktree hashes 634ae2083..920e84ff9 - retick at merge): the
accessibility quartet's P0 gate plus the owed shortcuts-host pin.
View::enter/exit/toggleKeyboardNavigation (layer-shell OnDemand,
three bulletproof exits: Escape / toggle / focus loss via
activeChanged), Meta+Alt+D kglobalaccel toggle,
setViewKeyboardNavigation D-Bus action + viewsData keyboardNavigation
readback (docs triple updated), VisibleIndex slot math
(countVisibleSlots/steppedVisibleSlot), containment
KeyboardNavigationHandler traversing the Meta+number entry space with
activateEntryAtIndex activation and indicator-hover focus highlight +
position badges. Evidence: tests/e2e/keyboard-navigation-mode.sh 8/8
in the nested vehicle INCLUDING the focus-loss self-exit;
shortcutshosttest pins the discovery walk + all four QML signatures
against the REAL qrc-aliased ability files (negative-tested; found
and pinned the stale-.qmlc-masks-qrc-drift trap - plan item filed to
audit the other qrc harnesses). Desk pass owed: real-keyboard
traversal section in docs/reference/manual-flake-removal-testing.md. Ledger:
docs/agent-logs/2026-07-17-keyboard-focus-mode.md. Vehicle traps
re-proven: forgetting dbus-run-session reaches the DESK dock;
konsole cold-start fakes focus-loss failures (qml window is the
committed focus-taker); default GL stalls startup (lavapipe env
required).

Item 0 (the re-pin) first, per the incident entry below. System state
found at session start: /run/current-system runs nixpkgs
26.11.20260715.753cc8a (NOT the incident entry's e7a3ca8 - the machine
was rebuilt again, as the entry predicted); full rev
753cc8a3a87467296ddd1fa93f0cc3e81120ee46 confirmed as the root nixpkgs
node of /persist/etc/nixos/flake.lock. Re-pinned flake.nix + lock to
exactly that; the new pin carries Plasma 6.7.3 / Qt 6.11.1 (was 6.6.5 /
6.11.0 - a real minor-version bump, so the expected sceneprobe re-bless
has a mechanism: Mesa AND the whole KDE stack moved). Throwaway layout
state preserved before the --fresh wipe (scratchpad _runconfig-backup;
restore build/_runconfig from it after the rebuild). Lockstep guard
added to gate-all.sh (compares flake.lock's nixpkgs rev against
/run/current-system's store-name suffix, exit 4 + re-pin recipe on
mismatch; probed positive and negative standalone).

NEW DIRECTION from Bree this session: X11 support is REMOVED, folded
into the stabilization order as item 7a. Researched timelines first:
KDE's "Going all-in on a Wayland future" (blogs.kde.org, 2025-11-26) -
Plasma 6.7 is the final X11-session release, 6.8 (October 2026) is
Wayland-exclusive, 6.7 X11 supported only into early 2027. Phase 4's
section in docs/tracking/PORTING_PLAN.md carries the recorded decision + the
7-item removal checklist; the stabilization prompt has the execution
order (source sites first, build system second, audit + docs last).

WORKFLOW CHANGE (from Bree, mid-session): feature branches + PRs from
now on, rebase/ff-merge only (never squash - the small-commit
bisection tool survives), gates green on the branch head before the
PR. Recorded in CLAUDE.md working agreements and the prompt. The
in-flight X11 work moved onto the x11-removal branch; master was
reset to origin. Also from Bree during X11 review: the non-wayland
refusal is default-deny (not an isPlatformX11 check - a hypothetical
third platform must refuse too), with offscreen as the one named
harness exception; the Xwayland prose came out of the refusal
message.

PR #1 MERGED 2026-07-17 (rebase-merge, post-rebase hashes
20a3c2506..3f857fbff re-resolved everywhere per the tick-at-merge
rule). Process notes now standing: PR reviews are LEAN by Bree's
direction (sonnet, concise diff-read prompt, no independent
build/ctest - the branch stamp already proves gates; the first
heavyweight reviewer was stopped mid-run to save tokens, its two
early leads were real and fixed: stale plugins.qmltypes + stale
skill/CLAUDE.md wording, commit 6545e05a9 with the import-path trap
in the body). The lean reviewer's verdict was MERGE with two
non-blocking leftovers, filed as a Phase 4 follow-up item. Real dock
runs the merged build. gh CLI: not installed, use `nix run
nixpkgs#gh --`, auth lives in the keyring and works.

X11 REMOVAL EXECUTED (branch x11-removal, 7 commits + docs; Phase 4
checklist all ticked except the byPassWM decision item): backend
removal 20a3c2506, non-wayland refusal 1ddb4140f, ifdef strips
9b41dd3ae, runtime isPlatformX11 branches 24e54b7d8 (~40 sites,
per-site audit in the body), Effects visual-mask machinery collapse
019c3aed3 (updateMask/forceMaskRedraw/maskCombinedRegion + the
caller-less subtracted/united region cluster; the mask PROPERTY
stays - QML reads it and ISHIDDENMASK stamps through it), build
system 3f857fbff (WITH_X11 gone, build-check single-tree: every gate
run ~2x faster). qmllint baseline SHRANK (VisibilityManager.qml
164->159). NEW DECISION ITEM for me: byPassWM is X11-only machinery
but a Qt5-faithful persisted setting with config UI - retiring it is
a product call, filed in Phase 4.

MERGE QUEUE STATE (running): color-group-audit MERGED (verdict
inventory + 2 fixes, b84c1f8ff/c2a0d718c; its missed ratchet-baseline
entry caught by the merge gate, fixed on master). p3b-transplants
MERGED (14 suites, ctest 81 entries, 4 upstream-inherited fixes
9178957fd/6aac18f32/aa2ab9555/9838e9cad; the rebase auto-merge kept a
stale ratchet count line - caught pre-gate, the union rule again).
wayland-id-sweep MERGED (ed9416a21 - both PR #1 review leftovers
closed; NEW plan item: the View-side activity-stop reshow hack needs
an activity-close reproduction before removal, its frame-extents
claim may be true on wayland KWin). STILL OUT: keyboard-focus-mode
agent, e2e-promotion agent (EX-14/15/17 recipes ride with it);
Accessible.* rollout queued behind the focus-mode landing (same QML
delegates). Load lesson: gate ctest timeouts under concurrent agent
builds are LOAD, not defects - verify solo before touching anything
(qmlcontracts timed out at 1528s total under four agents, passed in
0.87s solo).

ITEM 0 COMPLETE (all hashes in the Phase 11 plan item): re-pin
c147fbbdb, askDestroy contract repin 24dc3ee39 (libplasma 6.7 widened
the containment-type guard to containment()!=q - parking machinery
survives by construction, idempotence analysis in the commit body),
VUID-12384 suppression 250096280 (Qt RHI qrhivulkan.cpp:861 device
layers, new validation layers flag it), lockstep guard fa2721d2f,
restart-staged packaged-wrapper sweep fix 70e1ae9aa (the
module-autostarted package's comm is ".latte-dock-wra" - the old -x
latte-dock sweep left it holding the KDBusService name and the staged
dock exited silently; caught at the first post-autostart restart).
Sceneprobe 13/13 against COMMITTED goldens - no re-bless needed,
lavapipe stayed byte-identical across the Mesa/LLVM move. Nested
vehicle verified end-to-end on the fresh pin (running + 5 views
settled + clean SIGTERM trail over the private bus). Real dock on the
staged fresh build, 2 views settled == the on-disk layout's 2 latte
containments (the historical "6 views" notes described an older
layout era). Gates were re-run through gate-all after the final
commit; stamp caught up to HEAD and pushed WITH the pre-push hook
(no --no-verify needed anymore).
PHASE 8 IS OPEN - read its section in docs/tracking/PORTING_PLAN.md first; every item
is current there, several sections below are now RESOLVED and kept only as
archaeology.

## 2026-07-17: Phase 9 color-group audit (worktree color-group-audit)

Ran in a parallel worktree; merges may re-resolve the hashes below.
All three Phase 9 audit checklist items executed and ticked (verdicts
inline in the plan, full 160-line inventory in
docs/agent-logs/2026-07-17-color-group-audit.md). Two defects fixed:
the configure-mode popup collapse loop was dead since the port
(Plasma 6 Containment::applets returns Applets, `expanded` lives on
the graphic item; rerouted through deactivateApplets, premises pinned
by appletsexpandedpropertytest) - b84c1f8ff; the coloredView bare
themeExtended deref (the one unguarded consumer) now compares against
colorizerManager.plasmaTheme - c2a0d718c. The Header-group item is
CLOSED as a recorded decision: Latte paints its own panel from
plasma-theme SVGs / the colorizer palette, so panel chrome pairs with
that source (Qt5's own behavior), never [Colors:Header]. Owed desk
checks (also in the ledger): popup collapse on entering configure
mode; three SUSPECT mixed-theme sites (fallback LatteIndicator dots,
TaskIcon icon-color fallbacks, AppletAlternatives text over the
plasma dialog SVG); confirm org.kde.desktop is the active Kirigami
platform plugin; open Show Alternatives once (attached-property
resolution on a foreign applet item). Gates: 67/67 ctest, qmllint
baseline matched.

## 2026-07-17 ~01:00: INCIDENT - system rebuilt under the pinned env; sceneprobe gate down

READ THIS BEFORE RUNNING ANY GATE. The machine was nixos-rebuilt at
23:33 on 2026-07-16 (generation 32, nixpkgs 26.11.20260616 ->
26.11.20260711 / e7a3ca8) while the session and the flake pin stayed
on the old generation. Since then every NEW nested kwin_wayland
crashes at QApplication init (SIGSEGV, null call in the theme-change
path right after reading kdeglobals; cores in coredumpctl), so the
sceneprobe gate reports GATE BROKEN (its selftest-good cannot get a
compositor) and gate-all cannot go green. Eliminated by direct
bisection: X-client saturation (fixed separately, see below), orphan
process load, kwin config, private XDG_CONFIG_HOME, fresh
XDG_CACHE_HOME, pinned-only XDG_DATA_DIRS, forced pinned-Mesa
LD_LIBRARY_PATH/LIBGL/EGL dirs. The correlation that stands: the
23:33 rebuild is the only system-level change in the breakage window,
and only NEW processes on the kwin-QPA path die (the session
compositor, her dock, and pinned QtGui apps are fine; the staged dock
still initializes offscreen). THE FIX IS STRUCTURAL, next session's
FIRST block: re-pin the flake to the system's new nixpkgs (e7a3ca8)
per the flake's own match-the-desktop doctrine, full rebuild, re-run
everything, expect a sceneprobe golden re-bless (Mesa moved), and
verify the nested vehicle end to end. Consider rebooting into
generation 32 first so the session and system agree. Until then the
sceneprobe gate is DOWN for environmental reasons - the final push of
2026-07-17 went out with --no-verify after build-check + full ctest +
ratchet + qmllint ran green on the exact tree, the incident recorded
here as the required explanation.

Same night, found while root-causing (all fixed and committed): the
nested harness leaked a full xdg-desktop-portal + ksecretd set per
run with session-X connections that saturated the X client limit
(fix: nested sessions strip DISPLAY/XAUTHORITY), and killing the
dbus-run-session pid orphaned kwin children - 315 virtual
compositors were alive (fix: setsid + process-group kill in cleanup;
the 315 swept by hand). STILL OWED, needs me or my approval: the
~790 already-orphaned portal-family processes from before the fix
(automation correctly refused a name-scan kill of session-adjacent
services): ps -eo pid,etimes,comm | awk '$3 ~
/xdg-desktop-por|xdg-document-po|xdg-permission-|ksecretd-wrapp/ &&
$2 < 30000 {print $1}' | xargs -r kill - or a logout/login clears
them. RESOLVED at the desk: the 23:33 rebuild was Bree's own, deliberate.
nixos-upgrade.timer also exists (04:00 daily) - the lockstep guard in
the prompt's new item 0 is what makes either kind of rebuild loud
instead of mysterious.

PRE-SESSION CHECKLIST handed to Bree (2026-07-17, she plans to
rebuild and reboot before the next session):
1. Rebuild + reboot as planned - and `nix flake update latte-dock` in
   /persist/etc/nixos FIRST, now REQUIRED: megakill sets
   programs.latte-dock.autostart = true (committed in her repo), an
   option that only exists from 5525c45c8 - rebuilding on the stale
   input fails evaluation with an unknown-option error. Any nixpkgs
   revision is fine - the
   session re-pins to whatever /run/current-system actually runs at
   its start, not to a hash from these notes.
2. The reboot itself clears the ~790 orphaned portal processes and
   every other leak from tonight - no manual sweep needed anymore.
3. AUTOSTART RESOLVED (superseded; corrected 2026-07-17 after reading
   /persist/etc/nixos): Bree removed latte-dock-ng from the system
   (package-ID collision, her config comment has the story) and wired
   lattecotta in properly - the system flake takes
   github:whimbree/lattecotta-dock as an input and enables our
   nixosModules.default (programs.latte-dock.enable). The stale
   user-level autostart file that still pointed at the REMOVED ng
   store path was retired (renamed to .removed-ng.bak in
   ~/.config/autostart). Two facts that follow: (a) our module
   installs NO autostart entry, so post-reboot login brings up NO
   dock until one is started by hand (packaged `latte-dock` is on
   PATH, or scripts/start-dock.sh for the dev build after item 0) - a
   `programs.latte-dock.autostart` module option is a natural small
   feature for the re-pin session; (b) the module builds the package
   with the HOST's pkgs, so the PACKAGED dock is automatically in
   lockstep with the system at every rebuild - only the dev flake pin
   needs item 0. Also: system.autoUpgrade updates only the nixpkgs
   input, so the system's lattecotta SOURCE stays at whatever master
   the 23:33 lock captured; `nix flake update latte-dock` in
   /persist/etc/nixos at her next rebuild picks up tonight's fixes.
   And the machine-readable pin source for item 0's guard:
   /persist/etc/nixos/flake.lock (or /run/current-system's
   version-string short rev).
4. Expect the staged dock NOT to be trustworthy until item 0
   completes (old pinned build, new system substrate - the exact
   incident class); do not fight it manually, let the session land
   the re-pin first.
5. Decide the standing upgrade story when convenient: if
   nixos-upgrade.timer keeps auto-rebuilding at 04:00, the pin
   lockstep breaks at every upgrade; the item-0 guard makes it loud,
   but the flow (upgrade -> re-pin -> gate -> re-bless) stays manual
   until scripted.
6. The reboot doubles as the real logout/login check from the manual
   list - the session should read the journal for the dock's SIGTERM
   line and clean exit as its first observability read.

## 2026-07-16: stabilization execution session TWO (running record)

Driving docs/archive/stabilization-execution-prompt.md over the open
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

DEFECT ROOT-CAUSED AND CLOSED (the invalid-color hunt, full story):
the V4 caller trace (temporary Qt6::QmlPrivate instrumentation in
tools.cpp, built in build-probe/, since removed) named ~10 distinct
QML sites all receiving invalid colors at view creation - colorizer
Manager (all four brightness sites), AddItem, the default indicator,
config chrome. The simplest site (AddItem.qml:32 reading
Kirigami.Theme.backgroundColor directly) proves the source: the
Kirigami attached PlatformTheme serves default-constructed invalid
QColors on the FIRST evaluation of creation-time bindings, then its
change notify recomputes every consumer with real colors - a benign
self-correcting transient, documented at the tools.cpp boundary.
THE REAL DEFECT was ours: the "throwaway-only" framing was an
artifact - main.cpp's no-debug message handler swallowed EVERYTHING
including Criticals, so the real config produced the same refusals
silently (proven: 35 criticals on a real-config -d restart).
FIXED: production handler now passes Critical/Fatal to stderr,
verified live (34 criticals visible on a no-flag restart that was
previously silent; Qt still aborts after the handler on Fatal).
Repro recipe that made this cheap, now banked: the staged dock runs
fully isolated in the sceneprobe nested kwin -
`nix develop -c tests/sceneprobe/run_in_kwin.sh dbus-run-session --
env LATTE_CONFIG_HOME=<copy> timeout 45 scripts/run-staged.sh -d`
(the dock inherits the caller's bus and exits silently on the
KDBusService unique name without the dbus-run-session wrap; without
-d it used to print nothing at all). BUILD=<dir> redirects
run-staged to an alternate build tree for instrumented binaries.

SESSION TWO CLOSE: all four agent branches merged serially with full
gates and pushed; master at the WindowId container-contract repair.
Final state: ctest 66 entries, ratchet OK, qmllint baseline matched,
build-check both variants OK, sceneprobe 13/13, all worktrees pruned,
real dock running --user-config on the final binary (with -d, from
the last verification restart - harmless, drop it at the next
natural restart). EX-10 MATRIX RAN desk-independent in the nested
vehicle: 10/11 PASS (both dodges with trackerData cross-check,
windows-go-below, auto-hide hide + fall-through + edge-strip
readback; edge-reveal verified by a focused probe - the batch glide
arm is flaky harness choreography, noted in the ledger). VEHICLE v2
LESSON, hard-won: kwin and the probes must share ONE private bus
(dbus-run-session wrapping BOTH) - v1 gave them separate buses and
every KWin-scripting mutation silently no-op'd, faking three dodge
failures; and a kwin_js helper must check loadScript's return (mine
swallowed it - the banned shape, in my own tooling). PROCESS SCAR:
one gate verdict was misread from a tail -1 and a broken master got
pushed for ~20 minutes before the repair (98e7b03aa); gate verdicts
are now grepped for their literal OK marker. REMAINING for next
session: EX-15 wheels / EX-17 hover / EX-14 drags can run in the
nested vehicle (config flips + choreography, recipes in the ledger
notes); EX-12 palette flip wants the real config at an idle desk;
EX-19 visual checks + the desk items in
docs/reference/manual-flake-removal-testing.md; Phase 9 color-group audit;
quartet items: keyboard focus mode (P0), Accessible rollout, e2e
conversion (item c - fold scripts/run-e2e.sh, still untracked, plus
the nested-matrix scripts into tests/e2e as P4 lands).

SECOND DEFECT ROOT-CAUSED (found by the activateTaskAt busctl smoke
the D-Bus merge owed): identifyShortcutsHost still walked the Plasma
5 tree (find a CHILD named containmentViewLayout, then look inside)
but on Plasma 6 itemForApplet returns the containment root which IS
containmentViewLayout - so the shortcuts host never resolved on any
latte view and activate-entry/new-instance/shortcut-badges/applet-id
were ALL silently dead (the invalid-method early-return is the exact
silent shape CLAUDE.md bans; it now warns loudly). Meta+number only
looked alive through fallbacks. Fix mirrors Panel.qml's viewLayout
discovery (root first, child scan kept). Verified in the nested
vehicle: activateTaskAt entry 5 on the active konsole toggled it -
real Meta+N semantics; live invokes stopped refusing (desk was in
active use, so live model asserts were confounded and abandoned).
An offscreen pin test is OWED, filed in the keyboard-nav plan item.
METHOD note: the temporary child-objectName probe went into
build-probe only; her running dock never executed instrumented code.

NESTED-MATRIX VEHICLE PROVEN (major unlock, prototype in the session
scratchpad as nested-matrix-proto.sh - promote it into scripts/ when
the EX-10 matrix lands): inside a nested kwin_wayland --virtual
(1600x1000) with a private XDG_RUNTIME_DIR and dbus-run-session, all
four pieces work at once - the staged dock reaches lifecycleState
running, viewsData reports settled views, fakepointer injects into
the nested compositor WITHOUT the desktop-file allowlist
(KWIN_WAYLAND_NO_PERMISSION_CHECKS=1), and a real konsole client runs
inside for dodge-against-window tests. The EX-10 visibility/input
matrix therefore no longer needs the real desk at all; with
setViewVisibilityMode (D-Bus step 3, merging) the mode flips need no
config surgery either. This is also the P4 e2e architecture running
for real.

OLD RECORD of the investigation start (kept for the method): ~180x
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

Drove docs/archive/stabilization-execution-prompt.md top to bottom.

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
D-Bus interface design is WRITTEN (docs/reference/dbus-observability-interface.md)
and item 9 became the CaptSilver adoption plan
(docs/archive/captsilver-testability-adoption.md). FOUR worktree/read agents
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
   match. Linked-dock design prerequisites both discharged. The
   Create Linked Dock… continuation feature is unblocked but remains
   unimplemented. All gates
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
   committed and pushed - docs/tracking/QML_EXTRACTION_PLAN.md (bab18b2c,
   2fb1bd27, f017854c, e554bf04 + closing commit), with the Phase 10
   cross-reference item in docs/tracking/PORTING_PLAN.md. All 239 package QML
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
  the maintained-continuation reality (docs/reference/taskmanager-integration-research.md
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
