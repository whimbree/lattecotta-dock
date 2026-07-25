/*
    SPDX-FileCopyrightText: 2011 Aaron Seigo <aseigo@kde.org>
    SPDX-FileCopyrightText: 2013 Marco Martin <mart@kde.org>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

// Extra shadow padding derived from plasma-workspace
// (shell/panelshadows.cpp at 4c3ace3dfc7b06b3107b52b6e09508be14e73e8a,
// invent.kde.org/plasma/plasma-workspace; floating-panel refactor
// f7ee03d065b4e293746248f749a7965c4321b1cb).

#include "panelshadows_p.h"
#include "panelshadowstate.h"

#include <QDebug>
#include <KWindowShadow>

class PanelShadows::Private
{
public:
    Private(PanelShadows *shadows)
        : q(shadows)
    {
    }

    ~Private() = default;

    void clearTiles();
    void setupTiles();
    void initTile(const QString &element);
    void updateShadow(
        QWindow *window,
        const Latte::ViewPart::PanelShadowState::State &state);
    void clearShadow(QWindow *window);
    void updateShadows();
    bool hasShadows() const;

    PanelShadows *const q;

    Latte::ViewPart::PanelShadowState::Registry<QWindow *> m_windows;
    QHash<QWindow *, KWindowShadow *> m_shadows;
    QVector<KWindowShadowTile::Ptr> m_tiles;
};

class PanelShadowsSingleton
{
public:
    PanelShadowsSingleton() = default;

    PanelShadows self;
};

Q_GLOBAL_STATIC(PanelShadowsSingleton, privatePanelShadowsSelf)

PanelShadows::PanelShadows(QObject *parent, const QString &prefix)
    : KSvg::Svg(parent)
    , d(new Private(this))
{
    setImagePath(prefix);
    connect(this, &KSvg::Svg::repaintNeeded, this, [this]() {
        d->updateShadows();
    });
}

PanelShadows::~PanelShadows()
{
    delete d;
}

PanelShadows *PanelShadows::self()
{
    return &privatePanelShadowsSelf->self;
}

void PanelShadows::addWindow(
    QWindow *window,
    KSvg::FrameSvg::EnabledBorders enabledBorders,
    const QMargins &extraPadding)
{
    if (!window) {
        return;
    }

    const Latte::ViewPart::PanelShadowState::State state{
        enabledBorders, extraPadding};
    const auto update = d->m_windows.update(window, state);
    if (update
        == Latte::ViewPart::PanelShadowState::Update::Unchanged) {
        return;
    }

    d->updateShadow(window, state);
    if (update
        == Latte::ViewPart::PanelShadowState::Update::Changed) {
        return;
    }

    connect(window, &QObject::destroyed, this, [this, window]() {
        (void)d->m_windows.remove(window);
        d->clearShadow(window);
        if (d->m_windows.isEmpty()) {
            d->clearTiles();
        }
    });
}

void PanelShadows::removeWindow(QWindow *window)
{
    if (!d->m_windows.remove(window)) {
        return;
    }

    disconnect(window, nullptr, this, nullptr);
    d->clearShadow(window);

    if (d->m_windows.isEmpty()) {
        d->clearTiles();
    }
}

void PanelShadows::setEnabledBorders(QWindow *window, KSvg::FrameSvg::EnabledBorders enabledBorders)
{
    if (!window) {
        return;
    }

    auto state = d->m_windows.stateFor(window);
    if (!state
        || state->enabledBorders == enabledBorders) {
        return;
    }

    state->enabledBorders = enabledBorders;
    (void)d->m_windows.update(window, *state);
    d->updateShadow(window, *state);
}

void PanelShadows::setExtraPadding(QWindow *window,
                                   const QMargins &extraPadding)
{
    if (!window) {
        return;
    }

    auto state = d->m_windows.stateFor(window);
    if (!state
        || state->extraPadding == extraPadding) {
        return;
    }

    state->extraPadding = extraPadding;
    (void)d->m_windows.update(window, *state);
    d->updateShadow(window, *state);
}

void PanelShadows::Private::updateShadows()
{
    const bool hadShadowsBefore = !m_tiles.isEmpty();

    // has shadows now?
    if (hasShadows()) {
        if (hadShadowsBefore) {
            clearTiles();
        }
        for (auto i = m_windows.states().constBegin();
             i != m_windows.states().constEnd(); ++i) {
            updateShadow(i.key(), i.value());
        }
    } else {
        if (hadShadowsBefore) {
            for (auto i = m_windows.states().constBegin();
                 i != m_windows.states().constEnd(); ++i) {
                clearShadow(i.key());
            }
            clearTiles();
        }
    }
}

void PanelShadows::Private::initTile(const QString &element)
{
    const QImage image = q->pixmap(element).toImage();

    KWindowShadowTile::Ptr tile = KWindowShadowTile::Ptr::create();
    tile->setImage(image);

    m_tiles << tile;
}

void PanelShadows::Private::setupTiles()
{
    clearTiles();

    initTile(QStringLiteral("shadow-top"));
    initTile(QStringLiteral("shadow-topright"));
    initTile(QStringLiteral("shadow-right"));
    initTile(QStringLiteral("shadow-bottomright"));
    initTile(QStringLiteral("shadow-bottom"));
    initTile(QStringLiteral("shadow-bottomleft"));
    initTile(QStringLiteral("shadow-left"));
    initTile(QStringLiteral("shadow-topleft"));
}

void PanelShadows::Private::clearTiles()
{
    m_tiles.clear();
}

void PanelShadows::Private::updateShadow(QWindow *window,
    const Latte::ViewPart::PanelShadowState::State &state)
{
    if (!hasShadows()) {
        return;
    }

    if (m_tiles.isEmpty()) {
        setupTiles();
    }

    KWindowShadow *&shadow = m_shadows[window];

    if (!shadow) {
        shadow = new KWindowShadow(q);
    }

    if (shadow->isCreated()) {
        shadow->destroy();
    }

    const auto enabledBorders = state.enabledBorders;

    if (enabledBorders & KSvg::FrameSvg::TopBorder) {
        shadow->setTopTile(m_tiles.at(0));
    } else {
        shadow->setTopTile(nullptr);
    }

    if (enabledBorders & KSvg::FrameSvg::TopBorder && enabledBorders & KSvg::FrameSvg::RightBorder) {
        shadow->setTopRightTile(m_tiles.at(1));
    } else {
        shadow->setTopRightTile(nullptr);
    }

    if (enabledBorders & KSvg::FrameSvg::RightBorder) {
        shadow->setRightTile(m_tiles.at(2));
    } else {
        shadow->setRightTile(nullptr);
    }

    if (enabledBorders & KSvg::FrameSvg::BottomBorder && enabledBorders & KSvg::FrameSvg::RightBorder) {
        shadow->setBottomRightTile(m_tiles.at(3));
    } else {
        shadow->setBottomRightTile(nullptr);
    }

    if (enabledBorders & KSvg::FrameSvg::BottomBorder) {
        shadow->setBottomTile(m_tiles.at(4));
    } else {
        shadow->setBottomTile(nullptr);
    }

    if (enabledBorders & KSvg::FrameSvg::BottomBorder && enabledBorders & KSvg::FrameSvg::LeftBorder) {
        shadow->setBottomLeftTile(m_tiles.at(5));
    } else {
        shadow->setBottomLeftTile(nullptr);
    }

    if (enabledBorders & KSvg::FrameSvg::LeftBorder) {
        shadow->setLeftTile(m_tiles.at(6));
    } else {
        shadow->setLeftTile(nullptr);
    }

    if (enabledBorders & KSvg::FrameSvg::TopBorder && enabledBorders & KSvg::FrameSvg::LeftBorder) {
        shadow->setTopLeftTile(m_tiles.at(7));
    } else {
        shadow->setTopLeftTile(nullptr);
    }

    QMargins padding;

    if (enabledBorders & KSvg::FrameSvg::TopBorder) {
        const QSize marginHint = q->elementSize(QStringLiteral("shadow-hint-top-margin")).toSize();
        if (marginHint.isValid()) {
            padding.setTop(marginHint.height());
        } else {
            padding.setTop(m_tiles[0]->image().height());
        }
    }

    if (enabledBorders & KSvg::FrameSvg::RightBorder) {
        const QSize marginHint = q->elementSize(QStringLiteral("shadow-hint-right-margin")).toSize();
        if (marginHint.isValid()) {
            padding.setRight(marginHint.width());
        } else {
            padding.setRight(m_tiles[2]->image().width());
        }
    }

    if (enabledBorders & KSvg::FrameSvg::BottomBorder) {
        const QSize marginHint = q->elementSize(QStringLiteral("shadow-hint-bottom-margin")).toSize();
        if (marginHint.isValid()) {
            padding.setBottom(marginHint.height());
        } else {
            padding.setBottom(m_tiles[4]->image().height());
        }
    }

    if (enabledBorders & KSvg::FrameSvg::LeftBorder) {
        const QSize marginHint = q->elementSize(QStringLiteral("shadow-hint-left-margin")).toSize();
        if (marginHint.isValid()) {
            padding.setLeft(marginHint.width());
        } else {
            padding.setLeft(m_tiles[6]->image().width());
        }
    }

    shadow->setPadding(padding + state.extraPadding);
    shadow->setWindow(window);

    if (!shadow->create()) {
        qDebug() << "Couldn't create KWindowShadow for" << window;
    }
}

void PanelShadows::Private::clearShadow(QWindow *window)
{
    delete m_shadows.take(window);
}

bool PanelShadows::Private::hasShadows() const
{
    return q->hasElement(QStringLiteral("shadow-left"));
}

#include "moc_panelshadows_p.cpp"
