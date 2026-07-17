/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later

*/

#ifndef APPLETDATA_H
#define APPLETDATA_H

//! local
#include "genericdata.h"
#include "generictable.h"

//! Qt
#include <QMetaType>
#include <QString>

namespace Latte {
namespace Data {

class Applet : public Generic
{
public:
    Applet();
    Applet(Applet &&o);
    Applet(const Applet &o);

    //! Layout data
    //! initialized here because the default ctor does not set it while the
    //! copy/move ctors and operator== read it - reading an indeterminate
    //! bool is undefined behavior, not merely a wrong value
    bool isSelected{false};
    QString description;
    QString icon;
    QString storageId;
    QString subcontainmentId;

    bool isInstalled() const;
    bool isValid() const;

    QString visibleName() const;

    //! Operators
    Applet &operator=(const Applet &rhs);
    Applet &operator=(Applet &&rhs);
    bool operator==(const Applet &rhs) const;
    bool operator!=(const Applet &rhs) const;
};

typedef GenericTable<Applet> AppletsTable;

}
}

Q_DECLARE_METATYPE(Latte::Data::Applet)

#endif
