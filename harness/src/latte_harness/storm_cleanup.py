# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The pure FP-4C operation-storm cleanup decision, extracted for testing.

tests/e2e/linked-dock-operation-stress.py replaces the whole nested
configuration inside a transaction and must tear that transaction down safely on
every exit path. The teardown is a small state machine with a hard safety
contract:

- it runs on every path (the recipe drives it from a ``finally``, the bash
  ``trap cleanup EXIT``);
- it preserves the body's failure status and never masks it with a cleanup
  success (a body that failed 37 still exits 37; a body that succeeded but left
  residue exits nonzero);
- it enforces the teardown ordering: stop the dock BEFORE replacing the config
  (never replace it under a live dock), and restore the config FULLY before
  restarting (never start against a partial restore).

The decision is separated here from its live side effects (busctl stop/start,
the recursive pristine-config copy, the baseline re-verify) so it can be driven
in-process with mocks. That in-process drive
(harness/tests/test_storm_cleanup.py) is the redesign of the bash sourceguard
cleanup-EVAL test, which extracted and eval-executed the shell
``cleanup()``/``restore_config_exactly()`` function bodies in a mock harness and
has no direct Python analog: a Python function body is not eval-executable text,
so the contract is pinned by executing the real decision code with fakes rather
than by matching the source. The recipe wires ``CleanupDeps`` to the real
effects; sourceguardtest's matchesLinkedOperationStormE2eContract still pins the
recipe's cleanup WIRING structurally.
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass


@dataclass(frozen=True)
class CleanupDeps:
    """The side-effecting predicates the cleanup decision drives.

    Injected so the decision is a pure function over booleans: the recipe wires
    these to the real dock/config effects, the test wires fakes that also record
    the order in which they ran.
    """

    preserve_dock_log: Callable[[], bool]
    """cp the fixture dock log into the artifacts; True on success or when there
    is nothing to preserve."""
    stop_dock: Callable[[], bool]
    """stop the dock if it is running; True on a clean stop (or already down)."""
    running_dock_pid: Callable[[], int | None]
    """the recorded dock pid iff it is still alive, else None."""
    restore_config: Callable[[], bool]
    """recursively replace the config home with the pristine backup and verify."""
    dock_is_running: Callable[[], bool]
    start_dock: Callable[[], bool]
    reverify_baseline: Callable[[], bool]
    """re-capture the restored baseline and prove it projects to the pristine one."""
    warn: Callable[[str], None]


def perform_cleanup_transaction(
    deps: CleanupDeps,
    *,
    transaction_started: bool,
    backup_ready: bool,
    acceptance_completed: bool,
    original_status: int,
) -> int:
    """Decide the recipe's final exit status after tearing the transaction down.

    A direct port of the bash ``cleanup`` body with the ``exit`` statements
    replaced by a returned status. The safety ordering and status-preservation
    contract this enforces is documented on the module; it is proved by executing
    this function with mocked ``deps`` in harness/tests/test_storm_cleanup.py.
    """
    cleanup_failed = False
    dock_stopped = False
    config_safe_to_start = False

    if transaction_started:
        if not deps.preserve_dock_log():
            deps.warn("FAIL: FP-4C cleanup could not preserve the fixture dock log")
            cleanup_failed = True
        if not deps.stop_dock():
            deps.warn("FAIL: FP-4C cleanup could not stop the fixture dock")
            cleanup_failed = True
        pid = deps.running_dock_pid()
        if pid is not None:
            deps.warn(f"FAIL: FP-4C cleanup left fixture dock pid {pid} running")
            cleanup_failed = True
        else:
            dock_stopped = True

        if backup_ready and dock_stopped:
            if not deps.restore_config():
                deps.warn(
                    "FAIL: FP-4C cleanup could not recursively restore the pristine configuration"
                )
                cleanup_failed = True
            else:
                config_safe_to_start = True
        elif backup_ready:
            deps.warn(
                "FAIL: FP-4C cleanup refused to replace configuration under a live fixture dock"
            )
            cleanup_failed = True
        elif dock_stopped:
            # No fixture mutation occurs before the exact backup is complete.
            config_safe_to_start = True

        if (
            dock_stopped
            and config_safe_to_start
            and not deps.dock_is_running()
            and not deps.start_dock()
        ):
            deps.warn("FAIL: FP-4C cleanup could not restart the pristine nested dock")
            cleanup_failed = True

        if (
            backup_ready
            and config_safe_to_start
            and deps.dock_is_running()
            and not deps.reverify_baseline()
        ):
            cleanup_failed = True

    if not acceptance_completed and original_status == 0:
        deps.warn("FAIL: FP-4C recipe exited before completing its acceptance")
        original_status = 1
    if cleanup_failed:
        if original_status != 0:
            deps.warn(
                f"FAIL: FP-4C cleanup also failed after original recipe status {original_status}"
            )
            return original_status
        return 1
    return original_status
