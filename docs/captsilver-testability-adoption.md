# CaptSilver testability adoption plan

Written 2026-07-16 from a full read of latte-dock-qt6's test
infrastructure (method and dead ends in
docs/agent-logs/2026-07-16-captsilver-testability-study.md). This
replaces the microvm GUI-CI idea in the porting plan: the fork's
sceneprobe harness demonstrates that real-pixel regression testing
runs headlessly on pure CPU, which covers the rendering half of what
the microvm was for. HARD CONSTRAINT throughout: everything adopted
must run in CI under a plain VM (KVM or similar) - no real GPU,
no passthrough.

Source-state note: the local checkout sits at 9003f33a; the DI seams
and ~18 extra tests exist only at origin/main (read via git show).
The sceneprobe harness is identical in both. The fork has NO CI
config at all - its gates are local shell plus a pre-push hook, so
"their CI" was never a thing to copy; only the gates are.

## The headline answer: no dGPU anywhere

Sceneprobe is lavapipe-FIRST, not dGPU-first. Lavapipe (Mesa's
software Vulkan rasterizer) is the default device (`run.sh:10`), the
self-test runs on it, and its tolerance tier is the STRICTER one:
bit-exact (delta 0, budget 0), because `LP_NUM_THREADS=0` makes
llvmpipe rasterization deterministic. The dGPU mode is an opt-in
second golden set behind `--device dgpu` with the hardware-specific
env confined to one script block; golden sets are per-device and
independent, so running lavapipe-only never touches the dgpu PNGs.
We adopt the lavapipe path wholesale and simply never bless dgpu
goldens. Every piece of the chain - `kwin_wayland --virtual`,
lavapipe, the Vulkan validation layer - is pure CPU and VM-safe.

## How sceneprobe renders real pixels headlessly (the crown jewel)

This defeats the limitation we recorded ourselves: the offscreen QPA
never starts a scenegraph render loop, so TestCase.grabImage returns
blank white. Sceneprobe never grabs a window at all:

- `QQuickRenderControl` drives a never-shown QQuickWindow; frames are
  pumped manually (polish/begin/sync/render/end), exactly 5 of them.
- The render target is a hand-built `QRhiTextureRenderTarget`
  (RGBA8 256x256 + depth/stencil), read back with
  `QRhiReadbackDescription` into a QImage. No swapchain, no mapped
  surface, no compositor pixels.
- It runs under a nested `kwin_wayland --virtual` session (wayland
  QPA) only because QVulkanInstance needs platform glue the offscreen
  QPA lacks; the window still never maps.
- `QSG_RHI_BACKEND=vulkan` pinned in-process;
  `VK_ICD_FILENAMES=<lavapipe icd json>` selects the device.
- A custom `QAnimationDriver` steps a fixed 16ms per frame, so
  animated scenes read back identically regardless of wall clock -
  goldens were re-blessed once after this landed, which is the
  cautionary tale: determinism machinery FIRST, goldens second.

Three independent failure gates, two of which need no golden at all:

1. Shader/scenegraph gate: a message handler traps shader
   deserialization/preparation failures and any qt.scenegraph.general
   warning. This alone catches the MultiEffect defect family (our
   family 7) with zero pixels compared.
2. Vulkan validation gate: VK_LAYER_KHRONOS_validation with a debug
   messenger; suppressions are named VUID substrings in a file, each
   with a one-line reason.
3. Output gate: a blank-frame detector (near-transparent or uniform
   flat color fails - the exact failure our offscreen grabs produce),
   optional per-scene pixel/region expectations declared as
   `property var probeExpect` in the scene, then the golden compare.

