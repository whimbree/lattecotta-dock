/*
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Real-link behavioral test for the details-dialog color-scheme model
// (app/settings/detailsdialog/schemesmodel.cpp, linked through
// lattedock-core). The model enumerates *.colors files from the
// GenericDataLocation color-schemes/ subdirs (Importer::standardPathsFor),
// wraps each in a SchemeColors, sorts case-insensitively by scheme name and
// synthesizes row 0 as the "System Colors" (kdeglobals) entry.
//
// Transplanted from latte-dock-qt6 (tests/schemesmodeltest.cpp at
// 81384003, github.com/CaptSilver/latte-dock-qt6). The XDG isolation matters doubly here: XDG_DATA_HOME/DIRS
// point at temp dirs so the host's real schemes cannot perturb rows, and
// XDG_CONFIG_HOME is empty so the synthetic row-0 lookup finds no real
// kdeglobals (in THIS tree possibleSchemeFile then walks the BreezeLight
// fallback, which also resolves to nothing inside the temp dirs - row 0
// stays the empty-file SchemeColors either way).

// local
#include "settings/detailsdialog/schemesmodel.h"
#include "wm/schemecolors.h"

// Qt
#include <QColor>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

using namespace Latte::Settings::Model;

class SchemesModelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void enumerateSchemesFromDataDirs();
    void reportNameAndIdRoles();
    void reportParsedColorRoles();
    void synthesizeSystemColorsAtRowZero();
    void lookUpRowsBySchemeId();
    void refuseInvalidIndexesAndRoles();

private:
    QString writeColorsFile(const QString &name, const QString &body);

    QTemporaryDir m_dir;
    QString m_schemesDir;
    QString m_aquaPath;
    QString m_zebraPath;
};

QString SchemesModelTest::writeColorsFile(const QString &name, const QString &body)
{
    const QString path = m_schemesDir + QStringLiteral("/") + name;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return QString();
    }
    QTextStream out(&f);
    out << body;
    return path;
}

void SchemesModelTest::initTestCase()
{
    QVERIFY(m_dir.isValid());

    // QStandardPaths re-reads the XDG env on every call (no caching outside
    // test mode), so setting it here reaches every model constructed later.
    // DATA_HOME empty + DATA_DIRS at our temp root: only our schemes enter;
    // CONFIG_HOME empty: the row-0 kdeglobals lookup resolves to nothing.
    const QString dataHome = m_dir.filePath(QStringLiteral("xdg-data-home"));
    const QString configHome = m_dir.filePath(QStringLiteral("xdg-config-home"));
    QVERIFY(QDir().mkpath(dataHome));
    QVERIFY(QDir().mkpath(configHome));
    qputenv("XDG_DATA_HOME", dataHome.toLocal8Bit());
    qputenv("XDG_DATA_DIRS", m_dir.path().toLocal8Bit());
    qputenv("XDG_CONFIG_HOME", configHome.toLocal8Bit());

    m_schemesDir = m_dir.filePath(QStringLiteral("color-schemes"));
    QVERIFY(QDir().mkpath(m_schemesDir));

    // names chosen so the case-insensitive sort is deterministic
    m_aquaPath = writeColorsFile(QStringLiteral("Aqua.colors"), QStringLiteral(
        "[General]\n"
        "Name=Aqua Scheme\n"
        "\n"
        "[WM]\n"
        "activeBackground=10,20,30\n"
        "activeForeground=40,50,60\n"));
    QVERIFY(!m_aquaPath.isEmpty());

    m_zebraPath = writeColorsFile(QStringLiteral("Zebra.colors"), QStringLiteral(
        "[General]\n"
        "Name=Zebra Scheme\n"
        "\n"
        "[WM]\n"
        "activeBackground=200,100,50\n"
        "activeForeground=1,2,3\n"));
    QVERIFY(!m_zebraPath.isEmpty());
}

void SchemesModelTest::enumerateSchemesFromDataDirs()
{
    Schemes model;

    // row 0 is the synthetic System Colors entry, then our two schemes
    QCOMPARE(model.rowCount(), 3);

    // a flat list: rowCount under a valid parent is 0
    QCOMPARE(model.rowCount(model.index(0, 0)), 0);
}

void SchemesModelTest::reportNameAndIdRoles()
{
    Schemes model;

    // sorted case-insensitively by name: System Colors (empty-name scheme)
    // first, then Aqua, then Zebra
    const QModelIndex aqua = model.index(1, 0);
    const QModelIndex zebra = model.index(2, 0);

    QCOMPARE(model.data(aqua, Schemes::NAMEROLE).toString(), QStringLiteral("Aqua Scheme"));
    QCOMPARE(model.data(zebra, Schemes::NAMEROLE).toString(), QStringLiteral("Zebra Scheme"));

    // DisplayRole mirrors NAMEROLE
    QCOMPARE(model.data(aqua, Qt::DisplayRole).toString(), QStringLiteral("Aqua Scheme"));

    // IDROLE is the absolute scheme file path for non-row-0 entries
    QCOMPARE(model.data(aqua, Schemes::IDROLE).toString(), m_aquaPath);
    QCOMPARE(model.data(zebra, Schemes::IDROLE).toString(), m_zebraPath);
}

void SchemesModelTest::reportParsedColorRoles()
{
    Schemes model;

    const QModelIndex aqua = model.index(1, 0);

    // straight from the parsed SchemeColors: [WM]/activeForeground is the
    // text color, [WM]/activeBackground the background
    QCOMPARE(model.data(aqua, Schemes::TEXTCOLORROLE).value<QColor>(), QColor(40, 50, 60));
    QCOMPARE(model.data(aqua, Schemes::BACKGROUNDCOLORROLE).value<QColor>(), QColor(10, 20, 30));
}

void SchemesModelTest::synthesizeSystemColorsAtRowZero()
{
    Schemes model;

    const QModelIndex sys = model.index(0, 0);

    // row 0 is special-cased to the localized System Colors label and the
    // kdeglobals id regardless of the scheme it wraps
    QCOMPARE(model.data(sys, Schemes::IDROLE).toString(), QStringLiteral("kdeglobals"));
    QVERIFY(!model.data(sys, Schemes::NAMEROLE).toString().isEmpty());
}

void SchemesModelTest::lookUpRowsBySchemeId()
{
    Schemes model;

    // the empty id and the default kdeglobals id both map to row 0
    QCOMPARE(model.row(QString()), 0);
    QCOMPARE(model.row(QStringLiteral("kdeglobals")), 0);

    // a real scheme file path resolves to its sorted row
    QCOMPARE(model.row(m_aquaPath), 1);
    QCOMPARE(model.row(m_zebraPath), 2);

    // an unknown id resolves to -1
    QCOMPARE(model.row(m_dir.filePath(QStringLiteral("color-schemes/Missing.colors"))), -1);
}

void SchemesModelTest::refuseInvalidIndexesAndRoles()
{
    Schemes model;

    // out-of-range row, invalid index and an unhandled role all yield the
    // invalid QVariant (never a crash or a stale value)
    QVERIFY(!model.data(model.index(99, 0), Schemes::NAMEROLE).isValid());
    QVERIFY(!model.data(QModelIndex(), Schemes::NAMEROLE).isValid());
    QVERIFY(!model.data(model.index(1, 0), Qt::ToolTipRole).isValid());
}

QTEST_GUILESS_MAIN(SchemesModelTest)

#include "schemesmodeltest.moc"
