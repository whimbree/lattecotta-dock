# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
"""Installed-package gate: validate and smoke-test one explicit Latte package.

The typed port of scripts/installed-package-gate.sh (BP-4a, the
bash-to-python migration's package-gate engine chunk). Native package
recipes own package-manager installation; this engine consumes the
resulting filesystem root and prefix without consulting PATH, restaging
the build, or falling back to another Latte installation.
Install-artifact assertions were informed by latte-dock-ng's
docker/verify-install.sh at 9c12a79aaf9350e73059da5b293c931218419c05
(github.com/ruizhi-lab/latte-dock-ng); this implementation is original.

The refusal taxonomy is a contract: every FAIL diagnostic and exit code
is preserved verbatim from the bash engine because the unported selftest
(tests/installed-package-gate-selftest.sh, BP-4b) matches them, and they
are what a packager acts on. Exit codes: 0 pass, 2 refusal, 130/143 on
SIGINT/SIGTERM, with cleanup running on every exit path.

The nested-runtime half rides latte_harness.vehicle (the BP-2a port of
the compositor lifecycle): the dock and the compositor each run in their
own session/process group, and every teardown goes through
vehicle.stop_process_group with the leader-identity gate - the BP-4
convergence the bash engine's cleanup comment recorded.
"""

from __future__ import annotations

import os
import shutil
import signal
import subprocess
import sys
import time
from collections.abc import Callable, Sequence
from contextlib import suppress
from dataclasses import dataclass, field
from pathlib import Path
from typing import NoReturn

from pydantic import JsonValue

from latte_harness import package_gate_audit as audit
from latte_harness import vehicle
from latte_harness.paths import find_repo_root
from latte_harness.proc import SessionProcess, install_conventional_signal_exits

TOOL = "installed-package-gate"

USAGE = """\
Usage: scripts/installed-package-gate.sh --root ROOT [--prefix PREFIX]
       [--manifest MANIFEST] [--check-only]

  --root ROOT      Package filesystem root. Use / after installing the native
                   package in a clean container, or a package staging root.
  --prefix PREFIX  Absolute install prefix inside ROOT (default: /usr).
  --manifest FILE  Newline-delimited package manifest. Entries are absolute
                   paths in ROOT's namespace (for example /usr/bin/latte-dock).
                   Required for --root /; optional for isolated extraction roots.
  --check-only     Validate artifact provenance without starting nested KWin.

LATTE_QML_MODULE_PATH must be an explicit colon-separated allow-list of the
distro's Qt/KF6/Plasma QML roots. LATTE_RUNTIME_DATA_PATH optionally supplies
the corresponding data roots (default: /usr/local/share:/usr/share).
"""

# The validation-phase tool contract, checked before anything else (even
# argument parsing, matching the bash order). awk/jq/perl stay required
# although parsing moved into Python: the list is the documented tool
# contract for packagers and the selftest drives its refusals; shrinking
# it is a BP-4b decision. The exec shim (scripts/installed-package-gate.sh)
# carries a bash copy of this list for the interpreter-less refusal path;
# a unit test pins the two in lockstep.
VALIDATION_COMMANDS: tuple[str, ...] = (
    "awk",
    "cat",
    "dirname",
    "env",
    "find",
    "jq",
    "mktemp",
    "perl",
    "readelf",
    "readlink",
    "realpath",
    "rm",
    "timeout",
    "tr",
)

RUNTIME_COMMANDS: tuple[str, ...] = (
    "busctl",
    "cat",
    "chmod",
    "dbus-run-session",
    "env",
    "find",
    "kwin_wayland",
    "mkdir",
    "mktemp",
    "pgrep",
    "rm",
    "seq",
    "setsid",
    "sh",
    "sleep",
    "tail",
    "tr",
)

# The dock's settle bound: 90 one-second polls of the D-Bus lifecycle
# readback, requiring two consecutive identical viewsData replies.
DOCK_SETTLE_ATTEMPTS = 90
# The post-SIGTERM group-exit bound: 125 polls at 0.2s (25 seconds).
DOCK_SHUTDOWN_ATTEMPTS = 125
DOCK_SHUTDOWN_DELAY = 0.2

NESTED_SOCKET = "latte-installed-gate-wl"
NESTED_WIDTH = 1600
NESTED_HEIGHT = 1000

# The perl DynaLoader probe the bash engine used, kept as a subprocess:
# a plugin with a hanging constructor must be killable from outside
# (timeout TERM-then-KILL) and a crashing one must not take the gate
# process down, which an in-process dlopen could not guarantee.
_DYNALOADER_PROBE = """\
my $handle = DynaLoader::dl_load_file($ARGV[0], 0x02);
die DynaLoader::dl_error() unless $handle;
"""


class GateRefusal(Exception):
    """A gate refusal; the message is the exact FAIL diagnostic text."""


class _UsageRequested(Exception):
    """-h/--help: print usage and exit 0."""


def _print_fail(message: str) -> None:
    print(f"{TOOL}: FAIL: {message}", file=sys.stderr, flush=True)


def fail(message: str) -> NoReturn:
    raise GateRefusal(message)


def _fail_after(inner_diagnostic: str, outer_message: str) -> NoReturn:
    """Print a lib-level diagnostic, then refuse with the engine's own.

    The bash pattern ``helper || fail "..."`` printed both the helper's
    FAIL line and the engine's; both lines are part of the taxonomy.
    """
    _print_fail(inner_diagnostic)
    fail(outer_message)


def require_commands(phase: str, commands: Sequence[str]) -> None:
    for command in commands:
        if shutil.which(command) is None:
            fail(f"required {phase} command '{command}' is missing")


# ---- path resolution ---------------------------------------------------------


def normalize_lexically(raw: str) -> str | None:
    """GNU ``realpath -ms``: collapse . and .. lexically, never touch symlinks.

    Deliberately lexical: the package-namespace walk below re-resolves ..
    against the walked (not the host) tree, which is the whole point of
    doing normalization separately from symlink resolution.
    """
    if not raw:
        return None
    path = raw if raw.startswith("/") else os.path.join(os.getcwd(), raw)
    normalized = os.path.normpath(path)
    if normalized.startswith("//") and not normalized.startswith("///"):
        # POSIX lets normpath keep a leading double slash; GNU realpath does not.
        normalized = normalized[1:]
    return normalized


def resolve_following_symlinks(raw: str) -> str | None:
    """GNU ``realpath`` default: resolve symlinks, all but the last component
    must exist. None mirrors realpath's nonzero exit."""
    try:
        resolved = os.path.realpath(raw, strict=os.path.ALLOW_MISSING)
    except OSError:
        return None
    if not os.path.exists(resolved) and not os.path.isdir(os.path.dirname(resolved)):
        # ALLOW_MISSING tolerates missing intermediate components; GNU
        # realpath requires everything but the final component to exist.
        return None
    return resolved


@dataclass
class GateContext:
    """The resolved run parameters every check reads."""

    repo: str
    package_root: str = ""
    artifact_prefix: str = ""
    manifest_paths: set[str] = field(default_factory=set)
    manifest_enforced: bool = False


