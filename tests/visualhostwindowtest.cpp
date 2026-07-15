/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Pins Latte::visualHostWindowOf, the QObject-parent-chain resolution the
//! corona screen forwarding uses to find the window hosting an
//! applet-created dialog. The upstream parenting semantics it builds on are
//! pinned in tests/contracts/appletwindowparentingtest.cpp; this test pins
//! the Latte-side walk itself - most importantly that it reads
//! QObject::parent() and not QWindow::parent(), whose shadowing overload is
//! null for QML-declared dialogs and silently turned the first draft of the
//! walk into a matches-nothing loop.

#include "tools/commontools.h"

// Qt
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

class VisualHostWindowTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void resolvesTheDeclaringItemsWindow();
    void unhostedWindowResolvesToNull();
};

static QObject *createDeclaredDialog(QQmlEngine *engine)
{
    //! registering twice is harmless, so each test is order-independent
    qmlRegisterType<QQuickWindow>("org.kde.latte.hostwindowtest", 1, 0, "PlainQuickWindow");

    QQmlComponent component(engine);
    component.setData(R"(
        import QtQuick 2.15
        import org.kde.latte.hostwindowtest 1.0

        Item {
            PlainQuickWindow {
                objectName: "appletStyleDialog"
                visible: false
            }
        }
    )", QUrl(QStringLiteral("visualhostwindow.qml")));

    QObject *root = component.create();

    if (!root) {
        qCritical() << component.errorString();
    }

    return root;
}

void VisualHostWindowTest::resolvesTheDeclaringItemsWindow()
{
    QQmlEngine engine;
    QScopedPointer<QObject> root(createDeclaredDialog(&engine));
    QVERIFY(root);

    auto *declaringItem = qobject_cast<QQuickItem *>(root.data());
    auto *dialog = root->findChild<QQuickWindow *>(QStringLiteral("appletStyleDialog"));
    QVERIFY(declaringItem);
    QVERIFY(dialog);

    //! host the declaring item in a real window, the way an applet item
    //! lives in its containment view
    QQuickWindow hostWindow;
    declaringItem->setParentItem(hostWindow.contentItem());

    QCOMPARE(Latte::visualHostWindowOf(dialog), &hostWindow);
}

void VisualHostWindowTest::unhostedWindowResolvesToNull()
{
    QQmlEngine engine;
    QScopedPointer<QObject> root(createDeclaredDialog(&engine));
    QVERIFY(root);

    auto *dialog = root->findChild<QQuickWindow *>(QStringLiteral("appletStyleDialog"));
    QVERIFY(dialog);

    //! the declaring item is in no window: the walk must abstain, not crash
    //! and not return the dialog itself
    QCOMPARE(Latte::visualHostWindowOf(dialog), nullptr);

    //! a window with no QML lineage at all abstains too
    QQuickWindow standalone;
    QCOMPARE(Latte::visualHostWindowOf(&standalone), nullptr);
}

QTEST_MAIN(VisualHostWindowTest)

#include "visualhostwindowtest.moc"
