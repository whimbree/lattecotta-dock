# Cold audit: D27 window-change starvation, D28 stale color veto, and packaging

Scope: independent read-only review of the D27 (continuous same-window changes
starve tracker consumers) and D28 (the obsolete colorfulness veto blocks
palette-responsive applet content) fixes already merged into `main`, plus every
distinct Debian, RPM and Arch packaging prototype created on 2026-07-19.

No relevant index contained staged changes, no stash existed, and all relevant
worktrees were clean. The earlier D27/D28 root-cause investigation survived as
unstaged corrections in `docs/tracking/known-defects.md` and
`docs/tracking/session-handoff.md`, not as a separate review artifact.

## Verdicts

- D27 (continuous same-window changes starve tracker consumers): the merged
  maximize-only immediate flush is not an acceptable final fix. Replace it.
- D28 (the obsolete colorfulness veto blocks palette-responsive applet
  content): the merged show-desktop exception is not an acceptable final fix.
  Replace it.
- F1 (the Debian package): `DO NOT MERGE` for
  `packaging/debian-phase-f1` at `cf94d10c1`.
- F2-A (the first RPM package): `DO NOT MERGE` for
  `packaging/rpm-phase-f2` at `821beb8e9`.
- F2-B (the competing RPM package): `DO NOT MERGE` for `rpm-phase-f2` at
  `9522d4b09`. Its RPM macro structure is the better reconstruction base, but
  it does not satisfy the package gate.
- F2-C (the untracked RPM prototype): reject the distinct untracked
  `packaging/rpm/` implementation. It duplicates neither committed RPM branch.
- F3 (the Arch package): `DO NOT MERGE` for `packaging/arch-pkgbuild-f3` at
  `e6ebb9ddf`.

## D27 findings

The merged commits are `69c1f73ac`, `c946ea954`, and `09eca9adb`; the branch
commits are patch-equivalent rebases. `considerWindowChanged()` still restarts
the active 150 ms timer for every same-window event at
`app/wm/abstractwindowinterface.cpp:398-401`. A sustained stream can
therefore postpone `windowChanged` forever. The new `immediate` boolean and
dedicated maximize slot at `app/wm/waylandinterface.cpp:763-769,792` only
bypass the defect for `maximizedChanged`.

The test sends a short burst followed by a quiet period at
`tests/windowchangedebouncetest.cpp:90-109`, which the original starving
implementation already passes. Its remaining cases at lines 111-147 invoke the
new boolean directly rather than driving the real Wayland signal wiring or
tracker consumers. The test therefore proves the bypass, not bounded delivery.

Replacement contract: remove the boolean and dedicated maximize slot; start a
fixed deadline only when no same-window deadline is active; preserve the
different-window flush order; read the latest `WindowInfoWrap` at emission.
Permanent tests must sustain events beyond multiple deadlines, prove bounded
delivery and reduced notification count, prove A-then-B ordering, and drive
maximize/restore through the real `Windows` and `LastActiveWindow` graph.

## D28 findings

The merged code commit is `e523df1b5`; `e1103fc1e` is its patch-equivalent
branch commit. The plugin-id exception at
`containment/package/contents/ui/applet/AppletItem.qml:111-115,166-170` leaves
the obsolete whole-applet colorfulness veto in place for every other stock and
third-party applet. D21 (stock applet palette propagation) replaced destructive
FBO flattening with inherited `Kirigami.Theme` roles.
Fixed image, SVG, and rectangle pixels are not recolored by palette propagation,
so rendered colorfulness no longer answers a relevant safety question.

The committed test was already green before the exception. Its new assertion at
`tests/e2e/110-colorizer-applet-contrast.sh:104-107` is implied by the existing
assertion at lines 92-103, the panel-wide crop at lines 111-125 can pass on
unrelated dark pixels, and the classifier result needed to prove the exception
ran is not observable. The temporary fixture recorded in `known-defects.md`
proved that fixed pixels remain unchanged while responsive content in a mixed
applet gains the correct palette.

Replacement contract: remove the show-desktop exception, colorfulness veto,
probe, QML type, registration, and obsolete D-Bus reason. Keep the real
self-colored, explicitly blocked, inline-full, splitter, and disengaged
exemptions. Deterministic responsive-only, fixed-only, and mixed-content
fixtures must use per-control render crops and prove both palette response and
fixed-pixel stability.

## Packaging findings

The Debian smoke can skip installation when any `latte-dock` package is already
known to dpkg at `packaging/debian/smoke-installed.sh:28-40`, disables tests at
`packaging/debian/rules:8-12`, runs only a lifecycle smoke, retains ambient
module paths, and does not check the child exit status at
`packaging/debian/smoke-installed.sh:103-117`. Runtime QML dependencies are
incomplete, the source is incorrectly declared native, licensing is incomplete,
and final-head gate evidence is absent.

The Arch recipe declares the wrong package identity and version at
`packaging/arch/PKGBUILD:15-17`, tests a raw DESTDIR install rather than the
generated package at lines 67-103, uses an external unchecked source archive,
misclassifies dependencies and licenses, and contains a startup state check at
`packaging/arch/smoke-installed.sh:98` that cannot match the emitted JSON.
Ambient component fallback and unchecked shutdown status can also produce
false-green results.

RPM implementation A suppresses every generated QML dependency at
`packaging/rpm/latte-dock.spec:20-23`, carries an unrelated Debian commit,
bypasses distro CMake macros, and runs only one smoke recipe. RPM implementation
B has the better `%cmake*` and install-path foundation but has no
installed-package driver or `%check` between its `%install` and `%files`
sections, incomplete BuildRequires and license metadata, and no modeled
openSUSE self-import provides. The untracked third prototype is Fedora-specific
despite claiming an openSUSE path and does not invoke its smoke script.

All package replacements must build in dependency-minimal native buildroots,
install the exact generated artifact into a fresh runtime image, verify package
identity and manifest, forbid ambient build or Nix fallbacks, and run the full
installed-artifact contract from `docs/tracking/multi-distro-ci-plan.md`.

## Settings continuity

The settings completion initiative remains active and precedes implementation
with a planning gate. `docs/tracking/settings-surface-completion-plan.md` still
requires an explicit action-by-target matrix, minimized-window target
retention, propagation of missing `LastActiveWindow` state, invalid legacy task
action handling, and migration of every duplicated containment schema. A fresh
cold review must return `MERGE` before settings implementation is distributed.
