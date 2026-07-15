/*
    SPDX-FileCopyrightText: 2023 David Edmundson <davidedmundson@kde.org>
    SPDX-License-Identifier: LGPL-2.0-or-later
*/

//! Vendored from libplasma v6.6.5 src/plasmaquick/windowresizehandler.{h,cpp}
//! (the helper AppletPopup uses to make plasmashell's applet popups
//! interactively resizable). libplasma exports the class but treats it as
//! private plasmaquick API, and the same symbol name is loaded in-process
//! through libplasma itself, so the copy lives in the Latte::Quick namespace.
//! One deliberate addition on top of upstream: the resizeStarted() signal,
//! emitted when a press actually starts a system resize, so the owning
//! dialog can suspend its own re-anchoring for the duration of the drag.

#ifndef LATTEWINDOWRESIZEHANDLER_H
#define LATTEWINDOWRESIZEHANDLER_H

#include <QMargins>
#include <QObject>
#include <memory>

class QWindow;

namespace Latte {
namespace Quick {

class WindowResizeHandlerPrivate;

class WindowResizeHandler : public QObject
{
    Q_OBJECT
public:
    WindowResizeHandler(QWindow *parent);
    ~WindowResizeHandler() override;

    void setMargins(const QMargins &margins);
    QMargins margins() const;

    void setActiveEdges(Qt::Edges edges);
    Qt::Edges activeEdges() const;

    bool eventFilter(QObject *watched, QEvent *event) override;

Q_SIGNALS:
    //! Latte addition: fired right after startSystemResize() hands the drag
    //! to the compositor. From that moment the client stops receiving mouse
    //! events until the grab ends, so this is the only reliable "resize
    //! session began" notification the dialog can get.
    void resizeStarted();

private:
    std::unique_ptr<WindowResizeHandlerPrivate> d;
};

}
}

#endif
