---
name: latte-build-env
description: Building, QML staging, import-path rules and environment traps for the staged latte-dock Plasma 6 port on this NixOS machine.
---

# Latte build environment and staging

Everything here runs from the repo root, ~/Projects/latte-dock,
inside the flake devShell (`nix develop -c ...`). The devShell is pinned to
the exact nixpkgs revision the machine's desktop runs (Plasma 6.6.5 /
Qt 6.11.0); see the comment at the top of flake.nix for why that pin matters.

## Build commands

```
nix develop -c cmake --build build --target latte-dock            # the app
nix develop -c cmake --build build --target lattecontainmentplugin # containment QML plugin
nix develop -c cmake --build build --target lattetasksplugin       # tasks QML plugin
nix develop -c cmake --build build                                 # everything
```

The containment plugin lives in containment/plugin/, the tasks plugin in
plasmoid/plugin/. There is also lattecoreplugin under declarativeimports/core/.
A bare build with no --target builds all of them.

Staleness check: to decide whether the built artifacts are current, run the
bare build and read ninja's answer ("no work to do" means everything is
up to date). Do NOT reason from mtime comparisons against
build/bin/latte-dock: plugin sources (containment/plugin/, plasmoid/plugin/,
declarativeimports/) link into their own .so files, so the app binary
legitimately predates edits to them, and that comparison has produced a
confident wrong "stale binary" diagnosis before.

ABSOLUTE RULE: stop the dock before any rebuild. A running dock executes the
binary at build/bin, and rebuilding under it makes the process run a deleted
binary and crash confusingly (docs/session-handoff.md, Known traps).
`scripts/restart-staged.sh` does the stop correctly: it sends SIGTERM, then
SIGCONT (a SIGSTOPped instance never runs its TERM handler otherwise),
escalates to SIGKILL, and refuses to start while any instance survives.
Note it stops AND relaunches; for the stop-rebuild-launch cycle you need a
stop-only step first - use the stop sequence documented in
latte-live-verification (TERM, CONT, wait, KILL check), then build, then
restart-staged.sh. Stacked instances are real: three docks fighting over
layer surfaces was observed when a plain pkill left a stopped process
alive.

## What needs what

- QML or package changes (containment/package/, plasmoid/package/, shell,
  indicators): NO rebuild. `scripts/run-staged.sh` restages on every run, so
  restart-staged.sh alone picks them up.
- C++ changes (app/, containment/plugin/, plasmoid/plugin/,
  declarativeimports/): need the cmake build above, dock stopped first.
- After any QML edit, before launching:

```
nix develop -c scripts/qml-compile-gate.sh
```

The gate compiles every staged QML file in a real engine (Qt.createComponent,
offscreen), catching removed-type and removed-property errors in lazy,
interaction-only components that would otherwise need a click in a live
session to surface. It compiles but does not instantiate: runtime binding
evaluation is not checked. It skips files importing org.kde.latte.private.app
(registered inside the binary, never visible to a standalone engine) and the
dead *.5.2[0-5].qml version-ladder variants, and reports both skip counts.

## Staging model

`qml_env_stage` in scripts/lib-qml-env.sh runs
`cmake --install build --prefix build/_qmlstage` into a fresh stage on every
run. Binaries stay in build/bin; the stage holds the installed QML packages,
shell, indicators and Latte's C++ plugins under build/_qmlstage/lib/plugins.

Manifest quirk: cmake --install unconditionally rewrites
build/install_manifest.txt, which ECM's appstreamtest reads. The script saves
the manifest before staging and restores it after (or deletes it if there was
none), so the staged install never changes what that test validates. Do not
"simplify" that away.

