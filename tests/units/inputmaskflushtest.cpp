/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// InputMaskFlush (app/view/inputmaskflush.h): the pure "what region to hand
// QWindow::setMask" decision behind Effects::applyInputMaskToWindow. It exists
// because Qt6's wayland backend clips each frame's submitted buffer damage to
// the window mask: narrowing the mask the instant a masked dock's band shrinks
// along its LENGTH axis strands the just-vacated edge pixels, whose transparent
// repaint is dropped, and the compositor keeps compositing stale semi-
// transparent panel content there (a lighter frosted band at the former extent
// - caught live on a real top dock 2026-07-18 when "maximize panel length in
// presence of maximized windows" grew the dock to full width and released on
// un-maximize).
//
// The invariant this pins: a LENGTH-axis SHRINK keeps the window mask at the
// union of the bands (never clips the vacated region) and only a settle collapse
// narrows it back to the band. Reverting the seam to a direct setMask(band) -
// the shape both reference forks still ship - reintroduces the stale band and
// fails shrinkKeepsUnionUntilSettle below.
//
// The scoping this pins: a HIDDEN dock's band (the autohide/dodge reveal
// strip, a hidden sidebar's accept-input-nowhere sentinel) is NOT held - the
// dock leaves, nothing is stranded where it stood, and holding the former
// band as the window mask would over-capture pointer input across the hidden
// dock's body. The classifier is the dockIsHidden flag, never band shape (fix
// D4, the maximize-length + autohide-hide mask over-capture race): a hide
// landing while the previous band was parabolic-zoomed shrinks BOTH axes
// exactly like the parabolic zoom-out that MUST hold, and the sentinel is a
// valid 1x1 rect every geometric test misreads as a length shrink.
// hiddenBandAppliesDirectlyEvenMidSettle, hiddenSentinelNeverUnions and
// visibleBothAxisShrinkStillHolds pin those three faces.
//
// Every expected rect is hand-derived from the QRect union geometry, not
// produced by running the header under test.

#include <QtTest>

// Qt
#include <QRect>

// C++
#include <type_traits>

#include "../../app/view/inputmaskflush.h"

using namespace Latte::ViewPart::InputMaskFlush;

// invalid states designed out (step-2.5 law): the decision is a pure function
// of two plain value types, the length axis and the hidden flag, no object,
// no sentinel to misread
static_assert(std::is_same_v<decltype(windowMaskFor(QRect(), QRect(), Qt::Horizontal, false)), QRect>,
              "windowMaskFor stays a pure QRect->QRect->axis->hidden->QRect decision");
static_assert(std::is_same_v<decltype(needsSettleCollapse(QRect(), QRect())), bool>,
              "needsSettleCollapse stays a pure predicate");

class InputMaskFlushTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void clearBandClearsMask();
    void firstBandAppliedAsIs();
    void growAppliesBandDirectly();
    void shrinkKeepsUnionNotBand();
    void settlePredicateTracksWidth();
    void maximizeCycleReproduction();
    void shrinkKeepsUnionUntilSettle();
    void animatedShrinkNeverClipsVacatedEdges();
    void thicknessShrinkAppliesBandDirectly();
    void verticalDockHoldsOnHeightShrink();
    void hiddenBandAppliesDirectlyEvenMidSettle();
    void hiddenSentinelNeverUnions();
    void visibleBothAxisShrinkStillHolds();
};

//! A degenerate/clear band (width 0, or the Qt.rect(0,0,-1,-1) explicit clear
//! sentinel the QML mask core emits) clears the window mask regardless of what
//! was applied before.
void InputMaskFlushTest::clearBandClearsMask()
{
    const QRect applied(0, 0, 1440, 32);

    QCOMPARE(windowMaskFor(applied, QRect(), Qt::Horizontal, false), QRect());
    QCOMPARE(windowMaskFor(applied, QRect(0, 0, 0, 0), Qt::Horizontal, false), QRect());
    QCOMPARE(windowMaskFor(applied, QRect(0, 0, -1, -1), Qt::Horizontal, false), QRect());
    // a clear clears regardless of the hidden state
    QCOMPARE(windowMaskFor(applied, QRect(0, 0, -1, -1), Qt::Horizontal, true), QRect());
    // nothing to collapse to once cleared
    QVERIFY(!needsSettleCollapse(QRect(), QRect()));
}

