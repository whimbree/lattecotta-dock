/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Pins the vendored tasks Backend (plasmoid/plugin/backend.cpp, from
// plasma-desktop's taskmanager applet, carrying our deliberate extensions:
// the KWin window-view D-Bus watcher and showAudioStreamOsd). The periodic
// upstream sync pass diffs plasma-desktop's backend.cpp against this copy;
// these cases are the behavioral tripwire for hunks that change launcher
// classification, drag mime payloads or the property contracts.
//
// Transplanted from latte-dock-qt6 (tests/tasksbackendtest.cpp at
// 81384003, github.com/CaptSilver/latte-dock-qt6) and extended: the desktop-file table also proves a
// Type=Link .desktop and a remote url are rejected (isApplication must mean
// application, not just desktop-file), the no-Categories row pins the empty
// list, and the mime-data case pins ALL THREE keys the drag payload
// carries - the fork never pinned the taskbuttonitem entry, which is the
// one the drop side of Latte's own drag-and-drop reads.

#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextStream>

#include "backend.h"

class TasksBackendTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void isApplication_data();
    void isApplication();
    void isApplicationRejectsInvalidAndRemoteUrls();
    void applicationCategoriesReadsCategories();
    void applicationCategoriesEmptyWhenAbsent();
    void jsonArrayToUrlListConverts();
    void jsonArrayToUrlListEmptyGivesEmpty();
    void generateMimeDataCarriesAllDragKeys();
    void highlightWindowsRoundTrips();
    void highlightWindowsEmitsOnlyOnChange();
    void taskManagerItemRoundTrips();
    void tryDecodeApplicationsUrlPassesThroughUnknown();
    void windowViewAvailableIsQueryable();
    void middleClickDispatchStartsEmpty();
    void middleClickDispatchDistinguishesLauncherAndTask();
    void middleClickDispatchAcceptsEveryOfferedPair_data();
    void middleClickDispatchAcceptsEveryOfferedPair();
    void middleClickDispatchSequenceIsProcessMonotonic();
    void middleClickDispatchRefusesMalformedInput();

private:
    QString writeFile(const QString &name, const QString &body);

    QTemporaryDir m_dir;
};

QString TasksBackendTest::writeFile(const QString &name, const QString &body)
{
    const QString path = m_dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return QString();
    }
    QTextStream(&f) << body;
    f.close();
    return path;
}

void TasksBackendTest::isApplication_data()
{
    QTest::addColumn<QString>("fileName");
    QTest::addColumn<QString>("body");
    QTest::addColumn<bool>("expected");

    // isApplication gates whether a dropped url becomes a pinnable launcher.
    QTest::newRow("application desktop file")
        << QStringLiteral("app.desktop")
        << QStringLiteral("[Desktop Entry]\nType=Application\nName=T\nExec=true\n")
        << true;
    // A .desktop file that is NOT an application (a Link) must be refused:
    // the check is hasApplicationType(), not just the file extension.
    QTest::newRow("link desktop file")
        << QStringLiteral("link.desktop")
        << QStringLiteral("[Desktop Entry]\nType=Link\nName=T\nURL=https://kde.org\n")
        << false;
    QTest::newRow("plain file")
        << QStringLiteral("plain.txt")
        << QStringLiteral("hello")
        << false;
}

void TasksBackendTest::isApplication()
{
    QFETCH(QString, fileName);
    QFETCH(QString, body);
    QFETCH(bool, expected);

    Backend backend;
    const QString path = writeFile(fileName, body);
    QVERIFY(!path.isEmpty());
    QCOMPARE(backend.isApplication(QUrl::fromLocalFile(path)), expected);
}

void TasksBackendTest::isApplicationRejectsInvalidAndRemoteUrls()
{
    Backend backend;
    QVERIFY(!backend.isApplication(QUrl()));
    // Remote urls can never be desktop files; the local-file guard must
    // refuse them before any filesystem probe.
    QVERIFY(!backend.isApplication(QUrl(QStringLiteral("https://kde.org/app.desktop"))));
}

void TasksBackendTest::applicationCategoriesReadsCategories()
{
    const QString path =
        writeFile(QStringLiteral("cat.desktop"), QStringLiteral("[Desktop Entry]\nType=Application\nName=T\nExec=true\nCategories=Qt;KDE;\n"));
    const QStringList cats = Backend::applicationCategories(QUrl::fromLocalFile(path));
    QCOMPARE(cats, QStringList({QStringLiteral("Qt"), QStringLiteral("KDE")}));
}

