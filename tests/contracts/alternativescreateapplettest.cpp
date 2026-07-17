/*
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Contract: Plasma 6's ContainmentItem::createApplet takes a QRectF geometry
// hint - Qt5's took a QPoint. AlternativesHelper::loadAlternative swaps an
// applet for an alternative via QMetaObject::invokeMethod(contItem,
// "createApplet", ...), and invokeMethod resolves by exact argument types: a
// QPoint Q_ARG fails to resolve, returns false, and the swap silently no-ops
// AFTER the old applet was already destroyed - the applet just vanishes.
// Caught in this tree (app/alternativeshelper.cpp passed Q_ARG(QPoint) with
// the invoke result unchecked); transplanted from latte-dock-qt6's
// alternativescreateapplettest, adapted:
//
//   * The qmltypes path is pinned by CMake from the imported Plasma::Plasma
//     library location (the fork derived it from Qt's own import path and
//     QSKIPped when absent - in this nix env that skip would fire forever
//     and the pin would silently never run).
//   * A structural check pins OUR caller: alternativeshelper.cpp must pass
//     Q_ARG(QRectF, must not pass Q_ARG(QPoint, and must check the invoke
//     result (linking alternativeshelper.cpp headlessly would drag in the
//     whole corona graph, so the caller is pinned at the source).

#include <QFile>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QtTest>

class AlternativesCreateAppletTest : public QObject
{
    Q_OBJECT

private:
    static QString readFile(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString();
        }
        return QString::fromUtf8(f.readAll());
    }

private Q_SLOTS:
    void createAppletTakesQRectFGeometry();
    void callerPassesRectfGeometry();
};

//! The pinned libplasma's plasmoid qmltypes must declare createApplet with a
//! QRectF geom parameter. If a pin bump changes the type again, this fails
//! before the alternatives dialog silently eats an applet.
void AlternativesCreateAppletTest::createAppletTakesQRectFGeometry()
{
    const QString typesFile = QStringLiteral(PLASMOID_QMLTYPES_PATH);
    QVERIFY2(QFile::exists(typesFile),
             qPrintable(QStringLiteral("pinned plasmoid qmltypes vanished: %1").arg(typesFile)));

    const QString content = readFile(typesFile);
    QVERIFY2(!content.isEmpty(), qPrintable(typesFile));

    const int idx = content.indexOf(QStringLiteral("name: \"createApplet\""));
    QVERIFY2(idx >= 0, "createApplet not found in plasmoidplugin.qmltypes");

    //! the geometry parameter of the createApplet method block
    const QRegularExpression geomRe(
        QStringLiteral("Parameter \\{ name: \"geom\"; type: \"([A-Za-z0-9_]+)\" \\}"));
    const QRegularExpressionMatch m = geomRe.match(content, idx);
    QVERIFY2(m.hasMatch(), "createApplet geometry parameter not found");
    QCOMPARE(m.captured(1), QStringLiteral("QRectF"));
}

//! Our caller must follow the contract: Q_ARG(QRectF, never Q_ARG(QPoint,
//! and the invoke result checked - an unchecked false return is exactly the
//! silent no-op this pin exists for.
void AlternativesCreateAppletTest::callerPassesRectfGeometry()
{
    const QString src = readFile(QStringLiteral(REPO_ROOT "/app/alternativeshelper.cpp"));
    QVERIFY2(!src.isEmpty(), "could not read app/alternativeshelper.cpp");

    QVERIFY2(src.contains(QStringLiteral("Q_ARG(QRectF")),
             "alternativeshelper.cpp must pass the createApplet geometry as Q_ARG(QRectF, ...)");
    QVERIFY2(!src.contains(QStringLiteral("Q_ARG(QPoint")),
             "alternativeshelper.cpp passes Q_ARG(QPoint to createApplet - on Plasma 6 that "
             "invoke fails to resolve and the alternatives swap silently destroys the applet");
    QVERIFY2(src.contains(QStringLiteral("invoked")),
             "alternativeshelper.cpp must check invokeMethod's return value; a false return "
             "is the silent-swallow this contract exists to keep loud");
}

QTEST_GUILESS_MAIN(AlternativesCreateAppletTest)

#include "alternativescreateapplettest.moc"