Config: run-staged.sh uses a throwaway XDG_CONFIG_HOME at build/_runconfig by
default (override with LATTE_CONFIG_HOME), so the user's real Latte and Plasma
configuration is never touched. Pass `--user-config` to restart-staged.sh or
run-staged.sh to use the real one deliberately (`scripts/start-dock.sh` is the
daily-driver shorthand for exactly that). `BUILD=<dir>` redirects the whole
stage-and-run at an alternate build tree (how instrumented build-probe/
binaries run without touching build/bin under a live dock). XDG_DATA_DIRS
puts the staged share tree first so the staged shell package, containment and
indicators win, with the system dirs behind them for icons and themes.
`LATTE_RUN_WRAPPER="gdb -batch -ex run -ex bt --args"` gets a backtrace run.

Script roles (challenged and settled 2026-07-16): run-staged.sh is the
side-effect-free ENVIRONMENT CORE (foreground exec, manages no other
instance - what nested-compositor harnesses and gdb/timeout wrappers
call); restart-staged.sh is the DESK LIFECYCLE (safe kill + detach,
delegates to the core). The file boundary is the safety boundary: the
kill sequence must never live in the script harnesses invoke.

## Gate discipline

`scripts/gate-all.sh` is the single canonical gate runner (build-check:
full build + full ctest + coverage ratchet, qmllint gate,
sceneprobe gate). Its EXIT CODE is the only verdict; success prints
`GATES: ALL OK @ <sha>` last and stamps build/_gates-passed. The
committed pre-push hook (scripts/git-hooks/pre-push, enabled via
`git config core.hooksPath scripts/git-hooks`) refuses unstamped code
pushes; docs-only drift is exempt. Never scrape logs for gate success
and never combine reading a verdict with acting on it in one shell
invocation (docs/TESTING.md carries the incident this rule comes from).

## QML import path doctrine

This is the section that prevents the worst regressions. Read it before
touching flake.nix, the scripts, or any *_PATH variable.

Layering, from weakest to strongest (later -import wins in the gate; the
launcher flattens the same order into QML2_IMPORT_PATH):

1. LATTE_QML_MODULE_PATH, exported by the flake devShell. It is built at eval
   time from the PINNED dependency set (the makeSearchPath list in flake.nix)
   and is the only trustworthy source of QML module paths. mkShell does not
   run the Qt env hooks, so nothing else exports correct paths.
2. ldd-derived pins: lib-qml-env.sh walks the /nix/store paths that
   build/bin/latte-dock and liblattetasksplugin.so actually link and imports
   their qml dirs. These outrank the list, guaranteeing modules resolve from
   the exact packages the binaries link even if a second copy exists.
3. The staged Latte modules (build/_qmlstage/lib/qml) win last, over
   everything.

The desktop session's own variables (QML2_IMPORT_PATH, QML_IMPORT_PATH,
NIXPKGS_QT6_QML_IMPORT_PATH, NIXPKGS_QML_SEARCH_PATHS) leak differently
pinned Qt5 and Plasma paths whose plugins fail to dlopen here (private-API
symbol versioning). lib-qml-env.sh unsets the first two and deliberately
ignores the rest. Keep it that way.

NEVER append the system or session QML root to any of these paths. A foreign
Qt copy of a module Latte also resolves is a silent shadow: a broad append of
the system Qt6 QML tree once made `import org.kde.taskmanager` resolve from
the system copy and replaced the dock's right-click menu with the stock task
menu, with no error anywhere. This is the canonical regression in CLAUDE.md's
Regression discipline section.

To give an applet its private QML modules, add the OWNING PACKAGE to the
flake's LATTE_QML_MODULE_PATH list. See the applet-private-modules block in
flake.nix (commit 4c9f3bc7): plasma-desktop, bluedevil, plasma-nm,
kdeconnect-kde, kdeplasma-addons, powerdevil, print-manager and friends were
added this way. Same-pin whole-package roots are safe by construction: any
module duplicated across packages from the same pin is the identical
derivation family, and the staged Latte modules still win last. What is never
safe is a root from a different pin.

After editing flake.nix, the next `nix develop` re-evaluates it; the first
run may download the new packages. No other action is needed.

## Plugin path and platform theme

