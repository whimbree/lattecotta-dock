/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "settingssourcescanner.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QTest>

#include <cmath>
#include <optional>

class SettingsInventoryTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void validatesInventory();
    void validatesPrivateSharedComponentsStayVisualOnly();
    void rejectsMalformedInventory_data();
    void rejectsMalformedInventory();
    void rejectsCoverageRelationCountDrift();
    void rejectsNewSourceCandidate();
    void rejectsNewCreateNewItemCall();
    void rejectsChangedHiddenIndicatorLifecycle();
    void rejectsNewNoninteractiveLifecycle();
    void rejectsStaleCandidateSelector();
    void rejectsStaleRepresentativeSelector();
    void rejectsUncoveredLedgerIdentity();
    void rejectsMissingDeclaredSourceOwnership();
    void rejectsMissingDeclaredExemptionOwnership();
};

namespace
{

struct CoverageMapping
{
    QString selector;
    QStringList links;
    bool representative{false};
};

struct ParsedInventory
{
    QSet<QString> identities;
    QHash<QString, QString> declaredPaths;
    QHash<QString, QList<CoverageMapping>> mappings;
    QJsonObject measured;
    QMap<QString, int> kinds;
    int affordances{0};
    int exemptions{0};
    int representatives{0};
    int coverageRelations{0};
};

constexpr int ExpectedCoverageRelations = 1274;

struct ValidationResult
{
    QStringList errors;
    ParsedInventory inventory;
};

std::optional<QByteArray> readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::nullopt;
    }
    return file.readAll();
}

QJsonObject inventoryObject()
{
    const std::optional<QByteArray> contents = readFile(QStringLiteral(SETTINGS_INVENTORY_PATH));
    return contents ? QJsonDocument::fromJson(*contents).object() : QJsonObject{};
}

QStringList settingsSourcePaths(const QString &root)
{
    QSet<QString> sourcePaths;
    const QDir rootDirectory(root);
    const auto addRecursiveQmlRoot = [&](const QString &relativePath)
    {
        QDirIterator iterator(root + QLatin1Char('/') + relativePath, {QStringLiteral("*.qml")}, QDir::Files,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext())
        {
            sourcePaths.insert(rootDirectory.relativeFilePath(iterator.next()));
        }
    };

    addRecursiveQmlRoot(QStringLiteral("shell/package/contents/configuration"));
    addRecursiveQmlRoot(QStringLiteral("shell/package/contents/controls"));

    // Every exported shared component is scanned. The private subdirectory's
    // live TextFieldFocus, ButtonShadow, and RoundShadow uses are visual-only;
    // the inventory test proves that no other private type is live syntax. A
    // future private input path must become an explicit source root.
    const QDir sharedComponents(root + QStringLiteral("/declarativeimports/components"));
    for (const QString &component : sharedComponents.entryList({QStringLiteral("*.qml")}, QDir::Files))
    {
        sourcePaths.insert(QStringLiteral("declarativeimports/components/") + component);
    }

    const QDir indicators(root + QStringLiteral("/indicators"));
    for (const QString &indicator : indicators.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
    {
        addRecursiveQmlRoot(QStringLiteral("indicators/%1/package/config").arg(indicator));
    }

    const QStringList explicitPaths{
        QStringLiteral("app/view/settings/indicatoruimanager.cpp"),
        QStringLiteral("containment/package/contents/ui/applet/AppletItem.qml"),
        QStringLiteral("containment/package/contents/ui/editmode/ConfigOverlay.qml"),
        QStringLiteral("containment/package/contents/ui/layouts/EnvironmentActions.qml"),
        QStringLiteral("containmentactions/contextmenu/menu.cpp"),
        QStringLiteral("indicators/org.kde.latte.plasmatabstyle/package/ui/main.qml"),
        QStringLiteral("plasmoid/package/contents/ui/ContextMenu.qml"),
        QStringLiteral("plasmoid/package/contents/ui/previews/ToolTipInstance.qml"),
        QStringLiteral("plasmoid/package/contents/ui/previews/ToolTipWindowMouseArea.qml"),
        QStringLiteral("plasmoid/package/contents/ui/task/AudioStream.qml"),
        QStringLiteral("plasmoid/package/contents/ui/task/TaskMouseArea.qml"),
    };
    sourcePaths.unite(QSet<QString>(explicitPaths.cbegin(), explicitPaths.cend()));

    QStringList sorted(sourcePaths.cbegin(), sourcePaths.cend());
    sorted.sort();
    return sorted;
}

bool validIdentity(const QString &identity)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z][a-z0-9.-]+$"));
    return pattern.match(identity).hasMatch();
}

bool parseCatalogLink(const QJsonValue &value, QSet<int> &linkedCatalog, QStringList &errors, const QString &context)
{
    if (value.isArray())
    {
        const QJsonArray links = value.toArray();
        if (links.isEmpty())
        {
            errors.append(context + QStringLiteral(": catalog link array is empty"));
            return false;
        }
        bool valid = true;
        for (const QJsonValue &link : links)
        {
            valid = parseCatalogLink(link, linkedCatalog, errors, context) && valid;
        }
        return valid;
    }
    if (value.isDouble())
    {
        const double number = value.toDouble();
        if (std::floor(number) != number || number < 1 || number > 121)
        {
            errors.append(context + QStringLiteral(": catalog number is outside 1..121"));
            return false;
        }
        linkedCatalog.insert(static_cast<int>(number));
        return true;
    }
    if (value.isString() && value.toString().startsWith(QStringLiteral("none:")) && value.toString().size() > 5)
    {
        return true;
    }
    errors.append(context + QStringLiteral(": catalog link has invalid type or value"));
    return false;
}

void parseIdentity(ParsedInventory &parsed, QStringList &errors, const QString &identity, const QString &path,
                   const QString &context)
{
    if (!validIdentity(identity))
    {
        errors.append(context + QStringLiteral(": invalid identity"));
        return;
    }
    if (parsed.identities.contains(identity))
    {
        errors.append(QStringLiteral("duplicate identity %1").arg(identity));
        return;
    }
    parsed.identities.insert(identity);
    parsed.declaredPaths.insert(identity, path);
}

void parseMappings(const QJsonArray &values, const QString &path, const bool representative, ParsedInventory &parsed,
                   QSet<QString> &selectors, QStringList &errors)
{
    for (qsizetype index = 0; index < values.size(); ++index)
    {
        const QString context =
            QStringLiteral("%1:%2[%3]")
                .arg(path, representative ? QStringLiteral("representatives") : QStringLiteral("sites"))
                .arg(index);
        const QJsonValue value = values.at(index);
        if (!value.isArray())
        {
            errors.append(context + QStringLiteral(": mapping is not an array"));
            continue;
        }
        const QJsonArray mapping = value.toArray();
        if (mapping.size() != 2)
        {
            errors.append(context + QStringLiteral(": mapping must contain selector and links"));
            continue;
        }
        if (!mapping.at(0).isString() || mapping.at(0).toString().isEmpty())
        {
            errors.append(context + QStringLiteral(": selector is not a nonempty string"));
            continue;
        }
        const QString selector = mapping.at(0).toString();
        if (selectors.contains(selector))
        {
            errors.append(QStringLiteral("duplicate coverage selector %1:%2").arg(path, selector));
            continue;
        }
        selectors.insert(selector);
        if (!mapping.at(1).isArray())
        {
            errors.append(context + QStringLiteral(": links are not an array"));
            continue;
        }
        const QJsonArray linkValues = mapping.at(1).toArray();
        if (linkValues.isEmpty())
        {
            errors.append(context + QStringLiteral(": coverage links are empty"));
            continue;
        }
        QStringList links;
        QSet<QString> uniqueLinks;
        bool linksValid = true;
        for (const QJsonValue &linkValue : linkValues)
        {
            if (!linkValue.isString())
            {
                errors.append(context + QStringLiteral(": coverage link is not a string"));
                linksValid = false;
                continue;
            }
            const QString link = linkValue.toString();
            if (link.contains(QLatin1Char('*')))
            {
                errors.append(context + QStringLiteral(": wildcard coverage link is forbidden"));
                linksValid = false;
            }
            else if (!parsed.identities.contains(link))
            {
                errors.append(context + QStringLiteral(": unknown coverage identity %1").arg(link));
                linksValid = false;
            }
            else if (uniqueLinks.contains(link))
            {
                errors.append(context + QStringLiteral(": duplicate coverage link %1").arg(link));
                linksValid = false;
            }
            else
            {
                uniqueLinks.insert(link);
                links.append(link);
            }
        }
        if (linksValid)
        {
            parsed.mappings[path].append({selector, links, representative});
            parsed.coverageRelations += links.size();
            if (representative)
            {
                ++parsed.representatives;
            }
        }
    }
}

