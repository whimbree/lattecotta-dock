/*
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Real-object behavioral test for the static parser/path helpers of
// Latte::Layouts::Importer (linked through lattedock-core): fileVersion()
// including the .latterc KTar archive branches, nameOfConfigFile() (which
// pins the Qt6 remove(-1,...) end-wrap fix landed right before this test),
// the layout user paths, layoutExists()/availableLayouts(), the system
// shell data paths and standardPaths()/standardPathsFor() ordering, and
// the multipleLayoutsStatus() KConfig round trip. Every directory the
// statics read comes from XDG env vars pointed at throwaway temp dirs.
//
// Transplanted from latte-dock-qt6 (tests/importerlogictest.cpp at
// 81384003, github.com/CaptSilver/latte-dock-qt6) and adjusted: their uniqueLayoutName cases are NOT carried
// over (importerregressiontest already pins that helper deeper - six cases
// against their three), the data homes are split (XDG_DATA_HOME vs
// XDG_DATA_DIRS point at different dirs, so systemShellDataPath()'s
// last-entry pick is actually distinguishable from the local head), and
// fileVersion() gains the missing-LayoutSettings default row.

// local
#include "apptypes.h"
#include "layout/abstractlayout.h"
#include "layouts/importer.h"

// Qt
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

// KDE
#include <KConfig>
#include <KConfigGroup>
#include <KTar>

using Latte::Layouts::Importer;

class ImporterLogicTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void classifyLayoutFileVersions();
    void classifyMissingAndForeignFiles();
    void classifyConfigArchives_data();
    void classifyConfigArchives();
    void rejectNonTarArchive();

    void parseNameOfConfigFile_data();
    void parseNameOfConfigFile();

    void resolveLayoutUserPaths();
    void reportLayoutExistenceAndEnumerate();
    void resolveSystemShellPaths();
    void orderStandardPathsAndAppendSubPaths();
    void roundTripMultipleLayoutsStatus();

private:
    QString configPath() const { return m_configHome.path(); }
    QString latteDir() const { return m_configHome.path() + QStringLiteral("/latte"); }

    //! write a .layout.latte KConfig with the given LayoutSettings/version
    //! (INT_MIN = omit the LayoutSettings group entirely)
    QString writeLayoutFile(const QString &path, int version);

    //! build a .latterc tar archive shaped like what fileVersion() peeks at:
    //! lattedockrc[UniversalSettings]version, lattedock-appletsrc
    //! [LayoutSettings]version, and optionally a latte/ directory entry
    //! (-1 omits the respective member file)
    QString writeConfigArchive(const QString &path, int rcVersion, int appletsVersion, bool withLatteDir);

    QTemporaryDir m_configHome; //! XDG_CONFIG_HOME -> Latte::configPath()
    QTemporaryDir m_dataHome;   //! XDG_DATA_HOME   -> local data head
    QTemporaryDir m_dataSystem; //! XDG_DATA_DIRS   -> system data tail
};

QString ImporterLogicTest::writeLayoutFile(const QString &path, int version)
{
    KConfig c(path);
    if (version != INT_MIN) {
        c.group(QStringLiteral("LayoutSettings")).writeEntry(QStringLiteral("version"), version);
    } else {
        c.group(QStringLiteral("SomethingElse")).writeEntry(QStringLiteral("present"), true);
    }
    c.sync();
    return path;
}

QString ImporterLogicTest::writeConfigArchive(const QString &path, int rcVersion, int appletsVersion, bool withLatteDir)
{
    KTar archive(path, QStringLiteral("application/x-tar"));
    if (!archive.open(QIODevice::WriteOnly)) {
        return QString();
    }

    if (rcVersion >= 0) {
        const QByteArray rc = QStringLiteral("[UniversalSettings]\nversion=%1\n").arg(rcVersion).toUtf8();
        archive.writeFile(QStringLiteral("lattedockrc"), rc);
    }
    if (appletsVersion >= 0) {
        const QByteArray applets = QStringLiteral("[LayoutSettings]\nversion=%1\n").arg(appletsVersion).toUtf8();
        archive.writeFile(QStringLiteral("lattedock-appletsrc"), applets);
    }
    if (withLatteDir) {
        //! a member under latte/; copyTo() recreates the directory so the
        //! QDir(...).exists() probe in fileVersion() sees it
        archive.writeFile(QStringLiteral("latte/dummy"), QByteArray("x"));
    }

    archive.close();
    return path;
}

void ImporterLogicTest::initTestCase()
{
    QVERIFY(m_configHome.isValid());
    QVERIFY(m_dataHome.isValid());
    QVERIFY(m_dataSystem.isValid());

    //! configPath() reads ConfigLocation (XDG_CONFIG_HOME); the layouts dir
    //! lives under it. The data locations feed standardPaths() and
    //! systemShellDataPath(); pinning BOTH data vars keeps the host's real
    //! data dirs out entirely.
    qputenv("XDG_CONFIG_HOME", m_configHome.path().toLocal8Bit());
    qputenv("XDG_DATA_HOME", m_dataHome.path().toLocal8Bit());
    qputenv("XDG_DATA_DIRS", m_dataSystem.path().toLocal8Bit());

    QVERIFY(QDir(configPath()).mkpath(QStringLiteral("latte")));
    QVERIFY(Importer::layoutUserDir().startsWith(m_configHome.path()));
}

void ImporterLogicTest::classifyLayoutFileVersions()
{
    const QString v2 = writeLayoutFile(latteDir() + QStringLiteral("/v2.layout.latte"), 2);
    const QString v1 = writeLayoutFile(latteDir() + QStringLiteral("/v1.layout.latte"), 1);
    const QString bare = writeLayoutFile(latteDir() + QStringLiteral("/bare.layout.latte"), INT_MIN);

    QCOMPARE(Importer::fileVersion(v2), Importer::LayoutVersion2);
    //! a .layout.latte with version != 2 is not a recognized modern layout
    QCOMPARE(Importer::fileVersion(v1), Importer::UnknownFileType);
    //! a missing LayoutSettings group defaults to version 1 -> unknown
    QCOMPARE(Importer::fileVersion(bare), Importer::UnknownFileType);
}

void ImporterLogicTest::classifyMissingAndForeignFiles()
{
    QCOMPARE(Importer::fileVersion(latteDir() + QStringLiteral("/nope.layout.latte")),
             Importer::UnknownFileType);

    //! a file that exists but is neither .layout.latte nor .latterc
    const QString txt = configPath() + QStringLiteral("/plain.txt");
    QFile f(txt);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("hello");
    f.close();
    QCOMPARE(Importer::fileVersion(txt), Importer::UnknownFileType);

    //! a .latterc path that does not exist at all
    QCOMPARE(Importer::fileVersion(configPath() + QStringLiteral("/ghost.latterc")),
             Importer::UnknownFileType);
}

void ImporterLogicTest::classifyConfigArchives_data()
{
    QTest::addColumn<int>("rcVersion");
    QTest::addColumn<int>("appletsVersion");
    QTest::addColumn<bool>("withLatteDir");
    QTest::addColumn<int>("expected");

    //! version-1 rc + version-1 applets file: a complete old config
    QTest::newRow("v1 config") << 1 << 1 << false << (int)Importer::ConfigVersion1;
    //! version-2 rc + a latte/ layouts dir: a complete modern config
    QTest::newRow("v2 config") << 2 << -1 << true << (int)Importer::ConfigVersion2;
    //! version-2 rc but no latte/ dir: neither v1 nor a complete v2
    QTest::newRow("v2 rc without latte dir") << 2 << -1 << false << (int)Importer::UnknownFileType;
    //! v1 rc alone (no applets file): the v1 verdict needs both members
    QTest::newRow("v1 rc without applets") << 1 << -1 << false << (int)Importer::UnknownFileType;
    //! empty archive
    QTest::newRow("empty archive") << -1 << -1 << false << (int)Importer::UnknownFileType;
}

void ImporterLogicTest::classifyConfigArchives()
{
    QFETCH(int, rcVersion);
    QFETCH(int, appletsVersion);
    QFETCH(bool, withLatteDir);
    QFETCH(int, expected);

    static int counter = 0;
    const QString arc = writeConfigArchive(
        configPath() + QStringLiteral("/case%1.latterc").arg(++counter),
        rcVersion, appletsVersion, withLatteDir);
    QVERIFY(!arc.isEmpty());
    QCOMPARE((int)Importer::fileVersion(arc), expected);
}

void ImporterLogicTest::rejectNonTarArchive()
{
    const QString notar = configPath() + QStringLiteral("/garbage.latterc");
    QFile g(notar);
    QVERIFY(g.open(QIODevice::WriteOnly));
    g.write("this is not a tar archive");
    g.close();
    QCOMPARE(Importer::fileVersion(notar), Importer::UnknownFileType);
}

void ImporterLogicTest::parseNameOfConfigFile_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("expected");

    QTest::newRow("strips latterc") << QStringLiteral("/home/u/My Config.latterc") << QStringLiteral("My Config");
    QTest::newRow("bare latterc") << QStringLiteral("foo.latterc") << QStringLiteral("foo");
    QTest::newRow("non-latterc kept") << QStringLiteral("/p/lattedockrc") << QStringLiteral("lattedockrc");
    //! the not-found -1 handed to QString::remove wraps to the END on Qt 6
    //! and chopped the trailing character before the endsWith/chop fix
    QTest::newRow("no extension kept") << QStringLiteral("/p/Plasma") << QStringLiteral("Plasma");
    QTest::newRow("single char kept") << QStringLiteral("/p/A") << QStringLiteral("A");
}

void ImporterLogicTest::parseNameOfConfigFile()
{
    QFETCH(QString, path);
    QFETCH(QString, expected);
    QCOMPARE(Importer::nameOfConfigFile(path), expected);
}

void ImporterLogicTest::resolveLayoutUserPaths()
{
    QCOMPARE(Importer::layoutUserDir(), latteDir());
    QCOMPARE(Importer::layoutUserFilePath(QStringLiteral("My Layout")),
             latteDir() + QStringLiteral("/My Layout.layout.latte"));
}

void ImporterLogicTest::reportLayoutExistenceAndEnumerate()
{
    writeLayoutFile(Importer::layoutUserFilePath(QStringLiteral("Alpha")), 2);
    writeLayoutFile(Importer::layoutUserFilePath(QStringLiteral("Beta")), 2);

    QVERIFY(Importer::layoutExists(QStringLiteral("Alpha")));
    QVERIFY(Importer::layoutExists(QStringLiteral("Beta")));
    QVERIFY(!Importer::layoutExists(QStringLiteral("Gamma")));

    const QStringList avail = Importer::availableLayouts();
    QVERIFY(avail.contains(QStringLiteral("Alpha")));
    QVERIFY(avail.contains(QStringLiteral("Beta")));
    QVERIFY(!avail.contains(QStringLiteral("Gamma")));
}

void ImporterLogicTest::resolveSystemShellPaths()
{
    //! systemShellDataPath() picks the LAST standard data location - the
    //! system tail (our XDG_DATA_DIRS dir), never the local XDG_DATA_HOME
    //! head; the split temp dirs make the distinction observable
    const QString sysData = m_dataSystem.path() + QStringLiteral("/plasma/shells/org.kde.latte.shell");
    QCOMPARE(Importer::systemShellDataPath(), sysData);
    QCOMPARE(Importer::layoutTemplateSystemFilePath(QStringLiteral("Default")),
             sysData + QStringLiteral("/contents/templates/Default.layout.latte"));
}

void ImporterLogicTest::orderStandardPathsAndAppendSubPaths()
{
    const QStringList localFirst = Importer::standardPaths(true);
    const QStringList localLast = Importer::standardPaths(false);

    //! with split data dirs the local head must lead the localfirst order
    QVERIFY(localFirst.count() >= 2);
    QCOMPARE(localFirst.first(), m_dataHome.path());
    QCOMPARE(localFirst.last(), m_dataSystem.path());

    //! localfirst=false is the exact reverse
    QStringList reversed = localFirst;
    std::reverse(reversed.begin(), reversed.end());
    QCOMPARE(localLast, reversed);

    //! standardPathsFor appends the subPath to every entry, inserting a
    //! separator only when the subPath is not already slash-prefixed
    const QStringList withSub = Importer::standardPathsFor(QStringLiteral("latte"), true);
    QCOMPARE(withSub.count(), localFirst.count());
    for (int i = 0; i < withSub.count(); ++i) {
        QCOMPARE(withSub[i], localFirst[i] + QStringLiteral("/latte"));
    }

    const QStringList absSub = Importer::standardPathsFor(QStringLiteral("/abs"), true);
    for (int i = 0; i < absSub.count(); ++i) {
        QCOMPARE(absSub[i], localFirst[i] + QStringLiteral("/abs"));
    }
}

void ImporterLogicTest::roundTripMultipleLayoutsStatus()
{
    //! no hidden linked file present: Uninitialized, and a set is refused
    QCOMPARE(Importer::multipleLayoutsStatus(), Latte::MultipleLayouts::Uninitialized);
    Importer::setMultipleLayoutsStatus(Latte::MultipleLayouts::Running);
    QCOMPARE(Importer::multipleLayoutsStatus(), Latte::MultipleLayouts::Uninitialized);

    //! create the hidden linked file; now a status write round-trips
    const QString linked = Importer::layoutUserFilePath(
        QString::fromLatin1(Latte::Layout::MULTIPLELAYOUTSHIDDENNAME));
    writeLayoutFile(linked, 2);

    Importer::setMultipleLayoutsStatus(Latte::MultipleLayouts::Running);
    QCOMPARE(Importer::multipleLayoutsStatus(), Latte::MultipleLayouts::Running);

    Importer::setMultipleLayoutsStatus(Latte::MultipleLayouts::Paused);
    QCOMPARE(Importer::multipleLayoutsStatus(), Latte::MultipleLayouts::Paused);
}

QTEST_GUILESS_MAIN(ImporterLogicTest)

#include "importerlogictest.moc"
