/*
    SPDX-FileCopyrightText: 2018 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "effects.h"

// local
#include <config-latte.h>
#include <coretypes.h>
#include "inputmaskflush.h"
#include "panelshadows_p.h"
#include "view.h"
#include "../lattecorona.h"
#include "../wm/abstractwindowinterface.h"

// local tools
#include "../tools/commontools.h"

// Qt
#include <QRegion>

// KDE
#include <KWindowEffects>
#include <KWindowSystem>


namespace Latte {
namespace ViewPart {

Effects::Effects(Latte::View *parent)
    : QObject(parent),
      m_view(parent)
{
    m_corona = qobject_cast<Latte::Corona *>(m_view->corona());

    init();
}

Effects::~Effects()
{
}

void Effects::init()
{
    connect(this, &Effects::backgroundOpacityChanged, this, &Effects::updateEffects);
    connect(this, &Effects::backgroundOpacityChanged, this, &Effects::updateBackgroundContrastValues);
    connect(this, &Effects::backgroundCornersMaskChanged, this, &Effects::updateEffects);
    connect(this, &Effects::backgroundRadiusEnabledChanged, this, &Effects::updateEffects);
    connect(this, &Effects::drawEffectsChanged, this, &Effects::updateEffects);
    connect(this, &Effects::enabledBordersChanged, this, &Effects::updateEffects);
    connect(this, &Effects::rectChanged, this, &Effects::updateEffects);


    connect(this, &Effects::backgroundRadiusChanged, this, &Effects::updateBackgroundCorners);

    connect(this, &Effects::drawShadowsChanged, this, [&]() {
        if (m_view->behaveAsPlasmaPanel()) {
            updateEnabledBorders();
        }
    });

    connect(this, &Effects::backgroundAllCornersChanged, this, &Effects::updateEnabledBorders);

    connect(this, &Effects::popUpMarginChanged, this, &Effects::onPopUpMarginChanged);

    connect(m_view, &Latte::View::alignmentChanged, this, &Effects::updateEnabledBorders);
    connect(m_view, &Latte::View::maxLengthChanged, this, &Effects::updateEnabledBorders);
    connect(m_view, &Latte::View::offsetChanged, this, &Effects::updateEnabledBorders);
    connect(m_view, &Latte::View::screenEdgeMarginEnabledChanged, this, &Effects::updateEnabledBorders);
    connect(m_view, &Latte::View::behaveAsPlasmaPanelChanged, this, &Effects::updateEffects);
    connect(this, &Effects::drawShadowsChanged, this, &Effects::updateShadows);
    connect(m_view, &Latte::View::behaveAsPlasmaPanelChanged, this, &Effects::updateShadows);
    connect(m_view, &Latte::View::layoutChanged, this, &Effects::onPopUpMarginChanged);

    connect(&m_theme, &Plasma::Theme::themeChanged, this, [&]() {
        updateBackgroundContrastValues();
        updateEffects();
    });

    //! Once the band stops changing, narrow the window mask from the union it
    //! was kept at during the shrink (inputmaskflush.h) back to the exact band,
    //! so steady-state input hit-testing and libplasma popup anchoring read the
    //! real band. 100ms clears three 30Hz frames of animation/hitch; the timer
    //! is restarted on every band change so it only fires when the band is quiet.
    m_inputMaskSettleTimer.setSingleShot(true);
    m_inputMaskSettleTimer.setInterval(100);
    connect(&m_inputMaskSettleTimer, &QTimer::timeout, this, [this]() {
        if (!m_view || !InputMaskFlush::needsSettleCollapse(m_appliedInputMask, m_inputMask)) {
            return;
        }

        m_appliedInputMask = m_inputMask;
        m_view->setMask(m_appliedInputMask);
    });
}

bool Effects::animationsBlocked() const
{
    return m_animationsBlocked;
}

void Effects::setAnimationsBlocked(bool blocked)
{
    if (m_animationsBlocked == blocked) {
        return;
    }

    m_animationsBlocked = blocked;
    Q_EMIT animationsBlockedChanged();
}

bool Effects::backgroundAllCorners() const
{
    return m_backgroundAllCorners;
}

void Effects::setBackgroundAllCorners(bool allcorners)
{
    if (m_backgroundAllCorners == allcorners) {
        return;
    }

    m_backgroundAllCorners = allcorners;
    Q_EMIT backgroundAllCornersChanged();
}

bool Effects::backgroundRadiusEnabled() const
{
    return m_backgroundRadiusEnabled;
}

void Effects::setBackgroundRadiusEnabled(bool enabled)
{
    if (m_backgroundRadiusEnabled == enabled) {
        return;
    }

    m_backgroundRadiusEnabled = enabled;
    Q_EMIT backgroundRadiusEnabledChanged();
}

bool Effects::drawShadows() const
{
    return m_drawShadows;
}

void Effects::setDrawShadows(bool draw)
{
    if (m_drawShadows == draw) {
        return;
    }

    m_drawShadows = draw;

    Q_EMIT drawShadowsChanged();
}

bool Effects::drawEffects() const
{
    return m_drawEffects;
}

void Effects::setDrawEffects(bool draw)
{
    if (m_drawEffects == draw) {
        return;
    }

    m_drawEffects = draw;

    Q_EMIT drawEffectsChanged();
}

void Effects::setForceBottomBorder(bool draw)
{
    if (m_forceBottomBorder == draw) {
        return;
    }

    m_forceBottomBorder = draw;
    updateEnabledBorders();
}

void Effects::setForceTopBorder(bool draw)
{
    if (m_forceTopBorder == draw) {
        return;
    }

    m_forceTopBorder = draw;
    updateEnabledBorders();
}

int Effects::backgroundRadius()
{
    return m_backgroundRadius;
}

void Effects::setBackgroundRadius(const int &radius)
{
    if (m_backgroundRadius == radius) {
        return;
    }

    m_backgroundRadius = radius;
    Q_EMIT backgroundRadiusChanged();
}

float Effects::backgroundOpacity() const
{
    return m_backgroundOpacity;
}

void Effects::setBackgroundOpacity(float opacity)
{
    if (m_backgroundOpacity == opacity) {
        return;
    }

    m_backgroundOpacity = opacity;

    updateBackgroundContrastValues();
    Q_EMIT backgroundOpacityChanged();
}

int Effects::editShadow() const
{
    return m_editShadow;
}

void Effects::setEditShadow(int shadow)
{
    if (m_editShadow == shadow) {
        return;
    }

    m_editShadow = shadow;
    Q_EMIT editShadowChanged();
}

int Effects::innerShadow() const
{
    return m_innerShadow;
}

void Effects::setInnerShadow(int shadow)
{
    if (m_innerShadow == shadow)
        return;

    m_innerShadow = shadow;

    Q_EMIT innerShadowChanged();
}

int Effects::popUpMargin() const
{
    return m_view->layout() ? m_view->layout()->popUpMargin() : -1/*default*/;
}

