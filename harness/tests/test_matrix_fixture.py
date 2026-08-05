# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
"""matrix_fixture: KConfig round-trip fidelity, every refusal path, and a
byte-identical golden compared field-by-field against the pre-swap fixture.py.

The golden data files under tests/data/matrix_fixture/ were captured from the
original tests/e2e/matrix/fixture.py BEFORE it was deleted; a byte difference
here means the promoted generator diverged from the proven config transform.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from latte_harness import matrix_fixture
from latte_harness.matrix_fixture import (
    CellDescriptor,
    KConfigDocument,
    RefusedError,
    build_parser,
    descriptor_from_args,
    generate,
)

DATA = Path(__file__).parent / "data" / "matrix_fixture"


def _data(name: str) -> str:
    return (DATA / name).read_text()


def _make_seed(
    root: Path, *, layout_name: str = "My Layout.layout.latte", with_rc: bool = True
) -> Path:
    """A hermetic seed dir built from the committed golden seed files."""
    seed = root / "seed"
    (seed / "latte").mkdir(parents=True)
    _ = (seed / "latte" / layout_name).write_text(_data("seed.layout.latte"))
    if with_rc:
        _ = (seed / "lattedockrc").write_text(_data("seed.lattedockrc"))
    return seed


def _descriptor(**overrides: object) -> CellDescriptor:
    fields: dict[str, object] = {
        "view_type": "dock",
        "edge": "top",
        "alignment": "left",
        "display": "1out",
        "screen": "",
        "screen_id": matrix_fixture.SECONDARY_ABSENT_SCREEN_ID,
        "screen_geometry": "",
        "cell": "dock-top-left-1out",
    }
    fields.update(overrides)
    return CellDescriptor.model_validate(fields)


# --- KConfig parse/serialize -------------------------------------------------

CANONICAL = "[Group][1]\na=1\nb=2\n\n[Group][2]\nc=3\n"


def test_kconfig_round_trip_is_identity_for_canonical_text() -> None:
    assert KConfigDocument.parse(CANONICAL).serialize() == CANONICAL


def test_kconfig_preserves_group_and_key_order() -> None:
    doc = KConfigDocument.parse(CANONICAL)
    assert list(doc.groups) == ["[Group][1]", "[Group][2]"]
    assert list(doc.groups["[Group][1]"]) == ["a", "b"]


def test_kconfig_value_is_not_stripped_but_key_is() -> None:
    # KConfig is not standard INI: the value keeps its surrounding whitespace,
    # only the key is trimmed. This exact fidelity is why the parser is hand-rolled.
    doc = KConfigDocument.parse("[G]\n  spaced key  = value with spaces \n")
    assert doc.groups["[G]"] == {"spaced key": " value with spaces "}


def test_kconfig_value_keeps_embedded_equals() -> None:
    doc = KConfigDocument.parse("[G]\nlaunchers=a=1,b=2\n")
    assert doc.groups["[G]"]["launchers"] == "a=1,b=2"


def test_kconfig_duplicate_header_reselects_the_same_group() -> None:
    doc = KConfigDocument.parse("[G]\na=1\n\n[G]\nb=2\n")
    assert list(doc.groups) == ["[G]"]
    assert doc.groups["[G]"] == {"a": "1", "b": "2"}


def test_set_key_appends_new_and_updates_existing_in_place() -> None:
    doc = KConfigDocument.parse("[G]\na=1\nb=2\n")
    doc.set_key("[G]", "a", 9)  # existing: update in place, stringified
    doc.set_key("[G]", "c", 3)  # new: append at end
    doc.set_key("[NewGroup]", "x", "y")  # new group: append at end
    assert list(doc.groups["[G]"]) == ["a", "b", "c"]
    assert doc.groups["[G]"]["a"] == "9"
    assert list(doc.groups) == ["[G]", "[NewGroup]"]


def test_kconfig_unparseable_line_refuses() -> None:
    with pytest.raises(RefusedError) as excinfo:
        _ = KConfigDocument.parse("[G]\nno equals here\n")
    assert str(excinfo.value) == "unparseable config line (no '='): 'no equals here'"


# --- refusal paths (parametrized) -------------------------------------------

TOKEN_REFUSALS: list[tuple[list[str], str]] = [
    (
        ["--view-type", "slab", "--edge", "top", "--alignment", "left"],
        "view-type 'slab' is not one of dock|panel",
    ),
    (
        ["--view-type", "dock", "--edge", "diagonal", "--alignment", "left"],
        "edge 'diagonal' is not one of top|bottom|left|right",
    ),
    (
        ["--view-type", "dock", "--edge", "top", "--alignment", "skew"],
        "alignment 'skew' is not one of left|center|right|justify",
    ),
    (
        ["--view-type", "dock", "--edge", "top", "--alignment", "left", "--display", "3out"],
        "display '3out' is not one of 1out|2out",
    ),
]


@pytest.mark.parametrize(("args", "message"), TOKEN_REFUSALS)
def test_bad_token_is_refused_with_exact_message(args: list[str], message: str) -> None:
    namespace = build_parser().parse_args([*args, "--seed-dir", "s", "--out-dir", "o"])
    with pytest.raises(RefusedError) as excinfo:
        _ = descriptor_from_args(namespace)
    assert str(excinfo.value) == message


def test_missing_seed_dir_is_refused(tmp_path: Path) -> None:
    missing = tmp_path / "nope"
    with pytest.raises(RefusedError) as excinfo:
        _ = generate(str(missing), str(tmp_path / "out"), _descriptor())
    assert str(excinfo.value) == f"seed dir {str(missing)!r} does not exist"


def test_seed_without_latte_subdir_is_refused(tmp_path: Path) -> None:
    seed = tmp_path / "seed"
    seed.mkdir()
    with pytest.raises(RefusedError) as excinfo:
        _ = generate(str(seed), str(tmp_path / "out"), _descriptor())
    assert (
        str(excinfo.value)
        == f"seed dir {str(seed)!r} has no latte/ subdir (not a seeded config home)"
    )


def test_seed_without_any_layout_is_refused(tmp_path: Path) -> None:
    seed = tmp_path / "seed"
    (seed / "latte").mkdir(parents=True)
    with pytest.raises(RefusedError) as excinfo:
        _ = generate(str(seed), str(tmp_path / "out"), _descriptor())
    assert str(excinfo.value) == f"seed dir {str(seed)!r} carries no *.layout.latte"


def test_ambiguous_layouts_with_no_named_match_is_refused(tmp_path: Path) -> None:
    seed = tmp_path / "seed"
    (seed / "latte").mkdir(parents=True)
    _ = (seed / "latte" / "a.layout.latte").write_text(_data("seed.layout.latte"))
    _ = (seed / "latte" / "b.layout.latte").write_text(_data("seed.layout.latte"))
    _ = (seed / "lattedockrc").write_text("[UniversalSettings]\nmemoryUsage=0\n")
    with pytest.raises(RefusedError) as excinfo:
        _ = generate(str(seed), str(tmp_path / "out"), _descriptor())
    assert (
        str(excinfo.value)
        == f"seed dir {str(seed)!r} has 2 layouts and lattedockrc names none of them"
    )


def test_seed_without_latte_containment_is_refused(tmp_path: Path) -> None:
    seed = tmp_path / "seed"
    (seed / "latte").mkdir(parents=True)
    _ = (seed / "latte" / "x.layout.latte").write_text(
        "[Containments][1]\nplugin=org.kde.plasma.folder\n"
    )
    _ = (seed / "lattedockrc").write_text("[UniversalSettings]\nsingleModeLayoutName=x\n")
    with pytest.raises(RefusedError) as excinfo:
        _ = generate(str(seed), str(tmp_path / "out"), _descriptor())
    assert str(excinfo.value) == (
        "seed layout has no org.kde.latte.containment containment to parametrize"
    )


def test_seed_with_several_latte_containments_is_refused(tmp_path: Path) -> None:
    seed = tmp_path / "seed"
    (seed / "latte").mkdir(parents=True)
    _ = (seed / "latte" / "x.layout.latte").write_text(
        "[Containments][1]\nplugin=org.kde.latte.containment\n\n"
        "[Containments][2]\nplugin=org.kde.latte.containment\n"
    )
    _ = (seed / "lattedockrc").write_text("[UniversalSettings]\nsingleModeLayoutName=x\n")
    with pytest.raises(RefusedError) as excinfo:
        _ = generate(str(seed), str(tmp_path / "out"), _descriptor())
    assert str(excinfo.value) == (
        "seed layout has 2 Latte containments ([Containments][1], [Containments][2]); "
        "the matrix fixture needs exactly one"
    )


def test_two_out_named_pin_without_lattedockrc_is_refused(tmp_path: Path) -> None:
    seed = _make_seed(tmp_path, with_rc=False)
    descriptor = _descriptor(
        view_type="dock",
        edge="bottom",
        alignment="center",
        display="2out",
        screen="HDMI-A-2",
        screen_id=11,
        screen_geometry="1600,0 1600x1000",
        cell="dock-bottom-center-2out",
    )
    with pytest.raises(RefusedError) as excinfo:
        _ = generate(str(seed), str(tmp_path / "out"), descriptor)
    assert str(excinfo.value) == (
        f"seed dir {str(seed)!r} has no lattedockrc to seed [ScreenConnectors] "
        "into (a 2out pin needs it)"
    )


def test_refused_run_exits_2_writes_nothing(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    seed = _make_seed(tmp_path)
    out = tmp_path / "out"
    code = matrix_fixture.run(
        [
            "--seed-dir",
            str(seed),
            "--out-dir",
            str(out),
            "--view-type",
            "slab",
            "--edge",
            "top",
            "--alignment",
            "left",
        ]
    )
    captured = capsys.readouterr()
    assert code == 2
    assert not out.exists()
    assert captured.err == "fixture.py: REFUSED: view-type 'slab' is not one of dock|panel\n"


# --- golden generation (byte-identical to the pre-swap fixture.py) -----------


def test_golden_panel_bottom_justify(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    seed = _make_seed(tmp_path)
    out = tmp_path / "out"
    code = matrix_fixture.run(
        [
            "--seed-dir",
            str(seed),
            "--out-dir",
            str(out),
            "--view-type",
            "panel",
            "--edge",
            "bottom",
            "--alignment",
            "justify",
            "--display",
            "1out",
        ]
    )
    captured = capsys.readouterr()
    assert code == 0
    assert (out / "latte" / "My Layout.layout.latte").read_text() == _data(
        "panel-bottom-justify.layout.latte"
    )
    assert (out / "matrix-cell.json").read_text() == _data("panel-bottom-justify.matrix-cell.json")
    assert captured.out == _data("panel-bottom-justify.stdout")


def test_golden_dock_2out_named_seeds_screenconnectors(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    seed = _make_seed(tmp_path)
    out = tmp_path / "out"
    code = matrix_fixture.run(
        [
            "--seed-dir",
            str(seed),
            "--out-dir",
            str(out),
            "--view-type",
            "dock",
            "--edge",
            "bottom",
            "--alignment",
            "center",
            "--display",
            "2out",
            "--screen",
            "HDMI-A-2",
            "--screen-id",
            "11",
            "--screen-geometry",
            "1600,0 1600x1000",
        ]
    )
    captured = capsys.readouterr()
    assert code == 0
    assert (out / "latte" / "My Layout.layout.latte").read_text() == _data("dock2out.layout.latte")
    assert (out / "lattedockrc").read_text() == _data("dock2out.lattedockrc")
    assert (out / "matrix-cell.json").read_text() == _data("dock2out.matrix-cell.json")
    assert captured.out == _data("dock2out.stdout")