void TasksBackendTest::applicationCategoriesEmptyWhenAbsent()
{
    const QString path =
        writeFile(QStringLiteral("nocat.desktop"), QStringLiteral("[Desktop Entry]\nType=Application\nName=T\nExec=true\n"));
    QVERIFY(Backend::applicationCategories(QUrl::fromLocalFile(path)).isEmpty());
    QVERIFY(Backend::applicationCategories(QUrl()).isEmpty());
}

void TasksBackendTest::jsonArrayToUrlListConverts()
{
    Backend backend;
    const QJsonArray arr{QStringLiteral("file:///tmp/a"), QStringLiteral("file:///tmp/b")};
    const QList<QUrl> urls = backend.jsonArrayToUrlList(arr);
    QCOMPARE(urls.size(), 2);
    QCOMPARE(urls.at(0), QUrl(QStringLiteral("file:///tmp/a")));
    QCOMPARE(urls.at(1), QUrl(QStringLiteral("file:///tmp/b")));
}

void TasksBackendTest::jsonArrayToUrlListEmptyGivesEmpty()
{
    Backend backend;
    QVERIFY(backend.jsonArrayToUrlList(QJsonArray()).isEmpty());
}

void TasksBackendTest::generateMimeDataCarriesAllDragKeys()
{
    const QUrl url(QStringLiteral("file:///tmp/a"));
    const QVariant payload(QByteArrayLiteral("payload"));
    const QVariantMap m = Backend::generateMimeData(QStringLiteral("application/x-test"), payload, url);

    // All three entries matter: the taskurl feeds external drops, the named
    // mimetype feeds the requesting delegate, and the taskbuttonitem entry
    // is what Latte's own drop handling reads for task reordering.
    QCOMPARE(m.value(QStringLiteral("text/x-orgkdeplasmataskmanager_taskurl")).toString(), QStringLiteral("file:///tmp/a"));
    QCOMPARE(m.value(QStringLiteral("application/x-test")), payload);
    QCOMPARE(m.value(QStringLiteral("application/x-orgkdeplasmataskmanager_taskbuttonitem")), payload);
}

void TasksBackendTest::highlightWindowsRoundTrips()
{
    Backend backend;
    QCOMPARE(backend.highlightWindows(), false);
    backend.setHighlightWindows(true);
    QCOMPARE(backend.highlightWindows(), true);
}

void TasksBackendTest::highlightWindowsEmitsOnlyOnChange()
{
    Backend backend;
    QSignalSpy spy(&backend, &Backend::highlightWindowsChanged);
    backend.setHighlightWindows(true);
    QCOMPARE(spy.count(), 1);
    backend.setHighlightWindows(true); // same value -> no extra signal
    QCOMPARE(spy.count(), 1);
}

void TasksBackendTest::taskManagerItemRoundTrips()
{
    Backend backend;
    QCOMPARE(backend.taskManagerItem(), nullptr);

    QSignalSpy spy(&backend, &Backend::taskManagerItemChanged);
    QQuickItem item;
    backend.setTaskManagerItem(&item);
    QCOMPARE(backend.taskManagerItem(), &item);
    QCOMPARE(spy.count(), 1);

    backend.setTaskManagerItem(&item); // same item -> no extra signal
    QCOMPARE(spy.count(), 1);

    backend.setTaskManagerItem(nullptr);
    QCOMPARE(backend.taskManagerItem(), nullptr);
}

void TasksBackendTest::tryDecodeApplicationsUrlPassesThroughUnknown()
{
    // A non-"applications" URL is returned untouched.
    const QUrl file(QStringLiteral("file:///tmp/a"));
    QCOMPARE(Backend::tryDecodeApplicationsUrl(file), file);

    // An "applications:" URL with no matching service is returned untouched.
    const QUrl missing(QStringLiteral("applications:does.not.exist.latte.desktop"));
    QCOMPARE(Backend::tryDecodeApplicationsUrl(missing), missing);
}

