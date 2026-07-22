/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Render regressions for D25 (task icons stay stale after icon-theme changes)
//! and D128 (task artwork rounds below its autosized slot). The real shipped
//! ThemeAwareIcon is rendered with one named QIcon while KIconTheme switches
//! between two fixture themes that provide the same icon name as different
//! solid colors. The QML source QVariant and cache key must stay untouched;
//! only Kirigami.Icon's cached raster is rebuilt. Non-standard slot sizes must
//! paint completely instead of snapping the artwork to a smaller standard
//! icon size.

// local
#include "environment.h"

// Qt
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QTest>

// KDE
#include <KIconLoader>
#include <KIconTheme>

namespace
{
constexpr auto RedTheme = "latte-theme-red";
constexpr auto BlueTheme = "latte-theme-blue";
constexpr auto SharedIconName = "latte-theme-refresh-test";

void publishIconTheme(const char *theme)
{
    KIconTheme::forceThemeForTests(QString::fromLatin1(theme));
    KIconTheme::reconfigure();
    KIconLoader::global()->reconfigure(QString());

    // reconfigure() updates the loader cache. This explicit settings signal
    // models the notification emitted by the real settings path and also
    // makes repeated same-theme notifications deterministic in this test.
    KIconLoader::global()->iconLoaderSettingsChanged();
}

QColor renderedCenter(QQuickView &view)
{
    const QImage frame = view.grabWindow();
    if (frame.isNull()) {
        return {};
    }

    return frame.pixelColor(frame.width() / 2, frame.height() / 2);
}

QString viewErrors(const QQuickView &view)
{
    QStringList errors;
    for (const QQmlError &error : view.errors()) {
        errors.append(error.toString());
    }
    return errors.join(QLatin1Char('\n'));
}
}

class ThemeAwareIconTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void environmentDeduplicatesLoaderNotifications();
    void namedIconRefreshesWithoutSourceReassignment();
    void namelessPixmapIconRemainsUnchanged();
    void nonStandardSlotPaintsAtComputedSize();

private:
    void prepareView(QQuickView &view, const QIcon &icon,
                     const QSize &size = QSize{64, 64});

    Latte::Environment *m_environment{nullptr};
};

void ThemeAwareIconTest::initTestCase()
{
    const QByteArray pinnedModules = qgetenv("LATTE_QML_MODULE_PATH");
    QVERIFY2(!pinnedModules.isEmpty(),
             "LATTE_QML_MODULE_PATH is not set; run inside the flake devShell");

    publishIconTheme(RedTheme);
    m_environment = new Latte::Environment(this);
    qmlRegisterSingletonInstance("org.kde.latte.core", 0, 2,
                                 "Environment", m_environment);
}

void ThemeAwareIconTest::cleanupTestCase()
{
    KIconTheme::forceThemeForTests(QString());
    KIconTheme::reconfigure();
    KIconLoader::global()->reconfigure(QString());
}

void ThemeAwareIconTest::prepareView(QQuickView &view, const QIcon &icon,
                                     const QSize &size)
{
    const QStringList importPaths = QString::fromLocal8Bit(
        qgetenv("LATTE_QML_MODULE_PATH")).split(QLatin1Char(':'), Qt::SkipEmptyParts);
    for (auto it = importPaths.crbegin(); it != importPaths.crend(); ++it) {
        view.engine()->addImportPath(*it);
    }

    view.setColor(Qt::transparent);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setInitialProperties({{QStringLiteral("iconSource"), QVariant::fromValue(icon)}});
    view.setSource(QUrl::fromLocalFile(QStringLiteral(THEMEAWAREICON_QML_PATH)));
    QVERIFY2(view.status() == QQuickView::Ready, qPrintable(viewErrors(view)));
    view.resize(size);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTRY_VERIFY_WITH_TIMEOUT(!view.grabWindow().isNull(), 5000);
}

void ThemeAwareIconTest::environmentDeduplicatesLoaderNotifications()
{
    publishIconTheme(RedTheme);
    QSignalSpy themeChanged(m_environment, &Latte::Environment::iconThemeChanged);

    KIconLoader::global()->iconLoaderSettingsChanged();
    KIconLoader::global()->iconLoaderSettingsChanged();
    QCOMPARE(themeChanged.count(), 0);

    publishIconTheme(BlueTheme);
    QCOMPARE(themeChanged.count(), 1);

    KIconLoader::global()->iconLoaderSettingsChanged();
    QCOMPARE(themeChanged.count(), 1);
}

