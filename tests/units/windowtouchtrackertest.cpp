/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../../app/view/floatingtransition.h"
#include "../../app/view/windowtouchtracker.h"

#include <QAbstractListModel>
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTimer>
#include <QVariant>
#include <QtTest>

#include <utility>
#include <vector>

using Latte::ViewPart::FloatingTransition;
using Latte::ViewPart::WindowTouchTracker;
using namespace Latte::ViewPart::FloatingPanelGeometry;

namespace {

class WindowModel final : public QAbstractListModel
{
public:
    enum Role {
        IsWindow = Qt::UserRole + 1,
        IsHidden,
        IsMinimized,
        Geometry,
    };

    struct Row {
        QVariant isWindow{true};
        QVariant hidden{false};
        QVariant minimized{false};
        QVariant geometry{QRect{}};
    };

    explicit WindowModel(QObject *parent = nullptr)
        : QAbstractListModel(parent),
          m_roleNames(defaultRoleNames())
    {
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid()
                || index.row() < 0
                || index.row() >= rowCount()) {
            return {};
        }

        const Row &row = m_rows.at(
            static_cast<std::size_t>(index.row()));
        switch (role) {
        case IsWindow:
            return row.isWindow;
        case IsHidden:
            return row.hidden;
        case IsMinimized:
            return row.minimized;
        case Geometry:
            return row.geometry;
        default:
            return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return m_roleNames;
    }

    void append(Row row)
    {
        const int nextRow = rowCount();
        beginInsertRows(QModelIndex(), nextRow, nextRow);
        m_rows.push_back(std::move(row));
        endInsertRows();
    }

    void removeLast()
    {
        Q_ASSERT(!m_rows.empty());
        const int lastRow = rowCount() - 1;
        beginRemoveRows(QModelIndex(), lastRow, lastRow);
        m_rows.pop_back();
        endRemoveRows();
    }

    void resetRows(std::vector<Row> rows)
    {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
    }

    void replaceRoleNames(QHash<int, QByteArray> roleNames)
    {
        beginResetModel();
        m_roleNames = std::move(roleNames);
        endResetModel();
    }

    void announceGeometryChange()
    {
        Q_ASSERT(!m_rows.empty());
        const QModelIndex changed = index(0, 0);
        Q_EMIT dataChanged(changed, changed, {Geometry});
    }

    static QHash<int, QByteArray> defaultRoleNames()
    {
        return {
            {IsWindow, QByteArrayLiteral("IsWindow")},
            {IsHidden, QByteArrayLiteral("IsHidden")},
            {IsMinimized, QByteArrayLiteral("IsMinimized")},
            {Geometry, QByteArrayLiteral("Geometry")},
        };
    }

private:
    std::vector<Row> m_rows;
    QHash<int, QByteArray> m_roleNames;
};

Inputs geometry(int primaryStart = 0, int primaryLength = 100)
{
    return {
        .outputGeometry = QRect(0, 0, 200, 100),
        .edge = Edge::Bottom,
        .primaryAxisSpan = {primaryStart, primaryLength},
        .panelDepth = 10,
        .floatingGap = 5,
    };
}

WindowModel::Row touchingRow(const FloatingTransition &transition)
{
    return WindowModel::Row{
        .geometry = transition.stableTriggerGeometry(),
    };
}

void connectPolicy(WindowTouchTracker &tracker,
                   FloatingTransition &transition)
{
    QObject::connect(
        &tracker,
        &WindowTouchTracker::touchingWindowCountChanged,
        &transition,
        [&tracker, &transition]() {
            transition.reconcileTargetPolicy(
                transition.floatingPanelEligible(),
                transition.attachOnWindowTouchConfigured(),
                transition
                    .attachmentWaitsForPointerExitConfigured(),
                transition.pointerInsideView(),
                tracker.touchingWindowCount(), false);
        });
}

} // namespace

class WindowTouchTrackerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void followsRowsResetRemovalAndModelDestruction();
    void skipsNonWindowRowsBeforeWindowRoleValidation();
    void rejectsMalformedTrueWindowRoles_data();
    void rejectsMalformedTrueWindowRoles();
    void followsExplicitTriggerGeometryAndResetsInvalidStartup();
    void refusesRoleNameDrift();
    void fixedDeadlineCannotStarveUnderSustainedChanges();
};

