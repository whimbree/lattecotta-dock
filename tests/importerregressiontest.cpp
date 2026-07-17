/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Pins Importer::uniqueLayoutName()'s copy-suffix behavior across the
//! QRegExp -> QRegularExpression conversion. David Goree's latte-dock-qt6
//! (github.com/CaptSilver/latte-dock-qt6) found this exact
//! " - [0-9]+" match subtly behavior-sensitive during its port and added a
//! regression test; this is ours. The layouts directory the real statics read
//! comes from XDG_CONFIG_HOME, pointed at a throwaway temp dir so the host
//! configuration is never touched.

#include "layouts/importer.h"

// Qt
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using Latte::Layouts::Importer;

class ImporterRegressionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void keepsFreeName();
    void appendsFirstFreeSuffix();
    void stripsExistingCopySuffixAndProbes();
    void keepsNumericSuffixWhenNameIsFree();
    void treatsNonNumericSuffixAsPlainName();
    void stripsOnlyTheLastCopySuffix();

private:
    void makeLayout(const QString &name);

    QTemporaryDir m_configHome;
};

void ImporterRegressionTest::initTestCase()
{
    QVERIFY(m_configHome.isValid());
    qputenv("XDG_CONFIG_HOME", m_configHome.path().toUtf8());

    QVERIFY(QDir().mkpath(Importer::layoutUserDir()));

    //! the redirect must actually be in effect before any case writes files
    QVERIFY(Importer::layoutUserDir().startsWith(m_configHome.path()));
}

void ImporterRegressionTest::makeLayout(const QString &name)
{
    QFile file(Importer::layoutUserFilePath(name));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("[LayoutSettings]\n") > 0);
    file.close();

    QVERIFY(Importer::layoutExists(name));
}

void ImporterRegressionTest::keepsFreeName()
{
    QCOMPARE(Importer::uniqueLayoutName(QStringLiteral("Solo")), QStringLiteral("Solo"));
}

void ImporterRegressionTest::appendsFirstFreeSuffix()
{
    makeLayout(QStringLiteral("Alpha"));

    QCOMPARE(Importer::uniqueLayoutName(QStringLiteral("Alpha")), QStringLiteral("Alpha - 2"));
}

void ImporterRegressionTest::stripsExistingCopySuffixAndProbes()
{
    makeLayout(QStringLiteral("Beta"));
    makeLayout(QStringLiteral("Beta - 2"));

    //! an existing "Beta - 2" is recognized as a copy of "Beta": the suffix is
    //! stripped and probing continues after the taken slots
    QCOMPARE(Importer::uniqueLayoutName(QStringLiteral("Beta - 2")), QStringLiteral("Beta - 3"));
}

void ImporterRegressionTest::keepsNumericSuffixWhenNameIsFree()
{
    //! nothing named "Gamma - 7" exists, so the suffix is user-chosen naming,
    //! not a collision to resolve
    QCOMPARE(Importer::uniqueLayoutName(QStringLiteral("Gamma - 7")), QStringLiteral("Gamma - 7"));
}

void ImporterRegressionTest::treatsNonNumericSuffixAsPlainName()
{
    makeLayout(QStringLiteral("Delta - abc"));

    //! " - abc" must not match the numeric copy-suffix pattern; the whole
    //! string is the base name
    QCOMPARE(Importer::uniqueLayoutName(QStringLiteral("Delta - abc")), QStringLiteral("Delta - abc - 2"));
}

void ImporterRegressionTest::stripsOnlyTheLastCopySuffix()
{
    makeLayout(QStringLiteral("Epsilon - 2 - 3"));

    //! lastIndexOf semantics: only the final " - 3" is the copy suffix,
    //! "Epsilon - 2" is the base and it is free
    QCOMPARE(Importer::uniqueLayoutName(QStringLiteral("Epsilon - 2 - 3")), QStringLiteral("Epsilon - 2"));
}

QTEST_MAIN(ImporterRegressionTest)

#include "importerregressiontest.moc"
