/*
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Guards the indicator running-/active-state dot against the KF6 metadata-load
// regression. The little dot under a running app is QML from the default
// indicator package, but it is only ever instantiated if the C++ Factory first
// reads the package's metadata.json and registers it. On KF5 the bare
// KPluginMetaData(QString) ctor parsed a metadata file; on KF6 the
// single-string ctor resolves its argument as a loadable plugin *library*
// (QPluginLoader semantics), so it returns INVALID metadata for a plain .json
// file. metadataAreValid() then fails on every indicator, the default plugin
// is never registered, the View's Indicator never builds its QML component,
// the per-task IndicatorLevel Loader stays inactive, and no dot renders. The
// dedicated KF6 entry point for a JSON file is KPluginMetaData::fromJsonFile().
//
// This tree's live paths were fixed in 7a1e63ae3; a dead metadataAreValid
// overload still carrying the bare ctor was removed when this pin landed.
// Transplanted from latte-dock-qt6's indicatormetadatatest. Three checks:
//
//   * Behavioral: every shipped indicator metadata.json loads via
//     fromJsonFile() with the keys the Factory/Indicator chain needs.
//   * KF6 signature: the bare ctor does NOT parse these JSON files - if a
//     future KF6 makes it parse again, this fails and is the cue to re-read
//     factory.cpp's loading strategy.
//   * Structural: app/indicator/factory.cpp constructs indicator metadata via
//     fromJsonFile() and never via the bare-ctor-from-a-path form (linking
//     factory.cpp headlessly would drag in KNS/KArchive/Importer/KDirWatch,
//     so the fix is pinned at the source).

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QtTest>

#include <KPluginMetaData>

class IndicatorMetadataTest : public QObject
{
    Q_OBJECT

private:
    static QString indicatorsDir()
    {
        return QStringLiteral(REPO_ROOT "/indicators");
    }

    static QString readFile(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString();
        }
        return QString::fromUtf8(f.readAll());
    }

    //! the shipped packages, each a subdir of indicators/ holding metadata.json
    static QStringList shippedIndicatorDirs()
    {
        return {
            QStringLiteral("default"),
            QStringLiteral("org.kde.latte.plasma"),
            QStringLiteral("org.kde.latte.plasmatabstyle"),
        };
    }

private Q_SLOTS:
    //! Every shipped indicator's metadata.json must load through the KF6 JSON
    //! entry point with the keys Factory::metadataAreValid()/Indicator::load()
    //! require, and the referenced package files must exist.
    void shippedMetadataLoadsViaFromJsonFile()
    {
        for (const QString &dir : shippedIndicatorDirs()) {
            const QString pkgRoot = indicatorsDir() + QLatin1Char('/') + dir;
            const QString metaFile = pkgRoot + QStringLiteral("/metadata.json");
            QVERIFY2(QFileInfo::exists(metaFile), qPrintable(metaFile + QStringLiteral(" is missing")));

            const KPluginMetaData md = KPluginMetaData::fromJsonFile(metaFile);
            QVERIFY2(md.isValid(), qPrintable(metaFile + QStringLiteral(" did not load as valid metadata")));
            QCOMPARE(md.category(), QStringLiteral("Latte Indicator"));

            const QString mainScript = md.value(QStringLiteral("X-Latte-MainScript"));
            QVERIFY2(!mainScript.isEmpty(), qPrintable(dir + QStringLiteral(": X-Latte-MainScript is empty")));
            const QString mainScriptPath = pkgRoot + QStringLiteral("/package/") + mainScript;
            QVERIFY2(QFileInfo::exists(mainScriptPath), qPrintable(mainScriptPath + QStringLiteral(" does not exist")));

            //! ConfigXml is optional (plasmatabstyle ships none); when declared it must resolve
            const QString configXml = md.value(QStringLiteral("X-Latte-ConfigXml"));
            if (!configXml.isEmpty()) {
                const QString configXmlPath = pkgRoot + QStringLiteral("/package/") + configXml;
                QVERIFY2(QFileInfo::exists(configXmlPath), qPrintable(configXmlPath + QStringLiteral(" does not exist")));
            }
        }
    }

    //! The bug's signature: the bare KPluginMetaData(QString) ctor returns invalid
    //! metadata for these JSON files on KF6. If a future KF6 ever makes the bare
    //! ctor parse JSON again, this fails and is the cue to revisit factory.cpp.
    void bareConstructorIsInvalidOnKf6()
    {
        const QString metaFile = indicatorsDir() + QStringLiteral("/default/metadata.json");
        QVERIFY(QFileInfo::exists(metaFile));

        const KPluginMetaData bare(metaFile);
        QVERIFY2(!bare.isValid(),
                 "KPluginMetaData(QString) parsed the JSON file; the from-file ctor trap "
                 "this test guards may no longer apply on this KF6 - revisit factory.cpp.");
    }

    //! Factory must build indicator metadata from a JSON file via fromJsonFile(),
    //! never via the bare-ctor-from-a-path form that silently fails on KF6.
    void factoryUsesFromJsonFile()
    {
        const QString src = readFile(QStringLiteral(REPO_ROOT "/app/indicator/factory.cpp"));
        QVERIFY2(!src.isEmpty(), "could not read app/indicator/factory.cpp");

        QVERIFY2(src.contains(QStringLiteral("KPluginMetaData::fromJsonFile")),
                 "factory.cpp must load indicator metadata via KPluginMetaData::fromJsonFile()");

        //! Matches `KPluginMetaData(arg)` and `KPluginMetaData name(arg)` where arg
        //! is an identifier (a file/path variable). Does NOT match the empty
        //! `KPluginMetaData()`, the `KPluginMetaData var = ...` form, a `&ref`
        //! parameter, or the `KPluginMetaData::fromJsonFile(...)` call.
        const QRegularExpression bareFromFile(QStringLiteral("KPluginMetaData(\\s+\\w+)?\\s*\\(\\s*[A-Za-z_]"));
        const QRegularExpressionMatch m = bareFromFile.match(src);
        QVERIFY2(!m.hasMatch(),
                 qPrintable(QStringLiteral("factory.cpp constructs KPluginMetaData directly from a path "
                                           "(invalid on KF6); use fromJsonFile(): \"%1\"")
                                .arg(m.captured(0))));
    }
};

QTEST_GUILESS_MAIN(IndicatorMetadataTest)

#include "indicatormetadatatest.moc"
