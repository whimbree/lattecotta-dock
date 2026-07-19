# 2026-07-16: sceneprobe transplant (P1 of the CaptSilver adoption plan)

Executing P1 of docs/reference/captsilver-testability-adoption.md: transplant the
sceneprobe visual-regression harness from latte-dock-qt6 (read-only
reference at ~/Projects/latte-dock-qt6/tests/sceneprobe/) into this tree
as tests/sceneprobe/, lavapipe-only, wired into our own staging and gate
conventions. Hard constraint: pure CPU (Mesa lavapipe, LP_NUM_THREADS=0),
no dGPU mode anywhere.

## Source survey (read before writing anything)

Every file of their harness read in full:

- main.cpp (258 lines): QQuickRenderControl drives a never-shown
  QQuickWindow onto a hand-built QRhiTextureRenderTarget (RGBA8 256x256 +
  depth/stencil), 5 frames pumped manually under a fixed-step 16ms
  QAnimationDriver, then QRhiReadbackDescription readback. Three gates:
  shader/scenegraph message trap, Vulkan validation messenger with a
  suppressions file, output assertions (blank-frame floor, probeExpect,
  golden compare with probeTolerance override). Self-test scenes skip the
  golden compare by filename prefix.
- imagecompare.{h,cpp}: two-parameter comparator (per-channel delta +
  exceed-budget fraction), invariants, probeExpect checks, amplified
  diff, verdict line.
- imagecompare_test.cpp: 18 slots covering all comparator surfaces.
- imgdiff_main.cpp: CLI over the same comparator (P4 wants it later).
- run.sh: gate self-test (good passes, bad fails, blank fails), then all
  scenes; --bless copies actuals over goldens; DESTDIR-install staging.
- run_in_kwin.sh: throwaway nested kwin_wayland --virtual session,
  lavapipe ICD + LP_NUM_THREADS=0 (dgpu branch pins their RX 9070 XT via
  MESA_VK_DEVICE_SELECT - hardware-specific, NOT coming over).
- vk-suppressions.txt: two VUIDs, both Qt/lavapipe-internal, reasons
  recorded per line.
- suppressions.txt: LSan suppressions for opt-in leak checking.
- 16 scenes + per-device goldens. Goldens are NOT reused (adoption doc:
  bless fresh in our environment or not at all).

## Adaptation decisions

- lavapipe-only: run_in_kwin.sh keeps the SCENEPROBE_DEVICE plumbing
  (it is one case statement and the golden filenames carry the device
  name) but knows only "lavapipe"; anything else is a loud error. The
  dgpu env block (MESA_VK_DEVICE_SELECT etc.) is stripped, not ported.
