#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# CL-5 (the tasks-page settings audit) live leg, settling D10 (the Tasks config
# page renders but did not apply its settings - the inherited half-finished
# upstream feature; docs/known-defects.md D10). It proves the Tasks page's writes
# reach the RUNNING tasks plasmoid on a real dock, the "applies at all" question
# the audit plan's AU-5a demands answered first.
#
# THE D10 PROOF (why the config readback is decisive here). The ng eabf7c89a
# defect was that the Tasks page wrote a SEPARATE config map, invisible to the
# plasmoid's own plasmoid.configuration.* bindings. This port never had that
# split: TasksConfig.qml writes tasks.plasmoid.configuration.<key>, and Plasma's
# AppletQuickItem exposes `Q_PROPERTY(QObject *plasmoid READ applet CONSTANT)`,
# so tasks.plasmoid IS the Plasma::Applet and .configuration IS
# Applet::configuration() - the ONE KConfigPropertyMap the running plasmoid reads
# AND the object appletConfigData() reports. This recipe reads that live map: it
# seeds NON-DEFAULT values for every tasks-page control while the dock is
# stopped, restarts, and asserts appletConfigData reflects each one. A value read
# back through the applet's own live map proves the plasmoid loaded it into the
# same object the settings page writes and the readback reports - one map, three
# views. Non-default on purpose (the 032-effects shape): a readback that only
# ever showed the schema default would pass on a plasmoid that reads nothing, so
# every seeded value differs from its default and the assertion is a real signal.
#
# The RIGHT-KEY half (each control writes exactly its labelled key, no stray
# neighbour) is pinned deterministically in tests/taskshandleraudittest.cpp; the
# action-dispatch completeness (a chosen click/scroll action always has a
# handler) in tests/qml/tst_taskactions.qml. This recipe is the third leg: the
# live map carries what the page would write.
#
# A full backup of the layout file is restored on exit so the vehicle is left as
# found (the multi-key, multi-value seed makes a whole-file restore the simplest
# correct one - it cannot strand a stray key).
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"
source "$E2E_REPO/tests/e2e/matrix/matrix-lib.sh"
source "$E2E_REPO/tests/e2e/audit/audit-lib.sh"

view="$(e2e_tasks_view)" || e2e_fail "no tasks view"
applet="$(audit_tasks_applet_id "$view")" || e2e_fail "could not resolve the single tasks plasmoid under view $view"
echo "CL-5: tasks-config-apply view=$view tasks-applet=$applet"

backup="$(mktemp --suffix=.latte)"
cp "$E2E_LAYOUT" "$backup"
restore() {
    e2e_dock_stop >/dev/null 2>&1 || true
    cp "$backup" "$E2E_LAYOUT"
    rm -f "$backup"
}
trap restore EXIT

# tcfg <key> <value>: write into the tasks plasmoid's own config subgroup
# ([Containments][view][Applets][applet][Configuration][General]) - the on-disk
# home of the tasks.plasmoid.configuration.<key> map (092-task-reorder shape).
tcfg() {
    kwriteconfig6 --file "$E2E_LAYOUT" --group Containments --group "$view" \
        --group Applets --group "$applet" --group Configuration --group General \
        --key "$1" "$2"
}

# ---- seed non-default tasks config while the dock is stopped ------------------
# stop first: a clean SIGTERM flushes the CURRENT config, so the seed must land
# after the flush (030/110/032 order). Each value differs from its main.xml
# default (comment states the default) so the readback is a real signal.
e2e_dock_stop >/dev/null 2>&1 || true

# AU-5b Badges (91-95)
tcfg showInfoBadge false                    # default true
tcfg showProgressBadge false                # default true
tcfg showAudioBadge false                   # default true
tcfg audioBadgeActionsEnabled false         # default true
tcfg infoBadgeProminentColorEnabled true    # default false
# AU-5b Interaction (96-99); isPreferredForPositionShortcuts is view-gated, its
# checkbox is disabled unless this view owns shortcuts, so it is not seeded here
tcfg isPreferredForDroppedLaunchers false   # default true
tcfg showWindowActions true                 # default false
tcfg previewWindowAsPopup true              # default false
# AU-5b Filters (100-105)
tcfg showOnlyCurrentScreen true             # default false
tcfg showOnlyCurrentDesktop true            # default false
tcfg showOnlyCurrentActivity false          # default true
tcfg showWindowsOnlyFromLaunchers true      # default false
tcfg hideAllTasks true                      # default false
tcfg groupTasksByDefault false              # default true
# AU-5c Animations (106-110)
tcfg animationLauncherBouncing false        # default true
tcfg animationWindowInAttention false       # default true
tcfg animationNewWindowSliding false        # default true
tcfg animationWindowAddedInGroup false      # default true
tcfg animationWindowRemovedFromGroup false  # default true
# AU-5c Launchers (111-113): 2 = Global (default 0 = Unique)
tcfg launchersGroup 2
# AU-5c Scrolling (114-116)
tcfg scrollTasksEnabled true                # default false
tcfg autoScrollTasksEnabled false           # default true
tcfg manualScrollTasksType 2                # default 1
# AU-5c Actions (117-121): each seeded to a different offered enum value
tcfg leftClickAction 7                      # default 6 (PresentWindows) -> 7 (PreviewWindows)
tcfg middleClickAction 1                    # default 2 (NewInstance) -> 1 (Close)
tcfg hoverAction 2                          # default 9 (Preview+Highlight) -> 2 (HighlightWindows)
tcfg taskScrollAction 2                     # default 1 (ScrollTasks) -> 2 (ScrollToggleMinimized)
tcfg modifierClickAction 1                  # default 0 (None) -> 1 (Close)
tcfg modifier 3                             # default 1 (Ctrl) -> 3 (Meta)
tcfg modifierClick 2                        # default 0 (LeftClick) -> 2 (RightClick)

