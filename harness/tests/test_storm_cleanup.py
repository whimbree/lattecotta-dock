# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Drive the FP-4C operation-storm cleanup decision in-process.

This is the redesign of the bash sourceguard cleanup-EVAL test
(SourceGuardTest::linkedOperationStormE2e_cleanupPreservesFailureAndSafety),
which extracted the shell ``cleanup()``/``restore_config_exactly()`` function
bodies and eval-executed them in a mock bash harness across three cases. A Python
function body is not eval-executable text, so the contract is pinned here by
executing the real decision (latte_harness.storm_cleanup) with mocked side
effects - a stronger proof than the bash reconstruction, since it drives the
actual code the recipe wires.

The three cases are byte-identical to the bash harness's
``run_cleanup_case`` invocations:

    success:         original 37, stop ok,   pid gone, restore ok   -> 37, stop/restore/start
    live-dock:       original  0, stop fails, pid live, restore ok   ->  1, stop
    partial-restore: original  0, stop ok,   pid gone, restore fails ->  1, stop/restore

plus the acceptance-status gate and two DRIVEN mutation controls: a broken
cleanup that masks the failure status and a broken cleanup that replaces the
config under a live dock. Each mutant violates the guarded pattern and the
assertions below catch it, so the guard demonstrably fails when the pattern is
violated (the mutation-control the bash test carried, relocated from a source
text-mutation to an executed-behavior mutation).
"""

from __future__ import annotations

from latte_harness.storm_cleanup import CleanupDeps, perform_cleanup_transaction

_LIVE_PID = 4242


def _deps(
    *,
    stop_ok: bool,
    pid_live: bool,
    restore_ok: bool,
    steps: list[str],
    dock_running: bool = False,
) -> CleanupDeps:
    """Fakes that record the order they ran, mirroring the bash mock stubs.

    ``preserve_dock_log`` is a no-op success (the bash mock left E2E_DOCK_LOG
    unset, so no log was copied); ``dock_is_running`` is False (the bash
    ``dock_is_running(){ return 1; }``), which skips the baseline re-verify;
    ``reverify_baseline`` records "reverify" so a wrongly-reached re-verify would
    show up in the step log.
    """

    def stop() -> bool:
        steps.append("stop")
        return stop_ok

    def restore() -> bool:
        steps.append("restore")
        return restore_ok

    def start() -> bool:
        steps.append("start")
        return True

    def reverify() -> bool:
        steps.append("reverify")
        return True

    return CleanupDeps(
        preserve_dock_log=lambda: True,
        stop_dock=stop,
        running_dock_pid=lambda: _LIVE_PID if pid_live else None,
        restore_config=restore,
        dock_is_running=lambda: dock_running,
        start_dock=start,
        reverify_baseline=reverify,
        warn=lambda _message: None,
    )


def test_success_preserves_failure_status_and_full_ordering() -> None:
    steps: list[str] = []
    status = perform_cleanup_transaction(
        _deps(stop_ok=True, pid_live=False, restore_ok=True, steps=steps),
        transaction_started=True,
        backup_ready=True,
        acceptance_completed=True,
        original_status=37,
    )
    # The body's failure status survives a fully successful cleanup (37, not 0):
    # cleanup never masks a failure with a cleanup success.
    assert status == 37
    assert steps == ["stop", "restore", "start"]


def test_live_dock_refuses_config_replacement() -> None:
    steps: list[str] = []
    status = perform_cleanup_transaction(
        _deps(stop_ok=False, pid_live=True, restore_ok=True, steps=steps),
        transaction_started=True,
        backup_ready=True,
        acceptance_completed=True,
        original_status=0,
    )
    # The dock never stopped, so the config is NOT replaced (no restore) and the
    # would-be success turns into a failure.
    assert status == 1
    assert steps == ["stop"]


def test_partial_restore_never_starts() -> None:
    steps: list[str] = []
    status = perform_cleanup_transaction(
        _deps(stop_ok=True, pid_live=False, restore_ok=False, steps=steps),
        transaction_started=True,
        backup_ready=True,
        acceptance_completed=True,
        original_status=0,
    )
    # A restore that did not verify never leads to a restart against a partial
    # config, and the would-be success turns into a failure.
    assert status == 1
    assert steps == ["stop", "restore"]


def test_incomplete_acceptance_becomes_failure() -> None:
    steps: list[str] = []
    status = perform_cleanup_transaction(
        _deps(stop_ok=True, pid_live=False, restore_ok=True, steps=steps),
        transaction_started=False,
        backup_ready=False,
        acceptance_completed=False,
        original_status=0,
    )
    # A body that returned 0 without completing its acceptance is a failure, even
    # with no transaction to tear down.
    assert status == 1
    assert steps == []


# ---- driven mutation controls ---------------------------------------------
#
# Each mutant violates one dimension of the contract; the assertions above would
# catch it. Driving the mutant here proves the guard is not vacuous.


def _mutant_masks_failure_status(
    deps: CleanupDeps,
    *,
    transaction_started: bool,
    backup_ready: bool,
    acceptance_completed: bool,
    original_status: int,
) -> int:
    # BUG: runs the real teardown but returns the body status, masking a cleanup
    # failure with the body's success.
    _ = perform_cleanup_transaction(
        deps,
        transaction_started=transaction_started,
        backup_ready=backup_ready,
        acceptance_completed=acceptance_completed,
        original_status=original_status,
    )
    return original_status


def _mutant_replaces_config_under_live_dock(deps: CleanupDeps) -> None:
    # BUG: drops the dock-stopped gate and restores the config unconditionally.
    _ = deps.stop_dock()
    _ = deps.running_dock_pid()
    _ = deps.restore_config()


def test_mutation_control_status_masking_is_caught() -> None:
    steps: list[str] = []
    inputs = _deps(stop_ok=True, pid_live=False, restore_ok=False, steps=steps)
    real = perform_cleanup_transaction(
        inputs,
        transaction_started=True,
        backup_ready=True,
        acceptance_completed=True,
        original_status=0,
    )
    mutant = _mutant_masks_failure_status(
        _deps(stop_ok=True, pid_live=False, restore_ok=False, steps=[]),
        transaction_started=True,
        backup_ready=True,
        acceptance_completed=True,
        original_status=0,
    )
    # The real decision preserves the failure (1); the masking mutant returns 0.
    # The partial-restore case's `status == 1` assertion discriminates the two.
    assert real == 1
    assert mutant == 0
    assert real != mutant


def test_mutation_control_live_dock_replacement_is_caught() -> None:
    real_steps: list[str] = []
    _ = perform_cleanup_transaction(
        _deps(stop_ok=False, pid_live=True, restore_ok=True, steps=real_steps),
        transaction_started=True,
        backup_ready=True,
        acceptance_completed=True,
        original_status=0,
    )
    mutant_steps: list[str] = []
    _mutant_replaces_config_under_live_dock(
        _deps(stop_ok=False, pid_live=True, restore_ok=True, steps=mutant_steps)
    )
    # The real decision never restores under a live dock; the mutant does. The
    # live-dock case's `steps == ["stop"]` assertion discriminates the two.
    assert "restore" not in real_steps
    assert "restore" in mutant_steps