The comparator is two-parameter: per-channel max delta AND a budget
fraction of pixels allowed to exceed it (match iff diffFraction <=
budget). Lavapipe tier is {0, 0} - bit exact; per-scene overrides via
`probeTolerance` exist for exactly three MultiEffect multi-pass
scenes with measured run-to-run variance, numbers recorded in their
commit body. On failure it writes actual/expected/diff PNG triples
(diff amplified x8) plus a worst-pixel verdict line. Goldens are
plain git files, `<scene>.expected.<device>.png` next to the scene -
16 scenes x 2 devices = 296KB total, so no LFS needed. Blessing is
explicit (`--bless`, only for scenes that passed the other gates,
missing golden = failure with instructions). The gate SELF-TESTS
first: a good scene must pass, a broken-shader scene and a blank
scene must fail, else "GATE BROKEN" exit 3.

Their 16 scenes each mirror a named production call site: parabolic
zoom + colorizer, ShadowedItem, GlowPoint/indicator glow, BadgeEffect
(the one runtime-loaded .qsb - likeliest to catch shader breaks),
Kirigami.ShadowedRectangle (dock background), RectangularShadow,
background composition, tooltip preview, and six MultiEffect shader
permutations including a degenerate animated-shadow scene that
sweeps 0 -> 0.8 under the fixed clock.

## Adoption priorities

