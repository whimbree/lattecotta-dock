/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QTest>
class SettingsInventoryTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void validatesInventory();
};
namespace {
QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}
bool validCatalogLink(const QJsonValue &link, QSet<int> &linkedCatalog)
{
    if (link.isArray()) {
        const QJsonArray links = link.toArray();
        if (links.isEmpty()) {
            return false;
        }
        for (const QJsonValue &entry : links) {
            if (!validCatalogLink(entry, linkedCatalog)) {
                return false;
            }
        }
        return true;
    }
    if (link.isDouble()) {
        const int catalog = link.toInt();
        if (catalog < 1 || catalog > 121) {
            return false;
        }
        linkedCatalog.insert(catalog);
        return true;
    }
    return link.isString() && link.toString().startsWith(QStringLiteral("none:"))
            && link.toString().size() > 5;
}
bool sourceAnchorResolves(const QString &relativePath, int line)
{
    const QByteArray source = readFile(QStringLiteral(REPO_ROOT) + QLatin1Char('/') + relativePath);
    if (source.isEmpty()) {
        return false;
    }
    const QList<QByteArray> lines = source.split('\n');
    return line > 0 && line <= lines.size() && !lines.at(line - 1).trimmed().isEmpty();
}
}
void SettingsInventoryTest::validatesInventory()
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(readFile(QStringLiteral(SETTINGS_INVENTORY_PATH)), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());
    const QJsonObject root = document.object();
    QCOMPARE(root.value(QStringLiteral("schema")).toInt(), 1);
    QCOMPARE(root.value(QStringLiteral("statusOrder")).toArray().size(), 9);
    const QJsonObject defaults = root.value(QStringLiteral("defaults")).toObject();
    const QJsonObject oracle = defaults.value(QStringLiteral("runtimeOracle")).toObject();
    const QJsonObject accessibility = defaults.value(QStringLiteral("accessibility")).toObject();
    for (const QString &field : {QStringLiteral("fixture"), QStringLiteral("operation"),
                                 QStringLiteral("observable"), QStringLiteral("negative"),
                                 QStringLiteral("test")}) {
        QCOMPARE(oracle.value(field).toString(), QStringLiteral("TBD"));
    }
    for (const QString &field : {QStringLiteral("role"), QStringLiteral("name"),
                                 QStringLiteral("valueActions"), QStringLiteral("focusPrevious"),
                                 QStringLiteral("focusNext")}) {
        QVERIFY2(!accessibility.value(field).toString().isEmpty(), qPrintable(field));
    }
    QSet<QString> identities;
    QSet<int> linkedCatalog;
    QMap<QString, int> kinds;
    QSet<QString> validKinds;
    for (const QJsonValue &kind : root.value(QStringLiteral("kinds")).toArray()) {
        validKinds.insert(kind.toString());
    }
    const QRegularExpression identityPattern(QStringLiteral("^[a-z][a-z0-9.-]+$"));
    int recordCount = 0;
    for (const QJsonValue &groupValue : root.value(QStringLiteral("groups")).toArray()) {
        const QJsonObject group = groupValue.toObject();
        const QString path = group.value(QStringLiteral("path")).toString();
        QVERIFY(!path.isEmpty());
        QVERIFY(!group.value(QStringLiteral("multiplicity")).toString().isEmpty());
        for (const QJsonValue &rowValue : group.value(QStringLiteral("rows")).toArray()) {
            const QJsonArray row = rowValue.toArray();
            QCOMPARE(row.size(), 10);
            const QString identity = row.at(0).toString();
            QVERIFY(identityPattern.match(identity).hasMatch());
            QVERIFY2(!identities.contains(identity), qPrintable(identity));
            identities.insert(identity);
            QVERIFY2(sourceAnchorResolves(path, row.at(1).toInt()), qPrintable(path + QLatin1Char(':') + QString::number(row.at(1).toInt())));
            const QString kind = row.at(2).toString();
            QVERIFY2(validKinds.contains(kind), qPrintable(kind));
            ++kinds[kind];
            QVERIFY2(validCatalogLink(row.at(3), linkedCatalog), qPrintable(identity));
            for (int field = 4; field <= 8; ++field) {
                QVERIFY2(!row.at(field).toString().isEmpty(), qPrintable(identity));
            }
            const QString statuses = row.at(9).toString();
            QCOMPARE(statuses.size(), 9);
            QVERIFY2(QRegularExpression(QStringLiteral("^[ORGN]{9}$")).match(statuses).hasMatch(), qPrintable(identity));
            QVERIFY2(!statuses.contains(QLatin1Char('G')), "SC-F1 records no completion evidence");
            ++recordCount;
        }
    }
    for (int catalog = 1; catalog <= 121; ++catalog) {
        QVERIFY2(linkedCatalog.contains(catalog), qPrintable(QStringLiteral("catalog entry %1 is unlinked").arg(catalog)));
    }
    int exemptionCount = 0;
    for (const QJsonValue &value : root.value(QStringLiteral("exemptions")).toArray()) {
        const QJsonObject exemption = value.toObject();
        const QString identity = exemption.value(QStringLiteral("id")).toString();
        QVERIFY(identityPattern.match(identity).hasMatch());
        QVERIFY2(!identities.contains(identity), qPrintable(identity));
        identities.insert(identity);
        QVERIFY2(sourceAnchorResolves(exemption.value(QStringLiteral("path")).toString(),
                                      exemption.value(QStringLiteral("line")).toInt()), qPrintable(identity));
        QVERIFY2(!exemption.value(QStringLiteral("reason")).toString().isEmpty(), qPrintable(identity));
        ++exemptionCount;
    }
    QCOMPARE(recordCount, 270);
    QCOMPARE(exemptionCount, 21);
    qInfo() << "settings inventory:" << recordCount << "affordances," << exemptionCount << "exemptions";
    for (auto it = kinds.cbegin(); it != kinds.cend(); ++it) {
        qInfo().noquote() << QStringLiteral("  %1: %2").arg(it.key()).arg(it.value());
    }
}
QTEST_APPLESS_MAIN(SettingsInventoryTest)
#include "settingsinventorytest.moc"
