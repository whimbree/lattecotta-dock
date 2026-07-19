/*
    SPDX-FileCopyrightText: 2019-2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef LAUNCHERLISTOPS_H
#define LAUNCHERLISTOPS_H

// Qt
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

// C++
#include <algorithm>
#include <optional>

namespace Latte {
namespace LauncherLists {

//! Launcher list algebra (EX-11 in docs/tracking/QML_EXTRACTION_PLAN.md): the
//! stored-record grammar, separator name allocation, the order
//! reconciliation planner and the animation registries that lived across
//! abilities/Launchers.qml, abilities/launchers/Validator.qml and
//! TasksExtendedManager.qml. Michail's algorithms; the QML shells keep the
//! live parts (timers, signals, model lookups) and this core owns every
//! decision. Latte-authored, NOT part of the plasma-desktop vendored set
//! in plasmoid/plugin (see units/README.md).

//! ---- stored launcher records ----------------------------------------
//!
//! The stored-list grammar, written by libtaskmanager's launcherList
//! serialization (and by the activitiesTools.js legacy importer):
//! a launcher on all activities is a plain url; a launcher pinned to
//! specific activities is "[id1,id2]\nurl". Qt5's reader
//! (currentStoredLauncherList) never parsed the tag - it substring-scanned
//! the WHOLE raw record for the current activity id, so an id occurring
//! inside the url text would false-match, and a tag without its newline
//! produced a garbage launcher entry. The typed parse makes both
//! unrepresentable: activity matching is list membership, malformed
//! records are refused into `malformed` for the caller to report loudly
//! (the spec's "malformed tails as loud errors").

struct StoredRecord {
    QString url;
    //! empty == on all activities (a plain, untagged record)
    QStringList activities;

    bool operator==(const StoredRecord &) const = default;
};

struct ParsedStoredList {
    QVector<StoredRecord> records;
    //! raw records that do not fit the grammar; never silently dropped -
    //! the caller reports each one
    QStringList malformed;

    bool operator==(const ParsedStoredList &) const = default;
};

inline ParsedStoredList parseStoredRecords(const QStringList &stored)
{
    ParsedStoredList parsed;

    for (const QString &record : stored) {
        const qsizetype open = record.indexOf(QLatin1Char('['));

        if (open == -1) {
            // a plain url; Qt5 pushed it verbatim, including the empty
            // string (an empty config entry stays representable - the
            // model layer is the one that judges urls)
            parsed.records.append(StoredRecord{record, {}});
            continue;
        }

        // tagged record: "[id1,id2]\nurl", tag first, nothing before it.
        // Qt5 mis-read every off-grammar shape silently (a url containing
        // "[" lost its launcher, a tag without newline showed garbage);
        // those are refused here instead.
        const qsizetype close = record.indexOf(QLatin1Char(']'));
        const qsizetype newline = record.indexOf(QLatin1Char('\n'));
        const QStringList activities = (open == 0 && close > open)
                ? record.mid(open + 1, close - open - 1).split(QLatin1Char(','), Qt::SkipEmptyParts)
                : QStringList{};
        const bool wellFormed = open == 0
                && close > open
                && newline == close + 1
                && newline + 1 < record.size()
                && !activities.isEmpty();

        if (!wellFormed) {
            parsed.malformed.append(record);
            continue;
        }

        parsed.records.append(StoredRecord{record.mid(newline + 1), activities});
    }

    return parsed;
}

//! the launchers a dock on `currentActivity` shows, in stored order
//! (order is user-sacred data: no sorting, no dedup)
inline QStringList selectLaunchersForActivity(const QVector<StoredRecord> &records,
                                              const QString &currentActivity)
{
    QStringList launchers;

    for (const StoredRecord &record : records) {
        if (record.activities.isEmpty() || record.activities.contains(currentActivity)) {
            launchers.append(record.url);
        }
    }

    return launchers;
}

//! ---- internal separator names ----------------------------------------

//! Qt5's while(no<20) allocation cap: at most 19 internal separators
inline constexpr int MAXSEPARATORS = 19;

inline QString separatorName(int no)
{
    Q_ASSERT(no >= 1 && no <= MAXSEPARATORS);
    return QStringLiteral("file:///latte-separator") + QString::number(no)
            + QStringLiteral(".desktop");
}

//! Qt5 quirk preserved verbatim: the second test is indexOf(".desktop")
//! !== 1 - a POSITION-ONE check, not the contains-check it was surely
//! meant to be. Practical effect: any url containing "latte-separator"
//! classifies as a separator unless ".desktop" sits at index 1. Faithful
//! because TaskItem branches on it live; pinned by test.
inline bool isSeparatorName(const QString &launcher)
{
    return launcher.contains(QLatin1String("latte-separator"))
            && launcher.indexOf(QLatin1String(".desktop")) != 1;
}

//! candidate names in allocation order (1..19)
inline QStringList separatorCandidates()
{
    QStringList candidates;
    candidates.reserve(MAXSEPARATORS);

    for (int no = 1; no <= MAXSEPARATORS; ++no) {
        candidates.append(separatorName(no));
    }

    return candidates;
}

//! first candidate not yet taken - gaps fill before new numbers grow.
//! nullopt == all 19 taken (Qt5 returned "" silently; the QML-facing
//! wrapper keeps the "" contract but reports the exhaustion)
inline std::optional<QString> freeSeparatorName(const QStringList &takenNames)
{
    for (int no = 1; no <= MAXSEPARATORS; ++no) {
        QString candidate = separatorName(no);

        if (!takenNames.contains(candidate)) {
            return candidate;
        }
    }

    return std::nullopt;
}

//! ---- order reconciliation planning ------------------------------------
//!
//! Replaces Validator.qml's upwardIsBetter direction heuristic and its
//! twin move-one-then-restart loops (the EX-11 spec's redesign). The Qt5
//! heuristic spliced with goal.indexOf(val), which is -1 for an element
//! the goal does not know - a silent degradation the guard inventory
//! flagged; a planner over permutations never needs a direction at all.

struct Move {
    int from = 0;
    int to = 0;

    bool operator==(const Move &) const = default;
};

//! Ordered moves that transform `current` into `goal` when applied
//! left to right (list move semantics: remove at from, insert at to -
//! QList::move and libtaskmanager's TasksModel::move both do this).
//!
//! Empty plan == the lists are already equal. nullopt == not plannable:
//! the lists are not duplicate-free permutations of each other, which
//! during launcher sync is a NORMAL settling state (the model is still
//! churning; the caller retries later), not an error.
//!
//! The planner pulls goal[i] into slot i, left to right; each slot is
//! final once fixed, so applying only the FIRST move and replanning
//! strictly shrinks the plan - the termination guarantee the Qt5 loop
//! lacked, and what the one-move-per-tick QML caller leans on. One move
//! per misplaced slot is minimal for adjacent transpositions; a
//! longest-increasing-subsequence planner would be globally minimal but
//! earns nothing here (launcher lists are short and the caller applies
//! one move per 400ms tick) at real obviousness cost.
inline std::optional<QVector<Move>> planMoves(const QStringList &current, const QStringList &goal)
{
    if (current.size() != goal.size()) {
        return std::nullopt;
    }

    QStringList sortedCurrent = current;
    QStringList sortedGoal = goal;
    std::sort(sortedCurrent.begin(), sortedCurrent.end());
    std::sort(sortedGoal.begin(), sortedGoal.end());

    if (sortedCurrent != sortedGoal) {
        return std::nullopt;
    }

    // duplicate urls cannot happen in a launcher model; a plan over them
    // would be ambiguous, so they are not plannable either (the wrapper
    // additionally reports them - an anomaly, unlike a membership gap)
    if (std::adjacent_find(sortedCurrent.cbegin(), sortedCurrent.cend()) != sortedCurrent.cend()) {
        return std::nullopt;
    }

    QVector<Move> plan;
    QStringList working = current;

    for (qsizetype i = 0; i < goal.size(); ++i) {
        if (working.at(i) == goal.at(i)) {
            continue;
        }

        const qsizetype from = working.indexOf(goal.at(i), i + 1);
        Q_ASSERT(from > i); // guaranteed: permutation, slots < i already final

        working.move(from, i);
        plan.append(Move{int(from), int(i)});
    }

    Q_ASSERT(working == goal);
    return plan;
}

//! ---- animation registries ----------------------------------------------
//!
//! TasksExtendedManager.qml's parallel JS arrays as one tested container
//! per shape. The QML shell keeps the state machine around them (signals,
//! the paused-state count latches, the GC timer); these own membership.

//! The four string registries in one type. Equality is per registry:
//!
//! - SubstringEitherWay (waiting, to-be-added launchers) preserves Qt5's
//!   equals(): either url being a substring of the other, both nonempty.
//!   WHY substring: the same launcher travels in two spellings - the bare
//!   url and the url?iconData=... variant (launcherUrlWithIcon) - and the
//!   animation probes use whichever they hold; substring matching makes
//!   the two spellings one launcher. The nonempty rule means an empty url
//!   never matches anything: an empty record is representable but inert,
//!   exactly the Qt5 ghost the 30s GC swept (the wrapper reports adds of
//!   empty urls, behavior unchanged).
//! - Exact (immediate, to-be-removed launchers): plain equality, the two
//!   spellings are distinct keys (RealRemovalAnimation probes both).
class LauncherSet {
public:
    enum class Equality {
        Exact,
        SubstringEitherWay
    };

    explicit LauncherSet(Equality equality)
        : m_equality(equality)
    {
    }

    //! add-if-absent; true when it actually inserted (the QML count
    //! latches increment only on real inserts)
    bool add(const QString &launcher)
    {
        if (contains(launcher)) {
            return false;
        }

        m_items.append(launcher);
        return true;
    }

    //! removes the first matching record; true when one was removed (the
    //! QML waitingLauncherRemoved signal fires only on real removals)
    bool remove(const QString &launcher)
    {
        for (qsizetype i = 0; i < m_items.size(); ++i) {
            if (matches(m_items.at(i), launcher)) {
                m_items.removeAt(i);
                return true;
            }
        }

        return false;
    }

    bool contains(const QString &launcher) const
    {
        return std::any_of(m_items.cbegin(), m_items.cend(), [&](const QString &item) {
            return matches(item, launcher);
        });
    }

    int count() const
    {
        return int(m_items.size());
    }

    //! insertion order (the GC debug prints)
    QStringList items() const
    {
        return m_items;
    }

    void clear()
    {
        m_items.clear();
    }

private:
    bool matches(const QString &stored, const QString &probe) const
    {
        switch (m_equality) {
        case Equality::Exact:
            return stored == probe;
        case Equality::SubstringEitherWay:
            return !stored.isEmpty() && !probe.isEmpty()
                    && (stored.contains(probe) || probe.contains(stored));
        }

        Q_UNREACHABLE();
    }

    Equality m_equality;
    QStringList m_items;
};

//! launchersToBeMoved: where a just-added launcher must be repositioned.
//! Ends the -1 position sentinel (posOfLauncherToBeMoved) and Qt5's dead
//! mid-loop "safety checker" bailouts (the list was never mutated inside
//! those loops).
class MoveIntentSet {
public:
    struct Intent {
        QString launcher;
        int position = 0;

        bool operator==(const Intent &) const = default;
    };

    //! add-if-absent (a second intent for the same launcher keeps the
    //! first position, as Qt5 did); true when it actually inserted
    bool add(const QString &launcher, int position)
    {
        Q_ASSERT(position >= 0); // the wrapper refuses negatives loudly

        if (contains(launcher)) {
            return false;
        }

        m_intents.append(Intent{launcher, position});
        return true;
    }

    std::optional<int> positionOf(const QString &launcher) const
    {
        for (const Intent &intent : m_intents) {
            if (intent.launcher == launcher) {
                return intent.position;
            }
        }

        return std::nullopt;
    }

    bool contains(const QString &launcher) const
    {
        return positionOf(launcher).has_value();
    }

    bool remove(const QString &launcher)
    {
        for (qsizetype i = 0; i < m_intents.size(); ++i) {
            if (m_intents.at(i).launcher == launcher) {
                m_intents.removeAt(i);
                return true;
            }
        }

        return false;
    }

    int count() const
    {
        return int(m_intents.size());
    }

    QVector<Intent> items() const
    {
        return m_intents;
    }

    void clear()
    {
        m_intents.clear();
    }

private:
    QVector<Intent> m_intents;
};

//! frozenTasks: the parabolic zoom a task carries across its
//! launcher<->startup<->window identity change. Upsert-only writes make
//! duplicate ids unrepresentable (Qt5's removeFrozenTask removed the LAST
//! match, indistinguishable from first-match because setFrozenTask
//! already upserted).
class FrozenZoomSet {
public:
    struct Entry {
        QString id;
        qreal zoom = 0;

        bool operator==(const Entry &) const = default;
    };

    void setZoom(const QString &id, qreal zoom)
    {
        for (Entry &entry : m_entries) {
            if (entry.id == id) {
                entry.zoom = zoom;
                return;
            }
        }

        m_entries.append(Entry{id, zoom});
    }

    std::optional<qreal> zoomOf(const QString &id) const
    {
        for (const Entry &entry : m_entries) {
            if (entry.id == id) {
                return entry.zoom;
            }
        }

        return std::nullopt;
    }

    bool remove(const QString &id)
    {
        for (qsizetype i = 0; i < m_entries.size(); ++i) {
            if (m_entries.at(i).id == id) {
                m_entries.removeAt(i);
                return true;
            }
        }

        return false;
    }

    int count() const
    {
        return int(m_entries.size());
    }

    QVector<Entry> items() const
    {
        return m_entries;
    }

    void clear()
    {
        m_entries.clear();
    }

private:
    QVector<Entry> m_entries;
};

}
}

#endif