QRect Effects::rect() const
{
    return m_rect;
}

void Effects::setRect(QRect area)
{
    if (m_rect == area) {
        return;
    }

    m_rect = area;

    Q_EMIT rectChanged();
}

QRect Effects::mask() const
{
    return m_mask;
}

void Effects::setMask(QRect area)
{
    if (m_mask == area)
        return;

    //! the value is what matters here: QML (the visibility overlay, the
    //! debug window) reads it back and the visibility manager stamps
    //! ISHIDDENMASK through it; under wayland the WINDOW mask is owned by
    //! setInputMask() alone (input + damage, see there). The old X11 arms
    //! that painted a visual mask from this value died with the backend.
    m_mask = area;
    Q_EMIT maskChanged();
}

QRect Effects::inputMask() const
{
    return m_inputMask;
}

void Effects::setInputMask(QRect area)
{
    if (m_inputMask == area) {
        return;
    }

    m_inputMask = area;
    applyInputMaskToWindow();

    Q_EMIT inputMaskChanged();
}

QRect Effects::appliedInputMask() const
{
    return m_appliedInputMask;
}

void Effects::applyInputMaskToWindow()
{
    if (!m_view) {
        return;
    }

    //! Under Qt6's wayland backend the window mask no longer carries only the
    //! input area: Qt also restricts each frame's submitted damage to it, so an
    //! empty or degenerate region freezes the surface at its last content -
    //! initially fully transparent, which made the whole dock render 30fps into
    //! buffers that never showed. The mask computation legitimately passes
    //! degenerate rects while the layouter is still warming up (localGeometry
    //! width 0) and Qt.rect(0,0,-1,-1) as the explicit clear request; both must
    //! clear the mask instead of being forwarded.
    const QRect toApply = InputMaskFlush::windowMaskFor(m_appliedInputMask, m_inputMask);

    if (!toApply.isValid() || toApply.isEmpty()) {
        m_inputMaskSettleTimer.stop();
        m_appliedInputMask = QRect();
        m_view->setMask(QRegion());
        return;
    }

    //! Keep the window mask at the union across a SHRINK so the just-vacated
    //! edge pixels' transparent repaint is not clipped out of submitted damage
    //! (else the compositor freezes stale semi-transparent panel pixels there,
    //! the lighter band caught live on 2026-07-18 when maximizeWhenMaximized
    //! released on un-maximize). inputmaskflush.h carries the full rationale.
    m_appliedInputMask = toApply;
    m_view->setMask(m_appliedInputMask);

    //! While the band is still shrinking the applied mask stays wider than it;
    //! (re)arm the collapse so the window narrows back to the band once quiet.
    if (InputMaskFlush::needsSettleCollapse(m_appliedInputMask, m_inputMask)) {
        m_inputMaskSettleTimer.start();
    } else {
        m_inputMaskSettleTimer.stop();
    }
}

