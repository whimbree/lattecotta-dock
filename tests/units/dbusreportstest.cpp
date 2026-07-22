/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The pure layer of the viewsData() D-Bus read
// (docs/reference/dbus-observability-interface.md): ViewRecord -> JSON serialization
// and the enum-name mappings. The live collectors in app/dbusreports.cpp
// are three-line field reads off View and stay exercised by the running
// dock; everything a consumer parses is pinned here.

#include "dbusreports.h"

#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QThread>
#include <QVariantMap>

// C++
#include <cstddef>
#include <memory>

using namespace Latte;
using namespace Latte::DbusReports;

class DbusReportsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void viewTypeNames_data();
    void viewTypeNames();
    void edgeNames_data();
    void edgeNames();
    void alignmentNames_data();
    void alignmentNames();
    void orientationNames_data();
    void orientationNames();
    void screensGroupNames_data();
    void screensGroupNames();
    void dockRelationshipNames_data();
    void dockRelationshipNames();
    void visibilityModeNames_data();
    void visibilityModeNames();
    void rectSerialization();
    void recordSerialization();
    void viewRecordKeySet();
    void emptyInputMaskSerializesAsEmptyRegion();
    void recordsSerializeAsCompactJsonArray();
    void configureAppletsModeRequiresLocalEditMode();
    void runtimeObjectIdentitiesAreOpaqueStableAndMonotonic();
    void runtimeObjectIdentitiesRequireGuiThreadAffinity();
    void dockCollectionOrderingStabilizesViewAndControllerTokens();
    void dockRelationshipClassification_data();
    void dockRelationshipClassification();
    void dockRelationshipGraphAcceptsOnlyDirectLiveOriginals();
    void dockSystemSnapshotSerializesTypedRuntimeState();
    void dockSystemSnapshotCanonicalizesShuffledViewsAndCloneIds();
    void dockSystemSnapshotKeepsConfigureModeIsolatedToEditedView();

    void appletRecordSerialization();
    void appletRecordKeySet();
    void appletZReportsStackingResidue();
    void appletRecordsSerializeAsCompactJsonArray();
    void appletIdOrderStripsSplitters();
    void appletIdOrderDisambiguatesSamePluginApplets();
    void dropMarkerIsLiveSeparatesLiveFromClean();

    void visibilityModeRoundTrip_data();
    void visibilityModeRoundTrip();
    void settableVisibilityModeRefusals_data();
    void settableVisibilityModeRefusals();
    void trackerRecordSerialization();
    void trackerRecordKeySet();
    void trackerDataSerializesAsCompactJsonObject();

    void taskRecordSerialization();
    void taskRecordKeySet();
    void taskRecordsSerializeAsCompactJsonArray();
    void windowTaskOrderReadbackTracksAppIdAcrossReorder();
    void middleClickDispatchSerializesLauncherAndTaskOperations();
    void middleClickDispatchNoEventSerializesAsEmptyObject();
    void middleClickDispatchMapParsingAcceptsEveryOfferedPair_data();
    void middleClickDispatchMapParsingAcceptsEveryOfferedPair();
    void middleClickDispatchMapParsingRefusesMalformedState_data();
    void middleClickDispatchMapParsingRefusesMalformedState();
    void middleClickDispatchAggregateSelectsNewestAndSerializesExactly();
    void middleClickDispatchAggregateRefusesGlobalDuplicateSequence();
    void middleClickDispatchAggregateRefusesMalformedCandidate();
    void middleClickDispatchAggregateHandlesNoEventAndContainmentScope();

    void themeColorsModeNames_data();
    void themeColorsModeNames();
    void windowColorsModeNames_data();
    void windowColorsModeNames();
    void colorModeConfigValueRoundTrip();
    void colorModeConfigValueRefusals_data();
    void colorModeConfigValueRefusals();
    void colorizerRecordSerialization();
    void colorizerRecordKeySet();

    void memoryUsageNames_data();
    void memoryUsageNames();
    void layoutRecordSerialization();
    void layoutRecordKeySet();
    void layoutsDataSerialization();

    void screenRecordSerialization();
    void screenRecordKeySet();
    void screensDataSerializesAsCompactJsonArray();

    void configValueScalarMapping();
    void configValueColorFallsBackToCanonicalString();
    void configMapSerializesEveryKey();
    void viewLiveRecordSerialization();
    void viewLiveRecordKeySet();
    void configDataShape();
    void appletConfigDataShape();
};

//! the exact sorted key list of a serialized record: the schema pin that
//! makes accidental drift from docs/reference/dbus-observability-interface.md fail a
//! test instead of a D-Bus consumer
static QStringList sortedKeys(const QJsonObject &json)
{
    QStringList keys = json.keys();
    keys.sort();
    return keys;
}

static QVariantMap middleClickDispatchMap(const QString &rowIdentity,
                                          Tasks::MiddleClickRowKind rowKind,
                                          Tasks::Types::TaskAction configuredAction,
                                          Tasks::MiddleClickOperation operation,
                                          qint64 sequence)
{
    return {
        {QStringLiteral("rowIdentity"), rowIdentity},
        {QStringLiteral("rowKind"), static_cast<int>(rowKind)},
        {QStringLiteral("configuredAction"), static_cast<int>(configuredAction)},
        {QStringLiteral("dispatchedOperation"), static_cast<int>(operation)},
        {QStringLiteral("sequence"), QVariant::fromValue(sequence)}};
}

//! Every enum-name mapping is pinned with one data row per enum value, so
//! the full space of each Q_UNREACHABLE switch in dbusreports.h stays
//! covered and a failure names the exact value that drifted.

void DbusReportsTest::viewTypeNames_data()
{
    //! Latte enum columns ride as int: the Q_ENUM metaobject for
    //! Latte::Types is moc-compiled into the app binary, not into any
    //! library this test links, and a typed column odr-uses
    //! Types::staticMetaObject through QCOMPARE's failure printer
    QTest::addColumn<int>("type");
    QTest::addColumn<QString>("name");

    QTest::newRow("dock") << static_cast<int>(Types::DockView) << QStringLiteral("dock");
    QTest::newRow("panel") << static_cast<int>(Types::PanelView) << QStringLiteral("panel");
}

void DbusReportsTest::viewTypeNames()
{
    QFETCH(int, type);
    QFETCH(QString, name);

    QCOMPARE(viewTypeName(static_cast<Types::ViewType>(type)), name);
}

void DbusReportsTest::edgeNames_data()
{
    QTest::addColumn<Plasma::Types::Location>("edge");
    QTest::addColumn<QString>("name");

    QTest::newRow("bottom") << Plasma::Types::BottomEdge << QStringLiteral("bottom");
    QTest::newRow("top") << Plasma::Types::TopEdge << QStringLiteral("top");
    QTest::newRow("left") << Plasma::Types::LeftEdge << QStringLiteral("left");
    QTest::newRow("right") << Plasma::Types::RightEdge << QStringLiteral("right");
    QTest::newRow("floating") << Plasma::Types::Floating << QStringLiteral("floating");
    QTest::newRow("desktop") << Plasma::Types::Desktop << QStringLiteral("desktop");
    QTest::newRow("fullscreen") << Plasma::Types::FullScreen << QStringLiteral("fullscreen");
}

void DbusReportsTest::edgeNames()
{
    QFETCH(Plasma::Types::Location, edge);
    QFETCH(QString, name);

    QCOMPARE(edgeName(edge), name);
}

void DbusReportsTest::alignmentNames_data()
{
    QTest::addColumn<int>("alignment"); //! int: see viewTypeNames_data
    QTest::addColumn<QString>("name");

    QTest::newRow("none") << static_cast<int>(Types::NoneAlignment) << QStringLiteral("none");
    QTest::newRow("center") << static_cast<int>(Types::Center) << QStringLiteral("center");
    QTest::newRow("left") << static_cast<int>(Types::Left) << QStringLiteral("left");
    QTest::newRow("right") << static_cast<int>(Types::Right) << QStringLiteral("right");
    QTest::newRow("top") << static_cast<int>(Types::Top) << QStringLiteral("top");
    QTest::newRow("bottom") << static_cast<int>(Types::Bottom) << QStringLiteral("bottom");
    QTest::newRow("justify") << static_cast<int>(Types::Justify) << QStringLiteral("justify");
}

void DbusReportsTest::alignmentNames()
{
    QFETCH(int, alignment);
    QFETCH(QString, name);

    QCOMPARE(alignmentName(static_cast<Types::Alignment>(alignment)), name);
}

void DbusReportsTest::orientationNames_data()
{
    QTest::addColumn<Plasma::Types::FormFactor>("orientation");
    QTest::addColumn<QString>("name");

    QTest::newRow("planar") << Plasma::Types::Planar << QStringLiteral("planar");
    QTest::newRow("mediaCenter") << Plasma::Types::MediaCenter << QStringLiteral("mediaCenter");
    QTest::newRow("horizontal") << Plasma::Types::Horizontal << QStringLiteral("horizontal");
    QTest::newRow("vertical") << Plasma::Types::Vertical << QStringLiteral("vertical");
    QTest::newRow("application") << Plasma::Types::Application << QStringLiteral("application");
}

void DbusReportsTest::orientationNames()
{
    QFETCH(Plasma::Types::FormFactor, orientation);
    QFETCH(QString, name);

    QCOMPARE(orientationName(orientation), name);
}

void DbusReportsTest::screensGroupNames_data()
{
    QTest::addColumn<int>("group"); //! int: see viewTypeNames_data
    QTest::addColumn<QString>("name");

    QTest::newRow("single") << static_cast<int>(Types::SingleScreenGroup) << QStringLiteral("single");
    QTest::newRow("allScreens") << static_cast<int>(Types::AllScreensGroup) << QStringLiteral("allScreens");
    QTest::newRow("allSecondaryScreens") << static_cast<int>(Types::AllSecondaryScreensGroup)
                                          << QStringLiteral("allSecondaryScreens");
}

void DbusReportsTest::screensGroupNames()
{
    QFETCH(int, group);
    QFETCH(QString, name);

    QCOMPARE(screensGroupName(static_cast<Types::ScreensGroup>(group)), name);
}

void DbusReportsTest::dockRelationshipNames_data()
{
    QTest::addColumn<int>("relationship");
    QTest::addColumn<QString>("name");

    QTest::newRow("single") << static_cast<int>(DockRelationship::Single) << QStringLiteral("single");
    QTest::newRow("screensGroupOriginal")
        << static_cast<int>(DockRelationship::ScreensGroupOriginal)
        << QStringLiteral("screensGroupOriginal");
    QTest::newRow("screensGroupClone")
        << static_cast<int>(DockRelationship::ScreensGroupClone)
        << QStringLiteral("screensGroupClone");
}

void DbusReportsTest::dockRelationshipNames()
{
    QFETCH(int, relationship);
    QFETCH(QString, name);

    QCOMPARE(dockRelationshipName(static_cast<DockRelationship>(relationship)), name);
}

