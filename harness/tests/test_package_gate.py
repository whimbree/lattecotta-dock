# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
"""The installed-package-gate engine's contracts: the argument taxonomy,
GNU-realpath-shaped path resolution, the package-namespace symlink walk,
the manifest ownership forms, and the shim's bash preflight staying in
lockstep with the module's command list. Exact-message assertions
throughout - the refusal text is the interface the selftest and a
packager consume.
"""

import os
from pathlib import Path

import pytest

from latte_harness.package_gate import (
    GateArguments,
    GateContext,
    GateRefusal,
    _UsageRequested,  # pyright: ignore[reportPrivateUsage]
    enforce_manifest_ownership_set,
    normalize_lexically,
    parse_gate_arguments,
    require_one_match,
    resolve_following_symlinks,
    resolve_package_namespace_path,
)
from latte_harness.package_gate_audit import shell_wait_status

# ---- argument contract -------------------------------------------------------


def test_arguments_parse_the_full_contract() -> None:
    arguments = parse_gate_arguments(
        ["--root", "/pkg", "--prefix", "/opt", "--manifest", "/m.txt", "--check-only"]
    )
    assert arguments == GateArguments("/pkg", "/opt", "/m.txt", True)


def test_arguments_default_prefix_and_optional_manifest() -> None:
    arguments = parse_gate_arguments(["--root", "/pkg"])
    assert arguments == GateArguments("/pkg", "/usr", "", False)


def test_arguments_help_requests_usage() -> None:
    with pytest.raises(_UsageRequested):
        parse_gate_arguments(["--help"])
    with pytest.raises(_UsageRequested):
        parse_gate_arguments(["-h"])


@pytest.mark.parametrize(
    ("argv", "message"),
    [
        (["--root"], "--root needs a value"),
        (["--prefix"], "--prefix needs a value"),
        (["--manifest"], "--manifest needs a value"),
        (["--bogus"], "unknown argument '--bogus' (see --help)"),
        ([], "--root is required; implicit system-package lookup is forbidden"),
        (["--root", "pkg"], "--root must be absolute: pkg"),
        (["--root", "/pkg", "--prefix", "usr"], "--prefix must be absolute: usr"),
        (["--root", "/pkg", "--manifest", "m.txt"], "--manifest must be absolute: m.txt"),
        (
            ["--root", "/pkg", "--prefix", "/usr/../lib"],
            "--prefix must not contain . or .. components: /usr/../lib",
        ),
        (
            ["--root", "/pkg", "--prefix", "/usr/./lib"],
            "--prefix must not contain . or .. components: /usr/./lib",
        ),
        (
            ["--root", "/pkg", "--prefix", "/usr/.."],
            "--prefix must not contain . or .. components: /usr/..",
        ),
    ],
)
def test_arguments_refusals_carry_exact_messages(argv: list[str], message: str) -> None:
    with pytest.raises(GateRefusal) as refusal:
        parse_gate_arguments(argv)
    assert str(refusal.value) == message


# ---- path resolution ---------------------------------------------------------


def test_normalize_lexically_mirrors_realpath_ms() -> None:
    assert normalize_lexically("//tmp//x/../y") == "/tmp/y"
    assert normalize_lexically("/a/b/./c/") == "/a/b/c"
    assert normalize_lexically("/") == "/"
    assert normalize_lexically("") is None
    relative = normalize_lexically("x/../y")
    assert relative == os.path.normpath(os.path.join(os.getcwd(), "y"))


def test_resolve_following_symlinks_requires_existing_ancestry(tmp_path: Path) -> None:
    tmp_path = tmp_path.resolve()
    existing = tmp_path / "present"
    existing.touch()
    assert resolve_following_symlinks(str(existing)) == str(existing)
    # GNU realpath default: a missing final component is fine ...
    assert resolve_following_symlinks(str(tmp_path / "absent")) == str(tmp_path / "absent")
    # ... but a missing intermediate component is a resolution failure.
    assert resolve_following_symlinks(str(tmp_path / "absent-dir" / "child")) is None


def test_resolve_following_symlinks_follows_links(tmp_path: Path) -> None:
    tmp_path = tmp_path.resolve()
    target = tmp_path / "real-target"
    target.touch()
    link = tmp_path / "link"
    link.symlink_to(target)
    assert resolve_following_symlinks(str(link)) == str(target)
    dangling = tmp_path / "dangling"
    dangling.symlink_to(tmp_path / "nowhere")
    assert resolve_following_symlinks(str(dangling)) == str(tmp_path / "nowhere")


def _namespace_fixture(tmp_path: Path) -> tuple[GateContext, Path]:
    root = tmp_path.resolve() / "pkg"
    (root / "usr" / "lib").mkdir(parents=True)
    ctx = GateContext(repo=str(tmp_path.resolve() / "unused-repo"), package_root=str(root))
    return ctx, root


def test_namespace_walk_resolves_absolute_links_inside_the_package(tmp_path: Path) -> None:
    # The isolated-root semantics the selftest drives end to end: an absolute
    # symlink restarts from the package root, not from host /.
    ctx, root = _namespace_fixture(tmp_path)
    (root / "usr" / "lib" / "real.qml").touch()
    (root / "usr" / "lib" / "link.qml").symlink_to("/usr/lib/real.qml")
    resolved = resolve_package_namespace_path(ctx, "installed content", f"{root}/usr/lib/link.qml")
    assert resolved == f"{root}/usr/lib/real.qml"


