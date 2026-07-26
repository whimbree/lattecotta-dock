/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Direct contracts for identity and edit-session boundaries whose owning
//! Corona and config-window graphs require a running Plasma shell. Pure state
//! is tested separately under sanitizers; this guard pins its production use.

#include <QFile>
#include <QRegularExpression>
#include <QString>
#include <QtTest>

class DockIdentityContractTest : public QObject
{
    Q_OBJECT

private:
    static QString readFile(const QString &relativePath)
    {
        QFile file(QStringLiteral("%1/%2").arg(QStringLiteral(REPO_ROOT), relativePath));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return {};
        }

        return QString::fromUtf8(file.readAll());
    }

    static QString functionBody(const QString &source, const QString &signature)
    {
        const int signatureStart = source.indexOf(signature);
        if (signatureStart < 0) {
            return {};
        }

        const int openingBrace = source.indexOf(QLatin1Char('{'), signatureStart + signature.size());
        if (openingBrace < 0) {
            return {};
        }

        int depth = 0;
        for (int index = openingBrace; index < source.size(); ++index) {
            if (source.at(index) == QLatin1Char('{')) {
                ++depth;
            } else if (source.at(index) == QLatin1Char('}') && --depth == 0) {
                return source.mid(openingBrace, index - openingBrace + 1);
            }
        }

        return {};
    }

    static QString normalized(const QString &source)
    {
        QString code = source;
        code.remove(QRegularExpression(QStringLiteral("/\\*[\\s\\S]*?\\*/")));
        code.remove(QRegularExpression(QStringLiteral("//[^\\n]*")));
        code.remove(QRegularExpression(QStringLiteral("\\s+")));
        return code;
    }

private Q_SLOTS:
    void retargetIsLatestRequestOnlyAndEndsOldSessionFirst();
    void relationshipActionsGuardEveryProductionBoundary();
    void duplicateNormalizesRelationshipBeforeImport();
    void cloneEditRequestsResolveOneOriginalTarget();
    void cloneDestructionUnregistersMembership();
    void outputRetargetReplacesGeometryConnection();
    void relocationHandoffStopsOldRevealBeforeClaim();
    void relocationCompletionRejectsSupersededGeneration();
    void geometrySettlementIncludesDeferredWork();
    void containmentRemovalCommitsWithoutNotificationClose();
    void ignoredWindowCleanupRetainsOtherOwners();
    void persistentMenuIdentitySurvivesRuntimeGap();
    void geometryDiagnosticsReadEachViewsSizingAuthority();
    void appletMutationsUseRelationshipBoundary();
    void startupValidatesRelationshipGraphBeforeConstruction();
    void linkedRootRemovalIsRefusedAtEveryBoundary();
    void runtimeRecreationRebindsWholeRelationship();
    void crossLayoutMovesRevalidateBeforeImport();
    void clipboardCopyBreaksLinkedLineage();
    void refusedLayoutMoveCancelsRelocation();
    void outputEligibilityUsesPersistentPlacementAuthority();
    void linkedAppletGeometryRemainsPerView();
    void numericPlacementPinsStableOutputIdentity();
    void relocationCompletionCommitsPlacementTransaction();
};

void DockIdentityContractTest::relationshipActionsGuardEveryProductionBoundary()
{
    struct Boundary {
        QString signature;
        QString action;
        QString effect;
    };

    const QString coronaSource = readFile(QStringLiteral("app/lattecorona.cpp"));
    const QList<Boundary> coronaBoundaries{
        {QStringLiteral("void Corona::duplicateView"), QStringLiteral("Action::Duplicate"), QStringLiteral("view->duplicateView();")},
        {QStringLiteral("void Corona::createLinkedView"), QStringLiteral("Action::CreateLinked"), QStringLiteral("view->createLinkedView(screenId,static_cast<Plasma::Types::Location>(edge));")},
        {QStringLiteral("void Corona::setViewPlacement"), QStringLiteral("Action::Relocate"), QStringLiteral("view->positioner()->setNextLocation")},
        {QStringLiteral("void Corona::exportViewTemplate"), QStringLiteral("Action::ExportTemplate"), QStringLiteral("view->exportTemplate();")},
        {QStringLiteral("void Corona::moveViewToLayout"), QStringLiteral("Action::MoveToLayout"), QStringLiteral("view->positioner()->setNextLayout")},
        {QStringLiteral("void Corona::removeView"), QStringLiteral("Action::Remove"), QStringLiteral("view->removeView();")},
    };

    for (const auto &boundary : coronaBoundaries) {
        const QString body = normalized(functionBody(coronaSource, boundary.signature));
        const int policy = body.indexOf(QStringLiteral("ViewActionPolicy::permits(role,ViewActionPolicy::") + boundary.action);
        const int effect = body.indexOf(boundary.effect);
        QVERIFY2(policy >= 0 && effect > policy, qPrintable(boundary.signature));
    }

    const QString viewSource = readFile(QStringLiteral("app/view/view.cpp"));
    const QList<Boundary> viewBoundaries{
        {QStringLiteral("void View::duplicateView"), QStringLiteral("Action::Duplicate"), QStringLiteral("createViewFromTemplate(storedTmpViewFilepath,TemplateImportRelationship::IndependentSnapshot);")},
        {QStringLiteral("void View::createLinkedView(const int"), QStringLiteral("Action::CreateLinked"), QStringLiteral("m_layout->newView(linked);")},
        {QStringLiteral("void View::exportTemplate"), QStringLiteral("Action::ExportTemplate"), QStringLiteral("exportDlg->show();")},
        {QStringLiteral("void View::removeView"), QStringLiteral("Action::Remove"), QStringLiteral("removeAct->trigger();")},
    };

    for (const auto &boundary : viewBoundaries) {
        const QString body = normalized(functionBody(viewSource, boundary.signature));
        const int policy = body.indexOf(QStringLiteral("ViewActionPolicy::permits(role,ViewActionPolicy::") + boundary.action);
        const int effect = body.indexOf(boundary.effect);
        QVERIFY2(policy >= 0 && effect > policy, qPrintable(boundary.signature));
    }
}

