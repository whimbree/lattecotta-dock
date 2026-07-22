/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "settingssourcescanner.h"

#include <QTest>

using namespace Latte::SettingsSource;

class SettingsSourceScannerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void commentsAndStringsDoNotCreateSyntax();
    void discoversQmlObjectsMembersAndModels();
    void unrelatedInsertionKeepsIdSelectorsStable();
    void anonymousWrappersDoNotChangeNamedSelectors();
    void anonymousDigestIgnoresFormattingButRejectsSemanticChange();
    void identicalAnonymousSiblingsAreAmbiguous();
    void parentIdsDisambiguateIdenticalAnonymousObjects();
    void sameLineModelElementsHaveDistinctSelectors();
    void dynamicObjectsAreRepresentativesNotCandidates();
    void discoversDynamicQmlMenuActions();
    void separatorBundleIgnoresPrecedingAction();
    void discoversLifecycleOnNoninteractiveObjects();
    void ignoresNoninteractiveQmlCalls();
    void discoversCppActionAndMenuSites();
    void discoversQualifiedAliasedAndStackCppConstructions();
    void distinguishesOverloadsAndLambdaScopes();
    void rawStringPrefixesHideSyntax();
    void malformedSyntaxFailsLoudly();
};

void SettingsSourceScannerTest::commentsAndStringsDoNotCreateSyntax()
{
    const QString source = QStringLiteral(R"QML(
Item {
    property string fake: "Button { onClicked: broken() }"
    // MouseArea { onWheel: broken() }
    /* Connections { function onActivated() { broken() } } */
}
)QML");
    const ScanResult result = scanQml(source);

    QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join(QLatin1Char('\n'))));
    QVERIFY(result.candidates.isEmpty());
}

void SettingsSourceScannerTest::discoversQmlObjectsMembersAndModels()
{
    const QString source = QStringLiteral(R"QML(
Item {
    id: root
    Keys.onPressed: root.forceActiveFocus()
    Button {
        id: choice
        Component.onCompleted: currentIndex = 0
        acceptedButtons: Qt.LeftButton
        onClicked: root.activate()
        model: [
            { value: Types.First, text: "First" },
            { value: Types.Second, text: "Second" }
        ]
    }
    Connections {
        target: root
        function onActivated() { root.activate() }
    }
    Dialog {
        id: targetDialog
        onAccepted: root.activate()
    }
    CheckDelegate { id: checkDelegate }
    SpinBox { id: spinBox }
}
)QML");
    const ScanResult result = scanQml(source);
    const QSet<QString> selectors = result.candidateSelectors();

    QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join(QLatin1Char('\n'))));
    QCOMPARE(result.candidates.size(), 12);
    QVERIFY(selectors.contains(QStringLiteral("qml-object|id:root/id:choice")));
    QVERIFY(selectors.contains(QStringLiteral("qml-object|id:root/id:targetDialog")));
    QVERIFY(selectors.contains(QStringLiteral("qml-member|id:root/id:targetDialog|name:onAccepted")));
    QVERIFY(selectors.contains(QStringLiteral("qml-object|id:root/id:checkDelegate")));
    QVERIFY(selectors.contains(QStringLiteral("qml-object|id:root/id:spinBox")));
    QVERIFY(selectors.contains(QStringLiteral("qml-member|id:root/id:choice|name:onClicked")));
    QVERIFY(selectors.contains(QStringLiteral("qml-member|id:root|name:Keys.onPressed")));
    const QString modelSelector = QStringLiteral("qml-model|id:root/id:choice|binding:model|key:value:Types") + QChar(0x1f) +
                                  QLatin1Char('.') + QChar(0x1f) + QStringLiteral("First");
    QVERIFY(selectors.contains(modelSelector));
}

void SettingsSourceScannerTest::unrelatedInsertionKeepsIdSelectorsStable()
{
    const QString before = QStringLiteral("Item { id: root; Button { id: save; onClicked: root.save() } }");
    const QString after = QStringLiteral("Item { id: root; function unrelated() { return true } "
                                         "Button { id: save; onClicked: root.save() } }");

    QCOMPARE(scanQml(before).candidateSelectors(), scanQml(after).candidateSelectors());
}

void SettingsSourceScannerTest::anonymousWrappersDoNotChangeNamedSelectors()
{
    const QString direct = QStringLiteral("Item { id: root; Button { id: save; onClicked: root.save() } }");
    const QString wrapped = QStringLiteral("Item { id: root; Item { Button { id: save; onClicked: root.save() } } }");

    QCOMPARE(scanQml(direct).candidateSelectors(), scanQml(wrapped).candidateSelectors());
}