e2e_dock_start 90 || e2e_fail "dock never settled after seeding the tasks config"

# ---- assert the applet's LIVE config map reflects every seeded value ----------
snap="$(mktemp)"
audit_applet_config_snapshot "$view" "$applet" > "$snap" \
    || e2e_fail "appletConfigData snapshot failed for tasks applet $applet"
echo "--- tasks applet config snapshot (seeded keys) ---"
grep -E '^(show|hide|group|animation|launchersGroup|scroll|auto|manual|leftClick|middleClick|hover|taskScroll|modifier|isPreferred|previewWindow|audioBadge|infoBadge)' "$snap" || true

rc=0
# AU-5b Badges
audit_assert_reflects "$snap" showInfoBadge false                  || rc=1
audit_assert_reflects "$snap" showProgressBadge false              || rc=1
audit_assert_reflects "$snap" showAudioBadge false                 || rc=1
audit_assert_reflects "$snap" audioBadgeActionsEnabled false       || rc=1
audit_assert_reflects "$snap" infoBadgeProminentColorEnabled true  || rc=1
# AU-5b Interaction
audit_assert_reflects "$snap" isPreferredForDroppedLaunchers false || rc=1
audit_assert_reflects "$snap" showWindowActions true               || rc=1
audit_assert_reflects "$snap" previewWindowAsPopup true            || rc=1
# AU-5b Filters
audit_assert_reflects "$snap" showOnlyCurrentScreen true           || rc=1
audit_assert_reflects "$snap" showOnlyCurrentDesktop true          || rc=1
audit_assert_reflects "$snap" showOnlyCurrentActivity false        || rc=1
audit_assert_reflects "$snap" showWindowsOnlyFromLaunchers true    || rc=1
audit_assert_reflects "$snap" hideAllTasks true                    || rc=1
audit_assert_reflects "$snap" groupTasksByDefault false            || rc=1
# AU-5c Animations
audit_assert_reflects "$snap" animationLauncherBouncing false      || rc=1
audit_assert_reflects "$snap" animationWindowInAttention false     || rc=1
audit_assert_reflects "$snap" animationNewWindowSliding false      || rc=1
audit_assert_reflects "$snap" animationWindowAddedInGroup false    || rc=1
audit_assert_reflects "$snap" animationWindowRemovedFromGroup false || rc=1
# AU-5c Launchers / Scrolling / Actions
audit_assert_reflects "$snap" launchersGroup 2                     || rc=1
audit_assert_reflects "$snap" scrollTasksEnabled true              || rc=1
audit_assert_reflects "$snap" autoScrollTasksEnabled false         || rc=1
audit_assert_reflects "$snap" manualScrollTasksType 2              || rc=1
audit_assert_reflects "$snap" leftClickAction 7                    || rc=1
audit_assert_reflects "$snap" middleClickAction 1                  || rc=1
audit_assert_reflects "$snap" hoverAction 2                        || rc=1
audit_assert_reflects "$snap" taskScrollAction 2                   || rc=1
audit_assert_reflects "$snap" modifierClickAction 1               || rc=1
audit_assert_reflects "$snap" modifier 3                           || rc=1
audit_assert_reflects "$snap" modifierClick 2                      || rc=1
rm -f "$snap"
(( rc == 0 )) || e2e_fail "a tasks config value did not reflect through the applet's live map (see the snapshot above)"

# ---- the plasmoid stayed functional under the non-default config -------------
# hideAllTasks=true and the filters are behaviourally inert on this launcher-only
# vehicle (no window tasks to filter), but the plasmoid must still consume the
# config and render its launchers - a crash-or-hang here would be a real apply
# defect masquerading as a passing readback. viewTasksData answering with the
# launcher rows confirms the tasks plasmoid is alive and reading its config.
tasks_json="$(e2e_json viewTasksData u "$view")" || e2e_fail "viewTasksData failed after the seed"
launcher_count="$(grep -o '"isLauncher":true' <<<"$tasks_json" | wc -l)"
[[ "$launcher_count" -ge 1 ]] \
    || e2e_fail "the tasks plasmoid exposes no launchers after the seed - it did not survive the non-default config"
echo "tasks plasmoid alive under the seeded config: $launcher_count launcher row(s) in viewTasksData"

echo "PASS: CL-5 D10 - the Tasks page's writes reach the running plasmoid; appletConfigData"
echo "      reflects all 30 seeded tasks-page values through the applet's live map (AU-5a/5b/5c)"