ValidationResult parseInventoryShape(const QJsonValue &rootValue)
{
    ValidationResult result;
    if (!rootValue.isObject())
    {
        result.errors.append(QStringLiteral("inventory root is not an object"));
        return result;
    }
    const QJsonObject root = rootValue.toObject();
    if (!root.value(QStringLiteral("schema")).isDouble() || root.value(QStringLiteral("schema")).toInt(-1) != 2)
    {
        result.errors.append(QStringLiteral("schema must be numeric version 2"));
    }
    if (!root.value(QStringLiteral("title")).isString() || root.value(QStringLiteral("title")).toString().isEmpty())
    {
        result.errors.append(QStringLiteral("title is not a nonempty string"));
    }
    const QJsonValue copyrightValue = root.value(QStringLiteral("copyright"));
    if (!copyrightValue.isArray() || copyrightValue.toArray().isEmpty())
    {
        result.errors.append(QStringLiteral("copyright is not a nonempty array"));
    }
    else
    {
        for (const QJsonValue &holder : copyrightValue.toArray())
        {
            if (!holder.isString() || holder.toString().isEmpty())
            {
                result.errors.append(QStringLiteral("copyright contains an empty or non-string holder"));
            }
        }
    }
    if (root.value(QStringLiteral("license")) != QJsonValue(QStringLiteral("GPL-2.0-or-later")))
    {
        result.errors.append(QStringLiteral("license must be GPL-2.0-or-later"));
    }

    const QJsonValue kindsValue = root.value(QStringLiteral("kinds"));
    QSet<QString> validKinds;
    if (!kindsValue.isArray())
    {
        result.errors.append(QStringLiteral("kinds is not an array"));
    }
    else
    {
        for (const QJsonValue &kindValue : kindsValue.toArray())
        {
            if (!kindValue.isString() || kindValue.toString().isEmpty() || validKinds.contains(kindValue.toString()))
            {
                result.errors.append(QStringLiteral("kinds contains a non-string, empty, or duplicate value"));
                continue;
            }
            validKinds.insert(kindValue.toString());
        }
    }

    const QJsonArray expectedRowFields{
        QStringLiteral("auditIdentity"),    QStringLiteral("kind"),
        QStringLiteral("catalog"),          QStringLiteral("reachabilityAndEnabled"),
        QStringLiteral("inputPaths"),       QStringLiteral("intendedWriteOrAction"),
        QStringLiteral("persistence"),      QStringLiteral("novelMatrixCells"),
        QStringLiteral("completionStatus"),
    };
    const QJsonValue rowFieldsValue = root.value(QStringLiteral("rowFields"));
    if (!rowFieldsValue.isArray() || rowFieldsValue.toArray() != expectedRowFields)
    {
        result.errors.append(QStringLiteral("rowFields does not match the schema-2 row contract"));
    }
    const QJsonValue exemptionFieldsValue = root.value(QStringLiteral("exemptionFields"));
    if (!exemptionFieldsValue.isArray() ||
        exemptionFieldsValue.toArray() !=
            QJsonArray({QStringLiteral("id"), QStringLiteral("path"), QStringLiteral("reason")}))
    {
        result.errors.append(QStringLiteral("exemptionFields must be [id,path,reason]"));
    }
    const QJsonArray expectedStatusOrder{
        QStringLiteral("C1 Reachable"),          QStringLiteral("C2 Operable"),
        QStringLiteral("C3 Right Write"),        QStringLiteral("C4 Runtime Effect"),
        QStringLiteral("C5 Reflects and Syncs"), QStringLiteral("C6 Persists"),
        QStringLiteral("C7 Complete Choices"),   QStringLiteral("C8 Clean Lifecycle"),
        QStringLiteral("C9 Accessible"),
    };
    const QJsonValue statusOrderValue = root.value(QStringLiteral("statusOrder"));
    if (!statusOrderValue.isArray() || statusOrderValue.toArray() != expectedStatusOrder)
    {
        result.errors.append(QStringLiteral("statusOrder does not match C1 through C9"));
    }
    const QJsonObject expectedStatusCodes{
        {QStringLiteral("O"), QStringLiteral("open")},
        {QStringLiteral("R"), QStringLiteral("red evidence")},
        {QStringLiteral("G"), QStringLiteral("green evidence")},
        {QStringLiteral("N"), QStringLiteral("not applicable with a per-row reason")},
    };
    if (!root.value(QStringLiteral("statusCodes")).isObject() ||
        root.value(QStringLiteral("statusCodes")).toObject() != expectedStatusCodes)
    {
        result.errors.append(QStringLiteral("statusCodes does not match O/R/G/N"));
    }

    const QJsonValue defaultsValue = root.value(QStringLiteral("defaults"));
    if (!defaultsValue.isObject())
    {
        result.errors.append(QStringLiteral("defaults is not an object"));
    }
    else
    {
        const QJsonObject defaults = defaultsValue.toObject();
        const QJsonValue oracleValue = defaults.value(QStringLiteral("runtimeOracle"));
        if (!oracleValue.isObject())
        {
            result.errors.append(QStringLiteral("defaults.runtimeOracle is not an object"));
        }
        else
        {
            const QJsonObject oracle = oracleValue.toObject();
            for (const QString &field :
                 {QStringLiteral("fixture"), QStringLiteral("operation"), QStringLiteral("observable"),
                  QStringLiteral("negative"), QStringLiteral("test")})
            {
                if (oracle.value(field) != QJsonValue(QStringLiteral("TBD")))
                {
                    result.errors.append(QStringLiteral("defaults.runtimeOracle.%1 must remain TBD").arg(field));
                }
            }
        }
        const QJsonValue accessibilityValue = defaults.value(QStringLiteral("accessibility"));
        if (!accessibilityValue.isObject())
        {
            result.errors.append(QStringLiteral("defaults.accessibility is not an object"));
        }
        else
        {
            const QJsonObject accessibility = accessibilityValue.toObject();
            for (const QString &field : {QStringLiteral("role"), QStringLiteral("name"), QStringLiteral("valueActions"),
                                         QStringLiteral("focusPrevious"), QStringLiteral("focusNext")})
            {
                if (!accessibility.value(field).isString() || accessibility.value(field).toString().isEmpty())
                {
                    result.errors.append(
                        QStringLiteral("defaults.accessibility.%1 is empty or not a string").arg(field));
                }
            }
        }
    }

    QSet<int> linkedCatalog;
    const QJsonValue groupsValue = root.value(QStringLiteral("groups"));
    if (!groupsValue.isArray())
    {
        result.errors.append(QStringLiteral("groups is not an array"));
    }
    else
    {
        const QJsonArray groups = groupsValue.toArray();
        for (qsizetype groupIndex = 0; groupIndex < groups.size(); ++groupIndex)
        {
            const QString context = QStringLiteral("groups[%1]").arg(groupIndex);
            if (!groups.at(groupIndex).isObject())
            {
                result.errors.append(context + QStringLiteral(": group is not an object"));
                continue;
            }
            const QJsonObject group = groups.at(groupIndex).toObject();
            if (!group.value(QStringLiteral("path")).isString() ||
                group.value(QStringLiteral("path")).toString().isEmpty())
            {
                result.errors.append(context + QStringLiteral(": path is not a nonempty string"));
                continue;
            }
            const QString path = group.value(QStringLiteral("path")).toString();
            if (!group.value(QStringLiteral("multiplicity")).isString() ||
                group.value(QStringLiteral("multiplicity")).toString().isEmpty())
            {
                result.errors.append(context + QStringLiteral(": multiplicity is not a nonempty string"));
            }
            if (!group.value(QStringLiteral("rows")).isArray())
            {
                result.errors.append(context + QStringLiteral(": rows is not an array"));
                continue;
            }
            const QJsonArray rows = group.value(QStringLiteral("rows")).toArray();
            for (qsizetype rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
            {
                const QString rowContext = QStringLiteral("%1:rows[%2]").arg(context).arg(rowIndex);
                if (!rows.at(rowIndex).isArray())
                {
                    result.errors.append(rowContext + QStringLiteral(": row is not an array"));
                    continue;
                }
                const QJsonArray row = rows.at(rowIndex).toArray();
                if (row.size() != 9)
                {
                    result.errors.append(rowContext + QStringLiteral(": row must contain nine fields"));
                    continue;
                }
                if (!row.at(0).isString())
                {
                    result.errors.append(rowContext + QStringLiteral(": identity is not a string"));
                }
                else
                {
                    parseIdentity(result.inventory, result.errors, row.at(0).toString(), path, rowContext);
                }
                if (!row.at(1).isString() || !validKinds.contains(row.at(1).toString()))
                {
                    result.errors.append(rowContext + QStringLiteral(": kind is unknown or not a string"));
                }
                else
                {
                    ++result.inventory.kinds[row.at(1).toString()];
                }
                parseCatalogLink(row.at(2), linkedCatalog, result.errors, rowContext);
                for (int field = 3; field <= 7; ++field)
                {
                    if (!row.at(field).isString() || row.at(field).toString().isEmpty())
                    {
                        result.errors.append(
                            rowContext + QStringLiteral(": descriptive field %1 is empty or not a string").arg(field));
                    }
                }
                if (!row.at(8).isString() || row.at(8).toString().size() != 9 ||
                    !QRegularExpression(QStringLiteral("^[ORGN]{9}$")).match(row.at(8).toString()).hasMatch() ||
                    row.at(8).toString().contains(QLatin1Char('G')))
                {
                    result.errors.append(rowContext +
                                         QStringLiteral(": completion status is malformed or claims green evidence"));
                }
                ++result.inventory.affordances;
            }
        }
    }
    for (int catalog = 1; catalog <= 121; ++catalog)
    {
        if (!linkedCatalog.contains(catalog))
        {
            result.errors.append(QStringLiteral("catalog entry %1 is unlinked").arg(catalog));
        }
    }

    const QJsonValue exemptionsValue = root.value(QStringLiteral("exemptions"));
    if (!exemptionsValue.isArray())
    {
        result.errors.append(QStringLiteral("exemptions is not an array"));
    }
    else
    {
        const QJsonArray exemptions = exemptionsValue.toArray();
        for (qsizetype index = 0; index < exemptions.size(); ++index)
        {
            const QString context = QStringLiteral("exemptions[%1]").arg(index);
            if (!exemptions.at(index).isObject())
            {
                result.errors.append(context + QStringLiteral(": exemption is not an object"));
                continue;
            }
            const QJsonObject exemption = exemptions.at(index).toObject();
            if (!exemption.value(QStringLiteral("id")).isString())
            {
                result.errors.append(context + QStringLiteral(": id is not a string"));
                continue;
            }
            if (!exemption.value(QStringLiteral("path")).isString() ||
                exemption.value(QStringLiteral("path")).toString().isEmpty())
            {
                result.errors.append(context + QStringLiteral(": path is not a nonempty string"));
                continue;
            }
            if (!exemption.value(QStringLiteral("reason")).isString() ||
                exemption.value(QStringLiteral("reason")).toString().isEmpty())
            {
                result.errors.append(context + QStringLiteral(": reason is not a nonempty string"));
            }
            parseIdentity(result.inventory, result.errors, exemption.value(QStringLiteral("id")).toString(),
                          exemption.value(QStringLiteral("path")).toString(), context);
            ++result.inventory.exemptions;
        }
    }

    const QJsonValue coverageValue = root.value(QStringLiteral("sourceCoverage"));
    if (!coverageValue.isObject())
    {
        result.errors.append(QStringLiteral("sourceCoverage is not an object"));
        return result;
    }
    const QJsonObject coverage = coverageValue.toObject();
    if (!coverage.value(QStringLiteral("siteFields")).isArray() ||
        coverage.value(QStringLiteral("siteFields")).toArray() !=
            QJsonArray({QStringLiteral("selector"), QStringLiteral("covers")}))
    {
        result.errors.append(QStringLiteral("siteFields must be [selector,covers]"));
    }
    const QJsonValue measuredValue = coverage.value(QStringLiteral("measured"));
    if (!measuredValue.isObject())
    {
        result.errors.append(QStringLiteral("sourceCoverage.measured is not an object"));
    }
    else
    {
        result.inventory.measured = measuredValue.toObject();
        const QStringList fields{QStringLiteral("files"),
                                 QStringLiteral("candidates"),
                                 QStringLiteral("qmlObjects"),
                                 QStringLiteral("qmlMembers"),
                                 QStringLiteral("qmlModelElements"),
                                 QStringLiteral("qmlCalls"),
                                 QStringLiteral("cppConstructions"),
                                 QStringLiteral("cppCalls")};
        for (const QString &field : fields)
        {
            const QJsonValue measurement = result.inventory.measured.value(field);
            if (!measurement.isDouble() || std::floor(measurement.toDouble()) != measurement.toDouble() ||
                measurement.toDouble() < 0)
            {
                result.errors.append(QStringLiteral("measured.%1 is not a nonnegative integer").arg(field));
            }
        }
    }
    const QJsonValue filesValue = coverage.value(QStringLiteral("files"));
    if (!filesValue.isArray())
    {
        result.errors.append(QStringLiteral("sourceCoverage.files is not an array"));
        return result;
    }
    const QJsonArray files = filesValue.toArray();
    QSet<QString> coveragePaths;
    for (qsizetype index = 0; index < files.size(); ++index)
    {
        const QString context = QStringLiteral("sourceCoverage.files[%1]").arg(index);
        if (!files.at(index).isObject())
        {
            result.errors.append(context + QStringLiteral(": file entry is not an object"));
            continue;
        }
        const QJsonObject file = files.at(index).toObject();
        if (!file.value(QStringLiteral("path")).isString() || file.value(QStringLiteral("path")).toString().isEmpty())
        {
            result.errors.append(context + QStringLiteral(": path is not a nonempty string"));
            continue;
        }
        const QString path = file.value(QStringLiteral("path")).toString();
        if (coveragePaths.contains(path))
        {
            result.errors.append(QStringLiteral("duplicate coverage file %1").arg(path));
            continue;
        }
        coveragePaths.insert(path);
        if (!file.value(QStringLiteral("sites")).isArray())
        {
            result.errors.append(context + QStringLiteral(": sites is not an array"));
            continue;
        }
        if (!file.value(QStringLiteral("representatives")).isArray())
        {
            result.errors.append(context + QStringLiteral(": representatives is not an array"));
            continue;
        }
        QSet<QString> selectors;
        parseMappings(file.value(QStringLiteral("sites")).toArray(), path, false, result.inventory, selectors,
                      result.errors);
        parseMappings(file.value(QStringLiteral("representatives")).toArray(), path, true, result.inventory, selectors,
                      result.errors);
        if (!result.inventory.mappings.contains(path))
        {
            result.inventory.mappings.insert(path, {});
        }
    }
    return result;
}

ValidationResult validateInventory(const QJsonValue &rootValue, const QHash<QString, QString> &sourceOverrides = {})
{
    using namespace Latte::SettingsSource;

    ValidationResult result = parseInventoryShape(rootValue);
    if (!result.errors.isEmpty())
    {
        return result;
    }

    const QString repositoryRoot = QStringLiteral(REPO_ROOT);
    const QStringList sourcePaths = settingsSourcePaths(repositoryRoot);
    const QSet<QString> sourcePathSet(sourcePaths.cbegin(), sourcePaths.cend());
    const QSet<QString> ledgerPathSet(result.inventory.mappings.keyBegin(), result.inventory.mappings.keyEnd());
    for (const QString &path : sourcePaths)
    {
        if (!ledgerPathSet.contains(path))
        {
            result.errors.append(QStringLiteral("source file is absent from the ledger: %1").arg(path));
        }
    }
    for (const QString &path : ledgerPathSet)
    {
        if (!sourcePathSet.contains(path))
        {
            result.errors.append(QStringLiteral("ledger file is outside the source universe: %1").arg(path));
        }
    }
    for (auto owner = result.inventory.declaredPaths.cbegin(); owner != result.inventory.declaredPaths.cend(); ++owner)
    {
        if (!sourcePathSet.contains(owner.value()))
        {
            result.errors.append(QStringLiteral("declared source path is outside the source universe %1:%2")
                                     .arg(owner.key(), owner.value()));
        }
    }
    if (!result.errors.isEmpty())
    {
        return result;
    }

    QMap<QString, int> categoryCounts{
        {QStringLiteral("qml-object"), 0}, {QStringLiteral("qml-member"), 0},       {QStringLiteral("qml-model"), 0},
        {QStringLiteral("qml-call"), 0},   {QStringLiteral("cpp-construction"), 0}, {QStringLiteral("cpp-call"), 0},
    };
    QHash<QString, ScanResult> scans;
    int candidateCount = 0;
    for (const QString &path : sourcePaths)
    {
        QString source;
        if (sourceOverrides.contains(path))
        {
            source = sourceOverrides.value(path);
        }
        else
        {
            const std::optional<QByteArray> contents = readFile(repositoryRoot + QLatin1Char('/') + path);
            if (contents)
            {
                source = QString::fromUtf8(*contents);
            }
        }
        if (source.isEmpty())
        {
            result.errors.append(QStringLiteral("source file is empty or unreadable: %1").arg(path));
            continue;
        }
        ScanResult scan = path.endsWith(QLatin1String(".cpp")) ? scanCpp(source) : scanQml(source);
        for (const QString &scannerError : scan.errors)
        {
            result.errors.append(QStringLiteral("%1: %2").arg(path, scannerError));
        }
        for (const SourceSite &candidate : scan.candidates)
        {
            ++categoryCounts[candidate.category];
            ++candidateCount;
        }
        scans.insert(path, std::move(scan));
    }
    if (!result.errors.isEmpty())
    {
        return result;
    }

    QHash<QString, int> globalCoverage;
    QHash<QString, int> declaredCoverage;
    for (const QString &path : sourcePaths)
    {
        const ScanResult &scan = scans.value(path);
        QHash<QString, int> actualCandidateCounts;
        for (const SourceSite &candidate : scan.candidates)
        {
            ++actualCandidateCounts[candidate.selector];
        }
        QHash<QString, CoverageMapping> sites;
        QList<CoverageMapping> representatives;
        for (const CoverageMapping &mapping : result.inventory.mappings.value(path))
        {
            if (mapping.representative)
            {
                representatives.append(mapping);
            }
            else
            {
                sites.insert(mapping.selector, mapping);
            }
            for (const QString &identity : mapping.links)
            {
                ++globalCoverage[identity];
                if (result.inventory.declaredPaths.value(identity) == path)
                {
                    ++declaredCoverage[identity];
                }
            }
        }
        for (const SourceSite &candidate : scan.candidates)
        {
            if (!sites.contains(candidate.selector))
            {
                result.errors.append(QStringLiteral("unmapped source candidate %1:%2:%3")
                                         .arg(path)
                                         .arg(candidate.line)
                                         .arg(candidate.selector));
            }
        }
        for (auto site = sites.cbegin(); site != sites.cend(); ++site)
        {
            if (actualCandidateCounts.value(site.key()) != 1)
            {
                result.errors.append(
                    QStringLiteral("ledger candidate does not resolve exactly once %1:%2").arg(path, site.key()));
            }
        }
        for (const CoverageMapping &representative : representatives)
        {
            if (scan.candidateSelectors().contains(representative.selector))
            {
                result.errors.append(QStringLiteral("candidate cannot stand in as a representative %1:%2")
                                         .arg(path, representative.selector));
            }
            if (scan.resolveCount(representative.selector) != 1)
            {
                result.errors.append(QStringLiteral("representative does not resolve exactly once %1:%2")
                                         .arg(path, representative.selector));
            }
        }
    }

    const auto checkMeasurement = [&](const QString &field, const int actual)
    {
        const int recorded = result.inventory.measured.value(field).toInt(-1);
        if (recorded != actual)
        {
            result.errors.append(
                QStringLiteral("measured %1 is stale: ledger %2, source %3").arg(field).arg(recorded).arg(actual));
        }
    };
    checkMeasurement(QStringLiteral("files"), sourcePaths.size());
    checkMeasurement(QStringLiteral("candidates"), candidateCount);
    checkMeasurement(QStringLiteral("qmlObjects"), categoryCounts.value(QStringLiteral("qml-object")));
    checkMeasurement(QStringLiteral("qmlMembers"), categoryCounts.value(QStringLiteral("qml-member")));
    checkMeasurement(QStringLiteral("qmlModelElements"), categoryCounts.value(QStringLiteral("qml-model")));
    checkMeasurement(QStringLiteral("qmlCalls"), categoryCounts.value(QStringLiteral("qml-call")));
    checkMeasurement(QStringLiteral("cppConstructions"), categoryCounts.value(QStringLiteral("cpp-construction")));
    checkMeasurement(QStringLiteral("cppCalls"), categoryCounts.value(QStringLiteral("cpp-call")));

    QStringList identities(result.inventory.identities.cbegin(), result.inventory.identities.cend());
    identities.sort();
    for (const QString &identity : identities)
    {
        if (globalCoverage.value(identity) == 0)
        {
            result.errors.append(QStringLiteral("uncovered ledger identity %1").arg(identity));
        }
        if (declaredCoverage.value(identity) == 0)
        {
            result.errors.append(QStringLiteral("identity lacks declared-source mapping %1:%2")
                                     .arg(identity, result.inventory.declaredPaths.value(identity)));
        }
    }
    if (result.inventory.coverageRelations != ExpectedCoverageRelations)
    {
        result.errors.append(QStringLiteral("coverage relation count is stale: expected %1, ledger has %2")
                                 .arg(ExpectedCoverageRelations)
                                 .arg(result.inventory.coverageRelations));
    }
    return result;
}

QJsonObject mutateFirstMappingSelector(QJsonObject root, const QString &field)
{
    QJsonObject coverage = root.value(QStringLiteral("sourceCoverage")).toObject();
    QJsonArray files = coverage.value(QStringLiteral("files")).toArray();
    for (qsizetype fileIndex = 0; fileIndex < files.size(); ++fileIndex)
    {
        QJsonObject file = files.at(fileIndex).toObject();
        QJsonArray mappings = file.value(field).toArray();
        if (mappings.isEmpty())
        {
            continue;
        }
        QJsonArray mapping = mappings.at(0).toArray();
        mapping.replace(0, QString(mapping.at(0).toString() + QStringLiteral("-stale")));
        mappings.replace(0, mapping);
        file.insert(field, mappings);
        files.replace(fileIndex, file);
        break;
    }
    coverage.insert(QStringLiteral("files"), files);
    root.insert(QStringLiteral("sourceCoverage"), coverage);
    return root;
}

QJsonObject removeCoverageIdentity(QJsonObject root, const QString &removedIdentity)
{
    QJsonObject coverage = root.value(QStringLiteral("sourceCoverage")).toObject();
    QJsonArray files = coverage.value(QStringLiteral("files")).toArray();
    for (qsizetype fileIndex = 0; fileIndex < files.size(); ++fileIndex)
    {
        QJsonObject file = files.at(fileIndex).toObject();
        for (const QString &field : {QStringLiteral("sites"), QStringLiteral("representatives")})
        {
            QJsonArray mappings = file.value(field).toArray();
            for (qsizetype mappingIndex = mappings.size() - 1; mappingIndex >= 0; --mappingIndex)
            {
                QJsonArray mapping = mappings.at(mappingIndex).toArray();
                QJsonArray retained;
                for (const QJsonValue &link : mapping.at(1).toArray())
                {
                    if (link.toString() != removedIdentity)
                    {
                        retained.append(link);
                    }
                }
                if (retained.isEmpty())
                {
                    mappings.removeAt(mappingIndex);
                }
                else
                {
                    mapping.replace(1, retained);
                    mappings.replace(mappingIndex, mapping);
                }
            }
            file.insert(field, mappings);
        }
        files.replace(fileIndex, file);
    }
    coverage.insert(QStringLiteral("files"), files);
    root.insert(QStringLiteral("sourceCoverage"), coverage);
    return root;
}

QJsonObject replaceDeclaredLinks(QJsonObject root, const QString &path, const QString &identity,
                                 const QString &replacement)
{
    QJsonObject coverage = root.value(QStringLiteral("sourceCoverage")).toObject();
    QJsonArray files = coverage.value(QStringLiteral("files")).toArray();
    for (qsizetype fileIndex = 0; fileIndex < files.size(); ++fileIndex)
    {
        QJsonObject file = files.at(fileIndex).toObject();
        if (file.value(QStringLiteral("path")).toString() != path)
        {
            continue;
        }
        for (const QString &field : {QStringLiteral("sites"), QStringLiteral("representatives")})
        {
            QJsonArray mappings = file.value(field).toArray();
            for (qsizetype mappingIndex = 0; mappingIndex < mappings.size(); ++mappingIndex)
            {
                QJsonArray mapping = mappings.at(mappingIndex).toArray();
                QJsonArray links = mapping.at(1).toArray();
                for (qsizetype linkIndex = 0; linkIndex < links.size(); ++linkIndex)
                {
                    if (links.at(linkIndex).toString() == identity)
                    {
                        links.replace(linkIndex, replacement);
                    }
                }
                mapping.replace(1, links);
                mappings.replace(mappingIndex, mapping);
            }
            file.insert(field, mappings);
        }
        files.replace(fileIndex, file);
    }
    coverage.insert(QStringLiteral("files"), files);
    root.insert(QStringLiteral("sourceCoverage"), coverage);
    return root;
}

QJsonObject replaceFirstGroupField(QJsonObject root, const QString &field, const QJsonValue &replacement)
{
    QJsonArray groups = root.value(QStringLiteral("groups")).toArray();
    QJsonObject group = groups.at(0).toObject();
    group.insert(field, replacement);
    groups.replace(0, group);
    root.insert(QStringLiteral("groups"), groups);
    return root;
}

QJsonObject replaceFirstRowField(QJsonObject root, const int field, const QJsonValue &replacement)
{
    QJsonArray groups = root.value(QStringLiteral("groups")).toArray();
    QJsonObject group = groups.at(0).toObject();
    QJsonArray rows = group.value(QStringLiteral("rows")).toArray();
    QJsonArray row = rows.at(0).toArray();
    row.replace(field, replacement);
    rows.replace(0, row);
    group.insert(QStringLiteral("rows"), rows);
    groups.replace(0, group);
    root.insert(QStringLiteral("groups"), groups);
    return root;
}

QJsonObject replaceFirstExemptionField(QJsonObject root, const QString &field, const QJsonValue &replacement)
{
    QJsonArray exemptions = root.value(QStringLiteral("exemptions")).toArray();
    QJsonObject exemption = exemptions.at(0).toObject();
    exemption.insert(field, replacement);
    exemptions.replace(0, exemption);
    root.insert(QStringLiteral("exemptions"), exemptions);
    return root;
}

QJsonObject replaceCoverageField(QJsonObject root, const QString &field, const QJsonValue &replacement)
{
    QJsonObject coverage = root.value(QStringLiteral("sourceCoverage")).toObject();
    coverage.insert(field, replacement);
    root.insert(QStringLiteral("sourceCoverage"), coverage);
    return root;
}

QJsonObject replaceMeasurement(QJsonObject root, const QString &field, const QJsonValue &replacement)
{
    QJsonObject coverage = root.value(QStringLiteral("sourceCoverage")).toObject();
    QJsonObject measured = coverage.value(QStringLiteral("measured")).toObject();
    measured.insert(field, replacement);
    coverage.insert(QStringLiteral("measured"), measured);
    root.insert(QStringLiteral("sourceCoverage"), coverage);
    return root;
}

QJsonObject replaceFirstCoverageFileField(QJsonObject root, const QString &field, const QJsonValue &replacement)
{
    QJsonObject coverage = root.value(QStringLiteral("sourceCoverage")).toObject();
    QJsonArray files = coverage.value(QStringLiteral("files")).toArray();
    QJsonObject file = files.at(0).toObject();
    file.insert(field, replacement);
    files.replace(0, file);
    coverage.insert(QStringLiteral("files"), files);
    root.insert(QStringLiteral("sourceCoverage"), coverage);
    return root;
}

QJsonObject replaceFirstMapping(QJsonObject root, const QString &field, const QJsonValue &replacement)
{
    QJsonObject coverage = root.value(QStringLiteral("sourceCoverage")).toObject();
    QJsonArray files = coverage.value(QStringLiteral("files")).toArray();
    for (qsizetype fileIndex = 0; fileIndex < files.size(); ++fileIndex)
    {
        QJsonObject file = files.at(fileIndex).toObject();
        QJsonArray mappings = file.value(field).toArray();
        if (mappings.isEmpty())
        {
            continue;
        }
        mappings.replace(0, replacement);
        file.insert(field, mappings);
        files.replace(fileIndex, file);
        break;
    }
    coverage.insert(QStringLiteral("files"), files);
    root.insert(QStringLiteral("sourceCoverage"), coverage);
    return root;
}

QJsonObject replaceFirstMappingField(QJsonObject root, const QString &field, const int index,
                                     const QJsonValue &replacement)
{
    QJsonObject coverage = root.value(QStringLiteral("sourceCoverage")).toObject();
    QJsonArray files = coverage.value(QStringLiteral("files")).toArray();
    for (qsizetype fileIndex = 0; fileIndex < files.size(); ++fileIndex)
    {
        QJsonObject file = files.at(fileIndex).toObject();
        QJsonArray mappings = file.value(field).toArray();
        if (mappings.isEmpty())
        {
            continue;
        }
        QJsonArray mapping = mappings.at(0).toArray();
        mapping.replace(index, replacement);
        mappings.replace(0, mapping);
        file.insert(field, mappings);
        files.replace(fileIndex, file);
        break;
    }
    coverage.insert(QStringLiteral("files"), files);
    root.insert(QStringLiteral("sourceCoverage"), coverage);
    return root;
}

QJsonObject duplicateFirstCoverageLink(QJsonObject root)
{
    QJsonObject coverage = root.value(QStringLiteral("sourceCoverage")).toObject();
    QJsonArray files = coverage.value(QStringLiteral("files")).toArray();
    for (qsizetype fileIndex = 0; fileIndex < files.size(); ++fileIndex)
    {
        QJsonObject file = files.at(fileIndex).toObject();
        QJsonArray mappings = file.value(QStringLiteral("sites")).toArray();
        if (mappings.isEmpty())
        {
            continue;
        }
        QJsonArray mapping = mappings.at(0).toArray();
        QJsonArray links = mapping.at(1).toArray();
        links.append(links.at(0));
        mapping.replace(1, links);
        mappings.replace(0, mapping);
        file.insert(QStringLiteral("sites"), mappings);
        files.replace(fileIndex, file);
        break;
    }
    coverage.insert(QStringLiteral("files"), files);
    root.insert(QStringLiteral("sourceCoverage"), coverage);
    return root;
}

QJsonObject duplicateFirstCoverageSelector(QJsonObject root)
{
    QJsonObject coverage = root.value(QStringLiteral("sourceCoverage")).toObject();
    QJsonArray files = coverage.value(QStringLiteral("files")).toArray();
    for (qsizetype fileIndex = 0; fileIndex < files.size(); ++fileIndex)
    {
        QJsonObject file = files.at(fileIndex).toObject();
        QJsonArray mappings = file.value(QStringLiteral("sites")).toArray();
        if (mappings.isEmpty())
        {
            continue;
        }
        mappings.append(mappings.at(0));
        file.insert(QStringLiteral("sites"), mappings);
        files.replace(fileIndex, file);
        break;
    }
    coverage.insert(QStringLiteral("files"), files);
    root.insert(QStringLiteral("sourceCoverage"), coverage);
    return root;
}

QJsonObject moveFirstCandidateToRepresentatives(QJsonObject root)
{
    QJsonObject coverage = root.value(QStringLiteral("sourceCoverage")).toObject();
    QJsonArray files = coverage.value(QStringLiteral("files")).toArray();
    for (qsizetype fileIndex = 0; fileIndex < files.size(); ++fileIndex)
    {
        QJsonObject file = files.at(fileIndex).toObject();
        QJsonArray sites = file.value(QStringLiteral("sites")).toArray();
        if (sites.isEmpty())
        {
            continue;
        }
        QJsonArray representatives = file.value(QStringLiteral("representatives")).toArray();
        representatives.append(sites.takeAt(0));
        file.insert(QStringLiteral("sites"), sites);
        file.insert(QStringLiteral("representatives"), representatives);
        files.replace(fileIndex, file);
        break;
    }
    coverage.insert(QStringLiteral("files"), files);
    root.insert(QStringLiteral("sourceCoverage"), coverage);
    return root;
}

QJsonObject addCoverageRelation(QJsonObject root, const QString &identity)
{
    QJsonObject coverage = root.value(QStringLiteral("sourceCoverage")).toObject();
    QJsonArray files = coverage.value(QStringLiteral("files")).toArray();
    for (qsizetype fileIndex = 0; fileIndex < files.size(); ++fileIndex)
    {
        QJsonObject file = files.at(fileIndex).toObject();
        for (const QString &field : {QStringLiteral("sites"), QStringLiteral("representatives")})
        {
            QJsonArray mappings = file.value(field).toArray();
            for (qsizetype mappingIndex = 0; mappingIndex < mappings.size(); ++mappingIndex)
            {
                QJsonArray mapping = mappings.at(mappingIndex).toArray();
                QJsonArray links = mapping.at(1).toArray();
                if (links.contains(identity))
                {
                    continue;
                }
                links.append(identity);
                mapping.replace(1, links);
                mappings.replace(mappingIndex, mapping);
                file.insert(field, mappings);
                files.replace(fileIndex, file);
                coverage.insert(QStringLiteral("files"), files);
                root.insert(QStringLiteral("sourceCoverage"), coverage);
                return root;
            }
        }
    }
    return root;
}

} // namespace