void TasksBackendTest::windowViewAvailableIsQueryable()
{
    // The KWin window-view D-Bus watcher is one of this copy's deliberate
    // extensions over plasma-desktop's backend; the property must stay
    // readable (a sync pass that drops it breaks the group tooltip path).
    Backend backend;
    const QVariant v = backend.property("windowViewAvailable");
    QVERIFY(v.isValid());
    QVERIFY(v.typeId() == QMetaType::Bool);
}

void TasksBackendTest::middleClickDispatchStartsEmpty()
{
    Backend backend;
    QVERIFY(backend.latestMiddleClickDispatch().isEmpty());
    QVERIFY(backend.property("latestMiddleClickDispatch").toMap().isEmpty());
}

void TasksBackendTest::middleClickDispatchDistinguishesLauncherAndTask()
{
    Backend backend;
    QSignalSpy spy(&backend, &Backend::middleClickDispatchChanged);

    QVERIFY(backend.recordMiddleClickDispatch(QStringLiteral("applications:org.kde.dolphin.desktop"),
                                              true,
                                              Latte::Tasks::Types::NewInstance,
                                              QStringLiteral("activate")));

    QVariantMap event = backend.latestMiddleClickDispatch();
    QCOMPARE(event.value(QStringLiteral("rowIdentity")).toString(),
             QStringLiteral("applications:org.kde.dolphin.desktop"));
    QCOMPARE(event.value(QStringLiteral("rowKind")).toInt(),
             static_cast<int>(Latte::Tasks::MiddleClickRowKind::Launcher));
    QCOMPARE(event.value(QStringLiteral("configuredAction")).toInt(),
             static_cast<int>(Latte::Tasks::Types::NewInstance));
    QCOMPARE(event.value(QStringLiteral("dispatchedOperation")).toInt(),
             static_cast<int>(Latte::Tasks::MiddleClickOperation::RequestActivate));
    QVERIFY(event.value(QStringLiteral("sequence")).toLongLong() > 0);

    QVERIFY(backend.recordMiddleClickDispatch(QStringLiteral("applications:org.kde.dolphin.desktop"),
                                              false,
                                              Latte::Tasks::Types::NewInstance,
                                              QStringLiteral("newInstance")));

    event = backend.latestMiddleClickDispatch();
    QCOMPARE(event.value(QStringLiteral("rowKind")).toInt(),
             static_cast<int>(Latte::Tasks::MiddleClickRowKind::Task));
    QCOMPARE(event.value(QStringLiteral("dispatchedOperation")).toInt(),
             static_cast<int>(Latte::Tasks::MiddleClickOperation::RequestNewInstance));
    QCOMPARE(spy.count(), 2);
}

void TasksBackendTest::middleClickDispatchAcceptsEveryOfferedPair_data()
{
    QTest::addColumn<int>("configuredAction");
    QTest::addColumn<QString>("taskToken");
    QTest::addColumn<int>("taskOperation");

    using Action = Latte::Tasks::Types;
    using Operation = Latte::Tasks::MiddleClickOperation;
    QTest::newRow("none") << static_cast<int>(Action::NoneAction) << QString()
                          << static_cast<int>(Operation::None);
    QTest::newRow("close") << static_cast<int>(Action::Close) << QStringLiteral("close")
                           << static_cast<int>(Operation::RequestClose);
    QTest::newRow("new instance") << static_cast<int>(Action::NewInstance) << QStringLiteral("newInstance")
                                  << static_cast<int>(Operation::RequestNewInstance);
    QTest::newRow("toggle minimized") << static_cast<int>(Action::ToggleMinimized) << QStringLiteral("toggleMinimized")
                                      << static_cast<int>(Operation::RequestToggleMinimized);
    QTest::newRow("cycle") << static_cast<int>(Action::CycleThroughTasks) << QStringLiteral("cycleOrActivate")
                           << static_cast<int>(Operation::CycleOrActivate);
    QTest::newRow("toggle grouping") << static_cast<int>(Action::ToggleGrouping) << QStringLiteral("toggleGrouping")
                                     << static_cast<int>(Operation::RequestToggleGrouping);
}

