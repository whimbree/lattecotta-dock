/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The Layouts submenu uses QWidgetAction delegates that paint their layout
// name themselves. The child layout therefore measures only the radio button;
// the delegate's own sizeHint must account for the text and icon or Qt 6 sizes
// the entire submenu to the narrow radio-button column.

#include "layoutmenuitemwidget.h"

// Qt
#include <QAction>
#include <QMenu>
#include <QWidgetAction>
#include <QtTest>

class LayoutMenuItemWidgetTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void sizeHintIncludesPaintedColumns();
    void menuUsesDelegateWidth();
};

void LayoutMenuItemWidgetTest::sizeHintIncludesPaintedColumns()
{
    QMenu menu;
    QAction action(QStringLiteral("&Daily Driver Layout"), &menu);
    action.setCheckable(true);

    LayoutMenuItemWidget widget(&action, &menu);
    widget.ensurePolished();

    QString visibleText = action.text();
    visibleText.remove(QLatin1Char('&'));
    const int paintedTextWidth = widget.fontMetrics().horizontalAdvance(visibleText);
    const int paintedRadioWidth = widget.sizeHint().height() - 4;
    constexpr int paintedIconWidth{16 + 2};
    const int completePaintedWidth = paintedTextWidth
            + paintedRadioWidth
            + paintedIconWidth;

    QVERIFY2(widget.sizeHint().width() >= completePaintedWidth,
             "the delegate width must include its painted text, radio, and icon columns");
    QCOMPARE(widget.sizeHint(), widget.minimumSizeHint());
}

void LayoutMenuItemWidgetTest::menuUsesDelegateWidth()
{
    QMenu menu;
    QWidgetAction action(&menu);
    action.setText(QStringLiteral("&Daily Driver Layout"));
    action.setCheckable(true);

    auto *const delegate = new LayoutMenuItemWidget(&action, &menu);
    action.setDefaultWidget(delegate);
    menu.addAction(&action);
    menu.ensurePolished();
    delegate->ensurePolished();

    QVERIFY2(menu.sizeHint().width() >= delegate->sizeHint().width(),
             "the Layouts submenu must retain the delegate's complete width");
}

QTEST_MAIN(LayoutMenuItemWidgetTest)

#include "layoutmenuitemwidgettest.moc"