void DbusReportsTest::visibilityModeNames_data()
{
    QTest::addColumn<int>("mode"); //! int: see viewTypeNames_data
    QTest::addColumn<QString>("name");

    QTest::newRow("none") << static_cast<int>(Types::None) << QStringLiteral("none");
    QTest::newRow("alwaysVisible") << static_cast<int>(Types::AlwaysVisible) << QStringLiteral("alwaysVisible");
    QTest::newRow("autoHide") << static_cast<int>(Types::AutoHide) << QStringLiteral("autoHide");
    QTest::newRow("dodgeActive") << static_cast<int>(Types::DodgeActive) << QStringLiteral("dodgeActive");
    QTest::newRow("dodgeMaximized") << static_cast<int>(Types::DodgeMaximized) << QStringLiteral("dodgeMaximized");
    QTest::newRow("dodgeAllWindows") << static_cast<int>(Types::DodgeAllWindows) << QStringLiteral("dodgeAllWindows");
    QTest::newRow("windowsGoBelow") << static_cast<int>(Types::WindowsGoBelow) << QStringLiteral("windowsGoBelow");
    QTest::newRow("windowsCanCover") << static_cast<int>(Types::WindowsCanCover) << QStringLiteral("windowsCanCover");
    QTest::newRow("windowsAlwaysCover") << static_cast<int>(Types::WindowsAlwaysCover) << QStringLiteral("windowsAlwaysCover");
    QTest::newRow("sidebarOnDemand") << static_cast<int>(Types::SidebarOnDemand) << QStringLiteral("sidebarOnDemand");
    QTest::newRow("sidebarAutoHide") << static_cast<int>(Types::SidebarAutoHide) << QStringLiteral("sidebarAutoHide");
    QTest::newRow("normalWindow") << static_cast<int>(Types::NormalWindow) << QStringLiteral("normalWindow");
}

void DbusReportsTest::visibilityModeNames()
{
    QFETCH(int, mode);
    QFETCH(QString, name);

    QCOMPARE(visibilityModeName(static_cast<Types::Visibility>(mode)), name);
}

void DbusReportsTest::rectSerialization()
{
    const QJsonArray json = serializeRect(QRect(10, -20, 300, 44));
    QCOMPARE(json.count(), 4);
    QCOMPARE(json.at(0).toInt(), 10);
    QCOMPARE(json.at(1).toInt(), -20);
    QCOMPARE(json.at(2).toInt(), 300);
    QCOMPARE(json.at(3).toInt(), 44);
}

//! one fully populated record, so every field name and value type a D-Bus
//! consumer parses is pinned against docs/reference/dbus-observability-interface.md
void DbusReportsTest::recordSerialization()
{
    ViewRecord record;
    record.containmentId = 7;
    record.layout = QStringLiteral("My Layout");
    record.isCloned = true;
    record.isClonedFrom = 3;
    record.type = Types::PanelView;
    record.screen = QStringLiteral("DP-2");
    record.onPrimary = true;
    record.edge = Plasma::Types::LeftEdge;
    record.alignment = Types::Justify;
    record.visibilityMode = Types::DodgeMaximized;
    record.isHidden = true;
    record.inStartup = true;
    record.isOffScreen = true;
    record.absoluteGeometry = QRect(1, 2, 3, 4);
    record.localGeometry = QRect(5, 6, 7, 8);
    record.screenGeometry = QRect(0, 0, 2560, 1440);
    record.strutsThickness = 88;
    record.publishedStruts = QRect(0, 1352, 2560, 88);
    record.maskRect = QRect(9, 10, 11, 12);
    record.inputMask = QRect(13, 14, 15, 16);
    //! wider than inputMask: the union held across a shrink, the one state
    //! where the applied window mask and the logical band diverge
    record.appliedInputMask = QRect(13, 14, 40, 16);
    record.editMode = true;
    record.inConfigureAppletsMode = true;
    record.keyboardNavigation = true;

    const QJsonObject json = serializeViewRecord(record);

    QCOMPARE(json.value(QStringLiteral("containmentId")).toInt(), 7);
    QCOMPARE(json.value(QStringLiteral("layout")).toString(), QStringLiteral("My Layout"));
    QCOMPARE(json.value(QStringLiteral("isCloned")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("isClonedFrom")).toInt(), 3);
    QCOMPARE(json.value(QStringLiteral("type")).toString(), QStringLiteral("panel"));
    QCOMPARE(json.value(QStringLiteral("screen")).toString(), QStringLiteral("DP-2"));
    QCOMPARE(json.value(QStringLiteral("onPrimary")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("edge")).toString(), QStringLiteral("left"));
    QCOMPARE(json.value(QStringLiteral("alignment")).toString(), QStringLiteral("justify"));
    QCOMPARE(json.value(QStringLiteral("visibilityMode")).toString(), QStringLiteral("dodgeMaximized"));
    QCOMPARE(json.value(QStringLiteral("isHidden")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("inStartup")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("isOffScreen")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("absoluteGeometry")).toArray(), serializeRect(QRect(1, 2, 3, 4)));
    QCOMPARE(json.value(QStringLiteral("localGeometry")).toArray(), serializeRect(QRect(5, 6, 7, 8)));
    QCOMPARE(json.value(QStringLiteral("screenGeometry")).toArray(), serializeRect(QRect(0, 0, 2560, 1440)));
    QCOMPARE(json.value(QStringLiteral("strutsThickness")).toInt(), 88);
    QCOMPARE(json.value(QStringLiteral("publishedStruts")).toArray(), serializeRect(QRect(0, 1352, 2560, 88)));
    QCOMPARE(json.value(QStringLiteral("maskRect")).toArray(), serializeRect(QRect(9, 10, 11, 12)));

    const QJsonArray inputRegion = json.value(QStringLiteral("inputRegionRects")).toArray();
    QCOMPARE(inputRegion.count(), 1);
    QCOMPARE(inputRegion.at(0).toArray(), serializeRect(QRect(13, 14, 15, 16)));

    const QJsonArray appliedInputRegion = json.value(QStringLiteral("appliedInputRegionRects")).toArray();
    QCOMPARE(appliedInputRegion.count(), 1);
    QCOMPARE(appliedInputRegion.at(0).toArray(), serializeRect(QRect(13, 14, 40, 16)));

    QCOMPARE(json.value(QStringLiteral("editMode")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("inConfigureAppletsMode")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("keyboardNavigation")).toBool(), true);
}

void DbusReportsTest::viewRecordKeySet()
{
    const QStringList expected{
        QStringLiteral("absoluteGeometry"), QStringLiteral("alignment"),
        QStringLiteral("appliedInputRegionRects"),
        QStringLiteral("containmentId"), QStringLiteral("edge"),
        QStringLiteral("editMode"), QStringLiteral("inConfigureAppletsMode"),
        QStringLiteral("inStartup"), QStringLiteral("inputRegionRects"),
        QStringLiteral("isCloned"), QStringLiteral("isClonedFrom"),
        QStringLiteral("isHidden"), QStringLiteral("isOffScreen"),
        QStringLiteral("keyboardNavigation"),
        QStringLiteral("layout"), QStringLiteral("localGeometry"),
        QStringLiteral("maskRect"), QStringLiteral("onPrimary"),
        QStringLiteral("publishedStruts"), QStringLiteral("screen"),
        QStringLiteral("screenGeometry"), QStringLiteral("strutsThickness"),
        QStringLiteral("type"), QStringLiteral("visibilityMode")};

    QCOMPARE(sortedKeys(serializeViewRecord(ViewRecord{})), expected);
}

//! an invalid/empty input mask means "no input restriction published"
//! (Effects::setInputMask clears the window mask for those) and must read
//! as an empty array, not a degenerate rect
void DbusReportsTest::emptyInputMaskSerializesAsEmptyRegion()
{
    ViewRecord record;
    record.inputMask = QRect(); // default: invalid
    record.appliedInputMask = QRect(); // the applied mask shares the convention

    QJsonObject json = serializeViewRecord(record);
    QVERIFY(json.value(QStringLiteral("inputRegionRects")).toArray().isEmpty());
    QVERIFY(json.value(QStringLiteral("appliedInputRegionRects")).toArray().isEmpty());

    record.inputMask = QRect(0, 0, -1, -1); // the explicit clear request
    record.appliedInputMask = QRect(0, 0, -1, -1);
    json = serializeViewRecord(record);
    QVERIFY(json.value(QStringLiteral("inputRegionRects")).toArray().isEmpty());
    QVERIFY(json.value(QStringLiteral("appliedInputRegionRects")).toArray().isEmpty());
}

