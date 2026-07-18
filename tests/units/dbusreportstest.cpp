/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The pure layer of the viewsData() D-Bus read
// (docs/dbus-observability-interface.md): ViewRecord -> JSON serialization
// and the enum-name mappings. The live collectors in app/dbusreports.cpp
// are three-line field reads off View and stay exercised by the running
// dock; everything a consumer parses is pinned here.

#include "dbusreports.h"

#include <QJsonDocument>
#include <QTest>

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
    void visibilityModeNames_data();
    void visibilityModeNames();
    void rectSerialization();
    void recordSerialization();
    void viewRecordKeySet();
    void emptyInputMaskSerializesAsEmptyRegion();
    void recordsSerializeAsCompactJsonArray();

    void appletRecordSerialization();
    void appletRecordKeySet();
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
};

//! the exact sorted key list of a serialized record: the schema pin that
//! makes accidental drift from docs/dbus-observability-interface.md fail a
//! test instead of a D-Bus consumer
static QStringList sortedKeys(const QJsonObject &json)
{
    QStringList keys = json.keys();
    keys.sort();
    return keys;
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
//! consumer parses is pinned against docs/dbus-observability-interface.md
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

//! one fully populated applet record, pinning every field name and value
//! type of viewAppletsData() against docs/dbus-observability-interface.md
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

    const QJsonObject json = serializeAppletRecord(record);

    QCOMPARE(json.value(QStringLiteral("id")).toInt(), 12);
    QCOMPARE(json.value(QStringLiteral("plugin")).toString(), QStringLiteral("org.kde.latte.plasmoid"));
    QCOMPARE(json.value(QStringLiteral("index")).toInt(), 3);
    QCOMPARE(json.value(QStringLiteral("geometry")).toArray(), serializeRect(QRect(4, 5, 6, 7)));
    QCOMPARE(json.value(QStringLiteral("isExpanded")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("inScheduledDestruction")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("lockedZoom")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("colorizingBlocked")).toBool(), true);
}

void DbusReportsTest::appletRecordKeySet()
{
    const QStringList expected{
        QStringLiteral("colorizingBlocked"), QStringLiteral("geometry"),
        QStringLiteral("id"), QStringLiteral("inScheduledDestruction"),
        QStringLiteral("index"), QStringLiteral("isExpanded"),
        QStringLiteral("lockedZoom"), QStringLiteral("plugin")};

    QCOMPARE(sortedKeys(serializeAppletRecord(AppletRecord{})), expected);
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

//! The G1 applet-id-order readback (docs/e2e-interaction-test-plan.md):
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

//! The G3 drop-marker sentinel contract (docs/e2e-interaction-test-plan.md):
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
//! type of trackerData() against docs/dbus-observability-interface.md
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
//! type of viewTasksData() against docs/dbus-observability-interface.md -
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
}

void DbusReportsTest::colorizerRecordKeySet()
{
    const QStringList expected{
        QStringLiteral("applyingWindowColors"), QStringLiteral("backgroundIsBusy"),
        QStringLiteral("colorizerPresent"), QStringLiteral("containmentId"),
        QStringLiteral("currentBackgroundBrightness"), QStringLiteral("enabled"),
        QStringLiteral("mustBeShown"), QStringLiteral("scheme"),
        QStringLiteral("themeColorsMode"), QStringLiteral("windowColorsMode")};

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
//! type of layoutsData() against docs/dbus-observability-interface.md
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

QTEST_GUILESS_MAIN(DbusReportsTest)
#include "dbusreportstest.moc"