void SettingsInventoryTest::validatesInventory()
{
    const ValidationResult result = validateInventory(inventoryObject());
    QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join(QLatin1Char('\n'))));
    QCOMPARE(result.inventory.affordances, 278);
    QCOMPARE(result.inventory.exemptions, 25);
    QCOMPARE(result.inventory.representatives, 16);
    QCOMPARE(result.inventory.coverageRelations, ExpectedCoverageRelations);
    QCOMPARE(result.inventory.measured.value(QStringLiteral("files")).toInt(), 59);
    QCOMPARE(result.inventory.measured.value(QStringLiteral("candidates")).toInt(), 742);
    QCOMPARE(result.inventory.measured.value(QStringLiteral("qmlObjects")).toInt(), 241);
    QCOMPARE(result.inventory.measured.value(QStringLiteral("qmlMembers")).toInt(), 359);
    QCOMPARE(result.inventory.measured.value(QStringLiteral("qmlModelElements")).toInt(), 57);
    QCOMPARE(result.inventory.measured.value(QStringLiteral("qmlCalls")).toInt(), 57);
    QCOMPARE(result.inventory.measured.value(QStringLiteral("cppConstructions")).toInt(), 16);
    QCOMPARE(result.inventory.measured.value(QStringLiteral("cppCalls")).toInt(), 12);
    const QJsonArray exemptions = inventoryObject().value(QStringLiteral("exemptions")).toArray();
    QCOMPARE(exemptions.size(), 25);
    const QJsonObject twentyFifthExemption = exemptions.at(24).toObject();
    QCOMPARE(twentyFifthExemption.value(QStringLiteral("id")).toString(),
             QStringLiteral("exempt.shared.unused-spinbox"));
    QCOMPARE(twentyFifthExemption.value(QStringLiteral("path")).toString(),
             QStringLiteral("declarativeimports/components/SpinBox.qml"));
    QVERIFY(twentyFifthExemption.value(QStringLiteral("reason")).toString().contains(
        QStringLiteral("shipped but has no live per-view settings instantiation")));
    qInfo() << "settings inventory:" << result.inventory.affordances << "affordances," << result.inventory.exemptions
            << "exemptions";
    for (auto kind = result.inventory.kinds.cbegin(); kind != result.inventory.kinds.cend(); ++kind)
    {
        qInfo().noquote() << QStringLiteral("  %1: %2").arg(kind.key()).arg(kind.value());
    }
}

