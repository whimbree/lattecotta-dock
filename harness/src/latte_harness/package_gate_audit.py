# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
"""Provenance-audit core for the installed-package gate.

The typed port of scripts/lib-installed-package-gate.sh's binary-adjacent
half (BP-4a, the bash-to-python migration's package-gate engine chunk):
ELF search-path reading, /proc/<pid>/maps parsing, the mapped-path
provenance audit against the expected-mapping registry, the Qt 6
qtplugininfo selection probe, the process-environment readback, and the
AppStream standalone-application validator that lived as an inline perl
program in scripts/installed-package-gate.sh.

Every diagnostic here is part of the gate's refusal taxonomy: the
unported selftest (tests/installed-package-gate-selftest.sh, BP-4b) and
the unit suite both match these messages verbatim, so the exact text is
a contract, not styling. Parsers are pure functions over captured text
wherever possible so hostile inputs can be unit-tested without
subprocesses; the bash lib's remaining process-group helpers converged
into latte_harness.vehicle in BP-2a and are consumed from there by the
engine (latte_harness.package_gate).
"""

from __future__ import annotations

import fnmatch
import os
import re
import shutil
import subprocess
from collections.abc import Sequence
from enum import StrEnum
from pathlib import Path

from pydantic import BaseModel, Field, JsonValue, TypeAdapter, ValidationError

# The dynamic-loader injection surface: any of these reaching the installed
# dock would let ambient state steer symbol resolution, so the engine scrubs
# them from its own environment, launches the dock without them, and treats
# one surviving in /proc/<pid>/environ as a leak.
LOADER_INJECTION_VARIABLES: tuple[str, ...] = (
    "LD_ASSUME_KERNEL",
    "LD_AUDIT",
    "LD_BIND_NOT",
    "LD_BIND_NOW",
    "LD_CONFIG_FILE",
    "LD_DEBUG",
    "LD_DEBUG_OUTPUT",
    "LD_DYNAMIC_WEAK",
    "LD_HWCAP_MASK",
    "LD_LIBRARY_PATH",
    "LD_ORIGIN_PATH",
    "LD_PRELOAD",
    "LD_PREFER_MAP_32BIT_EXEC",
    "LD_PROFILE",
    "LD_PROFILE_OUTPUT",
    "LD_SHOW_AUXV",
    "LD_TRACE_LOADED_OBJECTS",
    "LD_USE_LOAD_BIAS",
    "LD_VERBOSE",
    "LD_WARN",
)


class AuditError(Exception):
    """A lib-level audit failure; the message is the exact bash diagnostic
    (without the "installed-package-gate: FAIL: " prefix the engine adds)."""


def drop_nul_bytes(captured_output: str) -> str:
    """Strip NUL bytes exactly like bash command substitution did.

    The bash engine captured every external tool's output through ``$( )``,
    which silently drops NUL bytes; perl's DynaLoader error text really
    does carry one, so a faithful port must keep dropping them or the
    diagnostics gain invisible bytes the bash never printed.
    """
    return captured_output.replace("\0", "")


def shell_wait_status(returncode: int) -> int:
    """A subprocess returncode as the shell's ``$?`` (128+signal on kills).

    Load-bearing for the timeout binary: GNU timeout escalating through
    ``--kill-after`` SIGKILLs its own process group, itself included, so
    the shell observed 137 where subprocess reports -9. Every exit-status
    comparison ported from bash must go through this mapping.
    """
    return returncode if returncode >= 0 else 128 - returncode


# ---- Qt 6 qtplugininfo selection --------------------------------------------

# One line of `<tool> --version` output naming a Qt plugin-info tool: tool
# name, then MAJOR.MINOR with an optional .PATCH. Anything else (extra lines,
# prefixed diagnostics, a v-prefix) is not accepted as a version report.
_QT_PLUGIN_INFO_VERSION = re.compile(
    r"^(qplugininfo|qtplugininfo|qtplugininfo6|qtplugininfo-qt6)"
    r"[ \t]+([0-9]+)\.([0-9]+)(\.([0-9]+))?$"
)


