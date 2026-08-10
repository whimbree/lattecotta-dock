/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../../app/view/floatingmaskhandshake.h"

#include <QtTest>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using Latte::ViewPart::FloatingMaskHandshake::RenderBridge;
using Latte::ViewPart::FloatingMaskHandshake::State;

class FloatingMaskHandshakeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void acceptsOnlyCurrentSubmittedPresentation();
    void closeWaitsForInFlightPostAndRefusesLateCallbacks();
};

void FloatingMaskHandshakeTest::acceptsOnlyCurrentSubmittedPresentation()
{
    State state;
    const QRect first{0, 5, 800, 54};
    const QRect second{0, 8, 800, 51};
    const QRect third{0, 2, 800, 57};

    const quint64 preArmSnapshot = state.generationForNextFrame();
    const quint64 firstGeneration = state.arm(first);
    QVERIFY(!state.canCollapse(preArmSnapshot, first));
    QVERIFY(!state.canCollapse(firstGeneration + 1, first));
    QVERIFY(state.canCollapse(firstGeneration, first));
    state.complete();
    QVERIFY(!state.canCollapse(firstGeneration, first));

    //! arm(second) must run for its state side effect (it advances the
    //! generation and sets the pending exact mask); the returned generation is
    //! not asserted on directly here, only the snapshots taken around it are.
    //! arm() is [[nodiscard]], so the result is bound (not dropped) and the
    //! binding marked maybe-unused rather than triggering -Werror=unused-result.
    [[maybe_unused]] const quint64 secondGeneration = state.arm(second);
    const quint64 secondSnapshot = state.generationForNextFrame();
    const quint64 thirdGeneration = state.arm(third);
    QVERIFY(!state.canCollapse(secondSnapshot, second));
    QVERIFY(!state.canCollapse(secondSnapshot, third));
    const quint64 thirdSnapshot = state.generationForNextFrame();
    QCOMPARE(thirdSnapshot, thirdGeneration);
    QVERIFY(state.canCollapse(thirdSnapshot, third));

    const quint64 legacyGeneration = state.transferToLegacy();
    QVERIFY(legacyGeneration > thirdGeneration);
    QVERIFY(!state.canCollapse(thirdGeneration, third));
    QVERIFY(!state.canCollapse(legacyGeneration, third));
}

void FloatingMaskHandshakeTest::
    closeWaitsForInFlightPostAndRefusesLateCallbacks()
{
    RenderBridge bridge;
    bridge.publish(42);
    QVERIFY(bridge.synchronizeForFrame());

    std::mutex gateMutex;
    std::condition_variable gateChanged;
    bool postEntered{false};
    bool releasePost{false};
    std::atomic_bool closeStarted{false};
    std::atomic_bool closeReturned{false};
    std::atomic_bool renderAccepted{false};
    quint64 postedGeneration{0};

    std::jthread renderThread([&]() {
        renderAccepted.store(
            bridge.afterFrame(
            [&](quint64 generation) {
                std::unique_lock lock{gateMutex};
                postedGeneration = generation;
                postEntered = true;
                gateChanged.notify_all();
                gateChanged.wait(lock, [&]() {
                    return releasePost;
                });
            }),
            std::memory_order_release);
    });

    {
        std::unique_lock lock{gateMutex};
        QVERIFY(gateChanged.wait_for(
            lock,
            std::chrono::seconds{2},
            [&]() {
                return postEntered;
            }));
    }

    std::jthread teardownThread([&]() {
        closeStarted.store(true, std::memory_order_release);
        bridge.close();
        closeReturned.store(true, std::memory_order_release);
    });

    QTRY_VERIFY_WITH_TIMEOUT(
        closeStarted.load(std::memory_order_acquire), 2000);
    QVERIFY(!closeReturned.load(std::memory_order_acquire));

    {
        const std::scoped_lock lock{gateMutex};
        releasePost = true;
    }
    gateChanged.notify_all();
    renderThread.join();
    teardownThread.join();

    QVERIFY(renderAccepted.load(std::memory_order_acquire));
    QCOMPARE(postedGeneration, quint64{42});
    QVERIFY(closeReturned.load(std::memory_order_acquire));

    bool latePostRan{false};
    QVERIFY(!bridge.synchronizeForFrame());
    QVERIFY(!bridge.afterFrame(
        [&](quint64) {
            latePostRan = true;
        }));
    QVERIFY(!latePostRan);
}

QTEST_MAIN(FloatingMaskHandshakeTest)

#include "floatingmaskhandshaketest.moc"