void SettingsSourceScannerTest::anonymousDigestIgnoresFormattingButRejectsSemanticChange()
{
    const QString original = QStringLiteral("Item { id: root; Button { text: \"Save\"; onClicked: root.save() } }");
    const QString reformatted = QStringLiteral("Item { id: root;\n // unrelated note\n Button { text: "
                                               "\"Save\";\n onClicked: root.save() } }");
    const QString changed = QStringLiteral("Item { id: root; Button { text: \"Delete\"; onClicked: "
                                           "root.remove() } }");

    QCOMPARE(scanQml(original).candidateSelectors(), scanQml(reformatted).candidateSelectors());
    QVERIFY(scanQml(original).candidateSelectors() != scanQml(changed).candidateSelectors());
}

void SettingsSourceScannerTest::identicalAnonymousSiblingsAreAmbiguous()
{
    const QString source = QStringLiteral(R"QML(
Item {
    id: root
    Button { text: "Same"; onClicked: root.run() }
    Button { text: "Same"; onClicked: root.run() }
}
)QML");
    const ScanResult result = scanQml(source);

    QVERIFY(result.errors.join(QLatin1Char('\n')).contains(QStringLiteral("ambiguous candidate selector")));
}

void SettingsSourceScannerTest::parentIdsDisambiguateIdenticalAnonymousObjects()
{
    const QString source = QStringLiteral(R"QML(
Item {
    id: root
    Item { id: first; Button { text: "Same"; onClicked: root.run() } }
    Item { id: second; Button { text: "Same"; onClicked: root.run() } }
}
)QML");
    const ScanResult result = scanQml(source);

    QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join(QLatin1Char('\n'))));
    QCOMPARE(result.candidates.size(), 4);
    QCOMPARE(result.candidateSelectors().size(), 4);
}

void SettingsSourceScannerTest::sameLineModelElementsHaveDistinctSelectors()
{
    const ScanResult result = scanQml(QStringLiteral("ComboBox { id: choice; model: [{value: Types.One}, "
                                                     "{value: Types.Two}, {value: Types.Three}] }"));
    int models = 0;
    for (const SourceSite &candidate : result.candidates)
    {
        models += candidate.category == QLatin1String("qml-model");
    }

    QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join(QLatin1Char('\n'))));
    QCOMPARE(models, 3);
    QCOMPARE(result.candidateSelectors().size(), result.candidates.size());
}

void SettingsSourceScannerTest::dynamicObjectsAreRepresentativesNotCandidates()
{
    const ScanResult result = scanQml(QStringLiteral(R"QML(
Item {
    function reload() {
        var row = { actionId: "add", text: "Add" }
        actions.append(row)
    }
}
)QML"));
    QString representative;
    for (auto it = result.resolvableLines.cbegin(); it != result.resolvableLines.cend(); ++it)
    {
        if (it.key().startsWith(QLatin1String("qml-js-object|name:row|")))
        {
            representative = it.key();
        }
    }

    QVERIFY(!representative.isEmpty());
    QVERIFY(!result.candidateSelectors().contains(representative));
    QCOMPARE(result.resolveCount(representative), 1);
}

void SettingsSourceScannerTest::discoversDynamicQmlMenuActions()
{
    const ScanResult result = scanQml(QStringLiteral(R"QML(
PlasmaExtras.Menu {
    id: menu
    onStatusChanged: menu.refresh()
    function newMenuItem(parent) { return parent }
    function newSeparator(parent) { return parent }
    function createMenuItem(parent) { return parent }
    function addDynamic() {
        var item = menu.newMenuItem(menu)
        var separator = menu.newSeparator(menu)
        var alternate = menu.createMenuItem(menu)
        var createNewItem = function(parent) { return parent }
        createNewItem(menu)
        item.text = "Open"
        item.clicked.connect(function() { menu.openCurrent() })
        menu.addMenuItem(item)
    }
    function makeItem(parent) {
        return Qt.createQmlObject("import org.kde.plasma.extras; MenuItem {}", parent)
    }
}
)QML"));
    QSet<QString> operations;
    QHash<QString, int> operationCounts;
    for (const SourceSite &candidate : result.candidates)
    {
        if (candidate.category == QLatin1String("qml-call"))
        {
            const QString operation = candidate.selector.section(QStringLiteral("|operation:"), 1).section(QLatin1Char('|'), 0, 0);
            operations.insert(operation);
            ++operationCounts[operation];
        }
    }

    QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join(QLatin1Char('\n'))));
    QCOMPARE(operations,
             QSet<QString>({QStringLiteral("newMenuItem"), QStringLiteral("newSeparator"), QStringLiteral("createMenuItem"),
                            QStringLiteral("createNewItem"), QStringLiteral("clicked.connect"), QStringLiteral("addMenuItem"),
                            QStringLiteral("Qt.createQmlObject<MenuItem>")}));
    for (const QString &operation : std::as_const(operations))
    {
        QCOMPARE(operationCounts.value(operation), 1);
    }
    QVERIFY(result.candidateSelectors().contains(QStringLiteral("qml-object|id:menu")));
    QVERIFY(result.candidateSelectors().contains(QStringLiteral("qml-member|id:menu|name:onStatusChanged")));
}