void DockIdentityContractTest::duplicateNormalizesRelationshipBeforeImport()
{
    const QString viewSource = readFile(QStringLiteral("app/view/view.cpp"));
    const QString create = normalized(functionBody(viewSource, QStringLiteral("void View::createViewFromTemplate")));

    const int independent = create.indexOf(QStringLiteral("relationship==TemplateImportRelationship::IndependentSnapshot"));
    const int normalize = create.indexOf(QStringLiteral("templateviews[0].toIndependentSnapshot()"), independent);
    const int import = create.indexOf(QStringLiteral("m_layout->newView(nextdata);"), normalize);

    QVERIFY2(independent >= 0 && normalize > independent && import > normalize,
             "live-view duplication must normalize relationship state before import");

    const QString settingsSource = readFile(QStringLiteral("app/settings/viewsdialog/viewscontroller.cpp"));
    const QString duplicateSelected = normalized(functionBody(settingsSource, QStringLiteral("void Views::duplicateSelectedViews")));
    const int settingsNormalize = duplicateSelected.indexOf(QStringLiteral("selectedviews[i].toIndependentSnapshot()"));
    const int settingsAppend = duplicateSelected.indexOf(QStringLiteral("appendViewFromViewTemplate(duplicatedview);"), settingsNormalize);
    QVERIFY2(settingsNormalize >= 0 && settingsAppend > settingsNormalize,
             "layouts-dialog duplication must normalize relationship state before import");

    const QString menuSource = readFile(QStringLiteral("containmentactions/contextmenu/menu.cpp"));
    const QString visibility = normalized(functionBody(menuSource, QStringLiteral("void Menu::updateVisibleActions")));
    const int cloneBranch = visibility.indexOf(QStringLiteral("if(m_view.isCloned)"));
    const int branchEnd = visibility.indexOf(QLatin1Char('}'), cloneBranch);
    const QString cloneActions = visibility.mid(cloneBranch, branchEnd - cloneBranch);
    QVERIFY(!cloneActions.contains(QStringLiteral("DUPLICATEVIEWACTION]->setVisible(false)")));
    QVERIFY(cloneActions.contains(QStringLiteral("EXPORTVIEWTEMPLATEACTION]->setVisible(false)")));
    QVERIFY(cloneActions.contains(QStringLiteral("MOVEVIEWACTION]->setVisible(false)")));
    QVERIFY(cloneActions.contains(QStringLiteral("REMOVEVIEWACTION]->setVisible(m_view.isExplicitlyLinked)")));
}

void DockIdentityContractTest::numericPlacementPinsStableOutputIdentity()
{
    const QString coronaSource = readFile(QStringLiteral("app/lattecorona.cpp"));
    const QString placement = normalized(functionBody(
        coronaSource, QStringLiteral("void Corona::setViewPlacement")));

    const int resolveConnector = placement.indexOf(
        QStringLiteral("m_screenPool->connector(screenId)"));
    const int position = placement.indexOf(
        QStringLiteral("setNextLocation(QString(),Types::SingleScreenGroup,"
                       "connector"),
        resolveConnector);

    QVERIFY2(resolveConnector >= 0 && position > resolveConnector,
             "numeric placement must pin the supplied stable output identity");
    QVERIFY2(!placement.contains(QStringLiteral("ONPRIMARYNAME")),
             "numeric placement must not silently become follow-primary policy");
}

void DockIdentityContractTest::relocationCompletionCommitsPlacementTransaction()
{
    const QString positionerSource = readFile(
        QStringLiteral("app/view/positioner.cpp"));
    const QString completion = normalized(functionBody(
        positionerSource,
        QStringLiteral("void Positioner::onLastRepositionApplyEvent")));
    const int finalSolve = completion.indexOf(
        QStringLiteral("solveAndApplyGeometry(true)"));
    const int reservation = completion.indexOf(
        QStringLiteral("publishReservationAfterAppliedPlacement()"),
        finalSolve);
    const int applied = completion.indexOf(
        QStringLiteral("m_appliedRelocationGeneration=m_relocationGeneration"),
        reservation);
    const int committed = completion.indexOf(
        QStringLiteral("Q_EMITplacementTransactionCommitted()"),
        applied);
    const int remap = completion.indexOf(
        QStringLiteral("m_view->showAppliedLayerShellPlacement()"),
        committed);
    const int revealGuard = completion.indexOf(
        QStringLiteral("setInRelocationShowing(true)"), remap);
    const int reveal = completion.indexOf(
        QStringLiteral("Q_EMITshowingAfterRelocationFinished()"),
        revealGuard);

    QVERIFY2(finalSolve >= 0 && reservation > finalSolve
                 && applied > reservation && committed > applied
                 && remap > committed && revealGuard > remap
                 && reveal > revealGuard,
             "relocation must apply surface and reservation ownership before generation commit, remap, and reveal");

    const QString setScreen = normalized(functionBody(
        positionerSource,
        QStringLiteral("bool Positioner::setScreenToFollow")));
    const int retire = setScreen.indexOf(
        QStringLiteral("beginPlacementTransaction(true)"));
    const int physicalMove = setScreen.indexOf(
        QStringLiteral("m_view->moveToScreen(scr)"), retire);
    const int screenCompletion = setScreen.indexOf(
        QStringLiteral("finishPendingScreenPlacementIfApplied()"),
        physicalMove);
    QVERIFY2(retire >= 0 && physicalMove > retire
                 && screenCompletion > physicalMove,
             "direct output reassignment must retire reservation ownership before moving the surface");

    const QString transaction = normalized(functionBody(
        positionerSource,
        QStringLiteral("void Positioner::initSignalingForLocationChangeSliding")));
    const int begin = transaction.indexOf(
        QStringLiteral("beginPlacementTransaction("));
    const int guard = transaction.indexOf(
        QStringLiteral("QScopedValueRollbackapplyingPlacement"),
        begin);
    const int output = transaction.indexOf(
        QStringLiteral("applyOutputPlacement("), guard);
    const int edge = transaction.indexOf(
        QStringLiteral("m_view->setLocation("),
        output);
    const int alignment = transaction.indexOf(
        QStringLiteral("m_view->setAlignment("),
        edge);
    QVERIFY2(begin >= 0 && guard > begin && output > guard
                 && edge > output && alignment > edge,
             "compound placement must retire ownership and stage output, edge, and alignment under one observer guard");

    const QString visibilitySource = readFile(
        QStringLiteral("app/view/visibilitymanager.cpp"));
    const QString publish = normalized(functionBody(
        visibilitySource,
        QStringLiteral("bool VisibilityManager::publishReservationAfterAppliedPlacement")));
    const QString update = normalized(functionBody(
        visibilitySource,
        QStringLiteral("bool VisibilityManager::updateStrutsBasedOnLayoutsAndActivities")));
    QVERIFY2(publish.contains(QStringLiteral(
                 "updateStrutsBasedOnLayoutsAndActivities(forceUpdate,ReservationUpdateContext::AppliedPlacement)")),
             "the post-placement publication boundary must carry its authoritative context");
    const int authoritativeContext = update.indexOf(QStringLiteral(
        "context==ReservationUpdateContext::AppliedPlacement"));
    const int ordinaryGenerationGuard = update.indexOf(QStringLiteral(
        "if(!placementIsAuthoritative&&("), authoritativeContext);
    QVERIFY2(authoritativeContext >= 0
                 && ordinaryGenerationGuard > authoritativeContext,
             "only ordinary reservation requests may defer on an unapplied placement generation");
}

