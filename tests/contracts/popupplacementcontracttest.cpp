/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Upstream contract (tests/contracts/README.md): the task context menu's
//! placement wiring. plasmoid/package/contents/ui/ContextMenu.qml assigns
//! LatteCore.Types.*Posed*Popup integers straight into PlasmaExtras.Menu's
//! `placement` property (QMenuProxy::PopupPlacement in libplasma's
//! plasmaextracomponents plugin). Nothing converts between the two enums -
//! the raw ints cross the QML boundary - so Latte's PopupPlacement values
//! must equal libplasma's, member for member, or every task right-click
//! menu poses on the wrong side of the dock without any error.
//!
//! The pin is read from the PINNED libplasma's plasmaextracomponents
//! qmltypes (located from the imported Plasma::Plasma library at configure
//! time, same technique as alternativescreateapplettest): the qmltypes
//! lists the enum's members in declaration order with implicit 0..N
//! values, and every Latte member must sit at the same index. A libplasma
//! bump that inserts, removes or reorders members fails HERE instead of
//! posing menus wrong in the live dock.
//!
//! Idea from latte-dock-qt6's coretypesenumtest (origin/main), which pins
//! three of the nine values as hardcoded ints; this parses the pinned
//! library's own declaration instead and pins all of them. Their
//! EdgePosition cases are not carried: that enum is their tree's addition
//! and does not exist here.

#include <coretypes.h>

// Qt
#include <QFile>
#include <QMetaEnum>
#include <QRegularExpression>
#include <QTest>

class PopupPlacementContractTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void matchLibplasmaPopupPlacementMemberForMember();

private:
    //! parse the ordered member list of the named enum out of a qmltypes
    //! file (values are implicit by declaration order in that format)
    QStringList readEnumMembersFromQmltypes(const QString &path, const QString &enumName);
};

QStringList PopupPlacementContractTest::readEnumMembersFromQmltypes(const QString &path, const QString &enumName)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringList();
    }
    const QString text = QString::fromUtf8(f.readAll());

    //! Enum { name: "<enumName>" ... values: [ "A", "B", ... ] }
    QRegularExpression block(
        QStringLiteral("Enum\\s*\\{[^{}]*name:\\s*\"%1\"[^{}]*values:\\s*\\[([^\\]]*)\\]").arg(enumName),
        QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch m = block.match(text);
    if (!m.hasMatch()) {
        return QStringList();
    }

    QStringList members;
    QRegularExpression quoted(QStringLiteral("\"([^\"]+)\""));
    QRegularExpressionMatchIterator it = quoted.globalMatch(m.captured(1));
    while (it.hasNext()) {
        members << it.next().captured(1);
    }
    return members;
}

void PopupPlacementContractTest::matchLibplasmaPopupPlacementMemberForMember()
{
    //! the pinned library's declaration, in order (index == value)
    const QStringList libplasmaMembers =
        readEnumMembersFromQmltypes(QStringLiteral(PLASMAEXTRAS_QMLTYPES_PATH),
                                    QStringLiteral("PopupPlacement"));
    QVERIFY2(!libplasmaMembers.isEmpty(),
             "could not parse PopupPlacement out of the pinned plasmaextracomponents qmltypes - "
             "the contract is un-pinned, re-audit before trusting the context menu placement");

    const QMetaEnum latteEnum = QMetaEnum::fromType<Latte::Types::PopupPlacement>();

    //! same member count: an added or removed libplasma member shifts
    //! every value after it
    QCOMPARE(latteEnum.keyCount(), libplasmaMembers.count());

    for (int value = 0; value < libplasmaMembers.count(); ++value) {
        const QString member = libplasmaMembers.at(value);
        QVERIFY2(latteEnum.keyToValue(member.toUtf8().constData()) == value,
                 qPrintable(QStringLiteral("Latte::Types::%1 != %2, libplasma's declared value - "
                                           "task context menus will pose wrong")
                            .arg(member).arg(value)));
    }
}

QTEST_GUILESS_MAIN(PopupPlacementContractTest)

#include "popupplacementcontracttest.moc"
