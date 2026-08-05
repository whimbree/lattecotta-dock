# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Proves the locked pydantic extension wheel loads and validates under
the active interpreter (the nix python on NixOS, the uv-managed 3.14
off-nix). pydantic-core is a compiled wheel; this test failing means the
interpreter/wheel pairing is broken, not that a model is wrong.
"""

import pytest
from pydantic import BaseModel, StrictInt, ValidationError


class _Probe(BaseModel):
    value: StrictInt


def test_pydantic_validates_at_runtime() -> None:
    assert _Probe(value=3).value == 3
    with pytest.raises(ValidationError) as excinfo:
        _Probe.model_validate({"value": "not-an-int"})
    assert excinfo.value.errors()[0]["loc"] == ("value",)
