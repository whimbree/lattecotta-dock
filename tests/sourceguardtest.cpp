/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Source-level guards for small routing/token correctness fixes whose owning
// View / Corona / settings-dialog graphs cannot be constructed offscreen:
//
//   * VisibilityManager::updateSidebarState  '==' typo for '=' (the sidebar
//     state was compared and discarded, never set)
//   * Settings::Controller::Layouts::modeIsChanged  missing '>' (pointer
//     arithmetic plus unqualified self-call, infinite recursion)
//   * View::WindowsTracker enabled Binding requesters, including empty-area
//     window actions and the floating-gap property spelling
//   * Wayland window-signal routing: maximize edges stay immediate while noisy
//     window properties stay coalesced
//   * VisibilityManager strut routing: discrete exclusive-zone thickness
//     changes publish directly while geometry churn stays throttled
//   * SC-T3 (the D29 narrow middle-click dispatch readback): the production QML
//     branch, stable identity, reporter aliases, and containment-lifecycle scope
//
// The first two guards follow David Goree's latte-dock-qt6
// (tests/sourceguardtest.cpp at 81384003, github.com/CaptSilver/latte-dock-qt6):
// read the real source, brace-match the function body, strip whitespace and
// assert the fixed token form both positively and negatively, so the typo
// cannot silently return. Only those two cases are adopted; the remaining
// guards are specific to this tree. The rest of his file pins a
// delegation-helper architecture that this tree deliberately does not share
// (docs/archive/captsilver-testability-adoption.md, the not-adopting list).

#include <QFile>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QtTest>

class SourceGuardTest : public QObject
{
    Q_OBJECT

private:
    static QString readFile(const QString &rel)
    {
        QFile f(QStringLiteral("%1/%2").arg(QStringLiteral(REPO_ROOT), rel));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString();
        }
        return QString::fromUtf8(f.readAll());
    }

    // Brace-matched body after the first signature or object token.
    static QString functionBody(const QString &src, const QString &sig)
    {
        const int s = src.indexOf(sig);
        if (s == -1) {
            return QString();
        }
        const int brace = src.indexOf(QLatin1Char('{'), s + sig.size());
        if (brace == -1) {
            return QString();
        }
        int depth = 0;
        int i = brace;
        for (; i < src.size(); ++i) {
            if (src.at(i) == QLatin1Char('{')) {
                ++depth;
            } else if (src.at(i) == QLatin1Char('}') && --depth == 0) {
                ++i;
                break;
            }
        }
        return src.mid(brace, i - brace);
    }

    static QString stripped(const QString &body)
    {
        QString s = body;
        s.remove(QRegularExpression(QStringLiteral("\\s+")));
        return s;
    }

private Q_SLOTS:
    void visibilityManager_updateSidebarState_assignsState();
    void layoutsController_modeIsChanged_delegatesToModel();
    void windowsTrackerBinding_keepsRequesters();
    void waylandWindowSignals_keepDeliveryPolicy();
    void visibilityManager_strutThicknessBypassesGeometryThrottle();
    void middleClickDispatch_keepsProductionRecordingContract();
    void middleClickDispatch_keepsContainmentLifecycleScope();
};

void SourceGuardTest::visibilityManager_updateSidebarState_assignsState()
{
    const QString s = stripped(functionBody(readFile(QStringLiteral("app/view/visibilitymanager.cpp")),
                                            QStringLiteral("void VisibilityManager::updateSidebarState()")));
    QVERIFY2(!s.isEmpty(), "updateSidebarState() not found");
    // Must ASSIGN the freshly computed state before emitting, not compare-and-discard.
    QVERIFY2(s.contains(QStringLiteral("m_isSidebar=cursidebarstate;")),
             "updateSidebarState must assign m_isSidebar (single '='), not compare it");
    QVERIFY2(!s.contains(QStringLiteral("m_isSidebar==cursidebarstate;")),
             "updateSidebarState has a discarded '==' comparison statement");
}

