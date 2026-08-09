#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""SC-WT1 (the D58 tracker-enablement root fix and regression): drive the real
EnvironmentActions empty-area MouseArea with fakepointer. trackerData proves
whether the QML binding enabled tracking; KWin's own window state independently
proves close and minimize effects. The neutral mode is the negative control for
both disabled settings, and each enabled mode starts with a no-target action.

Ported from tests/e2e/022-empty-area-window-actions.sh to latte_harness.recipe
(BP-3, the bash-to-python migration's input/wheel recipe batch R8). The tracker,
view-config and visibility readbacks ride recipe.py's typed boundary or the same
raw JSON one-liners the bash used; the konsole fixture window and the close /
minimize KWin effects are proven through the same e2e_kwin_js queries. The
recipe_finalized / cleanup body-status contract carries over as the main() -> int
pattern (the 072 precedent): the body's exit code is preserved and a cleanup
that leaves residue turns a would-be success into a failure. Every assertion,
poll bound, retry count and failure message is byte-identical, the SPDX header is
preserved, and the exec bit stays 100755 (D273).

One structurally-unreachable bash guard is dropped as dead in Python: the
"empty-area point is incomplete" check (empty_area_point returns two ints or
None). read_fixture_count's status/numeric refusals are dropped for good: a
KWin loadScript/run failure raises recipe.KwinScriptError at the shared
boundary (harness audit A2), so a count of 0 can only mean the fixture window
is genuinely absent, never an unreachable compositor scripting surface - the
loud-symptom-not-loud-refusal deviation this paragraph used to record is
retired.
"""

from __future__ import annotations

import contextlib
import io
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from collections.abc import Iterator
from dataclasses import dataclass, field
from pathlib import Path

from latte_harness import proc, recipe


@dataclass
class _State:
    view: int = 0
    layout: str = ""
    backup: str = ""
    fixture_proc: subprocess.Popen[bytes] | None = None
    fixture_id: str = ""
    fixture_title: str = ""
    fixture_count_value: int = 0
    recipe_finalized: bool = False
    current_id: str = ""
    minimized: str = ""
    active: str = ""
    tracker_enabled: str = ""
    tracker_present: str = ""
    pointer_x: int = 0
    pointer_y: int = 0
    group_args: tuple[str, ...] = field(default_factory=tuple)


_S = _State()


@contextlib.contextmanager
def _muted_stderr() -> Iterator[None]:
    """The cleanup dock stop's `>/dev/null 2>&1`: keep its diagnostics off the recipe output."""
    with contextlib.redirect_stderr(io.StringIO()):
        yield


def _pid_alive(pid: int) -> bool:
    """The bash ``kill -0``: alive iff a signal could be delivered."""
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def _latte_call(fail_message: str, *args: str) -> None:
    """`e2e_call ... >/dev/null || e2e_fail`: a lattedock action that fails loudly."""
    result = subprocess.run(
        [
            "busctl",
            "--user",
            "call",
            "org.kde.lattedock",
            "/Latte",
            "org.kde.LatteDock",
            *args,
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        if result.stderr:
            sys.stderr.write(result.stderr)
        recipe.fail(fail_message)


def _view_visibility_mode() -> str:
    """e2e_view_field visibilityMode: this view's visibility mode, or a loud refusal.

    W3 (widen the readback models): visibilityMode rides the typed recipe.View, so
    this reads recipe.views(); a refused reply (DbusUnavailableError) or a missing
    view raises RecipeError, which the wait loop maps to the bash's loud
    readback-failure message.
    """
    record = next((v for v in recipe.views() if v.containment_id == _S.view), None)
    if record is None:
        raise recipe.RecipeError(f"no view with containmentId {_S.view}")
    return record.visibility_mode


def _fixture_count() -> int:
    """fixture_count: KWin windows whose class is konsole and caption carries the title."""
    needle = f"|org.kde.konsole|{_S.fixture_title}"
    return sum(needle in line for line in recipe.dumpwins().splitlines())


def _read_fixture_count() -> None:
    _S.fixture_count_value = _fixture_count()


def _fixture_state_js() -> str:
    return (
        "for (const w of workspace.windowList()) {\n"
        "    if (w.resourceClass === 'org.kde.konsole' && w.caption.includes('"
        + _S.fixture_title
        + "')) {\n"
        "        print('@TAG@|' + w.internalId + '|' + w.minimized + '|' "
        "+ (workspace.activeWindow === w));\n"
        "    }\n"
        "}"
    )


def _read_fixture_state(label: str) -> None:
    state = recipe.kwin_js(_fixture_state_js())
    if not state or "\n" in state:
        recipe.fail(f"{label}: expected exactly one fixture state, got '{state or 'none'}'")
    _S.current_id, _S.minimized, _S.active = state.split("|")


def _read_tracker_state(label: str) -> None:
    try:
        tracker = recipe.read_json("trackerData", "u", str(_S.view))
        _S.tracker_enabled = str(tracker["enabled"]).lower()
        _S.tracker_present = str(tracker["lastActiveWindowPresent"]).lower()
    except (recipe.DbusUnavailableError, KeyError) as exc:
        recipe.fail(f"{label}: invalid trackerData readback: {exc}")


def _wait_tracker_state(expected_enabled: str, expected_present: str, label: str) -> None:
    enabled = present = ""
    for _ in range(40):
        _read_tracker_state(label)
        enabled, present = _S.tracker_enabled, _S.tracker_present
        if enabled == expected_enabled and present == expected_present:
            return
        time.sleep(0.25)
    recipe.fail(
        f"{label} tracker state was enabled={enabled} target={present}; "
        f"expected enabled={expected_enabled} target={expected_present}"
    )


def _wait_visibility_mode(expected: str) -> None:
    actual = ""
    for _ in range(40):
        try:
            actual = _view_visibility_mode()
        except recipe.RecipeError:
            recipe.fail(f"visibility-mode readback failed for view {_S.view}")
        if actual == expected:
            return
        time.sleep(0.25)
    recipe.fail(f"view {_S.view} stayed in visibility mode {actual}; expected {expected}")


def _write_config_key(key: str, value: str, label: str) -> None:
    rc = subprocess.run(
        ["kwriteconfig6", *_S.group_args, "--key", key, "--", value], check=False
    ).returncode
    if rc != 0:
        recipe.fail(f"{label}: could not write {key}={value}")


def _inject(label: str, *args: str) -> None:
    rc = subprocess.run([os.environ["E2E_FAKEPOINTER"], *args], check=False).returncode
    if rc != 0:
        recipe.fail(f"{label}: fakepointer '{' '.join(args)}' failed with status {rc}")


def _configure_mode(
    close_enabled: str, scroll_action: str, expected_tracker: str, label: str
) -> None:
    pid = recipe.dock_pid()
    if pid is not None and _pid_alive(pid) and not recipe.dock_stop():
        recipe.fail(f"{label}: could not stop the dock for configuration")

    _write_config_key("dragActiveWindowEnabled", "false", label)
    _write_config_key("closeActiveWindowEnabled", close_enabled, label)
    _write_config_key("scrollAction", scroll_action, label)
    _write_config_key("backgroundOnlyOnMaximized", "false", label)
    _write_config_key("solidBackgroundForMaximized", "false", label)
    _write_config_key("disablePanelShadowForMaximized", "false", label)
    _write_config_key("windowColors", "0", label)
    _write_config_key("screenEdgeMargin", "-1", label)
    _write_config_key("hideFloatingGapForMaximized", "false", label)

    if not recipe.dock_start(90):
        recipe.fail(f"{label}: dock did not restart")
    _latte_call(
        f"{label}: could not select alwaysVisible",
        "setViewVisibilityMode",
        "us",
        str(_S.view),
        "alwaysVisible",
    )
    _wait_visibility_mode("alwaysVisible")
    cfg = recipe.read_json("viewConfigData", "u", str(_S.view))["config"]
    if not (
        cfg["dragActiveWindowEnabled"] is False
        and cfg["closeActiveWindowEnabled"] == (close_enabled == "true")
        and cfg["scrollAction"] == int(scroll_action)
        and cfg["backgroundOnlyOnMaximized"] is False
        and cfg["solidBackgroundForMaximized"] is False
        and cfg["disablePanelShadowForMaximized"] is False
        and cfg["windowColors"] == 0
        and cfg["screenEdgeMargin"] == -1
        and cfg["hideFloatingGapForMaximized"] is False
    ):
        recipe.fail(f"{label}: in-process config does not match the neutral requester fixture")
    _wait_tracker_state(expected_tracker, "false", f"{label} without a target")


def _empty_area_point() -> tuple[int, int] | None:
    winx = recipe.view_window_x(_S.view)
    if winx is None:
        print(
            f"empty_area_point: could not resolve the rendered x origin for view {_S.view}",
            file=sys.stderr,
            flush=True,
        )
        return None
    target = recipe.view(_S.view)
    ax, ay, aw, ah = target.absolute_geometry
    lx = target.local_geometry[0]
    ox = winx
    drift = ox - (ax - lx)
    ax += drift
    spans = sorted(
        (ox + a.geometry[0], ox + a.geometry[0] + a.geometry[2])
        for a in recipe.view_applets(_S.view)
    )
    gaps: list[tuple[int, int]] = []
    cursor = ax
    for start, end in spans:
        if start > cursor:
            gaps.append((cursor, start))
        cursor = max(cursor, end)
    if ax + aw > cursor:
        gaps.append((cursor, ax + aw))
    best = max(gaps, key=lambda g: g[1] - g[0], default=(0, 0))
    if best[1] - best[0] < 8:
        print(f"widest empty-area gap is under 8px: {gaps}", file=sys.stderr, flush=True)
        return None
    return int((best[0] + best[1]) / 2), int(ay + ah / 2)


def _settle_empty_pointer() -> None:
    point = _empty_area_point()
    if point is None:
        recipe.fail("could not locate an empty view area")
    _S.pointer_x, _S.pointer_y = point
    _inject("settling pointer outside the view", "move", str(_S.pointer_x), "500")
    time.sleep(0.3)
    _inject(
        "settling pointer on the empty view area",
        "move",
        str(_S.pointer_x),
        str(_S.pointer_y),
    )
    time.sleep(0.8)


def _assert_fixture_normal(label: str) -> None:
    _read_fixture_state(label)
    if not (_S.current_id == _S.fixture_id and _S.minimized == "false"):
        recipe.fail(
            f"{label} changed the fixture unexpectedly "
            f"(id={_S.current_id} minimized={_S.minimized} active={_S.active})"
        )


def _wait_fixture_absent(label: str) -> None:
    for _ in range(40):
        _read_fixture_count()
        if _S.fixture_count_value == 0:
            return
        time.sleep(0.25)
    recipe.fail(f"{label} left {_S.fixture_count_value} fixture window(s) mapped")


def _spawn_fixture(title: str) -> None:
    _S.fixture_title = title
    _S.fixture_proc = None
    _S.fixture_id = ""
    _read_fixture_count()
    if _S.fixture_count_value != 0:
        recipe.fail(f"{title}: {_S.fixture_count_value} stale fixture window(s) already mapped")
    _S.fixture_proc = subprocess.Popen(
        ["konsole", "-p", f"LocalTabTitleFormat={title}"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )
    for _ in range(40):
        _read_fixture_count()
        if _S.fixture_count_value == 1:
            break
        time.sleep(0.25)
    if _S.fixture_count_value != 1:
        recipe.fail(f"{title}: fixture window count reached {_S.fixture_count_value} instead of 1")

    activation_js = (
        "for (const w of workspace.windowList()) {\n"
        "    if (w.resourceClass === 'org.kde.konsole' && w.caption.includes('" + title + "')) {\n"
        "        w.minimized = false;\n"
        "        w.setMaximize(false, false);\n"
        "        workspace.activeWindow = w;\n"
        "        print('@TAG@|' + w.internalId);\n"
        "    }\n"
        "}"
    )
    _S.fixture_id = recipe.kwin_js(activation_js)
    if not _S.fixture_id or "\n" in _S.fixture_id:
        recipe.fail(f"{title}: KWin did not identify exactly one fixture")

    for _ in range(40):
        _read_fixture_state(f"{title} activation wait")
        if _S.current_id == _S.fixture_id and _S.minimized == "false" and _S.active == "true":
            return
        time.sleep(0.25)
    recipe.fail(
        f"{title}: fixture did not settle active and restored "
        f"(id={_S.current_id} minimized={_S.minimized} active={_S.active})"
    )


def _terminate_fixture(label: str) -> None:
    proc = _S.fixture_proc
    if proc is not None:
        if _pid_alive(proc.pid):
            try:
                os.kill(proc.pid, signal.SIGTERM)
            except ProcessLookupError:
                recipe.fail(f"{label}: could not terminate fixture pid {proc.pid}")
        with contextlib.suppress(Exception):
            proc.wait()
        rc = proc.returncode
        wait_status = 128 - rc if rc is not None and rc < 0 else (rc if rc is not None else 0)
        if wait_status not in (0, 143):
            recipe.fail(
                f"{label}: fixture pid {proc.pid} exited unexpectedly with status {wait_status}"
            )
        if _pid_alive(proc.pid):
            recipe.fail(f"{label}: fixture pid {proc.pid} survived termination")
    _wait_fixture_absent(f"{label} absence check")
    _S.fixture_proc = None
    _S.fixture_id = ""


def _drive_close_until_absent() -> None:
    for attempt in (1, 2, 3, 4):
        _settle_empty_pointer()
        _inject(
            f"close-only middle click attempt {attempt}",
            "middleclick",
            str(_S.pointer_x),
            str(_S.pointer_y),
        )
        for _ in range(8):
            _read_fixture_count()
            if _S.fixture_count_value == 0:
                return
            time.sleep(0.25)
        print(f"  (middle click not delivered on attempt {attempt}, retrying)")
    recipe.fail(f"close-only middle click left {_S.fixture_count_value} fixture window(s) mapped")


def _drive_minimize_until_observed() -> None:
    for attempt in (1, 2, 3, 4):
        _settle_empty_pointer()
        _inject(
            f"minimize-toggle wheel attempt {attempt}",
            "scroll",
            str(_S.pointer_x),
            str(_S.pointer_y),
            "-1",
            "0",
        )
        for _ in range(8):
            _read_fixture_state("minimize-toggle effect wait")
            if _S.current_id == _S.fixture_id and _S.minimized == "true":
                return
            time.sleep(0.25)
        print(f"  (negative wheel not delivered on attempt {attempt}, retrying)")
    recipe.fail(
        f"minimize-toggle negative wheel did not minimize the fixture "
        f"(id={_S.current_id} minimized={_S.minimized} active={_S.active})"
    )


def _finalize_recipe() -> None:
    _terminate_fixture("final minimize fixture")
    _read_fixture_count()
    if _S.fixture_count_value != 0:
        recipe.fail(f"finalization left {_S.fixture_count_value} fixture window(s)")
    pid = recipe.dock_pid()
    if pid is None or not _pid_alive(pid):
        recipe.fail("finalization found no running dock to stop")
    if not recipe.dock_stop():
        recipe.fail(f"finalization could not stop dock pid {pid}")
    try:
        shutil.copyfile(_S.backup, _S.layout)
    except OSError:
        recipe.fail(f"finalization could not restore {_S.layout}")
    if Path(_S.backup).read_bytes() != Path(_S.layout).read_bytes():
        recipe.fail("finalization restored different layout bytes")
    _S.recipe_finalized = True


def _body() -> None:
    # Disabled controls: neither action may turn on the tracker or affect the same
    # active normal window.
    _configure_mode("false", "0", "false", "disabled close/minimize")
    _spawn_fixture("LATTE SC-WT1 DISABLED")
    _wait_tracker_state("false", "false", "disabled close/minimize with active window")
    _settle_empty_pointer()
    _inject("disabled close control", "middleclick", str(_S.pointer_x), str(_S.pointer_y))
    time.sleep(0.8)
    _assert_fixture_normal("disabled close")
    _inject(
        "disabled minimize-toggle control",
        "scroll",
        str(_S.pointer_x),
        str(_S.pointer_y),
        "-1",
        "0",
    )
    time.sleep(0.8)
    _assert_fixture_normal("disabled minimize-toggle")
    _wait_tracker_state("false", "false", "disabled controls after input")
    print("ok: disabled close and minimize-toggle kept tracker off and left the window normal")
    _terminate_fixture("disabled fixture")

    # Close-only: enabling the setting alone must enable tracking. The first click
    # proves the no-target contract is a no-op; the second closes a tracked window.
    _configure_mode("true", "0", "true", "close-only")
    _settle_empty_pointer()
    _inject(
        "close-only no-target control",
        "middleclick",
        str(_S.pointer_x),
        str(_S.pointer_y),
    )
    time.sleep(0.8)
    if not recipe.wait_running(5):
        recipe.fail("close-only no-target click stopped the dock")
    _wait_tracker_state("true", "false", "close-only after no-target click")
    print("ok: close-only no-target click was a no-op with tracking enabled")

    _spawn_fixture("LATTE SC-WT1 CLOSE")
    _wait_tracker_state("true", "true", "close-only with active window")
    _drive_close_until_absent()
    if not recipe.wait_running(5):
        recipe.fail("close-only target click stopped the dock")
    print("ok: close-only enabled tracking and removed the KWin window")
    _terminate_fixture("closed fixture process")

    # Minimize-toggle: the setting alone must enable tracking. A negative wheel with
    # no target is harmless; the same real input minimizes a tracked normal window.
    _configure_mode("false", "4", "true", "minimize-toggle")
    _settle_empty_pointer()
    _inject(
        "minimize-toggle no-target control",
        "scroll",
        str(_S.pointer_x),
        str(_S.pointer_y),
        "-1",
        "0",
    )
    time.sleep(0.8)
    if not recipe.wait_running(5):
        recipe.fail("minimize-toggle no-target wheel stopped the dock")
    _wait_tracker_state("true", "false", "minimize-toggle after no-target wheel")
    print("ok: minimize-toggle no-target wheel was a no-op with tracking enabled")

    _spawn_fixture("LATTE SC-WT1 MINIMIZE")
    _wait_tracker_state("true", "true", "minimize-toggle with active window")
    _drive_minimize_until_observed()
    _read_tracker_state("tracker state after minimize")
    if _S.tracker_enabled != "true":
        recipe.fail("minimize effect disabled the requester-owned tracker")
    print("ok: minimize-toggle enabled tracking and KWin reports the window minimized")

    _finalize_recipe()
    print("PASS: SC-WT1 empty-area tracker requester production paths")


def _cleanup() -> bool:
    cleanup_failed = False
    if not _S.recipe_finalized:
        proc = _S.fixture_proc
        if proc is not None:
            if _pid_alive(proc.pid):
                try:
                    os.kill(proc.pid, signal.SIGTERM)
                except ProcessLookupError:
                    print(
                        f"FAIL: cleanup could not terminate fixture pid {proc.pid}",
                        file=sys.stderr,
                        flush=True,
                    )
                    cleanup_failed = True
            with contextlib.suppress(Exception):
                proc.wait()
            rc = proc.returncode
            wait_status = 128 - rc if rc is not None and rc < 0 else (rc if rc is not None else 0)
            if wait_status not in (0, 143) or _pid_alive(proc.pid):
                print(
                    f"FAIL: cleanup fixture pid {proc.pid} did not terminate cleanly "
                    f"(wait={wait_status})",
                    file=sys.stderr,
                    flush=True,
                )
                cleanup_failed = True
        if _S.fixture_title:
            count = _fixture_count()
            if count != 0:
                print(
                    f"FAIL: cleanup could not prove fixture '{_S.fixture_title}' absent "
                    f"(count='{count}')",
                    file=sys.stderr,
                    flush=True,
                )
                cleanup_failed = True
        pid = recipe.dock_pid()
        if pid is not None and _pid_alive(pid):
            with _muted_stderr():
                stopped = recipe.dock_stop()
            if not stopped:
                print(
                    f"FAIL: cleanup could not stop dock pid {pid}",
                    file=sys.stderr,
                    flush=True,
                )
                cleanup_failed = True
        try:
            shutil.copyfile(_S.backup, _S.layout)
            restored = Path(_S.backup).read_bytes() == Path(_S.layout).read_bytes()
        except OSError:
            restored = False
        if not restored:
            print(
                f"FAIL: cleanup could not restore layout {_S.layout} from {_S.backup}",
                file=sys.stderr,
                flush=True,
            )
            cleanup_failed = True
    try:
        os.unlink(_S.backup)
    except OSError:
        print(f"FAIL: cleanup could not remove {_S.backup}", file=sys.stderr, flush=True)
        cleanup_failed = True
    if cleanup_failed:
        print("FAIL: SC-WT1 recipe cleanup left residue", file=sys.stderr, flush=True)
    return cleanup_failed


def _setup() -> None:
    try:
        _S.view = recipe.tasks_view()
    except recipe.RecipeError:
        recipe.fail("no horizontal tasks view")
    _S.layout = os.environ["E2E_LAYOUT"]
    _S.group_args = (
        "--file",
        _S.layout,
        "--group",
        "Containments",
        "--group",
        str(_S.view),
        "--group",
        "General",
    )
    _S.backup = tempfile.mkstemp()[1]
    shutil.copyfile(_S.layout, _S.backup)


def main() -> int:
    # The cleanup sits in a finally so it runs on EVERY exit path, like the
    # bash `trap cleanup EXIT`: the caught verdict exits, an unexpected
    # exception (an unguarded readback decode after the layout is already
    # modified), and the conventional signal exits installed below. Without
    # it, an unintended exit strands the konsole fixture and leaves the
    # modified E2E_LAYOUT unrestored, poisoning following recipes through
    # the runner's dock reuse.
    proc.install_conventional_signal_exits()
    _setup()
    status = 0
    try:
        try:
            _body()
        except SystemExit as exc:
            status = exc.code if isinstance(exc.code, int) else 1
        except recipe.RecipeError as exc:
            print(str(exc), file=sys.stderr, flush=True)
            status = 1
    finally:
        if _cleanup() and status == 0:
            status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
