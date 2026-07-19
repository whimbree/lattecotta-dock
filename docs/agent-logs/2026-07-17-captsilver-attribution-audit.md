# 2026-07-17 CaptSilver attribution audit (stabilization item 11)

Branch: `captsilver-attribution-audit` off master 8bb23ee04.
Ran inline in the orchestrating session (not a worktree subagent) - the
audit is one coherent read of the whole tree against David Goree's
latte-dock-qt6, better done by a single careful reader than fanned out.

## Scope

Stabilization-execution-prompt item 11: complete the CaptSilver
attribution the first sweep left partial. David Goree
(github.com/CaptSilver/latte-dock-qt6, davidgoree2003@gmail.com) is owed
an SPDX-FileCopyrightText line on every file derived from his work, and
every citation must be stranger-readable (full name, URL, per-file
commit, source path, what was taken) per the CLAUDE.md
"CITE TRANSPLANTED CODE PROPERLY" policy.

## Method

Full-tree grep for derivation markers (`capt`, `latte-dock-qt6`,
`CaptSilver`, `blueprint`, `transplant`, `adopted from`, `ported from`),
filtering false positives (`capture`/`caption`, and the edit-mode grid
image `blueprint.jpg` / `editBlueprint` / "blueprint margin" which have
nothing to do with capt). Every real match classified against the
policy's DERIVED (his expression adapted -> SPDX line + citation) vs
IDEA-only (his concept, our expression -> citation, no copyright claim)
vs divergence/contrast (documenting a capt mistake we avoided -> no
attribution owed). Per-file commits and exact source paths were read out
of `~/Projects/latte-dock-qt6` with `git log --diff-filter=A` and
`git ls-tree` at each introducing commit (all confirmed ancestors of the
reviewed-through 81384003).

## Commit 1 (f2658c6d6) - missing SPDX lines, 14 DERIVED files

These self-documented derivation from his work but lacked his SPDX line.
Line added ABOVE Bree's per policy; every existing line kept.

Pure-core headers (his extraction seam / struct interface / restructure
adopted, bodies re-derived from our Qt5-inherited sources):
  app/view/positionergeometry.h            capt 4a829185
  app/screengeometrycalculator.h           capt 6108a3bc
  app/layouts/storageidremapper.h          capt 73f64383
  app/layouts/activitysetalgebra.h         capt 941bb7fb (identical
      Latte::Layouts::ActivitySetAlgebra namespace + signatures - the old
      "following the same extraction" comment undersold a structurally
      identical header)
  declarativeimports/core/units/iconsourceclassifier.h  capt ad74a34a
  app/plasma/extended/panelbackgroundscan.h capt 15a317ff
  app/wm/windowtrackingpredicates.h        capt 9fba82c8
  app/wm/tracker/extraviewhints.h          capt 5b58c0a3
Test with his cases ported:
  tests/units/activitysetalgebratest.cpp   capt 941bb7fb
Sceneprobe harness (adopted near-verbatim, "unchanged"):
  tests/sceneprobe/main.cpp                capt 52c11b36
  tests/sceneprobe/imgdiff_main.cpp        capt cfb516e7
  tests/sceneprobe/imagecompare.h/.cpp/imagecompare_test.cpp  capt c3633f1a

Deliberately NO David Goree SPDX line:
  plasmoid/plugin/backend.{h,cpp} - the code is Eike Hein's plasma-desktop
  backend, vendored FOLLOWING capt's shape (a decision/idea, not his
  expression); claiming his copyright over Eike Hein's code would be as
  wrong as erasing it. Citation improved instead.

## Commit 2 (90b6d52e9) - stranger-readable citations

Expanded every "capt" shorthand comment to name David Goree's
latte-dock-qt6 in full with URL, per-file commit and source path, and
what was taken. Replaced the blanket 81384003 with the honest per-file
commit where one exists (screengeometrycalculator 6108a3bc, the
sceneprobe files, etc.). The eight derived headers, their test companions
(panelbackgroundscantest, storageidremappertest, iconsourceclassifiertest
- which already had SPDX lines but "capt" shorthand comments), the
sceneprobe files, colortoolstest, sourceguardtest,
popupplacementcontracttest, and importerregressiontest all touched. Later
same-file mentions use "Goree's" per the policy's shorthand allowance.

Idea-only, no copyright claim, said so explicitly: colortools folds his
case SHAPES but expected values come from our own ColorizerTools.js;
popupplacement took the enum-pinning IDEA but parses our own declaration.

docs/tracking/QML_EXTRACTION_PLAN.md: the Posture paragraph now names David Goree +
URL and defines "capt" as document-wide shorthand for that repo, with each
unit section recording the specific commit and source file - so the ~90
downstream "capt" mentions resolve to an exact source instead of undefined
jargon (single-source definition rather than churning 90 lines).

## Commit 3 (da933136d) - README credit

The Credits entry bundled CaptSilver with ruizhi-lab under
"independently-found fixes". David Goree now has his own line naming the
concrete contributions: sceneprobe harness adopted, docs/reference/TESTING.md
modeled on his testing commits, seven QML-extraction units (EX-07/08/09/
22/23/24/25) built on his seams, several test suites transplanted with his
cases. Michail, Varlesh and ruizhi-lab credits unchanged.

## Verification pass

The prompt's audit grep run tree-wide. Every DERIVED file carries his SPDX
line. Five files reference him WITHOUT one, all correctly exempt:
  - app/wm/waylandlayershell.cpp, tests/layershellmappingtest.cpp,
    tests/qml/tst_shadoweditem.qml: divergence/contrast notes documenting
    capt BUGS we avoided (WindowsGoBelow->LayerBottom mismapping,
    unresolved ShadowedItem type) - not derivations, no attribution owed;
    each already names latte-dock-qt6 clearly.
  - tests/qml/tst_colortools.qml: only the "capt-mixed"/"capt-purple"
    QTest data-row tags, idea-only (values are ours); the unit header's
    cross-reference explains their provenance.
The ~18 already-transplanted test suites (schemesmodeltest, coretoolstest,
...) were NOT churned: they already carry his SPDX line AND a
stranger-followable citation (URL + commit + path) and never used the bare
"capt" shorthand this pass targets.

## Not mine, left alone

CLAUDE.md carried a pre-existing uncommitted working-tree edit (a
"Current status" meta-comment removal) present before this session; left
unstaged, not part of this PR.

## Gates / merge

Comment/SPDX/doc-only change, no compilable code moved. gate-all.sh green
and stamped (the .cpp/.h touches make it a code push in the pre-push
hook's eyes). Opened as PR #7. Lean independent Sonnet review (cold
context, diff-read only) returned VERDICT: MERGE, no blockers: cardinal
rule held (zero SPDX lines removed), all 14 new lines verified as real
expression derivations (activitysetalgebra.h spot-checked identical
against capt 941bb7fb), backend.{h,cpp} exclusion confirmed correct, no
non-comment code changed. Two non-blocking nits: cosmetic orphan
line-wraps in windowtrackingpredicates.h + extraviewhints.h (fixed on the
branch, 8949b41e2, re-gated) and bare "capt" in internal session-log
prose (left - consistent with the repo's internal docs, not a source
citation). ff-merge on the MERGE verdict per the workflow.
