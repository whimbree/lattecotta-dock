/*
    SPDX-FileCopyrightText: 2011 Aaron Seigo <aseigo@kde.org>
    SPDX-FileCopyrightText: 2013 Marco Martin <mart@kde.org>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

// Extra shadow padding derived from plasma-workspace
// (shell/panelshadows.h at 4c3ace3dfc7b06b3107b52b6e09508be14e73e8a,
// invent.kde.org/plasma/plasma-workspace; floating-panel refactor
// f7ee03d065b4e293746248f749a7965c4321b1cb).

#pragma once

#include <QMargins>
#include <QSet>

#include <KSvg/FrameSvg>
#include <KSvg/Svg>

class PanelShadows : public KSvg::Svg
{
    Q_OBJECT

public:
    explicit PanelShadows(QObject *parent = nullptr, const QString &prefix = QStringLiteral("widgets/panel-background"));
    ~PanelShadows() override;

    static PanelShadows *self();

    void addWindow(
        QWindow *window,
        KSvg::FrameSvg::EnabledBorders enabledBorders =
            KSvg::FrameSvg::AllBorders,
        const QMargins &extraPadding = {});
    void removeWindow(QWindow *window);

    void setEnabledBorders(QWindow *window, KSvg::FrameSvg::EnabledBorders enabledBorders = KSvg::FrameSvg::AllBorders);
    void setExtraPadding(QWindow *window, const QMargins &extraPadding);

private:
    class Private;
    Private *const d;
};