def resolve_native_path(ctx: GateContext, label: str, raw: str) -> str:
    """Resolve a host path, refusing every development-provenance origin."""
    if raw.startswith("/nix/store/"):
        fail(
            f"{label} points into /nix/store ({raw}); native package validation "
            "must not consume the Nix package or development closure"
        )
    if raw.endswith("/_qmlstage") or "/_qmlstage/" in raw:
        fail(
            f"{label} points into a development _qmlstage ({raw}); install the "
            "native package into the caller-supplied root first"
        )
    resolved = resolve_following_symlinks(raw)
    if resolved is None:
        fail(f"{label} does not exist or cannot be resolved: {raw}")
    if resolved.startswith("/nix/store/"):
        fail(
            f"{label} resolves into /nix/store ({resolved}); native package "
            "validation must not consume the Nix package or development closure"
        )
    if resolved == ctx.repo or resolved.startswith(ctx.repo + "/"):
        fail(
            f"{label} resolves inside the source/build tree ({resolved}); "
            "pass an installed package root, not the checkout"
        )
    if resolved.endswith("/_qmlstage") or "/_qmlstage/" in resolved:
        fail(
            f"{label} resolves into a development _qmlstage ({resolved}); "
            "pass an installed package root"
        )
    return resolved


def resolve_package_namespace_path(ctx: GateContext, label: str, raw: str) -> str:
    """Resolve a path inside the package namespace, component by component.

    realpath follows absolute links from host /. Walk each component so an
    absolute link inside an extraction root restarts from that package root.
    """
    if ctx.package_root == "/":
        return resolve_native_path(ctx, label, raw)

    normalized = normalize_lexically(raw)
    if normalized is None:
        fail(f"{label} cannot be normalized in the package namespace: {raw}")
    if not audit.path_is_within(normalized, ctx.package_root):
        fail(f"{label} escapes the package root: {raw}")

    def pending_of(namespace_path: str) -> str:
        relative = namespace_path[len(ctx.package_root) :]
        return relative[1:] if relative.startswith("/") else relative

    pending = pending_of(normalized)
    resolved = ctx.package_root
    link_count = 0
    while pending:
        component, _, remainder = pending.partition("/")
        candidate = f"{resolved}/{component}"
        if os.path.islink(candidate):
            link_count += 1
            if link_count > 40:
                fail(f"{label} contains more than 40 chained symlinks: {raw}")
            try:
                target = os.readlink(candidate)
            except OSError:
                fail(f"{label} contains an unreadable symlink: {candidate}")
            if target.startswith("/"):
                candidate = f"{ctx.package_root}/{target[1:]}"
            else:
                candidate = f"{resolved}/{target}"
            if remainder:
                candidate = f"{candidate}/{remainder}"
            normalized = normalize_lexically(candidate)
            if normalized is None:
                fail(f"{label} cannot normalize a chained symlink target: {candidate}")
            if not audit.path_is_within(normalized, ctx.package_root):
                fail(f"{label} escapes the package root through a symlink: {normalized}")
            pending = pending_of(normalized)
            resolved = ctx.package_root
            continue
        resolved = candidate
        pending = remainder
    if not os.path.exists(resolved):
        fail(f"{label} does not exist in the package namespace: {raw}")
    return resolved


def _resolve_in_namespace_or(ctx: GateContext, label: str, raw: str, outer_message: str) -> str:
    """resolve_package_namespace_path with the bash two-line || fail wrap."""
    try:
        return resolve_package_namespace_path(ctx, label, raw)
    except GateRefusal as inner:
        _print_fail(str(inner))
        fail(outer_message)


# ---- manifest ownership and package files ------------------------------------


def require_manifest_ownership(ctx: GateContext, label: str, path: str) -> None:
    if not ctx.manifest_enforced:
        return
    logical_path = normalize_lexically(path)
    if logical_path is None:
        fail(f"cannot normalize {label} for package-manifest ownership: {path}")
    if logical_path not in ctx.manifest_paths:
        fail(
            f"{label} is present under the package prefix but omitted by the "
            f"package manifest: {logical_path}"
        )


def require_package_file(ctx: GateContext, label: str, path: str, allowed_tree: str) -> str:
    """Prove one installed file: present, manifest-owned, a regular file
    resolving inside its allowed tree. Returns the resolved path."""
    if not (os.path.exists(path) or os.path.islink(path)):
        fail(f"package is incomplete: missing {label} at {path}")
    require_manifest_ownership(ctx, f"installed {label}", path)
    resolved = _resolve_in_namespace_or(
        ctx,
        f"installed {label}",
        path,
        f"installed {label} cannot be resolved in the package namespace: {path}",
    )
    if not os.path.isfile(resolved):
        fail(f"installed {label} does not resolve to a regular file: {resolved}")
    if not audit.path_is_within(resolved, allowed_tree):
        fail(
            f"installed {label} resolves outside its allowed package tree "
            f"{allowed_tree}: {resolved}"
        )
    require_manifest_ownership(ctx, f"resolved installed {label}", resolved)
    return resolved


