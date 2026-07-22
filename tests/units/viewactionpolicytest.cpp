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
    void clonesCannotEscapeOriginalLifecyclePolicy();
};

void ViewActionPolicyTest::screenGroupOriginalRetainsIndependentDockActions()
{
    //! The policy role is Original for both a single-screen dock and an All
    //! Screens original. The latter must remain duplicable so the new original
    //! can establish a separate clone ensemble.
    QVERIFY(permits(Role::Original, Action::Duplicate));
    QVERIFY(permits(Role::Original, Action::ExportTemplate));
    QVERIFY(permits(Role::Original, Action::MoveToLayout));
    QVERIFY(permits(Role::Original, Action::Remove));
}

void ViewActionPolicyTest::clonesCannotEscapeOriginalLifecyclePolicy()
{
    QVERIFY(!permits(Role::Clone, Action::Duplicate));
    QVERIFY(!permits(Role::Clone, Action::ExportTemplate));
    QVERIFY(!permits(Role::Clone, Action::MoveToLayout));
    QVERIFY(!permits(Role::Clone, Action::Remove));
}

QTEST_GUILESS_MAIN(ViewActionPolicyTest)

#include "viewactionpolicytest.moc"
