/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef VIEWSTABLEDATA_H
#define VIEWSTABLEDATA_H

// local
#include "generictable.h"
#include "viewdata.h"

// Qt
#include <QList>

namespace Latte {
namespace Data {

class ViewsTable : public GenericTable<View>
{

public:
    ViewsTable();
    ViewsTable(ViewsTable &&o);
    ViewsTable(const ViewsTable &o);

    bool isInitialized{false};

    void print();

    void appendTemporaryView(const Data::View &view);

    bool hasContainmentId(const QString &cid) const;

    //! Explicit members are persistent relationship records. A root cannot be
    //! removed as one Plasma containment transaction while any of these
    //! records remain because Undo would restore only the root.
    [[nodiscard]] bool hasExplicitLinkedMembers(const QString &rootId) const;

    //! Empty means that every linked member names a present direct root.
    //! Persisted chains, cycles, missing roots, and duplicate identities are
    //! rejected before any runtime view is constructed.
    [[nodiscard]] QString relationshipValidationError() const;

    //! Operators
    ViewsTable &operator=(const ViewsTable &rhs);
    ViewsTable &operator=(ViewsTable &&rhs);
    bool operator==(const ViewsTable &rhs) const;
    bool operator!=(const ViewsTable &rhs) const;
    ViewsTable subtracted(const ViewsTable &rhs) const;
    ViewsTable onlyOriginals() const;
};

}
}

Q_DECLARE_METATYPE(Latte::Data::ViewsTable)

#endif
