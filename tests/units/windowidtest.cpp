/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Pins the WindowId newtype's whole boundary contract (the WindowId
//! newtype hardening item in docs/tracking/PORTING_PLAN.md):
//! - construction stays explicit-only: the compile-time pins below fail
//!   the build if a QByteArray or char* ever converts in implicitly
//!   again (the stringly-typed hole the newtype exists to close);
//! - toX11WId() answers nullopt for every malformed shape that the old
//!   ok-flag-less toUInt() silently turned into window 0;
//! - equality and QMap-key ordering are byte-for-byte the QByteArray
//!   semantics of the pre-newtype alias, so the substrate swap is
//!   provably behavior-preserving for the wayland tracking containers;
//! - the loud-absence boundary (parseX11WindowId) warns on malformed
//!   input and never produces a window id, and stays quiet for the
//!   documented empty no-window id.

#include "wm/windowid.h"

// Qt
#include <QMap>
#include <QtTest>

// C++
#include <functional>
#include <optional>
#include <type_traits>

using Latte::WindowSystem::WindowId;
using Latte::WindowSystem::parseX11WindowId;

//! compile-time pins: implicit conversion into WindowId stays deleted.
//! is_constructible covers even explicit construction from raw bytes
//! (the constructor is private, factories are the only doors), and
//! is_convertible/is_assignable cover the implicit routes separately so
//! a future public-but-explicit constructor still trips the right pin.
static_assert(!std::is_convertible_v<QByteArray, WindowId>);
static_assert(!std::is_convertible_v<const char *, WindowId>);
static_assert(!std::is_constructible_v<WindowId, QByteArray>);
static_assert(!std::is_constructible_v<WindowId, const char *>);
static_assert(!std::is_assignable_v<WindowId &, QByteArray>);
static_assert(!std::is_assignable_v<WindowId &, const char *>);
//! and the value-type basics the containers and signals rely on
static_assert(std::is_default_constructible_v<WindowId>);
static_assert(std::is_copy_constructible_v<WindowId>);
static_assert(std::is_move_constructible_v<WindowId>);

namespace {
//! capture every warning emitted while a callable runs, so quietness can
//! be asserted as strictly as loudness (QTest::ignoreMessage can only
//! demand a message, not forbid one)
QStringList warningsEmittedBy(const std::function<void()> &action)
{
    static QStringList capturedWarnings;
    capturedWarnings.clear();

    const QtMessageHandler previousHandler =
        qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString &message) {
            if (type == QtWarningMsg) {
                capturedWarnings << message;
            }
        });

    action();

    qInstallMessageHandler(previousHandler);

    return capturedWarnings;
}
}

class WindowIdTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void defaultAndX11NoneAreTheEmptyId();
    void waylandUuidRoundTripsItsBytes();
    void x11IdRoundTripsThroughDecimalBytes();

    void toX11WIdRefusesEveryMalformedShape_data();
    void toX11WIdRefusesEveryMalformedShape();
    void toX11WIdParsesEveryWellFormedShape_data();
    void toX11WIdParsesEveryWellFormedShape();

    void equalityIsByteWiseAcrossFactories();
    void mapOrderingMatchesTheOldByteArraySubstrate();
    void mapLookupFindsEqualKeysFromEitherFactory();

    void boundaryParseWarnsOnMalformedAndProducesNoWindow();
    void boundaryParseStaysQuietForTheEmptyId();
};

void WindowIdTest::defaultAndX11NoneAreTheEmptyId()
{
    QVERIFY(WindowId().isEmpty());
    QVERIFY(WindowId().bytes().isEmpty());

    //! 0 is X11 None, not a window - the factory maps it to the empty id
    QVERIFY(WindowId::fromX11WId(0).isEmpty());
    QCOMPARE(WindowId::fromX11WId(0), WindowId());
}

void WindowIdTest::waylandUuidRoundTripsItsBytes()
{
    //! the shape KWayland's PlasmaWindow::uuid() delivers
    const QByteArray uuid = QByteArrayLiteral("{4a5b6c7d-8e9f-4a1b-9c2d-3e4f5a6b7c8d}");
    const WindowId wid = WindowId::fromWaylandUuid(uuid);

    QVERIFY(!wid.isEmpty());
    QCOMPARE(wid.bytes(), uuid);
}

