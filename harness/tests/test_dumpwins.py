# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The dumpwins tool front door: it delegates the whole dump to
recipe.e2e_dumpwins and adds only the bash's empty-result fallback. The dump
itself (transient KWin script, tag isolation, DUMPWIN|... format) is proven in
test_recipe.py; here the only logic is the populated-vs-empty branch.
"""

import pytest

from latte_harness import dumpwins, recipe


def test_populated_dump_is_echoed_verbatim(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setattr(recipe, "dumpwins", lambda: "DUMPWIN|kitty|k|0,0 800x600|out|layer=2")
    dumpwins.main()
    assert capsys.readouterr().out == "DUMPWIN|kitty|k|0,0 800x600|out|layer=2\n"


def test_multiline_dump_keeps_every_line(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    dump = "DUMPWIN|a|x|0,0 1x1|o|layer=2\nDUMPWIN|b|y|1,1 2x2|o|layer=3"
    monkeypatch.setattr(recipe, "dumpwins", lambda: dump)
    dumpwins.main()
    assert capsys.readouterr().out == dump + "\n"


def test_empty_dump_prints_the_bash_fallback(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setattr(recipe, "dumpwins", lambda: "")
    dumpwins.main()
    assert capsys.readouterr().out == "no output captured\n"


def test_transport_failure_exits_loudly_instead_of_faking_an_empty_dump(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    # A2 (kwin_js swallowed loadScript failures into ""): the tool must exit 1
    # with the error on stderr, never print the ran-and-captured-nothing line.
    def unreachable() -> str:
        raise recipe.KwinScriptError("e2e_kwin_js: loadScript failed")

    monkeypatch.setattr(recipe, "dumpwins", unreachable)
    with pytest.raises(SystemExit) as excinfo:
        dumpwins.main()
    assert excinfo.value.code == 1
    captured = capsys.readouterr()
    assert captured.out == ""
    assert "loadScript failed" in captured.err