- ICD + validation layer come from the flake pin, not /usr or
  /run/opengl-driver: the devShell exports LATTE_VULKAN_LAVAPIPE_ICD
  (mesa's lvp_icd.x86_64.json) and LATTE_VK_LAYER_PATH
  (vulkan-validation-layers' explicit_layer.d). Pinned packages, so the
  goldens are blessed against the exact Mesa/Qt CI would run. These are
  process-level tools per the import-path doctrine: packages yes, no
  QML/plugin roots added to any *_PATH.
- kwin_wayland: kdePackages.kwin added to the devShell explicitly (it
  resolved from the session environment before, which is not a pin).
- Staging: their DESTDIR/usr/lib64 staging replaced with our
  scripts/lib-qml-env.sh qml_env_setup/qml_env_stage; the probe gets the
  full resolved import list (pinned module dirs + staged tree last) via
  LATTE_QML_IMPORT_PATH, which main.cpp now splits on ':' (theirs took a
  single dir).
- main.cpp checks that VK_LAYER_KHRONOS_validation is actually available
  before creating the instance and fails loudly if not: QVulkanInstance
  silently drops unsupported layers, which would turn gate 2 into a
  silent no-op on a machine without the layer package (exactly the
  silent-failure class CLAUDE.md bans).
- Scenes: selftest-good/bad/blank verbatim. shadoweditem adapted to our
  org.kde.latte.components.ShadowedItem (same URI and property names;
  ours is the static-paddingRect version from e3376405, and the scene
  asserts shadow presence outside the source rect so the padding
  contract is exercised). Six multieffect_* permutations adapted; every
  Text element replaced with shapes - the adoption doc flags unpinned
  fonts as a latent flake (their parabolic_zoom), and their multieffect
  scenes have the same unpinned Text. Shapes exercise the same shader
  paths without a fontconfig dependency. badgeeffect adapted (pinned
  kdeclarative ships org.kde.graphicaleffects with BadgeEffect.qml;
  our TaskIcon.qml imports it) with the Text replaced likewise.
- parabolic_zoom: SKIPPED for now per the task contract (unpinned font;
  needs a shipped test font or a text-free redesign). Follow-up noted
  below.
- Gate: scripts/sceneprobe-gate.sh in the style of qmllint-gate.sh
  (self-reexec into nix develop, loud failure text, gate self-test
  first). Not in default ctest: it needs a nested kwin_wayland, which is
  heavier than the offscreen ctest contract; it joins the script-gate
  family (like build-check.sh itself).
- Comparator unit test registered as sceneprobe_imagecompare in ctest;
  ratchet baseline 47 -> 48 with the sorted entry added.

## Follow-ups owed

- parabolic_zoom scene (needs a pinned/shipped font or a text-free
  parabolic scene; the adoption doc's P1 scene list also still wants the
  colorizer ColorOverlay stack, forced-monochromatic icon path, and
  indicator glow as scenes written for our defect history).
- Cross-machine golden verification: the adoption doc wants a bless on
  one machine and a gate on another before fully trusting the exact
  {0,0} tier. Only this machine available today; recorded here.

## Execution record

- Devshell pieces: pinned mesa 26.1.2 ships lvp_icd.x86_64.json;
  vulkan-validation-layers 1.4.341.0 at the pin; kdePackages.kwin 6.6.5.
  flake.nix exports LATTE_VULKAN_LAVAPIPE_ICD + LATTE_VK_LAYER_PATH and
  adds vulkan-headers/vulkan-loader to buildInputs (find_package(Vulkan)).
  package.nix gets the same two Vulkan deps: the package build keeps
  BUILD_TESTING on, so tests/sceneprobe compiles there too.
- kwin deviation, root-caused to observed behavior: kwin 6.6.5
  --exit-with-session NEVER launches the session app here (verified with
  a trivial id-to-file script: kwin comes up, wayland socket appears in
  the private XDG_RUNTIME_DIR, app never runs, no kwin output at all
  even with kwin_core logging on). run_in_kwin.sh therefore backgrounds
  kwin on a private socket, waits for the socket, runs the command
  directly in the caller's env, and tears kwin down. Simpler than the
  upstream generated-session-script dance and the exit code is the
  command's own by construction. One cleanup gotcha: the nested bus's
  xdg-desktop-portal FUSE-mounts $RT/doc, so cleanup unmounts it before
  rm (first run left an unremovable mountpoint behind).
- First full gate run: self-test ok (good passes, bad exits 1 via the
  shader gate, blank exits 1 via the output floor); all 9 scenes fail
  with only missing-reference errors, writing actuals. The pre-recorded
  VUID-VkShaderModuleCreateInfo-pCode-08740 suppression (validation
  layer vs lavapipe version skew on Qt's scenegraph SPIR-V) fires here
  too and is correctly suppressed; the other suppression
  (ppEnabledExtensionNames-01387) did not fire at our pin.
- shadoweditem probeExpect values MEASURED from the actual lavapipe
  render (magick pixel reads on the actual.png), not guessed: shadow
  below bottom edge (128,160)=#7b1e1e, right edge (184,128)=#642424,
  above top edge (128,97)=#542828, source center white, background
  corner #303030. All three shadow points sit OUTSIDE the effect item's
  bounds, so a zeroed/mis-scaled paddingRect clips them to background
  and fails; tolerances sized so background #303030 is rejected at each.
- Determinism: gate run twice pre-bless, all 9 actual PNGs byte-identical
  across runs (cmp). Notably multieffect_blur and multieffect_mask are
  bit-exact here - the upstream tree needed probeTolerance for blur
  (delta 2), mask (delta 16) and passthrough (delta 8); with the
  text-free scenes and pinned Mesa 26.1.2, NO scene needs a tolerance
  and every golden gates at the strict lavapipe {0,0} tier.
- Visual inspection of all 9 actuals before blessing (montage strips):
  every scene renders its intended content. multieffect_mask renders
  visually as all-pass (maskThresholdMin 0 + maskSpreadAtMin 1 passes
  m=0), same as the upstream fork's own golden - the scene's value is
  loading the mask shader variant, not the visual cutout.
- Blessed 9 goldens (53K total), then a fourth full run against the
  committed goldens: all PASS, gate green end-to-end on this machine,
  inside the devshell, pure CPU (llvmpipe, LP_NUM_THREADS=0).
- Gate-suite side-find, root-caused and fixed: build-check.sh re-execed
  into nix develop only when cmake was MISSING, but this host's system
  profile ships cmake 4.1 (without ninja), so from a bare shell the
  script silently reconfigured both build dirs with the system
  toolchain - first run died mid-build ("ninja: build stopped"), second
  died configuring the no-x11 variant ("CMake was unable to find
  Ninja"). Two different symptoms, one variable: which cmake resolved.
  Fixed with qmllint-gate's pinned-closure test (re-exec unless cmake
  resolves under /nix/store/*), then wiped both build dirs and re-ran
  everything fresh so no artifact of the wrong toolchain survives.

## Final gate results (all on the decontaminated fresh builds)

- Fresh cmake configure + full build, both variants, from empty build
  dirs: WITH_X11=ON and WITH_X11=OFF both rc=0.
- build-check.sh (with the re-exec fix): OK - full ctest 48/48 passed
  (includes qmllintgate: baseline matched untouched, no shipped QML in
  this task; qmlcompilegate, qmlinteraction, qmlcontracts, and the new
  sceneprobe_imagecompare), coverage-ratchet OK (48 ctest entries, 27
  unit headers paired).
- scripts/sceneprobe-gate.sh with the from-scratch probe binary against
  the committed goldens: self-test ok, 9/9 PASS - the goldens also
  survive a full rebuild, not just back-to-back runs.

## Commits

- 1d279ebe5 build: pin the pure-CPU Vulkan pieces the sceneprobe gate needs
- 16b738537 test: transplant the sceneprobe probe and comparator from
  latte-dock-qt6
- c821b6278 test: sceneprobe scenes and the lavapipe render gate
- dc042ace4 test: bless the sceneprobe lavapipe golden set
- 151a8c829 fix: build-check must re-exec unless the PINNED cmake is in
  PATH
- (this log + porting-plan tick: the docs commit following these)

## Post-merge note (orchestrator)

The serial merge rebased this branch onto master, so the hashes above
are the worktree's, not master's. Landed as: e0d5dcce9, e9f9d7734,
2c3547a18, c5372aba4, 27668839a (same subjects, same order).
