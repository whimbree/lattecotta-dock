# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The readback-model drift net, Part 1 (the fast-gate field-key pin) and the
unit proof of Part 2's strict round-trip logic (maintainer-requested 2026-08-07).

The workhorse is test_serializer_keys_match_the_models: it reads the REAL
app/dbusreports.h and asserts, per surface, that the keys the serializer emits
equal the keys the pin accounts for (model aliases | UNMODELED). It runs on
every change with no dock, so a C++ addition, removal, or rename that the models
have not tracked fails here first and cheapest.

The mutation-grade unit tests carry the weight the workhorse cannot: they prove
the extractor and the comparison actually DETECT an added key, a removed key, a
rename, and an unrecognized json-building idiom - so a green workhorse means the
models match truth, not that the net is blind. The Part 2 tests prove the strict
round-trip accepts a real full payload and rejects an injected unknown field.
"""

from __future__ import annotations

import pytest
from pydantic import ValidationError

from latte_harness.dbus_schema_pin import (
    APPLET_SURFACE,
    SURFACES,
    TASK_SURFACE,
    VIEW_SURFACE,
    KeySetComparison,
    SchemaDriftError,
    SchemaExtractionError,
    SerializerSurface,
    accounted_json_keys,
    compare_surface_keys,
    extract_emitted_json_keys,
    find_unaccounted_keys,
    model_json_keys,
    read_dbusreports_header,
    validate_records_strictly,
)
from latte_harness.recipe import Applet, Task, View

# ---- Part 1: the workhorse against the real serializer source --------------


@pytest.mark.parametrize("surface", SURFACES, ids=lambda s: s.name)
def test_serializer_keys_match_the_models(surface: SerializerSurface) -> None:
    """The always-on pin: what serialize*Record emits == what the pin accounts
    for. A drift in either direction fails here, named and directional."""
    comparison = compare_surface_keys(surface, read_dbusreports_header())
    assert comparison.matches, (
        f"{surface.name} drift: dock emits but the pin does not account for "
        f"{sorted(comparison.emitted_but_unaccounted)} (widen {surface.model.__name__} or its "
        f"UNMODELED set); the pin accounts for but the dock no longer emits "
        f"{sorted(comparison.accounted_but_not_emitted)} (a removed/renamed field or a stale "
        "UNMODELED entry)"
    )


def test_no_surface_lists_a_modeled_key_as_unmodeled() -> None:
    """UNMODELED is the keys the model does NOT declare; an overlap would be a
    contradictory double-count, so the pin refuses it."""
    for surface in SURFACES:
        overlap = model_json_keys(surface.model) & frozenset(surface.unmodeled)
        assert not overlap, f"{surface.name}: {sorted(overlap)} are both modeled and in UNMODELED"


def test_every_unmodeled_key_carries_a_justification() -> None:
    """UNMODELED is a key -> why map, so adding an entry structurally requires
    typing a reason; a blank one defeats that, so each must be a real string."""
    for surface in SURFACES:
        for key, why in surface.unmodeled.items():
            assert isinstance(why, str) and why.strip(), (
                f"{surface.name}: UNMODELED key {key!r} has no justification"
            )


def test_model_json_keys_reads_aliases_not_attribute_names() -> None:
    # The contract lives on the JSON aliases, never the snake_case python names.
    assert "containmentId" in model_json_keys(View)
    assert "containment_id" not in model_json_keys(View)
    assert model_json_keys(Applet) == {
        "id",
        "plugin",
        "geometry",
        "inScheduledDestruction",
        "z",
        "colorizerActive",
        "colorizerReason",
    }
    assert model_json_keys(Task) == {
        "appId",
        "launcherUrl",
        "isLauncher",
        "isGrouped",
        "childCount",
        "isActive",
    }


# ---- the extractor: mutation-grade proof it detects drift ------------------
#
# A fixture serializer pair in the exact dbusreports.h idiom - the singular
# record function whose keys are extracted, and the plural aggregate whose name
# contains the singular's, so the locator is proven to pick the right one.

_FIXTURE_HEADER = """
inline QJsonObject serializeThingRecord(const ThingRecord &record)
{
    QJsonObject json;
    json[QStringLiteral("alpha")] = record.alpha;
    json[QStringLiteral("beta")] = record.beta;
    json[QStringLiteral("gamma")] = serializeRect(record.gamma);
    return json;
}