QRect Effects::appletsLayoutGeometry() const
{
    return m_appletsLayoutGeometry;
}

void Effects::setAppletsLayoutGeometry(const QRect &geom)
{
    if (m_appletsLayoutGeometry == geom) {
        return;
    }

    m_appletsLayoutGeometry = geom;
    m_view->setProperty("_applets_layout_geometry", QVariant(m_appletsLayoutGeometry));

    Q_EMIT appletsLayoutGeometryChanged();
}

QQuickItem *Effects::panelBackgroundSvg() const
{
    return m_panelBackgroundSvg;
}

void Effects::setPanelBackgroundSvg(QQuickItem *quickitem)
{
    if (m_panelBackgroundSvg == quickitem) {
        return;
    }

    m_panelBackgroundSvg = quickitem;
    Q_EMIT panelBackgroundSvgChanged();
}

void Effects::onPopUpMarginChanged()
{
    m_view->setProperty("_applets_popup_margin", QVariant(popUpMargin()));
}

QRegion Effects::customMask(const QRect &rect)
{
    QRegion result = rect;
    int dx = rect.right() - m_cornersMaskRegion.topLeft.boundingRect().width() + 1;
    int dy = rect.bottom() - m_cornersMaskRegion.topLeft.boundingRect().height() + 1;

    if (m_hasTopLeftCorner) {
        QRegion tl = m_cornersMaskRegion.topLeft;
        tl.translate(rect.x(), rect.y());
        result = result.subtracted(tl);
    }

    if (m_hasTopRightCorner) {
        QRegion tr = m_cornersMaskRegion.topRight;
        tr.translate(rect.x() + dx, rect.y());
        result = result.subtracted(tr);
    }

    if (m_hasBottomRightCorner) {
        QRegion br = m_cornersMaskRegion.bottomRight;
        br.translate(rect.x() + dx, rect.y() + dy);
        result = result.subtracted(br);
    }

    if (m_hasBottomLeftCorner) {
        QRegion bl = m_cornersMaskRegion.bottomLeft;
        bl.translate(rect.x(), rect.y() + dy);
        result = result.subtracted(bl);
    }

    return result;
}

void Effects::updateBackgroundCorners()
{
    if (m_backgroundRadius<0) {
        return;
    }

    m_corona->themeExtended()->cornersMask(m_backgroundRadius);

    m_cornersMaskRegion = m_corona->themeExtended()->cornersMask(m_backgroundRadius);
    Q_EMIT backgroundCornersMaskChanged();
}


void Effects::clearShadows()
{
    PanelShadows::self()->removeWindow(m_view);
}

void Effects::updateShadows()
{
    if (m_view->behaveAsPlasmaPanel() && drawShadows()) {
        PanelShadows::self()->addWindow(m_view, enabledBorders());
    } else {
        PanelShadows::self()->removeWindow(m_view);
    }
}

