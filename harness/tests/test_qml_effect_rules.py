# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Negative controls for the effect-rule scanner, plus a current-tree pass.

Each rule's synthetic violation must be detected, and each legal shape must
pass; the module must also render the real shipped tree clean (the BP
equivalence contract: same verdict as the bash version).
"""

import pytest

from latte_harness import qml_effect_rules as effect

# --- Rule 1: autoPaddingEnabled may only ever be the literal false ----------


def test_rule1_flags_non_false_assignment() -> None:
    hits = effect.autopadding_violations("    autoPaddingEnabled: true\n", "/tree/E.qml")
    assert hits == ["/tree/E.qml:1:    autoPaddingEnabled: true"]


def test_rule1_flags_bound_assignment() -> None:
    hits = effect.autopadding_violations("autoPaddingEnabled: someProperty\n", "/tree/E.qml")
    assert hits == ["/tree/E.qml:1:autoPaddingEnabled: someProperty"]


@pytest.mark.parametrize(
    "line",
    [
        "autoPaddingEnabled: false",
        "autoPaddingEnabled: false;",
        "autoPaddingEnabled: false // static, see e3376405",
        "    autoPaddingEnabled:false ",
    ],
)
def test_rule1_allows_literal_false(line: str) -> None:
    assert effect.autopadding_violations(line + "\n", "/tree/E.qml") == []


def test_rule1_ignores_prose_without_assignment() -> None:
    # A comment mentioning the property (no `identifier:` assignment shape).
    assert effect.autopadding_violations("// never set autoPaddingEnabled here\n", "/x.qml") == []


def test_rule1_falsely_is_not_the_literal_false() -> None:
    # `false` followed by a letter is not the allowed literal, so it is flagged.
    hits = effect.autopadding_violations("autoPaddingEnabled: falsely\n", "/tree/E.qml")
    assert hits == ["/tree/E.qml:1:autoPaddingEnabled: falsely"]


# --- Rule 2: every when-gated Binding declares an explicit restoreMode -------


def test_rule2_flags_when_gated_binding_without_restoremode() -> None:
    text = "Binding {\n    target: foo\n    when: hovered\n    value: 1\n}\n"
    assert effect.binding_violations(text, "/tree/B.qml") == ["/tree/B.qml:1"]


def test_rule2_allows_when_gated_binding_with_restoremode() -> None:
    text = "Binding {\n    when: hovered\n    restoreMode: Binding.RestoreNone\n    value: 1\n}\n"
    assert effect.binding_violations(text, "/tree/B.qml") == []


def test_rule2_ignores_binding_without_when() -> None:
    text = "Binding {\n    target: foo\n    value: 1\n}\n"
    assert effect.binding_violations(text, "/tree/B.qml") == []


def test_rule2_matches_binding_on_property_form() -> None:
    text = "Binding on width {\n    when: hovered\n    value: 1\n}\n"
    assert effect.binding_violations(text, "/tree/B.qml") == ["/tree/B.qml:1"]


def test_rule2_reports_correct_line_for_later_binding() -> None:
    text = (
        "Item {\n"
        "    Binding {\n"
        "        when: a\n"
        "        restoreMode: Binding.RestoreNone\n"
        "    }\n"
        "    Binding {\n"
        "        when: b\n"
        "    }\n"
        "}\n"
    )
    assert effect.binding_violations(text, "/tree/B.qml") == ["/tree/B.qml:6"]


def test_rule2_restoremode_of_one_binding_does_not_satisfy_another() -> None:
    # The brace-matcher scopes restoreMode to its own Binding block: a sibling
    # Binding declaring it must not excuse a when-gated one that omits it.
    text = "Binding {\n    restoreMode: Binding.RestoreNone\n}\nBinding {\n    when: hovered\n}\n"
    assert effect.binding_violations(text, "/tree/B.qml") == ["/tree/B.qml:4"]


# --- Current-tree verdict ---------------------------------------------------


def test_current_tree_passes(capsys: pytest.CaptureFixture[str]) -> None:
    effect.main()  # raises SystemExit(1) if the shipped tree violates a rule
    out = capsys.readouterr().out
    assert out.startswith("qml-effect-rules: OK")
