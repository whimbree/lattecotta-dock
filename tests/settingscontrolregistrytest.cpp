/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "settingscontrolregistry.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <memory>

using namespace Latte;

namespace
{

class GeometryProvider : public SettingsControlSurfaceGeometryProvider
{
  public:
    QRectF geometry{1000.5, 2000.25, 240.0, 180.0};

    std::optional<QRectF> settingsControlSurfaceGeometry() const override
    {
        return geometry;
    }
};

struct Fixture
{
    SettingsControlRegistry registry;
    GeometryProvider geometryProvider;
    QQmlEngine engine;
    QQuickWindow window;
    QQuickItem *root{nullptr};
    std::unique_ptr<QObject> lifetime{std::make_unique<QObject>()};

    Fixture()
    {
        window.resize(240, 180);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/settingscontrolregistryfixture/Fixture.qml")));
        root = qobject_cast<QQuickItem *>(component.create());
        if (!root)
        {
            qFatal("settings-control fixture failed to load: %s", qPrintable(component.errorString()));
        }
        root->setParent(window.contentItem());
        root->setParentItem(window.contentItem());
        window.show();
        QCoreApplication::processEvents();
    }

    QQuickItem *item(const char *name) const
    {
        return root->findChild<QQuickItem *>(QString::fromLatin1(name));
    }

    quint64 openScope(uint containmentId = 7, QString surface = QStringLiteral("fixture.primary"))
    {
        const auto generation = registry.replaceScope(
            {containmentId, std::move(surface), std::nullopt, lifetime.get(), root, &geometryProvider});
        if (!generation)
        {
            qFatal("fixture scope registration failed");
        }
        return *generation;
    }

    quint64 addControl(quint64 generation, QQuickItem *control, QString identity, QString kind,
                       std::optional<QString> instance = std::nullopt,
                       std::optional<SettingsControlRegistry::PopupDescriptor> popup = std::nullopt)
    {
        const auto token = registry.registerControl(generation, {std::move(identity),
                                                                 std::move(kind),
                                                                 std::move(instance),
                                                                 control,
                                                                 QByteArrayLiteral("currentValue"),
                                                                 {{QStringLiteral("primary"), control}},
                                                                 popup});
        if (!token)
        {
            qFatal("fixture control registration failed");
        }
        return *token;
    }

    QJsonArray data(uint containmentId = 7)
    {
        return QJsonDocument::fromJson(registry.viewSettingsControlsData(containmentId).toUtf8()).array();
    }
};

QJsonObject byInstance(const QJsonArray &records, const QString &instance)
{
    for (const auto &value : records)
    {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("instanceKey")).toString() == instance)
        {
            return object;
        }
    }
    return {};
}

QJsonObject byIdentity(const QJsonArray &records, const QString &identity)
{
    for (const auto &value : records)
    {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("auditIdentity")).toString() == identity)
        {
            return object;
        }
    }
    return {};
}

} // namespace

class SettingsControlRegistryTest : public QObject
{
    Q_OBJECT

  private Q_SLOTS:
    void snapshotsLiveGeometryStateFocusAndCurrentValues();
    void popupRowsTrackOpenGenerationsAndIndividualDestruction();
    void controlAndOwnerDestructionCleanOnlyTheirScope();
    void reloadAndRetargetAllocateStrictlyNewerGenerations();
    void duplicateMalformedAndUnsupportedLiveEntriesRefuse();
};

