/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// EX-07 StorageIdRemapper (docs/QML_EXTRACTION_PLAN.md): the id-assignment
// math of layout import/duplicate.
//
// Case provenance (per docs/TESTING.md): the eleven core slots are ported
// from latte-dock-qt6's storageidremappertest.cpp at 81384003, github.com/CaptSilver/latte-dock-qt6 (their
// 73f64383 extraction of the same Qt5-inherited algorithm; our body was
// diffed against Qt5 f0ad7b23 and is identical, so their case analysis
// transfers). The exhaustion-flag slot is ours: the step-2.5
// loud-exhaustion divergence surfaces exhaustion as IdRemap.exhausted
// instead of leaving "" ids for the caller to discover.

#include <QtTest>

#include "../../app/layouts/storageidremapper.h"

using namespace Latte::Layouts::StorageIdRemapper;
using Latte::Layouts::StorageIdRemapper::IdRemap;

#define QL(x) QStringLiteral(x)

class StorageIdRemapperTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void availableId_findsFirstGapAtOrAboveBase();
    void availableId_exhaustionReturnsEmpty();
    void remap_keepsHighFreeIdsUnchanged();
    void remap_reassignsLowIds();
    void remap_reassignsCollidingUsedId();
    void remap_containmentsBeforeApplets_distinctRanges();
    void remap_appletAllocationSkipsContainmentAssignments();
    void remap_twoCycleCheckDoesNotCorruptNonCycleCase();
    void remap_emptyIdGetsFreshId();
    void mapped_passthroughForUnknownKey();
    void remap_orderKeyBehavior_unknownTokenMapsToEmpty();
    void exhaustion_setsTheLoudFlag();
};

void StorageIdRemapperTest::availableId_findsFirstGapAtOrAboveBase()
{
    // ({"12","13"},{},12) -> "14": 12 taken, 13 taken, 14 is first gap
    QCOMPARE(availableId({QL("12"), QL("13")}, {}, 12), QL("14"));
    // ({},{},12) -> "12": nothing taken, base is free
    QCOMPARE(availableId({}, {}, 12), QL("12"));
    // ({"40"},{"41"},40) -> "42": 40 in all, 41 in assigned, 42 free
    QCOMPARE(availableId({QL("40")}, {QL("41")}, 40), QL("42"));
    // ({"5"},{},12) -> "12": 5 is in all but below base; base=12 is free
    QCOMPARE(availableId({QL("5")}, {}, 12), QL("12"));
}

void StorageIdRemapperTest::availableId_exhaustionReturnsEmpty()
{
    // only the slot at base=31999 is taken; 32000 is the exclusive cap
    QCOMPARE(availableId({QL("31999")}, {}, 31999), QL(""));
}

void StorageIdRemapperTest::remap_keepsHighFreeIdsUnchanged()
{
    // usedIds empty -> no collisions; containment 12 and applet 40 keep
    // themselves
    IdRemap r = remap({{}, {QL("12")}, {QL("40")}});
    QCOMPARE(r.assigned.value(QL("12")), QL("12"));
    QCOMPARE(r.assigned.value(QL("40")), QL("40"));
    QVERIFY(!r.exhausted);
}

void StorageIdRemapperTest::remap_reassignsLowIds()
{
    // containment "1" < 12 -> reassigned to 12 (lowest free containment
    // slot); applet "5" < 40 -> reassigned to 40
    IdRemap r = remap({{}, {QL("1")}, {QL("5")}});
    QCOMPARE(r.assigned.value(QL("1")), QL("12"));
    QCOMPARE(r.assigned.value(QL("5")), QL("40"));
}

void StorageIdRemapperTest::remap_reassignsCollidingUsedId()
{
    // the destination already has id "12"; importing a containment also
    // numbered "12" must receive the next free id
    IdRemap r = remap({{QL("12")}, {QL("12")}, {}});
    QCOMPARE(r.assigned.value(QL("12")), QL("13"));
}

