/*
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Real-link behavioral test for two small view-side models (linked through
// lattedock-core):
//   ViewPart::TasksModel - the ordered plasmoid list behind a view's tasks
//     applets; QML reads it through the "tasks" UserRole, so insert/remove
//     ranges and the count signal discipline are what the dock's task rows
//     ride on.
//   ViewPart::IndicatorPart::Info - the indicator capability record every
//     loaded indicator package publishes; each property is a guarded setter
//     whose NOTIFY drives QML re-evaluation, so a wrong emit shape either
//     stalls the indicator UI or spins re-layouts.
//
// Transplanted from latte-dock-qt6 (tests/viewmodelstest.cpp at
// 81384003, github.com/CaptSilver/latte-dock-qt6). Bare AppletQuickItem instances stand in for real plasmoids
// (public default ctor at the 6.7.3 pin; the applet() hook connect inside
// addTask degrades to a connect warning, which is exactly what production
// tolerates for a not-yet-attached applet).

// local
#include "view/indicator/indicatorinfo.h"
#include "view/tasksmodel.h"

// Qt
#include <QObject>
#include <QSignalSpy>
#include <QTest>
#include <QVariant>

// Plasma
#include <PlasmaQuick/AppletQuickItem>

using Latte::ViewPart::TasksModel;
using Info = Latte::ViewPart::IndicatorPart::Info;

class ViewModelsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    //! TasksModel
    void startEmpty();
    void publishTasksRoleName();
    void insertTaskWithRangeAndCountSignal();
    void dedupeRepeatedPlasmoid();
    void returnPlasmoidAtUserRole();
    void refuseOutOfRangeRows();
    void refuseNonUserRoles();
    void removeTaskWithRangeAndCountSignal();
    void ignoreRemoveOfUnknownPlasmoid();
    void ignoreRemoveOfNull();

    //! IndicatorPart::Info
    void reportCapabilityDefaults();
    void flipEveryBoolCapabilityWithOneSignal();
    void setExtraMaskThicknessWithOneSignal();
    void setPaddingsWithOneSignal();
    void emitOnlyOnRealChange();
};

void ViewModelsTest::startEmpty()
{
    TasksModel model;
    QCOMPARE(model.count(), 0);
    QCOMPARE(model.rowCount(), 0);
}

void ViewModelsTest::publishTasksRoleName()
{
    TasksModel model;
    QCOMPARE(model.roleNames().value(Qt::UserRole), QByteArrayLiteral("tasks"));
}

void ViewModelsTest::insertTaskWithRangeAndCountSignal()
{
    TasksModel model;
    QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy countSpy(&model, &TasksModel::countChanged);

    PlasmaQuick::AppletQuickItem item;
    model.addTask(&item);

    QCOMPARE(model.count(), 1);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(insertSpy.count(), 1);
    QCOMPARE(countSpy.count(), 1);

    // the first element inserts at range [0, 0]
    const auto args = insertSpy.takeFirst();
    QCOMPARE(args.at(1).toInt(), 0);
    QCOMPARE(args.at(2).toInt(), 0);
}

void ViewModelsTest::dedupeRepeatedPlasmoid()
{
    TasksModel model;
    PlasmaQuick::AppletQuickItem item;

    model.addTask(&item);
    QCOMPARE(model.count(), 1);

    QSignalSpy countSpy(&model, &TasksModel::countChanged);
    // the same plasmoid again is rejected by the contains() guard
    model.addTask(&item);
    QCOMPARE(model.count(), 1);
    QCOMPARE(countSpy.count(), 0);
}

void ViewModelsTest::returnPlasmoidAtUserRole()
{
    TasksModel model;
    PlasmaQuick::AppletQuickItem first;
    PlasmaQuick::AppletQuickItem second;
    model.addTask(&first);
    model.addTask(&second);

    QCOMPARE(model.count(), 2);

    auto *got0 = model.data(model.index(0, 0), Qt::UserRole).value<PlasmaQuick::AppletQuickItem *>();
    auto *got1 = model.data(model.index(1, 0), Qt::UserRole).value<PlasmaQuick::AppletQuickItem *>();
    QCOMPARE(got0, &first);
    QCOMPARE(got1, &second);
}

void ViewModelsTest::refuseOutOfRangeRows()
{
    TasksModel model;
    PlasmaQuick::AppletQuickItem item;
    model.addTask(&item);

    // past the end and negative rows yield the invalid QVariant
    QVERIFY(!model.data(model.index(5, 0), Qt::UserRole).isValid());
    QVERIFY(!model.data(model.index(-1, 0), Qt::UserRole).isValid());
}

void ViewModelsTest::refuseNonUserRoles()
{
    TasksModel model;
    PlasmaQuick::AppletQuickItem item;
    model.addTask(&item);

    // only Qt::UserRole carries data; DisplayRole falls through to invalid
    QVERIFY(!model.data(model.index(0, 0), Qt::DisplayRole).isValid());
}

void ViewModelsTest::removeTaskWithRangeAndCountSignal()
{
    TasksModel model;
    PlasmaQuick::AppletQuickItem first;
    PlasmaQuick::AppletQuickItem second;
    model.addTask(&first);
    model.addTask(&second);
    QCOMPARE(model.count(), 2);

    QSignalSpy removeSpy(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy countSpy(&model, &TasksModel::countChanged);

    model.removeTask(&first);

    QCOMPARE(model.count(), 1);
    QCOMPARE(removeSpy.count(), 1);
    QCOMPARE(countSpy.count(), 1);

    // first sat at index 0, so the removed range is [0, 0]
    const auto args = removeSpy.takeFirst();
    QCOMPARE(args.at(1).toInt(), 0);
    QCOMPARE(args.at(2).toInt(), 0);

    // the survivor shifts into row 0
    auto *survivor = model.data(model.index(0, 0), Qt::UserRole).value<PlasmaQuick::AppletQuickItem *>();
    QCOMPARE(survivor, &second);
}

void ViewModelsTest::ignoreRemoveOfUnknownPlasmoid()
{
    TasksModel model;
    PlasmaQuick::AppletQuickItem known;
    PlasmaQuick::AppletQuickItem stranger;
    model.addTask(&known);

    QSignalSpy countSpy(&model, &TasksModel::countChanged);
    // a plasmoid the model never tracked is rejected by the contains() guard
    model.removeTask(&stranger);
    QCOMPARE(model.count(), 1);
    QCOMPARE(countSpy.count(), 0);
}

void ViewModelsTest::ignoreRemoveOfNull()
{
    TasksModel model;
    PlasmaQuick::AppletQuickItem item;
    model.addTask(&item);

    QSignalSpy countSpy(&model, &TasksModel::countChanged);
    // the leading !plasmoid guard short-circuits before any list touch
    model.removeTask(nullptr);
    QCOMPARE(model.count(), 1);
    QCOMPARE(countSpy.count(), 0);
}

void ViewModelsTest::reportCapabilityDefaults()
{
    Info info(nullptr);

    QCOMPARE(info.needsIconColors(), false);
    QCOMPARE(info.needsMouseEventCoordinates(), false);
    QCOMPARE(info.providesClickedAnimation(), false);
    QCOMPARE(info.providesHoveredAnimation(), false);
    QCOMPARE(info.providesInAttentionAnimation(), false);
    QCOMPARE(info.providesTaskLauncherAnimation(), false);
    QCOMPARE(info.providesGroupedWindowAddedAnimation(), false);
    QCOMPARE(info.providesGroupedWindowRemovedAnimation(), false);
    QCOMPARE(info.providesFrontLayer(), false);
    QCOMPARE(info.extraMaskThickness(), 0);
    QCOMPARE(info.minLengthPadding(), 0.0f);
    QCOMPARE(info.minThicknessPadding(), 0.0f);
}

void ViewModelsTest::flipEveryBoolCapabilityWithOneSignal()
{
    Info info(nullptr);

    // one row per bool capability: NOTIFY signal, setter, getter
    struct BoolCapability {
        const char *notifySignal;
        std::function<void(Info &, bool)> set;
        std::function<bool(const Info &)> get;
    };

    const std::vector<BoolCapability> capabilities = {
        {SIGNAL(needsIconColorsChanged()), [](Info &i, bool v) { i.setNeedsIconColors(v); }, [](const Info &i) { return i.needsIconColors(); }},
        {SIGNAL(needsMouseEventCoordinatesChanged()), [](Info &i, bool v) { i.setNeedsMouseEventCoordinates(v); }, [](const Info &i) { return i.needsMouseEventCoordinates(); }},
        {SIGNAL(providesClickedAnimationChanged()), [](Info &i, bool v) { i.setProvidesClickedAnimation(v); }, [](const Info &i) { return i.providesClickedAnimation(); }},
        {SIGNAL(providesHoveredAnimationChanged()), [](Info &i, bool v) { i.setProvidesHoveredAnimation(v); }, [](const Info &i) { return i.providesHoveredAnimation(); }},
        {SIGNAL(providesInAttentionAnimationChanged()), [](Info &i, bool v) { i.setProvidesInAttentionAnimation(v); }, [](const Info &i) { return i.providesInAttentionAnimation(); }},
        {SIGNAL(providesTaskLauncherAnimationChanged()), [](Info &i, bool v) { i.setProvidesTaskLauncherAnimation(v); }, [](const Info &i) { return i.providesTaskLauncherAnimation(); }},
        {SIGNAL(providesGroupedWindowAddedAnimationChanged()), [](Info &i, bool v) { i.setProvidesGroupedWindowAddedAnimation(v); }, [](const Info &i) { return i.providesGroupedWindowAddedAnimation(); }},
        {SIGNAL(providesGroupedWindowRemovedAnimationChanged()), [](Info &i, bool v) { i.setProvidesGroupedWindowRemovedAnimation(v); }, [](const Info &i) { return i.providesGroupedWindowRemovedAnimation(); }},
        {SIGNAL(providesFrontLayerChanged()), [](Info &i, bool v) { i.setProvidesFrontLayer(v); }, [](const Info &i) { return i.providesFrontLayer(); }},
    };

    for (const auto &capability : capabilities) {
        QSignalSpy spy(&info, capability.notifySignal);
        QVERIFY(spy.isValid());
        QCOMPARE(capability.get(info), false);
        capability.set(info, true);
        QCOMPARE(capability.get(info), true);
        QCOMPARE(spy.count(), 1);
    }
}

void ViewModelsTest::setExtraMaskThicknessWithOneSignal()
{
    Info info(nullptr);
    QSignalSpy spy(&info, &Info::extraMaskThicknessChanged);
    QVERIFY(spy.isValid());

    info.setExtraMaskThickness(7);
    QCOMPARE(info.extraMaskThickness(), 7);
    QCOMPARE(spy.count(), 1);
}

void ViewModelsTest::setPaddingsWithOneSignal()
{
    Info info(nullptr);

    QSignalSpy lengthSpy(&info, &Info::minLengthPaddingChanged);
    QSignalSpy thicknessSpy(&info, &Info::minThicknessPaddingChanged);

    info.setMinLengthPadding(0.25f);
    QCOMPARE(info.minLengthPadding(), 0.25f);
    QCOMPARE(lengthSpy.count(), 1);

    info.setMinThicknessPadding(0.5f);
    QCOMPARE(info.minThicknessPadding(), 0.5f);
    QCOMPARE(thicknessSpy.count(), 1);
}

void ViewModelsTest::emitOnlyOnRealChange()
{
    Info info(nullptr);

    // bool: writing the current value is a guarded no-op
    QSignalSpy boolSpy(&info, &Info::needsIconColorsChanged);
    info.setNeedsIconColors(false); // already false
    QCOMPARE(boolSpy.count(), 0);
    info.setNeedsIconColors(true);
    QCOMPARE(boolSpy.count(), 1);
    info.setNeedsIconColors(true); // no change
    QCOMPARE(boolSpy.count(), 1);

    // int
    QSignalSpy intSpy(&info, &Info::extraMaskThicknessChanged);
    info.setExtraMaskThickness(0); // already 0
    QCOMPARE(intSpy.count(), 0);
    info.setExtraMaskThickness(3);
    QCOMPARE(intSpy.count(), 1);
    info.setExtraMaskThickness(3); // no change
    QCOMPARE(intSpy.count(), 1);

    // float
    QSignalSpy floatSpy(&info, &Info::minLengthPaddingChanged);
    info.setMinLengthPadding(0.0f); // already 0
    QCOMPARE(floatSpy.count(), 0);
    info.setMinLengthPadding(1.0f);
    QCOMPARE(floatSpy.count(), 1);
    info.setMinLengthPadding(1.0f); // no change
    QCOMPARE(floatSpy.count(), 1);
}

QTEST_MAIN(ViewModelsTest)

#include "viewmodelstest.moc"
