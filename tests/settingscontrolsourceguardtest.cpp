/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Corona construction needs the full Plasma shell graph and a session bus, so
// this guard pins the generated-adaptor input and the deliberately thin Corona
// seam. Controlled mutations prove that missing transport or bypassed registry
// delegation makes the guard red rather than merely exercising a parser.

#include <QFile>
#include <QRegularExpression>
#include <QTest>

namespace
{

QString readFile(const QString &relativePath)
{
    QFile file(QStringLiteral("%1/%2").arg(QStringLiteral(REPO_ROOT), relativePath));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QString normalized(QString source)
{
    source.remove(QRegularExpression(QStringLiteral("<!--[\\s\\S]*?-->")));
    source.remove(QRegularExpression(QStringLiteral("/\\*[\\s\\S]*?\\*/")));
    source.remove(QRegularExpression(QStringLiteral("//[^\\n]*")));
    source.remove(QRegularExpression(QStringLiteral("\\s+")));
    return source;
}

QString functionBody(const QString &source, const QString &signature)
{
    const int signatureStart = source.indexOf(signature);
    const int braceStart = source.indexOf(QLatin1Char('{'), signatureStart + signature.size());
    if (signatureStart < 0 || braceStart < 0)
    {
        return {};
    }

    int depth = 0;
    for (int index = braceStart; index < source.size(); ++index)
    {
        if (source.at(index) == QLatin1Char('{'))
        {
            ++depth;
        }
        else if (source.at(index) == QLatin1Char('}') && --depth == 0)
        {
            return source.mid(braceStart, index - braceStart + 1);
        }
    }
    return {};
}

bool hasExactXmlSignature(const QString &xml)
{
    return normalized(xml).contains(QStringLiteral("<methodname=\"viewSettingsControlsData\">"
                                                   "<argname=\"data\"type=\"s\"direction=\"out\"/>"
                                                   "<argname=\"containmentId\"type=\"u\"direction=\"in\"/>"
                                                   "</method>"));
}

bool hasThinValidatedDelegation(const QString &source)
{
    const QString body = normalized(functionBody(source, QStringLiteral("QString Corona::viewSettingsControlsData")));
    return body == QStringLiteral("{autoview=m_layoutsManager->synchronizer()->"
                                  "viewForContainment(containmentId);"
                                  "if(!view){qWarning()<<\"corona:"
                                  "viewSettingsControlsDataqueriedforcontainment\""
                                  "<<containmentId<<\"whichhasnoview\";"
                                  "returnQStringLiteral(\"[]\");}"
                                  "returnm_settingsControlRegistry->"
                                  "viewSettingsControlsData(containmentId);}");
}

} // namespace

class SettingsControlSourceGuardTest : public QObject
{
    Q_OBJECT

  private Q_SLOTS:
    void xmlAndCoronaDelegationMatchPublicContract();
    void controlledMutationsAreRejected();
};

void SettingsControlSourceGuardTest::xmlAndCoronaDelegationMatchPublicContract()
{
    QVERIFY(hasExactXmlSignature(readFile(QStringLiteral("app/dbus/org.kde.LatteDock.xml"))));
    QVERIFY(hasThinValidatedDelegation(readFile(QStringLiteral("app/lattecorona.cpp"))));

    const QString header = normalized(readFile(QStringLiteral("app/lattecorona.h")));
    QVERIFY(header.contains(QStringLiteral("QStringviewSettingsControlsData(constuint&containmentId);")));
    QVERIFY(header.contains(QStringLiteral("SettingsControlRegistry*settingsControlRegistry()const;")));

    const QString cmake = normalized(readFile(QStringLiteral("app/CMakeLists.txt")));
    QVERIFY(cmake.contains(QStringLiteral("settingscontrolregistry.cpp")));
    QVERIFY(cmake.contains(QStringLiteral("qt_add_dbus_adaptor(lattedock-app_SRCS${latte_dbusXML}"
                                          "lattecorona.hLatte::Coronalattedockadaptor)")));
}

void SettingsControlSourceGuardTest::controlledMutationsAreRejected()
{
    const QString xml = readFile(QStringLiteral("app/dbus/org.kde.LatteDock.xml"));
    QVERIFY(hasExactXmlSignature(xml));
    QString mutation = xml;
    mutation.replace(QStringLiteral("viewSettingsControlsData"), QStringLiteral("settingsControlsData"));
    QVERIFY(!hasExactXmlSignature(mutation));
    mutation = xml;
    mutation.replace(QStringLiteral("<arg name=\"containmentId\" type=\"u\" direction=\"in\"/>"),
                     QStringLiteral("<arg name=\"containmentId\" type=\"s\" direction=\"in\"/>"));
    QVERIFY(!hasExactXmlSignature(mutation));

    const QString corona = readFile(QStringLiteral("app/lattecorona.cpp"));
    QVERIFY(hasThinValidatedDelegation(corona));
    mutation = corona;
    mutation.replace(QStringLiteral("return "
                                    "m_settingsControlRegistry->"
                                    "viewSettingsControlsData(containmentId);"),
                     QStringLiteral("return QStringLiteral(\"[]\");"));
    QVERIFY(!hasThinValidatedDelegation(mutation));
    mutation = corona;
    mutation.replace(QStringLiteral("if (!view)"), QStringLiteral("if (false)"));
    QVERIFY(!hasThinValidatedDelegation(mutation));
}

QTEST_GUILESS_MAIN(SettingsControlSourceGuardTest)

#include "settingscontrolsourceguardtest.moc"
