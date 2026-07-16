/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Pins WindowInfoWrap's parent-id semantics for both window-system id
//! shapes. WindowId is a QByteArray: X11 ids ride as decimal strings,
//! wayland ids are QUuid strings, empty means "no parent". The Qt5-era
//! numeric test (parentId.toInt() > 0) silently classified every wayland
//! child window as a main window because toInt() on a UUID string is 0 -
//! same int-vs-string-id family as the QML lastActiveWinInGroup fix
//! (5a77d2da).

#include "wm/windowinfowrap.h"

// Qt
#include <QtTest>

using Latte::WindowSystem::WindowInfoWrap;

class WindowInfoWrapTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void waylandUuidParentMakesChildWindow();
    void x11DecimalParentMakesChildWindow();
    void emptyParentMakesMainWindow();
};

void WindowInfoWrapTest::waylandUuidParentMakesChildWindow()
{
    WindowInfoWrap winfo;
    //! the shape KWayland's PlasmaWindow::uuid() delivers
    winfo.setParentId(QByteArrayLiteral("{4a5b6c7d-8e9f-4a1b-9c2d-3e4f5a6b7c8d}"));

    QVERIFY(winfo.isChildWindow());
    QVERIFY(!winfo.isMainWindow());
}

void WindowInfoWrapTest::x11DecimalParentMakesChildWindow()
{
    WindowInfoWrap winfo;
    //! the shape xwindowinterface stores: QByteArray::number(transientId)
    winfo.setParentId(QByteArrayLiteral("77594625"));

    QVERIFY(winfo.isChildWindow());
    QVERIFY(!winfo.isMainWindow());
}

void WindowInfoWrapTest::emptyParentMakesMainWindow()
{
    WindowInfoWrap winfo;

    QVERIFY(winfo.isMainWindow());
    QVERIFY(!winfo.isChildWindow());
}

QTEST_MAIN(WindowInfoWrapTest)

#include "windowinfowraptest.moc"
