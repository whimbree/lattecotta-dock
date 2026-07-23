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
//   * Views reporting: global applet rearrangement is effective only for the
//     dock that is locally in edit mode
//   * Layout-length animation tracking: horizontal and vertical changes share
//     one registration path and the settle timer owns the matching removal
//   * Centered applet-row placement: background-only parabolic clamping cannot
//     feed back into the stable content offset
//   * Dock background routing: Justify joins all dock alignments in the
//     shadow-aware visual fit while panel mode retains its full-span path
//   * Dock-system reporting: persistent-id ordering and original/clone
//     relationship classification stay on their pure seams
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

    static QString normalizedCode(const QString &source)
    {
        QString code = source;
        code.remove(QRegularExpression(QStringLiteral("/\\*[\\s\\S]*?\\*/")));
        code.remove(QRegularExpression(QStringLiteral("//[^\\n]*")));
        code.remove(QRegularExpression(QStringLiteral("\\s+")));
        return code;
    }

    static bool matchesExactMiddleClickReporterForwarding(const QString &body)
    {
        return normalizedCode(body) == QStringLiteral(
                   "{taskMouseArea.dispatchReporter.recordMiddleClickDispatch("
                   "taskMouseArea.stableRowIdentity(),taskMouseArea.dispatchIsLauncher,"
                   "taskMouseArea.configuredMiddleClickAction,operation);}");
    }

    static bool matchesEffectiveConfigureModeCollection(const QString &body)
    {
        const QString code = normalizedCode(body);
        const QString effectiveAssignment = QStringLiteral(
            "record.inConfigureAppletsMode=effectiveConfigureAppletsMode("
            "record.editMode,globalConfigureAppletsMode);");
        return code.count(effectiveAssignment) == 1
            && !code.contains(QStringLiteral(
                "record.inConfigureAppletsMode=globalConfigureAppletsMode;"));
    }

    static bool matchesLengthAnimationTrackerContract(const QString &source)
    {
        const QString normalizedSource = normalizedCode(source);
        const QString registration = normalizedCode(functionBody(
            source, QStringLiteral("function registerLengthAnimation()")));
        const QString horizontal = normalizedCode(functionBody(
            source, QStringLiteral("onContentsWidthChanged:")));
        const QString vertical = normalizedCode(functionBody(
            source, QStringLiteral("onContentsHeightChanged:")));
        const int timerStart = source.indexOf(QStringLiteral("id:delayUpdateMaskArea"));
        const QString settlement = timerStart == -1
            ? QString{}
            : normalizedCode(functionBody(source.mid(timerStart), QStringLiteral("onTriggered:")));

        const QString registerCall = QStringLiteral(
            "layoutsContainer.registerLengthAnimation();");
        const QString addEvent = QStringLiteral(
            "animations.needLength.addEvent(layoutsContainer);");
        const QString removeEvent = QStringLiteral(
            "animations.needLength.removeEvent(layoutsContainer);");

        const int registrationSetsActive = registration.indexOf(QStringLiteral(
            "layoutsContainer.animationSent=true;"));
        const int registrationAddsEvent = registration.indexOf(addEvent);
        const int settlementRemovesEvent = settlement.indexOf(removeEvent);
        const int settlementClearsActive = settlement.indexOf(QStringLiteral(
            "layoutsContainer.animationSent=false;"));

        return !registration.isEmpty()
            && horizontal.count(registerCall) == 1
            && vertical.count(registerCall) == 1
            && !horizontal.contains(QStringLiteral("animations.needLength."))
            && !vertical.contains(QStringLiteral("animations.needLength."))
            && registration.count(addEvent) == 1
            && !registration.contains(removeEvent)
            && registrationSetsActive != -1
            && registrationAddsEvent > registrationSetsActive
            && settlement.count(removeEvent) == 1
            && settlementRemovesEvent != -1
            && settlementClearsActive > settlementRemovesEvent
            && normalizedSource.count(addEvent) == 1
            && normalizedSource.count(removeEvent) == 1;
    }

    static bool matchesCenteredAppletOffsetOwnership(const QString &source)
    {
        const int mainLayoutStart = source.indexOf(QStringLiteral("id: _mainLayout"));
        if (mainLayoutStart == -1) {
            return false;
        }

        const QString offsetBinding = normalizedCode(functionBody(
            source.mid(mainLayoutStart), QStringLiteral("offset:")));
        return offsetBinding.contains(QStringLiteral(
                   "return(root.myView.alignment===LatteCore.Types.Justify)"
                   "?inJustifyCenterOffset:root.offset"))
            && !offsetBinding.contains(QStringLiteral(
                "background.offset-parabolicOffsetting"));
    }

    static bool matchesDockBackgroundFitRouting(const QString &source)
    {
        const int lengthStart = source.indexOf(QStringLiteral("\n    length: {"));
        const int offsetStart = source.indexOf(QStringLiteral("\n    offset: {"));
        if (lengthStart == -1 || offsetStart == -1) {
            return false;
        }

        const QString lengthBinding = normalizedCode(functionBody(
            source.mid(lengthStart), QStringLiteral("length:")));
        const QString offsetBinding = normalizedCode(functionBody(
            source.mid(offsetStart), QStringLiteral("offset:")));

        const int panelPath = lengthBinding.indexOf(QStringLiteral(
            "if(root.behaveAsPlasmaPanel&&LatteCore.WindowSystem.compositingActive)"));
        const int requestedLength = lengthBinding.indexOf(QStringLiteral(
            "constrequestedLength=myView.alignment===LatteCore.Types.Justify"
            "?maximumLength:Math.max("));
        const int fittedLength = lengthBinding.indexOf(QStringLiteral(
            "returnbackgroundStateResolver.dockBackgroundLength("
            "requestedLength,maximumLength,barLine.totals.shadowsLength);"));

        return panelPath != -1
            && requestedLength > panelPath
            && fittedLength > requestedLength
            && !lengthBinding.contains(QStringLiteral(
                "if(myView.alignment===LatteCore.Types.Justify){returnroot.maxLength;}"))
            && offsetBinding.contains(QStringLiteral(
                "if(myView.alignment===LatteCore.Types.Justify){return0;}"));
    }

    static bool matchesDockCollectionOrderingRoute(const QString &body)
    {
        const QString code = normalizedCode(body);
        const int orderInput = code.indexOf(QStringLiteral(
            "collectionOrder.append(DockCollectionOrderInput{persistentDockId,sourceIndex});"));
        const int ordering = code.indexOf(QStringLiteral(
            "orderDockCollectionByPersistentId(collectionOrder);"));
        const int orderedLoop = code.indexOf(QStringLiteral(
            "for(constqsizetypesourceIndex:orderedSourceIndexes)"), ordering);
        const int firstIdentityLookup = code.indexOf(QStringLiteral("identities->"));

        return orderInput != -1
            && ordering > orderInput
            && orderedLoop > ordering
            && firstIdentityLookup > orderedLoop
            && code.count(QStringLiteral(
                "for(constqsizetypesourceIndex:orderedSourceIndexes)")) == 2
            && !code.contains(QStringLiteral("for(auto*view:views)"));
    }

    static bool matchesDockRelationshipClassifierRoute(const QString &body)
    {
        const QString code = normalizedCode(body);
        const int lineageInput = code.indexOf(QStringLiteral(
            "lineages.append(DockLineageInput{persistentDockId,"));
        const int classification = code.indexOf(QStringLiteral(
            "constautorelationships=classifyDockRelationshipGraph(lineages);"), lineageInput);
        const int refusal = code.indexOf(QStringLiteral("if(!relationships){"), classification);
        const int refusalReturn = code.indexOf(QStringLiteral("returnstd::nullopt;"), refusal);
        const int logicalAssignment = code.indexOf(QStringLiteral(
            "record.logicalDockId=relationship.logicalDockId;"), refusalReturn);
        const int originalAssignment = code.indexOf(QStringLiteral(
            "record.originalDockId=relationship.originalDockId;"), logicalAssignment);
        const int relationshipAssignment = code.indexOf(QStringLiteral(
            "record.relationship=relationship.relationship;"), originalAssignment);
        const int identityLookup = code.indexOf(QStringLiteral("identities->idFor(view);"));

        return lineageInput != -1
            && classification > lineageInput
            && refusal > classification
            && refusalReturn > refusal
            && logicalAssignment > classification
            && originalAssignment > logicalAssignment
            && relationshipAssignment > originalAssignment
            && identityLookup > relationshipAssignment
            && code.contains(QStringLiteral(
                "qCritical()<<\"dbusreports:refusingdock-systemsnapshotwithmalformeddocklineage\""))
            && code.contains(QStringLiteral("Q_ASSERT(relationships.has_value());returnstd::nullopt;"))
            && !code.contains(QStringLiteral("continue;"))
            && !code.contains(QStringLiteral(
                "record.logicalDockId=view->isCloned()?"));
    }

    static bool matchesRuntimeIdentityRegistryContract(const QString &body)
    {
        const QString code = normalizedCode(body);
        return code.count(QStringLiteral("Q_ASSERT(hasRequiredThreadAffinity(object));")) == 1
            && code.count(QStringLiteral("Q_ASSERT(hasRequiredThreadAffinity(this));")) == 2
            && code.count(QStringLiteral("m_ids.erase(existing);")) == 1
            && code.count(QStringLiteral("m_ids.erase(entry);")) == 1
            && code.contains(QStringLiteral(
                "QObject::connect(trackedObject,&QObject::destroyed,this,"))
            && code.contains(QStringLiteral("},Qt::DirectConnection);Q_ASSERT(retirement);"))
            && code.contains(QStringLiteral("returnm_ids.size();"))
            && code.contains(QStringLiteral(
                "returnapplication&&thread()==application->thread()"
                "&&QThread::currentThread()==thread()&&object&&object->thread()==thread();"));
    }

    static bool matchesMiddleClickCollectorBridge(const QString &body)
    {
        const QString code = normalizedCode(body);
        const auto capture = [&code](const QString &pattern) {
            const auto match = QRegularExpression(pattern).match(code);
            return match.hasMatch() ? match.captured(1) : QString{};
        };
        const auto position = [&code](const QString &pattern, int offset = 0) {
            return QRegularExpression(pattern).match(code, offset).capturedStart();
        };

        const QString containmentId = capture(QStringLiteral(
            "const(?:uint|auto)([A-Za-z_][A-Za-z0-9_]*)=view->containment\\(\\)->id\\(\\);"));
        const QString candidates = capture(QStringLiteral(
            "QList<MiddleClickDispatchCandidate>([A-Za-z_][A-Za-z0-9_]*);"));
        const QString applet = capture(QStringLiteral(
            "for\\(auto\\*([A-Za-z_][A-Za-z0-9_]*):view->containment\\(\\)->applets\\(\\)\\)"));
        if (containmentId.isEmpty() || candidates.isEmpty() || applet.isEmpty()) {
            return false;
        }

        const QString escapedContainmentId = QRegularExpression::escape(containmentId);
        const QString escapedCandidates = QRegularExpression::escape(candidates);
        const QString escapedApplet = QRegularExpression::escape(applet);
        const QString scopeCheck = QStringLiteral("if\\(%1!=containmentId\\)").arg(escapedContainmentId);
        const QString appletLoop = QStringLiteral(
            "for\\(auto\\*%1:view->containment\\(\\)->applets\\(\\)\\)").arg(escapedApplet);
        const QString pluginFilter = QStringLiteral(
            "if\\(%1->pluginMetaData\\(\\)\\.pluginId\\(\\)!=(?:QLatin1String|QStringLiteral)"
            "\\(\"org\\.kde\\.latte\\.plasmoid\"\\)\\)\\{continue;\\}").arg(escapedApplet);

        const int containmentPosition = position(QStringLiteral(
            "const(?:uint|auto)%1=view->containment\\(\\)->id\\(\\);").arg(escapedContainmentId));
        const int scopeCheckPosition = position(scopeCheck, containmentPosition);
        const int candidatesPosition = position(QStringLiteral(
            "QList<MiddleClickDispatchCandidate>%1;").arg(escapedCandidates), scopeCheckPosition);
        const int loopPosition = position(appletLoop, candidatesPosition);
        const int scopeRefusalPosition = code.indexOf(QStringLiteral("returnQStringLiteral(\"{}\");"), scopeCheckPosition);
        const int pluginFilterPosition = position(pluginFilter, loopPosition);

        const QString quickItem = capture(
            QStringLiteral("auto\\*?([A-Za-z_][A-Za-z0-9_]*)=PlasmaQuick::AppletQuickItem::itemForApplet\\(")
            + escapedApplet + QStringLiteral("\\);"));
        if (quickItem.isEmpty()) {
            return false;
        }
        const QString escapedQuickItem = QRegularExpression::escape(quickItem);
        const QString itemLookup = QStringLiteral(
            "auto\\*?%1=PlasmaQuick::AppletQuickItem::itemForApplet\\(%2\\);")
                                       .arg(escapedQuickItem, escapedApplet);
        const int itemLookupPosition = position(itemLookup, pluginFilterPosition);
        const QString missingQuickItemToken = QStringLiteral("if(!%1)").arg(quickItem);
        const int missingQuickItemPosition = code.indexOf(missingQuickItemToken, itemLookupPosition);
        const QString missingQuickItemBranch = functionBody(code, missingQuickItemToken);
        const int warningPosition = missingQuickItemBranch.indexOf(QStringLiteral("qWarning()<<"));
        const int missingQuickItemContinuePosition = code.indexOf(QStringLiteral("continue;"), missingQuickItemPosition);

        const QString value = capture(
            QStringLiteral("const(?:QVariant|auto)([A-Za-z_][A-Za-z0-9_]*)=readLiveProperty\\(")
            + escapedQuickItem + QStringLiteral(",\"latestMiddleClickDispatch\"\\);"));
        if (value.isEmpty()) {
            return false;
        }
        const QString escapedValue = QRegularExpression::escape(value);
        const QString propertyRead = QStringLiteral(
            "const(?:QVariant|auto)%1=readLiveProperty\\(%2,\"latestMiddleClickDispatch\"\\);")
                                         .arg(escapedValue, escapedQuickItem);
        const QString append = QStringLiteral(
            "%1\\.append\\(MiddleClickDispatchCandidate\\{%2,static_cast<int>\\(%3->id\\(\\)\\),%4\\}\\);")
                                   .arg(escapedCandidates, escapedContainmentId, escapedApplet, escapedValue);
        const int propertyPosition = position(propertyRead, missingQuickItemPosition);
        const int appendPosition = position(append, propertyPosition);

        const QString selection = capture(
            QStringLiteral("constauto([A-Za-z_][A-Za-z0-9_]*)=selectLatestMiddleClickDispatch\\(containmentId,")
            + escapedCandidates + QStringLiteral("\\);"));
        if (selection.isEmpty()) {
            return false;
        }
        const QString escapedSelection = QRegularExpression::escape(selection);
        const QString selector = QStringLiteral(
            "constauto%1=selectLatestMiddleClickDispatch\\(containmentId,%2\\);")
                                     .arg(escapedSelection, escapedCandidates);
        const QString refusalSwitch = QStringLiteral("switch\\(%1\\.refusal\\)").arg(escapedSelection);
        const QString serialization = QStringLiteral(
            "caseMiddleClickDispatchRefusal::None:returnserializeMiddleClickDispatchData\\(%1\\.record\\);")
                                          .arg(escapedSelection);
        const int selectorPosition = position(selector, appendPosition);
        const int switchPosition = position(refusalSwitch, selectorPosition);
        const int serializationPosition = position(serialization, switchPosition);

        const QRegularExpression destroyedStateAccess(
            QStringLiteral("%1->(?:destroyed|isDestroyed)\\(\\)|"
                           "%1->property\\(\"(?:destroyed|inScheduledDestruction)\"\\)")
                .arg(escapedApplet),
            QRegularExpression::CaseInsensitiveOption);

        return containmentPosition != -1
            && scopeCheckPosition > containmentPosition
            && candidatesPosition > scopeCheckPosition
            && loopPosition > candidatesPosition
            && scopeRefusalPosition > scopeCheckPosition
            && scopeRefusalPosition < loopPosition
            && pluginFilterPosition > loopPosition
            && itemLookupPosition > pluginFilterPosition
            && missingQuickItemPosition > itemLookupPosition
            && warningPosition != -1
            && missingQuickItemBranch.indexOf(QStringLiteral("continue;"), warningPosition) > warningPosition
            && missingQuickItemContinuePosition > missingQuickItemPosition
            && missingQuickItemContinuePosition < propertyPosition
            && propertyPosition > missingQuickItemPosition
            && appendPosition > propertyPosition
            && selectorPosition > appendPosition
            && switchPosition > selectorPosition
            && serializationPosition > switchPosition
            && code.contains(QStringLiteral("caseMiddleClickDispatchRefusal::ContainmentMismatch:"))
            && code.contains(QStringLiteral("caseMiddleClickDispatchRefusal::MalformedState:"))
            && code.contains(QStringLiteral("caseMiddleClickDispatchRefusal::DuplicateSequence:"))
            && code.count(QStringLiteral("selectLatestMiddleClickDispatch(")) == 1
            && code.count(QStringLiteral("serializeMiddleClickDispatchData(")) == 1
            && code.lastIndexOf(QStringLiteral("returnQStringLiteral(\"{}\");")) > switchPosition
            && !destroyedStateAccess.match(code).hasMatch()
            && !code.contains(QStringLiteral("inScheduledDestruction"));
    }