void TasksBackendTest::middleClickDispatchAcceptsEveryOfferedPair()
{
    QFETCH(int, configuredAction);
    QFETCH(QString, taskToken);
    QFETCH(int, taskOperation);

    Backend backend;
    QVERIFY(backend.recordMiddleClickDispatch(QStringLiteral("applications:task.desktop"),
                                              false,
                                              configuredAction,
                                              taskToken));
    QVariantMap event = backend.latestMiddleClickDispatch();
    QCOMPARE(event.value(QStringLiteral("configuredAction")).toInt(), configuredAction);
    QCOMPARE(event.value(QStringLiteral("dispatchedOperation")).toInt(), taskOperation);
    const qint64 taskSequence = event.value(QStringLiteral("sequence")).toLongLong();

    //! The pure-launcher exception applies to every offered configured action.
    QVERIFY(backend.recordMiddleClickDispatch(QStringLiteral("applications:launcher.desktop"),
                                              true,
                                              configuredAction,
                                              QStringLiteral("activate")));
    event = backend.latestMiddleClickDispatch();
    QCOMPARE(event.value(QStringLiteral("configuredAction")).toInt(), configuredAction);
    QCOMPARE(event.value(QStringLiteral("dispatchedOperation")).toInt(),
             static_cast<int>(Latte::Tasks::MiddleClickOperation::RequestActivate));
    QCOMPARE(event.value(QStringLiteral("sequence")).toLongLong(), taskSequence + 1);
}

void TasksBackendTest::middleClickDispatchSequenceIsProcessMonotonic()
{
    Backend first;
    Backend second;

    QVERIFY(first.recordMiddleClickDispatch(QStringLiteral("applications:first.desktop"),
                                            true,
                                            Latte::Tasks::Types::NewInstance,
                                            QStringLiteral("activate")));
    const qint64 firstSequence = first.latestMiddleClickDispatch().value(QStringLiteral("sequence")).toLongLong();

    QVERIFY(first.recordMiddleClickDispatch(QStringLiteral("applications:first.desktop"),
                                            false,
                                            Latte::Tasks::Types::NewInstance,
                                            QStringLiteral("newInstance")));
    const qint64 secondSequence = first.latestMiddleClickDispatch().value(QStringLiteral("sequence")).toLongLong();

    QVERIFY(second.recordMiddleClickDispatch(QStringLiteral("applications:second.desktop"),
                                             true,
                                             Latte::Tasks::Types::Close,
                                             QStringLiteral("activate")));
    const qint64 thirdSequence = second.latestMiddleClickDispatch().value(QStringLiteral("sequence")).toLongLong();

    QCOMPARE(secondSequence, firstSequence + 1);
    QCOMPARE(thirdSequence, secondSequence + 1);
}

void TasksBackendTest::middleClickDispatchRefusesMalformedInput()
{
    Backend backend;

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("outside the offered middle-click set")));
    QVERIFY(!backend.recordMiddleClickDispatch(QStringLiteral("applications:test.desktop"), true, 99,
                                               QStringLiteral("activate")));

    const QList<int> unofferedActions{
        Latte::Tasks::Types::PresentWindows,
        Latte::Tasks::Types::PreviewWindows,
        Latte::Tasks::Types::HighlightWindows,
        Latte::Tasks::Types::PreviewAndHighlightWindows};
    for (const int action : unofferedActions) {
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("outside the offered middle-click set")));
        QVERIFY(!backend.recordMiddleClickDispatch(QStringLiteral("applications:test.desktop"), true,
                                                   action,
                                                   QStringLiteral("activate")));
    }

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("unknown operation")));
    QVERIFY(!backend.recordMiddleClickDispatch(QStringLiteral("applications:test.desktop"), false,
                                               Latte::Tasks::Types::NewInstance,
                                               QStringLiteral("teleport")));

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("configured action, row kind, and operation disagree")));
    QVERIFY(!backend.recordMiddleClickDispatch(QStringLiteral("applications:test.desktop"), false,
                                               Latte::Tasks::Types::NewInstance,
                                               QStringLiteral("activate")));

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("configured action, row kind, and operation disagree")));
    QVERIFY(!backend.recordMiddleClickDispatch(QStringLiteral("applications:test.desktop"), false,
                                               Latte::Tasks::Types::Close,
                                               QStringLiteral("newInstance")));

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("configured action, row kind, and operation disagree")));
    QVERIFY(!backend.recordMiddleClickDispatch(QStringLiteral("applications:test.desktop"), true,
                                               Latte::Tasks::Types::Close,
                                               QStringLiteral("close")));

    QVERIFY(backend.latestMiddleClickDispatch().isEmpty());
}

QTEST_MAIN(TasksBackendTest)
#include "tasksbackendtest.moc"
