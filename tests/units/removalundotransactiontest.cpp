/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../../app/layout/removalundotransaction.h"

#include <QtTest>

namespace {

using Latte::Layout::RemovalUndoTransaction;

constexpr bool resolvesHappyPathAtCompileTime()
{
    RemovalUndoTransaction transaction;
    const auto token = transaction.beginRemoval();
    return transaction.queueUndoResolutionIfCurrent(
               token)
        && transaction.resolveUndoOrRequireRemoval(
               token,
               []() constexpr { return true; },
               []() constexpr { return true; })
            == RemovalUndoTransaction::UndoResolution::
                Restored
        && transaction.phase()
            == RemovalUndoTransaction::Phase::Restored;
}

static_assert(resolvesHappyPathAtCompileTime());

class RemovalUndoTransactionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void queuedUndoDefersRestorationCallbacks();
    void restoreFailureFinalizesInOwnedOrder();
    void resumeFailureRequiresRemoval();
    void runtimeRetirementFailureStopsFinalization();
    void containmentDestructionRefusalCannotCommitPersistence();
    void persistenceFailureIsUnrecoverable();
    void staleQueuedGenerationCannotDestroyReplacement();
};

void RemovalUndoTransactionTest::
queuedUndoDefersRestorationCallbacks()
{
    RemovalUndoTransaction transaction;
    QStringList events;
    const auto token =
        transaction.beginRemoval();

    QVERIFY(transaction.queueUndoResolutionIfCurrent(
        token));
    QVERIFY2(
        events.isEmpty(),
        "destroyedChanged(false) may only queue work; libplasma child state is still transient");
    QCOMPARE(
        transaction.resolveUndoOrRequireRemoval(
            token,
            [&events]() {
                events << QStringLiteral("restore");
                return true;
            },
            [&events]() {
                events << QStringLiteral("resume");
                return true;
            }),
        RemovalUndoTransaction::UndoResolution::
            Restored);
    QCOMPARE(
        events,
        QStringList({"restore", "resume"}));
}

void RemovalUndoTransactionTest::
restoreFailureFinalizesInOwnedOrder()
{
    RemovalUndoTransaction transaction;
    QStringList events;
    const auto token =
        transaction.beginRemoval();
    QVERIFY(transaction.queueUndoResolutionIfCurrent(
        token));

    QCOMPARE(
        transaction.resolveUndoOrRequireRemoval(
            token,
            [&events]() {
                events << QStringLiteral("restore");
                return false;
            },
            [&events]() {
                events << QStringLiteral("resume");
                return true;
            }),
        RemovalUndoTransaction::UndoResolution::
            RemovalRequired);
    QCOMPARE(events, QStringList({"restore"}));

    QCOMPARE(
        transaction.finalizeRemovalIfCurrent(
            token,
            [&events]() {
                events << QStringLiteral("retire-runtime");
                return true;
            },
            [&events]() {
                events << QStringLiteral("destroy-containment");
                return true;
            },
            [&events]() {
                events << QStringLiteral("commit-tombstone");
                return true;
            }),
        RemovalUndoTransaction::FinalizationResult::
            Removed);
    QCOMPARE(
        events,
        QStringList({
            "restore",
            "retire-runtime",
            "destroy-containment",
            "commit-tombstone",
        }));
}

void RemovalUndoTransactionTest::
resumeFailureRequiresRemoval()
{
    RemovalUndoTransaction transaction;
    QStringList events;
    const auto token =
        transaction.beginRemoval();
    QVERIFY(transaction.queueUndoResolutionIfCurrent(
        token));

    QCOMPARE(
        transaction.resolveUndoOrRequireRemoval(
            token,
            [&events]() {
                events << QStringLiteral("restore");
                return true;
            },
            [&events]() {
                events << QStringLiteral("resume");
                return false;
            }),
        RemovalUndoTransaction::UndoResolution::
            RemovalRequired);
    QCOMPARE(
        events,
        QStringList({"restore", "resume"}));
    QCOMPARE(
        transaction.phase(),
        RemovalUndoTransaction::Phase::
            RemovalFinalizationQueued);
}

void RemovalUndoTransactionTest::
runtimeRetirementFailureStopsFinalization()
{
    RemovalUndoTransaction transaction;
    QStringList events;
    const auto token =
        transaction.beginRemoval();
    QVERIFY(
        transaction.queueRemovalFinalizationIfCurrent(
            token));
    QCOMPARE(
        transaction.finalizeRemovalIfCurrent(
            token,
            [&events]() {
                events << QStringLiteral("retire-runtime");
                return false;
            },
            [&events]() {
                events << QStringLiteral("destroy-containment");
                return true;
            },
            [&events]() {
                events << QStringLiteral("commit-tombstone");
                return true;
            }),
        RemovalUndoTransaction::FinalizationResult::
            RuntimeRetirementFailed);
    QCOMPARE(
        events,
        QStringList({"retire-runtime"}));
}

void RemovalUndoTransactionTest::
containmentDestructionRefusalCannotCommitPersistence()
{
    RemovalUndoTransaction transaction;
    QStringList events;
    const auto token =
        transaction.beginRemoval();
    QVERIFY(
        transaction.queueRemovalFinalizationIfCurrent(
            token));
    QCOMPARE(
        transaction.finalizeRemovalIfCurrent(
            token,
            [&events]() {
                events << QStringLiteral("retire-runtime");
                return true;
            },
            [&events]() {
                events << QStringLiteral("destroy-containment");
                return false;
            },
            [&events]() {
                events << QStringLiteral("commit-tombstone");
                return true;
            }),
        RemovalUndoTransaction::FinalizationResult::
            DestructionFailed);
    QCOMPARE(
        events,
        QStringList({
            "retire-runtime",
            "destroy-containment",
        }));
}

void RemovalUndoTransactionTest::
persistenceFailureIsUnrecoverable()
{
    RemovalUndoTransaction transaction;
    QStringList events;
    const auto token =
        transaction.beginRemoval();
    QVERIFY(
        transaction.queueRemovalFinalizationIfCurrent(
            token));
    QCOMPARE(
        transaction.finalizeRemovalIfCurrent(
            token,
            [&events]() {
                events << QStringLiteral("retire-runtime");
                return true;
            },
            [&events]() {
                events << QStringLiteral("destroy-containment");
                return true;
            },
            [&events]() {
                events << QStringLiteral("commit-tombstone");
                return false;
            }),
        RemovalUndoTransaction::FinalizationResult::
            PersistenceFailed);
    QCOMPARE(
        transaction.phase(),
        RemovalUndoTransaction::Phase::
            Unrecoverable);
    QCOMPARE(
        events,
        QStringList({
            "retire-runtime",
            "destroy-containment",
            "commit-tombstone",
        }));
}

void RemovalUndoTransactionTest::
staleQueuedGenerationCannotDestroyReplacement()
{
    RemovalUndoTransaction transaction;
    const auto stale =
        transaction.beginRemoval();
    QVERIFY(transaction.queueRemovalFinalizationIfCurrent(
        stale));
    const auto replacement =
        transaction.beginRemoval();
    QCOMPARE(replacement, stale + 1);
    QVERIFY(
        transaction.finalizeRemovalIfCurrent(
            stale,
            []() { return true; },
            []() { return true; },
            []() { return true; })
        == RemovalUndoTransaction::FinalizationResult::
            Stale);
    QCOMPARE(
        transaction.phase(),
        RemovalUndoTransaction::Phase::Suspended);
}

}

QTEST_MAIN(RemovalUndoTransactionTest)

#include "removalundotransactiontest.moc"