private Q_SLOTS:
    void visibilityManager_updateSidebarState_assignsState();
    void layoutsController_modeIsChanged_delegatesToModel();
    void windowsTrackerBinding_keepsRequesters();
    void waylandWindowSignals_keepDeliveryPolicy();
    void visibilityManager_strutThicknessBypassesGeometryThrottle();
    void viewsDataConfigureMode_keepsPerViewContract();
    void viewsDataConfigureMode_sourceGuardRejectsGlobalLeak();
    void layoutLengthChanges_shareAnimationTrackerRegistration();
    void layoutLengthChanges_sourceGuardRejectsVerticalRemoval();
    void centeredAppletOffset_ignoresBoundedBackgroundMovement();
    void centeredAppletOffset_sourceGuardRejectsVisualFeedback();
    void dockBackgroundFit_includesJustifyDockMode();
    void dockBackgroundFit_sourceGuardsRejectBypasses();
    void dockSystemCollection_keepsPureRouting();
    void dockSystemCollection_sourceGuardsRejectControlledMutations();
    void dockSystemIdentityRegistry_keepsLifetimeAndAffinityContract();
    void dockSystemIdentityRegistry_sourceGuardsRejectControlledMutations();
    void middleClickDispatch_keepsProductionRecordingContract();
    void middleClickDispatch_keepsContainmentLifecycleScope();
    void middleClickDispatch_sourceGuardsRejectControlledMutations();
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