void DbusReportsTest::recordsSerializeAsCompactJsonArray()
{
    ViewRecord first;
    first.containmentId = 1;
    ViewRecord second;
    second.containmentId = 2;

    const QString data = serializeViewRecords({first, second});

    //! compact serialization: no newlines, per the interface doc
    QVERIFY(!data.contains(QLatin1Char('\n')));

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(data.toUtf8(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QVERIFY(document.isArray());
    QCOMPARE(document.array().count(), 2);
    QCOMPARE(document.array().at(0).toObject().value(QStringLiteral("containmentId")).toInt(), 1);
    QCOMPARE(document.array().at(1).toObject().value(QStringLiteral("containmentId")).toInt(), 2);

    QCOMPARE(serializeViewRecords({}), QStringLiteral("[]"));
}

void DbusReportsTest::configureAppletsModeRequiresLocalEditMode()
{
    static_assert(effectiveConfigureAppletsMode(true, true));
    static_assert(!effectiveConfigureAppletsMode(true, false));
    static_assert(!effectiveConfigureAppletsMode(false, true));
    static_assert(!effectiveConfigureAppletsMode(false, false));
}

void DbusReportsTest::runtimeObjectIdentitiesAreOpaqueStableAndMonotonic()
{
    RuntimeObjectIdentityRegistry identities;
    QCOMPARE(identities.trackedObjectCount(), 0);
    alignas(QObject) std::byte objectStorage[sizeof(QObject)];
    auto *const objectAddress = reinterpret_cast<QObject *>(objectStorage);
    auto *const first = std::construct_at(objectAddress);
    QObject second;

    const quint64 firstId = identities.idFor(first);
    const quint64 secondId = identities.idFor(&second);

    QVERIFY(firstId > 0);
    QVERIFY(secondId > firstId);
    QCOMPARE(identities.trackedObjectCount(), 2);
    QCOMPARE(identities.idFor(first), firstId);
    QCOMPARE(identities.tokenFor(first), QStringLiteral("object-%1").arg(firstId));
    QVERIFY(!identities.tokenFor(first).contains(QStringLiteral("0x")));

    void *const reusedAddress = first;
    std::destroy_at(first);
    QCOMPARE(identities.trackedObjectCount(), 1);
    auto *const replacement = std::construct_at(objectAddress);
    QCOMPARE(static_cast<void *>(replacement), reusedAddress);

    const quint64 replacementId = identities.idFor(replacement);
    QVERIFY(replacementId > secondId);
    QVERIFY(replacementId != firstId);
    QCOMPARE(identities.trackedObjectCount(), 2);

    std::destroy_at(replacement);
    QCOMPARE(identities.trackedObjectCount(), 1);
}

void DbusReportsTest::runtimeObjectIdentitiesRequireGuiThreadAffinity()
{
    RuntimeObjectIdentityRegistry identities;
    QObject mainThreadObject;
    QVERIFY(identities.hasRequiredThreadAffinity(&mainThreadObject));

    QThread worker;
    QObject workerContext;
    QVERIFY(workerContext.moveToThread(&worker));
    worker.start();

    QVERIFY(!identities.hasRequiredThreadAffinity(&workerContext));

    bool workerCallAccepted = true;
    bool moveBackSucceeded = false;
    QThread *const mainThread = QCoreApplication::instance()->thread();
    QVERIFY(QMetaObject::invokeMethod(
        &workerContext,
        [&]() {
            workerCallAccepted = identities.hasRequiredThreadAffinity(&mainThreadObject);
            moveBackSucceeded = workerContext.moveToThread(mainThread);
        },
        Qt::BlockingQueuedConnection));
    QVERIFY(!workerCallAccepted);
    QVERIFY(moveBackSucceeded);

    worker.quit();
    QVERIFY(worker.wait());
}

void DbusReportsTest::dockCollectionOrderingStabilizesViewAndControllerTokens()
{
    struct LiveIdentityInput {
        uint persistentDockId;
        const QObject *view;
        const QObject *controller;
    };

    QObject view7;
    QObject view30;
    QObject view41;
    QObject controller7;
    QObject sharedController;

    const auto collectAssignments = [](const QList<LiveIdentityInput> &source,
                                       RuntimeObjectIdentityRegistry &identities) {
        QList<DockCollectionOrderInput> ordering;
        ordering.reserve(source.size());
        for (qsizetype sourceIndex = 0; sourceIndex < source.size(); ++sourceIndex) {
            ordering.append(DockCollectionOrderInput{
                source.at(sourceIndex).persistentDockId,
                sourceIndex});
        }

        QStringList assignments;
        for (const qsizetype sourceIndex : orderDockCollectionByPersistentId(ordering)) {
            const auto &input = source.at(sourceIndex);
            assignments.append(QStringLiteral("%1:%2:%3")
                                   .arg(input.persistentDockId)
                                   .arg(identities.tokenFor(input.view))
                                   .arg(identities.tokenFor(input.controller)));
        }
        return assignments;
    };

    const QList<LiveIdentityInput> firstSource{
        {41, &view41, &sharedController},
        {7, &view7, &controller7},
        {30, &view30, &sharedController}};
    const QList<LiveIdentityInput> shuffledSource{
        {30, &view30, &sharedController},
        {41, &view41, &sharedController},
        {7, &view7, &controller7}};

    RuntimeObjectIdentityRegistry firstRegistry;
    RuntimeObjectIdentityRegistry shuffledRegistry;
    const QStringList firstAssignments = collectAssignments(firstSource, firstRegistry);
    const QStringList shuffledAssignments = collectAssignments(shuffledSource, shuffledRegistry);

    QCOMPARE(shuffledAssignments, firstAssignments);
    QCOMPARE(firstAssignments, (QStringList{
        QStringLiteral("7:object-1:object-2"),
        QStringLiteral("30:object-3:object-4"),
        QStringLiteral("41:object-5:object-4")}));
}

void DbusReportsTest::dockRelationshipClassification_data()
{
    QTest::addColumn<uint>("persistentDockId");
    QTest::addColumn<int>("groupId");
    QTest::addColumn<bool>("isOriginal");
    QTest::addColumn<bool>("isCloned");
    QTest::addColumn<bool>("isSingle");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<uint>("logicalDockId");
    QTest::addColumn<int>("originalDockId");
    QTest::addColumn<int>("relationship");

    QTest::newRow("standalone")
        << 7U << 7 << true << false << true << true
        << 7U << -1 << static_cast<int>(DockRelationship::Single);
    QTest::newRow("screens-group original")
        << 30U << 30 << true << false << false << true
        << 30U << -1 << static_cast<int>(DockRelationship::ScreensGroupOriginal);
    QTest::newRow("valid clone")
        << 41U << 30 << false << true << false << true
        << 30U << 30 << static_cast<int>(DockRelationship::ScreensGroupClone);

    const int ignoredRelationship = static_cast<int>(DockRelationship::Single);
    QTest::newRow("zero persistent id")
        << 0U << 0 << true << false << true << false << 0U << -1 << ignoredRelationship;
    QTest::newRow("neither original nor clone")
        << 7U << 7 << false << false << true << false << 0U << -1 << ignoredRelationship;
    QTest::newRow("both original and clone")
        << 7U << 7 << true << true << false << false << 0U << -1 << ignoredRelationship;
    QTest::newRow("original points elsewhere")
        << 30U << 7 << true << false << false << false << 0U << -1 << ignoredRelationship;
    QTest::newRow("clone marked single")
        << 41U << 30 << false << true << true << false << 0U << -1 << ignoredRelationship;
    QTest::newRow("clone missing original")
        << 41U << -1 << false << true << false << false << 0U << -1 << ignoredRelationship;
    QTest::newRow("self-referential clone cycle")
        << 41U << 41 << false << true << false << false << 0U << -1 << ignoredRelationship;
}

void DbusReportsTest::dockRelationshipClassification()
{
    QFETCH(uint, persistentDockId);
    QFETCH(int, groupId);
    QFETCH(bool, isOriginal);
    QFETCH(bool, isCloned);
    QFETCH(bool, isSingle);
    QFETCH(bool, valid);
    QFETCH(uint, logicalDockId);
    QFETCH(int, originalDockId);
    QFETCH(int, relationship);

    const auto classification = classifyDockRelationship(DockLineageInput{
        persistentDockId, groupId, isOriginal, isCloned, isSingle});
    QCOMPARE(classification.has_value(), valid);

    if (!classification) {
        return;
    }

    QCOMPARE(classification->logicalDockId, logicalDockId);
    QCOMPARE(classification->originalDockId, originalDockId);
    QCOMPARE(static_cast<int>(classification->relationship), relationship);
}

void DbusReportsTest::dockRelationshipGraphAcceptsOnlyDirectLiveOriginals()
{
    const DockLineageInput independent{7U, 7, true, false, true};
    const DockLineageInput original{30U, 30, true, false, false};
    const DockLineageInput lowClone{41U, 30, false, true, false};
    const DockLineageInput highClone{51U, 30, false, true, false};

    const auto valid = classifyDockRelationshipGraph(
        {highClone, independent, lowClone, original});
    QVERIFY(valid);
    QCOMPARE(valid->size(), 4);
    QCOMPARE(valid->value(41).logicalDockId, 30U);
    QCOMPARE(valid->value(51).relationship, DockRelationship::ScreensGroupClone);

    const QList<QList<DockLineageInput>> invalidGraphs{
        {lowClone},
        {independent, DockLineageInput{41U, 7, false, true, false}},
        {DockLineageInput{41U, 51, false, true, false},
         DockLineageInput{51U, 41, false, true, false}},
        {original, DockLineageInput{41U, 51, false, true, false},
         DockLineageInput{51U, 30, false, true, false}},
        {original, original},
        {original, DockLineageInput{0U, 30, false, true, false}}};

    for (const auto &invalid : invalidGraphs) {
        QVERIFY(!classifyDockRelationshipGraph(invalid));
    }
}

void DbusReportsTest::dockSystemSnapshotSerializesTypedRuntimeState()
{
    DockSystemViewRecord record;
    record.runtimeViewId = 19;
    record.persistentDockId = 7;
    record.logicalDockId = 3;
    record.originalDockId = 3;
    record.relationship = DockRelationship::ScreensGroupClone;
    record.screensGroup = Types::AllScreensGroup;
    record.layout = QStringLiteral("Work");
    record.screenId = 2;
    record.screen = QStringLiteral("DP-2");
    record.onPrimary = false;
    record.type = Types::DockView;
    record.edge = Plasma::Types::LeftEdge;
    record.orientation = Plasma::Types::Vertical;
    record.alignment = Types::Top;
    record.maximumLengthRatio = 0.8F;
    record.offsetRatio = 0.1F;
    record.configuredIconSize = 64;
    record.effectiveIconSize = 52;
    record.availablePrimaryLength = 900;
    record.normalThickness = 72;
    record.maximumNormalThickness = 96;
    record.windowGeometry = QRect(1, 2, 3, 4);
    record.absoluteGeometry = QRect(5, 6, 7, 8);
    record.localGeometry = QRect(9, 10, 11, 12);
    record.screenGeometry = QRect(0, 0, 2560, 1440);
    record.canvasGeometry = QRect(13, 14, 15, 16);
    record.effectsRect = QRect(17, 18, 19, 20);
    record.appletsLayoutGeometry = QRect(21, 22, 23, 24);
    record.maskRect = QRect(25, 26, 27, 28);
    record.inputMask = QRect(29, 30, 31, 32);
    record.appliedInputMask = QRect(33, 34, 35, 36);
    record.strutsThickness = 48;
    record.publishedStruts = QRect(0, 0, 48, 1440);
    record.visibilityMode = Types::DodgeActive;
    record.isHidden = true;
    record.inStartup = true;
    record.isOffScreen = true;
    record.inRelocationAnimation = true;
    record.inDelete = true;
    record.inReadyState = true;
    record.editMode = true;
    record.settingsWindowShown = true;
    record.objects.view = QStringLiteral("object-19");
    record.objects.containment = QStringLiteral("object-20");
    record.objects.configuration = QStringLiteral("object-21");
    record.objects.layout = QStringLiteral("object-22");
    record.objects.layoutController = QStringLiteral("object-23");
    record.objects.geometryController = QStringLiteral("object-24");
    record.objects.editController = QStringLiteral("object-25");
    record.objects.configWindow = QStringLiteral("object-26");

    DockSystemSnapshot snapshot;
    snapshot.snapshotSequence = 41;
    snapshot.globalConfigureAppletsMode = true;
    snapshot.views = {record};

    const QString data = serializeDockSystemSnapshot(snapshot);
    QVERIFY(!data.contains(QLatin1Char('\n')));
    const QJsonObject root = QJsonDocument::fromJson(data.toUtf8()).object();

    QCOMPARE(sortedKeys(root), (QStringList{
        QStringLiteral("globalConfigureAppletsMode"), QStringLiteral("schemaVersion"),
        QStringLiteral("snapshotSequence"), QStringLiteral("stacking"), QStringLiteral("views")}));
    QCOMPARE(root.value(QStringLiteral("schemaVersion")).toInt(), DockSystemSnapshot::SchemaVersion);
    QCOMPARE(root.value(QStringLiteral("snapshotSequence")).toString(), QStringLiteral("41"));
    QCOMPARE(root.value(QStringLiteral("globalConfigureAppletsMode")).toBool(), true);
    const QJsonValue stackingValue = root.value(QStringLiteral("stacking"));
    QCOMPARE(stackingValue.type(), QJsonValue::Object);
    const QJsonObject stacking = stackingValue.toObject();
    QCOMPARE(sortedKeys(stacking), (QStringList{
        QStringLiteral("available"), QStringLiteral("reason")}));
    QCOMPARE(stacking.value(QStringLiteral("available")).type(), QJsonValue::Bool);
    QCOMPARE(stacking.value(QStringLiteral("reason")).type(), QJsonValue::String);
    QCOMPARE(stacking.value(QStringLiteral("available")).toBool(), false);
    QVERIFY(!stacking.value(QStringLiteral("reason")).toString().isEmpty());

    const QJsonObject view = root.value(QStringLiteral("views")).toArray().at(0).toObject();
    QCOMPARE(sortedKeys(view), (QStringList{
        QStringLiteral("absoluteGeometry"), QStringLiteral("alignment"),
        QStringLiteral("appletsLayoutGeometry"),
        QStringLiteral("appliedInputMask"), QStringLiteral("availablePrimaryLength"),
        QStringLiteral("canvasGeometry"), QStringLiteral("cloneDockIds"),
        QStringLiteral("configuredIconSize"), QStringLiteral("edge"),
        QStringLiteral("editMode"), QStringLiteral("effectiveConfigureAppletsMode"),
        QStringLiteral("effectiveIconSize"), QStringLiteral("effectsRect"),
        QStringLiteral("inDelete"), QStringLiteral("inReadyState"),
        QStringLiteral("inRelocationAnimation"), QStringLiteral("inStartup"),
        QStringLiteral("inputMask"), QStringLiteral("isHidden"),
        QStringLiteral("isOffScreen"), QStringLiteral("layout"),
        QStringLiteral("localGeometry"), QStringLiteral("logicalDockId"),
        QStringLiteral("maskRect"), QStringLiteral("maximumLengthRatio"),
        QStringLiteral("maximumNormalThickness"), QStringLiteral("normalThickness"),
        QStringLiteral("objects"), QStringLiteral("offsetRatio"),
        QStringLiteral("onPrimary"), QStringLiteral("orientation"),
        QStringLiteral("originalDockId"), QStringLiteral("persistentDockId"),
        QStringLiteral("publishedStruts"), QStringLiteral("relationship"),
        QStringLiteral("runtimeViewId"), QStringLiteral("screen"),
        QStringLiteral("screenGeometry"), QStringLiteral("screenId"),
        QStringLiteral("screensGroup"), QStringLiteral("settingsWindowShown"),
        QStringLiteral("strutsThickness"), QStringLiteral("type"),
        QStringLiteral("visibilityMode"), QStringLiteral("windowGeometry")}));
    QCOMPARE(view.value(QStringLiteral("runtimeViewId")).toString(), QStringLiteral("19"));
    QCOMPARE(view.value(QStringLiteral("persistentDockId")).toInt(), 7);
    QCOMPARE(view.value(QStringLiteral("logicalDockId")).toInt(), 3);
    QCOMPARE(view.value(QStringLiteral("originalDockId")).toInt(), 3);
    QCOMPARE(view.value(QStringLiteral("relationship")).toString(), QStringLiteral("screensGroupClone"));
    QCOMPARE(view.value(QStringLiteral("screensGroup")).toString(), QStringLiteral("allScreens"));
    QCOMPARE(view.value(QStringLiteral("orientation")).toString(), QStringLiteral("vertical"));
    QCOMPARE(view.value(QStringLiteral("configuredIconSize")).toInt(), 64);
    QCOMPARE(view.value(QStringLiteral("effectiveIconSize")).toInt(), 52);
    QCOMPARE(view.value(QStringLiteral("availablePrimaryLength")).toInt(), 900);
    QCOMPARE(view.value(QStringLiteral("effectsRect")).toArray(), serializeRect(record.effectsRect));
    QCOMPARE(view.value(QStringLiteral("appletsLayoutGeometry")).toArray(), serializeRect(record.appletsLayoutGeometry));
    QCOMPARE(view.value(QStringLiteral("visibilityMode")).toString(), QStringLiteral("dodgeActive"));
    QCOMPARE(view.value(QStringLiteral("isHidden")).toBool(), true);
    QCOMPARE(view.value(QStringLiteral("inRelocationAnimation")).toBool(), true);
    QCOMPARE(view.value(QStringLiteral("inReadyState")).toBool(), true);
    QCOMPARE(view.value(QStringLiteral("effectiveConfigureAppletsMode")).toBool(), true);
    const QJsonObject objects = view.value(QStringLiteral("objects")).toObject();
    QCOMPARE(sortedKeys(objects), (QStringList{
        QStringLiteral("configWindow"), QStringLiteral("configuration"),
        QStringLiteral("containment"), QStringLiteral("editController"),
        QStringLiteral("geometryController"), QStringLiteral("layout"),
        QStringLiteral("layoutController"), QStringLiteral("view")}));
    QCOMPARE(objects.value(QStringLiteral("layoutController")).toString(),
             QStringLiteral("object-23"));
}

void DbusReportsTest::dockSystemSnapshotCanonicalizesShuffledViewsAndCloneIds()
{
    DockSystemViewRecord original;
    original.runtimeViewId = 30;
    original.persistentDockId = 30;
    original.logicalDockId = 30;
    original.relationship = DockRelationship::ScreensGroupOriginal;
    original.screensGroup = Types::AllScreensGroup;

    DockSystemViewRecord highClone;
    highClone.runtimeViewId = 51;
    highClone.persistentDockId = 51;
    highClone.logicalDockId = 30;
    highClone.originalDockId = 30;
    highClone.relationship = DockRelationship::ScreensGroupClone;
    highClone.screensGroup = Types::AllScreensGroup;

    DockSystemViewRecord lowClone = highClone;
    lowClone.runtimeViewId = 41;
    lowClone.persistentDockId = 41;

    DockSystemViewRecord independent;
    independent.runtimeViewId = 7;
    independent.persistentDockId = 7;
    independent.logicalDockId = 7;

    DockSystemSnapshot first;
    first.snapshotSequence = 9;
    first.views = {highClone, original, independent, lowClone};
    DockSystemSnapshot shuffled = first;
    shuffled.views = {lowClone, independent, highClone, original};

    QCOMPARE(serializeDockSystemSnapshot(first), serializeDockSystemSnapshot(shuffled));

    const QJsonArray views = QJsonDocument::fromJson(serializeDockSystemSnapshot(first).toUtf8())
                                 .object().value(QStringLiteral("views")).toArray();
    QCOMPARE(views.at(0).toObject().value(QStringLiteral("persistentDockId")).toInt(), 7);
    QCOMPARE(views.at(1).toObject().value(QStringLiteral("persistentDockId")).toInt(), 30);
    QCOMPARE(views.at(2).toObject().value(QStringLiteral("persistentDockId")).toInt(), 41);
    QCOMPARE(views.at(3).toObject().value(QStringLiteral("persistentDockId")).toInt(), 51);
    QCOMPARE(views.at(1).toObject().value(QStringLiteral("cloneDockIds")).toArray(),
             (QJsonArray{41, 51}));
}

void DbusReportsTest::dockSystemSnapshotKeepsConfigureModeIsolatedToEditedView()
{
    DockSystemViewRecord edited;
    edited.runtimeViewId = 1;
    edited.persistentDockId = 1;
    edited.logicalDockId = 1;
    edited.editMode = true;

    DockSystemViewRecord unrelated;
    unrelated.runtimeViewId = 2;
    unrelated.persistentDockId = 2;
    unrelated.logicalDockId = 2;
    unrelated.editMode = false;

    DockSystemSnapshot snapshot;
    snapshot.snapshotSequence = 1;
    snapshot.globalConfigureAppletsMode = true;
    snapshot.views = {unrelated, edited};

    const QJsonArray views = QJsonDocument::fromJson(serializeDockSystemSnapshot(snapshot).toUtf8())
                                 .object().value(QStringLiteral("views")).toArray();
    QCOMPARE(views.at(0).toObject().value(QStringLiteral("effectiveConfigureAppletsMode")).toBool(), true);
    QCOMPARE(views.at(1).toObject().value(QStringLiteral("effectiveConfigureAppletsMode")).toBool(), false);

    snapshot.globalConfigureAppletsMode = false;
    const QJsonArray disabled = QJsonDocument::fromJson(serializeDockSystemSnapshot(snapshot).toUtf8())
                                    .object().value(QStringLiteral("views")).toArray();
    QCOMPARE(disabled.at(0).toObject().value(QStringLiteral("effectiveConfigureAppletsMode")).toBool(), false);
    QCOMPARE(disabled.at(1).toObject().value(QStringLiteral("effectiveConfigureAppletsMode")).toBool(), false);
}

//! one fully populated applet record, pinning every field name and value
//! type of viewAppletsData() against docs/reference/dbus-observability-interface.md
void DbusReportsTest::appletRecordSerialization()
{
    AppletRecord record;
    record.id = 12;
    record.plugin = QStringLiteral("org.kde.latte.plasmoid");
    record.index = 3;
    record.geometry = QRect(4, 5, 6, 7);
    record.isExpanded = true;
    record.inScheduledDestruction = true;
    record.lockedZoom = true;
    record.colorizingBlocked = true;
    //! D21: the effective colorize decision and its reason
    record.colorizerActive = true;
    record.colorizerReason = QStringLiteral("applied");
    //! a lifted delegate: 900 is the z ConfigOverlay parks a dragged applet at
    //! over the edit chrome, the residue value the G2 readback exists to surface
    record.z = 900;

    const QJsonObject json = serializeAppletRecord(record);

    QCOMPARE(json.value(QStringLiteral("id")).toInt(), 12);
    QCOMPARE(json.value(QStringLiteral("plugin")).toString(), QStringLiteral("org.kde.latte.plasmoid"));
    QCOMPARE(json.value(QStringLiteral("index")).toInt(), 3);
    QCOMPARE(json.value(QStringLiteral("geometry")).toArray(), serializeRect(QRect(4, 5, 6, 7)));
    QCOMPARE(json.value(QStringLiteral("isExpanded")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("inScheduledDestruction")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("lockedZoom")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("colorizingBlocked")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("colorizerActive")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("colorizerReason")).toString(), QStringLiteral("applied"));
    QCOMPARE(json.value(QStringLiteral("z")).toDouble(), 900.0);
}

void DbusReportsTest::appletRecordKeySet()
{
    const QStringList expected{
        QStringLiteral("colorizerActive"), QStringLiteral("colorizerReason"),
        QStringLiteral("colorizingBlocked"), QStringLiteral("geometry"),
        QStringLiteral("id"), QStringLiteral("inScheduledDestruction"),
        QStringLiteral("index"), QStringLiteral("isExpanded"),
        QStringLiteral("lockedZoom"), QStringLiteral("plugin"),
        QStringLiteral("z")};

    QCOMPARE(sortedKeys(serializeAppletRecord(AppletRecord{})), expected);
}

//! The G2 stacking readback contract (docs/tracking/e2e-interaction-test-plan.md): the
//! z field carries the applet delegate's stacking order as a real number, and
//! its at-rest baseline is 0 (the layout default). An applet-reorder that
//! stranded the dragged delegate lifted over the edit chrome shows here as a
//! nonzero z (ConfigOverlay parks it at 900), so the 480ae30e3 "icons stuck
//! over chrome" residue is queryable instead of golden-only. This pins the
//! clean baseline and that fractional z survives serialization (wayland
//! delivers fractional geometry; z is qreal, not int).
void DbusReportsTest::appletZReportsStackingResidue()
{
    //! clean baseline: an at-rest applet reports z 0, the value an abort
    //! assertion compares back to
    QCOMPARE(serializeAppletRecord(AppletRecord{}).value(QStringLiteral("z")).toDouble(), 0.0);

    //! a stranded lift reads as its real z, not rounded away
    AppletRecord lifted;
    lifted.z = 900.5;
    QCOMPARE(serializeAppletRecord(lifted).value(QStringLiteral("z")).toDouble(), 900.5);
}

void DbusReportsTest::appletRecordsSerializeAsCompactJsonArray()
{
    AppletRecord first;
    first.id = 1;
    AppletRecord second;
    second.id = 2;

    const QString data = serializeAppletRecords({first, second});

    //! compact serialization: no newlines, per the interface doc
    QVERIFY(!data.contains(QLatin1Char('\n')));

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(data.toUtf8(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QVERIFY(document.isArray());
    QCOMPARE(document.array().count(), 2);
    QCOMPARE(document.array().at(0).toObject().value(QStringLiteral("id")).toInt(), 1);
    QCOMPARE(document.array().at(1).toObject().value(QStringLiteral("id")).toInt(), 2);

    QCOMPARE(serializeAppletRecords({}), QStringLiteral("[]"));
}

//! The G1 applet-id-order readback (docs/tracking/e2e-interaction-test-plan.md):
//! appletIdOrder() drops the justify-splitter sentinels
//! (LayoutManager::JUSTIFYSPLITTERID = -10) from a raw appletsOrder() and
//! keeps every real applet id in place, so viewAppletsOrder reports exactly
//! the applets in visual order regardless of alignment (consistent with the
//! splitter-skipping collectAppletsData already does for viewAppletsData).
void DbusReportsTest::appletIdOrderStripsSplitters()
{
    //! a non-justify view carries no splitters: the order passes through
    QCOMPARE(appletIdOrder(QList<int>{5, 7, 9}), (QList<int>{5, 7, 9}));
    //! an empty view reports an empty order, not a crash
    QCOMPARE(appletIdOrder(QList<int>{}), (QList<int>{}));
    //! justify threads two -10 sentinels marking the three zones; both are
    //! dropped and the applet ids keep their order
    QCOMPARE(appletIdOrder(QList<int>{-10, 5, 7, -10, 9}), (QList<int>{5, 7, 9}));
    //! a leading empty zone (two adjacent splitters) must not shift ids
    QCOMPARE(appletIdOrder(QList<int>{-10, -10, 5, 7, 9}), (QList<int>{5, 7, 9}));
    //! applet id 0 is a real id and survives the non-negative filter
    QCOMPARE(appletIdOrder(QList<int>{0, -10, 3}), (QList<int>{0, 3}));
}

//! The disambiguation the readback exists for: two applets of the SAME
//! plugin carry DISTINCT instance ids, and appletIdOrder keeps both in
//! visual order, so a test can tell them apart and track their order even
//! though the plugin string is identical - which the plugin string alone
//! cannot express (F2/F3/A1/A2).
void DbusReportsTest::appletIdOrderDisambiguatesSamePluginApplets()
{
    //! ids 11 and 14 stand in for two org.kde.plasma.marginsseparator
    //! instances sitting either side of a justify splitter
    const QList<int> order = appletIdOrder(QList<int>{11, -10, 14});

    QCOMPARE(order.count(), 2);
    QCOMPARE(order.at(0), 11);
    QCOMPARE(order.at(1), 14);
    QVERIFY(order.at(0) != order.at(1));
}

//! The G3 drop-marker sentinel contract (docs/tracking/e2e-interaction-test-plan.md):
//! viewDropMarkerIndex reports the drag placeholder's visual insert index, or
//! -1 when no marker is live. The trap this pins is that index 0 is the
//! LEADING insert position - a live marker, NOT "absent" - so an add/reorder
//! abort assertion can read "clean" (< 0) without confusing a leading-index
//! marker for a stranded one. Both states are proven here: the -1 (and any
//! negative) clean sentinel and the live indices including the 0 boundary.
void DbusReportsTest::dropMarkerIsLiveSeparatesLiveFromClean()
{
    //! clean: -1 is the no-marker sentinel the layout walk emits at rest and
    //! after an abort
    QVERIFY(!dropMarkerIsLive(-1));
    //! live: index 0 is the leading insert position, a real marker - the
    //! off-by-one this predicate exists to get right
    QVERIFY(dropMarkerIsLive(0));
    //! live at any interior/tail index
    QVERIFY(dropMarkerIsLive(3));
    //! defensive: only -1 is emitted, but the contract is "negative = clean",
    //! so any negative reads as no marker rather than a spurious live one
    QVERIFY(!dropMarkerIsLive(-5));
}

//! every visibility mode must survive name -> mode -> name, so the two
//! directions of the mapping can never drift apart; the settable parse
//! agrees with the full inverse everywhere except "none"
void DbusReportsTest::visibilityModeRoundTrip_data()
{
    visibilityModeNames_data();
}

void DbusReportsTest::visibilityModeRoundTrip()
{
    QFETCH(int, mode);
    QFETCH(QString, name);

    const auto expected = static_cast<Types::Visibility>(mode);
    const auto parsed = visibilityModeFromName(name);

    QVERIFY(parsed.has_value());
    QCOMPARE(static_cast<int>(*parsed), static_cast<int>(expected));
    QCOMPARE(visibilityModeName(*parsed), name);

    const auto settable = settableVisibilityModeFromName(name);

    if (expected == Types::None) {
        QVERIFY(!settable.has_value());
    } else {
        QVERIFY(settable.has_value());
        QCOMPARE(static_cast<int>(*settable), static_cast<int>(expected));
    }
}

//! degenerate mode names must be refused (nullopt), never guessed: the
//! D-Bus boundary turns nullopt into a loud qWarning refusal
void DbusReportsTest::settableVisibilityModeRefusals_data()
{
    QTest::addColumn<QString>("name");

    QTest::newRow("unknown") << QStringLiteral("dodgeEverything");
    QTest::newRow("empty") << QString();
    QTest::newRow("case mismatch") << QStringLiteral("AutoHide");
    QTest::newRow("surrounding space") << QStringLiteral(" autoHide ");
    QTest::newRow("unset-state name") << QStringLiteral("none");
    QTest::newRow("numeric") << QStringLiteral("1");
}

void DbusReportsTest::settableVisibilityModeRefusals()
{
    QFETCH(QString, name);

    QVERIFY(!settableVisibilityModeFromName(name).has_value());
}

//! one fully populated tracker record, pinning every field name and value
//! type of trackerData() against docs/reference/dbus-observability-interface.md
void DbusReportsTest::trackerRecordSerialization()
{
    TrackerRecord record;
    record.containmentId = 9;
    record.enabled = true;
    record.activeWindowTouching = true;
    record.activeWindowTouchingEdge = true;
    record.activeWindowMaximized = true;
    record.existsWindowActive = true;
    record.existsWindowTouching = true;
    record.existsWindowTouchingEdge = true;
    record.existsWindowMaximized = true;
    record.lastActiveWindowPresent = true;
    record.lastActiveWindowAppName = QStringLiteral("firefox");

    const QJsonObject json = serializeTrackerRecord(record);

    QCOMPARE(json.value(QStringLiteral("containmentId")).toInt(), 9);
    QCOMPARE(json.value(QStringLiteral("enabled")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("activeWindowTouching")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("activeWindowTouchingEdge")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("activeWindowMaximized")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("existsWindowActive")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("existsWindowTouching")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("existsWindowTouchingEdge")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("existsWindowMaximized")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("lastActiveWindowPresent")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("lastActiveWindowAppName")).toString(), QStringLiteral("firefox"));
}

void DbusReportsTest::trackerRecordKeySet()
{
    const QStringList expected{
        QStringLiteral("activeWindowMaximized"), QStringLiteral("activeWindowTouching"),
        QStringLiteral("activeWindowTouchingEdge"), QStringLiteral("containmentId"),
        QStringLiteral("enabled"), QStringLiteral("existsWindowActive"),
        QStringLiteral("existsWindowMaximized"), QStringLiteral("existsWindowTouching"),
        QStringLiteral("existsWindowTouchingEdge"), QStringLiteral("lastActiveWindowAppName"),
        QStringLiteral("lastActiveWindowPresent")};

    QCOMPARE(sortedKeys(serializeTrackerRecord(TrackerRecord{})), expected);
}

void DbusReportsTest::trackerDataSerializesAsCompactJsonObject()
{
    TrackerRecord record;
    record.containmentId = 4;

    const QString data = serializeTrackerData(record);

    //! compact serialization: no newlines, per the interface doc
    QVERIFY(!data.contains(QLatin1Char('\n')));

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(data.toUtf8(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());
    QCOMPARE(document.object().value(QStringLiteral("containmentId")).toInt(), 4);
}

//! one fully populated task record, pinning every field name and value
//! type of viewTasksData() against docs/reference/dbus-observability-interface.md -
//! note there is deliberately NO title field anywhere in the schema
void DbusReportsTest::taskRecordSerialization()
{
    TaskRecord record;
    record.appletId = 5;
    record.index = 2;
    record.appId = QStringLiteral("firefox");
    record.launcherUrl = QStringLiteral("applications:firefox.desktop");
    record.isLauncher = true;
    record.isGrouped = true;
    record.childCount = 3;
    record.isActive = true;
    record.isMinimized = true;
    record.demandsAttention = true;
    record.badge = 7;
    record.geometry = QRect(100, 200, 48, 48);

    const QJsonObject json = serializeTaskRecord(record);

    QCOMPARE(json.value(QStringLiteral("appletId")).toInt(), 5);
    QCOMPARE(json.value(QStringLiteral("index")).toInt(), 2);
    QCOMPARE(json.value(QStringLiteral("appId")).toString(), QStringLiteral("firefox"));
    QCOMPARE(json.value(QStringLiteral("launcherUrl")).toString(), QStringLiteral("applications:firefox.desktop"));
    QCOMPARE(json.value(QStringLiteral("isLauncher")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("isGrouped")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("childCount")).toInt(), 3);
    QCOMPARE(json.value(QStringLiteral("isActive")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("isMinimized")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("demandsAttention")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("badge")).toInt(), 7);
    QCOMPARE(json.value(QStringLiteral("geometry")).toArray(), serializeRect(QRect(100, 200, 48, 48)));
}

void DbusReportsTest::taskRecordKeySet()
{
    const QStringList expected{
        QStringLiteral("appId"), QStringLiteral("appletId"),
        QStringLiteral("badge"), QStringLiteral("childCount"),
        QStringLiteral("demandsAttention"), QStringLiteral("geometry"),
        QStringLiteral("index"), QStringLiteral("isActive"),
        QStringLiteral("isGrouped"), QStringLiteral("isLauncher"),
        QStringLiteral("isMinimized"), QStringLiteral("launcherUrl")};

    QCOMPARE(sortedKeys(serializeTaskRecord(TaskRecord{})), expected);
}

void DbusReportsTest::taskRecordsSerializeAsCompactJsonArray()
{
    TaskRecord first;
    first.index = 0;
    TaskRecord second;
    second.index = 1;

    const QString data = serializeTaskRecords({first, second});

    QVERIFY(!data.contains(QLatin1Char('\n')));

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(data.toUtf8(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QVERIFY(document.isArray());
    QCOMPARE(document.array().count(), 2);
    QCOMPARE(document.array().at(1).toObject().value(QStringLiteral("index")).toInt(), 1);

    QCOMPARE(serializeTaskRecords({}), QStringLiteral("[]"));
}

//! G4 (docs/tracking/e2e-interaction-test-plan.md section 9): index + appId ARE the
//! window-task order readback the F4/A3 window-task scenarios assert on. This
//! pins the contract in the serializer: the array is emitted in row order (so
//! position i carries index i), and appId is the identity that travels WITH a
//! window across a reorder while its index changes. Modelled here as a
//! tasksModel.move(0 -> 2) on three window tasks (isLauncher=false, distinct
//! appIds): before the move alpha sits at index 0; after it, the rows are
//! [beta, gamma, alpha] renumbered 0..2, so alpha now reads at index 2 - the
//! window is followed by its stable appId, not by a slot. A same-appId
//! ambiguity is why the F4/A3 window fixture spawns DISTINCT-appId windows
//! (O6); this test is the pure proof the readback expresses the order, no live
//! model needed.
void DbusReportsTest::windowTaskOrderReadbackTracksAppIdAcrossReorder()
{
    auto windowTask = [](int index, const QString &appId) {
        TaskRecord record;
        record.index = index;
        record.appId = appId;
        record.isLauncher = false; //! a real window task, not a pinned launcher
        return record;
    };

    //! the serialized array position where a given appId lands, or -1
    auto indexOfAppId = [](const QString &data, const QString &appId) {
        const QJsonArray array = QJsonDocument::fromJson(data.toUtf8()).array();
        for (int i = 0; i < array.count(); ++i) {
            const QJsonObject task = array.at(i).toObject();
            if (task.value(QStringLiteral("appId")).toString() == appId) {
                return task.value(QStringLiteral("index")).toInt();
            }
        }
        return -1;
    };

    const QString before = serializeTaskRecords(
        {windowTask(0, QStringLiteral("alpha")),
         windowTask(1, QStringLiteral("beta")),
         windowTask(2, QStringLiteral("gamma"))});

    QCOMPARE(indexOfAppId(before, QStringLiteral("alpha")), 0);
    QCOMPARE(indexOfAppId(before, QStringLiteral("beta")), 1);
    QCOMPARE(indexOfAppId(before, QStringLiteral("gamma")), 2);

    //! tasksModel.move(0 -> 2): rows become [beta, gamma, alpha], renumbered
    const QString after = serializeTaskRecords(
        {windowTask(0, QStringLiteral("beta")),
         windowTask(1, QStringLiteral("gamma")),
         windowTask(2, QStringLiteral("alpha"))});

    //! the identity followed its window: alpha moved from index 0 to index 2,
    //! beta and gamma each shifted down one - the order changed and the
    //! readback shows it by appId, which is exactly what the reorder driver
    //! and its abort assertions compare
    QCOMPARE(indexOfAppId(after, QStringLiteral("alpha")), 2);
    QCOMPARE(indexOfAppId(after, QStringLiteral("beta")), 0);
    QCOMPARE(indexOfAppId(after, QStringLiteral("gamma")), 1);

    QVERIFY(before != after);
}

void DbusReportsTest::middleClickDispatchSerializesLauncherAndTaskOperations()
{
    Tasks::MiddleClickDispatchRecord launcher;
    launcher.rowIdentity = QStringLiteral("applications:org.kde.dolphin.desktop");
    launcher.rowKind = Tasks::MiddleClickRowKind::Launcher;
    launcher.configuredAction = Tasks::Types::NewInstance;
    launcher.dispatchedOperation = Tasks::MiddleClickOperation::RequestActivate;
    launcher.sequence = 41;

    QJsonObject json = serializeMiddleClickDispatchRecord(launcher);
    QCOMPARE(sortedKeys(json),
             (QStringList{QStringLiteral("configuredAction"), QStringLiteral("dispatchedOperation"),
                          QStringLiteral("rowIdentity"), QStringLiteral("rowKind"),
                          QStringLiteral("sequence")}));
    QCOMPARE(json.value(QStringLiteral("rowIdentity")).toString(), launcher.rowIdentity);
    QCOMPARE(json.value(QStringLiteral("rowKind")).toString(), QStringLiteral("launcher"));
    QCOMPARE(json.value(QStringLiteral("configuredAction")).toString(), QStringLiteral("newInstance"));
    QCOMPARE(json.value(QStringLiteral("dispatchedOperation")).toString(), QStringLiteral("requestActivate"));
    QCOMPARE(json.value(QStringLiteral("sequence")).toInteger(), 41);

    Tasks::MiddleClickDispatchRecord task = launcher;
    task.rowKind = Tasks::MiddleClickRowKind::Task;
    task.dispatchedOperation = Tasks::MiddleClickOperation::RequestNewInstance;
    task.sequence = 42;

    json = QJsonDocument::fromJson(serializeMiddleClickDispatchData(task).toUtf8()).object();
    QCOMPARE(json.value(QStringLiteral("rowKind")).toString(), QStringLiteral("task"));
    QCOMPARE(json.value(QStringLiteral("configuredAction")).toString(), QStringLiteral("newInstance"));
    QCOMPARE(json.value(QStringLiteral("dispatchedOperation")).toString(), QStringLiteral("requestNewInstance"));
    QCOMPARE(json.value(QStringLiteral("sequence")).toInteger(), 42);
}

void DbusReportsTest::middleClickDispatchNoEventSerializesAsEmptyObject()
{
    QCOMPARE(serializeMiddleClickDispatchData(std::nullopt), QStringLiteral("{}"));
}

void DbusReportsTest::middleClickDispatchMapParsingAcceptsEveryOfferedPair_data()
{
    QTest::addColumn<int>("configuredAction");
    QTest::addColumn<int>("taskOperation");

    using Action = Tasks::Types;
    using Operation = Tasks::MiddleClickOperation;
    QTest::newRow("none") << static_cast<int>(Action::NoneAction) << static_cast<int>(Operation::None);
    QTest::newRow("close") << static_cast<int>(Action::Close) << static_cast<int>(Operation::RequestClose);
    QTest::newRow("new instance") << static_cast<int>(Action::NewInstance) << static_cast<int>(Operation::RequestNewInstance);
    QTest::newRow("toggle minimized") << static_cast<int>(Action::ToggleMinimized)
                                      << static_cast<int>(Operation::RequestToggleMinimized);
    QTest::newRow("cycle") << static_cast<int>(Action::CycleThroughTasks) << static_cast<int>(Operation::CycleOrActivate);
    QTest::newRow("toggle grouping") << static_cast<int>(Action::ToggleGrouping)
                                     << static_cast<int>(Operation::RequestToggleGrouping);
}

void DbusReportsTest::middleClickDispatchMapParsingAcceptsEveryOfferedPair()
{
    QFETCH(int, configuredAction);
    QFETCH(int, taskOperation);

    const auto action = static_cast<Tasks::Types::TaskAction>(configuredAction);
    const auto operation = static_cast<Tasks::MiddleClickOperation>(taskOperation);
    QVariantMap data = middleClickDispatchMap(QStringLiteral("applications:org.kde.dolphin.desktop"),
                                              Tasks::MiddleClickRowKind::Task,
                                              action,
                                              operation,
                                              17);

    auto record = middleClickDispatchRecordFromMap(data);
    QVERIFY(record.has_value());
    QCOMPARE(record->rowIdentity, QStringLiteral("applications:org.kde.dolphin.desktop"));
    QCOMPARE(static_cast<int>(record->rowKind), static_cast<int>(Tasks::MiddleClickRowKind::Task));
    QCOMPARE(static_cast<int>(record->configuredAction), configuredAction);
    QCOMPARE(static_cast<int>(record->dispatchedOperation), taskOperation);
    QCOMPARE(record->sequence, 17);

    //! Every offered action is valid on a launcher only with RequestActivate.
    data.insert(QStringLiteral("rowKind"), static_cast<int>(Tasks::MiddleClickRowKind::Launcher));
    data.insert(QStringLiteral("dispatchedOperation"), static_cast<int>(Tasks::MiddleClickOperation::RequestActivate));
    record = middleClickDispatchRecordFromMap(data);
    QVERIFY(record.has_value());
    QCOMPARE(static_cast<int>(record->configuredAction), configuredAction);
    QCOMPARE(static_cast<int>(record->dispatchedOperation),
             static_cast<int>(Tasks::MiddleClickOperation::RequestActivate));
}

void DbusReportsTest::middleClickDispatchMapParsingRefusesMalformedState_data()
{
    QTest::addColumn<QVariantMap>("data");

    const QVariantMap valid = middleClickDispatchMap(QStringLiteral("applications:test.desktop"),
                                                     Tasks::MiddleClickRowKind::Launcher,
                                                     Tasks::Types::NewInstance,
                                                     Tasks::MiddleClickOperation::RequestActivate,
                                                     1);

    QVariantMap malformed = valid;
    malformed.remove(QStringLiteral("rowIdentity"));
    QTest::newRow("missing identity") << malformed;

    malformed = valid;
    malformed.insert(QStringLiteral("rowKind"), 99);
    QTest::newRow("unknown row kind") << malformed;

    malformed = valid;
    malformed.insert(QStringLiteral("configuredAction"), 99);
    QTest::newRow("unknown action") << malformed;

    malformed = valid;
    malformed.insert(QStringLiteral("configuredAction"), static_cast<int>(Tasks::Types::PresentWindows));
    QTest::newRow("unoffered present-windows action") << malformed;

    malformed = valid;
    malformed.insert(QStringLiteral("configuredAction"), static_cast<int>(Tasks::Types::PreviewWindows));
    QTest::newRow("unoffered preview-windows action") << malformed;

    malformed = valid;
    malformed.insert(QStringLiteral("configuredAction"), static_cast<int>(Tasks::Types::HighlightWindows));
    QTest::newRow("unoffered highlight-windows action") << malformed;

    malformed = valid;
    malformed.insert(QStringLiteral("configuredAction"), static_cast<int>(Tasks::Types::PreviewAndHighlightWindows));
    QTest::newRow("unoffered preview-and-highlight action") << malformed;

    malformed = valid;
    malformed.insert(QStringLiteral("dispatchedOperation"), 99);
    QTest::newRow("unknown operation") << malformed;

    malformed = valid;
    malformed.insert(QStringLiteral("rowKind"), static_cast<int>(Tasks::MiddleClickRowKind::Task));
    QTest::newRow("task row with launcher operation") << malformed;

    malformed = valid;
    malformed.insert(QStringLiteral("rowKind"), static_cast<int>(Tasks::MiddleClickRowKind::Task));
    malformed.insert(QStringLiteral("configuredAction"), static_cast<int>(Tasks::Types::Close));
    malformed.insert(QStringLiteral("dispatchedOperation"), static_cast<int>(Tasks::MiddleClickOperation::RequestNewInstance));
    QTest::newRow("task action operation mismatch") << malformed;

    malformed = valid;
    malformed.insert(QStringLiteral("dispatchedOperation"), static_cast<int>(Tasks::MiddleClickOperation::RequestNewInstance));
    QTest::newRow("launcher non-activate operation") << malformed;

    malformed = valid;
    malformed.insert(QStringLiteral("sequence"), QVariant::fromValue<qint64>(0));
    QTest::newRow("nonpositive sequence") << malformed;

    malformed = valid;
    malformed.insert(QStringLiteral("sequence"), QStringLiteral("1"));
    QTest::newRow("coercible sequence type") << malformed;

    malformed = valid;
    malformed.insert(QStringLiteral("unexpected"), true);
    QTest::newRow("unexpected field") << malformed;
}

void DbusReportsTest::middleClickDispatchMapParsingRefusesMalformedState()
{
    QFETCH(QVariantMap, data);
    QVERIFY(!middleClickDispatchRecordFromMap(data).has_value());
}

void DbusReportsTest::middleClickDispatchAggregateSelectsNewestAndSerializesExactly()
{
    const QList<MiddleClickDispatchCandidate> candidates{
        {7, 100, QVariantMap{}},
        {7, 101, middleClickDispatchMap(QStringLiteral("applications:first.desktop"),
                                        Tasks::MiddleClickRowKind::Task,
                                        Tasks::Types::Close,
                                        Tasks::MiddleClickOperation::RequestClose,
                                        5)},
        {7, 102, middleClickDispatchMap(QStringLiteral("applications:newest.desktop"),
                                        Tasks::MiddleClickRowKind::Task,
                                        Tasks::Types::NewInstance,
                                        Tasks::MiddleClickOperation::RequestNewInstance,
                                        10)}};

    const auto selection = selectLatestMiddleClickDispatch(7, candidates);
    QCOMPARE(static_cast<int>(selection.refusal), static_cast<int>(MiddleClickDispatchRefusal::None));
    QVERIFY(selection.record.has_value());
    QCOMPARE(selection.record->sequence, 10);
    QCOMPARE(serializeMiddleClickDispatchData(selection.record),
             QStringLiteral("{\"configuredAction\":\"newInstance\",\"dispatchedOperation\":\"requestNewInstance\","
                            "\"rowIdentity\":\"applications:newest.desktop\",\"rowKind\":\"task\",\"sequence\":10}"));
}

void DbusReportsTest::middleClickDispatchAggregateRefusesGlobalDuplicateSequence()
{
    const auto candidate = [](int appletId, qint64 sequence) {
        return MiddleClickDispatchCandidate{
            7,
            appletId,
            middleClickDispatchMap(QStringLiteral("applications:%1.desktop").arg(appletId),
                                   Tasks::MiddleClickRowKind::Launcher,
                                   Tasks::Types::Close,
                                   Tasks::MiddleClickOperation::RequestActivate,
                                   sequence)};
    };
    const auto selection = selectLatestMiddleClickDispatch(
        7, {candidate(101, 5), candidate(102, 10), candidate(103, 5)});

    QCOMPARE(static_cast<int>(selection.refusal),
             static_cast<int>(MiddleClickDispatchRefusal::DuplicateSequence));
    QCOMPARE(selection.appletId, 103);
    QCOMPARE(selection.duplicateSequence, 5);
    QVERIFY(!selection.record.has_value());
    QCOMPARE(serializeMiddleClickDispatchData(selection.record), QStringLiteral("{}"));
}

void DbusReportsTest::middleClickDispatchAggregateRefusesMalformedCandidate()
{
    const QList<MiddleClickDispatchCandidate> candidates{
        {7, 101, middleClickDispatchMap(QStringLiteral("applications:valid.desktop"),
                                        Tasks::MiddleClickRowKind::Task,
                                        Tasks::Types::Close,
                                        Tasks::MiddleClickOperation::RequestClose,
                                        5)},
        {7, 102, QStringLiteral("non-map state")}};

    const auto selection = selectLatestMiddleClickDispatch(7, candidates);
    QCOMPARE(static_cast<int>(selection.refusal), static_cast<int>(MiddleClickDispatchRefusal::MalformedState));
    QCOMPARE(selection.appletId, 102);
    QVERIFY(!selection.record.has_value());
    QCOMPARE(serializeMiddleClickDispatchData(selection.record), QStringLiteral("{}"));
}

void DbusReportsTest::middleClickDispatchAggregateHandlesNoEventAndContainmentScope()
{
    const QVariantMap noEvent;
    auto selection = selectLatestMiddleClickDispatch(7, {{7, 101, noEvent}, {7, 102, noEvent}});
    QCOMPARE(static_cast<int>(selection.refusal), static_cast<int>(MiddleClickDispatchRefusal::None));
    QVERIFY(!selection.record.has_value());
    QCOMPARE(serializeMiddleClickDispatchData(selection.record), QStringLiteral("{}"));

    const QVariantMap valid = middleClickDispatchMap(QStringLiteral("applications:other.desktop"),
                                                     Tasks::MiddleClickRowKind::Launcher,
                                                     Tasks::Types::NewInstance,
                                                     Tasks::MiddleClickOperation::RequestActivate,
                                                     6);
    selection = selectLatestMiddleClickDispatch(7, {{8, 201, valid}});
    QCOMPARE(static_cast<int>(selection.refusal),
             static_cast<int>(MiddleClickDispatchRefusal::ContainmentMismatch));
    QCOMPARE(selection.candidateContainmentId, 8U);
    QCOMPARE(selection.appletId, 201);
    QVERIFY(!selection.record.has_value());
}

void DbusReportsTest::themeColorsModeNames_data()
{
    QTest::addColumn<int>("mode"); //! int: see viewTypeNames_data
    QTest::addColumn<QString>("name");

    QTest::newRow("plasma") << static_cast<int>(Containment::Types::PlasmaThemeColors) << QStringLiteral("plasma");
    QTest::newRow("reverse") << static_cast<int>(Containment::Types::ReverseThemeColors) << QStringLiteral("reverse");
    QTest::newRow("smart") << static_cast<int>(Containment::Types::SmartThemeColors) << QStringLiteral("smart");
    QTest::newRow("dark") << static_cast<int>(Containment::Types::DarkThemeColors) << QStringLiteral("dark");
    QTest::newRow("light") << static_cast<int>(Containment::Types::LightThemeColors) << QStringLiteral("light");
    QTest::newRow("layout") << static_cast<int>(Containment::Types::LayoutThemeColors) << QStringLiteral("layout");
}

void DbusReportsTest::themeColorsModeNames()
{
    QFETCH(int, mode);
    QFETCH(QString, name);

    QCOMPARE(themeColorsModeName(static_cast<Containment::Types::ThemeColorsGroup>(mode)), name);
}

void DbusReportsTest::windowColorsModeNames_data()
{
    QTest::addColumn<int>("mode"); //! int: see viewTypeNames_data
    QTest::addColumn<QString>("name");

    QTest::newRow("none") << static_cast<int>(Containment::Types::NoneWindowColors) << QStringLiteral("none");
    QTest::newRow("active") << static_cast<int>(Containment::Types::ActiveWindowColors) << QStringLiteral("active");
    QTest::newRow("touching") << static_cast<int>(Containment::Types::TouchingWindowColors) << QStringLiteral("touching");
}

void DbusReportsTest::windowColorsModeNames()
{
    QFETCH(int, mode);
    QFETCH(QString, name);

    QCOMPARE(windowColorsModeName(static_cast<Containment::Types::WindowColorsGroup>(mode)), name);
}

//! every valid config int must come back as its enum value, so the
//! validators cannot drift from the enums they guard
void DbusReportsTest::colorModeConfigValueRoundTrip()
{
    for (const auto mode : {Containment::Types::PlasmaThemeColors, Containment::Types::ReverseThemeColors,
                            Containment::Types::SmartThemeColors, Containment::Types::DarkThemeColors,
                            Containment::Types::LightThemeColors, Containment::Types::LayoutThemeColors}) {
        const auto parsed = themeColorsFromConfigValue(static_cast<int>(mode));
        QVERIFY(parsed.has_value());
        QCOMPARE(static_cast<int>(*parsed), static_cast<int>(mode));
    }

    for (const auto mode : {Containment::Types::NoneWindowColors, Containment::Types::ActiveWindowColors,
                            Containment::Types::TouchingWindowColors}) {
        const auto parsed = windowColorsFromConfigValue(static_cast<int>(mode));
        QVERIFY(parsed.has_value());
        QCOMPARE(static_cast<int>(*parsed), static_cast<int>(mode));
    }
}

//! the mode ints arrive from the user-editable containment config, so
//! out-of-range values must parse to nullopt (the collector turns that
//! into a loud qWarning), never be cast into a Q_UNREACHABLE switch
void DbusReportsTest::colorModeConfigValueRefusals_data()
{
    QTest::addColumn<int>("value");

    QTest::newRow("negative") << -1;
    QTest::newRow("past themeColors end") << 6;
    QTest::newRow("garbage") << 9000;
}

void DbusReportsTest::colorModeConfigValueRefusals()
{
    QFETCH(int, value);

    QVERIFY(!themeColorsFromConfigValue(value).has_value());
    //! 6 and 9000 are also outside the smaller windowColors range,
    //! together with everything below zero
    QVERIFY(!windowColorsFromConfigValue(value).has_value());
}

//! one fully populated colorizer record, pinning every field name and
//! value type of colorizerData() against the interface doc and the ledger
void DbusReportsTest::colorizerRecordSerialization()
{
    ColorizerRecord record;
    record.containmentId = 3;
    record.enabled = true;
    record.themeColors = Containment::Types::SmartThemeColors;
    record.windowColors = Containment::Types::TouchingWindowColors;
    record.colorizerPresent = true;
    record.mustBeShown = true;
    record.applyingWindowColors = true;
    record.backgroundIsBusy = true;
    record.currentBackgroundBrightness = 42.5;
    record.scheme = QStringLiteral("DarkScheme.colors");
    //! D21: the resolved colours + brightnesses
    record.applyColor = QColor(QStringLiteral("#202326"));
    record.textColor = QColor(QStringLiteral("#202326"));
    record.backgroundColor = QColor(QStringLiteral("#fcfcfc"));
    record.applyColorBrightness = 34.5;
    record.backgroundColorBrightness = 240.0;

    const QJsonObject json = serializeColorizerRecord(record);

    QCOMPARE(json.value(QStringLiteral("containmentId")).toInt(), 3);
    QCOMPARE(json.value(QStringLiteral("enabled")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("themeColorsMode")).toString(), QStringLiteral("smart"));
    QCOMPARE(json.value(QStringLiteral("windowColorsMode")).toString(), QStringLiteral("touching"));
    QCOMPARE(json.value(QStringLiteral("colorizerPresent")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("mustBeShown")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("applyingWindowColors")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("backgroundIsBusy")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("currentBackgroundBrightness")).toDouble(), 42.5);
    QCOMPARE(json.value(QStringLiteral("scheme")).toString(), QStringLiteral("DarkScheme.colors"));
    QCOMPARE(json.value(QStringLiteral("applyColor")).toString(), QStringLiteral("#202326"));
    QCOMPARE(json.value(QStringLiteral("textColor")).toString(), QStringLiteral("#202326"));
    QCOMPARE(json.value(QStringLiteral("backgroundColor")).toString(), QStringLiteral("#fcfcfc"));
    QCOMPARE(json.value(QStringLiteral("applyColorBrightness")).toDouble(), 34.5);
    QCOMPARE(json.value(QStringLiteral("backgroundColorBrightness")).toDouble(), 240.0);
    //! an unresolved (invalid) colour serialises to "" not QColor's "#000000"
    ColorizerRecord unresolved;
    QCOMPARE(serializeColorizerRecord(unresolved).value(QStringLiteral("applyColor")).toString(), QString());
}

void DbusReportsTest::colorizerRecordKeySet()
{
    const QStringList expected{
        QStringLiteral("applyColor"), QStringLiteral("applyColorBrightness"),
        QStringLiteral("applyingWindowColors"), QStringLiteral("backgroundColor"),
        QStringLiteral("backgroundColorBrightness"), QStringLiteral("backgroundIsBusy"),
        QStringLiteral("colorizerPresent"), QStringLiteral("containmentId"),
        QStringLiteral("currentBackgroundBrightness"), QStringLiteral("enabled"),
        QStringLiteral("mustBeShown"), QStringLiteral("scheme"),
        QStringLiteral("textColor"), QStringLiteral("themeColorsMode"),
        QStringLiteral("windowColorsMode")};

    QCOMPARE(sortedKeys(serializeColorizerRecord(ColorizerRecord{})), expected);

    //! the unmeasured-brightness sentinel is the Manager item's own and
    //! must survive serialization unchanged
    QCOMPARE(serializeColorizerRecord(ColorizerRecord{}).value(QStringLiteral("currentBackgroundBrightness")).toDouble(), -1000.0);
}

void DbusReportsTest::memoryUsageNames_data()
{
    QTest::addColumn<int>("memory"); //! int: see viewTypeNames_data
    QTest::addColumn<QString>("name");

    //! Current is the query sentinel memoryUsage() never returns; the
    //! mapping still covers it so the switch stays exhaustive
    QTest::newRow("current") << static_cast<int>(MemoryUsage::Current) << QStringLiteral("current");
    QTest::newRow("single") << static_cast<int>(MemoryUsage::SingleLayout) << QStringLiteral("single");
    QTest::newRow("multiple") << static_cast<int>(MemoryUsage::MultipleLayouts) << QStringLiteral("multiple");
}

void DbusReportsTest::memoryUsageNames()
{
    QFETCH(int, memory);
    QFETCH(QString, name);

    QCOMPARE(memoryUsageName(static_cast<MemoryUsage::LayoutsMemory>(memory)), name);
}

//! one fully populated layout record, pinning every field name and value
//! type of layoutsData() against docs/reference/dbus-observability-interface.md
void DbusReportsTest::layoutRecordSerialization()
{
    LayoutRecord record;
    record.name = QStringLiteral("My Layout");
    record.isActive = true;
    record.activities = QStringList{QStringLiteral("uuid-1"), QStringLiteral("uuid-2")};
    record.viewsCount = 2;

    const QJsonObject json = serializeLayoutRecord(record);

    QCOMPARE(json.value(QStringLiteral("name")).toString(), QStringLiteral("My Layout"));
    QCOMPARE(json.value(QStringLiteral("isActive")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("activities")).toArray(), QJsonArray::fromStringList(record.activities));
    QCOMPARE(json.value(QStringLiteral("viewsCount")).toInt(), 2);
}

void DbusReportsTest::layoutRecordKeySet()
{
    const QStringList expected{
        QStringLiteral("activities"), QStringLiteral("isActive"),
        QStringLiteral("name"), QStringLiteral("viewsCount")};

    QCOMPARE(sortedKeys(serializeLayoutRecord(LayoutRecord{})), expected);
}

void DbusReportsTest::layoutsDataSerialization()
{
    LayoutRecord layout;
    layout.name = QStringLiteral("My Layout");

    const QString data = serializeLayoutsData(MemoryUsage::MultipleLayouts, {layout});

    QVERIFY(!data.contains(QLatin1Char('\n')));

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(data.toUtf8(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());

    const QJsonObject json = document.object();
    QCOMPARE(sortedKeys(json), (QStringList{QStringLiteral("layouts"), QStringLiteral("memoryUsage")}));
    QCOMPARE(json.value(QStringLiteral("memoryUsage")).toString(), QStringLiteral("multiple"));
    QCOMPARE(json.value(QStringLiteral("layouts")).toArray().count(), 1);
    QCOMPARE(json.value(QStringLiteral("layouts")).toArray().at(0).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("My Layout"));

    //! no layouts still answers the full schema
    const QJsonDocument empty = QJsonDocument::fromJson(serializeLayoutsData(MemoryUsage::SingleLayout, {}).toUtf8());
    QCOMPARE(empty.object().value(QStringLiteral("memoryUsage")).toString(), QStringLiteral("single"));
    QVERIFY(empty.object().value(QStringLiteral("layouts")).toArray().isEmpty());
}

void DbusReportsTest::screenRecordSerialization()
{
    ScreenRecord record;
    record.id = 11;
    record.name = QStringLiteral("Virtual-2");
    record.geometry = QRect(1600, 0, 1600, 1000);
    record.isActive = true;
    record.isPrimary = false;

    const QJsonObject json = serializeScreenRecord(record);

    QCOMPARE(json.value(QStringLiteral("id")).toInt(), 11);
    QCOMPARE(json.value(QStringLiteral("name")).toString(), QStringLiteral("Virtual-2"));
    QCOMPARE(json.value(QStringLiteral("geometry")).toArray(), serializeRect(record.geometry));
    QCOMPARE(json.value(QStringLiteral("isActive")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("isPrimary")).toBool(), false);
}

void DbusReportsTest::screenRecordKeySet()
{
    const QStringList expected{
        QStringLiteral("geometry"), QStringLiteral("id"),
        QStringLiteral("isActive"), QStringLiteral("isPrimary"),
        QStringLiteral("name")};

    QCOMPARE(sortedKeys(serializeScreenRecord(ScreenRecord{})), expected);
}

void DbusReportsTest::screensDataSerializesAsCompactJsonArray()
{
    ScreenRecord primary;
    primary.id = 10;
    primary.name = QStringLiteral("Virtual-1");
    primary.geometry = QRect(0, 0, 1600, 1000);
    primary.isActive = true;
    primary.isPrimary = true;

    ScreenRecord secondary;
    secondary.id = 11;
    secondary.name = QStringLiteral("Virtual-2");
    secondary.geometry = QRect(1600, 0, 1600, 1000);
    secondary.isActive = true;
    secondary.isPrimary = false;

    const QString data = serializeScreensData({primary, secondary});

    //! one line: a busctl-piped consumer parses it whole
    QVERIFY(!data.contains(QLatin1Char('\n')));

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(data.toUtf8(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QVERIFY(document.isArray());

    const QJsonArray array = document.array();
    QCOMPARE(array.count(), 2);
    QCOMPARE(array.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Virtual-1"));
    QCOMPARE(array.at(0).toObject().value(QStringLiteral("isPrimary")).toBool(), true);
    QCOMPARE(array.at(1).toObject().value(QStringLiteral("id")).toInt(), 11);
    QCOMPARE(array.at(1).toObject().value(QStringLiteral("isPrimary")).toBool(), false);

    //! no screens still answers a well-formed empty array
    QCOMPARE(serializeScreensData({}), QStringLiteral("[]"));
}

//! viewConfigData/appletConfigData carry the user's own dock config values,
//! whose types are int/double/bool/string/stringlist - each maps to a JSON
//! scalar so the audit's snapshot-diff compares equal-means-equal.
void DbusReportsTest::configValueScalarMapping()
{
    QCOMPARE(configValueToJson(QVariant(90)).toInt(), 90);
    QCOMPARE(configValueToJson(QVariant(0.5)).toDouble(), 0.5);
    QCOMPARE(configValueToJson(QVariant(true)).toBool(), true);
    QCOMPARE(configValueToJson(QVariant(QStringLiteral("org.kde.latte.default"))).toString(),
             QStringLiteral("org.kde.latte.default"));
    //! a zero and an empty string are real values, never collapsed to null
    QCOMPARE(configValueToJson(QVariant(0)).type(), QJsonValue::Double);
    QCOMPARE(configValueToJson(QVariant(QString())).type(), QJsonValue::String);
}

//! a config value with no JSON scalar (a QColor, e.g. shadowColor) becomes a
//! canonical string, NOT the JSON null QJsonValue::fromVariant hands back -
//! otherwise every color would compare equal in the diff (a false PASS)
void DbusReportsTest::configValueColorFallsBackToCanonicalString()
{
    const QJsonValue red = configValueToJson(QVariant::fromValue(QColor(255, 0, 0)));
    const QJsonValue blue = configValueToJson(QVariant::fromValue(QColor(0, 0, 255)));

    QCOMPARE(red.type(), QJsonValue::String);
    QVERIFY(!red.toString().isEmpty());
    //! two different colors must serialize to two different strings
    QVERIFY(red.toString() != blue.toString());
}

void DbusReportsTest::configMapSerializesEveryKey()
{
    QVariantMap config;
    config.insert(QStringLiteral("maxLength"), 100);
    config.insert(QStringLiteral("minLength"), 30);
    config.insert(QStringLiteral("titleTooltips"), false);

    const QJsonObject json = serializeConfigMap(config);

    QCOMPARE(json.keys().count(), 3);
    QCOMPARE(json.value(QStringLiteral("maxLength")).toInt(), 100);
    QCOMPARE(json.value(QStringLiteral("minLength")).toInt(), 30);
    QCOMPARE(json.value(QStringLiteral("titleTooltips")).toBool(), false);

    //! an empty config is a well-formed empty object, not a crash
    QVERIFY(serializeConfigMap({}).isEmpty());
}

void DbusReportsTest::viewLiveRecordSerialization()
{
    ViewLiveRecord record;
    record.byPassWM = true;
    record.isPreferredForShortcuts = true;
    record.visibilityTimerShow = 100;
    record.visibilityTimerHide = 700;
    record.visibilityEnableKWinEdges = true;
    record.visibilityRaiseOnDesktop = true;
    record.visibilityRaiseOnActivity = true;
    record.indicatorPresent = true;
    record.indicatorEnabled = true;
    record.indicatorType = QStringLiteral("org.kde.latte.default");
    record.indicatorCustomType = QStringLiteral("");
    record.inAdvancedModeForEditSettings = true;
    record.settingsWindowScaleWidth = 0.96;
    record.settingsWindowScaleHeight = 0.85;

    const QJsonObject json = serializeViewLiveRecord(record);

    QCOMPARE(json.value(QStringLiteral("byPassWM")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("isPreferredForShortcuts")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("visibilityTimerShow")).toInt(), 100);
    QCOMPARE(json.value(QStringLiteral("visibilityTimerHide")).toInt(), 700);
    QCOMPARE(json.value(QStringLiteral("visibilityEnableKWinEdges")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("visibilityRaiseOnDesktop")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("visibilityRaiseOnActivity")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("indicatorPresent")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("indicatorEnabled")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("indicatorType")).toString(), QStringLiteral("org.kde.latte.default"));
    QCOMPARE(json.value(QStringLiteral("indicatorCustomType")).toString(), QString());
    QCOMPARE(json.value(QStringLiteral("inAdvancedModeForEditSettings")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("settingsWindowScaleWidth")).toDouble(), 0.96);
    QCOMPARE(json.value(QStringLiteral("settingsWindowScaleHeight")).toDouble(), 0.85);
}

void DbusReportsTest::viewLiveRecordKeySet()
{
    const QStringList expected{
        QStringLiteral("byPassWM"), QStringLiteral("inAdvancedModeForEditSettings"),
        QStringLiteral("indicatorCustomType"),
        QStringLiteral("indicatorEnabled"), QStringLiteral("indicatorPresent"),
        QStringLiteral("indicatorType"), QStringLiteral("isPreferredForShortcuts"),
        QStringLiteral("settingsWindowScaleHeight"), QStringLiteral("settingsWindowScaleWidth"),
        QStringLiteral("visibilityEnableKWinEdges"), QStringLiteral("visibilityRaiseOnActivity"),
        QStringLiteral("visibilityRaiseOnDesktop"), QStringLiteral("visibilityTimerHide"),
        QStringLiteral("visibilityTimerShow")};

    QCOMPARE(sortedKeys(serializeViewLiveRecord(ViewLiveRecord{})), expected);
}

//! the viewConfigData() top-level shape a consumer parses: a compact object
//! with containmentId, the config values object, and the live "view" object
void DbusReportsTest::configDataShape()
{
    QVariantMap config;
    config.insert(QStringLiteral("maxLength"), 90);

    ViewLiveRecord live;
    live.byPassWM = true;

    const QString data = serializeConfigData(12u, config, live);

    QVERIFY(!data.contains(QLatin1Char('\n')));

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(data.toUtf8(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());

    const QJsonObject json = document.object();
    QCOMPARE(sortedKeys(json), (QStringList{QStringLiteral("config"), QStringLiteral("containmentId"), QStringLiteral("view")}));
    QCOMPARE(json.value(QStringLiteral("containmentId")).toInt(), 12);
    QCOMPARE(json.value(QStringLiteral("config")).toObject().value(QStringLiteral("maxLength")).toInt(), 90);
    QCOMPARE(json.value(QStringLiteral("view")).toObject().value(QStringLiteral("byPassWM")).toBool(), true);
}

//! the appletConfigData() top-level shape: a compact object keyed by
//! containment id, applet id and plugin, carrying the applet's config values
void DbusReportsTest::appletConfigDataShape()
{
    QVariantMap config;
    config.insert(QStringLiteral("showInfoBadge"), true);

    const QString data = serializeAppletConfigData(12u, 4, QStringLiteral("org.kde.latte.plasmoid"), config);

    QVERIFY(!data.contains(QLatin1Char('\n')));

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(data.toUtf8(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());

    const QJsonObject json = document.object();
    QCOMPARE(sortedKeys(json), (QStringList{QStringLiteral("appletId"), QStringLiteral("config"),
                                            QStringLiteral("containmentId"), QStringLiteral("plugin")}));
    QCOMPARE(json.value(QStringLiteral("appletId")).toInt(), 4);
    QCOMPARE(json.value(QStringLiteral("plugin")).toString(), QStringLiteral("org.kde.latte.plasmoid"));
    QCOMPARE(json.value(QStringLiteral("config")).toObject().value(QStringLiteral("showInfoBadge")).toBool(), true);
}

QTEST_GUILESS_MAIN(DbusReportsTest)
#include "dbusreportstest.moc"