def collect_find_results(label: str, find_arguments: Sequence[str]) -> list[str]:
    """Run find and refuse a partial scan.

    The external find binary is load-bearing, not convenience: the
    selftest injects hostile find producers via PATH to prove a truncated
    scan can never pass validation, so the scan must go through PATH's
    find and its exit code must gate the result.
    """
    result = subprocess.run(
        ["find", *find_arguments, "-print0"],
        stdout=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        fail(f"{label} scan failed before a complete result was available")
    return [os.fsdecode(chunk) for chunk in result.stdout.split(b"\0") if chunk]


def require_one_match(label: str, matches: Sequence[str]) -> str:
    if len(matches) != 1:
        joined = " ".join(matches) if matches else "none"
        fail(f"expected exactly one installed {label}, found {len(matches)} ({joined})")
    return matches[0]


# ---- package-tree and ELF audits ---------------------------------------------


def audit_package_tree(ctx: GateContext, label: str, tree: str) -> None:
    """Audit one installed runtime tree: manifest ownership of every entry
    and full provenance of every symlink (no escape from the package root
    or the tree, no development or /nix/store provider)."""
    if os.path.islink(tree):
        fail(f"{label} root must not be a symlink: {tree}")
    if not os.path.isdir(tree):
        fail(f"package is incomplete: missing {label} at {tree}")
    tree_resolved = resolve_native_path(ctx, f"installed {label}", tree)
    if not audit.path_is_within(tree_resolved, ctx.artifact_prefix):
        fail(f"installed {label} resolves outside the package prefix: {tree_resolved}")

    entries = collect_find_results(
        f"{label} contents", [tree, "(", "-type", "f", "-o", "-type", "l", ")"]
    )
    links: list[str] = []
    for entry in entries:
        require_manifest_ownership(ctx, f"{label} content", entry)
        if os.path.islink(entry):
            links.append(entry)
    for link in links:
        try:
            target = os.readlink(link)
        except OSError:
            fail(f"{label} contains an unreadable symlink: {link}")
        if target.startswith("/") and ctx.package_root != "/":
            target_candidate = f"{ctx.package_root}/{target[1:]}"
        elif target.startswith("/"):
            target_candidate = target
        else:
            target_candidate = f"{os.path.dirname(link)}/{target}"
        target_normalized = normalize_lexically(target_candidate)
        if target_normalized is None:
            fail(f"{label} symlink target cannot be normalized: {link} -> {target}")
        if not audit.path_is_within(target_normalized, ctx.package_root):
            fail(
                f"{label} contains a symlink target escaping the package root: "
                f"{link} -> {target_normalized}"
            )
        if ctx.package_root == "/":
            logical_target = target_normalized
        else:
            logical_target = target_normalized[len(ctx.package_root) :] or "/"
        if logical_target.startswith("/nix/store/"):
            fail(f"{label} contains a symlink into /nix/store: {link} -> {logical_target}")
        link_resolved = _resolve_in_namespace_or(
            ctx,
            f"{label} symlink",
            target_candidate,
            f"{label} contains a broken or unresolvable symlink: {link} -> {target}",
        )
        if link_resolved == ctx.repo or link_resolved.startswith(ctx.repo + "/"):
            fail(
                f"{label} contains a symlink into the source/build tree: {link} -> {link_resolved}"
            )
        if link_resolved.endswith("/_qmlstage") or "/_qmlstage/" in link_resolved:
            fail(
                f"{label} contains a symlink into a development _qmlstage: "
                f"{link} -> {link_resolved}"
            )
        provider_dir = link_resolved
        if not os.path.isdir(provider_dir):
            provider_dir = os.path.dirname(provider_dir)
        # An isolated package root is the provenance boundary already proved
        # above. Markers on its external ancestors describe the staging host,
        # not the installed content. A live --root / check still walks to /.
        while audit.path_is_within(provider_dir, ctx.package_root):
            if os.path.exists(f"{provider_dir}/.git") or os.path.isfile(
                f"{provider_dir}/CMakeLists.txt"
            ):
                fail(
                    f"{label} contains a symlink into a source tree: {link} -> {target_normalized}"
                )
            if os.path.isfile(f"{provider_dir}/CMakeCache.txt") or os.path.isdir(
                f"{provider_dir}/CMakeFiles"
            ):
                fail(
                    f"{label} contains a symlink into a CMake build tree: "
                    f"{link} -> {target_normalized}"
                )
            if provider_dir == ctx.package_root:
                break
            provider_dir = os.path.dirname(provider_dir)
        if not audit.path_is_within(link_resolved, tree_resolved):
            fail(
                f"{label} contains a symlink escaping its installed runtime tree: "
                f"{link} -> {link_resolved}"
            )


def audit_elf_search_paths(
    ctx: GateContext, label: str, elf: str, require_elf: bool = True
) -> None:
    """Audit an artifact's RUNPATH/RPATH: every loader search entry must be
    $ORIGIN-anchored (or absolute only under --root /) and resolve to an
    installed directory inside the package prefix."""
    header_check = subprocess.run(
        ["readelf", "-h", "--", elf],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if header_check.returncode != 0:
        if not require_elf:
            return
        fail(f"{label} is not a valid ELF artifact: {elf}")

    try:
        search_paths = audit.read_elf_search_paths(elf)
    except audit.AuditError as err:
        _fail_after(str(err), f"{label} ELF search metadata could not be read completely")
    origin = os.path.dirname(elf)
    for search_path in search_paths:
        if search_path.startswith(":") or search_path.endswith(":") or "::" in search_path:
            fail(f"{label} ELF RUNPATH/RPATH contains an empty loader search entry: {search_path}")
        entries = search_path.split(":") if search_path else []
        for entry in entries:
            if entry.startswith("/"):
                if ctx.package_root != "/":
                    fail(
                        f"{label} ELF RUNPATH/RPATH uses an absolute entry that the "
                        f"loader cannot interpret inside isolated package root "
                        f"{ctx.package_root}: {entry}; use $ORIGIN-relative paths"
                    )
                expanded = entry
            elif "${ORIGIN}" in entry or "$ORIGIN" in entry:
                expanded = entry.replace("${ORIGIN}", origin).replace("$ORIGIN", origin)
            else:
                fail(
                    f"{label} ELF RUNPATH/RPATH entry is relative to ambient working state: {entry}"
                )
            if "$" in expanded:
                fail(f"{label} ELF RUNPATH/RPATH uses an unsupported dynamic loader token: {entry}")
            normalized = normalize_lexically(expanded)
            if normalized is None:
                fail(f"{label} ELF RUNPATH/RPATH entry cannot be normalized: {entry}")
            resolved = _resolve_in_namespace_or(
                ctx,
                f"{label} ELF RUNPATH/RPATH entry",
                normalized,
                f"{label} ELF RUNPATH/RPATH entry cannot be resolved in the "
                f"package namespace: {entry}",
            )
            if not os.path.isdir(resolved):
                fail(
                    f"{label} ELF RUNPATH/RPATH entry is not an installed directory: "
                    f"{entry} -> {resolved}"
                )
            if not audit.path_is_within(resolved, ctx.artifact_prefix):
                fail(
                    f"{label} ELF RUNPATH/RPATH entry escapes the package prefix: "
                    f"{entry} -> {resolved}"
                )


# ---- plugin loading and metadata ---------------------------------------------


def require_loadable_plugin(label: str, plugin: str) -> None:
    """dlopen the plugin in a disposable perl process under a bounded timeout."""
    result = subprocess.run(
        [
            "timeout",
            "--kill-after=1s",
            "5s",
            "env",
            "LD_BIND_NOW=1",
            "perl",
            "-MDynaLoader",
            "-e",
            _DYNALOADER_PROBE,
            plugin,
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    if result.returncode == 0:
        return
    if audit.shell_wait_status(result.returncode) in (124, 137):
        fail(f"{label} loader timed out for installed artifact {plugin}")
    loader_output = audit.drop_nul_bytes(result.stdout).rstrip("\n")
    fail(f"{label} cannot be loaded from the installed artifact {plugin}: {loader_output}")


@dataclass(frozen=True)
class PluginContract:
    """One installed plugin's expected Qt metadata identity."""

    label: str
    iid: str
    class_name: str
    declares_contract: Callable[[JsonValue], bool]
    contract_description: str


def require_plugin_metadata(qt_plugin_info: str, contract: PluginContract, plugin: str) -> None:
    result = subprocess.run(
        [
            "timeout",
            "--kill-after=1s",
            "5s",
            qt_plugin_info,
            "--full-json",
            "-f",
            "compact",
            plugin,
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    metadata_output = audit.drop_nul_bytes(result.stdout).rstrip("\n")
    if result.returncode != 0:
        if audit.shell_wait_status(result.returncode) in (124, 137):
            fail(f"{contract.label} metadata inspection timed out for {plugin}")
        fail(f"{contract.label} has no valid Qt plugin metadata at {plugin}: {metadata_output}")
    metadata = audit.load_plugin_metadata_json(metadata_output)
    iid = metadata.get("IID") if isinstance(metadata, dict) else None
    if not isinstance(iid, str):
        fail(f"{contract.label} metadata has no string IID at {plugin}")
    class_name = metadata.get("className") if isinstance(metadata, dict) else None
    if not isinstance(class_name, str):
        fail(f"{contract.label} metadata has no string className at {plugin}")
    if iid != contract.iid:
        fail(f"{contract.label} has IID '{iid}', expected '{contract.iid}'")
    if class_name != contract.class_name:
        fail(f"{contract.label} has class '{class_name}', expected '{contract.class_name}'")
    if not contract.declares_contract(metadata):
        fail(f"{contract.label} metadata does not declare {contract.contract_description}")


def require_appstream_metadata(metadata_path: str) -> None:
    try:
        xml = Path(metadata_path).read_text(errors="surrogateescape")
    except OSError as err:
        # The perl validator died with its open error; carry the same shape.
        fail(
            "installed AppStream metadata violates the standalone application "
            f"contract: cannot open {metadata_path}: {err.strerror}"
        )
    diagnostic = audit.validate_appstream_metadata(xml)
    if diagnostic is not None:
        fail(
            f"installed AppStream metadata violates the standalone application "
            f"contract: {diagnostic}"
        )


def _always_declared(_metadata: JsonValue) -> bool:
    return True


PLUGIN_CONTRACTS: tuple[PluginContract, ...] = (
    PluginContract(
        "Latte core QML plugin",
        "org.qt-project.Qt.QQmlExtensionInterface",
        "LatteCorePlugin",
        _always_declared,
        "the Latte core QML extension type",
    ),
    PluginContract(
        "Latte containment QML plugin",
        "org.qt-project.Qt.QQmlExtensionInterface",
        "LatteContainmentPlugin",
        _always_declared,
        "the Latte containment QML extension type",
    ),
    PluginContract(
        "Latte tasks QML plugin",
        "org.qt-project.Qt.QQmlExtensionInterface",
        "LatteTasksPlugin",
        _always_declared,
        "the Latte tasks QML extension type",
    ),
    PluginContract(
        "Latte containment-actions plugin",
        "org.kde.KPluginFactory",
        "MenuFactory",
        audit.declares_containment_actions_contract,
        "the org.kde.latte.contextmenu Plasma/ContainmentActions type",
    ),
    PluginContract(
        "Latte indicator package-structure plugin",
        "org.kde.KPluginFactory",
        "latte_packagestructure_indicator_factory",
        audit.declares_indicator_structure_contract,
        "the Latte/Indicator package-structure type for org.kde.latte-dock",
    ),
)


# ---- argument contract -------------------------------------------------------


@dataclass(frozen=True)
class GateArguments:
    root_raw: str
    prefix: str
    manifest_raw: str
    check_only: bool


def parse_gate_arguments(argv: Sequence[str]) -> GateArguments:
    root_raw = ""
    prefix = "/usr"
    manifest_raw = ""
    check_only = False
    remaining = list(argv)
    while remaining:
        argument = remaining.pop(0)
        if argument == "--root":
            if not remaining:
                fail("--root needs a value")
            root_raw = remaining.pop(0)
        elif argument == "--prefix":
            if not remaining:
                fail("--prefix needs a value")
            prefix = remaining.pop(0)
        elif argument == "--manifest":
            if not remaining:
                fail("--manifest needs a value")
            manifest_raw = remaining.pop(0)
        elif argument == "--check-only":
            check_only = True
        elif argument in ("-h", "--help"):
            raise _UsageRequested
        else:
            fail(f"unknown argument '{argument}' (see --help)")

    if not root_raw:
        fail("--root is required; implicit system-package lookup is forbidden")
    if not root_raw.startswith("/"):
        fail(f"--root must be absolute: {root_raw}")
    if not prefix.startswith("/"):
        fail(f"--prefix must be absolute: {prefix}")
    if manifest_raw and not manifest_raw.startswith("/"):
        fail(f"--manifest must be absolute: {manifest_raw}")
    if "/../" in prefix or prefix.endswith("/..") or "/./" in prefix:
        fail(f"--prefix must not contain . or .. components: {prefix}")
    return GateArguments(root_raw, prefix, manifest_raw, check_only)


def _read_manifest_entries(manifest_raw: str) -> list[str]:
    """The manifest's lines, with bash mapfile semantics (a final segment
    without a trailing newline still counts; interior empties survive)."""
    try:
        # newline="" disables universal-newline translation: a \r\n line must
        # surface its carriage return to the CR refusal, as bash mapfile did.
        content = Path(manifest_raw).read_text(newline="", errors="surrogateescape")
    except OSError:
        fail(f"package manifest could not be read completely: {manifest_raw}")
    if content == "":
        return []
    lines = content.split("\n")
    if content.endswith("\n"):
        lines.pop()
    return lines


def enforce_manifest_ownership_set(ctx: GateContext, arguments: GateArguments) -> None:
    """Validate the manifest and load its normalized host paths into ctx."""
    manifest_raw = arguments.manifest_raw
    if not os.path.isfile(manifest_raw):
        fail(f"package manifest is missing or not a regular file: {manifest_raw}")
    manifest_entries = _read_manifest_entries(manifest_raw)
    if not manifest_entries:
        fail(f"package manifest is empty: {manifest_raw}")
    for manifest_entry in manifest_entries:
        if not manifest_entry:
            fail(f"package manifest contains an empty entry: {manifest_raw}")
        if "\r" in manifest_entry:
            fail(f"package manifest contains a carriage return: {manifest_entry}")
        if not manifest_entry.startswith("/"):
            fail(
                "package manifest entries must be absolute in the package "
                f"namespace: {manifest_entry}"
            )
        if ctx.package_root == "/":
            manifest_host_path = manifest_entry
        else:
            manifest_host_path = f"{ctx.package_root}/{manifest_entry[1:]}"
        normalized_host_path = normalize_lexically(manifest_host_path)
        if normalized_host_path is None:
            fail(f"package manifest entry cannot be normalized: {manifest_entry}")
        if not audit.path_is_within(normalized_host_path, ctx.artifact_prefix):
            fail(f"package manifest entry is outside the package prefix: {manifest_entry}")
        if normalized_host_path in ctx.manifest_paths:
            fail(f"package manifest contains a duplicate entry: {manifest_entry}")
        ctx.manifest_paths.add(normalized_host_path)
    ctx.manifest_enforced = True


# ---- the validation (check) phase --------------------------------------------


@dataclass
class ValidatedPackage:
    """What the check phase proves and the runtime phase consumes."""

    binary: str
    package_qml: str
    package_plugins: str
    package_data: str
    qml_import_path: str
    xdg_data_dirs: str
    plugin_paths: tuple[str, ...]


def _resolve_package_roots(ctx: GateContext, arguments: GateArguments) -> None:
    ctx.package_root = resolve_native_path(ctx, "package root", arguments.root_raw)
    if os.path.exists(f"{ctx.package_root}/.git") or os.path.isfile(
        f"{ctx.package_root}/CMakeLists.txt"
    ):
        fail(
            f"package root is a source tree ({ctx.package_root}); install the "
            "native package into a separate filesystem root first"
        )
    if os.path.isfile(f"{ctx.package_root}/CMakeCache.txt") or os.path.isdir(
        f"{ctx.package_root}/CMakeFiles"
    ):
        fail(
            f"package root is a CMake build tree ({ctx.package_root}); "
            "pass the package installation root instead"
        )
    if ctx.package_root == "/":
        prefix_path = arguments.prefix
    else:
        prefix_path = f"{ctx.package_root}/{arguments.prefix[1:]}"
    ctx.artifact_prefix = resolve_native_path(ctx, "package prefix", prefix_path)
    if not audit.path_is_within(ctx.artifact_prefix, ctx.package_root):
        fail(
            f"package prefix escapes package root: {ctx.artifact_prefix} is "
            f"outside {ctx.package_root}"
        )


def _discover_library_roots(ctx: GateContext) -> list[str]:
    library_roots: list[str] = []
    for candidate in (f"{ctx.artifact_prefix}/lib", f"{ctx.artifact_prefix}/lib64"):
        if not os.path.isdir(candidate):
            continue
        resolved = resolve_native_path(ctx, "package library root", candidate)
        if not audit.path_is_within(resolved, ctx.artifact_prefix):
            fail(f"package library root escapes the package prefix: {resolved}")
        if resolved not in library_roots:
            library_roots.append(resolved)
    if not library_roots:
        fail(f"package prefix has no lib or lib64 directory: {ctx.artifact_prefix}")
    return library_roots


def _assemble_qml_import_path(ctx: GateContext, package_qml: str) -> str:
    allowlist = os.environ.get("LATTE_QML_MODULE_PATH", "")
    if not allowlist:
        # bash used ${VAR:?message}: a distinct exit 1 with the same message.
        print(
            f"{TOOL}: LATTE_QML_MODULE_PATH: installed-package-gate needs an "
            "explicit distro QML allow-list in LATTE_QML_MODULE_PATH",
            file=sys.stderr,
            flush=True,
        )
        raise SystemExit(1)
    if allowlist.startswith(":") or allowlist.endswith(":") or "::" in allowlist:
        fail(
            "LATTE_QML_MODULE_PATH contains an empty entry, which would search an ambient directory"
        )
    qml_imports: list[str] = []
    for candidate in allowlist.split(":"):
        if not candidate.startswith("/"):
            fail(f"LATTE_QML_MODULE_PATH entries must be absolute: {candidate}")
        resolved = resolve_native_path(ctx, "QML allow-list entry", candidate)
        if os.path.exists(f"{resolved}/org/kde/latte") and resolved != package_qml:
            fail(
                f"QML allow-list entry {resolved} contains a foreign org/kde/latte "
                "tree; remove the preinstalled Latte package before validation"
            )
        if resolved not in qml_imports and resolved != package_qml:
            qml_imports.append(resolved)
    # The package's Latte modules are last, matching lib-qml-env.sh's precedence:
    # later entries win and no dependency root can shadow org.kde.latte.*.
    qml_imports.append(package_qml)
    return ":".join(qml_imports)


def _assemble_runtime_data_dirs(ctx: GateContext, package_data: str) -> str:
    runtime_data_raw = os.environ.get("LATTE_RUNTIME_DATA_PATH") or "/usr/local/share:/usr/share"
    if (
        runtime_data_raw.startswith(":")
        or runtime_data_raw.endswith(":")
        or "::" in runtime_data_raw
    ):
        fail("LATTE_RUNTIME_DATA_PATH contains an empty entry")
    runtime_data = [package_data]
    for candidate in runtime_data_raw.split(":"):
        if not candidate.startswith("/"):
            fail(f"LATTE_RUNTIME_DATA_PATH entries must be absolute: {candidate}")
        if not os.path.isdir(candidate):
            continue
        resolved = resolve_native_path(ctx, "runtime data allow-list entry", candidate)
        if resolved == package_data:
            continue
        for marker in (
            "plasma/shells/org.kde.latte.shell",
            "plasma/plasmoids/org.kde.latte.containment",
            "plasma/plasmoids/org.kde.latte.plasmoid",
            "latte/indicators",
        ):
            if os.path.exists(f"{resolved}/{marker}"):
                fail(
                    f"runtime data root {resolved} contains foreign Latte data "
                    f"({marker}); remove the preinstalled package before validation"
                )
        runtime_data.append(resolved)
    return ":".join(runtime_data)


def validate_installed_package(
    ctx: GateContext, arguments: GateArguments, qt_plugin_info: str
) -> ValidatedPackage:
    """The full provenance/completeness validation (the --check-only scope)."""
    _resolve_package_roots(ctx, arguments)

    if ctx.package_root == "/" and not arguments.manifest_raw:
        fail(
            "--manifest is required with --root / so preinstalled same-prefix "
            "artifacts cannot satisfy the package gate"
        )
    if arguments.manifest_raw:
        enforce_manifest_ownership_set(ctx, arguments)

    binary_path = f"{ctx.artifact_prefix}/bin/latte-dock"
    if not (os.path.exists(binary_path) or os.path.islink(binary_path)):
        fail(
            f"installed binary is missing or not executable at {binary_path}; "
            "PATH fallback is forbidden"
        )
    binary = require_package_file(ctx, "binary", binary_path, ctx.artifact_prefix)
    if not os.access(binary, os.X_OK):
        fail(f"installed binary target is not executable at {binary}; PATH fallback is forbidden")

    library_roots = _discover_library_roots(ctx)

    qml_manifests = collect_find_results(
        "Latte QML manifest discovery",
        [
            *library_roots,
            "(",
            "-type",
            "f",
            "-o",
            "-type",
            "l",
            ")",
            "-path",
            "*/org/kde/latte/core/qmldir",
        ],
    )
    qml_manifest = require_one_match("org.kde.latte.core/qmldir", qml_manifests)
    package_qml = qml_manifest.removesuffix("/org/kde/latte/core/qmldir")
    package_qml = resolve_native_path(ctx, "installed Latte QML root", package_qml)
    if not audit.path_is_within(package_qml, ctx.artifact_prefix):
        fail(f"installed Latte QML root escapes the package prefix: {package_qml}")
    require_package_file(
        ctx, "core QML module metadata", qml_manifest, f"{package_qml}/org/kde/latte/core"
    )

    latte_qml_tree = f"{package_qml}/org/kde/latte"
    core_plugin = require_package_file(
        ctx,
        "core QML plugin",
        f"{package_qml}/org/kde/latte/core/liblattecoreplugin.so",
        latte_qml_tree,
    )
    containment_plugin = require_package_file(
        ctx,
        "containment QML plugin",
        f"{package_qml}/org/kde/latte/private/containment/liblattecontainmentplugin.so",
        latte_qml_tree,
    )
    tasks_plugin = require_package_file(
        ctx,
        "tasks QML plugin",
        f"{package_qml}/org/kde/latte/private/tasks/liblattetasksplugin.so",
        latte_qml_tree,
    )
    require_package_file(
        ctx,
        "containment QML module metadata",
        f"{package_qml}/org/kde/latte/private/containment/qmldir",
        f"{package_qml}/org/kde/latte/private/containment",
    )
    require_package_file(
        ctx,
        "tasks QML module metadata",
        f"{package_qml}/org/kde/latte/private/tasks/qmldir",
        f"{package_qml}/org/kde/latte/private/tasks",
    )
    audit_package_tree(ctx, "Latte QML tree", latte_qml_tree)

    action_plugins = collect_find_results(
        "Latte containment-actions plugin discovery",
        [
            *library_roots,
            "(",
            "-type",
            "f",
            "-o",
            "-type",
            "l",
            ")",
            "-path",
            "*/plasma/containmentactions/org.kde.latte.contextmenu.so",
        ],
    )
    action_plugin_path = require_one_match("Latte containment-actions plugin", action_plugins)
    package_plugins = action_plugin_path.removesuffix(
        "/plasma/containmentactions/org.kde.latte.contextmenu.so"
    )
    package_plugins = resolve_native_path(ctx, "installed Latte plugin root", package_plugins)
    if not audit.path_is_within(package_plugins, ctx.artifact_prefix):
        fail(f"installed Latte plugin root escapes the package prefix: {package_plugins}")
    action_plugin = require_package_file(
        ctx,
        "Latte containment-actions plugin",
        action_plugin_path,
        f"{package_plugins}/plasma/containmentactions",
    )
    indicator_package_plugin = require_package_file(
        ctx,
        "Latte indicator package-structure plugin",
        f"{package_plugins}/kpackage/packagestructure/latte_indicator.so",
        f"{package_plugins}/kpackage/packagestructure",
    )

    package_data = resolve_native_path(
        ctx, "installed Latte data root", f"{ctx.artifact_prefix}/share"
    )
    if not audit.path_is_within(package_data, ctx.artifact_prefix):
        fail(f"installed Latte data root escapes the package prefix: {package_data}")
    shell_package = f"{package_data}/plasma/shells/org.kde.latte.shell"
    containment_package = f"{package_data}/plasma/plasmoids/org.kde.latte.containment"
    tasks_package = f"{package_data}/plasma/plasmoids/org.kde.latte.plasmoid"
    require_package_file(
        ctx, "shell package metadata", f"{shell_package}/metadata.json", shell_package
    )
    require_package_file(
        ctx,
        "containment package metadata",
        f"{containment_package}/metadata.json",
        containment_package,
    )
    require_package_file(
        ctx, "tasks applet package metadata", f"{tasks_package}/metadata.json", tasks_package
    )
    require_package_file(
        ctx,
        "desktop entry",
        f"{package_data}/applications/org.kde.latte-dock.desktop",
        f"{package_data}/applications",
    )
    appstream_metadata = require_package_file(
        ctx,
        "AppStream metadata",
        f"{package_data}/metainfo/org.kde.latte-dock.appdata.xml",
        f"{package_data}/metainfo",
    )
    require_appstream_metadata(appstream_metadata)
    audit_package_tree(ctx, "Latte shell package", shell_package)
    audit_package_tree(ctx, "Latte containment package", containment_package)
    audit_package_tree(ctx, "Latte tasks applet package", tasks_package)
    audit_package_tree(ctx, "Latte data tree", f"{package_data}/latte")
    if not os.path.isdir(f"{package_data}/latte/indicators/default"):
        fail(
            "package is incomplete: missing default indicator under "
            f"{package_data}/latte/indicators"
        )

    audit_elf_search_paths(ctx, "installed binary", binary)
    plugin_paths = (
        core_plugin,
        containment_plugin,
        tasks_plugin,
        action_plugin,
        indicator_package_plugin,
    )
    for contract, plugin in zip(PLUGIN_CONTRACTS, plugin_paths, strict=True):
        audit_elf_search_paths(ctx, contract.label, plugin)
        require_plugin_metadata(qt_plugin_info, contract, plugin)
        require_loadable_plugin(contract.label, plugin)

    qml_import_path = _assemble_qml_import_path(ctx, package_qml)
    xdg_data_dirs = _assemble_runtime_data_dirs(ctx, package_data)

    print(f"{TOOL}: artifact prefix: {ctx.artifact_prefix}", flush=True)
    print(f"{TOOL}: binary: {binary}", flush=True)
    print(f"{TOOL}: Latte QML root: {package_qml}", flush=True)
    print(f"{TOOL}: Latte plugin root: {package_plugins}", flush=True)
    print(f"{TOOL}: Latte data root: {package_data}", flush=True)
    print(f"{TOOL}: QML allow-list: {qml_import_path}", flush=True)

    return ValidatedPackage(
        binary=binary,
        package_qml=package_qml,
        package_plugins=package_plugins,
        package_data=package_data,
        qml_import_path=qml_import_path,
        xdg_data_dirs=xdg_data_dirs,
        plugin_paths=plugin_paths,
    )


# ---- the nested-runtime phase ------------------------------------------------


@dataclass
class RuntimeCleanupState:
    """The live resources the exit path must reclaim, whatever the exit."""

    runtime_dir: Path | None = None
    kwin: SessionProcess | None = None
    kwin_starttime: str | None = None
    dock: SessionProcess | None = None
    dock_starttime: str | None = None


def run_exit_cleanup(state: RuntimeCleanupState) -> int:
    """The bash EXIT-trap cleanup: stop the dock group, stop the compositor
    group (both strict, identity-gated, bounded), then the FUSE unmount and
    runtime-dir removal. Returns 0, or 2 when a group outlived its bounds."""
    cleanup_status = 0
    if state.dock is not None:
        if (
            vehicle.stop_process_group(
                state.dock.pid,
                f"dock process group {state.dock.pid}",
                expected_starttime=state.dock_starttime,
            )
            != 0
        ):
            cleanup_status = 2
        state.dock.poll()  # reap the zombie leader this process still holds
        state.dock = None
    if state.kwin is not None:
        if (
            vehicle.stop_process_group(
                state.kwin.pid,
                f"nested KWin process group {state.kwin.pid}",
                expected_starttime=state.kwin_starttime,
            )
            != 0
        ):
            cleanup_status = 2
        state.kwin.poll()
        state.kwin = None
    if state.runtime_dir is not None:
        # Process cleanup is bounded above; this is only the FUSE and
        # runtime-directory half of the vehicle teardown (pgid=None).
        vehicle.stop_compositor(state.runtime_dir, None)
        state.runtime_dir = None
    return cleanup_status


def _require_runtime_commands() -> None:
    require_commands("runtime", RUNTIME_COMMANDS)
    if shutil.which("fusermount3") is None and shutil.which("fusermount") is None:
        fail("required runtime command 'fusermount3' or 'fusermount' is missing")


def _start_nested_compositor(state: RuntimeCleanupState) -> vehicle.VehicleSession:
    """Bring up the gate's private nested compositor, or exit 2 with its log."""
    runtime_dir = state.runtime_dir
    assert runtime_dir is not None
    (runtime_dir / "kwin-config").mkdir(parents=True, exist_ok=True)
    (runtime_dir / "kwin-cache").mkdir(parents=True, exist_ok=True)
    extra_env = [
        f"WAYLAND_DISPLAY={NESTED_SOCKET}",
        f"XDG_CONFIG_HOME={runtime_dir}/kwin-config",
        f"XDG_CACHE_HOME={runtime_dir}/kwin-cache",
        "QT_FORCE_STDERR_LOGGING=1",
    ]
    argv = vehicle.build_kwin_argv(runtime_dir, NESTED_WIDTH, NESTED_HEIGHT, NESTED_SOCKET, 1)
    session_env = vehicle.build_session_env(os.environ, runtime_dir, extra_env)
    log = vehicle.log_path(runtime_dir)
    with log.open("w") as handle:
        kwin = SessionProcess.spawn(argv, env=session_env, stdout=handle, stderr=subprocess.STDOUT)
    state.kwin = kwin
    state.kwin_starttime = vehicle.leader_starttime(kwin.pid)

    socket_path = runtime_dir / NESTED_SOCKET
    for _ in range(vehicle.SOCKET_WAIT_ATTEMPTS):
        if socket_path.is_socket():
            break
        if kwin.poll() is not None:
            break  # a dead compositor can never bind; stop waiting early
        time.sleep(vehicle.SOCKET_WAIT_DELAY)
    if not socket_path.is_socket():
        print(
            f"{vehicle.TOOL}: nested kwin_wayland never brought up its socket; its log:",
            file=sys.stderr,
            flush=True,
        )
        with suppress(OSError):
            sys.stderr.write(log.read_text(errors="replace"))
        sys.stderr.flush()
        raise SystemExit(2)
    bus = vehicle.read_bus_address(runtime_dir)
    return vehicle.VehicleSession(
        runtime_dir, NESTED_SOCKET, kwin.pid, bus, log, state.kwin_starttime
    )


def _export_session_environment(session: vehicle.VehicleSession) -> None:
    os.environ["XDG_RUNTIME_DIR"] = str(session.runtime_dir)
    os.environ["WAYLAND_DISPLAY"] = session.socket
    os.environ["DBUS_SESSION_BUS_ADDRESS"] = session.bus
    os.environ.pop("DISPLAY", None)
    os.environ.pop("XAUTHORITY", None)


def _build_dock_environment(package: ValidatedPackage, runtime_dir: Path) -> dict[str, str]:
    """The installed dock's launch environment: loader-injection and ambient
    QML/plugin variables removed, the validated allow-lists forced."""
    dock_env = dict(os.environ)
    for variable in audit.LOADER_INJECTION_VARIABLES:
        dock_env.pop(variable, None)
    for variable in (
        "QML_IMPORT_PATH",
        "NIXPKGS_QT6_QML_IMPORT_PATH",
        "NIXPKGS_QML_SEARCH_PATHS",
        "QT_PLUGIN_PATH",
    ):
        dock_env.pop(variable, None)
    dock_env["QML2_IMPORT_PATH"] = package.qml_import_path
    dock_env["XDG_CONFIG_HOME"] = f"{runtime_dir}/latte-config"
    dock_env["XDG_CACHE_HOME"] = f"{runtime_dir}/latte-cache"
    dock_env["XDG_DATA_HOME"] = f"{runtime_dir}/latte-data"
    dock_env["XDG_DATA_DIRS"] = package.xdg_data_dirs
    dock_env["QT_QPA_PLATFORM"] = "wayland"
    dock_env["QT_QPA_PLATFORMTHEME"] = ""
    dock_env["QT_FORCE_STDERR_LOGGING"] = "1"
    dock_env["LATTE_EXTRA_PLUGIN_PATHS"] = package.package_plugins
    return dock_env


def _print_log_tail(log_file: Path, line_count: int = 40) -> None:
    try:
        text = log_file.read_text(errors="replace")
    except OSError:
        return
    if not text:
        return  # tail of an empty file prints nothing
    if text.endswith("\n"):
        text = text[:-1]
    for line in text.split("\n")[-line_count:]:
        print(line, file=sys.stderr)
    sys.stderr.flush()


def _query_dbus_readback(method: str) -> str:
    """One pull-queried readback from the dock's D-Bus surface.

    The reply is compared literally, never parsed: the settle contract is
    two consecutive byte-identical viewsData replies, and parsing would
    change what "identical" means.
    """
    result = subprocess.run(
        ["busctl", "--user", "call", "org.kde.lattedock", "/Latte", "org.kde.LatteDock", method],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return ""
    return audit.drop_nul_bytes(result.stdout).rstrip("\n")


def _await_settled_dock(dock: SessionProcess, dock_log: Path) -> None:
    """Wait for a stable running view or refuse with the dock's log tail."""
    state_reply = ""
    previous_views = ""
    for _ in range(DOCK_SETTLE_ATTEMPTS):
        if dock.poll() is not None:
            print(
                f"{TOOL}: installed dock exited during startup; log tail:",
                file=sys.stderr,
                flush=True,
            )
            _print_log_tail(dock_log)
            fail("installed binary did not survive startup")
        state_reply = _query_dbus_readback("lifecycleState")
        views_reply = _query_dbus_readback("viewsData")
        if (
            state_reply == 's "running"'
            and views_reply
            and views_reply != 's "[]"'
            and 'inStartup\\":true' not in views_reply
        ):
            if views_reply == previous_views:
                return
            previous_views = views_reply
        time.sleep(1)
    print(
        f"{TOOL}: dock failed to settle (last lifecycle reply: {state_reply or 'none'}); log tail:",
        file=sys.stderr,
        flush=True,
    )
    _print_log_tail(dock_log)
    fail("installed dock did not reach a stable running view within 90 seconds")


def _require_running_identity(dock_pid: int, binary: str) -> None:
    running_exe = resolve_following_symlinks(f"/proc/{dock_pid}/exe")
    if running_exe is None:
        fail(f"cannot resolve /proc/{dock_pid}/exe for the running dock")
    if running_exe != binary:
        fail(f"running executable is {running_exe}, expected the installed artifact {binary}")
    print(f"{TOOL}: running executable verified: {running_exe}", flush=True)


def _require_scrubbed_environment(dock_pid: int, package: ValidatedPackage) -> None:
    environ_file = f"/proc/{dock_pid}/environ"
    try:
        actual_qml = audit.read_environment_value(environ_file, "QML2_IMPORT_PATH")
    except audit.AuditError as err:
        _fail_after(str(err), "cannot verify the running dock's QML2_IMPORT_PATH")
    try:
        actual_plugins = audit.read_environment_value(environ_file, "LATTE_EXTRA_PLUGIN_PATHS")
    except audit.AuditError as err:
        _fail_after(str(err), "cannot verify the running dock's LATTE_EXTRA_PLUGIN_PATHS")
    if actual_qml != package.qml_import_path:
        fail(
            "running QML2_IMPORT_PATH differs from the validated allow-list "
            f"(actual '{actual_qml}', expected '{package.qml_import_path}')"
        )
    if actual_plugins != package.package_plugins:
        fail(
            "running LATTE_EXTRA_PLUGIN_PATHS differs from the installed plugin "
            f"root (actual '{actual_plugins}', expected '{package.package_plugins}')"
        )
    try:
        raw_environ = Path(environ_file).read_bytes()
    except OSError:
        fail(f"cannot read /proc/{dock_pid}/environ while checking forbidden variables")
    # The bash scanned the tr'd newline-joined text with substring patterns;
    # the same scan is kept for parity. (Its known theoretical false positive
    # - a variable VALUE containing "\nNAME=" - is inherited, not fixed here.)
    process_env = raw_environ.decode("utf-8", errors="surrogateescape").replace("\0", "\n")
    for forbidden in (
        "QML_IMPORT_PATH",
        "NIXPKGS_QT6_QML_IMPORT_PATH",
        "NIXPKGS_QML_SEARCH_PATHS",
        "QT_PLUGIN_PATH",
        *audit.LOADER_INJECTION_VARIABLES,
    ):
        if process_env.startswith(f"{forbidden}=") or f"\n{forbidden}=" in process_env:
            fail(f"forbidden ambient variable {forbidden} leaked into the installed dock")


def _resolve_plugin_for_mapping(label: str, path: str) -> str:
    resolved = resolve_following_symlinks(path)
    if resolved is None:
        fail(f"cannot resolve installed {label} for mapping validation")
    return resolved


def _build_expected_mapping_registry(
    package: ValidatedPackage,
) -> audit.ExpectedMappingRegistry:
    core, containment, tasks, action, indicator = package.plugin_paths
    registrations = (
        # The binary is already the fully resolved installed artifact.
        ("binary", package.binary, True),
        ("core QML plugin", _resolve_plugin_for_mapping("core QML plugin", core), True),
        (
            "containment QML plugin",
            _resolve_plugin_for_mapping("containment QML plugin", containment),
            True,
        ),
        ("tasks QML plugin", _resolve_plugin_for_mapping("tasks QML plugin", tasks), True),
        (
            "containment-actions plugin",
            _resolve_plugin_for_mapping("containment-actions plugin", action),
            True,
        ),
        # latte_indicator.so is a KPackage structure used while
        # opening/installing indicator packages, not by normal dock startup.
        # Its applicable runtime contract is the bounded metadata/type/dlopen
        # validation in the check phase. Keeping it registered still rejects
        # a foreign copy if startup ever maps it.
        (
            "indicator package-structure plugin",
            _resolve_plugin_for_mapping("indicator package-structure plugin", indicator),
            False,
        ),
    )
    registry = audit.ExpectedMappingRegistry()
    for label, resolved, required in registrations:
        try:
            registry.register(label, resolved, required)
        except audit.AuditError as err:
            fail(str(err))
    return registry


def _wait_until_group_exits(pgid: int, attempts: int, delay: float) -> str:
    """Poll the zombie-aware group status until gone/error or the bound."""
    for _ in range(attempts):
        status = vehicle.group_live_status(pgid)
        if status != "live":
            return status
        time.sleep(delay)
    return vehicle.group_live_status(pgid)


def _shut_down_dock(state: RuntimeCleanupState) -> None:
    """SIGTERM the dock group and require a clean, bounded, zero-status exit."""
    dock = state.dock
    assert dock is not None
    # A vanished group between settling and now is not swallowed: the
    # wait-status check below reports what actually happened to the leader.
    with suppress(ProcessLookupError):
        os.killpg(dock.pid, signal.SIGTERM)
    group_status = _wait_until_group_exits(dock.pid, DOCK_SHUTDOWN_ATTEMPTS, DOCK_SHUTDOWN_DELAY)
    if group_status == "live":
        fail(f"installed dock process group {dock.pid} survived SIGTERM for 25 seconds")
    if group_status == "error":
        fail(
            f"cannot determine whether installed dock process group {dock.pid} exited after SIGTERM"
        )
    # KSignalHandler turns SIGTERM into qGuiApp->quit(), so a clean installed
    # shutdown returns from the event loop with status zero rather than 143.
    actual_status = audit.shell_wait_status(dock.wait())
    if actual_status != 0:
        fail(f"installed dock after SIGTERM exited with status {actual_status}, expected 0")
    state.dock = None


def run_nested_runtime_phase(
    ctx: GateContext, package: ValidatedPackage, state: RuntimeCleanupState
) -> None:
    """Start the installed dock inside a private nested compositor, require it
    to settle, and audit the running process's provenance end to end."""
    _require_runtime_commands()

    state.runtime_dir = vehicle.prepare_runtime_dir()
    session = _start_nested_compositor(state)
    _export_session_environment(session)

    runtime_dir = session.runtime_dir
    for private_home in ("latte-config", "latte-cache", "latte-data"):
        (runtime_dir / private_home).mkdir(parents=True, exist_ok=True)
    dock_log = runtime_dir / "latte-dock.log"

    print(f"{TOOL}: starting installed dock in the nested compositor", flush=True)
    dock_env = _build_dock_environment(package, runtime_dir)
    with dock_log.open("w") as handle:
        dock = SessionProcess.spawn(
            [package.binary, "-d"], env=dock_env, stdout=handle, stderr=subprocess.STDOUT
        )
    state.dock = dock
    state.dock_starttime = vehicle.leader_starttime(dock.pid)

    _await_settled_dock(dock, dock_log)
    _require_running_identity(dock.pid, package.binary)
    _require_scrubbed_environment(dock.pid, package)

    registry = _build_expected_mapping_registry(package)
    result = audit.audit_mapped_paths(
        f"/proc/{dock.pid}/maps", ctx.artifact_prefix, ctx.repo, registry
    )
    for verified in result.verified_paths:
        print(f"{TOOL}: mapped artifact verified: {verified}", flush=True)
    if result.violation is not None:
        fail(result.violation.message)

    _shut_down_dock(state)


# ---- entry point -------------------------------------------------------------


def _run_gate(argv: Sequence[str], state: RuntimeCleanupState) -> None:
    require_commands("validation", VALIDATION_COMMANDS)
    qt_plugin_info = audit.find_qt6_plugin_info()
    if qt_plugin_info is None:
        fail(
            "required Qt 6 validation command 'qtplugininfo' is missing or "
            "reports a non-Qt-6 version"
        )
    arguments = parse_gate_arguments(argv)

    ctx = GateContext(repo=str(find_repo_root()))
    package = validate_installed_package(ctx, arguments, qt_plugin_info)

    if arguments.check_only:
        print(f"{TOOL}: CHECK OK", flush=True)
        return

    run_nested_runtime_phase(ctx, package, state)
    print(
        f"{TOOL}: PASS (installed executable, QML plugins, data, startup, and shutdown verified)",
        flush=True,
    )


def main(argv: Sequence[str] | None = None) -> int:
    install_conventional_signal_exits()
    for variable in audit.LOADER_INJECTION_VARIABLES:
        os.environ.pop(variable, None)
    state = RuntimeCleanupState()
    status = 0
    try:
        try:
            _run_gate(argv if argv is not None else sys.argv[1:], state)
        except _UsageRequested:
            print(USAGE, end="", flush=True)
            return 0
        except GateRefusal as refusal:
            _print_fail(str(refusal))
            status = 2
    finally:
        # Cleanup runs on every exit path, signal exits included (the
        # conventional-exit handler raises SystemExit(130/143) which unwinds
        # through here); its failure escalates only a would-be success.
        cleanup_status = run_exit_cleanup(state)
    if status == 0 and cleanup_status != 0:
        status = cleanup_status
    return status


if __name__ == "__main__":
    raise SystemExit(main())