inline QString serializeThingRecords(const QList<ThingRecord> &records)
{
    QJsonArray array;
    for (const auto &record : records) {
        array.append(serializeThingRecord(record));
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}
"""

_THING_SIG = "QJsonObject serializeThingRecord(const ThingRecord &record)"


def test_extractor_pulls_the_emitted_keys_from_the_singular_function() -> None:
    keys = extract_emitted_json_keys(_FIXTURE_HEADER, _THING_SIG)
    # The plural function that follows (serializeThingRecords) carries no
    # json[...] of its own, so the brace-matched body is exactly the singular's.
    assert keys == {"alpha", "beta", "gamma"}


def test_extractor_detects_an_added_key() -> None:
    # A C++ ADDITION: the serializer now emits "delta" the model does not know.
    mutated = _FIXTURE_HEADER.replace(
        'json[QStringLiteral("gamma")] = serializeRect(record.gamma);',
        'json[QStringLiteral("gamma")] = serializeRect(record.gamma);\n'
        '    json[QStringLiteral("delta")] = record.delta;',
    )
    comparison = KeySetComparison(
        surface="thing",
        emitted=extract_emitted_json_keys(mutated, _THING_SIG),
        accounted=frozenset({"alpha", "beta", "gamma"}),
    )
    assert not comparison.matches
    assert comparison.emitted_but_unaccounted == {"delta"}
    assert comparison.accounted_but_not_emitted == frozenset()


def test_extractor_detects_a_removed_or_renamed_key() -> None:
    # A REMOVAL/RENAME: the serializer stops emitting "beta"; the pin still
    # accounts for it, so it surfaces as accounted-but-not-emitted.
    mutated = _FIXTURE_HEADER.replace('    json[QStringLiteral("beta")] = record.beta;\n', "")
    comparison = KeySetComparison(
        surface="thing",
        emitted=extract_emitted_json_keys(mutated, _THING_SIG),
        accounted=frozenset({"alpha", "beta", "gamma"}),
    )
    assert not comparison.matches
    assert comparison.accounted_but_not_emitted == {"beta"}
    assert comparison.emitted_but_unaccounted == frozenset()


def test_extractor_refuses_an_unrecognized_subscript_idiom() -> None:
    # A key written as a non-literal subscript could be emitted a way the
    # QStringLiteral regex cannot see: the completeness guard must refuse, not
    # silently under-count.
    mutated = _FIXTURE_HEADER.replace(
        '    json[QStringLiteral("beta")] = record.beta;',
        "    json[someRuntimeKey] = record.beta;",
    )
    with pytest.raises(SchemaExtractionError, match="unrecognized context"):
        extract_emitted_json_keys(mutated, _THING_SIG)


def test_extractor_refuses_a_method_mutation_idiom() -> None:
    # A key inserted via json.insert(...) would bypass the subscript regex; the
    # guard forbids method mutation of the json object outright.
    mutated = _FIXTURE_HEADER.replace(
        '    json[QStringLiteral("beta")] = record.beta;',
        '    json.insert(QStringLiteral("beta"), record.beta);',
    )
    with pytest.raises(SchemaExtractionError, match="unrecognized context"):
        extract_emitted_json_keys(mutated, _THING_SIG)


def test_extractor_refuses_a_pass_by_reference_builder_shape() -> None:
    # The hole M2 (the extractor completeness-guard hardening) closes: a
    # shared-builder refactor that emits keys via helper(json) leaves the
    # json[QStringLiteral] count intact while adding keys the extractor cannot
    # see. The token-classification guard rejects the bare `json` argument, so
    # Part 1 cannot go false-green under such a refactor.
    mutated = _FIXTURE_HEADER.replace(
        '    json[QStringLiteral("beta")] = record.beta;',
        "    appendCommonThingKeys(json, record);",
    )
    with pytest.raises(SchemaExtractionError, match="unrecognized context"):
        extract_emitted_json_keys(mutated, _THING_SIG)


def test_extractor_refuses_a_whole_object_reassignment_shape() -> None:
    # The other M2 shape: json = buildBase(...) would seed keys from a helper the
    # extractor never reads. The reassigned `json` token is unrecognized.
    mutated = _FIXTURE_HEADER.replace(
        "    QJsonObject json;",
        "    QJsonObject json = buildThingBase(record);",
    )
    with pytest.raises(SchemaExtractionError, match="unrecognized context"):
        extract_emitted_json_keys(mutated, _THING_SIG)


def test_extractor_refuses_a_missing_signature() -> None:
    with pytest.raises(SchemaExtractionError, match="signature not found"):
        extract_emitted_json_keys(_FIXTURE_HEADER, "QJsonObject serializeMissing(const X &r)")


# ---- Part 2 logic: the strict round-trip on real-shaped payloads -----------
#
# A COMPLETE record per surface - every key the serializer emits, modeled and
# unmodeled - i.e. exactly what a real dock reply carries. validate_records_strictly
# must accept it (nothing unaccounted, all required fields present, types coerce),
# and reject the same record once an unknown field is injected.


def _full_view_record() -> dict[str, object]:
    return {
        "containmentId": 16,
        "layout": "Default",
        "isCloned": False,
        "isClonedFrom": -1,
        "type": "dock",
        "screen": "Virtual-0",
        "onPrimary": True,
        "edge": "bottom",
        "alignment": "center",
        "visibilityMode": "alwaysVisible",
        "isHidden": False,
        "inStartup": False,
        "isOffScreen": False,
        "absoluteGeometry": [0, 900, 1600, 100],
        "localGeometry": [0, 0, 1600, 100],
        "screenGeometry": [0, 0, 1600, 1000],
        "strutsThickness": 100,
        "publishedStruts": [0, 900, 1600, 100],
        "maskRect": [0, 0, 1600, 100],
        "inputRegionRects": [[0, 0, 1600, 100]],
        "appliedInputRegionRects": [[0, 0, 1600, 100]],
        "editMode": False,
        "linkedEditHighlight": False,
        "inConfigureAppletsMode": False,
        "keyboardNavigation": False,
        "containmentAcceptsInput": True,
        "ownsPanelFocusSession": False,
    }


def _full_applet_record() -> dict[str, object]:
    return {
        "id": 4,
        "plugin": "org.kde.latte.plasmoid",
        "index": 0,
        "geometry": [10, 0, 200, 100],
        "isExpanded": False,
        "inScheduledDestruction": False,
        "lockedZoom": False,
        "colorizingBlocked": False,
        "colorizerActive": False,
        "colorizerReason": "applied",
        "z": 0.0,
    }


def _full_task_record() -> dict[str, object]:
    return {
        "appletId": 4,
        "index": 0,
        "appId": "org.kde.konsole",
        "launcherUrl": "applications:org.kde.konsole.desktop",
        "isLauncher": True,
        "isGrouped": False,
        "childCount": 0,
        "isActive": False,
        "isMinimized": False,
        "demandsAttention": False,
        "badge": 0,
        "geometry": [0, 0, 50, 100],
    }


def test_a_full_record_carries_exactly_the_accounted_keys() -> None:
    # Each fixture is a real dock reply's key set: modeled aliases plus the
    # UNMODELED ones. So the fixtures cannot silently drift from the pin either.
    assert set(_full_view_record()) == accounted_json_keys(VIEW_SURFACE)
    assert set(_full_applet_record()) == accounted_json_keys(APPLET_SURFACE)
    assert set(_full_task_record()) == accounted_json_keys(TASK_SURFACE)


@pytest.mark.parametrize(
    ("surface", "record"),
    [
        (VIEW_SURFACE, _full_view_record()),
        (APPLET_SURFACE, _full_applet_record()),
        (TASK_SURFACE, _full_task_record()),
    ],
    ids=["viewsData", "viewAppletsData", "viewTasksData"],
)
def test_strict_round_trip_accepts_a_real_full_payload(
    surface: SerializerSurface, record: dict[str, object]
) -> None:
    # No unaccounted key, every required modeled field present, types coerce.
    validate_records_strictly([record], surface)


@pytest.mark.parametrize(
    ("surface", "record"),
    [
        (VIEW_SURFACE, _full_view_record()),
        (APPLET_SURFACE, _full_applet_record()),
        (TASK_SURFACE, _full_task_record()),
    ],
    ids=["viewsData", "viewAppletsData", "viewTasksData"],
)
def test_strict_round_trip_rejects_an_injected_unknown_field(
    surface: SerializerSurface, record: dict[str, object]
) -> None:
    # The captured payload gains a field the pin does not account for (the
    # runtime/helper-emitted key Part 1's text extraction would miss): the
    # round-trip must fail loudly, naming the surface and the key.
    injected = {**record, "surpriseRuntimeKey": 1}
    assert find_unaccounted_keys(injected, surface) == {"surpriseRuntimeKey"}
    with pytest.raises(SchemaDriftError, match="surpriseRuntimeKey"):
        validate_records_strictly([injected], surface)


def test_strict_round_trip_rejects_a_missing_required_field() -> None:
    # A modeled field the dock stopped emitting: pydantic stays the loud layer.
    incomplete = {k: v for k, v in _full_view_record().items() if k != "editMode"}
    with pytest.raises(ValidationError):
        validate_records_strictly([incomplete], VIEW_SURFACE)


def test_strict_round_trip_rejects_a_type_surprise() -> None:
    # A modeled field whose type drifted (a string where an int is the contract).
    surprised = {**_full_applet_record(), "id": "not-an-int"}
    with pytest.raises(ValidationError):
        validate_records_strictly([surprised], APPLET_SURFACE)