void SettingsInventoryTest::validatesPrivateSharedComponentsStayVisualOnly()
{
    using namespace Latte::SettingsSource;

    const QString root = QStringLiteral(REPO_ROOT);
    const QStringList visualPrivateComponents{
        QStringLiteral("declarativeimports/components/private/ButtonShadow.qml"),
        QStringLiteral("declarativeimports/components/private/RoundShadow.qml"),
        QStringLiteral("declarativeimports/components/private/TextFieldFocus.qml"),
    };
    for (const QString &path : visualPrivateComponents)
    {
        const std::optional<QByteArray> contents = readFile(root + QLatin1Char('/') + path);
        QVERIFY2(contents.has_value(), qPrintable(path));
        const ScanResult scan = scanQml(QString::fromUtf8(*contents));
        QVERIFY2(scan.errors.isEmpty(), qPrintable(path + QLatin1Char(':') + scan.errors.join(QLatin1Char('\n'))));
        for (const SourceSite &candidate : scan.candidates)
        {
            QVERIFY2(candidate.category == QLatin1String("qml-member") &&
                         (candidate.selector.endsWith(QLatin1String("|name:Component.onCompleted")) ||
                          candidate.selector.endsWith(QLatin1String("|name:Component.onDestruction"))),
                     qPrintable(QStringLiteral("private visual component gained input candidate %1:%2")
                                    .arg(path, candidate.selector)));
        }
    }

    const QSet<QString> expectedLivePrivateUses{
        QStringLiteral("ButtonShadow"),
        QStringLiteral("RoundShadow"),
        QStringLiteral("TextFieldFocus"),
    };
    QSet<QString> livePrivateUses;
    for (const QString &path : settingsSourcePaths(root))
    {
        if (!path.endsWith(QLatin1String(".qml")))
        {
            continue;
        }
        const std::optional<QByteArray> contents = readFile(root + QLatin1Char('/') + path);
        QVERIFY2(contents.has_value(), qPrintable(path));
        const Detail::LexResult lexical = Detail::lex(QString::fromUtf8(*contents));
        QVERIFY2(lexical.errors.isEmpty(), qPrintable(path + QLatin1Char(':') + lexical.errors.join(QLatin1Char('\n'))));
        for (int index = 0; index + 2 < lexical.tokens.size(); ++index)
        {
            if (lexical.tokens.at(index).text != QLatin1String("Private") ||
                lexical.tokens.at(index + 1).text != QLatin1String(".") ||
                lexical.tokens.at(index + 2).kind != Token::Kind::Identifier)
            {
                continue;
            }
            const QString privateType = lexical.tokens.at(index + 2).text;
            QVERIFY2(expectedLivePrivateUses.contains(privateType),
                     qPrintable(QStringLiteral("unscanned private component became live syntax %1:%2")
                                    .arg(path, privateType)));
            livePrivateUses.insert(privateType);
        }
    }
    QCOMPARE(livePrivateUses, expectedLivePrivateUses);
}

