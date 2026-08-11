/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Real-link behavioral test (through lattedock-core) for the ThemeExtended
// runtime scheme refresh, D298 (ThemeExtended scheme snapshots stale after a
// runtime color-scheme change):
//
// On the Plasma 6 auto-accent path SchemeColors::possibleSchemeFile("kdeglobals")
// resolves to the CONSTANT ~/.config/kdeglobals whenever kdeglobals carries
// [WM] activeBackground, so a runtime color-scheme change arrives as new
// content at an unchanged path. The pre-fix path-equality early return in
// Theme::setOriginalSchemeFile() swallowed every watcher-driven refresh, and
// the dark/light/reversed scheme snapshots plus isLightTheme stayed stale
// until restart. Theme::refreshOriginalScheme() re-snapshots unconditionally
// from the two KDirWatch callbacks; this test drives the REAL watcher wiring:
// plant a dark auto-accent kdeglobals, Theme::load(), rewrite the file light
// in place, and require the published schemes to follow.
//
// Hermetic env (custom main, set before QGuiApplication): XDG_CONFIG_HOME,
// XDG_DATA_HOME, XDG_DATA_DIRS and XDG_CACHE_HOME all point at a temp root,
// so no plasma theme colors file can resolve (forcing the kdeglobals branch
// of loadThemePaths()) and the planted kdeglobals is the one watched.

// local
#include "plasma/extended/theme.h"
#include "wm/schemecolors.h"
#include "tools/commontools.h"

// Qt
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

// KDE
#include <KSharedConfig>

using Latte::PlasmaExtended::Theme;
using Latte::WindowSystem::SchemeColors;

namespace {

//! auto-accent marker present in both bodies: [WM] activeBackground keeps
//! possibleSchemeFile("kdeglobals") on the constant-path resolution across
//! the flip, which is exactly the condition that exposed the stale snapshots
const QString darkKdeglobals = QStringLiteral(
    "[General]\n"
    "ColorScheme=ProbeDark\n"
    "\n"
    "[WM]\n"
    "activeBackground=35,38,41\n"
    "activeForeground=252,252,252\n"
    "inactiveBackground=42,46,50\n"
    "inactiveForeground=161,169,177\n"
    "\n"
    "[Colors:Window]\n"
    "BackgroundNormal=35,38,41\n"
    "ForegroundNormal=252,252,252\n");

const QString lightKdeglobals = QStringLiteral(
    "[General]\n"
    "ColorScheme=ProbeLight\n"
    "\n"
    "[WM]\n"
    "activeBackground=227,229,231\n"
    "activeForeground=35,38,41\n"
    "inactiveBackground=239,240,241\n"
    "inactiveForeground=112,125,138\n"
    "\n"
    "[Colors:Window]\n"
    "BackgroundNormal=239,240,241\n"
    "ForegroundNormal=35,38,41\n");

bool writeFileAtomicallyEnough(const QString &path, const QString &body)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    QTextStream(&f) << body;
    return true;
}

} // namespace

class ThemeExtendedRefreshTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    //! the defect's premise: under auto-accent the resolved scheme file is a
    //! constant path, so changes can only ever arrive as content changes
    void resolveConstantKdeglobalsPathUnderAutoAccent();

    //! the fix: a runtime rewrite of that constant path refreshes the
    //! published scheme snapshots and the lightness through the real watcher
    void refreshSchemesOnRuntimeKdeglobalsRewrite();

private:
    QString m_kdeglobalsPath;
};

void ThemeExtendedRefreshTest::initTestCase()
{
    m_kdeglobalsPath = Latte::configPath() + QStringLiteral("/kdeglobals");
    QVERIFY(writeFileAtomicallyEnough(m_kdeglobalsPath, darkKdeglobals));
}

void ThemeExtendedRefreshTest::resolveConstantKdeglobalsPathUnderAutoAccent()
{
    QCOMPARE(SchemeColors::possibleSchemeFile(QStringLiteral("kdeglobals")), m_kdeglobalsPath);
}