P1 - sceneprobe, lavapipe-only (~700 lines: probe binary 258 +
comparator 190 + imgdiff CLI 33 + two scripts + scenes). The missing
tier between our unit tests and live verification; closes exactly the
pixel-peeping gap the extraction initiative's live tail exposed.
Transplant notes:
  - Architecture-independent (Qt6::Gui/Quick/Qml + GuiPrivate for
    qrhi.h - private API, verify against our pinned 6.11; plus Vulkan
    headers). Touches zero app code, so no upstream-shape conflict.
  - Nix surgery: ICD path from the devshell (mesa's lvp_icd json),
    kwin_wayland into the devshell, our staged-QML layout instead of
    their usr/lib64 paths. Wire as scripts/sceneprobe-gate.sh next to
    the other gates.
  - NEVER reuse their goldens: bless ours inside the exact
    environment CI runs. Our nix pin is an advantage - pinned
    Qt+Mesa+fontconfig makes cross-machine lavapipe determinism
    actually credible, where their distrobox goldens were
    per-machine. Verify by blessing on one machine and gating on
    another before trusting the exact tier.
  - Pin fonts in scenes (their parabolic_zoom renders Text with no
    family pinned - latent flake; ship a test font or avoid text).
  - Adopt the gate-self-test pattern as-is.
  - Scenes to write for OUR defect history: the ShadowedItem static
    paddingRect contract, the colorizer ColorOverlay stack, the
    forced-monochromatic icon path, indicator glow, BadgeEffect, the
    six MultiEffect permutations, a degenerate animated scene.

P2 - contract-test transplants pinning Plasma 6 silent failures
(each is a "renders fine, silently does nothing" pin - our
stub-tracking nightmare class; verify each against Qt5 semantics
before adopting): alternativescreateapplettest (createApplet takes
QRectF now; wrong type = silent no-op), indicatormetadatatest (KF6
KPluginMetaData single-string ctor no longer parses a path),
indicatorsknsrctest (KNSCore reads KPackageStructure not
KPackageType), contextmenudefaulttest (Plasma 6 loads no
[ActionPlugins]), editmodehandleactiontest (applet.action() is
gone), appletzoomsizetest + bindingrestoremodetest (Binding
restoreMode collapses applet size on hover).

P3 - behavioral tests via our lattedock-core object library (their
glob-link trick approximates what our object library does cleanly):
screenpooltest and visibilityrevealtest first (Phase 8 subjects),
then abstractlayouttest, activitiesinfotest,
syncedlaunchersclienttest, lastactivewindowtest, the vendored-backend
pins (smartlauncheritemtest, tasksbackendtest - these also serve the
plasma-desktop sync pass), datatypestest, settingsmodelstest. Skip
their mirror-logic tests (storageroundtriptest, templatesnametest) -
TESTING.md already rejects reimplementation-testing.

P3b - the remaining transplant candidates (filed 2026-07-17 from a
mechanical include-level pass over every fork test we lack by name;
the 33-name gap splits into 10 their-architecture skips, a few
already-covered-differently, and these, all over headers our tree
shares - each still gets the P3 method: verify the premise against
OUR code first, raise past the fork's cases, negative-test):
shortcutstest (shortcutstracker+modifiertracker; high value now that
the shortcuts host is alive again), storagetest (layouts/storage.h
breadth beyond our id-remap pair), universalsettingstest,
layoutmanagertest + appletremovaltest (containment LayoutManager
restore/save/removal beyond our parking suite), importerlogictest
(breadth beyond importerregressiontest), layoutsmodeltest +
viewsmodeltest + schemesmodeltest (settings models beyond our
applets+screens pair), viewmodelstest (app/view/tasksmodel +
indicatorinfo), wmtoolstest (wm/tasktools + schemecolors),
commontoolstest/coretoolstest/generictoolstest (helper suites,
partial overlap with our units - premise check decides),
coretypesenumtest (enum value pins; synergizes with the D-Bus name
maps), panelbackgroundtest + configcontrolstest (verify against our
panelbackgroundscantest/backgroundstatetest and the qml
config-control tests before adopting). Their-architecture skips, so
the next pass does not re-derive them: addviewdecision,
startuplayoutplanner, validviewsmapbuilder, viewcontainertransition,
viewpriority, viewsyncplan, activitystatescache, settingsnameutils
(their extractions), windowindex (perf-only candidate, already
banked), storagevalidator (build OURS deliberately - already banked
for Phase 8/11 with our layout-corruption history as the driver).

P4 - e2e pixel assertions: their latte-imgdiff CLI (comes free with
P1) + KWin ScreenShot2 before/after diffs under a nested kwin, driven
over D-Bus with a seeded HOME. Composes directly with our
observability surface and tests/e2e/. Their honest finding worth
keeping: on-disk config is an unreliable removal witness (lazy
flush); D-Bus state + pixels are conclusive.

P5 - ideas, not code: the coverage-harness self-test principle (a
fixture proving the tracer can tell executed from unexecuted, so a
broken harness cannot report green) and their honest-coverage rules
(no construction-only credit, every claimed unit asserts an
observable effect, live-only units listed rather than gamed). Their
mock-context technique for package QML (declare every unqualified
context name on the TestCase root) is directly reusable in qmltests.

Not adopting: the DI-seam test family (CoronaEngine/IScreenInfo/
IViewFactory - their diverged architecture; revisit per-case only if
we choose the same extraction for our own reasons), their glob-link
CMake machinery (ours is better), and mirror-logic tests.

Direction change 2026-07-16 (my call, mid-session-two): the dgpu tier
was first listed here as not-adopted ("undocumented local extra").
Revised: the harness must WORK with a GPU but never REQUIRE one - it
runs in CI/CD (plain VM, lavapipe), on my desktop (real GPU), and in
a microvm. So the dgpu device dispatch is a DOCUMENTED optional
extra: lavapipe stays the default and the only tier CI gates on;
dgpu is opt-in (--device dgpu), its golden set independent and
unblessed until someone blesses it deliberately; a dgpu run without
goldens still runs the shader/validation/blank-frame gates and says
loudly that no goldens exist for the device. The VM-only constraint
still binds everything CI depends on.

Standing quality rule (my direction, 2026-07-16): the fork's tests
are the floor, not the ceiling. Every adopted test gets raised to
this tree's bar - edge cases the fork skipped, loud-refusal cases,
data-driven tables over single happy paths, negative-tested premises,
and the step-2.5 machinery (sanitizers, forced asserts, types) doing
real work. Weak test shapes get redesigned, not copied.

## Test inventory verdict in one line

Of the fork-only tests: ~11 test their restructured architecture
(skip), ~25 are transplant candidates against upstream-shaped code we
share (P2+P3 above name the priority ones), and the rest cover
subsystems we already pin differently (notably our versions carry
ASan+UBSan+QT_FORCE_ASSERTS, which theirs do not).
