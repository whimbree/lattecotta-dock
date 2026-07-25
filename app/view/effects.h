/*
    SPDX-FileCopyrightText: 2018 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef EFFECTS_H
#define EFFECTS_H

// local
#include "../plasma/extended/theme.h"
#include "floatingmaskhandshake.h"

// Qt
#include <QObject>
#include <QMargins>
#include <QPointer>
#include <QQuickView>
#include <QRect>
#include <QTimer>

#include <array>
#include <memory>

// Plasma
#include <KSvg/FrameSvg>
#include <Plasma/Theme>

namespace Latte {
class Corona;
class View;
}

namespace Latte {
namespace ViewPart {

class Effects: public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool animationsBlocked READ animationsBlocked NOTIFY animationsBlockedChanged)
    Q_PROPERTY(bool drawShadows READ drawShadows WRITE setDrawShadows NOTIFY drawShadowsChanged)
    Q_PROPERTY(bool drawEffects READ drawEffects WRITE setDrawEffects NOTIFY drawEffectsChanged)

    //! thickness shadow size when is drawn inside the window from qml
    Q_PROPERTY(int editShadow READ editShadow WRITE setEditShadow NOTIFY editShadowChanged)
    Q_PROPERTY(int innerShadow READ innerShadow WRITE setInnerShadow NOTIFY innerShadowChanged)

    Q_PROPERTY(bool backgroundAllCorners READ backgroundAllCorners WRITE setBackgroundAllCorners NOTIFY backgroundAllCornersChanged)
    Q_PROPERTY(bool backgroundRadiusEnabled READ backgroundRadiusEnabled WRITE setBackgroundRadiusEnabled NOTIFY backgroundRadiusEnabledChanged)
    Q_PROPERTY(int backgroundRadius READ backgroundRadius WRITE setBackgroundRadius NOTIFY backgroundRadiusChanged)
    Q_PROPERTY(float backgroundOpacity READ backgroundOpacity WRITE setBackgroundOpacity NOTIFY backgroundOpacityChanged)

    Q_PROPERTY(int popUpMargin READ popUpMargin NOTIFY popUpMarginChanged)

    Q_PROPERTY(QRect mask READ mask WRITE setMask NOTIFY maskChanged)
    Q_PROPERTY(QRect rect READ rect WRITE setRect NOTIFY rectChanged)
    Q_PROPERTY(QRect inputMask READ inputMask WRITE setInputMask NOTIFY inputMaskChanged)
    Q_PROPERTY(QRect appletsLayoutGeometry READ appletsLayoutGeometry WRITE setAppletsLayoutGeometry NOTIFY appletsLayoutGeometryChanged)

    Q_PROPERTY(KSvg::FrameSvg::EnabledBorders enabledBorders READ enabledBorders NOTIFY enabledBordersChanged)

    Q_PROPERTY(QQuickItem *panelBackgroundSvg READ panelBackgroundSvg WRITE setPanelBackgroundSvg NOTIFY panelBackgroundSvgChanged)

public:
    explicit Effects(Latte::View *parent);
    ~Effects() override;

    bool animationsBlocked() const;
    void setAnimationsBlocked(bool blocked);

    bool backgroundAllCorners() const;
    void setBackgroundAllCorners(bool allcorners);

    bool backgroundRadiusEnabled() const;
    void setBackgroundRadiusEnabled(bool enabled);

    bool drawShadows() const;
    void setDrawShadows(bool draw);

    bool drawEffects() const;
    void setDrawEffects(bool draw);

    void setForceTopBorder(bool draw);
    void setForceBottomBorder(bool draw);

    int editShadow() const;
    void setEditShadow(int shadow);

    int innerShadow() const;
    void setInnerShadow(int shadow);

    int backgroundRadius();
    void setBackgroundRadius(const int &radius);

    int popUpMargin() const;

    float backgroundOpacity() const;
    void setBackgroundOpacity(float opacity);

    QRect mask() const;
    void setMask(QRect area);

    QRect inputMask() const;
    void setInputMask(QRect area);

    //! The region actually handed to QWindow::setMask. Docks can retain a
    //! length-shrink union until settle; stable panels retain an old union for
    //! one submitted clearing frame. View::event still enforces inputMask()
    //! exactly during that damage-only extension. Read back over D-Bus.
    QRect appliedInputMask() const;
    bool floatingDamageMaskPending() const;
    quint64 floatingDamageMaskGeneration() const;

    QRect rect() const;
    void setRect(QRect area);

    QRect appletsLayoutGeometry() const;
    void setAppletsLayoutGeometry(const QRect &geom);

    KSvg::FrameSvg::EnabledBorders enabledBorders() const;

    QQuickItem *panelBackgroundSvg() const;
    void setPanelBackgroundSvg(QQuickItem *quickitem);

public Q_SLOTS:
    void applyFloatingPanelPresentation();
    void clearShadows();
    void updateShadows();
    void updateEffects();
    void updateEnabledBorders();

Q_SIGNALS:
    void animationsBlockedChanged();
    void appletsLayoutGeometryChanged();
    void backgroundAllCornersChanged();
    void backgroundCornersMaskChanged();
    void backgroundOpacityChanged();
    void backgroundRadiusEnabledChanged();
    void backgroundRadiusChanged();
    void drawShadowsChanged();
    void drawEffectsChanged();
    void editShadowChanged();
    void enabledBordersChanged();
    void maskChanged();
    void innerShadowChanged();
    void inputMaskChanged();
    void panelBackgroundSvgChanged();
    void popUpMarginChanged();
    void rectChanged();


private Q_SLOTS:
    void init();

    void onPopUpMarginChanged();

    void updateBackgroundContrastValues();
    void updateBackgroundCorners();

private:
    bool backgroundRadiusIsEnabled() const;
    bool floatingPresentationOwnsPaint() const;
    bool floatingPresentationOwnsInput() const;
    qreal currentMidValue(const qreal &max, const qreal &factor, const qreal &min) const;
    QRegion customMask(const QRect &rect);

    //! hand m_inputMask to the QWindow, keeping the window mask wide across a
    //! LENGTH-axis shrink so the vacated region's clearing damage is not clipped
    //! (Qt6 wayland couples mask() to submitted damage; see inputmaskflush.h)
    void applyInputMaskToWindow();
    void collapseFloatingDamageMask(quint64 submittedGeneration);
    void publishFloatingMaskGeneration(quint64 generation);

    //! the dock's length axis: horizontal for Top/Bottom docks, vertical for
    //! Left/Right - the axis whose shrink InputMaskFlush holds the union across
    Qt::Orientation lengthAxis() const;

private:
    bool m_animationsBlocked{false};
    bool m_backgroundAllCorners{false};
    bool m_backgroundRadiusEnabled{false};
    bool m_drawShadows{true};
    bool m_drawEffects{false};
    bool m_forceTopBorder{false};
    bool m_forceBottomBorder{false};

    bool m_hasTopLeftCorner{false};
    bool m_hasTopRightCorner{false};
    bool m_hasBottomLeftCorner{false};
    bool m_hasBottomRightCorner{false};

    int m_editShadow{0};
    int m_innerShadow{0};

    int m_backgroundRadius{-1};
    float m_backgroundOpacity{1.0};

    qreal m_backEffectContrast{1};
    qreal m_backEffectIntesity{1};
    qreal m_backEffectSaturation{1};

    QRect m_rect;
    QRect m_mask;
    QRect m_inputMask;
    //! the region actually handed to QWindow::setMask; kept at the union of
    //! the bands seen since the last settle so a LENGTH-shrinking band never
    //! clips the vacated region's clearing damage (inputmaskflush.h)
    QRect m_appliedInputMask;
    QRect m_appletsLayoutGeometry;
    QMargins m_shadowPaddingOffsets;
    quint64 m_floatingAnchorRevision{0};

    //! collapses m_appliedInputMask back to the exact band once the band stops
    //! changing (restarted on every band change, so it only fires when quiet)
    QTimer m_inputMaskSettleTimer;
    FloatingMaskHandshake::State m_floatingMaskHandshake;
    std::shared_ptr<FloatingMaskHandshake::RenderBridge>
        m_floatingMaskRenderBridge;
    std::array<QMetaObject::Connection, 2> m_renderConnections;

    QPointer<Latte::View> m_view;
    QPointer<Latte::Corona> m_corona;

    PlasmaExtended::CornerRegions m_cornersMaskRegion;

    Plasma::Theme m_theme;

    //only for the mask, not to actually paint
    KSvg::FrameSvg::EnabledBorders m_enabledBorders{KSvg::FrameSvg::AllBorders};

    //assigned from qml side in order to access the official panel background svg
    QQuickItem *m_panelBackgroundSvg{nullptr};

    //! Subtracted and United Mask regions
};

}
}

#endif
