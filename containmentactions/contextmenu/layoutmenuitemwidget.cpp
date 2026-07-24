/*
    SPDX-FileCopyrightText: 2021 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "layoutmenuitemwidget.h"

// local
#include "generictools.h"

// Qt
#include <QApplication>
#include <QDebug>
#include <QHBoxLayout>
#include <QPainter>
#include <QRadioButton>
#include <QStyleOptionMenuItem>

namespace {

constexpr int itemMargin{2};
constexpr int contentPadding{9};

}

LayoutMenuItemWidget::LayoutMenuItemWidget(QAction *action, QWidget *parent)
    : QWidget(parent),
      m_action(action)
{
    Q_ASSERT(m_action);

    auto *const layout = new QHBoxLayout(this);
    auto *const radioButton = new QRadioButton(this);
    radioButton->setCheckable(true);
    radioButton->setChecked(action->isChecked());
    radioButton->setVisible(action->isVisible() && action->isCheckable());

    layout->addWidget(radioButton);

    setMouseTracking(true);
}

void LayoutMenuItemWidget::setIcon(bool isBackgroundFile, const QString &iconName)
{
    m_isBackgroundFile = isBackgroundFile;
    m_iconName = iconName;
}

QSize LayoutMenuItemWidget::paintedContentSizeHint() const
{
    Q_ASSERT(m_action);

    QStyleOptionMenuItem option;
    option.initFrom(this);
    option.text = m_action->text();
    option.menuItemType = QStyleOptionMenuItem::Normal;
    option.menuHasCheckableItems = m_action->isCheckable();

    QSize contentSize = fontMetrics().size(
        Qt::TextSingleLine | Qt::TextShowMnemonic, m_action->text());
    contentSize.rheight() += contentPadding;
    contentSize.rwidth() += contentPadding;
    return style()->sizeFromContents(QStyle::CT_MenuItem, &option, contentSize, this);
}

QSize LayoutMenuItemWidget::sizeHint() const
{
    //! QWidgetAction asks its default widget for sizeHint(), while this custom
    //! delegate paints its icon and text outside the radio button's layout.
    //! Returning QWidget::sizeHint() therefore measures only the radio button
    //! and collapses the Qt 6 submenu to that narrow column.
    return paintedContentSizeHint();
}

QSize LayoutMenuItemWidget::minimumSizeHint() const
{
    return paintedContentSizeHint();
}

void LayoutMenuItemWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.save();
    QStyleOptionMenuItem opt;
    opt.initFrom(this);
    opt.text = m_action->text();
    opt.menuItemType = QStyleOptionMenuItem::Normal;
    opt.menuHasCheckableItems = false;

    if (rect().contains(mapFromGlobal(QCursor::pos()))) {
        opt.state |= QStyle::State_Selected;
    }

    //! background
    Latte::drawBackground(&painter, style(), opt);

    //! radio button
    const int radioSize = opt.rect.height() - 2 * itemMargin;
    QRect remained;

    if (qApp->layoutDirection() == Qt::LeftToRight) {
        remained = QRect(opt.rect.x() + radioSize,
                         opt.rect.y(),
                         opt.rect.width() - radioSize,
                         opt.rect.height());
    } else {
        remained = QRect(opt.rect.x(),
                         opt.rect.y(),
                         opt.rect.width() - radioSize,
                         opt.rect.height());
    }

    opt.rect  = remained;

    //! icon
    const int thickPadding = (opt.rect.height() - qMax(16, opt.maxIconWidth)) / 2; //old value 4
    remained = Latte::remainedFromLayoutIcon(opt, Qt::AlignLeft, 1, thickPadding);
    Latte::drawLayoutIcon(&painter,
                          opt,
                          m_isBackgroundFile,
                          m_iconName,
                          Qt::AlignLeft,
                          1,
                          thickPadding);
    opt.rect  = remained;

    //! text
    opt.text = opt.text.remove("&");
    //style()->drawControl(QStyle::CE_MenuItem, &opt, &painter, this);
    Latte::drawFormattedText(&painter, opt);

    painter.restore();
}

