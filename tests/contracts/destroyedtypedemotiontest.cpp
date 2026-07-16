/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Upstream contract (tests/contracts/README.md): by the time QObject emits
//! destroyed(), the object is demoted to a plain QObject - the derived
//! destructors have already run. Consequences a destroyed() handler must
//! respect, each one earned in this port:
//!
//! - qobject_cast to the derived type returns null, so cast-then-remove
//!   silently removes nothing (d6d57e61: SyncedLaunchers left dangling
//!   clients forever and later crashed on a launcher drop).
//! - QPointer/QWeakPointer observing the object already read null.
//! - Derived-class state is freed; dereferencing it reads freed memory
//!   (GenericLayout::containmentDestroyed asked the dying containment for
//!   its location - fixed by caching the edge in Positioner).
//! - What DOES survive is pointer identity, which is why removal by
//!   identity (indexOf/take/remove against the QObject*) is the correct
//!   handler shape.
//!
//! qmltest cannot express this (QML cannot observe C++ destructor phases),
//! so it lives as an ecm_add_test per the contract-suite rules.

// Qt
#include <QPointer>
#include <QtTest>

class DemotionProbe : public QObject
{
    Q_OBJECT

public:
    explicit DemotionProbe(QObject *parent = nullptr)
        : QObject(parent)
    {
    }
};

class DestroyedTypeDemotionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void destroyedHandlerSeesDemotedObject();
};

void DestroyedTypeDemotionTest::destroyedHandlerSeesDemotedObject()
{
    auto *probe = new DemotionProbe;
    QPointer<DemotionProbe> guard(probe);

    bool handlerRan{false};
    bool identityPreserved{false};
    bool castReturnedNull{false};
    bool guardAlreadyNull{false};
    QByteArray reportedClassName;

    connect(probe, &QObject::destroyed, this, [&](QObject *dying) {
        handlerRan = true;

        //! identity survives: this is the one thing a handler may rely on
        identityPreserved = (dying == probe);

        //! the derived type is gone: the exact d6d57e61 trap
        castReturnedNull = (qobject_cast<DemotionProbe *>(dying) == nullptr);

        //! weak references were invalidated before the signal fired
        guardAlreadyNull = guard.isNull();

        //! the metaobject has been demoted to the base class
        reportedClassName = dying->metaObject()->className();
    });

    delete probe;

    QVERIFY(handlerRan);
    QVERIFY2(identityPreserved,
             "destroyed() no longer hands out the original pointer - identity-based "
             "removal in destroyed() handlers (syncedlaunchers, layoutmanager, "
             "panelshadows) is broken");
    QVERIFY2(castReturnedNull,
             "qobject_cast to the derived type succeeded inside a destroyed() handler - "
             "the demotion contract changed; re-audit every destroyed() handler that "
             "avoids casts because of it");
    QVERIFY2(guardAlreadyNull,
             "QPointer still tracked the object inside its destroyed() handler");
    QCOMPARE(reportedClassName, QByteArrayLiteral("QObject"));
}

QTEST_MAIN(DestroyedTypeDemotionTest)

#include "destroyedtypedemotiontest.moc"
