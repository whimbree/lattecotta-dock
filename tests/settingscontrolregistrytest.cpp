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
#include <QThread>

#include <memory>
#include <tuple>
#include <utility>

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

class PopupState : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool open READ isOpen WRITE setOpen NOTIFY openChanged)

  public:
    bool isOpen() const
    {
        return m_open;
    }

    void setOpen(bool open)
    {
        if (m_open == open)
        {
            return;
        }
        m_open = open;
        Q_EMIT openChanged();
    }

  Q_SIGNALS:
    void openChanged();

  private:
    bool m_open{false};
};

class ForeignThreadObjects
{
  public:
    ForeignThreadObjects()
    {
        m_thread.start();
    }

    ~ForeignThreadObjects()
    {
        QThread *guiThread = QCoreApplication::instance()->thread();
        for (QObject *object : std::as_const(m_objects))
        {
            const bool invoked = QMetaObject::invokeMethod(
                object, [object, guiThread]() { object->moveToThread(guiThread); }, Qt::BlockingQueuedConnection);
            if (!invoked || object->thread() != guiThread)
            {
                qFatal("failed to return foreign test object to the GUI thread");
            }
        }
        m_thread.quit();
        m_thread.wait();
        qDeleteAll(m_objects);
    }

    template<typename T>
    T *create()
    {
        auto *object = new T;
        if (!object->moveToThread(&m_thread))
        {
            qFatal("failed to move test object to the foreign thread");
        }
        m_objects.append(object);
        return object;
    }

