#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""The readback-model drift net, Part 2: the round-trip against REAL dock bytes
(maintainer-requested 2026-08-07).

Part 1 (harness/tests/test_dbus_schema_pin.py) pins the pydantic readback models
to the C++ serializer by parsing dbusreports.h with no dock - deterministic, and
the workhorse. This recipe is the truth backstop: it captures a real viewsData /
viewAppletsData / viewTasksData payload from the running vehicle dock and
validates every record strictly against the SAME accounting (model aliases plus
the surface's deliberately-unmodeled keys). It catches what the static text
extraction cannot see - a helper- or runtime-emitted key, a missing required
field, or a type that no longer coerces - against the actual bytes on the wire.

A drift here is a loud, deliberate signal, not a false alarm (the design note in
latte_harness.dbus_schema_pin): Part 1 hard-fails first on any serializer key
change, so this fires only on something the text could not reveal, and the fix is
always a one-line pin update both parts read.

It is a dedicated always-run recipe, so the round-trip is exercised on every full
gate, not only when some other recipe happens to read a surface - the exact gap
the net closes.
"""

from __future__ import annotations

from collections.abc import Mapping, Sequence

from pydantic import ValidationError

from latte_harness import recipe
from latte_harness.dbus_schema_pin import (
    APPLET_SURFACE,
    TASK_SURFACE,
    VIEW_SURFACE,
    SchemaDriftError,
    SerializerSurface,
    validate_records_strictly,
)


def _validate(records: Sequence[Mapping[str, object]], surface: SerializerSurface) -> int:
    """Validate a captured payload strictly; a drift becomes a loud recipe fail
    naming the surface, the record, and the key or field. Returns the count."""
    try:
        validate_records_strictly(records, surface)
    except (SchemaDriftError, ValidationError) as err:
        recipe.fail(f"{surface.name} drift against the real dock payload: {err}")
    return len(records)


def main() -> None:
    if not recipe.wait_running(30):
        recipe.fail("dock not running")
    if not recipe.wait_settled(30):
        recipe.fail("views did not settle")

    views = recipe.read_json("viewsData")
    view_count = _validate(views, VIEW_SURFACE)
    applet_total = 0
    task_total = 0
    for view in views:
        containment_id = view["containmentId"]
        applet_total += _validate(
            recipe.read_json("viewAppletsData", "u", str(containment_id)), APPLET_SURFACE
        )
        # A view without a tasks plasmoid legitimately reports "[]" (collectTasksData),
        # so every view is safe to read; an empty payload validates as zero records.
        task_total += _validate(
            recipe.read_json("viewTasksData", "u", str(containment_id)), TASK_SURFACE
        )

    print(
        f"dbus readback schema round-trip OK: {view_count} views, "
        f"{applet_total} applets, {task_total} tasks validated against the pin"
    )


if __name__ == "__main__":
    recipe.run(main)
