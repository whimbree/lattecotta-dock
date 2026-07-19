# 2026-07-16: layer-shell probe guard + WindowId newtype hardening

Working ledger for the two-part pass on branch
`worktree-agent-a4de546c2d2153134`. Merge will rebase; hashes below are
branch-local.

## Part 1: guard the LATTE_LAYERSHELL_HAS_SET_SCREEN probe

### Probe analysis

- Site: top-level CMakeLists.txt (try_compile against
  cmake/CheckLayerShellSetScreen.cpp, linking LayerShellQt::Interface).
  The result variable is a NORMAL variable, so the probe re-ran on every
  configure and the `add_definitions` branch adopted whatever that run
  said. One configure in a broken environment (system cmake outside the
  devshell, torn toolchain) silently flips the define; layer-shell
  output pinning then falls back to QWindow::setScreen() with only a
  STATUS line nobody reads.
- What it probes: whether LayerShellQt::Window::setScreen(QScreen*)
  exists. Verified against the PINNED dependency: layer-shell-qt 6.6.5
  (/nix/store/5517bbc2...-layer-shell-qt-6.6.5-dev/include/LayerShellQt/window.h
  line 132) ships the member. So in this environment a probe failure is
  never a real "no" - it is an environment defect.

### Chosen guard semantics

Two independent defenses, mirroring the flavor of 27668839a
(pinned-cmake re-exec guard):

1. Failure triage by diagnostic, not by exit code: the only failure
   accepted as a legitimate "API absent" answer is compiler output
   matching `no member named.*setScreen` (gcc and clang shapes, quote
   style agnostic). Any other failure output (missing header, dead
   compiler, link failure) is an environment defect and configuration
   stops with FATAL_ERROR carrying the full probe output. This stays
   correct across re-pins: a future layer-shell-qt that really removed
   the member produces exactly the missing-member diagnostic and flows
   to a quiet FALSE.
2. The answer is cached (INTERNAL) keyed on LayerShellQt_DIR: a plain
   reconfigure reuses the cached answer instead of re-probing, and
   re-pinning layer-shell-qt (new store path, new LayerShellQt_DIR)
   re-probes automatically. This makes the latte-build-env skill's
   "probes cache" claim actually true - before this the probe never
   cached.

### Verification (see commit body too)

- fresh configure in the devshell: probe runs once, STATUS "available",
  CMakeCache carries LATTE_LAYERSHELL_HAS_SET_SCREEN=TRUE.
- reconfigure: no re-probe (proven by making a re-probe impossible -
  probe source temporarily renamed - and reconfiguring successfully).
- broken-env simulation: probe source temporarily given an unresolvable
  #include, fresh build dir; configure dies with the FATAL_ERROR and
  the probe output names the missing header.
- legit-absence simulation: probe call temporarily renamed to a
  nonexistent member; fresh configure proceeds to FALSE + the
  "not available" STATUS instead of FATAL.

## Part 2: WindowId newtype hardening

### Newtype design

`app/wm/windowid.h`, class Latte::WindowSystem::WindowId, QByteArray
storage (zero conversion from KWayland uuid()):

- default ctor = the documented no-window id (isEmpty() true);
- named factories only: fromWaylandUuid(QByteArray),
  fromX11WId(quint64) (0, X11 None, maps to the empty id);
- std::optional<quint32> toX11WId(): checked decimal parse, nullopt for
  empty and for malformed bytes - the silent-0 toUInt() becomes a
  type-enforced absence; a parsed 0 is ALSO refused (0 is X11 None, not
  a window), so window 0 becomes unproducible at the type level;
- the loud boundary parse lives next to the type as a free function
  (parseX11WindowId(wid, operation) - quiet nullopt for the documented
  empty/no-window id, qWarning + nullopt for malformed bytes) rather
  than file-local in xwindowinterface, so the negative test can drive
  it headless;
- bytes() for the QVariant/QML and logging boundaries (the QML winId
  property stays QVariant-of-QByteArray per 8e8cdf31);
- operator== and operator< (QMap/QList keys), QDebug streaming;
- implicit conversion from QByteArray/char* designed out; pinned by
  static_asserts in the unit test.

### Conversion sequencing (why the chunks are shaped this way)

The type flip itself is atomic: every signature in wm/ shares the
alias, so "one subsystem per commit" cannot hold across the flip
without giving the newtype temporary implicit conversions, which would
defeat the explicit-construction guarantee the test pins. Instead all
SEMANTIC changes land as their own pre-flip commits while WindowId is
still QByteArray (each independently buildable and revertable), and the
flip commit is purely mechanical renames:

1. fix(wm): checked X11 id parsing (the ok-flag sites) - real behavior
   change, lands alone.
2. refactor(view): the wayland lazy re-resolve sites - record the stale
   numeric premise, preserve behavior exactly.
3. feat+test: the newtype header and its unit test (standalone).
4. port(wm): the mechanical flip.

### Findings hit along the way

