# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The readback-model drift net: pin the pydantic readback models to the C++
D-Bus serializer that feeds them (maintainer-requested 2026-08-07).

The problem it closes: recipe.View / recipe.Applet / recipe.Task are a
HAND-MAINTAINED MIRROR of app/dbusreports.h's serializeViewRecord /
serializeAppletRecord / serializeTaskRecord, pinned to it by nothing. Before
this net, drift was caught only partially and one-directionally - a removed or
renamed required field raised ValidationError, but ONLY when a gate-driven
recipe happened to read that surface; an ADDED dock field was silently dropped
by the models' extra="ignore"; type and alias drift were caught only
incidentally. This module is the dedicated net that catches drift in BOTH
directions, deterministically.

TWO PARTS share one source of truth, the SURFACES registry below:

- Part 1 (the fast-gate field-key pin, harness/tests/test_dbus_schema_pin.py):
  extract the JSON keys each serializer emits straight from dbusreports.h and
  assert, per surface, that they equal the keys the pin accounts for
  (accounted_json_keys). A C++ ADDITION -> a key the pin does not account for
  -> RED. A C++ REMOVAL/RENAME of a modeled key -> a model alias the dock no
  longer emits -> RED. Runs with no dock, on every change; the workhorse.

- Part 2 (the nested-vehicle round-trip, tests/e2e/001-dbus-readback-schema.py):
  capture a REAL viewsData / viewAppletsData / viewTasksData payload from the
  running dock and validate each record strictly against the SAME accounting,
  so a key the static text extraction missed (a helper- or runtime-emitted
  one) and any type drift on a modeled field fail loudly against real bytes.

