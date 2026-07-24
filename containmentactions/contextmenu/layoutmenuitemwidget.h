/*
    SPDX-FileCopyrightText: 2021 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef LAYOUTMENUITEMWIDGET_H
#define LAYOUTMENUITEMWIDGET_H

// Qt
#include <QAction>
#include <QWidget>
#include <QWidgetAction>
#include <QPaintEvent>
#include <QStyleOptionMenuItem>


class LayoutMenuItemWidget : public QWidget {
    Q_OBJECT

public:
    LayoutMenuItemWidget(QAction *action, QWidget *parent);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    void setIcon(bool isBackgroundFile, const QString &iconName);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QSize paintedContentSizeHint() const;

    QAction *m_action{nullptr};
    bool m_isBackgroundFile{false};
    QString m_iconName;
};

#endif