//! With no prior applied mask (startup) the band is handed through unchanged;
//! there is no vacated region to protect yet.
void InputMaskFlushTest::firstBandAppliedAsIs()
{
    const QRect band(44, 8, 1353, 24);

    QCOMPARE(windowMaskFor(QRect(), band, Qt::Horizontal, false), band);
    QCOMPARE(windowMaskFor(QRect(0, 0, 0, 0), band, Qt::Horizontal, false), band);
    QVERIFY(!needsSettleCollapse(band, band));
}

//! Growing (un-maximized band -> full width): a grow is not a length shrink, so
//! the band is applied directly and no collapse is owed. Growing never strands.
void InputMaskFlushTest::growAppliesBandDirectly()
{
    const QRect band(44, 8, 1353, 24);
    const QRect full(0, 0, 1440, 32);

    const QRect grown = windowMaskFor(band, full, Qt::Horizontal, false);
    QCOMPARE(grown, full);
    QVERIFY(!needsSettleCollapse(grown, full));
}

//! Shrinking along the length axis (full width -> band): the union stays at the
//! wider applied region, NOT the band, so the vacated edges [0,44) and
//! [1397,1440) remain inside the window mask and their clearing damage is not
//! clipped. A collapse is owed.
void InputMaskFlushTest::shrinkKeepsUnionNotBand()
{
    const QRect full(0, 0, 1440, 32);
    const QRect band(44, 8, 1353, 24);

    const QRect shrunk = windowMaskFor(full, band, Qt::Horizontal, false);
    QCOMPARE(shrunk, full);                 // stays wide, does not narrow to band
    QVERIFY(shrunk.contains(band));
    QVERIFY(needsSettleCollapse(shrunk, band));

    // the left/right vacated slivers are still covered by the applied mask
    QVERIFY(shrunk.contains(QRect(0, 8, 44, 24)));      // left of the band
    QVERIFY(shrunk.contains(QRect(1397, 8, 43, 24)));   // right of the band
}

//! needsSettleCollapse is exactly "applied is a non-empty band wider than / not
//! equal to the logical band", the condition Effects arms its settle timer on.
void InputMaskFlushTest::settlePredicateTracksWidth()
{
    const QRect band(44, 8, 1353, 24);
    const QRect full(0, 0, 1440, 32);

    QVERIFY(needsSettleCollapse(full, band));    // wider -> collapse owed
    QVERIFY(!needsSettleCollapse(band, band));   // exact -> nothing owed
    QVERIFY(!needsSettleCollapse(full, QRect()));            // empty band -> nothing owed
    QVERIFY(!needsSettleCollapse(full, QRect(0, 0, 0, 0)));  // zero-size band -> nothing owed
    QVERIFY(!needsSettleCollapse(QRect(), QRect()));
}

//! The end-to-end state machine Effects drives across a maximizeWhenMaximized
//! cycle: band -> full (grow, applied==full) -> band (shrink, applied stays
//! full) -> settle collapse (applied==band). This is the exact sequence that
//! produced the live artifact before the fix.
void InputMaskFlushTest::maximizeCycleReproduction()
{
    const QRect band(44, 8, 1353, 24);
    const QRect full(0, 0, 1440, 32);

    QRect applied = band;                        // steady state before maximize

    // maximize: band grows to full
    applied = windowMaskFor(applied, full, Qt::Horizontal, false);
    QCOMPARE(applied, full);
    QVERIFY(!needsSettleCollapse(applied, full));

    // un-maximize: band shrinks; the applied mask must NOT snap to the band
    applied = windowMaskFor(applied, band, Qt::Horizontal, false);
    QCOMPARE(applied, full);
    QVERIFY(needsSettleCollapse(applied, band));

    // settle collapse (the timer's job): now narrow to the exact band
    applied = band;
    QVERIFY(!needsSettleCollapse(applied, band));
}

