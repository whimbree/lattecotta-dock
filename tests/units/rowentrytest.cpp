/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Pins the shared RowEntry type-family contract (rowentry.h) that both
//! EX-06 VisibleIndexEngine and the planned EX-14 DropEventClassifier
//! build on: the default entry shape, index-keyed (not position-keyed)
//! lookup, and the Qt5-era visible-item skip rule.

#include "../../declarativeimports/core/units/rowentry.h"

#include <QtTest>

#include <type_traits>

//! consumers pass rows by value into wrappers and tests; a RowEntry must
//! stay a plain value aggregate - losing this silently would make the
//! collector marshalling in the QML wrappers copy-unsafe
static_assert(std::is_aggregate_v<Latte::RowEntry>);
static_assert(std::is_trivially_copyable_v<Latte::RowEntry>);

using namespace Latte;

class RowEntryTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void defaultEntry_isUnindexedPlainItem();
    void entryAt_findsByIndexField_notByPosition();
    void entryAt_missingIndex_isLegitimateAbsence();
    void entryAt_emptyRow_isAbsent();
    void isVisibleItem_skipRuleTruthTable();
};

void RowEntryTest::defaultEntry_isUnindexedPlainItem()
{
    //! the documented default: mirrors the QML items' "property int
    //! index: -1" not-yet-indexed state, with plain-item flags
    const RowEntry entry;
    QCOMPARE(entry.index, -1);
    QCOMPARE(entry.isSeparator, false);
    QCOMPARE(entry.isHidden, false);
    QCOMPARE(entry.isMarginsSeparator, false);
    QCOMPARE(entry.subItemCount, 1);
}

void RowEntryTest::entryAt_findsByIndexField_notByPosition()
{
    //! the containment gap shape: startLayout 0..1, endLayout begins high
    const RowEntries row{{.index = 0}, {.index = 1}, {.index = 100, .isSeparator = true}};

    const auto found = entryAt(row, 100);
    QVERIFY(found.has_value());
    QCOMPARE(found->index, 100);
    QCOMPARE(found->isSeparator, true);

    //! list position 2 is not an index key
    QVERIFY(!entryAt(row, 2).has_value());
}

void RowEntryTest::entryAt_missingIndex_isLegitimateAbsence()
{
    const RowEntries row{{.index = 0}, {.index = 2}};

    //! the gap index between layouts and the off-edge neighbor are
    //! absences the walks probe on purpose - absent, not an error
    QVERIFY(!entryAt(row, 1).has_value());
    QVERIFY(!entryAt(row, -1).has_value());
    QVERIFY(!entryAt(row, 3).has_value());
}

void RowEntryTest::entryAt_emptyRow_isAbsent()
{
    QVERIFY(!entryAt(RowEntries{}, 0).has_value());
}

void RowEntryTest::isVisibleItem_skipRuleTruthTable()
{
    //! IndexerPrivate.qml's Qt5-era skip rule: separators, margins-area
    //! separators and hidden items are all invisible for counting
    QCOMPARE(isVisibleItem(RowEntry{.index = 0}), true);
    QCOMPARE(isVisibleItem(RowEntry{.index = 0, .isSeparator = true}), false);
    QCOMPARE(isVisibleItem(RowEntry{.index = 0, .isHidden = true}), false);
    QCOMPARE(isVisibleItem(RowEntry{.index = 0, .isMarginsSeparator = true}), false);
    //! a hidden separator is invisible on both counts
    QCOMPARE(isVisibleItem(RowEntry{.index = 0, .isSeparator = true, .isHidden = true}), false);
    //! an empty multi-item applet is still a visible item slot (it
    //! contributes 0 sub-items, but the skip rule is about kind, not count)
    QCOMPARE(isVisibleItem(RowEntry{.index = 0, .subItemCount = 0}), true);
}

QTEST_GUILESS_MAIN(RowEntryTest)
#include "rowentrytest.moc"
