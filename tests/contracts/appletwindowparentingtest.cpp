/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Upstream contract (tests/contracts/README.md): how Qt Quick parents a
//! QQuickWindow-derived window declared inside an Item - the shape of every
//! applet-created dialog (the comic applet's full-size viewer is the live
//! case: a PlasmaCore.Dialog declared in the applet's QML).
//!
//! Two halves, both keyed on by Corona::eventFilter's screen forwarding for
//! applet dialogs (plan item "Applet-created dialogs open on the wrong
//! screen", caught live 2026-07-15):
//!
//! - NEGATIVE: no transientParent is assigned. Qt 6 moved the Qt 5
//!   "declared-inside-an-item windows become transient for the item's
//!   window" magic into QQuickWindowQmlImpl (the QML `Window {}` type);
//!   plain QQuickWindow subclasses registered from C++ - PlasmaCore.Dialog
//!   among them - no longer get it. A fix keyed on transientParent (the
//!   first draft of the corona filter was) matches nothing.
//! - POSITIVE: the window's QObject parent IS the declaring item (resource
//!   child via data_append), so the item's window() is the reliable route
//!   from an applet dialog to the hosting Latte::View.
//!
//! qmltest cannot read QObject::parent() of a window (the QML `parent`
//! property on a Window is the QWindow parent), so this is a C++ test.

// Qt
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

class AppletWindowParentingTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void windowDeclaredInsideItemIsResourceChildWithoutTransientParent();
};

void AppletWindowParentingTest::windowDeclaredInsideItemIsResourceChildWithoutTransientParent()
{
    qmlRegisterType<QQuickWindow>("org.kde.latte.contracttest", 1, 0, "PlainQuickWindow");

    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick 2.15
        import org.kde.latte.contracttest 1.0

        Item {
            PlainQuickWindow {
                objectName: "appletStyleDialog"
                visible: false
            }
        }
    )", QUrl(QStringLiteral("appletwindowparenting.qml")));

    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));

    QQuickItem *declaringItem = qobject_cast<QQuickItem *>(root.data());
    QVERIFY(declaringItem);

    auto *window = root->findChild<QQuickWindow *>(QStringLiteral("appletStyleDialog"));
    QVERIFY(window);

    //! POSITIVE half: the declaring item owns the window as a resource
    //! child. QWindow::parent() shadows QObject::parent() with the
    //! window-parent overload, so read the QObject one explicitly - that is
    //! also the pointer the corona filter walks.
    QCOMPARE(window->QObject::parent(), static_cast<QObject *>(declaringItem));

    //! NEGATIVE half: nothing assigns a transient parent (and the declaring
    //! item has no window here, exactly like an applet loaded before its
    //! containment view maps - there is no windowChanged catch-up hook
    //! either, that was the Qt 5 mechanism)
    QCOMPARE(window->transientParent(), nullptr);
}

QTEST_MAIN(AppletWindowParentingTest)

#include "appletwindowparentingtest.moc"
