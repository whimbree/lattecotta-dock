/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../../app/view/viewactionpolicy.h"

#include <QtTest>

using Latte::ViewActionPolicy::Action;
using Latte::ViewActionPolicy::permits;
using Latte::ViewActionPolicy::Role;

class ViewActionPolicyTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void screenGroupOriginalRetainsIndependentDockActions();
    void screenGroupReplicaCannotEscapeRootOwnership();
    void explicitLinkedMemberOwnsPlacementAndRemoval();
};

void ViewActionPolicyTest::screenGroupOriginalRetainsIndependentDockActions()
{
    //! The policy role is Original for both a single-screen dock and an All
    //! Screens original. Duplicate normalizes the copied relationship before
    //! import, so the new dock remains one independent snapshot.
    QVERIFY(permits(Role::Original, Action::Duplicate));
    QVERIFY(permits(Role::Original, Action::CreateLinked));
    QVERIFY(permits(Role::Original, Action::Relocate));
    QVERIFY(permits(Role::Original, Action::ExportTemplate));
    QVERIFY(permits(Role::Original, Action::MoveToLayout));
    QVERIFY(permits(Role::Original, Action::Remove));
}

void ViewActionPolicyTest::screenGroupReplicaCannotEscapeRootOwnership()
{
    QVERIFY(permits(Role::ScreenGroupReplica, Action::Duplicate));
    QVERIFY(permits(Role::ScreenGroupReplica, Action::CreateLinked));
    QVERIFY(!permits(Role::ScreenGroupReplica, Action::Relocate));
    QVERIFY(!permits(Role::ScreenGroupReplica, Action::ExportTemplate));
    QVERIFY(!permits(Role::ScreenGroupReplica, Action::MoveToLayout));
    QVERIFY(!permits(Role::ScreenGroupReplica, Action::Remove));
}

void ViewActionPolicyTest::explicitLinkedMemberOwnsPlacementAndRemoval()
{
    QVERIFY(permits(Role::ExplicitLinkedMember, Action::Duplicate));
    QVERIFY(permits(Role::ExplicitLinkedMember, Action::CreateLinked));
    QVERIFY(permits(Role::ExplicitLinkedMember, Action::Relocate));
    QVERIFY(!permits(Role::ExplicitLinkedMember, Action::ExportTemplate));
    QVERIFY(!permits(Role::ExplicitLinkedMember, Action::MoveToLayout));
    QVERIFY(permits(Role::ExplicitLinkedMember, Action::Remove));
}

QTEST_GUILESS_MAIN(ViewActionPolicyTest)

#include "viewactionpolicytest.moc"
