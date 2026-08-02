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
#include <array>
#include <cstddef>
#include <limits>
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
    void linkPlacementNames_data();
    void linkPlacementNames();
    void transitionNames();
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
    void dockRelationshipGraphAcceptsOnlyDirectLiveRoots();
    void dockSystemSnapshotSerializesTypedRuntimeState();
    void dockSystemSnapshotPinsNullableWireStates();
    void dockSystemSnapshotPreservesFractionalTransitionGeometry();
    void windowTouchAuthorityCountsRejectDivergence();
    void dockSystemSnapshotRejectsTransitionDisagreement();
    void dockSystemSnapshotRejectsReservationDisagreement();
    void dockSystemSnapshotCanonicalizesShuffledViewsAndLinkedIds();
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

static void requireJsonType(const QJsonObject &json,
                            const QString &key,
                            QJsonValue::Type expected)
{
    const auto actual = json.value(key).type();
    QVERIFY2(actual == expected,
             qPrintable(QStringLiteral("%1 has JSON type %2, expected %3")
                            .arg(key).arg(static_cast<int>(actual)).arg(static_cast<int>(expected))));
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

static DockSystemViewRecord stableBottomTransitionRecord()
{
    DockSystemViewRecord record;
    record.runtimeViewId = 7;
    record.persistentDockId = 7;
    record.logicalDockId = 7;
    record.type = Types::PanelView;
    record.edge = Plasma::Types::BottomEdge;
    record.normalThickness = 40;
    record.maximumNormalThickness = 47;
    record.screenEdgeMargin = 7;
    record.presentedScreenEdgeGap = 3;
    record.windowGeometry = QRect(100, 953, 300, 47);
    record.absoluteGeometry = QRect(100, 960, 300, 40);
    record.screenGeometry = QRect(0, 0, 1920, 1000);
    record.surfaceGeometry = QRect(100, 953, 300, 47);
    record.layerShellPresent = true;
    record.layerShellMargins = QMargins{};
    record.floatingGapConfigured = true;
    record.floatingPanelConfigured = true;
    record.floatingPanelEligible = true;
    record.attachOnWindowTouchConfigured = false;
    record.attachmentDeferredByPointer = false;
    record.touchingWindowCount = 0;
    record.visibilityMode = Types::AlwaysVisible;
    record.transitionTarget =
        DockTransitionTarget::Floated;
    record.transitionProgress = 0.375;
    record.transitionAnimationDuration = 200;
    record.transitionPhase =
        DockTransitionPhase::Floating;
    record.transitionDirection =
        DockTransitionDirection::TowardFloated;
    record.transitionRunning = true;
    record.transitionGeometryPresent = true;
    record.transitionGeometryRevision = 11;
    record.stableCanvasGeometry =
        QRect(100, 953, 300, 47);
    record.attachedPresentationGeometry =
        QRect(0, 7, 300, 40);
    record.floatedPresentationGeometry =
        QRect(0, 0, 300, 40);
    record.currentVisibleGeometry =
        QRectF(0.0, 4.375, 300.0, 40.0);
    record.computedPaintMaskGeometry =
        record.currentVisibleGeometry;
    record.computedInputBridgeGeometry =
        QRectF(0.0, 4.375, 300.0, 42.625);
    record.effectsRect = QRect(0, 4, 300, 41);
    record.maskRect = record.effectsRect;
    record.inputMask = QRect(0, 4, 300, 43);
    record.appliedInputMask = record.inputMask;
    record.floatingDamageMaskGeneration = 31;
    record.enabledBorders = {
        QStringLiteral("top"),
        QStringLiteral("right"),
        QStringLiteral("bottom"),
        QStringLiteral("left"),
    };
    record.shadowEnabledBorders =
        record.enabledBorders;
    record.shadowPaddingOffsets =
        QMargins(0, -4, 0, -2);
    record.floatingAppletPopupsPreferred = true;
    record.floatingAnchorRevision = 29;
    record.contentTranslation =
        QPointF(0.0, 4.375);
    record.stableTriggerGeometry =
        QRect(100, 952, 300, 47);
    record.stableAppletMeasurementBounds =
        QRect(0, 0, 300, 40);
    record.stablePrimaryAxisStart = 100;
    record.stablePrimaryAxisLength = 300;
    record.stableLayerShellMargin = 0;
    record.surfaceGeometryPublicationRevision = 17;
    record.layerShellConfigureRequestRevision = 23;
    record.requestedReservationDepth = 40;
    record.objects.geometryController =
        QStringLiteral("object-1");
    record.objects.transitionController =
        QStringLiteral("object-2");
    record.objects.windowTouchTracker =
        QStringLiteral("object-3");
    return record;
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

    QTest::newRow("independent")
        << static_cast<int>(DockRelationship::Independent) << QStringLiteral("independent");
    QTest::newRow("linkedRoot")
        << static_cast<int>(DockRelationship::LinkedRoot) << QStringLiteral("linkedRoot");
    QTest::newRow("linkedMember")
        << static_cast<int>(DockRelationship::LinkedMember) << QStringLiteral("linkedMember");
}

void DbusReportsTest::dockRelationshipNames()
{
    QFETCH(int, relationship);
    QFETCH(QString, name);

    QCOMPARE(dockRelationshipName(static_cast<DockRelationship>(relationship)), name);
}

void DbusReportsTest::linkPlacementNames_data()
{
    QTest::addColumn<int>("placement");
    QTest::addColumn<QString>("name");

    QTest::newRow("screen group derived")
        << static_cast<int>(Data::View::LinkPlacement::ScreenGroupDerived)
        << QStringLiteral("screenGroupDerived");
    QTest::newRow("explicit target")
        << static_cast<int>(Data::View::LinkPlacement::ExplicitTarget)
        << QStringLiteral("explicitTarget");
}

void DbusReportsTest::linkPlacementNames()
{
    QFETCH(int, placement);
    QFETCH(QString, name);

    QCOMPARE(linkPlacementName(static_cast<Data::View::LinkPlacement>(placement)), name);
}

void DbusReportsTest::transitionNames()
{
    QCOMPARE(
        transitionTargetName(
            DockTransitionTarget::Attached),
        QStringLiteral("attached"));
    QCOMPARE(
        transitionTargetName(
            DockTransitionTarget::Floated),
        QStringLiteral("floated"));

    QCOMPARE(
        transitionPhaseName(
            DockTransitionPhase::Resting),
        QStringLiteral("resting"));
    QCOMPARE(
        transitionPhaseName(
            DockTransitionPhase::Attaching),
        QStringLiteral("attaching"));
    QCOMPARE(
        transitionPhaseName(
            DockTransitionPhase::Floating),
        QStringLiteral("floating"));

    QCOMPARE(
        transitionDirectionName(
            DockTransitionDirection::None),
        QStringLiteral("none"));
    QCOMPARE(
        transitionDirectionName(
            DockTransitionDirection::TowardAttached),
        QStringLiteral("towardAttached"));
    QCOMPARE(
        transitionDirectionName(
            DockTransitionDirection::TowardFloated),
        QStringLiteral("towardFloated"));
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

    const QJsonArray margins = serializeMargins(QMargins(1, 2, 3, 4));
    QCOMPARE(margins, QJsonArray({1, 2, 3, 4}));
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
    record.linkedEditHighlight = true;
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
    QCOMPARE(json.value(QStringLiteral("linkedEditHighlight")).toBool(), true);
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
        QStringLiteral("layout"), QStringLiteral("linkedEditHighlight"),
        QStringLiteral("localGeometry"),
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
    QTest::addColumn<int>("linkPlacement");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<uint>("logicalDockId");
    QTest::addColumn<int>("originalDockId");
    QTest::addColumn<int>("relationship");

    QTest::newRow("standalone")
        << 7U << 7 << true << false << true
        << static_cast<int>(Data::View::LinkPlacement::ScreenGroupDerived) << true
        << 7U << -1 << static_cast<int>(DockRelationship::Independent);
    QTest::newRow("linked root")
        << 30U << 30 << true << false << false
        << static_cast<int>(Data::View::LinkPlacement::ScreenGroupDerived) << true
        << 30U << -1 << static_cast<int>(DockRelationship::LinkedRoot);
    QTest::newRow("screen-group member")
        << 41U << 30 << false << true << false
        << static_cast<int>(Data::View::LinkPlacement::ScreenGroupDerived) << true
        << 30U << 30 << static_cast<int>(DockRelationship::LinkedMember);
    QTest::newRow("explicit member")
        << 51U << 30 << false << true << false
        << static_cast<int>(Data::View::LinkPlacement::ExplicitTarget) << true
        << 30U << 30 << static_cast<int>(DockRelationship::LinkedMember);

    const int derived = static_cast<int>(Data::View::LinkPlacement::ScreenGroupDerived);
    const int ignoredRelationship = static_cast<int>(DockRelationship::Independent);
    QTest::newRow("zero persistent id")
        << 0U << 0 << true << false << true << derived << false
        << 0U << -1 << ignoredRelationship;
    QTest::newRow("neither original nor clone")
        << 7U << 7 << false << false << true << derived << false
        << 0U << -1 << ignoredRelationship;
    QTest::newRow("both original and clone")
        << 7U << 7 << true << true << false << derived << false
        << 0U << -1 << ignoredRelationship;
    QTest::newRow("original points elsewhere")
        << 30U << 7 << true << false << false << derived << false
        << 0U << -1 << ignoredRelationship;
    QTest::newRow("original has explicit placement")
        << 30U << 30 << true << false << false
        << static_cast<int>(Data::View::LinkPlacement::ExplicitTarget) << false
        << 0U << -1 << ignoredRelationship;
    QTest::newRow("clone marked single")
        << 41U << 30 << false << true << true << derived << false
        << 0U << -1 << ignoredRelationship;
    QTest::newRow("clone missing original")
        << 41U << -1 << false << true << false << derived << false
        << 0U << -1 << ignoredRelationship;
    QTest::newRow("self-referential clone cycle")
        << 41U << 41 << false << true << false << derived << false
        << 0U << -1 << ignoredRelationship;
    QTest::newRow("invalid placement enum")
        << 41U << 30 << false << true << false << 99 << false
        << 0U << -1 << ignoredRelationship;
}

void DbusReportsTest::dockRelationshipClassification()
{
    QFETCH(uint, persistentDockId);
    QFETCH(int, groupId);
    QFETCH(bool, isOriginal);
    QFETCH(bool, isCloned);
    QFETCH(bool, isSingle);
    QFETCH(int, linkPlacement);
    QFETCH(bool, valid);
    QFETCH(uint, logicalDockId);
    QFETCH(int, originalDockId);
    QFETCH(int, relationship);

    const auto classification = classifyDockRelationship(DockLineageInput{
        persistentDockId,
        groupId,
        isOriginal,
        isCloned,
        isSingle,
        static_cast<Data::View::LinkPlacement>(linkPlacement)});
    QCOMPARE(classification.has_value(), valid);

    if (!classification) {
        return;
    }

    QCOMPARE(classification->logicalDockId, logicalDockId);
    QCOMPARE(classification->originalDockId, originalDockId);
    QCOMPARE(static_cast<int>(classification->relationship), relationship);
}

void DbusReportsTest::dockRelationshipGraphAcceptsOnlyDirectLiveRoots()
{
    constexpr auto derived = Data::View::LinkPlacement::ScreenGroupDerived;
    constexpr auto explicitTarget = Data::View::LinkPlacement::ExplicitTarget;
    const DockLineageInput independent{7U, 7, true, false, true, derived};
    const DockLineageInput original{30U, 30, true, false, false, derived};
    const DockLineageInput lowMember{41U, 30, false, true, false, derived};
    const DockLineageInput highMember{51U, 30, false, true, false, explicitTarget};

    const auto valid = classifyDockRelationshipGraph(
        {highMember, independent, lowMember, original});
    QVERIFY(valid);
    QCOMPARE(valid->size(), 4);
    QCOMPARE(valid->value(41).logicalDockId, 30U);
    QCOMPARE(valid->value(51).relationship, DockRelationship::LinkedMember);
    QVERIFY(valid->value(41).linkPlacement == derived);
    QVERIFY(valid->value(51).linkPlacement == explicitTarget);

    const QList<QList<DockLineageInput>> invalidGraphs{
        {lowMember},
        {independent, DockLineageInput{41U, 7, false, true, false, derived}},
        {DockLineageInput{41U, 51, false, true, false, derived},
         DockLineageInput{51U, 41, false, true, false, explicitTarget}},
        {original, DockLineageInput{41U, 51, false, true, false, derived},
         DockLineageInput{51U, 30, false, true, false, explicitTarget}},
        {original, original},
        {original, DockLineageInput{0U, 30, false, true, false, derived}}};

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
    record.relationship = DockRelationship::LinkedMember;
    record.linkPlacement = Data::View::LinkPlacement::ExplicitTarget;
    record.screensGroup = Types::SingleScreenGroup;
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
    record.screenEdgeMargin = 8;
    record.presentedScreenEdgeGap = 6;
    record.screenEdgeBackend = QStringLiteral("kwinAutoHide");
    record.screenEdgeArmed = false;
    record.screenEdgeRegistered = true;
    record.compositorScreenEdgeSupported = true;
    record.visibilityContainsMouse = true;
    record.windowGeometry = QRect(1, 2, 3, 4);
    record.absoluteGeometry = QRect(5, 6, 7, 8);
    record.localGeometry = QRect(9, 10, 11, 12);
    record.screenGeometry = QRect(0, 0, 2560, 1440);
    record.surfaceGeometry = QRect(0, 40, 384, 1360);
    record.canvasGeometry = QRect(13, 14, 15, 16);
    record.effectsRect = QRect(17, 18, 19, 20);
    record.appletsLayoutGeometry = QRect(21, 22, 23, 24);
    record.maskRect = QRect(25, 26, 27, 28);
    record.inputMask = QRect(29, 30, 31, 32);
    record.appliedInputMask = QRect(33, 34, 35, 36);
    record.enabledBorders = {
        QStringLiteral("top"),
        QStringLiteral("left"),
    };
    record.shadowEnabledBorders =
        record.enabledBorders;
    record.shadowPaddingOffsets =
        QMargins(-5, -6, -7, -8);
    record.floatingAppletPopupsPreferred = true;
    record.floatingAnchorRevision =
        std::numeric_limits<quint64>::max() - 3;
    record.strutsThickness = 48;
    record.publishedStruts = QRect(0, 0, 48, 1440);
    record.layerShellPresent = true;
    record.layerShellAnchors = {
        QStringLiteral("top"),
        QStringLiteral("bottom"),
        QStringLiteral("left")};
    record.layerShellMargins = QMargins(0, 40, 0, 40);
    record.layerShellExclusiveEdge = QStringLiteral("left");
    record.layerShellExclusiveZone = 48;
    record.reservationSurfacePresent = true;
    record.reservationOutputId = 10;
    record.reservationEdge =
        Plasma::Types::LeftEdge;
    record.reservationContributionDepth = 40;
    record.reservationPublishedDepth = 48;
    record.reservationGroupMemberCount = 2;
    record.reservationGroupGeneration = 53;
    record.reservationContributorDockIds = {7, 9};
    record.reservationGeometry = QRect(0, 40, 48, 1360);
    record.reservationWindowGeometry = QRect(0, 40, 1, 1360);
    record.reservationLayerShellAnchors = {
        QStringLiteral("top"),
        QStringLiteral("bottom"),
        QStringLiteral("left")};
    record.reservationLayerShellMargins = QMargins(0, 40, 0, -40);
    record.reservationLayerShellExclusiveEdge = QStringLiteral("left");
    record.reservationLayerShellExclusiveZone = 48;
    record.floatingGapConfigured = true;
    record.floatingPanelConfigured = true;
    record.floatingPanelEligible = true;
    record.attachOnWindowTouchConfigured = true;
    record.attachmentWaitsForPointerExitConfigured = true;
    record.pointerInsideView = true;
    record.attachmentDeferredByPointer = true;
    record.touchingWindowCount = 2;
    record.windowTouchGeometryRoleType =
        QStringLiteral("QRect");
    record.transitionTarget =
        DockTransitionTarget::Floated;
    record.transitionProgress = 0.375;
    record.transitionAnimationDuration = 200;
    record.transitionPhase =
        DockTransitionPhase::Floating;
    record.transitionDirection =
        DockTransitionDirection::TowardFloated;
    record.transitionRunning = true;
    record.transitionGeometryPresent = true;
    record.transitionGeometryRevision =
        std::numeric_limits<quint64>::max() - 2;
    record.stableCanvasGeometry =
        QRect(1, 2, 72, 300);
    record.attachedPresentationGeometry =
        QRect(8, 0, 64, 300);
    record.floatedPresentationGeometry =
        QRect(0, 0, 64, 300);
    record.currentVisibleGeometry =
        QRectF(5.0, 0.0, 64.0, 300.0);
    record.computedPaintMaskGeometry =
        QRectF(5.0, 0.0, 64.0, 300.0);
    record.computedInputBridgeGeometry =
        QRectF(5.0, 0.0, 67.0, 300.0);
    record.contentTranslation =
        QPointF(5.0, 0.0);
    record.stableTriggerGeometry =
        QRect(1, 2, 65, 300);
    record.stableAppletMeasurementBounds =
        QRect(0, 0, 64, 300);
    record.stablePrimaryAxisStart = 2;
    record.stablePrimaryAxisLength = 300;
    record.stableLayerShellMargin = 0;
    record.surfaceGeometryPublicationRevision =
        std::numeric_limits<quint64>::max();
    record.layerShellConfigureRequestRevision =
        std::numeric_limits<quint64>::max() - 1;
    record.requestedReservationDepth = 64;
    record.visibilityMode = Types::DodgeActive;
    record.isHidden = true;
    record.inStartup = true;
    record.isOffScreen = true;
    record.inRelocationAnimation = true;
    record.inRelocationShowing = true;
    record.geometrySettled = true;
    record.relocationGeneration = 17;
    record.appliedRelocationGeneration = 16;
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
    record.objects.reservationPublisher = QStringLiteral("object-27");
    record.objects.transitionController =
        QStringLiteral("object-28");
    record.objects.windowTouchTracker =
        QStringLiteral("object-29");

    DockSystemSnapshot snapshot;
    snapshot.snapshotSequence = 41;
    snapshot.globalConfigureAppletsMode = true;
    snapshot.reservationStateGeneration = 53;
    DockReservationGroupRecord reservationGroup;
    reservationGroup.outputId = 10;
    reservationGroup.edge = Plasma::Types::LeftEdge;
    reservationGroup.generation = 53;
    reservationGroup.publishedDepth = 48;
    reservationGroup.contributorDockIds = {9, 7};
    reservationGroup.geometry = record.reservationGeometry;
    reservationGroup.windowGeometry =
        record.reservationWindowGeometry;
    reservationGroup.layerShellPresent = true;
    reservationGroup.layerShellAnchors =
        record.reservationLayerShellAnchors;
    reservationGroup.layerShellMargins =
        record.reservationLayerShellMargins;
    reservationGroup.layerShellExclusiveEdge =
        record.reservationLayerShellExclusiveEdge;
    reservationGroup.layerShellExclusiveZone =
        record.reservationLayerShellExclusiveZone;
    reservationGroup.publisher =
        record.objects.reservationPublisher;
    snapshot.reservationGroups = {reservationGroup};
    snapshot.views = {record};

    const QString data = serializeDockSystemSnapshot(snapshot);
    QVERIFY(!data.contains(QLatin1Char('\n')));
    const QJsonObject root = QJsonDocument::fromJson(data.toUtf8()).object();

    QCOMPARE(sortedKeys(root), (QStringList{
        QStringLiteral("globalConfigureAppletsMode"),
        QStringLiteral("reservationGroups"),
        QStringLiteral("reservationStateGeneration"),
        QStringLiteral("schemaVersion"),
        QStringLiteral("snapshotSequence"),
        QStringLiteral("stacking"),
        QStringLiteral("views")}));
    QCOMPARE(DockSystemSnapshot::SchemaVersion, 11);
    QCOMPARE(root.value(QStringLiteral("schemaVersion")).toInt(), 11);
    QCOMPARE(root.value(QStringLiteral("snapshotSequence")).toString(), QStringLiteral("41"));
    QCOMPARE(
        root.value(
            QStringLiteral("reservationStateGeneration")).toString(),
        QStringLiteral("53"));
    QCOMPARE(root.value(QStringLiteral("globalConfigureAppletsMode")).toBool(), true);
    const QJsonValue stackingValue = root.value(QStringLiteral("stacking"));
    QCOMPARE(stackingValue.type(), QJsonValue::Object);
    const QJsonObject stacking = stackingValue.toObject();
    QCOMPARE(sortedKeys(stacking), (QStringList{
        QStringLiteral("available"), QStringLiteral("reason")}));
    QCOMPARE(stacking.value(QStringLiteral("available")).type(), QJsonValue::Bool);
    QCOMPARE(stacking.value(QStringLiteral("reason")).type(), QJsonValue::String);
    QCOMPARE(stacking.value(QStringLiteral("available")).toBool(), false);
    QCOMPARE(stacking.value(QStringLiteral("reason")).toString(),
             QStringLiteral("Inward same-edge stacking is unsupported; "
                            "stable-span overlap is not yet rejected."));

    requireJsonType(root, QStringLiteral("schemaVersion"), QJsonValue::Double);
    requireJsonType(root, QStringLiteral("snapshotSequence"), QJsonValue::String);
    requireJsonType(root, QStringLiteral("globalConfigureAppletsMode"), QJsonValue::Bool);
    requireJsonType(root, QStringLiteral("reservationGroups"), QJsonValue::Array);
    requireJsonType(root, QStringLiteral("reservationStateGeneration"), QJsonValue::String);
    requireJsonType(root, QStringLiteral("stacking"), QJsonValue::Object);
    requireJsonType(root, QStringLiteral("views"), QJsonValue::Array);

    const QJsonObject group =
        root.value(
            QStringLiteral("reservationGroups"))
            .toArray().at(0).toObject();
    QCOMPARE(sortedKeys(group), (QStringList{
        QStringLiteral("contributorDockIds"),
        QStringLiteral("edge"),
        QStringLiteral("generation"),
        QStringLiteral("geometry"),
        QStringLiteral("layerShellAnchors"),
        QStringLiteral("layerShellExclusiveEdge"),
        QStringLiteral("layerShellExclusiveZone"),
        QStringLiteral("layerShellMargins"),
        QStringLiteral("layerShellPresent"),
        QStringLiteral("memberCount"),
        QStringLiteral("outputId"),
        QStringLiteral("publishedDepth"),
        QStringLiteral("publisher"),
        QStringLiteral("windowGeometry")}));
    QCOMPARE(
        group.value(QStringLiteral("outputId")).toInt(),
        10);
    QCOMPARE(
        group.value(QStringLiteral("edge")).toString(),
        QStringLiteral("left"));
    QCOMPARE(
        group.value(QStringLiteral("generation")).toString(),
        QStringLiteral("53"));
    QCOMPARE(
        group.value(QStringLiteral("publishedDepth")).toInt(),
        48);
    QCOMPARE(
        group.value(
            QStringLiteral("contributorDockIds")).toArray(),
        (QJsonArray{7, 9}));
    QCOMPARE(
        group.value(QStringLiteral("memberCount")).toInt(),
        2);
    QCOMPARE(
        group.value(QStringLiteral("publisher")).toString(),
        QStringLiteral("object-27"));

    const QJsonObject view = root.value(QStringLiteral("views")).toArray().at(0).toObject();
    QCOMPARE(sortedKeys(view), (QStringList{
        QStringLiteral("absoluteGeometry"), QStringLiteral("alignment"),
        QStringLiteral("appletsLayoutGeometry"),
        QStringLiteral("appliedInputMask"), QStringLiteral("appliedRelocationGeneration"),
        QStringLiteral("attachOnWindowTouchConfigured"),
        QStringLiteral("attachedPresentationGeometry"),
        QStringLiteral("attachmentDeferredByPointer"),
        QStringLiteral("attachmentWaitsForPointerExitConfigured"),
        QStringLiteral("availablePrimaryLength"),
        QStringLiteral("canvasGeometry"),
        QStringLiteral("compositorScreenEdgeSupported"),
        QStringLiteral("computedInputBridgeGeometry"),
        QStringLiteral("computedPaintMaskGeometry"),
        QStringLiteral("configuredIconSize"), QStringLiteral("contentTranslation"),
        QStringLiteral("currentVisibleGeometry"),
        QStringLiteral("dockGapHideRequested"), QStringLiteral("edge"),
        QStringLiteral("editMode"), QStringLiteral("effectiveConfigureAppletsMode"),
        QStringLiteral("effectiveIconSize"), QStringLiteral("effectsRect"),
        QStringLiteral("enabledBorders"),
        QStringLiteral("floatedPresentationGeometry"),
        QStringLiteral("floatingAnchorRevision"),
        QStringLiteral("floatingAppletPopupsPreferred"),
        QStringLiteral("floatingDamageMaskGeneration"),
        QStringLiteral("floatingDamageMaskPending"),
        QStringLiteral("floatingGapConfigured"),
        QStringLiteral("floatingPanelConfigured"),
        QStringLiteral("floatingPanelEligible"),
        QStringLiteral("geometrySettled"), QStringLiteral("inDelete"),
        QStringLiteral("inReadyState"), QStringLiteral("inRelocationAnimation"),
        QStringLiteral("inRelocationShowing"), QStringLiteral("inStartup"),
        QStringLiteral("inputMask"), QStringLiteral("isHidden"),
        QStringLiteral("isOffScreen"),
        QStringLiteral("layerShellAnchors"),
        QStringLiteral("layerShellConfigureRequestRevision"),
        QStringLiteral("layerShellExclusiveEdge"),
        QStringLiteral("layerShellExclusiveZone"), QStringLiteral("layerShellMargins"),
        QStringLiteral("layerShellPresent"), QStringLiteral("layout"),
        QStringLiteral("linkPlacement"), QStringLiteral("linkedDockIds"),
        QStringLiteral("localGeometry"), QStringLiteral("logicalDockId"),
        QStringLiteral("maskRect"), QStringLiteral("maximumLengthRatio"),
        QStringLiteral("maximumNormalThickness"), QStringLiteral("normalThickness"),
        QStringLiteral("objects"), QStringLiteral("offsetRatio"),
        QStringLiteral("onPrimary"), QStringLiteral("orientation"),
        QStringLiteral("originalDockId"), QStringLiteral("persistentDockId"),
        QStringLiteral("pointerInsideView"),
        QStringLiteral("presentedScreenEdgeGap"),
        QStringLiteral("publishedStruts"), QStringLiteral("relationship"),
        QStringLiteral("relocationGeneration"),
        QStringLiteral("requestedReservationDepth"),
        QStringLiteral("reservationContributionDepth"),
        QStringLiteral("reservationContributorDockIds"),
        QStringLiteral("reservationEdge"),
        QStringLiteral("reservationGeometry"),
        QStringLiteral("reservationGroupGeneration"),
        QStringLiteral("reservationGroupMemberCount"),
        QStringLiteral("reservationLayerShellAnchors"),
        QStringLiteral("reservationLayerShellExclusiveEdge"),
        QStringLiteral("reservationLayerShellExclusiveZone"),
        QStringLiteral("reservationLayerShellMargins"),
        QStringLiteral("reservationOutputId"),
        QStringLiteral("reservationPublishedDepth"),
        QStringLiteral("reservationSurfacePresent"),
        QStringLiteral("reservationWindowGeometry"),
        QStringLiteral("runtimeViewId"),
        QStringLiteral("screen"),
        QStringLiteral("screenEdgeArmed"),
        QStringLiteral("screenEdgeBackend"),
        QStringLiteral("screenEdgeMargin"),
        QStringLiteral("screenEdgeRegistered"),
        QStringLiteral("screenGeometry"), QStringLiteral("screenId"),
        QStringLiteral("screensGroup"), QStringLiteral("settingsWindowShown"),
        QStringLiteral("shadowEnabledBorders"),
        QStringLiteral("shadowPaddingOffsets"),
        QStringLiteral("stableAppletMeasurementBounds"),
        QStringLiteral("stableCanvasGeometry"),
        QStringLiteral("stableLayerShellMargin"),
        QStringLiteral("stablePrimaryAxisLength"),
        QStringLiteral("stablePrimaryAxisStart"),
        QStringLiteral("stableTriggerGeometry"),
        QStringLiteral("strutsThickness"), QStringLiteral("surfaceGeometry"),
        QStringLiteral("surfaceGeometryPublicationRevision"),
        QStringLiteral("touchingWindowCount"),
        QStringLiteral("transitionAnimationDuration"),
        QStringLiteral("transitionDirection"),
        QStringLiteral("transitionGeometryPresent"),
        QStringLiteral("transitionGeometryRevision"),
        QStringLiteral("transitionPhase"),
        QStringLiteral("transitionProgress"),
        QStringLiteral("transitionRunning"),
        QStringLiteral("transitionTarget"),
        QStringLiteral("type"),
        QStringLiteral("visibilityContainsMouse"),
        QStringLiteral("visibilityMode"), QStringLiteral("windowGeometry"),
        QStringLiteral("windowTouchGeometryRoleType")}));
    QCOMPARE(view.value(QStringLiteral("runtimeViewId")).toString(), QStringLiteral("19"));
    QCOMPARE(view.value(QStringLiteral("persistentDockId")).toInt(), 7);
    QCOMPARE(view.value(QStringLiteral("logicalDockId")).toInt(), 3);
    QCOMPARE(view.value(QStringLiteral("originalDockId")).toInt(), 3);
    QCOMPARE(view.value(QStringLiteral("relationship")).toString(), QStringLiteral("linkedMember"));
    QCOMPARE(view.value(QStringLiteral("linkPlacement")).toString(), QStringLiteral("explicitTarget"));
    QCOMPARE(view.value(QStringLiteral("screensGroup")).toString(), QStringLiteral("single"));
    QCOMPARE(view.value(QStringLiteral("orientation")).toString(), QStringLiteral("vertical"));
    QCOMPARE(view.value(QStringLiteral("configuredIconSize")).toInt(), 64);
    QCOMPARE(view.value(QStringLiteral("effectiveIconSize")).toInt(), 52);
    QCOMPARE(view.value(QStringLiteral("availablePrimaryLength")).toInt(), 900);
    QCOMPARE(view.value(QStringLiteral("screenEdgeMargin")).toInt(), 8);
    QCOMPARE(view.value(QStringLiteral("screenEdgeBackend")).toString(),
             QStringLiteral("kwinAutoHide"));
    QCOMPARE(view.value(QStringLiteral("screenEdgeArmed")).toBool(), false);
    QCOMPARE(view.value(QStringLiteral("screenEdgeRegistered")).toBool(), true);
    QCOMPARE(view.value(QStringLiteral("compositorScreenEdgeSupported")).toBool(), true);
    QCOMPARE(view.value(QStringLiteral("visibilityContainsMouse")).toBool(), true);
    QCOMPARE(
        view.value(
            QStringLiteral("presentedScreenEdgeGap")).toInt(),
        6);
    auto startupRecord = record;
    startupRecord.presentedScreenEdgeGap.reset();
    const QJsonObject startupView =
        serializeDockSystemViewRecord(
            startupRecord,
            false);
    requireJsonType(
        startupView,
        QStringLiteral(
            "presentedScreenEdgeGap"),
        QJsonValue::Null);
    QCOMPARE(view.value(QStringLiteral("surfaceGeometry")).toArray(),
             serializeRect(record.surfaceGeometry));
    QCOMPARE(view.value(QStringLiteral("layerShellAnchors")).toArray(),
             QJsonArray::fromStringList(record.layerShellAnchors));
    QCOMPARE(view.value(QStringLiteral("layerShellMargins")).toArray(),
             serializeMargins(record.layerShellMargins));
    QCOMPARE(view.value(QStringLiteral("layerShellExclusiveEdge")).toString(),
             QStringLiteral("left"));
    QCOMPARE(view.value(QStringLiteral("layerShellExclusiveZone")).toInt(), 48);
    QCOMPARE(view.value(QStringLiteral("reservationGeometry")).toArray(),
             serializeRect(record.reservationGeometry));
    QCOMPARE(view.value(QStringLiteral("reservationOutputId")).toInt(), 10);
    QCOMPARE(
        view.value(QStringLiteral("reservationEdge")).toString(),
        QStringLiteral("left"));
    QCOMPARE(view.value(QStringLiteral("reservationContributionDepth")).toInt(), 40);
    QCOMPARE(view.value(QStringLiteral("reservationPublishedDepth")).toInt(), 48);
    QCOMPARE(view.value(QStringLiteral("reservationGroupMemberCount")).toInt(), 2);
    QCOMPARE(
        view.value(
            QStringLiteral("reservationGroupGeneration")).toString(),
        QStringLiteral("53"));
    QCOMPARE(
        view.value(
            QStringLiteral("reservationContributorDockIds")).toArray(),
        (QJsonArray{7, 9}));
    QCOMPARE(view.value(QStringLiteral("reservationWindowGeometry")).toArray(),
             serializeRect(record.reservationWindowGeometry));
    QCOMPARE(view.value(QStringLiteral("reservationLayerShellAnchors")).toArray(),
             QJsonArray::fromStringList(record.reservationLayerShellAnchors));
    QCOMPARE(view.value(QStringLiteral("reservationLayerShellMargins")).toArray(),
             serializeMargins(record.reservationLayerShellMargins));
    QCOMPARE(view.value(QStringLiteral("reservationLayerShellExclusiveEdge")).toString(),
             QStringLiteral("left"));
    QCOMPARE(view.value(QStringLiteral("reservationLayerShellExclusiveZone")).toInt(), 48);
    QCOMPARE(
        view.value(
            QStringLiteral("floatingGapConfigured")).toBool(),
        true);
    QCOMPARE(
        view.value(
            QStringLiteral("floatingPanelConfigured")).toBool(),
        true);
    QCOMPARE(
        view.value(
            QStringLiteral("floatingPanelEligible")).toBool(),
        true);
    QCOMPARE(
        view.value(
            QStringLiteral("attachOnWindowTouchConfigured")).toBool(),
        true);
    QCOMPARE(
        view.value(
            QStringLiteral(
                "attachmentWaitsForPointerExitConfigured")).toBool(),
        true);
    QCOMPARE(
        view.value(
            QStringLiteral("pointerInsideView")).toBool(),
        true);
    QCOMPARE(
        view.value(
            QStringLiteral("attachmentDeferredByPointer")).toBool(),
        true);
    QCOMPARE(
        view.value(
            QStringLiteral("dockGapHideRequested")).toBool(),
        false);
    QCOMPARE(
        view.value(
            QStringLiteral("touchingWindowCount")).toInt(),
        2);
    QCOMPARE(
        view.value(
            QStringLiteral("windowTouchGeometryRoleType")).toString(),
        QStringLiteral("QRect"));
    QCOMPARE(
        view.value(
            QStringLiteral("transitionTarget")).toString(),
        QStringLiteral("floated"));
    QCOMPARE(
        view.value(
            QStringLiteral("transitionProgress")).toDouble(),
        0.375);
    QCOMPARE(
        view.value(
            QStringLiteral("transitionAnimationDuration")).toInt(),
        200);
    QCOMPARE(
        view.value(
            QStringLiteral("transitionPhase")).toString(),
        QStringLiteral("floating"));
    QCOMPARE(
        view.value(
            QStringLiteral("transitionDirection")).toString(),
        QStringLiteral("towardFloated"));
    QCOMPARE(
        view.value(
            QStringLiteral("transitionRunning")).toBool(),
        true);
    QCOMPARE(
        view.value(
            QStringLiteral("transitionGeometryPresent")).toBool(),
        true);
    QCOMPARE(
        view.value(
            QStringLiteral(
                "transitionGeometryRevision"))
            .toString(),
        QString::number(
            std::numeric_limits<quint64>::max() - 2));
    QCOMPARE(
        view.value(
            QStringLiteral("currentVisibleGeometry")).toArray(),
        serializeRectF(
            *record.currentVisibleGeometry));
    QCOMPARE(
        view.value(
            QStringLiteral("computedPaintMaskGeometry")).toArray(),
        serializeRectF(
            *record.computedPaintMaskGeometry));
    QCOMPARE(
        view.value(
            QStringLiteral("computedInputBridgeGeometry")).toArray(),
        serializeRectF(
            *record.computedInputBridgeGeometry));
    QCOMPARE(
        view.value(
            QStringLiteral("contentTranslation")).toArray(),
        serializePointF(
            *record.contentTranslation));
    QCOMPARE(
        view.value(
            QStringLiteral("requestedReservationDepth")).toInt(),
        64);
    QCOMPARE(
        view.value(
            QStringLiteral(
                "surfaceGeometryPublicationRevision"))
            .toString(),
        QString::number(
            std::numeric_limits<quint64>::max()));
    QCOMPARE(
        view.value(
            QStringLiteral(
                "layerShellConfigureRequestRevision"))
            .toString(),
        QString::number(
            std::numeric_limits<quint64>::max() - 1));
    QCOMPARE(view.value(QStringLiteral("effectsRect")).toArray(), serializeRect(record.effectsRect));
    QCOMPARE(
        view.value(
            QStringLiteral("floatingDamageMaskPending")).toBool(),
        false);
    QCOMPARE(
        view.value(
            QStringLiteral("floatingDamageMaskGeneration")).toString(),
        QString::number(
            record.floatingDamageMaskGeneration));
    QCOMPARE(
        view.value(QStringLiteral("enabledBorders")).toArray(),
        QJsonArray::fromStringList(record.enabledBorders));
    QCOMPARE(
        view.value(QStringLiteral("shadowPaddingOffsets")).toArray(),
        serializeMargins(*record.shadowPaddingOffsets));
    QCOMPARE(
        view.value(QStringLiteral("shadowEnabledBorders")).toArray(),
        QJsonArray::fromStringList(
            *record.shadowEnabledBorders));
    QCOMPARE(
        view.value(
            QStringLiteral("floatingAppletPopupsPreferred")).toBool(),
        true);
    QCOMPARE(
        view.value(
            QStringLiteral("floatingAnchorRevision")).toString(),
        QString::number(record.floatingAnchorRevision));
    QCOMPARE(view.value(QStringLiteral("appletsLayoutGeometry")).toArray(), serializeRect(record.appletsLayoutGeometry));
    QCOMPARE(view.value(QStringLiteral("visibilityMode")).toString(), QStringLiteral("dodgeActive"));
    QCOMPARE(view.value(QStringLiteral("isHidden")).toBool(), true);
    QCOMPARE(view.value(QStringLiteral("inRelocationAnimation")).toBool(), true);
    QCOMPARE(view.value(QStringLiteral("inRelocationShowing")).toBool(), true);
    QCOMPARE(view.value(QStringLiteral("geometrySettled")).toBool(), true);
    QCOMPARE(view.value(QStringLiteral("relocationGeneration")).toString(), QStringLiteral("17"));
    QCOMPARE(view.value(QStringLiteral("appliedRelocationGeneration")).toString(), QStringLiteral("16"));
    QCOMPARE(view.value(QStringLiteral("inReadyState")).toBool(), true);
    QCOMPARE(view.value(QStringLiteral("effectiveConfigureAppletsMode")).toBool(), true);
    const QJsonObject objects = view.value(QStringLiteral("objects")).toObject();
    QCOMPARE(sortedKeys(objects), (QStringList{
        QStringLiteral("configWindow"), QStringLiteral("configuration"),
        QStringLiteral("containment"), QStringLiteral("editController"),
        QStringLiteral("geometryController"), QStringLiteral("layout"),
        QStringLiteral("layoutController"), QStringLiteral("reservationPublisher"),
        QStringLiteral("transitionController"),
        QStringLiteral("view"), QStringLiteral("windowTouchTracker")}));
    QCOMPARE(objects.value(QStringLiteral("layoutController")).toString(),
             QStringLiteral("object-23"));
    QCOMPARE(objects.value(QStringLiteral("reservationPublisher")).toString(),
             QStringLiteral("object-27"));
    QCOMPARE(objects.value(QStringLiteral("transitionController")).toString(),
             QStringLiteral("object-28"));
    QCOMPARE(objects.value(QStringLiteral("windowTouchTracker")).toString(),
             QStringLiteral("object-29"));

    const QStringList stringFields{
        QStringLiteral("runtimeViewId"), QStringLiteral("relocationGeneration"),
        QStringLiteral("appliedRelocationGeneration"), QStringLiteral("relationship"),
        QStringLiteral("linkPlacement"),
        QStringLiteral("screensGroup"), QStringLiteral("layout"), QStringLiteral("screen"),
        QStringLiteral("type"), QStringLiteral("edge"), QStringLiteral("orientation"),
        QStringLiteral("alignment"), QStringLiteral("visibilityMode"),
        QStringLiteral("layerShellExclusiveEdge"),
        QStringLiteral("reservationEdge"),
        QStringLiteral("reservationGroupGeneration"),
        QStringLiteral("reservationLayerShellExclusiveEdge"),
        QStringLiteral("surfaceGeometryPublicationRevision"),
        QStringLiteral("layerShellConfigureRequestRevision"),
        QStringLiteral("floatingDamageMaskGeneration"),
        QStringLiteral("floatingAnchorRevision"),
        QStringLiteral("transitionDirection"),
        QStringLiteral("transitionGeometryRevision"),
        QStringLiteral("transitionPhase"),
        QStringLiteral("transitionTarget"),
        QStringLiteral("screenEdgeBackend"),
        QStringLiteral("windowTouchGeometryRoleType")};
    const QStringList numberFields{
        QStringLiteral("persistentDockId"), QStringLiteral("logicalDockId"),
        QStringLiteral("originalDockId"), QStringLiteral("screenId"),
        QStringLiteral("maximumLengthRatio"), QStringLiteral("offsetRatio"),
        QStringLiteral("configuredIconSize"), QStringLiteral("effectiveIconSize"),
        QStringLiteral("availablePrimaryLength"), QStringLiteral("normalThickness"),
        QStringLiteral("maximumNormalThickness"), QStringLiteral("screenEdgeMargin"),
        QStringLiteral("presentedScreenEdgeGap"),
        QStringLiteral("strutsThickness"),
        QStringLiteral("layerShellExclusiveZone"),
        QStringLiteral("reservationContributionDepth"),
        QStringLiteral("reservationGroupMemberCount"),
        QStringLiteral("reservationOutputId"),
        QStringLiteral("reservationPublishedDepth"),
        QStringLiteral("reservationLayerShellExclusiveZone"),
        QStringLiteral("requestedReservationDepth"),
        QStringLiteral("stableLayerShellMargin"),
        QStringLiteral("stablePrimaryAxisLength"),
        QStringLiteral("stablePrimaryAxisStart"),
        QStringLiteral("touchingWindowCount"),
        QStringLiteral("transitionAnimationDuration"),
        QStringLiteral("transitionProgress")};
    const QStringList booleanFields{
        QStringLiteral("onPrimary"), QStringLiteral("isHidden"), QStringLiteral("inStartup"),
        QStringLiteral("isOffScreen"), QStringLiteral("inRelocationAnimation"),
        QStringLiteral("inRelocationShowing"), QStringLiteral("geometrySettled"),
        QStringLiteral("inDelete"), QStringLiteral("inReadyState"), QStringLiteral("editMode"),
        QStringLiteral("effectiveConfigureAppletsMode"), QStringLiteral("settingsWindowShown"),
        QStringLiteral("layerShellPresent"), QStringLiteral("reservationSurfacePresent"),
        QStringLiteral("floatingDamageMaskPending"),
        QStringLiteral("floatingAppletPopupsPreferred"),
        QStringLiteral("attachOnWindowTouchConfigured"),
        QStringLiteral("attachmentDeferredByPointer"),
        QStringLiteral("attachmentWaitsForPointerExitConfigured"),
        QStringLiteral("dockGapHideRequested"),
        QStringLiteral("floatingGapConfigured"),
        QStringLiteral("floatingPanelConfigured"),
        QStringLiteral("floatingPanelEligible"),
        QStringLiteral("pointerInsideView"),
        QStringLiteral("screenEdgeArmed"),
        QStringLiteral("screenEdgeRegistered"),
        QStringLiteral("compositorScreenEdgeSupported"),
        QStringLiteral("visibilityContainsMouse"),
        QStringLiteral("transitionGeometryPresent"),
        QStringLiteral("transitionRunning")};
    const QStringList arrayFields{
        QStringLiteral("linkedDockIds"), QStringLiteral("windowGeometry"),
        QStringLiteral("absoluteGeometry"), QStringLiteral("localGeometry"),
        QStringLiteral("screenGeometry"), QStringLiteral("surfaceGeometry"),
        QStringLiteral("canvasGeometry"),
        QStringLiteral("effectsRect"), QStringLiteral("appletsLayoutGeometry"),
        QStringLiteral("maskRect"), QStringLiteral("inputMask"),
        QStringLiteral("appliedInputMask"), QStringLiteral("publishedStruts"),
        QStringLiteral("enabledBorders"),
        QStringLiteral("layerShellAnchors"), QStringLiteral("layerShellMargins"),
        QStringLiteral("shadowEnabledBorders"),
        QStringLiteral("shadowPaddingOffsets"),
        QStringLiteral("reservationContributorDockIds"),
        QStringLiteral("reservationGeometry"),
        QStringLiteral("reservationWindowGeometry"),
        QStringLiteral("reservationLayerShellAnchors"),
        QStringLiteral("reservationLayerShellMargins"),
        QStringLiteral("stableCanvasGeometry"),
        QStringLiteral("attachedPresentationGeometry"),
        QStringLiteral("floatedPresentationGeometry"),
        QStringLiteral("currentVisibleGeometry"),
        QStringLiteral("computedPaintMaskGeometry"),
        QStringLiteral("computedInputBridgeGeometry"),
        QStringLiteral("contentTranslation"),
        QStringLiteral("stableTriggerGeometry"),
        QStringLiteral("stableAppletMeasurementBounds")};
    for (const auto &key : stringFields) {
        requireJsonType(view, key, QJsonValue::String);
    }
    for (const auto &key : numberFields) {
        requireJsonType(view, key, QJsonValue::Double);
    }
    for (const auto &key : booleanFields) {
        requireJsonType(view, key, QJsonValue::Bool);
    }
    for (const auto &key : arrayFields) {
        requireJsonType(view, key, QJsonValue::Array);
    }
    requireJsonType(view, QStringLiteral("objects"), QJsonValue::Object);
    for (const auto &key : objects.keys()) {
        requireJsonType(objects, key, QJsonValue::String);
    }
}

void DbusReportsTest::dockSystemSnapshotPinsNullableWireStates()
{
    DockSystemViewRecord record;
    record.runtimeViewId = 1;
    record.persistentDockId = 7;
    record.logicalDockId = 7;
    record.relationship = DockRelationship::Independent;
    record.screensGroup = Types::SingleScreenGroup;

    DockSystemSnapshot snapshot;
    snapshot.snapshotSequence = 1;
    snapshot.views = {record};

    const QJsonObject root = QJsonDocument::fromJson(
        serializeDockSystemSnapshot(snapshot).toUtf8()).object();
    requireJsonType(
        root,
        QStringLiteral("reservationStateGeneration"),
        QJsonValue::String);
    QCOMPARE(
        root.value(
            QStringLiteral("reservationStateGeneration")).toString(),
        QStringLiteral("0"));
    requireJsonType(
        root,
        QStringLiteral("reservationGroups"),
        QJsonValue::Array);
    QVERIFY(
        root.value(
            QStringLiteral("reservationGroups")).toArray().isEmpty());

    const QJsonObject view =
        root.value(
            QStringLiteral("views")).toArray().at(0).toObject();
    requireJsonType(view, QStringLiteral("originalDockId"), QJsonValue::Null);
    requireJsonType(view, QStringLiteral("linkPlacement"), QJsonValue::Null);
    requireJsonType(view, QStringLiteral("configuredIconSize"), QJsonValue::Null);
    requireJsonType(view, QStringLiteral("effectiveIconSize"), QJsonValue::Null);
    requireJsonType(view, QStringLiteral("availablePrimaryLength"), QJsonValue::Null);
    requireJsonType(view, QStringLiteral("layerShellExclusiveEdge"), QJsonValue::Null);
    requireJsonType(view, QStringLiteral("layerShellExclusiveZone"), QJsonValue::Null);
    requireJsonType(view, QStringLiteral("layerShellPresent"), QJsonValue::Bool);
    requireJsonType(view, QStringLiteral("layerShellAnchors"), QJsonValue::Array);
    requireJsonType(view, QStringLiteral("layerShellMargins"), QJsonValue::Array);
    QCOMPARE(view.value(QStringLiteral("layerShellPresent")).toBool(), false);
    requireJsonType(view, QStringLiteral("reservationLayerShellExclusiveEdge"), QJsonValue::Null);
    requireJsonType(view, QStringLiteral("reservationLayerShellExclusiveZone"), QJsonValue::Null);
    requireJsonType(view, QStringLiteral("reservationOutputId"), QJsonValue::Null);
    requireJsonType(view, QStringLiteral("reservationEdge"), QJsonValue::Null);
    requireJsonType(view, QStringLiteral("reservationContributionDepth"), QJsonValue::Null);
    requireJsonType(view, QStringLiteral("reservationPublishedDepth"), QJsonValue::Null);
    requireJsonType(view, QStringLiteral("reservationGroupMemberCount"), QJsonValue::Null);
    requireJsonType(view, QStringLiteral("reservationGroupGeneration"), QJsonValue::Null);
    requireJsonType(view, QStringLiteral("reservationContributorDockIds"), QJsonValue::Array);
    QVERIFY(
        view.value(
            QStringLiteral("reservationContributorDockIds")).toArray().isEmpty());
    requireJsonType(view, QStringLiteral("reservationSurfacePresent"), QJsonValue::Bool);
    requireJsonType(view, QStringLiteral("reservationGeometry"), QJsonValue::Array);
    requireJsonType(view, QStringLiteral("reservationWindowGeometry"), QJsonValue::Array);
    requireJsonType(view, QStringLiteral("reservationLayerShellAnchors"), QJsonValue::Array);
    requireJsonType(view, QStringLiteral("reservationLayerShellMargins"), QJsonValue::Array);
    QCOMPARE(view.value(QStringLiteral("reservationSurfacePresent")).toBool(), false);
    requireJsonType(view, QStringLiteral("screensGroup"), QJsonValue::String);
    QCOMPARE(view.value(QStringLiteral("screensGroup")).toString(), QStringLiteral("single"));
    requireJsonType(view, QStringLiteral("floatingPanelConfigured"), QJsonValue::Bool);
    requireJsonType(view, QStringLiteral("floatingDamageMaskPending"), QJsonValue::Bool);
    requireJsonType(view, QStringLiteral("floatingDamageMaskGeneration"), QJsonValue::String);
    requireJsonType(view, QStringLiteral("enabledBorders"), QJsonValue::Array);
    requireJsonType(view, QStringLiteral("shadowEnabledBorders"), QJsonValue::Null);
    requireJsonType(view, QStringLiteral("shadowPaddingOffsets"), QJsonValue::Null);
    requireJsonType(view, QStringLiteral("floatingAppletPopupsPreferred"), QJsonValue::Bool);
    requireJsonType(view, QStringLiteral("floatingAnchorRevision"), QJsonValue::String);
    requireJsonType(view, QStringLiteral("floatingGapConfigured"), QJsonValue::Bool);
    requireJsonType(view, QStringLiteral("floatingPanelEligible"), QJsonValue::Bool);
    requireJsonType(view, QStringLiteral("attachOnWindowTouchConfigured"), QJsonValue::Bool);
    requireJsonType(
        view,
        QStringLiteral("attachmentWaitsForPointerExitConfigured"),
        QJsonValue::Bool);
    requireJsonType(view, QStringLiteral("pointerInsideView"), QJsonValue::Bool);
    requireJsonType(view, QStringLiteral("attachmentDeferredByPointer"), QJsonValue::Bool);
    requireJsonType(view, QStringLiteral("dockGapHideRequested"), QJsonValue::Bool);
    requireJsonType(view, QStringLiteral("touchingWindowCount"), QJsonValue::Double);
    requireJsonType(view, QStringLiteral("windowTouchGeometryRoleType"), QJsonValue::String);
    requireJsonType(view, QStringLiteral("transitionTarget"), QJsonValue::String);
    requireJsonType(view, QStringLiteral("transitionProgress"), QJsonValue::Double);
    requireJsonType(
        view,
        QStringLiteral("transitionAnimationDuration"),
        QJsonValue::Double);
    requireJsonType(view, QStringLiteral("transitionPhase"), QJsonValue::String);
    requireJsonType(view, QStringLiteral("transitionDirection"), QJsonValue::String);
    requireJsonType(view, QStringLiteral("transitionRunning"), QJsonValue::Bool);
    requireJsonType(view, QStringLiteral("transitionGeometryPresent"), QJsonValue::Bool);
    requireJsonType(view, QStringLiteral("transitionGeometryRevision"), QJsonValue::String);
    requireJsonType(view, QStringLiteral("stableLayerShellMargin"), QJsonValue::Double);
    requireJsonType(view, QStringLiteral("screenEdgeMargin"), QJsonValue::Double);
    requireJsonType(
        view,
        QStringLiteral("presentedScreenEdgeGap"),
        QJsonValue::Null);
    QCOMPARE(view.value(QStringLiteral("floatingGapConfigured")).toBool(), false);
    QCOMPARE(view.value(QStringLiteral("floatingPanelConfigured")).toBool(), false);
    QCOMPARE(view.value(QStringLiteral("floatingDamageMaskPending")).toBool(), false);
    QCOMPARE(view.value(QStringLiteral("floatingDamageMaskGeneration")).toString(),
             QStringLiteral("0"));
    QVERIFY(view.value(QStringLiteral("enabledBorders")).toArray().isEmpty());
    QVERIFY(view.value(QStringLiteral("shadowEnabledBorders")).isNull());
    QVERIFY(view.value(QStringLiteral("shadowPaddingOffsets")).isNull());
    QCOMPARE(view.value(QStringLiteral("floatingAppletPopupsPreferred")).toBool(),
             false);
    QCOMPARE(view.value(QStringLiteral("floatingAnchorRevision")).toString(),
             QStringLiteral("0"));
    QCOMPARE(view.value(QStringLiteral("floatingPanelEligible")).toBool(), false);
    QCOMPARE(view.value(QStringLiteral("attachOnWindowTouchConfigured")).toBool(), false);
    QCOMPARE(
        view.value(
            QStringLiteral(
                "attachmentWaitsForPointerExitConfigured")).toBool(),
        false);
    QCOMPARE(view.value(QStringLiteral("pointerInsideView")).toBool(), false);
    QCOMPARE(view.value(QStringLiteral("attachmentDeferredByPointer")).toBool(), false);
    QCOMPARE(view.value(QStringLiteral("dockGapHideRequested")).toBool(), false);
    QCOMPARE(view.value(QStringLiteral("touchingWindowCount")).toInt(), 0);
    QCOMPARE(view.value(QStringLiteral("windowTouchGeometryRoleType")).toString(),
             QString{});
    QCOMPARE(view.value(QStringLiteral("transitionTarget")).toString(), QStringLiteral("floated"));
    QCOMPARE(view.value(QStringLiteral("transitionProgress")).toDouble(), 1.0);
    QCOMPARE(
        view.value(
            QStringLiteral("transitionAnimationDuration")).toInt(),
        0);
    QCOMPARE(view.value(QStringLiteral("transitionPhase")).toString(), QStringLiteral("resting"));
    QCOMPARE(view.value(QStringLiteral("transitionDirection")).toString(), QStringLiteral("none"));
    QCOMPARE(view.value(QStringLiteral("transitionRunning")).toBool(), false);
    QCOMPARE(view.value(QStringLiteral("transitionGeometryPresent")).toBool(), false);
    QCOMPARE(view.value(QStringLiteral("transitionGeometryRevision")).toString(),
             QStringLiteral("0"));
    QCOMPARE(view.value(QStringLiteral("stableLayerShellMargin")).toInt(), 0);
    QCOMPARE(view.value(QStringLiteral("screenEdgeMargin")).toInt(), 0);
    QVERIFY(
        view.value(
            QStringLiteral(
                "presentedScreenEdgeGap")).isNull());
    const QStringList absentTransitionFields{
        QStringLiteral("attachedPresentationGeometry"),
        QStringLiteral("contentTranslation"),
        QStringLiteral("currentVisibleGeometry"),
        QStringLiteral("floatedPresentationGeometry"),
        QStringLiteral("computedInputBridgeGeometry"),
        QStringLiteral("computedPaintMaskGeometry"),
        QStringLiteral("requestedReservationDepth"),
        QStringLiteral("stableAppletMeasurementBounds"),
        QStringLiteral("stableCanvasGeometry"),
        QStringLiteral("stablePrimaryAxisLength"),
        QStringLiteral("stablePrimaryAxisStart")};
    for (const auto &key : absentTransitionFields) {
        requireJsonType(view, key, QJsonValue::Null);
    }
    requireJsonType(
        view,
        QStringLiteral("stableTriggerGeometry"),
        QJsonValue::Null);
    requireJsonType(
        view,
        QStringLiteral(
            "surfaceGeometryPublicationRevision"),
        QJsonValue::String);
    requireJsonType(
        view,
        QStringLiteral(
            "layerShellConfigureRequestRevision"),
        QJsonValue::String);
    QCOMPARE(
        view.value(
            QStringLiteral(
                "surfaceGeometryPublicationRevision"))
            .toString(),
        QStringLiteral("0"));
    QCOMPARE(
        view.value(
            QStringLiteral(
                "layerShellConfigureRequestRevision"))
            .toString(),
        QStringLiteral("0"));

    const QJsonObject objects = view.value(QStringLiteral("objects")).toObject();
    for (const auto &key : objects.keys()) {
        requireJsonType(objects, key, QJsonValue::Null);
    }
}

void DbusReportsTest::dockSystemSnapshotPreservesFractionalTransitionGeometry()
{
    DockSystemSnapshot snapshot;
    snapshot.views = {
        stableBottomTransitionRecord()};

    const QJsonObject view =
        QJsonDocument::fromJson(
            serializeDockSystemSnapshot(
                snapshot).toUtf8())
            .object()
            .value(
                QStringLiteral("views"))
            .toArray()
            .at(0)
            .toObject();

    const QJsonArray visible =
        view.value(
            QStringLiteral(
                "currentVisibleGeometry"))
            .toArray();
    const QJsonArray paint =
        view.value(
            QStringLiteral(
                "computedPaintMaskGeometry"))
            .toArray();
    const QJsonArray bridge =
        view.value(
            QStringLiteral(
                "computedInputBridgeGeometry"))
            .toArray();
    const QJsonArray translation =
        view.value(
            QStringLiteral(
                "contentTranslation"))
            .toArray();

    QCOMPARE(visible.at(1).toDouble(), 4.375);
    QCOMPARE(paint.at(1).toDouble(), 4.375);
    QCOMPARE(bridge.at(1).toDouble(), 4.375);
    QCOMPARE(bridge.at(3).toDouble(), 42.625);
    QCOMPARE(translation.at(1).toDouble(), 4.375);
    QVERIFY(
        visible.at(1).toDouble()
        != static_cast<int>(
            visible.at(1).toDouble()));
}

void DbusReportsTest::windowTouchAuthorityCountsRejectDivergence()
{
    constexpr WindowTouchAuthorityCounts synchronized{
        .transitionCopy = 2,
        .trackerAuthority = 2,
    };
    constexpr WindowTouchAuthorityCounts divergent{
        .transitionCopy = 1,
        .trackerAuthority = 2,
    };

    static_assert(
        validateWindowTouchAuthorityCounts(
            synchronized)
            == std::optional<int>{2});
    static_assert(
        !validateWindowTouchAuthorityCounts(
             divergent)
             .has_value());

    QCOMPARE(
        validateWindowTouchAuthorityCounts(
            synchronized),
        std::optional<int>{2});
    QVERIFY(
        !validateWindowTouchAuthorityCounts(
             divergent)
             .has_value());
}

void DbusReportsTest::dockSystemSnapshotRejectsTransitionDisagreement()
{
    DockSystemSnapshot valid;
    valid.views = {
        stableBottomTransitionRecord()};
    QVERIFY(dockTransitionRecordsAgree(valid));

    const auto rejects =
        [&valid](const auto &mutate) {
            DockSystemSnapshot invalid = valid;
            mutate(invalid);
            QVERIFY(
                !dockTransitionRecordsAgree(
                    invalid));
        };

    rejects([](auto &snapshot) {
        snapshot.views[0]
            .objects.transitionController.clear();
    });
    rejects([](auto &snapshot) {
        ++*snapshot.views[0]
               .presentedScreenEdgeGap;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .transitionAnimationDuration = -1;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].screenEdgeBackend = QStringLiteral("unknown");
    });
    {
        DockSystemSnapshot capable = valid;
        capable.views[0].compositorScreenEdgeSupported = true;
        QVERIFY(dockTransitionRecordsAgree(capable));
    }
    rejects([](auto &snapshot) {
        auto &view = snapshot.views[0];
        view.visibilityMode = Types::AutoHide;
        view.screenEdgeBackend = QStringLiteral("kwinAutoHide");
    });

    {
        DockSystemSnapshot hidden = valid;
        auto &view = hidden.views[0];
        view.visibilityMode = Types::AutoHide;
        view.floatingPanelEligible = false;
        view.isHidden = true;
        view.screenEdgeBackend = QStringLiteral("kwinAutoHide");
        view.screenEdgeArmed = true;
        view.screenEdgeRegistered = true;
        view.compositorScreenEdgeSupported = true;
        QVERIFY(dockTransitionRecordsAgree(hidden));

        view.inRelocationAnimation = true;
        QVERIFY(!dockTransitionRecordsAgree(hidden));
        view.inRelocationAnimation = false;

        view.visibilityContainsMouse = true;
        QVERIFY(!dockTransitionRecordsAgree(hidden));
    }

    {
        DockSystemSnapshot startup = valid;
        startup.views[0].inStartup = true;
        startup.views[0].inReadyState = false;
        startup.views[0]
            .presentedScreenEdgeGap.reset();
        QVERIFY(
            dockTransitionRecordsAgree(
                startup));

        startup.views[0].inStartup = false;
        startup.views[0].inReadyState = true;
        QVERIFY(
            !dockTransitionRecordsAgree(
                startup));
    }
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .objects.transitionController =
            snapshot.views[0]
                .objects.geometryController;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .objects.windowTouchTracker.clear();
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .objects.windowTouchTracker =
            snapshot.views[0]
                .objects.transitionController;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .objects.view =
            snapshot.views[0]
                .objects.transitionController;
    });
    rejects([](auto &snapshot) {
        auto second =
            snapshot.views[0];
        second.persistentDockId = 8;
        second.logicalDockId = 8;
        second.objects.geometryController =
            QStringLiteral("object-4");
        snapshot.views.append(second);
    });
    rejects([](auto &snapshot) {
        auto second =
            snapshot.views[0];
        second.persistentDockId = 8;
        second.logicalDockId = 8;
        second.objects.geometryController =
            QStringLiteral("object-4");
        second.objects.transitionController =
            QStringLiteral("object-5");
        second.objects.windowTouchTracker =
            QStringLiteral("object-6");
        second.objects.view =
            snapshot.views[0]
                .objects.transitionController;
        snapshot.views.append(second);
    });
    rejects([](auto &snapshot) {
        auto second =
            snapshot.views[0];
        second.persistentDockId = 8;
        second.logicalDockId = 8;
        second.objects.geometryController =
            QStringLiteral("object-4");
        second.objects.transitionController =
            QStringLiteral("object-5");
        snapshot.views.append(second);
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .floatingPanelEligible = false;
        snapshot.views[0]
            .transitionTarget =
            DockTransitionTarget::Attached;
        snapshot.views[0]
            .transitionProgress = 0.0;
        snapshot.views[0]
            .transitionPhase =
            DockTransitionPhase::Resting;
        snapshot.views[0]
            .transitionDirection =
            DockTransitionDirection::None;
        snapshot.views[0]
            .transitionRunning = false;
        snapshot.views[0]
            .currentVisibleGeometry =
            QRectF(
                *snapshot.views[0]
                    .attachedPresentationGeometry);
        snapshot.views[0]
            .computedPaintMaskGeometry =
            snapshot.views[0]
                .currentVisibleGeometry;
        snapshot.views[0]
            .computedInputBridgeGeometry =
            snapshot.views[0]
                .currentVisibleGeometry;
        snapshot.views[0]
            .contentTranslation =
            QPointF(0.0, 7.0);
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .floatingPanelConfigured = false;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .visibilityMode = Types::DodgeActive;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .transitionTarget =
            DockTransitionTarget::Attached;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .transitionDirection =
            DockTransitionDirection::TowardAttached;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .transitionRunning = false;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .transitionProgress =
            std::numeric_limits<qreal>::quiet_NaN();
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .transitionProgress = 1.5;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .touchingWindowCount = -1;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .windowTouchGeometryRoleType =
            QStringLiteral("QRectF");
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .attachmentDeferredByPointer = true;
        snapshot.views[0]
            .attachmentWaitsForPointerExitConfigured = true;
        snapshot.views[0]
            .pointerInsideView = true;
        snapshot.views[0]
            .touchingWindowCount = 1;
        snapshot.views[0]
            .windowTouchGeometryRoleType.clear();
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .currentVisibleGeometry =
            QRectF(
                snapshot.views[0]
                    .currentVisibleGeometry
                    ->toRect());
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .computedPaintMaskGeometry
            ->translate(0.25, 0.0);
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .computedInputBridgeGeometry
            ->translate(0.25, 0.0);
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .contentTranslation =
            QPointF(0.0, 4.0);
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .stableCanvasGeometry
            ->translate(1, 0);
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .stableCanvasGeometry
            ->translate(0, -1);
        snapshot.views[0]
            .surfaceGeometry
            .translate(0, -1);
        snapshot.views[0]
            .absoluteGeometry
            .translate(0, -1);
        snapshot.views[0]
            .stableTriggerGeometry
            ->translate(0, -1);
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .screenGeometry =
            QRect(0, 0, 200, 1000);
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .surfaceGeometry
            .translate(1, 0);
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .absoluteGeometry
            .translate(1, 0);
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .stableTriggerGeometry
            ->translate(1, 0);
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .stableTriggerGeometry =
            QRect(100, 959, 300, 42);
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .attachedPresentationGeometry
            ->translate(-1, 0);
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .floatedPresentationGeometry
            ->translate(1, -1);
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .currentVisibleGeometry
            ->translate(0, 3.0);
        snapshot.views[0]
            .computedPaintMaskGeometry =
            snapshot.views[0]
                .currentVisibleGeometry;
        snapshot.views[0]
            .computedInputBridgeGeometry =
            snapshot.views[0]
                .currentVisibleGeometry;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .computedInputBridgeGeometry =
            QRectF(0.0, -0.25, 300.0, 47.25);
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .stableAppletMeasurementBounds
            ->translate(1, 0);
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .stablePrimaryAxisLength = 299;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .stablePrimaryAxisStart = 101;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .stableLayerShellMargin = 1;
        snapshot.views[0]
            .layerShellMargins.setBottom(1);
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .requestedReservationDepth = 39;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .transitionGeometryPresent = false;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .currentVisibleGeometry.reset();
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].maskRect = {};
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].inputMask =
            QRect(QPoint{}, snapshot.views[0]
                                .stableCanvasGeometry->size());
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].appliedInputMask =
            snapshot.views[0].inputMask.adjusted(
                0, 0, 1, 0);
    });

    {
        DockSystemSnapshot prePlacement;
        auto panel = stableBottomTransitionRecord();
        panel.transitionTarget =
            DockTransitionTarget::Floated;
        panel.transitionProgress = 1.0;
        panel.presentedScreenEdgeGap =
            panel.screenEdgeMargin;
        panel.transitionPhase =
            DockTransitionPhase::Resting;
        panel.transitionDirection =
            DockTransitionDirection::None;
        panel.transitionRunning = false;
        panel.transitionGeometryPresent = false;
        panel.stableCanvasGeometry.reset();
        panel.attachedPresentationGeometry.reset();
        panel.floatedPresentationGeometry.reset();
        panel.currentVisibleGeometry.reset();
        panel.computedPaintMaskGeometry.reset();
        panel.computedInputBridgeGeometry.reset();
        panel.contentTranslation.reset();
        panel.stableAppletMeasurementBounds.reset();
        panel.stablePrimaryAxisStart.reset();
        panel.stablePrimaryAxisLength.reset();
        panel.requestedReservationDepth.reset();
        prePlacement.views = {panel};
        QVERIFY(
            !dockTransitionRecordsAgree(
                prePlacement));

        prePlacement.views[0]
            .stableTriggerGeometry.reset();
        QVERIFY(
            dockTransitionRecordsAgree(
                prePlacement));
    }

    auto attached = stableBottomTransitionRecord();
    attached.attachOnWindowTouchConfigured = true;
    attached.attachmentDeferredByPointer = false;
    attached.touchingWindowCount = 1;
    attached.windowTouchGeometryRoleType =
        QStringLiteral("QRect");
    attached.transitionTarget =
        DockTransitionTarget::Attached;
    attached.transitionProgress = 0.0;
    attached.presentedScreenEdgeGap = 0;
    attached.transitionPhase =
        DockTransitionPhase::Resting;
    attached.transitionDirection =
        DockTransitionDirection::None;
    attached.transitionRunning = false;
    attached.currentVisibleGeometry =
        QRectF(*attached.attachedPresentationGeometry);
    attached.computedPaintMaskGeometry =
        attached.currentVisibleGeometry;
    attached.computedInputBridgeGeometry =
        attached.currentVisibleGeometry;
    attached.contentTranslation =
        QPointF(0.0, 7.0);
    attached.effectsRect =
        attached.currentVisibleGeometry->toAlignedRect();
    attached.maskRect = attached.effectsRect;
    attached.inputMask = attached.effectsRect;
    attached.appliedInputMask = attached.inputMask;
    attached.shadowPaddingOffsets =
        QMargins(0, -7, 0, 0);
    attached.enabledBorders = {
        QStringLiteral("top"),
        QStringLiteral("right"),
        QStringLiteral("left"),
    };
    attached.shadowEnabledBorders =
        attached.enabledBorders;
    attached.floatingAppletPopupsPreferred = false;

    DockSystemSnapshot attachedSnapshot;
    attachedSnapshot.views = {attached};
    QVERIFY(dockTransitionRecordsAgree(attachedSnapshot));

    const auto rejectsBrokenAttachmentEquation =
        [&attachedSnapshot](const auto &breakRequiredArm) {
            DockSystemSnapshot invalid = attachedSnapshot;
            breakRequiredArm(invalid.views[0]);
            QVERIFY(!dockTransitionRecordsAgree(invalid));
        };
    rejectsBrokenAttachmentEquation([](auto &record) {
        record.floatingPanelEligible = false;
    });
    rejectsBrokenAttachmentEquation([](auto &record) {
        record.attachOnWindowTouchConfigured = false;
    });
    rejectsBrokenAttachmentEquation([](auto &record) {
        record.attachmentDeferredByPointer = true;
        record.attachmentWaitsForPointerExitConfigured = true;
        record.pointerInsideView = true;
    });
    rejectsBrokenAttachmentEquation([](auto &record) {
        record.touchingWindowCount = 0;
    });

    {
        DockSystemSnapshot dockSnapshot;
        DockSystemViewRecord dockRecord;
        dockRecord.runtimeViewId = 8;
        dockRecord.persistentDockId = 8;
        dockRecord.logicalDockId = 8;
        dockRecord.type = Types::DockView;
        dockRecord.edge = Plasma::Types::BottomEdge;
        dockRecord.maximumNormalThickness = 15;
        dockRecord.screenEdgeMargin = 5;
        dockRecord.presentedScreenEdgeGap = 0;
        dockRecord.absoluteGeometry =
            QRect(20, 90, 100, 10);
        dockRecord.screenGeometry =
            QRect(0, 0, 200, 100);
        dockRecord.floatingGapConfigured = true;
        dockRecord.floatingPanelConfigured = false;
        dockRecord.attachOnWindowTouchConfigured = true;
        dockRecord.dockGapHideRequested = true;
        dockRecord.touchingWindowCount = 1;
        dockRecord.windowTouchGeometryRoleType =
            QStringLiteral("QRect");
        dockRecord.visibilityMode = Types::AlwaysVisible;
        dockRecord.transitionTarget =
            DockTransitionTarget::Attached;
        dockRecord.transitionProgress = 0.0;
        dockRecord.enabledBorders = {
            QStringLiteral("top"),
            QStringLiteral("right"),
            QStringLiteral("left"),
        };
        dockRecord.geometrySettled = true;
        dockRecord.inReadyState = true;
        dockRecord.stableTriggerGeometry =
            QRect(20, 84, 100, 15);
        dockRecord.objects.transitionController =
            QStringLiteral("dock-transition");
        dockRecord.objects.windowTouchTracker =
            QStringLiteral("dock-window-touch");
        dockSnapshot.views = {dockRecord};
        auto &dock = dockSnapshot.views[0];
        QVERIFY(dockTransitionRecordsAgree(dockSnapshot));

        dock.screenEdgeMargin = 0;
        QVERIFY(!dockTransitionRecordsAgree(dockSnapshot));
        dock.screenEdgeMargin = 5;
        constexpr std::array dockVisibilityModes{
            Types::AlwaysVisible,
            Types::AutoHide,
            Types::DodgeActive,
            Types::DodgeMaximized,
            Types::DodgeAllWindows,
            Types::WindowsGoBelow,
            Types::WindowsCanCover,
            Types::WindowsAlwaysCover,
            Types::SidebarOnDemand,
            Types::SidebarAutoHide,
            Types::NormalWindow,
        };
        for (const Types::Visibility mode : dockVisibilityModes) {
            dock.visibilityMode = mode;
            QVERIFY(dockTransitionRecordsAgree(dockSnapshot));
        }
        dock.visibilityMode = Types::AlwaysVisible;
        dock.stableTriggerGeometry->translate(1, 0);
        QVERIFY(!dockTransitionRecordsAgree(dockSnapshot));
        dock.stableTriggerGeometry =
            QRect(20, 84, 100, 15);
        dock.stableTriggerGeometry.reset();
        QVERIFY(!dockTransitionRecordsAgree(dockSnapshot));
        dock.inReadyState = false;
        dock.dockGapHideRequested = false;
        dock.touchingWindowCount = 0;
        dock.windowTouchGeometryRoleType.clear();
        dock.transitionTarget =
            DockTransitionTarget::Floated;
        dock.transitionProgress = 1.0;
        dock.presentedScreenEdgeGap = 5;
        dock.enabledBorders = {
            QStringLiteral("top"),
            QStringLiteral("right"),
            QStringLiteral("bottom"),
            QStringLiteral("left"),
        };
        QVERIFY(dockTransitionRecordsAgree(dockSnapshot));
        dock.stableTriggerGeometry =
            QRect(20, 84, 100, 15);
        dock.inReadyState = true;
        dock.dockGapHideRequested = true;
        dock.touchingWindowCount = 1;
        dock.windowTouchGeometryRoleType =
            QStringLiteral("QRect");
        dock.transitionTarget =
            DockTransitionTarget::Attached;
        dock.transitionProgress = 0.0;
        dock.presentedScreenEdgeGap = 0;
        dock.enabledBorders = {
            QStringLiteral("top"),
            QStringLiteral("right"),
            QStringLiteral("left"),
        };
        dock.visibilityMode = Types::DodgeActive;
        QVERIFY(dockTransitionRecordsAgree(dockSnapshot));
        dock.visibilityMode = Types::AlwaysVisible;
        dock.floatingPanelConfigured = true;
        QVERIFY(!dockTransitionRecordsAgree(dockSnapshot));
        dock.floatingPanelConfigured = false;
        dock.floatingGapConfigured = false;
        QVERIFY(!dockTransitionRecordsAgree(dockSnapshot));
        dock.floatingGapConfigured = true;
        dock.floatingPanelEligible = true;
        QVERIFY(!dockTransitionRecordsAgree(dockSnapshot));
        dock.floatingPanelEligible = false;
        dock.dockGapHideRequested = false;
        QVERIFY(!dockTransitionRecordsAgree(dockSnapshot));
    }

    {
        //! Justify Dock touch authority is solved from persistent placement,
        //! not from the rendered background. At 60%, float truncation places
        //! the stable trigger one pixel before the QML-rendered rectangle.
        DockSystemSnapshot justifyDockSnapshot;
        DockSystemViewRecord justifyDock;
        justifyDock.runtimeViewId = 9;
        justifyDock.persistentDockId = 9;
        justifyDock.logicalDockId = 9;
        justifyDock.type = Types::DockView;
        justifyDock.edge = Plasma::Types::TopEdge;
        justifyDock.alignment = Types::Justify;
        justifyDock.maximumLengthRatio = 0.6F;
        justifyDock.maximumNormalThickness = 106;
        justifyDock.screenEdgeMargin = 18;
        justifyDock.presentedScreenEdgeGap = 18;
        justifyDock.windowGeometry = QRect(0, 0, 1600, 384);
        justifyDock.absoluteGeometry = QRect(320, 18, 960, 88);
        justifyDock.screenGeometry = QRect(0, 0, 1600, 1000);
        justifyDock.floatingGapConfigured = true;
        justifyDock.floatingPanelConfigured = false;
        justifyDock.attachOnWindowTouchConfigured = true;
        justifyDock.visibilityMode = Types::AlwaysVisible;
        justifyDock.transitionTarget = DockTransitionTarget::Floated;
        justifyDock.transitionProgress = 1.0;
        justifyDock.enabledBorders = {
            QStringLiteral("top"),
            QStringLiteral("right"),
            QStringLiteral("bottom"),
            QStringLiteral("left"),
        };
        justifyDock.geometrySettled = true;
        justifyDock.inReadyState = true;
        justifyDock.stableTriggerGeometry = QRect(319, 1, 960, 106);
        justifyDock.objects.transitionController =
            QStringLiteral("justify-dock-transition");
        justifyDock.objects.windowTouchTracker =
            QStringLiteral("justify-dock-window-touch");
        justifyDockSnapshot.views = {justifyDock};

        QVERIFY(dockTransitionRecordsAgree(justifyDockSnapshot));
        justifyDockSnapshot.views[0].stableTriggerGeometry =
            QRect(320, 1, 960, 106);
        QVERIFY(!dockTransitionRecordsAgree(justifyDockSnapshot));
    }

    {
        DockSystemSnapshot deferredSnapshot = valid;
        auto &deferred = deferredSnapshot.views[0];
        deferred.attachOnWindowTouchConfigured = true;
        deferred.attachmentWaitsForPointerExitConfigured = true;
        deferred.pointerInsideView = true;
        deferred.attachmentDeferredByPointer = true;
        deferred.touchingWindowCount = 1;
        deferred.windowTouchGeometryRoleType =
            QStringLiteral("QRect");
        QVERIFY(dockTransitionRecordsAgree(deferredSnapshot));

        deferred.attachmentWaitsForPointerExitConfigured = false;
        QVERIFY(!dockTransitionRecordsAgree(deferredSnapshot));
        deferred.attachmentWaitsForPointerExitConfigured = true;
        deferred.pointerInsideView = false;
        QVERIFY(!dockTransitionRecordsAgree(deferredSnapshot));
    }

    {
        auto pending = valid;
        pending.views[0].floatingDamageMaskPending = true;
        pending.views[0].appliedInputMask =
            pending.views[0].inputMask.united(
                pending.views[0].inputMask.translated(0, -1));
        QVERIFY(dockTransitionRecordsAgree(pending));
    }
    rejects([](auto &snapshot) {
        snapshot.views[0].floatingDamageMaskPending = true;
        snapshot.views[0].floatingDamageMaskGeneration = 0;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].enabledBorders.removeLast();
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].shadowPaddingOffsets->setBottom(-3);
    });
    rejects([](auto &snapshot) {
        snapshot.views[0]
            .shadowEnabledBorders
            ->removeLast();
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].floatingAppletPopupsPreferred = false;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].floatingAnchorRevision = 0;
    });
    {
        auto noShadow = valid;
        noShadow.views[0].shadowEnabledBorders.reset();
        noShadow.views[0].shadowPaddingOffsets.reset();
        QVERIFY(dockTransitionRecordsAgree(noShadow));
    }
    rejects([](auto &snapshot) {
        snapshot.views[0].shadowEnabledBorders.reset();
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].visibilityMode =
            Types::SidebarOnDemand;
        snapshot.views[0].floatingDamageMaskPending = true;
    });

    DockSystemSnapshot ineligible;
    auto ineligibleView =
        stableBottomTransitionRecord();
    ineligibleView.floatingPanelEligible = false;
    ineligibleView.transitionTarget =
        DockTransitionTarget::Floated;
    ineligibleView.transitionProgress = 1.0;
    ineligibleView.presentedScreenEdgeGap =
        ineligibleView.screenEdgeMargin;
    ineligibleView.transitionPhase =
        DockTransitionPhase::Resting;
    ineligibleView.transitionDirection =
        DockTransitionDirection::None;
    ineligibleView.transitionRunning = false;
    ineligibleView.currentVisibleGeometry =
        QRectF(
            *ineligibleView
                .floatedPresentationGeometry);
    ineligibleView.computedPaintMaskGeometry =
        ineligibleView.currentVisibleGeometry;
    ineligibleView.computedInputBridgeGeometry =
        QRectF(0.0, 0.0, 300.0, 47.0);
    ineligibleView.effectsRect =
        QRect(0, 0, 300, 40);
    ineligibleView.maskRect =
        ineligibleView.effectsRect;
    ineligibleView.inputMask =
        QRect(0, 0, 300, 47);
    ineligibleView.appliedInputMask =
        ineligibleView.inputMask;
    ineligibleView.shadowPaddingOffsets =
        QMargins(0, 0, 0, -7);
    ineligibleView.contentTranslation =
        QPointF{};
    ineligible.views = {
        ineligibleView};
    QVERIFY(
        dockTransitionRecordsAgree(
            ineligible));

    const auto setBottomProgress =
        [](DockSystemViewRecord &record, qreal progress) {
            record.transitionProgress = progress;
            record.presentedScreenEdgeGap =
                qRound(
                    static_cast<qreal>(
                        record.screenEdgeMargin)
                    * progress);
            const qreal visibleY = 7.0 * (1.0 - progress);
            record.currentVisibleGeometry =
                QRectF(0.0, visibleY, 300.0, 40.0);
            record.computedPaintMaskGeometry =
                record.currentVisibleGeometry;
            record.computedInputBridgeGeometry =
                record.currentVisibleGeometry->united(
                    QRectF(*record.attachedPresentationGeometry));
            record.effectsRect =
                record.currentVisibleGeometry->toAlignedRect();
            record.maskRect = record.effectsRect;
            record.inputMask =
                record.computedInputBridgeGeometry->toAlignedRect();
            record.appliedInputMask = record.inputMask;
            const QRect paint =
                record.effectsRect;
            const QSize canvasSize =
                record.stableCanvasGeometry->size();
            record.shadowPaddingOffsets = {
                -paint.x(),
                -paint.y(),
                -(canvasSize.width()
                  - (paint.x() + paint.width())),
                -(canvasSize.height()
                  - (paint.y() + paint.height())),
            };
            record.contentTranslation = QPointF(0.0, visibleY);
        };

    DockSystemSnapshot attachedPanel = valid;
    auto &attachedView = attachedPanel.views[0];
    attachedView.attachOnWindowTouchConfigured = true;
    attachedView.touchingWindowCount = 1;
    attachedView.windowTouchGeometryRoleType =
        QStringLiteral("QRect");
    attachedView.transitionTarget =
        DockTransitionTarget::Attached;
    attachedView.transitionPhase =
        DockTransitionPhase::Resting;
    attachedView.transitionDirection =
        DockTransitionDirection::None;
    attachedView.transitionRunning = false;
    attachedView.floatingAppletPopupsPreferred = false;
    setBottomProgress(attachedView, 0.0);
    attachedView.enabledBorders.removeAll(
        QStringLiteral("bottom"));
    attachedView.shadowEnabledBorders =
        attachedView.enabledBorders;
    QVERIFY(dockTransitionRecordsAgree(attachedPanel));

    attachedView.enabledBorders.insert(
        2, QStringLiteral("bottom"));
    attachedView.shadowEnabledBorders =
        attachedView.enabledBorders;
    QVERIFY(!dockTransitionRecordsAgree(attachedPanel));

    DockSystemSnapshot flushPanel;
    auto flushView = stableBottomTransitionRecord();
    flushView.floatingGapConfigured = false;
    flushView.floatingPanelConfigured = false;
    flushView.screenEdgeMargin = 0;
    flushView.presentedScreenEdgeGap = 0;
    flushView.floatingPanelEligible = false;
    flushView.floatingAppletPopupsPreferred = false;
    flushView.transitionTarget =
        DockTransitionTarget::Floated;
    flushView.transitionProgress = 1.0;
    flushView.transitionPhase =
        DockTransitionPhase::Resting;
    flushView.transitionDirection =
        DockTransitionDirection::None;
    flushView.transitionRunning = false;
    flushView.windowGeometry =
        QRect(100, 960, 300, 40);
    flushView.surfaceGeometry =
        flushView.windowGeometry;
    flushView.stableCanvasGeometry =
        flushView.windowGeometry;
    flushView.stableTriggerGeometry =
        QRect(100, 959, 300, 40);
    flushView.attachedPresentationGeometry =
        QRect(0, 0, 300, 40);
    flushView.floatedPresentationGeometry =
        flushView.attachedPresentationGeometry;
    flushView.currentVisibleGeometry =
        QRectF(*flushView.attachedPresentationGeometry);
    flushView.computedPaintMaskGeometry =
        flushView.currentVisibleGeometry;
    flushView.computedInputBridgeGeometry =
        flushView.currentVisibleGeometry;
    flushView.effectsRect = QRect(0, 0, 300, 40);
    flushView.maskRect = flushView.effectsRect;
    flushView.inputMask = flushView.effectsRect;
    flushView.appliedInputMask = flushView.inputMask;
    flushView.shadowPaddingOffsets =
        QMargins{};
    flushView.contentTranslation = QPointF{};
    flushPanel.views = {flushView};
    QVERIFY(dockTransitionRecordsAgree(flushPanel));

    DockSystemSnapshot sidebarPanel;
    auto sidebarView = ineligibleView;
    sidebarView.visibilityMode =
        Types::SidebarOnDemand;
    sidebarView.inputMask = QRect(0, 46, 300, 1);
    sidebarView.appliedInputMask =
        sidebarView.inputMask;
    sidebarPanel.views = {sidebarView};
    QVERIFY(dockTransitionRecordsAgree(sidebarPanel));

    DockSystemSnapshot nearFloated = ineligible;
    setBottomProgress(nearFloated.views[0], 1.0 - 1e-13);
    QVERIFY(!dockTransitionRecordsAgree(nearFloated));

    DockSystemSnapshot nearAttached = ineligible;
    nearAttached.views[0].floatingPanelEligible = true;
    nearAttached.views[0].transitionTarget =
        DockTransitionTarget::Attached;
    nearAttached.views[0].floatingAppletPopupsPreferred =
        false;
    setBottomProgress(nearAttached.views[0], 1e-13);
    QVERIFY(!dockTransitionRecordsAgree(nearAttached));
}

