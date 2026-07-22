/*
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Real-link behavioral test for Latte::Layouts::Storage, the layout-file
// engine everything .latte rides on. Drives the Storage::self() singleton
// (linked through lattedock-core) against temp KConfig fixtures; every method
// exercised operates on a file path or KConfigGroup without a live Corona:
// containment/applet enumeration, the view() deserializer and updateView()
// serializer, subcontainment detection, clone handling, the errors/warnings
// scanners, template export/import and the null-corona screen-id branch.
//
// Transplanted from latte-dock-qt6 (tests/storagetest.cpp at 81384003, github.com/CaptSilver/latte-dock-qt6)
// and raised: adds the view() defaults table for unset keys, the
// updateView() non-Latte refusal, the clean-layout negative for
// errors/warnings, the containment-id filter observability in plugins()
// (their case only asserted rowCount >= 1), and the LayoutSettings +
// isPreferredForShortcuts clearing in exportTemplate. Their
// updateViewRoundTripsThroughKConfig caught upstream's dead-key maxLength
// write (their fix b48903ec); the same defect was live here and is fixed in
// the commit right before this test landed - the round-trip case pins it.

// local
#include "data/appletdata.h"
#include "data/errordata.h"
#include "data/generictable.h"
#include "data/genericdata.h"
#include "data/viewdata.h"
#include "data/viewstable.h"
#include "layout/centrallayout.h"
#include "layouts/storage.h"
#include "screenpool.h"

#include <coretypes.h>

// Qt
#include <QFile>
#include <QGuiApplication>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

// KDE
#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>

using Latte::Layouts::Storage;

class StorageTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    //! id helpers
    void pinIdSentinelsAndValidity();

    //! group classification
    void classifyLatteContainmentByPlugin();
    void resolveSubContainmentIdFromBothIdentityKeys();
    void rejectPreloadShellAsAppletGroup();

    //! view() deserializer
    void deserializeViewFromContainmentGroup();
    void deserializeViewDefaultsForUnsetKeys();
    void refuseMalformedLinkedPlacement();
    void refuseViewForNonLatteContainment();

    //! updateView() serializer
    void roundTripViewThroughKConfig();
    void refuseUpdateViewOnNonLatteGroup();

    //! enumeration
    void enumerateOnlyLatteContainmentsAsViews();
    void listSubcontainmentsOfContainmentGroup();
    void reportContainsViewOnlyForLatteIds();
    void enumerateViewsOfInactiveLayout();
    void validatePersistedRelationshipGraphs_data();
    void validatePersistedRelationshipGraphs();
    void restoreRemovalSnapshotReplacesPartialGroup();

    //! clones
    void detectClonedViewsOnlyForLatteContainments();
    void removeScreenGroupDerivedViewsKeepsPersistentRelationships();

    //! removal
    void removeContainmentDeletesExactlyThatGroup();
    void removeViewDropsViewAndItsSubcontainments();

    //! screen id resolution
    void resolveNoScreenIdWithoutCorona();

    //! integrity scanners
    void reportNoErrorsOrWarningsForCleanLayout();
    void reportDuplicateAppletIdsAsError();
    void reportOrphanedSubcontainmentAsWarning();

    //! template export/import
    //! (importContainments is private here, unlike the fork's tree; its
    //! observable effect is pinned through newView's inactive branch in
    //! remapIdsWhenAddingTemplateViewToInactiveLayout)
    void stripUnapprovedAppletsFromExportedTemplate();
    void clearLayoutSettingsInExportedTemplate();

    //! metadata
    void fallBackToPluginIdForUnknownApplet();
    void gatherAppletPluginsFilteredByContainmentId();

    //! template instantiation
    void remapIdsWhenAddingTemplateViewToInactiveLayout();
    void writeStoredViewOfInactiveLayoutToTempFile();

private:
    //! Writes a layout file with one Latte containment (id 1) carrying a
    //! plasmoid applet (id 2) and a systray applet (id 3) whose configuration
    //! points at subcontainment 99, plus a non-Latte containment (id 5) with
    //! an own applet (id 6) and the subcontainment itself (id 99). Returns
    //! the file path.
    QString writeLayoutFixture(const QString &name);

    QTemporaryDir m_dir;
};

void StorageTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    QVERIFY(Storage::self() != nullptr);
}

void StorageTest::validatePersistedRelationshipGraphs_data()
{
    QTest::addColumn<QString>("shape");
    QTest::addColumn<bool>("valid");

    QTest::newRow("direct-root") << QStringLiteral("direct") << true;
    QTest::newRow("missing-root") << QStringLiteral("missing") << false;
    QTest::newRow("linked-chain") << QStringLiteral("chain") << false;
    QTest::newRow("self-cycle") << QStringLiteral("self") << false;
    QTest::newRow("two-member-cycle") << QStringLiteral("cycle") << false;
}

void StorageTest::validatePersistedRelationshipGraphs()
{
    QFETCH(QString, shape);
    QFETCH(bool, valid);

    const QString path = m_dir.filePath(QStringLiteral("relationship-%1.layout.latte").arg(shape));
    KSharedConfigPtr config = KSharedConfig::openConfig(path);
    KConfigGroup containments(config, QStringLiteral("Containments"));

    const auto writeDock = [&containments](const int id,
                                           const int rootId,
                                           const Latte::Data::View::LinkPlacement placement) {
        KConfigGroup group = containments.group(QString::number(id));
        group.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
        group.writeEntry(QStringLiteral("isClonedFrom"), rootId);
        group.writeEntry(QStringLiteral("linkPlacement"), static_cast<int>(placement));
    };

    constexpr auto local = Latte::Data::View::LinkPlacement::ScreenGroupDerived;
    constexpr auto linked = Latte::Data::View::LinkPlacement::ExplicitTarget;
    writeDock(1, Latte::Data::View::ISCLONEDNULL, local);

    if (shape == QStringLiteral("direct")) {
        writeDock(2, 1, linked);
    } else if (shape == QStringLiteral("missing")) {
        writeDock(2, 99, linked);
    } else if (shape == QStringLiteral("chain")) {
        writeDock(2, 1, linked);
        writeDock(3, 2, linked);
    } else if (shape == QStringLiteral("self")) {
        writeDock(2, 2, linked);
    } else if (shape == QStringLiteral("cycle")) {
        writeDock(2, 3, linked);
        writeDock(3, 2, linked);
    }
    config->sync();

    const Latte::Data::ViewsTable views = Storage::self()->views(path);
    QCOMPARE(views.relationshipValidationError().isEmpty(), valid);
}

void StorageTest::restoreRemovalSnapshotReplacesPartialGroup()
{
    const QString snapshotPath = m_dir.filePath(QStringLiteral("removal-snapshot.layout.latte"));
    const QString destinationPath = m_dir.filePath(QStringLiteral("removal-destination.layout.latte"));

    {
        const KSharedConfigPtr snapshot = KSharedConfig::openConfig(snapshotPath);
        KConfigGroup containments(snapshot, QStringLiteral("Containments"));
        KConfigGroup containment = containments.group(QStringLiteral("12"));
        containment.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
        containment.writeEntry(QStringLiteral("isClonedFrom"), 1);
        containment.writeEntry(QStringLiteral("linkPlacement"),
                               static_cast<int>(Latte::Data::View::LinkPlacement::ExplicitTarget));
        KConfigGroup applet = containment.group(QStringLiteral("Applets")).group(QStringLiteral("40"));
        applet.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.plasma.minimizeall"));
        snapshot->sync();
    }

    {
        const KSharedConfigPtr destination = KSharedConfig::openConfig(destinationPath);
        KConfigGroup containments(destination, QStringLiteral("Containments"));
        KConfigGroup partial = containments.group(QStringLiteral("12"));
        partial.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
        partial.writeEntry(QStringLiteral("stalePartialValue"), true);
        destination->sync();
    }

    QVERIFY(Storage::self()->restoreView(destinationPath, snapshotPath));

    const KSharedConfigPtr restored = KSharedConfig::openConfig(destinationPath);
    const KConfigGroup containments(restored, QStringLiteral("Containments"));
    const KConfigGroup containment = containments.group(QStringLiteral("12"));
    QCOMPARE(containment.readEntry(QStringLiteral("isClonedFrom"), -1), 1);
    QCOMPARE(containment.readEntry(QStringLiteral("linkPlacement"), -1),
             static_cast<int>(Latte::Data::View::LinkPlacement::ExplicitTarget));
    QVERIFY(!containment.hasKey(QStringLiteral("stalePartialValue")));
    QCOMPARE(containment.group(QStringLiteral("Applets")).group(QStringLiteral("40"))
                 .readEntry(QStringLiteral("plugin"), QString{}),
             QStringLiteral("org.kde.plasma.minimizeall"));
}

QString StorageTest::writeLayoutFixture(const QString &name)
{
    const QString path = m_dir.filePath(name);
    KSharedConfigPtr ptr = KSharedConfig::openConfig(path);
    KConfigGroup conts(ptr, QStringLiteral("Containments"));

    KConfigGroup c1 = conts.group(QStringLiteral("1"));
    c1.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
    c1.writeEntry(QStringLiteral("name"), QStringLiteral("My Dock"));
    c1.writeEntry(QStringLiteral("location"), (int)Plasma::Types::LeftEdge);
    c1.writeEntry(QStringLiteral("onPrimary"), false);
    c1.writeEntry(QStringLiteral("lastScreen"), 12);
    c1.writeEntry(QStringLiteral("screensGroup"), (int)Latte::Types::AllSecondaryScreensGroup);
    c1.writeEntry(QStringLiteral("isClonedFrom"), 5);
    c1.group(QStringLiteral("General")).writeEntry(QStringLiteral("maxLength"), (float)80.0);
    c1.group(QStringLiteral("General")).writeEntry(QStringLiteral("alignment"), (int)Latte::Types::Justify);
    c1.group(QStringLiteral("General")).writeEntry(QStringLiteral("screenEdgeMargin"), 7);

    KConfigGroup applets = c1.group(QStringLiteral("Applets"));
    KConfigGroup a2 = applets.group(QStringLiteral("2"));
    a2.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.plasmoid"));
    KConfigGroup a3 = applets.group(QStringLiteral("3"));
    a3.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.plasma.private.systemtray"));
    a3.group(QStringLiteral("Configuration")).writeEntry(QStringLiteral("SystrayContainmentId"), 99);

    KConfigGroup c5 = conts.group(QStringLiteral("5"));
    c5.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.desktopcontainment"));
    KConfigGroup a6 = c5.group(QStringLiteral("Applets")).group(QStringLiteral("6"));
    a6.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.plasma.kickoff"));

    KConfigGroup c99 = conts.group(QStringLiteral("99"));
    c99.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.plasma.private.systemtray"));

    ptr->sync();
    return path;
}

void StorageTest::pinIdSentinelsAndValidity()
{
    QCOMPARE(Storage::IDNULL, -1);
    QCOMPARE(Storage::IDBASE, 0);
    QVERIFY(!Storage::isValid(-1));
    QVERIFY(Storage::isValid(0));
    QVERIFY(Storage::isValid(99));
}

void StorageTest::classifyLatteContainmentByPlugin()
{
    KConfig cfg(m_dir.filePath(QStringLiteral("plugincheck.latte")));

    KConfigGroup latte = cfg.group(QStringLiteral("latte"));
    latte.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
    QVERIFY(Storage::self()->isLatteContainment(latte));

    KConfigGroup other = cfg.group(QStringLiteral("other"));
    other.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.plasma.desktop"));
    QVERIFY(!Storage::self()->isLatteContainment(other));

    KConfigGroup empty = cfg.group(QStringLiteral("empty"));
    QVERIFY(!Storage::self()->isLatteContainment(empty));
}

void StorageTest::resolveSubContainmentIdFromBothIdentityKeys()
{
    const QString path = writeLayoutFixture(QStringLiteral("subs.latte"));
    KSharedConfigPtr ptr = KSharedConfig::openConfig(path);
    KConfigGroup applets =
        KConfigGroup(ptr, QStringLiteral("Containments")).group(QStringLiteral("1")).group(QStringLiteral("Applets"));

    // the systray applet declares [Configuration]SystrayContainmentId 99
    QCOMPARE(Storage::self()->subContainmentId(applets.group(QStringLiteral("3"))), 99);

    // a plain plasmoid is not a subcontainment
    QCOMPARE(Storage::self()->subContainmentId(applets.group(QStringLiteral("2"))), Storage::IDNULL);

    // ContainmentId is the second recognized identity key
    KConfig cfg(m_dir.filePath(QStringLiteral("groupapplet.latte")));
    KConfigGroup grp = cfg.group(QStringLiteral("g"));
    grp.group(QStringLiteral("Configuration")).writeEntry(QStringLiteral("ContainmentId"), 42);
    QCOMPARE(Storage::self()->subContainmentId(grp), 42);
}

void StorageTest::rejectPreloadShellAsAppletGroup()
{
    KConfig cfg(m_dir.filePath(QStringLiteral("validity.latte")));

    // no own keys, only [Configuration]PreloadWeight: the husk a removed
    // applet leaves behind, not a real applet
    KConfigGroup shell = cfg.group(QStringLiteral("shell"));
    shell.group(QStringLiteral("Configuration")).writeEntry(QStringLiteral("PreloadWeight"), 42);
    QVERIFY(!Storage::appletGroupIsValid(shell));

    // a real applet has a plugin key
    KConfigGroup real = cfg.group(QStringLiteral("real"));
    real.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.plasmoid"));
    real.group(QStringLiteral("Configuration")).writeEntry(QStringLiteral("PreloadWeight"), 42);
    QVERIFY(Storage::appletGroupIsValid(real));
}

void StorageTest::deserializeViewFromContainmentGroup()
{
    const QString path = writeLayoutFixture(QStringLiteral("viewread.latte"));
    KSharedConfigPtr ptr = KSharedConfig::openConfig(path);
    KConfigGroup c1 = KConfigGroup(ptr, QStringLiteral("Containments")).group(QStringLiteral("1"));

    Latte::Data::View v = Storage::self()->view(c1);

    QVERIFY(v.isValid());
    QCOMPARE(v.id, QStringLiteral("1"));
    QCOMPARE(v.name, QStringLiteral("My Dock"));
    QCOMPARE(v.onPrimary, false);
    QCOMPARE(v.screen, 12);
    QCOMPARE(v.isClonedFrom, 5);
    QCOMPARE(v.linkPlacement, Latte::Data::View::LinkPlacement::ScreenGroupDerived);
    QCOMPARE(v.screenEdgeMargin, 7);
    QCOMPARE(v.screensGroup, Latte::Types::AllSecondaryScreensGroup);
    QCOMPARE(v.edge, Plasma::Types::LeftEdge);
    QCOMPARE(v.maxLength, (float)80.0);
    QCOMPARE(v.alignment, Latte::Types::Justify);

    // the systray applet under this view is reported as a subcontainment
    QCOMPARE(v.subcontainments.rowCount(), 1);
    QCOMPARE(v.subcontainments[(uint)0].id, QStringLiteral("99"));
}

void StorageTest::deserializeViewDefaultsForUnsetKeys()
{
    // a bare Latte containment with nothing but the plugin key: view() must
    // hand back the documented default for every field, not garbage
    KConfig cfg(m_dir.filePath(QStringLiteral("defaults.latte")));
    KConfigGroup g = cfg.group(QStringLiteral("Containments")).group(QStringLiteral("21"));
    g.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));

    Latte::Data::View v = Storage::self()->view(g);

    QVERIFY(v.isValid());
    QCOMPARE(v.name, QString());
    QCOMPARE(v.onPrimary, true);
    QCOMPARE(v.screen, Storage::IDNULL);
    QCOMPARE(v.isClonedFrom, Latte::Data::View::ISCLONEDNULL);
    QCOMPARE(v.linkPlacement, Latte::Data::View::LinkPlacement::ScreenGroupDerived);
    QCOMPARE(v.screenEdgeMargin, -1);
    QCOMPARE(v.screensGroup, Latte::Types::SingleScreenGroup);
    QCOMPARE(v.edge, Plasma::Types::BottomEdge);
    QCOMPARE(v.maxLength, (float)100.0);
    QCOMPARE(v.alignment, Latte::Types::Center);
    QCOMPARE(v.subcontainments.rowCount(), 0);
}

void StorageTest::refuseMalformedLinkedPlacement()
{
    KConfig cfg(m_dir.filePath(QStringLiteral("malformed-linked-placement.latte")));

    KConfigGroup invalidEnum = cfg.group(QStringLiteral("Containments")).group(QStringLiteral("31"));
    invalidEnum.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
    invalidEnum.writeEntry(QStringLiteral("isClonedFrom"), 1);
    invalidEnum.writeEntry(QStringLiteral("linkPlacement"), 99);
    QVERIFY(!Storage::self()->view(invalidEnum).isValid());

    KConfigGroup missingRoot = cfg.group(QStringLiteral("Containments")).group(QStringLiteral("32"));
    missingRoot.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
    missingRoot.writeEntry(QStringLiteral("isClonedFrom"), Latte::Data::View::ISCLONEDNULL);
    missingRoot.writeEntry(
        QStringLiteral("linkPlacement"),
        static_cast<int>(Latte::Data::View::LinkPlacement::ExplicitTarget));
    QVERIFY(!Storage::self()->view(missingRoot).isValid());
}

void StorageTest::refuseViewForNonLatteContainment()
{
    KConfig cfg(m_dir.filePath(QStringLiteral("nonlatte.latte")));
    KConfigGroup g = cfg.group(QStringLiteral("Containments")).group(QStringLiteral("5"));
    g.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.desktopcontainment"));
    g.writeEntry(QStringLiteral("name"), QStringLiteral("desktop"));

    Latte::Data::View v = Storage::self()->view(g);
    QVERIFY(!v.isValid());
    QVERIFY(v.name.isEmpty());
}

void StorageTest::roundTripViewThroughKConfig()
{
    const QString path = m_dir.filePath(QStringLiteral("update.latte"));
    {
        KSharedConfigPtr ptr = KSharedConfig::openConfig(path);
        KConfigGroup g = KConfigGroup(ptr, QStringLiteral("Containments")).group(QStringLiteral("7"));
        g.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
        g.sync();

        Latte::Data::View nv;
        nv.name = QStringLiteral("Written");
        nv.screensGroup = Latte::Types::AllScreensGroup;
        nv.onPrimary = false;
        nv.isClonedFrom = 4;
        nv.linkPlacement = Latte::Data::View::LinkPlacement::ExplicitTarget;
        nv.screen = 13;
        nv.screenEdgeMargin = 9;
        nv.edge = Plasma::Types::TopEdge;
        nv.maxLength = (float)55.0;
        nv.alignment = Latte::Types::Justify;
        Storage::self()->updateView(g, nv);
    }

    // read back from a fresh handle: a real on-disk round trip through view()
    KConfig fresh(path);
    KConfigGroup g = fresh.group(QStringLiteral("Containments")).group(QStringLiteral("7"));
    Latte::Data::View r = Storage::self()->view(g);

    QCOMPARE(r.name, QStringLiteral("Written"));
    QCOMPARE(r.screensGroup, Latte::Types::AllScreensGroup);
    QCOMPARE(r.onPrimary, false);
    QCOMPARE(r.isClonedFrom, 4);
    QCOMPARE(r.linkPlacement, Latte::Data::View::LinkPlacement::ExplicitTarget);
    QCOMPARE(r.screen, 13);
    QCOMPARE(r.screenEdgeMargin, 9);
    QCOMPARE(r.edge, Plasma::Types::TopEdge);
    QCOMPARE(r.maxLength, (float)55.0);
    QCOMPARE(r.alignment, Latte::Types::Justify);

    // maxLength must serialize under [General] where view() reads it; a
    // containment-level write is the dead key the fix right before this test
    // retired (upstream inheritance, fork parallel b48903ec)
    QCOMPARE(g.readEntry(QStringLiteral("maxLength"), (float)-1.0), (float)-1.0);
    QCOMPARE(g.group(QStringLiteral("General")).readEntry(QStringLiteral("maxLength"), (float)-1.0), (float)55.0);
}

void StorageTest::refuseUpdateViewOnNonLatteGroup()
{
    // updateView() must not scribble view keys over a foreign containment
    const QString path = m_dir.filePath(QStringLiteral("foreign.latte"));
    KSharedConfigPtr ptr = KSharedConfig::openConfig(path);
    KConfigGroup g = KConfigGroup(ptr, QStringLiteral("Containments")).group(QStringLiteral("8"));
    g.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.desktopcontainment"));
    g.sync();

    Latte::Data::View nv;
    nv.name = QStringLiteral("MustNotLand");
    Storage::self()->updateView(g, nv);

    KConfig fresh(path);
    KConfigGroup freshGroup = fresh.group(QStringLiteral("Containments")).group(QStringLiteral("8"));
    QVERIFY(!freshGroup.hasKey(QStringLiteral("name")));
}

void StorageTest::enumerateOnlyLatteContainmentsAsViews()
{
    const QString path = writeLayoutFixture(QStringLiteral("viewsfile.latte"));
    Latte::Data::ViewsTable table = Storage::self()->views(path);

    // only containment 1 is org.kde.latte.containment; 5 and 99 are not views
    QCOMPARE(table.rowCount(), 1);
    QVERIFY(table.containsId(QStringLiteral("1")));
    QVERIFY(!table.containsId(QStringLiteral("5")));
    QVERIFY(!table.containsId(QStringLiteral("99")));
    QCOMPARE(table[(uint)0].name, QStringLiteral("My Dock"));

    // 99 is reachable only as view 1's subcontainment, not as its own view
    QVERIFY(table.hasContainmentId(QStringLiteral("1")));
    QVERIFY(table.hasContainmentId(QStringLiteral("99")));
    QVERIFY(!table.hasContainmentId(QStringLiteral("5")));
}

void StorageTest::listSubcontainmentsOfContainmentGroup()
{
    const QString path = writeLayoutFixture(QStringLiteral("subsfromgroup.latte"));
    KSharedConfigPtr ptr = KSharedConfig::openConfig(path);
    KConfigGroup conts(ptr, QStringLiteral("Containments"));

    Latte::Data::GenericTable<Latte::Data::Generic> subs =
        Storage::self()->subcontainments(conts.group(QStringLiteral("1")));
    QCOMPARE(subs.rowCount(), 1);
    QCOMPARE(subs[(uint)0].id, QStringLiteral("99"));

    // a non-Latte containment group yields no subcontainments
    QCOMPARE(Storage::self()->subcontainments(conts.group(QStringLiteral("5"))).rowCount(), 0);
}

void StorageTest::reportContainsViewOnlyForLatteIds()
{
    const QString path = writeLayoutFixture(QStringLiteral("contains.latte"));

    QVERIFY(Storage::self()->containsView(path, 1));
    QVERIFY(!Storage::self()->containsView(path, 5));    // exists but not Latte
    QVERIFY(!Storage::self()->containsView(path, 99));   // exists but not Latte
    QVERIFY(!Storage::self()->containsView(path, 1234)); // missing
}

void StorageTest::enumerateViewsOfInactiveLayout()
{
    const QString path = writeLayoutFixture(QStringLiteral("viewslayout.latte"));
    Latte::CentralLayout layout(nullptr, path, QStringLiteral("viewslayout"));
    QVERIFY(!layout.isActive());

    Latte::Data::ViewsTable table = Storage::self()->views(&layout);
    QCOMPARE(table.rowCount(), 1);
    QVERIFY(table.containsId(QStringLiteral("1")));
}

void StorageTest::detectClonedViewsOnlyForLatteContainments()
{
    KConfig cfg(m_dir.filePath(QStringLiteral("clones.latte")));
    KConfigGroup conts = cfg.group(QStringLiteral("Containments"));

    KConfigGroup cloned = conts.group(QStringLiteral("10"));
    cloned.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
    cloned.writeEntry(QStringLiteral("isClonedFrom"), 3);
    QVERIFY(Storage::self()->isClonedView(cloned));

    // default isClonedFrom == ISCLONEDNULL: not a clone
    KConfigGroup notcloned = conts.group(QStringLiteral("11"));
    notcloned.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
    QVERIFY(!Storage::self()->isClonedView(notcloned));

    // a non-Latte containment is never a cloned view even with isClonedFrom set
    KConfigGroup nonlatte = conts.group(QStringLiteral("12"));
    nonlatte.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.desktopcontainment"));
    nonlatte.writeEntry(QStringLiteral("isClonedFrom"), 3);
    QVERIFY(!Storage::self()->isClonedView(nonlatte));
}

void StorageTest::removeScreenGroupDerivedViewsKeepsPersistentRelationships()
{
    const QString path = m_dir.filePath(QStringLiteral("clonesremove.latte"));
    {
        KSharedConfigPtr ptr = KSharedConfig::openConfig(path);
        KConfigGroup conts(ptr, QStringLiteral("Containments"));
        KConfigGroup c1 = conts.group(QStringLiteral("1"));
        c1.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
        KConfigGroup c10 = conts.group(QStringLiteral("10"));
        c10.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
        c10.writeEntry(QStringLiteral("isClonedFrom"), 1);
        KConfigGroup c11 = conts.group(QStringLiteral("11"));
        c11.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
        c11.writeEntry(QStringLiteral("isClonedFrom"), 1);
        c11.writeEntry(
            QStringLiteral("linkPlacement"),
            static_cast<int>(Latte::Data::View::LinkPlacement::ExplicitTarget));
        ptr->sync();
    }

    Storage::self()->removeScreenGroupDerivedViews(path);

    KConfig fresh(path);
    KConfigGroup conts = fresh.group(QStringLiteral("Containments"));
    QVERIFY(conts.hasGroup(QStringLiteral("1")));    // relationship root kept
    QVERIFY(!conts.hasGroup(QStringLiteral("10"))); // derived replica removed
    QVERIFY(conts.hasGroup(QStringLiteral("11")));  // explicit linked member kept
}

void StorageTest::removeContainmentDeletesExactlyThatGroup()
{
    const QString path = writeLayoutFixture(QStringLiteral("removecont.latte"));

    Storage::self()->removeContainment(path, QStringLiteral("5"));

    KConfig fresh(path);
    KConfigGroup conts = fresh.group(QStringLiteral("Containments"));
    QVERIFY(!conts.hasGroup(QStringLiteral("5")));
    QVERIFY(conts.hasGroup(QStringLiteral("1")));

    // an empty id and a missing id are no-ops that must not touch the file
    Storage::self()->removeContainment(path, QString());
    Storage::self()->removeContainment(path, QStringLiteral("777"));
    KConfig after(path);
    QVERIFY(after.group(QStringLiteral("Containments")).hasGroup(QStringLiteral("1")));
    QVERIFY(after.group(QStringLiteral("Containments")).hasGroup(QStringLiteral("99")));
}

void StorageTest::removeViewDropsViewAndItsSubcontainments()
{
    const QString path = writeLayoutFixture(QStringLiteral("removeview.latte"));

    KSharedConfigPtr ptr = KSharedConfig::openConfig(path);
    KConfigGroup c1 = KConfigGroup(ptr, QStringLiteral("Containments")).group(QStringLiteral("1"));
    Latte::Data::View v = Storage::self()->view(c1);
    QCOMPARE(v.subcontainments.rowCount(), 1);

    Storage::self()->removeView(path, v);

    KConfig fresh(path);
    KConfigGroup conts = fresh.group(QStringLiteral("Containments"));
    QVERIFY(!conts.hasGroup(QStringLiteral("1")));
    QVERIFY(!conts.hasGroup(QStringLiteral("99")));
    QVERIFY(conts.hasGroup(QStringLiteral("5"))); // untouched

    // an invalid view is a no-op
    Latte::Data::View invalid;
    Storage::self()->removeView(path, invalid);
    QVERIFY(KConfig(path).group(QStringLiteral("Containments")).hasGroup(QStringLiteral("5")));
}

void StorageTest::resolveNoScreenIdWithoutCorona()
{
    // the Corona overload short-circuits to NOSCREENID when corona is null,
    // regardless of the view payload
    Latte::Data::View v;
    v.setState(Latte::Data::View::IsCreated);
    v.onPrimary = true;
    QCOMPARE(Storage::self()->expectedViewScreenId(static_cast<Latte::Corona *>(nullptr), v),
             Latte::ScreenPool::NOSCREENID);
}

void StorageTest::reportNoErrorsOrWarningsForCleanLayout()
{
    // a well-formed layout: unique applet ids and every non-Latte containment
    // reachable as some view's subcontainment - neither scanner may cry wolf.
    // The shared fixture does not qualify: its foreign desktop containment
    // (id 5) hangs off no view, which IS an orphan to the warnings scanner.
    const QString path = m_dir.filePath(QStringLiteral("clean.latte"));
    {
        KSharedConfigPtr ptr = KSharedConfig::openConfig(path);
        KConfigGroup conts(ptr, QStringLiteral("Containments"));
        KConfigGroup c1 = conts.group(QStringLiteral("1"));
        c1.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
        KConfigGroup applets = c1.group(QStringLiteral("Applets"));
        applets.group(QStringLiteral("2")).writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.plasmoid"));
        KConfigGroup a3 = applets.group(QStringLiteral("3"));
        a3.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.plasma.private.systemtray"));
        a3.group(QStringLiteral("Configuration")).writeEntry(QStringLiteral("SystrayContainmentId"), 99);
        conts.group(QStringLiteral("99")).writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.plasma.private.systemtray"));
        ptr->sync();
    }
    Latte::CentralLayout layout(nullptr, path, QStringLiteral("clean"));
    QVERIFY(!layout.isActive());

    QVERIFY(Storage::self()->errors(&layout).isEmpty());
    QVERIFY(Storage::self()->warnings(&layout).isEmpty());
}

void StorageTest::reportDuplicateAppletIdsAsError()
{
    // two Latte containments each carry applet id "7" -> APPLETSWITHSAMEID
    const QString path = m_dir.filePath(QStringLiteral("dupapplets.latte"));
    {
        KSharedConfigPtr ptr = KSharedConfig::openConfig(path);
        KConfigGroup conts(ptr, QStringLiteral("Containments"));
        for (const QString &cid : {QStringLiteral("1"), QStringLiteral("2")}) {
            KConfigGroup c = conts.group(cid);
            c.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
            KConfigGroup a = c.group(QStringLiteral("Applets")).group(QStringLiteral("7"));
            a.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.plasmoid"));
        }
        ptr->sync();
    }

    Latte::CentralLayout layout(nullptr, path, QStringLiteral("dupapplets"));
    QVERIFY(!layout.isActive());

    const Latte::Data::ErrorsList errs = Storage::self()->errors(&layout);
    QCOMPARE(errs.count(), 1);
    QCOMPARE(errs[0].id, QString(QLatin1String(Latte::Data::Error::APPLETSWITHSAMEID)));
    QCOMPARE(errs[0].information.rowCount(), 2); // both occurrences of "7"
}

void StorageTest::reportOrphanedSubcontainmentAsWarning()
{
    // a non-Latte containment reachable from no view -> ORPHANEDSUBCONTAINMENT
    const QString path = m_dir.filePath(QStringLiteral("orphansub.latte"));
    {
        KSharedConfigPtr ptr = KSharedConfig::openConfig(path);
        KConfigGroup conts(ptr, QStringLiteral("Containments"));

        KConfigGroup c1 = conts.group(QStringLiteral("1"));
        c1.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));

        KConfigGroup c5 = conts.group(QStringLiteral("5"));
        c5.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.plasma.private.systemtray"));
        ptr->sync();
    }

    Latte::CentralLayout layout(nullptr, path, QStringLiteral("orphansub"));
    QVERIFY(!layout.isActive());

    const Latte::Data::WarningsList warns = Storage::self()->warnings(&layout);
    QCOMPARE(warns.count(), 1);
    QCOMPARE(warns[0].id, QString(QLatin1String(Latte::Data::Warning::ORPHANEDSUBCONTAINMENT)));
}

void StorageTest::stripUnapprovedAppletsFromExportedTemplate()
{
    // both applets carry a [Configuration] subgroup so the strip is
    // observable: exportTemplate deletes the config subgroups of unapproved,
    // non-subcontainment applets and leaves the approved applet intact.
    // (a dedicated non-clone fixture: exportTemplate's derived-view cleanup
    // pass would drop the shared fixture's legacy derived containment)
    const QString origin = m_dir.filePath(QStringLiteral("exportsrc.latte"));
    {
        KSharedConfigPtr ptr = KSharedConfig::openConfig(origin);
        KConfigGroup conts(ptr, QStringLiteral("Containments"));
        KConfigGroup c1 = conts.group(QStringLiteral("1"));
        c1.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));
        c1.writeEntry(QStringLiteral("layoutId"), QStringLiteral("SomeLayout"));
        c1.writeEntry(QStringLiteral("isPreferredForShortcuts"), true);
        KConfigGroup applets = c1.group(QStringLiteral("Applets"));

        KConfigGroup a2 = applets.group(QStringLiteral("2"));
        a2.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.plasmoid"));
        a2.group(QStringLiteral("Configuration")).writeEntry(QStringLiteral("keep"), QStringLiteral("yes"));

        KConfigGroup a3 = applets.group(QStringLiteral("3"));
        a3.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.plasma.private.systemtray"));
        a3.group(QStringLiteral("Configuration")).writeEntry(QStringLiteral("gone"), QStringLiteral("soon"));
        ptr->sync();
    }
    const QString dest = m_dir.filePath(QStringLiteral("exported.latte"));

    // approve only the plasmoid (applet "2"); the systray applet "3" is not
    Latte::Data::AppletsTable approved;
    Latte::Data::Applet a;
    a.id = QStringLiteral("org.kde.latte.plasmoid");
    approved << a;

    QVERIFY(Storage::self()->exportTemplate(origin, dest, approved));
    QVERIFY(QFile(dest).exists());

    KConfig cfg(dest);
    KConfigGroup c1 = cfg.group(QStringLiteral("Containments")).group(QStringLiteral("1"));
    KConfigGroup exportedApplets = c1.group(QStringLiteral("Applets"));

    // unapproved applet 3: its configuration subgroup is stripped, but the
    // applet's own plugin key survives (the strip removes config, not applets)
    QVERIFY(exportedApplets.group(QStringLiteral("3")).groupList().isEmpty());
    QCOMPARE(exportedApplets.group(QStringLiteral("3")).readEntry(QStringLiteral("plugin"), QString()),
             QStringLiteral("org.kde.plasma.private.systemtray"));

    // approved applet 2: configuration and plugin survive untouched
    QCOMPARE(exportedApplets.group(QStringLiteral("2")).group(QStringLiteral("Configuration")).readEntry(QStringLiteral("keep"), QString()),
             QStringLiteral("yes"));

    // per-layout identity is cleared on every exported containment
    QCOMPARE(c1.readEntry(QStringLiteral("layoutId"), QStringLiteral("x")), QString());
    QCOMPARE(c1.readEntry(QStringLiteral("isPreferredForShortcuts"), true), false);
}

void StorageTest::clearLayoutSettingsInExportedTemplate()
{
    // an exported template must not leak the origin's activity assignment or
    // shortcut preference (clearExportedLayoutSettings)
    const QString origin = m_dir.filePath(QStringLiteral("settingssrc.latte"));
    {
        KSharedConfigPtr ptr = KSharedConfig::openConfig(origin);
        KConfigGroup c1 = KConfigGroup(ptr, QStringLiteral("Containments")).group(QStringLiteral("1"));
        c1.writeEntry(QStringLiteral("plugin"), QStringLiteral("org.kde.latte.containment"));

        KConfigGroup settings(ptr, QStringLiteral("LayoutSettings"));
        settings.writeEntry(QStringLiteral("preferredForShortcutsTouched"), true);
        settings.writeEntry(QStringLiteral("lastUsedActivity"), QStringLiteral("someactivity"));
        settings.writeEntry(QStringLiteral("activities"), QStringList{QStringLiteral("a1"), QStringLiteral("a2")});
        ptr->sync();
    }
    const QString dest = m_dir.filePath(QStringLiteral("settingsexported.latte"));

    QVERIFY(Storage::self()->exportTemplate(origin, dest, Latte::Data::AppletsTable()));

    KConfig cfg(dest);
    KConfigGroup settings = cfg.group(QStringLiteral("LayoutSettings"));
    QCOMPARE(settings.readEntry(QStringLiteral("preferredForShortcutsTouched"), true), false);
    QCOMPARE(settings.readEntry(QStringLiteral("lastUsedActivity"), QStringLiteral("x")), QString());
    QCOMPARE(settings.readEntry(QStringLiteral("activities"), QStringList{QStringLiteral("x")}), QStringList());
}

void StorageTest::fallBackToPluginIdForUnknownApplet()
{
    // an unknown plugin id yields data named after the id itself - the
    // "not installed here" marker the settings dialogs display
    Latte::Data::Applet data = Storage::self()->metadata(QStringLiteral("org.kde.nonexistent.applet.xyz"));
    QCOMPARE(data.id, QStringLiteral("org.kde.nonexistent.applet.xyz"));
    QCOMPARE(data.name, QStringLiteral("org.kde.nonexistent.applet.xyz"));
}

void StorageTest::gatherAppletPluginsFilteredByContainmentId()
{
    const QString path = writeLayoutFixture(QStringLiteral("pluginsfile.latte"));

    // containment 1 (and its subcontainment 99): plasmoid + systray, but NOT
    // the kickoff applet living in the foreign containment 5
    Latte::Data::AppletsTable scoped = Storage::self()->plugins(path, 1);
    QCOMPARE(scoped.rowCount(), 2);
    QVERIFY(scoped.containsId(QStringLiteral("org.kde.latte.plasmoid")));
    QVERIFY(scoped.containsId(QStringLiteral("org.kde.plasma.private.systemtray")));
    QVERIFY(!scoped.containsId(QStringLiteral("org.kde.plasma.kickoff")));

    // IDNULL means all containments: kickoff joins the table exactly once
    Latte::Data::AppletsTable all = Storage::self()->plugins(path, Storage::IDNULL);
    QCOMPARE(all.rowCount(), 3);
    QVERIFY(all.containsId(QStringLiteral("org.kde.plasma.kickoff")));
}

void StorageTest::remapIdsWhenAddingTemplateViewToInactiveLayout()
{
    // the destination inactive layout already owns id "1"; the origin
    // template also uses "1". newView() must remap the incoming ids and
    // import the result, so the returned view carries a fresh id and the
    // destination ends with both views present.
    const QString destPath = writeLayoutFixture(QStringLiteral("dest.latte"));
    Latte::CentralLayout dest(nullptr, destPath, QStringLiteral("dest"));
    QVERIFY(!dest.isActive());

    const QString originPath = writeLayoutFixture(QStringLiteral("origin.latte"));

    Latte::Data::View nextViewData;
    nextViewData.setState(Latte::Data::View::IsCreated, originPath);

    Latte::Data::View added = Storage::self()->newView(&dest, nextViewData);
    QVERIFY(added.isValid());
    QVERIFY(!added.id.isEmpty());
    QVERIFY(added.id != QStringLiteral("1")); // remapped away from the collision

    Latte::Data::ViewsTable table = Storage::self()->views(destPath);
    QCOMPARE(table.rowCount(), 2);
    QVERIFY(table.containsId(QStringLiteral("1")));
    QVERIFY(table.containsId(added.id));
}

void StorageTest::writeStoredViewOfInactiveLayoutToTempFile()
{
    const QString path = writeLayoutFixture(QStringLiteral("storedview.latte"));
    Latte::CentralLayout layout(nullptr, path, QStringLiteral("storedview"));
    QVERIFY(!layout.isActive());

    const QString stored = Storage::self()->storedView(&layout, 1);
    QVERIFY(!stored.isEmpty());
    QVERIFY(QFile(stored).exists());

    // the stored file carries the view containment and its subcontainment
    KConfig cfg(stored);
    KConfigGroup conts = cfg.group(QStringLiteral("Containments"));
    QVERIFY(conts.hasGroup(QStringLiteral("1")));
    QVERIFY(conts.hasGroup(QStringLiteral("99")));

    // a non-existent containment id yields an empty path
    QVERIFY(Storage::self()->storedView(&layout, 4242).isEmpty());
}

int main(int argc, char *argv[])
{
    // Point the XDG homes at a throwaway dir before QGuiApplication:
    // CentralLayout/Storage only touch explicit file paths, but metadata()
    // walks KPackage lookups and nothing here may read or write the real
    // desktop's config (same pattern as screenpooltest).
    static QTemporaryDir xdgHome;
    qputenv("XDG_CONFIG_HOME", (xdgHome.path() + QStringLiteral("/config")).toUtf8());
    qputenv("XDG_DATA_HOME", (xdgHome.path() + QStringLiteral("/data")).toUtf8());

    QGuiApplication app(argc, argv);
    StorageTest tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "storagetest.moc"