void SettingsControlRegistryTest::snapshotsLiveGeometryStateFocusAndCurrentValues()
{
    Fixture fixture;
    const quint64 generation = fixture.openScope();
    fixture.addControl(generation, fixture.item("controlB"), QStringLiteral("same.audit"), QStringLiteral("choice"),
                       QStringLiteral("b"));
    fixture.addControl(generation, fixture.item("controlA"), QStringLiteral("same.audit"), QStringLiteral("choice"),
                       QStringLiteral("a"));
    fixture.addControl(generation, fixture.item("hiddenControl"), QStringLiteral("hidden.audit"),
                       QStringLiteral("button"));
    fixture.addControl(generation, fixture.item("disabledControl"), QStringLiteral("disabled.audit"),
                       QStringLiteral("button"));

    fixture.window.requestActivate();
    fixture.item("focusChild")->forceActiveFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();

    const QString firstJson = fixture.registry.viewSettingsControlsData(7);
    QCOMPARE(firstJson, fixture.registry.viewSettingsControlsData(7));
    const QJsonArray records = QJsonDocument::fromJson(firstJson.toUtf8()).array();
    QCOMPARE(records.size(), 4);

    const QJsonObject first = byInstance(records, QStringLiteral("a"));
    const QJsonObject second = byInstance(records, QStringLiteral("b"));
    QVERIFY(first.value(QStringLiteral("focused")).toBool());
    QCOMPARE(first.value(QStringLiteral("current")).toString(), QStringLiteral("alpha"));
    QCOMPARE(second.value(QStringLiteral("current")).toBool(), true);

    const QJsonObject firstHit = first.value(QStringLiteral("hits")).toArray().at(0).toObject();
    QCOMPARE(firstHit.value(QStringLiteral("globalGeometry")).toArray(), (QJsonArray{1030.5, 2030.25, 40.0, 20.0}));
    QCOMPARE(firstHit.value(QStringLiteral("clippedGeometry")).toArray(),
             firstHit.value(QStringLiteral("globalGeometry")).toArray());
    const QJsonObject secondHit = second.value(QStringLiteral("hits")).toArray().at(0).toObject();
    QCOMPARE(secondHit.value(QStringLiteral("globalGeometry")).toArray(), (QJsonArray{1090.5, 2055.25, 60.0, 30.0}));
    QCOMPARE(secondHit.value(QStringLiteral("clippedGeometry")).toArray(), (QJsonArray{1090.5, 2055.25, 30.0, 30.0}));
    QVERIFY(secondHit.value(QStringLiteral("mapped")).toBool());

    const QJsonObject hidden = byIdentity(records, QStringLiteral("hidden.audit"));
    QVERIFY(!hidden.value(QStringLiteral("visible")).toBool());
    const QJsonObject hiddenHit = hidden.value(QStringLiteral("hits")).toArray().at(0).toObject();
    QVERIFY(!hiddenHit.value(QStringLiteral("mapped")).toBool());
    QVERIFY(hiddenHit.value(QStringLiteral("clippedGeometry")).isNull());
    QVERIFY(!byIdentity(records, QStringLiteral("disabled.audit")).value(QStringLiteral("enabled")).toBool());

    fixture.item("controlA")->setProperty("currentValue", QStringLiteral("changed"));
    fixture.geometryProvider.geometry.translate(50.25, 10.5);
    const QJsonObject live = byInstance(fixture.data(), QStringLiteral("a"));
    QCOMPARE(live.value(QStringLiteral("current")).toString(), QStringLiteral("changed"));
    QCOMPARE(
        live.value(QStringLiteral("hits")).toArray().at(0).toObject().value(QStringLiteral("globalGeometry")).toArray(),
        (QJsonArray{1080.75, 2040.75, 40.0, 20.0}));
}

void SettingsControlRegistryTest::popupRowsTrackOpenGenerationsAndIndividualDestruction()
{
    Fixture fixture;
    const quint64 generation = fixture.openScope();
    QQuickItem *popupControl = fixture.item("popupControl");
    const quint64 token = fixture.addControl(
        generation, popupControl, QStringLiteral("popup.audit"), QStringLiteral("combo"), std::nullopt,
        SettingsControlRegistry::PopupDescriptor{popupControl, QByteArrayLiteral("popupOpen"),
                                                 fixture.item("popupItem")});

    QQuickItem *row = fixture.item("popupRow");
    QQuickItem *separator = fixture.item("popupSeparator");
    QVERIFY(fixture.registry.registerPopupRow(token, {QStringLiteral("popup.row.first"),
                                                      QStringLiteral("option"),
                                                      std::nullopt,
                                                      0,
                                                      QStringLiteral("stable-first"),
                                                      row,
                                                      QByteArrayLiteral("currentValue"),
                                                      {{QStringLiteral("row"), row}}}));
    QVERIFY(fixture.registry.registerPopupRow(token, {QStringLiteral("popup.row.separator"),
                                                      QStringLiteral("separator"),
                                                      std::nullopt,
                                                      1,
                                                      std::monostate{},
                                                      separator,
                                                      QByteArrayLiteral("currentValue"),
                                                      {{QStringLiteral("row"), separator}}}));

    QJsonObject popup = fixture.data().at(0).toObject().value(QStringLiteral("popup")).toObject();
    QVERIFY(!popup.value(QStringLiteral("open")).toBool());
    QVERIFY(!popup.value(QStringLiteral("mapped")).toBool());
    QVERIFY(popup.value(QStringLiteral("generation")).isNull());
    QCOMPARE(popup.value(QStringLiteral("rows")).toArray().size(), 2);

    popupControl->setProperty("popupOpen", true);
    QCoreApplication::processEvents();
    popup = fixture.data().at(0).toObject().value(QStringLiteral("popup")).toObject();
    QVERIFY(popup.value(QStringLiteral("open")).toBool());
    QVERIFY(popup.value(QStringLiteral("mapped")).toBool());
    const quint64 firstPopupGeneration = popup.value(QStringLiteral("generation")).toString().toULongLong();
    QVERIFY(firstPopupGeneration > generation);
    const QJsonObject separatorJson = popup.value(QStringLiteral("rows")).toArray().at(1).toObject();
    QCOMPARE(separatorJson.value(QStringLiteral("kind")).toString(), QStringLiteral("separator"));
    QVERIFY(separatorJson.value(QStringLiteral("value")).isNull());
    QVERIFY(!separatorJson.value(QStringLiteral("enabled")).toBool());

    delete row;
    popup = fixture.data().at(0).toObject().value(QStringLiteral("popup")).toObject();
    QCOMPARE(popup.value(QStringLiteral("rows")).toArray().size(), 1);
    QCOMPARE(popup.value(QStringLiteral("rows"))
                 .toArray()
                 .at(0)
                 .toObject()
                 .value(QStringLiteral("auditIdentity"))
                 .toString(),
             QStringLiteral("popup.row.separator"));

    popupControl->setProperty("popupOpen", false);
    popupControl->setProperty("popupOpen", true);
    QCoreApplication::processEvents();
    popup = fixture.data().at(0).toObject().value(QStringLiteral("popup")).toObject();
    QVERIFY(popup.value(QStringLiteral("generation")).toString().toULongLong() > firstPopupGeneration);
}

