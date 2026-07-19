// SPDX-FileCopyrightText: 2026 Latte Dock contributors
// SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, derived)
// SPDX-FileCopyrightText: 2026 Bree Spektor
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Adopted from David Goree's latte-dock-qt6
// (tests/sceneprobe/imgdiff_main.cpp at cfb516e7,
// github.com/CaptSilver/latte-dock-qt6; see
// docs/archive/captsilver-testability-adoption.md, P1) - Goree's CLI (the positional
// pair + --delta/--budget/--out) is unchanged. Local addition (interaction
// render-golden bridge, P2 / C-I6): the --tier flag resolves the compare
// rigor from SCENEPROBE_TIER through the SAME parseGoldenTier/toleranceForTier
// the sceneprobe render gate uses, so the e2e golden bridge does not duplicate
// the delta/budget numbers in shell - the C++ tier table stays the one source.
// latte-imgdiff <actual.png> <expected.png> [--tier] [--delta N] [--budget F] [--out diff.png]
// Exit 0 on match, 1 on mismatch, 2 on usage/load/tier error. Prints the verdict line to stderr.
#include <QGuiApplication>
#include <QImage>
#include <cstdio>
#include <optional>
#include "imagecompare.h"
using namespace LatteProbe;

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    const QStringList a = app.arguments();
    if (a.size() < 3) {
        std::fprintf(stderr, "usage: latte-imgdiff actual.png expected.png [--tier] [--delta N] [--budget F] [--out diff.png]\n");
        return 2;
    }
    // --tier picks the base tolerance from SCENEPROBE_TIER (bitexact|tolerance)
    // via the shared tier table; explicit --delta/--budget still override it.
    CompareTolerance tol{2, 0.005};
    QString outPath;
    bool useTier = false;
    std::optional<int> deltaOverride;
    std::optional<double> budgetOverride;
    for (int i = 3; i < a.size(); ++i) {
        if (a[i] == QLatin1String("--delta") && i + 1 < a.size()) deltaOverride = a[++i].toInt();
        else if (a[i] == QLatin1String("--budget") && i + 1 < a.size()) budgetOverride = a[++i].toDouble();
        else if (a[i] == QLatin1String("--out") && i + 1 < a.size()) outPath = a[++i];
        else if (a[i] == QLatin1String("--tier")) useTier = true;
    }
    if (useTier) {
        // Refuse an unknown tier LOUDLY at the boundary, exactly as the
        // sceneprobe probe does - a typo must not fall through to a wrong rigor.
        const std::optional<GoldenTier> tier = parseGoldenTier(qgetenv("SCENEPROBE_TIER"));
        if (!tier) {
            std::fprintf(stderr, "latte-imgdiff: SCENEPROBE_TIER='%s' is not a known golden tier (bitexact|tolerance)\n",
                         qgetenv("SCENEPROBE_TIER").constData());
            return 2;
        }
        tol = toleranceForTier(*tier);
    }
    if (deltaOverride) tol.perChannelDelta = *deltaOverride;
    if (budgetOverride) tol.maxExceedFraction = *budgetOverride;
    QImage actual(a[1]), expected(a[2]);
    if (actual.isNull() || expected.isNull()) {
        std::fprintf(stderr, "latte-imgdiff: failed to load input(s)\n");
        return 2;
    }
    const CompareResult r = compareImages(actual, expected, tol);
    std::fprintf(stderr, "%s\n", qPrintable(verdictLine(a[1], QStringLiteral("imgdiff"), r)));
    if (!outPath.isEmpty()) amplifiedDiff(actual, expected).save(outPath);
    return r.match ? 0 : 1;
}
