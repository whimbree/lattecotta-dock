/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef INPUTMASKFLUSH_H
#define INPUTMASKFLUSH_H

// Qt
#include <QRect>

namespace Latte {
namespace ViewPart {

//! Qt6's wayland backend couples QWindow::mask() to each frame's submitted
//! buffer damage (Effects::setInputMask records the history: an empty mask
//! froze the whole surface at its last content). The consequence that forced
//! this seam: when a masked dock's visible band shrinks along its LENGTH axis,
//! the just-vacated edge pixels are repainted transparent by the scene graph,
//! but that damage falls outside the new, smaller mask and is dropped. The
//! compositor keeps compositing the stale semi-transparent panel pixels there,
//! a lighter frosted band at the former extent (reproduced live on a real top
//! dock, 2026-07-18). Qt5/X11 shape masks did not clip damage, so this is a
//! platform-forced Qt6 deviation with no upstream precedent.
//!
//! BLAST RADIUS - the only external driver of the input mask is
//! VisibilityManager.qml -> effects.inputMask, so this decision governs EVERY
//! input-mask shrink, not just maximize-length. computeInputMask shrinks the
//! band along the LENGTH axis on two paths - "maximize panel length in presence
//! of maximized windows" releasing on un-maximize, and parabolic zoom-OUT
//! (unhover) narrowing the full-length zoomed band back to the applet band -
//! and BOTH are the same frosted-band bug, so BOTH are held by design. An
//! autohide/dodge HIDE collapses the band to its reveal strip (and a hidden
//! sidebar to the accept-input-nowhere sentinel); those are deliberately NOT
//! held, classified by the visibility state, not band shape (see below).
//!
//! The fix keeps the WINDOW mask at the union of the bands seen since a
//! LENGTH-axis shrink began, so the shrink never clips the vacated region's
//! clearing damage, and collapses back to the exact band once the band stops
//! changing (a coalescing timer in Effects). These pure helpers own the "what
//! region to hand QWindow::setMask" decision so the invariant is testable
//! without a live compositor. m_inputMask still reports the logical band for
//! readback.
namespace InputMaskFlush {

//! The region to hand QWindow::setMask, given the region currently applied to
//! the window, the new logical band, the dock's LENGTH axis (horizontal for
//! Top/Bottom docks, vertical for Left/Right), and whether the dock is hidden.
//! A clear/degenerate band clears the mask; a first band with no prior applied
//! mask is used as-is.
//!
//! The union is held ONLY for a visible dock's shrink along the LENGTH axis:
//! that is the frosted-band case (the dock stays put and its length-ends
//! vacate), so the union keeps the vacated ends inside the mask and their
//! clearing damage is submitted. A grow, a visible thickness-axis shrink, and
//! EVERY hidden-dock band return the band directly.
//!
//! A hidden dock's band is a departure artifact - the autohide/dodge reveal
//! strip, or a hidden sidebar's accept-input-nowhere sentinel - never a
//! visible band that shrank in place: the dock leaves, nothing stale is
//! stranded where it stood, and holding the former band as the WINDOW mask
//! would over-capture pointer input across the vacated dock body while the
//! dock is hidden (clicks swallowed instead of falling through). The hidden
//! flag, not band shape, makes that call (fix D4): a hide landing while the
//! previous band was parabolic-zoomed shrinks BOTH axes exactly like the
//! parabolic zoom-out that MUST hold, and the sentinel is a valid 1x1 rect
//! every geometric test misreads as a length shrink - before this flag, a
//! hidden sidebar's sentinel was unioned into a full-window input mask for the
//! settle interval on every hide (caught in the nested vehicle, 2026-08-11).
//! Input hit-testing rides the same mask as the damage clip, so the hold is
//! scoped to exactly where the frosted band is.
inline QRect windowMaskFor(const QRect &applied, const QRect &band, Qt::Orientation lengthAxis,
                           bool dockIsHidden)
{
    if (!band.isValid() || band.isEmpty()) {
        return QRect();
    }

    if (!applied.isValid() || applied.isEmpty()) {
        return band;
    }

    if (dockIsHidden) {
        return band;
    }

    const bool lengthShrank = (lengthAxis == Qt::Horizontal)
            ? band.width() < applied.width()
            : band.height() < applied.height();

    if (!lengthShrank) {
        return band;
    }

    //! Contract: on a length shrink the region handed to setMask never drops
    //! coverage of what is currently applied. The union keeps the vacated ends'
    //! clearing damage inside the mask (the whole reason this seam exists);
    //! coverage only narrows through the deliberate settle collapse in Effects,
    //! never here. united() satisfies this by construction. A naive `return
    //! band` violates it on a shrink and trips this assert under the sanitized
    //! tests (QT_FORCE_ASSERTS live, stripped in the shipped dock) - the
    //! tripwire that keeps a future "simplification" from reintroducing the
    //! stale frosted band.
    const QRect result = applied.united(band);
    Q_ASSERT(result.contains(applied));
    return result;
}

//! Whether the applied window mask is still wider than the band, so the settle
//! collapse must run once the band stops changing (steady-state hit-testing and
//! libplasma popup anchoring both read the window mask and need the real band).
inline bool needsSettleCollapse(const QRect &applied, const QRect &band)
{
    return band.isValid() && !band.isEmpty() && applied != band;
}

} // namespace InputMaskFlush
} // namespace ViewPart
} // namespace Latte

#endif
