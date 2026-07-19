// SPDX-FileCopyrightText: 2026 Latte Dock contributors
// SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Adopted from David Goree's latte-dock-qt6
// (tests/sceneprobe/imagecompare.h at c3633f1a,
// github.com/CaptSilver/latte-dock-qt6; see
// docs/reference/captsilver-testability-adoption.md, P1) - the comparator API
// (CompareTolerance/CompareResult and its five functions) is Goree's,
// unchanged. Local addition (multi-distro CI Phase C): the GoldenTier
// vocabulary + parseGoldenTier/toleranceForTier, which decouple the
// golden-compare RIGOR from the render DEVICE so a tolerance-tier distro
// (Fedora, KDE neon) gates at a bounded delta while the NixOS merge gate
// stays bit-exact.
#pragma once
#include <QByteArray>
#include <QImage>
#include <QColor>
#include <QRect>
#include <QString>
#include <QVariantList>
#include <optional>

namespace LatteProbe {

struct CompareTolerance {
    int perChannelDelta = 0;       // max allowed |delta| per channel (0..255); 0 = exact
    double maxExceedFraction = 0.0; // fraction of pixels allowed to exceed perChannelDelta
};

// Golden-compare rigor tier, decoupled from the render DEVICE. The device
// keys the golden FILENAME (every matrix distro renders the lavapipe device,
// so they all compare .expected.lavapipe.png); the tier keys HOW TIGHTLY that
// compare gates. BitExact is the NixOS/dev merge gate (byte-identical);
// Tolerance is the bounded-delta rolling/non-nix release gate. Modelled as an
// enum so a bad tier cannot be represented past the parse boundary - the
// device axis once carried both jobs and denied Fedora/neon the tolerance
// they actually render at, which is the coupling this split removes.
enum class GoldenTier { BitExact, Tolerance };

// Parse the SCENEPROBE_TIER env value. Empty (unset) defaults to BitExact so
// the merge gate stays byte-identical when nothing sets the tier. An unknown
// value returns nullopt so the caller refuses it LOUDLY rather than silently
// falling through to a wrong rigor (a typo'd tier must not mask a regression
// by defaulting to bit-exact, nor hide one by defaulting to tolerance).
// Case-sensitive by design: the tier is a fixed vocabulary set by the
// container ENV and the gate script, not free-form user text.
std::optional<GoldenTier> parseGoldenTier(const QByteArray &raw);

// The CompareTolerance each tier gates at. The tolerance-tier value and its
// measured justification live at the definition site.
CompareTolerance toleranceForTier(GoldenTier tier);

struct CompareResult {
    bool match = true;
    int diffPixels = 0;
    double diffFraction = 0.0;
    int maxDelta = 0;
    int maxX = -1;
    int maxY = -1;
    QColor expectedAtMax;
    QColor actualAtMax;
    QRect diffBounds;
};

// "" if OK, else a human-readable failure reason.
QString checkInvariants(const QImage &actual, double minOpaqueFraction);
// "" if all probeExpect entries pass, else first failure reason.
QString checkExpectations(const QImage &actual, const QVariantList &expectations);
CompareResult compareImages(const QImage &actual, const QImage &expected, const CompareTolerance &tol);
QImage amplifiedDiff(const QImage &actual, const QImage &expected, int gain = 8);
QString verdictLine(const QString &scene, const QString &device, const CompareResult &r);

} // namespace LatteProbe
