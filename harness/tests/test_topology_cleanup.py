# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Drive the FP-4B topology-restore cleanup decision in-process.

This is the redesign of the bash sourceguard cleanup-EVAL test
(the eval-harness inside SourceGuardTest::windowTouchTopologyE2e_cleanupGuard...),
which extracted the shell ``cleanup()`` body and eval-executed it in a mock bash
harness across two cases. A Python function body is not eval-executable text, so
the contract is pinned here by executing the real decision
(latte_harness.topology_cleanup) with mocked side effects - a stronger proof than
the bash reconstruction, since it drives the actual code the recipe wires.

The success and failure cases are behaviourally identical to the bash harness's
``run_cleanup_case`` invocations:

    success:      original 37, topology ok, stop ok,   pid gone, restore ok -> 37
    dock-restart-failed: original  0, topology ok, stop ok, pid gone, restore ok,
                         start fails                                        ->  1

plus the two safety dimensions the FP-4B teardown adds (a still-alive dock is
never restarted; a failed output-topology restore is a failure) and two DRIVEN
mutation controls: a broken cleanup that masks the failure status, and a broken
cleanup that restarts the dock before restoring the pristine config. Each mutant
violates the guarded pattern and the assertions below catch it, so the guard
demonstrably fails when the pattern is violated (the mutation-control the bash
test carried, relocated from a source text-mutation to an executed-behavior
mutation).
"""

from __future__ import annotations

from latte_harness.topology_cleanup import (
    TopologyCleanupDeps,
    perform_topology_cleanup,
)

_LIVE_PID = 4242


def _deps(
    *,
    restore_topology_ok: bool = True,
    stop_ok: bool = True,
    restore_config_ok: bool = True,
    pid_live: bool = False,
    start_ok: bool = True,
    steps: list[str],
) -> TopologyCleanupDeps:
    """Fakes that record the order they ran, mirroring the bash mock stubs."""

    def restore_topology() -> bool:
        steps.append("restore_topology")
        return restore_topology_ok

    def stop() -> bool:
        steps.append("stop")
        return stop_ok

    def restore_config() -> bool:
        steps.append("restore_config")
        return restore_config_ok

    def running_pid() -> int | None:
        steps.append("pid")
        return _LIVE_PID if pid_live else None

    def start() -> bool:
        steps.append("start")
        return start_ok

    return TopologyCleanupDeps(
        restore_output_topology=restore_topology,
        stop_dock=stop,
        restore_config=restore_config,
        running_dock_pid=running_pid,
        start_dock=start,
        warn=lambda _message: None,
    )


def test_success_preserves_failure_status_and_full_ordering() -> None:
    steps: list[str] = []
    status = perform_topology_cleanup(
        _deps(steps=steps),
        topology_captured=True,
        fixture_transaction_active=True,
        original_status=37,
    )
    # The body's failure status survives a fully successful cleanup (37, not 0):
    # cleanup never masks a failure with a cleanup success.
    assert status == 37
    assert steps == ["restore_topology", "stop", "restore_config", "pid", "start"]


def test_restart_failure_becomes_failure() -> None:
    steps: list[str] = []
    status = perform_topology_cleanup(
        _deps(start_ok=False, steps=steps),
        topology_captured=True,
        fixture_transaction_active=True,
        original_status=0,
    )
    # A dock that never restarts turns a would-be success into a failure.
    assert status == 1
    assert steps == ["restore_topology", "stop", "restore_config", "pid", "start"]


def test_live_dock_is_never_restarted() -> None:
    steps: list[str] = []
    status = perform_topology_cleanup(
        _deps(pid_live=True, steps=steps),
        topology_captured=True,
        fixture_transaction_active=True,
        original_status=0,
    )
    # A dock still alive after the stop is a failure, and the restart never fires
    # (the running_dock_pid safety predicate gates it), so no double dock.
    assert status == 1
    assert "start" not in steps
    assert steps == ["restore_topology", "stop", "restore_config", "pid"]


def test_topology_restore_failure_becomes_failure() -> None:
    steps: list[str] = []
    status = perform_topology_cleanup(
        _deps(restore_topology_ok=False, steps=steps),
        topology_captured=True,
        fixture_transaction_active=False,
        original_status=0,
    )
    # A failed OUTPUT-topology restore alone is a failure, even with no fixture
    # transaction to tear down.
    assert status == 1
    assert steps == ["restore_topology"]


def test_no_transaction_passes_body_status_through() -> None:
    steps: list[str] = []
    status = perform_topology_cleanup(
        _deps(steps=steps),
        topology_captured=False,
        fixture_transaction_active=False,
        original_status=0,
    )
    # Nothing was armed, so nothing runs and the body's status is returned as-is.
    assert status == 0
    assert steps == []


# ---- driven mutation controls ---------------------------------------------
#
# Each mutant violates one dimension of the contract; the assertions above would
# catch it. Driving the mutant here proves the guard is not vacuous.


def _mutant_masks_failure_status(
    deps: TopologyCleanupDeps,
    *,
    topology_captured: bool,
    fixture_transaction_active: bool,
    original_status: int,
) -> int:
    # BUG: runs the real teardown but returns the body status, masking a cleanup
    # failure with the body's success.
    _ = perform_topology_cleanup(
        deps,
        topology_captured=topology_captured,
        fixture_transaction_active=fixture_transaction_active,
        original_status=original_status,
    )
    return original_status


def _mutant_restarts_before_config_restore(deps: TopologyCleanupDeps) -> None:
    # BUG: restarts the dock before restoring the pristine config, so the
    # restarted dock reads the mutated three-panel fixture, not the pristine seed.
    _ = deps.restore_output_topology()
    _ = deps.stop_dock()
    _ = deps.running_dock_pid()
    _ = deps.start_dock()
    _ = deps.restore_config()


def test_mutation_control_status_masking_is_caught() -> None:
    real = perform_topology_cleanup(
        _deps(start_ok=False, steps=[]),
        topology_captured=True,
        fixture_transaction_active=True,
        original_status=0,
    )
    mutant = _mutant_masks_failure_status(
        _deps(start_ok=False, steps=[]),
        topology_captured=True,
        fixture_transaction_active=True,
        original_status=0,
    )
    # The real decision preserves the failure (1); the masking mutant returns 0.
    # The restart-failure case's `status == 1` assertion discriminates the two.
    assert real == 1
    assert mutant == 0
    assert real != mutant


def test_mutation_control_restart_before_restore_is_caught() -> None:
    real_steps: list[str] = []
    _ = perform_topology_cleanup(
        _deps(steps=real_steps),
        topology_captured=True,
        fixture_transaction_active=True,
        original_status=0,
    )
    mutant_steps: list[str] = []
    _mutant_restarts_before_config_restore(_deps(steps=mutant_steps))
    # The real decision restores the config before it restarts; the mutant
    # reverses that. The success case's step-ordering assertion discriminates.
    assert real_steps.index("restore_config") < real_steps.index("start")
    assert mutant_steps.index("restore_config") > mutant_steps.index("start")