void WindowTouchTrackerTest::followsRowsResetRemovalAndModelDestruction()
{
    FloatingTransition transition;
    transition.setAnimationDuration(0);
    QVERIFY(transition.configureGeometry(geometry()));
    transition.reconcileTargetPolicy(
        true, true, false, false, 0, false);

    WindowTouchTracker tracker;
    tracker.setTriggerGeometry(transition.stableTriggerGeometry());
    connectPolicy(tracker, transition);
    auto *model = new WindowModel;
    model->append(touchingRow(transition));
    tracker.setModel(model);

    QTRY_COMPARE_WITH_TIMEOUT(tracker.touchingWindowCount(), 1, 100);
    QCOMPARE(tracker.geometryRoleTypeName(), QStringLiteral("QRect"));
    QCOMPARE(transition.target(), FloatingTransition::Target::Attached);

    model->removeLast();
    QTRY_COMPARE_WITH_TIMEOUT(tracker.touchingWindowCount(), 0, 100);
    QCOMPARE(tracker.geometryRoleTypeName(), QString{});
    QCOMPARE(transition.target(), FloatingTransition::Target::Floated);

    model->append(touchingRow(transition));
    QTRY_COMPARE_WITH_TIMEOUT(tracker.touchingWindowCount(), 1, 100);

    model->resetRows({
        WindowModel::Row{.geometry = QRect(500, 500, 20, 20)},
    });
    QTRY_COMPARE_WITH_TIMEOUT(tracker.touchingWindowCount(), 0, 100);

    model->resetRows({touchingRow(transition)});
    QTRY_COMPARE_WITH_TIMEOUT(tracker.touchingWindowCount(), 1, 100);
    delete model;

    QCOMPARE(tracker.model(), nullptr);
    QCOMPARE(tracker.touchingWindowCount(), 0);
    QCOMPARE(tracker.geometryRoleTypeName(), QString{});
    QCOMPARE(transition.touchingWindowCount(), 0);
    QCOMPARE(transition.target(), FloatingTransition::Target::Floated);
}

void WindowTouchTrackerTest::
    skipsNonWindowRowsBeforeWindowRoleValidation()
{
    FloatingTransition transition;
    transition.setAnimationDuration(0);
    QVERIFY(transition.configureGeometry(geometry()));

    WindowTouchTracker tracker;
    tracker.setTriggerGeometry(transition.stableTriggerGeometry());
    WindowModel model;
    model.append(WindowModel::Row{
        .isWindow = false,
        .hidden = QStringLiteral("not a window role"),
        .minimized = 19,
        .geometry = QStringLiteral("not a QRect"),
    });
    model.append(touchingRow(transition));
    tracker.setModel(&model);

    QTRY_COMPARE_WITH_TIMEOUT(tracker.touchingWindowCount(), 1, 100);
    QCOMPARE(tracker.geometryRoleTypeName(), QStringLiteral("QRect"));

    QTest::ignoreMessage(
        QtCriticalMsg,
        QRegularExpression(
            "WindowTouchTracker requires bool role \"IsWindow\" at row 0.*"));
    model.resetRows({
        WindowModel::Row{
            .isWindow = QStringLiteral("not a bool"),
            .hidden = false,
            .minimized = false,
            .geometry = transition.stableTriggerGeometry(),
        },
        touchingRow(transition),
    });
    QTRY_COMPARE_WITH_TIMEOUT(tracker.touchingWindowCount(), 0, 100);
    QCOMPARE(tracker.geometryRoleTypeName(), QString{});
}

void WindowTouchTrackerTest::rejectsMalformedTrueWindowRoles_data()
{
    QTest::addColumn<int>("malformedRole");
    QTest::addColumn<QVariant>("malformedValue");
    QTest::addColumn<QString>("expectedCritical");

    QTest::newRow("malformed IsHidden")
        << static_cast<int>(WindowModel::IsHidden)
        << QVariant{QStringLiteral("not a bool")}
        << QStringLiteral(
               "WindowTouchTracker requires bool role \"IsHidden\""
               " at row 0 but received QString");
    QTest::newRow("malformed IsMinimized")
        << static_cast<int>(WindowModel::IsMinimized)
        << QVariant{QStringLiteral("not a bool")}
        << QStringLiteral(
               "WindowTouchTracker requires bool role \"IsMinimized\""
               " at row 0 but received QString");
    QTest::newRow("non-QRect Geometry")
        << static_cast<int>(WindowModel::Geometry)
        << QVariant{QStringLiteral("not a QRect")}
        << QStringLiteral(
               "WindowTouchTracker requires QRect Geometry"
               " at row 0 but received QString");
}

void WindowTouchTrackerTest::rejectsMalformedTrueWindowRoles()
{
    QFETCH(int, malformedRole);
    QFETCH(QVariant, malformedValue);
    QFETCH(QString, expectedCritical);

    FloatingTransition transition;
    transition.setAnimationDuration(0);
    QVERIFY(transition.configureGeometry(geometry()));

    WindowTouchTracker tracker;
    tracker.setTriggerGeometry(transition.stableTriggerGeometry());
    WindowModel model;
    model.append(touchingRow(transition));
    tracker.setModel(&model);
    QTRY_COMPARE_WITH_TIMEOUT(tracker.touchingWindowCount(), 1, 100);
    QCOMPARE(tracker.geometryRoleTypeName(), QStringLiteral("QRect"));

    WindowModel::Row malformed = touchingRow(transition);
    switch (malformedRole) {
    case WindowModel::IsHidden:
        malformed.hidden = malformedValue;
        break;
    case WindowModel::IsMinimized:
        malformed.minimized = malformedValue;
        break;
    case WindowModel::Geometry:
        malformed.geometry = malformedValue;
        break;
    default:
        QFAIL("test data named a role outside the true-window validation set");
    }

    const QByteArray expectedMessage =
        expectedCritical.toUtf8();
    QTest::ignoreMessage(
        QtCriticalMsg,
        expectedMessage.constData());
    model.resetRows({std::move(malformed)});

    QTRY_COMPARE_WITH_TIMEOUT(tracker.touchingWindowCount(), 0, 100);
    QCOMPARE(tracker.geometryRoleTypeName(), QString{});
}

