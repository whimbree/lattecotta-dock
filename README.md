# <img src="logo.png" width="48"/> Latte Dock (Plasma 6 port)

Latte is a dock based on plasma frameworks that provides an elegant and
intuitive experience for your tasks and plasmoids. It animates its contents
with a parabolic zoom effect and tries to be there only when it is needed.

**"Art in Coffee"**

This repository is a from-scratch **Plasma 6 / Qt 6 / Wayland** port of KDE's
[latte-dock](https://invent.kde.org/plasma/latte-dock), forked with the full
original history intact. Upstream development stopped in the Plasma 5 era;
this fork carries the codebase to Plasma 6.6 / Qt 6.11 / KDE Frameworks 6 and
is developed against a real daily-driver Wayland session.

Status
======

Working and in daily use on Wayland: multi-dock layouts, the tasks applet
with previews and badges, edit mode with widget rearranging, per-applet
context menus and settings, indicators, layer-shell placement and struts,
parabolic zoom, and the settings windows. X11 is best-effort: it must
compile, but it is not run-tested and never blocks Wayland work.

The port is tracked as an explicit checklist in
[docs/PORTING_PLAN.md](docs/PORTING_PLAN.md): 13 phases from build system to
upstream preparation, one commit-traceable item per task. Read
[docs/session-handoff.md](docs/session-handoff.md) for the current working
state. Eventual upstream contribution to KDE is the goal, which is why the
history is kept clean and every fix names its root cause and evidence.

Relationship to other Plasma 6 ports
====================================

Two community ports exist (ruizhi-lab/latte-dock-ng and
CaptSilver/latte-dock-qt6, both Wayland-only). Both were evaluated in depth,
running and live-debugging them rather than reading commit logs, before this
fork started fresh from upstream. They are tracked as reference material:
their fixes are reviewed periodically and re-derived here as new commits when
they are correct, never merged or cherry-picked. The analysis and the sync
process live in the repository docs.

Building
========

Development happens on NixOS through the flake, which pins the exact
nixpkgs revision of the desktop it runs against (Plasma 6.6 / Qt 6.11):

```
nix develop -c cmake -B build
nix develop -c cmake --build build
```

The staged run never touches your real Plasma or Latte configuration; it
uses a throwaway config home and a private QML/plugin staging tree:

```
scripts/restart-staged.sh -d
```

`scripts/` also carries the verification tooling used during development:
a QML compile gate, a KWin window-geometry dumper, and a Wayland pointer
injector for headless interaction tests. On non-Nix systems the classic
CMake build applies, with dependencies as declared in `CMakeLists.txt`
(Qt >= 6.6, KDE Frameworks >= 6.5, Plasma >= 6.5, LayerShellQt,
PlasmaWaylandProtocols).

Upstream and license
====================

Latte Dock was created by Michail Vourlakos and developed by the KDE
community; logos and icons by [Varlesh](https://github.com/varlesh). This
fork exists to continue that work on Plasma 6, not to replace it; if
upstream development resumes, the intent is to contribute this port back.

Licensed under GPL-2.0-or-later, same as upstream. See the original KDE
repository at https://invent.kde.org/plasma/latte-dock for the Plasma 5
codebase and its bug tracker.
