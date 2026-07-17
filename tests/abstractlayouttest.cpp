/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Exercises the real Latte::Layout::AbstractLayout code through the
// lattedock-core object library:
//  - the [LayoutSettings] save/load round-trip (each setter persists via its
//    change signal; a fresh object must read the values back),
//  - the guarded-setter signal discipline that persistence rides on, and
//  - the static layoutName() path/extension parser, table-driven over the
//    degenerate shapes too.
//
// Transplanted from latte-dock-qt6 (tests/abstractlayouttest.cpp at
// 81384003, github.com/CaptSilver/latte-dock-qt6) and extended: their layoutName table stops at four rows; ours
// adds the empty/degenerate paths and the double-extension shapes, and pins
// the setter signal guard the round-trip case silently relies on. Their
// 443ac361 fixed the not-found-index chop in layoutName(); the same defect
// was live here and is fixed in the commit right before this test landed.

#include "layout/abstractlayout.h"

#include <KConfig>
#include <KConfigGroup>
#include <QObject>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

using namespace Latte::Layout;

class AbstractLayoutTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    QString seededLayoutPath(const QString &fileName);

private Q_SLOTS:
    void initTestCase();
    void layoutSettingsRoundTrip();
    void settersEmitOnceAndOnlyOnRealChange();
    void layoutName_data();
    void layoutName();
};

void AbstractLayoutTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
}

// AbstractLayout's ctor only loads an existing file, so tests must seed one.
QString AbstractLayoutTest::seededLayoutPath(const QString &fileName)
{
    const QString path = m_dir.filePath(fileName);

    KConfig seed(path);
    seed.group(QStringLiteral("LayoutSettings")).writeEntry(QStringLiteral("version"), 1);
    seed.sync();

    return path;
}

void AbstractLayoutTest::layoutSettingsRoundTrip()
{
    const QString path = seededLayoutPath(QStringLiteral("Test.layout.latte"));

    const QStringList launchers = {QStringLiteral("applications:firefox.desktop"),
                                   QStringLiteral("applications:org.kde.dolphin.desktop")};

    // Set values that all differ from the loaded defaults, so each setter fires
    // its change signal and persists through saveConfig.
    {
        AbstractLayout layout(nullptr, path, QStringLiteral("Test"));
        layout.setVersion(5);
        layout.setColor(QStringLiteral("purple"));
        layout.setIcon(QStringLiteral("starred-symbolic"));
        layout.setLaunchers(launchers);
        layout.setPopUpMargin(9);
        layout.setPreferredForShortcutsTouched(true);
        layout.syncSettings();
    }

    // A fresh object must read every field back from disk.
    AbstractLayout reloaded(nullptr, path, QStringLiteral("Test"));
    QCOMPARE(reloaded.version(), 5);
    QCOMPARE(reloaded.color(), QStringLiteral("purple"));
    QCOMPARE(reloaded.icon(), QStringLiteral("starred-symbolic"));
    QCOMPARE(reloaded.launchers(), launchers);
    QCOMPARE(reloaded.popUpMargin(), 9);
    QCOMPARE(reloaded.preferredForShortcutsTouched(), true);

    // The values really landed under [LayoutSettings] on disk.
    KConfig disk(path);
    const KConfigGroup ls = disk.group(QStringLiteral("LayoutSettings"));
    QCOMPARE(ls.readEntry(QStringLiteral("color"), QString()), QStringLiteral("purple"));
    QCOMPARE(ls.readEntry(QStringLiteral("launchers"), QStringList()), launchers);
}

void AbstractLayoutTest::settersEmitOnceAndOnlyOnRealChange()
{
    const QString path = seededLayoutPath(QStringLiteral("Signals.layout.latte"));

    AbstractLayout layout(nullptr, path, QStringLiteral("Signals"));

    // Persistence rides the change signals (init() wires them to saveConfig),
    // so the guard matters in both directions: a missing emit never saves the
    // new value, and a re-emit on an identical value re-saves the whole
    // [LayoutSettings] group for nothing on every touch.
    QSignalSpy colorSpy(&layout, &AbstractLayout::colorChanged);
    QSignalSpy versionSpy(&layout, &AbstractLayout::versionChanged);

    layout.setColor(QStringLiteral("purple"));
    QCOMPARE(colorSpy.count(), 1);
    layout.setColor(QStringLiteral("purple"));
    QCOMPARE(colorSpy.count(), 1);
    layout.setColor(QStringLiteral("green"));
    QCOMPARE(colorSpy.count(), 2);

    layout.setVersion(7);
    QCOMPARE(versionSpy.count(), 1);
    layout.setVersion(7);
    QCOMPARE(versionSpy.count(), 1);
}

void AbstractLayoutTest::layoutName_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("expected");

    QTest::newRow("layout file")       << QStringLiteral("/a/b/My Layout.layout.latte") << QStringLiteral("My Layout");
    QTest::newRow("bare layout file")  << QStringLiteral("Default.layout.latte")        << QStringLiteral("Default");
    // The one-char-short regression pin: a not-found lastIndexOf fed into
    // QString::remove() chopped the final character of non-layout names.
    QTest::newRow("non-layout kept")   << QStringLiteral("/p/notes.txt")                << QStringLiteral("notes.txt");
    QTest::newRow("no extension kept") << QStringLiteral("/p/Plasma")                   << QStringLiteral("Plasma");
    // Degenerate inputs must pass through as empty, never crash or invent.
    QTest::newRow("empty path")        << QString()                                     << QString();
    QTest::newRow("directory path")    << QStringLiteral("/a/b/")                       << QString();
    // The extension is stripped only as a SUFFIX, exactly once.
    QTest::newRow("extension inside")  << QStringLiteral("My.layout.latte.backup")      << QStringLiteral("My.layout.latte.backup");
    QTest::newRow("double extension")  << QStringLiteral("A.layout.latte.layout.latte") << QStringLiteral("A.layout.latte");
    QTest::newRow("bare extension")    << QStringLiteral(".layout.latte")               << QString();
}

void AbstractLayoutTest::layoutName()
{
    QFETCH(QString, path);
    QFETCH(QString, expected);
    QCOMPARE(AbstractLayout::layoutName(path), expected);
}

QTEST_GUILESS_MAIN(AbstractLayoutTest)

#include "abstractlayouttest.moc"