void SettingsSourceScannerTest::discoversLifecycleOnNoninteractiveObjects()
{
    const ScanResult result = scanQml(QStringLiteral(R"QML(
Item {
    id: root
    Component.onCompleted: initialize()
    Component.onDestruction: release()
    onWidthChanged: updateGeometry()
}
)QML"));
    const QSet<QString> selectors = result.candidateSelectors();

    QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join(QLatin1Char('\n'))));
    QCOMPARE(result.candidates.size(), 2);
    QVERIFY(selectors.contains(QStringLiteral("qml-member|id:root|name:Component.onCompleted")));
    QVERIFY(selectors.contains(QStringLiteral("qml-member|id:root|name:Component.onDestruction")));
    QVERIFY(!selectors.contains(QStringLiteral("qml-member|id:root|name:onWidthChanged")));
}

void SettingsSourceScannerTest::separatorBundleIgnoresPrecedingAction()
{
    const auto scanWithPrecedingText = [](const QString &text)
    {
        return scanQml(QStringLiteral(R"QML(
PlasmaExtras.Menu {
    id: menu
    function rebuild() {
        var before = menu.newMenuItem(menu)
        before.text = "%1"
        menu.newSeparator(menu)
        var after = menu.newMenuItem(menu)
        after.text = "After"
    }
}
)QML")
                           .arg(text));
    };
    const ScanResult before = scanWithPrecedingText(QStringLiteral("Before"));
    const ScanResult changed = scanWithPrecedingText(QStringLiteral("Changed"));
    const auto separatorData = [](const ScanResult &scan)
    {
        QString selector;
        QHash<QString, int> operations;
        for (const SourceSite &candidate : scan.candidates)
        {
            if (candidate.category != QLatin1String("qml-call"))
            {
                continue;
            }
            const QString operation = candidate.selector.section(QStringLiteral("|operation:"), 1).section(QLatin1Char('|'), 0, 0);
            ++operations[operation];
            if (operation == QLatin1String("newSeparator"))
            {
                selector = candidate.selector;
            }
        }
        return QPair{selector, operations};
    };

    QVERIFY2(before.errors.isEmpty(), qPrintable(before.errors.join(QLatin1Char('\n'))));
    QVERIFY2(changed.errors.isEmpty(), qPrintable(changed.errors.join(QLatin1Char('\n'))));
    const auto [beforeSelector, beforeOperations] = separatorData(before);
    const auto [changedSelector, changedOperations] = separatorData(changed);
    QCOMPARE(beforeOperations.value(QStringLiteral("newMenuItem")), 2);
    QCOMPARE(beforeOperations.value(QStringLiteral("newSeparator")), 1);
    QCOMPARE(beforeOperations.size(), 2);
    QCOMPARE(changedOperations, beforeOperations);
    QVERIFY(beforeSelector.contains(QStringLiteral("|right-boundary-sha256:")));
    const Detail::LexResult separatorStatement = Detail::lex(QStringLiteral("menu.newSeparator(menu)"));
    const QString expectedBundle = Detail::tokenDigest(separatorStatement.tokens, 0, separatorStatement.tokens.size());
    QVERIFY(beforeSelector.endsWith(QStringLiteral("|bundle-sha256:%1").arg(expectedBundle)));
    QCOMPARE(beforeSelector, changedSelector);
}

void SettingsSourceScannerTest::ignoresNoninteractiveQmlCalls()
{
    const ScanResult result = scanQml(QStringLiteral(R"QML(
Item {
    id: root
    function rebuild() {
        var metrics = Qt.createQmlObject("import QtQuick; TextMetrics {}", root)
        model.append({name: "row"})
        root.updateGeometry()
    }
}
)QML"));

    QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join(QLatin1Char('\n'))));
    QVERIFY(result.candidates.isEmpty());
}

void SettingsSourceScannerTest::discoversCppActionAndMenuSites()
{
    const QString source = QStringLiteral(R"CPP(
void Menu::restore()
{
    const char *fake = "new QAction; menu.addAction()";
    // new QMenu;
    m_menu = new QMenu;
    m_actions[1] = m_menu->menuAction();
    QAction *action = m_menu->addAction("Open");
    m_menu->addSeparator();
    QWidgetAction *widget = new QWidgetAction(m_menu);
    for (int index = 0; index < 1; ++index) {
        m_menu->addAction("Nested");
    }
}
)CPP");
    const ScanResult result = scanCpp(source);

    QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join(QLatin1Char('\n'))));
    QCOMPARE(result.candidates.size(), 6);
    for (const SourceSite &candidate : result.candidates)
    {
        QVERIFY(candidate.selector.contains(QStringLiteral("function:Menu::restore")));
        QVERIFY(!candidate.selector.contains(QStringLiteral("function:for")));
        QVERIFY(candidate.selector.contains(QStringLiteral("sha256:")));
    }
}

