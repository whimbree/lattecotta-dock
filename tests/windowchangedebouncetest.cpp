/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Behavioral regression test for AbstractWindowInterface's window-change
// coalescing and its real tracker consumers. The concrete interface and mutable
// requestInfo registry scaffold were transplanted from latte-dock-qt6
// (tests/lastactivewindowtest.cpp at 81384003,
// github.com/CaptSilver/latte-dock-qt6).

#include "wm/abstractwindowinterface.h"
#include "wm/tracker/lastactivewindow.h"
#include "wm/tracker/trackedgeneralinfo.h"
#include "wm/tracker/windowstracker.h"

#include <QGuiApplication>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace Latte::WindowSystem;

class TestableWindowInterface : public AbstractWindowInterface
{
    Q_OBJECT

public:
    explicit TestableWindowInterface(QObject *parent = nullptr)
        : AbstractWindowInterface(parent)
    {
    }

    void consider(const WindowId &wid)
    {
        considerWindowChanged(wid, WindowChangeDelivery::Coalesced);
    }

    void considerImmediately(const WindowId &wid)
    {
        considerWindowChanged(wid, WindowChangeDelivery::Immediate);
    }

    void retire(const WindowId &wid)
    {
        discardPendingWindowChange(wid);
        Q_EMIT windowRemoved(wid);
    }

    void announce(const WindowInfoWrap &winfo)
    {
        m_windowInfo[winfo.wid()] = winfo;
        Q_EMIT windowAdded(winfo.wid());
    }

    void seed(const WindowInfoWrap &winfo)
    {
        m_windowInfo.insert(winfo.wid(), winfo);
    }

    void activate(const WindowId &wid)
    {
        Q_EMIT activeWindowChanged(wid);
    }

    void change(const WindowInfoWrap &winfo)
    {
        m_windowInfo[winfo.wid()] = winfo;
        consider(winfo.wid());
    }

    void changeImmediately(const WindowInfoWrap &winfo)
    {
        m_windowInfo[winfo.wid()] = winfo;
        considerWindowChanged(winfo.wid(), WindowChangeDelivery::Immediate);
    }

    void setViewExtraFlags(QObject *, bool, Latte::Types::Visibility) override {}
    bool setViewStruts(QWindow &, const QRect &, Plasma::Types::Location) override { return true; }
    void setWindowOnActivities(const WindowId &, const QStringList &) override {}
    bool removeViewStruts(QWindow &) override { return true; }

    WindowId activeWindow() override { return WindowId(); }
    WindowInfoWrap requestInfo(WindowId wid) override
    {
        WindowInfoWrap info = m_windowInfo.value(wid);
        info.setIsValid(info.isValid() && !hasBlockedTracking(wid));
        return info;
    }
    WindowInfoWrap requestInfoActive() override { return WindowInfoWrap(); }

    void skipTaskBar(const QDialog &) override {}
    void slideWindow(QWindow &, Slide) override {}
    void enableBlurBehind(QWindow &) override {}
    void setActiveEdge(QWindow *, bool) override {}

    void requestActivate(WindowId) override {}
    void requestClose(WindowId) override {}
    void requestMoveWindow(WindowId, QPoint) override {}
    void requestToggleIsOnAllDesktops(WindowId) override {}
    void requestToggleKeepAbove(WindowId) override {}
    void requestToggleMinimized(WindowId) override {}
    void requestToggleMaximized(WindowId) override {}
    void setKeepAbove(WindowId, bool) override {}
    void setKeepBelow(WindowId, bool) override {}

    bool windowCanBeDragged(WindowId) override { return false; }
    bool windowCanBeMaximized(WindowId) override { return false; }

    QIcon iconFor(WindowId) override { return QIcon(); }
    WindowId winIdFor(QString, QRect) override { return WindowId(); }
    WindowId winIdFor(QString, QString) override { return WindowId(); }
    AppData appDataFor(WindowId) override { return AppData(); }

    void switchToNextVirtualDesktop() override {}
    void switchToPreviousVirtualDesktop() override {}
    void setFrameExtents(QWindow *, const QMargins &) override {}
    void setInputMask(QWindow *, const QRect &) override {}

private:
    QHash<WindowId, WindowInfoWrap> m_windowInfo;
};

class WindowChangeDebounceTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void sustainedChangesDeliverWhileInputIsActive();
    void burstCoalescesAndConsumersReadLatestState();
    void differentWindowFlushesPendingBeforeStartingDeadline();
    void immediateChangeFlushesDifferentPendingWindowFirst();
    void immediateStateChangesPropagateWithoutDeadline();
    void retiredWindowDiscardsPendingChange();
    void ignoredWindowRefreshesWithoutPretendingDestruction();
    void rejectedActiveWindowDoesNotPublishTrackerRow();

private:
    static WindowId makeWid(int id)
    {
        return WindowId::fromWaylandUuid(QByteArray::number(id));
    }

    static WindowInfoWrap makeShownWindow(const WindowId &wid, int revision, bool maximized)
    {
        WindowInfoWrap winfo;
        winfo.setWid(wid);
        winfo.setIsValid(true);
        winfo.setIsActive(true);
        winfo.setIsOnAllDesktops(true);
        winfo.setIsOnAllActivities(true);
        winfo.setIsMaxVert(maximized);
        winfo.setIsMaxHoriz(maximized);
        winfo.setDisplay(QStringLiteral("revision-%1").arg(revision));
        winfo.setGeometry(QRect(revision, revision, 100, 100));
        return winfo;
    }
};

void WindowChangeDebounceTest::sustainedChangesDeliverWhileInputIsActive()
{
    TestableWindowInterface wm;
    QSignalSpy changedSpy(&wm, &AbstractWindowInterface::windowChanged);
    const WindowId wid = makeWid(1);

    int inputCount = 0;
    QTimer input;
    input.setTimerType(Qt::PreciseTimer);
    input.setInterval(40);
    connect(&input, &QTimer::timeout, &wm, [&]() {
        ++inputCount;
        wm.consider(wid);
    });

    input.start();
    ++inputCount;
    wm.consider(wid);

    QTRY_VERIFY_WITH_TIMEOUT(changedSpy.count() >= 2, 3000);
    QVERIFY(input.isActive());
    QVERIFY2(changedSpy.count() < inputCount,
             qPrintable(QStringLiteral("%1 notifications were not coalesced from %2 active inputs")
                            .arg(changedSpy.count())
                            .arg(inputCount)));
    for (const QList<QVariant> &arguments : changedSpy) {
        QCOMPARE(arguments.at(0).value<WindowId>(), wid);
    }

    input.stop();
}

void WindowChangeDebounceTest::burstCoalescesAndConsumersReadLatestState()
{
    TestableWindowInterface wm;
    Tracker::Windows *windows = wm.windowsTracker();
    QSignalSpy interfaceSpy(&wm, &AbstractWindowInterface::windowChanged);
    QSignalSpy trackerSpy(windows, &Tracker::Windows::windowChanged);
    const WindowId wid = makeWid(2);

    wm.announce(makeShownWindow(wid, 0, false));
    for (int revision = 1; revision <= 5; ++revision) {
        wm.change(makeShownWindow(wid, revision, revision == 5));
    }

    QTRY_COMPARE_WITH_TIMEOUT(interfaceSpy.count(), 1, 500);
    QCOMPARE(trackerSpy.count(), 1);
    QCOMPARE(interfaceSpy.at(0).at(0).value<WindowId>(), wid);
    QCOMPARE(trackerSpy.at(0).at(0).value<WindowId>(), wid);

    const WindowInfoWrap consumed = windows->infoFor(wid);
    QCOMPARE(consumed.display(), QStringLiteral("revision-5"));
    QCOMPARE(consumed.geometry(), QRect(5, 5, 100, 100));
    QVERIFY(consumed.isMaximized());

    QTest::qWait(200);
    QCOMPARE(interfaceSpy.count(), 1);
    QCOMPARE(trackerSpy.count(), 1);
}

void WindowChangeDebounceTest::differentWindowFlushesPendingBeforeStartingDeadline()
{
    TestableWindowInterface wm;
    QSignalSpy changedSpy(&wm, &AbstractWindowInterface::windowChanged);
    const WindowId first = makeWid(3);
    const WindowId second = makeWid(4);

    wm.consider(first);
    wm.consider(first);
    wm.consider(second);

    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(changedSpy.at(0).at(0).value<WindowId>(), first);

    QTRY_COMPARE_WITH_TIMEOUT(changedSpy.count(), 2, 500);
    QCOMPARE(changedSpy.at(1).at(0).value<WindowId>(), second);
}