void ThemeAwareIconTest::namedIconRefreshesWithoutSourceReassignment()
{
    publishIconTheme(RedTheme);
    const QIcon icon = KDE::icon(QString::fromLatin1(SharedIconName));
    QVERIFY(!icon.isNull());
    QCOMPARE(icon.name(), QString::fromLatin1(SharedIconName));

    QQuickView view;
    prepareView(view, icon);
    QObject *iconItem = view.rootObject();
    QVERIFY(iconItem);

    const QIcon storedIcon = iconItem->property("iconSource").value<QIcon>();
    const qint64 cacheKey = storedIcon.cacheKey();
    QCOMPARE(storedIcon.name(), QString::fromLatin1(SharedIconName));
    QSignalSpy sourceChanged(iconItem, SIGNAL(iconSourceChanged()));

    QTRY_COMPARE_WITH_TIMEOUT(renderedCenter(view), QColor(Qt::red), 5000);

    publishIconTheme(BlueTheme);
    const QIcon reboundSource = iconItem->property("source").value<QIcon>();
    QCOMPARE(reboundSource.name(), QString::fromLatin1(SharedIconName));
    QCOMPARE(reboundSource.cacheKey(), cacheKey);
    QCOMPARE(icon.pixmap(32, 32).toImage().pixelColor(16, 16), QColor(Qt::blue));
    QTRY_COMPARE_WITH_TIMEOUT(renderedCenter(view), QColor(Qt::blue), 5000);

    QCOMPARE(sourceChanged.count(), 0);
    const QIcon refreshedSource = iconItem->property("iconSource").value<QIcon>();
    QCOMPARE(refreshedSource.name(), QString::fromLatin1(SharedIconName));
    QCOMPARE(refreshedSource.cacheKey(), cacheKey);
}

void ThemeAwareIconTest::namelessPixmapIconRemainsUnchanged()
{
    publishIconTheme(RedTheme);
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::green);
    const QIcon icon(pixmap);
    QVERIFY(icon.name().isEmpty());

    QQuickView view;
    prepareView(view, icon);
    QObject *iconItem = view.rootObject();
    QVERIFY(iconItem);

    const qint64 cacheKey = iconItem->property("iconSource").value<QIcon>().cacheKey();
    QSignalSpy sourceChanged(iconItem, SIGNAL(iconSourceChanged()));
    QTRY_COMPARE_WITH_TIMEOUT(renderedCenter(view), QColor(Qt::green), 5000);

    publishIconTheme(BlueTheme);
    const QIcon reboundSource = iconItem->property("source").value<QIcon>();
    QVERIFY(reboundSource.name().isEmpty());
    QCOMPARE(reboundSource.cacheKey(), cacheKey);
    QTRY_COMPARE_WITH_TIMEOUT(renderedCenter(view), QColor(Qt::green), 5000);

    QCOMPARE(sourceChanged.count(), 0);
    const QIcon refreshedSource = iconItem->property("iconSource").value<QIcon>();
    QVERIFY(refreshedSource.name().isEmpty());
    QCOMPARE(refreshedSource.cacheKey(), cacheKey);
}

void ThemeAwareIconTest::nonStandardSlotPaintsAtComputedSize()
{
    publishIconTheme(RedTheme);
    const QIcon icon = KDE::icon(QString::fromLatin1(SharedIconName));
    QVERIFY(!icon.isNull());

    QQuickView view;
    constexpr QSize slotSize{63, 63};
    prepareView(view, icon, slotSize);
    QObject *const iconItem = view.rootObject();
    QVERIFY(iconItem);

    QCOMPARE(iconItem->property("roundToIconSize").toBool(), false);
    QTRY_COMPARE_WITH_TIMEOUT(iconItem->property("paintedWidth").toReal(),
                              static_cast<qreal>(slotSize.width()), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(iconItem->property("paintedHeight").toReal(),
                              static_cast<qreal>(slotSize.height()), 5000);

    const QImage frame = view.grabWindow();
    QVERIFY(!frame.isNull());
    QCOMPARE(frame.pixelColor(1, 1), QColor(Qt::red));
    QCOMPARE(frame.pixelColor(frame.width() - 2, frame.height() - 2),
             QColor(Qt::red));
}

int main(int argc, char **argv)
{
    const QByteArray fixtures = QByteArrayLiteral(ICON_THEME_FIXTURE_ROOT);
    const QByteArray existingDataDirs = qgetenv("XDG_DATA_DIRS");
    QByteArray dataDirs = fixtures;
    if (!existingDataDirs.isEmpty()) {
        dataDirs.append(':');
        dataDirs.append(existingDataDirs);
    }
    qputenv("XDG_DATA_DIRS", dataDirs);
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QSG_RHI_BACKEND", "software");

    QGuiApplication app(argc, argv);
    ThemeAwareIconTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "themeawareicontest.moc"