//! Re-stating the regression as a single assertion a future "simplification"
//! trips: while the band is the shrunk band, the applied window mask must still
//! cover the full former extent (so damage clears it). A direct setMask(band)
//! would make applied == band here and fail.
void InputMaskFlushTest::shrinkKeepsUnionUntilSettle()
{
    const QRect full(0, 0, 1440, 32);
    const QRect band(44, 8, 1353, 24);

    const QRect appliedDuringShrink = windowMaskFor(full, band, Qt::Horizontal, false);
    QVERIFY2(appliedDuringShrink == full,
             "a shrinking band must keep the window mask at the former (wider) "
             "extent so Qt6 wayland does not clip the vacated region's clearing "
             "damage; narrowing straight to the band reintroduces the stale band");
}

//! The shrink is animated (Behavior on length in the containment QML), so the
//! band arrives as many decreasing steps. Each step's union must still cover
//! every edge vacated since the burst began, i.e. the applied mask stays at the
//! burst maximum the whole way down. Verified by folding windowMaskFor across a
//! descending sequence and checking coverage of the first (widest) band.
void InputMaskFlushTest::animatedShrinkNeverClipsVacatedEdges()
{
    const QRect steps[] = {
        QRect(0, 0, 1440, 32),      // full width (maximized)
        QRect(20, 4, 1400, 28),
        QRect(30, 6, 1380, 26),
        QRect(44, 8, 1353, 24),     // settled band
    };

    QRect applied;
    for (const QRect &step : steps) {
        applied = windowMaskFor(applied, step, Qt::Horizontal, false);
        // never clips below the widest band seen so far in the burst
        QVERIFY(applied.contains(steps[0]));
    }

    // and the whole burst stayed pinned at the burst maximum until settle
    QCOMPARE(applied, steps[0]);
    QVERIFY(needsSettleCollapse(applied, steps[3]));
}

//! An autohide/dodge HIDE collapses the band to its reveal strip: same LENGTH
//! (width, for a horizontal dock), thinner, written while the dock is HIDDEN.
//! The strip is applied DIRECTLY, never the union - the dock leaves, nothing
//! stale is stranded where it stood, and holding the former band would keep
//! the whole vacated dock body as the window mask while the dock is hidden,
//! over-capturing pointer input (clicks swallowed, the reveal strip widened).
//! No collapse is owed. A VISIBLE thickness-only shrink (same length, thinner)
//! also applies directly through the length test - only length shrinks hold.
void InputMaskFlushTest::thicknessShrinkAppliesBandDirectly()
{
    const QRect shown(44, 8, 1353, 24);   // shown band, 24px thick
    const QRect strip(44, 30, 1353, 2);   // reveal strip, same width, 2px thick

    const QRect hidden = windowMaskFor(shown, strip, Qt::Horizontal, true);
    QCOMPARE(hidden, strip);               // strip applied directly, not united
    QVERIFY(!needsSettleCollapse(hidden, strip));

    // and the reverse (strip -> shown, a thickness GROW on the visible reveal)
    // also applies the band directly
    QCOMPARE(windowMaskFor(strip, shown, Qt::Horizontal, false), shown);

    // a visible thickness-only shrink is not a length shrink either: direct
    const QRect thinner(44, 14, 1353, 18);
    QCOMPARE(windowMaskFor(shown, thinner, Qt::Horizontal, false), thinner);
}