void DockIdentityContractTest::retargetIsLatestRequestOnlyAndEndsOldSessionFirst()
{
    const QString source = readFile(QStringLiteral("app/view/settings/primaryconfigview.cpp"));
    const QString setParent = normalized(functionBody(source, QStringLiteral("void PrimaryConfigView::setParentView")));
    const QString schedule = normalized(functionBody(source, QStringLiteral("void PrimaryConfigView::scheduleRetarget")));
    const QString cancel = normalized(functionBody(source, QStringLiteral("void PrimaryConfigView::cancelPendingRetarget")));
    const QString ownership = normalized(functionBody(source, QStringLiteral("bool PrimaryConfigView::hasEditRequestFor")));
    const QString show = normalized(functionBody(source, QStringLiteral("void PrimaryConfigView::showConfigWindow")));

    QVERIFY2(!setParent.isEmpty() && !schedule.isEmpty() && !cancel.isEmpty() && !ownership.isEmpty() && !show.isEmpty(),
             "PrimaryConfigView retarget functions were not found");

    QVERIFY(setParent.contains(QStringLiteral("hideConfigWindow();scheduleRetarget(view);")));
    const int endOld = setParent.indexOf(QStringLiteral("endConfiguringSession();"));
    const int rebind = setParent.indexOf(QStringLiteral("initParentView(view);"), endOld);
    const int showNew = setParent.indexOf(QStringLiteral("showConfigWindow();"), rebind);
    QVERIFY(endOld >= 0 && rebind > endOld && showNew > rebind);

    QVERIFY(cancel.contains(QStringLiteral("m_retargetTimer.stop();")));
    QVERIFY(cancel.contains(QStringLiteral("m_retargetRequests.cancelRequest();")));
    QVERIFY(schedule.contains(QStringLiteral("token=m_pendingRetargetToken")));
    QVERIFY(schedule.contains(QStringLiteral("m_retargetRequests.consumeIfCurrent(token)")));
    QVERIFY(ownership.contains(QStringLiteral("m_pendingParentView==view")));
    QVERIFY(ownership.contains(QStringLiteral("m_retargetRequests.isTargetPending(targetKey(view))")));

    const int beginSession = show.indexOf(QStringLiteral("setUserConfiguring(true);"));
    const int visibleReturn = show.indexOf(QStringLiteral("if(isVisible())"));
    QVERIFY(beginSession >= 0 && visibleReturn > beginSession);
}

void DockIdentityContractTest::cloneEditRequestsResolveOneOriginalTarget()
{
    const QString cloneSource = readFile(QStringLiteral("app/view/clonedview.cpp"));
    const QString target = normalized(functionBody(cloneSource, QStringLiteral("View *ClonedView::configurationTargetView")));
    QVERIFY(target.contains(QStringLiteral("m_linkPlacement==Data::View::LinkPlacement::ExplicitTarget")));
    QVERIFY(target.contains(QStringLiteral("returnthis;")));
    QVERIFY(target.contains(QStringLiteral("returnm_originalView.data();")));

    const QString configure = normalized(functionBody(cloneSource, QStringLiteral("void ClonedView::showConfigurationInterface")));
    const int explicitBranch = configure.indexOf(QStringLiteral("LinkPlacement::ExplicitTarget"));
    const int localEdit = configure.indexOf(QStringLiteral("View::showConfigurationInterface(applet);"), explicitBranch);
    const int linkedEdit = configure.indexOf(QStringLiteral("configurationTargetView()"), localEdit);
    QVERIFY(explicitBranch >= 0 && localEdit > explicitBranch && linkedEdit > localEdit);

    const QString coronaSource = readFile(QStringLiteral("app/lattecorona.cpp"));
    const QString editMode = normalized(functionBody(coronaSource, QStringLiteral("void Corona::setViewEditMode")));
    const QString configureApplets = normalized(functionBody(coronaSource, QStringLiteral("void Corona::setViewConfiguringApplets")));

    QVERIFY(editMode.contains(QStringLiteral("view->configurationTargetView()")));
    QVERIFY(editMode.contains(QStringLiteral("configurationTarget->showSettingsWindow();")));
    QVERIFY(editMode.contains(QStringLiteral("!configView->hasEditRequestFor(configurationTarget)")));
    QVERIFY(!editMode.contains(QStringLiteral("configView->parentView()!=configurationTarget")));
    QVERIFY(configureApplets.contains(QStringLiteral("view->configurationTargetView()")));
    QVERIFY(configureApplets.contains(QStringLiteral("!configurationTarget->inEditMode()")));
}

void DockIdentityContractTest::cloneDestructionUnregistersMembership()
{
    const QString originalHeader = normalized(readFile(QStringLiteral("app/view/originalview.h")));
    QVERIFY(originalHeader.contains(QStringLiteral("QList<QPointer<Latte::ClonedView>>m_clones;")));
    QVERIFY(originalHeader.contains(QStringLiteral("voidforgetClone(Latte::ClonedView*view);")));

    const QString cloneSource = readFile(QStringLiteral("app/view/clonedview.cpp"));
    const QString destructor = normalized(functionBody(cloneSource, QStringLiteral("ClonedView::~ClonedView")));
    QVERIFY(destructor.contains(QStringLiteral("m_originalView->forgetClone(this);")));

    const QString originalSource = readFile(QStringLiteral("app/view/originalview.cpp"));
    const QString removeClone = normalized(functionBody(originalSource, QStringLiteral("void OriginalView::removeClone")));
    const int forget = removeClone.indexOf(QStringLiteral("forgetClone(view);"));
    const int removeContainment = removeClone.indexOf(QStringLiteral("view->layout()->removeView(view->data());"));
    QVERIFY(forget >= 0 && removeContainment > forget);
}

void DockIdentityContractTest::outputRetargetReplacesGeometryConnection()
{
    const QString source = readFile(QStringLiteral("app/view/positioner.cpp"));
    const QString setScreen = normalized(functionBody(source, QStringLiteral("bool Positioner::setScreenToFollow")));
    const int disconnectOld = setScreen.indexOf(QStringLiteral("QObject::disconnect(m_screenGeometryConnection);"));
    const int replaceConnection = setScreen.indexOf(QStringLiteral("m_screenGeometryConnection=connect(scr,&QScreen::geometryChanged"));

    QVERIFY(disconnectOld >= 0 && replaceConnection > disconnectOld);
    QCOMPARE(setScreen.count(QStringLiteral("&QScreen::geometryChanged")), 1);
}

void DockIdentityContractTest::relocationHandoffStopsOldRevealBeforeClaim()
{
    const QString source = readFile(
        QStringLiteral("containment/package/contents/ui/VisibilityManager.qml"));
    const QString handoff = normalized(functionBody(
        source, QStringLiteral("function slotHideDockDuringLocationChange")));

    const int stopOldReveal = handoff.indexOf(
        QStringLiteral("slidingAnimationAutoHiddenIn.stop();"));
    const int claimHide = handoff.indexOf(
        QStringLiteral("manager.inRelocationHiding=true;"), stopOldReveal);
    const int startHide = handoff.indexOf(
        QStringLiteral("slidingAnimationAutoHiddenOut.init();"), claimHide);
    QVERIFY2(stopOldReveal >= 0 && claimHide > stopOldReveal && startHide > claimHide,
             "a new relocation must stop the old reveal before claiming its hide transaction");
}