void SourceGuardTest::viewsDataConfigureMode_keepsPerViewContract()
{
    const QString source = readFile(QStringLiteral("app/dbusreports.cpp"));
    const QString collector = functionBody(source, QStringLiteral("ViewRecord collectViewRecord"));
    QVERIFY2(!collector.isEmpty(), "collectViewRecord not found");
    QVERIFY2(matchesEffectiveConfigureModeCollection(collector),
             "D76 (configure-applets mode leaked across docks) must combine local and global state");
}

void SourceGuardTest::viewsDataConfigureMode_sourceGuardRejectsGlobalLeak()
{
    const QString source = readFile(QStringLiteral("app/dbusreports.cpp"));
    QString collector = normalizedCode(
        functionBody(source, QStringLiteral("ViewRecord collectViewRecord")));
    QVERIFY(matchesEffectiveConfigureModeCollection(collector));

    const QString effectiveAssignment = QStringLiteral(
        "record.inConfigureAppletsMode=effectiveConfigureAppletsMode("
        "record.editMode,globalConfigureAppletsMode);");
    QCOMPARE(collector.count(effectiveAssignment), 1);
    collector.replace(effectiveAssignment, QStringLiteral(
        "record.inConfigureAppletsMode=globalConfigureAppletsMode;"));
    QVERIFY2(!matchesEffectiveConfigureModeCollection(collector),
             "restoring the direct global D76 assignment must fail the collector guard");
}

