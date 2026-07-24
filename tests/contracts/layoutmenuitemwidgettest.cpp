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
#include <QProxyStyle>
#include <QWidgetAction>
#include <QtTest>

namespace {

class OddHeightMenuStyle final : public QProxyStyle
{
public:
    QSize sizeFromContents(ContentsType type,
                           const QStyleOption *option,
                           const QSize &size,
                           const QWidget *widget) const override
    {
        QSize result = QProxyStyle::sizeFromContents(type, option, size, widget);
        if (type == QStyle::CT_MenuItem && result.height() % 2 == 0) {
            result.rheight() += 1;
        }
        return result;
    }
};

}

class LayoutMenuItemWidgetTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void sizeHintIncludesPaintedColumns();
    void menuUsesDelegateWidth();
};

void LayoutMenuItemWidgetTest::sizeHintIncludesPaintedColumns()
{
    OddHeightMenuStyle oddHeightStyle;
    QMenu menu;
    menu.setStyle(&oddHeightStyle);
    QAction action(QStringLiteral("&Daily Driver Layout"), &menu);
    action.setCheckable(true);

    LayoutMenuItemWidget widget(&action, &menu);
    widget.setStyle(&oddHeightStyle);
    widget.ensurePolished();

    const QSize hint = widget.sizeHint();
    QCOMPARE(hint.height() % 2, 1);

    QString visibleText = action.text();
    visibleText.remove(QLatin1Char('&'));
    const int paintedTextWidth = widget.fontMetrics().horizontalAdvance(visibleText);
    constexpr int totalRadioMargin{4};
    const int paintedRadioWidth = hint.height() - totalRadioMargin;
    constexpr int minimumIconWidth{16};
    constexpr int totalIconLengthMargin{2};
    const int iconThicknessMargin = (hint.height() - minimumIconWidth) / 2;
    const int paintedIconWidth = hint.height() - (2 * iconThicknessMargin)
            + totalIconLengthMargin;
    const int completePaintedWidth = paintedTextWidth
            + paintedRadioWidth
            + paintedIconWidth;

    QVERIFY2(hint.width() >= completePaintedWidth,
             "the delegate width must include its painted text, radio, and icon columns");
    QCOMPARE(hint, widget.minimumSizeHint());
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