void SettingsInventoryTest::rejectsMalformedInventory_data()
{
    QTest::addColumn<QJsonValue>("mutation");
    QTest::addColumn<QString>("expectedError");

    const QJsonObject baseline = inventoryObject();
    const int candidateBaseline = baseline.value(QStringLiteral("sourceCoverage"))
                                      .toObject()
                                      .value(QStringLiteral("measured"))
                                      .toObject()
                                      .value(QStringLiteral("candidates"))
                                      .toInt();
    const auto add = [](const char *name, const QJsonValue &mutation, const QString &expectedError)
    { QTest::newRow(name) << mutation << expectedError; };
    const auto replaceRootField =
        [&](const char *name, const QString &field, const QJsonValue &replacement, const QString &expectedError)
    {
        QJsonObject mutation = baseline;
        mutation.insert(field, replacement);
        add(name, mutation, expectedError);
    };

    add("root-array", QJsonArray{}, QStringLiteral("inventory root is not an object"));
    replaceRootField("schema-string", QStringLiteral("schema"), QStringLiteral("2"),
                     QStringLiteral("schema must be numeric version 2"));
    replaceRootField("empty-title", QStringLiteral("title"), QString(),
                     QStringLiteral("title is not a nonempty string"));
    replaceRootField("copyright-object", QStringLiteral("copyright"), QJsonObject{},
                     QStringLiteral("copyright is not a nonempty array"));
    replaceRootField("copyright-non-string", QStringLiteral("copyright"), QJsonArray{1},
                     QStringLiteral("copyright contains an empty or non-string holder"));
    replaceRootField("wrong-license", QStringLiteral("license"), QStringLiteral("MIT"),
                     QStringLiteral("license must be GPL-2.0-or-later"));
    replaceRootField("kinds-object", QStringLiteral("kinds"), QJsonObject{}, QStringLiteral("kinds is not an array"));
    replaceRootField("duplicate-kind", QStringLiteral("kinds"),
                     QJsonArray{QStringLiteral("button"), QStringLiteral("button")},
                     QStringLiteral("kinds contains a non-string, empty, or duplicate value"));

    QJsonArray rowFields = baseline.value(QStringLiteral("rowFields")).toArray();
    rowFields.replace(0, QStringLiteral("line"));
    replaceRootField("row-fields-renamed", QStringLiteral("rowFields"), rowFields,
                     QStringLiteral("rowFields does not match the schema-2 row contract"));
    replaceRootField("exemption-fields-object", QStringLiteral("exemptionFields"), QJsonObject{},
                     QStringLiteral("exemptionFields must be [id,path,reason]"));
    QJsonArray statusOrder = baseline.value(QStringLiteral("statusOrder")).toArray();
    statusOrder.replace(0, QStringLiteral("C1 Exists"));
    replaceRootField("status-order-renamed", QStringLiteral("statusOrder"), statusOrder,
                     QStringLiteral("statusOrder does not match C1 through C9"));
    replaceRootField("status-codes-array", QStringLiteral("statusCodes"), QJsonArray{},
                     QStringLiteral("statusCodes does not match O/R/G/N"));
    replaceRootField("defaults-array", QStringLiteral("defaults"), QJsonArray{},
                     QStringLiteral("defaults is not an object"));

    QJsonObject defaults = baseline.value(QStringLiteral("defaults")).toObject();
    defaults.insert(QStringLiteral("runtimeOracle"), QJsonArray{});
    replaceRootField("oracle-array", QStringLiteral("defaults"), defaults,
                     QStringLiteral("defaults.runtimeOracle is not an object"));
    defaults = baseline.value(QStringLiteral("defaults")).toObject();
    QJsonObject oracle = defaults.value(QStringLiteral("runtimeOracle")).toObject();
    oracle.insert(QStringLiteral("fixture"), QStringLiteral("done"));
    defaults.insert(QStringLiteral("runtimeOracle"), oracle);
    replaceRootField("oracle-premature-evidence", QStringLiteral("defaults"), defaults,
                     QStringLiteral("defaults.runtimeOracle.fixture must remain TBD"));
    defaults = baseline.value(QStringLiteral("defaults")).toObject();
    defaults.insert(QStringLiteral("accessibility"), QJsonArray{});
    replaceRootField("accessibility-array", QStringLiteral("defaults"), defaults,
                     QStringLiteral("defaults.accessibility is not an object"));
    defaults = baseline.value(QStringLiteral("defaults")).toObject();
    QJsonObject accessibility = defaults.value(QStringLiteral("accessibility")).toObject();
    accessibility.insert(QStringLiteral("role"), QString());
    defaults.insert(QStringLiteral("accessibility"), accessibility);
    replaceRootField("accessibility-empty-field", QStringLiteral("defaults"), defaults,
                     QStringLiteral("defaults.accessibility.role is empty or not a string"));

    replaceRootField("groups-object", QStringLiteral("groups"), QJsonObject{},
                     QStringLiteral("groups is not an array"));
    QJsonObject mutation = baseline;
    QJsonArray groups = mutation.value(QStringLiteral("groups")).toArray();
    groups.replace(0, QStringLiteral("not-an-object"));
    mutation.insert(QStringLiteral("groups"), groups);
    add("group-string", mutation, QStringLiteral("group is not an object"));
    add("group-empty-path", replaceFirstGroupField(baseline, QStringLiteral("path"), QString()),
        QStringLiteral("path is not a nonempty string"));
    add("group-empty-multiplicity", replaceFirstGroupField(baseline, QStringLiteral("multiplicity"), QString()),
        QStringLiteral("multiplicity is not a nonempty string"));
    add("rows-object", replaceFirstGroupField(baseline, QStringLiteral("rows"), QJsonObject{}),
        QStringLiteral("rows is not an array"));

    mutation = baseline;
    groups = mutation.value(QStringLiteral("groups")).toArray();
    QJsonObject group = groups.at(0).toObject();
    QJsonArray rows = group.value(QStringLiteral("rows")).toArray();
    rows.replace(0, QStringLiteral("not-an-array"));
    group.insert(QStringLiteral("rows"), rows);
    groups.replace(0, group);
    mutation.insert(QStringLiteral("groups"), groups);
    add("row-string", mutation, QStringLiteral("row is not an array"));
    mutation = baseline;
    groups = mutation.value(QStringLiteral("groups")).toArray();
    group = groups.at(0).toObject();
    rows = group.value(QStringLiteral("rows")).toArray();
    QJsonArray row = rows.at(0).toArray();
    row.removeLast();
    rows.replace(0, row);
    group.insert(QStringLiteral("rows"), rows);
    groups.replace(0, group);
    mutation.insert(QStringLiteral("groups"), groups);
    add("short-row", mutation, QStringLiteral("row must contain nine fields"));
    add("numeric-identity", replaceFirstRowField(baseline, 0, 7), QStringLiteral("identity is not a string"));
    add("unknown-kind", replaceFirstRowField(baseline, 1, QStringLiteral("dial")),
        QStringLiteral("kind is unknown or not a string"));
    add("empty-catalog-array", replaceFirstRowField(baseline, 2, QJsonArray{}),
        QStringLiteral("catalog link array is empty"));
    add("fractional-catalog", replaceFirstRowField(baseline, 2, 1.5),
        QStringLiteral("catalog number is outside 1..121"));
    add("empty-description", replaceFirstRowField(baseline, 3, QString()),
        QStringLiteral("descriptive field 3 is empty or not a string"));
    add("green-without-evidence", replaceFirstRowField(baseline, 8, QStringLiteral("GOOOOOOOO")),
        QStringLiteral("completion status is malformed or claims green evidence"));

    replaceRootField("exemptions-object", QStringLiteral("exemptions"), QJsonObject{},
                     QStringLiteral("exemptions is not an array"));
    mutation = baseline;
    QJsonArray exemptions = mutation.value(QStringLiteral("exemptions")).toArray();
    exemptions.replace(0, QStringLiteral("not-an-object"));
    mutation.insert(QStringLiteral("exemptions"), exemptions);
    add("exemption-string", mutation, QStringLiteral("exemption is not an object"));
    add("exemption-numeric-id", replaceFirstExemptionField(baseline, QStringLiteral("id"), 3),
        QStringLiteral("id is not a string"));
    add("exemption-empty-path", replaceFirstExemptionField(baseline, QStringLiteral("path"), QString()),
        QStringLiteral("path is not a nonempty string"));
    add("exemption-empty-reason", replaceFirstExemptionField(baseline, QStringLiteral("reason"), QString()),
        QStringLiteral("reason is not a nonempty string"));

    replaceRootField("coverage-array", QStringLiteral("sourceCoverage"), QJsonArray{},
                     QStringLiteral("sourceCoverage is not an object"));
    add("site-fields-object", replaceCoverageField(baseline, QStringLiteral("siteFields"), QJsonObject{}),
        QStringLiteral("siteFields must be [selector,covers]"));
    add("measured-array", replaceCoverageField(baseline, QStringLiteral("measured"), QJsonArray{}),
        QStringLiteral("sourceCoverage.measured is not an object"));
    add("fractional-measurement", replaceMeasurement(baseline, QStringLiteral("files"), 1.5),
        QStringLiteral("measured.files is not a nonnegative integer"));
    add("files-object", replaceCoverageField(baseline, QStringLiteral("files"), QJsonObject{}),
        QStringLiteral("sourceCoverage.files is not an array"));
    mutation = baseline;
    QJsonObject coverage = mutation.value(QStringLiteral("sourceCoverage")).toObject();
    QJsonArray files = coverage.value(QStringLiteral("files")).toArray();
    files.replace(0, QStringLiteral("not-an-object"));
    coverage.insert(QStringLiteral("files"), files);
    mutation.insert(QStringLiteral("sourceCoverage"), coverage);
    add("coverage-file-string", mutation, QStringLiteral("file entry is not an object"));
    add("coverage-file-empty-path", replaceFirstCoverageFileField(baseline, QStringLiteral("path"), QString()),
        QStringLiteral("path is not a nonempty string"));
    add("sites-object", replaceFirstCoverageFileField(baseline, QStringLiteral("sites"), QJsonObject{}),
        QStringLiteral("sites is not an array"));
    add("representatives-object",
        replaceFirstCoverageFileField(baseline, QStringLiteral("representatives"), QJsonObject{}),
        QStringLiteral("representatives is not an array"));
    add("mapping-string", replaceFirstMapping(baseline, QStringLiteral("sites"), QStringLiteral("not-an-array")),
        QStringLiteral("mapping is not an array"));
    add("short-mapping", replaceFirstMapping(baseline, QStringLiteral("sites"), QJsonArray{QStringLiteral("selector")}),
        QStringLiteral("mapping must contain selector and links"));
    add("numeric-selector", replaceFirstMappingField(baseline, QStringLiteral("sites"), 0, 4),
        QStringLiteral("selector is not a nonempty string"));
    add("links-object", replaceFirstMappingField(baseline, QStringLiteral("sites"), 1, QJsonObject{}),
        QStringLiteral("links are not an array"));
    add("empty-links", replaceFirstMappingField(baseline, QStringLiteral("sites"), 1, QJsonArray{}),
        QStringLiteral("coverage links are empty"));
    add("numeric-link", replaceFirstMappingField(baseline, QStringLiteral("sites"), 1, QJsonArray{2}),
        QStringLiteral("coverage link is not a string"));
    add("wildcard-link",
        replaceFirstMappingField(baseline, QStringLiteral("sites"), 1, QJsonArray{QStringLiteral("canvas.*")}),
        QStringLiteral("wildcard coverage link is forbidden"));
    add("unknown-link",
        replaceFirstMappingField(baseline, QStringLiteral("sites"), 1, QJsonArray{QStringLiteral("canvas.unknown")}),
        QStringLiteral("unknown coverage identity canvas.unknown"));
    add("duplicate-link", duplicateFirstCoverageLink(baseline), QStringLiteral("duplicate coverage link"));
    add("duplicate-selector", duplicateFirstCoverageSelector(baseline), QStringLiteral("duplicate coverage selector"));

    mutation = baseline;
    coverage = mutation.value(QStringLiteral("sourceCoverage")).toObject();
    files = coverage.value(QStringLiteral("files")).toArray();
    files.append(files.at(0));
    coverage.insert(QStringLiteral("files"), files);
    mutation.insert(QStringLiteral("sourceCoverage"), coverage);
    add("duplicate-coverage-file", mutation, QStringLiteral("duplicate coverage file"));
    mutation = baseline;
    coverage = mutation.value(QStringLiteral("sourceCoverage")).toObject();
    files = coverage.value(QStringLiteral("files")).toArray();
    files.removeAt(0);
    coverage.insert(QStringLiteral("files"), files);
    mutation.insert(QStringLiteral("sourceCoverage"), coverage);
    add("missing-source-file", mutation, QStringLiteral("source file is absent from the ledger"));
    mutation = baseline;
    coverage = mutation.value(QStringLiteral("sourceCoverage")).toObject();
    files = coverage.value(QStringLiteral("files")).toArray();
    files.append(QJsonObject{{QStringLiteral("path"), QStringLiteral("outside.qml")},
                             {QStringLiteral("sites"), QJsonArray{}},
                             {QStringLiteral("representatives"), QJsonArray{}}});
    coverage.insert(QStringLiteral("files"), files);
    mutation.insert(QStringLiteral("sourceCoverage"), coverage);
    add("outside-source-file", mutation, QStringLiteral("ledger file is outside the source universe"));
    add("outside-declared-path",
        replaceFirstGroupField(baseline, QStringLiteral("path"), QStringLiteral("outside.qml")),
        QStringLiteral("declared source path is outside the source universe"));
    add("stale-measurement", replaceMeasurement(baseline, QStringLiteral("candidates"), candidateBaseline + 1),
        QStringLiteral("measured candidates is stale"));
    add("candidate-as-representative", moveFirstCandidateToRepresentatives(baseline),
        QStringLiteral("candidate cannot stand in as a representative"));
}

