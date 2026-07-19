# Task-manager backend: vendoring vs closer KDE integration

Research pass 2026-07-12, feeding the Phase 12 "raise the direction
with KDE" item. Question: can the Phase 6 vendored backend
(org.kde.latte.private.tasks, commit 14c973b3) be replaced by closer
integration with what KDE actually ships today, and what would each
option cost?

## What we vendored

~1,600 lines in plasmoid/plugin, provenance preserved from
plasma-desktop (Eike Hein's backend):

- backend.{h,cpp}: jump-list / places / recent-document context-menu
  actions, application-url helpers, drag mime helpers, KWin
  WindowView/HighlightWindow D-Bus with QDBusServiceWatcher,
  kactivitymanagerd plugin settings.
- smartlauncherbackend/item: launcher badges + progress over the Unity
  launcher D-Bus API.
- types, plugin registration, kcfg.

## What KDE ships today (verified against Plasma 6.6.5 on this
## system and invent.kde.org master, 2026-07-12)

- `org.kde.plasma.private.taskmanager` (the QML module the old shim
  route depended on) NO LONGER EXISTS. The installed system's
  plasma/private QML tree has no taskmanager entry at all.
- The Task Manager applet is one C++ binary applet plugin
  (plasma-desktop: lib/qt-6/plugins/plasma/applets/
  org.kde.plasma.taskmanager.so). On master, applets/taskmanager
  contains the SAME backend.cpp/backend.h/smartlauncherbackend/
  smartlauncheritem files we vendored, compiled directly into the
  applet. KDE keeps this code private on purpose; there is no plugin
  subdirectory and no QML module anymore.
- The public surface is libtaskmanager (plasma-workspace): the model
  stack (TasksModel, WaylandTasksModel, launcher/startup models,
  grouping/filter proxies, ActivityInfo, VirtualDesktopInfo) plus
  tasktools. Checked on master: no jump-list, menu-building or
  smart-launcher helpers have been added since 6.6. Latte already
  consumes libtaskmanager for everything it covers.
- No public KDE API exists anywhere for: building jump-list/places/
  recent-doc QAction lists for QML, or Unity-API badge tracking.
  Web search found no upstream discussion of exposing them.

## What the vendored code actually is

backend.cpp is glue over PUBLIC, stable KF6 APIs: KService/
KServiceAction, KApplicationTrader, KFilePlacesModel, KIO
ApplicationLauncherJob, KDesktopFile, PlasmaActivities, KWindowSystem/
KWindowEffects, plain QDBus. It is not a forked library; it is the
same private-glue-over-public-API architecture upstream's own applet
uses. The Frameworks APIs underneath carry KDE's deprecation policy,
so the vendor's foundation is as stable as anything in the stack.

## Reference-fork evidence

- latte-dock-ng abandoned the shim too (it had no module left to shim
  to). They now vendor their own 579-line
  contextmenuactionsbackend (3 invokables: jump lists, places, recent
  documents) and dropped everything else. What that narrowing costs,
  read at ng checkout c705aa7e7:
  - Smart-launcher badges gone (unread counts, progress bars, urgent
    flash over the Unity launcher D-Bus API), but TaskIcon.qml:67
    still instantiates the deleted SmartLauncherItem type, so the
    Loader errors on every task instead of the feature being removed
    cleanly.
  - KWin window-view and hover-highlight gone, but main.qml's
    Component.onCompleted still runs
    root.activateWindowView.connect(backend.activateWindowView) on a
    backend without that slot: the connect throws, and the JS abort
    also kills updateListViewParent() and the context-menu error
    check that follow it - collateral silent breakage, the exact
    handler-abort class as the ComboBox.pressed bug.
  - backend.windowViewAvailable reads undefined in
    TaskMouseArea.qml:222, so the grouped-task "present windows"
    gate is permanently false with no error at all
    (TaskItem.qml:499's comment admits it is unreliable).
  - Audio-stream OSD and the drag/process/menu helpers (parentPid,
    generateMimeData, isApplication, applicationCategories,
    globalRect, ungrabMouse) also gone from the backend surface.
  The lesson: narrowing the vendored surface without sweeping every
  QML consumer converts missing features into silent wrong-behavior.
- latte-dock-qt6 vendors the full backend (the shape we adopted).
- Net: three independent ports all copy the same plasma-desktop file.
  That is the strongest argument to hand KDE for making it public.

## Options

A. Keep the full vendor (status quo)
   + Qt5-feature-complete (badges, jump lists, places, recents).
   + Zero dependence on Plasma private internals; only stable KF6
     public API underneath.
   + Same architecture as upstream's own applet - defensible in
     review, and our copy stays diffable against upstream's
     backend.cpp for cheap fix-porting.
   - ~1,600 lines to watch; drift risk vs plasma-desktop behavior
     (menu subtleties, new activities semantics) unless synced.

B. Trim to a minimal backend (latte-dock-ng's direction)
   + Roughly a third of the surface.
   - Loses smart-launcher badges, breaking the Qt5-faithful
     agreement for a feature Latte's UI exposes. Rejected unless the
     feature is deliberately dropped some day.

C. Rewrite the glue in Latte's own idiom (de-provenance)
   - Same maintenance burden, but loses the line-for-line
     diffability against upstream's backend.cpp that makes syncing
     cheap. No real upside. Rejected.

D. Upstream the helpers into libtaskmanager (the native endgame)
   Propose (issue or draft MR on plasma-workspace) moving jump-list/
   places/recent-doc action building and SmartLauncher tracking from
   the applet into libtaskmanager as public API. libtaskmanager
   exists precisely to share task-manager logic, tasktools already
   holds shared helpers, and the applet backend is the last private
   piece every third-party task manager must copy (three forks as
   evidence).
   + The only real fix; ends the vendor eventually; helps the
     ecosystem; folds naturally into the Phase 12 outreach that must
     happen before any upstream submission anyway.
   - Slow: needs KDE buy-in and API design (QAction lists across the
     QML boundary is awkward as public API; upstream may prefer a
     menu-model shape); lands in a future Plasma release at the
     earliest, so the vendor stays as fallback behind a version
     check for years either way.

## Recommendation

Keep A as the working baseline, pursue D as the outreach pitch:

1. Now: record the exact plasma-desktop commit the vendor was taken
   from in the plugin headers, and add "diff upstream
   applets/taskmanager/backend.cpp against our copy" to the
   reference-fork sync checklist, so drift is a diff and not a hunt.
2. Phase 12 outreach: pitch the libtaskmanager public API with the
   three-forks-all-vendor-this evidence, alongside the X11
   support-level question that item already carries.
3. If upstream accepts: migrate behind a Plasma version check and
   delete the vendor when the minimum supported Plasma allows.

Note for completeness: the org.kde.taskmanager shadowing regression
in the staging notes is a packaging concern (foreign-Qt system copy
of the PUBLIC libtaskmanager plugin resolving ahead of the pinned
one on NixOS), unrelated to the vendoring question - closer
integration would not remove that failure class.