void DockIdentityContractTest::relocationCompletionRejectsSupersededGeneration()
{
    const QString source = readFile(QStringLiteral("app/view/positioner.cpp"));
    const QString schedule = normalized(functionBody(
        source, QStringLiteral("void Positioner::scheduleLastRepositionApplyEvent")));
    const QString setNext = normalized(functionBody(
        source, QStringLiteral("void Positioner::setNextLocation")));

    const int capture = schedule.indexOf(QStringLiteral("scheduledGeneration=m_relocationGeneration"));
    const int reject = schedule.indexOf(
        QStringLiteral("scheduledGeneration!=m_relocationGeneration"), capture);
    const int apply = schedule.indexOf(QStringLiteral("onLastRepositionApplyEvent();"), reject);
    QVERIFY2(capture >= 0 && reject > capture && apply > reject,
             "delayed relocation completion must reject an older placement generation");

    const int submit = setNext.indexOf(QStringLiteral(
        "m_placementRequests.submit("));
    const int claim = setNext.indexOf(QStringLiteral(
        "m_relocationGeneration=submission.request.token;"), submit);
    const int beginHide = setNext.indexOf(QStringLiteral("Q_EMIThidingForRelocationStarted();"));
    QVERIFY2(submit >= 0 && claim > submit && beginHide > claim,
             "a complete latest-intent generation must be claimed before asynchronous hiding begins");
}

void DockIdentityContractTest::geometrySettlementIncludesDeferredWork()
{
    const QString source = readFile(QStringLiteral("app/view/positioner.cpp"));
    const QString settled = normalized(functionBody(
        source, QStringLiteral("bool Positioner::geometryIsSettled")));

    const QStringList requiredAuthorities{
        QStringLiteral("m_relocationGeneration==m_appliedRelocationGeneration"),
        QStringLiteral("!inRelocationAnimation()"),
        QStringLiteral("!m_inRelocationShowing"),
        QStringLiteral("!m_screenSyncTimer.isActive()"),
        QStringLiteral("!m_syncGeometryTimer.isActive()"),
        QStringLiteral("!m_validateGeometryTimer.isActive()"),
        QStringLiteral("m_view->screen()==m_screenToFollow"),
    };

    for (const auto &authority : requiredAuthorities) {
        QVERIFY2(settled.contains(authority), qPrintable(authority));
    }
}