void WindowIdTest::x11IdRoundTripsThroughDecimalBytes()
{
    const WindowId wid = WindowId::fromX11WId(77594625);

    QVERIFY(!wid.isEmpty());
    QCOMPARE(wid.bytes(), QByteArrayLiteral("77594625"));
    QCOMPARE(wid.toX11WId(), std::optional<quint32>(77594625));
}

void WindowIdTest::toX11WIdRefusesEveryMalformedShape_data()
{
    QTest::addColumn<QByteArray>("bytes");

    //! every shape the old ok-flag-less toUInt() turned into window 0
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("wayland-uuid") << QByteArrayLiteral("{4a5b6c7d-8e9f-4a1b-9c2d-3e4f5a6b7c8d}");
    QTest::newRow("non-numeric") << QByteArrayLiteral("garbage");
    QTest::newRow("trailing-garbage") << QByteArrayLiteral("12ab");
    QTest::newRow("embedded-space") << QByteArrayLiteral("12 34");
    QTest::newRow("hex-prefixed") << QByteArrayLiteral("0x1f");
    QTest::newRow("negative") << QByteArrayLiteral("-1");
    QTest::newRow("overflow-just-past-quint32") << QByteArrayLiteral("4294967296");
    QTest::newRow("overflow-way-past-quint32") << QByteArrayLiteral("18446744073709551617");
    //! 0 parses, but X11 None is not a window: refused by design so
    //! window 0 is unproducible from this type
    QTest::newRow("zero") << QByteArrayLiteral("0");
    QTest::newRow("zero-padded-zero") << QByteArrayLiteral("000");
}

void WindowIdTest::toX11WIdRefusesEveryMalformedShape()
{
    QFETCH(QByteArray, bytes);

    //! fromWaylandUuid is the byte-verbatim door, so it carries the
    //! malformed shapes into the type exactly as a foreign id would
    const WindowId wid = WindowId::fromWaylandUuid(bytes);

    QCOMPARE(wid.toX11WId(), std::optional<quint32>());
}

void WindowIdTest::toX11WIdParsesEveryWellFormedShape_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<quint32>("expected");

    QTest::newRow("small") << QByteArrayLiteral("1") << quint32(1);
    QTest::newRow("typical") << QByteArrayLiteral("77594625") << quint32(77594625);
    //! above INT_MAX: pins the retirement of the old toInt() sites that
    //! parsed such ids to 0 (requestActivate, requestToggleMaximized)
    QTest::newRow("past-int-max") << QByteArrayLiteral("3000000000") << quint32(3000000000);
    QTest::newRow("quint32-max") << QByteArrayLiteral("4294967295") << quint32(4294967295U);
}

void WindowIdTest::toX11WIdParsesEveryWellFormedShape()
{
    QFETCH(QByteArray, bytes);
    QFETCH(quint32, expected);

    QCOMPARE(WindowId::fromWaylandUuid(bytes).toX11WId(), std::optional<quint32>(expected));
    //! and the same answer through the numeric factory
    QCOMPARE(WindowId::fromX11WId(expected).toX11WId(), std::optional<quint32>(expected));
}

void WindowIdTest::equalityIsByteWiseAcrossFactories()
{
    const QByteArray uuid = QByteArrayLiteral("{4a5b6c7d-8e9f-4a1b-9c2d-3e4f5a6b7c8d}");

    QCOMPARE(WindowId::fromWaylandUuid(uuid), WindowId::fromWaylandUuid(uuid));
    QVERIFY(WindowId::fromWaylandUuid(uuid) != WindowId::fromWaylandUuid(QByteArrayLiteral("{other}")));

    //! byte equality holds across factories: the id IS its bytes, the
    //! factory only names the platform the bytes came from
    QCOMPARE(WindowId::fromX11WId(42), WindowId::fromWaylandUuid(QByteArrayLiteral("42")));
}

