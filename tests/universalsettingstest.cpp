/*
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Real-link behavioral test for Latte::UniversalSettings over a throwaway
// KSharedConfig. Constructed with a plain QObject parent the internal
// qobject_cast<Latte::Corona*> yields a null corona, so every
// corona-independent getter/setter runs headlessly: each setter flips state,
// emits exactly once (guarded no-op on repeats) and persists the whole
// [UniversalSettings] group through the signal->saveConfig wiring; a second
// instance loading the same file reproduces every persisted value. The
// corona-dereferencing getters (splitterIconPath and friends) are
// deliberately not touched - with a null corona they would crash.
//
// Transplanted from latte-dock-qt6 (tests/universalsettingstest.cpp at
// 81384003, github.com/CaptSilver/latte-dock-qt6; their ctor grew a DI corona parameter, ours keeps upstream's
// parent-cast shape) and raised: pins THIS tree's deliberate divergence that
// inConfigureAppletsMode is transient and never persisted (loadConfig
// deletes stale entries older builds wrote - the fork's tree still persists
// it and their round trip asserts the opposite).

// local
#include "apptypes.h"
#include "data/contextmenudata.h"
#include "data/preferencesdata.h"
#include "settings/universalsettings.h"

#include <coretypes.h>

// Qt
#include <QFile>
#include <QGuiApplication>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

// KDE
#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>

using Latte::UniversalSettings;
namespace Data = Latte::Data;

class UniversalSettingsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();

    void constructWithNullCoronaFromPlainParent();
    void reportMemberDefaultsBeforeLoad();
    void emitOncePerRealChangeAndGuardRepeats();
    void roundTripSettingsThroughConfigFile();
    void keepConfigureAppletsModeTransient();
    void loadDocumentedDefaultsFromEmptyConfig();
    void roundTripScreenScales();
    void pinSensitivityHardwiredHigh();

private:
    KSharedConfig::Ptr openFreshConfig();
    //! load() enables autostart on a virgin config (first-run behavior);
    //! seeding userConfiguredAutostart keeps load() deterministic and off
    //! the autostart directory (enableAutostart depends on a desktop file
    //! in the environment's standard paths, so its outcome is not ours to
    //! assert here).
    void seedUserConfiguredAutostart();

    QTemporaryDir m_dir;
    QString m_configPath;
};

KSharedConfig::Ptr UniversalSettingsTest::openFreshConfig()
{
    // a standalone backing file, not the global config: nothing real leaks in
    return KSharedConfig::openConfig(m_configPath, KConfig::SimpleConfig);
}

void UniversalSettingsTest::seedUserConfiguredAutostart()
{
    KConfig seed(m_configPath, KConfig::SimpleConfig);
    seed.group(QStringLiteral("UniversalSettings")).writeEntry(QStringLiteral("userConfiguredAutostart"), true);
    seed.sync();
}

void UniversalSettingsTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_configPath = m_dir.filePath(QStringLiteral("lattedockrc"));
}

void UniversalSettingsTest::init()
{
    // every case starts from an empty config file
    QFile::remove(m_configPath);
}

void UniversalSettingsTest::constructWithNullCoronaFromPlainParent()
{
    // the whole headless approach hinges on this: a plain QObject parent
    // keeps m_corona null and the corona-dependent code untouched
    QObject parent;
    UniversalSettings settings(openFreshConfig(), &parent);

    // reaching corona-independent getters must not crash
    QCOMPARE(settings.showInfoWindow(), true);
    QCOMPARE(settings.metaPressAndHoldEnabled(), true);
}

void UniversalSettingsTest::reportMemberDefaultsBeforeLoad()
{
    // member initializers define the pre-load state, independent of config
    UniversalSettings settings(openFreshConfig(), this);

    QCOMPARE(settings.showInfoWindow(), true);
    QCOMPARE(settings.metaPressAndHoldEnabled(), true);
    QCOMPARE(settings.isAvailableGeometryBroadcastedToPlasma(), true);
    QCOMPARE(settings.badges3DStyle(), false);
    QCOMPARE(settings.canDisableBorders(), false);
    QCOMPARE(settings.inAdvancedModeForEditSettings(), false);
    QCOMPARE(settings.inConfigureAppletsMode(), false);
    QCOMPARE(settings.version(), 1);
    QCOMPARE(settings.screenTrackerInterval(), 2500);
    QCOMPARE(settings.parabolicSpread(), Data::Preferences::PARABOLICSPREAD);
    QCOMPARE(settings.thicknessMarginInfluence(), Data::Preferences::THICKNESSMARGININFLUENCE);
    QVERIFY(settings.singleModeLayoutName().isEmpty());
}

void UniversalSettingsTest::emitOncePerRealChangeAndGuardRepeats()
{
    UniversalSettings settings(openFreshConfig(), this);

    QSignalSpy showSpy(&settings, &UniversalSettings::showInfoWindowChanged);
    QSignalSpy spreadSpy(&settings, &UniversalSettings::parabolicSpreadChanged);
    QSignalSpy advancedSpy(&settings, &UniversalSettings::inAdvancedModeForEditSettingsChanged);
    QSignalSpy configureSpy(&settings, &UniversalSettings::inConfigureAppletsModeChanged);

    // a real change emits exactly once and updates the getter
    settings.setShowInfoWindow(false);
    QCOMPARE(settings.showInfoWindow(), false);
    QCOMPARE(showSpy.count(), 1);

    // setting the same value again is a guarded no-op: no extra signal
    settings.setShowInfoWindow(false);
    QCOMPARE(showSpy.count(), 1);

    settings.setParabolicSpread(7);
    QCOMPARE(settings.parabolicSpread(), 7);
    QCOMPARE(spreadSpy.count(), 1);
    settings.setParabolicSpread(7);
    QCOMPARE(spreadSpy.count(), 1);

    settings.setInAdvancedModeForEditSettings(true);
    QCOMPARE(settings.inAdvancedModeForEditSettings(), true);
    QCOMPARE(advancedSpy.count(), 1);

    settings.setInConfigureAppletsMode(true);
    QCOMPARE(settings.inConfigureAppletsMode(), true);
    QCOMPARE(configureSpy.count(), 1);
    settings.setInConfigureAppletsMode(true);
    QCOMPARE(configureSpy.count(), 1);
}

void UniversalSettingsTest::roundTripSettingsThroughConfigFile()
{
    const QStringList launchers = {QStringLiteral("applications:org.kde.dolphin.desktop"),
                                   QStringLiteral("applications:org.kde.konsole.desktop")};
    const QStringList actions = {QStringLiteral("_layouts"), QStringLiteral("_preferences")};

    // each setter fires its change signal, which is wired to saveConfig: the
    // whole [UniversalSettings] group is rewritten and synced to disk
    {
        UniversalSettings settings(openFreshConfig(), this);
        settings.setShowInfoWindow(false);
        settings.setBadges3DStyle(true);
        settings.setCanDisableBorders(true);
        settings.setInAdvancedModeForEditSettings(true);
        settings.setIsAvailableGeometryBroadcastedToPlasma(false);
        settings.setMetaPressAndHoldEnabled(false);
        settings.setVersion(7);
        settings.setScreenTrackerInterval(4000);
        settings.setParabolicSpread(9);
        settings.setThicknessMarginInfluence(0.5f);
        settings.setSingleModeLayoutName(QStringLiteral("MyLayout"));
        settings.setLaunchers(launchers);
        settings.setContextMenuActionsAlwaysShown(actions);
        settings.syncSettings();
    }

    // the values really landed under [UniversalSettings] on disk
    {
        KConfig disk(m_configPath, KConfig::SimpleConfig);
        const KConfigGroup g = disk.group(QStringLiteral("UniversalSettings"));
        QCOMPARE(g.readEntry(QStringLiteral("version"), 1), 7);
        QCOMPARE(g.readEntry(QStringLiteral("showInfoWindow"), true), false);
        QCOMPARE(g.readEntry(QStringLiteral("badges3DStyle"), false), true);
        QCOMPARE(g.readEntry(QStringLiteral("parabolicSpread"), 0), 9);
        QCOMPARE(g.readEntry(QStringLiteral("singleModeLayoutName"), QString()), QStringLiteral("MyLayout"));
        QCOMPARE(g.readEntry(QStringLiteral("launchers"), QStringList()), launchers);
        // the thickness margin is persisted under a deliberately different key
        QCOMPARE(g.readEntry(QStringLiteral("parabolicThicknessMarginInfluence"), 1.0f), 0.5f);
    }

    // a second instance loading the same config reproduces every value;
    // seed the autostart flag first so load() stays deterministic
    seedUserConfiguredAutostart();
    UniversalSettings reloaded(openFreshConfig(), this);
    reloaded.load();

    QCOMPARE(reloaded.showInfoWindow(), false);
    QCOMPARE(reloaded.badges3DStyle(), true);
    QCOMPARE(reloaded.canDisableBorders(), true);
    QCOMPARE(reloaded.inAdvancedModeForEditSettings(), true);
    QCOMPARE(reloaded.isAvailableGeometryBroadcastedToPlasma(), false);
    QCOMPARE(reloaded.metaPressAndHoldEnabled(), false);
    QCOMPARE(reloaded.version(), 7);
    QCOMPARE(reloaded.screenTrackerInterval(), 4000);
    QCOMPARE(reloaded.parabolicSpread(), 9);
    QCOMPARE(reloaded.thicknessMarginInfluence(), 0.5f);
    QCOMPARE(reloaded.singleModeLayoutName(), QStringLiteral("MyLayout"));
    QCOMPARE(reloaded.launchers(), launchers);
    QCOMPARE(reloaded.contextMenuActionsAlwaysShown(), actions);
    // layoutsMemoryUsage is private here (Layouts::Manager territory),
    // unlike the fork's tree - deliberately not reached
}

void UniversalSettingsTest::keepConfigureAppletsModeTransient()
{
    // THIS tree's contract (deliberate divergence, see loadConfig's comment):
    // the rearrange sub-mode is transient editing state - flipping it never
    // persists, and a stale entry an older build wrote is dropped on load,
    // so a session that dies while configuring cannot poison the next one
    {
        UniversalSettings settings(openFreshConfig(), this);
        settings.setInConfigureAppletsMode(true);
        settings.syncSettings();

        KConfig disk(m_configPath, KConfig::SimpleConfig);
        QVERIFY(!disk.group(QStringLiteral("UniversalSettings")).hasKey(QStringLiteral("inConfigureAppletsMode")));
    }

    // a stale entry (older build) is deleted on load and never restored
    {
        KConfig seed(m_configPath, KConfig::SimpleConfig);
        seed.group(QStringLiteral("UniversalSettings")).writeEntry(QStringLiteral("inConfigureAppletsMode"), true);
        seed.sync();
    }
    seedUserConfiguredAutostart();

    UniversalSettings reloaded(openFreshConfig(), this);
    reloaded.load();
    QCOMPARE(reloaded.inConfigureAppletsMode(), false);

    KConfig disk(m_configPath, KConfig::SimpleConfig);
    QVERIFY(!disk.group(QStringLiteral("UniversalSettings")).hasKey(QStringLiteral("inConfigureAppletsMode")));
}

void UniversalSettingsTest::loadDocumentedDefaultsFromEmptyConfig()
{
    seedUserConfiguredAutostart();

    UniversalSettings settings(openFreshConfig(), this);
    settings.load();

    // documented defaults, read through loadConfig on an otherwise empty group
    QCOMPARE(settings.version(), 1);
    QCOMPARE(settings.badges3DStyle(), false);
    QCOMPARE(settings.canDisableBorders(), false);
    QCOMPARE(settings.showInfoWindow(), true);
    QCOMPARE(settings.metaPressAndHoldEnabled(), true);
    QCOMPARE(settings.isAvailableGeometryBroadcastedToPlasma(), true);
    QCOMPARE(settings.screenTrackerInterval(), 2500);
    QCOMPARE(settings.parabolicSpread(), Data::Preferences::PARABOLICSPREAD);
    QCOMPARE(settings.thicknessMarginInfluence(), Data::Preferences::THICKNESSMARGININFLUENCE);

    // contextMenuActionsAlwaysShown defaults to the built-in always-visible set
    QCOMPARE(settings.contextMenuActionsAlwaysShown(), Latte::Data::ContextMenu::ACTIONSALWAYSVISIBLE);
}

void UniversalSettingsTest::roundTripScreenScales()
{
    seedUserConfiguredAutostart();

    {
        UniversalSettings settings(openFreshConfig(), this);
        settings.load();

        // an unknown screen always reads back unity scale
        QCOMPARE(settings.screenWidthScale(QStringLiteral("Nonexistent")), 1.0f);
        QCOMPARE(settings.screenHeightScale(QStringLiteral("Nonexistent")), 1.0f);

        QSignalSpy scaleSpy(&settings, &UniversalSettings::screenScalesChanged);
        settings.setScreenScales(QStringLiteral("DP-1"), 1.25f, 0.75f);
        QCOMPARE(scaleSpy.count(), 1);
        QCOMPARE(settings.screenWidthScale(QStringLiteral("DP-1")), 1.25f);
        QCOMPARE(settings.screenHeightScale(QStringLiteral("DP-1")), 0.75f);

        // same values are a guarded no-op
        settings.setScreenScales(QStringLiteral("DP-1"), 1.25f, 0.75f);
        QCOMPARE(scaleSpy.count(), 1);
    }

    // scales persist to the nested [UniversalSettings][ScreenScales] group
    // and a fresh load brings them back
    UniversalSettings reloaded(openFreshConfig(), this);
    reloaded.load();
    QCOMPARE(reloaded.screenWidthScale(QStringLiteral("DP-1")), 1.25f);
    QCOMPARE(reloaded.screenHeightScale(QStringLiteral("DP-1")), 0.75f);
}

void UniversalSettingsTest::pinSensitivityHardwiredHigh()
{
    // upstream's own decision (40b4851da): sensitivity() hard-returns
    // HighMouseSensitivity because the option never gathered user interest;
    // the setter updates the member but the getter ignores it. Qt5-faithful,
    // pinned so a future cleanup does not "fix" it silently.
    UniversalSettings settings(openFreshConfig(), this);
    QCOMPARE(settings.sensitivity(), Latte::Settings::HighMouseSensitivity);

    settings.setSensitivity(Latte::Settings::LowMouseSensitivity);
    QCOMPARE(settings.sensitivity(), Latte::Settings::HighMouseSensitivity);
}

int main(int argc, char *argv[])
{
    // The ctor resolves kwinrc and load() resolves the autostart dir through
    // Latte::configPath(); point XDG_CONFIG_HOME at a throwaway dir before
    // QGuiApplication so neither can read or write the real desktop config
    // (same pattern as screenpooltest). The ctor also wires
    // QGuiApplication::screenAdded/screenRemoved, so a real gui app object
    // is required; the offscreen platform keeps it headless.
    static QTemporaryDir xdgConfig;
    qputenv("XDG_CONFIG_HOME", xdgConfig.path().toUtf8());

    QGuiApplication app(argc, argv);
    UniversalSettingsTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "universalsettingstest.moc"