void DockIdentityContractTest::containmentRemovalCommitsWithoutNotificationClose()
{
    const QString source = readFile(QStringLiteral("app/layout/genericlayout.cpp"));
    const QString schedule = normalized(functionBody(
        source, QStringLiteral("void GenericLayout::scheduleRemovalCommit")));
    const QString cancel = normalized(functionBody(
        source, QStringLiteral("void GenericLayout::cancelRemovalCommit")));
    const QString transition = normalized(functionBody(
        source, QStringLiteral("void GenericLayout::destroyedChanged")));
    const QString finalDestruction = normalized(functionBody(
        source, QStringLiteral("void GenericLayout::containmentDestroyed")));
    const QString viewSource = readFile(QStringLiteral("app/view/view.cpp"));
    const QString removeView = normalized(functionBody(
        viewSource, QStringLiteral("void View::removeView")));
    const QString suspendView = normalized(functionBody(
        viewSource, QStringLiteral("bool View::suspendForReversibleRemoval")));
    const QString resumeView = normalized(functionBody(
        viewSource, QStringLiteral("bool View::resumeFromReversibleRemoval")));
    const QString visibilitySource = readFile(
        QStringLiteral("app/view/visibilitymanager.cpp"));
    const QString suspendVisibility = normalized(functionBody(
        visibilitySource,
        QStringLiteral("bool VisibilityManager::suspendForReversibleRemoval")));
    const QString resumeVisibility = normalized(functionBody(
        visibilitySource,
        QStringLiteral("bool VisibilityManager::resumeFromReversibleRemoval")));
    const QString commitPersistence = normalized(functionBody(
        source, QStringLiteral("bool GenericLayout::commitPreparedViewRemoval")));
    const QString undoResolution = normalized(functionBody(
        source, QStringLiteral("void GenericLayout::scheduleRemovalUndoResolution")));
    const QString removalFinalization = normalized(functionBody(
        source, QStringLiteral("void GenericLayout::scheduleRemovalFinalization")));
    const QString commitAfterDestruction = normalized(functionBody(
        source, QStringLiteral(
            "bool GenericLayout::\ncommitRemovalPersistenceAfterDestruction")));
    const QString permanentRemoval = normalized(functionBody(
        source, QStringLiteral("void GenericLayout::removeView")));
    const QString viewLookup = normalized(functionBody(
        source, QStringLiteral("Latte::View *GenericLayout::viewForContainment")));
    const QString storageSource = readFile(
        QStringLiteral("app/layouts/storage.cpp"));
    const QString storageSync = normalized(functionBody(
        storageSource,
        QStringLiteral("bool Storage::syncToLayoutFile")));
    const QString persistConfiguration = normalized(
        functionBody(
            storageSource,
            QStringLiteral(
                "bool persistConfigurationOrReportFailure")));

    QVERIFY(source.contains(QStringLiteral(
        "constexpr auto RemovalUndoWindow = std::chrono::seconds{60};")));
    QVERIFY(schedule.contains(QStringLiteral("cancelRemovalCommit(containment);")));
    QVERIFY(schedule.contains(QStringLiteral(
        "m_removalCommitTimers.value(containment)==timer")));
    const int expiryTransaction = schedule.indexOf(
        QStringLiteral(
            "m_removalTransactions.find(containment)"));
    const int queueExpiryFinalization = schedule.indexOf(
        QStringLiteral(
            "queueRemovalFinalizationIfCurrent(token)"),
        expiryTransaction);
    const int deferExpiryFinalization = schedule.indexOf(
        QStringLiteral("scheduleRemovalFinalization("),
        queueExpiryFinalization);
    QVERIFY2(expiryTransaction >= 0
                 && queueExpiryFinalization
                    > expiryTransaction
                 && deferExpiryFinalization
                    > queueExpiryFinalization,
             "root removal expiry must use the checked transaction finalizer");
    const int viewlessDestroy = schedule.indexOf(
        QStringLiteral("containment->destroy();"),
        deferExpiryFinalization);
    const int viewlessProof = schedule.indexOf(
        QStringLiteral(
            "m_containments.contains(containment)"),
        viewlessDestroy);
    QVERIFY2(viewlessDestroy > deferExpiryFinalization
                 && viewlessProof > viewlessDestroy,
             "viewless expiry must prove that direct destruction released layout ownership");
    QVERIFY(cancel.contains(QStringLiteral("m_removalCommitTimers.take(containment)")));

    const int suspend = transition.indexOf(QStringLiteral(
        "view->suspendForReversibleRemoval()"));
    const int park = transition.indexOf(QStringLiteral(
        "m_waitingLatteViews[containment]=view;"));
    const int arm = transition.indexOf(QStringLiteral(
        "scheduleRemovalCommit(containment);"), park);
    const int viewless = transition.indexOf(
        QStringLiteral("if(!view){"));
    const int viewlessReturn = transition.indexOf(
        QStringLiteral("return;"), viewless);
    const int rootTransaction = transition.indexOf(
        QStringLiteral(
            "m_removalTransactions[containment]"));
    const int queueUndo = transition.indexOf(
        QStringLiteral(
            "queueUndoResolutionIfCurrent(token)"),
        arm);
    const int deferUndo = transition.indexOf(
        QStringLiteral(
            "scheduleRemovalUndoResolution("),
        queueUndo);
    QVERIFY2(suspend >= 0 && park > suspend && arm > park,
             "remove must suspend before parking and arming its expiry");
    QVERIFY2(viewless >= 0
                 && viewlessReturn > viewless
                 && rootTransaction > viewlessReturn,
             "viewless subcontainments must not create root-view transactions");
    QVERIFY2(queueUndo > arm && deferUndo > queueUndo,
             "destroyedChanged(false) may only queue the root Undo transaction");
    QVERIFY2(!transition.contains(QStringLiteral("restoreView("))
                 && !transition.contains(QStringLiteral(
                     "syncToLayoutFile("))
                 && !transition.contains(QStringLiteral(
                     "resumeFromReversibleRemoval(")),
             "root Undo callbacks run before libplasma clears child transient state");
    QVERIFY2(!transition.contains(QStringLiteral("tombstoneViewFromSnapshot(")),
             "destroyedChanged fires before libplasma finishes transient subtree writes");
    QVERIFY(finalDestruction.contains(QStringLiteral("cancelRemovalCommit(containment);")));

    const int trigger = removeView.indexOf(QStringLiteral("removeAct->trigger();"));
    const int commit = removeView.indexOf(
        QStringLiteral("commitPreparedViewRemoval("), trigger);
    QVERIFY2(trigger >= 0 && commit > trigger,
             "the reversible persistence tombstone must commit after libplasma finishes askDestroy");
    QVERIFY(commitPersistence.contains(QStringLiteral(
        "tombstoneViewFromSnapshot(activeConfig,snapshot)")));
    const int undoQueuedTurn = undoResolution.indexOf(
        QStringLiteral("QTimer::singleShot(0,this,"));
    const int restorePersistence = undoResolution.indexOf(
        QStringLiteral("restoreView(activeConfig,snapshot)"),
        undoQueuedTurn);
    const int resume = undoResolution.indexOf(
        QStringLiteral(
            "waitingView->resumeFromReversibleRemoval()"),
        restorePersistence);
    const int reactivate = undoResolution.indexOf(
        QStringLiteral(
            "m_latteViews[containmentIdentity]=restoredView;"),
        resume);
    const int failedUndo = undoResolution.indexOf(
        QStringLiteral("scheduleRemovalFinalization("),
        resume);
    QVERIFY2(undoQueuedTurn >= 0
                 && restorePersistence > undoQueuedTurn
                 && resume > restorePersistence
                 && failedUndo > resume
                 && reactivate > resume,
             "queued Undo must restore persistence before runtime ownership and finalize every failed restore");

    const int queuedTurn = removalFinalization.indexOf(
        QStringLiteral("QTimer::singleShot(0,this,"));
    const int acceptsToken = removalFinalization.indexOf(
        QStringLiteral("liveTransaction->token()!=token"),
        queuedTurn);
    const int retireRuntime = removalFinalization.indexOf(
        QStringLiteral(
            "retireRuntimeViewForFailedRemoval(identity)"),
        acceptsToken);
    const int failedUndoDestroy = removalFinalization.indexOf(
        QStringLiteral("guardedContainment->destroy();"),
        retireRuntime);
    const int destructionProof = removalFinalization.indexOf(
        QStringLiteral(
            "!m_containments.contains(identity)"),
        failedUndoDestroy);
    const int failedUndoCommit = removalFinalization.indexOf(
        QStringLiteral("commitRemovalPersistenceAfterDestruction(memoryUsage,activeConfig,snapshot)"),
        destructionProof);
    QVERIFY2(queuedTurn >= 0 && acceptsToken > queuedTurn
                 && retireRuntime > acceptsToken
                 && failedUndoDestroy > retireRuntime
                 && destructionProof > failedUndoDestroy
                 && failedUndoCommit > destructionProof,
             "failed Undo must reject stale generations, retire runtime ownership, prove containment destruction, then tombstone");
    QVERIFY2(!viewLookup.contains(QStringLiteral("m_waitingLatteViews")),
             "suspended removal Views must not remain runtime action targets");
    QVERIFY(commitAfterDestruction.contains(QStringLiteral(
        "tombstoneViewFromSnapshot(activeConfig,snapshot)")));
    const int permanentDestroy = permanentRemoval.indexOf(
        QStringLiteral("destroyContainment(viewcontainment)"));
    const int permanentCommit = permanentRemoval.indexOf(
        QStringLiteral("commitRemovalPersistenceAfterDestruction("),
        permanentDestroy);
    const int permanentOwnershipProof = permanentRemoval.indexOf(
        QStringLiteral(
            "m_containments.contains(viewcontainment)"),
        permanentDestroy);
    QVERIFY2(permanentDestroy >= 0
                 && permanentOwnershipProof > permanentDestroy
                 && permanentCommit > permanentOwnershipProof,
             "direct permanent removal must prove runtime retirement before persistence");
    QVERIFY2(storageSync.contains(
                 QStringLiteral(
                     "persistConfigurationOrReportFailure(filePtr,"))
                 && persistConfiguration.contains(
                     QStringLiteral("if(config->sync())")),
             "multiple-layout persistence failures must reach the removal transaction");
    QVERIFY2(storageSync.contains(
                 QStringLiteral(
                     "screenGroupDerivedSubtree.contains(owner)"))
                 && storageSync.contains(
                     QStringLiteral(
                         "!screenGroupDerivedSubtree.contains(containment)")),
             "multiple-layout projection must exclude complete derived ownership subtrees");

    const int retireReservation = suspendVisibility.indexOf(
        QStringLiteral("m_reservationPublication.remove("));
    const int retireHelpers = suspendVisibility.indexOf(
        QStringLiteral("deleteEdgeGhostWindow();"), retireReservation);
    QVERIFY2(retireReservation >= 0 && retireHelpers > retireReservation,
             "reversible removal must retire reservation ownership before helper surfaces");
    const int republish = resumeVisibility.indexOf(
        QStringLiteral("updateStrutsBasedOnLayoutsAndActivities("));
    const int restoreHelpers = resumeVisibility.indexOf(
        QStringLiteral("updateKWinEdgesSupport();"), republish);
    QVERIFY2(republish >= 0 && restoreHelpers > republish,
             "Undo must republish reservation ownership before helper surfaces");

    const int suspendVisibilityOwner = suspendView.indexOf(
        QStringLiteral("m_visibility->suspendForReversibleRemoval()"));
    const int unmap = suspendView.indexOf(
        QStringLiteral("setVisible(false);"), suspendVisibilityOwner);
    QVERIFY2(suspendVisibilityOwner >= 0 && unmap > suspendVisibilityOwner,
             "the canvas must unmap only after reservation retirement");
    const int resumeVisibilityOwner = resumeView.indexOf(
        QStringLiteral("m_visibility->resumeFromReversibleRemoval()"));
    const int remap = resumeView.indexOf(
        QStringLiteral("setVisible(true);"), resumeVisibilityOwner);
    QVERIFY2(resumeVisibilityOwner >= 0 && remap > resumeVisibilityOwner,
             "the canvas must remap only after reservation publication");
}

