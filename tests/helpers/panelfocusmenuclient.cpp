/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QApplication>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QShortcut>

#include <cstdio>

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QMainWindow window;
    window.setWindowTitle(QStringLiteral("LATTE PANEL FOCUS MENU CLIENT"));
    window.resize(700, 500);

    QMenu *const fileMenu = window.menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(QStringLiteral("Focus restoration action"), [] {
        std::fputs("PANEL_FOCUS_ACTION_TRIGGERED\n", stderr);
        std::fflush(stderr);
    });

    QMenu *const editMenu = window.menuBar()->addMenu(QStringLiteral("&Edit"));
    editMenu->addAction(QStringLiteral("Secondary action"), [] {});

    QShortcut focusDeliveryProbe(QKeySequence(Qt::Key_F12), &window);
    QObject::connect(&focusDeliveryProbe, &QShortcut::activated, [] {
        std::fputs("PANEL_FOCUS_KEY_DELIVERED\n", stderr);
        std::fflush(stderr);
    });

    window.show();
    return application.exec();
}