void SourceGuardTest::layoutLengthChanges_shareAnimationTrackerRegistration()
{
    const QString source = readFile(QStringLiteral(
        "containment/package/contents/ui/layouts/LayoutsContainer.qml"));
    QVERIFY2(matchesLengthAnimationTrackerContract(source),
             "both layout axes must register one length animation and let the settle timer remove it");
}

void SourceGuardTest::layoutLengthChanges_sourceGuardRejectsVerticalRemoval()
{
    QString source = readFile(QStringLiteral(
        "containment/package/contents/ui/layouts/LayoutsContainer.qml"));
    QVERIFY(matchesLengthAnimationTrackerContract(source));

    const QString registrationCall = QStringLiteral(
        "layoutsContainer.registerLengthAnimation();");
    const int verticalStart = source.indexOf(QStringLiteral("onContentsHeightChanged:"));
    const int verticalCall = source.indexOf(registrationCall, verticalStart);
    QVERIFY(verticalStart != -1 && verticalCall > verticalStart);

    source.replace(verticalCall, registrationCall.size(), QStringLiteral(
        "animations.needLength.removeEvent(layoutsContainer);"));
    QVERIFY2(!matchesLengthAnimationTrackerContract(source),
             "restoring the vertical remove-at-start defect must fail the tracker contract");
}

