/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef LATTE_AUDIT_CONFIGSNAPSHOTDIFF_H
#define LATTE_AUDIT_CONFIGSNAPSHOTDIFF_H

// The pure diff core of the edit-mode settings audit's Tier B check
// (docs/tracking/edit-mode-settings-audit-plan.md section 2). It answers the audit's
// three per-control questions over two viewConfigData()/appletConfigData()
// "config" snapshots taken before and after driving a control:
//
//   P1 APPLIES        - driving the control changed at least the key it is for
//   P2 RIGHT KEY ONLY - the EXACT set of keys that changed is the expected set,
//                       so a stray side-effect write (the D15 minLength drag) or
//                       a write to a schema-absent key (the S-a dead keys) fails
//   P3 REFLECTS STATE - a snapshot carries a key at the expected value
//
// It is a pure QJsonObject -> answer core so it can be pinned deterministically
// under ASan+UBSan (configsnapshotdifftest.cpp) and re-used by the QML-handler
// wiring harness (settingswiringharnesstest.cpp) that drives real handlers
// against a stub config map. The live shell harness (tests/e2e/audit/audit-lib.sh)
// mirrors these exact semantics in python over the same snapshots.
//
// DEFAULT-DELETION SAFETY: a key present on EXACTLY ONE side counts as changed
// (a diff), never silently ignored. viewConfigData reads the in-process config
// map, which keeps every schema key (a key returned to its default reads as its
// default value, not "missing"), so this case should not arise in normal use.
// The diff refuses to DEPEND on that: if a snapshot source ever drops a key
// (the KConfig on-disk default-deletion trap), the diff reports it loudly as a
// change (a false-FAIL) instead of swallowing it as "no change" (a false-PASS).
// Loud-and-wrong is debuggable; silent-and-wrong is the defect class the whole
// audit exists to catch.

#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QString>
#include <QStringList>

namespace Latte {
namespace AuditHarness {

//! the sorted, de-duplicated set of keys whose value differs between two
//! config snapshots. A key present on exactly one side is a change (the
//! default-deletion safety note above); a key present on both sides is a
//! change iff its JSON value differs.
inline QStringList changedConfigKeys(const QJsonObject &before, const QJsonObject &after)
{
    QSet<QString> keys;

    for (auto it = before.constBegin(); it != before.constEnd(); ++it) {
        keys.insert(it.key());
    }
    for (auto it = after.constBegin(); it != after.constEnd(); ++it) {
        keys.insert(it.key());
    }

    QStringList changed;

    for (const QString &key : keys) {
        const bool inBefore = before.contains(key);
        const bool inAfter = after.contains(key);

        if (inBefore != inAfter || before.value(key) != after.value(key)) {
            changed << key;
        }
    }

    changed.sort();
    return changed;
}

//! the expected key set, normalized the same way changedConfigKeys() is (sorted
//! and de-duplicated) so equality is order-independent
inline QStringList normalizedKeys(const QStringList &keys)
{
    QStringList normalized = keys;
    normalized.sort();
    normalized.removeDuplicates();
    return normalized;
}

//! P1 APPLIES: driving the control changed the key it is labelled for. A
//! control whose key did NOT change wrote nothing that surface reads - the D10
//! class - and this returns false so the audit fails it.
inline bool controlApplies(const QJsonObject &before, const QJsonObject &after, const QString &key)
{
    return changedConfigKeys(before, after).contains(key);
}

//! P2 RIGHT KEY ONLY: the changed-key set EQUALS the expected set. A stray
//! extra key (a coupled side effect) makes this false; a missing expected key
//! makes this false. This is the decisive test for "the control writes its own
//! key and nothing else".
inline bool onlyExpectedKeysChanged(const QJsonObject &before, const QJsonObject &after, const QStringList &expected)
{
    return changedConfigKeys(before, after) == normalizedKeys(expected);
}

//! P3 REFLECTS STATE: a snapshot carries the key at the expected value. Used
//! for "the control shows the dock's current value on open" and for confirming
//! a value the audit wrote is what the snapshot reads back.
inline bool valueReflects(const QJsonObject &snapshot, const QString &key, const QJsonValue &expected)
{
    return snapshot.contains(key) && snapshot.value(key) == expected;
}

}
}

#endif