void WindowChangeDebounceTest::immediateChangeFlushesDifferentPendingWindowFirst()
{
    TestableWindowInterface wm;
    QSignalSpy changedSpy(&wm, &AbstractWindowInterface::windowChanged);
    const WindowId pending = makeWid(5);
    const WindowId immediate = makeWid(6);

    wm.consider(pending);
    wm.considerImmediately(immediate);

    QCOMPARE(changedSpy.count(), 2);
    QCOMPARE(changedSpy.at(0).at(0).value<WindowId>(), pending);
    QCOMPARE(changedSpy.at(1).at(0).value<WindowId>(), immediate);

    QTest::qWait(200);
    QCOMPARE(changedSpy.count(), 2);
}

void WindowChangeDebounceTest::immediateStateChangesPropagateWithoutDeadline()
{
    TestableWindowInterface wm;
    Tracker::TrackedGeneralInfo trackedInfo(wm.windowsTracker());
    trackedInfo.setEnabled(true);
    Tracker::LastActiveWindow *lastActive = trackedInfo.lastActiveWindow();
    QSignalSpy changedSpy(&wm, &AbstractWindowInterface::windowChanged);
    const WindowId wid = makeWid(7);

    wm.announce(makeShownWindow(wid, 0, false));
    trackedInfo.setActiveWindow(wid);
    QVERIFY(lastActive->isValid());
    QVERIFY(!lastActive->isMaximized());

    wm.change(makeShownWindow(wid, 1, false));
    wm.changeImmediately(makeShownWindow(wid, 2, true));
    QCOMPARE(changedSpy.count(), 1);
    QVERIFY(lastActive->isMaximized());
    QCOMPARE(wm.windowsTracker()->infoFor(wid).display(), QStringLiteral("revision-2"));

    wm.changeImmediately(makeShownWindow(wid, 3, false));
    QCOMPARE(changedSpy.count(), 2);
    QVERIFY(!lastActive->isMaximized());

    QTest::qWait(200);
    QCOMPARE(changedSpy.count(), 2);
}

void WindowChangeDebounceTest::retiredWindowDiscardsPendingChange()
{
    TestableWindowInterface wm;
    QSignalSpy changedSpy(&wm, &AbstractWindowInterface::windowChanged);
    QSignalSpy removedSpy(&wm, &AbstractWindowInterface::windowRemoved);
    const WindowId wid = makeWid(8);

    wm.announce(makeShownWindow(wid, 0, false));
    wm.change(makeShownWindow(wid, 1, false));
    wm.retire(wid);

    QCOMPARE(removedSpy.count(), 1);
    QVERIFY(!wm.windowsTracker()->containsWindow(wid));
    QTest::qWait(200);
    QCOMPARE(changedSpy.count(), 0);
    QVERIFY(!wm.windowsTracker()->containsWindow(wid));
}

void WindowChangeDebounceTest::ignoredWindowRefreshesWithoutPretendingDestruction()
{
    TestableWindowInterface wm;
    QObject owner;
    QSignalSpy changedSpy(&wm, &AbstractWindowInterface::windowChanged);
    QSignalSpy removedSpy(&wm, &AbstractWindowInterface::windowRemoved);
    const WindowId wid = makeWid(9);

    wm.announce(makeShownWindow(wid, 0, false));
    QVERIFY(wm.windowsTracker()->isValidFor(wid));

    wm.registerIgnoredWindow(wid, &owner);
    QCOMPARE(changedSpy.count(), 1);
    QVERIFY(wm.windowsTracker()->containsWindow(wid));
    QVERIFY(!wm.windowsTracker()->isValidFor(wid));

    wm.unregisterIgnoredWindow(wid, &owner);
    QCOMPARE(changedSpy.count(), 2);
    QCOMPARE(removedSpy.count(), 0);
    QVERIFY(wm.windowsTracker()->isValidFor(wid));
}

void WindowChangeDebounceTest::rejectedActiveWindowDoesNotPublishTrackerRow()
{
    TestableWindowInterface wm;
    const WindowId wid = makeWid(10);
    WindowInfoWrap rejected = makeShownWindow(wid, 0, false);
    rejected.setIsValid(false);

    wm.seed(rejected);
    wm.activate(wid);

    QVERIFY(!wm.windowsTracker()->containsWindow(wid));
}

int main(int argc, char *argv[])
{
    static QTemporaryDir xdgConfig;
    qputenv("XDG_CONFIG_HOME", xdgConfig.path().toUtf8());

    QGuiApplication app(argc, argv);
    WindowChangeDebounceTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "windowchangedebouncetest.moc"
