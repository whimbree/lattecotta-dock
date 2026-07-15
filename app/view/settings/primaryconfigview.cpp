/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "primaryconfigview.h"

// local
#include <config-latte.h>
#include "canvasconfigview.h"
#include "indicatoruimanager.h"
#include "secondaryconfigview.h"
#include "../effects.h"
#include "../panelshadows_p.h"
#include "../view.h"
#include "../../lattecorona.h"
#include "../../layouts/manager.h"
#include "../../layout/genericlayout.h"
#include "../../settings/universalsettings.h"
#include "../../wm/abstractwindowinterface.h"
#include "../../wm/waylandlayershell.h"

// local tools
#include "../../tools/commontools.h"

// Qt
#include <QQuickItem>
#include <QQmlContext>
#include <QQmlEngine>
#include <QScreen>

// KDE
#include <KLocalizedContext>
#include <KWindowEffects>
#include <KWindowSystem>

// Plasma
#include <KPackage/Package>

#define CANVASWINDOWINTERVAL 50
#define PRIMARYWINDOWINTERVAL 250
#define SECONDARYWINDOWINTERVAL 200
#define SLIDEOUTINTERVAL 400

namespace Latte {
namespace ViewPart {

PrimaryConfigView::PrimaryConfigView(Latte::View *view, const bool &showOnCreation)
    : SubConfigView(view, QString("#primaryconfigview#")),
      m_indicatorUiManager(new Config::IndicatorUiManager(this))
{
    connect(this, &QQuickWindow::xChanged, this, &PrimaryConfigView::xChanged);
    connect(this, &QQuickWindow::yChanged, this, &PrimaryConfigView::yChanged);

    connect(this, &QQuickView::widthChanged, this, &PrimaryConfigView::updateEffects);
    connect(this, &QQuickView::heightChanged, this, &PrimaryConfigView::updateEffects);

    connect(this, &PrimaryConfigView::availableScreenGeometryChanged, this, &PrimaryConfigView::syncGeometry);

    connect(this, &QQuickView::statusChanged, [&](QQuickView::Status status) {
        if (status == QQuickView::Ready) {
            updateEffects();
            //! the QML root now has a real size, so a syncGeometry that bailed
            //! earlier (zero size, no surface placement) can complete
            syncGeometry();
        }
    });

    if (m_corona) {
        connections << connect(m_corona, &Latte::Corona::raiseViewsTemporaryChanged, this, &PrimaryConfigView::raiseDocksTemporaryChanged);
        connections << connect(m_corona, &Latte::Corona::availableScreenRectChangedFrom, this, &PrimaryConfigView::updateAvailableScreenGeometry);

        connections << connect(m_corona->layoutsManager(), &Latte::Layouts::Manager::currentLayoutIsSwitching, this, [this]() {
            if (isVisible()) {
                hideConfigWindow();
            }
        });

        connect(m_corona->universalSettings(), &Latte::UniversalSettings::inAdvancedModeForEditSettingsChanged,
                this, &PrimaryConfigView::updateShowInlineProperties);
        connect(m_corona->universalSettings(), &Latte::UniversalSettings::inAdvancedModeForEditSettingsChanged,
                this, &PrimaryConfigView::syncGeometry);
    }

    m_availableScreemGeometryTimer.setSingleShot(true);
    m_availableScreemGeometryTimer.setInterval(250);

    connections << connect(&m_availableScreemGeometryTimer, &QTimer::timeout, this, [this]() {
        instantUpdateAvailableScreenGeometry();
    });

    if (showOnCreation) {
        setParentView(view);
    } else {
        //! warmup: wire the parent view (context properties, layer surface,
        //! connections) WITHOUT showConfigWindow() - no configuring session
        //! starts on the containment (userConfiguring stays false, so the
        //! dock never flashes edit visuals) and no window maps. init()
        //! below still loads the chrome QML, which is the expensive part
        //! the warmup exists to pay in the background.
        initParentView(view);
    }

    init();
}

PrimaryConfigView::~PrimaryConfigView()
{
    if (m_canvasConfigView) {
        delete m_canvasConfigView;
    }

    if (m_secConfigView) {
        delete m_secConfigView;
    }
}

void PrimaryConfigView::init()
{
    SubConfigView::init();

    QByteArray tempFilePath = "lattedockconfigurationui";

    auto source = QUrl::fromLocalFile(m_latteView->containment()->corona()->kPackage().filePath(tempFilePath));
    setSource(source);
    syncGeometry();
}

Config::IndicatorUiManager *PrimaryConfigView::indicatorUiManager()
{
    return m_indicatorUiManager;
}

void PrimaryConfigView::setOnActivities(QStringList activities)
{
    m_corona->wm()->setWindowOnActivities(trackedWindowId(), activities);

    if (m_secConfigView) {
        m_corona->wm()->setWindowOnActivities(m_secConfigView->trackedWindowId(), activities);
    }

    if (m_canvasConfigView) {
        m_corona->wm()->setWindowOnActivities(m_canvasConfigView->trackedWindowId(), activities);
    }
}

void PrimaryConfigView::requestActivate()
{
    if (m_latteView && m_latteView->visibility()) {
        if (KWindowSystem::isPlatformX11()) {
            m_latteView->visibility()->setViewOnFrontLayer();
        } else if (KWindowSystem::isPlatformWayland()) {
            m_corona->wm()->requestActivate(m_latteView->positioner()->trackedWindowId());
        }
    }

    if (m_secConfigView) {
        m_secConfigView->requestActivate();
    }

    SubConfigView::requestActivate();
}

void PrimaryConfigView::showConfigWindow()
{
    if (isVisible()) {
        return;
    }

    if (m_latteView && m_latteView->containment()) {
        m_latteView->containment()->setUserConfiguring(true);
    }

    showAfter(PRIMARYWINDOWINTERVAL);
    showCanvasWindow();
    showSecondaryWindow();
}

void PrimaryConfigView::hideConfigWindow()
{
    //! Session-end semantics live HERE, not in hideEvent: the window also
    //! hides transiently (deferred show on view retargeting, layer-surface
    //! remaps), and a transient hide must not end the user's configuring
    //! session. Observed live: cycling the settings between docks fired a
    //! transient hide ~300ms after the new containment's
    //! setUserConfiguring(true), so the new session was silently killed and
    //! the chrome sat open with editMode false: no blueprint, no
    //! configure-applets frames, no dragging.
    endConfiguringSession();

    //! a pending deferred show would remap the chrome after the session
    //! ended (see SubConfigView::cancelDeferredShow); the canvas and
    //! secondary windows cancel their own inside their hideConfigWindow()
    cancelDeferredShow();

    if (KWindowSystem::isPlatformWayland()) {
        //!NOTE: Avoid crash in wayland environment with qt5.9
        close();
    } else {
        hide();
    }

    hideCanvasWindow();
    hideSecondaryWindow();
}

void PrimaryConfigView::endConfiguringSession()
{
    if (!m_latteView) {
        return;
    }

    if (m_latteView->containment()) {
        //! ends the containment's editing state; the containment QML then
        //! also clears the rearrange (configure-applets) sub-mode via its
        //! onEditModeChanged reset, so a closed chrome can never leave the
        //! dock stuck in edit or rearrange visuals
        m_latteView->containment()->setUserConfiguring(false);
    }

    const auto mode = m_latteView->visibility()->mode();

    if ((mode == Types::AlwaysVisible || mode == Types::WindowsGoBelow)
            && !(m_originalMode == Types::AlwaysVisible || m_originalMode == Types::WindowsGoBelow)) {
        //! mode changed to AlwaysVisible OR WindowsGoBelow FROM Dodge mode
        if (m_originalByPassWM) {
            //! if original by pass is active
            m_latteView->layout()->recreateView(m_latteView->containment());
        }
    } else if (m_latteView->byPassWM() != m_originalByPassWM) {
        m_latteView->layout()->recreateView(m_latteView->containment());
    }
}

void PrimaryConfigView::showCanvasWindow()
{
    if (!m_canvasConfigView) {
        m_canvasConfigView = new CanvasConfigView(m_latteView, this);
    }

    if (m_canvasConfigView && !m_canvasConfigView->isVisible()){
        m_canvasConfigView->showAfter(CANVASWINDOWINTERVAL);
    }
}

void PrimaryConfigView::hideCanvasWindow()
{
    if (m_canvasConfigView) {
        m_canvasConfigView->hideConfigWindow();
    }
}

void PrimaryConfigView::showSecondaryWindow()
{
    bool isValidShowing{m_latteView->formFactor() == Plasma::Types::Horizontal && inAdvancedMode()};

    if (!isValidShowing) {
        return;
    }

    if (!m_secConfigView) {
        m_secConfigView = new SecondaryConfigView(m_latteView, this);
    }

    if (m_secConfigView && !m_secConfigView->isVisible()){
        m_secConfigView->showAfter(SECONDARYWINDOWINTERVAL);
    }
}

void PrimaryConfigView::hideSecondaryWindow()
{
    if (m_secConfigView) {
        m_secConfigView->hideConfigWindow();
    }
}

void PrimaryConfigView::setParentView(Latte::View *view, const bool &immediate)
{
    if (m_latteView == view) {
        return;
    }

    if (m_latteView && !immediate) {
        hideConfigWindow();

        //!slide-out delay; the target view can be destroyed while the slide-out
        //!plays (dock removal, layout switch), so hold it through a QPointer and
        //!drop the retarget loudly instead of dereferencing a dangling pointer
        QPointer<Latte::View> nextview(view);

        QTimer::singleShot(SLIDEOUTINTERVAL, this, [this, nextview]() {
            if (!nextview) {
                qWarning() << "PrimaryConfigView: the view to be configured was destroyed during the slide-out delay; the settings window stays hidden";
                return;
            }

            initParentView(nextview);
            showConfigWindow();
        });
    } else {
        initParentView(view);
        showConfigWindow();
    }
}

void PrimaryConfigView::initParentView(Latte::View *view)
{
    setIsReady(false);

    SubConfigView::initParentView(view);

    viewconnections << connect(m_latteView, &Latte::View::layoutChanged, this, [this]() {
        if (m_latteView->layout()) {
            updateAvailableScreenGeometry();
        }
    });

    viewconnections << connect(m_latteView, &Latte::View::editThicknessChanged, this, [this]() {
        updateAvailableScreenGeometry();
    });

    viewconnections << connect(m_latteView, &Latte::View::maxNormalThicknessChanged, this, [this]() {
        updateAvailableScreenGeometry();
    });

    viewconnections << connect(m_latteView, &Latte::View::locationChanged, this, [this]() {
        updateAvailableScreenGeometry();
    });

    viewconnections << connect(m_latteView->positioner(), &Latte::ViewPart::Positioner::currentScreenChanged, this, [this]() {
        updateAvailableScreenGeometry();
    });

    viewconnections << connect(m_corona->universalSettings(), &Latte::UniversalSettings::inAdvancedModeForEditSettingsChanged, m_latteView, &Latte::View::inSettingsAdvancedModeChanged);
    viewconnections << connect(m_latteView->containment(), &Plasma::Containment::immutabilityChanged, this, &PrimaryConfigView::immutabilityChanged);   

    m_originalByPassWM = m_latteView->byPassWM();
    m_originalMode = m_latteView->visibility()->mode();

    updateEnabledBorders();
    updateAvailableScreenGeometry();
    syncGeometry();

    setIsReady(true);

    if (m_canvasConfigView) {
        m_canvasConfigView->setParentView(view);
    }

    if (m_secConfigView) {
        m_secConfigView->setParentView(view);
    }

    //! inform view about the current settings level
    Q_EMIT m_latteView->inSettingsAdvancedModeChanged();
}

void PrimaryConfigView::instantUpdateAvailableScreenGeometry()
{
    if (!m_latteView || !m_latteView->positioner()) {
        return;
    }

    int currentScrId = m_latteView->positioner()->currentScreenId();

    QList<Latte::Types::Visibility> ignoreModes{Latte::Types::SidebarOnDemand,Latte::Types::SidebarAutoHide};

    if (m_latteView->visibility() && m_latteView->visibility()->isSidebar()) {
        ignoreModes.removeAll(Latte::Types::SidebarOnDemand);
        ignoreModes.removeAll(Latte::Types::SidebarAutoHide);
    }

    QString activityid = m_latteView->layout()->lastUsedActivity();

    m_availableScreenGeometry = m_corona->availableScreenRectWithCriteria(currentScrId, activityid, ignoreModes, {}, false, true);
    Q_EMIT availableScreenGeometryChanged();
}

void PrimaryConfigView::updateAvailableScreenGeometry(View *origin)
{
    //! Deviation from upstream (d30143f7 excluded origin == m_latteView):
    //! after a restart the corona integrates this view's own reserved screen
    //! area LATE, so the warmed chrome computed availableScreenGeometry from
    //! the strut-less full screen and a first open right after a restart
    //! mapped the settings window taller than the available area - observed
    //! live overflowing the screen top by the dock's reserved thickness
    //! (99px), stuck that way because the correcting
    //! availableScreenRectChangedFrom(self) was dropped here. Self-origin
    //! updates are exactly the ones that heal this; churn is bounded by the
    //! debounce timer below and by syncGeometry's no-change early-return.
    Q_UNUSED(origin);

    if (!m_latteView || !m_latteView->layout()) {
        return;
    }

    if (!m_availableScreemGeometryTimer.isActive()) {
        m_availableScreemGeometryTimer.start();
    }
}

QRect PrimaryConfigView::availableScreenGeometry() const
{
    return m_availableScreenGeometry;
}

QRect PrimaryConfigView::geometryWhenVisible() const
{
    return m_geometryWhenVisible;
}

void PrimaryConfigView::syncGeometry()
{
    if (!m_latteView || !m_latteView->layout() || !m_latteView->containment() || !rootObject()) {
        return;
    }

    //! The config QML root is a Loader (active: plasmoid && ... && latteView);
    //! the sized dialog is its loaded item. Read the item's size, because the
    //! Loader itself does not take on the explicitly-sized item's width/height.
    QQuickItem *content = rootObject();
    if (QQuickItem *loaded = rootObject()->property("item").value<QQuickItem *>()) {
        content = loaded;
    }
    const QSize size(content->width(), content->height());

    //! Before the QML content has laid out, rootObject reports 0x0. Committing
    //! a zero-sized wlr-layer surface is a fatal protocol error; do not place
    //! the window until it has a real size. rootObject's width/height changes
    //! (connected in init()) and the post-show re-sync re-run this once sized.
    if (size.isEmpty()) {
        return;
    }
    const auto location = m_latteView->containment()->location();
    const auto scrGeometry = m_latteView->screenGeometry();
    const auto availGeometry = m_availableScreenGeometry;
    const auto canvasGeometry = m_latteView->positioner()->canvasGeometry();

    int canvasThickness = m_latteView->formFactor() == Plasma::Types::Vertical ? canvasGeometry.width() : canvasGeometry.height();

    QPoint position{0, 0};

    int xPos{0};
    int yPos{0};

    switch (m_latteView->formFactor()) {
    case Plasma::Types::Horizontal: {
        //! always at the screen's end, clear of the dock's center where the
        //! rearrange toggle and the widgets being configured live (Qt5 only
        //! did this in advanced mode; centered it covers what is edited)
        if (qApp->isLeftToRight()) {
            xPos = availGeometry.x() + availGeometry.width() - size.width();
        } else {
            xPos = availGeometry.x();
        }

        if (location == Plasma::Types::TopEdge) {
            yPos = scrGeometry.y() + canvasThickness;
        } else if (location == Plasma::Types::BottomEdge) {
            yPos = scrGeometry.y() + scrGeometry.height() - canvasThickness - size.height();
        }
    }
        break;

    case Plasma::Types::Vertical: {
        if (location == Plasma::Types::LeftEdge) {
            xPos = scrGeometry.x() + canvasThickness;
            yPos =  availGeometry.y() + (availGeometry.height() - size.height())/2;
        } else if (location == Plasma::Types::RightEdge) {
            xPos = scrGeometry.x() + scrGeometry.width() - canvasThickness - size.width();
            yPos =  availGeometry.y() + (availGeometry.height() - size.height())/2;
        }
    }
        break;

    default:
        qWarning() << "no sync geometry, wrong formFactor";
        break;
    }

    position = {xPos, yPos};

    updateEnabledBorders();

    auto geometry = QRect(position.x(), position.y(), size.width(), size.height());

    QRect winGeometry(x(), y(), width(), height());

    if (m_geometryWhenVisible == geometry && winGeometry == geometry) {
        return;
    }

    m_geometryWhenVisible = geometry;

    //! size the window before touching the layer-shell anchors: an
    //! unanchored surface that commits at zero size is a fatal protocol
    //! error (see LayerShell::setUnanchored)
    setMaximumSize(size);
    setMinimumSize(size);
    resize(size);

    if (KWindowSystem::isPlatformWayland()) {
        //! layer-shell ignores setPosition(). Unanchored the compositor
        //! centres the window mid-screen, nowhere near the dock; pin it to
        //! the position computed above (right end of the available area,
        //! sitting on the edit canvas) the same way the widget explorer does.
        Latte::WindowSystem::LayerShell::applyFixedGeometry(this, m_latteView->screen(), geometry, scrGeometry);
    } else {
        setPosition(position);
    }

    Q_EMIT m_latteView->configWindowGeometryChanged();
}

void PrimaryConfigView::showEvent(QShowEvent *ev)
{
    updateAvailableScreenGeometry();

    SubConfigView::showEvent(ev);

    if (!m_latteView) {
        return;
    }

    setFlags(wFlags());
    m_corona->wm()->setViewExtraFlags(this, false, Latte::Types::NormalWindow);

    syncGeometry();

    m_screenSyncTimer.start();
    QTimer::singleShot(400, this, &PrimaryConfigView::syncGeometry);

    updateShowInlineProperties();

    showCanvasWindow();

    Q_EMIT showSignal();

    if (m_latteView && m_latteView->layout()) {
        m_latteView->layout()->setLastConfigViewFor(m_latteView);
    }
}

void PrimaryConfigView::hideEvent(QHideEvent *ev)
{
    if (!m_latteView) {
        return;
    }

    //! deliberately NO session-end work here: hideEvent fires for transient
    //! hides too (deferred show on view retargeting, layer-surface remaps).
    //! Ending the session belongs to hideConfigWindow(), the deliberate
    //! close path (close button, focus loss, layout switching).

    setVisible(false);
}

SecondaryConfigView *PrimaryConfigView::secondaryWindow() const
{
    return m_secConfigView;
}

bool PrimaryConfigView::hasFocus() const
{
    bool primaryHasHocus{isActive()};
    bool secHasFocus{m_secConfigView && m_secConfigView->isActive()};
    bool canvasHasFocus{m_canvasConfigView && m_canvasConfigView->isActive()};
    bool viewHasFocus{m_latteView && (m_latteView->containsMouse() || m_latteView->alternativesIsShown())};

    return (m_blockFocusLost || viewHasFocus || primaryHasHocus || secHasFocus || canvasHasFocus);
}

void PrimaryConfigView::focusOutEvent(QFocusEvent *ev)
{
    Q_UNUSED(ev);

    if (!m_latteView) {
        return;
    }

    const auto *focusWindow = qGuiApp->focusWindow();

    if (focusWindow && (focusWindow->flags().testFlag(Qt::Popup)
                                || focusWindow->flags().testFlag(Qt::ToolTip))) {
        return;
    }

    //! Focus moving to our own canvas or chooser window is never a loss. On
    //! Wayland the layer-shell chrome windows take keyboard focus the moment
    //! they map, BEFORE their isActive() state lands, so the hasFocus() check
    //! below races that transfer: the primary closed itself the instant its
    //! own canvas window showed (observed live at 1ms apart). Identity of the
    //! new focus window is race-free where activation state is not.
    if (focusWindow && (focusWindow == m_canvasConfigView || focusWindow == m_secConfigView)) {
        return;
    }

    if (!hasFocus()) {
        hideConfigWindow();
    }
}

void PrimaryConfigView::immutabilityChanged(Plasma::Types::ImmutabilityType type)
{
    if (type != Plasma::Types::Mutable && isVisible()) {
        hideConfigWindow();
    }
}

bool PrimaryConfigView::isReady() const
{
    return m_isReady;
}

void PrimaryConfigView::setIsReady(bool ready)
{
    if (m_isReady == ready) {
        return;
    }

    m_isReady = ready;
    Q_EMIT isReadyChanged();
}


bool PrimaryConfigView::sticker() const
{
    return m_blockFocusLost;
}

void PrimaryConfigView::setSticker(bool blockFocusLost)
{
    if (m_blockFocusLost == blockFocusLost)
        return;

    m_blockFocusLost = blockFocusLost;
}

bool PrimaryConfigView::showInlineProperties() const
{
    return m_showInlineProperties;
}
void PrimaryConfigView::setShowInlineProperties(bool show)
{
    if (m_showInlineProperties == show) {
        return;
    }

    m_showInlineProperties = show;
    Q_EMIT showInlinePropertiesChanged();
}

void PrimaryConfigView::updateShowInlineProperties()
{
    if (!m_latteView) {
        return;
    }

    bool showSecWindow{false};
    bool advancedApprovedSecWindow{false};

    if (inAdvancedMode() && m_latteView->formFactor() != Plasma::Types::Vertical) {
        showSecWindow = true;
        advancedApprovedSecWindow = true;
    }

    //! consider screen geometry for showing or not the secondary window
    if (showSecWindow && !geometryWhenVisible().isNull()) {
        if (m_secConfigView && m_secConfigView->geometryWhenVisible().intersects(geometryWhenVisible())) {
            showSecWindow = false;
        } else if (advancedApprovedSecWindow) {
            showSecWindow = true;
        }
    }

    if (showSecWindow) {
        showSecondaryWindow();

       // QTimer::singleShot(150, m_secConfigView, SLOT(show()));
        setShowInlineProperties(false);
    } else {
        hideSecondaryWindow();
        setShowInlineProperties(true);
    }

    // qDebug() << " showSecWindow:" << showSecWindow << " _ " << " inline:"<< !showSecWindow;
}

bool PrimaryConfigView::inAdvancedMode() const
{
    return m_corona->universalSettings()->inAdvancedModeForEditSettings();
}

//!BEGIN borders
void PrimaryConfigView::updateEnabledBorders()
{
    if (!this->screen()) {
        return;
    }

    KSvg::FrameSvg::EnabledBorders borders = KSvg::FrameSvg::AllBorders;

    switch (m_latteView->location()) {
    case Plasma::Types::TopEdge:
        borders &= m_inReverse ? ~KSvg::FrameSvg::BottomBorder : ~KSvg::FrameSvg::TopBorder;
        break;

    case Plasma::Types::LeftEdge:
        borders &= ~KSvg::FrameSvg::LeftBorder;
        break;

    case Plasma::Types::RightEdge:
        borders &= ~KSvg::FrameSvg::RightBorder;
        break;

    case Plasma::Types::BottomEdge:
        borders &= m_inReverse ? ~KSvg::FrameSvg::TopBorder : ~KSvg::FrameSvg::BottomBorder;
        break;

    default:
        break;
    }

    if (m_enabledBorders != borders) {
        m_enabledBorders = borders;

        m_corona->dialogShadows()->addWindow(this, m_enabledBorders);

        Q_EMIT enabledBordersChanged();
    }
}
//!END borders

void PrimaryConfigView::updateEffects()
{
    //! Don't apply any effect before the wayland surface is created under wayland
    //! https://bugs.kde.org/show_bug.cgi?id=392890
    if (KWindowSystem::isPlatformWayland() && !isVisible()) {
        return;
    }

    if (!m_background) {
        m_background = new KSvg::FrameSvg(this);
    }

    if (m_background->imagePath() != "dialogs/background") {
        m_background->setImagePath(QStringLiteral("dialogs/background"));
    }

    m_background->setEnabledBorders(m_enabledBorders);
    m_background->resizeFrame(size());

    QRegion mask = m_background->mask();

    QRegion fixedMask = mask.isNull() ? QRegion(QRect(0,0,width(),height())) : mask;

    if (!fixedMask.isEmpty()) {
        setMask(fixedMask);
    } else {
        setMask(QRegion());
    }

    if (Latte::compositingActive()) {
        KWindowEffects::enableBlurBehind(this, true, fixedMask);
    } else {
        KWindowEffects::enableBlurBehind(this, false);
    }
}

}
}