void SourceGuardTest::centeredAppletOffset_ignoresBoundedBackgroundMovement()
{
    const QString source = readFile(QStringLiteral(
        "containment/package/contents/ui/layouts/LayoutsContainer.qml"));
    QVERIFY2(matchesCenteredAppletOffsetOwnership(source),
             "centered applet placement must not consume the bounded visual background offset");
}

void SourceGuardTest::centeredAppletOffset_sourceGuardRejectsVisualFeedback()
{
    QString source = readFile(QStringLiteral(
        "containment/package/contents/ui/layouts/LayoutsContainer.qml"));
    QVERIFY(matchesCenteredAppletOffsetOwnership(source));

    const QString stableOffset = QStringLiteral(
        "? inJustifyCenterOffset : root.offset");
    QCOMPARE(source.count(stableOffset), 1);
    source.replace(stableOffset, QStringLiteral(
        "? inJustifyCenterOffset : background.offset - parabolicOffsetting"));
    QVERIFY2(!matchesCenteredAppletOffsetOwnership(source),
             "restoring visual-offset feedback must fail the applet placement guard");
}

void SourceGuardTest::dockBackgroundFit_includesJustifyDockMode()
{
    const QString source = readFile(QStringLiteral(
        "containment/package/contents/ui/background/MultiLayered.qml"));
    QVERIFY2(matchesDockBackgroundFitRouting(source),
             "dock-mode Justify must share the shadow-aware complete-visual fit");
}