void SourceGuardTest::layoutsController_modeIsChanged_delegatesToModel()
{
    const QString s = stripped(functionBody(readFile(QStringLiteral("app/settings/settingsdialog/layoutscontroller.cpp")),
                                            QStringLiteral("bool Layouts::modeIsChanged() const")));
    QVERIFY2(!s.isEmpty(), "Layouts::modeIsChanged() not found");
    QVERIFY2(s.contains(QStringLiteral("m_model->modeIsChanged()")),
             "modeIsChanged must delegate via m_model->modeIsChanged()");
    QVERIFY2(!s.contains(QStringLiteral("m_model-modeIsChanged")),
             "modeIsChanged has the missing-'>' pointer-arithmetic / self-recursion typo");
}

void SourceGuardTest::windowsTrackerBinding_keepsRequesters()
{
    const QString source = readFile(QStringLiteral("containment/package/contents/ui/BindingsExternal.qml"));
    const int section = source.indexOf(QStringLiteral("//! View::WindowsTracker bindings"));
    QVERIFY2(section != -1, "View::WindowsTracker bindings section not found");

    QString binding = functionBody(source.mid(section), QStringLiteral("Binding"));
    QVERIFY2(!binding.isEmpty(), "View::WindowsTracker Binding not found");
    binding.remove(QRegularExpression(QStringLiteral("/\\*[\\s\\S]*?\\*/")));
    binding = stripped(binding);

    const QString trackerTarget = QStringLiteral("target:latteView&&latteView.windowsTracker?latteView.windowsTracker:null");
    QVERIFY2(binding.contains(trackerTarget)
             && binding.contains(QStringLiteral("property:\"enabled\"")),
             "View::WindowsTracker enabled Binding not found after its section marker");

    const int valueStart = binding.indexOf(QStringLiteral("value:"));
    const int restoreStart = binding.indexOf(QStringLiteral("restoreMode:"), valueStart);
    QVERIFY2(valueStart != -1 && restoreStart != -1, "WindowsTracker value expression not found");
    const QString actual = binding.mid(valueStart + 6, restoreStart - valueStart - 6);
    const QString expected = QStringLiteral(
        "(latteView&&latteView.visibility&&!(latteView.visibility.mode===LatteCore.Types.AlwaysVisible"
        "||latteView.visibility.mode===LatteCore.Types.WindowsGoBelow"
        "||latteView.visibility.mode===LatteCore.Types.AutoHide))"
        "||indexer.clientsTrackingWindowsCount>0"
        "||root.dragActiveWindowEnabled"
        "||Plasmoid.configuration.closeActiveWindowEnabled"
        "||Plasmoid.configuration.scrollAction===LatteContainment.Types.ScrollToggleMinimized"
        "||((root.backgroundOnlyOnMaximized||Plasmoid.configuration.solidBackgroundForMaximized"
        "||root.disablePanelShadowMaximized||root.windowColors!==LatteContainment.Types.NoneWindowColors))"
        "||(root.screenEdgeMarginEnabled&&Plasmoid.configuration.hideFloatingGapForMaximized)");
    QCOMPARE(actual, expected);
    QVERIFY2(!binding.contains(QStringLiteral("root.screenEdgeMarginsEnabled")),
             "WindowsTracker hide-gap arm uses the nonexistent plural screenEdgeMarginsEnabled property");
}

void SourceGuardTest::waylandWindowSignals_keepDeliveryPolicy()
{
    const QString s = stripped(functionBody(readFile(QStringLiteral("app/wm/waylandinterface.cpp")),
                                            QStringLiteral("void WaylandInterface::trackWindow(KWayland::Client::PlasmaWindow *w)")));
    QVERIFY2(!s.isEmpty(), "WaylandInterface::trackWindow() not found");

    const QString maximizedSignal = QStringLiteral("&PlasmaWindow::maximizedChanged");
    QCOMPARE(s.count(maximizedSignal), 1);
    QVERIFY2(s.contains(QStringLiteral("connect(w,&PlasmaWindow::maximizedChanged,this,&WaylandInterface::updateWindowMaximized);")),
             "maximizedChanged must retain its immediate updateWindowMaximized route");

    const QStringList noisySignals{
        QStringLiteral("activeChanged"),
        QStringLiteral("titleChanged"),
        QStringLiteral("fullscreenChanged"),
        QStringLiteral("geometryChanged"),
        QStringLiteral("minimizedChanged"),
        QStringLiteral("shadedChanged"),
        QStringLiteral("skipTaskbarChanged"),
        QStringLiteral("onAllDesktopsChanged"),
        QStringLiteral("parentWindowChanged"),
        QStringLiteral("plasmaVirtualDesktopEntered"),
        QStringLiteral("plasmaVirtualDesktopLeft"),
        QStringLiteral("plasmaActivityEntered"),
        QStringLiteral("plasmaActivityLeft"),
    };
    for (const QString &signal : noisySignals) {
        const QString signalReference = QStringLiteral("&PlasmaWindow::%1").arg(signal);
        const QString expectedConnection = QStringLiteral("connect(w,%1,this,&WaylandInterface::updateWindow);").arg(signalReference);
        QCOMPARE(s.count(signalReference), 1);
        QVERIFY2(s.contains(expectedConnection),
                 qPrintable(QStringLiteral("%1 must retain its coalesced updateWindow route").arg(signal)));
    }
}

