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
//   * Dock background rendering: custom shadows use one fixed-pixel effect
//     footprint on both axes and publish that footprint to geometry owners
//   * Dock resize animation: icon size is the only animation authority and
//     derived margins follow it without nested per-frame retargeting
//   * Floating-panel acceptance: recipe 071 keeps a partial Justify QWindow,
//     reservation, applet span, and physical-publication revisions stable
//     through both directions and a rapid reversal storm
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
        const QString backgroundTotals = normalizedCode(backgroundTotalsSource);
        const QString viewHeader = normalizedCode(viewHeaderSource);
        const QString viewImplementation =
            normalizedCode(viewImplementationSource);

        return main.contains(QStringLiteral(
                   "readonlypropertyboolfloatingTransitionEligible:"
                   "latteView&&latteView.floatingPanelConfigured"
                   "&&Plasmoid.configuration.hideFloatingGapForMaximized"
                   "&&latteView.visibility"
                   "&&latteView.visibility.mode===LatteCore.Types.AlwaysVisible"))
            && !main.contains(QStringLiteral(
                   "floatingTransitionEligible:behaveAsPlasmaPanel"
                   "&&latteView.visibility.mode===LatteCore.Types.WindowsGoBelow"))
            && main.contains(QStringLiteral(
                   "propertyrealmaxLengthPerCentage:behaveAsPlasmaPanel"
                   "?Plasmoid.configuration.maxLength"))
            && bindings.contains(QStringLiteral(
                   "property:\"screenEdgeMarginEnabled\""
                   "when:latteViewvalue:root.screenEdgeMarginEnabled"))
            && visibility.contains(QStringLiteral(
                   "property:\"eligible\""
                   "when:root.latteView&&root.latteView.floatingTransition"
                   "value:root.floatingTransitionEligible"))
            && visibility.contains(QStringLiteral(
                   "property:\"animationDuration\""
                   "when:root.latteView&&root.latteView.floatingTransition"
                   "value:manager.animationSpeed"))
            && bindings.contains(QStringLiteral(
                   "property:\"strutsThickness\""
                   "when:latteView&&latteView.visibility"
                   "value:{if(root.behaveAsPlasmaPanel)"
                   "{returnvisibilityManager.thicknessAsPanel;}"))
            && visibility.contains(QStringLiteral(
                   "functionupdateFloatingTransition():void"
                   "{if(!latteView||!latteView.floatingTransition){return;}"))
            && visibility.contains(QStringLiteral(
                   "if(root.floatingGapIsAttached)"
                   "{latteView.floatingTransition.requestAttached();}"
                   "else{latteView.floatingTransition.requestFloated();}"))
            && visibility.contains(QStringLiteral(
                   "functiononEligibleChanged()"
                   "{manager.updateFloatingTransition();}"))
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
                   "mask.screenEdge:(!root.screenEdgeMarginEnabled"
                   "||(!metrics.stablePanelEnvelope&&root.hideThickScreenGap))"
                   "?0:Plasmoid.configuration.screenEdgeMargin"))
            && backgroundTotals.contains(QStringLiteral(
                   "property:\"minThickness\""
                   "when:totalsItem.stablePanelEnvelope"
                   "||!(hideThickScreenGap||hideLengthScreenGaps)"))
            && viewHeader.contains(QStringLiteral(
                   "Q_PROPERTY(boolfloatingPanelConfigured"
                   "READisFloatingPanel"
                   "NOTIFYfloatingPanelConfiguredChanged)"))
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
                   "\"appletsLayoutGeometry\","
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
                   "expected_h=$((screen_h-stable_reservation_depth))"))
            && !code.contains(QStringLiteral("max_strut<base_strut"))
            && !code.contains(QStringLiteral("reservation_ms"));
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
                "record.floatingPanelConfigured="
                "view->isFloatingPanel();"))
            && code.contains(QStringLiteral(
                "record.floatingPanelEligible="
                "transition->eligible();"))
            && code.contains(QStringLiteral(
                "record.transitionProgress="
                "transition->floatingness();"))
            && code.contains(QStringLiteral(
                "record.transitionGeometryRevision="
                "transition->geometryRevision();"))
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
                "if(!dockTransitionRecordsAgree(snapshot)){"
                "qCritical()<<\"dbusreports:refusingdock-system"
                "snapshotwhosetransitionrecordsdisagree\";"
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
                    "void VisibilityManager::updateStrutsBasedOnLayoutsAndActivities")));
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
                "m_timerBlockStrutsUpdate.start();")) == 2
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
                    "void VisibilityManager::updateStrutsBasedOnLayoutsAndActivities")));
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
    void stableFloatingPanelE2e_keepsCanvasAndRevisionsFixed();
    void floatingPresentationConsumers_keepSingleAuthority();
    void panelToDockInputHandoff_bypassesOrdinaryAnimationGate();
    void panelToDockInputHandoff_rejectsMissingDirectWrite();
    void dockBackgroundFit_includesJustifyDockMode();
    void dockBackgroundFit_sourceGuardsRejectBypasses();
    void appletBudget_excludesInternalPaddingButNotShadows();
    void appletBudget_sourceGuardRejectsShadowSubtraction();
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
        "&& latteView.floatingPanelConfigured");
    QCOMPARE(main.count(eligibility), 1);
    main.replace(eligibility,
                 QStringLiteral(
                     "readonly property bool floatingTransitionEligible: "
                     "latteView\n"
                     "                                                       "
                     "&& screenEdgeMarginEnabled"));

    QVERIFY2(!matchesStableFloatingPanelQmlContract(
                 main, bindings, visibility, layouts, metrics, backgroundTotals,
                 viewHeader, viewImplementation),
             "QML eligibility must not diverge from the C++ positive-gap "
             "configuration authority");
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
        "layouts:layoutsContainerwindow:latteView}")));
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
        "schema 6 transition fields must read the per-view controller without rounding and fail closed");
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
                "transition->eligible()")),
        1);
    inferredEligibility.replace(
        QStringLiteral(
            "transition->eligible()"),
        QStringLiteral(
            "view->isFloatingPanel()"));
    QVERIFY2(
        !matchesTransitionSnapshotRoute(
            inferredEligibility),
        "inferring eligibility outside FloatingTransition must fail the collector guard");

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