void SourceGuardTest::dockBackgroundFit_sourceGuardsRejectBypasses()
{
    const QString original = readFile(QStringLiteral(
        "containment/package/contents/ui/background/MultiLayered.qml"));
    QVERIFY(matchesDockBackgroundFitRouting(original));

    QString lengthBypass = original;
    const QString fittedCall = QStringLiteral(
        "return backgroundStateResolver.dockBackgroundLength(requestedLength,\n"
        "                                                             maximumLength,\n"
        "                                                             barLine.totals.shadowsLength);");
    QCOMPARE(lengthBypass.count(fittedCall), 1);
    lengthBypass.replace(fittedCall, QStringLiteral("return requestedLength;"));
    QVERIFY2(!matchesDockBackgroundFitRouting(lengthBypass),
             "bypassing the complete-visual fit must fail the routing guard");

    QString offsetBypass = original;
    const QString justifyOffset = QStringLiteral(
        "if (myView.alignment === LatteCore.Types.Justify) {\n"
        "            return 0;\n"
        "        }");
    QCOMPARE(offsetBypass.count(justifyOffset), 1);
    offsetBypass.replace(justifyOffset, QStringLiteral(
        "if (myView.alignment === LatteCore.Types.Justify) {\n"
        "            return root.offset;\n"
        "        }"));
    QVERIFY2(!matchesDockBackgroundFitRouting(offsetBypass),
             "restoring an unconstrained Justify offset must fail the routing guard");
}

void SourceGuardTest::dockSystemCollection_keepsPureRouting()
{
    const QString source = readFile(QStringLiteral("app/dbusreports.cpp"));
    const QString systemCollector = functionBody(
        source, QStringLiteral("collectDockSystemSnapshot("));
    QVERIFY2(!systemCollector.isEmpty(), "collectDockSystemSnapshot not found");
    QVERIFY2(matchesDockCollectionOrderingRoute(systemCollector),
             "dock-system collection must order persistent ids before every identity lookup");
    QVERIFY2(matchesDockRelationshipClassifierRoute(systemCollector),
             "dock-system collection must route lineage through the tested classifier");
}

void SourceGuardTest::dockSystemCollection_sourceGuardsRejectControlledMutations()
{
    const QString source = readFile(QStringLiteral("app/dbusreports.cpp"));
    const QString systemCollector = functionBody(
        source, QStringLiteral("collectDockSystemSnapshot("));
    QVERIFY(matchesDockCollectionOrderingRoute(systemCollector));
    QString unorderedCollection = normalizedCode(systemCollector);
    QCOMPARE(unorderedCollection.count(QStringLiteral(
        "orderDockCollectionByPersistentId(collectionOrder)")), 1);
    unorderedCollection.replace(QStringLiteral(
                                    "orderDockCollectionByPersistentId(collectionOrder)"),
                                QStringLiteral("QList<qsizetype>{}"));
    QVERIFY2(!matchesDockCollectionOrderingRoute(unorderedCollection),
             "bypassing persistent-id ordering must fail the collector guard");

    QVERIFY(matchesDockRelationshipClassifierRoute(systemCollector));
    QString directRelationship = normalizedCode(systemCollector);
    QCOMPARE(directRelationship.count(QStringLiteral("classifyDockRelationshipGraph(")), 1);
    directRelationship.replace(QStringLiteral("classifyDockRelationshipGraph("),
                               QStringLiteral("legacyDockRelationshipGraph("));
    QVERIFY2(!matchesDockRelationshipClassifierRoute(directRelationship),
             "bypassing whole-graph relationship validation must fail the collector guard");

    const QString dataCollector = normalizedCode(functionBody(
        source, QStringLiteral("QString collectDockSystemData(")));
    QVERIFY2(dataCollector.contains(QStringLiteral(
                 "returnsnapshot?serializeDockSystemSnapshot(*snapshot):QString();")),
             "malformed lineage must refuse the complete query instead of serializing partial JSON");
}

void SourceGuardTest::dockSystemIdentityRegistry_keepsLifetimeAndAffinityContract()
{
    const QString source = readFile(QStringLiteral("app/dbusreports.h"));
    const int registryStart = source.indexOf(QStringLiteral(
        "class RuntimeObjectIdentityRegistry final"));
    const int registryEnd = source.indexOf(QStringLiteral("//! One view's windows-tracker facts"),
                                           registryStart);
    QVERIFY2(registryStart != -1 && registryEnd > registryStart,
             "RuntimeObjectIdentityRegistry class not found");
    const QString registry = source.mid(registryStart, registryEnd - registryStart);
    QVERIFY2(matchesRuntimeIdentityRegistryContract(registry),
             "runtime identity retirement and GUI-affinity contract drifted");
}