void SourceGuardTest::visibilityManager_strutThicknessBypassesGeometryThrottle()
{
    const QString s = stripped(functionBody(readFile(QStringLiteral("app/view/visibilitymanager.cpp")),
                                            QStringLiteral("void VisibilityManager::setMode(Latte::Types::Visibility mode)")));
    QVERIFY2(!s.isEmpty(), "VisibilityManager::setMode() not found");
    QCOMPARE(s.count(QStringLiteral("&VisibilityManager::strutsThicknessChanged")), 1);
    QVERIFY2(s.contains(QStringLiteral("connect(this,&VisibilityManager::strutsThicknessChanged,this,[&](){updateStrutsBasedOnLayoutsAndActivities();})")),
             "strutsThicknessChanged must publish the layer-shell exclusive zone directly");
    QVERIFY2(!s.contains(QStringLiteral("connect(this,&VisibilityManager::strutsThicknessChanged,&VisibilityManager::updateStrutsAfterTimer)")),
             "strutsThicknessChanged must not wait behind the geometry throttle");
    QCOMPARE(s.count(QStringLiteral("&Latte::View::absoluteGeometryChanged")), 1);
    QVERIFY2(s.contains(QStringLiteral("connect(m_latteView,&Latte::View::absoluteGeometryChanged,this,&VisibilityManager::updateStrutsAfterTimer)")),
             "absoluteGeometryChanged must retain the floating-panel feedback throttle");
    QCOMPARE(s.count(QStringLiteral("&Latte::ScreenPool::screenGeometryChanged")), 1);
    QVERIFY2(s.contains(QStringLiteral("connect(m_corona->screenPool(),&Latte::ScreenPool::screenGeometryChanged,this,&VisibilityManager::updateStrutsAfterTimer)")),
             "screenGeometryChanged must retain the floating-panel feedback throttle");
    QCOMPARE(s.count(QStringLiteral("&ViewPart::Positioner::isOffScreenChanged")), 1);
    QVERIFY2(s.contains(QStringLiteral("connect(m_latteView->positioner(),&ViewPart::Positioner::isOffScreenChanged,this,&VisibilityManager::updateStrutsAfterTimer)")),
             "isOffScreenChanged must retain the floating-panel feedback throttle");
}