def version_output_names_qt6_tool(version_output: str) -> bool:
    """True when a --version capture is a single-line Qt 6 tool report.

    Mirrors the bash probe: command substitution stripped trailing
    newlines, any remaining newline (a multi-line report such as a
    diagnostic followed by a Qt 5 version) is rejected, and the major
    version must be exactly 6.
    """
    stripped = version_output.rstrip("\n")
    if "\n" in stripped:
        return False
    match = _QT_PLUGIN_INFO_VERSION.match(stripped)
    return match is not None and match.group(2) == "6"


def _tool_reports_qt6_version(tool: str) -> bool:
    """Probe one candidate's --version under a bounded timeout.

    The timeout binary carries the TERM-then-KILL escalation (a hanging
    probe must not stall candidate selection); a nonzero exit, including
    the timeout's own 124/137, simply disqualifies the candidate.
    """
    result = subprocess.run(
        ["timeout", "--kill-after=1s", "2s", tool, "--version"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return False
    return version_output_names_qt6_tool(drop_nul_bytes(result.stdout))


def choose_qt6_plugin_info(candidates: Sequence[str]) -> str | None:
    """The first candidate whose --version proves a Qt 6 tool, or None."""
    for candidate in candidates:
        if _tool_reports_qt6_version(candidate):
            return candidate
    return None


def find_qt6_plugin_info() -> str | None:
    """Locate a Qt 6 qtplugininfo without trusting the unsuffixed name.

    Candidate order matters: distro-specific names (qtplugininfo6,
    qtplugininfo-qt6) cannot be shadowed by an unsuffixed Qt 5 tool, then
    the fixed Qt 6 tool directories, and only then the unsuffixed PATH
    name, which the version probe still has to prove is Qt 6.
    """
    candidates: list[str] = []
    seen: set[str] = set()
    for name in ("qtplugininfo6", "qtplugininfo-qt6"):
        found = shutil.which(name)
        if found is not None and found not in seen:
            candidates.append(found)
            seen.add(found)
    for fixed in ("/usr/lib/qt6/bin/qtplugininfo", "/usr/lib64/qt6/bin/qtplugininfo"):
        if os.access(fixed, os.X_OK) and fixed not in seen:
            candidates.append(fixed)
            seen.add(fixed)
    found = shutil.which("qtplugininfo")
    if found is not None and found not in seen:
        candidates.append(found)
    return choose_qt6_plugin_info(candidates)


# ---- ELF search-path reading -------------------------------------------------


def _collapse_like_shell_substitution(values: Sequence[str]) -> list[str]:
    """Join, strip trailing newlines, and re-split, exactly like bash.

    The bash lib piped parser output through ``$( )`` (which strips ALL
    trailing newlines) and a ``[[ -n ]]`` guard before the line-read loop.
    The observable consequences carry over: an all-empty result collapses
    to no entries, and trailing empty entries are dropped while interior
    empties survive.
    """
    joined = "\n".join(values).rstrip("\n")
    if not joined:
        return []
    return joined.split("\n")


def parse_elf_search_paths(readelf_output: str) -> list[str]:
    """RPATH/RUNPATH values from ``readelf -d`` output.

    Mirrors the bash awk: on each line naming (RPATH) or (RUNPATH), take
    the text between the first '[' and the first ']'. A matched line
    without brackets passes through with the failed substitutions applied
    (i.e. unchanged), same as awk's sub().
    """
    values: list[str] = []
    for line in readelf_output.split("\n"):
        if re.search(r"\((RPATH|RUNPATH)\)", line) is None:
            continue
        value = re.sub(r"^[^\[]*\[", "", line, count=1)
        value = re.sub(r"\].*$", "", value, count=1)
        values.append(value)
    return _collapse_like_shell_substitution(values)


def read_elf_search_paths(elf: str) -> list[str]:
    """Run readelf -d on ``elf`` and parse its loader search paths.

    Raises AuditError when readelf cannot inspect the file; the engine
    prints this diagnostic and then its own could-not-be-read-completely
    refusal, matching the bash lib+engine two-line failure.
    """
    result = subprocess.run(
        ["readelf", "-d", "--", elf],
        capture_output=True,
        text=True,
        env={**os.environ, "LC_ALL": "C"},
        check=False,
    )
    if result.returncode != 0:
        raise AuditError(f"readelf could not inspect dynamic metadata for {elf}")
    return parse_elf_search_paths(drop_nul_bytes(result.stdout))


# ---- /proc/<pid>/maps parsing ------------------------------------------------

# /proc/<pid>/maps has five whitespace-delimited fields before the optional
# pathname. Strip only those fields so spaces inside the pathname survive.
# re.ASCII pins \S/\s to the awk [[:space:]] class.
_MAPS_FIELD_PREFIX = re.compile(r"^\S+\s+\S+\s+\S+\s+\S+\s+\S+\s+", re.ASCII)


def parse_mapped_paths(maps_text: str) -> list[str]:
    """Absolute pathnames from /proc/<pid>/maps contents.

    Anonymous mappings (five fields, no pathname) and pseudo-paths such as
    [heap] are skipped; a pathname containing spaces is preserved whole.
    """
    paths: list[str] = []
    for line in maps_text.split("\n"):
        stripped, substituted = _MAPS_FIELD_PREFIX.subn("", line, count=1)
        if substituted and stripped.startswith("/"):
            paths.append(stripped)
    return _collapse_like_shell_substitution(paths)


def read_mapped_paths(maps_file: str) -> list[str]:
    """Parse the mapped pathnames of ``maps_file``, refusing unreadable input."""
    try:
        # newline="" keeps any stray \r bytes intact (awk read the file raw).
        maps_text = Path(maps_file).read_text(newline="")
    except OSError as err:
        raise AuditError(f"cannot parse process mappings from {maps_file}") from err
    return parse_mapped_paths(maps_text)


# ---- provenance classification -----------------------------------------------


def path_is_within(path: str, base: str) -> bool:
    """True when ``path`` is ``base`` or below it (pure string containment)."""
    return base == "/" or path == base or path.startswith(base + "/")


_LATTE_RUNTIME_BASENAMES = (
    "latte-dock",
    "liblatte*.so*",
    "latte_*.so*",
    "org.kde.latte*.so*",
)


def is_latte_runtime_path(path: str) -> bool:
    """True when a mapped path names a Latte runtime artifact.

    Either the basename matches one of the Latte artifact patterns or the
    path runs through an org/kde/latte QML module directory.
    """
    name = path.rsplit("/", 1)[-1]
    if any(fnmatch.fnmatchcase(name, pattern) for pattern in _LATTE_RUNTIME_BASENAMES):
        return True
    return "/org/kde/latte/" in path


def find_development_provider(provider_path: str) -> str | None:
    """Name the development tree an ancestor of ``provider_path`` marks.

    Walks from the containing directory toward (but never onto) the
    filesystem root: a .git entry or CMakeLists.txt names a "source tree",
    a CMakeCache.txt or CMakeFiles directory a "CMake build tree". Returns
    None when no ancestor is marked.
    """
    provider_dir = provider_path
    if not os.path.isdir(provider_dir):
        provider_dir = provider_dir.rsplit("/", 1)[0]
    while provider_dir and provider_dir != "/":
        if os.path.exists(f"{provider_dir}/.git") or os.path.isfile(
            f"{provider_dir}/CMakeLists.txt"
        ):
            return "source tree"
        if os.path.isfile(f"{provider_dir}/CMakeCache.txt") or os.path.isdir(
            f"{provider_dir}/CMakeFiles"
        ):
            return "CMake build tree"
        provider_dir = provider_dir.rsplit("/", 1)[0]
        if not provider_dir:
            provider_dir = "/"
    return None


# ---- the expected-mapping registry and the audit -----------------------------


class ExpectedMappingRegistry(BaseModel):
    """The installed artifacts the settled dock may (and must) map.

    Keyed by basename because /proc/maps identity is checked in two steps:
    a mapped Latte-looking path must first resolve to a registered
    basename, then to exactly the registered installed path. ``required``
    keeps registration order; the audit's verified output follows it.
    """

    expected: dict[str, str] = Field(default_factory=dict)
    required: list[str] = Field(default_factory=list)

    def register(self, label: str, path: str, required: bool) -> None:
        """Add one installed artifact, refusing a basename collision.

        Two registered artifacts sharing a basename would make the maps
        identity check ambiguous, so the collision is a gate refusal, not
        a silent overwrite.
        """
        name = path.rsplit("/", 1)[-1]
        if name in self.expected:
            raise AuditError(
                f"installed {label} has mapped-artifact basename '{name}', "
                f"already used by {self.expected[name]}"
            )
        self.expected[name] = path
        if required:
            self.required.append(name)


class MappedPathViolationKind(StrEnum):
    UNREADABLE_MAPS = "unreadable-maps"
    NIX_ARTIFACT = "nix-artifact"
    SOURCE_TREE = "source-tree"
    QMLSTAGE_ARTIFACT = "qmlstage-artifact"
    DEVELOPMENT_PROVIDER = "development-provider"
    UNRESOLVABLE_RUNTIME = "unresolvable-runtime"
    ESCAPES_PREFIX = "escapes-prefix"
    UNEXPECTED_RUNTIME = "unexpected-runtime"
    WRONG_PROVIDER = "wrong-provider"
    MISSING_REQUIRED = "missing-required"


class MappedPathViolation(BaseModel):
    """One provenance violation; ``message`` is the exact refusal text."""

    kind: MappedPathViolationKind
    message: str


class MappedPathAuditResult(BaseModel):
    """The audit's verdict: at most one violation (the audit stops at the
    first, like the bash), plus the required artifacts verified before it."""

    violation: MappedPathViolation | None = None
    verified_paths: list[str] = Field(default_factory=list)

    @property
    def passed(self) -> bool:
        return self.violation is None


def _resolve_mapped_runtime(mapped: str) -> str | None:
    """realpath for a mapped Latte runtime; None when it cannot resolve."""
    try:
        resolved = os.path.realpath(mapped, strict=True)
    except OSError:
        return None
    return resolved


def audit_mapped_paths(
    maps_file: str,
    artifact_prefix: str,
    source_root: str,
    registry: ExpectedMappingRegistry,
) -> MappedPathAuditResult:
    """Classify every mapped path of the settled dock for provenance.

    The invariants, in check order per unique mapped path:

    - nothing maps from /nix/store, the source/build tree, a development
      _qmlstage, or any tree an ancestor marks as a source or CMake build
      tree (development artifacts must never satisfy a package gate);
    - anything Latte-looking (by basename or an org/kde/latte path), and
      anything whose basename is registered, must resolve to exactly the
      registered installed path inside the package prefix;
    - every registered required artifact must actually be mapped.

    Non-Latte paths outside the registry (the distro's own Qt/KF6 stack)
    are benign. Returns the first violation, or the verified required
    paths in registration order.
    """

    def violated(kind: MappedPathViolationKind, message: str) -> MappedPathAuditResult:
        return MappedPathAuditResult(violation=MappedPathViolation(kind=kind, message=message))

    try:
        mapped_paths = read_mapped_paths(maps_file)
    except AuditError as err:
        return violated(MappedPathViolationKind.UNREADABLE_MAPS, str(err))

    examined: set[str] = set()
    seen: set[str] = set()
    for mapped in mapped_paths:
        if mapped in examined:
            continue
        examined.add(mapped)
        if mapped.startswith("/nix/store/"):
            return violated(
                MappedPathViolationKind.NIX_ARTIFACT,
                f"running dock mapped a Nix artifact: {mapped}",
            )
        if path_is_within(mapped, source_root):
            return violated(
                MappedPathViolationKind.SOURCE_TREE,
                f"running dock mapped the source/build tree: {mapped}",
            )
        if mapped.endswith("/_qmlstage") or "/_qmlstage/" in mapped:
            return violated(
                MappedPathViolationKind.QMLSTAGE_ARTIFACT,
                f"running dock mapped a development _qmlstage artifact: {mapped}",
            )
        provider = find_development_provider(mapped)
        if provider is not None:
            return violated(
                MappedPathViolationKind.DEVELOPMENT_PROVIDER,
                f"running dock mapped a {provider} artifact: {mapped}",
            )

        mapped_name = mapped.rsplit("/", 1)[-1]
        if mapped_name not in registry.expected and not is_latte_runtime_path(mapped):
            continue
        resolved = _resolve_mapped_runtime(mapped)
        if resolved is None:
            return violated(
                MappedPathViolationKind.UNRESOLVABLE_RUNTIME,
                f"mapped Latte runtime cannot be resolved: {mapped}",
            )
        if not path_is_within(resolved, artifact_prefix):
            return violated(
                MappedPathViolationKind.ESCAPES_PREFIX,
                f"mapped Latte runtime escapes the package prefix: {mapped} -> {resolved}",
            )
        name = resolved.rsplit("/", 1)[-1]
        if name not in registry.expected:
            return violated(
                MappedPathViolationKind.UNEXPECTED_RUNTIME,
                f"unexpected Latte runtime is mapped: {resolved}",
            )
        if resolved != registry.expected[name]:
            return violated(
                MappedPathViolationKind.WRONG_PROVIDER,
                f"{name} mapped from {resolved}, expected {registry.expected[name]}",
            )
        seen.add(name)

    verified: list[str] = []
    for required in registry.required:
        if required not in seen:
            result = violated(
                MappedPathViolationKind.MISSING_REQUIRED,
                f"required installed artifact {required} is not mapped by the settled dock",
            )
            result.verified_paths = verified
            return result
        verified.append(registry.expected[required])
    return MappedPathAuditResult(verified_paths=verified)


# ---- process-environment readback --------------------------------------------


def read_environment_value(environment_file: str, variable: str) -> str:
    """One variable's value from a NUL-separated /proc/<pid>/environ file.

    Mirrors the bash tr-to-newlines pipeline exactly, including its
    treatment of NUL and newline as equivalent separators; surrogateescape
    keeps arbitrary environment bytes round-trippable. Raises AuditError
    with the read-failure or the no-entry diagnostic.
    """
    try:
        raw = Path(environment_file).read_bytes()
    except OSError as err:
        raise AuditError(f"cannot read process environment from {environment_file}") from err
    text = raw.decode("utf-8", errors="surrogateescape").replace("\0", "\n")
    for entry in text.split("\n"):
        name, separator, value = entry.partition("=")
        if name == variable:
            # An entry without '=' matched whole; bash printed it unchanged.
            return value if separator else entry
    raise AuditError(f"process environment has no {variable} entry")


# ---- Qt plugin metadata contracts --------------------------------------------


def declares_containment_actions_contract(metadata: JsonValue) -> bool:
    """The org.kde.latte.contextmenu Plasma/ContainmentActions declaration.

    The typed twin of the jq filter: KPlugin.Id must be the string
    'org.kde.latte.contextmenu' and ServiceTypes an array containing
    'Plasma/ContainmentActions' (a bare string does not satisfy the
    array-typed contract).
    """
    if not isinstance(metadata, dict):
        return False
    meta = metadata.get("MetaData")
    if not isinstance(meta, dict):
        return False
    kplugin = meta.get("KPlugin")
    if not isinstance(kplugin, dict):
        return False
    plugin_id = kplugin.get("Id")
    if not isinstance(plugin_id, str) or plugin_id != "org.kde.latte.contextmenu":
        return False
    service_types = kplugin.get("ServiceTypes")
    if not isinstance(service_types, list):
        return False
    return any(entry == "Plasma/ContainmentActions" for entry in service_types)


def declares_indicator_structure_contract(metadata: JsonValue) -> bool:
    """The Latte/Indicator package-structure declaration for org.kde.latte-dock.

    KPackageStructure must be the string 'Latte/Indicator' (an array does
    not satisfy the string-typed contract) and X-KDE-ParentApp the string
    'org.kde.latte-dock'.
    """
    if not isinstance(metadata, dict):
        return False
    meta = metadata.get("MetaData")
    if not isinstance(meta, dict):
        return False
    structure = meta.get("KPackageStructure")
    if not isinstance(structure, str) or structure != "Latte/Indicator":
        return False
    parent_app = meta.get("X-KDE-ParentApp")
    return isinstance(parent_app, str) and parent_app == "org.kde.latte-dock"


_JSON_VALUE = TypeAdapter[JsonValue](JsonValue)


def load_plugin_metadata_json(metadata_output: str) -> JsonValue | None:
    """Parse qtplugininfo's --full-json output; None when it is not JSON."""
    try:
        return _JSON_VALUE.validate_json(metadata_output)
    except ValidationError:
        return None


# ---- AppStream standalone-application validation -----------------------------

# The validator is a deliberate mini-parser, ported statement-for-statement
# from the inline perl program the bash engine carried: a strict subset of
# XML (prolog, comments, CDATA inside the root, elements with quoted
# attributes, text) is accepted and anything else refused, so hostile or
# malformed metadata cannot skate through a lenient parser. The reject
# messages are the refusal taxonomy the selftest matches.

_XML_NAME = r"[A-Za-z_][A-Za-z0-9_.:-]*"
_XML_PROLOG = re.compile(r"<\?[^?]*\?>")
_XML_COMMENT = re.compile(r"<!--.*?-->", re.DOTALL)
_XML_CDATA = re.compile(r"<!\[CDATA\[(.*?)\]\]>", re.DOTALL)
_XML_CLOSING_TAG = re.compile(rf"</({_XML_NAME})\s*>", re.ASCII)
_XML_OPENING_TAG = re.compile(rf"<({_XML_NAME})([^<>]*?)(/?)>", re.ASCII | re.DOTALL)
_XML_TEXT = re.compile(r"[^<]+", re.DOTALL)
_XML_ATTRIBUTE = re.compile(rf"\s+({_XML_NAME})\s*=\s*(?:\"([^\"]*)\"|'([^']*)')", re.ASCII)
_WHITESPACE_TRIM = re.compile(r"^\s+|\s+$", re.ASCII)


class _AppStreamReject(Exception):
    """Internal short-circuit carrying the exact reject diagnostic."""


class _AppStreamNode:
    __slots__ = ("attributes", "child_count", "name", "text")

    def __init__(self, name: str, attributes: dict[str, str]) -> None:
        self.name = name
        self.attributes = attributes
        self.text = ""
        self.child_count = 0


class _AppStreamScan:
    """The accumulated component facts the final gauntlet judges."""

    def __init__(self) -> None:
        self.component_ids: list[str] = []
        self.launchables: list[tuple[str, str]] = []
        self.provider_names: list[str] = []
        self.provider_binaries: list[str] = []
        self.provider_libraries: list[str] = []
        self.replacement_child_names: list[str] = []
        self.replaced_component_ids: list[str] = []
        self.replacement_id_child_counts: list[int] = []
        self.replacement_container_texts: list[str] = []
        self.component_type: str = ""
        self.extends_count = 0
        self.provides_count = 0
        self.replaces_count = 0
        self.root_count = 0

    def finish_node(
        self, node: _AppStreamNode, parent: str | None, grandparent: str | None
    ) -> None:
        text = _WHITESPACE_TRIM.sub("", node.text)
        if parent == "component":
            if node.name == "id":
                self.component_ids.append(text)
            if node.name == "extends":
                self.extends_count += 1
            if node.name == "provides":
                self.provides_count += 1
            if node.name == "replaces":
                self.replaces_count += 1
                self.replacement_container_texts.append(text)
            if node.name == "launchable":
                self.launchables.append((text, node.attributes.get("type", "")))
        if parent == "provides" and grandparent == "component":
            self.provider_names.append(node.name)
            if node.name == "binary":
                self.provider_binaries.append(text)
            if node.name == "library":
                self.provider_libraries.append(text)
        if parent == "replaces" and grandparent == "component":
            self.replacement_child_names.append(node.name)
            if node.name == "id":
                self.replaced_component_ids.append(text)
                self.replacement_id_child_counts.append(node.child_count)


def _parse_tag_attributes(name: str, attribute_source: str) -> dict[str, str]:
    attributes: dict[str, str] = {}
    position = 0
    while True:
        match = _XML_ATTRIBUTE.match(attribute_source, position)
        if match is None:
            break
        attribute_name = match.group(1)
        if attribute_name in attributes:
            raise _AppStreamReject(f"<{name}> repeats attribute {attribute_name}")
        double_value, single_value = match.group(2), match.group(3)
        attributes[attribute_name] = double_value if double_value is not None else single_value
        position = match.end()
    if not re.fullmatch(r"\s*", attribute_source[position:], re.ASCII):
        raise _AppStreamReject(f"<{name}> contains malformed attributes")
    return attributes


def _scan_appstream_xml(xml: str) -> _AppStreamScan:
    scan = _AppStreamScan()
    stack: list[_AppStreamNode] = []
    position = 0

    def parent_names() -> tuple[str | None, str | None]:
        parent = stack[-1].name if stack else None
        grandparent = stack[-2].name if len(stack) > 1 else None
        return parent, grandparent

    while position < len(xml):
        match = _XML_PROLOG.match(xml, position)
        if match is not None:
            position = match.end()
            continue
        match = _XML_COMMENT.match(xml, position)
        if match is not None:
            position = match.end()
            continue
        match = _XML_CDATA.match(xml, position)
        if match is not None:
            if not stack:
                raise _AppStreamReject("CDATA is not allowed outside the component root")
            stack[-1].text += match.group(1)
            position = match.end()
            continue
        match = _XML_CLOSING_TAG.match(xml, position)
        if match is not None:
            name = match.group(1)
            if not stack:
                raise _AppStreamReject(f"closing tag </{name}> has no opening tag")
            node = stack.pop()
            if node.name != name:
                raise _AppStreamReject(f"closing tag </{name}> does not match <{node.name}>")
            scan.finish_node(node, *parent_names())
            position = match.end()
            continue
        match = _XML_OPENING_TAG.match(xml, position)
        if match is not None:
            name, attribute_source, self_closing = match.group(1), match.group(2), match.group(3)
            attributes = _parse_tag_attributes(name, attribute_source)
            if not stack:
                scan.root_count += 1
                if name != "component":
                    raise _AppStreamReject(f"root element is <{name}>, expected <component>")
                scan.component_type = attributes.get("type", "")
            if stack:
                stack[-1].child_count += 1
            node = _AppStreamNode(name, attributes)
            if self_closing:
                scan.finish_node(node, *parent_names())
            else:
                stack.append(node)
            position = match.end()
            continue
        match = _XML_TEXT.match(xml, position)
        if match is not None:
            text = match.group(0)
            if stack:
                stack[-1].text += text
            elif re.search(r"\S", text, re.ASCII):
                raise _AppStreamReject(
                    "non-whitespace text is not allowed outside the component root"
                )
            position = match.end()
            continue
        raise _AppStreamReject("metadata contains unsupported or malformed XML")

    if stack:
        raise _AppStreamReject(f"unclosed tag <{stack[-1].name}>")
    return scan


def _judge_appstream_scan(scan: _AppStreamScan) -> None:
    if scan.root_count != 1:
        raise _AppStreamReject(f"metadata contains {scan.root_count} root elements, expected one")
    if scan.component_type != "desktop-application":
        raise _AppStreamReject(
            f"component type is '{scan.component_type}', expected 'desktop-application'"
        )
    if not (len(scan.component_ids) == 1 and scan.component_ids[0] == "org.kde.latte-dock"):
        raise _AppStreamReject("component ID must be exactly org.kde.latte-dock")
    if not (
        len(scan.launchables) == 1
        and scan.launchables[0][0] == "org.kde.latte-dock.desktop"
        and scan.launchables[0][1] == "desktop-id"
    ):
        raise _AppStreamReject("launchable must be exactly desktop-id org.kde.latte-dock.desktop")
    if scan.extends_count != 0:
        raise _AppStreamReject("standalone component must not declare extends")
    if not (
        scan.replaces_count == 1
        and len(scan.replacement_container_texts) == 1
        and scan.replacement_container_texts[0] == ""
        and len(scan.replacement_child_names) == 1
        and scan.replacement_child_names[0] == "id"
        and len(scan.replaced_component_ids) == 1
        and scan.replaced_component_ids[0] == "org.kde.latte-dock.desktop"
        and len(scan.replacement_id_child_counts) == 1
        and scan.replacement_id_child_counts[0] == 0
    ):
        raise _AppStreamReject(
            "replaces must contain only the released org.kde.latte-dock.desktop component ID"
        )
    if scan.provider_libraries:
        raise _AppStreamReject(f"provider must not advertise library {scan.provider_libraries[0]}")
    if not (
        scan.provides_count == 1
        and len(scan.provider_names) == 1
        and scan.provider_names[0] == "binary"
        and len(scan.provider_binaries) == 1
        and scan.provider_binaries[0] == "latte-dock"
    ):
        raise _AppStreamReject("provides must contain only the latte-dock binary")


def validate_appstream_metadata(xml: str) -> str | None:
    """Validate AppStream metadata against the standalone-application contract.

    Returns None on success or the exact reject diagnostic; the engine
    wraps the diagnostic in its violates-the-contract refusal.
    """
    try:
        _judge_appstream_scan(_scan_appstream_xml(xml))
    except _AppStreamReject as reject:
        return str(reject)
    return None