def test_namespace_walk_refuses_symlink_escape(tmp_path: Path) -> None:
    ctx, root = _namespace_fixture(tmp_path)
    outside = tmp_path.resolve() / "outside.qml"
    outside.touch()
    escape = root / "usr" / "lib" / "escape.qml"
    escape.symlink_to(os.path.relpath(outside, escape.parent))
    with pytest.raises(GateRefusal) as refusal:
        resolve_package_namespace_path(ctx, "installed content", str(escape))
    assert str(refusal.value) == (
        f"installed content escapes the package root through a symlink: {outside}"
    )


def test_namespace_walk_bounds_symlink_chains(tmp_path: Path) -> None:
    ctx, root = _namespace_fixture(tmp_path)
    (root / "loop").symlink_to("/loop")  # absolute self-link: an unbounded chain
    with pytest.raises(GateRefusal) as refusal:
        resolve_package_namespace_path(ctx, "installed content", f"{root}/loop")
    assert str(refusal.value) == (
        f"installed content contains more than 40 chained symlinks: {root}/loop"
    )


def test_namespace_walk_requires_existence(tmp_path: Path) -> None:
    ctx, root = _namespace_fixture(tmp_path)
    with pytest.raises(GateRefusal) as refusal:
        resolve_package_namespace_path(ctx, "installed content", f"{root}/usr/lib/ghost")
    assert str(refusal.value) == (
        f"installed content does not exist in the package namespace: {root}/usr/lib/ghost"
    )


def test_namespace_walk_refuses_paths_outside_the_root(tmp_path: Path) -> None:
    ctx, _ = _namespace_fixture(tmp_path)
    with pytest.raises(GateRefusal) as refusal:
        resolve_package_namespace_path(ctx, "installed content", "/etc/passwd")
    assert str(refusal.value) == "installed content escapes the package root: /etc/passwd"


# ---- manifest ownership forms ------------------------------------------------


def _manifest_context(tmp_path: Path) -> GateContext:
    root = tmp_path.resolve() / "pkg"
    (root / "usr").mkdir(parents=True)
    return GateContext(
        repo=str(tmp_path.resolve() / "unused-repo"),
        package_root=str(root),
        artifact_prefix=f"{root}/usr",
    )


def _manifest_arguments(manifest: Path) -> GateArguments:
    return GateArguments("/pkg", "/usr", str(manifest), True)


def test_manifest_loads_normalized_host_paths(tmp_path: Path) -> None:
    ctx = _manifest_context(tmp_path)
    manifest = tmp_path / "manifest"
    manifest.write_text("/usr/bin/latte-dock\n/usr/lib/liblattecoreplugin.so\n")
    enforce_manifest_ownership_set(ctx, _manifest_arguments(manifest))
    assert ctx.manifest_enforced
    assert ctx.manifest_paths == {
        f"{ctx.package_root}/usr/bin/latte-dock",
        f"{ctx.package_root}/usr/lib/liblattecoreplugin.so",
    }


def test_manifest_final_line_without_newline_still_counts(tmp_path: Path) -> None:
    ctx = _manifest_context(tmp_path)
    manifest = tmp_path / "manifest"
    manifest.write_text("/usr/bin/latte-dock")
    enforce_manifest_ownership_set(ctx, _manifest_arguments(manifest))
    assert ctx.manifest_paths == {f"{ctx.package_root}/usr/bin/latte-dock"}


@pytest.mark.parametrize(
    ("content", "message_template"),
    [
        ("", "package manifest is empty: {manifest}"),
        ("/usr/bin/latte-dock\n\n", "package manifest contains an empty entry: {manifest}"),
        (
            "/usr/bin/latte-dock\r\n",
            "package manifest contains a carriage return: /usr/bin/latte-dock\r",
        ),
        (
            "usr/bin/latte-dock\n",
            "package manifest entries must be absolute in the package namespace: "
            "usr/bin/latte-dock",
        ),
        (
            "/etc/passwd\n",
            "package manifest entry is outside the package prefix: /etc/passwd",
        ),
        (
            "/usr/bin/latte-dock\n/usr/bin/latte-dock\n",
            "package manifest contains a duplicate entry: /usr/bin/latte-dock",
        ),
    ],
)
def test_manifest_refusals_carry_exact_messages(
    tmp_path: Path, content: str, message_template: str
) -> None:
    ctx = _manifest_context(tmp_path)
    manifest = tmp_path / "manifest"
    manifest.write_text(content)
    with pytest.raises(GateRefusal) as refusal:
        enforce_manifest_ownership_set(ctx, _manifest_arguments(manifest))
    assert str(refusal.value) == message_template.format(manifest=manifest)


def test_manifest_must_be_a_regular_file(tmp_path: Path) -> None:
    ctx = _manifest_context(tmp_path)
    missing = tmp_path / "missing.manifest"
    with pytest.raises(GateRefusal) as refusal:
        enforce_manifest_ownership_set(ctx, _manifest_arguments(missing))
    assert str(refusal.value) == (f"package manifest is missing or not a regular file: {missing}")


# ---- small helpers -----------------------------------------------------------


def test_require_one_match_messages() -> None:
    assert require_one_match("thing", ["/only"]) == "/only"
    with pytest.raises(GateRefusal) as none_refusal:
        require_one_match("thing", [])
    assert str(none_refusal.value) == "expected exactly one installed thing, found 0 (none)"
    with pytest.raises(GateRefusal) as many_refusal:
        require_one_match("thing", ["/a", "/b"])
    assert str(many_refusal.value) == "expected exactly one installed thing, found 2 (/a /b)"


def test_shell_wait_status_mirrors_shell_wait() -> None:
    assert shell_wait_status(0) == 0
    assert shell_wait_status(7) == 7
    assert shell_wait_status(-6) == 134  # SIGABRT, the selftest's pinned example
    assert shell_wait_status(-9) == 137  # SIGKILL, how bash saw a --kill-after timeout
    assert shell_wait_status(-15) == 143  # SIGTERM