void WindowIdTest::mapOrderingMatchesTheOldByteArraySubstrate()
{
    //! representative id population: uuid-shaped wayland keys plus
    //! decimal X11 keys whose lexical order differs from numeric order
    const QList<QByteArray> population = {
        QByteArrayLiteral("{4a5b6c7d-8e9f-4a1b-9c2d-3e4f5a6b7c8d}"),
        QByteArrayLiteral("{0a0b0c0d-1111-2222-3333-444455556666}"),
        QByteArrayLiteral("9"),
        QByteArrayLiteral("10"),
        QByteArrayLiteral("77594625"),
    };

    QMap<WindowId, int> newSubstrate;
    QMap<QByteArray, int> oldSubstrate;

    for (int i = 0; i < population.count(); ++i) {
        newSubstrate.insert(WindowId::fromWaylandUuid(population[i]), i);
        oldSubstrate.insert(population[i], i);
    }

    //! identical key walk proves identical strict-weak-ordering, so the
    //! wayland tracking maps behave exactly as they did on the alias
    QList<QByteArray> newWalk;
    for (auto it = newSubstrate.constBegin(); it != newSubstrate.constEnd(); ++it) {
        newWalk << it.key().bytes();
    }

    QCOMPARE(newWalk, oldSubstrate.keys());

    //! and the known lexical quirk stays pinned explicitly: decimal ids
    //! order as strings ("10" before "9"), same as the alias always did
    QVERIFY(WindowId::fromX11WId(10) < WindowId::fromX11WId(9));
}

void WindowIdTest::mapLookupFindsEqualKeysFromEitherFactory()
{
    QMap<WindowId, QString> schemes;
    schemes.insert(WindowId::fromWaylandUuid(QByteArrayLiteral("{4a5b6c7d-8e9f-4a1b-9c2d-3e4f5a6b7c8d}")), QStringLiteral("dark"));
    schemes.insert(WindowId::fromX11WId(77594625), QStringLiteral("light"));

    QVERIFY(schemes.contains(WindowId::fromWaylandUuid(QByteArrayLiteral("{4a5b6c7d-8e9f-4a1b-9c2d-3e4f5a6b7c8d}"))));
    QCOMPARE(schemes.value(WindowId::fromX11WId(77594625)), QStringLiteral("light"));
    QVERIFY(!schemes.contains(WindowId()));
}

void WindowIdTest::boundaryParseWarnsOnMalformedAndProducesNoWindow()
{
    const WindowId foreign = WindowId::fromWaylandUuid(QByteArrayLiteral("{4a5b6c7d-8e9f-4a1b-9c2d-3e4f5a6b7c8d}"));

    std::optional<quint32> parsed = std::optional<quint32>(0);
    const QStringList warnings = warningsEmittedBy([&]() {
        parsed = parseX11WindowId(foreign, "windowidtest");
    });

    //! the absence is loud: exactly one warning, naming the operation
    //! and the offending bytes
    QCOMPARE(warnings.count(), 1);
    QVERIFY(warnings.first().contains(QStringLiteral("windowidtest")));
    QVERIFY(warnings.first().contains(QStringLiteral("refusing malformed X11 window id")));
    QVERIFY(warnings.first().contains(QStringLiteral("4a5b6c7d")));

    //! and no window id is produced - in particular NOT window 0, which
    //! is what the pre-newtype toUInt() handed every caller here
    QVERIFY(!parsed.has_value());
}

void WindowIdTest::boundaryParseStaysQuietForTheEmptyId()
{
    std::optional<quint32> parsed = std::optional<quint32>(0);
    const QStringList warnings = warningsEmittedBy([&]() {
        parsed = parseX11WindowId(WindowId(), "windowidtest");
    });

    //! the empty id is the documented no-window value: refused, but
    //! quietly - it is a legitimate absence, not a defect
    QVERIFY(warnings.isEmpty());
    QVERIFY(!parsed.has_value());
}

QTEST_GUILESS_MAIN(WindowIdTest)

#include "windowidtest.moc"
