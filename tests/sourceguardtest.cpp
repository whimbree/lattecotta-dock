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
//   * Reservation publication: member state commits only after coordinator
//     success and keys same-geometry moves by the layer-shell output
//   * Occupied-geometry propagation: a changed stable dock rectangle notifies
//     perpendicular peers before they solve their available region
//   * Views reporting: global applet rearrangement is effective only for the
//     dock that is locally in edit mode
//   * Layout-length animation tracking: horizontal and vertical changes share
//     one registration path and the settle timer owns the matching removal
//   * Centered applet-row placement: background-only parabolic clamping cannot
//     feed back into the stable content offset
//   * Dock background routing: Center and Justify preserve one stable solid
//     position while asymmetric shadows affect presentation placement only
//   * Justify applet placement: the physical applet container follows the
//     fitted solid background instead of extending into its shadow margins
//   * Dock background thickness: current and maximum item metrics share the
//     monotonic theme-minimum interpolation instead of duplicating its formula
//   * Floating Panel applet clearance: attached visual-border changes cannot
//     resize the stable primary-axis applet or popup span
//   * Multi-output cleanup: documented writable fields settle before a complete
//     semantic KScreen-state comparison rejects any unhandled drift
//   * Dock background rendering: custom shadows use one fixed-pixel effect
//     footprint on both axes and publish that footprint to geometry owners
//   * Dock resize animation: icon size is the only animation authority and
//     derived margins follow it without nested per-frame retargeting
//   * Floating-panel acceptance: recipe 071 keeps a partial Justify QWindow,
//     reservation, applet span, and physical-publication revisions stable
//     through both directions and a rapid reversal storm
//   * FP-4C linked-view operation storm: a validated typed plan enters a
//     whole-config transaction before any mutation, records exact replay and
//     schema-9 state, and restores the pristine nested runtime on every exit
//   * Theme-aware icon rendering: every view shares the registered singleton's
//     QML engine and offscreen software teardown stays on the basic render loop
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
#include <QProcess>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
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

    static bool matchesJustifyLayoutSolidSpanOwnership(const QString &layoutSource,
                                                       const QString &mainSource)
    {
        const QString layout = normalizedCode(layoutSource);
        const QString main = normalizedCode(mainSource);

        return layout.contains(QStringLiteral(
                   "readonlypropertyrealjustifyOwningCanvasLength:"
                   "root.isHorizontal?parent.width:parent.height"))
            && layout.contains(QStringLiteral(
                   "readonlypropertyrealjustifyLayoutLength:"
                   "background.length"))
            && layout.contains(QStringLiteral(
                "readonlypropertyrealjustifyLayoutOrigin:"
                "(justifyOwningCanvasLength-justifyLayoutLength)/2"))
            && layout.count(QStringLiteral("returnjustifyLayoutOrigin;")) == 2
            && layout.contains(QStringLiteral(
                "width:hasStablePanelGeometry"
                "?floatingTransition.appletMeasurementBounds.width"
                ":(root.isHorizontal&&root.myView.alignment"
                "===LatteCore.Types.Justify"
                "?justifyLayoutLength:parent.width)"))
            && layout.contains(QStringLiteral(
                "height:hasStablePanelGeometry"
                "?floatingTransition.appletMeasurementBounds.height"
                ":(root.isVertical&&root.myView.alignment"
                "===LatteCore.Types.Justify"
                "?justifyLayoutLength:parent.height)"))
            && main.contains(QStringLiteral("id:backgroundCanvas"))
            && main.contains(QStringLiteral(
                "x:root.behaveAsPlasmaPanel?layoutsContainer.x"
                ":(root.isHorizontal?0:layoutsContainer.x)"))
            && main.contains(QStringLiteral(
                "y:root.behaveAsPlasmaPanel?layoutsContainer.y"
                ":(root.isVertical?0:layoutsContainer.y)"))
            && main.contains(QStringLiteral(
                "width:root.behaveAsPlasmaPanel?layoutsContainer.width"
                ":(root.isHorizontal?parent.width:layoutsContainer.width)"))
            && main.contains(QStringLiteral(
                "height:root.behaveAsPlasmaPanel?layoutsContainer.height"
                ":(root.isVertical?parent.height:layoutsContainer.height)"))
            && !main.contains(QStringLiteral("anchors.fill:layoutsContainer"));
    }

    static bool matchesStableDockAutomaticSizingContract(
        const QString &autoSizeSource,
        const QString &layouterPrivateSource,
        const QString &mainSource)
    {
        const QString autoSize = normalizedCode(autoSizeSource);
        const QString layouterPrivate = normalizedCode(
            layouterPrivateSource);
        const QString main = normalizedCode(mainSource);

        return layouterPrivate.contains(QStringLiteral(
                   "readonlypropertyintautomaticSizingContentsMaxLength:"
                   "{constbackgroundTotals=background.totals;"
                   "constavailableLength="
                   "root.automaticSizingMaximumLength"
                   "-backgroundTotals.paddingsLength;"
                   "returnavailableLength;}"))
            && autoSize.contains(QStringLiteral(
                   "functiononAutomaticSizingMaximumLengthChanged()"))
            && autoSize.contains(QStringLiteral(
                   "functiononAutomaticSizingContentsMaxLengthChanged()"))
            && autoSize.contains(QStringLiteral(
                   "sizer.layouter.automaticSizingContentsMaxLength"))
            && main.contains(QStringLiteral(
                   "availablePrimaryLength:"
                   "_layouter.automaticSizingContentsMaxLength"))
            && !autoSize.contains(QStringLiteral(
                   "functiononMaxLengthChanged()"))
            && !autoSize.contains(QStringLiteral(
                   "functiononContentsMaxLengthChanged()"));
    }

    static bool matchesStableFloatingPanelQmlContract(
        const QString &mainSource,
        const QString &bindingsSource,
        const QString &visibilitySource,
        const QString &layoutsSource,
        const QString &metricsSource,
        const QString &backgroundTotalsSource,
        const QString &viewHeaderSource,
        const QString &viewImplementationSource)
    {
        const QString main = normalizedCode(mainSource);
        const QString bindings = normalizedCode(bindingsSource);
        const QString visibility = normalizedCode(visibilitySource);
        const QString layouts = normalizedCode(layoutsSource);
        const QString metrics = normalizedCode(metricsSource);
        const QString metricsPrivate = normalizedCode(
            readFile(QStringLiteral(
                "containment/package/contents/ui/abilities/privates/"
                "MetricsPrivate.qml")));
        const QString backgroundTotals = normalizedCode(backgroundTotalsSource);
        const QString viewHeader = normalizedCode(viewHeaderSource);
        const QString viewImplementation =
            normalizedCode(viewImplementationSource);

        return main.contains(QStringLiteral(
                   "readonlypropertyboolfloatingTransitionEligible:"
                   "latteView&&root.behaveAsPlasmaPanel"
                   "&&latteView.floatingPanelConfigured"
                   "&&latteView.visibility"
                   "&&latteView.visibility.mode===LatteCore.Types.AlwaysVisible"))
            && main.contains(QStringLiteral(
                   "readonlypropertyboolattachOnWindowTouchConfigured:"
                   "Plasmoid.configuration.hideFloatingGapForMaximized"))
            && main.contains(QStringLiteral(
                   "readonlypropertybool"
                   "attachmentWaitsForPointerExitConfigured:"
                   "Plasmoid.configuration.floatingGapHidingWaitsMouse"))
            && main.contains(QStringLiteral(
                   "readonlypropertyboolpointerInsideView:"
                   "!!(latteView"
                   "&&latteView.visibility"
                   "&&latteView.visibility.containsMouse)"))
            && main.contains(QStringLiteral(
                   "readonlypropertyboolbehaveAsDockWithMask:"
                   "!behaveAsPlasmaPanel"))
            && main.contains(QStringLiteral(
                   "readonlypropertybooldirectDockWindowTouchEligible:"
                   "latteView"
                   "&&root.behaveAsDockWithMask"
                   "&&latteView.floatingGapConfigured"
                   "&&!latteView.floatingPanelConfigured"
                   "&&attachOnWindowTouchConfigured"
                   "&&latteView.visibility"
                   "&&(latteView.visibility.mode"
                   "===LatteCore.Types.AlwaysVisible"
                   "||latteView.visibility.mode"
                   "===LatteCore.Types.WindowsGoBelow)"))
            && main.contains(QStringLiteral(
                   "readonlypropertybooldockGapHideRequested:"
                   "directDockWindowTouchEligible"
                   "&&latteView.windowTouchTracker"
                   "&&latteView.windowTouchTracker."
                   "touchingWindowCount>0"))
            && main.contains(QStringLiteral(
                   "readonlypropertyrealfloatingPresentationProgress:"
                   "latteView&&latteView.floatingTransition"
                   "?latteView.floatingTransition.floatingness:1.0"))
            && main.contains(QStringLiteral(
                   "readonlypropertyrealpresentedDockMaximumLengthPercent:"
                   "_backgroundState.presentedDockMaximumLengthPercent("
                   "Plasmoid.configuration.maxLength,"
                   "root.floatingPresentationProgress,"
                   "root.dockFloatingTransitionOwnsGap"
                   "&&root.maximizeWhenMaximized)"))
            && main.contains(QStringLiteral(
                   "readonlypropertybooldockFloatingTransitionOwnsGap:"
                   "root.behaveAsDockWithMask"
                   "&&latteView"
                   "&&!latteView.floatingPanelConfigured"
                   "&&latteView.floatingTransition"
                   "&&(directDockWindowTouchEligible"
                   "||latteView.floatingTransition.running"
                   "||floatingPresentationDisplaced)"))
            && viewImplementation.contains(QStringLiteral(
                   "connect(m_visibility,"
                   "&ViewPart::VisibilityManager::isHiddenChanged,"
                   "this,[&](){if(m_visibility->isHidden())"
                   "{m_interface->deactivateApplets();}"
                   "m_effects->applyFloatingPresentationProgress();});"))
            && viewImplementation.contains(QStringLiteral(
                   "connect(m_visibility,"
                   "&ViewPart::VisibilityManager::isSidebarChanged,"
                   "m_effects,"
                   "&ViewPart::Effects::applyFloatingPresentationProgress);"))
            && !main.contains(QStringLiteral(
                   "floatingTransitionEligible:behaveAsPlasmaPanel"
                   "&&latteView.visibility.mode===LatteCore.Types.WindowsGoBelow"))
            && main.contains(QStringLiteral(
                   "propertyrealmaxLengthPerCentage:behaveAsPlasmaPanel"
                   "?Plasmoid.configuration.maxLength"
                   ":(hideLengthScreenGaps"
                   "&&!dockFloatingTransitionOwnsGap"
                   "?100:Plasmoid.configuration.maxLength)"))
            && main.contains(QStringLiteral(
                   "readonlypropertyintautomaticSizingMaximumLength:"
                   "{if(!root.dockFloatingTransitionOwnsGap)"
                   "{returnroot.maxLength;}returnroot.isHorizontal"
                   "?root.width*(Plasmoid.configuration.maxLength/100)"
                   ":root.height*(Plasmoid.configuration.maxLength/100);}"))
            && main.contains(QStringLiteral(
                   "constmaximize=behaveAsPlasmaPanel"
                   "||(!dockFloatingTransitionOwnsGap"
                   "&&maximizeWhenMaximized"
                   "&&latteView.windowsTracker.currentScreen."
                   "existsWindowMaximized);"))
            && main.contains(QStringLiteral(
                   "constpresentedMaximumLengthPercent="
                   "dockFloatingTransitionOwnsGap"
                   "?presentedDockMaximumLengthPercent"
                   ":maxLengthPerCentage;"))
            && main.contains(QStringLiteral(
                   "BehavioronmaxLengthPerCentage"
                   "{enabled:root.behaveAsDockWithMask"
                   "&&!root.dockFloatingTransitionOwnsGap"))
            && matchesStableDockAutomaticSizingContract(
                readFile(QStringLiteral(
                    "containment/package/contents/ui/abilities/"
                    "AutoSize.qml")),
                readFile(QStringLiteral(
                    "containment/package/contents/ui/abilities/privates/"
                    "LayouterPrivate.qml")),
                mainSource)
            && bindings.contains(QStringLiteral(
                   "property:\"screenEdgeMarginEnabled\""
                   "when:latteViewvalue:root.screenEdgeMarginEnabled"))
            && bindings.contains(QStringLiteral(
                   "property:\"appletsLayoutGeometry\""
                   "restoreMode:Binding.RestoreNone"
                   "when:latteView&&latteView.effects"
                   "&&(externalBindings.containmentItem.behaveAsPlasmaPanel"
                   "||visibilityManager.inNormalState)"))
            && visibility.contains(QStringLiteral(
                   "property:\"animationDuration\""
                   "when:root.latteView&&root.latteView.floatingTransition"
                   "value:manager.animationSpeed"))
            && visibility.contains(QStringLiteral(
                   "propertyboolinClientSideScreenEdgeSliding:false"))
            && main.contains(QStringLiteral(
                   "inClientSideScreenEdgeSliding:"
                   "root.behaveAsDockWithMask"
                   "&&(root.dockFloatingTransitionOwnsGap"
                   "?(root.floatingPresentationDisplaced"
                   "||(root.latteView"
                   "&&root.latteView.floatingTransition"
                   "&&root.latteView.floatingTransition.running))"
                   ":root.hideThickScreenGap)"))
            && !visibility.contains(QStringLiteral(
                   "property:\"eligible\""))
            && bindings.contains(QStringLiteral(
                   "property:\"strutsThickness\""
                   "when:latteView&&latteView.visibility"
                   "value:{if(root.behaveAsPlasmaPanel)"
                   "{returnvisibilityManager.thicknessAsPanel;}"))
            && bindings.contains(QStringLiteral(
                   "constedgeThickness=isCapableToHideScreenGap"
                   "?0:metrics.mask.screenEdge*mirrorGapFactor;"
                   "returnedgeThickness"
                   "+metrics.mask.thickness."
                   "maxNormalForItemsWithoutScreenEdge;"))
            && bindings.contains(QStringLiteral(
                   "constownsStableAttachedReservation="
                   "root.screenEdgeMarginEnabled"
                   "&&Plasmoid.configuration."
                   "hideFloatingGapForMaximized;"
                   "constedgeThickness="
                   "ownsStableAttachedReservation"
                   "?0:metrics.mask.screenEdge;"))
            && main.contains(QStringLiteral(
                   "constcurrentTouchingWindowCount="
                   "latteView.windowTouchTracker.touchingWindowCount;"
                   "constcurrentDockGapHideRequested="
                   "directDockWindowTouchEligible"
                   "&&currentTouchingWindowCount>0;"))
            && main.contains(QStringLiteral(
                   "latteView.floatingTransition.reconcileTargetPolicy("
                   "floatingTransitionEligible,"
                   "attachOnWindowTouchConfigured,"
                   "attachmentWaitsForPointerExitConfigured,"
                   "pointerInsideView,"
                   "currentTouchingWindowCount,"
                   "currentDockGapHideRequested);"))
            && main.contains(QStringLiteral(
                   "onDirectDockWindowTouchEligibleChanged:"
                   "reconcileFloatingTargetPolicy()"))
            && !visibility.contains(QStringLiteral(
                   "updateFloatingTransition"))
            && !visibility.contains(QStringLiteral(
                   "requestAttached"))
            && !visibility.contains(QStringLiteral(
                   "requestFloated"))
            && !visibility.contains(QStringLiteral("slidingInRealFloating"))
            && !visibility.contains(QStringLiteral("slidingOutRealFloating"))
            && layouts.contains(QStringLiteral(
                   "floatingTransition.currentVisibleGeometry.x"))
            && layouts.contains(QStringLiteral(
                   "floatingTransition.currentVisibleGeometry.y"))
            && layouts.contains(QStringLiteral(
                   "floatingTransition.appletMeasurementBounds.width"))
            && layouts.contains(QStringLiteral(
                   "floatingTransition.appletMeasurementBounds.height"))
            && metrics.contains(QStringLiteral(
                   "if(metrics.dockTransitionOwnsGap)"
                   "{returnMath.round("
                   "metrics.configuredScreenEdgeMargin"
                   "*metrics.presentationProgress);}"))
            && metrics.contains(QStringLiteral(
                   "if(metrics.dockTransitionOwnsGap)"
                   "{returnmetrics.margin.screenEdge;}"))
            && metricsPrivate.contains(QStringLiteral(
                   "readonlypropertyintpresentedScreenEdgeGap:"
                   "mets.margin.screenEdge"))
            && metricsPrivate.contains(QStringLiteral(
                   "enabled:!root.behaveAsPlasmaPanel"
                   "&&!mets.dockTransitionOwnsGap"))
            && backgroundTotals.contains(QStringLiteral(
                   "property:\"minThickness\""
                   "when:totalsItem.stablePanelEnvelope"
                   "||!totalsItem.dockTransitionDisplaced"
                   "&&!(hideThickScreenGap||hideLengthScreenGaps)"))
            && viewHeader.contains(QStringLiteral(
                   "Q_PROPERTY(boolfloatingGapConfigured"
                   "READfloatingGapConfigured"
                   "NOTIFYfloatingGapConfiguredChanged)"))
            && viewHeader.contains(QStringLiteral(
                   "Q_PROPERTY(boolfloatingPanelConfigured"
                   "READisFloatingPanel"
                   "NOTIFYfloatingPanelConfiguredChanged)"))
            && viewImplementation.contains(QStringLiteral(
                   "boolView::floatingGapConfigured()const"
                   "{returnm_screenEdgeMarginEnabled"
                   "&&m_screenEdgeMargin>0;}"))
            && viewImplementation.contains(QStringLiteral(
                   "boolView::isFloatingPanel()const"
                   "{returnm_behaveAsPlasmaPanel"
                   "&&floatingGapConfigured();}"))
            && viewImplementation.count(QStringLiteral(
                   "constboolwasFloatingGapConfigured="
                   "floatingGapConfigured();")) == 2
            && viewImplementation.count(QStringLiteral(
                   "if(wasFloatingGapConfigured"
                   "!=floatingGapConfigured())"
                   "{Q_EMITfloatingGapConfiguredChanged();}")) == 2
            && viewImplementation.count(QStringLiteral(
                   "constboolwasFloatingPanel=isFloatingPanel();")) == 3
            && viewImplementation.count(QStringLiteral(
                   "if(wasFloatingPanel!=isFloatingPanel())"
                   "{Q_EMITfloatingPanelConfiguredChanged();}")) == 3;
    }

    static bool matchesStableFloatingPanelE2eContract(const QString &source)
    {
        const QString code = normalizedCode(source);
        const qsizetype cleanupTrap = code.indexOf(QStringLiteral("trapcleanupEXIT"));
        const qsizetype fixtureStage = code.indexOf(QStringLiteral(
            "matrix_stagepanel-bottom-justify-1out"));

        return cleanupTrap >= 0
            && fixtureStage > cleanupTrap
            && code.contains(QStringLiteral(
                   "matrix_init||e2e_fail"
                   "\"couldnotcapturethepristinenestedconfiguration\""
                   "configured=1"
                   "matrix_stagepanel-bottom-justify-1out"))
            && code.contains(QStringLiteral(
                   "kwriteconfig6\"${group_args[@]}\"--key"
                   "maximizeWhenMaximizedfalse"))
            && code.contains(QStringLiteral(
                   "kwriteconfig6\"${group_args[@]}\"--keymaxLength60"))
            && code.contains(QStringLiteral(
                   "kwriteconfig6\"${group_args[@]}\"--keyalignment10"))
            && code.contains(QStringLiteral(
                   "snapshot=json.load(sys.stdin)"
                   "match=[vforvinsnapshot['views']"
                   "ifv['persistentDockId']==$view]"))
            && code.contains(QStringLiteral(
                   "\"windowGeometry\",\"absoluteGeometry\","
                   "\"surfaceGeometry\",\"canvasGeometry\","
                   "\"stableCanvasGeometry\","))
            && code.contains(QStringLiteral(
                   "\"stableAppletMeasurementBounds\","
                   "\"stablePrimaryAxisStart\","
                   "\"stablePrimaryAxisLength\","
                   "\"availablePrimaryLength\","
                   "\"configuredIconSize\",\"effectiveIconSize\","))
            && code.contains(QStringLiteral(
                   "\"reservationGroupGeneration\","
                   "\"reservationContributorDockIds\","
                   "\"reservationGeometry\",\"layerShellMargins\","
                   "\"layerShellAnchors\",\"layerShellExclusiveEdge\","))
            && code.contains(QStringLiteral(
                   "\"transitionController\":v[\"objects\"]"
                   "[\"transitionController\"]"))
            && code.contains(QStringLiteral(
                   "\"reservationPublisher\":v[\"objects\"]"
                   "[\"reservationPublisher\"]"))
            && code.contains(QStringLiteral(
                   "v[\"surfaceGeometryPublicationRevision\"],"
                   "v[\"layerShellConfigureRequestRevision\"]"))
            && code.contains(QStringLiteral(
                   "*v[\"appletsLayoutGeometry\"],"
                   "math.floor(v[\"computedPaintMaskGeometry\"][1]),"
                   "math.ceil(v[\"computedPaintMaskGeometry\"][1]"
                   "+v[\"computedPaintMaskGeometry\"][3])"
                   "-math.floor(v[\"computedPaintMaskGeometry\"][1])"))
            && code.contains(QStringLiteral(
                   "[[\"$x$width\"=="
                   "\"$base_popup_primary_x$base_popup_primary_width\"]]"))
            && code.contains(QStringLiteral(
                   "[[\"$y$height\"==\"$paint_y$paint_height\"]]"))
            && code.contains(QStringLiteral(
                   "capture_progress_only_transitionattachedattaching"))
            && code.contains(QStringLiteral(
                   "capture_progress_only_transitionfloatedfloating"))
            && code.contains(QStringLiteral(
                   "formaximizedintruefalsetruefalsetruefalsetruefalse;do"))
            && code.contains(QStringLiteral(
                   "if[[\"$target\"==\"$expected_target\""
                   "&&\"$phase\"==\"$expected_phase\""
                   "&&\"$running\"==true]]"
                   "\\&&awk-vprogress=\"$progress\""
                   "'BEGIN{exit!(progress>0.0&&progress<1.0)}'"))
            && code.contains(QStringLiteral(
                   "[[\"$geometry_revision$surface_revision$layer_revision\""
                   "==\"$base_revisions\"]]"))
            && code.contains(QStringLiteral(
                   "wait_for_in_flight_target"
                   "\"$expected_target\"\"$expected_phase\""))
            && code.contains(QStringLiteral(
                   "assert_stable_contract\"rapidreversalstorm\""))
            && code.contains(QStringLiteral(
                   "kwriteconfig6\"${group_args[@]}\""
                   "--keyscreenEdgeMargin0"))
            && code.contains(QStringLiteral(
                   "if[[\"$view_type\"==panel"
                   "&&\"$visibility_mode\"==alwaysVisible"
                   "&&\"$configured_panel\"==false"
                   "&&\"$eligible_panel\"==false"
                   "&&\"$target\"==floated"
                   "&&\"$phase\"==resting"
                   "&&\"$running\"==false]]"
                   "\\&&awk-vactual=\"$progress\""))
            && code.contains(QStringLiteral(
                   "wait_for_zero_gap_floated_snapshot"))
            && code.contains(QStringLiteral(
                   "matrix_stagedock-bottom-center-1out"))
            && code.contains(QStringLiteral(
                   "str(v[\"dockGapHideRequested\"]).lower()"))
            && code.contains(QStringLiteral(
                   "str(v[\"floatingGapConfigured\"]).lower()"))
            && code.contains(QStringLiteral(
                   "if[[\"$active_maximized\"==\"$expected_maximized\""
                   "&&\"$exists_maximized\"==\"$expected_maximized\""
                   "&&\"$view_type\"==dock"
                   "&&\"$visibility_mode\"==\"$expected_visibility\""
                   "&&\"$floating_gap_configured\"==true"
                   "&&\"$configured_panel\"==false"
                   "&&\"$eligible_panel\"==false"
                   "&&\"$configured_hide\"==true"
                   "&&\"$dock_request\"==\"$expected_request\""))
            && code.contains(QStringLiteral(
                   "&&\"$transition_geometry\"==false"
                   "&&\"$panel_geometry_absent\"==true"
                   "&&\"$floating_popups\"==false"))
            && code.contains(QStringLiteral(
                   "wait_for_dock_gap_policy"
                   "alwaysVisibletruetrueattached0"))
            && code.contains(QStringLiteral(
                   "setViewVisibilityModeus\"$view\"windowsGoBelow"))
            && code.contains(QStringLiteral(
                   "wait_for_dock_gap_policy"
                   "windowsGoBelowtruetrueattached0"))
            && code.contains(QStringLiteral(
                   "v[\"presentedScreenEdgeGap\"]"))
            && code.contains(QStringLiteral(
                   "expected_h=$((screen_h-stable_reservation_depth))"))
            && !code.contains(QStringLiteral("max_strut<base_strut"))
            && !code.contains(QStringLiteral("reservation_ms"));
    }

    static bool matchesWindowTouchAuthorityContract(
        const QString &mainQmlSource,
        const QString &modelQmlSource,
        const QString &trackerHeaderSource,
        const QString &trackerImplementationSource,
        const QString &transitionHeaderSource,
        const QString &transitionImplementationSource,
        const QString &viewHeaderSource,
        const QString &viewImplementationSource,
        const QString &behaviorConfigSource)
    {
        const QString mainQml =
            normalizedCode(mainQmlSource);
        const QString modelQml =
            normalizedCode(modelQmlSource);
        const QString trackerHeader =
            normalizedCode(trackerHeaderSource);
        const QString trackerImplementation =
            normalizedCode(trackerImplementationSource);
        const QString transitionHeader =
            normalizedCode(transitionHeaderSource);
        const QString transitionImplementation =
            normalizedCode(transitionImplementationSource);
        const QString viewHeader =
            normalizedCode(viewHeaderSource);
        const QString viewImplementation =
            normalizedCode(viewImplementationSource);
        const QString behaviorConfig =
            normalizedCode(behaviorConfigSource);

        return modelQml.contains(QStringLiteral(
                   "TaskManager.TasksModel{id:tasksModel"))
            && mainQml.contains(QStringLiteral(
                   "readonlypropertyboolfloatingTransitionEligible:"
                   "latteView&&root.behaveAsPlasmaPanel"
                   "&&latteView.floatingPanelConfigured"
                   "&&latteView.visibility"
                   "&&latteView.visibility.mode"
                   "===LatteCore.Types.AlwaysVisible"))
            && modelQml.contains(QStringLiteral(
                   "filterByVirtualDesktop:true"))
            && modelQml.contains(QStringLiteral(
                   "filterByActivity:true"))
            && modelQml.contains(QStringLiteral(
                   "filterByScreen:false"))
            && modelQml.contains(QStringLiteral(
                   "groupMode:TaskManager.TasksModel.GroupDisabled"))
            && modelQml.contains(QStringLiteral(
                   "sortMode:TaskManager.TasksModel.SortDisabled"))
            && !modelQml.contains(QStringLiteral(
                   "filterByScreen:true"))
            && trackerHeader.contains(QStringLiteral(
                   "staticconstexprintEvaluationDelayMs=10;"))
            && trackerHeader.contains(QStringLiteral(
                   "Q_PROPERTY(QRecttriggerGeometryREADtriggerGeometry"
                   "WRITEsetTriggerGeometry"
                   "NOTIFYtriggerGeometryChanged)"))
            && trackerImplementation.contains(QStringLiteral(
                   "QByteArrayLiteral(\"IsWindow\")"))
            && trackerImplementation.contains(QStringLiteral(
                   "QByteArrayLiteral(\"IsHidden\")"))
            && trackerImplementation.contains(QStringLiteral(
                   "QByteArrayLiteral(\"IsMinimized\")"))
            && trackerImplementation.contains(QStringLiteral(
                   "QByteArrayLiteral(\"Geometry\")"))
            && trackerImplementation.contains(QStringLiteral(
                   "constautoisWindow=exactBool("
                   "model->data(index,roles.isWindow),"
                   "QByteArrayLiteral(\"IsWindow\"),row);"
                   "if(!isWindow){returnstd::nullopt;}"
                   "if(!*isWindow){continue;}"
                   "constautoisHidden=exactBool("))
            && trackerImplementation.contains(QStringLiteral(
                   "if(!m_evaluationTimer.isActive())"
                   "{m_evaluationTimer.start();}"))
            && trackerImplementation.contains(QStringLiteral(
                   "StableWindowTouchTrigger::fromGeometry("
                   "m_triggerGeometry)"))
            && !trackerImplementation.contains(QStringLiteral(
                   "m_evaluationTimer.start(EvaluationDelayMs)"))
            && transitionHeader.contains(QStringLiteral(
                   "Q_INVOKABLEvoidreconcileTargetPolicy("
                   "boolfloatingPanelEligible,"
                   "boolattachOnWindowTouchConfigured,"
                   "boolattachmentWaitsForPointerExitConfigured,"
                   "boolpointerInsideView,"
                   "inttouchingWindowCount,"
                   "booldockGapHideRequested);"))
            && mainQml.contains(QStringLiteral(
                   "constcurrentTouchingWindowCount="
                   "latteView.windowTouchTracker.touchingWindowCount;"
                   "constcurrentDockGapHideRequested="
                   "directDockWindowTouchEligible"
                   "&&currentTouchingWindowCount>0;"
                   "latteView.floatingTransition."
                   "reconcileTargetPolicy("
                   "floatingTransitionEligible,"
                   "attachOnWindowTouchConfigured,"
                   "attachmentWaitsForPointerExitConfigured,"
                   "pointerInsideView,"
                   "currentTouchingWindowCount,"
                   "currentDockGapHideRequested);"
                   "updateFloatingAppletPopupHint();"))
            && mainQml.contains(QStringLiteral(
                   "functiononFloatingPanelConfiguredChanged(){"
                   "root.updateFloatingAppletPopupHint();}"))
            && mainQml.contains(QStringLiteral(
                   "functiononTargetChanged(){"
                   "root.updateFloatingAppletPopupHint();}"))
            && mainQml.contains(QStringLiteral(
                   "if(nextHints!==previousHints){"
                   "Plasmoid.containmentDisplayHints=nextHints;}"))
            && transitionImplementation.contains(QStringLiteral(
                   "constboolattachmentAlreadyTargeted="
                   "m_target==Target::Attached;"))
            && transitionImplementation.contains(QStringLiteral(
                   "m_attachmentDeferredByPointer="
                   "attachmentRequested"
                   "&&m_attachmentWaitsForPointerExitConfigured"
                   "&&m_pointerInsideView"
                   "&&!attachmentAlreadyTargeted;"))
            && transitionImplementation.contains(QStringLiteral(
                   "constboolshouldAttach="
                   "attachmentRequested"
                   "&&!m_attachmentDeferredByPointer;"))
            && transitionImplementation.contains(QStringLiteral(
                   "if(dockGapHideRequested"
                   "&&(floatingPanelEligible"
                   "||!attachOnWindowTouchConfigured"
                   "||touchingWindowCount<=0))"))
            && !transitionHeader.contains(QStringLiteral(
                   "requestAttached"))
            && !transitionHeader.contains(QStringLiteral(
                   "requestFloated"))
            && viewHeader.contains(QStringLiteral(
                   "Q_PROPERTY(Latte::ViewPart::WindowTouchTracker"
                   "*windowTouchTrackerREADwindowTouchTrackerCONSTANT)"))
            && viewImplementation.contains(QStringLiteral(
                   "newViewPart::WindowTouchTracker(this)"))
            && viewImplementation.contains(QStringLiteral(
                   "m_windowTouchTracker->setTriggerGeometry("
                   "m_floatingTransition->hasGeometry()"
                   "?m_floatingTransition->stableTriggerGeometry()"
                   ":QRect{});"))
            && viewImplementation.contains(QStringLiteral(
                   "ViewPart::FloatingPanelGeometry::solve(inputs)"))
            && viewImplementation.contains(QStringLiteral(
                   "if(m_alignment==Latte::Types::Justify)"))
            && viewImplementation.contains(QStringLiteral(
                   "ViewPart::FloatingPanelGeometry::solvePlacement({"
                   ".outputGeometry=outputGeometry,"
                   ".availablePrimaryGeometry=geometry(),"
                   ".edge=*edge,"
                   ".alignment=ViewPart::FloatingPanelGeometry::"
                   "PrimaryAxisAlignment::Center,"
                   ".maxLength=maxLength(),"
                   ".offset=0.0F,"))
            && mainQml.contains(QStringLiteral(
                   "readonlypropertybooldockGapHideRequested:"
                   "directDockWindowTouchEligible"
                   "&&latteView.windowTouchTracker"
                   "&&latteView.windowTouchTracker."
                   "touchingWindowCount>0"))
            && !viewImplementation.contains(QStringLiteral(
                   "&ViewPart::WindowTouchTracker::"
                   "touchingWindowCountChanged"))
            && mainQml.contains(QStringLiteral(
                   "functiononTouchingWindowCountChanged()"
                   "{root.reconcileFloatingTargetPolicy();}"))
            && behaviorConfig.contains(QStringLiteral(
                   "text:latteView.type===LatteCore.Types.PanelView"
                   "?i18n(\"Attachpanelwhenawindowtouchesit\")"
                   ":i18n(\"Hidefloatinggapformaximizedwindows\")"))
            && behaviorConfig.contains(QStringLiteral(
                   "text:latteView.type===LatteCore.Types.PanelView"
                   "?i18n(\"Waituntilthepointerleavesbeforeattaching\")"
                   ":i18n(\"Delayfloatinggaphidinguntilmouseleaves\")"));
    }

    static bool matchesWindowTouchE2eContract(
        const QString &source)
    {
        const QString code = normalizedCode(source);
        const qsizetype cleanupTrap =
            code.indexOf(QStringLiteral("trapcleanupEXIT"));
        const qsizetype fixtureStage =
            code.indexOf(QStringLiteral(
                "matrix_stagepanel-bottom-justify-1out"));

        return cleanupTrap >= 0
            && fixtureStage > cleanupTrap
            && code.contains(QStringLiteral(
                   "--keyfloatingInternalGapIsForcedfalse"))
            && code.contains(QStringLiteral(
                   "snapshot['schemaVersion']!=9"))
            && code.contains(QStringLiteral(
                   "v[\"attachOnWindowTouchConfigured\"]"))
            && code.contains(QStringLiteral(
                   "v[\"attachmentWaitsForPointerExitConfigured\"]"))
            && code.contains(QStringLiteral(
                   "v[\"pointerInsideView\"]"))
            && code.contains(QStringLiteral(
                   "v[\"attachmentDeferredByPointer\"]"))
            && code.contains(QStringLiteral(
                   "v[\"floatingGapConfigured\"]"))
            && code.contains(QStringLiteral(
                   "v[\"touchingWindowCount\"]"))
            && code.contains(QStringLiteral(
                   "v[\"windowTouchGeometryRoleType\"]"))
            && code.contains(QStringLiteral(
                   "*v[\"appletsLayoutGeometry\"],"
                   "math.floor(v[\"computedPaintMaskGeometry\"][1]),"
                   "math.ceil(v[\"computedPaintMaskGeometry\"][1]"
                   "+v[\"computedPaintMaskGeometry\"][3])"
                   "-math.floor(v[\"computedPaintMaskGeometry\"][1])"))
            && code.contains(QStringLiteral(
                   "[[\"$x$width\"=="
                   "\"$base_popup_primary_x$base_popup_primary_width\"]]"))
            && code.contains(QStringLiteral(
                   "[[\"$y$height\"==\"$paint_y$paint_height\"]]"))
            && code.contains(QStringLiteral(
                   "\"windowTouchTracker\":v[\"objects\"]"
                   "[\"windowTouchTracker\"]"))
            && !code.contains(QStringLiteral(
                   "Qt.rect("))
            && code.contains(QStringLiteral(
                   "constgeometry=Object.assign({},w.frameGeometry);"
                   "geometry.x=$x;"
                   "geometry.y=$y;"
                   "geometry.width=$width;"
                   "geometry.height=$height;"
                   "w.frameGeometry=geometry;"))
            && code.contains(QStringLiteral(
                   "org.kde.kglobalaccel.Component"
                   "invokeShortcuts\"WindowMove\""))
            && code.contains(QStringLiteral(
                   "nudge_verticalDown\"$touch_nudges\""))
            && code.contains(QStringLiteral(
                   "nudge_verticalUp\"$touch_nudges\""))
            && code.count(QStringLiteral(
                   "capture_fractional_policyfalsefalse1attachedattaching"))
                   >= 3
            && code.count(QStringLiteral(
                   "capture_fractional_policyfalsefalse0floatedfloating"))
                   >= 3
            && code.contains(QStringLiteral(
                   "\"fractionalfloating-to-attachingreversal\""))
            && code.contains(QStringLiteral(
                   "\"fractionalattaching-to-floatingreversal\""))
            && code.contains(QStringLiteral(
                   "\"interactivedragbackout\""))
            && code.contains(QStringLiteral(
                   "\"interactivedragintostabletrigger\""))
            && code.contains(QStringLiteral(
                   "fpkeyEscape"))
            && code.contains(QStringLiteral(
                   "wait_for_konsole_geometry"))
            && code.contains(QStringLiteral(
                   "\"$baseline_x\""))
            && code.contains(QStringLiteral(
                   "\"$baseline_y\""))
            && code.contains(QStringLiteral(
                   "\"$client_width\""))
            && code.contains(QStringLiteral(
                   "\"$client_height\""))
            && code.contains(QStringLiteral(
                   "set_konsole_maximizedfalse"))
            && code.contains(QStringLiteral(
                   "wait_for_maximize_mode0"))
            && code.contains(QStringLiteral(
                   "set_konsole_maximizedtrue"))
            && code.contains(QStringLiteral(
                   "wait_for_maximize_mode3"))
            && code.contains(QStringLiteral(
                   "[[\"$geometry_role_type\"==QRect]]"))
            && code.contains(QStringLiteral(
                   "wait_for_policytruefalse1attached0"))
            && code.contains(QStringLiteral(
                   "capture_fractional_policytruefalse0floatedfloating"))
            && code.contains(QStringLiteral(
                   "wait_for_policytruefalse0floated1"))
            && code.contains(QStringLiteral(
                   "wait_for_policytruetrue1floated1"))
            && code.contains(QStringLiteral(
                   "capture_fractional_policyfalsefalse1attachedattaching"))
            && code.contains(QStringLiteral(
                   "fpglide2020\"$pointer_x\"\"$pointer_y\""))
            && code.contains(QStringLiteral(
                   "fpglide\"$pointer_x\"\"$pointer_y\"2020"))
            && code.contains(QStringLiteral(
                   "\"pointer-presentattachmentdeferral\""))
            && code.count(QStringLiteral(
                   "wait_for_policyfalsefalse0floated1")) >= 2
            && code.contains(QStringLiteral(
                   "kill\"$kpid\""))
            && code.contains(QStringLiteral(
                   "\"couldnotdestroythesinglewindow-touchclient\""))
            && code.count(QStringLiteral(
                   "assert_stable_contract")) >= 4
            && code.contains(QStringLiteral(
                   "\"reservationStateGeneration\":"
                   "snapshot[\"reservationStateGeneration\"]"))
            && code.contains(QStringLiteral(
                   "v[\"transitionGeometryRevision\"],"
                   "v[\"surfaceGeometryPublicationRevision\"],"
                   "v[\"layerShellConfigureRequestRevision\"]"))
            && code.contains(QStringLiteral(
                   "\"$transition_token\"!=\"$tracker_token\""));
    }

    static bool matchesLiveTitlebarWindowTouchE2eContract(
        const QString &source)
    {
        const QString code = normalizedCode(source);
        const qsizetype cleanupTrap =
            code.indexOf(QStringLiteral("trapcleanupEXIT"));
        const qsizetype panelCase =
            code.indexOf(QStringLiteral(
                "configure_casepanel-top-center-1out"));
        const qsizetype dockCase =
            code.indexOf(QStringLiteral(
                "configure_casedock-top-center-1out"));
        const qsizetype justifyDockCase =
            code.indexOf(QStringLiteral(
                "configure_casedock-top-justify-1out"));
        const QString heldPolicy = normalizedCode(functionBody(
            source, QStringLiteral("wait_for_policy_while_held()")));
        const QString heldFraction = normalizedCode(functionBody(
            source,
            QStringLiteral("wait_for_fractional_progress_while_held()")));
        const QString heldProof = QStringLiteral(
            "kill-0\"$drag_pid\"2>/dev/null"
            "\\||e2e_fail\"$boundaryappearedonlyafterbuttonrelease\"");

        return cleanupTrap >= 0
            && panelCase > cleanupTrap
            && dockCase > panelCase
            && justifyDockCase > dockCase
            && code.contains(QStringLiteral(
                   "snapshot['schemaVersion']!=9"))
            && code.contains(QStringLiteral(
                   "--keyfloatingGapHidingWaitsMousefalse"))
            && code.contains(QStringLiteral(
                   "--keymaximizeWhenMaximizedtrue"))
            && code.contains(QStringLiteral(
                   "fpdraghold900"))
            && code.contains(QStringLiteral(
                   "\"$start_x\"\"$touching_y\""))
            && code.contains(QStringLiteral(
                   "\"$start_x\"\"$start_y\"&"
                   "drag_pid=$!"))
            && code.count(QStringLiteral(
                   "kill-0\"$drag_pid\"")) >= 2
            && heldPolicy.contains(heldProof)
            && heldFraction.contains(heldProof)
            && heldFraction.contains(QStringLiteral(
                   "fractional_presentation_probe"))
            && !heldFraction.contains(QStringLiteral(
                   "<<<\"$(presentation_probe)\""))
            && code.contains(QStringLiteral(
                   "exercise_held_dragpaneltruefalseattached"))
            && code.contains(QStringLiteral(
                   "exercise_held_dragdockfalsetrueattached"))
            && code.contains(QStringLiteral(
                   "exercise_held_dragdockfalsetrueattachedtrue"))
            && code.contains(QStringLiteral(
                   "assert_partial_dock_presentation"
                   "\\"
                   "\"$expected_typeliveattachment\""
                   "\\"
                   "\"$base_presented_x\""
                   "\"$base_presented_length\""))
            && code.contains(QStringLiteral(
                   "wait_for_partial_panel_attached_presentation"
                   "\\"
                   "\"$base_presented_x\""
                   "\"$base_presented_length\""))
            && code.contains(QStringLiteral(
                   "\"$borders\"==bottom,left,right"))
            && code.count(QStringLiteral(
                   "wait_for_policy_while_held")) >= 4
            && code.count(QStringLiteral(
                   "wait_for_fractional_progress_while_held")) >= 3
            && code.contains(QStringLiteral(
                   "presented_gap_matches_progress"))
            && code.contains(QStringLiteral(
                   "dock_length_matches_progress"))
            && code.contains(QStringLiteral(
                   "v[\"windowGeometry\"][0]+v[\"effectsRect\"][0]"))
            && code.contains(QStringLiteral(
                   "v[\"effectsRect\"][2]"))
            && code.count(QStringLiteral(
                   "wait_for_dock_attached_presentation_while_held")) >= 2
            && code.contains(QStringLiteral(
                   "\"$presented_x\"-eq\"$output_x\""
                   "&&\"$presented_length\"-eq\"$output_length\""
                   "&&\"$borders\"==bottom"))
            && code.contains(QStringLiteral(
                   "wait_for_dock_floated_presentation"
                   "\\"
                   "\"$base_presented_x\"\"$base_presented_length\""))
            && code.contains(QStringLiteral(
                   "\"$borders\"==bottom"))
            && code.contains(QStringLiteral(
                   "\"$borders\"==bottom,left,right,top"))
            && code.contains(QStringLiteral(
                   "v[\"presentedScreenEdgeGap\"]"))
            && code.count(QStringLiteral(
                   "assert_stable_physical_snapshot")) >= 4
            && code.contains(QStringLiteral(
                   "actual=\"$(stable_physical_snapshot)\""))
            && code.contains(QStringLiteral(
                   "\"stableTriggerGeometry\":"
                   "v[\"stableTriggerGeometry\"]"))
            && code.contains(QStringLiteral(
                   "\"reservationStateGeneration\":"
                   "snapshot[\"reservationStateGeneration\"]"))
            && code.contains(QStringLiteral(
                   "\"surfaceGeometryPublicationRevision\":"
                   "v[\"surfaceGeometryPublicationRevision\"]"))
            && code.contains(QStringLiteral(
                   "\"layerShellConfigureRequestRevision\":"
                   "v[\"layerShellConfigureRequestRevision\"]"))
            && code.contains(QStringLiteral(
                   "\"windowTouchTracker\":"
                   "v[\"objects\"][\"windowTouchTracker\"]"))
            && code.contains(QStringLiteral(
                   "\"configuredIconSize\":v[\"configuredIconSize\"]"))
            && code.contains(QStringLiteral(
                   "\"effectiveIconSize\":v[\"effectiveIconSize\"]"))
            && code.contains(QStringLiteral(
                   "\"absoluteGeometry\":v[\"absoluteGeometry\"]"))
            && code.contains(QStringLiteral(
                   "\"localGeometry\":v[\"localGeometry\"]"))
            && code.contains(QStringLiteral(
                   "\"availablePrimaryLength\":"
                   "v[\"availablePrimaryLength\"]"))
            && code.contains(QStringLiteral(
                   "\"$trigger_height\""
                   "-eq\"$expected_envelope_depth\""))
            && code.contains(QStringLiteral(
                   "expected_envelope_depth=$((normal+gap))"
                   "expected_normal=\"$maximum\""));
    }

    static bool matchesWindowTouchTopologyE2eContract(
        const QString &recipeSource,
        const QString &oracleSource)
    {
        const QString recipe = normalizedCode(recipeSource);
        const QString cleanup = normalizedCode(functionBody(
            recipeSource, QStringLiteral("cleanup()")));
        const QString oracle = normalizedCode(oracleSource);
        const qsizetype pristineCapture =
            recipe.indexOf(QStringLiteral("matrix_init"));
        const qsizetype transactionActivation =
            recipe.indexOf(QStringLiteral("fixture_transaction_active=1"));
        const qsizetype cleanupTrap =
            recipe.indexOf(QStringLiteral("trapcleanupEXIT"));
        const qsizetype fixtureStage =
            recipe.indexOf(QStringLiteral(
                "matrix_stagepanel-bottom-justify-1out"));
        const qsizetype topologyRestore =
            cleanup.indexOf(QStringLiteral(
                "mo_restore_output_topology\"$original_topology\""));
        const qsizetype transactionGuard =
            cleanup.indexOf(QStringLiteral(
                "if((fixture_transaction_active==1));then"));
        const qsizetype pristineRemoval =
            cleanup.indexOf(QStringLiteral(
                "rm-rf\"${E2E_CONFIG_HOME:?}\""), transactionGuard);
        const qsizetype pristineRestore =
            cleanup.indexOf(QStringLiteral(
                "cp-r\"$MATRIX_PRISTINE\"\"$E2E_CONFIG_HOME\""),
                pristineRemoval);
        const qsizetype dockRestart =
            cleanup.indexOf(QStringLiteral(
                "elif!e2e_dock_start90;then"), pristineRestore);

        return recipe.contains(QStringLiteral(
                   "fixture_transaction_active=0"))
            && pristineCapture >= 0
            && transactionActivation > pristineCapture
            && cleanupTrap >= 0
            && fixtureStage > cleanupTrap
            && fixtureStage > transactionActivation
            && topologyRestore >= 0
            && transactionGuard > topologyRestore
            && pristineRemoval > transactionGuard
            && pristineRestore > pristineRemoval
            && dockRestart > pristineRestore
            && cleanup.contains(QStringLiteral(
                   "((body_status==0))&&body_status=1"))
            && cleanup.contains(QStringLiteral("exit\"$body_status\""))
            && recipe.count(QStringLiteral("duplicate_independently")) >= 3
            && recipe.contains(QStringLiteral(
                   "setViewPlacementuiii\"$view_a\"\"$primary_id\"41"))
            && recipe.contains(QStringLiteral(
                   "setViewPlacementuiii\"$view_b\"\"$primary_id\"42"))
            && recipe.contains(QStringLiteral(
                   "setViewPlacementuiii\"$view_c\"\"$secondary_id\"50"))
            && recipe.count(QStringLiteral(
                   "mo_place_secondary_for_topology")) == 3
            && recipe.contains(QStringLiteral(
                   "drive_client_casegap-onlynone"))
            && recipe.contains(QStringLiteral(
                   "drive_client_casefull-primary\"$view_a,$view_b\""))
            && recipe.contains(QStringLiteral(
                   "drive_client_casespanning\"$view_b,$view_c\""))
            && recipe.contains(QStringLiteral(
                   "drive_client_caseminimized\"$view_b,$view_c\"true"))
            && recipe.contains(QStringLiteral(
                   "[[\"$after_restart\"==\"$before_restart\"]]"))
            && oracle.contains(QStringLiteral(
                   "ifsnapshot.get(\"schemaVersion\")!=9:"))
            && oracle.contains(QStringLiteral(
                   "view[\"relationship\"]!=\"independent\""))
            && oracle.contains(QStringLiteral(
                   "iflen(set(tokens))!=len(tokens):"))
            && oracle.contains(QStringLiteral(
                   "group[\"publishedDepth\"]!=max(depths)"))
            && oracle.contains(QStringLiteral(
                   "group[\"publishedDepth\"]==sum(depths)"))
            && oracle.contains(QStringLiteral(
                   "ifrect_counts.get(expected,0)!=1:"))
            && oracle.contains(QStringLiteral(
                   "\"availablePrimaryLength\","))
            && oracle.contains(QStringLiteral(
                   "\"transitionGeometryRevision\","))
            && oracle.contains(QStringLiteral(
                   "\"popupAnchorPrimarySpan\":"))
            && oracle.contains(QStringLiteral(
                   "assert_popup_primary_geometry("))
            && oracle.contains(QStringLiteral(
                   "\"continuousA-Bactivationstrip\""))
            && oracle.contains(QStringLiteral(
                   "\"wrongpopupprimaryspan\""))
            && oracle.contains(QStringLiteral(
                   "\"popupprimaryoriginoutsidestablepaint\""));
    }

    static bool matchesLinkedOperationStormE2eContract(
        const QString &recipeSource,
        const QString &modelSource)
    {
        const QString recipe = normalizedCode(recipeSource);
        const QString cleanup = normalizedCode(functionBody(
            recipeSource, QStringLiteral("cleanup()")));
        const QString restore = normalizedCode(functionBody(
            recipeSource, QStringLiteral("restore_config_exactly()")));
        const QString visualOwnership = normalizedCode(functionBody(
            recipeSource,
            QStringLiteral("assert_visual_window_ownership()")));
        const QString durableMoveReadback = normalizedCode(functionBody(
            recipeSource,
            QStringLiteral("assert_no_pending_view_move()")));
        const QString durableMoveLifecycle = normalizedCode(functionBody(
            recipeSource,
            QStringLiteral("assert_view_move_lifecycle()")));
        const QString model = normalizedCode(modelSource);

        const qsizetype privateSessionGuard =
            recipe.lastIndexOf(QStringLiteral(
                "require_private_nested_session"));
        const qsizetype privatePlanStage =
            recipe.indexOf(QStringLiteral(
                "candidate_plan=\"$(mktemp"
                "\"$E2E_RT/fp4c-operation-plan.XXXXXX\")\""));
        const qsizetype suppliedPlanValidation =
            recipe.indexOf(QStringLiteral(
                "python3\"$MODEL\"validate-plan"
                "<\"$plan_source\">/dev/null"));
        const qsizetype privatePlanCopy =
            recipe.indexOf(QStringLiteral(
                "cp--\"$plan_source\"\"$candidate_plan\""));
        const qsizetype cleanupTrap =
            recipe.indexOf(QStringLiteral("trapcleanupEXIT"));
        const qsizetype baselineCapture =
            recipe.indexOf(QStringLiteral(
                "snapshot>\"$baseline_snapshot_file\""));
        const qsizetype pristineStop =
            recipe.indexOf(QStringLiteral(
                "stop_dock_if_running"), baselineCapture);
        const qsizetype pristineBackup =
            recipe.indexOf(QStringLiteral(
                "cp-a--\"$E2E_CONFIG_HOME/.\"\"$backup_dir/\""));
        const qsizetype configReplacement =
            recipe.indexOf(QStringLiteral(
                "rm-rf--\"$E2E_CONFIG_HOME\""), pristineBackup);
        const qsizetype cleanupStop =
            cleanup.indexOf(QStringLiteral("stop_dock_if_running"));
        const qsizetype cleanupRemove =
            restore.indexOf(QStringLiteral(
                "rm-rf--\"$E2E_CONFIG_HOME\""));
        const qsizetype cleanupRestore =
            restore.indexOf(QStringLiteral(
                "cp-a--\"$backup_dir/.\"\"$E2E_CONFIG_HOME/\""),
                cleanupRemove);
        const qsizetype cleanupCompare =
            restore.indexOf(QStringLiteral(
                "diff-qr--no-dereference"
                "\"$backup_dir\"\"$E2E_CONFIG_HOME\""),
                cleanupRestore);
        const qsizetype cleanupRestart =
            cleanup.indexOf(QStringLiteral(
                "e2e_dock_start90"));

        constexpr qsizetype minimumPlanValidationCount = 3;
        return privateSessionGuard >= 0
            && privatePlanStage > privateSessionGuard
            && suppliedPlanValidation > privatePlanStage
            && privatePlanCopy > suppliedPlanValidation
            && cleanupTrap > privatePlanCopy
            && baselineCapture > cleanupTrap
            && pristineStop > baselineCapture
            && pristineBackup > pristineStop
            && configReplacement > pristineBackup
            && cleanupStop >= 0
            && cleanup.contains(QStringLiteral(
                   "if[[\"$backup_ready\"==true"
                   "&&\"$dock_stopped\"==true]];then"))
            && cleanup.contains(QStringLiteral(
                   "restore_config_exactly"))
            && cleanup.count(QStringLiteral(
                   "&&\"$config_safe_to_start\"==true")) == 2
            && cleanupRemove >= 0
            && cleanupRestore > cleanupRemove
            && cleanupCompare > cleanupRestore
            && cleanupRestart > cleanupStop
            && recipe.count(QStringLiteral(
                   "python3\"$MODEL\"validate-plan"))
                >= minimumPlanValidationCount
            && recipe.contains(QStringLiteral(
                   "kwriteconfig6\"${panel_group[@]}\""
                   "--keyminLength45"))
            && recipe.contains(QStringLiteral(
                   "kwriteconfig6\"${panel_group[@]}\""
                   "--keymaxLength45"))
            && recipe.contains(QStringLiteral(
                   "kwriteconfig6\"${panel_group[@]}\""
                   "--keyscreenEdgeMargin18"))
            && recipe.contains(QStringLiteral(
                   "assert_tombstone_on_disk"
                   "\"$removed_this_step\""))
            && recipe.contains(QStringLiteral(
                   "removal_elapsed_ms<60000"))
            && recipe.contains(QStringLiteral(
                   "build_replay_header_input"
                   "|python3\"$MODEL\"replay-header"
                   ">\"$replay_file\""))
            && recipe.contains(QStringLiteral(
                   "python3\"$MODEL\"validate-replay"))
            && recipe.contains(QStringLiteral(
                   "--plan\"$plan_file\""))
            && recipe.contains(QStringLiteral(
                   "--replay\"$replay_file\""))
            && recipe.contains(QStringLiteral(
                   "wait_for_quiescent_projection"))
            && recipe.contains(QStringLiteral(
                   "wait_for_visual_window_ownership"))
            && recipe.contains(QStringLiteral(
                   "||-n\"$removed_this_step\""))
            && visualOwnership.contains(QStringLiteral(
                   "python3\"$MODEL\""
                   "assert-visual-window-ownership"))
            && visualOwnership.contains(QStringLiteral(
                   "\"outputs\":outputs"))
            && durableMoveReadback.contains(QStringLiteral(
                   "e2e_jsonviewMoveTransactionsData"))
            && durableMoveReadback.contains(QStringLiteral(
                   "\"journalCreatedGeneration\""))
            && durableMoveReadback.contains(QStringLiteral(
                   "\"commitDecisionGeneration\""))
            && durableMoveReadback.contains(QStringLiteral(
                   "\"journalRetiredGeneration\""))
            && durableMoveReadback.contains(QStringLiteral(
                   "state[\"schemaVersion\"]!=2"))
            && durableMoveReadback.contains(QStringLiteral(
                   "state[\"transactions\"]!=[]"))
            && durableMoveLifecycle.contains(QStringLiteral(
                   "python3\"$MODEL\"assert-view-move-lifecycle"))
            && recipe.count(QStringLiteral(
                   "assert_no_pending_view_move"))
                == 5
            && recipe.contains(QStringLiteral(
                   "$step_tag.view-move.before.json"))
            && recipe.contains(QStringLiteral(
                   "$step_tag.view-move.after.json"))
            && recipe.contains(QStringLiteral(
                   "final.view-move-transactions.json"))
            && recipe.contains(QStringLiteral(
                   "--groupUniversalSettings--keymemoryUsage1"))
            && recipe.contains(QStringLiteral(
                   "e2e_jsonlayoutsData"))
            && recipe.contains(QStringLiteral(
                   "\"name\":screen[\"name\"]"))
            && recipe.contains(QStringLiteral(
                   "\"geometry\":screen[\"geometry\"]"))
            && model.contains(QStringLiteral(
                   "\"currentVisibleGeometry\""))
            && !model.contains(QStringLiteral("\"visibleGeometry\""))
            && model.contains(QStringLiteral(
                   "classOperationKind(str,Enum):"))
            && model.contains(QStringLiteral(
                   "MOVE_LAYOUT=\"moveLayout\""))
            && model.contains(QStringLiteral(
                   "\"method\":\"moveViewToLayout\""))
            && model.contains(QStringLiteral(
                   "ifoperation.kindisOperationKind.MOVE_LAYOUT"))
            && model.contains(QStringLiteral(
                   "expected_delta=(1ifoperation.kindis"
                   "OperationKind.MOVE_LAYOUTelse0)"))
            && model.contains(QStringLiteral(
                   "observed_delta!=expected_delta"))
            && model.contains(QStringLiteral(
                   "iflen(group_keys)!=len(set(group_keys)):"))
            && model.contains(QStringLiteral(
                   "ifleft[1]>right[0]:"))
            && model.contains(QStringLiteral(
                   "classOutputSnapshot:"))
            && model.contains(QStringLiteral(
                   "outputs=parse_outputs(payload[\"outputs\"])"))
            && model.contains(QStringLiteral(
                   "expected_global_reservation_window_geometry("))
            && model.contains(QStringLiteral(
                   "group[\"geometry\"]!=expected_geometry"))
            && model.contains(QStringLiteral(
                   "group[\"layerShellAnchors\"]!=expected_anchors"))
            && model.contains(QStringLiteral(
                   "group[\"layerShellMargins\"]!=[0,0,0,0]"))
            && model.contains(QStringLiteral(
                   "view[\"publishedStruts\"]!=expected_strut"))
            && model.contains(QStringLiteral(
                   "forgroupinsnapshot[\"reservationGroups\"]:"))
            && model.contains(QStringLiteral(
                   "iflen(candidates)!=1:"))
            && model.contains(QStringLiteral(
                   "ifunmatched:"))
            && model.contains(QStringLiteral(
                   "ifoperation!=expected_operation:"))
            && model.contains(QStringLiteral(
                   "andafter_views[persistent_id]"
                   "[\"runtimeViewId\"]inbefore_runtime_ids"));
    }

    static bool matchesCompleteKScreenRestoreContract(const QString &source)
    {
        const QString code = normalizedCode(source);
        const QString waitBody = normalizedCode(functionBody(
            source,
            QStringLiteral("_mo_wait_for_captured_output_topology()")));
        const QString restoreBody = normalizedCode(functionBody(
            source,
            QStringLiteral("mo_restore_output_topology()")));

        return code.contains(QStringLiteral(
                   "defsort_identity_collection(records,identity,path):"))
            && code.contains(QStringLiteral(
                   "payload.get(\"outputs\"),\"name\","
                   "f\"{label}.outputs\""))
            && code.contains(QStringLiteral(
                   "if\"modes\"incanonical_output:"))
            && code.contains(QStringLiteral(
                   "canonical_output[\"modes\"]="
                   "sort_identity_collection("))
            && code.contains(QStringLiteral(
                   "difference=first_difference(captured,current)"))
            && code.contains(QStringLiteral(
                   "completeKScreenstatedriftedat{difference}"))
            && code.contains(QStringLiteral(
                   "sorted(priorities)==[1,2]"))
            && code.contains(QStringLiteral(
                   "ifnotisinstance(payload,dict):"))
            && code.contains(QStringLiteral(
                   "ifnotisinstance(output_name,str)ornotoutput_name:"))
            && code.contains(QStringLiteral(
                   "\"output.${E2E_MO_PRIMARY}.priority.1\""))
            && code.contains(QStringLiteral(
                   "\"output.${E2E_MO_SECONDARY}.priority.2\""))
            && waitBody.contains(QStringLiteral(
                   "_mo_compare_output_state_semantically"
                   "\"$captured\"\"$current\"2>&1"))
            && waitBody.contains(QStringLiteral(
                   "if((comparison_status==2));then"))
            && !waitBody.contains(QStringLiteral(
                   "[[\"$current_projection\"==\"$expected\"]]"
                   "&&return0"))
            && restoreBody.count(QStringLiteral(
                   "\"output.${name}.${enabled}\"")) == 1
            && restoreBody.count(QStringLiteral(
                   "\"output.${name}.rotation.${rotation}\"")) == 1
            && restoreBody.count(QStringLiteral(
                   "\"output.${name}.scale.${scale}\"")) == 1
            && restoreBody.count(QStringLiteral(
                   "\"output.${name}.position.${x},${y}\"")) == 1
            && restoreBody.count(QStringLiteral(
                   "\"output.${name}.priority.${priority}\"")) == 1
            && !restoreBody.contains(QStringLiteral(".mode."))
            && restoreBody.contains(QStringLiteral(
                   "localnameenabledrotationscalexypriority"));
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
            "if(barLine.stablePanelEnvelope"
            "&&LatteCore.WindowSystem.compositingActive)"));
        const int requestedLength = lengthBinding.indexOf(QStringLiteral(
            "constrequestedLength=myView.alignment===LatteCore.Types.Justify"
            "?maximumLength:Math.max("));
        const int owningCanvas = lengthBinding.indexOf(QStringLiteral(
            "constviewPrimaryLength=Plasmoid.formFactor"
            "===PlasmaCore.Types.Horizontal?barLine.parent.width:barLine.parent.height;"));
        const int fittedLength = lengthBinding.indexOf(QStringLiteral(
            "returnbackgroundStateResolver.dockBackgroundLength("
            "requestedLength,viewPrimaryLength);"));

        return panelPath != -1
            && requestedLength > panelPath
            && owningCanvas > requestedLength
            && fittedLength > owningCanvas
            && !lengthBinding.contains(QStringLiteral("shadowMarginsLength"))
            && !lengthBinding.contains(QStringLiteral(
                "dockBackgroundLength(requestedLength,maximumLength)"))
            && offsetBinding.contains(QStringLiteral(
                "consttailShadowLength=Plasmoid.formFactor"
                "===PlasmaCore.Types.Horizontal"
                "?barLine.shadows.left:barLine.shadows.top;"))
            && offsetBinding.contains(QStringLiteral(
                "constheadShadowLength=Plasmoid.formFactor"
                "===PlasmaCore.Types.Horizontal"
                "?barLine.shadows.right:barLine.shadows.bottom;"))
            && offsetBinding.contains(QStringLiteral(
                "constrequestedSolidOffset="
                "alignment===LatteCore.Types.Justify"
                "?0:root.offset+layoutsContainerItem.mainLayout.parabolicOffsetting;"))
            && offsetBinding.contains(QStringLiteral(
                "returnbackgroundStateResolver.dockVisualCenterOffset("
                "requestedSolidOffset,barLine.length,tailShadowLength,"
                "headShadowLength,viewPrimaryLength);"))
            && !offsetBinding.contains(QStringLiteral(
                "backgroundStateResolver.centeredDockOffset("))
            && !offsetBinding.contains(QStringLiteral(
                "barLine.totals.visualLength"))
            && source.count(QStringLiteral(
                "anchors.horizontalCenterOffset: barLine.offset;"
                " anchors.verticalCenterOffset: 0;")) == 4
            && source.count(QStringLiteral(
                "anchors.horizontalCenterOffset: 0;"
                " anchors.verticalCenterOffset: barLine.offset;")) == 4;
    }

    static bool matchesStableFloatingPanelLayoutClearance(
        const QString &backgroundSource,
        const QString &bindingsSource)
    {
        const QString background = normalizedCode(backgroundSource);
        const QString bindings = normalizedCode(bindingsSource);
        const QString tailRoundness = normalizedCode(functionBody(
            backgroundSource,
            QStringLiteral("readonly property int tailRoundness:")));
        const QString headRoundness = normalizedCode(functionBody(
            backgroundSource,
            QStringLiteral("readonly property int headRoundness:")));

        const QString stableAuthority = QStringLiteral(
            "readonlypropertyboolstablePrimaryAxisLayoutClearance:"
            "!!(barLine.dockView"
            "&&(barLine.dockView.floatingPanelConfigured"
            "||barLine.containmentRoot.dockFloatingTransitionOwnsGap))");
        const QString topClearance = QStringLiteral(
            "readonlypropertybooltopLayoutClearanceIsRequired:"
            "backgroundStateResolver.layoutClearanceIsRequired("
            "barLine.hasTopBorder,barLine.containmentRoot.isVertical,"
            "barLine.stablePrimaryAxisLayoutClearance)");
        const QString bottomClearance = QStringLiteral(
            "readonlypropertyboolbottomLayoutClearanceIsRequired:"
            "backgroundStateResolver.layoutClearanceIsRequired("
            "barLine.hasBottomBorder,barLine.containmentRoot.isVertical,"
            "barLine.stablePrimaryAxisLayoutClearance)");
        const QString leftClearance = QStringLiteral(
            "readonlypropertyboolleftLayoutClearanceIsRequired:"
            "backgroundStateResolver.layoutClearanceIsRequired("
            "barLine.hasLeftBorder,barLine.containmentRoot.isHorizontal,"
            "barLine.stablePrimaryAxisLayoutClearance)");
        const QString rightClearance = QStringLiteral(
            "readonlypropertyboolrightLayoutClearanceIsRequired:"
            "backgroundStateResolver.layoutClearanceIsRequired("
            "barLine.hasRightBorder,barLine.containmentRoot.isHorizontal,"
            "barLine.stablePrimaryAxisLayoutClearance)");

        const QString commonLivePaddingInputs = QStringLiteral(
            "barLine.customRadiusIsEnabled,barLine.customRadius,");
        const QString commonLivePaddingTail = QStringLiteral(
            "metrics.margin.length,indicators.info.backgroundCornerMargin)");
        const QString topPadding = QStringLiteral(
            "paddings.top:backgroundStateResolver.edgePadding("
            "barLine.topLayoutClearanceIsRequired,root.isVertical,")
            + commonLivePaddingInputs
            + QStringLiteral(
                "barLine.themeExtendedBackground?"
                "barLine.themeExtendedBackground.paddingTop:0,"
                "solidBackground.margins.top,")
            + commonLivePaddingTail;
        const QString bottomPadding = QStringLiteral(
            "paddings.bottom:backgroundStateResolver.edgePadding("
            "barLine.bottomLayoutClearanceIsRequired,root.isVertical,")
            + commonLivePaddingInputs
            + QStringLiteral(
                "barLine.themeExtendedBackground?"
                "barLine.themeExtendedBackground.paddingBottom:0,"
                "solidBackground.margins.bottom,")
            + commonLivePaddingTail;
        const QString leftPadding = QStringLiteral(
            "paddings.left:backgroundStateResolver.edgePadding("
            "barLine.leftLayoutClearanceIsRequired,root.isHorizontal,")
            + commonLivePaddingInputs
            + QStringLiteral(
                "barLine.themeExtendedBackground?"
                "barLine.themeExtendedBackground.paddingLeft:0,"
                "solidBackground.margins.left,")
            + commonLivePaddingTail;
        const QString rightPadding = QStringLiteral(
            "paddings.right:backgroundStateResolver.edgePadding("
            "barLine.rightLayoutClearanceIsRequired,root.isHorizontal,")
            + commonLivePaddingInputs
            + QStringLiteral(
                "barLine.themeExtendedBackground?"
                "barLine.themeExtendedBackground.paddingRight:0,"
                "solidBackground.margins.right,")
            + commonLivePaddingTail;

        return background.count(stableAuthority) == 1
            && background.count(QStringLiteral(
                   "backgroundStateResolver.layoutClearanceIsRequired(")) == 4
            && background.contains(topClearance)
            && background.contains(bottomClearance)
            && background.contains(leftClearance)
            && background.contains(rightClearance)
            && background.contains(topPadding)
            && background.contains(bottomPadding)
            && background.contains(leftPadding)
            && background.contains(rightPadding)
            && tailRoundness.contains(QStringLiteral(
                   "constlayoutClearanceIsRequired=root.isHorizontal"
                   "?leftLayoutClearanceIsRequired"
                   ":topLayoutClearanceIsRequired;"
                   "if(layoutClearanceIsRequired)"))
            && headRoundness.contains(QStringLiteral(
                   "constlayoutClearanceIsRequired=root.isHorizontal"
                   "?rightLayoutClearanceIsRequired"
                   ":bottomLayoutClearanceIsRequired;"
                   "if(layoutClearanceIsRequired)"))
            && tailRoundness.contains(QStringLiteral(
                   "constexpected=customRadiusIsEnabled?customAppliedRadius"
                   ":Math.max(themePadding,solidBackgroundPadding);"))
            && headRoundness.contains(QStringLiteral(
                   "constexpected=customRadiusIsEnabled?customAppliedRadius"
                   ":Math.max(themePadding,solidBackgroundPadding);"))
            && !tailRoundness.contains(QStringLiteral("hasLeftBorder"))
            && !tailRoundness.contains(QStringLiteral("hasTopBorder"))
            && !headRoundness.contains(QStringLiteral("hasRightBorder"))
            && !headRoundness.contains(QStringLiteral("hasBottomBorder"))
            && background.contains(QStringLiteral(
                   "hasLeftBorder:hasAllBorders||"
                   "((solidBackground.enabledBorders"
                   "&KSvg.FrameSvg.LeftBorder)>0)"))
            && background.contains(QStringLiteral(
                   "shadows.left:hasLeftBorder&&root.behaveAsDockWithMask"))
            && bindings.contains(QStringLiteral(
                   "varpanelTail=externalBindings.dockBackground.tailRoundness"
                   "+externalBindings.dockBackground.tailRoundnessMargin;"))
            && bindings.contains(QStringLiteral(
                   "varpanelHead=externalBindings.dockBackground.headRoundness"
                   "+externalBindings.dockBackground.headRoundnessMargin;"));
    }

    static bool matchesBackgroundVisualThicknessRouting(const QString &source)
    {
        const QString current = normalizedCode(functionBody(
            source, QStringLiteral("totals.visualThickness:")));
        const QString maximum = normalizedCode(functionBody(
            source, QStringLiteral("totals.visualMaxThickness:")));

        return current.contains(QStringLiteral(
                   "constitemThickness=metrics.iconSize+2*metrics.margin.tailThickness;"))
            && current.contains(QStringLiteral(
                   "returnbackgroundStateResolver.visualThickness("
                   "totals.minThickness,itemThickness,sizeFraction);"))
            && maximum.contains(QStringLiteral(
                   "constitemThickness=metrics.maxIconSize+2*metrics.margin.maxTailThickness;"))
            && maximum.contains(QStringLiteral(
                   "returnbackgroundStateResolver.visualThickness("
                   "totals.minThickness,itemThickness,sizeFraction);"))
            && !current.contains(QStringLiteral("if(totals.minThickness<"))
            && !maximum.contains(QStringLiteral("if(totals.minThickness<"));
    }

    static bool matchesShadowIndependentAppletBudget(const QString &source)
    {
        const QString budget = normalizedCode(functionBody(
            source, QStringLiteral("readonly property int contentsMaxLength:")));
        return budget.contains(QStringLiteral(
                   "returnroot.maxLength-backgroundTotals.paddingsLength;"))
            && !budget.contains(QStringLiteral("shadowsLength"));
    }

    static bool matchesAspectIndependentBackgroundShadow(const QString &customBackground,
                                                         const QString &backgroundShadow,
                                                         const QString &multiLayered,
                                                         const QString &effectMetrics,
                                                         const QString &shadowedItem)
    {
        const QString custom = normalizedCode(customBackground);
        const QString effect = normalizedCode(backgroundShadow);
        const QString layered = normalizedCode(multiLayered);
        const QString metrics = normalizedCode(effectMetrics);
        const QString shadowed = normalizedCode(shadowedItem);
        const int backgroundShadowStart = custom.indexOf(QStringLiteral(
            "BackgroundShadow{id:backgroundShadow"));
        const int painterStart = custom.indexOf(QStringLiteral(
            "Rectangle{id:painter"));
        const QString backgroundShadowBlock = backgroundShadowStart >= 0
                && painterStart > backgroundShadowStart
            ? custom.mid(backgroundShadowStart, painterStart - backgroundShadowStart)
            : QString{};

        return !backgroundShadowBlock.isEmpty()
            && backgroundShadowBlock.contains(QStringLiteral(
                "visible:main.shadowEnabled&&main.shadowSize>0"))
            && backgroundShadowBlock.contains(QStringLiteral("blur:main.shadowSize"))
            && backgroundShadowBlock.contains(QStringLiteral("z:painter.z-1"))
            && backgroundShadowBlock.contains(QStringLiteral("color:main.shadowColor"))
            && !backgroundShadowBlock.contains(QStringLiteral("opacity:"))
            && !backgroundShadowBlock.contains(QStringLiteral("0.336"))
            && custom.contains(QStringLiteral("opacity:backgroundOpacity"))
            && custom.contains(QStringLiteral(
                "readonlypropertyaliasshadowPaintMargin:"
                "backgroundShadow.paintMargin"))
            && !custom.contains(QStringLiteral("layer.effect:BackgroundShadow{"))
            && !custom.contains(QStringLiteral("Kirigami.ShadowedRectangle"))
            && effect.contains(QStringLiteral(
                "importQtQuick.Effects"))
            && effect.contains(QStringLiteral("RectangularShadow{"))
            && effect.contains(QStringLiteral(
                "importorg.kde.latte.components1.0asLatteComponents"))
            && effect.contains(QStringLiteral(
                "readonlypropertyintpaintMargin:"
                "LatteComponents.EffectMetrics.rectangularShadowMarginFor("
                "blur,spread)"))
            && effect.contains(QStringLiteral("offset:Qt.vector2d(0,0)"))
            && effect.contains(QStringLiteral("spread:0"))
            && !effect.contains(QStringLiteral("LatteComponents.ShadowedItem"))
            && metrics.contains(QStringLiteral(
                "readonlypropertyintpostBlurGuardPx:2"))
            && metrics.contains(QStringLiteral(
                "functionshadowPaddingFor(sizePx:real,horizontalOffset:real,"
                "verticalOffset:real):int"))
            && metrics.contains(QStringLiteral(
                "functionrectangularShadowMarginFor(blurPx:real,spreadPx:real):int"))
            && shadowed.contains(QStringLiteral(
                "readonlypropertyintshadowPaddingPx:"
                "EffectMetrics.shadowPaddingFor(shadowSizePx,"
                "shadowHorizontalOffset,shadowVerticalOffset)"))
            && layered.contains(QStringLiteral(
                "readonlypropertyintcustomShadowPaintMargin:"
                "overlayedBackground.shadowPaintMargin"))
            && layered.count(QStringLiteral(
                "barLine.customShadowPaintMargin")) == 8
            && layered.contains(QStringLiteral(
                "shadowEnabled:customShadowIsEnabled"))
            && layered.contains(QStringLiteral(
                "shadowSize:Math.max(0,customShadow)"));
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

    static bool matchesReservationOutputAuthorityRoute(
        const QString &body)
    {
        const QString code = normalizedCode(body);
        return code.count(QStringLiteral(
                   "constauto*constreservationOutput="
                   "layerShell?layerShell->screen():nullptr;")) == 1
            && code.contains(QStringLiteral(
                "||!reservationOutput"
                "||!geometry.isValid()"
                "||!reservationOutput->geometry().contains(geometry)"))
            && !code.contains(QStringLiteral(
                "reservation->screen()"));
    }

    static bool matchesTransitionSnapshotRoute(
        const QString &body)
    {
        const QString code =
            normalizedCode(body);
        return code.contains(QStringLiteral(
                   "constauto*consttransition="
                   "view->floatingTransition();"))
            && code.contains(QStringLiteral(
                   "constauto*constwindowTouchTracker="
                   "view->windowTouchTracker();"))
            && code.contains(QStringLiteral(
                "record.floatingGapConfigured="
                "view->floatingGapConfigured();"))
            && code.contains(QStringLiteral(
                "record.floatingPanelConfigured="
                "view->isFloatingPanel();"))
            && code.contains(QStringLiteral(
                "record.floatingPanelEligible="
                "transition->floatingPanelEligible();"))
            && code.contains(QStringLiteral(
                "record.attachOnWindowTouchConfigured="
                "transition->attachOnWindowTouchConfigured();"))
            && code.contains(QStringLiteral(
                "record.attachmentWaitsForPointerExitConfigured="
                "transition->"
                "attachmentWaitsForPointerExitConfigured();"))
            && code.contains(QStringLiteral(
                "record.pointerInsideView="
                "transition->pointerInsideView();"))
            && code.contains(QStringLiteral(
                "record.attachmentDeferredByPointer="
                "transition->attachmentDeferredByPointer();"))
            && code.contains(QStringLiteral(
                "record.dockGapHideRequested="
                "transition->dockGapHideRequested();"))
            && code.contains(QStringLiteral(
                "constWindowTouchAuthorityCountswindowTouchCounts{"
                "transition->touchingWindowCount(),"
                "windowTouchTracker->touchingWindowCount(),};"))
            && code.contains(QStringLiteral(
                "constautotouchingWindowCount="
                "validateWindowTouchAuthorityCounts("
                "windowTouchCounts);"
                "if(!touchingWindowCount){qCritical()"
                "<<\"dbusreports:refusingdock-systemsnapshotwithdivergent"
                "\"\"window-touchauthoritiesforpersistentdock\""
                "<<record.persistentDockId"
                "<<\"runtimeview\"<<record.runtimeViewId"))
            && code.contains(QStringLiteral(
                "<<\"transitioncopy\""
                "<<windowTouchCounts.transitionCopy"
                "<<\"trackerauthority\""
                "<<windowTouchCounts.trackerAuthority;"
                "returnstd::nullopt;}"))
            && code.contains(QStringLiteral(
                "record.touchingWindowCount="
                "*touchingWindowCount;"))
            && !code.contains(QStringLiteral(
                "record.touchingWindowCount="
                "transition->touchingWindowCount();"))
            && code.contains(QStringLiteral(
                "record.transitionProgress="
                "transition->floatingness();"))
            && code.contains(QStringLiteral(
                "record.transitionGeometryRevision="
                "transition->geometryRevision();"))
            && code.contains(QStringLiteral(
                "if(windowTouchTracker->triggerGeometry().isValid()){"
                "record.stableTriggerGeometry="
                "windowTouchTracker->triggerGeometry();}"))
            && !code.contains(QStringLiteral(
                "record.stableTriggerGeometry="
                "transition->stableTriggerGeometry();"))
            && code.contains(QStringLiteral(
                "record.enabledBorders=enabledBorderNames("
                "view->effects()->enabledBorders());"))
            && code.contains(QStringLiteral(
                "if(constautoshadowState=PanelShadows::self()->"
                "shadowStateFor(view)){"
                "record.shadowEnabledBorders=enabledBorderNames("
                "shadowState->enabledBorders);"
                "record.shadowPaddingOffsets="
                "shadowState->extraPadding;}"))
            && code.contains(QStringLiteral(
                "record.floatingAnchorRevision="
                "view->effects()->floatingAnchorRevision();"))
            && code.contains(QStringLiteral(
                "record.floatingAppletPopupsPreferred="
                "view->containment()->containmentDisplayHints().testFlag("
                "Plasma::Types::ContainmentPrefersFloatingApplets);"))
            && code.contains(QStringLiteral(
                "record.currentVisibleGeometry="
                "transition->currentVisibleGeometry();"))
            && code.contains(QStringLiteral(
                "record.computedPaintMaskGeometry="
                "transition->currentVisibleGeometry();"))
            && code.contains(QStringLiteral(
                "record.computedInputBridgeGeometry="
                "transition->fittsBridgeGeometry();"))
            && code.contains(QStringLiteral(
                "record.stableLayerShellMargin="
                "physicalLayerShellMarginAtEdge("
                "record.layerShellMargins,record.edge);"))
            && code.contains(QStringLiteral(
                "record.surfaceGeometryPublicationRevision="
                "view->positioner()->"
                "surfaceGeometryPublicationRevision();"))
            && code.contains(QStringLiteral(
                "record.layerShellConfigureRequestRevision="
                "view->layerShellConfigureRequestRevision();"))
            && code.contains(QStringLiteral(
                "record.objects.transitionController="
                "identities->tokenFor(transition);"))
            && code.contains(QStringLiteral(
                "record.objects.windowTouchTracker="
                "identities->tokenFor(windowTouchTracker);"))
            && code.contains(QStringLiteral(
                "if(!dockTransitionRecordsAgree(snapshot)){"
                "for(constauto&view:snapshot.views){"
                "qCritical()<<\"dbusreports:transitiondisagreement"
                "fordock\""))
            && code.contains(QStringLiteral(
                "<<\"record\"<<serializeDockSystemViewRecord("
                "view,globalConfigureAppletsMode);}"
                "returnstd::nullopt;}"))
            && !code.contains(QStringLiteral(
                "currentVisibleGeometry().toRect()"))
            && !code.contains(QStringLiteral(
                "fittsBridgeGeometry().toRect()"));
    }

    static bool matchesReservationPublicationCommitRoute(
        const QString &visibilityHeader,
        const QString &visibilitySource)
    {
        const QString header = normalizedCode(
            visibilityHeader);
        const QString update = normalizedCode(
            functionBody(
                visibilitySource,
                QStringLiteral(
                    "bool VisibilityManager::updateStrutsBasedOnLayoutsAndActivities")));
        const QString setMode = normalizedCode(
            functionBody(
                visibilitySource,
                QStringLiteral(
                    "void VisibilityManager::setMode")));
        const int modeCommit = setMode.indexOf(
            QStringLiteral("m_mode=mode;"));
        const int retirement = setMode.indexOf(
            QStringLiteral(
                "m_reservationPublication.remove("),
            modeCommit);

        return header.contains(QStringLiteral(
                   "ScreenSpaceReservationPublicationState"
                   "m_reservationPublication;"))
            && !visibilitySource.contains(QStringLiteral(
                "m_publishedStruts"))
            && update.contains(QStringLiteral(
                "if(m_mode==Types::AlwaysVisible"
                "&&m_strutsThickness>0"))
            && update.contains(QStringLiteral(
                "constScreenSpaceReservationPublicationTarget"
                "candidate{*computedStruts,outputId,"
                "m_latteView->location()};"))
            && update.contains(QStringLiteral(
                "m_reservationPublication.update("
                "candidate,forceUpdate,"
                "[this](constScreenSpaceReservationPublicationTarget"
                "&target){"
                "returnm_wm->setViewStruts("
                "*m_latteView,target.struts,"
                "target.edge);})"))
            && update.count(QStringLiteral(
                "m_timerBlockStrutsUpdate.start();")) >= 2
            && modeCommit != -1
            && retirement > modeCommit
            && setMode.contains(QStringLiteral(
                "if(mustRetireReservation"
                "&&!m_reservationPublication.remove("))
            && setMode.count(QStringLiteral(
                "m_timerBlockStrutsUpdate.start();")) == 1;
    }

    static bool matchesReservationMemberOutputAuthorityRoute(
        const QString &coordinatorSource,
        const QString &visibilitySource)
    {
        const QString coordinator = normalizedCode(
            coordinatorSource);
        const QString visibilityUpdate = normalizedCode(
            functionBody(
                visibilitySource,
                QStringLiteral(
                    "bool VisibilityManager::updateStrutsBasedOnLayoutsAndActivities")));
        const QString outputResolver = normalizedCode(
            functionBody(
                coordinatorSource,
                QStringLiteral(
                    "QScreen *reservationOutputForView")));

        return outputResolver.contains(QStringLiteral(
                   "constauto*constlayerShell="
                   "view.layerShellWindow();"
                   "returnlayerShell?layerShell->screen():nullptr;"))
            && coordinator.count(QStringLiteral(
                "reservationOutputForView(")) == 3
            && !coordinator.contains(QStringLiteral(
                "view.screen()"))
            && !coordinator.contains(QStringLiteral(
                "runtime->second.view->screen()"))
            && visibilityUpdate.contains(QStringLiteral(
                "constauto*constlayerShell="
                "m_latteView->layerShellWindow();"
                "QScreen*constscreen="
                "layerShell?layerShell->screen():nullptr;"))
            && !visibilityUpdate.contains(QStringLiteral(
                "m_latteView->screen()"));
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

    static bool matchesThemeAwareIconTestLifecycle(const QString &testSource,
                                                   const QString &cmakeSource)
    {
        const QString testCode = normalizedCode(testSource);
        const QString initCode = normalizedCode(functionBody(
            testSource,
            QStringLiteral("void ThemeAwareIconTest::initTestCase()")));
        const QString prepareCode = normalizedCode(functionBody(
            testSource,
            QStringLiteral("void ThemeAwareIconTest::prepareView(")));
        const QString mainCode = normalizedCode(functionBody(
            testSource,
            QStringLiteral("int main(")));

        const QRegularExpression anyViewConstruction(QStringLiteral(
            "QQuickView[A-Za-z_][A-Za-z0-9_]*(?:\\([^;{}]*\\))?;"));
        const QRegularExpression sharedViewConstruction(QStringLiteral(
            "QQuickView[A-Za-z_][A-Za-z0-9_]*"
            "\\(m_engine\\.get\\(\\),nullptr\\);"));
        const int viewConstructions =
            testCode.count(anyViewConstruction);
        const int sharedViewConstructions =
            testCode.count(sharedViewConstruction);

        const QString directRenderLoop = QStringLiteral(
            "qputenv(\"QSG_RENDER_LOOP\",\"basic\");");
        const int directRenderLoopPosition = mainCode.indexOf(directRenderLoop);
        const int applicationPosition =
            mainCode.indexOf(QStringLiteral("QGuiApplicationapp("));

        const QString targetStart =
            QStringLiteral("ecm_add_test(themeawareicontest.cpp");
        const int targetStartPosition = cmakeSource.indexOf(targetStart);
        const int nextTargetPosition = targetStartPosition == -1
            ? -1
            : cmakeSource.indexOf(
                QStringLiteral("\necm_add_test("),
                targetStartPosition + targetStart.size());
        const QString targetBlock = targetStartPosition == -1
            ? QString{}
            : cmakeSource.mid(
                targetStartPosition,
                nextTargetPosition == -1
                    ? -1
                    : nextTargetPosition - targetStartPosition);
        const QString targetCode = normalizedCode(targetBlock);
        const QString ctestRenderLoop = QStringLiteral(
            "set_tests_properties(themeawareicontestPROPERTIES"
            "ENVIRONMENT\"QT_QPA_PLATFORM=offscreen;"
            "QSG_RHI_BACKEND=software;QSG_RENDER_LOOP=basic\")");

        return testCode.contains(QStringLiteral(
                   "std::unique_ptr<QQmlEngine>m_engine;"))
            && initCode.contains(QStringLiteral(
                "m_engine=std::make_unique<QQmlEngine>();"))
            && prepareCode.contains(QStringLiteral(
                "QCOMPARE(view.engine(),m_engine.get());"))
            && viewConstructions >= 3
            && sharedViewConstructions == viewConstructions
            && mainCode.count(directRenderLoop) == 1
            && directRenderLoopPosition >= 0
            && applicationPosition > directRenderLoopPosition
            && targetCode.count(ctestRenderLoop) == 1;
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

    static bool matchesPanelToDockInputHandoff(
        const QString &visibilityManager)
    {
        const QString source =
            normalizedCode(visibilityManager);
        return source.contains(QStringLiteral(
            "functiononBehaveAsPlasmaPanelChanged(){"
            "manager.updateMaskArea();"
            "manager.updateInputGeometry();}"));
    }

    static bool matchesStableDockOccupancyContract(
        const QString &visibilityManager,
        const QString &bridgeHeader,
        const QString &viewSource)
    {
        const QString visibility = normalizedCode(visibilityManager);
        const QString bridge = normalizedCode(bridgeHeader);
        const QString view = normalizedCode(viewSource);
        const QString updateMask = normalizedCode(functionBody(
            visibilityManager, QStringLiteral("function updateMaskArea()")));
        const QString updateInput = normalizedCode(functionBody(
            visibilityManager,
            QStringLiteral("function updateInputGeometry()")));

        return bridge.contains(QStringLiteral(
                   "Q_INVOKABLEQRectstableJustifyDockOccupancyFor("))
            && visibility.contains(QStringLiteral(
                   "target:rootfunctiononAutomaticSizingMaximumLengthChanged(){"
                   "manager.updateMaskArea();}"))
            && view.contains(QStringLiteral(
                   "connect(this,&View::maxLengthChanged,this,"
                   "&View::updateWindowTouchTriggerGeometry);"))
            && updateMask.contains(QStringLiteral(
                   "constpresentedLocalGeometry="
                   "manager.presentedLocalGeometry();"))
            && updateMask.contains(QStringLiteral(
                   "if(!root.dockFloatingTransitionOwnsGap){"
                   "latteView.localGeometry=presentedLocalGeometry;"
                   "}elseif(root.myView.alignment==="
                   "LatteCore.Types.Justify){"
                   "latteView.localGeometry="
                   "maskGeometry.stableJustifyDockOccupancyFor("))
            && updateMask.contains(QStringLiteral(
                   "root.automaticSizingMaximumLength,"))
            && updateMask.contains(QStringLiteral(
                   "metrics.configuredScreenEdgeMargin);"
                   "}else{latteView.localGeometry="
                   "maskGeometry.localGeometryFor("
                   "Plasmoid.location,false,"))
            && updateMask.contains(QStringLiteral(
                   "latteView.effects.rect,"
                   "metrics.totals.thickness,"
                   "metrics.configuredScreenEdgeMargin);"))
            && updateInput.contains(QStringLiteral(
                   "constinputLengthGeometry="
                   "manager.window.visibility.isHidden"
                   "||manager.window.visibility.isSidebar"
                   "?manager.window.localGeometry"
                   ":manager.presentedLocalGeometry();"))
            && updateInput.contains(QStringLiteral(
                   "metrics.mask.screenEdge,"
                   "inputLengthGeometry,"));
    }

private Q_SLOTS:
    void visibilityManager_updateSidebarState_assignsState();
    void layoutsController_modeIsChanged_delegatesToModel();
    void windowsTrackerBinding_keepsRequesters();
    void waylandWindowSignals_keepDeliveryPolicy();
    void visibilityManager_strutThicknessBypassesGeometryThrottle();
    void occupiedGeometryChange_notifiesPerpendicularPeers();
    void viewsDataConfigureMode_keepsPerViewContract();
    void viewsDataConfigureMode_sourceGuardRejectsGlobalLeak();
    void layoutLengthChanges_shareAnimationTrackerRegistration();
    void layoutLengthChanges_sourceGuardRejectsVerticalRemoval();
    void centeredAppletOffset_ignoresBoundedBackgroundMovement();
    void centeredAppletOffset_sourceGuardRejectsVisualFeedback();
    void justifyAppletSpan_followsSolidBackground();
    void justifyAppletSpan_sourceGuardRejectsShadowOverlap();
    void stableFloatingPanelQml_keepsOneTransitionAuthority();
    void stableFloatingPanelQml_rejectsDivergentZeroGapEligibility();
    void stableFloatingPanelQml_rejectsDroppedDockTransitionOwnership();
    void stableDockAutomaticSizing_rejectsPresentedLengthFeedback();
    void stableDockOccupancy_separatesPresentationFromReservations();
    void stableDockOccupancy_rejectsPresentationFeedback();
    void stablePanelPopupAnchor_rejectsLegacyAnimationFreeze();
    void stableFloatingPanelE2e_keepsCanvasAndRevisionsFixed();
    void windowTouchAuthority_keepsDedicatedStableModel();
    void windowTouchAuthority_rejectsControlledMutations();
    void windowTouchE2e_drivesOneStableTriggerClient();
    void liveTitlebarWindowTouchE2e_coversPanelsAndDocksBeforeRelease();
    void liveTitlebarWindowTouchE2e_rejectsMissingHeldProof();
    void windowTouchTopologyE2e_keepsIndependentRegionsAndOutputs();
    void windowTouchTopologyE2e_cleanupGuardRejectsControlledMutations();
    void linkedOperationStormE2e_keepsTransactionalReplayContract();
    void linkedOperationStormE2e_sourceGuardRejectsControlledMutations();
    void linkedOperationStormE2e_cleanupPreservesFailureAndSafety();
    void multiOutputRestore_keepsCompleteSemanticStateContract();
    void multiOutputRestore_sourceGuardRejectsProjectionOnlyVerification();
    void multiOutputRestore_sourceGuardRejectsMissingPrioritySetter();
    void floatingPresentationConsumers_keepSingleAuthority();
    void panelToDockInputHandoff_bypassesOrdinaryAnimationGate();
    void panelToDockInputHandoff_rejectsMissingDirectWrite();
    void dockBackgroundFit_includesJustifyDockMode();
    void dockBackgroundFit_sourceGuardsRejectBypasses();
    void appletBudget_excludesInternalPaddingButNotShadows();
    void appletBudget_sourceGuardRejectsShadowSubtraction();
    void floatingPanelLayoutClearance_keepsStablePrimarySpan();
    void floatingPanelLayoutClearance_sourceGuardsRejectDivergence();
    void backgroundVisualThickness_usesMonotonicCore();
    void backgroundVisualThickness_sourceGuardRejectsDivergence();
    void dockBackgroundShadow_keepsFixedPixelFootprint();
    void dockBackgroundShadow_sourceGuardsRejectAspectScaledRenderer();
    void iconResizeAnimation_keepsSingleAuthority();
    void themeAwareIconRenderTest_keepsLifecycleContract();
    void themeAwareIconRenderTest_sourceGuardRejectsControlledMutations();
    void dockSystemCollection_keepsPureRouting();
    void dockSystemCollection_sourceGuardsRejectControlledMutations();
    void dockSystemTransitionCollection_keepsAuthoritativeRouting();
    void dockSystemTransitionCollection_rejectsControlledMutations();
    void reservationPublication_keepsFailureAtomicRoute();
    void reservationPublication_usesLayerShellOutputIdentity();
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

void SourceGuardTest::occupiedGeometryChange_notifiesPerpendicularPeers()
{
    const QString body = stripped(functionBody(
        readFile(QStringLiteral("app/view/view.cpp")),
        QStringLiteral("void View::updateAbsoluteGeometry")));
    QVERIFY2(!body.isEmpty(), "View::updateAbsoluteGeometry not found");

    const QString changedAssignment = QStringLiteral(
        "constboolgeometryChanged=m_absoluteGeometry!=absGeometry;");
    const int decision = body.indexOf(changedAssignment);
    const int stateWrite = body.indexOf(QStringLiteral("m_absoluteGeometry=absGeometry;"));
    const int peerNotification = body.indexOf(QStringLiteral(
        "if(geometryChanged||bypassChecks){"));
    const int rectNotification = body.indexOf(QStringLiteral(
        "Q_EMITavailableScreenRectChangedFrom(this);"), peerNotification);
    const int regionNotification = body.indexOf(QStringLiteral(
        "Q_EMITavailableScreenRegionChangedFrom(this);"), rectNotification);

    QVERIFY2(decision >= 0 && decision < stateWrite,
             "the geometry transition must be captured before writing the new rectangle");
    QVERIFY2(peerNotification > stateWrite
             && rectNotification > peerNotification
             && regionNotification > rectNotification,
             "a changed occupied rectangle must notify perpendicular peer solvers");
    QVERIFY2(!body.contains(QStringLiteral("if((m_absoluteGeometry!=absGeometry)||bypassChecks)")),
             "comparing after assignment suppresses every ordinary peer notification");
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

void SourceGuardTest::justifyAppletSpan_followsSolidBackground()
{
    const QString layoutSource = readFile(QStringLiteral(
        "containment/package/contents/ui/layouts/LayoutsContainer.qml"));
    const QString mainSource = readFile(QStringLiteral(
        "containment/package/contents/ui/main.qml"));
    QVERIFY2(matchesJustifyLayoutSolidSpanOwnership(layoutSource, mainSource),
             "Justify applets must occupy the fitted solid background span");
}

void SourceGuardTest::justifyAppletSpan_sourceGuardRejectsShadowOverlap()
{
    const QString originalLayout = readFile(QStringLiteral(
        "containment/package/contents/ui/layouts/LayoutsContainer.qml"));
    const QString originalMain = readFile(QStringLiteral(
        "containment/package/contents/ui/main.qml"));
    QVERIFY(matchesJustifyLayoutSolidSpanOwnership(originalLayout, originalMain));

    QString outerVisualLengthRestored = originalLayout;
    const QString solidWidth = QStringLiteral(
        "? justifyLayoutLength : parent.width");
    QCOMPARE(outerVisualLengthRestored.count(solidWidth), 1);
    outerVisualLengthRestored.replace(solidWidth,
                                      QStringLiteral("? root.maxLength : parent.width"));
    QVERIFY2(!matchesJustifyLayoutSolidSpanOwnership(outerVisualLengthRestored,
                                                     originalMain),
             "restoring the outer visual width must fail the Justify ownership guard");

    QString outerVisualOriginRestored = originalLayout;
    const QString solidOrigin = QStringLiteral(
        "readonly property real justifyLayoutOrigin: (justifyOwningCanvasLength\n"
        "                                                 - justifyLayoutLength) / 2");
    QCOMPARE(outerVisualOriginRestored.count(solidOrigin), 1);
    outerVisualOriginRestored.replace(
        solidOrigin,
        QStringLiteral(
            "readonly property real justifyLayoutOrigin:\n"
            "        (justifyOwningCanvasLength - background.totals.visualLength) / 2\n"
            "        + backgroundShadowTailLength"));
    QVERIFY2(!matchesJustifyLayoutSolidSpanOwnership(outerVisualOriginRestored,
                                                     originalMain),
             "making the applet origin depend on shadows must fail the guard");

    QString shadowBudgetRestored = originalLayout;
    const QString stableLength = QStringLiteral(
        "readonly property real justifyLayoutLength: background.length");
    QCOMPARE(shadowBudgetRestored.count(stableLength), 1);
    shadowBudgetRestored.replace(
        stableLength,
        QStringLiteral(
            "readonly property real justifyLayoutLength: Math.max(\n"
            "        0, background.totals.visualLength\n"
            "        - backgroundShadowTailLength - backgroundShadowHeadLength)"));
    QVERIFY2(!matchesJustifyLayoutSolidSpanOwnership(shadowBudgetRestored,
                                                     originalMain),
             "deriving stable layout length from shadow chrome must fail the guard");

    QString layoutOwnedCanvas = originalMain;
    const QString independentCanvas = QStringLiteral(
        "            x: root.behaveAsPlasmaPanel\n"
        "               ? layoutsContainer.x : (root.isHorizontal ? 0 : layoutsContainer.x)\n"
        "            y: root.behaveAsPlasmaPanel\n"
        "               ? layoutsContainer.y : (root.isVertical ? 0 : layoutsContainer.y)\n"
        "            width: root.behaveAsPlasmaPanel\n"
        "                   ? layoutsContainer.width\n"
        "                   : (root.isHorizontal ? parent.width : layoutsContainer.width)\n"
        "            height: root.behaveAsPlasmaPanel\n"
        "                    ? layoutsContainer.height\n"
        "                    : (root.isVertical ? parent.height : layoutsContainer.height)");
    QCOMPARE(layoutOwnedCanvas.count(independentCanvas), 1);
    layoutOwnedCanvas.replace(independentCanvas,
                              QStringLiteral("            anchors.fill: layoutsContainer"));
    QVERIFY2(!matchesJustifyLayoutSolidSpanOwnership(originalLayout,
                                                     layoutOwnedCanvas),
             "making the background canvas depend on the applet span must fail the guard");
}

void SourceGuardTest::stableFloatingPanelQml_keepsOneTransitionAuthority()
{
    const QString main = readFile(QStringLiteral(
        "containment/package/contents/ui/main.qml"));
    const QString bindings = readFile(QStringLiteral(
        "containment/package/contents/ui/BindingsExternal.qml"));
    const QString visibility = readFile(QStringLiteral(
        "containment/package/contents/ui/VisibilityManager.qml"));
    const QString layouts = readFile(QStringLiteral(
        "containment/package/contents/ui/layouts/LayoutsContainer.qml"));
    const QString metrics = readFile(QStringLiteral(
        "containment/package/contents/ui/abilities/Metrics.qml"));
    const QString backgroundTotals = readFile(QStringLiteral(
        "containment/package/contents/ui/background/types/Totals.qml"));
    const QString viewHeader = readFile(QStringLiteral(
        "app/view/view.h"));
    const QString viewImplementation = readFile(QStringLiteral(
        "app/view/view.cpp"));

    QVERIFY2(matchesStableFloatingPanelQmlContract(
                 main, bindings, visibility, layouts, metrics, backgroundTotals,
                 viewHeader, viewImplementation),
             "floating panels must animate only controller progress inside one"
             " stable window, reservation, and applet-measurement envelope");
}

void SourceGuardTest::stableFloatingPanelQml_rejectsDivergentZeroGapEligibility()
{
    QString main = readFile(QStringLiteral(
        "containment/package/contents/ui/main.qml"));
    const QString bindings = readFile(QStringLiteral(
        "containment/package/contents/ui/BindingsExternal.qml"));
    const QString visibility = readFile(QStringLiteral(
        "containment/package/contents/ui/VisibilityManager.qml"));
    const QString layouts = readFile(QStringLiteral(
        "containment/package/contents/ui/layouts/LayoutsContainer.qml"));
    const QString metrics = readFile(QStringLiteral(
        "containment/package/contents/ui/abilities/Metrics.qml"));
    const QString backgroundTotals = readFile(QStringLiteral(
        "containment/package/contents/ui/background/types/Totals.qml"));
    const QString viewHeader = readFile(QStringLiteral(
        "app/view/view.h"));
    const QString viewImplementation = readFile(QStringLiteral(
        "app/view/view.cpp"));

    const QString eligibility = QStringLiteral(
        "readonly property bool floatingTransitionEligible: latteView\n"
        "                                                       "
        "&& root.behaveAsPlasmaPanel\n"
        "                                                       "
        "&& latteView.floatingPanelConfigured");
    QCOMPARE(main.count(eligibility), 1);
    main.replace(eligibility,
                 QStringLiteral(
                     "readonly property bool floatingTransitionEligible: "
                     "latteView\n"
                     "                                                       "
                     "&& screenEdgeMarginEnabled\n"
                     "                                                       "
                     "&& latteView.floatingPanelConfigured"));

    QVERIFY2(!matchesStableFloatingPanelQmlContract(
                 main, bindings, visibility, layouts, metrics, backgroundTotals,
                 viewHeader, viewImplementation),
             "QML eligibility must not diverge from the C++ positive-gap "
             "configuration authority");

    QString dockMain = readFile(QStringLiteral(
        "containment/package/contents/ui/main.qml"));
    const QString dockGapAuthority = QStringLiteral(
        "        && latteView.floatingGapConfigured\n"
        "        && !latteView.floatingPanelConfigured");
    QCOMPARE(dockMain.count(dockGapAuthority), 1);
    dockMain.replace(
        dockGapAuthority,
        QStringLiteral(
            "        && latteView.floatingPanelConfigured\n"
            "        && !latteView.floatingPanelConfigured"));
    QVERIFY2(!matchesStableFloatingPanelQmlContract(
                 dockMain, bindings, visibility, layouts, metrics,
                 backgroundTotals, viewHeader, viewImplementation),
             "Dock gap hiding must use the view's presentation-independent"
             " positive-gap authority, never Panel identity");

    QString dockVisibilityMain = readFile(QStringLiteral(
        "containment/package/contents/ui/main.qml"));
    const QString dockVisibilityModes = QStringLiteral(
        "        && (latteView.visibility.mode === "
        "LatteCore.Types.AlwaysVisible\n"
        "            || latteView.visibility.mode === "
        "LatteCore.Types.WindowsGoBelow)");
    QCOMPARE(
        dockVisibilityMain.count(
            dockVisibilityModes),
        1);
    dockVisibilityMain.replace(
        dockVisibilityModes,
        QStringLiteral(
            "        && latteView.visibility.mode === "
            "LatteCore.Types.AlwaysVisible"));
    QVERIFY2(!matchesStableFloatingPanelQmlContract(
                 dockVisibilityMain, bindings, visibility, layouts, metrics,
                 backgroundTotals, viewHeader, viewImplementation),
             "the legacy Dock request must cover both visibility modes that"
             " consume hideThickScreenGap");

    QString gapImplementation = viewImplementation;
    const QString positiveGapDefinition = QStringLiteral(
        "    return m_screenEdgeMarginEnabled\n"
        "        && m_screenEdgeMargin > 0;");
    QCOMPARE(gapImplementation.count(positiveGapDefinition), 1);
    gapImplementation.replace(
        positiveGapDefinition,
        QStringLiteral("    return false;"));
    QVERIFY2(!matchesStableFloatingPanelQmlContract(
                 readFile(QStringLiteral(
                     "containment/package/contents/ui/main.qml")),
                 bindings, visibility, layouts, metrics, backgroundTotals,
                 viewHeader, gapImplementation),
             "the shared floating-gap predicate must retain its exact"
             " positive-margin semantics");

    QString panelImplementation = viewImplementation;
    const QString panelIdentity = QStringLiteral(
        "    return m_behaveAsPlasmaPanel\n"
        "        && floatingGapConfigured();");
    QCOMPARE(panelImplementation.count(panelIdentity), 1);
    panelImplementation.replace(
        panelIdentity,
        QStringLiteral("    return floatingGapConfigured();"));
    QVERIFY2(!matchesStableFloatingPanelQmlContract(
                 readFile(QStringLiteral(
                     "containment/package/contents/ui/main.qml")),
                 bindings, visibility, layouts, metrics, backgroundTotals,
                 viewHeader, panelImplementation),
             "floating Panel identity must remain the conjunction of Panel"
             " behavior and the shared positive-gap predicate");
}

void SourceGuardTest::stableFloatingPanelQml_rejectsDroppedDockTransitionOwnership()
{
    const QString originalMain = readFile(QStringLiteral(
        "containment/package/contents/ui/main.qml"));
    const QString bindings = readFile(QStringLiteral(
        "containment/package/contents/ui/BindingsExternal.qml"));
    const QString visibility = readFile(QStringLiteral(
        "containment/package/contents/ui/VisibilityManager.qml"));
    const QString layouts = readFile(QStringLiteral(
        "containment/package/contents/ui/layouts/LayoutsContainer.qml"));
    const QString metrics = readFile(QStringLiteral(
        "containment/package/contents/ui/abilities/Metrics.qml"));
    const QString backgroundTotals = readFile(QStringLiteral(
        "containment/package/contents/ui/background/types/Totals.qml"));
    const QString viewHeader = readFile(QStringLiteral(
        "app/view/view.h"));
    const QString originalViewImplementation = readFile(QStringLiteral(
        "app/view/view.cpp"));

    const auto matches =
        [&](const QString &main, const QString &viewImplementation) {
            return matchesStableFloatingPanelQmlContract(
                main, bindings, visibility, layouts, metrics,
                backgroundTotals, viewHeader, viewImplementation);
        };

    struct OwnershipMutation {
        QString source;
        QString replacement;
    };
    const QList<OwnershipMutation> retainedOwnershipMutations{
        {
            QStringLiteral(
                "        const currentTouchingWindowCount =\n"
                "            latteView.windowTouchTracker."
                "touchingWindowCount;\n"),
            QStringLiteral(
                "        const currentTouchingWindowCount = 0;\n"),
        },
        {
            QStringLiteral(
                "        const currentDockGapHideRequested =\n"
                "            directDockWindowTouchEligible\n"
                "            && currentTouchingWindowCount > 0;\n"),
            QStringLiteral(
                "        const currentDockGapHideRequested =\n"
                "            dockGapHideRequested;\n"),
        },
        {
            QStringLiteral(
                "        && (directDockWindowTouchEligible\n"),
            QStringLiteral(
                "        && (false\n"),
        },
        {
            QStringLiteral(
                "            || latteView.floatingTransition.running\n"),
            QString{},
        },
        {
            QStringLiteral(
                "            || floatingPresentationDisplaced)\n"),
            QStringLiteral(
                "            || false)\n"),
        },
        {
            QStringLiteral(
                "            root.floatingPresentationProgress,\n"),
            QStringLiteral(
                "            1.0,\n"),
        },
        {
            QStringLiteral(
                "            root.dockFloatingTransitionOwnsGap\n"
                "                && root.maximizeWhenMaximized)\n"),
            QStringLiteral(
                "            root.dockFloatingTransitionOwnsGap\n"
                "                && true)\n"),
        },
        {
            QStringLiteral(
                "          || (!dockFloatingTransitionOwnsGap\n"
                "              && maximizeWhenMaximized\n"),
            QStringLiteral(
                "          || (maximizeWhenMaximized\n"),
        },
        {
            QStringLiteral(
                "                 && !root.dockFloatingTransitionOwnsGap\n"
                "                 && Plasmoid.configuration."
                "floatingGapHidingWaitsMouse\n"),
            QStringLiteral(
                "                 && Plasmoid.configuration."
                "floatingGapHidingWaitsMouse\n"),
        },
        {
            QStringLiteral(
                "        return root.isHorizontal\n"
                "            ? root.width * "
                "(Plasmoid.configuration.maxLength / 100)\n"
                "            : root.height * "
                "(Plasmoid.configuration.maxLength / 100);\n"),
            QStringLiteral(
                "        return root.maxLength;\n"),
        },
    };
    for (const auto &mutation : retainedOwnershipMutations) {
        QString mutatedMain = originalMain;
        QCOMPARE(
            mutatedMain.count(
                mutation.source),
            1);
        mutatedMain.replace(
            mutation.source,
            mutation.replacement);
        QVERIFY2(
            !matches(
                mutatedMain,
                originalViewImplementation),
            qPrintable(
                QStringLiteral(
                    "disabling Dock transition ownership arm '%1' must fail")
                    .arg(
                        mutation.source.trimmed())));
    }

    const QList<QString> ownershipAwareCallbacks{
        QStringLiteral(
            "                m_effects->"
            "applyFloatingPresentationProgress();"),
        QStringLiteral(
            "                    &ViewPart::Effects::"
            "applyFloatingPresentationProgress);"),
    };
    for (const QString &callback : ownershipAwareCallbacks) {
        QString mutatedViewImplementation =
            originalViewImplementation;
        QString destructiveCallback = callback;
        destructiveCallback.replace(
            QStringLiteral(
                "applyFloatingPresentationProgress"),
            QStringLiteral(
                "applyFloatingPanelPresentation"));
        QCOMPARE(
            mutatedViewImplementation.count(
                callback),
            1);
        mutatedViewImplementation.replace(
            callback,
            destructiveCallback);
        QVERIFY2(
            !matches(
                originalMain,
                mutatedViewImplementation),
            "ordinary visibility changes must not invoke the destructive"
            " Panel-to-Dock ownership handoff");
    }
}

void SourceGuardTest::
    stableDockAutomaticSizing_rejectsPresentedLengthFeedback()
{
    const QString originalAutoSize = readFile(QStringLiteral(
        "containment/package/contents/ui/abilities/AutoSize.qml"));
    const QString originalLayouter = readFile(QStringLiteral(
        "containment/package/contents/ui/abilities/privates/"
        "LayouterPrivate.qml"));
    const QString originalMain = readFile(QStringLiteral(
        "containment/package/contents/ui/main.qml"));
    QVERIFY(matchesStableDockAutomaticSizingContract(
        originalAutoSize, originalLayouter, originalMain));

    QString eventLeak = originalAutoSize;
    const QString stableEvent = QStringLiteral(
        "function onAutomaticSizingMaximumLengthChanged()");
    QCOMPARE(eventLeak.count(stableEvent), 1);
    eventLeak.replace(stableEvent,
                      QStringLiteral("function onMaxLengthChanged()"));
    QVERIFY2(!matchesStableDockAutomaticSizingContract(
                 eventLeak, originalLayouter, originalMain),
             "presentation frames must not trigger an automatic-size pass");

    QString budgetLeak = originalAutoSize;
    const QString stableBudget = QStringLiteral(
        "sizer.layouter.automaticSizingContentsMaxLength");
    QCOMPARE(budgetLeak.count(stableBudget), 1);
    budgetLeak.replace(stableBudget,
                       QStringLiteral("sizer.layouter.contentsMaxLength"));
    QVERIFY2(!matchesStableDockAutomaticSizingContract(
                 budgetLeak, originalLayouter, originalMain),
             "automatic sizing must not consume the presented Dock length");

    QString layouterLeak = originalLayouter;
    const QString stableMaximum = QStringLiteral(
        "const availableLength = root.automaticSizingMaximumLength\n"
        "            - backgroundTotals.paddingsLength;");
    QCOMPARE(layouterLeak.count(stableMaximum), 1);
    layouterLeak.replace(
        stableMaximum,
        QStringLiteral(
            "const availableLength = root.maxLength\n"
            "            - backgroundTotals.paddingsLength;"));
    QVERIFY2(!matchesStableDockAutomaticSizingContract(
                 originalAutoSize, layouterLeak, originalMain),
             "the resting fit budget must not alias live presentation state");

    const QString stableDiagnostic = QStringLiteral(
        "        availablePrimaryLength: "
        "_layouter.automaticSizingContentsMaxLength\n");
    QCOMPARE(originalMain.count(stableDiagnostic), 1);
    QString diagnosticLeak = originalMain;
    diagnosticLeak.replace(
        stableDiagnostic,
        QStringLiteral(
            "        availablePrimaryLength: "
            "_layouter.contentsMaxLength\n"));
    QVERIFY2(!matchesStableDockAutomaticSizingContract(
                 originalAutoSize, originalLayouter, diagnosticLeak),
             "the per-view available-length readback must not consume the"
             " animated presentation budget");
}

void SourceGuardTest::
    stableDockOccupancy_separatesPresentationFromReservations()
{
    QVERIFY2(matchesStableDockOccupancyContract(
                 readFile(QStringLiteral(
                     "containment/package/contents/ui/VisibilityManager.qml")),
                 readFile(QStringLiteral(
                     "containment/plugin/maskgeometrybridge.h")),
                 readFile(QStringLiteral("app/view/view.cpp"))),
             "a live Dock must retain its configured gap, a Justify Dock must"
             " also retain its resting length, configured length changes must"
             " refresh occupancy and touch authority directly, and input must"
             " follow the animated presentation");
}

void SourceGuardTest::
    stableDockOccupancy_rejectsPresentationFeedback()
{
    const QString bridge = readFile(QStringLiteral(
        "containment/plugin/maskgeometrybridge.h"));
    const QString viewSource = readFile(QStringLiteral("app/view/view.cpp"));
    QString visibility = readFile(QStringLiteral(
        "containment/package/contents/ui/VisibilityManager.qml"));
    QVERIFY(matchesStableDockOccupancyContract(
        visibility, bridge, viewSource));

    const QString selectedInput = QStringLiteral(
        "                                                                "
        "inputLengthGeometry,\n");
    QCOMPARE(visibility.count(selectedInput), 1);
    visibility.replace(
        selectedInput,
        QStringLiteral(
            "                                                                "
            "manager.presentedLocalGeometry(),\n"));
    QVERIFY2(!matchesStableDockOccupancyContract(
                 visibility, bridge, viewSource),
             "hidden input must not collapse onto the 1x1 effects sentinel");

    visibility = readFile(QStringLiteral(
        "containment/package/contents/ui/VisibilityManager.qml"));
    const QString hiddenSelection = QStringLiteral(
        "        const inputLengthGeometry = manager.window.visibility.isHidden\n"
        "                || manager.window.visibility.isSidebar\n"
        "            ? manager.window.localGeometry\n"
        "            : manager.presentedLocalGeometry();\n");
    QCOMPARE(visibility.count(hiddenSelection), 1);
    visibility.replace(
        hiddenSelection,
        QStringLiteral(
            "        const inputLengthGeometry = latteView.localGeometry;\n"));
    QVERIFY2(!matchesStableDockOccupancyContract(
                 visibility, bridge, viewSource),
             "visible input must follow animated paint instead of stable"
             " occupancy");

    visibility = readFile(QStringLiteral(
        "containment/package/contents/ui/VisibilityManager.qml"));
    const QString stableLength = QStringLiteral(
        "                    root.automaticSizingMaximumLength,\n");
    QCOMPARE(visibility.count(stableLength), 1);
    visibility.replace(stableLength,
                       QStringLiteral("                    root.maxLength,\n"));
    QVERIFY2(!matchesStableDockOccupancyContract(
                 visibility, bridge, viewSource),
             "stable occupancy must not consume the animated maximum length");

    visibility = readFile(QStringLiteral(
        "containment/package/contents/ui/VisibilityManager.qml"));
    const QString stableGap = QStringLiteral(
        "                            metrics.configuredScreenEdgeMargin);\n");
    QCOMPARE(visibility.count(stableGap), 2);
    visibility.replace(
        stableGap,
        QStringLiteral(
            "                            metrics.mask.screenEdge);\n"));
    QVERIFY2(!matchesStableDockOccupancyContract(
                 visibility, bridge, viewSource),
             "every live Dock alignment must keep its configured gap out of"
             " animated occupancy");

    visibility = readFile(QStringLiteral(
        "containment/package/contents/ui/VisibilityManager.qml"));
    const QString occupancyRefresh = QStringLiteral(
        "        function onAutomaticSizingMaximumLengthChanged() {\n"
        "            //! A fully attached Justify Dock keeps a full-width effects rect,\n"
        "            //! so a configured length change has no paint signal to refresh\n"
        "            //! its stable occupied footprint. The configured resting budget\n"
        "            //! is an independent authority and must publish itself directly.\n"
        "            manager.updateMaskArea();\n"
        "        }");
    QCOMPARE(visibility.count(occupancyRefresh), 1);
    visibility.replace(
        occupancyRefresh,
        QStringLiteral(
            "        function onAutomaticSizingMaximumLengthChanged() {\n"
            "            //! Mutation probe: route to the wrong authority.\n"
            "            manager.updateInputGeometry();\n"
            "        }"));
    QVERIFY2(!matchesStableDockOccupancyContract(
                 visibility, bridge, viewSource),
             "configured resting length must explicitly republish stable"
             " occupancy even when attached paint remains full-span");

    QString triggerLeak = viewSource;
    const QString triggerRefresh = QStringLiteral(
        "    connect(this, &View::maxLengthChanged,\n"
        "            this, &View::updateWindowTouchTriggerGeometry);");
    QCOMPARE(triggerLeak.count(triggerRefresh), 1);
    triggerLeak.replace(
        triggerRefresh,
        QStringLiteral(
            "    connect(this, &View::screenGeometryChanged,\n"
            "            this, &View::updateWindowTouchTriggerGeometry);"));
    QVERIFY2(!matchesStableDockOccupancyContract(
                 readFile(QStringLiteral(
                     "containment/package/contents/ui/VisibilityManager.qml")),
                 bridge, triggerLeak),
             "configured resting length must explicitly recompute the"
             " window-touch trigger even when attached paint remains"
             " full-span");
}

void SourceGuardTest::stablePanelPopupAnchor_rejectsLegacyAnimationFreeze()
{
    const QString main = readFile(QStringLiteral(
        "containment/package/contents/ui/main.qml"));
    QString bindings = readFile(QStringLiteral(
        "containment/package/contents/ui/BindingsExternal.qml"));
    const QString visibility = readFile(QStringLiteral(
        "containment/package/contents/ui/VisibilityManager.qml"));
    const QString layouts = readFile(QStringLiteral(
        "containment/package/contents/ui/layouts/LayoutsContainer.qml"));
    const QString metrics = readFile(QStringLiteral(
        "containment/package/contents/ui/abilities/Metrics.qml"));
    const QString backgroundTotals = readFile(QStringLiteral(
        "containment/package/contents/ui/background/types/Totals.qml"));
    const QString viewHeader = readFile(QStringLiteral(
        "app/view/view.h"));
    const QString viewImplementation = readFile(QStringLiteral(
        "app/view/view.cpp"));

    const QString panelAwareGate = QStringLiteral(
        "        when: latteView\n"
        "              && latteView.effects\n"
        "              && (externalBindings.containmentItem.behaveAsPlasmaPanel\n"
        "                  || visibilityManager.inNormalState)");
    QCOMPARE(bindings.count(panelAwareGate), 1);
    bindings.replace(
        panelAwareGate,
        QStringLiteral(
            "        when: latteView\n"
            "              && latteView.effects\n"
            "              && visibilityManager.inNormalState"));

    QVERIFY2(!matchesStableFloatingPanelQmlContract(
                 main, bindings, visibility, layouts, metrics, backgroundTotals,
                 viewHeader, viewImplementation),
             "stable Panel popup anchors must not freeze during task-removal"
             " animations; the legacy normal-state gate remains Dock-only");
}

void SourceGuardTest::stableFloatingPanelE2e_keepsCanvasAndRevisionsFixed()
{
    const QString source = readFile(QStringLiteral(
        "tests/e2e/071-maximized-window-length.sh"));
    QVERIFY2(matchesStableFloatingPanelE2eContract(source),
             "recipe 071 must keep the partial QWindow, applet measurements,"
             " maximum-depth reservation, and physical-publication revisions"
             " stable through qreal progress and rapid reversals");
}

void SourceGuardTest::windowTouchAuthority_keepsDedicatedStableModel()
{
    QVERIFY2(
        matchesWindowTouchAuthorityContract(
            readFile(QStringLiteral(
                "containment/package/contents/ui/main.qml")),
            readFile(QStringLiteral(
                "containment/package/contents/ui/WindowTouchTracker.qml")),
            readFile(QStringLiteral(
                "app/view/windowtouchtracker.h")),
            readFile(QStringLiteral(
                "app/view/windowtouchtracker.cpp")),
            readFile(QStringLiteral(
                "app/view/floatingtransition.h")),
            readFile(QStringLiteral(
                "app/view/floatingtransition.cpp")),
            readFile(QStringLiteral(
                "app/view/view.h")),
            readFile(QStringLiteral(
                "app/view/view.cpp")),
            readFile(QStringLiteral(
                "shell/package/contents/configuration/pages/"
                "BehaviorConfig.qml"))),
        "window touch must use one per-view ungrouped current-context model,"
        " a non-restarting 10 ms deadline, and one atomic transition policy");
}

void SourceGuardTest::windowTouchAuthority_rejectsControlledMutations()
{
    const QString mainQml = readFile(QStringLiteral(
        "containment/package/contents/ui/main.qml"));
    QString model = readFile(QStringLiteral(
        "containment/package/contents/ui/WindowTouchTracker.qml"));
    const QString trackerHeader = readFile(QStringLiteral(
        "app/view/windowtouchtracker.h"));
    QString trackerImplementation = readFile(QStringLiteral(
        "app/view/windowtouchtracker.cpp"));
    const QString transitionHeader = readFile(QStringLiteral(
        "app/view/floatingtransition.h"));
    const QString transitionImplementation = readFile(QStringLiteral(
        "app/view/floatingtransition.cpp"));
    const QString viewHeader = readFile(QStringLiteral(
        "app/view/view.h"));
    const QString viewImplementation = readFile(QStringLiteral(
        "app/view/view.cpp"));
    const QString behaviorConfig = readFile(QStringLiteral(
        "shell/package/contents/configuration/pages/BehaviorConfig.qml"));

    QCOMPARE(model.count(QStringLiteral("filterByScreen: false")), 1);
    model.replace(
        QStringLiteral("filterByScreen: false"),
        QStringLiteral("filterByScreen: true"));
    QVERIFY(!matchesWindowTouchAuthorityContract(
        mainQml, model, trackerHeader, trackerImplementation,
        transitionHeader, transitionImplementation,
        viewHeader, viewImplementation, behaviorConfig));

    trackerImplementation = readFile(QStringLiteral(
        "app/view/windowtouchtracker.cpp"));
    QCOMPARE(
        trackerImplementation.count(
            QStringLiteral("if (!m_evaluationTimer.isActive())")),
        1);
    trackerImplementation.replace(
        QStringLiteral("if (!m_evaluationTimer.isActive())"),
        QStringLiteral("if (true)"));
    QVERIFY(!matchesWindowTouchAuthorityContract(
        mainQml,
        readFile(QStringLiteral(
            "containment/package/contents/ui/WindowTouchTracker.qml")),
        trackerHeader, trackerImplementation,
        transitionHeader, transitionImplementation,
        viewHeader, viewImplementation, behaviorConfig));

    trackerImplementation = readFile(QStringLiteral(
        "app/view/windowtouchtracker.cpp"));
    const QString nonWindowClassification = QStringLiteral(
        "        if (!*isWindow) {\n"
        "            continue;\n"
        "        }\n");
    QCOMPARE(
        trackerImplementation.count(
            nonWindowClassification),
        1);
    trackerImplementation.remove(
        nonWindowClassification);
    QVERIFY2(!matchesWindowTouchAuthorityContract(
                 mainQml,
                 readFile(QStringLiteral(
                     "containment/package/contents/ui/"
                     "WindowTouchTracker.qml")),
                 trackerHeader, trackerImplementation,
                 transitionHeader, transitionImplementation,
                 viewHeader, viewImplementation, behaviorConfig),
             "window-only roles must not be decoded before a heterogeneous"
             " task row is classified");

    QString dynamicJustifyTrigger = viewImplementation;
    const QString stableRatio = QStringLiteral(
        "                .maxLength = maxLength(),\n");
    QCOMPARE(dynamicJustifyTrigger.count(stableRatio), 1);
    dynamicJustifyTrigger.replace(
        stableRatio,
        QStringLiteral("                .maxLength = 1.0F,\n"));
    QVERIFY2(!matchesWindowTouchAuthorityContract(
                 mainQml,
                 readFile(QStringLiteral(
                     "containment/package/contents/ui/"
                     "WindowTouchTracker.qml")),
                 trackerHeader,
                 readFile(QStringLiteral(
                     "app/view/windowtouchtracker.cpp")),
                 transitionHeader, transitionImplementation,
                 viewHeader, dynamicJustifyTrigger, behaviorConfig),
             "a presented full-span Dock must not widen its stable touch"
             " trigger authority");
}

void SourceGuardTest::windowTouchE2e_drivesOneStableTriggerClient()
{
    QVERIFY2(
        matchesWindowTouchE2eContract(readFile(QStringLiteral(
            "tests/e2e/072-window-touch-transition.sh"))),
        "recipe 072 must drive one real non-maximized client across the"
        " stable trigger through interactive reversals, Escape restoration,"
        " committed maximize, pointer deferral, and destruction while"
        " physical geometry and per-view authorities remain fixed");
}

void SourceGuardTest::
    liveTitlebarWindowTouchE2e_coversPanelsAndDocksBeforeRelease()
{
    QVERIFY2(
        matchesLiveTitlebarWindowTouchE2eContract(
            readFile(QStringLiteral(
                "tests/e2e/074-live-titlebar-window-touch.sh"))),
        "recipe 074 must cross and reverse Panel, partial Center Dock, and"
        " expanding Justify Dock triggers during a button-held titlebar"
        " drag without changing physical surface or reservation state");
}

void SourceGuardTest::
    liveTitlebarWindowTouchE2e_rejectsMissingHeldProof()
{
    QString recipe = readFile(QStringLiteral(
        "tests/e2e/074-live-titlebar-window-touch.sh"));
    QVERIFY(matchesLiveTitlebarWindowTouchE2eContract(recipe));

    const QString heldProof = QStringLiteral(
        "                kill -0 \"$drag_pid\" 2>/dev/null \\\n"
        "                    || e2e_fail \"$boundary appeared only after "
        "button release\"\n");
    QCOMPARE(recipe.count(heldProof), 1);
    recipe.remove(heldProof);
    QVERIFY2(
        !matchesLiveTitlebarWindowTouchE2eContract(recipe),
        "a visual endpoint observed after release is not evidence of live"
        " button-held attachment");

    QString partialEndpoint = readFile(QStringLiteral(
        "tests/e2e/074-live-titlebar-window-touch.sh"));
    const QString fullSpan = QStringLiteral(
        "              && \"$presented_length\" -eq "
        "\"$output_length\"\n");
    QCOMPARE(partialEndpoint.count(fullSpan), 1);
    partialEndpoint.replace(
        fullSpan,
        QStringLiteral(
            "              && \"$presented_length\" -lt "
            "\"$output_length\"\n"));
    QVERIFY2(!matchesLiveTitlebarWindowTouchE2eContract(partialEndpoint),
             "live attachment must prove the complete output span, not"
             " merely a wider partial presentation");
}

void SourceGuardTest::windowTouchTopologyE2e_keepsIndependentRegionsAndOutputs()
{
    QVERIFY2(
        matchesWindowTouchTopologyE2eContract(
            readFile(QStringLiteral(
                "tests/e2e/073-window-touch-topology.sh")),
            readFile(QStringLiteral(
                "tests/e2e/fixtures/fp4b/oracle.py"))),
        "recipe 073 must preserve independent partial-panel authorities,"
        " exact disjoint activation, maximum-depth reservations, and restart"
        " state across full-touching, partial-touching, and disconnected"
        " landscape/portrait outputs");
}

void SourceGuardTest::windowTouchTopologyE2e_cleanupGuardRejectsControlledMutations()
{
    const QString recipeSource = readFile(QStringLiteral(
        "tests/e2e/073-window-touch-topology.sh"));
    const QString recipe = normalizedCode(recipeSource);
    const QString oracle = readFile(QStringLiteral(
        "tests/e2e/fixtures/fp4b/oracle.py"));
    QVERIFY(matchesWindowTouchTopologyE2eContract(recipe, oracle));

    QString lateActivation = recipe;
    const QString activation =
        QStringLiteral("fixture_transaction_active=1");
    QCOMPARE(lateActivation.count(activation), 1);
    lateActivation.remove(activation);
    const QString stageFailure = QStringLiteral(
        "matrix_stagepanel-bottom-justify-1out"
        "\\"
        "||e2e_fail\"couldnotstagetheFP-4Bpanelseed\"");
    QCOMPARE(lateActivation.count(stageFailure), 1);
    lateActivation.replace(stageFailure, stageFailure + activation);
    QVERIFY2(!matchesWindowTouchTopologyE2eContract(
                 lateActivation, oracle),
             "arming restoration after matrix_stage must fail the lifecycle guard");

    QString missingRestore = recipe;
    const QString restore = QStringLiteral(
        "cp-r\"$MATRIX_PRISTINE\"\"$E2E_CONFIG_HOME\""
        "\\"
        "||cleanup_failed=1");
    QCOMPARE(missingRestore.count(restore), 1);
    missingRestore.remove(restore);
    QVERIFY2(!matchesWindowTouchTopologyE2eContract(
                 missingRestore, oracle),
             "removing pristine-config restoration must fail the lifecycle guard");

    QString missingRestart = recipe;
    const QString restart =
        QStringLiteral("elif!e2e_dock_start90;then");
    QCOMPARE(missingRestart.count(restart), 1);
    missingRestart.replace(restart, QStringLiteral("eliftrue;then"));
    QVERIFY2(!matchesWindowTouchTopologyE2eContract(
                 missingRestart, oracle),
             "removing the pristine nested-dock restart must fail the lifecycle guard");

    const QString cleanupBody = functionBody(
        recipeSource, QStringLiteral("cleanup()"));
    QVERIFY2(!cleanupBody.isEmpty(), "production cleanup function not found");

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString harness = QStringLiteral(R"SH(
cleanup_body=$1
test_root=$2
MATRIX_PRISTINE=$test_root/pristine
E2E_CONFIG_HOME=$test_root/config
CALL_LOG=$test_root/calls.log
mkdir -p "$MATRIX_PRISTINE"
printf 'pristine\n' > "$MATRIX_PRISTINE/state"

run_cleanup_case() {
    body_status=$1
    DOCK_START_STATUS=$2
    expected_status=$3
    rm -rf "$E2E_CONFIG_HOME"
    mkdir -p "$E2E_CONFIG_HOME"
    printf 'staged\n' > "$E2E_CONFIG_HOME/state"
    : > "$CALL_LOG"
    (
        eval "cleanup() $cleanup_body"
        client_pid=0
        fixture_transaction_active=1
        topology_captured=1
        original_topology=captured-topology
        mo_restore_output_topology() {
            printf 'topology:%s\n' "$1" >> "$CALL_LOG"
        }
        e2e_dock_stop() {
            printf 'stop\n' >> "$CALL_LOG"
        }
        e2e_dock_pid() {
            printf 'pid\n' >> "$CALL_LOG"
        }
        e2e_dock_start() {
            printf 'start:%s\n' "$1" >> "$CALL_LOG"
            return "$DOCK_START_STATUS"
        }
        return_status() {
            return "$1"
        }
        return_status "$body_status"
        cleanup
    )
    actual_status=$?
    [[ "$actual_status" -eq "$expected_status" ]] || return 1
    [[ "$(cat "$E2E_CONFIG_HOME/state")" == pristine ]] || return 1
}

run_cleanup_case 37 0 37 || exit 1
[[ "$(cat "$CALL_LOG")" == $'topology:captured-topology\nstop\npid\nstart:90' ]] \
    || exit 1
run_cleanup_case 0 1 1 || exit 1
)SH");
    QProcess process;
    process.start(
        QStringLiteral("bash"),
        {QStringLiteral("-c"),
         harness,
         QStringLiteral("fp4b-cleanup-test"),
         cleanupBody,
         temporary.path()});
    QVERIFY(process.waitForStarted());
    QVERIFY(process.waitForFinished());
    const QByteArray processError = process.readAllStandardError();
    QVERIFY2(
        process.exitStatus() == QProcess::NormalExit
            && process.exitCode() == 0,
        processError.constData());
}

void SourceGuardTest::linkedOperationStormE2e_keepsTransactionalReplayContract()
{
    QVERIFY2(
        matchesLinkedOperationStormE2eContract(
            readFile(QStringLiteral(
                "tests/e2e/linked-dock-operation-stress.sh")),
            readFile(QStringLiteral(
                "tests/e2e/fixtures/fp4c/operation_model.py"))),
        "the FP-4C operation storm must validate its typed plan before a"
        " whole-config transaction, prove every schema-9 checkpoint and"
        " compositor owner, record exact replay, and restore the pristine"
        " nested state on every exit");
}

void SourceGuardTest::linkedOperationStormE2e_sourceGuardRejectsControlledMutations()
{
    const QString recipe = readFile(QStringLiteral(
        "tests/e2e/linked-dock-operation-stress.sh"));
    const QString model = readFile(QStringLiteral(
        "tests/e2e/fixtures/fp4c/operation_model.py"));
    QVERIFY(matchesLinkedOperationStormE2eContract(recipe, model));

    QString missingDurableMoveReadback = recipe;
    const QString durableMoveReadback =
        QStringLiteral("viewMoveTransactionsData");
    QCOMPARE(
        missingDurableMoveReadback.count(
            durableMoveReadback),
        1);
    missingDurableMoveReadback.replace(
        durableMoveReadback,
        QStringLiteral("dockSystemData"));
    QVERIFY2(
        !matchesLinkedOperationStormE2eContract(
            missingDurableMoveReadback,
            model),
        "removing durable move readback must fail the FP-4C guard");

    QString missingCrossLayoutAction = model;
    const QString crossLayoutAction =
        QStringLiteral("\"method\": \"moveViewToLayout\"");
    QCOMPARE(missingCrossLayoutAction.count(crossLayoutAction), 1);
    missingCrossLayoutAction.replace(
        crossLayoutAction,
        QStringLiteral("\"method\": \"setViewPlacement\""));
    QVERIFY2(
        !matchesLinkedOperationStormE2eContract(
            recipe,
            missingCrossLayoutAction),
        "replacing the durable cross-layout action must fail the FP-4C guard");

    QString missingLifecycleVerdict = recipe;
    const QString lifecycleVerdict =
        QStringLiteral(
            "python3 \"$MODEL\" assert-view-move-lifecycle");
    QCOMPARE(missingLifecycleVerdict.count(lifecycleVerdict), 1);
    missingLifecycleVerdict.replace(
        lifecycleVerdict,
        QStringLiteral("python3 \"$MODEL\" assert-checkpoint"));
    QVERIFY2(
        !matchesLinkedOperationStormE2eContract(
            missingLifecycleVerdict,
            model),
        "removing durable move lifecycle deltas must fail the FP-4C guard");

    QString missingTrap = recipe;
    const QString trap = QStringLiteral("trap cleanup EXIT");
    QCOMPARE(missingTrap.count(trap), 1);
    missingTrap.remove(trap);
    QVERIFY2(
        !matchesLinkedOperationStormE2eContract(missingTrap, model),
        "removing pre-mutation cleanup arming must fail the FP-4C guard");

    QString liveRestore = recipe;
    const QString stoppedGate = QStringLiteral(
        "[[ \"$backup_ready\" == true && \"$dock_stopped\" == true ]]");
    QCOMPARE(liveRestore.count(stoppedGate), 1);
    liveRestore.replace(
        stoppedGate,
        QStringLiteral("[[ \"$backup_ready\" == true ]]"));
    QVERIFY2(
        !matchesLinkedOperationStormE2eContract(liveRestore, model),
        "allowing config replacement under a live dock must fail the FP-4C guard");

    QString partialRestoreStart = recipe;
    const QString restoredGate =
        QStringLiteral("&& \"$config_safe_to_start\" == true");
    QCOMPARE(partialRestoreStart.count(restoredGate), 2);
    partialRestoreStart.remove(restoredGate);
    QVERIFY2(
        !matchesLinkedOperationStormE2eContract(partialRestoreStart, model),
        "starting against a partial restore must fail the FP-4C guard");

    QString incompleteWindowSet = model;
    const QString exactWindowSet =
        QStringLiteral("if len(candidates) != 1:");
    QCOMPARE(incompleteWindowSet.count(exactWindowSet), 2);
    incompleteWindowSet.replace(exactWindowSet, QStringLiteral("if False:"));
    QVERIFY2(
        !matchesLinkedOperationStormE2eContract(recipe, incompleteWindowSet),
        "ignoring leaked layer-3 windows must fail the FP-4C guard");

    QString legacySchema = model;
    QVERIFY(legacySchema.contains(QStringLiteral("currentVisibleGeometry")));
    legacySchema.replace(
        QStringLiteral("currentVisibleGeometry"),
        QStringLiteral("visibleGeometry"));
    QVERIFY2(
        !matchesLinkedOperationStormE2eContract(recipe, legacySchema),
        "using the nonexistent visibleGeometry field must fail the FP-4C guard");

    QString missingExternalOutputs = recipe;
    const QString visualInput = QStringLiteral(
        "{\"snapshot\": snapshot, \"outputs\": outputs, \"windows\": windows}");
    QCOMPARE(missingExternalOutputs.count(visualInput), 1);
    missingExternalOutputs.replace(
        visualInput,
        QStringLiteral("{\"snapshot\": snapshot, \"windows\": windows}"));
    QVERIFY2(
        !matchesLinkedOperationStormE2eContract(
            missingExternalOutputs,
            model),
        "visual ownership must consume independent output identity and geometry");

    QString circularReservationGeometry = model;
    const QString externalGeometry =
        QStringLiteral(
            "expected_global_reservation_window_geometry");
    QCOMPARE(
        circularReservationGeometry.count(
            externalGeometry),
        2);
    circularReservationGeometry.replace(
        externalGeometry,
        QStringLiteral(
            "observed_reservation_window_geometry"));
    QVERIFY2(
        !matchesLinkedOperationStormE2eContract(
            recipe,
            circularReservationGeometry),
        "reservation publishers must be derived from external output geometry");
}

void SourceGuardTest::linkedOperationStormE2e_cleanupPreservesFailureAndSafety()
{
    const QString recipe = readFile(QStringLiteral(
        "tests/e2e/linked-dock-operation-stress.sh"));
    const QString cleanupBody = functionBody(
        recipe, QStringLiteral("cleanup()"));
    const QString restoreBody = functionBody(
        recipe, QStringLiteral("restore_config_exactly()"));
    QVERIFY2(!cleanupBody.isEmpty(), "FP-4C cleanup function not found");
    QVERIFY2(!restoreBody.isEmpty(), "FP-4C restore function not found");

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString harness = QStringLiteral(R"SH(
cleanup_body=$1
restore_body=$2
test_root=$3
E2E_RT=$test_root/runtime
E2E_CONFIG_HOME=$E2E_RT/config
backup_dir=$E2E_RT/backup
mkdir -p "$E2E_CONFIG_HOME" "$backup_dir"
printf 'staged\n' > "$E2E_CONFIG_HOME/state"
printf 'pristine\n' > "$backup_dir/state"

path_is_within() {
    return 0
}
eval "restore_config_exactly() $restore_body"
backup_ready=true
restore_config_exactly || exit 10
[[ "$(cat "$E2E_CONFIG_HOME/state")" == pristine ]] || exit 11

eval "cleanup() $cleanup_body"
run_cleanup_case() {
    case_name=$1
    original_status=$2
    stop_status=$3
    pid_live=$4
    restore_status=$5
    expected_status=$6
    expected_log=$7
    case_log=$test_root/$case_name.log
    : > "$case_log"
    (
        transaction_started=true
        backup_ready=true
        acceptance_completed=true
        cleanup_failed=0
        artifact_dir=$test_root
        baseline_projection_file=$test_root/baseline
        stop_dock_if_running() {
            printf 'stop\n' >> "$case_log"
            return "$stop_status"
        }
        e2e_dock_pid() {
            [[ "$pid_live" == true ]] || return 1
            printf '%s\n' "$$"
        }
        restore_config_exactly() {
            printf 'restore\n' >> "$case_log"
            return "$restore_status"
        }
        dock_is_running() {
            return 1
        }
        e2e_dock_start() {
            printf 'start\n' >> "$case_log"
            return 0
        }
        snapshot() {
            return 1
        }
        return_status() {
            return "$1"
        }
        return_status "$original_status"
        cleanup
    )
    actual_status=$?
    [[ "$actual_status" -eq "$expected_status" ]] || exit 20
    [[ "$(cat "$case_log")" == "$expected_log" ]] || exit 21
}

run_cleanup_case success 37 0 false 0 37 $'stop\nrestore\nstart' || exit $?
run_cleanup_case live-dock 0 1 true 0 1 $'stop' || exit $?
run_cleanup_case partial-restore 0 0 false 1 1 $'stop\nrestore' || exit $?
)SH");

    QProcess process;
    process.start(
        QStringLiteral("bash"),
        {QStringLiteral("-c"),
         harness,
         QStringLiteral("fp4c-cleanup-test"),
         cleanupBody,
         restoreBody,
         temporary.path()});
    QVERIFY(process.waitForStarted());
    QVERIFY(process.waitForFinished());
    const QByteArray processError = process.readAllStandardError();
    QVERIFY2(
        process.exitStatus() == QProcess::NormalExit
            && process.exitCode() == 0,
        processError.constData());
}

void SourceGuardTest::multiOutputRestore_keepsCompleteSemanticStateContract()
{
    QVERIFY2(
        matchesCompleteKScreenRestoreContract(readFile(QStringLiteral(
            "tests/e2e/matrix/multi-output-lib.sh"))),
        "multi-output cleanup must restore every field this harness can"
        " mutate and compare the complete captured KScreen payload without"
        " guessed setters");
}

void SourceGuardTest::multiOutputRestore_sourceGuardRejectsProjectionOnlyVerification()
{
    QString source = readFile(QStringLiteral(
        "tests/e2e/matrix/multi-output-lib.sh"));
    QVERIFY(matchesCompleteKScreenRestoreContract(source));

    const QString semanticComparison = QStringLiteral(
        "_mo_compare_output_state_semantically \"$captured\" \"$current\" 2>&1");
    QCOMPARE(source.count(semanticComparison), 1);
    source.replace(semanticComparison, QStringLiteral(
        "printf '%s\\n' \"$current_projection\""));
    QVERIFY2(
        !matchesCompleteKScreenRestoreContract(source),
        "restoring projection-only verification must fail the source guard");
}

void SourceGuardTest::multiOutputRestore_sourceGuardRejectsMissingPrioritySetter()
{
    QString source = readFile(QStringLiteral(
        "tests/e2e/matrix/multi-output-lib.sh"));
    QVERIFY(matchesCompleteKScreenRestoreContract(source));

    const QString prioritySetter = QStringLiteral(
        "\"output.${name}.priority.${priority}\"");
    QCOMPARE(source.count(prioritySetter), 1);
    source.replace(prioritySetter, QStringLiteral(
        "\"output.${name}.position.${x},${y}\""));
    QVERIFY2(
        !matchesCompleteKScreenRestoreContract(source),
        "omitting the captured priority setter must fail the source guard");
}

void SourceGuardTest::floatingPresentationConsumers_keepSingleAuthority()
{
    const QString effects = normalizedCode(readFile(
        QStringLiteral("app/view/effects.cpp")));
    const QString view = normalizedCode(readFile(
        QStringLiteral("app/view/view.cpp")));
    const QString inputHelper = normalizedCode(readFile(
        QStringLiteral("app/view/floatinginputevent.h")));
    const QString popupHelper = normalizedCode(readFile(
        QStringLiteral("app/view/floatingpopuppresentation.h")));
    const QString bindings = normalizedCode(readFile(
        QStringLiteral(
            "containment/package/contents/ui/BindingsExternal.qml")));
    const QString main = normalizedCode(readFile(
        QStringLiteral("containment/package/contents/ui/main.qml")));
    const QString background = normalizedCode(readFile(
        QStringLiteral(
            "containment/package/contents/ui/background/MultiLayered.qml")));
    const QString visibility = normalizedCode(readFile(
        QStringLiteral(
            "containment/package/contents/ui/VisibilityManager.qml")));
    const QString dialog = normalizedCode(readFile(
        QStringLiteral("declarativeimports/core/dialog.cpp")));
    const QString positioner = normalizedCode(readFile(
        QStringLiteral("app/view/positioner.cpp")));
    const QString transition = normalizedCode(readFile(
        QStringLiteral("app/view/floatingtransition.cpp")));
    const QString positionerCore = normalizedCode(readFile(
        QStringLiteral("app/view/positionergeometry.h")));
    const QString panelShadowsHeader = normalizedCode(readFile(
        QStringLiteral("app/view/panelshadows_p.h")));
    const QString panelShadowsSource = normalizedCode(readFile(
        QStringLiteral("app/view/panelshadows.cpp")));

    const qsizetype eventStart = view.indexOf(
        QStringLiteral("boolView::event(QEvent*e)"));
    const qsizetype projection = view.indexOf(
        QStringLiteral("FloatingInputEvent::routeMouseEvent("),
        eventStart);
    const qsizetype observer = view.indexOf(
        QStringLiteral("Q_EMITeventTriggered(e)"), eventStart);
    QVERIFY(eventStart >= 0 && projection > eventStart
            && observer > projection);
    QVERIFY(view.contains(QStringLiteral("QEvent::MouseButtonDblClick")));
    QVERIFY(view.contains(QStringLiteral("mapToGlobal(adjusted)")));
    QVERIFY(view.contains(QStringLiteral(
        "QScopedValueRollback<bool>projectionGuard")));
    QVERIFY(inputHelper.contains(QStringLiteral("source.pixelDelta()")));
    QVERIFY(inputHelper.contains(QStringLiteral("source.angleDelta()")));
    QVERIFY(inputHelper.contains(QStringLiteral("source.phase()")));
    QVERIFY(inputHelper.contains(QStringLiteral("source.pointingDevice()")));

    QVERIFY(effects.contains(QStringLiteral(
        "EffectRegion::rasterizedTranslatedShape(visibleShape,localShape)")));
    QVERIFY(effects.contains(QStringLiteral(
        "PanelBorderDecision::enabledBorders(")));
    QVERIFY(effects.contains(QStringLiteral(
        "PanelBorderDecision::doesPresentationFillOutputPrimaryAxis("
        "m_rect,surfaceGeometry,assignedOutputGeometry,*edge)")));
    QVERIFY(effects.contains(QStringLiteral(
        "positioner->surfaceGeometry()")));
    QVERIFY(effects.contains(QStringLiteral(
        "positioner->surfaceOutputGeometry()")));
    QVERIFY(effects.contains(QStringLiteral(
        "positioner->surfacePlacementGeneration()"
        "!=positioner->relocationGeneration()")));
    QVERIFY(!effects.contains(QStringLiteral(
        "m_view->screenGeometry()")));
    QVERIFY(effects.contains(QStringLiteral(
        "Effectsrefusedinvalidbordergeometryfor")));
    QVERIFY(effects.contains(QStringLiteral(
        "if(m_waitingForLegacyDockPresentation&&!m_rect.isValid())")));
    QVERIFY(effects.contains(QStringLiteral(
        "if(!m_rect.isValid()||!surfaceGeometry.isValid()"
        "||!assignedOutputGeometry.isValid())")));
    QVERIFY(view.contains(QStringLiteral(
        "&ViewPart::Positioner::surfaceGeometryPublicationRevisionChanged,"
        "m_effects,&ViewPart::Effects::updateEnabledBorders")));
    QVERIFY(positioner.contains(QStringLiteral(
        "m_appliedSurfaceGeometry=solved.surface;"
        "m_appliedOutputGeometry=assignedScreenGeometry;"
        "m_surfacePlacementGeneration=m_relocationGeneration;")));
    QVERIFY(positioner.contains(QStringLiteral(
        "++m_surfaceGeometryPublicationRevision;"
        "applySolvedWindowGeometry(solved.surface);")));
    const qsizetype layerApplication = positioner.indexOf(QStringLiteral(
        "if(!m_view->applyPositionedLayerShellGeometry("
        "placementScreen,solved->surface))"));
    const qsizetype appliedPublication = positioner.indexOf(QStringLiteral(
        "publishAppliedGeometry(*solved,assignedScreenGeometry,"));
    QVERIFY(layerApplication >= 0
            && appliedPublication > layerApplication);
    QVERIFY(!positioner.mid(layerApplication,
                            appliedPublication - layerApplication)
                 .contains(QStringLiteral("configureGeometry(")));
    QVERIFY(transition.contains(QStringLiteral(
        "if(installGeometryWithoutNotification(solution)){"
        "publishInstalledGeometryChange();}")));
    QVERIFY(positioner.contains(QStringLiteral(
        "transition->installGeometryWithoutNotification("
        "solved.floatingPresentation);")));
    QVERIFY(positioner.contains(QStringLiteral(
        "if(transitionChanged){"
        "transition->publishInstalledGeometryChange();}"
        "Q_EMITsurfaceGeometryPublicationRevisionChanged();")));
    QVERIFY(!effects.contains(QStringLiteral(
        "enableBlurBehind(m_view,true);")));
    QVERIFY(!effects.contains(QStringLiteral(
        "enableBackgroundContrast(m_view,m_theme.backgroundContrastEnabled(),"
        "m_backEffectContrast,m_backEffectIntesity,m_backEffectSaturation);")));
    QVERIFY(effects.contains(QStringLiteral(
        "if(!m_view->behaveAsPlasmaPanel()){"
        "publishFloatingMaskGeneration("
        "m_floatingMaskHandshake.transferToLegacy());")));
    QVERIFY(effects.contains(QStringLiteral(
        "m_view->setProperty(\"_floating_visible_geometry\",QVariant{});")));
    QVERIFY(effects.contains(QStringLiteral(
        "m_view->setProperty(\"_floating_anchor_revision\","
        "QVariant::fromValue(++m_floatingAnchorRevision));")));

    QVERIFY(background.contains(QStringLiteral(
        "if(!latteView||barLine.containmentRoot.behaveAsPlasmaPanel)"
        "return;")));
    QVERIFY(background.contains(QStringLiteral(
        "requiredpropertyItemcontainmentRoot")));
    QVERIFY(background.contains(QStringLiteral(
        "requiredpropertyvardockView")));
    QVERIFY(bindings.contains(QStringLiteral(
        "if(!externalBindings.dockView"
        "||!externalBindings.dockBackground){console.error(")));
    QVERIFY(visibility.contains(QStringLiteral(
        "if(manager.window.behaveAsPlasmaPanel"
        "&&!manager.window.visibility.isHidden"
        "&&!manager.window.visibility.isSidebar){return;}")));
    QVERIFY(main.contains(QStringLiteral(
        "VisibilityManager{id:visibilityManager"
        "layouts:layoutsContainerwindow:latteView"
        "inClientSideScreenEdgeSliding:")));
    QVERIFY(main.contains(QStringLiteral(
        "Background.MultiLayered{id:_background"
        "containmentRoot:rootdockView:latteView}")));
    QVERIFY(main.contains(QStringLiteral(
        "floatingTransition.displayHintsWithFloatingPreference(")));
    QVERIFY(popupHelper.contains(QStringLiteral(
        "currentHints|floatingHint")));
    QVERIFY(popupHelper.contains(QStringLiteral(
        "currentHints&~floatingHint")));
    QVERIFY(dialog.contains(QStringLiteral(
        "isAnchorRevisionProperty(")));
    QVERIFY(dialog.contains(QStringLiteral(
        "FloatingPopupPresentation::perpendicularAnchor(")));
    QVERIFY(dialog.contains(QStringLiteral(
        "updateGeometry();")));
    QVERIFY(dialog.contains(QStringLiteral(
        "&QQuickItem::windowChanged")));
    QVERIFY(dialog.contains(QStringLiteral(
        "m_anchorWindowFilter.followWindow(window)")));
    QVERIFY(dialog.contains(QStringLiteral(
        "m_anchorWindowFilter.observes(watched)")));
    QVERIFY(visibility.contains(QStringLiteral(
        "functiononIsSidebarChanged(){manager.updateInputGeometry();}")));
    QVERIFY(panelShadowsHeader.contains(QStringLiteral(
        "shadowStateFor(constQWindow*window)const")));
    QVERIFY(panelShadowsHeader.contains(QStringLiteral(
        "voidshadowStateChanged(QWindow*window)")));
    QCOMPARE(panelShadowsSource.count(QStringLiteral(
        "Q_EMITshadowStateChanged(window);")), 5);

    QVERIFY(!positionerCore.contains(
        QStringLiteral("behaveAsPlasmaPanel")));
    QVERIFY(!positionerCore.contains(
        QStringLiteral("floatingGap")));
    QVERIFY(!positionerCore.contains(
        QStringLiteral("visibilitySlideOffset")));
}

void SourceGuardTest::
    panelToDockInputHandoff_bypassesOrdinaryAnimationGate()
{
    const QString visibility = readFile(QStringLiteral(
        "containment/package/contents/ui/VisibilityManager.qml"));
    QVERIFY(matchesPanelToDockInputHandoff(visibility));
    QVERIFY(normalizedCode(visibility).contains(QStringLiteral(
        "if(manager.updateIsEnabled){manager.updateInputGeometry();}")));
}

void SourceGuardTest::
    panelToDockInputHandoff_rejectsMissingDirectWrite()
{
    const QString original = readFile(QStringLiteral(
        "containment/package/contents/ui/VisibilityManager.qml"));
    QVERIFY(matchesPanelToDockInputHandoff(original));

    QString missingDirectWrite = original;
    const QString directSequence =
        QStringLiteral("            manager.updateMaskArea();\n"
                       "            // updateMaskArea intentionally gates ordinary animation writes on\n"
                       "            // updateIsEnabled. This ownership handoff is not an animation:\n"
                       "            // the old native panel bridge must be replaced even during\n"
                       "            // autosize, relocation, or a slide.\n"
                       "            manager.updateInputGeometry();");
    QVERIFY(missingDirectWrite.contains(directSequence));
    missingDirectWrite.replace(
        directSequence,
        QStringLiteral("            manager.updateMaskArea();"));
    QVERIFY(!matchesPanelToDockInputHandoff(
        missingDirectWrite));
}

void SourceGuardTest::dockBackgroundFit_includesJustifyDockMode()
{
    const QString source = readFile(QStringLiteral(
        "containment/package/contents/ui/background/MultiLayered.qml"));
    QVERIFY2(matchesDockBackgroundFitRouting(source),
             "dock-mode Justify must share the shadow-independent solid fit");
}

void SourceGuardTest::dockBackgroundFit_sourceGuardsRejectBypasses()
{
    const QString original = readFile(QStringLiteral(
        "containment/package/contents/ui/background/MultiLayered.qml"));
    QVERIFY(matchesDockBackgroundFitRouting(original));

    QString lengthBypass = original;
    const QString fittedCall = QStringLiteral(
        "return backgroundStateResolver.dockBackgroundLength(requestedLength,\n"
        "                                                             viewPrimaryLength);");
    QCOMPARE(lengthBypass.count(fittedCall), 1);
    lengthBypass.replace(fittedCall, QStringLiteral("return requestedLength;"));
    QVERIFY2(!matchesDockBackgroundFitRouting(lengthBypass),
             "bypassing the solid-background fit must fail the routing guard");

    QString restingMaximumRestored = original;
    QCOMPARE(restingMaximumRestored.count(fittedCall), 1);
    restingMaximumRestored.replace(
        fittedCall,
        QStringLiteral(
            "return backgroundStateResolver.dockBackgroundLength(requestedLength,\n"
            "                                                             maximumLength);"));
    QVERIFY2(!matchesDockBackgroundFitRouting(restingMaximumRestored),
             "using the configured resting maximum as a hover clipping plane must fail");

    QString shadowBudgetRestored = original;
    const QString stableJustifyLength = QStringLiteral(
        "? maximumLength\n"
        "                : Math.max(root.minLength,");
    QCOMPARE(shadowBudgetRestored.count(stableJustifyLength), 1);
    shadowBudgetRestored.replace(
        stableJustifyLength,
        QStringLiteral(
            "? Math.max(0, maximumLength - barLine.totals.shadowsLength)\n"
            "                : Math.max(root.minLength,"));
    QVERIFY2(!matchesDockBackgroundFitRouting(shadowBudgetRestored),
             "charging shadow chrome against the solid budget must fail");

    const QString visualPlacement = QStringLiteral(
        "return backgroundStateResolver.dockVisualCenterOffset(\n"
        "                    requestedSolidOffset,\n"
        "                    barLine.length,\n"
        "                    tailShadowLength,\n"
        "                    headShadowLength,\n"
        "                    viewPrimaryLength);");
    QCOMPARE(original.count(visualPlacement), 1);

    QString offsetBypass = original;
    offsetBypass.replace(
        visualPlacement,
        QStringLiteral("return requestedSolidOffset;"));
    QVERIFY2(!matchesDockBackgroundFitRouting(offsetBypass),
             "placing the visual at the solid center must fail for asymmetric shadows");

    QString completeVisualClampRestored = original;
    completeVisualClampRestored.replace(
        visualPlacement,
        QStringLiteral(
            "return backgroundStateResolver.centeredDockOffset(\n"
            "                    requestedSolidOffset\n"
            "                        + (headShadowLength - tailShadowLength) / 2,\n"
            "                    barLine.totals.visualLength,\n"
            "                    viewPrimaryLength);"));
    QVERIFY2(!matchesDockBackgroundFitRouting(completeVisualClampRestored),
             "clamping the complete visual may not displace the stable solid");

    QString swappedShadowEnds = original;
    swappedShadowEnds.replace(
        visualPlacement,
        QStringLiteral(
            "return backgroundStateResolver.dockVisualCenterOffset(\n"
            "                    requestedSolidOffset,\n"
            "                    barLine.length,\n"
            "                    headShadowLength,\n"
            "                    tailShadowLength,\n"
            "                    viewPrimaryLength);"));
    QVERIFY2(!matchesDockBackgroundFitRouting(swappedShadowEnds),
             "swapping semantic tail and head shadow lengths must fail");

    QString justifySharesConfiguredOffset = original;
    const QString zeroJustifyOffset = QStringLiteral(
        "const requestedSolidOffset = alignment === LatteCore.Types.Justify\n"
        "                ? 0\n"
        "                : root.offset + layoutsContainerItem.mainLayout.parabolicOffsetting;");
    QCOMPARE(justifySharesConfiguredOffset.count(zeroJustifyOffset), 1);
    justifySharesConfiguredOffset.replace(
        zeroJustifyOffset,
        QStringLiteral(
            "const requestedSolidOffset = alignment === LatteCore.Types.Justify\n"
            "                ? root.offset\n"
            "                : root.offset + layoutsContainerItem.mainLayout.parabolicOffsetting;"));
    QVERIFY2(!matchesDockBackgroundFitRouting(justifySharesConfiguredOffset),
             "Justify and Center must enter the same bridge with their stable offsets");
}

void SourceGuardTest::appletBudget_excludesInternalPaddingButNotShadows()
{
    const QString source = readFile(QStringLiteral(
        "containment/package/contents/ui/abilities/privates/LayouterPrivate.qml"));
    QVERIFY2(matchesShadowIndependentAppletBudget(source),
             "the stable applet budget must subtract only internal background padding");
}

void SourceGuardTest::appletBudget_sourceGuardRejectsShadowSubtraction()
{
    QString source = readFile(QStringLiteral(
        "containment/package/contents/ui/abilities/privates/LayouterPrivate.qml"));
    const QString stableBudget = QStringLiteral(
        "return root.maxLength - backgroundTotals.paddingsLength;");
    QCOMPARE(source.count(stableBudget), 1);
    source.replace(
        stableBudget,
        QStringLiteral(
            "return root.maxLength - backgroundTotals.paddingsLength\n"
            "                - backgroundTotals.shadowsLength;"));

    QVERIFY2(!matchesShadowIndependentAppletBudget(source),
             "restoring shadow subtraction must fail the applet-budget guard");
}

void SourceGuardTest::floatingPanelLayoutClearance_keepsStablePrimarySpan()
{
    const QString background = readFile(QStringLiteral(
        "containment/package/contents/ui/background/MultiLayered.qml"));
    const QString bindings = readFile(QStringLiteral(
        "containment/package/contents/ui/BindingsExternal.qml"));

    QVERIFY2(matchesStableFloatingPanelLayoutClearance(background, bindings),
             "configured floating Panels and live-attached Docks must keep one"
             " primary-axis applet and popup clearance budget while visual"
             " borders change");
}

void SourceGuardTest::floatingPanelLayoutClearance_sourceGuardsRejectDivergence()
{
    const QString originalBackground = readFile(QStringLiteral(
        "containment/package/contents/ui/background/MultiLayered.qml"));
    const QString originalBindings = readFile(QStringLiteral(
        "containment/package/contents/ui/BindingsExternal.qml"));
    QVERIFY(matchesStableFloatingPanelLayoutClearance(
        originalBackground, originalBindings));

    QString broadPanelAuthority = originalBackground;
    const QString exactAuthority = QStringLiteral(
        "readonly property bool stablePrimaryAxisLayoutClearance: "
        "!!(barLine.dockView\n"
        "                                                                 "
        "&& (barLine.dockView.floatingPanelConfigured\n"
        "                                                                     "
        "|| barLine.containmentRoot.dockFloatingTransitionOwnsGap))");
    QCOMPARE(broadPanelAuthority.count(exactAuthority), 1);
    broadPanelAuthority.replace(
        exactAuthority,
        QStringLiteral(
            "readonly property bool stablePrimaryAxisLayoutClearance: "
            "barLine.stablePanelEnvelope"));
    QVERIFY2(!matchesStableFloatingPanelLayoutClearance(
                 broadPanelAuthority, originalBindings),
             "zero-gap Panels must not gain floating layout clearance");

    QString paddingFollowsPaint = originalBackground;
    const QString stableLeftPadding = QStringLiteral(
        "paddings.left: "
        "backgroundStateResolver.edgePadding("
        "barLine.leftLayoutClearanceIsRequired,");
    QCOMPARE(paddingFollowsPaint.count(stableLeftPadding), 1);
    paddingFollowsPaint.replace(
        stableLeftPadding,
        QStringLiteral(
            "paddings.left: "
            "backgroundStateResolver.edgePadding(barLine.hasLeftBorder,"));
    QVERIFY2(!matchesStableFloatingPanelLayoutClearance(
                 paddingFollowsPaint, originalBindings),
             "primary-axis padding must not return to the changing visual"
             " border set");

    QString popupRoundnessFollowsPaint = originalBackground;
    const QString stableTailRoundness = QStringLiteral(
        "const layoutClearanceIsRequired = root.isHorizontal\n"
        "                ? leftLayoutClearanceIsRequired "
        ": topLayoutClearanceIsRequired;");
    QCOMPARE(popupRoundnessFollowsPaint.count(stableTailRoundness), 1);
    popupRoundnessFollowsPaint.replace(
        stableTailRoundness,
        QStringLiteral(
            "const layoutClearanceIsRequired = root.isHorizontal\n"
            "                ? hasLeftBorder : hasTopBorder;"));
    QVERIFY2(!matchesStableFloatingPanelLayoutClearance(
                 popupRoundnessFollowsPaint, originalBindings),
             "popup roundness must use the same stable clearance predicate"
             " as applet padding");

    QString visualBorderFollowsLayout = originalBackground;
    const QString visualLeftBorder = QStringLiteral(
        "hasLeftBorder: hasAllBorders || "
        "((solidBackground.enabledBorders "
        "& KSvg.FrameSvg.LeftBorder) > 0)");
    QCOMPARE(visualBorderFollowsLayout.count(visualLeftBorder), 1);
    visualBorderFollowsLayout.replace(
        visualLeftBorder,
        QStringLiteral(
            "hasLeftBorder: leftLayoutClearanceIsRequired"));
    QVERIFY2(!matchesStableFloatingPanelLayoutClearance(
                 visualBorderFollowsLayout, originalBindings),
             "the stable layout predicate must not feed back into painted"
             " borders or shadows");

    QString popupBypassesRoundness = originalBindings;
    const QString popupTail = QStringLiteral(
        "var panelTail = "
        "externalBindings.dockBackground.tailRoundness\n"
        "                        + "
        "externalBindings.dockBackground.tailRoundnessMargin;");
    QCOMPARE(popupBypassesRoundness.count(popupTail), 1);
    popupBypassesRoundness.replace(
        popupTail,
        QStringLiteral("var panelTail = 0;"));
    QVERIFY2(!matchesStableFloatingPanelLayoutClearance(
                 originalBackground, popupBypassesRoundness),
             "stable Panel popup bounds must consume the same live"
             " roundness clearance as applet sizing");
}

void SourceGuardTest::backgroundVisualThickness_usesMonotonicCore()
{
    const QString source = readFile(QStringLiteral(
        "containment/package/contents/ui/background/MultiLayered.qml"));
    QVERIFY2(matchesBackgroundVisualThicknessRouting(source),
             "current and maximum background thickness must share the monotonic core");
}

void SourceGuardTest::backgroundVisualThickness_sourceGuardRejectsDivergence()
{
    const QString original = readFile(QStringLiteral(
        "containment/package/contents/ui/background/MultiLayered.qml"));
    QVERIFY(matchesBackgroundVisualThicknessRouting(original));

    QString formulaRestored = original;
    const QString coreCall = QStringLiteral(
        "return backgroundStateResolver.visualThickness(totals.minThickness,");
    QCOMPARE(formulaRestored.count(coreCall), 2);
    formulaRestored.replace(coreCall,
                            QStringLiteral("return totals.minThickness + sizeFraction * ("));
    QVERIFY2(!matchesBackgroundVisualThicknessRouting(formulaRestored),
             "duplicating the thickness formula in QML must fail the routing guard");

    QString maximumUsesCurrentMetrics = original;
    const QString maximumMetrics = QStringLiteral(
        "metrics.maxIconSize + 2 * metrics.margin.maxTailThickness");
    QCOMPARE(maximumUsesCurrentMetrics.count(maximumMetrics), 1);
    maximumUsesCurrentMetrics.replace(
        maximumMetrics,
        QStringLiteral("metrics.iconSize + 2 * metrics.margin.tailThickness"));
    QVERIFY2(!matchesBackgroundVisualThicknessRouting(maximumUsesCurrentMetrics),
             "maximum thickness must not consume current item metrics");
}

void SourceGuardTest::dockBackgroundShadow_keepsFixedPixelFootprint()
{
    const QString custom = readFile(QStringLiteral(
        "containment/package/contents/ui/colorizer/CustomBackground.qml"));
    const QString effect = readFile(QStringLiteral(
        "containment/package/contents/ui/colorizer/BackgroundShadow.qml"));
    const QString layered = readFile(QStringLiteral(
        "containment/package/contents/ui/background/MultiLayered.qml"));
    const QString metrics = readFile(QStringLiteral(
        "declarativeimports/components/EffectMetrics.qml"));
    const QString shadowed = readFile(QStringLiteral(
        "declarativeimports/components/ShadowedItem.qml"));

    QVERIFY2(matchesAspectIndependentBackgroundShadow(custom, effect, layered,
                                                       metrics, shadowed),
             "custom background shadows must publish one fixed-pixel effect footprint");
}

void SourceGuardTest::dockBackgroundShadow_sourceGuardsRejectAspectScaledRenderer()
{
    const QString originalCustom = readFile(QStringLiteral(
        "containment/package/contents/ui/colorizer/CustomBackground.qml"));
    const QString originalEffect = readFile(QStringLiteral(
        "containment/package/contents/ui/colorizer/BackgroundShadow.qml"));
    const QString originalLayered = readFile(QStringLiteral(
        "containment/package/contents/ui/background/MultiLayered.qml"));
    const QString originalMetrics = readFile(QStringLiteral(
        "declarativeimports/components/EffectMetrics.qml"));
    const QString originalShadowed = readFile(QStringLiteral(
        "declarativeimports/components/ShadowedItem.qml"));
    QVERIFY(matchesAspectIndependentBackgroundShadow(originalCustom,
                                                     originalEffect,
                                                     originalLayered,
                                                     originalMetrics,
                                                     originalShadowed));

    QString aspectScaled = originalEffect;
    const QString fixedEffect = QStringLiteral("RectangularShadow {");
    QCOMPARE(aspectScaled.count(fixedEffect), 1);
    aspectScaled.replace(fixedEffect,
                         QStringLiteral("Kirigami.ShadowedRectangle {"));
    QVERIFY2(!matchesAspectIndependentBackgroundShadow(originalCustom,
                                                       aspectScaled,
                                                       originalLayered,
                                                       originalMetrics,
                                                       originalShadowed),
             "restoring the aspect-scaled Kirigami renderer must fail the guard");

    QString opacityCoupled = originalCustom;
    const QString independentBlur = QStringLiteral("blur: main.shadowSize");
    QCOMPARE(opacityCoupled.count(independentBlur), 1);
    opacityCoupled.replace(independentBlur,
                           QStringLiteral("opacity: main.backgroundOpacity\n"
                                          "        blur: main.shadowSize"));
    QVERIFY2(!matchesAspectIndependentBackgroundShadow(opacityCoupled,
                                                       originalEffect,
                                                       originalLayered,
                                                       originalMetrics,
                                                       originalShadowed),
             "binding sibling opacity to background opacity must fail the guard");

    QString frontStacked = originalCustom;
    const QString behindPainter = QStringLiteral("z: painter.z - 1");
    QCOMPARE(frontStacked.count(behindPainter), 1);
    frontStacked.replace(behindPainter, QStringLiteral("z: painter.z + 1"));
    QVERIFY2(!matchesAspectIndependentBackgroundShadow(frontStacked,
                                                       originalEffect,
                                                       originalLayered,
                                                       originalMetrics,
                                                       originalShadowed),
             "placing the shadow over its painter must fail the guard");

    QString kirigamiAlphaCompensation = originalCustom;
    const QString directThemeColor = QStringLiteral("color: main.shadowColor");
    QCOMPARE(kirigamiAlphaCompensation.count(directThemeColor), 1);
    kirigamiAlphaCompensation.replace(
        directThemeColor,
        QStringLiteral(
            "color: Qt.rgba(main.shadowColor.r,\n"
            "                       main.shadowColor.g,\n"
            "                       main.shadowColor.b,\n"
            "                       Math.min(1, 0.336 + main.shadowColor.a))"));
    QVERIFY2(!matchesAspectIndependentBackgroundShadow(kirigamiAlphaCompensation,
                                                       originalEffect,
                                                       originalLayered,
                                                       originalMetrics,
                                                       originalShadowed),
             "restoring Kirigami-specific alpha compensation must fail the guard");

    QString disconnectedAlias = originalCustom;
    const QString liveMargin = QStringLiteral(
        "readonly property alias shadowPaintMargin: backgroundShadow.paintMargin");
    QCOMPARE(disconnectedAlias.count(liveMargin), 1);
    disconnectedAlias.replace(liveMargin,
                              QStringLiteral("readonly property int shadowPaintMargin: 20"));
    QVERIFY2(!matchesAspectIndependentBackgroundShadow(disconnectedAlias,
                                                       originalEffect,
                                                       originalLayered,
                                                       originalMetrics,
                                                       originalShadowed),
             "replacing the renderer-owned margin alias must fail the guard");

    QString guessedMargins = originalLayered;
    guessedMargins.replace(QStringLiteral("barLine.customShadowPaintMargin"),
                           QStringLiteral("customShadow"));
    QVERIFY2(!matchesAspectIndependentBackgroundShadow(originalCustom,
                                                       originalEffect,
                                                       guessedMargins,
                                                       originalMetrics,
                                                       originalShadowed),
             "disconnecting geometry from the effect footprint must fail the guard");

    QString missingMetricsImport = originalEffect;
    const QString metricsImport = QStringLiteral(
        "import org.kde.latte.components 1.0 as LatteComponents\n");
    QCOMPARE(missingMetricsImport.count(metricsImport), 1);
    missingMetricsImport.remove(metricsImport);
    QVERIFY2(!matchesAspectIndependentBackgroundShadow(originalCustom,
                                                       missingMetricsImport,
                                                       originalLayered,
                                                       originalMetrics,
                                                       originalShadowed),
             "removing the effect-metrics import must fail the guard");

    QString divergentRenderer = originalShadowed;
    divergentRenderer.replace(QStringLiteral(
        "EffectMetrics.shadowPaddingFor("), QStringLiteral("Math.ceil("));
    QVERIFY2(!matchesAspectIndependentBackgroundShadow(originalCustom,
                                                       originalEffect,
                                                       originalLayered,
                                                       originalMetrics,
                                                       divergentRenderer),
             "giving the renderer a private padding formula must fail the guard");
}

void SourceGuardTest::iconResizeAnimation_keepsSingleAuthority()
{
    const QString privateMetrics = readFile(QStringLiteral(
        "containment/package/contents/ui/abilities/privates/MetricsPrivate.qml"));
    const QString iconBehavior = functionBody(privateMetrics,
                                              QStringLiteral("Behavior on iconSize"));
    QVERIFY2(!iconBehavior.isEmpty(), "icon-size Behavior not found");
    QVERIFY2(iconBehavior.contains(QStringLiteral("SmoothedAnimation")),
             "icon resizing must preserve velocity when its target changes");
    QVERIFY2(iconBehavior.contains(QStringLiteral(
                 "velocity: 240 / Math.max(animations.speedFactor.current, 0.01)")),
             "icon resizing must keep a distance-independent velocity tied to the animation speed");
    QVERIFY2(!iconBehavior.contains(QStringLiteral("NumberAnimation")),
             "fixed-duration NumberAnimation makes resize speed depend on distance");

    const QString marginBehavior = functionBody(privateMetrics, QStringLiteral("margin {"));
    QVERIFY2(!marginBehavior.contains(QStringLiteral("Behavior on length"))
             && !marginBehavior.contains(QStringLiteral("Behavior on tailThickness"))
             && !marginBehavior.contains(QStringLiteral("Behavior on headThickness")),
             "derived margins must not start animations that chase iconSize on every frame");
    QVERIFY2(!privateMetrics.contains(QStringLiteral("padding {\n        Behavior on length")),
             "derived length padding must not animate an already-animated icon size");

    const QString metrics = stripped(readFile(QStringLiteral(
        "containment/package/contents/ui/abilities/Metrics.qml")));
    QVERIFY2(metrics.contains(QStringLiteral(
                 "margin.tailThickness:marginMinThickness+fraction.thicknessMargin*Math.max(0,iconSize-marginMinThickness)"))
             && metrics.contains(QStringLiteral(
                 "background.totals.visualThickness-iconSize-margin.tailThickness")),
             "thickness margins must derive from the animated iconSize value");
}

void SourceGuardTest::themeAwareIconRenderTest_keepsLifecycleContract()
{
    const QString testSource = readFile(QStringLiteral("tests/themeawareicontest.cpp"));
    const QString cmakeSource = readFile(QStringLiteral("tests/CMakeLists.txt"));

    QVERIFY2(matchesThemeAwareIconTestLifecycle(testSource, cmakeSource),
             "ThemeAwareIcon rendering must keep one QML engine and synchronous "
             "offscreen software teardown");
}

void SourceGuardTest::themeAwareIconRenderTest_sourceGuardRejectsControlledMutations()
{
    const QString testSource = readFile(QStringLiteral("tests/themeawareicontest.cpp"));
    const QString cmakeSource = readFile(QStringLiteral("tests/CMakeLists.txt"));
    QVERIFY(matchesThemeAwareIconTestLifecycle(testSource, cmakeSource));

    QString defaultEngine = testSource;
    const QRegularExpression sharedView(QStringLiteral(
        "QQuickView\\s+([A-Za-z_][A-Za-z0-9_]*)"
        "\\(m_engine\\.get\\(\\),\\s*nullptr\\);"));
    const QRegularExpressionMatch sharedViewMatch =
        sharedView.match(defaultEngine);
    QVERIFY(sharedViewMatch.hasMatch());
    defaultEngine.replace(
        sharedViewMatch.capturedStart(),
        sharedViewMatch.capturedLength(),
        QStringLiteral("QQuickView differentlyNamedView;"));
    QVERIFY2(!matchesThemeAwareIconTestLifecycle(defaultEngine, cmakeSource),
             "default-constructed views must fail the one-engine lifecycle guard");

    QString lateBasicLoop = testSource;
    const QString directLoopBeforeApplication = QStringLiteral(
        "qputenv(\"QSG_RENDER_LOOP\", \"basic\");\n\n"
        "    QGuiApplication app(argc, argv);");
    QCOMPARE(lateBasicLoop.count(directLoopBeforeApplication), 1);
    lateBasicLoop.replace(
        directLoopBeforeApplication,
        QStringLiteral(
            "QGuiApplication app(argc, argv);\n\n"
            "    qputenv(\"QSG_RENDER_LOOP\", \"basic\");"));
    QVERIFY2(!matchesThemeAwareIconTestLifecycle(lateBasicLoop, cmakeSource),
             "render-loop selection after QGuiApplication must fail the lifecycle guard");

    QString wrongCTestTarget = cmakeSource;
    QCOMPARE(wrongCTestTarget.count(QStringLiteral(
                 ";QSG_RENDER_LOOP=basic")),
             1);
    wrongCTestTarget.remove(QStringLiteral(";QSG_RENDER_LOOP=basic"));
    wrongCTestTarget.append(QStringLiteral(
        "\nset_tests_properties(sourceguardtest PROPERTIES "
        "ENVIRONMENT \"QSG_RENDER_LOOP=basic\")\n"));
    QVERIFY2(!matchesThemeAwareIconTestLifecycle(testSource, wrongCTestTarget),
             "render-loop selection on another CTest target must fail the lifecycle guard");
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
    QVERIFY2(matchesReservationOutputAuthorityRoute(systemCollector),
             "reservation validation must use the synchronously committed layer-shell output");
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

    QVERIFY(matchesReservationOutputAuthorityRoute(systemCollector));
    QString staleOutput = normalizedCode(systemCollector);
    QCOMPARE(staleOutput.count(QStringLiteral(
        "layerShell->screen()")), 1);
    staleOutput.replace(
        QStringLiteral("layerShell->screen()"),
        QStringLiteral("reservation->screen()"));
    QVERIFY2(!matchesReservationOutputAuthorityRoute(staleOutput),
             "restoring the stale QWindow output must fail the collector guard");

    const QString dataCollector = normalizedCode(functionBody(
        source, QStringLiteral("QString collectDockSystemData(")));
    QVERIFY2(dataCollector.contains(QStringLiteral(
                 "returnsnapshot?serializeDockSystemSnapshot(*snapshot):QString();")),
             "malformed lineage must refuse the complete query instead of serializing partial JSON");
}

void SourceGuardTest::dockSystemTransitionCollection_keepsAuthoritativeRouting()
{
    const QString collector =
        functionBody(
            readFile(
                QStringLiteral(
                    "app/dbusreports.cpp")),
            QStringLiteral(
                "collectDockSystemSnapshot("));
    QVERIFY2(
        !collector.isEmpty(),
        "collectDockSystemSnapshot not found");
    QVERIFY2(
        matchesTransitionSnapshotRoute(
            collector),
        "schema 9 transition fields must read their per-view authorities without rounding and fail closed");
}

void SourceGuardTest::dockSystemTransitionCollection_rejectsControlledMutations()
{
    const QString collector =
        functionBody(
            readFile(
                QStringLiteral(
                    "app/dbusreports.cpp")),
            QStringLiteral(
                "collectDockSystemSnapshot("));
    QVERIFY(
        matchesTransitionSnapshotRoute(
            collector));

    QString inferredPopupPreference =
        normalizedCode(collector);
    QCOMPARE(
        inferredPopupPreference.count(
            QStringLiteral(
                "view->containment()->containmentDisplayHints().testFlag("
                "Plasma::Types::ContainmentPrefersFloatingApplets)")),
        1);
    inferredPopupPreference.replace(
        QStringLiteral(
            "view->containment()->containmentDisplayHints().testFlag("
            "Plasma::Types::ContainmentPrefersFloatingApplets)"),
        QStringLiteral(
            "transition->floatingAppletPopupsPreferred()"));
    QVERIFY2(
        !matchesTransitionSnapshotRoute(
            inferredPopupPreference),
        "inferring the popup hint from its target must fail the live readback guard");

    QString inferredEligibility =
        normalizedCode(collector);
    QCOMPARE(
        inferredEligibility.count(
            QStringLiteral(
                "transition->floatingPanelEligible()")),
        1);
    inferredEligibility.replace(
        QStringLiteral(
            "transition->floatingPanelEligible()"),
        QStringLiteral(
            "view->isFloatingPanel()"));
    QVERIFY2(
        !matchesTransitionSnapshotRoute(
            inferredEligibility),
        "inferring eligibility outside FloatingTransition must fail the collector guard");

    QString controllerOwnedTouchCount =
        normalizedCode(collector);
    QCOMPARE(
        controllerOwnedTouchCount.count(
            QStringLiteral(
                "record.touchingWindowCount="
                "*touchingWindowCount;")),
        1);
    controllerOwnedTouchCount.replace(
        QStringLiteral(
            "record.touchingWindowCount="
            "*touchingWindowCount;"),
        QStringLiteral(
            "record.touchingWindowCount="
            "transition->touchingWindowCount();"));
    QVERIFY2(
        !matchesTransitionSnapshotRoute(
            controllerOwnedTouchCount),
        "bypassing tracker-authority agreement must fail the collector guard");

    QString inferredGeometryRevision =
        normalizedCode(collector);
    QCOMPARE(
        inferredGeometryRevision.count(
            QStringLiteral(
                "transition->geometryRevision()")),
        1);
    inferredGeometryRevision.replace(
        QStringLiteral(
            "transition->geometryRevision()"),
        QStringLiteral("0"));
    QVERIFY2(
        !matchesTransitionSnapshotRoute(
            inferredGeometryRevision),
        "inventing a stable-geometry revision must fail the collector guard");

    QString roundedGeometry =
        normalizedCode(collector);
    QCOMPARE(
        roundedGeometry.count(
            QStringLiteral(
                "transition->currentVisibleGeometry()")),
        2);
    roundedGeometry.replace(
        QStringLiteral(
            "transition->currentVisibleGeometry()"),
        QStringLiteral(
            "transition->currentVisibleGeometry().toRect()"));
    QVERIFY2(
        !matchesTransitionSnapshotRoute(
            roundedGeometry),
        "rounding qreal transition geometry must fail the collector guard");

    QString positionerIdentity =
        normalizedCode(collector);
    QCOMPARE(
        positionerIdentity.count(
            QStringLiteral(
                "identities->tokenFor(transition)")),
        1);
    positionerIdentity.replace(
        QStringLiteral(
            "identities->tokenFor(transition)"),
        QStringLiteral(
            "identities->tokenFor(view->positioner())"));
    QVERIFY2(
        !matchesTransitionSnapshotRoute(
            positionerIdentity),
        "reusing the Positioner token must fail the collector guard");

    QString inferredStableMargin =
        normalizedCode(collector);
    QCOMPARE(
        inferredStableMargin.count(
            QStringLiteral(
                "physicalLayerShellMarginAtEdge("
                "record.layerShellMargins,record.edge)")),
        1);
    inferredStableMargin.replace(
        QStringLiteral(
            "physicalLayerShellMarginAtEdge("
            "record.layerShellMargins,record.edge)"),
        QStringLiteral("0"));
    QVERIFY2(
        !matchesTransitionSnapshotRoute(
            inferredStableMargin),
        "inventing a stable margin instead of reading layer-shell state must fail the collector guard");

    QString swappedRevisionAuthorities =
        normalizedCode(collector);
    QCOMPARE(
        swappedRevisionAuthorities.count(
            QStringLiteral(
                "view->positioner()->"
                "surfaceGeometryPublicationRevision()")),
        1);
    swappedRevisionAuthorities.replace(
        QStringLiteral(
            "view->positioner()->"
            "surfaceGeometryPublicationRevision()"),
        QStringLiteral(
            "view->layerShellConfigureRequestRevision()"));
    QVERIFY2(
        !matchesTransitionSnapshotRoute(
            swappedRevisionAuthorities),
        "reading both churn counters from the View must fail the collector guard");

    QString validationBypass =
        normalizedCode(collector);
    QCOMPARE(
        validationBypass.count(
            QStringLiteral(
                "dockTransitionRecordsAgree(snapshot)")),
        1);
    validationBypass.replace(
        QStringLiteral(
            "dockTransitionRecordsAgree(snapshot)"),
        QStringLiteral("true"));
    QVERIFY2(
        !matchesTransitionSnapshotRoute(
            validationBypass),
        "bypassing transition consistency validation must fail the collector guard");
}

void SourceGuardTest::reservationPublication_keepsFailureAtomicRoute()
{
    const QString header = readFile(
        QStringLiteral("app/view/visibilitymanager.h"));
    const QString source = readFile(
        QStringLiteral("app/view/visibilitymanager.cpp"));
    QVERIFY2(
        matchesReservationPublicationCommitRoute(
            header,
            source),
        "VisibilityManager must commit member struts only after the coordinator accepts publication or removal");

    QString earlyCommit = source;
    earlyCommit.replace(
        QStringLiteral(
            "return m_wm->setViewStruts("),
        QStringLiteral(
            "m_wm->setViewStruts("));
    QVERIFY2(
        !matchesReservationPublicationCommitRoute(
            header,
            earlyCommit),
        "discarding the publication result must fail the reservation commit guard");

    QString staleCandidate = source;
    staleCandidate.replace(
        QStringLiteral(
            "target.struts,"),
        QStringLiteral(
            "m_reservationPublication.publishedStruts(),"));
    QVERIFY2(
        !matchesReservationPublicationCommitRoute(
            header,
            staleCandidate),
        "publishing the old acknowledged rectangle must fail the reservation commit guard");

    QString ungatedMode = source;
    ungatedMode.replace(
        QStringLiteral(
            "if (m_mode == Types::AlwaysVisible\n"
            "            && m_strutsThickness > 0"),
        QStringLiteral(
            "if (m_strutsThickness > 0"));
    QVERIFY2(
        !matchesReservationPublicationCommitRoute(
            header,
            ungatedMode),
        "removing the AlwaysVisible gate must fail the reservation commit guard");
}

void SourceGuardTest::reservationPublication_usesLayerShellOutputIdentity()
{
    const QString coordinator = readFile(QStringLiteral(
        "app/view/helpers/screenspacereservationcoordinator.cpp"));
    const QString visibility = readFile(QStringLiteral(
        "app/view/visibilitymanager.cpp"));
    QVERIFY2(
        matchesReservationMemberOutputAuthorityRoute(
            coordinator,
            visibility),
        "same-geometry output migration must use LayerShellQt's committed output identity at both publication boundaries");

    QString staleCoordinator = coordinator;
    staleCoordinator.replace(
        QStringLiteral(
            "reservationOutputForView(view)"),
        QStringLiteral(
            "view.screen()"));
    QVERIFY2(
        !matchesReservationMemberOutputAuthorityRoute(
            staleCoordinator,
            visibility),
        "restoring the coordinator QWindow output must fail the member-output guard");

    QString staleVisibility = visibility;
    staleVisibility.replace(
        QStringLiteral(
            "layerShell->screen()"),
        QStringLiteral(
            "m_latteView->screen()"));
    QVERIFY2(
        !matchesReservationMemberOutputAuthorityRoute(
            coordinator,
            staleVisibility),
        "restoring the VisibilityManager QWindow output must fail the member-output guard");
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