void SettingsControlRegistryTest::controlAndOwnerDestructionCleanOnlyTheirScope()
{
    Fixture fixture;
    const quint64 generation = fixture.openScope();
    QQuickItem *first = fixture.item("controlA");
    fixture.addControl(generation, first, QStringLiteral("first"), QStringLiteral("button"));
    fixture.addControl(generation, fixture.item("controlB"), QStringLiteral("second"), QStringLiteral("button"));
    QCOMPARE(fixture.data().size(), 2);

    delete first;
    QJsonArray records = fixture.data();
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.at(0).toObject().value(QStringLiteral("auditIdentity")).toString(), QStringLiteral("second"));

    fixture.lifetime.reset();
    QCOMPARE(fixture.registry.viewSettingsControlsData(7), QStringLiteral("[]"));
}

void SettingsControlRegistryTest::reloadAndRetargetAllocateStrictlyNewerGenerations()
{
    Fixture fixture;
    const quint64 first = fixture.openScope();
    fixture.addControl(first, fixture.item("controlA"), QStringLiteral("old"), QStringLiteral("button"));

    const quint64 reload = fixture.openScope();
    QVERIFY(reload > first);
    QCOMPARE(fixture.registry.viewSettingsControlsData(7), QStringLiteral("[]"));
    fixture.addControl(reload, fixture.item("controlB"), QStringLiteral("reload"), QStringLiteral("button"));

    const quint64 retarget = fixture.openScope(8, QStringLiteral("fixture.secondary"));
    QVERIFY(retarget > reload);
    QCOMPARE(fixture.registry.viewSettingsControlsData(7), QStringLiteral("[]"));
    QCOMPARE(fixture.registry.viewSettingsControlsData(8), QStringLiteral("[]"));
    fixture.addControl(retarget, fixture.item("controlA"), QStringLiteral("retarget"), QStringLiteral("button"));
    QCOMPARE(fixture.data(8).at(0).toObject().value(QStringLiteral("loadGeneration")).toString(),
             QString::number(retarget));
}

void SettingsControlRegistryTest::duplicateMalformedAndUnsupportedLiveEntriesRefuse()
{
    Fixture fixture;
    const quint64 generation = fixture.openScope();
    QQuickItem *control = fixture.item("controlA");
    fixture.addControl(generation, control, QStringLiteral("duplicate"), QStringLiteral("button"),
                       QStringLiteral("one"));
    QVERIFY(!fixture.registry.registerControl(generation, {QStringLiteral("duplicate"),
                                                           QStringLiteral("button"),
                                                           QStringLiteral("one"),
                                                           fixture.item("controlB"),
                                                           QByteArrayLiteral("currentValue"),
                                                           {{QStringLiteral("primary"), fixture.item("controlB")}},
                                                           std::nullopt}));
    QVERIFY(!fixture.registry.registerControl(generation, {QString(),
                                                           QStringLiteral("button"),
                                                           std::nullopt,
                                                           fixture.item("controlB"),
                                                           QByteArrayLiteral("currentValue"),
                                                           {{QStringLiteral("primary"), fixture.item("controlB")}},
                                                           std::nullopt}));
    QVERIFY(!fixture.registry.registerControl(generation, {QStringLiteral("missing-property"),
                                                           QStringLiteral("button"),
                                                           std::nullopt,
                                                           fixture.item("controlB"),
                                                           QByteArrayLiteral("missing"),
                                                           {{QStringLiteral("primary"), fixture.item("controlB")}},
                                                           std::nullopt}));
    QCOMPARE(fixture.data().size(), 1);

    control->setRotation(15.0);
    QCOMPARE(fixture.registry.viewSettingsControlsData(7), QStringLiteral("[]"));
    control->setRotation(0.0);
    control->setProperty("currentValue", QVariantList{1, 2});
    QCOMPARE(fixture.registry.viewSettingsControlsData(7), QStringLiteral("[]"));
}

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    SettingsControlRegistryTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "settingscontrolregistrytest.moc"