DESIGN JUDGMENT (recorded, per the maintainer's request):

- The models are a DELIBERATE SUBSET of the emitted surface: extra="ignore" is
  the intended forward-compat tolerance, and the W3 readback-model widening
  wave grew each model only to "the surface the recipes actually read", not to
  every emitted key. A bare
  set(emitted) == set(model aliases) equality would therefore be RED right now
  against ~20 deliberately-unmodeled keys, and forcing every model to a full
  mirror would carry fields no recipe uses - against the stated design. So the
  pin's contract is set(emitted) == set(model aliases) | set(UNMODELED), where
  UNMODELED is the explicit, per-surface record of keys the dock emits that no
  recipe reads yet. This keeps BOTH-direction detection (the maintainer's real
  goal) while respecting the subset design: every NEW emitted key trips Part 1,
  and the fix is a conscious one-line choice - widen the model (a recipe will
  read it) or add the key to UNMODELED (it stays deliberately unmodeled). That
  is the mark-it-both-ways / name-things discipline applied to schema drift; a
  stale UNMODELED entry the dock stopped emitting also trips the equality, so
  the allow-list cannot rot.

- Part 2 HARD-FAILS (does not merely warn) on a payload key outside the
  accounting. Justification: Part 1 already hard-fails statically and
  deterministically on any serializer key change, so by the time Part 2 runs
  the pin is already in lockstep with the serializer text; a Part 2 failure
  therefore fires ONLY on something the static text could not reveal (a runtime
  or helper-emitted key, a missing required field, a non-coercing type) - which
  is exactly the backstop gap. Warning there would let that precise silent-drift
  case through. A legitimate dock field addition is never a false alarm: it
  fails Part 1 first (the cheap gate), the pin gets its one-line update, and
  Part 2 then passes because it reads the same accounting. The loud failure is
  the wanted signal, and the fix is always a one-line pin update both parts read.
"""

from __future__ import annotations

import re
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from pydantic import BaseModel

from latte_harness.paths import find_repo_root
from latte_harness.recipe import Applet, Task, View

# The serializer source the pin reads, repo-relative (never a hardcoded path).
_DBUSREPORTS_H = Path("app") / "dbusreports.h"


class SchemaExtractionError(RuntimeError):
    """The serializer uses a JSON-building idiom the key extractor does not
    recognize, so a key could be emitted a way it cannot see. Raised loudly
    rather than silently under-counting - extend the extractor (and lean on
    Part 2's real-bytes round-trip as the backstop meanwhile)."""


class SchemaDriftError(RuntimeError):
    """A real readback payload carries a key the pin does not account for: a
    runtime- or helper-emitted key Part 1's static extraction missed. Names the
    offending surface, record index, and key so the fix is a one-line pin update."""


@dataclass(frozen=True, slots=True)
class SerializerSurface:
    """One readback surface bound to its three pinned facts: the C++ serializer
    that emits it, the pydantic model that reads it, and the keys the dock emits
    that no recipe models yet (UNMODELED). The single source of truth both parts
    of the net read.

    ``unmodeled`` is a key -> justification map, not a bare set, on purpose:
    adding an entry structurally REQUIRES typing why the key is deliberately
    unmodeled, so UNMODELED cannot become the lazy path to silence a RED pin
    (widen the model instead if a recipe will read the field). The justification
    is documentation the pin carries, never consulted by the accounting."""

    name: str  # the D-Bus read method, e.g. "viewsData"
    signature: str  # the exact serializer signature located in dbusreports.h
    model: type[BaseModel]
    unmodeled: Mapping[str, str]


# The three always-emitted per-record surfaces the recipes read. Each UNMODELED
# entry is the deliberate record of a key serialize*Record emits that no recipe
# reads, WITH why it stays unmodeled - so the model does not declare it and the
# pin accounts for it here instead. Adding one is a conscious act that must state
# its reason: widen the model instead if a recipe will read the field.
VIEW_SURFACE = SerializerSurface(
    name="viewsData",
    signature="QJsonObject serializeViewRecord(const ViewRecord &record)",
    model=View,
    unmodeled={
        "layout": "the layout name; recipes read placement/geometry, not the layout",
        "type": "dock vs panel; recipes read edge and geometry, not the view type",
        "onPrimary": "the primary-output flag; no recipe branches on it",
        "isOffScreen": "the off-screen flag; recipes assert settled on-screen geometry",
        "strutsThickness": "strut reservation depth; a dockSystemData/struts concern",
        "publishedStruts": "the published strut rect; read via dockSystemData, not here",
        "maskRect": "the paint mask rect; a rendering concern, not a recipe input",
        "inputRegionRects": "the input region; recipes drive via geometry, not the mask",
        "appliedInputRegionRects": "the applied input region; same, a mid-shrink mask detail",
        "linkedEditHighlight": "the linked-edit highlight flag; no recipe reads it",
    },
)

APPLET_SURFACE = SerializerSurface(
    name="viewAppletsData",
    signature="QJsonObject serializeAppletRecord(const AppletRecord &record)",
    model=Applet,
    unmodeled={
        "index": "the applet-order index; recipes use viewAppletsOrder / plugin+geometry",
        "isExpanded": "the popup-expanded flag; no recipe asserts it",
        "lockedZoom": "the per-applet zoom-lock; no recipe reads it",
        "colorizingBlocked": "the colorizer opt-out INPUT; recipes read the DECISION "
        "(colorizerActive / colorizerReason)",
    },
)

TASK_SURFACE = SerializerSurface(
    name="viewTasksData",
    signature="QJsonObject serializeTaskRecord(const TaskRecord &record)",
    model=Task,
    unmodeled={
        "appletId": "the owning applet id; recipes resolve the tasks view another way",
        "index": "the row index; the array position already carries order",
        "isMinimized": "per-row minimized state; no recipe asserts it",
        "demandsAttention": "per-row attention state; no recipe asserts it",
        "badge": "the per-row badge count; no recipe asserts it",
        "geometry": "the per-row WM rect; recipes compute icon centers from applet geometry",
    },
)

SURFACES: tuple[SerializerSurface, ...] = (VIEW_SURFACE, APPLET_SURFACE, TASK_SURFACE)


# ---- the model side: the keys a model declares -----------------------------


def model_json_keys(model: type[BaseModel]) -> frozenset[str]:
    """The JSON keys a readback model declares: each field's alias, or its name
    when unaliased. The models map camelCase JSON keys onto snake_case python
    attributes via alias, so the contract lives on the alias/json-key set, never
    the python attribute names."""
    return frozenset((field.alias or name) for name, field in model.model_fields.items())


def accounted_json_keys(surface: SerializerSurface) -> frozenset[str]:
    """Every JSON key the pin accounts for on a surface: the modeled aliases plus
    the deliberately-unmodeled keys. The single accounting both parts compare
    against. The UNMODELED justifications are documentation only; the accounting
    is over its keys."""
    return model_json_keys(surface.model) | frozenset(surface.unmodeled)


# ---- the C++ side: the keys a serializer emits -----------------------------

# json[QStringLiteral("KEY")] = ... : the one key-emitting idiom all three
# serialize*Record functions use. The trailing `=` anchors it to an assignment
# target, so a QStringLiteral appearing as a value could never be miscounted.
_EMITTED_KEY_RE = re.compile(r'json\[QStringLiteral\("([^"\\]+)"\)\]\s*=')
# Every bare `json` token in the body. The completeness guard classifies each
# one: a key could reach the object by subscript assignment (recognized), but
# also by method mutation (json.insert(...)), by pass-by-reference to a shared
# builder (helper(json)), or by whole-object reassignment (json = buildBase(...)).
# Only the recognized idiom, the declaration, and the return are allowed; any
# other `json` token trips SchemaExtractionError instead of silently under-counting.
_JSON_TOKEN_RE = re.compile(r"\bjson\b")
_JSON_DECL_RE = re.compile(r"QJsonObject\s+json\s*;")
_JSON_RETURN_RE = re.compile(r"return\s+json\s*;")

# CAVEAT (latent, string/comment-unaware): _JSON_TOKEN_RE matches the token
# `json` anywhere, including inside a string literal or a // comment. No current
# serializer body mentions the word `json` in prose, so this never fires today;
# if one ever does, the guard fails CLOSED (a loud SchemaExtractionError asking
# to extend the extractor), never false-green - the safe direction for a drift
# net. Strip comments/strings here if that ever becomes noisy.


def _serializer_body(header_text: str, signature: str) -> str:
    """The brace-delimited body of one serializer, located by its EXACT signature
    (so serializeViewRecord is never confused with serializeViewRecords) and
    brace-matched to its close. The brace matcher is likewise string/comment-
    unaware; the three serializer bodies carry no brace inside a string or
    comment, so it is exact for them."""
    start = header_text.find(signature)
    if start < 0:
        raise SchemaExtractionError(
            f"serializer signature not found in dbusreports.h: {signature!r}"
        )
    brace = header_text.find("{", start)
    if brace < 0:
        raise SchemaExtractionError(f"no opening brace after signature: {signature!r}")
    depth = 0
    for index in range(brace, len(header_text)):
        char = header_text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return header_text[brace + 1 : index]
    raise SchemaExtractionError(f"unterminated serializer body: {signature!r}")


def extract_emitted_json_keys(header_text: str, signature: str) -> frozenset[str]:
    """The complete set of JSON keys a serializer emits, parsed from its body.

    Completeness guard (the loud-not-silent discipline): EVERY bare `json` token
    in the body must be one of three recognized forms - the `QJsonObject json;`
    declaration, a `json[QStringLiteral("KEY")] =` key assignment, or the final
    `return json;`. Any other `json` token means a key could reach the object a
    way this extractor cannot see - a `json.insert(...)` method mutation, a
    `helper(json)` pass-by-reference to a shared builder, or a `json =
    buildBase(...)` reassignment - so the guard refuses loudly
    (SchemaExtractionError) rather than under-count. Part 2's real-bytes
    round-trip is the backstop if a future emit shape ever slips past even this.
    """
    body = _serializer_body(header_text, signature)
    keys = frozenset(match.group(1) for match in _EMITTED_KEY_RE.finditer(body))

    total = len(_JSON_TOKEN_RE.findall(body))
    key_assignments = len(_EMITTED_KEY_RE.findall(body))
    declarations = len(_JSON_DECL_RE.findall(body))
    returns = len(_JSON_RETURN_RE.findall(body))
    recognized = key_assignments + declarations + returns
    if total != recognized:
        raise SchemaExtractionError(
            f"{signature}: {total - recognized} bare `json` token(s) in an unrecognized context "
            '(only the QJsonObject json; declaration, json[QStringLiteral("KEY")] = assignments, '
            "and return json; are recognized). A json.insert(...), helper(json), or json = ... "
            "shape would under-count the emitted keys; extend extract_emitted_json_keys"
        )
    return keys


def read_dbusreports_header(root: Path | None = None) -> str:
    """The serializer source text the pin extracts from, rooted at the repo."""
    repo_root = root if root is not None else find_repo_root()
    return (repo_root / _DBUSREPORTS_H).read_text()


# ---- Part 1: the field-key comparison --------------------------------------


@dataclass(frozen=True, slots=True)
class KeySetComparison:
    """One surface's emitted-vs-accounted key comparison. The two difference
    sets name the drift direction so a failure message is actionable."""

    surface: str
    emitted: frozenset[str]
    accounted: frozenset[str]

    @property
    def emitted_but_unaccounted(self) -> frozenset[str]:
        """Keys the dock emits that the pin does not account for: a C++ ADDITION.
        Fix by widening the model (a recipe will read it) or the UNMODELED set."""
        return self.emitted - self.accounted

    @property
    def accounted_but_not_emitted(self) -> frozenset[str]:
        """Keys the pin accounts for that the dock no longer emits: a REMOVAL /
        RENAME of a modeled key, or a stale UNMODELED entry."""
        return self.accounted - self.emitted

    @property
    def matches(self) -> bool:
        return self.emitted == self.accounted


def compare_surface_keys(surface: SerializerSurface, header_text: str) -> KeySetComparison:
    """The Part 1 comparison for one surface: what the serializer emits versus
    what the pin accounts for."""
    return KeySetComparison(
        surface=surface.name,
        emitted=extract_emitted_json_keys(header_text, surface.signature),
        accounted=accounted_json_keys(surface),
    )


# ---- Part 2: the strict round-trip on real bytes ---------------------------


def find_unaccounted_keys(
    record: Mapping[str, object], surface: SerializerSurface
) -> frozenset[str]:
    """The record keys the pin does not account for (the round-trip's extra-key
    check: effectively extra="forbid", but relative to model | UNMODELED so the
    deliberately-unmodeled keys the dock really emits are not false positives)."""
    return frozenset(record.keys()) - accounted_json_keys(surface)


def validate_records_strictly(
    records: Sequence[Mapping[str, object]], surface: SerializerSurface
) -> None:
    """The round-trip's strict check on a real payload: raise SchemaDriftError if
    any record carries a key the pin does not account for (a runtime/helper key
    the static extraction missed), then validate the modeled subset with the
    production model so a missing required field or a non-coercing type fails
    loudly (pydantic ValidationError names the field). Returns None on success.
    """
    for index, record in enumerate(records):
        unaccounted = find_unaccounted_keys(record, surface)
        if unaccounted:
            raise SchemaDriftError(
                f"{surface.name}[{index}] carries {sorted(unaccounted)}, which neither "
                f"{surface.model.__name__} nor its known-unmodeled set declares; widen the model "
                "(if a recipe will read it) or the surface's UNMODELED set in dbus_schema_pin"
            )
        surface.model.model_validate(record)
