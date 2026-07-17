/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
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
// origin/main) and extended: the desktop-file table also proves a
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

QTEST_MAIN(TasksBackendTest)
#include "tasksbackendtest.moc"
