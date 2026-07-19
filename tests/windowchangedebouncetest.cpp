/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Behavioral regression test for AbstractWindowInterface::considerWindowChanged.
// A minimal concrete subclass of the abstract interface exercises the debounce
// timer directly: normal geometry/title churn is delayed 150 ms, while a
// discrete maximized state edge flushes synchronously.

#include "wm/abstractwindowinterface.h"

#include <QGuiApplication>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace Latte::WindowSystem;

//! Concrete WM that implements the pure virtuals as no-ops and exposes the
//! protected considerWindowChanged() to the test so the real debounce path runs.
class TestableWindowInterface : public AbstractWindowInterface
{
    Q_OBJECT
public:
    explicit TestableWindowInterface(QObject *parent = nullptr)
        : AbstractWindowInterface(parent) {}

    void consider(WindowId wid, bool immediate = false)
    {
        considerWindowChanged(wid, immediate);
    }

    void setViewExtraFlags(QObject *, bool, Latte::Types::Visibility) override {}
    void setViewStruts(QWindow &, const QRect &, Plasma::Types::Location) override {}
    void setWindowOnActivities(const WindowId &, const QStringList &) override {}
    void removeViewStruts(QWindow &) override {}

    WindowId activeWindow() override { return WindowId(); }
    WindowInfoWrap requestInfo(WindowId) override { return WindowInfoWrap(); }
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
};

class WindowChangeDebounceTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void normalDebounceDelaysUntilTimerFires();
    void immediateFlushesDuringActiveDebounce();
    void immediateFlushesUnrelatedPendingFirst();

private:
    static WindowId makeWid(int id)
    {
        return WindowId::fromWaylandUuid(QByteArray::number(id));
    }
};

void WindowChangeDebounceTest::normalDebounceDelaysUntilTimerFires()
{
    TestableWindowInterface wm;
    QSignalSpy spy(&wm, &AbstractWindowInterface::windowChanged);

    const WindowId wid = makeWid(1);

    wm.consider(wid);
    wm.consider(wid);
    wm.consider(wid);

    QCOMPARE(spy.count(), 0);

    QTest::qWait(50);
    QCOMPARE(spy.count(), 0);

    QTest::qWait(200);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).value<WindowId>(), wid);
}

void WindowChangeDebounceTest::immediateFlushesDuringActiveDebounce()
{
    TestableWindowInterface wm;
    QSignalSpy spy(&wm, &AbstractWindowInterface::windowChanged);

    const WindowId wid = makeWid(2);

    wm.consider(wid);
    QCOMPARE(spy.count(), 0);

    wm.consider(wid, true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).value<WindowId>(), wid);

    QTest::qWait(200);
    QCOMPARE(spy.count(), 0);
}

void WindowChangeDebounceTest::immediateFlushesUnrelatedPendingFirst()
{
    TestableWindowInterface wm;
    QSignalSpy spy(&wm, &AbstractWindowInterface::windowChanged);

    const WindowId pending = makeWid(3);
    const WindowId immediate = makeWid(4);

    wm.consider(pending);
    QCOMPARE(spy.count(), 0);

    wm.consider(immediate, true);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(0).at(0).value<WindowId>(), pending);
    QCOMPARE(spy.at(1).at(0).value<WindowId>(), immediate);

    QTest::qWait(200);
    QCOMPARE(spy.count(), 2);
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