- xwindowinterface has MORE ok-ignoring parse sites than the six the
  plan item counted: toUInt() appears across ~15 functions plus two
  toInt() sites (requestActivate, requestToggleMaximized's NETWinInfo).
  All funneled through one checked parse.
- lattecorona.cpp onColorSchemeChanged (X11 branch) parses a D-Bus
  string with toULongLong() ignoring ok - external boundary, same
  family.
- waylandinterface activeWindow() returns `0` - a null char* silently
  constructing the empty QByteArray. Exactly the implicit-conversion
  hazard the newtype removes.
- The three `isPlatformWayland() && id.toInt() <= 0` sites
  (positioner/subwindow/subconfigview trackedWindowId()) are a stale
  numeric premise: wayland ids are uuids, uuid.toInt() is 0, the check
  is constant-true, so every call re-resolves. Deliberately preserved
  as an unconditional wayland re-resolve (not tightened to isEmpty()):
  the subwindow reshow hack remaps surfaces with a fresh uuid and
  latteWindowAdded only fires for isAcceptableWindow() windows, so
  skip-taskbar subwindows may depend on the lazy re-resolve. Tightening
  needs a live session; flagged for merge-time verification.

### Test-quality direction (mid-task, from Bree)

The step-2.5 floor is a floor. The windowid unit test must carry:
exhaustive malformed-input rows for toX11WId() (empty, non-numeric,
trailing garbage, overflow past quint32, negative), a compile-time pin
that implicit conversion stays deleted (static_assert on
!std::is_convertible where expressible), QMap key ordering/equality
semantics pinned against the old QByteArray behavior so the substrate
swap is provably behavior-preserving on the wayland path, and the
loud-absence boundary negative-tested (malformed id in, assert the
qWarning fires via QTest::ignoreMessage and that window 0 is NOT
produced).

### Chunk notes

- chunk 1 (checked X11 parsing, pre-flip): all toUInt()/toInt() id
  parses in xwindowinterface.cpp funneled through one file-local
  parseX11WindowId(wid, operation) helper (quiet on the documented
  empty id, qWarning + refuse on malformed bytes). Two latent toInt()
  bugs retired on the way: requestActivate and requestToggleMaximized
  turned ids above INT_MAX into silent window-0 operations.
  isAcceptableWindow(unparseable) now answers false instead of the
  old accidental true from a window-0 KWindowInfo (reachable only
  through degenerate paths; noted in a comment at the site).
  lattecorona's windowColorScheme D-Bus handler (X11 branch) refuses
  malformed ids loudly instead of tagging window 0. The helper moves
  next to the newtype at the flip so the boundary contract is
  unit-testable headless.
- chunk 2 (wayland lazy re-resolve, pre-flip): the three getters keep
  the unconditional wayland re-resolve; commit body carries the
  latteWindowAdded coverage analysis.
- chunk 3 (newtype + windowidtest): landed standalone; nothing included
  the header until the flip.
- chunk 4 (the flip): windowinfowrap.h's alias and windowIdFromWId()
  replaced by including windowid.h; xwindowinterface's file-local parse
  helper deleted in favor of the shared one (which also refuses a
  parsed 0, matching the old setKeepAbove/Below '<= 0' guards);
  wayland uuid() productions wrapped in fromWaylandUuid at every site;
  activeWindow()'s 'return 0;' (null char* to empty QByteArray - the
  poster implicit conversion) now returns WindowId() with a comment;
  X11 transientFor parent-id construction simplified through
  fromX11WId's 0-maps-to-empty rule; isNull() -> isEmpty() everywhere
  (equivalent: ids were only ever default-null or set non-empty);
  LastActiveWindow gained typed currentWindowId() so windowstracker
  stops round-tripping the id through the QVariant QML boundary
  (currentWinId().toByteArray()); the QML winId property still ships
  QVariant(bytes()). Checked: no queued connection marshals a WindowId
  (the only wm QueuedConnection lambda is argument-free), so
  Q_DECLARE_METATYPE is future-proofing, not a runtime dependency.
  Both WITH_X11 variants compile; full ctest 55/55.

### Gate results (all green, run on this branch after the flip)

- full ctest: 55/55 passed (includes the new windowidtest and the
  EX-23 windowtrackingpredicatestest / windowinfowraptest safety net).
- scripts/coverage-ratchet.sh: OK (55 ctest entries, 29 unit headers
  paired; baseline bumped 54 -> 55 with windowidtest in the entry list
  and app/wm/windowid.h in app-subtree-units.list).
- scripts/qmllint-gate.sh: OK (233 files, 155 finding files, 6025
  curated warnings, baseline matched - no QML touched by this pass).
- scripts/build-check.sh --fresh: OK - both WITH_X11 variants wiped,
  reconfigured (the Part 1 probe guard ran fresh in both) and rebuilt,
  ctest re-run green inside it.

### Final commit list (branch-local hashes; the merge will rebase them)

- c94c9de6f build: stop the layer-shell setScreen probe from silently
  flipping on a broken configure (Part 1)
- 612045025 fix(wm): X11 window-id parses check the ok flag instead of
  silently acting on window 0
- 352b32b39 refactor(view): record the stale numeric premise behind
  the wayland trackedWindowId re-resolve
- 3fcd3be9e feat(wm): WindowId newtype with checked X11 parse, pinned
  by a unit test
- 215b79847 port(wm): flip WindowId from the QByteArray alias to the
  newtype
- (this ledger finalization commit)

### Owed at merge time

- Live wayland session pass over the wm surface: active-window
  tracking, dodge/tracking behavior, color scheme following, and the
  subwindow/config-view uuid re-resolution after a reshow (the
  trackedWindowId lazy-resolve notes above). Headless gates cannot
  drive KWayland.
- Decision point recorded, not taken: tightening the three
  trackedWindowId() re-resolves to isEmpty() once latteWindowAdded
  coverage for skip-taskbar subwindows is verified live.
- docs/tracking/PORTING_PLAN.md tick for the WindowId item (left to the
  orchestrator per the worktree footprint rules), and a Phase 4-ish
  note for the probe guard if wanted.
