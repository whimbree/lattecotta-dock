/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Real-link behavioral test for the two free-standing window-system helpers
// (linked through lattedock-core):
//   app/wm/tasktools.cpp    - appDataFromUrl()/defaultApplication() URL
//                             parsing behind launcher/task identity
//   app/wm/schemecolors.cpp - the .colors color-scheme parser behind the
//                             dynamic window-scheme coloring
//
// Transplanted from latte-dock-qt6 (tests/wmtoolstest.cpp at origin/main)
// and raised: the skipTaskbar query parsing went data-driven, and
// possibleSchemeFile() gains the by-NAME resolution rows (exact name and
// the whitespace/dash-simplified lookup) through a scheme planted under a
// temp XDG_DATA_HOME - the fork only covered the absolute-path branch.
// XDG_DATA_HOME points at a temp dir (custom main); XDG_DATA_DIRS stays
// the host's so the KDesktopFile branch behaves like production. Their
// icon-non-null assertion is deliberately dropped: QIcon::fromTheme
// resolves against whatever icon themes the environment ships, which is
// not this test's contract.

// local
#include "wm/schemecolors.h"
#include "wm/tasktools.h"

// Qt
#include <QColor>
#include <QFile>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QUrl>

using namespace Latte::WindowSystem;

class WmToolsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    //! tasktools - defaultApplication
    void refuseNonPreferredSchemes();
    void refusePreferredUrlWithoutHost();

    //! tasktools - appDataFromUrl
    void keepUrlForPlainLocalFile();
    void parseSkipTaskbarQuery_data();
    void parseSkipTaskbarQuery();
    void fallBackToFileNameForUnresolvedUrl();
    void readUnregisteredDesktopFileDirectly();

    //! schemecolors
    void parseWmSelectionWindowAndButtonGroups();
    void readWindowGroupWhenPlasmaTheme();
    void resolveSchemeNameFromGeneralGroup();
    void reportInvalidColorsForMissingFile();
    void resolveSchemeFileFromAbsolutePathAndName();

private:
    QString writeColorsFile(const QString &name, const QString &body);

    QTemporaryDir m_dir;
};

QString WmToolsTest::writeColorsFile(const QString &name, const QString &body)
{
    const QString path = m_dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return QString();
    }
    QTextStream out(&f);
    out << body;
    return path;
}

void WmToolsTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
}

void WmToolsTest::refuseNonPreferredSchemes()
{
    // only preferred:// is handled; anything else short-circuits to empty
    QCOMPARE(defaultApplication(QUrl(QStringLiteral("applications:firefox.desktop"))), QString());
    QCOMPARE(defaultApplication(QUrl(QStringLiteral("file:///usr/share/applications/x.desktop"))), QString());
}

void WmToolsTest::refusePreferredUrlWithoutHost()
{
    QCOMPARE(defaultApplication(QUrl(QStringLiteral("preferred://"))), QString());

    // and through appDataFromUrl: no service resolves, so id stays empty
    const AppData data = appDataFromUrl(QUrl(QStringLiteral("preferred://")));
    QVERIFY(data.id.isEmpty());
}

void WmToolsTest::keepUrlForPlainLocalFile()
{
    const QUrl url(QStringLiteral("file:///tmp/does-not-exist"));
    const AppData data = appDataFromUrl(url);
    // for a plain non-desktop local file no resolver fires: url is untouched
    QCOMPARE(data.url, url);
    QVERIFY(!data.skipTaskbar);
}

void WmToolsTest::parseSkipTaskbarQuery_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<bool>("expected");

    QTest::newRow("true") << "file:///tmp/thing?skipTaskbar=true" << true;
    QTest::newRow("false") << "file:///tmp/thing?skipTaskbar=false" << false;
    QTest::newRow("absent defaults false") << "file:///tmp/thing?other=1" << false;
    // only the literal "true" enables it
    QTest::newRow("non-boolean value") << "file:///tmp/thing?skipTaskbar=1" << false;
}

void WmToolsTest::parseSkipTaskbarQuery()
{
    QFETCH(QString, url);
    QFETCH(bool, expected);
    QCOMPARE(appDataFromUrl(QUrl(url)).skipTaskbar, expected);
}

