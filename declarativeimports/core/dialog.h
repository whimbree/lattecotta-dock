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

#include <QMetaObject>

#include <array>

// Plasma
#include <Plasma/Plasma>
#include <PlasmaQuick/Dialog>

namespace Latte {
namespace Quick {

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

public:
    explicit Dialog(QQuickItem *parent = nullptr);

    bool containsMouse() const;

    Plasma::Types::Location edge() const;
    void setEdge(const Plasma::Types::Location &edge);

    bool respectsAppletsLayoutGeometry() const;
    void setRespectsAppletsLayoutGeometry(bool respects);

    QPoint popupPosition(QQuickItem *item, const QSize &size) override;

Q_SIGNALS:
    void containsMouseChanged();
    void edgeChanged();
    void respectsAppletsLayoutGeometryChanged();

protected:
  //  void adjustGeometry(const QRect &geom) override;

    bool event(QEvent *e) override;

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

private:
    bool m_containsMouse{false};
    bool m_respectsAppletsLayoutGeometry{true};

    //! last anchored position sent to the plasmashell surface; re-asserted
    //! after base-class Move/Expose handling, which re-sends the stale
    //! QWindow::position() (frozen on wayland) and would undo it
    bool m_hasPendingWaylandPosition{false};
    QPoint m_pendingWaylandPosition;

    Plasma::Types::Location m_edge{Plasma::Types::BottomEdge};

    std::array<QMetaObject::Connection, 2> m_visualParentConnections;
    std::array<QMetaObject::Connection, 2> m_mainItemConnections;

};

}
}

#endif