void DockIdentityContractTest::ignoredWindowCleanupRetainsOtherOwners()
{
    const QStringList ownerFiles{
        QStringLiteral("app/infoview.cpp"),
        QStringLiteral("app/view/helpers/subwindow.cpp"),
        QStringLiteral("app/view/positioner.cpp"),
        QStringLiteral("app/view/settings/subconfigview.cpp"),
    };

    for (const auto &file : ownerFiles) {
        const QString code = normalized(readFile(file));
        QVERIFY2(code.contains(QStringLiteral("registerIgnoredWindow")), qPrintable(file));
        QVERIFY2(!QRegularExpression(QStringLiteral("(?:un)?registerIgnoredWindow\\([^,;]+\\);"))
                      .match(code).hasMatch(),
                 qPrintable(file + QStringLiteral(" has an ownerless ignored-window call")));
    }

    const QString wayland = normalized(readFile(QStringLiteral("app/wm/waylandinterface.cpp")));
    QVERIFY(wayland.contains(QStringLiteral("registerIgnoredWindow(WindowId::fromWaylandUuid(w->uuid()),w);")));
}

void DockIdentityContractTest::persistentMenuIdentitySurvivesRuntimeGap()
{
    const QString coronaSource = readFile(QStringLiteral("app/lattecorona.cpp"));
    const QString contextData = normalized(functionBody(coronaSource, QStringLiteral("QStringList Corona::contextMenuData")));

    QVERIFY(contextData.contains(QStringLiteral("boolisCloned{true};")));
    QVERIFY(contextData.contains(QStringLiteral("layout->containmentForId(containmentId)")));
    QVERIFY(contextData.contains(QStringLiteral("isClonedView(persistentContainment)")));
    QVERIFY(contextData.contains(QStringLiteral("persistentContainment->config().readEntry(\"viewType\"")));
    QVERIFY(contextData.contains(QStringLiteral("persistentContainment->config().readEntry(\"linkPlacement\"")));
    QVERIFY(contextData.contains(QStringLiteral("if(!isCloned)")));

    const QString menuSource = readFile(QStringLiteral("containmentactions/contextmenu/menu.cpp"));
    const QString parseView = normalized(functionBody(menuSource, QStringLiteral("bool Menu::updateViewData")));
    const QString populateTemplates = normalized(functionBody(menuSource, QStringLiteral("void Menu::populateViewTemplates")));
    const QString visibleActions = normalized(functionBody(menuSource, QStringLiteral("void Menu::updateVisibleActions")));

    QVERIFY(parseView.contains(QStringLiteral("m_view=ViewTypeData{};")));
    QVERIFY(parseView.contains(QStringLiteral("returnfalse;")));
    QVERIFY(parseView.contains(QStringLiteral("vdata.count()!=5")));
    QVERIFY(populateTemplates.contains(QStringLiteral("if(m_contextDataValid)")));
    QVERIFY(!populateTemplates.contains(QStringLiteral("!m_view.isCloned")));
    QVERIFY(visibleActions.contains(QStringLiteral("setVisible(m_contextDataValid&&!configuring)")));
    QVERIFY(visibleActions.contains(QStringLiteral("m_view.explicitLinkedMembersCount>0")));
    QVERIFY(visibleActions.contains(QStringLiteral("removeAction->setEnabled(!rootHasExplicitMembers)")));
}

void DockIdentityContractTest::geometryDiagnosticsReadEachViewsSizingAuthority()
{
    const QString metricsSource = normalized(readFile(
        QStringLiteral("containment/package/contents/ui/abilities/privates/MetricsPrivate.qml")));
    const QString containmentSource = normalized(readFile(
        QStringLiteral("containment/package/contents/ui/main.qml")));
    const QString reportsSource = normalized(readFile(QStringLiteral("app/dbusreports.cpp")));

    QVERIFY(metricsSource.contains(QStringLiteral("propertyintavailablePrimaryLength:0")));
    QVERIFY(containmentSource.contains(
        QStringLiteral("availablePrimaryLength:_layouter.contentsMaxLength")));
    QVERIFY(reportsSource.contains(
        QStringLiteral("readLiveProperty(metrics,\"availablePrimaryLength\")")));
    QVERIFY(!reportsSource.contains(
        QStringLiteral("readLiveProperty(editController,\"maxLength\")")));
}

