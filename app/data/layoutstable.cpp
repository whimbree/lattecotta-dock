/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "layoutstable.h"

#include <QDebug>

namespace Latte {
namespace Data {

LayoutsTable::LayoutsTable()
    : GenericTable<Layout>()
{
}

LayoutsTable::LayoutsTable(LayoutsTable &&o)
    : GenericTable<Layout>(o)
{

}

LayoutsTable::LayoutsTable(const LayoutsTable &o)
    : GenericTable<Layout>(o)
{

}

//! Operators
LayoutsTable &LayoutsTable::operator=(const LayoutsTable &rhs)
{
    m_list = rhs.m_list;
    return (*this);
}

LayoutsTable &LayoutsTable::operator=(LayoutsTable &&rhs)
{
    m_list = rhs.m_list;
    return (*this);
}

LayoutsTable LayoutsTable::subtracted(const LayoutsTable &rhs) const
{
    LayoutsTable subtract;

    if ((*this) == rhs) {
        return subtract;
    }

    for(int i=0; i<m_list.count(); ++i) {
        if (!rhs.containsId(m_list[i].id)) {
            subtract << m_list[i];
        }
    }

    return subtract;
}

//! Chooses the layout that should inherit the Free-Activities assignment
//! after its holder left the table, or an empty id when no inheritance is
//! needed or possible:
//! - while a Free-Activities or All-Activities holder survives, free
//!   activities are still covered and nothing changes;
//! - else the first enabled layout (non-empty activities) inherits;
//! - else the first layout at all inherits (a disabled survivor becomes the
//!   free-activities layout rather than leaving free activities serving no
//!   layout - the lowest-priority arm of upstream's original
//!   autoAssignFreeActivitiesLayout(), commit 0d39ee6f5);
//! - an empty table has nothing to inherit.
QString LayoutsTable::freeActivitiesInheritorId() const
{
    for(int i=0; i<m_list.count(); ++i) {
        if (m_list[i].activities.contains(Data::Layout::FREEACTIVITIESID)
                || m_list[i].activities.contains(Data::Layout::ALLACTIVITIESID)) {
            return QString();
        }
    }

    for(int i=0; i<m_list.count(); ++i) {
        if (!m_list[i].activities.isEmpty()) {
            return m_list[i].id;
        }
    }

    return m_list.isEmpty() ? QString() : m_list[0].id;
}

void LayoutsTable::setLayoutForFreeActivities(const QString &id)
{
    int row = indexOf(id);


    if (row>=0) {
        m_list[row].activities = QStringList(Data::Layout::FREEACTIVITIESID);
    }
}

}
}
