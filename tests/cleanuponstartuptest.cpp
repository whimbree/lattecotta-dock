/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// D302 (the startup config scrub wrote around the corona's live KConfig
// repository): AbstractLayout::setFile and Plasma's Corona::config() open an
// active layout file as (path, KConfig::SimpleConfig), and the CentralLayout
// is constructed before Manager::loadLatteLayout runs its cleanup. The
// KSharedConfig cache keys on open flags, so the old default-flags open in
// Manager::cleanupOnStartup created a SECOND repository over the same file:
// its deletions synced the disk but never reached the already-parsed entry
// map the session restores from, and a restored ghost containment could then
// write itself straight back to disk.
//
// This test constructs exactly that corona-shaped double-open against temp
// layout fixtures and pins both duties of the real cleanupOnStartup:
// the scrub lands in the already-open SimpleConfig repository (the
// in-session half - red before the D302 fix), and it reaches the disk bytes
// even though the shared repository outlives the call (the on-disk half,
// which the fix's explicit sync carries; the pre-fix code got it for free
// from the temporary instance's destructor).

// local
#include "layouts/manager.h"

// KDE
#include <KConfigGroup>
#include <KSharedConfig>

// Qt
#include <QFile>
#include <QGuiApplication>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

namespace {

//! One layout fixture carrying both scrub targets next to a survivor of each
//! kind: deprecated contextmenu action bindings ("1") beside a binding that
//! must stay ("2"), and a ghost desktopcontainment ("7") beside a real Latte
//! containment ("8").
const char *const SEEDCONTENTS =
    "[ActionPlugins][1]\n"
    "RightButton;NoModifier=org.kde.contextmenu\n"
    "\n"
    "[ActionPlugins][2]\n"
    "RightButton;NoModifier=org.kde.standarddockmenu\n"
    "\n"
    "[Containments][7]\n"
    "plugin=org.kde.desktopcontainment\n"
    "\n"
    "[Containments][8]\n"
    "plugin=org.kde.latte.containment\n";

} // namespace

class CleanupOnStartupTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void scrubIsVisibleToTheCoronasSharedRepository();
    void scrubReachesDiskWhileTheSharedRepositoryLives();

private:
    //! Seeds a fresh fixture under its own name: KSharedConfig caches by
    //! path, so every case gets a distinct file to keep its repository
    //! state independent of the other cases.
    QString seedLayoutFile(const QString &fileName);

    QTemporaryDir m_tempDir;
};

QString CleanupOnStartupTest::seedLayoutFile(const QString &fileName)
{
    const QString path = m_tempDir.path() + QStringLiteral("/") + fileName;

    QFile seed(path);
    if (!seed.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QString();
    }

    seed.write(SEEDCONTENTS);
    seed.close();

    return path;
}

void CleanupOnStartupTest::scrubIsVisibleToTheCoronasSharedRepository()
{
    const QString path = seedLayoutFile(QStringLiteral("visible.layout.latte"));
    QVERIFY(!path.isEmpty());

    //! The corona-shaped open FIRST, exactly as startup orders it: the
    //! CentralLayout's setFile parses the file before cleanupOnStartup runs.
    KSharedConfigPtr coronaRepository =
        KSharedConfig::openConfig(path, KConfig::SimpleConfig);
    QVERIFY(KConfigGroup(coronaRepository, "ActionPlugins").hasGroup("1"));
    QVERIFY(KConfigGroup(coronaRepository, "Containments").hasGroup("7"));

    Latte::Layouts::Manager::cleanupOnStartup(path);

    //! No reparse: the scrub must have landed in the SAME repository the
    //! session restores from, or the corona loads the pre-scrub state (D302).
    KConfigGroup actionGroups(coronaRepository, "ActionPlugins");
    QVERIFY2(!actionGroups.hasGroup("1"),
             "deprecated contextmenu binding survived in the corona's repository");
    QVERIFY2(actionGroups.hasGroup("2"),
             "non-deprecated action binding was scrubbed too");

    KConfigGroup containmentGroups(coronaRepository, "Containments");
    QVERIFY2(!containmentGroups.hasGroup("7"),
             "ghost desktopcontainment survived in the corona's repository");
    QVERIFY2(containmentGroups.hasGroup("8"),
             "real Latte containment was scrubbed too");
}

void CleanupOnStartupTest::scrubReachesDiskWhileTheSharedRepositoryLives()
{
    const QString path = seedLayoutFile(QStringLiteral("disk.layout.latte"));
    QVERIFY(!path.isEmpty());

    //! Held across the call: the shared repository outliving cleanupOnStartup
    //! is the startup reality (the CentralLayout keeps it), so the on-disk
    //! scrub may not rely on a destructor sync of a temporary instance.
    KSharedConfigPtr coronaRepository =
        KSharedConfig::openConfig(path, KConfig::SimpleConfig);
    QVERIFY(KConfigGroup(coronaRepository, "ActionPlugins").hasGroup("1"));

    Latte::Layouts::Manager::cleanupOnStartup(path);

    //! Raw bytes, not KConfig: any KConfig read could be served from the
    //! cached repository; the disk truth is what Storage's independent opens
    //! of the same file parse.
    QFile onDisk(path);
    QVERIFY(onDisk.open(QIODevice::ReadOnly));
    const QByteArray bytes = onDisk.readAll();

    QVERIFY2(!bytes.contains("[ActionPlugins][1]"),
             "deprecated contextmenu binding still on disk");
    QVERIFY2(bytes.contains("[ActionPlugins][2]"),
             "non-deprecated action binding lost from disk");
    QVERIFY2(!bytes.contains("[Containments][7]"),
             "ghost desktopcontainment still on disk");
    QVERIFY2(bytes.contains("[Containments][8]"),
             "real Latte containment lost from disk");
}

int main(int argc, char *argv[])
{
    // Point the XDG homes at a throwaway dir before QGuiApplication: the
    // fixtures are explicit temp paths, but the pre-fix default-flags open
    // cascades kdeglobals and nothing here may read or write the real
    // desktop's config (same pattern as storagetest).
    static QTemporaryDir xdgHome;
    qputenv("XDG_CONFIG_HOME", (xdgHome.path() + QStringLiteral("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (xdgHome.path() + QStringLiteral("/data")).toUtf8());

    QGuiApplication app(argc, argv);
    CleanupOnStartupTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "cleanuponstartuptest.moc"
