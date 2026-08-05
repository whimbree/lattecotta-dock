# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The deterministic render gate: latte-sceneprobe over every committed scene.

The typed port of scripts/sceneprobe-gate.sh and tests/sceneprobe/run_in_kwin.sh
(BP-2d, the bash-to-python migration's sceneprobe chunk). Two entry points, one
module:

- ``gate`` (scripts/sceneprobe-gate.sh): stage the source's Latte QML modules,
  bring up a throwaway nested kwin_wayland per scene, run latte-sceneprobe, and
  fail if any real scene fails. Three failure gates live INSIDE the probe binary
  (shader/scenegraph warnings, Vulkan validation errors, and output assertions
  incl. the golden compare); this module owns the choreography and the exit-code
  contract around them. It SELF-TESTS first (selftest-good must pass,
  selftest-bad and selftest-blank must fail) so a broken gate is caught before
  its verdicts are trusted.
- ``run-in-kwin`` (tests/sceneprobe/run_in_kwin.sh): run an arbitrary command
  under a throwaway nested kwin_wayland session with the Vulkan device dispatch
  and probe env applied, exit with the command's own code. The gate uses the
  same internal choreography directly (no subprocess); this subcommand keeps the
  standalone wrapper the latte-debugging skill and dgpu exploration invoke.

The golden comparison is NOT re-implemented here: latte-sceneprobe links
tests/sceneprobe/imagecompare.* and does the compare against
``<scene>.expected.<device>.png`` at the tier SCENEPROBE_TIER selects (bitexact
by default). This module forwards the identical env the bash exported, so the
thresholds, filenames, and failure wording come from the one C++ owner.

Exit codes carry over verbatim: 0 pass, 1 a real scene failed, 2 a setup
failure (no probe binary, an unusable Vulkan device, or the nested compositor
never bound its socket), 3 the gate itself is broken (a self-test verdict
disagreed).
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from collections.abc import Mapping, MutableMapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from latte_harness import proc, qmlenv, vehicle
from latte_harness.log import fail, info
from latte_harness.paths import RepoPaths
from latte_harness.proc import install_conventional_signal_exits

TOOL = "sceneprobe-gate"

# The nested compositor geometry and socket the bash gate used
# (`nested_kwin_start 256 256 sceneprobe-wl`). 256x256 matches the probe's own
# fixed render size; the socket name only has to be stable within one session.
PROBE_WIDTH = 256
PROBE_HEIGHT = 256
PROBE_SOCKET = "sceneprobe-wl"

# The probe's own wall-clock ceiling (bash `timeout 90 "$@"`). A hung probe is
# killed and reported as this shell-`timeout` exit code, never a silent hang.
PROBE_TIMEOUT = 90.0
EXIT_TIMEOUT = 124  # GNU `timeout`'s exit code when it kills the command

# Distinguished exit codes, byte-identical to the bash gate.
EXIT_SETUP = 2  # no probe binary, an unusable device, or the socket never bound
EXIT_GATE_BROKEN = 3  # a self-test verdict disagreed: the gate cannot be trusted

# The one image tier default: SCENEPROBE_TIER unset or empty means bit-exact
# (the NixOS/dev merge gate). The probe refuses an unknown tier loudly itself;
# this only supplies the default the bash `${SCENEPROBE_TIER:-bitexact}` did.
DEFAULT_TIER = "bitexact"


class DeviceDispatchError(RuntimeError):
    """The Vulkan device could not be dispatched (unknown device, or the ICD /
    validation-layer manifests the flake devShell exports are missing).

    Carries the exact run_in_kwin.sh message so the failure wording is
    unchanged; setup failures exit 2.
    """


@dataclass(frozen=True, slots=True)
class DeviceConfig:
    """The resolved Vulkan device dispatch (run_in_kwin.sh's ONE dispatch point).

    ``icd`` is the forced lavapipe ICD manifest, empty for dgpu (the loader
    enumerates the host's drivers there). ``layers`` is the validation-layer
    manifest dir, forced from the pin in both modes.
    """

    name: str
    icd: str
    layers: str

    @property
    def is_lavapipe(self) -> bool:
        return self.name == "lavapipe"


@dataclass(frozen=True, slots=True)
class ProbeRun:
    """One command run under the nested compositor: its exit code and, when the
    caller captured it, the combined stdout+stderr for a failure dump."""

    exit_code: int
    output: str


@dataclass(frozen=True, slots=True)
class QmlImportSetup:
    """The QML paths qml_env_setup resolved: build/stage dirs, the install
    qmldir subdir, and the full ``-import`` list (win-last order)."""

    build: Path
    stage: Path
    qmldir: str
    imports: list[str]


# ---- pure logic (unit-tested directly) -------------------------------------


def resolve_tier(env: Mapping[str, str]) -> str:
    """The golden-compare tier, defaulting to bit-exact (bash ``:-bitexact``).

    Unset OR empty falls back to the default, matching the shell ``:-``; the
    probe validates the value and refuses an unknown tier loudly, so this only
    supplies the default and never second-guesses a set value.
    """
    return env.get("SCENEPROBE_TIER") or DEFAULT_TIER


def flatten_import_dirs(imports: Sequence[str]) -> str:
    """Colon-join the directories out of a ``-import <dir>`` flag list.

    qmlenv hands back ``["-import", d0, "-import", d1, ...]``; the probe reads a
    colon-separated LATTE_QML_IMPORT_PATH, so take the odd indices (the dirs)
    exactly as the bash ``for ((i=1; i<len; i+=2))`` loop did.
    """
    return ":".join(imports[1::2])


def resolve_device_config(env: Mapping[str, str]) -> DeviceConfig:
    """The Vulkan device dispatch, mirroring run_in_kwin.sh's case statement.

    lavapipe (default): force the pinned ICD, refusing loudly if it is unset or
    missing. dgpu: no ICD forced (the loader enumerates the host's drivers). Any
    other value is refused loudly. The validation-layer manifest dir is required
    in both modes (the probe drops an unsupported layer silently, gutting the
    validation gate, so its absence is a hard setup failure). Every message is
    the run_in_kwin.sh wording verbatim.
    """
    device = env.get("SCENEPROBE_DEVICE") or "lavapipe"
    if device == "lavapipe":
        icd = env.get("LATTE_VULKAN_LAVAPIPE_ICD") or ""
        if not icd or not Path(icd).is_file():
            raise DeviceDispatchError(
                "lavapipe ICD not found (LATTE_VULKAN_LAVAPIPE_ICD unset or missing; "
                "run inside the flake devShell)"
            )
    elif device == "dgpu":
        icd = ""
    else:
        raise DeviceDispatchError(
            f"unsupported SCENEPROBE_DEVICE '{device}': lavapipe (default, CI tier) "
            "or dgpu (opt-in host GPU)"
        )
    layers = env.get("LATTE_VK_LAYER_PATH") or ""
    if not layers or not Path(layers).is_dir():
        raise DeviceDispatchError(
            "validation layer manifests not found (LATTE_VK_LAYER_PATH unset or missing; "
            "run inside the flake devShell)"
        )
    return DeviceConfig(device, icd, layers)


def build_probe_env(
    base_env: Mapping[str, str], runtime_dir: Path, socket: str, device: DeviceConfig
) -> dict[str, str]:
    """The probe's environment, mirroring run_in_kwin.sh's ``env`` prefix exactly.

    Start from the caller's environment (so the gate's exported SCENEPROBE_* /
    LATTE_QML_IMPORT_PATH / LATTE_VK_SUPPRESSIONS reach the probe), STRIP
    DISPLAY and XAUTHORITY (nothing in the nested session needs the real X
    server), point the probe at the nested wayland socket and runtime dir, and
    force the Vulkan RHI backend. LP_NUM_THREADS=0 (bit-reproducible lavapipe)
    and the forced ICD are lavapipe-only; dgpu leaves the loader's enumeration
    alone. The validation-layer path is forced in both modes.
    """
    env = dict(base_env)
    env.pop("DISPLAY", None)
    env.pop("XAUTHORITY", None)
    env["QT_QPA_PLATFORM"] = "wayland"
    env["WAYLAND_DISPLAY"] = socket
    env["XDG_RUNTIME_DIR"] = str(runtime_dir)
    env["QSG_RHI_BACKEND"] = "vulkan"
    if device.is_lavapipe:
        env["LP_NUM_THREADS"] = "0"
        env["VK_ICD_FILENAMES"] = device.icd
    env["VK_LAYER_PATH"] = device.layers
    return env


def scene_passed(exit_code: int) -> bool:
    """A scene run's PASS/FAIL verdict: only a clean 0 is a pass.

    Mirrors the bash ``if run_scene "$s"; then PASS; else FAIL`` - any nonzero
    (a gate failure 1, a setup failure 2, a timeout 124) is a FAIL.
    """
    return exit_code == 0


def selftest_disagrees(expected_exit: int, actual_exit: int) -> bool:
    """Whether a self-test's exit code disagrees with its wiring expectation.

    selftest-good must exit 0, selftest-bad and selftest-blank must exit 1; any
    disagreement means the gate's own pass/fail wiring is broken (exit 3), so a
    later real verdict cannot be trusted.
    """
    return actual_exit != expected_exit


# ---- nested-kwin choreography (the run_in_kwin.sh internals) ----------------


def _compositor_failure_text(err: vehicle.CompositorStartError) -> str:
    """The socket-timeout dump the bash printed when nested_kwin_start failed.

    running_compositor (the library) raises rather than printing, so reproduce
    the ``start`` subcommand's stderr: the header plus kwin's captured log.
    """
    header = f"{vehicle.TOOL}: nested kwin_wayland never brought up its socket; its log:\n"
    return header + err.log_text


def _run_command(
    command: Sequence[str], probe_env: Mapping[str, str], *, capture: bool
) -> ProbeRun:
    """Run ``command`` with the probe env under the bash ``timeout 90``.

    A hung command is killed by the bounded run and reported as the shell
    ``timeout`` exit code (124), never a silent hang. When ``capture`` is set the
    combined stdout+stderr is returned for a failure dump; otherwise it streams
    to this process's stdout/stderr (the standalone run_in_kwin.sh behaviour).
    """
    try:
        result = proc.run(
            [str(part) for part in command],
            env=probe_env,
            timeout=PROBE_TIMEOUT,
            capture=capture,
        )
    except subprocess.TimeoutExpired:
        return ProbeRun(EXIT_TIMEOUT, f"latte-sceneprobe timed out after {int(PROBE_TIMEOUT)}s")
    output = (result.stdout + result.stderr) if capture else ""
    return ProbeRun(result.returncode, output)


def run_in_nested_kwin(
    command: Sequence[str], base_env: Mapping[str, str], *, capture: bool
) -> ProbeRun:
    """Run ``command`` inside a throwaway nested kwin_wayland: the run_in_kwin.sh
    choreography as one function.

    Resolve the Vulkan device first (a bad device never brings up a compositor),
    then bring the compositor up via the vehicle library's running_compositor
    (its finally is the bash ``trap nested_kwin_cleanup EXIT INT TERM``), build
    the probe env against the bound socket, and run the command. A device
    dispatch failure or a compositor that never binds its socket is a setup
    failure (exit 2), with the failing message carried in ``output`` for the
    caller to surface.
    """
    try:
        device = resolve_device_config(base_env)
    except DeviceDispatchError as err:
        return ProbeRun(EXIT_SETUP, str(err))
    runtime_dir = vehicle.prepare_runtime_dir()
    try:
        with vehicle.running_compositor(
            runtime_dir, PROBE_WIDTH, PROBE_HEIGHT, PROBE_SOCKET, 1, [], base_env
        ) as session:
            probe_env = build_probe_env(base_env, runtime_dir, session.socket, device)
            return _run_command(command, probe_env, capture=capture)
    except vehicle.CompositorStartError as err:
        return ProbeRun(EXIT_SETUP, _compositor_failure_text(err))


# ---- QML environment (qml_env_setup / qml_env_stage as library calls) ------


def resolve_qml_import_setup(repo: Path, env: Mapping[str, str]) -> QmlImportSetup:
    """The build/stage/qmldir/imports qml_env_setup resolved, as library calls.

    BUILD and STAGE overrides are honoured exactly as qmlenv.build_setup_script
    does (CI sets BUILD). The staged Latte tree is the last ``-import`` so it
    wins, same as every other QML gate. Refuses loudly when the flake devShell's
    LATTE_QML_MODULE_PATH is absent - the gate cannot resolve the import list
    without it.
    """
    module_path = env.get("LATTE_QML_MODULE_PATH")
    if not module_path:
        fail(
            TOOL,
            "LATTE_QML_MODULE_PATH is unset; run inside the flake devShell "
            "(nix develop provides it)",
        )
    build = Path(env["BUILD"]) if env.get("BUILD") else repo / "build"
    stage = Path(env["STAGE"]) if env.get("STAGE") else build / "_qmlstage"
    qmldir = qmlenv.resolve_install_qmldir(build)
    imports = qmlenv.assemble_imports(module_path, build, stage, qmldir)
    return QmlImportSetup(build, stage, qmldir, imports)


def apply_qml_env_mutations(env: MutableMapping[str, str]) -> None:
    """The env mutations qml_env_setup emitted: drop the profile's QML2 import
    path and re-export the nixpkgs Qt6 seed vars with the packaged latte-dock
    leaf stripped (the D8/D271 doctrine, owned by qmlenv)."""
    env.pop("QML2_IMPORT_PATH", None)
    env.pop("QML_IMPORT_PATH", None)
    for var in qmlenv.NIXPKGS_SEED_VARS:
        current = env.get(var)
        if current:
            env[var] = qmlenv.strip_packaged_latte_dock(current)


def _clean_artifact_pngs(artifacts: Path) -> None:
    """Remove stale actual/expected/diff PNGs from a prior run (bash ``rm -f``).

    Only the artifact triples in the artifacts dir are removed; the committed
    ``<scene>.expected.<device>.png`` goldens under scenes/ carry the device in
    their name and live elsewhere, so they are never touched.
    """
    for pattern in ("*.actual.png", "*.diff.png", "*.expected.png"):
        for path in artifacts.glob(pattern):
            path.unlink(missing_ok=True)


def _export_probe_env(
    here: Path, setup: QmlImportSetup, env: MutableMapping[str, str], *, bless: bool
) -> Path:
    """Apply every export the bash gate did between qml_env_stage and the runs.

    Mutates ``env`` in place (it is os.environ, which the per-scene runs inherit
    as their base): the flattened import path, the QT_PLUGIN_PATH drop, the
    device/tier/suppressions selectors, and the artifacts dir. Returns the
    resolved artifacts dir.
    """
    apply_qml_env_mutations(env)
    env["LATTE_QML_IMPORT_PATH"] = flatten_import_dirs(setup.imports)
    # The session's plugin list points at foreign Qt builds; the pinned Qt finds
    # its own plugins (QML2_IMPORT_PATH is already dropped above).
    env.pop("QT_PLUGIN_PATH", None)
    env["SCENEPROBE_DEVICE"] = "lavapipe"
    env["SCENEPROBE_TIER"] = resolve_tier(env)
    env["LATTE_VK_SUPPRESSIONS"] = str(here / "vk-suppressions.txt")
    artifacts = (
        Path(env["SCENEPROBE_ARTIFACTS"])
        if env.get("SCENEPROBE_ARTIFACTS")
        else setup.build / "_sceneprobe-artifacts"
    )
    env["SCENEPROBE_ARTIFACTS"] = str(artifacts)
    artifacts.mkdir(parents=True, exist_ok=True)
    _clean_artifact_pngs(artifacts)
    info(TOOL, f"artifacts in {artifacts}")
    if bless:
        env["SCENEPROBE_BLESS"] = "1"
    return artifacts


# ---- the gate ---------------------------------------------------------------


@dataclass(frozen=True, slots=True)
class _SelfTest:
    """One self-test scene and the exit code that proves the gate's wiring."""

    filename: str
    expected_exit: int
    broken_message: str


SELF_TESTS = (
    _SelfTest("selftest-good.qml", 0, "GATE BROKEN: selftest-good failed"),
    _SelfTest("selftest-bad.qml", 1, "GATE BROKEN: selftest-bad exited {actual}, expected 1"),
    _SelfTest(
        "selftest-blank.qml",
        1,
        "GATE BROKEN: selftest-blank exited {actual}, expected 1 (output floor)",
    ),
)


def _run_scene(probe: Path, scene: Path, env: Mapping[str, str]) -> ProbeRun:
    """Run one scene through a fresh nested compositor, capturing its output.

    A fresh compositor per scene matches the bash (each run_in_kwin.sh
    invocation brought its own up) so no render state leaks between scenes.
    """
    return run_in_nested_kwin([str(probe), str(scene)], env, capture=True)


def _run_self_test(probe: Path, scenes: Path, env: Mapping[str, str]) -> int | None:
    """Prove the gate's own pass/fail wiring; return EXIT_GATE_BROKEN or None.

    None means the self-test passed (good passes, bad and blank fail). A
    disagreement prints the GATE BROKEN line and the offending run's output,
    exactly as the bash did before trusting any real verdict.
    """
    for self_test in SELF_TESTS:
        run = _run_scene(probe, scenes / self_test.filename, env)
        if selftest_disagrees(self_test.expected_exit, run.exit_code):
            info(TOOL, self_test.broken_message.format(actual=run.exit_code))
            sys.stdout.write(run.output)
            sys.stdout.flush()
            return EXIT_GATE_BROKEN
    info(TOOL, "self-test ok (good passes, bad fails, blank fails)")
    return None


def _bless_scene(scene: Path, artifacts: Path, device: str) -> None:
    """Copy a passing scene's rendered candidate over its committed golden.

    Only runs under --bless and only for a scene that just passed; a missing
    candidate is skipped quietly (the bash ``[[ -f "$cand" ]]``).
    """
    candidate = artifacts / f"{scene.stem}.actual.png"
    if candidate.is_file():
        golden = scene.parent / f"{scene.stem}.expected.{device}.png"
        shutil.copyfile(candidate, golden)
        print(f"  blessed {golden.name}")


def _run_scenes(
    probe: Path, scenes: Path, artifacts: Path, env: Mapping[str, str], *, bless: bool
) -> int:
    """Run every real scene (skipping the self-tests); return the failure count."""
    device = env.get("SCENEPROBE_DEVICE") or "lavapipe"
    fails = 0
    for scene in sorted(scenes.glob("*.qml")):
        if scene.name.startswith("selftest-"):
            continue
        run = _run_scene(probe, scene, env)
        if scene_passed(run.exit_code):
            print(f"PASS  {scene.name}")
            if bless:
                _bless_scene(scene, artifacts, device)
        else:
            print(f"FAIL  {scene.name}")
            sys.stdout.write(run.output)
            sys.stdout.flush()
            fails += 1
    return fails


def run_gate(repo: Path, *, bless: bool) -> int:
    """Stage the QML modules, self-test, run every scene, return the verdict.

    0 all scenes passed, 1 at least one real scene failed, 2 no probe binary,
    3 the self-test found the gate broken.
    """
    here = repo / "tests" / "sceneprobe"
    setup = resolve_qml_import_setup(repo, os.environ)

    probe = setup.build / "bin" / "latte-sceneprobe"
    if not os.access(probe, os.X_OK):
        fail(TOOL, f"no latte-sceneprobe at {probe} (build first)", EXIT_SETUP)

    # Stage the current source's Latte QML modules so scenes import the real
    # org.kde.latte.* trees, same as every other QML gate (qml_env_stage).
    qmlenv.stage_qml_modules(setup.build, setup.stage)

    artifacts = _export_probe_env(here, setup, os.environ, bless=bless)

    scenes = here / "scenes"
    broken = _run_self_test(probe, scenes, os.environ)
    if broken is not None:
        return broken

    fails = _run_scenes(probe, scenes, artifacts, os.environ, bless=bless)
    if fails == 0:
        info(TOOL, "PASS")
        return 0
    info(TOOL, f"FAIL ({fails} scene(s); actual/expected/diff triples in {artifacts})")
    return 1


# ---- entry points -----------------------------------------------------------


def _run_in_kwin_command(command: Sequence[str]) -> int:
    """The run_in_kwin.sh replacement: run ``command`` under nested kwin.

    Streams the command's output (no capture) and exits with its code, exactly
    as the standalone wrapper did. A device dispatch or socket-bind failure
    prints its message to stderr and exits 2.
    """
    if not command:
        fail(TOOL, "run-in-kwin needs a command to run", EXIT_SETUP)
    run = run_in_nested_kwin(command, os.environ, capture=False)
    if run.output:
        print(run.output, file=sys.stderr, flush=True)
    return run.exit_code


def main(argv: Sequence[str] | None = None) -> None:
    args = list(sys.argv[1:] if argv is None else argv)
    install_conventional_signal_exits()

    # run-in-kwin carries an arbitrary command that brings its own flags and a
    # leading `--` (e.g. `dbus-run-session -- latte-sceneprobe scene.qml`), which
    # argparse's REMAINDER mishandles; peel it off by hand before argparse.
    if args and args[0] == "run-in-kwin":
        raise SystemExit(_run_in_kwin_command(args[1:]))

    parser = argparse.ArgumentParser(
        prog="latte_harness.sceneprobe_gate",
        description="Deterministic render gate: latte-sceneprobe over every committed scene.",
    )
    parser.add_argument(
        "--bless",
        action="store_true",
        help="re-bless goldens from passing scenes' rendered candidates",
    )
    parsed = parser.parse_args(args)
    raise SystemExit(run_gate(RepoPaths.discover().root, bless=parsed.bless))


if __name__ == "__main__":
    main()