void SourceGuardTest::dockSystemIdentityRegistry_sourceGuardsRejectControlledMutations()
{
    const QString source = readFile(QStringLiteral("app/dbusreports.h"));
    const int registryStart = source.indexOf(QStringLiteral(
        "class RuntimeObjectIdentityRegistry final"));
    const int registryEnd = source.indexOf(QStringLiteral("//! One view's windows-tracker facts"),
                                           registryStart);
    QString registry = normalizedCode(source.mid(registryStart, registryEnd - registryStart));
    QVERIFY(matchesRuntimeIdentityRegistryContract(registry));

    QString queuedRetirement = registry;
    queuedRetirement.replace(QStringLiteral("Qt::DirectConnection"),
                             QStringLiteral("Qt::AutoConnection"));
    QVERIFY2(!matchesRuntimeIdentityRegistryContract(queuedRetirement),
             "queued retirement must fail the immediate-lifetime guard");

    QString uncheckedCaller = registry;
    uncheckedCaller.replace(QStringLiteral("QThread::currentThread()==thread()"),
                            QStringLiteral("true"));
    QVERIFY2(!matchesRuntimeIdentityRegistryContract(uncheckedCaller),
             "removing caller-thread affinity must fail the registry guard");

    QString unretiredGeneration = registry;
    unretiredGeneration.replace(QStringLiteral("m_ids.erase(entry);"), QString());
    QVERIFY2(!matchesRuntimeIdentityRegistryContract(unretiredGeneration),
             "removing synchronous generation retirement must fail the registry guard");
}

void SourceGuardTest::middleClickDispatch_keepsProductionRecordingContract()
{
    const QString mouseAreaSource = readFile(QStringLiteral("plasmoid/package/contents/ui/task/TaskMouseArea.qml"));
    const QString forwarding = functionBody(mouseAreaSource, QStringLiteral("function recordMiddleClickDispatch(operation)"));
    QVERIFY2(matchesExactMiddleClickReporterForwarding(forwarding),
             "recordMiddleClickDispatch must forward stable identity, row kind, configured action, and operation in order");

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
    const QString collector = functionBody(readFile(QStringLiteral("app/dbusreports.cpp")),
                                           QStringLiteral("QString collectMiddleClickDispatchData"));
    QVERIFY2(!collector.isEmpty(), "collectMiddleClickDispatchData not found");
    QVERIFY2(matchesMiddleClickCollectorBridge(collector),
             "collector must validate containment, append each current applet property, and serialize only the tested selector result");
}

