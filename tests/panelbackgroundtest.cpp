/*
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The shell view wrapper (shell/package/contents/views/Panel.qml) is a
// KSvg.FrameSvgItem sized to the WHOLE view, and the view is deliberately
// oversized to the maximum parabolic-zoom thickness. Upstream picked the
// wrapper's imagePath with
//
//     containment.backgroundHints === PlasmaCore.Types.NoBackground
//         ? "" : "widgets/panel-background"
//
// On Plasma 5 the containment graphic object exposed backgroundHints, Latte
// set NoBackground, and the wrapper drew nothing. On Plasma 6 the property
// is gone, the comparison reads undefined, the ternary falls through and
// the SVG paints across the whole oversized view - on Wayland (no X11
// shape-mask to hide the overflow) a dark band over the zoom reserve.
// Our Panel.qml keeps imagePath empty and lets the containment own all
// background drawing (the comment block in the file carries the story).
//
// Transplanted from latte-dock-qt6 (tests/panelbackgroundtest.cpp at
// 81384003, github.com/CaptSilver/latte-dock-qt6; both trees fixed it the same way): two cases document the
// legacy check's fallthrough semantics in a live QML engine, one scans the
// shipped wrapper so the broken check cannot come back.

// Qt
#include <QFile>
#include <QObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QRegularExpression>
#include <QScopedPointer>
#include <QTest>

class PanelBackgroundTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void paintBandWhenBackgroundHintsIsAbsent();
    void suppressBandWhenBackgroundHintsSaysNoBackground();
    void keepShippedWrapperFreeOfPanelBackground();

private:
    //! evaluate the legacy imagePath ternary against a containment built
    //! from the given QML fragment, returning the imagePath it would use
    QString evaluateLegacyImagePath(const QString &containmentFragment);
};

QString PanelBackgroundTest::evaluateLegacyImagePath(const QString &containmentFragment)
{
    QQmlEngine engine;
    QQmlComponent component(&engine);

    //! noBackground stands in for PlasmaCore.Types.NoBackground; the exact
    //! value is irrelevant, only that an ABSENT property can never compare
    //! equal to it
    const QString qml = QStringLiteral(R"(
        import QtQuick 2.15
        Item {
            id: root
            property int noBackground: 7
            property QtObject containment: %1
            readonly property string imagePath:
                containment && containment.backgroundHints === root.noBackground ? "" : "widgets/panel-background"
        }
    )").arg(containmentFragment);

    component.setData(qml.toUtf8(), QUrl());
    QScopedPointer<QObject> root(component.create());
    if (!root) {
        qCritical() << "legacy fragment failed to create:" << component.errorString();
        return QStringLiteral("<create-failed>");
    }

    return QQmlProperty::read(root.data(), QStringLiteral("imagePath")).toString();
}

//! Plasma 6: no backgroundHints on the containment graphic object, the
//! ternary falls through to the SVG - the band. Documents the exact
//! regression the shipped fix guards.
void PanelBackgroundTest::paintBandWhenBackgroundHintsIsAbsent()
{
    QCOMPARE(evaluateLegacyImagePath(QStringLiteral("QtObject { }")),
             QStringLiteral("widgets/panel-background"));
}

//! Plasma 5 shape: with the property present and equal, the wrapper drew
//! nothing. Kept so the test says the logic was once correct and broke
//! only because the property disappeared.
void PanelBackgroundTest::suppressBandWhenBackgroundHintsSaysNoBackground()
{
    QCOMPARE(evaluateLegacyImagePath(QStringLiteral("QtObject { property int backgroundHints: 7 }")),
             QString());
}

//! The fix: the shipped wrapper must not depend on backgroundHints and must
//! not paint a panel background of its own.
void PanelBackgroundTest::keepShippedWrapperFreeOfPanelBackground()
{
    QFile file(QStringLiteral(PANEL_QML_PATH));
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(file.errorString()));
    const QString source = QString::fromUtf8(file.readAll());

    //! the active imagePath assignment is empty (comments are ignored by
    //! the line anchors)
    static const QRegularExpression activeImagePath(QStringLiteral("^\\s*imagePath:\\s*\"\"\\s*$"),
                                                    QRegularExpression::MultilineOption);
    QVERIFY2(activeImagePath.match(source).hasMatch(),
             "Panel.qml must set imagePath to an empty string so it never paints a band.");

    //! the broken backgroundHints check must not come back as an active line
    static const QRegularExpression activeBackgroundHints(QStringLiteral("^\\s*imagePath:.*backgroundHints"),
                                                          QRegularExpression::MultilineOption);
    QVERIFY2(!activeBackgroundHints.match(source).hasMatch(),
             "Panel.qml must not gate imagePath on containment.backgroundHints (absent on Plasma 6).");
}

QTEST_GUILESS_MAIN(PanelBackgroundTest)

#include "panelbackgroundtest.moc"