void SettingsInventoryTest::rejectsMalformedInventory()
{
    QFETCH(QJsonValue, mutation);
    QFETCH(QString, expectedError);

    const QString errors = validateInventory(mutation).errors.join(QLatin1Char('\n'));
    QVERIFY2(errors.contains(expectedError), qPrintable(errors));
}

void SettingsInventoryTest::rejectsCoverageRelationCountDrift()
{
    const QJsonObject mutation = addCoverageRelation(inventoryObject(), QStringLiteral("canvas.maximum-length.wheel"));
    const QString errors = validateInventory(mutation).errors.join(QLatin1Char('\n'));
    QVERIFY2(errors.contains(QStringLiteral("coverage relation count is stale: expected 1274, ledger has 1275")),
             qPrintable(errors));
}

void SettingsInventoryTest::rejectsNewSourceCandidate()
{
    const QString path = QStringLiteral("shell/package/contents/configuration/"
                                        "canvas/maxlength/RulerMouseArea.qml");
    const std::optional<QByteArray> contents = readFile(QStringLiteral(REPO_ROOT) + QLatin1Char('/') + path);
    QVERIFY(contents.has_value());
    QHash<QString, QString> overrides;
    overrides.insert(path, QString::fromUtf8(*contents) + QStringLiteral("\nButton { id: scF2Mutation; onClicked: "
                                                                         "scF2Mutation.destroy() }\n"));

    const QString errors = validateInventory(inventoryObject(), overrides).errors.join(QLatin1Char('\n'));
    QVERIFY2(errors.contains(QStringLiteral("unmapped source candidate")), qPrintable(errors));
}