void SourceGuardTest::middleClickDispatch_sourceGuardsRejectControlledMutations()
{
    const QString forwarding = functionBody(
        readFile(QStringLiteral("plasmoid/package/contents/ui/task/TaskMouseArea.qml")),
        QStringLiteral("function recordMiddleClickDispatch(operation)"));
    QVERIFY(matchesExactMiddleClickReporterForwarding(forwarding));
    QVERIFY2(!matchesExactMiddleClickReporterForwarding(QStringLiteral("{}")),
             "a missing reporter call must make the production seam guard fail");
    QVERIFY2(!matchesExactMiddleClickReporterForwarding(QStringLiteral("{return;}")),
             "a no-op reporter helper must make the production seam guard fail");

    QString wrongReporter = normalizedCode(forwarding);
    QCOMPARE(wrongReporter.count(QStringLiteral("taskMouseArea.dispatchReporter")), 1);
    wrongReporter.replace(QStringLiteral("taskMouseArea.dispatchReporter"),
                          QStringLiteral("taskMouseArea.dispatchModel"));
    QVERIFY2(!matchesExactMiddleClickReporterForwarding(wrongReporter),
             "the wrong reporter object must make the production seam guard fail");

    QString swappedArguments = normalizedCode(forwarding);
    const QString expectedArguments = QStringLiteral(
        "taskMouseArea.stableRowIdentity(),taskMouseArea.dispatchIsLauncher,"
        "taskMouseArea.configuredMiddleClickAction,operation");
    const QString swapped = QStringLiteral(
        "taskMouseArea.dispatchIsLauncher,taskMouseArea.stableRowIdentity(),"
        "taskMouseArea.configuredMiddleClickAction,operation");
    QCOMPARE(swappedArguments.count(expectedArguments), 1);
    swappedArguments.replace(expectedArguments, swapped);
    QVERIFY2(!matchesExactMiddleClickReporterForwarding(swappedArguments),
             "swapped reporter arguments must make the production seam guard fail");

    const QString collector = functionBody(readFile(QStringLiteral("app/dbusreports.cpp")),
                                           QStringLiteral("QString collectMiddleClickDispatchData"));
    QVERIFY(matchesMiddleClickCollectorBridge(collector));
    QString withoutAppend = normalizedCode(collector);
    const QString append = QStringLiteral(
        "candidates.append(MiddleClickDispatchCandidate{actualContainmentId,"
        "static_cast<int>(applet->id()),value});");
    QCOMPARE(withoutAppend.count(append), 1);
    withoutAppend.remove(append);
    QVERIFY2(!matchesMiddleClickCollectorBridge(withoutAppend),
             "removing candidate append must make the collector bridge guard fail");

    QString selectorBypass = normalizedCode(collector);
    const QString selector = QStringLiteral(
        "constautoselection=selectLatestMiddleClickDispatch(containmentId,candidates);");
    QCOMPARE(selectorBypass.count(selector), 1);
    selectorBypass.replace(selector, QStringLiteral("constMiddleClickDispatchSelectionselection{};"));
    QVERIFY2(!matchesMiddleClickCollectorBridge(selectorBypass),
             "bypassing the tested selector must make the collector bridge guard fail");

    QString wrongContainment = normalizedCode(collector);
    wrongContainment.replace(selector,
                             QStringLiteral("constautoselection=selectLatestMiddleClickDispatch("
                                            "actualContainmentId,candidates);"));
    QVERIFY2(!matchesMiddleClickCollectorBridge(wrongContainment),
             "passing the wrong containment to the selector must make the collector bridge guard fail");

    QString destroyedAppletFilter = normalizedCode(collector);
    const QString propertyRead = QStringLiteral(
        "constQVariantvalue=readLiveProperty(plasmoidRoot,\"latestMiddleClickDispatch\");");
    QCOMPARE(destroyedAppletFilter.count(propertyRead), 1);
    destroyedAppletFilter.replace(
        propertyRead,
        QStringLiteral("if(applet->destroyed()){continue;}") + propertyRead);
    QVERIFY2(!matchesMiddleClickCollectorBridge(destroyedAppletFilter),
             "filtering destroyed applet state must make the collector bridge guard fail");

    QString withoutPluginFilter = normalizedCode(collector);
    const QString pluginFilter = QStringLiteral(
        "if(applet->pluginMetaData().pluginId()!=QLatin1String(\"org.kde.latte.plasmoid\")){continue;}");
    QCOMPARE(withoutPluginFilter.count(pluginFilter), 1);
    withoutPluginFilter.remove(pluginFilter);
    QVERIFY2(!matchesMiddleClickCollectorBridge(withoutPluginFilter),
             "removing the exact Latte Tasks plugin filter must make the collector bridge guard fail");

    QString wrongQuickItemSource = normalizedCode(collector);
    QCOMPARE(wrongQuickItemSource.count(QStringLiteral("itemForApplet(applet)")), 1);
    wrongQuickItemSource.replace(QStringLiteral("itemForApplet(applet)"),
                                 QStringLiteral("itemForApplet(view->containment())"));
    QVERIFY2(!matchesMiddleClickCollectorBridge(wrongQuickItemSource),
             "reading a quick item from the wrong applet source must make the collector bridge guard fail");

    QString withoutMissingItemWarning = normalizedCode(collector);
    const QString missingQuickItemToken = QStringLiteral("if(!plasmoidRoot)");
    const QString missingQuickItemBranch = functionBody(withoutMissingItemWarning, missingQuickItemToken);
    QVERIFY(!missingQuickItemBranch.isEmpty());
    QCOMPARE(missingQuickItemBranch.count(QStringLiteral("qWarning()")), 1);
    QCOMPARE(withoutMissingItemWarning.count(missingQuickItemBranch), 1);
    QString quietMissingQuickItemBranch = missingQuickItemBranch;
    quietMissingQuickItemBranch.replace(QStringLiteral("qWarning()"), QStringLiteral("qDebug()"));
    withoutMissingItemWarning.replace(missingQuickItemBranch, quietMissingQuickItemBranch);
    QVERIFY(withoutMissingItemWarning.contains(quietMissingQuickItemBranch));
    QVERIFY2(!matchesMiddleClickCollectorBridge(withoutMissingItemWarning),
             "removing the loud missing-item warning must make the collector bridge guard fail");
}

QTEST_GUILESS_MAIN(SourceGuardTest)

#include "sourceguardtest.moc"