void StorageIdRemapperTest::remap_containmentsBeforeApplets_distinctRanges()
{
    // low-numbered ids all get fresh ids from their respective bases:
    // containments "1"->12, "2"->13; applets "3"->40, "4"->41
    IdRemap r = remap({{}, {QL("1"), QL("2")}, {QL("3"), QL("4")}});
    QCOMPARE(r.assigned.value(QL("1")), QL("12"));
    QCOMPARE(r.assigned.value(QL("2")), QL("13"));
    QCOMPARE(r.assigned.value(QL("3")), QL("40"));
    QCOMPARE(r.assigned.value(QL("4")), QL("41"));
}

void StorageIdRemapperTest::remap_appletAllocationSkipsContainmentAssignments()
{
    // containment "50" keeps id "50" (running assigned set = ["50"]).
    // applet "50": 50 >= 40 and not used, but "50" IS in the running set
    // -> allocated availableId({}, ["50"], 40) = "40". The applet pass
    // overwrites the shared "50" key, so assigned["50"] = "40".
    IdRemap r = remap({{}, {QL("50")}, {QL("50")}});
    QCOMPARE(r.assigned.value(QL("50")), QL("40"));
}

void StorageIdRemapperTest::remap_twoCycleCheckDoesNotCorruptNonCycleCase()
{
    // The "PROBLEM APPEARED" 2-cycle fix fires when
    // assigned[assigned[X]] == X for X != assigned[X]. capt's exhaustive
    // analysis (81384003) shows natural inputs cannot trigger it with
    // this algorithm - any id forced to remap is either below base
    // (unavailable as a return value) or in usedIds (excluded from
    // availableId returns) - so the fix is defensive; this pins that it
    // does NOT corrupt a near-cycle (X->Y where Y is also a key but
    // assigned[Y] != X):
    // usedIds={"12"}, containments {"12","13"}:
    //   "12" is used -> "13"; "13" is in the running set -> "14";
    //   cycle check on "12": assigned["13"]="14" != "12" -> untouched
    IdRemap r = remap({{QL("12")}, {QL("12"), QL("13")}, {}});
    QCOMPARE(r.assigned.value(QL("12")), QL("13"));
    QCOMPARE(r.assigned.value(QL("13")), QL("14"));
}

void StorageIdRemapperTest::remap_emptyIdGetsFreshId()
{
    // "" -> toInt() == 0 < 12 -> fresh id from the containment base
    IdRemap r = remap({{}, {QL("")}, {}});
    QCOMPARE(r.assigned.value(QL("")), QL("12"));
}

void StorageIdRemapperTest::mapped_passthroughForUnknownKey()
{
    IdRemap r;
    QCOMPARE(r.mapped(QL("99")), QL("99"));
}

void StorageIdRemapperTest::remap_orderKeyBehavior_unknownTokenMapsToEmpty()
{
    // the shipped order-key rewrite did `assigned[token]` (operator[]),
    // which yields "" for unknown tokens; the cutover uses
    // assigned.value(token) to stay output-equivalent without the
    // side-effecting insert - unknown token -> ""
    IdRemap r = remap({{}, {QL("12")}, {QL("40")}});
    QVERIFY(!r.assigned.contains(QL("99")));
    QCOMPARE(r.assigned.value(QL("99")), QString());
}

void StorageIdRemapperTest::exhaustion_setsTheLoudFlag()
{
    // drive one allocation pass at the edge of the id space: the only
    // free slot below the cap is taken, so the assignment comes back
    // empty and the remap must flag exhaustion for the caller to abort
    // loudly (storage.cpp refuses to write "" group names)
    IdRemap r;
    QStringList running;
    assignIds({QL("7")}, maxId - 1, {QString::number(maxId - 1)}, running, r);
    QVERIFY(r.exhausted);
    QCOMPARE(r.assigned.value(QL("7")), QString(""));
}

QTEST_GUILESS_MAIN(StorageIdRemapperTest)

#include "storageidremappertest.moc"
