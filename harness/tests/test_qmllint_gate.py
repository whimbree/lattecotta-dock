# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The qmllint ratchet's pure logic: parse, filter, count, serialize, diff.

The negative controls carry the weight: a non-curated category must not be
counted, a shuffled input must serialize identically (the D270 codepoint-order
contract), a corrupt baseline must be refused, and the fingerprint delta must
name the exact warning that appeared or vanished (the D269 diagnostic).
"""

import json
import shlex
from pathlib import Path

import pytest

from latte_harness import qmlenv
from latte_harness.qmllint_gate import (
    BASELINE_HEADER,
    SEED_VARS,
    FingerprintArtifact,
    FingerprintRecord,
    collect_fingerprints,
    curated_counts,
    diverging_files,
    fingerprint_delta,
    format_current,
    import_flags,
    load_previous_fingerprint,
    message_digest,
    parse_baseline,
    parse_report,
    per_category_counts,
    prepare_child_env,
    render_baseline,
    resolve_context,
    sorted_like_find,
    write_fingerprint,
)


def _report_json(files: dict[str, list[tuple[str, int, str]]]) -> str:
    """Build a qmllint --json blob. files maps filename -> [(id, line, message)]."""
    return json.dumps(
        {
            "revision": 3,
            "files": [
                {
                    "filename": name,
                    "success": not warnings,
                    "warnings": [
                        {"id": wid, "line": line, "column": 1, "type": "warning", "message": msg}
                        for (wid, line, msg) in warnings
                    ],
                }
                for name, warnings in files.items()
            ],
        }
    )


# --- parse + curated filter --------------------------------------------------


def test_parse_report_validates_the_boundary() -> None:
    report = parse_report(_report_json({"/s/A.qml": [("unqualified", 5, "x")]}))
    assert report.files[0].filename == "/s/A.qml"
    assert report.files[0].warnings[0].id == "unqualified"


def test_collect_fingerprints_keeps_only_curated_and_strips_stage() -> None:
    blob = _report_json(
        {
            "/stage/lib/qml/A.qml": [
                ("unqualified", 5, "Unqualified access"),
                ("import", 1, "not curated"),
                ("missing-property", 2, "also not curated"),
                ("deprecated", 9, "old syntax"),
            ]
        }
    )
    fps = collect_fingerprints(parse_report(blob), "/stage/")
    # Only the two curated categories survive; the stage prefix is stripped; the
    # records are sorted by (file, line, category), so line 5 precedes line 9.
    assert [(f.file, f.category, f.line) for f in fps] == [
        ("lib/qml/A.qml", "unqualified", 5),
        ("lib/qml/A.qml", "deprecated", 9),
    ]


def test_curated_counts_omits_zero_and_totals_per_file() -> None:
    blob = _report_json(
        {
            "/s/A.qml": [("unqualified", 1, "a"), ("unqualified", 2, "b"), ("import", 3, "x")],
            "/s/B.qml": [("import", 1, "only non-curated")],
            "/s/C.qml": [("deprecated", 4, "d")],
        }
    )
    counts = curated_counts(parse_report(blob), "/s/")
    assert counts == {"A.qml": 2, "C.qml": 1}  # B.qml (no curated) omitted


def test_per_category_counts_breaks_down_by_category() -> None:
    blob = _report_json(
        {"/s/A.qml": [("unqualified", 1, "a"), ("unqualified", 2, "b"), ("deprecated", 3, "c")]}
    )
    table = per_category_counts(parse_report(blob), "/s/")
    assert table == {"A.qml": {"unqualified": 2, "deprecated": 1}}


# --- D270: codepoint-order serialization, locale-independent -----------------


def test_format_current_is_codepoint_sorted() -> None:
    # C collation: uppercase 'A' (0x41) sorts before lowercase 'a' (0x61), which
    # is exactly the committed baseline's order and the D270 symptom's opposite.
    counts = {"b/appletabilities/C.qml": 3, "b/AppletAbilities.qml": 7, "a/Z.qml": 1}
    body = format_current(counts)
    assert body == "1\ta/Z.qml\n7\tb/AppletAbilities.qml\n3\tb/appletabilities/C.qml\n"


def test_format_current_shuffled_input_serializes_identically() -> None:
    # The D270 retirement contract: order of the input mapping cannot change the
    # output. Two dicts with the same items in different insertion order must
    # serialize byte-for-byte the same.
    items = [
        ("lib/qml/Z.qml", 2),
        ("lib/qml/A.qml", 5),
        ("share/M.qml", 1),
        ("lib/qml/appletabilities/x.qml", 4),
        ("lib/qml/AppletAbilities.qml", 9),
    ]
    forward = dict(items)
    reversed_order = dict(reversed(items))
    assert format_current(forward) == format_current(reversed_order)
    # And it is genuinely codepoint-sorted, not merely stable.
    rels = [line.split("\t", 1)[1] for line in format_current(forward).splitlines()]
    assert rels == sorted(rels)


def test_render_baseline_uses_the_committed_header_verbatim() -> None:
    text = render_baseline({"A.qml": 1})
    assert text.startswith(BASELINE_HEADER)
    assert text == BASELINE_HEADER + "1\tA.qml\n"


# --- baseline parse + refusal ------------------------------------------------


def test_parse_baseline_drops_comments_and_blanks() -> None:
    text = "# header\n#   more\n\n10\tlib/qml/A.qml\n3\tshare/B.qml\n"
    assert parse_baseline(text) == {"lib/qml/A.qml": 10, "share/B.qml": 3}


@pytest.mark.parametrize(
    "bad",
    [
        "10 lib/qml/A.qml\n",  # space, not a tab
        "notanumber\tlib/qml/A.qml\n",  # non-numeric count
        "10\t\n",  # empty rel
        "\tlib/qml/A.qml\n",  # empty count
    ],
)
def test_parse_baseline_refuses_malformed_lines(bad: str) -> None:
    with pytest.raises(ValueError, match="malformed baseline line"):
        parse_baseline(bad)


def test_baseline_render_parse_round_trip() -> None:
    counts = {"lib/qml/A.qml": 4, "share/B.qml": 1, "lib/qml/appletabilities/C.qml": 2}
    assert parse_baseline(render_baseline(counts)) == counts


# --- divergence + fingerprint delta (D269) -----------------------------------


def test_diverging_files_names_mismatches_and_one_sided() -> None:
    baseline = {"A.qml": 3, "B.qml": 2, "C.qml": 1}
    current = {"A.qml": 3, "B.qml": 5, "D.qml": 7}  # B changed, C gone, D new
    assert diverging_files(baseline, current) == ["B.qml", "C.qml", "D.qml"]


def _fp(file: str, category: str, line: int, message: str) -> FingerprintRecord:
    return FingerprintRecord(
        file=file, category=category, line=line, digest=message_digest(message), message=message
    )


def test_fingerprint_delta_names_appeared_and_vanished() -> None:
    previous = [
        _fp("A.qml", "unqualified", 5, "access to foo"),
        _fp("A.qml", "unqualified", 9, "access to bar"),
    ]
    current = [
        _fp("A.qml", "unqualified", 5, "access to foo"),  # unchanged
        _fp("A.qml", "deprecated", 12, "old syntax"),  # appeared
    ]
    delta = fingerprint_delta(previous, current)
    assert not delta.empty
    assert [(r.line, r.category) for r in delta.appeared] == [(12, "deprecated")]
    assert [(r.line, r.category) for r in delta.vanished] == [(9, "unqualified")]


def test_fingerprint_delta_empty_when_identical() -> None:
    fps = [_fp("A.qml", "unqualified", 5, "x")]
    assert fingerprint_delta(fps, list(fps)).empty


def test_fingerprint_digest_distinguishes_messages() -> None:
    # Same file/category/line, different message text -> different identity, so a
    # message-only change is nameable (the D269 "exact vanished warning" goal).
    a = _fp("A.qml", "unqualified", 5, "access to foo")
    b = _fp("A.qml", "unqualified", 5, "access to bar")
    assert a.identity != b.identity


# --- fingerprint artifact I/O ------------------------------------------------


def test_fingerprint_artifact_round_trips(tmp_path: Path) -> None:
    records = [_fp("A.qml", "unqualified", 5, "x"), _fp("B.qml", "deprecated", 2, "y")]
    path = tmp_path / "_qmllint_fingerprint.json"
    write_fingerprint(path, records)
    loaded = load_previous_fingerprint(path)
    assert loaded is not None
    assert [r.identity for r in loaded] == [r.identity for r in records]


def test_load_previous_fingerprint_absent_is_none(tmp_path: Path) -> None:
    assert load_previous_fingerprint(tmp_path / "nope.json") is None


def test_load_previous_fingerprint_corrupt_is_none_not_a_crash(tmp_path: Path) -> None:
    # A corrupt artifact is diagnostic-only: it must degrade to "no prior data",
    # never fail the gate (the verdict comes from counts vs the baseline).
    path = tmp_path / "_qmllint_fingerprint.json"
    path.write_text("{ this is not valid json")
    assert load_previous_fingerprint(path) is None


# --- child env + context: locked to the qmlenv bridge ------------------------

_PACKAGED = "/nix/store/aaaa-latte-dock-0.11/lib/qt-6/qml"
_KIRIGAMI = "/nix/store/cccc-kirigami-6.28.0/lib/qt-6/qml"


def test_prepare_child_env_strips_import_paths_and_seed_leaf() -> None:
    env = {
        "QML2_IMPORT_PATH": "/whatever",
        "QML_IMPORT_PATH": "/whatever2",
        "NIXPKGS_QT6_QML_IMPORT_PATH": f"{_KIRIGAMI}:{_PACKAGED}",
        "NIXPKGS_QML_SEARCH_PATHS": "",  # empty -> left untouched
        "PATH": "/usr/bin",
    }
    child = prepare_child_env(env)
    assert "QML2_IMPORT_PATH" not in child
    assert "QML_IMPORT_PATH" not in child
    assert child["NIXPKGS_QT6_QML_IMPORT_PATH"] == _KIRIGAMI  # packaged leaf gone
    assert child["NIXPKGS_QML_SEARCH_PATHS"] == ""  # empty stays empty (no strip)
    assert child["PATH"] == "/usr/bin"  # unrelated vars pass through


def _parse_bridge(script: str) -> tuple[dict[str, str], set[str], dict[str, str]]:
    """Split build_setup_script output into (scalars, unset-vars, exports)."""
    scalars: dict[str, str] = {}
    unset: set[str] = set()
    exports: dict[str, str] = {}
    for line in script.splitlines():
        if line.startswith("imports=("):
            continue
        toks = shlex.split(line)
        if not toks:
            continue
        if toks[0] == "unset":
            unset.update(toks[1:])
        elif toks[0] == "export":
            key, _, value = toks[1].partition("=")
            exports[key] = value
        else:
            key, _, value = toks[0].partition("=")
            scalars[key] = value
    return scalars, unset, exports


def test_child_env_matches_the_qmlenv_bridge(tmp_path: Path) -> None:
    # Lock SEED_VARS + prepare_child_env to qmlenv.build_setup_script so the two
    # implementations of the import-path doctrine cannot drift.
    module = tmp_path / "mod"
    module.mkdir()
    env = {
        "LATTE_QML_MODULE_PATH": str(module),
        "QML2_IMPORT_PATH": "/x",
        "QML_IMPORT_PATH": "/y",
        "NIXPKGS_QT6_QML_IMPORT_PATH": f"{_KIRIGAMI}:{_PACKAGED}",
    }
    _scalars, unset, exports = _parse_bridge(qmlenv.build_setup_script(tmp_path / "repo", env))
    child = prepare_child_env(env)

    assert unset == {"QML2_IMPORT_PATH", "QML_IMPORT_PATH"}
    for var in unset:
        assert var not in child
    # Every export the bridge emits, the child env reproduces, and no seed
    # var the gate touches is missing from the bridge (both-directions drift
    # guard).
    assert set(exports) == {v for v in SEED_VARS if env.get(v)}
    for var, value in exports.items():
        assert child[var] == value


def test_resolve_context_matches_the_bridge_scalars_and_imports(tmp_path: Path) -> None:
    module = tmp_path / "mod"
    module.mkdir()
    repo = tmp_path / "repo"
    env = {"LATTE_QML_MODULE_PATH": str(module)}

    scalars, _unset, _exports = _parse_bridge(qmlenv.build_setup_script(repo, env))
    ctx = resolve_context(repo, env)

    assert str(ctx.build) == scalars["build"]
    assert str(ctx.stage) == scalars["stage"]
    assert ctx.qmldir == scalars["qmldir"]

    # imports come straight from qmlenv.assemble_imports, so they match the array
    # the bridge emits verbatim.
    script_lines = qmlenv.build_setup_script(repo, env).splitlines()
    imports_line = next(line for line in script_lines if line.startswith("imports=("))
    bridge_imports = shlex.split(imports_line[len("imports=(") : -1])
    assert ctx.imports == bridge_imports


def test_resolve_context_honours_build_and_stage_overrides(tmp_path: Path) -> None:
    module = tmp_path / "mod"
    module.mkdir()
    build = tmp_path / "b"
    stage = tmp_path / "s"
    env = {"LATTE_QML_MODULE_PATH": str(module), "BUILD": str(build), "STAGE": str(stage)}
    ctx = resolve_context(tmp_path / "repo", env)
    assert ctx.build == build
    assert ctx.stage == stage


def test_resolve_context_refuses_without_module_path(tmp_path: Path) -> None:
    from latte_harness.qmlenv import MissingModulePathError

    with pytest.raises(MissingModulePathError):
        resolve_context(tmp_path / "repo", {})


# --- import flags ------------------------------------------------------------


def test_import_flags_stage_first_then_import_dirs() -> None:
    stage = Path("/st")
    imports = ["-import", "/a", "-import", "/b", "-import", "/st/lib/qml"]
    assert import_flags(stage, imports) == [
        "-I",
        "/st/lib/qml",  # the leading staged tree (mirrors the shell version)
        "-I",
        "/a",
        "-I",
        "/b",
        "-I",
        "/st/lib/qml",
    ]


def test_sorted_like_find_is_locale_independent(monkeypatch: pytest.MonkeyPatch) -> None:
    # The D269 fix contract: input order is codepoint sort no matter what
    # locale the invoking shell exports. A UTF-8 collation would interleave
    # 'appletabilities/' before 'AppletAbilities.qml'; codepoint order must
    # hold under both environments, so the qmllint warning set cannot vary
    # with the invoking shell (see the function's docstring).
    paths = [
        Path("b/appletabilities/x.qml"),
        Path("b/AppletAbilities.qml"),
        Path("a/Z.qml"),
    ]
    expected = [
        "a/Z.qml",
        "b/AppletAbilities.qml",
        "b/appletabilities/x.qml",
    ]
    for env in ("C", "en_US.UTF-8"):
        monkeypatch.setenv("LC_ALL", env)
        monkeypatch.setenv("LC_COLLATE", env)
        monkeypatch.setenv("LANG", env)
        assert [str(p) for p in sorted_like_find(paths)] == expected


def test_artifact_serialization_is_indented_json(tmp_path: Path) -> None:
    # A human-readable, diffable artifact (observability-first): valid JSON that
    # round-trips through the model.
    path = tmp_path / "fp.json"
    write_fingerprint(path, [_fp("A.qml", "unqualified", 1, "x")])
    parsed = FingerprintArtifact.model_validate_json(path.read_text())
    assert parsed.warnings[0].file == "A.qml"
