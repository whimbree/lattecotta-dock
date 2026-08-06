# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The pure FP-4B topology-restore cleanup decision, extracted for testing.

tests/e2e/073-window-touch-topology.py drives three physical output
arrangements against three independent partial floating panels inside a
whole-config transaction, and must tear that transaction down safely on every
exit path. The teardown is a small state machine with a hard safety contract:

- it runs on every path (the recipe drives it from a ``finally``, the bash
  ``trap cleanup EXIT``);
- it preserves the body's failure status and never masks it with a cleanup
  success (a body that failed 37 still exits 37; a body that succeeded but
  failed to restore output or dock state exits nonzero);
- it enforces the teardown ordering: restore the captured OUTPUT topology, then
  stop the dock, then replace the config, and restore the config BEFORE
  restarting, and never restart a dock that is still alive.

Unlike the FP-4C storm cleanup (latte_harness.storm_cleanup), this decision
restores output topology and has NO dock-stop gate on the config replacement:
the bash recipe removed and re-copied E2E_CONFIG_HOME unconditionally after the
stop attempt, marking cleanup failed only when the stop, the copy, the still-
alive dock, or the restart said so. A single shared generic core would have to
paper over both shapes, so each recipe keeps its own decision core with its own
Deps.

The decision is separated here from its live side effects (mo_restore topology,
busctl stop/start, the recursive pristine-config copy) so it can be driven
in-process with mocks. That in-process drive (harness/tests/test_topology_cleanup.py)
is the redesign of the bash sourceguard cleanup-EVAL test, which extracted and
eval-executed the shell ``cleanup()`` body in a mock harness and has no direct
Python analog: a Python function body is not eval-executable text, so the
contract is pinned by executing the real decision code with fakes rather than by
matching the source. The recipe wires ``TopologyCleanupDeps`` to the real
effects; sourceguardtest's matchesWindowTouchTopologyE2eContract still pins the
recipe's cleanup WIRING structurally.
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass


@dataclass(frozen=True)
class TopologyCleanupDeps:
    """The side-effecting predicates the cleanup decision drives.

    Injected so the decision is a pure function over booleans: the recipe wires
    these to the real dock/config/output effects, the test wires fakes that also
    record the order in which they ran.
    """

    restore_output_topology: Callable[[], bool]
    """mo_restore_output_topology the captured KScreen topology; True on success."""
    stop_dock: Callable[[], bool]
    """stop the dock; True on a clean stop (or already down)."""
    restore_config: Callable[[], bool]
    """recursively replace the config home with the pristine seed; True on a clean
    copy (the bash rm -rf + cp -r ... || cleanup_failed)."""
    running_dock_pid: Callable[[], int | None]
    """the recorded dock pid iff it is still alive, else None (the safety predicate
    that keeps the restart from resurrecting a still-live dock)."""
    start_dock: Callable[[], bool]
    warn: Callable[[str], None]


def perform_topology_cleanup(
    deps: TopologyCleanupDeps,
    *,
    topology_captured: bool,
    fixture_transaction_active: bool,
    original_status: int,
) -> int:
    """Decide the recipe's final exit status after tearing the transaction down.

    A direct port of the bash ``cleanup`` body with the ``exit`` statement
    replaced by a returned status. The safety ordering and status-preservation
    contract this enforces is documented on the module; it is proved by executing
    this function with mocked ``deps`` in harness/tests/test_topology_cleanup.py.
    """
    cleanup_failed = False

    if topology_captured and not deps.restore_output_topology():
        deps.warn("FP-4B cleanup could not restore the captured output topology")
        cleanup_failed = True

    if fixture_transaction_active:
        if not deps.stop_dock():
            deps.warn("FP-4B cleanup could not stop the fixture dock")
            cleanup_failed = True
        if not deps.restore_config():
            cleanup_failed = True
        if deps.running_dock_pid() is not None:
            cleanup_failed = True
        elif not deps.start_dock():
            deps.warn("FP-4B cleanup could not restart the pristine nested dock")
            cleanup_failed = True

    if cleanup_failed:
        deps.warn("FAIL: FP-4B topology cleanup did not restore output and dock state")
        if original_status == 0:
            original_status = 1
    return original_status