void SettingsSourceScannerTest::discoversQualifiedAliasedAndStackCppConstructions()
{
    const ScanResult result = scanCpp(QStringLiteral(R"CPP(
using ActionAlias = Widgets::QAction;
typedef Widgets::QMenu MenuAlias;
void Menu::build()
{
    Widgets::QAction qualified("Qualified");
    ::QAction globallyQualified("Global");
    ActionAlias alias("Alias");
    MenuAlias menu;
    QWidgetAction widget{&menu};
    auto *heap = new Widgets::QMenu;
    auto copiedAction = QAction("Copied");
    auto copiedMenu = ::QMenu();
    QAction typedCopy = QAction("Typed");
}
)CPP"));
    int constructions = 0;
    QSet<QString> canonicalTypes;
    for (const SourceSite &candidate : result.candidates)
    {
        if (candidate.category == QLatin1String("cpp-construction"))
        {
            ++constructions;
            canonicalTypes.insert(candidate.selector.section(QStringLiteral("|type:"), 1).section(QLatin1Char('|'), 0, 0));
        }
    }

    QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join(QLatin1Char('\n'))));
    QCOMPARE(constructions, 9);
    QCOMPARE(canonicalTypes, QSet<QString>({QStringLiteral("QAction"), QStringLiteral("QWidgetAction"), QStringLiteral("QMenu")}));
}

void SettingsSourceScannerTest::distinguishesOverloadsAndLambdaScopes()
{
    const QString source = QStringLiteral(R"CPP(
void Menu::build(int value)
{
    auto add = [&]() { menu.addAction("Same"); };
}
void Menu::build(QString value)
{
    auto add = [&]() { menu.addAction("Same"); };
    auto advanced = [&]() mutable noexcept [[nodiscard]] -> QAction * { menu.addAction("Advanced"); return nullptr; };
}
)CPP");
    const ScanResult result = scanCpp(source);
    QSet<QString> calls;
    for (const SourceSite &candidate : result.candidates)
    {
        if (candidate.category == QLatin1String("cpp-call"))
        {
            calls.insert(candidate.selector);
        }
    }

    QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join(QLatin1Char('\n'))));
    QCOMPARE(calls.size(), 3);
    for (const QString &selector : calls)
    {
        QVERIFY(selector.contains(QStringLiteral("function:Menu::build|signature-sha256:")));
        QVERIFY(selector.contains(QStringLiteral("/lambda-sha256:")));
    }
}

void SettingsSourceScannerTest::rawStringPrefixesHideSyntax()
{
    const QString source = QStringLiteral(
        "void Menu::build() {\n"
        "auto a = R\"x(new QAction; menu.addAction())x\";\n"
        "auto b = u8R\"x(new QMenu; menu.addAction())x\";\n"
        "auto c = uR\"x(new QAction)x\"; auto d = UR\"x(new QAction)x\"; auto e = LR\"x(new QAction)x\";\n"
        "}\n");
    const ScanResult result = scanCpp(source);

    QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join(QLatin1Char('\n'))));
    QVERIFY(result.candidates.isEmpty());
}

void SettingsSourceScannerTest::malformedSyntaxFailsLoudly()
{
    const ScanResult malformedDelimiter = scanCpp(QStringLiteral("auto x = u8R\"bad delimiter(payload)bad delimiter\";"));
    const ScanResult unterminatedRaw = scanCpp(QStringLiteral("auto x = LR\"tag(payload);"));
    const ScanResult unclosedQml = scanQml(QStringLiteral("Item { Button {"));
    const ScanResult mismatchedQml = scanQml(QStringLiteral("Item { property var broken: (] }"));

    QVERIFY(malformedDelimiter.errors.join(QLatin1Char('\n')).contains(QStringLiteral("malformed raw string delimiter")));
    QVERIFY(unterminatedRaw.errors.join(QLatin1Char('\n')).contains(QStringLiteral("unterminated raw string")));
    QVERIFY(unclosedQml.errors.join(QLatin1Char('\n')).contains(QStringLiteral("unclosed")));
    QVERIFY(mismatchedQml.errors.join(QLatin1Char('\n')).contains(QStringLiteral("unmatched")));
}

QTEST_GUILESS_MAIN(SettingsSourceScannerTest)

#include "settingssourcescannertest.moc"