void ThemeExtendedRefreshTest::refreshSchemesOnRuntimeKdeglobalsRewrite()
{
    Theme theme(KSharedConfig::openConfig(), nullptr);
    theme.load();

    //! startup state from the dark kdeglobals; the default snapshot reads
    //! [Colors:Window] (plasmaTheme scheme semantics)
    QVERIFY(theme.defaultTheme());
    QCOMPARE(theme.defaultTheme()->backgroundColor(), QColor(35, 38, 41));
    QCOMPARE(theme.defaultTheme()->textColor(), QColor(252, 252, 252));
    QVERIFY(!theme.isLightTheme());
    QCOMPARE(theme.darkTheme(), theme.defaultTheme());

    //! KDirWatch verifies inotify events with a stat whose timestamps are
    //! SECOND-granular (time_t in scanEntry, kcoreaddons 6.28); a rewrite in
    //! the same wall-clock second as the recorded stat scans as NoChange and
    //! emits nothing. Real scheme flips happen well after startup, so the
    //! test waits out the second instead of racing that dedup (measured: the
    //! immediate rewrite lost the dirty signal in roughly half the runs).
    const QFileInfo watched(m_kdeglobalsPath);
    const qint64 recordedSec = qMax(watched.lastModified().toSecsSinceEpoch(),
                                    watched.metadataChangeTime().toSecsSinceEpoch());
    while (QDateTime::currentSecsSinceEpoch() <= recordedSec) {
        QTest::qWait(50);
    }

    //! the runtime scheme change: new content, same path. Only the KDirWatch
    //! inotify route can deliver this (a raw file write emits no KConfig
    //! change notification), so the assertion below proves the watcher path
    QVERIFY(writeFileAtomicallyEnough(m_kdeglobalsPath, lightKdeglobals));

    //! the pre-fix behavior kept the dark snapshot forever, so this re-read
    //! of defaultTheme() (the object is recreated on refresh) times out red
    //! without the fix
    QTRY_COMPARE_WITH_TIMEOUT(theme.defaultTheme()->backgroundColor(), QColor(239, 240, 241), 10000);
    QCOMPARE(theme.defaultTheme()->textColor(), QColor(35, 38, 41));
    QVERIFY(theme.isLightTheme());
    QCOMPARE(theme.lightTheme(), theme.defaultTheme());

    //! the reversed snapshot must have been regenerated from the new content
    //! too: its window colors are the light ones swapped
    QVERIFY(theme.darkTheme());
    QVERIFY(theme.darkTheme() != theme.defaultTheme());
    QCOMPARE(theme.darkTheme()->backgroundColor(), QColor(35, 38, 41));
    QCOMPARE(theme.darkTheme()->textColor(), QColor(239, 240, 241));
}

int main(int argc, char *argv[])
{
    //! the whole XDG surface moves to a temp root before QGuiApplication:
    //! config home carries the planted kdeglobals this test rewrites, and
    //! empty data dirs guarantee no plasma theme colors file resolves, so
    //! loadThemePaths() takes the kdeglobals branch under test
    static QTemporaryDir xdgRoot;
    qputenv("XDG_CONFIG_HOME", (xdgRoot.path() + QStringLiteral("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (xdgRoot.path() + QStringLiteral("/data")).toUtf8());
    qputenv("XDG_DATA_DIRS", (xdgRoot.path() + QStringLiteral("/data")).toUtf8());
    qputenv("XDG_CACHE_HOME", (xdgRoot.path() + QStringLiteral("/cache")).toUtf8());
    QDir().mkpath(xdgRoot.path() + QStringLiteral("/config"));
    QDir().mkpath(xdgRoot.path() + QStringLiteral("/data"));

    QGuiApplication app(argc, argv);
    ThemeExtendedRefreshTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "themeextendedrefreshtest.moc"