void DockIdentityContractTest::appletMutationsUseRelationshipBoundary()
{
    const QString coronaSource = normalized(readFile(QStringLiteral("app/lattecorona.cpp")));
    const QString viewHeader = normalized(readFile(QStringLiteral("app/view/view.h")));
    const QString linkedRaw = readFile(QStringLiteral("app/view/clonedview.cpp"));
    const QString linkedSource = normalized(linkedRaw);
    const QString containmentInterface = normalized(readFile(QStringLiteral("app/view/containmentinterface.cpp")));
    const QString widgetExplorer = normalized(readFile(QStringLiteral("shell/package/contents/views/WidgetExplorer.qml")));
    const QString dragDrop = normalized(readFile(QStringLiteral("containment/package/contents/ui/DragDropArea.qml")));
    const QString configOverlay = normalized(readFile(QStringLiteral("containment/package/contents/ui/editmode/ConfigOverlay.qml")));
    const QString contextLayer = normalized(readFile(QStringLiteral("app/declarativeimports/contextmenulayerquickitem.cpp")));

    QVERIFY(viewHeader.contains(QStringLiteral("Q_INVOKABLEvirtualbooladdApplet(constQString&pluginId);")));
    QVERIFY(viewHeader.contains(QStringLiteral("Q_INVOKABLEvirtualboolremoveApplet(intappletId);")));
    QVERIFY(coronaSource.contains(QStringLiteral("view->addApplet(pluginId)")));
    QVERIFY(coronaSource.contains(QStringLiteral("view->removeApplet(static_cast<int>(appletId))")));
    QVERIFY(widgetExplorer.contains(QStringLiteral("latteView.addApplet(pluginName)")));
    QVERIFY(dragDrop.contains(QStringLiteral("latteView.synchronizeDroppedApplet(event.mimeData,eventx,eventy)")));
    QVERIFY(configOverlay.contains(QStringLiteral("latteView.removeApplet(currentApplet.applet.plasmoid.id)")));
    QVERIFY(contextLayer.contains(QStringLiteral("m_latteView->removeApplet(appletId)")));
    QVERIFY(contextLayer.contains(QStringLiteral("relationshipAwareRemove->setVisible(closeApplet->isVisible())")));
    QVERIFY(!containmentInterface.contains(QStringLiteral("Q_EMITapplet->appletDeleted(applet)")));
    QVERIFY(containmentInterface.contains(QStringLiteral("QMetaObject::invokeMethod(applet,\"askDestroy\",Qt::DirectConnection)")));
    QVERIFY(linkedSource.contains(QStringLiteral("destroyAppletImmediately(memberId)")));
    QVERIFY(linkedSource.contains(QStringLiteral(
        "restoreAppletFrom(originalApplet.applet,locallyOwnedAppletConfigurationKeys())")));

    const QString add = normalized(functionBody(linkedRaw, QStringLiteral("bool ClonedView::addApplet")));
    const QString remove = normalized(functionBody(linkedRaw, QStringLiteral("bool ClonedView::removeApplet")));
    const QString reorder = normalized(functionBody(linkedRaw, QStringLiteral("void ClonedView::updateOriginalAppletsOrder")));
    QVERIFY2(add.contains(QStringLiteral("m_originalView->addApplet(pluginId)")), qPrintable(add));
    QVERIFY(remove.contains(QStringLiteral("originalAppletId(appletId)")));
    QVERIFY(remove.contains(QStringLiteral("m_originalView->removeApplet(originalId)")));
    QVERIFY(reorder.contains(QStringLiteral("translateToOriginalsOrder(clonedOrder)")));
    QVERIFY(reorder.contains(QStringLiteral("m_originalView->extendedInterface()->setAppletsOrder(originalOrder)")));
}

void DockIdentityContractTest::startupValidatesRelationshipGraphBeforeConstruction()
{
    const QString source = readFile(QStringLiteral("app/layout/genericlayout.cpp"));
    const QString initialize = normalized(functionBody(source, QStringLiteral("bool GenericLayout::initContainments")));
    const QString add = normalized(functionBody(source, QStringLiteral("void GenericLayout::addView")));

    const int validate = initialize.indexOf(QStringLiteral("persistedViews.relationshipValidationError()"));
    const int construct = initialize.indexOf(QStringLiteral("addNextStartupView(pending)"), validate);
    QVERIFY2(validate >= 0 && construct > validate,
             "the complete persisted relationship graph must be validated before startup constructs a view");
    QVERIFY(add.contains(QStringLiteral("qobject_cast<Latte::OriginalView*>(view)")));
    QVERIFY(add.contains(QStringLiteral("if(!originalview)")));
}

void DockIdentityContractTest::linkedRootRemovalIsRefusedAtEveryBoundary()
{
    const QString viewSource = readFile(QStringLiteral("app/view/view.cpp"));
    const QString removeView = normalized(functionBody(viewSource, QStringLiteral("void View::removeView")));
    const int viewGuard = removeView.indexOf(QStringLiteral("||!canRemove())"));
    const int trigger = removeView.indexOf(QStringLiteral("removeAct->trigger()"));
    QVERIFY(viewGuard >= 0 && trigger > viewGuard);

    const QString layoutSource = readFile(QStringLiteral("app/layout/genericlayout.cpp"));
    const QString removePersisted = normalized(functionBody(
        layoutSource, QStringLiteral("void GenericLayout::removeView")));
    const int relationshipGuard = removePersisted.indexOf(
        QStringLiteral("currentViews.hasExplicitLinkedMembers(viewData.id)"));
    const int destroy = removePersisted.indexOf(QStringLiteral("destroyContainment(viewcontainment)"));
    QVERIFY(relationshipGuard >= 0 && destroy > relationshipGuard);

    const QString modelSource = readFile(QStringLiteral("app/settings/viewsdialog/viewsmodel.cpp"));
    const QString removeModel = normalized(functionBody(
        modelSource, QStringLiteral("void Views::removeView")));
    QVERIFY(removeModel.contains(QStringLiteral("m_viewsTable.hasExplicitLinkedMembers(id)")));

    const QString settingsQml = normalized(readFile(
        QStringLiteral("shell/package/contents/configuration/LatteDockConfiguration.qml")));
    QVERIFY(settingsQml.contains(QStringLiteral("enabled:dialog.advancedLevel&&latteView.canRemove")));
    QVERIFY(settingsQml.contains(QStringLiteral("Removelinkeddocksfirst")));
}

void DockIdentityContractTest::runtimeRecreationRebindsWholeRelationship()
{
    const QString layoutSource = readFile(QStringLiteral("app/layout/genericlayout.cpp"));
    const QString recreate = normalized(functionBody(
        layoutSource, QStringLiteral("void GenericLayout::recreateView")));
    const int collectMembers = recreate.indexOf(
        QStringLiteral("view->relationshipRootView()==relationshipRoot"));
    const int removeMembers = recreate.indexOf(
        QStringLiteral("m_latteViews.take(it->data())"), collectMembers);
    const int removeRoot = recreate.indexOf(
        QStringLiteral("m_latteViews.take(rootContainment.data())"), removeMembers);
    const int addRelationship = recreate.indexOf(
        QStringLiteral("for(intindex=0;index<containmentsToRecreate.size();++index)"), removeRoot);
    const int finishReconciliation = recreate.indexOf(
        QStringLiteral("root->synchronizeScreenGroupMembers()"), addRelationship);
    QVERIFY2(collectMembers >= 0 && removeMembers > collectMembers && removeRoot > removeMembers
                 && addRelationship > removeRoot && finishReconciliation > addRelationship,
             "root recreation must replace members first, rebuild root first, then reconcile the complete group");

    const QString rootSource = readFile(QStringLiteral("app/view/originalview.cpp"));
    const QString synchronize = normalized(functionBody(
        rootSource, QStringLiteral("void OriginalView::synchronizeScreenGroupMembers")));
    QVERIFY(synchronize.contains(QStringLiteral("layout()->isRecreatingView(containment())")));
    QVERIFY(rootSource.contains(QStringLiteral("OriginalView::~OriginalView() = default;")));

    const QString coronaSource = readFile(QStringLiteral("app/lattecorona.cpp"));
    const QString reload = normalized(functionBody(
        coronaSource, QStringLiteral("void Corona::reloadView")));
    QVERIFY(reload.contains(QStringLiteral("if(!m_debugDbusEnabled)")));
    QVERIFY(reload.contains(QStringLiteral("LATTE_DEBUG_DBUS=1")));
    QVERIFY(reload.contains(QStringLiteral("view->reloadRuntimeView()")));

    QVERIFY(recreate.contains(QStringLiteral("constLayout::ViewsMapeligibleViews=validViewsMap()")));
    QVERIFY(recreate.contains(QStringLiteral("mapContainsId(&eligibleViews,candidate->id())")));
}

