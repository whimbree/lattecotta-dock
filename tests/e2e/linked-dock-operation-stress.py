#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""FP-4C deterministic linked-dock operation storm. The Python is only the
private-session transaction and D-Bus adapter. The typed plan, resolved
identities and every semantic state verdict live in operation_model.py, which is
still driven as a subprocess (identical ``python3 <model> <subcommand>`` argv and
stdin/stdout as the bash), so the model half of the contract is untouched.

Ported from tests/e2e/linked-dock-operation-stress.sh to latte_harness.recipe
(BP-3, the bash-to-python migration's deferred FP-4C stress recipe). Every poll
bound, iteration count, expected status, marker and failure message is
byte-identical; the storm is NOT trimmed for speed. dockSystemData /
viewMoveTransactionsData / layoutsData carry fields the typed models do not, so
they are read as raw JSON at the same boundary the bash python one-liners used; a
refused or malformed reply during a poll reads as a non-match, exactly like the
bash predicate exiting non-zero.

The cleanup safety net (cleanup runs on every exit path, preserves the body's
failure status, never masks it with a cleanup success, and enforces the teardown
ordering - stop the dock before replacing config, restore fully before restart)
is factored into the pure, importable ``perform_cleanup_transaction`` decision
core so it can be driven in-process with mocks. The behavioral proof lives in
harness/tests/test_storm_cleanup.py (the redesign of the bash
cleanup-EVAL sourceguard test, which eval-executed the shell function bodies in a
mock harness and has no direct Python analog); the transactional-replay structure
is still pinned by sourceguardtest's matchesLinkedOperationStormE2eContract,
retargeted to this recipe.
"""

from __future__ import annotations

import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from typing import Any

from latte_harness import proc, recipe
from latte_harness.storm_cleanup import CleanupDeps, perform_cleanup_transaction


def _warn(message: str) -> None:
    print(message, file=sys.stderr, flush=True)


E2E_REPO = os.environ["E2E_REPO"]
MODEL = f"{E2E_REPO}/tests/e2e/fixtures/fp4c/operation_model.py"
FIXTURE_GENERATOR = f"{E2E_REPO}/harness/src/latte_harness/matrix_fixture.py"
_REQUESTED_SEED = os.environ.get("LATTE_LINKED_STRESS_SEED", "127934575")
_SUPPLIED_PLAN = os.environ.get("LATTE_LINKED_STRESS_PLAN", "")


@dataclass
class _State:
    """The recipe's mutable transaction state (the bash top-of-file globals)."""

    acceptance_completed: bool = False
    backup_ready: bool = False
    transaction_started: bool = False
    rt: str = ""
    config_home: str = ""
    layout: str = ""
    transaction_dir: str = ""
    backup_dir: str = ""
    fixture_dir: str = ""
    artifact_dir: str = ""
    plan_file: str = ""
    operations_file: str = ""
    replay_file: str = ""
    bindings_file: str = ""
    outputs_file: str = ""
    layouts_file: str = ""
    baseline_snapshot_file: str = ""
    baseline_projection_file: str = ""
    candidate_plan: str = ""


_S = _State()


# ---- model / config / D-Bus adapters --------------------------------------


def _model(
    subcommand: str, *args: str, stdin: str | None = None, quiet: bool = False
) -> subprocess.CompletedProcess[str]:
    """Drive operation_model.py as a subprocess (the bash ``python3 "$MODEL"``).

    Identical argv, stdin and stdout to the shell; ``quiet`` is the bash
    ``2>/dev/null`` the polling probes used to hide the model's not-yet verdicts.
    """
    result = subprocess.run(
        ["python3", MODEL, subcommand, *args],
        input=stdin,
        capture_output=True,
        text=True,
        check=False,
    )
    if not quiet and result.stderr:
        sys.stderr.write(result.stderr)
    return result


def _kwriteconfig(*args: str) -> bool:
    return recipe.kwriteconfig(*args) == 0


def _kwrite_or_fail(fail_message: str, *args: str) -> None:
    if not _kwriteconfig(*args):
        recipe.fail(fail_message)


def _snapshot() -> str:
    """dockSystemData as plain JSON text (the bash ``snapshot``)."""
    return recipe.json_payload("dockSystemData")


def _load(path: str) -> Any:
    with open(path, encoding="utf-8") as stream:
        return json.load(stream)


def _compact(value: Any) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


# ---- dock lifecycle predicates (the bash helpers) --------------------------


def _dock_is_running() -> bool:
    pid = recipe.dock_pid()
    return pid is not None and recipe.pid_alive(pid)


def _stop_dock_if_running() -> bool:
    if _dock_is_running():
        return recipe.dock_stop()
    return True


def _path_is_within(child: str, parent: str) -> bool:
    child_real = os.path.realpath(child)
    parent_real = os.path.realpath(parent)
    return child_real.startswith(parent_real + "/")


# ---- durable-move readback and lifecycle (raw JSON boundary) ---------------


def _assert_no_pending_view_move(destination: str) -> bool:
    """viewMoveTransactionsData must carry the exact schema-2 fields and hold no
    pending transaction (the bash inline python, verbatim)."""
    payload = recipe.json_payload("viewMoveTransactionsData")
    with open(destination, "w", encoding="utf-8") as stream:
        stream.write(payload)
    try:
        state = json.loads(payload)
    except json.JSONDecodeError:
        return False
    expected = {
        "schemaVersion",
        "journalCreatedGeneration",
        "commitDecisionGeneration",
        "journalRetiredGeneration",
        "transactions",
    }
    if set(state) != expected:
        print("durable move readback has missing or surplus fields", file=sys.stderr, flush=True)
        return False
    if state["schemaVersion"] != 2:
        print("durable move readback schema changed", file=sys.stderr, flush=True)
        return False
    if state["transactions"] != []:
        print(
            "operation checkpoint retained a pending durable move: "
            + json.dumps(state["transactions"], sort_keys=True),
            file=sys.stderr,
            flush=True,
        )
        return False
    return True


def _assert_view_move_lifecycle(step_file: str, before_file: str, after_file: str) -> bool:
    """Feed the model {step, before, after} deltas through assert-view-move-lifecycle."""
    payload = _compact(
        {"step": _load(step_file), "before": _load(before_file), "after": _load(after_file)}
    )
    return _model("assert-view-move-lifecycle", stdin=payload).returncode == 0


# ---- snapshot capture / model-input builders (the bash heredocs) -----------


def _capture_snapshot(destination: str) -> bool:
    candidate = f"{destination}.next"
    with open(candidate, "w", encoding="utf-8") as stream:
        stream.write(_snapshot())
    if os.path.getsize(candidate) > 0:
        os.replace(candidate, destination)
        return True
    os.remove(candidate)
    return False


def _build_resolve_input(step_file: str) -> str:
    return _compact(
        {
            "step": _load(step_file),
            "bindings": _load(_S.bindings_file),
            "outputs": _load(_S.outputs_file),
            "layouts": _load(_S.layouts_file),
        }
    )


def _build_result_input(step_file: str, before_file: str, after_file: str) -> str:
    return _compact(
        {
            "step": _load(step_file),
            "bindings": _load(_S.bindings_file),
            "before": _load(before_file),
            "after": _load(after_file),
        }
    )


def _build_replay_header_input() -> str:
    return _compact(
        {
            "plan": _load(_S.plan_file),
            "bindings": _load(_S.bindings_file),
            "outputs": _load(_S.outputs_file),
            "layouts": _load(_S.layouts_file),
        }
    )


def _build_checkpoint_input(through: int, snapshot_file: str) -> str:
    return _compact(
        {
            "plan": _load(_S.plan_file),
            "through": int(through),
            "bindings": _load(_S.bindings_file),
            "outputs": _load(_S.outputs_file),
            "layouts": _load(_S.layouts_file),
            "snapshot": _load(snapshot_file),
        }
    )


def _build_edit_input(snapshot_file: str, target: str, editing: bool, configuring: bool) -> str:
    return _compact(
        {
            "snapshot": _load(snapshot_file),
            "bindings": _load(_S.bindings_file),
            "target": target,
            "editing": editing,
            "configuring": configuring,
        }
    )


def _build_reload_input(before_file: str, after_file: str, resolved_file: str) -> str:
    return _compact(
        {
            "before": _load(before_file),
            "after": _load(after_file),
            "bindings": _load(_S.bindings_file),
            "affected": _load(resolved_file)["action"]["affected"],
        }
    )


def _write_json_field(source_file: str, field_path: str, destination: str) -> None:
    value = _load(source_file)
    for component in field_path.split("."):
        value = value[component]
    with open(destination, "w", encoding="utf-8") as stream:
        stream.write(_compact(value))
        stream.write("\n")


def _append_record_field(source_file: str, field_path: str) -> None:
    value = _load(source_file)
    for component in field_path.split("."):
        value = value[component]
    with open(_S.replay_file, "a", encoding="utf-8") as stream:
        stream.write(_compact(value))
        stream.write("\n")


def _scalar(value: Any) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    return str(value)


def _action_arguments(resolved: dict[str, Any]) -> list[str]:
    result: list[str] = []
    for value in resolved["action"].get("args", []):
        if isinstance(value, bool):
            result.append("true" if value else "false")
        elif isinstance(value, (str, int)):
            result.append(str(value))
        else:
            recipe.fail(f"D-Bus argument is not a shell scalar: {value!r}")
    return result


# ---- private-session guard -------------------------------------------------


def _require_private_nested_session() -> None:
    if os.environ.get("E2E_MODE") != "nested":
        recipe.fail("FP-4C operation stress is nested-only")
    if int(os.environ.get("E2E_OUTPUT_COUNT", "0")) != 2:
        recipe.fail("FP-4C operation stress requires exactly two nested outputs")
    rt = os.environ.get("E2E_RT", "")
    if not rt or not os.path.isdir(rt):
        recipe.fail("FP-4C has no private nested runtime directory")
    runtime_real = os.path.realpath(rt)
    if not os.path.exists(runtime_real):
        recipe.fail("FP-4C could not resolve its nested runtime directory")
    if os.path.realpath(os.environ.get("XDG_RUNTIME_DIR", "/missing")) != runtime_real:
        recipe.fail("FP-4C XDG_RUNTIME_DIR is not the nested runtime")
    config_home = os.environ.get("E2E_CONFIG_HOME", "/missing")
    if not os.path.exists(config_home):
        recipe.fail("FP-4C could not resolve its configuration home")
    config_real = os.path.realpath(config_home)
    if not config_real.startswith(runtime_real + "/"):
        recipe.fail("FP-4C configuration home is not private to the nested runtime")
    if not _path_is_within(os.environ["E2E_LAYOUT"], config_home):
        recipe.fail("FP-4C layout is outside the private configuration home")
    if not _path_is_within(os.environ["E2E_DOCK_PIDFILE"], rt):
        recipe.fail("FP-4C dock pid file is outside the nested runtime")
    if not _path_is_within(os.environ["E2E_DOCK_LOG"], rt):
        recipe.fail("FP-4C dock log is outside the nested runtime")
    wayland_socket = os.path.join(rt, os.environ["WAYLAND_DISPLAY"])
    if not _is_socket(wayland_socket):
        recipe.fail("FP-4C Wayland socket is not in the nested runtime")
    bus_file = os.path.join(rt, "bus-address")
    bus_ok = os.path.isfile(bus_file) and _read(bus_file) == os.environ.get(
        "DBUS_SESSION_BUS_ADDRESS", ""
    )
    if not bus_ok:
        recipe.fail("FP-4C D-Bus address is not the nested session bus")
    if not (os.path.isfile(MODEL) and os.path.isfile(FIXTURE_GENERATOR)):
        recipe.fail("FP-4C model or fixture generator is missing")


def _is_socket(path: str) -> bool:
    try:
        return stat.S_ISSOCK(os.stat(path).st_mode)
    except OSError:
        return False


def _read(path: str) -> str:
    with open(path, encoding="utf-8") as stream:
        return stream.read().rstrip("\n")


def _read_or_empty(path: str) -> str:
    """A tolerant read: the bash ``<"$file"`` that leaves stdin empty (and the
    model failing) when the snapshot was never captured."""
    try:
        with open(path, encoding="utf-8") as stream:
            return stream.read().rstrip("\n")
    except OSError:
        return ""


def _capture_layer3_latte_windows(destination: str) -> bool:
    rows = recipe.kwin_js(
        "for (const window of workspace.windowList()) {\n"
        "        if (String(window.resourceClass) === 'latte-dock' && window.layer === 3) {\n"
        "            print('@TAG@|' + JSON.stringify({\n"
        "                id: String(window.internalId),\n"
        "                caption: String(window.caption),\n"
        "                geometry: [\n"
        "                    Math.round(window.frameGeometry.x),\n"
        "                    Math.round(window.frameGeometry.y),\n"
        "                    Math.round(window.frameGeometry.width),\n"
        "                    Math.round(window.frameGeometry.height)\n"
        "                ],\n"
        "                output: window.output ? window.output.name : null\n"
        "            }));\n"
        "        }\n"
        "    }",
        0.05,
    )
    records = [json.loads(line) for line in rows.splitlines() if line]
    with open(destination, "w", encoding="utf-8") as stream:
        stream.write(_compact(records))
    return True


def _assert_visual_window_ownership(
    snapshot_file: str, windows_file: str, output_file: str
) -> bool:
    """Feed {snapshot, outputs, windows} through assert-visual-window-ownership."""
    payload = _compact(
        {
            "snapshot": _load(snapshot_file),
            "outputs": _load(output_file),
            "windows": _load(windows_file),
        }
    )
    return _model("assert-visual-window-ownership", stdin=payload).returncode == 0


def _assert_tombstone_on_disk(persistent_id: str) -> bool:
    dock_id = int(persistent_id)
    prefix = f"[Containments][{dock_id}]"
    with open(_S.layout, encoding="utf-8") as stream:
        groups = [
            line.strip()
            for line in stream
            if line.lstrip().startswith("[") and line.rstrip().endswith("]")
        ]
    survivors = [group for group in groups if group == prefix or group.startswith(prefix + "[")]
    if survivors:
        print(
            "removed containment subtree survived the immediate tombstone: "
            + ", ".join(survivors[:8]),
            file=sys.stderr,
            flush=True,
        )
        return False
    return True


def _assert_latest_intent_probe(before_file: str, after_file: str) -> bool:
    plan = _load(_S.plan_file)
    bindings = _load(_S.bindings_file)
    before = _load(before_file)
    after = _load(after_file)
    target = bindings[plan["latestIntentProbe"]["target"]]
    before_view = next(v for v in before["views"] if v["persistentDockId"] == target)
    after_view = next(v for v in after["views"] if v["persistentDockId"] == target)
    stable_placement = ("screenId", "edge", "alignment", "onPrimary")
    if any(before_view[key] != after_view[key] for key in stable_placement):
        print("latest-intent probe did not return to its exact origin", file=sys.stderr, flush=True)
        return False
    before_generation = int(before_view["relocationGeneration"])
    after_generation = int(after_view["relocationGeneration"])
    if after_generation != before_generation + 2:
        print(
            "latest-intent probe did not claim exactly two generations: "
            f"{before_generation} -> {after_generation}",
            file=sys.stderr,
            flush=True,
        )
        return False
    if (
        not after_view["geometrySettled"]
        or after_view["relocationGeneration"] != after_view["appliedRelocationGeneration"]
    ):
        print(
            "latest-intent probe did not settle its newest generation", file=sys.stderr, flush=True
        )
        return False
    return True


# ---- bounded wait loops (byte-identical bounds and sleeps) ------------------


def _wait_for_checkpoint(through: int, current_file: str, attempts: int = 240) -> bool:
    for _ in range(attempts):
        if not _dock_is_running():
            print(
                f"FAIL: fixture dock exited before FP-4C checkpoint {through}",
                file=sys.stderr,
                flush=True,
            )
            return False
        _capture_snapshot(current_file)
        if os.path.exists(current_file) and os.path.getsize(current_file) > 0:
            payload = _build_checkpoint_input(through, current_file)
            if _model("assert-checkpoint", stdin=payload, quiet=True).returncode == 0:
                return True
        time.sleep(0.25)
    payload = _build_checkpoint_input(through, current_file)
    return _model("assert-checkpoint", stdin=payload).returncode == 0


def _wait_for_quiescent_projection(output_file: str, snapshot_file: str) -> bool:
    previous = ""
    current = ""
    repeats = 0
    for _ in range(240):
        if not _dock_is_running():
            print(
                "FAIL: fixture dock exited before its state quiesced", file=sys.stderr, flush=True
            )
            return False
        _capture_snapshot(snapshot_file)
        result = _model("quiescent-projection", stdin=_read_or_empty(snapshot_file), quiet=True)
        current = result.stdout.rstrip("\n") if result.returncode == 0 else ""
        if current and current == previous:
            repeats += 1
            if repeats >= 2:
                with open(output_file, "w", encoding="utf-8") as stream:
                    stream.write(current + "\n")
                return True
        else:
            repeats = 0
        previous = current
        time.sleep(0.25)
    return _model("quiescent-projection", stdin=_read_or_empty(snapshot_file)).returncode == 0


def _wait_for_bound_result(
    step_file: str, before_file: str, after_file: str, result_file: str
) -> bool:
    for _ in range(240):
        if not _dock_is_running():
            print(
                "FAIL: fixture dock exited before an operation result bound",
                file=sys.stderr,
                flush=True,
            )
            return False
        _capture_snapshot(after_file)
        if os.path.exists(after_file) and os.path.getsize(after_file) > 0:
            payload = _build_result_input(step_file, before_file, after_file)
            bound = _model("bind-result", stdin=payload, quiet=True)
            if bound.returncode == 0:
                with open(result_file, "w", encoding="utf-8") as stream:
                    stream.write(bound.stdout)
                _write_json_field(result_file, "bindings", _S.bindings_file)
                _append_record_field(result_file, "record")
                return True
        time.sleep(0.25)
    payload = _build_result_input(step_file, before_file, after_file)
    return _model("bind-result", stdin=payload).returncode == 0


def _wait_for_edit_outcome(
    target: str, editing: bool, configuring: bool, current_file: str
) -> bool:
    for _ in range(240):
        if not _dock_is_running():
            print(
                "FAIL: fixture dock exited during an edit transition", file=sys.stderr, flush=True
            )
            return False
        _capture_snapshot(current_file)
        if os.path.exists(current_file) and os.path.getsize(current_file) > 0:
            payload = _build_edit_input(current_file, target, editing, configuring)
            if _model("assert-edit", stdin=payload, quiet=True).returncode == 0:
                return True
        time.sleep(0.25)
    payload = _build_edit_input(current_file, target, editing, configuring)
    return _model("assert-edit", stdin=payload).returncode == 0


def _wait_for_runtime_reload(before_file: str, after_file: str, resolved_file: str) -> bool:
    for _ in range(240):
        if not _dock_is_running():
            print("FAIL: fixture dock exited during runtime reload", file=sys.stderr, flush=True)
            return False
        _capture_snapshot(after_file)
        if os.path.exists(after_file) and os.path.getsize(after_file) > 0:
            payload = _build_reload_input(before_file, after_file, resolved_file)
            if _model("assert-runtime-reload", stdin=payload, quiet=True).returncode == 0:
                return True
        time.sleep(0.25)
    payload = _build_reload_input(before_file, after_file, resolved_file)
    return _model("assert-runtime-reload", stdin=payload).returncode == 0


def _wait_for_visual_window_ownership(through: int, snapshot_file: str, windows_file: str) -> bool:
    for _ in range(120):
        if not _dock_is_running():
            print(
                "FAIL: fixture dock exited before visual ownership settled",
                file=sys.stderr,
                flush=True,
            )
            return False
        _capture_snapshot(snapshot_file)
        if os.path.exists(snapshot_file) and os.path.getsize(snapshot_file) > 0:
            payload = _build_checkpoint_input(through, snapshot_file)
            if (
                _model("assert-checkpoint", stdin=payload, quiet=True).returncode == 0
                and _capture_layer3_latte_windows(windows_file)
                and _assert_visual_window_ownership(snapshot_file, windows_file, _S.outputs_file)
            ):
                return True
        time.sleep(0.25)
    payload = _build_checkpoint_input(through, snapshot_file)
    if _model("assert-checkpoint", stdin=payload).returncode != 0:
        return False
    if not _capture_layer3_latte_windows(windows_file):
        return False
    return _assert_visual_window_ownership(snapshot_file, windows_file, _S.outputs_file)


# ---- config restore + cleanup (the safety net) -----------------------------


def _restore_config_exactly() -> bool:
    """Recursively replace the config home with the pristine backup and verify.

    The bash restore_config_exactly: refuse without a backup, refuse if either
    path escapes the nested runtime, then rm-rf + recreate + cp-a + diff-qr.
    """
    if not _S.backup_ready:
        return False
    if not _path_is_within(_S.config_home, _S.rt):
        return False
    if not _path_is_within(_S.backup_dir, _S.rt):
        return False
    shutil.rmtree(_S.config_home, ignore_errors=True)
    os.makedirs(_S.config_home, exist_ok=True)
    try:
        _ = shutil.copytree(_S.backup_dir, _S.config_home, dirs_exist_ok=True)
    except OSError:
        return False
    return _diff_identical(_S.backup_dir, _S.config_home)


def _diff_identical(left: str, right: str) -> bool:
    return (
        subprocess.run(
            ["diff", "-qr", "--no-dereference", left, right],
            stdout=subprocess.DEVNULL,
            check=False,
        ).returncode
        == 0
    )


def _reverify_restored_baseline() -> bool:
    """The four-step cleanup re-verify: capture, assert-baseline, project, compare.

    Short-circuits at the first failing step with its bash diagnostic (the bash
    elif chain), returning False; True only when the restored runtime baseline
    projects byte-identically to the pristine one.
    """
    baseline = f"{_S.artifact_dir}/cleanup-baseline.json"
    projection = f"{_S.artifact_dir}/cleanup-baseline.projection.json"
    payload = _snapshot()
    if not payload:
        print(
            "FAIL: FP-4C cleanup could not capture the restored runtime baseline",
            file=sys.stderr,
            flush=True,
        )
        return False
    with open(baseline, "w", encoding="utf-8") as stream:
        stream.write(payload)
    if _model("assert-baseline", stdin=_read(baseline)).returncode != 0:
        print(
            "FAIL: FP-4C cleanup did not restore the pristine runtime baseline",
            file=sys.stderr,
            flush=True,
        )
        return False
    projected = _model("durable-projection", stdin=_read(baseline))
    if projected.returncode != 0:
        print(
            "FAIL: FP-4C cleanup could not project the restored runtime baseline",
            file=sys.stderr,
            flush=True,
        )
        return False
    with open(projection, "w", encoding="utf-8") as stream:
        stream.write(projected.stdout)
    if _read(projection) != _read(_S.baseline_projection_file):
        print(
            "FAIL: FP-4C cleanup runtime baseline differs after exact config restore",
            file=sys.stderr,
            flush=True,
        )
        return False
    return True


def _running_dock_pid() -> int | None:
    """The recorded pid iff it is still alive (the bash e2e_dock_pid + kill -0)."""
    pid = recipe.dock_pid()
    if pid is not None and recipe.pid_alive(pid):
        return pid
    return None


def _preserve_dock_log() -> bool:
    """cp the fixture dock log into the artifacts; True on success or when there
    is nothing to preserve (no artifact dir or no log yet)."""
    log = os.environ.get("E2E_DOCK_LOG", "")
    if not _S.artifact_dir or not os.path.isfile(log):
        return True
    try:
        shutil.copy(log, f"{_S.artifact_dir}/fixture-dock.log")
    except OSError:
        return False
    return True


def _live_cleanup_deps() -> CleanupDeps:
    """Wire the pure cleanup core to the real dock/config side effects."""
    return CleanupDeps(
        preserve_dock_log=_preserve_dock_log,
        stop_dock=_stop_dock_if_running,
        running_dock_pid=_running_dock_pid,
        restore_config=_restore_config_exactly,
        dock_is_running=_dock_is_running,
        start_dock=lambda: recipe.dock_start(90),
        reverify_baseline=_reverify_restored_baseline,
        warn=_warn,
    )


def _cleanup(original_status: int) -> int:
    return perform_cleanup_transaction(
        _live_cleanup_deps(),
        transaction_started=_S.transaction_started,
        backup_ready=_S.backup_ready,
        acceptance_completed=_S.acceptance_completed,
        original_status=original_status,
    )


# ---- the storm body --------------------------------------------------------


def _body() -> None:
    _require_private_nested_session()

    _S.rt = os.environ["E2E_RT"]
    _S.config_home = os.environ["E2E_CONFIG_HOME"]
    _S.layout = os.environ["E2E_LAYOUT"]

    fd, _S.candidate_plan = tempfile.mkstemp(prefix="fp4c-operation-plan.", dir=_S.rt)
    os.close(fd)
    if _SUPPLIED_PLAN:
        plan_source = os.path.realpath(_SUPPLIED_PLAN)
        if not os.path.exists(_SUPPLIED_PLAN):
            recipe.fail("LATTE_LINKED_STRESS_PLAN does not resolve to a readable plan")
        if not (os.path.isfile(plan_source) and os.access(plan_source, os.R_OK)):
            recipe.fail("LATTE_LINKED_STRESS_PLAN is not a readable regular file")
        if _model("validate-plan", stdin=_read(plan_source)).returncode != 0:
            recipe.fail("the supplied FP-4C operation plan is invalid")
        shutil.copyfile(plan_source, _S.candidate_plan)
        if _model("validate-plan", stdin=_read(_S.candidate_plan)).returncode != 0:
            recipe.fail("the private copy of the supplied FP-4C plan is invalid")
    else:
        generated = _model("generate-plan", "--seed", _REQUESTED_SEED)
        if generated.returncode != 0:
            recipe.fail("could not generate the typed FP-4C operation plan")
        with open(_S.candidate_plan, "w", encoding="utf-8") as stream:
            stream.write(generated.stdout)
        if _model("validate-plan", stdin=_read(_S.candidate_plan)).returncode != 0:
            recipe.fail("the generated FP-4C operation plan failed its own validator")
    stress_seed = str(_load(_S.candidate_plan)["seed"])
    if not stress_seed.isdigit():
        recipe.fail("the validated FP-4C plan seed is not an unsigned integer")

    _S.artifact_dir = tempfile.mkdtemp(
        prefix=f"linked-dock-operation-stress.seed-{stress_seed}.run-",
        dir=os.environ["E2E_ARTIFACTS"],
    )
    _S.transaction_dir = tempfile.mkdtemp(prefix="fp4c-operation-stress.", dir=_S.rt)
    if not _path_is_within(_S.transaction_dir, _S.rt):
        recipe.fail("FP-4C transaction directory escaped the nested runtime")
    _S.backup_dir = f"{_S.transaction_dir}/pristine-config"
    _S.fixture_dir = f"{_S.transaction_dir}/panel-fixture"
    _S.plan_file = f"{_S.artifact_dir}/plan.json"
    _S.operations_file = f"{_S.artifact_dir}/operations.jsonl"
    _S.replay_file = f"{_S.artifact_dir}/replay.jsonl"
    _S.bindings_file = f"{_S.transaction_dir}/bindings.json"
    _S.outputs_file = f"{_S.transaction_dir}/outputs.json"
    _S.layouts_file = f"{_S.transaction_dir}/layouts.json"
    _S.baseline_snapshot_file = f"{_S.artifact_dir}/pristine-baseline.json"
    _S.baseline_projection_file = f"{_S.artifact_dir}/pristine-baseline.projection.json"
    shutil.copyfile(_S.candidate_plan, _S.plan_file)
    if _read(_S.candidate_plan) != _read(_S.plan_file):
        recipe.fail("the artifact FP-4C plan differs from its validated input")

    latest_intent_final_seq = str(_load(_S.plan_file)["latestIntentProbe"]["finalSeq"])
    if not (latest_intent_final_seq.isdigit() and int(latest_intent_final_seq) >= 1):
        recipe.fail("the latest-intent probe sequence is malformed")

    # No command that can stop the pristine dock or replace its configuration
    # runs outside the cleanup transaction armed here.
    proc.install_conventional_signal_exits()  # trap 'exit 130' INT / 'exit 143' TERM
    _S.transaction_started = True  # trap cleanup EXIT

    baseline_payload = _snapshot()
    if not baseline_payload:
        recipe.fail("could not capture the pristine FP-4C baseline")
    with open(_S.baseline_snapshot_file, "w", encoding="utf-8") as stream:
        stream.write(baseline_payload)
    if _model("assert-baseline", stdin=_read(_S.baseline_snapshot_file)).returncode != 0:
        recipe.fail("the starting nested configuration is not the pristine one-view baseline")
    projected = _model("durable-projection", stdin=_read(_S.baseline_snapshot_file))
    if projected.returncode != 0:
        recipe.fail("could not project the pristine FP-4C cleanup baseline")
    with open(_S.baseline_projection_file, "w", encoding="utf-8") as stream:
        stream.write(projected.stdout)

    if not _stop_dock_if_running():
        recipe.fail("could not stop the pristine dock before its configuration backup")
    os.makedirs(_S.backup_dir, exist_ok=True)
    _ = shutil.copytree(_S.config_home, _S.backup_dir, dirs_exist_ok=True)
    if not _diff_identical(_S.config_home, _S.backup_dir):
        recipe.fail("the pristine whole-configuration backup differs after copy")
    _S.backup_ready = True

    fixture = subprocess.run(
        [
            "uv",
            "run",
            "--locked",
            "--project",
            f"{E2E_REPO}/harness",
            "python",
            "-m",
            "latte_harness.matrix_fixture",
            "--seed-dir",
            _S.backup_dir,
            "--out-dir",
            _S.fixture_dir,
            "--view-type",
            "panel",
            "--edge",
            "bottom",
            "--alignment",
            "justify",
            "--display",
            "1out",
            "--cell",
            "fp4c-partial-floating-panel",
        ],
        stdout=subprocess.DEVNULL,
        check=False,
    )
    if fixture.returncode != 0:
        recipe.fail("could not generate the FP-4C panel fixture from the fresh backup")

    shutil.rmtree(_S.config_home, ignore_errors=True)
    os.makedirs(_S.config_home, exist_ok=True)
    _ = shutil.copytree(_S.fixture_dir, _S.config_home, dirs_exist_ok=True)
    if not recipe.dock_start(90):
        recipe.fail("the generated FP-4C panel fixture did not start")

    initial_snapshot = f"{_S.artifact_dir}/generated-panel.json"
    payload = _snapshot()
    if not payload:
        recipe.fail("could not capture the generated FP-4C panel")
    with open(initial_snapshot, "w", encoding="utf-8") as stream:
        stream.write(payload)
    if _model("assert-baseline", stdin=_read(initial_snapshot)).returncode != 0:
        recipe.fail("the generated FP-4C fixture is not exactly one independent view")
    root_id = str(_load(initial_snapshot)["views"][0]["persistentDockId"])
    if not (root_id.isdigit() and int(root_id) >= 1):
        recipe.fail("the validated FP-4C root identity is malformed")

    if not recipe.dock_stop():
        recipe.fail("could not stop the generated panel before deterministic configuration")
    panel_group = [
        "--file",
        _S.layout,
        "--group",
        "Containments",
        "--group",
        root_id,
        "--group",
        "General",
    ]
    _kwrite_or_fail(
        "could not set the FP-4C panel minimum length", *panel_group, "--key", "minLength", "45"
    )
    _kwrite_or_fail(
        "could not set the FP-4C panel maximum length", *panel_group, "--key", "maxLength", "45"
    )
    _kwrite_or_fail(
        "could not pin the FP-4C panel length",
        *panel_group,
        "--key",
        "maximizeWhenMaximized",
        "false",
    )
    _kwrite_or_fail(
        "could not enable FP-4C window-touch attachment",
        *panel_group,
        "--key",
        "hideFloatingGapForMaximized",
        "true",
    )
    _kwrite_or_fail(
        "could not disable FP-4C pointer deferral",
        *panel_group,
        "--key",
        "floatingGapHidingWaitsMouse",
        "false",
    )
    _kwrite_or_fail(
        "could not set the positive FP-4C floating gap",
        *panel_group,
        "--key",
        "screenEdgeMargin",
        "18",
    )
    _kwrite_or_fail(
        "could not retain the panel-owned FP-4C gap",
        *panel_group,
        "--key",
        "floatingInternalGapIsForced",
        "false",
    )
    _kwrite_or_fail(
        "could not disable FP-4C parabolic zoom", *panel_group, "--key", "zoomLevel", "0"
    )
    _kwrite_or_fail(
        "could not retain FP-4C theme-panel behavior",
        *panel_group,
        "--key",
        "useThemePanel",
        "true",
    )
    _kwrite_or_fail(
        "could not retain the FP-4C panel background thickness",
        *panel_group,
        "--key",
        "panelSize",
        "100",
    )

    origin_layout_name = os.path.basename(_S.layout).removesuffix(".layout.latte")
    destination_layout_name = "FP4C Destination"
    layouts_directory = os.path.dirname(_S.layout)
    destination_layout = f"{layouts_directory}/{destination_layout_name}.layout.latte"
    hidden_layout = f"{layouts_directory}/.multiple-layouts_hidden.layout.latte"
    shutil.copyfile(
        f"{E2E_REPO}/shell/package/contents/templates/Empty.layout.latte", destination_layout
    )
    shutil.copyfile(
        f"{E2E_REPO}/shell/package/contents/templates/.multiple-layouts_hidden.layout.latte",
        hidden_layout,
    )
    _kwrite_or_fail(
        "could not activate the FP-4C origin on all activities",
        "--file",
        _S.layout,
        "--group",
        "LayoutSettings",
        "--key",
        "activities",
        "{0}",
    )
    _kwrite_or_fail(
        "could not activate the FP-4C destination on all activities",
        "--file",
        destination_layout,
        "--group",
        "LayoutSettings",
        "--key",
        "activities",
        "{0}",
    )
    _kwrite_or_fail(
        "could not enable FP-4C multiple-layout mode",
        "--file",
        f"{_S.config_home}/lattedockrc",
        "--group",
        "UniversalSettings",
        "--key",
        "memoryUsage",
        "1",
    )

    if not recipe.dock_start(90):
        recipe.fail("the configured two-layout FP-4C fixture did not start")

    active_layouts_file = f"{_S.artifact_dir}/active-layouts.json"
    layouts_payload = recipe.json_payload("layoutsData")
    with open(active_layouts_file, "w", encoding="utf-8") as stream:
        stream.write(layouts_payload)
    _resolve_two_active_layouts(active_layouts_file, origin_layout_name, destination_layout_name)

    runtime_views_file = f"{_S.artifact_dir}/runtime-root.json"
    with open(runtime_views_file, "w", encoding="utf-8") as stream:
        stream.write(recipe.json_payload("viewsData"))
    root_id = _resolve_remapped_root(runtime_views_file, origin_layout_name)

    recipe.call_or_fail(
        "could not set the FP-4C root to Always Visible",
        "setViewVisibilityMode",
        "us",
        root_id,
        "alwaysVisible",
    )
    for _ in range(40):
        with open(runtime_views_file, "w", encoding="utf-8") as stream:
            stream.write(recipe.json_payload("viewsData"))
        views = _load(runtime_views_file)
        matching = [v for v in views if v.get("containmentId") == int(root_id)]
        if len(matching) == 1 and matching[0].get("visibilityMode") == "alwaysVisible":
            break
        time.sleep(0.25)
    else:
        recipe.fail("the FP-4C root did not enter Always Visible mode")

    screens_file = f"{_S.artifact_dir}/screens.json"
    with open(screens_file, "w", encoding="utf-8") as stream:
        stream.write(recipe.json_payload("screensData"))
    _resolve_two_outputs(screens_file)
    with open(_S.bindings_file, "w", encoding="utf-8") as stream:
        stream.write(f'{{"root":{root_id}}}\n')

    if _model("validate-plan", stdin=_read(_S.plan_file)).returncode != 0:
        recipe.fail("the artifact FP-4C operation plan failed revalidation")
    emitted = _model("emit-operations", stdin=_read(_S.plan_file))
    if emitted.returncode != 0:
        recipe.fail("could not emit the typed FP-4C operations")
    with open(_S.operations_file, "w", encoding="utf-8") as stream:
        stream.write(emitted.stdout)
    header = _model("replay-header", stdin=_build_replay_header_input())
    if header.returncode != 0:
        recipe.fail("could not record the resolved FP-4C replay header")
    with open(_S.replay_file, "w", encoding="utf-8") as stream:
        stream.write(header.stdout)

    _drive_operations(int(latest_intent_final_seq))


def _resolve_two_active_layouts(active_layouts_file: str, origin: str, destination: str) -> None:
    fail = "the FP-4C fixture did not activate exactly two layouts"
    state = _load(active_layouts_file)
    if state.get("memoryUsage") != "multiple":
        _warn("FP-4C did not enter multiple-layout mode")
        recipe.fail(fail)
    active = {
        layout["name"]: layout for layout in state.get("layouts", []) if layout.get("isActive")
    }
    expected = {origin, destination}
    if set(active) != expected:
        _warn(f"expected active layouts {sorted(expected)}, got {sorted(active)}")
        recipe.fail(fail)
    if any(layout.get("activities") != ["{0}"] for layout in active.values()):
        _warn("both FP-4C layouts must be active on all activities")
        recipe.fail(fail)
    with open(_S.layouts_file, "w", encoding="utf-8") as stream:
        stream.write(_compact({"origin": origin, "destination": destination}))
        stream.write("\n")


def _resolve_remapped_root(runtime_views_file: str, origin: str) -> str:
    views = _load(runtime_views_file)
    matches = [
        v
        for v in views
        if v.get("layout") == origin and v.get("type") == "panel" and not v.get("isCloned")
    ]
    if len(views) != 1 or len(matches) != 1:
        _warn(
            "multiple-layout activation did not retain exactly one independent "
            f"origin panel: {views!r}"
        )
        recipe.fail("could not bind the remapped FP-4C runtime root identity")
    view = matches[0]
    if view.get("inStartup") or view.get("isOffScreen"):
        _warn("the remapped FP-4C root is not ready on its output")
        recipe.fail("could not bind the remapped FP-4C runtime root identity")
    containment_id = str(view["containmentId"])
    if not (containment_id.isdigit() and int(containment_id) >= 1):
        recipe.fail("the remapped FP-4C runtime root identity is malformed")
    return containment_id


def _resolve_two_outputs(screens_file: str) -> None:
    screens = [screen for screen in _load(screens_file) if screen["isActive"]]
    primary = [screen for screen in screens if screen["isPrimary"]]
    secondary = [screen for screen in screens if not screen["isPrimary"]]
    if len(screens) != 2 or len(primary) != 1 or len(secondary) != 1:
        _warn(f"expected two active screens with one primary, got {screens!r}")
        recipe.fail("could not resolve exactly two active FP-4C output identities")

    def output_record(screen: dict[str, Any]) -> dict[str, Any]:
        return {"id": screen["id"], "name": screen["name"], "geometry": screen["geometry"]}

    with open(_S.outputs_file, "w", encoding="utf-8") as stream:
        stream.write(
            _compact(
                {"primary": output_record(primary[0]), "secondary": output_record(secondary[0])}
            )
        )
        stream.write("\n")


def _drive_operations(latest_intent_final_seq: int) -> None:
    step_dir = f"{_S.artifact_dir}/steps"
    os.makedirs(step_dir, exist_ok=True)
    pending_steps: list[str] = []
    pending_resolved: list[str] = []
    pending_before_file = ""
    pending_tombstone_id = ""
    removal_started_ns = 0
    last_step_number = 0

    with open(_S.operations_file, encoding="utf-8") as operations:
        operation_rows = operations.read().splitlines()

    for operation_row_text in operation_rows:
        if not operation_row_text:
            continue
        operation_row = json.loads(operation_row_text)
        step_number = operation_row["seq"]
        last_step_number = step_number
        step_tag = f"{step_number:03d}"
        step_file = f"{step_dir}/{step_tag}.operation.json"
        resolved_file = f"{step_dir}/{step_tag}.resolved.json"
        before_file = f"{step_dir}/{step_tag}.before.json"
        after_file = f"{step_dir}/{step_tag}.after.json"
        result_file = f"{step_dir}/{step_tag}.result.json"
        view_move_before_file = f"{step_dir}/{step_tag}.view-move.before.json"
        view_move_after_file = f"{step_dir}/{step_tag}.view-move.after.json"
        with open(step_file, "w", encoding="utf-8") as stream:
            stream.write(operation_row_text + "\n")

        resolved = _model("resolve-operation", stdin=_build_resolve_input(step_file))
        if resolved.returncode != 0:
            recipe.fail(f"could not resolve FP-4C operation {step_number}")
        with open(resolved_file, "w", encoding="utf-8") as stream:
            stream.write(resolved.stdout)
        resolved_json = json.loads(resolved.stdout)
        action_kind = _scalar(resolved_json["action"]["kind"])
        operation_kind = _scalar(operation_row["operation"]["kind"])
        checkpoint = bool(operation_row["checkpoint"])
        if not _assert_no_pending_view_move(view_move_before_file):
            recipe.fail(
                f"FP-4C operation {step_number} started with an invalid durable move lifecycle"
            )

        if not pending_before_file:
            payload = _snapshot()
            if not payload:
                recipe.fail(f"could not capture state before FP-4C operation {step_number}")
            with open(before_file, "w", encoding="utf-8") as stream:
                stream.write(payload)
            pending_before_file = before_file

        removed_this_step = ""
        reload_this_step = False
        restart_this_step = False
        before_reload_projection = ""
        after_reload_quiescent = ""
        after_reload_projection = ""
        before_restart_projection = ""
        after_restart_quiescent = ""
        after_restart_projection = ""
        if action_kind == "dbus":
            method = _scalar(resolved_json["action"]["method"])
            signature = _scalar(resolved_json["action"]["signature"])
            action_args = _action_arguments(resolved_json)
            if method == "removeView":
                removal_started_ns = time.time_ns()
            if method == "reloadView":
                before_reload_quiescent = f"{step_dir}/{step_tag}.before-reload.quiescent.json"
                before_reload_projection = f"{step_dir}/{step_tag}.before-reload.projection.json"
                after_reload_quiescent = f"{step_dir}/{step_tag}.after-reload.quiescent.json"
                after_reload_projection = f"{step_dir}/{step_tag}.after-reload.projection.json"
                if not _wait_for_quiescent_projection(before_reload_quiescent, before_file):
                    recipe.fail(f"state before reload operation {step_number} did not quiesce")
                projected = _model("durable-projection", stdin=_read(before_file))
                if projected.returncode != 0:
                    recipe.fail(
                        f"could not project durable state before reload operation {step_number}"
                    )
                with open(before_reload_projection, "w", encoding="utf-8") as stream:
                    stream.write(projected.stdout)
            recipe.call_or_fail(
                f"D-Bus transport failed for FP-4C operation {step_number} ({method})",
                method,
                signature,
                *action_args,
            )

            if method == "removeView":
                removed_this_step = action_args[0] if action_args else ""
                if not (removed_this_step.isdigit() and int(removed_this_step) >= 1):
                    recipe.fail(f"removeView operation {step_number} has no persistent identity")
            elif method == "reloadView":
                reload_this_step = True
            elif method in ("setViewEditMode", "setViewConfiguringApplets"):
                if not checkpoint:
                    recipe.fail(f"edit operation {step_number} cannot be an unchecked burst member")
                edit_target = _scalar(operation_row["operation"]["target"])
                if operation_kind == "beginEdit":
                    expected_editing, expected_configuring = True, False
                elif operation_kind == "configureAppletsOn":
                    expected_editing, expected_configuring = True, True
                elif operation_kind == "configureAppletsOff":
                    expected_editing, expected_configuring = True, False
                elif operation_kind == "endEdit":
                    expected_editing, expected_configuring = False, False
                else:
                    recipe.fail(
                        f"operation {step_number} maps an invalid edit kind '{operation_kind}'"
                    )
                if not _wait_for_edit_outcome(
                    edit_target, expected_editing, expected_configuring, after_file
                ):
                    recipe.fail(f"edit ownership did not match FP-4C operation {step_number}")
        elif action_kind == "restart":
            restart_this_step = True
            if not (checkpoint and len(pending_steps) == 0):
                recipe.fail(f"restart operation {step_number} is not an isolated checkpoint")
            before_restart_projection = f"{step_dir}/{step_tag}.before-restart.projection.json"
            after_restart_projection = f"{step_dir}/{step_tag}.after-restart.projection.json"
            before_restart_quiescent = f"{step_dir}/{step_tag}.before-restart.quiescent.json"
            after_restart_quiescent = f"{step_dir}/{step_tag}.after-restart.quiescent.json"
            if not _wait_for_quiescent_projection(before_restart_quiescent, before_file):
                recipe.fail(f"state before restart operation {step_number} did not quiesce")
            projected = _model("durable-projection", stdin=_read(before_file))
            if projected.returncode != 0:
                recipe.fail(
                    f"could not project durable state before restart operation {step_number}"
                )
            with open(before_restart_projection, "w", encoding="utf-8") as stream:
                stream.write(projected.stdout)
            if not recipe.dock_stop():
                recipe.fail(f"dock did not stop for FP-4C restart operation {step_number}")
            if pending_tombstone_id and not _assert_tombstone_on_disk(pending_tombstone_id):
                recipe.fail(f"the stopped layout resurrected removed view {pending_tombstone_id}")
            if not recipe.dock_start(90):
                recipe.fail(f"dock did not start for FP-4C restart operation {step_number}")
            if pending_tombstone_id:
                removal_elapsed_ms = (time.time_ns() - removal_started_ns) // 1000000
                if not removal_elapsed_ms < 60000:
                    recipe.fail(
                        f"removal restart missed the 60-second Undo interval "
                        f"({removal_elapsed_ms}ms)"
                    )
        else:
            recipe.fail(
                f"FP-4C operation {step_number} resolved unsupported action '{action_kind}'"
            )

        pending_steps.append(step_file)
        pending_resolved.append(resolved_file)
        if not checkpoint:
            if action_kind != "dbus":
                recipe.fail("only D-Bus actions may defer an FP-4C checkpoint")
            if operation_kind != "move":
                if operation_kind not in ("createLinked", "duplicateIndependent"):
                    recipe.fail(f"operation {step_number} cannot defer its semantic checkpoint")
                _append_record_field(resolved_file, "record")
                if not _wait_for_bound_result(
                    step_file, pending_before_file, after_file, result_file
                ):
                    recipe.fail(f"snapshot did not bind FP-4C operation {step_number}")
                pending_steps = []
                pending_resolved = []
                pending_before_file = ""
            if not _assert_no_pending_view_move(view_move_after_file):
                recipe.fail(
                    f"unchecked FP-4C operation {step_number} retained a durable move transaction"
                )
            if not _assert_view_move_lifecycle(
                step_file, view_move_before_file, view_move_after_file
            ):
                recipe.fail(
                    f"unchecked FP-4C operation {step_number} changed the durable move lifecycle"
                )
            continue

        # Reaching here means checkpoint is True: the bash re-asserted
        # `[[ "$checkpoint" == true ]]` to catch a malformed non-true/non-false
        # string, structurally impossible once the model emits a JSON bool.
        if reload_this_step and not _wait_for_runtime_reload(
            pending_before_file, after_file, resolved_file
        ):
            recipe.fail("linked-root runtime reload did not rotate exactly its affected views")

        for pending_step, pending_resolve in zip(pending_steps, pending_resolved, strict=True):
            pending_sequence = _load(pending_step)["seq"]
            pending_after = f"{step_dir}/{pending_sequence:03d}.after.json"
            pending_result = f"{step_dir}/{pending_sequence:03d}.result.json"
            _append_record_field(pending_resolve, "record")
            if not _wait_for_bound_result(
                pending_step, pending_before_file, pending_after, pending_result
            ):
                recipe.fail(
                    f"snapshot did not prove the result of FP-4C operation {pending_sequence}"
                )

        last_checkpoint_file = f"{step_dir}/{step_tag}.checkpoint.json"
        checkpoint_attempts = 240
        if removed_this_step:
            # A reversible removal is a suspension transaction, not a request to
            # wait for Plasma's 60-second Undo expiry.
            checkpoint_attempts = 20
        if not _wait_for_checkpoint(step_number, last_checkpoint_file, checkpoint_attempts):
            recipe.fail(f"FP-4C operation checkpoint {step_number} did not converge")
        if not _assert_no_pending_view_move(view_move_after_file):
            recipe.fail(
                f"FP-4C operation checkpoint {step_number} retained a durable move transaction"
            )
        if not _assert_view_move_lifecycle(step_file, view_move_before_file, view_move_after_file):
            recipe.fail(
                f"FP-4C operation checkpoint {step_number} has an incomplete durable move lifecycle"
            )
        if step_number == latest_intent_final_seq and not _assert_latest_intent_probe(
            pending_before_file, last_checkpoint_file
        ):
            recipe.fail(
                "rapid return-to-origin did not preserve the newest complete placement intent"
            )
        if (reload_this_step or restart_this_step or removed_this_step) and (
            not _wait_for_visual_window_ownership(
                step_number,
                f"{step_dir}/{step_tag}.visual-snapshot.json",
                f"{step_dir}/{step_tag}.layer3-windows.json",
            )
        ):
            recipe.fail(f"FP-4C checkpoint {step_number} has leaked or duplicate visual QWindows")

        if reload_this_step:
            if not _wait_for_quiescent_projection(after_reload_quiescent, last_checkpoint_file):
                recipe.fail(f"state after reload operation {step_number} did not quiesce")
            projected = _model("durable-projection", stdin=_read(last_checkpoint_file))
            if projected.returncode != 0:
                recipe.fail(f"could not project durable state after reload operation {step_number}")
            with open(after_reload_projection, "w", encoding="utf-8") as stream:
                stream.write(projected.stdout)
            if _read(before_reload_projection) != _read(after_reload_projection):
                _print_projection_diff(before_reload_projection, after_reload_projection)
                recipe.fail(f"reload operation {step_number} changed the exact durable projection")

        if removed_this_step:
            # This is deliberately the first persistence read after the runtime
            # removal verdict. It proves the synchronous tombstone, not expiry of
            # the 60-second Plasma Undo timer.
            if not _assert_tombstone_on_disk(removed_this_step):
                recipe.fail(f"removed view {removed_this_step} was not tombstoned immediately")
            pending_tombstone_id = removed_this_step
        elif pending_tombstone_id and not restart_this_step:
            recipe.fail(f"FP-4C operation {step_number} intervened before the removal restart")

        if restart_this_step:
            if not _wait_for_quiescent_projection(after_restart_quiescent, last_checkpoint_file):
                recipe.fail(f"state after restart operation {step_number} did not quiesce")
            projected = _model("durable-projection", stdin=_read(last_checkpoint_file))
            if projected.returncode != 0:
                recipe.fail(
                    f"could not project durable state after restart operation {step_number}"
                )
            with open(after_restart_projection, "w", encoding="utf-8") as stream:
                stream.write(projected.stdout)
            if _read(before_restart_projection) != _read(after_restart_projection):
                _print_projection_diff(before_restart_projection, after_restart_projection)
                recipe.fail(f"restart operation {step_number} changed the exact durable projection")
            pending_tombstone_id = ""

        pending_steps = []
        pending_resolved = []
        pending_before_file = ""

    if len(pending_steps) != 0:
        recipe.fail("the typed FP-4C plan ended inside an unchecked placement burst")

    final_snapshot = f"{_S.artifact_dir}/final.json"
    final_quiescent = f"{_S.artifact_dir}/final.quiescent.json"
    if not _wait_for_quiescent_projection(final_quiescent, final_snapshot):
        recipe.fail("the final FP-4C operation state did not remain quiescent")
    if (
        _model(
            "assert-checkpoint",
            stdin=_build_checkpoint_input(last_step_number or 0, final_snapshot),
        ).returncode
        != 0
    ):
        recipe.fail("the final quiescent FP-4C state diverged from the typed plan")
    if not _assert_no_pending_view_move(f"{_S.artifact_dir}/final.view-move-transactions.json"):
        recipe.fail("the final FP-4C state retained a durable move transaction")
    if not _wait_for_visual_window_ownership(
        last_step_number or 0,
        f"{_S.artifact_dir}/final.visual-snapshot.json",
        f"{_S.artifact_dir}/final.layer3-windows.json",
    ):
        recipe.fail("the final FP-4C state has leaked or duplicate visual QWindows")
    if (
        _model("validate-replay", "--plan", _S.plan_file, "--replay", _S.replay_file).returncode
        != 0
    ):
        recipe.fail("the resolved FP-4C replay is incomplete or inconsistent")

    _S.acceptance_completed = True
    print(
        f"FP-4C operation stress passed seed {_load(_S.plan_file)['seed']}; "
        f"resolved replay and snapshots: {_S.artifact_dir}"
    )


def _print_projection_diff(before: str, after: str) -> None:
    subprocess.run(["diff", "-u", before, after], check=False)


def main() -> None:
    # run_with_cleanup owns the try-body / finally shape; the pure
    # perform_cleanup_transaction core (wired in _cleanup) OWNS the final status.
    # install_signal_exits=False is deliberate: this recipe arms the signal exits
    # itself INSIDE _body (at _S.transaction_started), so no command that can stop
    # the pristine dock or replace its configuration runs while an interrupt would
    # route through cleanup - the exact signal-arming ordering the bash kept.
    recipe.run_with_cleanup(_body, _cleanup, install_signal_exits=False)


if __name__ == "__main__":
    main()