void Effects::updateEffects()
{
    //! Don't apply any effect before the wayland surface is created under wayland
    //! https://bugs.kde.org/show_bug.cgi?id=392890
    //! there is no separate plasma-shell surface to wait on anymore, so gate
    //! on the native window handle being realized
    if (KWindowSystem::isPlatformWayland() && !m_view->handle()) {
        return;
    }

    bool clearEffects{true};

    if (m_drawEffects) {
        if (!m_view->behaveAsPlasmaPanel()) {
            if (!m_rect.isNull() && !m_rect.isEmpty() && m_rect != VisibilityManager::ISHIDDENMASK) {
                QRegion backMask;

                if (m_backgroundRadiusEnabled) {
                    //! CustomBackground way
                    backMask = customMask(QRect(0,0,m_rect.width(), m_rect.height()));
                } else {
                    //! Plasma::Theme way
                    //! this is used when compositing is disabled and provides
                    //! the correct way for the mask to be painted in order for
                    //! rounded corners to be shown correctly
                    if (!m_panelBackgroundSvg) {
                        return;
                    }

                    if (m_rect == VisibilityManager::ISHIDDENMASK) {
                        clearEffects = true;
                    } else {
                        const QVariant maskProperty = m_panelBackgroundSvg->property("mask");
                        if (static_cast<QMetaType::Type>(maskProperty.type()) == QMetaType::QRegion) {
                            backMask = maskProperty.value<QRegion>();
                        }
                    }
                }

                //! adjust mask coordinates based on local coordinates
                int fX = m_rect.x(); int fY = m_rect.y();

                //! There are cases that mask is NULL even though it should not
                //! Example: SidebarOnDemand from v0.10 that BEHAVEASPLASMAPANEL in EditMode
                //! switching multiple times between inConfigureAppletsMode and LiveEditMode
                //! is such a case
                QRegion fixedMask;

                if (!backMask.isNull()) {
                    fixedMask = backMask;
                    fixedMask.translate(fX, fY);
                } else {
                    fixedMask = QRect(fX, fY, m_rect.width(), m_rect.height());
                }

                if (!fixedMask.isEmpty()) {
                    clearEffects = false;
                    KWindowEffects::enableBlurBehind(m_view, true, fixedMask);
                    KWindowEffects::enableBackgroundContrast(m_view,
                                                             m_theme.backgroundContrastEnabled(),
                                                             m_backEffectContrast,
                                                             m_backEffectIntesity,
                                                             m_backEffectSaturation,
                                                             fixedMask);
                }
            }
        } else {
            //!  BEHAVEASPLASMAPANEL case
            clearEffects = false;
            KWindowEffects::enableBlurBehind(m_view, true);
            KWindowEffects::enableBackgroundContrast(m_view,
                                                     m_theme.backgroundContrastEnabled(),
                                                     m_backEffectContrast,
                                                     m_backEffectIntesity,
                                                     m_backEffectSaturation);
        }
    }

    if (clearEffects) {
        KWindowEffects::enableBlurBehind(m_view, false);
        KWindowEffects::enableBackgroundContrast(m_view, false);
    }
}

//!BEGIN draw panel shadows outside the dock window
KSvg::FrameSvg::EnabledBorders Effects::enabledBorders() const
{
    return m_enabledBorders;
}

qreal Effects::currentMidValue(const qreal &max, const qreal &factor, const qreal &min) const
{
    if (max==min || factor==0) {
        return min;
    }

    qreal space = 0;
    qreal distance = 0;

    if (max<min) {
        space = min-max;
        distance = factor*space;
        return 1-distance;
    } else {
        space = max-min;
        distance = factor*space;
        return 1+distance;
    }
}