void SettingsInventoryTest::rejectsNewCreateNewItemCall()
{
    const QString path = QStringLiteral("plasmoid/package/contents/ui/ContextMenu.qml");
    const std::optional<QByteArray> contents = readFile(QStringLiteral(REPO_ROOT) + QLatin1Char('/') + path);
    QVERIFY(contents.has_value());
    QString source = QString::fromUtf8(*contents);
    const qsizetype rootEnd = source.lastIndexOf(QStringLiteral("\n}"));
    QVERIFY(rootEnd >= 0);
    source.insert(rootEnd, QStringLiteral("\n    function scF2CreateNewItemMutation() {\n"
                                          "        createNewItem(\"mutation\", \"Mutation\", \"file:///mutation\", [])\n"
                                          "    }\n"));

    QHash<QString, QString> overrides;
    overrides.insert(path, source);
    const QString errors = validateInventory(inventoryObject(), overrides).errors.join(QLatin1Char('\n'));
    QVERIFY2(errors.contains(QStringLiteral("unmapped source candidate %1:").arg(path)) &&
                 errors.contains(QStringLiteral("operation:createNewItem")),
             qPrintable(errors));
}

void SettingsInventoryTest::rejectsChangedHiddenIndicatorLifecycle()
{
    const QString path = QStringLiteral("shell/package/contents/controls/IndicatorConfigUiManager.qml");
    const std::optional<QByteArray> contents = readFile(QStringLiteral(REPO_ROOT) + QLatin1Char('/') + path);
    QVERIFY(contents.has_value());
    QString source = QString::fromUtf8(*contents);
    QCOMPARE(source.count(QStringLiteral("Component.onCompleted")), 1);
    source.replace(QStringLiteral("Component.onCompleted"), QStringLiteral("Component.onDestruction"));

    QHash<QString, QString> overrides;
    overrides.insert(path, source);
    const QString errors = validateInventory(inventoryObject(), overrides).errors.join(QLatin1Char('\n'));
    QVERIFY2(errors.contains(QStringLiteral("unmapped source candidate %1:").arg(path)) &&
                 errors.contains(QStringLiteral("id:hiddenIndicatorPage|name:Component.onDestruction")),
             qPrintable(errors));
}

