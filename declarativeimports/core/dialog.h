/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef LATTEDIALOG_H
#define LATTEDIALOG_H

// Qt
#include <QEvent>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QTimer>

#include <QMetaObject>

#include <array>

// Plasma
#include <Plasma/Plasma>
#include <PlasmaQuick/Dialog>

namespace Plasma {
class Applet;
}

namespace Latte {
namespace Quick {

class WindowResizeHandler;

class Dialog : public PlasmaQuick::Dialog {
    Q_OBJECT
    Q_PROPERTY (bool containsMouse READ containsMouse NOTIFY containsMouseChanged)

    //! it is used instead of location property in order to not break borders drawing
    Q_PROPERTY(Plasma::Types::Location edge READ edge WRITE setEdge NOTIFY edgeChanged)

    //! whether popupPosition() may clamp the popup inside the containment's
    //! applets-layout area. Plasma applet popups want that (they belong to a
    //! specific applet's spot in the dock); the window previews do not: a wide
    //! preview for a task near the dock's end would be shoved sideways away
    //! from its task icon instead of centering on it
    Q_PROPERTY(bool respectsAppletsLayoutGeometry READ respectsAppletsLayoutGeometry WRITE setRespectsAppletsLayoutGeometry NOTIFY respectsAppletsLayoutGeometryChanged)

    //! resizable persistent popups (continuation feature, from libplasma
    //! v6.6.5 AppletPopup): the Plasma::Applet whose own config group
    //! persists popupWidth/popupHeight - the SAME keys plasmashell uses, so
    //! sizes travel with layout export and survive moves between shells.
    //! Setting it (on an AppletPopup-typed dialog only) arms interactive
    //! edge-drag resizing and loads any persisted size.
    Q_PROPERTY(QObject *applet READ applet WRITE setApplet NOTIFY appletChanged)

    //! the persisted popup CONTENT size (margins excluded, mirroring
    //! plasmashell's "save size without margins, so we're robust against
    //! theme changes"). Empty when the applet has no custom size. Writable
    //! so the sizing-chain tests can drive it; runtime writers are the
    //! resize session and the config watcher.
    Q_PROPERTY(QSize customPopupSize READ customPopupSize WRITE setCustomPopupSize NOTIFY customPopupSizeChanged)

public:
    explicit Dialog(QQuickItem *parent = nullptr);

    bool containsMouse() const;

    Plasma::Types::Location edge() const;
    void setEdge(const Plasma::Types::Location &edge);

    bool respectsAppletsLayoutGeometry() const;
    void setRespectsAppletsLayoutGeometry(bool respects);

    QObject *applet() const;
    void setApplet(QObject *applet);

    QSize customPopupSize() const;
    void setCustomPopupSize(const QSize &size);

    QPoint popupPosition(QQuickItem *item, const QSize &size) override;

Q_SIGNALS:
    void containsMouseChanged();
    void edgeChanged();
    void respectsAppletsLayoutGeometryChanged();
    void appletChanged();
    void customPopupSizeChanged();

protected:
  //  void adjustGeometry(const QRect &geom) override;

    bool event(QEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *e) override;

private Q_SLOTS:
    void setContainsMouse(bool contains);
    void updatePopUpEnabledBorders();

    void onVisualParentChanged();
    void onMainItemChanged();
    void updateGeometry();

private:
    bool isRespectingAppletsLayoutGeometry() const;
    QRect appletsLayoutGeometryFromContainment() const;

    int appletsPopUpMargin() const;

    QSize pendingContentSize() const;
    void syncAnchoredWaylandPosition();
    void updateSlideEffect(const QRect &globalGeometry);

    QMargins frameMargins() const;
    void updateResizeSupport();
    void onSystemResizeStarted();
    void finishSystemResize();
    void loadPersistedPopupSize();
    void persistPopupSize();

private:
    bool m_containsMouse{false};
    bool m_respectsAppletsLayoutGeometry{true};

    Plasma::Types::Location m_edge{Plasma::Types::BottomEdge};

    std::array<QMetaObject::Connection, 2> m_visualParentConnections;
    std::array<QMetaObject::Connection, 2> m_mainItemConnections;

    //! resizable persistent popups state
    QPointer<Plasma::Applet> m_applet;
    WindowResizeHandler *m_resizeHandler{nullptr};
    QSize m_customPopupSize;
    QSize m_preResizeCustomSize;
    QSize m_resizeStartContentSize;
    bool m_inSystemResize{false};
    QTimer m_resizeSettleTimer;
};

}
}

#endif