run-staged.sh UNSETS QT_PLUGIN_PATH entirely, sets
`QT_QPA_PLATFORMTHEME=` (empty), and hands Latte's plugin dirs over as
`LATTE_EXTRA_PLUGIN_PATHS` (the staged plugin tree plus the exact
kwindowsystem plugin leaf the binary links). main.cpp feeds that
variable into process-local library paths. All three are load-bearing,
per the comments in the script:

- The session's QT_PLUGIN_PATH points at the system Plasma's plugins, built
  against a different Qt; loading its platform-theme plugin into this process
  "segfaults in QCoreApplication::init". The nix-built Qt finds its own
  plugins through baked-in paths, so the session list is dropped and the
  platform theme integration skipped entirely.
- Latte's own C++ plugins are staged, not system-installed, most
  importantly plasma/containmentactions/org.kde.latte.contextmenu, which
  builds the dock's right-click menu; the kwindowsystem leaf gives the
  process its wayland backend (dialog shadows, popup slide).
- Why NOT QT_PLUGIN_PATH for those: the dock's environment is copied
  verbatim into every app it launches (KIO's systemd runner), and a
  child of a different Qt build dlopening the pinned kwindowsystem
  plugin is an ABI mismatch. A LATTE_-namespaced variable is inert for
  children (the 00a6766c child-env-leak fix).

Do not add the session's plugin dirs back "to fix" a missing plugin; find the
owning staged or pinned location instead.

## CMake feature probes

The top-level CMakeLists.txt runs a try_compile probe,
LATTE_LAYERSHELL_HAS_SET_SCREEN (source: cmake/CheckLayerShellSetScreen.cpp),
detecting whether LayerShellQt::Window::setScreen(QScreen*) exists. That API
was added in LayerShellQt 6.6 and later removed upstream again; the fallback
is plain QWindow::setScreen(). The result gates #ifdef blocks in
app/wm/waylandlayershell.cpp. When layer-shell output pinning misbehaves
(dock or edit chrome on the wrong monitor), check the cached value:

```
grep LATTE_LAYERSHELL_HAS_SET_SCREEN build/CMakeCache.txt
```

Since 46ce2dfbb the probe is guarded: the answer caches keyed on
LayerShellQt_DIR (a re-pin re-probes automatically, plain reconfigures
do not), only a missing-setScreen-member diagnostic counts as a real
"no", and any other probe failure is a FATAL_ERROR naming the likely
environment cause - a broken configure can no longer silently flip the
define.

## Blast radius and causation (from CLAUDE.md)

Environment, launcher, build and module-resolution changes are never narrow.
Adding a directory to QML2_IMPORT_PATH, QT_PLUGIN_PATH, XDG_DATA_DIRS or the
like can shadow ANY same-named module or plugin the process already resolves.
When something regresses after such a change, verify causation: revert that
single variable and retest, or read the running process's actual environment,
before "fixing" the suspected cause. The launcher QML-path change and the
QT_PLUGIN_PATH gap were distinct faults; only reverting and re-reading the
live env told them apart. After a fix, confirm against the real running dock,
not just a green build.

## Quick triage

| Symptom | Likely cause |
| --- | --- |
| "module X is not installed" at applet load | Owning package missing from the flake's LATTE_QML_MODULE_PATH list; add the package, re-enter nix develop |
| Stock Plasma task menu on right-click | Foreign org.kde.taskmanager shadowing Latte's pinned copy (a system/session QML root got appended), or QT_PLUGIN_PATH lost the staged containmentactions plugin |
| Segfault at startup inside QCoreApplication | Session platform-theme plugin leaked in; QT_QPA_PLATFORMTHEME must be empty and QT_PLUGIN_PATH staged-only |
| Dock crashed right after a rebuild | Rebuilt while a dock was running from build/bin; stop with restart-staged.sh first |
| Dock never dies, instances stack | SIGSTOPped orphan instance; restart-staged.sh handles TERM+CONT+KILL, use it instead of pkill |

## Related skills

- latte-live-verification: launching and verifying against the live session.
- latte-plasma6-defect-families: module-shadowing fingerprints and other
  recurring defect patterns.