void SettingsInventoryTest::rejectsNewNoninteractiveLifecycle()
{
    const QString path = QStringLiteral("shell/package/contents/configuration/canvas/maxlength/RulerMouseArea.qml");
    const std::optional<QByteArray> contents = readFile(QStringLiteral(REPO_ROOT) + QLatin1Char('/') + path);
    QVERIFY(contents.has_value());
    QString source = QString::fromUtf8(*contents);
    const qsizetype rootEnd = source.lastIndexOf(QStringLiteral("\n}"));
    QVERIFY(rootEnd >= 0);
    source.insert(rootEnd, QStringLiteral("\n    Item {\n"
                                          "        id: scF2LifecycleMutation\n"
                                          "        Component.onCompleted: initialize()\n"
                                          "    }\n"));
    QHash<QString, QString> overrides;
    overrides.insert(path, source);

    const QString errors = validateInventory(inventoryObject(), overrides).errors.join(QLatin1Char('\n'));
    QVERIFY2(errors.contains(QStringLiteral("unmapped source candidate %1:").arg(path)) &&
                 errors.contains(QStringLiteral("id:scF2LifecycleMutation|name:Component.onCompleted")),
             qPrintable(errors));
}

void SettingsInventoryTest::rejectsStaleCandidateSelector()
{
    const QString errors = validateInventory(mutateFirstMappingSelector(inventoryObject(), QStringLiteral("sites")))
                               .errors.join(QLatin1Char('\n'));
    QVERIFY2(errors.contains(QStringLiteral("ledger candidate does not resolve exactly once")), qPrintable(errors));
}

void SettingsInventoryTest::rejectsStaleRepresentativeSelector()
{
    const QString errors =
        validateInventory(mutateFirstMappingSelector(inventoryObject(), QStringLiteral("representatives")))
            .errors.join(QLatin1Char('\n'));
    QVERIFY2(errors.contains(QStringLiteral("representative does not resolve exactly once")), qPrintable(errors));
}

void SettingsInventoryTest::rejectsUncoveredLedgerIdentity()
{
    const QString identity = QStringLiteral("canvas.maximum-length.wheel");
    const QString errors =
        validateInventory(removeCoverageIdentity(inventoryObject(), identity)).errors.join(QLatin1Char('\n'));
    QVERIFY2(errors.contains(QStringLiteral("uncovered ledger identity %1").arg(identity)), qPrintable(errors));
}

void SettingsInventoryTest::rejectsMissingDeclaredSourceOwnership()
{
    const QString identity = QStringLiteral("behavior.delay.show");
    const QString path = QStringLiteral("shell/package/contents/configuration/pages/BehaviorConfig.qml");
    const QJsonObject mutation =
        replaceDeclaredLinks(inventoryObject(), path, identity, QStringLiteral("behavior.delay.hide"));
    const QString errors = validateInventory(mutation).errors.join(QLatin1Char('\n'));
    QVERIFY2(errors.contains(QStringLiteral("identity lacks declared-source mapping %1:%2").arg(identity, path)),
             qPrintable(errors));
}

void SettingsInventoryTest::rejectsMissingDeclaredExemptionOwnership()
{
    const QString identity = QStringLiteral("exempt.shared.combo-delegate");
    const QString path = QStringLiteral("declarativeimports/components/ComboBox.qml");
    const QJsonObject mutation =
        replaceDeclaredLinks(inventoryObject(), path, identity, QStringLiteral("behavior.screen.combo"));
    const QString errors = validateInventory(mutation).errors.join(QLatin1Char('\n'));
    QVERIFY2(errors.contains(QStringLiteral("identity lacks declared-source mapping %1:%2").arg(identity, path)),
             qPrintable(errors));
}

QTEST_APPLESS_MAIN(SettingsInventoryTest)

#include "settingsinventorytest.moc"
