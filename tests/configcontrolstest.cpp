/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The settings windows are QtQuick Controls 2 / PlasmaComponents 3, and the
// Controls-1 port left a class of property/type holdovers an import grep
// cannot see - they only surface when a config window loads. This pins the
// Qt6 rules behind each shape plus the shipped sources that depend on them:
//
//   * Qt6 marks base members final (Control.implicitWidth, AbstractButton
//     .icon): a QML type redeclaring one fails to LOAD entirely - why our
//     components TextField binds implicitWidth and ItemDelegate exposes
//     iconSource instead of icon.
//   * Controls 2 buttons have no "tooltip" property; the tooltip moved to
//     the ToolTip attached property - why our CheckBox declares its own
//     tooltip string and wires it through ToolTip.text.
//   * Controls 2 TabBar replaced currentTab with currentIndex.
//   * A Qt6 sequence-type model satisfies neither Array.isArray nor .get():
//     a ListModel is detected by probing get(), which the ComboBox rides.
//
// Transplanted from latte-dock-qt6 (tests/configcontrolstest.cpp at
// origin/main), with the final-property semantics driven through plain
// QtQuick.Controls (the rules are Qt's, not Plasma's, and the base types
// resolve without the Plasma import path) and the source scans pointed at
// this tree's declarativeimports/components files.

// Qt
#include <QFile>
#include <QObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QScopedPointer>
#include <QTest>

class ConfigControlsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    //! Qt6 platform rules
    void refuseRedeclaredImplicitWidth();
    void acceptImplicitWidthBinding();
    void refuseCustomIconPropertyOnButton();
    void refuseTooltipPropertyOnButton();
    void acceptToolTipAttachedProperty();
    void refuseCurrentTabOnTabBar();
    void acceptCurrentIndexOnTabBar();
    void distinguishListModelByGetMethod();

    //! shipped sources riding those rules
    void keepCheckBoxTooltipWiredToAttachedToolTip();
    void keepTextFieldTextColorAndImplicitWidthBinding();
    void keepItemDelegateIconSourceRename();

private:
    //! compile the fragment, returning the component error string (empty = compiles)
    QString compileError(const QString &qml);
    QString readShippedSource(const QString &path);
};

QString ConfigControlsTest::compileError(const QString &qml)
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(qml.toUtf8(), QUrl());
    return component.isError() ? component.errorString() : QString();
}