void DbusReportsTest::dockSystemSnapshotRejectsReservationDisagreement()
{
    DockReservationGroupRecord group;
    group.outputId = 10;
    group.edge = Plasma::Types::BottomEdge;
    group.generation = 4;
    group.publishedDepth = 48;
    group.contributorDockIds = {1, 2};
    group.geometry = QRect(0, 952, 1600, 48);
    group.windowGeometry = QRect(0, 0, 1600, 1);
    group.layerShellPresent = true;
    group.layerShellAnchors = {
        QStringLiteral("bottom"),
        QStringLiteral("left"),
        QStringLiteral("right")};
    group.layerShellExclusiveEdge =
        QStringLiteral("bottom");
    group.layerShellExclusiveZone = 48;
    group.publisher = QStringLiteral("object-9");

    const auto contributor =
        [&group](const uint id, const int depth) {
            DockSystemViewRecord view;
            view.persistentDockId = id;
            view.logicalDockId = id;
            view.screenId = group.outputId;
            view.edge = group.edge;
            view.reservationSurfacePresent = true;
            view.reservationOutputId =
                group.outputId;
            view.reservationEdge = group.edge;
            view.reservationContributionDepth =
                depth;
            view.publishedStruts =
                QRect(0, 1000 - depth, 1600, depth);
            view.reservationPublishedDepth =
                group.publishedDepth;
            view.reservationGroupMemberCount =
                static_cast<int>(
                    group.contributorDockIds.size());
            view.reservationGroupGeneration =
                group.generation;
            view.reservationContributorDockIds =
                group.contributorDockIds;
            view.reservationGeometry =
                group.geometry;
            view.reservationWindowGeometry =
                group.windowGeometry;
            view.reservationLayerShellAnchors =
                group.layerShellAnchors;
            view.reservationLayerShellMargins =
                group.layerShellMargins;
            view.reservationLayerShellExclusiveEdge =
                group.layerShellExclusiveEdge;
            view.reservationLayerShellExclusiveZone =
                group.layerShellExclusiveZone;
            view.objects.reservationPublisher =
                group.publisher;
            return view;
        };

    DockSystemSnapshot valid;
    valid.reservationStateGeneration = 4;
    valid.reservationGroups = {group};
    DockSystemViewRecord independent;
    independent.persistentDockId = 3;
    independent.logicalDockId = 3;
    valid.views = {
        contributor(1, 40),
        contributor(2, 48),
        independent};
    QVERIFY(dockReservationRecordsAgree(valid));

    const auto rejects =
        [&valid](const auto &mutate) {
            DockSystemSnapshot invalid = valid;
            mutate(invalid);
            QVERIFY(!dockReservationRecordsAgree(invalid));
        };
    rejects([](auto &snapshot) {
        snapshot.reservationGroups[0].publisher.clear();
    });
    rejects([](auto &snapshot) {
        snapshot.reservationGroups[0].geometry = QRect();
    });
    rejects([](auto &snapshot) {
        snapshot.reservationGroups[0].windowGeometry = QRect();
    });
    rejects([](auto &snapshot) {
        snapshot.reservationGroups[0].layerShellPresent = false;
    });
    rejects([](auto &snapshot) {
        snapshot.reservationGroups[0].layerShellAnchors.clear();
    });
    rejects([](auto &snapshot) {
        snapshot.reservationGroups[0].generation = 5;
    });
    rejects([](auto &snapshot) {
        snapshot.reservationGroups[0].contributorDockIds.removeLast();
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].reservationSurfacePresent = false;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].screenId = 11;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].edge = Plasma::Types::TopEdge;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].reservationOutputId = 11;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].reservationEdge =
            Plasma::Types::TopEdge;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].publishedStruts.setHeight(39);
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].reservationPublishedDepth = 47;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].reservationGroupMemberCount = 1;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].reservationGroupGeneration = 3;
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].reservationContributorDockIds.removeLast();
    });
    rejects([](auto &snapshot) {
        std::swap(
            snapshot.views[0].reservationContributorDockIds[0],
            snapshot.views[0].reservationContributorDockIds[1]);
    });
    rejects([](auto &snapshot) {
        std::swap(
            snapshot.reservationGroups[0].contributorDockIds[0],
            snapshot.reservationGroups[0].contributorDockIds[1]);
        for (auto &view : snapshot.views) {
            if (view.reservationContributorDockIds.size() == 2) {
                std::swap(
                    view.reservationContributorDockIds[0],
                    view.reservationContributorDockIds[1]);
            }
        }
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].reservationGeometry.translate(1, 0);
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].objects.reservationPublisher =
            QStringLiteral("object-10");
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].reservationWindowGeometry.translate(1, 0);
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].reservationLayerShellAnchors.removeLast();
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].reservationLayerShellMargins =
            QMargins(1, 0, 0, 0);
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].reservationLayerShellExclusiveEdge =
            QStringLiteral("top");
    });
    rejects([](auto &snapshot) {
        snapshot.views[0].reservationLayerShellExclusiveZone = 47;
    });
    rejects([](auto &snapshot) {
        snapshot.views[1].reservationContributionDepth = 47;
    });
    rejects([](auto &snapshot) {
        snapshot.views.clear();
    });
    rejects([](auto &snapshot) {
        snapshot.reservationGroups.clear();
    });

    const auto rejectsIndependentResidue =
        [&valid](const auto &mutate) {
            DockSystemSnapshot invalid = valid;
            mutate(invalid.views[2]);
            QVERIFY(!dockReservationRecordsAgree(invalid));
        };
    rejectsIndependentResidue(
        [&group](auto &view) {
            view.publishedStruts = group.geometry;
        });
    rejectsIndependentResidue(
        [](auto &view) {
            view.publishedStruts = QRect(1, 2, 0, 0);
        });
    rejectsIndependentResidue(
        [](auto &view) {
            view.reservationSurfacePresent = true;
        });
    rejectsIndependentResidue(
        [](auto &view) {
            view.reservationOutputId = 10;
        });
    rejectsIndependentResidue(
        [](auto &view) {
            view.reservationEdge =
                Plasma::Types::BottomEdge;
        });
    rejectsIndependentResidue(
        [](auto &view) {
            view.reservationContributionDepth = 1;
        });
    rejectsIndependentResidue(
        [](auto &view) {
            view.reservationPublishedDepth = 1;
        });
    rejectsIndependentResidue(
        [](auto &view) {
            view.reservationGroupMemberCount = 1;
        });
    rejectsIndependentResidue(
        [](auto &view) {
            view.reservationGroupGeneration = 1;
        });
    rejectsIndependentResidue(
        [](auto &view) {
            view.reservationContributorDockIds = {3};
        });
    rejectsIndependentResidue(
        [&group](auto &view) {
            view.reservationGeometry = group.geometry;
        });
    rejectsIndependentResidue(
        [&group](auto &view) {
            view.reservationWindowGeometry =
                group.windowGeometry;
        });
    rejectsIndependentResidue(
        [&group](auto &view) {
            view.reservationLayerShellAnchors =
                group.layerShellAnchors;
        });
    rejectsIndependentResidue(
        [](auto &view) {
            view.reservationLayerShellMargins =
                QMargins(1, 0, 0, 0);
        });
    rejectsIndependentResidue(
        [](auto &view) {
            view.reservationLayerShellExclusiveEdge =
                QStringLiteral("bottom");
        });
    rejectsIndependentResidue(
        [](auto &view) {
            view.reservationLayerShellExclusiveZone = 1;
        });
    rejectsIndependentResidue(
        [](auto &view) {
            view.objects.reservationPublisher =
                QStringLiteral("object-10");
        });
}