void WindowTouchTrackerTest::
    followsExplicitTriggerGeometryAndResetsInvalidStartup()
{
    FloatingTransition transition;
    transition.setAnimationDuration(0);
    QVERIFY(transition.configureGeometry(geometry(0, 80)));
    transition.reconcileTargetPolicy(
        true, true, false, false, 0, false);

    const QRect firstTrigger = transition.stableTriggerGeometry();
    const auto secondSolution = solve(geometry(120, 80));
    QVERIFY(secondSolution.has_value());
    const QRect secondTrigger = secondSolution->trigger.value;
    QVERIFY(!firstTrigger.intersects(secondTrigger));

    WindowTouchTracker tracker;
    connectPolicy(tracker, transition);
    WindowModel model;
    model.append(WindowModel::Row{.geometry = firstTrigger});
    tracker.setModel(&model);

    QVERIFY(!tracker.triggerGeometry().isValid());
    QTest::qWait(WindowTouchTracker::EvaluationDelayMs * 2);
    QCOMPARE(tracker.touchingWindowCount(), 0);
    QCOMPARE(transition.target(), FloatingTransition::Target::Floated);

    tracker.setTriggerGeometry(firstTrigger);
    QTRY_COMPARE_WITH_TIMEOUT(tracker.touchingWindowCount(), 1, 100);
    QCOMPARE(transition.target(), FloatingTransition::Target::Attached);

    tracker.setTriggerGeometry(secondTrigger);
    QTRY_COMPARE_WITH_TIMEOUT(tracker.touchingWindowCount(), 0, 100);
    QCOMPARE(transition.target(), FloatingTransition::Target::Floated);

    tracker.setTriggerGeometry(firstTrigger);
    QTRY_COMPARE_WITH_TIMEOUT(tracker.touchingWindowCount(), 1, 100);
    QCOMPARE(transition.target(), FloatingTransition::Target::Attached);

    tracker.setTriggerGeometry({});
    QVERIFY(!tracker.triggerGeometry().isValid());
    QCOMPARE(tracker.touchingWindowCount(), 0);
    QCOMPARE(transition.target(), FloatingTransition::Target::Floated);
}

void WindowTouchTrackerTest::refusesRoleNameDrift()
{
    FloatingTransition transition;
    transition.setAnimationDuration(0);
    QVERIFY(transition.configureGeometry(geometry()));

    WindowTouchTracker tracker;
    tracker.setTriggerGeometry(transition.stableTriggerGeometry());
    WindowModel model;
    model.append(touchingRow(transition));
    tracker.setModel(&model);
    QTRY_COMPARE_WITH_TIMEOUT(tracker.touchingWindowCount(), 1, 100);

    auto roles = WindowModel::defaultRoleNames();
    roles[WindowModel::Geometry] = QByteArrayLiteral("WindowGeometry");
    QTest::ignoreMessage(
        QtCriticalMsg,
        QRegularExpression(
            "WindowTouchTracker requires model role \"Geometry\".*"));
    model.replaceRoleNames(std::move(roles));
    QTRY_COMPARE_WITH_TIMEOUT(tracker.touchingWindowCount(), 0, 100);
    QCOMPARE(tracker.geometryRoleTypeName(), QString{});
}

void WindowTouchTrackerTest::
    fixedDeadlineCannotStarveUnderSustainedChanges()
{
    FloatingTransition transition;
    transition.setAnimationDuration(0);
    QVERIFY(transition.configureGeometry(geometry()));

    WindowTouchTracker tracker;
    tracker.setTriggerGeometry(transition.stableTriggerGeometry());
    WindowModel model;
    model.append(WindowModel::Row{
        .geometry = QRect(500, 500, 20, 20),
    });
    tracker.setModel(&model);
    QTest::qWait(30);
    QCOMPARE(tracker.touchingWindowCount(), 0);

    model.resetRows({touchingRow(transition)});
    QSignalSpy countChanged(
        &tracker, &WindowTouchTracker::touchingWindowCountChanged);
    QTimer sustainedChanges;
    sustainedChanges.setInterval(1);
    connect(&sustainedChanges, &QTimer::timeout,
            &model, &WindowModel::announceGeometryChange);
    sustainedChanges.start();

    QVERIFY2(
        countChanged.wait(60),
        "restarting the 10 ms timer under every dataChanged would starve this"
        " signal while the sustained-change timer remains active");
    QVERIFY(sustainedChanges.isActive());
    sustainedChanges.stop();
    QCOMPARE(tracker.touchingWindowCount(), 1);
}

QTEST_MAIN(WindowTouchTrackerTest)

#include "windowtouchtrackertest.moc"