void WmToolsTest::fallBackToFileNameForUnresolvedUrl()
{
    // no service DB match, no readable desktop file: the URL file name
    const AppData data = appDataFromUrl(QUrl(QStringLiteral("file:///tmp/myfile.bin")));
    QCOMPARE(data.name, QStringLiteral("myfile.bin"));
}

void WmToolsTest::readUnregisteredDesktopFileDirectly()
{
    // a .desktop file that no KService knows falls through to the
    // KDesktopFile branch, which reads Name/GenericName directly and derives
    // the id from the file name with the .desktop suffix stripped; TryExec
    // must point at a real executable for the read to happen at all
    const QString shell = QStandardPaths::findExecutable(QStringLiteral("sh"));
    QVERIFY2(!shell.isEmpty(), "no sh executable available to use as TryExec");

    const QString body = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Latte WmTools Probe\n"
        "GenericName=Probe Generic\n"
        "Icon=utilities-terminal\n"
        "Exec=%1\n"
        "TryExec=%1\n").arg(shell);

    const QString path = m_dir.filePath(QStringLiteral("lattewmtoolsprobe.desktop"));
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream(&f) << body;
    }

    const AppData data = appDataFromUrl(QUrl::fromLocalFile(path));

    QCOMPARE(data.name, QStringLiteral("Latte WmTools Probe"));
    QCOMPARE(data.genericName, QStringLiteral("Probe Generic"));
    QCOMPARE(data.id, QStringLiteral("lattewmtoolsprobe"));
}

void WmToolsTest::parseWmSelectionWindowAndButtonGroups()
{
    const QString body = QStringLiteral(
        "[General]\n"
        "Name=WmToolsScheme\n"
        "\n"
        "[WM]\n"
        "activeBackground=10,20,30\n"
        "activeForeground=40,50,60\n"
        "inactiveBackground=70,80,90\n"
        "inactiveForeground=100,110,120\n"
        "\n"
        "[Colors:Selection]\n"
        "BackgroundNormal=200,0,0\n"
        "ForegroundNormal=0,200,0\n"
        "\n"
        "[Colors:Window]\n"
        "ForegroundPositive=1,2,3\n"
        "ForegroundNeutral=4,5,6\n"
        "ForegroundNegative=7,8,9\n"
        "\n"
        "[Colors:Button]\n"
        "ForegroundNormal=11,12,13\n"
        "BackgroundNormal=14,15,16\n"
        "DecorationHover=17,18,19\n"
        "DecorationFocus=20,21,22\n");

    const QString path = writeColorsFile(QStringLiteral("WmToolsScheme.colors"), body);
    QVERIFY(!path.isEmpty());

    SchemeColors scheme(nullptr, path, /*plasmaTheme*/ false);

    // the non-plasma path reads [WM] for the active/inactive pairs
    QCOMPARE(scheme.backgroundColor(), QColor(10, 20, 30));
    QCOMPARE(scheme.textColor(), QColor(40, 50, 60));
    QCOMPARE(scheme.inactiveBackgroundColor(), QColor(70, 80, 90));
    QCOMPARE(scheme.inactiveTextColor(), QColor(100, 110, 120));

    QCOMPARE(scheme.highlightColor(), QColor(200, 0, 0));
    QCOMPARE(scheme.highlightedTextColor(), QColor(0, 200, 0));

    QCOMPARE(scheme.positiveTextColor(), QColor(1, 2, 3));
    QCOMPARE(scheme.neutralTextColor(), QColor(4, 5, 6));
    QCOMPARE(scheme.negativeTextColor(), QColor(7, 8, 9));

    QCOMPARE(scheme.buttonTextColor(), QColor(11, 12, 13));
    QCOMPARE(scheme.buttonBackgroundColor(), QColor(14, 15, 16));
    QCOMPARE(scheme.buttonHoverColor(), QColor(17, 18, 19));
    QCOMPARE(scheme.buttonFocusColor(), QColor(20, 21, 22));

    QCOMPARE(scheme.schemeFile(), path);
    QCOMPARE(scheme.schemeName(), QStringLiteral("WmToolsScheme"));
}