void DockIdentityContractTest::crossLayoutMovesRevalidateBeforeImport()
{
    const QString source = readFile(QStringLiteral("app/settings/viewsdialog/viewscontroller.cpp"));
    const QString validate = normalized(functionBody(
        source, QStringLiteral("bool Views::canCommitMoveDestinations")));
    QVERIFY(validate.contains(QStringLiteral("currentOriginViews.allowsMoveToAnotherLayout(originViewId)")));

    const QString save = normalized(functionBody(source, QStringLiteral("void Views::save")));
    const int guard = save.indexOf(QStringLiteral("if(!canCommitMoveDestinations(newViews))"));
    const int importTemplate = save.indexOf(QStringLiteral("central->newView(newViews[i])"), guard);
    const int importLayout = save.indexOf(QStringLiteral("central->newView(adjustedview)"), guard);
    QVERIFY2(guard >= 0 && importTemplate > guard && importLayout > guard,
             "the current origin graph must be revalidated before either destination import path");
}

void DockIdentityContractTest::clipboardCopyBreaksLinkedLineage()
{
    const QString source = readFile(QStringLiteral("app/settings/viewsdialog/viewscontroller.cpp"));
    const QString copy = normalized(functionBody(
        source, QStringLiteral("void Views::copySelectedViews")));
    const int normalizeRelationship = copy.indexOf(
        QStringLiteral("clipboardviews[i]=clipboardviews[i].toIndependentSnapshot()"));
    const int publishClipboard = copy.indexOf(
        QStringLiteral("setClipboardContents(clipboardviews)"), normalizeRelationship);
    QVERIFY2(normalizeRelationship >= 0 && publishClipboard > normalizeRelationship,
             "Copy must remove linked lineage before publishing clipboard records");

    const QString viewDataSource = readFile(QStringLiteral("app/data/viewdata.cpp"));
    const QString snapshot = normalized(functionBody(
        viewDataSource, QStringLiteral("View View::toIndependentSnapshot")));
    QVERIFY(snapshot.contains(QStringLiteral("snapshot.isMoveOrigin=false")));
    QVERIFY(snapshot.contains(QStringLiteral("snapshot.isMoveDestination=false")));
}

void DockIdentityContractTest::refusedLayoutMoveCancelsRelocation()
{
    const QString managerHeader = readFile(QStringLiteral("app/layouts/manager.h"));
    QVERIFY(managerHeader.contains(QStringLiteral("[[nodiscard]] bool moveView")));

    const QString managerSource = readFile(QStringLiteral("app/layouts/manager.cpp"));
    const QString move = normalized(functionBody(
        managerSource, QStringLiteral("bool Manager::moveView")));
    const int relationshipGuard = move.indexOf(
        QStringLiteral("participatesInLegacyLayoutMove"));
    const int refuse = move.indexOf(QStringLiteral("returnfalse"), relationshipGuard);
    const int unassign = move.indexOf(QStringLiteral("unassignFromLayout"), refuse);
    const int accept = move.lastIndexOf(QStringLiteral("returntrue"));
    QVERIFY2(relationshipGuard >= 0 && refuse > relationshipGuard
                 && unassign > refuse && accept > unassign,
             "the move transaction must report refusal before unassigning its source");

    const QString positionerSource = readFile(QStringLiteral("app/view/positioner.cpp"));
    const QString relocation = normalized(functionBody(
        positionerSource, QStringLiteral("void Positioner::initSignalingForLocationChangeSliding")));
    const int checkedMove = relocation.indexOf(
        QStringLiteral("if(!m_corona->layoutsManager()->moveView"));
    const int cancel = relocation.indexOf(
        QStringLiteral("cancelFailedLayoutRelocation()"), checkedMove);
    QVERIFY2(checkedMove >= 0 && cancel > checkedMove,
             "a late move refusal must re-enter the normal reveal path");

    const QString cancellation = normalized(functionBody(
        positionerSource, QStringLiteral("void Positioner::cancelFailedLayoutRelocation")));
    QVERIFY(cancellation.contains(QStringLiteral(
        "cancelToCommittedIfCurrent(")));
    QVERIFY(cancellation.contains(QStringLiteral(
        "projectPendingPlacement(")));
    QVERIFY(cancellation.contains(QStringLiteral("scheduleLastRepositionApplyEvent()")));
}

void DockIdentityContractTest::outputEligibilityUsesPersistentPlacementAuthority()
{
    const QString source = readFile(QStringLiteral("app/layout/genericlayout.cpp"));
    const QString map = normalized(functionBody(
        source, QStringLiteral("Layout::ViewsMap GenericLayout::validViewsMap")));
    QVERIFY(map.contains(QStringLiteral("m_pendingContainmentUpdates.containsId(containmentId)")));
    QVERIFY(map.contains(QStringLiteral("Layouts::Storage::self()->view(containment->config())")));
    QVERIFY(!map.contains(QStringLiteral("m_latteViews[containment]->data()")));
    QVERIFY(map.contains(QStringLiteral("isScreenActive(view.screen)")));
}

void DockIdentityContractTest::linkedAppletGeometryRemainsPerView()
{
    const QString cloneSource = readFile(QStringLiteral("app/view/clonedview.cpp"));
    const QString fromRoot = normalized(functionBody(
        cloneSource, QStringLiteral("void ClonedView::onOriginalAppletConfigPropertyChanged")));
    const QString toRoot = normalized(functionBody(
        cloneSource, QStringLiteral("void ClonedView::updateOriginalAppletConfigProperty")));
    const QString reconcile = normalized(functionBody(
        cloneSource, QStringLiteral("bool ClonedView::reconcileAppletProjectionWithRoot")));

    QVERIFY(fromRoot.contains(QStringLiteral("synchronizesAppletConfigurationKey(key)")));
    QVERIFY(toRoot.contains(QStringLiteral("synchronizesAppletConfigurationKey(key)")));
    QVERIFY(reconcile.contains(QStringLiteral("locallyOwnedAppletConfigurationKeys()")));

    const QString storageSource = readFile(QStringLiteral("app/layouts/storage.cpp"));
    const QString import = normalized(functionBody(
        storageSource, QStringLiteral("Data::View Storage::newView")));
    QVERIFY(import.contains(QStringLiteral("if(nextViewData.isCloned())")));
    QVERIFY(import.contains(QStringLiteral("clearLinkedMemberLocalAppletConfiguration(temp2File)")));
}

QTEST_GUILESS_MAIN(DockIdentityContractTest)

#include "dockidentitycontracttest.moc"
