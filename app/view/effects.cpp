/*
    SPDX-FileCopyrightText: 2018 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "effects.h"

// local
#include <config-latte.h>
#include <coretypes.h>
#include "effectregion.h"
#include "floatingtransition.h"
#include "inputmaskflush.h"
#include "panelborderdecision.h"
#include "panelshadows_p.h"
#include "positioner.h"
#include "view.h"
#include "visibilitymanager.h"
#include "../lattecorona.h"
#include "../wm/abstractwindowinterface.h"

// local tools
#include "../tools/commontools.h"

// Qt
#include <QRegion>

#include <optional>

// KDE
#include <KWindowEffects>
#include <KWindowSystem>


namespace Latte {
namespace ViewPart {

namespace {

std::optional<FloatingPanelGeometry::Edge> presentationEdge(
    Plasma::Types::Location location)
{
    switch (location) {
    case Plasma::Types::TopEdge:
        return FloatingPanelGeometry::Edge::Top;
    case Plasma::Types::RightEdge:
        return FloatingPanelGeometry::Edge::Right;
    case Plasma::Types::BottomEdge:
        return FloatingPanelGeometry::Edge::Bottom;
    case Plasma::Types::LeftEdge:
        return FloatingPanelGeometry::Edge::Left;
    case Plasma::Types::Floating:
    case Plasma::Types::Desktop:
    case Plasma::Types::FullScreen:
        qCritical() << "Effects received a non-edge panel location:" << location;
        return std::nullopt;
    }

    Q_UNREACHABLE();
}

std::optional<PanelBorderDecision::Alignment> presentationAlignment(
    int alignment)
{
    switch (alignment) {
    case Latte::Types::Left:
    case Latte::Types::Top:
        return PanelBorderDecision::Alignment::Start;
    case Latte::Types::Right:
    case Latte::Types::Bottom:
        return PanelBorderDecision::Alignment::End;
    case Latte::Types::Justify:
        return PanelBorderDecision::Alignment::Justify;
    case Latte::Types::Center:
    case Latte::Types::NoneAlignment:
        return PanelBorderDecision::Alignment::Center;
    }

    qCritical() << "Effects received an unknown panel alignment:" << alignment;
    return std::nullopt;
}

} // namespace

Effects::Effects(Latte::View *parent)
    : QObject(parent),
      m_floatingMaskRenderBridge(
          std::make_shared<
              FloatingMaskHandshake::RenderBridge>()),
      m_view(parent)
{
    m_corona = qobject_cast<Latte::Corona *>(m_view->corona());

    init();
}

Effects::~Effects()
{
    for (const auto &connection : m_renderConnections) {
        disconnect(connection);
    }

    // QQuickWindow render signals are direct across the render thread.
    // Closing waits for an in-flight callback to finish posting; callbacks
    // already dispatched but not entered retain the shared bridge, observe
    // the closed state, and never dereference this destroyed Effects object.
    m_floatingMaskRenderBridge->close();
}

void Effects::init()
{
    connect(this, &Effects::backgroundOpacityChanged, this, &Effects::updateEffects);
    connect(this, &Effects::backgroundOpacityChanged, this, &Effects::updateBackgroundContrastValues);
    connect(this, &Effects::backgroundCornersMaskChanged, this, &Effects::updateEffects);
    connect(this, &Effects::backgroundRadiusEnabledChanged, this, &Effects::updateEffects);
    connect(this, &Effects::drawEffectsChanged, this, &Effects::updateEffects);
    connect(this, &Effects::enabledBordersChanged, this, &Effects::updateEffects);
    connect(this, &Effects::panelBackgroundSvgChanged,
            this, &Effects::updateEffects);
    connect(this, &Effects::rectChanged, this, &Effects::updateEffects);
    connect(this, &Effects::rectChanged,
            this, &Effects::updateEnabledBorders);


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
    connect(m_view, &Latte::View::floatingGapConfiguredChanged,
            this, &Effects::updateEnabledBorders);
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

    // QWindow::setMask also clips submitted Wayland damage. Keep the old
    // floating bridge for one rendered clearing frame, then collapse to the
    // exact logical bridge. View::event rejects the old-only input meanwhile.
    const auto renderBridge = m_floatingMaskRenderBridge;
    m_renderConnections[0] = connect(
        m_view,
        &QQuickWindow::beforeSynchronizing,
        this,
        [renderBridge]() {
            (void)renderBridge->synchronizeForFrame();
        },
        Qt::DirectConnection);
    m_renderConnections[1] = connect(
        m_view,
        &QQuickWindow::afterFrameEnd,
        this,
        [this, renderBridge]() {
            (void)renderBridge->afterFrame(
                [this](quint64 submittedGeneration) {
                    QMetaObject::invokeMethod(
                        this,
                        [this, submittedGeneration]() {
                            collapseFloatingDamageMask(
                                submittedGeneration);
                        },
                        Qt::QueuedConnection);
                });
        },
        Qt::DirectConnection);
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
    if (floatingPresentationOwnsPaint()) {
        qCritical() << "Effects refused a legacy rect write while the stable "
                       "panel controller owns paint";
        return;
    }

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
    if (floatingPresentationOwnsPaint()) {
        qCritical() << "Effects refused a legacy mask write while the stable "
                       "panel controller owns paint";
        return;
    }

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
    if (floatingPresentationOwnsInput()) {
        qCritical() << "Effects refused a legacy input-mask write while the "
                       "visible stable panel controller owns input";
        return;
    }

    publishFloatingMaskGeneration(
        m_floatingMaskHandshake.transferToLegacy());

    if (m_inputMask == area) {
        return;
    }

    m_inputMask = area;
    applyInputMaskToWindow();

    Q_EMIT inputMaskChanged();
}

bool Effects::floatingPresentationOwnsPaint() const
{
    return m_view
        && m_view->behaveAsPlasmaPanel()
        && m_view->floatingTransition()
        && m_view->floatingTransition()->hasGeometry();
}

bool Effects::floatingPresentationOwnsInput() const
{
    return floatingPresentationOwnsPaint()
        && (!m_view->visibility()
            || (!m_view->visibility()->isHidden()
                && !m_view->visibility()->isSidebar()));
}

void Effects::applyFloatingPresentationProgress()
{
    if (!m_view) {
        return;
    }

    if (m_view->behaveAsPlasmaPanel()) {
        applyFloatingPanelPresentation();
        return;
    }

    //! Docks retain QML ownership of their paint and input rectangles. Only
    //! endpoint borders derive from the shared fractional presentation; using
    //! the Panel handoff here would clear the Dock-owned effects rectangle.
    updateEnabledBorders();
}

void Effects::applyFloatingPanelPresentation()
{
    if (!m_view) {
        return;
    }

    if (!m_view->behaveAsPlasmaPanel()) {
        publishFloatingMaskGeneration(
            m_floatingMaskHandshake.transferToLegacy());
        const bool rectWasChanged = !m_rect.isNull();
        const bool maskWasChanged = !m_mask.isNull();
        m_rect = {};
        m_mask = {};
        m_view->setProperty("_floating_visible_geometry",
                            QVariant{});
        m_view->setProperty("_floating_anchor_revision",
                            QVariant::fromValue(
                                ++m_floatingAnchorRevision));
        if (!m_shadowPaddingOffsets.isNull()) {
            m_shadowPaddingOffsets = {};
            PanelShadows::self()->setExtraPadding(m_view, {});
        }
        updateEnabledBorders();
        updateEffects();
        updateShadows();
        if (rectWasChanged) {
            Q_EMIT rectChanged();
        }
        if (maskWasChanged) {
            Q_EMIT maskChanged();
        }
        return;
    }

    FloatingTransition *transition = m_view->floatingTransition();
    if (!transition || !transition->hasGeometry()) {
        // Positioner publishes the first stable solution after the panel type
        // property becomes visible. currentGeometryChanged retries this
        // synchronization as soon as that expected startup absence ends.
        return;
    }

    const QRect paintBounds = transition->currentPaintMaskGeometry();
    const QRect inputBridge = transition->currentInputBridgeGeometry();
    if (!paintBounds.isValid() || paintBounds.isEmpty()
        || !inputBridge.isValid() || inputBridge.isEmpty()) {
        qCritical() << "Effects refused degenerate floating panel presentation"
                    << paintBounds << inputBridge;
        return;
    }

    const bool rectWasChanged = m_rect != paintBounds;
    const bool maskWasChanged = m_mask != paintBounds;
    m_rect = paintBounds;
    m_mask = paintBounds;
    m_shadowPaddingOffsets =
        transition->currentShadowPaddingOffsets();

    // Hidden and sidebar panels keep VisibilityManager's reveal-strip mask.
    // A visible stable panel bypasses InputMaskFlush: its exact partial span
    // may never retain a stale old union during a shrink.
    const bool legacyRevealMaskOwnsInput =
        m_view->visibility()
        && (m_view->visibility()->isHidden()
            || m_view->visibility()->isSidebar());
    bool inputWasChanged{false};
    if (legacyRevealMaskOwnsInput) {
        publishFloatingMaskGeneration(
            m_floatingMaskHandshake.transferToLegacy());
    } else {
        inputWasChanged = m_inputMask != inputBridge;
        m_inputMask = inputBridge;
        m_inputMaskSettleTimer.stop();
        const QRect clearingMask =
            m_appliedInputMask.isValid()
            && !inputBridge.contains(m_appliedInputMask)
            ? m_appliedInputMask.united(inputBridge)
            : inputBridge;
        if (m_appliedInputMask != clearingMask) {
            m_appliedInputMask = clearingMask;
            m_view->setMask(QRegion(m_appliedInputMask));
        }
        if (m_appliedInputMask != m_inputMask) {
            publishFloatingMaskGeneration(
                m_floatingMaskHandshake.arm(m_inputMask));
            m_view->update();
        } else {
            publishFloatingMaskGeneration(
                m_floatingMaskHandshake.adoptExact(
                    m_inputMask));
        }
    }

    m_view->setProperty("_floating_visible_geometry",
                        QVariant(transition->currentVisibleGeometry()));
    m_view->setProperty("_floating_anchor_revision",
                        QVariant::fromValue(++m_floatingAnchorRevision));
    PanelShadows::self()->setExtraPadding(m_view,
                                          m_shadowPaddingOffsets);

    updateEnabledBorders();
    updateEffects();

    // Backing values and native state are installed before observers can pull
    // any property, so D-Bus never sees a mixed presentation frame.
    if (rectWasChanged) {
        Q_EMIT rectChanged();
    }
    if (maskWasChanged) {
        Q_EMIT maskChanged();
    }
    if (inputWasChanged) {
        Q_EMIT inputMaskChanged();
    }
}

void Effects::collapseFloatingDamageMask(quint64 submittedGeneration)
{
    const bool presentationStillOwnsInput =
        m_view
        && m_view->behaveAsPlasmaPanel()
        && (!m_view->visibility()
            || (!m_view->visibility()->isHidden()
                && !m_view->visibility()->isSidebar()));
    if (!presentationStillOwnsInput
        || !m_floatingMaskHandshake.canCollapse(
            submittedGeneration, m_inputMask)) {
        return;
    }

    m_floatingMaskHandshake.complete();
    if (m_appliedInputMask == m_inputMask) {
        return;
    }
    m_appliedInputMask = m_inputMask;
    m_view->setMask(QRegion(m_appliedInputMask));
}

void Effects::publishFloatingMaskGeneration(quint64 generation)
{
    m_floatingMaskRenderBridge->publish(generation);
}

QRect Effects::appliedInputMask() const
{
    return m_appliedInputMask;
}

bool Effects::floatingDamageMaskPending() const
{
    return m_floatingMaskHandshake.pending();
}

quint64 Effects::floatingDamageMaskGeneration() const
{
    return m_floatingMaskHandshake.generation();
}

quint64 Effects::floatingAnchorRevision() const
{
    return m_floatingAnchorRevision;
}

Qt::Orientation Effects::lengthAxis() const
{
    const Plasma::Types::Location location = m_view->location();
    return (location == Plasma::Types::LeftEdge || location == Plasma::Types::RightEdge)
            ? Qt::Vertical : Qt::Horizontal;
}

void Effects::applyInputMaskToWindow()
{
    if (!m_view) {
        return;
    }

    //! InputMaskFlush owns the region decision: on a Qt6-wayland masked dock the
    //! window mask both gates input AND clips each frame's submitted damage, so a
    //! LENGTH-axis shrink (maximize-length release, parabolic zoom-out) is held at
    //! the union until it settles, keeping the vacated ends' clearing damage
    //! inside the mask (the frosted-band fix caught live 2026-07-18); a grow or a
    //! thickness-axis shrink (autohide/dodge HIDE collapsing to the reveal strip)
    //! is applied straight through, so a hidden dock never over-captures input
    //! across its vacated body. See inputmaskflush.h.
    const QRect toApply = InputMaskFlush::windowMaskFor(m_appliedInputMask, m_inputMask, lengthAxis());

    //! The mask computation legitimately passes degenerate rects while the
    //! layouter is still warming up (localGeometry width 0) and Qt.rect(0,0,-1,-1)
    //! as the explicit clear request. Under the same mask/damage coupling an empty
    //! or degenerate region freezes the surface at its last content (initially
    //! transparent, which once made the whole dock render 30fps into buffers that
    //! never showed), so those clear the mask instead of being forwarded.
    if (!toApply.isValid() || toApply.isEmpty()) {
        m_inputMaskSettleTimer.stop();
        m_appliedInputMask = QRect();
        m_view->setMask(QRegion());
        return;
    }

    m_appliedInputMask = toApply;
    m_view->setMask(m_appliedInputMask);

    //! While the length band is still shrinking the applied mask stays wider than
    //! it; (re)arm the collapse so the window narrows back to the band once quiet.
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
        PanelShadows::self()->addWindow(m_view,
                                        enabledBorders(),
                                        m_shadowPaddingOffsets);
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

    if (m_drawEffects && !m_rect.isNull() && !m_rect.isEmpty()
        && m_rect != VisibilityManager::ISHIDDENMASK) {
        const QRectF visibleShape =
            m_view->isFloatingPanel()
                && m_view->floatingTransition()
                && m_view->floatingTransition()->hasGeometry()
            ? m_view->floatingTransition()->currentVisibleGeometry()
            : QRectF(m_rect);
        const QRect localVisibleBounds{
            QPoint{},
            visibleShape.size().toSize()};

        QRegion localShape;
        if (m_backgroundRadiusEnabled) {
            localShape = customMask(localVisibleBounds);
        } else if (m_panelBackgroundSvg) {
            const QVariant maskProperty =
                m_panelBackgroundSvg->property("mask");
            if (static_cast<QMetaType::Type>(maskProperty.type())
                == QMetaType::QRegion) {
                localShape = maskProperty.value<QRegion>();
            }
        }

        // The rectangle fallback is deliberate for themes that publish no
        // mask. When a rounded mask exists, fractional translation is
        // outward-rasterized per source rectangle so the ceil-side row stays
        // covered without filling transparent corners.
        const QRegion effectRegion =
            EffectRegion::rasterizedTranslatedShape(
                visibleShape,
                localShape);
        if (!effectRegion.isEmpty()) {
            clearEffects = false;
            KWindowEffects::enableBlurBehind(
                m_view, true, effectRegion);
            KWindowEffects::enableBackgroundContrast(
                m_view,
                m_theme.backgroundContrastEnabled(),
                m_backEffectContrast,
                m_backEffectIntesity,
                m_backEffectSaturation,
                effectRegion);
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
    const Positioner *const positioner = m_view->positioner();
    if (!positioner) {
        qCritical() << "Effects cannot update panel borders without a Positioner for"
                    << m_view->validTitle();
        return;
    }

    if (positioner->relocationGeneration()
            != positioner->appliedRelocationGeneration()) {
        //! Placement is atomic. Keep the previous applied borders while the
        //! requested output, edge and surface are intentionally incoherent;
        //! placementTransactionCommitted recomputes from the complete result.
        return;
    }

    QScreen *const assignedScreen = positioner->assignedScreen();
    if (!assignedScreen) {
        qCritical() << "Effects cannot update panel borders without an assigned output for"
                    << m_view->validTitle();
        return;
    }

    const QRect assignedOutputGeometry = assignedScreen->geometry();
    const QRect surfaceGeometry = positioner->surfaceGeometry();
    if (!assignedOutputGeometry.isValid()
            || (m_rect.isValid() && !surfaceGeometry.isValid())) {
        qCritical() << "Effects refused invalid border geometry for"
                    << m_view->validTitle()
                    << "presentation=" << m_rect
                    << "surface=" << surfaceGeometry
                    << "output=" << assignedOutputGeometry;
        return;
    }

    const auto edge = presentationEdge(m_view->location());
    const auto alignment =
        presentationAlignment(m_view->alignment());
    if (!edge || !alignment) {
        return;
    }

    const FloatingTransition *transition =
        m_view->floatingTransition();
    const bool configuredFloatingPanel =
        m_view->isFloatingPanel()
        && transition
        && transition->hasGeometry();
    const bool configuredFloatingDock =
        !m_view->behaveAsPlasmaPanel()
        && m_view->floatingGapConfigured()
        && transition;
    const bool configuredFloatingPresentation =
        configuredFloatingPanel || configuredFloatingDock;
    const bool floatingBoundaryVisible =
        configuredFloatingPanel
        ? transition->screenEdgeBorderVisible()
        : configuredFloatingDock
            && transition->floatingness() != 0.0;
    const KSvg::FrameSvg::EnabledBorders borders =
        PanelBorderDecision::enabledBorders({
            .edge = *edge,
            .alignment = *alignment,
            .configuredFloatingPresentation =
                configuredFloatingPresentation,
            .screenEdgeBorderVisible =
                configuredFloatingPresentation
                && floatingBoundaryVisible,
            .floatingCornersVisible =
                configuredFloatingPresentation
                && floatingBoundaryVisible,
            .primaryAxisFillsOutput =
                PanelBorderDecision::doesPresentationFillOutputPrimaryAxis(
                    m_rect,
                    surfaceGeometry,
                    assignedOutputGeometry,
                    *edge),
            .screenEdgeMarginEnabled =
                m_view->screenEdgeMarginEnabled(),
            .backgroundAllCorners = m_backgroundAllCorners,
            .forcePrimaryStartBorder = m_forceTopBorder,
            .forcePrimaryEndBorder = m_forceBottomBorder,
            .maxLength = m_view->maxLength(),
            .offset = m_view->offset(),
        });

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