//! For a Left/Right dock the LENGTH axis is vertical: a height shrink is the
//! frosted-band case and is held at the union, while the hidden reveal strip
//! is applied directly. The mirror of the horizontal cases above.
void InputMaskFlushTest::verticalDockHoldsOnHeightShrink()
{
    const QRect fullV(0, 0, 32, 1440);    // full-height left dock band
    const QRect bandV(8, 44, 24, 1353);   // shorter band

    // length (height) shrink: union held
    const QRect shrunk = windowMaskFor(fullV, bandV, Qt::Vertical, false);
    QCOMPARE(shrunk, fullV);
    QVERIFY(needsSettleCollapse(shrunk, bandV));

    // the hidden reveal strip: applied directly
    const QRect stripV(0, 44, 2, 1353);   // same height as bandV, 2px thick
    QCOMPARE(windowMaskFor(bandV, stripV, Qt::Vertical, true), stripV);
    QVERIFY(!needsSettleCollapse(windowMaskFor(bandV, stripV, Qt::Vertical, true), stripV));
}

//! The D4 race (the maximize-length + autohide-hide mask over-capture): a HIDE
//! landing while a length-shrink settle is still pending compares the strip
//! against the HELD UNION, which any geometric classifier reads as another
//! length shrink (strip width 1353 < held 1440). The hidden flag must win: the
//! strip applies directly even mid-settle, so a hidden dock never holds its
//! former body as the input mask.
void InputMaskFlushTest::hiddenBandAppliesDirectlyEvenMidSettle()
{
    const QRect heldUnion(0, 0, 1440, 32);  // union still held from a length shrink
    const QRect strip(44, 30, 1353, 2);     // the HIDE's reveal strip

    const QRect hidden = windowMaskFor(heldUnion, strip, Qt::Horizontal, true);
    QCOMPARE(hidden, strip);
    QVERIFY(!needsSettleCollapse(hidden, strip));

    // the same hide landing while the previous band was parabolic-zoomed
    // (both axes wider than the strip): still the strip, never a union
    const QRect zoomedFull(0, 0, 1440, 48);
    QCOMPARE(windowMaskFor(zoomedFull, strip, Qt::Horizontal, true), strip);
}

//! A hidden sidebar's accept-input-nowhere sentinel is a VALID 1x1 rect at
//! (-1,-1) (maskgeometrybridge's kAcceptInputNowhere), which a geometric
//! length test misreads as a shrink; before the hidden flag it was unioned
//! with the applied band into a full-window input mask for the settle interval
//! on every hide (caught in the nested vehicle, 2026-08-11). Accept-nowhere
//! must be applied exactly as handed over.
void InputMaskFlushTest::hiddenSentinelNeverUnions()
{
    const QRect band(44, 8, 1353, 24);
    const QRect sentinel(-1, -1, 1, 1);

    QCOMPARE(windowMaskFor(band, sentinel, Qt::Horizontal, true), sentinel);

    // even mid-settle over a held union
    const QRect heldUnion(0, 0, 1440, 32);
    QCOMPARE(windowMaskFor(heldUnion, sentinel, Qt::Horizontal, true), sentinel);
}

//! The case that FORBIDS classifying the hide by band shape: the parabolic
//! zoom-out of a VISIBLE dock shrinks BOTH axes at once (full-span zoomed band
//! back to the resting applet band), exactly the shape of a hide landing while
//! zoomed - and it is one of the two frosted-band cases, so it MUST hold the
//! union. Only the hidden flag separates the two.
void InputMaskFlushTest::visibleBothAxisShrinkStillHolds()
{
    const QRect zoomedFull(0, 0, 1440, 48);   // parabolic: full span, zoomed thickness
    const QRect restBand(44, 24, 1353, 24);   // resting band: shorter AND thinner

    const QRect held = windowMaskFor(zoomedFull, restBand, Qt::Horizontal, false);
    QCOMPARE(held, zoomedFull.united(restBand));
    QVERIFY(held.contains(zoomedFull));       // never clips the vacated zoomed extent
    QVERIFY(needsSettleCollapse(held, restBand));
}

QTEST_APPLESS_MAIN(InputMaskFlushTest)
#include "inputmaskflushtest.moc"