void WmToolsTest::readWindowGroupWhenPlasmaTheme()
{
    // with plasmaTheme=true the active/inactive pairs come from
    // [Colors:Window], so the [WM] values must be ignored
    const QString body = QStringLiteral(
        "[General]\n"
        "Name=PlasmaProbe\n"
        "\n"
        "[WM]\n"
        "activeBackground=99,99,99\n"
        "\n"
        "[Colors:Window]\n"
        "BackgroundNormal=30,30,30\n"
        "ForegroundNormal=40,40,40\n"
        "BackgroundAlternate=50,50,50\n"
        "ForegroundInactive=60,60,60\n");

    const QString path = writeColorsFile(QStringLiteral("PlasmaProbe.colors"), body);
    QVERIFY(!path.isEmpty());

    SchemeColors scheme(nullptr, path, /*plasmaTheme*/ true);

    QCOMPARE(scheme.backgroundColor(), QColor(30, 30, 30));
    QCOMPARE(scheme.textColor(), QColor(40, 40, 40));
    QCOMPARE(scheme.inactiveBackgroundColor(), QColor(50, 50, 50));
    QCOMPARE(scheme.inactiveTextColor(), QColor(60, 60, 60));
}

void WmToolsTest::resolveSchemeNameFromGeneralGroup()
{
    const QString path = writeColorsFile(QStringLiteral("namedscheme.colors"),
                                         QStringLiteral("[General]\nName=Custom Display Name\n"));
    QVERIFY(!path.isEmpty());
    QCOMPARE(SchemeColors::schemeName(path), QStringLiteral("Custom Display Name"));

    // no [General]/Name: the file base name sans extension
    const QString path2 = writeColorsFile(QStringLiteral("noname.colors"),
                                          QStringLiteral("[WM]\nactiveBackground=1,1,1\n"));
    QVERIFY(!path2.isEmpty());
    QCOMPARE(SchemeColors::schemeName(path2), QStringLiteral("noname"));
}

void WmToolsTest::reportInvalidColorsForMissingFile()
{
    // a scheme that resolves to no file leaves schemeFile empty and the
    // colors default-constructed (invalid)
    SchemeColors scheme(nullptr, m_dir.filePath(QStringLiteral("absent.colors")), false);
    QVERIFY(scheme.schemeFile().isEmpty());
    QVERIFY(!scheme.backgroundColor().isValid());
    QVERIFY(!scheme.textColor().isValid());
}

void WmToolsTest::resolveSchemeFileFromAbsolutePathAndName()
{
    // an existing absolute .colors path is returned verbatim
    const QString path = writeColorsFile(QStringLiteral("Absolute.colors"),
                                         QStringLiteral("[General]\nName=Abs\n"));
    QVERIFY(!path.isEmpty());
    QCOMPARE(SchemeColors::possibleSchemeFile(path), path);

    // a non-existent absolute .colors path resolves to nothing
    QVERIFY(SchemeColors::possibleSchemeFile(m_dir.filePath(QStringLiteral("Nope.colors"))).isEmpty());

    // a scheme NAME resolves through the standard data locations
    // (color-schemes/<name>.colors under our temp XDG_DATA_HOME)
    const QString schemesDir = qEnvironmentVariable("XDG_DATA_HOME") + QStringLiteral("/color-schemes");
    QVERIFY(QDir().mkpath(schemesDir));
    const QString planted = schemesDir + QStringLiteral("/ResolveMe.colors");
    {
        QFile f(planted);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream(&f) << QStringLiteral("[General]\nName=Resolve Me\n");
    }

    QCOMPARE(SchemeColors::possibleSchemeFile(QStringLiteral("ResolveMe")), planted);

    // a display name with whitespace/dashes falls back to the simplified
    // file lookup ("Resolve Me" / "Resolve-Me" -> ResolveMe.colors)
    QCOMPARE(SchemeColors::possibleSchemeFile(QStringLiteral("Resolve Me")), planted);
    QCOMPARE(SchemeColors::possibleSchemeFile(QStringLiteral("Resolve-Me")), planted);
}

int main(int argc, char *argv[])
{
    // XDG_DATA_HOME moves to a temp dir before QGuiApplication so the
    // scheme-name resolution rows can plant color-schemes/ fixtures without
    // touching (or depending on) the real user data dir; XDG_DATA_DIRS
    // stays the host's, keeping KService/KDesktopFile behavior production-
    // shaped for the tasktools cases.
    static QTemporaryDir xdgDataHome;
    qputenv("XDG_DATA_HOME", xdgDataHome.path().toUtf8());

    QGuiApplication app(argc, argv);
    WmToolsTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "wmtoolstest.moc"