void DbusReportsTest::dockSystemSnapshotCanonicalizesShuffledViewsAndLinkedIds()
{
    DockSystemViewRecord original;
    original.runtimeViewId = 30;
    original.persistentDockId = 30;
    original.logicalDockId = 30;
    original.relationship = DockRelationship::LinkedRoot;
    original.screensGroup = Types::AllScreensGroup;

    DockSystemViewRecord highClone;
    highClone.runtimeViewId = 51;
    highClone.persistentDockId = 51;
    highClone.logicalDockId = 30;
    highClone.originalDockId = 30;
    highClone.relationship = DockRelationship::LinkedMember;
    highClone.linkPlacement = Data::View::LinkPlacement::ScreenGroupDerived;
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
    DockReservationGroupRecord secondaryBottom;
    secondaryBottom.outputId = 11;
    secondaryBottom.edge = Plasma::Types::BottomEdge;
    secondaryBottom.generation = 8;
    secondaryBottom.contributorDockIds = {51, 41};
    DockReservationGroupRecord primaryLeft;
    primaryLeft.outputId = 10;
    primaryLeft.edge = Plasma::Types::LeftEdge;
    primaryLeft.generation = 7;
    primaryLeft.contributorDockIds = {30, 7};
    first.reservationGroups = {
        secondaryBottom,
        primaryLeft};
    DockSystemSnapshot shuffled = first;
    shuffled.views = {lowClone, independent, highClone, original};
    shuffled.reservationGroups = {
        primaryLeft,
        secondaryBottom};
    shuffled.reservationGroups[0].contributorDockIds =
        {7, 30};

    QCOMPARE(serializeDockSystemSnapshot(first), serializeDockSystemSnapshot(shuffled));

    const QJsonObject root =
        QJsonDocument::fromJson(
            serializeDockSystemSnapshot(first).toUtf8()).object();
    const QJsonArray views =
        root.value(QStringLiteral("views")).toArray();
    QCOMPARE(views.at(0).toObject().value(QStringLiteral("persistentDockId")).toInt(), 7);
    QCOMPARE(views.at(1).toObject().value(QStringLiteral("persistentDockId")).toInt(), 30);
    QCOMPARE(views.at(2).toObject().value(QStringLiteral("persistentDockId")).toInt(), 41);
    QCOMPARE(views.at(3).toObject().value(QStringLiteral("persistentDockId")).toInt(), 51);
    QCOMPARE(views.at(1).toObject().value(QStringLiteral("linkedDockIds")).toArray(),
             (QJsonArray{41, 51}));
    const QJsonArray groups =
        root.value(
            QStringLiteral("reservationGroups")).toArray();
    QCOMPARE(
        groups.at(0).toObject()
            .value(QStringLiteral("outputId")).toInt(),
        10);
    QCOMPARE(
        groups.at(1).toObject()
            .value(QStringLiteral("outputId")).toInt(),
        11);
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
