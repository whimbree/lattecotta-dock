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
    record.editMode = true;
    record.inConfigureAppletsMode = true;

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

    QCOMPARE(json.value(QStringLiteral("editMode")).toBool(), true);
    QCOMPARE(json.value(QStringLiteral("inConfigureAppletsMode")).toBool(), true);
}

void DbusReportsTest::viewRecordKeySet()
{
    const QStringList expected{
        QStringLiteral("absoluteGeometry"), QStringLiteral("alignment"),
        QStringLiteral("containmentId"), QStringLiteral("edge"),
        QStringLiteral("editMode"), QStringLiteral("inConfigureAppletsMode"),
        QStringLiteral("inStartup"), QStringLiteral("inputRegionRects"),
        QStringLiteral("isCloned"), QStringLiteral("isClonedFrom"),
        QStringLiteral("isHidden"), QStringLiteral("isOffScreen"),
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

    QJsonObject json = serializeViewRecord(record);
    QVERIFY(json.value(QStringLiteral("inputRegionRects")).toArray().isEmpty());

    record.inputMask = QRect(0, 0, -1, -1); // the explicit clear request
    json = serializeViewRecord(record);
    QVERIFY(json.value(QStringLiteral("inputRegionRects")).toArray().isEmpty());
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

QTEST_GUILESS_MAIN(DbusReportsTest)
#include "dbusreportstest.moc"