void Effects::updateBackgroundContrastValues()
{
    if (!m_theme.backgroundContrastEnabled()) {
        m_backEffectContrast = 1.0;
        m_backEffectIntesity = 1.0;
        m_backEffectSaturation = 1.0;
        return;
    }

    if (m_backgroundOpacity == -1 /*Default plasma opacity option*/) {
        m_backEffectContrast = m_theme.backgroundContrast();
        m_backEffectIntesity = m_theme.backgroundIntensity();
        m_backEffectSaturation = m_theme.backgroundSaturation();
    } else {
        m_backEffectContrast = currentMidValue(m_theme.backgroundContrast(), m_backgroundOpacity, 1);
        m_backEffectIntesity = currentMidValue(m_theme.backgroundIntensity(), m_backgroundOpacity, 1);
        m_backEffectSaturation = currentMidValue(m_theme.backgroundSaturation(), m_backgroundOpacity, 1);
    }
}

void Effects::updateEnabledBorders()
{
    if (!m_view->screen()) {
        return;
    }

    KSvg::FrameSvg::EnabledBorders borders = KSvg::FrameSvg::AllBorders;

    if (!m_view->screenEdgeMarginEnabled() && !m_backgroundAllCorners) {
        switch (m_view->location()) {
        case Plasma::Types::TopEdge:
            borders &= ~KSvg::FrameSvg::TopBorder;
            break;

        case Plasma::Types::LeftEdge:
            borders &= ~KSvg::FrameSvg::LeftBorder;
            break;

        case Plasma::Types::RightEdge:
            borders &= ~KSvg::FrameSvg::RightBorder;
            break;

        case Plasma::Types::BottomEdge:
            borders &= ~KSvg::FrameSvg::BottomBorder;
            break;

        default:
            break;
        }
    }

    if (!m_backgroundAllCorners) {
        if ((m_view->location() == Plasma::Types::LeftEdge || m_view->location() == Plasma::Types::RightEdge)) {
            if (m_view->maxLength() == 1 && m_view->alignment() == Latte::Types::Justify) {
                if (!m_forceTopBorder) {
                    borders &= ~KSvg::FrameSvg::TopBorder;
                }

                if (!m_forceBottomBorder) {
                    borders &= ~KSvg::FrameSvg::BottomBorder;
                }
            }

            if (m_view->alignment() == Latte::Types::Top && !m_forceTopBorder && m_view->offset() == 0) {
                borders &= ~KSvg::FrameSvg::TopBorder;
            }

            if (m_view->alignment() == Latte::Types::Bottom && !m_forceBottomBorder && m_view->offset() == 0) {
                borders &= ~KSvg::FrameSvg::BottomBorder;
            }
        }

        if (m_view->location() == Plasma::Types::TopEdge || m_view->location() == Plasma::Types::BottomEdge) {
            if (m_view->maxLength() == 1 && m_view->alignment() == Latte::Types::Justify) {
                borders &= ~KSvg::FrameSvg::LeftBorder;
                borders &= ~KSvg::FrameSvg::RightBorder;
            }

            if (m_view->alignment() == Latte::Types::Left && m_view->offset() == 0) {
                borders &= ~KSvg::FrameSvg::LeftBorder;
            }

            if (m_view->alignment() == Latte::Types::Right  && m_view->offset() == 0) {
                borders &= ~KSvg::FrameSvg::RightBorder;
            }
        }
    }

    m_hasTopLeftCorner =  (borders == KSvg::FrameSvg::AllBorders) || ((borders & KSvg::FrameSvg::TopBorder) && (borders & KSvg::FrameSvg::LeftBorder));
    m_hasTopRightCorner =  (borders == KSvg::FrameSvg::AllBorders) || ((borders & KSvg::FrameSvg::TopBorder) && (borders & KSvg::FrameSvg::RightBorder));
    m_hasBottomLeftCorner =  (borders == KSvg::FrameSvg::AllBorders) || ((borders & KSvg::FrameSvg::BottomBorder) && (borders & KSvg::FrameSvg::LeftBorder));
    m_hasBottomRightCorner =  (borders == KSvg::FrameSvg::AllBorders) || ((borders & KSvg::FrameSvg::BottomBorder) && (borders & KSvg::FrameSvg::RightBorder));

    if (m_enabledBorders != borders) {
        m_enabledBorders = borders;
        Q_EMIT enabledBordersChanged();
    }

    if (!m_view->behaveAsPlasmaPanel() || !m_drawShadows) {
        PanelShadows::self()->removeWindow(m_view);
    } else {
        PanelShadows::self()->setEnabledBorders(m_view, borders);
    }
}
//!END draw panel shadows outside the dock window

}
}