void SourceGuardTest::middleClickDispatch_keepsProductionRecordingContract()
{
    const QString mouseAreaSource = readFile(QStringLiteral("plasmoid/package/contents/ui/task/TaskMouseArea.qml"));
    const QString release = stripped(functionBody(mouseAreaSource, QStringLiteral("onReleased: (mouse) =>")));
    QVERIFY2(!release.isEmpty(), "TaskMouseArea.onReleased not found");

    const int middleBranchStart = release.indexOf(
        QStringLiteral("elseif(mouse.button==Qt.MiddleButton&&!root.disableAllWindowsFunctionality){"));
    const int leftBranchStart = release.indexOf(QStringLiteral("elseif(mouse.button==Qt.LeftButton){"), middleBranchStart);
    QVERIFY2(middleBranchStart != -1 && leftBranchStart > middleBranchStart,
             "production middle-click branch not found before the left-click branch");
    const QString middleBranch = release.mid(middleBranchStart, leftBranchStart - middleBranchStart);
    QVERIFY2(middleBranch.contains(QStringLiteral(
                 "if(!taskItem.isLauncher){executeStandardAction(root.middleClickAction,true);}"
                 "else{taskMouseArea.recordMiddleClickDispatch(\"activate\");activateTask();}")),
             "middle-click recording must stay in the production task/launcher dispatch branch");
    QVERIFY2(!middleBranch.contains(QStringLiteral("isGroupParent"))
                 && !middleBranch.contains(QStringLiteral("isStartup")),
             "grouped and startup rows must retain task classification; only isLauncher selects the launcher exception");

    const QString execute = stripped(functionBody(mouseAreaSource, QStringLiteral("function executeStandardAction(action, recordsMiddleClick)")));
    const int recordPosition = execute.indexOf(QStringLiteral("taskMouseArea.recordMiddleClickDispatch(command);"));
    const int dispatchPosition = execute.indexOf(QStringLiteral("switch(command)"));
    QVERIFY2(recordPosition != -1 && dispatchPosition > recordPosition,
             "task-row dispatch recording must precede the selected production operation");

    const QString identity = stripped(functionBody(mouseAreaSource, QStringLiteral("function stableRowIdentity()")));
    QVERIFY2(identity.contains(QStringLiteral("taskMouseArea.dispatchModel.LauncherUrlWithoutIcon"))
                 && identity.contains(QStringLiteral("launcherIdentity.length>0?launcherIdentity:String(taskMouseArea.dispatchModel.AppId||\"\")")),
             "row identity must prefer LauncherUrlWithoutIcon and fall back to AppId");

    const QString taskItemSource = stripped(readFile(QStringLiteral("plasmoid/package/contents/ui/task/TaskItem.qml")));
    QVERIFY2(taskItemSource.contains(QStringLiteral("propertyboolisGroupParent:(IsGroupParent===true)?true:false"))
                 && taskItemSource.contains(QStringLiteral("propertyboolisLauncher:(IsLauncher===true)?true:false"))
                 && taskItemSource.contains(QStringLiteral("propertyboolisStartup:(IsStartup===true)?true:false")),
             "TaskItem grouped, launcher, and startup model classifications must remain explicit");
    const QString reporterWiring = stripped(functionBody(taskItemSource, QStringLiteral("TaskMouseArea")));
    QVERIFY2(reporterWiring.contains(QStringLiteral("dispatchReporter:root.middleClickDispatchReporter"))
                 && reporterWiring.contains(QStringLiteral("dispatchModel:taskItem.m"))
                 && reporterWiring.contains(QStringLiteral("dispatchIsLauncher:taskItem.isLauncher"))
                 && reporterWiring.contains(QStringLiteral("configuredMiddleClickAction:root.middleClickAction")),
             "TaskItem must wire production model, classification, action, and reporter into TaskMouseArea");

    const QString mainSource = stripped(readFile(QStringLiteral("plasmoid/package/contents/ui/main.qml")));
    QVERIFY2(mainSource.contains(QStringLiteral("readonlypropertyaliaslatestMiddleClickDispatch:backend.latestMiddleClickDispatch"))
                 && mainSource.contains(QStringLiteral("readonlypropertyaliasmiddleClickDispatchReporter:backend")),
             "tasks root must retain both backend reporter aliases");
}

void SourceGuardTest::middleClickDispatch_keepsContainmentLifecycleScope()
{
    const QString collector = stripped(functionBody(readFile(QStringLiteral("app/dbusreports.cpp")),
                                                    QStringLiteral("QString collectMiddleClickDispatchData")));
    QVERIFY2(!collector.isEmpty(), "collectMiddleClickDispatchData not found");
    QVERIFY2(collector.contains(QStringLiteral("for(auto*applet:view->containment()->applets())")),
             "middle-click collection must iterate the requested view's current containment applets");
    QVERIFY2(!collector.contains(QStringLiteral("inScheduledDestruction")),
             "applets in Plasma's undo window remain queryable until actual destruction removes them");
    QVERIFY2(collector.contains(QStringLiteral("hasnoquickitemyet;taskMiddleClickDispatchDatacannotberead"))
                 && collector.contains(QStringLiteral("continue;")),
             "a startup-transient missing quick item must remain a loud, intentional unavailable candidate");
}

QTEST_GUILESS_MAIN(SourceGuardTest)

#include "sourceguardtest.moc"
