/*
    SPDX-FileCopyrightText: 2023 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef AUTOHIDESCREENEDGE_H
#define AUTOHIDESCREENEDGE_H

#include <memory>

#include <QObject>
#include <QPointer>

namespace Latte {
class View;

namespace ViewPart {

class WaylandAutoHideScreenEdgeV1;
class WaylandScreenEdgeManagerV1;

//! Binds the real dock layer surface to KWin's compositor-owned screen edge.
//!
//! Adapted from plasma-workspace (shell/autohidescreenedge.cpp at
//! 4c3ace3dfc7b06b3107b52b6e09508be14e73e8a,
//! invent.kde.org/plasma/plasma-workspace).
class AutoHideScreenEdge final : public QObject
{
    Q_OBJECT

public:
    explicit AutoHideScreenEdge(Latte::View *view, QObject *parent = nullptr);
    ~AutoHideScreenEdge() override;

    [[nodiscard]] bool isArmed() const;
    [[nodiscard]] bool isEnabled() const;
    [[nodiscard]] bool isRegistered() const;
    [[nodiscard]] bool isSupported() const;

    void setArmed(bool armed);
    void setEnabled(bool enabled);

Q_SIGNALS:
    void armedChanged();
    void registeredChanged();
    void supportedChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    [[nodiscard]] bool createEdge();
    void destroyEdge();
    void refreshRegistration();

    QPointer<Latte::View> m_view;
    std::unique_ptr<WaylandScreenEdgeManagerV1> m_manager;
    std::unique_ptr<WaylandAutoHideScreenEdgeV1> m_edge;
    bool m_armed{false};
    bool m_enabled{false};
    bool m_reportedUnsupported{false};
};

}
}

#endif // AUTOHIDESCREENEDGE_H