QString ConfigControlsTest::readShippedSource(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCritical() << "cannot read shipped source:" << path;
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

//! redeclaring implicitWidth is fatal in Qt6 (final on Control) - the exact
//! reason the components TextField switched to a binding
void ConfigControlsTest::refuseRedeclaredImplicitWidth()
{
    const QString err = compileError(QStringLiteral(
        "import QtQuick.Controls\n"
        "TextField { readonly property real implicitWidth: 5 }\n"));
    QVERIFY2(!err.isEmpty(), "redeclaring TextField.implicitWidth must fail to compile on Qt6");
    QVERIFY2(err.contains(QStringLiteral("final"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("expected a 'final' override error, got: %1").arg(err)));
}

void ConfigControlsTest::acceptImplicitWidthBinding()
{
    QCOMPARE(compileError(QStringLiteral(
        "import QtQuick.Controls\n"
        "TextField { implicitWidth: 5 }\n")), QString());
}

//! AbstractButton.icon is final in Qt6; a custom icon property collides and
//! the whole type fails to load - why ItemDelegate renamed to iconSource
void ConfigControlsTest::refuseCustomIconPropertyOnButton()
{
    const QString err = compileError(QStringLiteral(
        "import QtQuick.Controls\n"
        "Button { property string icon: \"x\" }\n"));
    QVERIFY2(!err.isEmpty(), "a custom 'icon' property on a Button must fail to compile on Qt6");
    QVERIFY2(err.contains(QStringLiteral("final"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("expected a 'final' override error, got: %1").arg(err)));
}

void ConfigControlsTest::refuseTooltipPropertyOnButton()
{
    QVERIFY2(!compileError(QStringLiteral(
                 "import QtQuick.Controls\n"
                 "Button { tooltip: \"x\" }\n")).isEmpty(),
             "Controls 2 Button has no 'tooltip' property; the assignment must not compile");
}

void ConfigControlsTest::acceptToolTipAttachedProperty()
{
    QCOMPARE(compileError(QStringLiteral(
        "import QtQuick.Controls\n"
        "Button { ToolTip.text: \"x\"; ToolTip.visible: false }\n")), QString());
}

void ConfigControlsTest::refuseCurrentTabOnTabBar()
{
    QVERIFY2(!compileError(QStringLiteral(
                 "import QtQuick.Controls\n"
                 "TabBar { currentTab: null }\n")).isEmpty(),
             "Controls 2 TabBar has no 'currentTab'; use currentIndex / currentItem");
}

void ConfigControlsTest::acceptCurrentIndexOnTabBar()
{
    QCOMPARE(compileError(QStringLiteral(
        "import QtQuick.Controls\n"
        "TabBar { currentIndex: 0 }\n")), QString());
}

//! a ListModel exposes get(); a JS array (and a Qt6 sequence model) does
//! not - the ComboBox must branch on get(), never Array.isArray
void ConfigControlsTest::distinguishListModelByGetMethod()
{
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(QByteArrayLiteral(
        "import QtQuick\n"
        "import QtQml.Models\n"
        "Item {\n"
        "    property bool listModelHasGet: false\n"
        "    property bool arrayHasGet: false\n"
        "    ListModel { id: lm; ListElement { name: \"a\" } }\n"
        "    property var arr: [1, 2, 3]\n"
        "    Component.onCompleted: {\n"
        "        listModelHasGet = (typeof lm.get === \"function\");\n"
        "        arrayHasGet = (typeof arr.get === \"function\");\n"
        "    }\n"
        "}\n"), QUrl());
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    QCOMPARE(QQmlProperty::read(root.data(), QStringLiteral("listModelHasGet")).toBool(), true);
    QCOMPARE(QQmlProperty::read(root.data(), QStringLiteral("arrayHasGet")).toBool(), false);
}

void ConfigControlsTest::keepCheckBoxTooltipWiredToAttachedToolTip()
{
    const QString src = readShippedSource(QStringLiteral(CHECKBOX_QML_PATH));
    QVERIFY(!src.isEmpty());
    QVERIFY2(src.contains(QStringLiteral("property string tooltip")),
             "the components CheckBox must declare a tooltip property (settings pages set it)");
    QVERIFY2(src.contains(QStringLiteral("ToolTip.text")),
             "the components CheckBox must wire tooltip through the ToolTip attached property");
}

void ConfigControlsTest::keepTextFieldTextColorAndImplicitWidthBinding()
{
    const QString src = readShippedSource(QStringLiteral(TEXTFIELD_QML_PATH));
    QVERIFY(!src.isEmpty());
    QVERIFY2(src.contains(QStringLiteral("property color textColor")),
             "the components TextField must declare textColor (the suffix label reads it)");
    QVERIFY2(!src.contains(QStringLiteral("readonly property int implicitWidth"))
                 && !src.contains(QStringLiteral("readonly property real implicitWidth")),
             "TextField must not redeclare implicitWidth (final on Qt6); bind it instead");
    QVERIFY2(src.contains(QStringLiteral("implicitWidth:")),
             "TextField must bind the inherited implicitWidth");
}

void ConfigControlsTest::keepItemDelegateIconSourceRename()
{
    const QString src = readShippedSource(QStringLiteral(ITEMDELEGATE_QML_PATH));
    QVERIFY(!src.isEmpty());
    QVERIFY2(src.contains(QStringLiteral("property string iconSource")),
             "ItemDelegate must expose iconSource");
    QVERIFY2(!src.contains(QStringLiteral("property string icon\n"))
                 && !src.contains(QStringLiteral("property string icon ")),
             "ItemDelegate must not declare a custom 'icon' property (collides with the final AbstractButton.icon)");
}

QTEST_GUILESS_MAIN(ConfigControlsTest)

#include "configcontrolstest.moc"
