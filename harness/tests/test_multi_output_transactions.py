# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""multi_output's REAL transaction drivers (mo_capture_output_topology /
mo_restore_output_topology) against a recording fake kscreen-doctor: the
behavioral port of the deleted bash contract test's fake-doctor proofs
(tests/multioutputstatecontracttest.sh, removed by cleanup BW-3 - the
multi-output bash closed loop). Three contracts nothing pure covers, and the
live selftest drives cannot either: the selftest normalizes priorities to the
canonical 1,2 before capture and its mutations touch only rotation and
position, so a restore that silently dropped both priority setters, or a
cleanup verification weakened to the restorable projection, would pass every
live control. Each test here observes its failure directly - the recorded
mutation calls or the driven drift - never a source-text match.
"""

from __future__ import annotations

import json

import pytest

from latte_harness import multi_output
from latte_harness.multi_output import MultiOutputError


def _canonical_capture(*, virtual_1_hdr: bool = False) -> str:
    """The canonical captured KScreen payload, ported from the deleted bash
    contract test: canonical priorities 1,2, a rotated scaled secondary, and
    fields with no restore setter (currentModeId, modes, capabilities, metadata)
    that the semantic postcondition must still verify. ``virtual_1_hdr`` is the
    controlled unrestorable-field drift knob."""
    return json.dumps(
        {
            "outputs": [
                {
                    "name": "Virtual-1",
                    "enabled": True,
                    "rotation": 1,
                    "scale": 1,
                    "pos": {"x": 0, "y": 0},
                    "currentModeId": "1",
                    "priority": 1,
                    "modes": [{"id": "1", "size": {"width": 1600, "height": 1000}}],
                    "capabilities": {"hdr": virtual_1_hdr},
                },
                {
                    "name": "Virtual-2",
                    "enabled": True,
                    "rotation": 8,
                    "scale": 1.25,
                    "pos": {"x": 1600, "y": 120},
                    "currentModeId": "3",
                    "priority": 2,
                    "modes": [{"id": "3", "size": {"width": 1000, "height": 1600}}],
                    "capabilities": {"hdr": False},
                },
            ],
            "screen": {"currentSize": {"width": 2600, "height": 1720}},
            "metadata": {"backend": "KWayland"},
        }
    )


def _wire_fake_doctor(monkeypatch: pytest.MonkeyPatch, read_state: str) -> list[tuple[str, ...]]:
    """Point the discovery env at the fixture names, disarm the nested-vehicle
    gate, and replace both kscreen-doctor edges: reads return ``read_state``,
    mutations are recorded and report success. Returns the mutation ledger."""
    set_calls: list[tuple[str, ...]] = []

    def record_set(*args: str) -> bool:
        set_calls.append(args)
        return True

    def gate_disarmed(caller: str) -> None:
        del caller

    def read_back() -> str | None:
        return read_state

    monkeypatch.setenv("E2E_MO_PRIMARY", "Virtual-1")
    monkeypatch.setenv("E2E_MO_SECONDARY", "Virtual-2")
    monkeypatch.setattr(multi_output, "_require_topology_mutation", gate_disarmed)
    monkeypatch.setattr(multi_output, "_kscreen_read", read_back)
    monkeypatch.setattr(multi_output, "_kscreen_set", record_set)
    return set_calls


def test_restore_emits_both_captured_priority_setters_in_one_atomic_call(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    # The read-back equals the capture, so the semantic postcondition passes and
    # the recorded ledger is the whole restore: one atomic kscreen-doctor call
    # carrying exactly the two captured priority setters. A restore that dropped
    # a priority setter, or split the transaction, fails here and nowhere live.
    captured = _canonical_capture()
    set_calls = _wire_fake_doctor(monkeypatch, captured)
    multi_output.mo_restore_output_topology(captured)
    assert len(set_calls) == 1
    call = set_calls[0]
    priority_setters = sorted(arg for arg in call if ".priority." in arg)
    assert priority_setters == [
        "output.Virtual-1.priority.1",
        "output.Virtual-2.priority.2",
    ]
    # currentModeId is deliberately never assigned through guessed kscreen-doctor
    # syntax (the deleted matcher's .mode. exclusion, pinned behaviorally): the
    # semantic postcondition verifies the mode, a setter must not guess at it.
    assert not any(".mode." in arg for arg in call)
    # Each output's enabled state and scale are restored exactly once inside the
    # atomic call. Rotation and position stay live-driven (the R12 dual-output
    # recipe-port round's place/restore transactions), not re-pinned here.
    for name in ("Virtual-1", "Virtual-2"):
        assert sum(arg == f"output.{name}.enable" for arg in call) == 1
        assert sum(arg.startswith(f"output.{name}.scale.") for arg in call) == 1


@pytest.mark.parametrize(
    "malformed",
    [
        "[]",
        json.dumps(
            {
                "outputs": [
                    {"name": [], "enabled": True, "priority": 1},
                    {"name": "Virtual-2", "enabled": True, "priority": 2},
                ]
            }
        ),
    ],
)
def test_capture_refuses_malformed_state_before_any_mutation(
    monkeypatch: pytest.MonkeyPatch, malformed: str
) -> None:
    # Malformed priority state is a refusal, never a request to normalize: the
    # loud error must arrive with the mutation ledger still empty.
    set_calls = _wire_fake_doctor(monkeypatch, malformed)
    with pytest.raises(MultiOutputError, match="malformed priority state"):
        _ = multi_output.mo_capture_output_topology()
    assert set_calls == []


def test_restore_fails_loudly_when_an_unrestorable_field_drifted(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    # The drifted field (a capability) has no restore setter, so the restorable
    # projection still matches the capture; only the complete semantic
    # comparison can see the drift. A wait loop weakened to projection-only
    # verification accepts this read-back and fails this test - the behavioral
    # form of the deleted sourceGuardRejectsProjectionOnlyVerification text pin.
    def instant(seconds: float) -> None:
        # The verify loop polls 120 times before its verdict; the fake read-back
        # never changes, so sleeping between polls buys nothing but wall time.
        del seconds

    captured = _canonical_capture()
    set_calls = _wire_fake_doctor(monkeypatch, _canonical_capture(virtual_1_hdr=True))
    monkeypatch.setattr(multi_output.time, "sleep", instant)
    with pytest.raises(MultiOutputError, match="drifted at") as failure:
        multi_output.mo_restore_output_topology(captured)
    assert "hdr" in str(failure.value)
    assert len(set_calls) == 1  # the restore itself was still emitted atomically
