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
    void ignoredWindowCleanupRetainsOtherOwners();
    void persistentMenuIdentitySurvivesRuntimeGap();
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
        {QStringLiteral("void Corona::moveViewToLayout"), QStringLiteral("Action::MoveToLayout"), QStringLiteral("view->positioner()->setNextLocation")},
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
    const QString setScreen = normalized(functionBody(source, QStringLiteral("void Positioner::setScreenToFollow")));
    const int disconnectOld = setScreen.indexOf(QStringLiteral("QObject::disconnect(m_screenGeometryConnection);"));
    const int replaceConnection = setScreen.indexOf(QStringLiteral("m_screenGeometryConnection=connect(scr,&QScreen::geometryChanged"));

    QVERIFY(disconnectOld >= 0 && replaceConnection > disconnectOld);
    QCOMPARE(setScreen.count(QStringLiteral("&QScreen::geometryChanged")), 1);
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
    QVERIFY(parseView.contains(QStringLiteral("vdata.count()!=4")));
    QVERIFY(populateTemplates.contains(QStringLiteral("if(m_contextDataValid)")));
    QVERIFY(!populateTemplates.contains(QStringLiteral("!m_view.isCloned")));
    QVERIFY(visibleActions.contains(QStringLiteral("setVisible(m_contextDataValid&&!configuring)")));
}

QTEST_GUILESS_MAIN(DockIdentityContractTest)

#include "dockidentitycontracttest.moc"
