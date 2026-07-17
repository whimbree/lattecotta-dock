/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// A right-click on the dock must raise Latte's context menu. The menu is
// driven by a ContainmentActions plugin registered for the
// "RightButton;NoModifier" trigger; ContextMenuLayerQuickItem looks the plugin
// up by that exact trigger string. On Plasma 6 libplasma loads no
// [ActionPlugins] defaults of its own, and Latte's own save cycle makes it
// worse: the per-layout file is rewritten with only [Containments], so a
// containmentActions binding restored once is serialized away again. The
// right-click menu (and with it edit mode) works in this tree because of a
// chain with three links, each of which failed silently at least once during
// the port, so ALL of them are pinned here:
//
//   1. shell/package/contents/defaults maps RightButton;NoModifier to
//      org.kde.latte.contextmenu for fresh containments.
//   2. GenericLayout::addContainment re-asserts that mapping on every
//      containment wired to a layout (app/layout/genericlayout.cpp - the
//      comment there carries the save-cycle story).
//   3. KF6 derives a plugin's id from its .so FILE NAME, not the embedded
//      KPlugin/Id, so the built library must be named
//      org.kde.latte.contextmenu.so or findPluginById() misses and
//      setContainmentActions() silently no-ops (the CMakeLists of the plugin
//      carries the rename; this test pins the whole id chain).
//
// Transplanted from latte-dock-qt6's contextmenudefaulttest; their fallback
// lives in View, ours in GenericLayout, so check 2 is adapted.

#include <QtTest>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QObject>
#include <QRegularExpression>

#include <Plasma/ContainmentActions>

class ContextMenuDefaultTest : public QObject
{
    Q_OBJECT

private:
    static QString readFile(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString();
        }
        return QString::fromUtf8(file.readAll());
    }

private Q_SLOTS:
    void rightClickMapsToExpectedTrigger();
    void shippedDefaultsDeclareLatteContextMenu();
    void layoutReassertsDefaultRightButtonAction();
    void contextMenuPluginIsNamedAfterItsConfigId();
};

//! The trigger Latte registers must be exactly what a right-click produces,
//! otherwise the lookup in ContextMenuLayerQuickItem::mousePressEvent misses
//! and no menu shows.
void ContextMenuDefaultTest::rightClickMapsToExpectedTrigger()
{
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(1, 1), QPointF(1, 1),
                      Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QCOMPARE(Plasma::ContainmentActions::eventToString(&press),
             QStringLiteral("RightButton;NoModifier"));
}

//! The shipped defaults must map RightButton to Latte's standard context menu
//! plugin; the GenericLayout re-assert has to match this id.
void ContextMenuDefaultTest::shippedDefaultsDeclareLatteContextMenu()
{
    const QString src = readFile(QStringLiteral(LATTE_DEFAULTS_PATH));
    QVERIFY2(!src.isEmpty(), "shell defaults file must be readable.");
    QVERIFY2(src.contains(QStringLiteral("RightButton;NoModifier=org.kde.latte.contextmenu")),
             "shell defaults must map RightButton;NoModifier to org.kde.latte.contextmenu.");
}

//! GenericLayout must re-assert the default action on every containment it
//! adopts, so the menu survives the [ActionPlugins]-less save cycle (the
//! Plasma 6 regression that left containmentActions() empty).
void ContextMenuDefaultTest::layoutReassertsDefaultRightButtonAction()
{
    const QString src = readFile(QStringLiteral(GENERICLAYOUT_CPP_PATH));
    QVERIFY2(!src.isEmpty(), "genericlayout.cpp must be readable.");

    //! the exact re-assert call, whitespace-tolerant across the line break -
    //! separate substring checks were too weak (the trigger and the plugin id
    //! also appear on the neighboring lookup line, so a gutted call still
    //! passed them; caught during this pin's negative test)
    const QRegularExpression reassertCall(QStringLiteral(
        "setContainmentActions\\(QStringLiteral\\(\"RightButton;NoModifier\"\\),\\s*"
        "QStringLiteral\\(\"org\\.kde\\.latte\\.contextmenu\"\\)\\)"));
    QVERIFY2(src.contains(reassertCall),
             "GenericLayout must re-assert the default RightButton;NoModifier -> "
             "org.kde.latte.contextmenu containment action on adopted containments.");
}

//! KF6 derives a plugin's id from its .so file name, not from the embedded
//! KPlugin/Id field. The defaults, the layout re-assert and the metadata all
//! say "org.kde.latte.contextmenu", so the BUILT library must carry exactly
//! that basename and the metadata Id must agree - otherwise libplasma's
//! lookup-by-id fails and right-click loads no menu.
void ContextMenuDefaultTest::contextMenuPluginIsNamedAfterItsConfigId()
{
    const QFileInfo plugin(QStringLiteral(CONTEXTMENU_PLUGIN_FILE));
    QVERIFY2(plugin.exists(),
             qPrintable(QStringLiteral("the built context-menu plugin is missing: %1")
                            .arg(plugin.filePath())));
    QCOMPARE(plugin.completeBaseName(), QStringLiteral("org.kde.latte.contextmenu"));

    const QString metaSrc = readFile(QStringLiteral(CONTEXTMENU_METADATA_JSON));
    QVERIFY2(!metaSrc.isEmpty(), "context-menu plugin metadata json must be readable.");
    const QJsonDocument doc = QJsonDocument::fromJson(metaSrc.toUtf8());
    QVERIFY2(doc.isObject(), "context-menu plugin metadata json must parse.");
    const QString metadataId =
        doc.object().value(QStringLiteral("KPlugin")).toObject().value(QStringLiteral("Id")).toString();
    QCOMPARE(metadataId, plugin.completeBaseName());
}

QTEST_MAIN(ContextMenuDefaultTest)

#include "contextmenudefaulttest.moc"
