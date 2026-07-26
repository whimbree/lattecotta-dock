/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "layout/viewmovetransaction.h"

#include <QTest>

using Latte::Layout::ViewMoveTransaction;

class ViewMoveTransactionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void commitsInDurableOrder();
    void rejectsBeforeTheCommitDecision();
    void requiresRollbackRecoveryWhenStagingCannotBeRemoved();
    void trustsObservedCommitStateOverWriteReturnValue();
    void requiresRecoveryForUnknownCommitOwnership();
    void commitsWithRecoveryWhenOriginRetirementFails();
    void cannotBeReused();
};

void ViewMoveTransactionTest::commitsInDurableOrder()
{
    ViewMoveTransaction transaction;
    QStringList operations;

    const auto result = transaction.commit(
        [&operations]() {
            operations << QStringLiteral("journal");
            return true;
        },
        [&operations]() {
            operations << QStringLiteral("destination");
            return true;
        },
        [&operations]() {
            operations << QStringLiteral("decision");
            return true;
        },
        [&operations]() {
            operations << QStringLiteral("observe");
            return ViewMoveTransaction::PersistentOwner::Destination;
        },
        [&operations]() {
            operations << QStringLiteral("rollback");
            return true;
        },
        [&operations]() {
            operations << QStringLiteral("origin");
            return true;
        });

    QCOMPARE(result, ViewMoveTransaction::Result::Committed);
    QCOMPARE(transaction.phase(), ViewMoveTransaction::Phase::Complete);
    QCOMPARE(operations, QStringList({
        QStringLiteral("journal"),
        QStringLiteral("destination"),
        QStringLiteral("decision"),
        QStringLiteral("observe"),
        QStringLiteral("origin"),
    }));
}

void ViewMoveTransactionTest::rejectsBeforeTheCommitDecision()
{
    for (const bool destinationSucceeds : {false, true}) {
        ViewMoveTransaction transaction;
        bool decisionAttempted{false};
        bool originRetirementAttempted{false};
        bool rollbackAttempted{false};

        const auto result = transaction.commit(
            []() { return true; },
            [destinationSucceeds]() {
                return destinationSucceeds;
            },
            [&decisionAttempted]() {
                decisionAttempted = true;
                return false;
            },
            []() {
                return ViewMoveTransaction::PersistentOwner::Origin;
            },
            [&rollbackAttempted]() {
                rollbackAttempted = true;
                return true;
            },
            [&originRetirementAttempted]() {
                originRetirementAttempted = true;
                return true;
            });

        QCOMPARE(result, ViewMoveTransaction::Result::Rejected);
        QCOMPARE(transaction.phase(), ViewMoveTransaction::Phase::Rejected);
        QCOMPARE(decisionAttempted, destinationSucceeds);
        QVERIFY(rollbackAttempted);
        QVERIFY(!originRetirementAttempted);
    }
}

void ViewMoveTransactionTest::
requiresRollbackRecoveryWhenStagingCannotBeRemoved()
{
    ViewMoveTransaction transaction;
    const auto result = transaction.commit(
        []() { return true; },
        []() { return true; },
        []() { return false; },
        []() {
            return ViewMoveTransaction::PersistentOwner::Origin;
        },
        []() { return false; },
        []() { return true; });

    QCOMPARE(
        result,
        ViewMoveTransaction::Result::RejectedRecoveryRequired);
    QCOMPARE(
        transaction.phase(),
        ViewMoveTransaction::Phase::RecoveryRequired);
}

void ViewMoveTransactionTest::
trustsObservedCommitStateOverWriteReturnValue()
{
    ViewMoveTransaction transaction;
    bool originRetired{false};
    const auto result = transaction.commit(
        []() { return true; },
        []() { return true; },
        []() { return false; },
        []() {
            return ViewMoveTransaction::PersistentOwner::Destination;
        },
        []() { return true; },
        [&originRetired]() {
            originRetired = true;
            return true;
        });

    QCOMPARE(result, ViewMoveTransaction::Result::Committed);
    QVERIFY(originRetired);
}

void ViewMoveTransactionTest::
requiresRecoveryForUnknownCommitOwnership()
{
    ViewMoveTransaction transaction;
    bool rollbackAttempted{false};
    const auto result = transaction.commit(
        []() { return true; },
        []() { return true; },
        []() { return false; },
        []() {
            return ViewMoveTransaction::PersistentOwner::Unknown;
        },
        [&rollbackAttempted]() {
            rollbackAttempted = true;
            return true;
        },
        []() { return true; });

    QCOMPARE(
        result,
        ViewMoveTransaction::Result::RejectedRecoveryRequired);
    QVERIFY(!rollbackAttempted);
}

void ViewMoveTransactionTest::
commitsWithRecoveryWhenOriginRetirementFails()
{
    ViewMoveTransaction transaction;
    const auto result = transaction.commit(
        []() { return true; },
        []() { return true; },
        []() { return true; },
        []() {
            return ViewMoveTransaction::PersistentOwner::Destination;
        },
        []() { return true; },
        []() { return false; });

    QCOMPARE(
        result,
        ViewMoveTransaction::Result::CommittedRecoveryRequired);
    QCOMPARE(
        transaction.phase(),
        ViewMoveTransaction::Phase::RecoveryRequired);
}

void ViewMoveTransactionTest::cannotBeReused()
{
    ViewMoveTransaction transaction;
    const auto commit = [&transaction]() {
        return transaction.commit(
            []() { return true; },
            []() { return true; },
            []() { return true; },
            []() {
                return ViewMoveTransaction::PersistentOwner::Destination;
            },
            []() { return true; },
            []() { return true; });
    };

    QCOMPARE(commit(), ViewMoveTransaction::Result::Committed);
    QCOMPARE(commit(), ViewMoveTransaction::Result::Rejected);
}

QTEST_APPLESS_MAIN(ViewMoveTransactionTest)

#include "viewmovetransactiontest.moc"
