# <img src="logo.png" width="48"/> Lattecotta Dock

**"Art in Coffee, kiln-fired for Plasma6/QT6"**

The name in plain words: the latte half is upstream Latte Dock's "Art
in Coffee". The cotta half is terracotta: the Qt5 code went into the
kiln and came out fired clay - a from-scratch Plasma 6/Qt6
continuation with sanitized, unit-tested pure cores, type-disciplined
and asserted at entry.

> **Continuation notice**: this is a from-scratch **Plasma 6 / Qt 6 /
> Wayland** port of KDE's [latte-dock](https://invent.kde.org/plasma/latte-dock),
> forked with the full original history intact and maintained by
> [whimbree](https://github.com/whimbree) as a living continuation.
> Upstream development stopped in the Plasma 5 era; this fork carries the
> codebase to Plasma 6.7 / Qt 6.11 / KDE Frameworks 6 and is developed
> against a real daily-driver Wayland session. The port is Wayland-only,
> matching Plasma 6.8+'s Wayland-exclusive direction; X11 applications are
> unaffected (they reach the dock as Xwayland windows).

Latte is a dock based on plasma frameworks that provides an elegant and
intuitive experience for your tasks and plasmoids. It animates its contents
with a parabolic zoom effect and tries to be there only when it is needed.

Screenshots
===========

A floating dock at rest:

![](docs/screenshots/dock.png)

Parabolic zoom with live window previews on hover:

![](docs/screenshots/parabolic-hover.png)

Edit mode, with the blueprint grid and widget rearranging:

![](docs/screenshots/edit-mode.png)

Applet popups, themed and resizable (edge-drag to resize, size persists
per applet):

<img src="docs/screenshots/volume-popup.png" width="320"/>

Status
======

Working and in daily use on Wayland: multi-dock and multi-screen layouts,
the tasks applet with live window previews, group thumbnails, badges and
audio indicators, task and tooltip preview icons that refresh immediately
after icon-theme changes, parabolic zoom, edit mode with drag rearranging,
per-applet context menus and settings, the three bundled indicator styles,
layer-shell placement with struts and auto-hide/dodge visibility modes,
layout management with templates and import/export, applet colorization,
immediate work-area updates when maximize state changes a floating gap,
and the full settings UI. Beyond upstream parity, the port has grown
continuation features of its own, the first being resizable applet popups
with per-applet size persistence.

Dock identity boundaries keep screen-group clones inside one original's
lifecycle and treat independent duplicates as separate docks.

The stabilization initiative is complete: the feel-critical QML math
(parabolic zoom, previews, launcher ordering, drag classification,
visibility masks, badges, scrolling, colorization decisions) lives in
pure C++ cores with sanitized unit tests, the dock's runtime state is
inspectable and drivable over D-Bus
([D-Bus interface reference](docs/reference/dbus-interface-reference.md)),
including a read-only query for the latest task-icon middle-click dispatch
and a lifecycle-safe read-only settings-control registry whose production
controls join alongside their component and page tests,
rendering is guarded by a committed-golden scene gate that runs on pure CPU,
and an independent settings-source gate keeps every interactive declaration,
handler, contracted lifecycle hook, and dynamic menu action linked to the
provisional evidence ledger or an explicit exemption. Inherited bugs found
along the way are fixed at origin, each with its evidence in the commit body.

Roadmap
=======

The real tracker is [the porting plan](docs/tracking/PORTING_PLAN.md): 13
phases, one commit-traceable checklist item per task. The coarse picture:

- [x] Build system, Qt 6 / KF 6 migration, Wayland layer-shell backend
- [x] QML controls and rendering migration (Plasma 6 component stack)
- [x] Task manager subsystem (previews, groups, badges, launchers)
- [x] Widget management, drag-and-drop, edit mode
- [x] Nix packaging (flake with package, overlay and NixOS module)
- [x] Resizable persistent applet popups (continuation feature)
- [x] Stabilization: the QML logic extraction initiative
      ([the QML extraction plan](docs/tracking/QML_EXTRACTION_PLAN.md)) moved
      feel-critical QML logic into tested, sanitized C++ cores - all 25
      units executed; a tail of live verification recipes is tracked in
      the plan's executed notes
- [x] D-Bus observability and driving surface: any view's runtime state
      is pull-queryable as JSON, including an atomic canonically ordered
      all-docks topology snapshot with stable process-local object identities,
      and the coarse user actions are callable, so tests and tooling inspect
      state instead of peeping pixels
- [x] Headless visual-regression testing: real-pixel scene rendering on
      pure CPU under a nested compositor, bit-exact committed goldens,
      shader/validation/blank-frame gates; an opt-in GPU device mode
      exists but nothing requires it
- [ ] Layout/config persistence, session shutdown, multi-screen edge cases
      (the open phase; live-driven fixes land here continuously)
- [ ] Theming and colorization polish audit
- [ ] CI/CD and multi-distro matrix: the local gates (build-check, QML
      compile gate, qmllint ratchet, coverage ratchet, scene-render gate)
      are pure shell over cmake/ctest and CI-portable by design, and the
      port is proven to build and render headless in containers on eight
      distributions - Arch, Debian, openSUSE Tumbleweed, Fedora, KDE neon,
      Void and Gentoo, alongside the NixOS bit-exact tier - each behind the
      same nested-compositor sceneprobe gate with graduated per-distro
      golden rigor (bit-exact where the distro's Mesa/LLVM matches, a
      bounded tolerance where it does not); the hosted matrix pipeline
      itself is not stood up yet
- [x] Automated end-to-end GUI testing: a maintained recipe suite runs
      desk-independently inside a nested compositor - the staged dock,
      pointer injection, real client windows and KWin screenshots all
      isolated from the live session - asserting over the D-Bus surface
      first and over pixel goldens only where pixels are the thing under
      test (hover previews, drag reorder, wheel routing, lifecycle)
- [x] Local native packaging beyond NixOS: all five recipe formats are
      available for Debian-family `.deb` packages, a shared Fedora/openSUSE
      `.rpm`, Arch `PKGBUILD`/`.SRCINFO`, a Gentoo ebuild overlay, and a Void
      `xbps-src` template. The Debian-family, shared RPM, Arch, and Void formats
      have fresh-environment install proof. Gentoo has a built GPKG, a Portage-
      owned manifest, package/source reinstall evidence, and installed-artifact
      proof through exact mappings, nested-KWin startup, and clean shutdown. No
      official package or repository, package publication, package-artifact CI,
      release, tag, artifact upload, sponsorship, or distribution endorsement
      exists.
- [ ] Accessibility: keyboard navigation for every interactive surface,
      Accessible roles/names on every interactive item, and a
      screen-reader pass with Orca as the acceptance test. Two pieces
      exist: a keyboard focus mode (Meta+Alt+D or D-Bus) makes the
      otherwise focus-refusing dock window take keyboard input -
      arrow-key traversal over the same entry space the Meta+number
      shortcuts address, Enter activates, the item indicators double as
      the focus highlight, and Escape or any focus loss returns the
      window to focus-refusing - and the core surfaces carry Accessible
      semantics: tasks announce title, window count and badge values,
      applets their plasmoid title, the settings' custom controls and
      edit-mode chrome their visible labels, and every press action
      runs the same handler the click does. The Orca acceptance pass
      and the remaining surfaces (group previews, per-control labels
      on the config pages) are the open half
- [ ] Companion applets as sibling repos consumed by flake input: the
      Latte separator applet, then a full Qt 6 port of
      [applet-window-appmenu](https://github.com/psifidotos/applet-window-appmenu)
- [ ] Further continuation features: dock replication, background color
      picker, scrollable group previews

The high-priority slice of what remains, from the plan's own ordering
(each item carries its full context in its phase section there):

1. The accessibility quartet's open half: Accessible.* rollout across
   the interactive items and the Orca acceptance pass, building on the
   landed keyboard focus mode
2. The hosted CI pipeline on top of the existing gates and the
   now-maintained nested-vehicle e2e suite (the suite already runs
   desk-independently, asserting over the D-Bus surface with
   KWin-screenshot pixel goldens where pixels are the thing under test;
   standing it up in hosted CI is what remains)
3. Theming polish: the color-group audit (which theme object each of
   the ~12 Kirigami color read sites should follow)
4. The remaining live-verification recipes from the extraction ledger
   (most now runnable in the nested vehicle) and the desk checklist
5. The armed Phase 8 watch items: the intermittent startup-stranding
   defect and the lock/unlock DPMS arm, both instrumented and waiting
   to fire naturally

How it is built
===============

Every fix names its root cause and the evidence in its commit body, and
the tree defends itself: 97 ctest entries and 31 paired unit headers,
with the unit suites running under ASan+UBSan with forced asserts, a QML
compile gate that loads every shipped QML file in a real engine, contract tests
that pin the exact libplasma/KSvg/Qt behaviors the dock relies on (so a
dependency bump fails in ctest instead of misbehaving on screen), a
scene-render gate that compares real rasterized pixels against
committed goldens on pure CPU, a qmllint ratchet with a committed
baseline that only shrinks, and a coverage ratchet that makes untested
cores un-mergeable. Feel-critical changes (the parabolic engine,
the preview pipeline) additionally get live verification on a running
Wayland session with injected pointer glides and screenshot comparison
before they merge. The whole dock and its plugins can also be built
with ASan+UBSan and live Q_ASSERTs (a `-DLATTE_SANITIZE`
build, off by default), then driven either in the nested compositor or
against the real session, so undefined behavior and tripped invariants
in the running dock abort with a stack instead of misbehaving silently;
the merge gate drives that sanitized dock through the nested e2e recipes
and asserts, from the running process's own memory map, that the binary
under test really is the instrumented one, so integration-path UB fails
the gate rather than shipping. Native package recipes also share an
installed-artifact gate: live-root checks require package-manager ownership,
package-internal links resolve in the package namespace with selected targets
kept inside their artifact trees, the installed dock must be an ELF executable,
plugin identities are checked before bounded loading, and a package-style
install must settle and shut down cleanly under nested KWin while mapping the
installed executable and startup plugins rather than checkout or preinstalled
copies.
[The testing doc](docs/reference/TESTING.md) carries the
standard; [the session handoff](docs/tracking/session-handoff.md) carries the
current working state.

Relationship to other Plasma 6 ports
====================================

Two community ports exist ([ruizhi-lab/latte-dock-ng](https://github.com/ruizhi-lab/latte-dock-ng)
and [CaptSilver/latte-dock-qt6](https://github.com/CaptSilver/latte-dock-qt6),
both Wayland-only). Both were evaluated in depth, running and
live-debugging them rather than reading commit logs, before this fork
started fresh from upstream. They are tracked as reference material:
their fixes are reviewed periodically and re-derived here as new commits
when they are correct, never merged or cherry-picked wholesale. The
analysis and the sync process live in the repository docs.

Installation
============

### Requirements

- **Plasma >= 6.5**, **Qt >= 6.6**, **KDE Frameworks >= 6.5**
- LayerShellQt, PlasmaWaylandProtocols
- A Wayland session (the dock refuses to start on anything else)
- Tools: cmake >= 3.16, ninja or make, extra-cmake-modules, a C++20
  compiler

### NixOS (flake)

Add the flake as an input and enable the module:

```nix
# flake.nix
inputs.latte-dock.url = "github:whimbree/lattecotta-dock";

# in your nixosSystem call
nixpkgs.lib.nixosSystem {
  system = "x86_64-linux";
  modules = [
    inputs.latte-dock.nixosModules.default
    ./configuration.nix
  ];
};
```

```nix
# configuration.nix
{
  programs.latte-dock.enable = true;
}
```

The module installs the dock system-wide (required so KWin finds the
`.desktop` entry for window-management integration and the shell/applet
packages resolve), building it against **your** nixpkgs' Qt/KF6 stack,
not this flake's development pin. `overlays.default` is also exported if
you prefer wiring `pkgs.latte-dock` yourself, and one-off builds work
without any wiring:

```bash
nix build github:whimbree/lattecotta-dock
nix run github:whimbree/lattecotta-dock
```

### Local native package recipes

The repository carries native recipes that build packages locally; none are
published through a distribution repository or package service:

- Debian-family `.deb`: [`packaging/debian/`](packaging/debian/), using the
  tracked [`build-package`](packaging/debian/build-package) helper.
- Fedora and openSUSE `.rpm`: the shared
  [`latte-dock.spec`](packaging/rpm/latte-dock.spec), using the tracked
  [`make-snapshot-source.sh`](packaging/rpm/make-snapshot-source.sh) helper.
- Arch package: [`packaging/arch/PKGBUILD`](packaging/arch/PKGBUILD) with its
  generated [`packaging/arch/.SRCINFO`](packaging/arch/.SRCINFO).
- Gentoo ebuild overlay: [`packaging/gentoo/`](packaging/gentoo/).
- Void `xbps-src` template and staging helper:
  [`packaging/void/`](packaging/void/).

The Debian-family, shared RPM, Arch, and Void artifacts were installed in fresh
target environments and ran the installed-package nested-runtime gate. The
Gentoo recipe produced a local GPKG; its package/source reinstall supplied the
Portage-owned manifest, and the installed-artifact gate passed exact executable
and plugin mappings, nested-KWin startup, and clean shutdown. These are source-
tree build inputs, not official or published packages. No package-artifact CI,
release, tag, upload, sponsorship, or distribution endorsement exists.

### From source (any distro)

```bash
git clone https://github.com/whimbree/lattecotta-dock.git
cd lattecotta-dock
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --parallel
sudo cmake --install build
```

### Development builds

Development happens through the flake devShell, which pins the exact
Qt/KF6/Plasma revisions the port is verified against:

```bash
nix develop -c cmake -B build -G Ninja
nix develop -c cmake --build build
scripts/restart-staged.sh -d   # staged run: throwaway config home,
                               # private QML/plugin staging, never
                               # touches your real Plasma or Latte config
```

`scripts/` carries the verification tooling: `build-check.sh` (full
build + full ctest + the ratchets), the QML gates, a KWin
window-geometry dumper, and a Wayland pointer injector for driving the
live dock headlessly.

The devShell also ships `clangd`, and configuring the build emits
`build/compile_commands.json`, so an editor with the clangd extension
resolves the whole Qt6/KF6 tree with no false diagnostics out of the
box. Launch the editor from inside `nix develop` and follow
[the clangd setup guide](docs/reference/clangd-setup.md).

Running
=======

```bash
latte-dock
```

or launch **Latte Dock** from the applications menu. Layouts live in
`~/.config/latte/`; upstream Plasma 5 Latte layouts import cleanly.

Credits
=======

- **Michail Vourlakos** and the KDE community: the original Latte Dock,
  a decade of upstream work this port stands on.
- [Varlesh](https://github.com/varlesh): logos and icons.
- The reference port by [ruizhi-lab](https://github.com/ruizhi-lab/latte-dock-ng),
  whose independently-found fixes are periodically reviewed and credited
  in commit messages where their work informed ours.
- **David Goree** ([CaptSilver/latte-dock-qt6](https://github.com/CaptSilver/latte-dock-qt6)),
  whose Qt6 testability work runs through this port well beyond a handful
  of fixes: the sceneprobe visual-regression harness was adopted from his
  repository, the testing standard in `docs/reference/TESTING.md` was modeled on his
  testing commits, seven of the QML-extraction units took his extraction
  seams as their starting point, and several test suites were transplanted
  with his cases as the foundation of that coverage. Files derived from his
  work carry his SPDX-FileCopyrightText line, with the specific commit and
  source file cited in each.

License
=======

Licensed under **GPL-2.0-or-later**, same as upstream. The original KDE
repository remains at https://invent.kde.org/plasma/latte-dock with the
Plasma 5 codebase and its bug tracker.