  private:
    QThread m_thread;
    QList<QObject *> m_objects;
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
    void replacementsDisconnectRetiredLifecycleCallbacks();
    void crossThreadScopeAndDescriptorObjectsRetireRegistrations();
    void popupRowsRequirePopupAncestryAndUniqueWireValues();
    void registrationFailuresRetireWholeLoadGeneration();
    void unsupportedLiveEntriesRefuseWholeView();
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

void SettingsControlRegistryTest::replacementsDisconnectRetiredLifecycleCallbacks()
{
    Fixture fixture;
    auto firstOwner = std::make_unique<QObject>();
    auto secondOwner = std::make_unique<QObject>();
    auto thirdOwner = std::make_unique<QObject>();
    auto firstPopupState = std::make_unique<PopupState>();
    auto thirdPopupState = std::make_unique<PopupState>();

    const auto firstGeneration = fixture.registry.replaceScope(
        {7, QStringLiteral("fixture.primary"), std::nullopt, firstOwner.get(), fixture.root,
         &fixture.geometryProvider});
    QVERIFY(firstGeneration);
    const quint64 firstToken = fixture.addControl(
        *firstGeneration, fixture.item("controlA"), QStringLiteral("old.popup"), QStringLiteral("combo"),
        std::nullopt,
        SettingsControlRegistry::PopupDescriptor{firstPopupState.get(), QByteArrayLiteral("open"),
                                                 fixture.item("popupItem")});
    QVERIFY(fixture.registry.registerPopupRow(firstToken, {QStringLiteral("old.row"),
                                                           QStringLiteral("option"),
                                                           std::nullopt,
                                                           0,
                                                           QStringLiteral("old"),
                                                           fixture.item("popupRow"),
                                                           QByteArrayLiteral("currentValue"),
                                                           {{QStringLiteral("row"), fixture.item("popupRow")}}}));

    const auto secondGeneration = fixture.registry.replaceScope(
        {7, QStringLiteral("fixture.primary"), std::nullopt, secondOwner.get(), fixture.root,
         &fixture.geometryProvider});
    QVERIFY(secondGeneration);
    QVERIFY(*secondGeneration > *firstGeneration);
    fixture.addControl(*secondGeneration, fixture.item("controlB"), QStringLiteral("second"),
                       QStringLiteral("button"));

    firstPopupState->setOpen(true);
    firstPopupState.reset();
    firstOwner.reset();
    delete fixture.item("popupRow");
    delete fixture.item("controlA");
    QCOMPARE(fixture.data().size(), 1);
    QCOMPARE(fixture.data().at(0).toObject().value(QStringLiteral("auditIdentity")).toString(),
             QStringLiteral("second"));

    const auto thirdGeneration = fixture.registry.replaceScope(
        {7, QStringLiteral("fixture.primary"), std::nullopt, thirdOwner.get(), fixture.root,
         &fixture.geometryProvider});
    QVERIFY(thirdGeneration);
    QVERIFY(*thirdGeneration > *secondGeneration);
    fixture.addControl(*thirdGeneration, fixture.item("controlB"), QStringLiteral("current.popup"),
                       QStringLiteral("combo"), std::nullopt,
                       SettingsControlRegistry::PopupDescriptor{thirdPopupState.get(), QByteArrayLiteral("open"),
                                                                fixture.item("popupItem")});

    secondOwner.reset();
    QCOMPARE(fixture.data().size(), 1);
    thirdPopupState->setOpen(true);
    const QJsonObject popup =
        fixture.data().at(0).toObject().value(QStringLiteral("popup")).toObject();
    QVERIFY(popup.value(QStringLiteral("open")).toBool());
    QVERIFY(popup.value(QStringLiteral("generation")).toString().toULongLong() > *thirdGeneration);

    thirdPopupState.reset();
    QCOMPARE(fixture.registry.viewSettingsControlsData(7), QStringLiteral("[]"));
}

void SettingsControlRegistryTest::crossThreadScopeAndDescriptorObjectsRetireRegistrations()
{
    Fixture fixture;
    ForeignThreadObjects foreign;
    QObject *foreignOwner = foreign.create<QObject>();
    QQuickItem *foreignSurface = foreign.create<QQuickItem>();
    GeometryProvider *foreignProvider = foreign.create<GeometryProvider>();
    QQuickItem *foreignItem = foreign.create<QQuickItem>();
    PopupState *foreignPopupState = foreign.create<PopupState>();

    QVERIFY(!fixture.registry.replaceScope({7, QStringLiteral("foreign.owner"), std::nullopt, foreignOwner,
                                            fixture.root, &fixture.geometryProvider}));
    QVERIFY(!fixture.registry.replaceScope({7, QStringLiteral("foreign.surface"), std::nullopt,
                                            fixture.lifetime.get(), foreignSurface, &fixture.geometryProvider}));
    QVERIFY(!fixture.registry.replaceScope({7, QStringLiteral("foreign.provider"), std::nullopt,
                                            fixture.lifetime.get(), fixture.root, foreignProvider}));

    quint64 generation = fixture.openScope();
    QVERIFY(!fixture.registry.registerControl(generation, {QStringLiteral("foreign.control"),
                                                            QStringLiteral("button"),
                                                            std::nullopt,
                                                            foreignItem,
                                                            QByteArrayLiteral("currentValue"),
                                                            {{QStringLiteral("primary"), fixture.item("controlA")}},
                                                            std::nullopt}));
    QCOMPARE(fixture.registry.viewSettingsControlsData(7), QStringLiteral("[]"));

    generation = fixture.openScope();
    QVERIFY(!fixture.registry.registerControl(generation, {QStringLiteral("foreign.hit"),
                                                            QStringLiteral("button"),
                                                            std::nullopt,
                                                            fixture.item("controlA"),
                                                            QByteArrayLiteral("currentValue"),
                                                            {{QStringLiteral("primary"), foreignItem}},
                                                            std::nullopt}));
    QCOMPARE(fixture.registry.viewSettingsControlsData(7), QStringLiteral("[]"));

    generation = fixture.openScope();
    QVERIFY(!fixture.registry.registerControl(
        generation,
        {QStringLiteral("foreign.popup.state"), QStringLiteral("combo"), std::nullopt,
         fixture.item("popupControl"), QByteArrayLiteral("currentValue"),
         {{QStringLiteral("primary"), fixture.item("popupControl")}},
         SettingsControlRegistry::PopupDescriptor{foreignPopupState, QByteArrayLiteral("open"),
                                                  fixture.item("popupItem")}}));
    QCOMPARE(fixture.registry.viewSettingsControlsData(7), QStringLiteral("[]"));

    generation = fixture.openScope();
    QVERIFY(!fixture.registry.registerControl(
        generation,
        {QStringLiteral("foreign.popup.item"), QStringLiteral("combo"), std::nullopt,
         fixture.item("popupControl"), QByteArrayLiteral("currentValue"),
         {{QStringLiteral("primary"), fixture.item("popupControl")}},
         SettingsControlRegistry::PopupDescriptor{fixture.item("popupControl"), QByteArrayLiteral("popupOpen"),
                                                  foreignItem}}));
    QCOMPARE(fixture.registry.viewSettingsControlsData(7), QStringLiteral("[]"));

    generation = fixture.openScope();
    quint64 token = fixture.addControl(
        generation, fixture.item("popupControl"), QStringLiteral("foreign.row.item"), QStringLiteral("combo"),
        std::nullopt,
        SettingsControlRegistry::PopupDescriptor{fixture.item("popupControl"), QByteArrayLiteral("popupOpen"),
                                                 fixture.item("popupItem")});
    QVERIFY(!fixture.registry.registerPopupRow(token, {QStringLiteral("foreign.row"),
                                                        QStringLiteral("option"),
                                                        std::nullopt,
                                                        0,
                                                        QStringLiteral("foreign"),
                                                        foreignItem,
                                                        QByteArrayLiteral("currentValue"),
                                                        {{QStringLiteral("row"), fixture.item("popupRow")}}}));
    QCOMPARE(fixture.registry.viewSettingsControlsData(7), QStringLiteral("[]"));

    generation = fixture.openScope();
    token = fixture.addControl(
        generation, fixture.item("popupControl"), QStringLiteral("foreign.row.hit"), QStringLiteral("combo"),
        std::nullopt,
        SettingsControlRegistry::PopupDescriptor{fixture.item("popupControl"), QByteArrayLiteral("popupOpen"),
                                                 fixture.item("popupItem")});
    QVERIFY(!fixture.registry.registerPopupRow(token, {QStringLiteral("foreign.hit.row"),
                                                        QStringLiteral("option"),
                                                        std::nullopt,
                                                        0,
                                                        QStringLiteral("foreign-hit"),
                                                        fixture.item("popupRow"),
                                                        QByteArrayLiteral("currentValue"),
                                                        {{QStringLiteral("row"), foreignItem}}}));
    QCOMPARE(fixture.registry.viewSettingsControlsData(7), QStringLiteral("[]"));
}

void SettingsControlRegistryTest::popupRowsRequirePopupAncestryAndUniqueWireValues()
{
    Fixture fixture;

    auto openPopup = [&fixture](QString identity) {
        const quint64 generation = fixture.openScope();
        const quint64 token = fixture.addControl(
            generation, fixture.item("popupControl"), std::move(identity), QStringLiteral("combo"), std::nullopt,
            SettingsControlRegistry::PopupDescriptor{fixture.item("popupControl"),
                                                     QByteArrayLiteral("popupOpen"), fixture.item("popupItem")});
        return std::pair{generation, token};
    };

    auto [generation, token] = openPopup(QStringLiteral("unrelated.row"));
    QVERIFY(!fixture.registry.registerPopupRow(token, {QStringLiteral("outside.row"),
                                                        QStringLiteral("option"),
                                                        std::nullopt,
                                                        0,
                                                        QStringLiteral("outside"),
                                                        fixture.item("controlA"),
                                                        QByteArrayLiteral("currentValue"),
                                                        {{QStringLiteral("row"), fixture.item("controlA")}}}));
    QCOMPARE(fixture.registry.viewSettingsControlsData(7), QStringLiteral("[]"));
    QVERIFY(!fixture.registry.registerControl(generation, {QStringLiteral("retired"),
                                                            QStringLiteral("button"),
                                                            std::nullopt,
                                                            fixture.item("controlB"),
                                                            QByteArrayLiteral("currentValue"),
                                                            {{QStringLiteral("primary"), fixture.item("controlB")}},
                                                            std::nullopt}));

    std::tie(generation, token) = openPopup(QStringLiteral("unrelated.hit"));
    QVERIFY(!fixture.registry.registerPopupRow(token, {QStringLiteral("outside.hit"),
                                                        QStringLiteral("option"),
                                                        std::nullopt,
                                                        0,
                                                        QStringLiteral("outside-hit"),
                                                        fixture.item("popupRow"),
                                                        QByteArrayLiteral("currentValue"),
                                                        {{QStringLiteral("row"), fixture.item("controlA")}}}));
    QCOMPARE(fixture.registry.viewSettingsControlsData(7), QStringLiteral("[]"));

    std::tie(generation, token) = openPopup(QStringLiteral("numeric.values"));
    QVERIFY(fixture.registry.registerPopupRow(token, {QStringLiteral("integer.row"),
                                                       QStringLiteral("option"),
                                                       std::nullopt,
                                                       0,
                                                       qint64{1},
                                                       fixture.item("popupRow"),
                                                       QByteArrayLiteral("currentValue"),
                                                       {{QStringLiteral("row"), fixture.item("popupRow")}}}));
    QVERIFY(!fixture.registry.registerPopupRow(token, {QStringLiteral("double.row"),
                                                        QStringLiteral("option"),
                                                        std::nullopt,
                                                        1,
                                                        1.0,
                                                        fixture.item("popupSeparator"),
                                                        QByteArrayLiteral("currentValue"),
                                                        {{QStringLiteral("row"), fixture.item("popupSeparator")}}}));
    QCOMPARE(fixture.registry.viewSettingsControlsData(7), QStringLiteral("[]"));

    std::tie(generation, token) = openPopup(QStringLiteral("null.values"));
    QVERIFY(fixture.registry.registerPopupRow(token, {QStringLiteral("null.first"),
                                                       QStringLiteral("separator"),
                                                       std::nullopt,
                                                       0,
                                                       std::monostate{},
                                                       fixture.item("popupRow"),
                                                       QByteArrayLiteral("currentValue"),
                                                       {{QStringLiteral("row"), fixture.item("popupRow")}}}));
    QVERIFY(!fixture.registry.registerPopupRow(token, {QStringLiteral("null.second"),
                                                        QStringLiteral("separator"),
                                                        std::nullopt,
                                                        1,
                                                        std::monostate{},
                                                        fixture.item("popupSeparator"),
                                                        QByteArrayLiteral("currentValue"),
                                                        {{QStringLiteral("row"), fixture.item("popupSeparator")}}}));
    QCOMPARE(fixture.registry.viewSettingsControlsData(7), QStringLiteral("[]"));
}

void SettingsControlRegistryTest::registrationFailuresRetireWholeLoadGeneration()
{
    Fixture fixture;
    quint64 generation = fixture.openScope();
    fixture.addControl(generation, fixture.item("controlA"), QStringLiteral("duplicate"), QStringLiteral("button"),
                       QStringLiteral("one"));
    QVERIFY(!fixture.registry.registerControl(generation, {QStringLiteral("duplicate"),
                                                            QStringLiteral("button"),
                                                            QStringLiteral("one"),
                                                            fixture.item("controlB"),
                                                            QByteArrayLiteral("currentValue"),
                                                            {{QStringLiteral("primary"), fixture.item("controlB")}},
                                                            std::nullopt}));
    QCOMPARE(fixture.registry.viewSettingsControlsData(7), QStringLiteral("[]"));
    QVERIFY(!fixture.registry.registerControl(generation, {QStringLiteral("after.retirement"),
                                                            QStringLiteral("button"),
                                                            std::nullopt,
                                                            fixture.item("controlB"),
                                                            QByteArrayLiteral("currentValue"),
                                                            {{QStringLiteral("primary"), fixture.item("controlB")}},
                                                            std::nullopt}));

    generation = fixture.openScope();
    fixture.addControl(generation, fixture.item("controlA"), QStringLiteral("valid.before.bad"),
                       QStringLiteral("button"));
    QVERIFY(!fixture.registry.registerControl(generation, {QStringLiteral("missing.property"),
                                                            QStringLiteral("button"),
                                                            std::nullopt,
                                                            fixture.item("controlB"),
                                                            QByteArrayLiteral("missing"),
                                                            {{QStringLiteral("primary"), fixture.item("controlB")}},
                                                            std::nullopt}));
    QCOMPARE(fixture.registry.viewSettingsControlsData(7), QStringLiteral("[]"));

    generation = fixture.openScope();
    const quint64 token = fixture.addControl(
        generation, fixture.item("popupControl"), QStringLiteral("popup.before.bad.row"), QStringLiteral("combo"),
        std::nullopt,
        SettingsControlRegistry::PopupDescriptor{fixture.item("popupControl"), QByteArrayLiteral("popupOpen"),
                                                 fixture.item("popupItem")});
    QVERIFY(fixture.registry.registerPopupRow(token, {QStringLiteral("valid.row"),
                                                       QStringLiteral("option"),
                                                       std::nullopt,
                                                       0,
                                                       QStringLiteral("valid"),
                                                       fixture.item("popupRow"),
                                                       QByteArrayLiteral("currentValue"),
                                                       {{QStringLiteral("row"), fixture.item("popupRow")}}}));
    QVERIFY(!fixture.registry.registerPopupRow(token, {QString(),
                                                        QStringLiteral("option"),
                                                        std::nullopt,
                                                        1,
                                                        QStringLiteral("bad"),
                                                        fixture.item("popupSeparator"),
                                                        QByteArrayLiteral("currentValue"),
                                                        {{QStringLiteral("row"), fixture.item("popupSeparator")}}}));
    QCOMPARE(fixture.registry.viewSettingsControlsData(7), QStringLiteral("[]"));
    QVERIFY(!fixture.registry.registerPopupRow(token, {QStringLiteral("after.retirement"),
                                                        QStringLiteral("option"),
                                                        std::nullopt,
                                                        2,
                                                        QStringLiteral("late"),
                                                        fixture.item("popupSeparator"),
                                                        QByteArrayLiteral("currentValue"),
                                                        {{QStringLiteral("row"), fixture.item("popupSeparator")}}}));
}

void SettingsControlRegistryTest::unsupportedLiveEntriesRefuseWholeView()
{
    Fixture fixture;
    const quint64 generation = fixture.openScope();
    QQuickItem *control = fixture.item("controlA");
    fixture.addControl(generation, control, QStringLiteral("live.invalid"), QStringLiteral("button"));

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
