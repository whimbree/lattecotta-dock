# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Loud, prefix-consistent status output.

The bash harness prints ``<tool>: FAIL <why>`` and exits nonzero;
silence is not error handling (the failures-and-root-cause agreements).
These helpers keep that exact surface so log-reading habits and the
docs' quoted output stay valid across the migration. fail() never
returns.
"""

from __future__ import annotations

import sys
from typing import NoReturn


def info(tool: str, msg: str) -> None:
    print(f"{tool}: {msg}", flush=True)


def warn(tool: str, msg: str) -> None:
    print(f"{tool}: WARNING: {msg}", file=sys.stderr, flush=True)


def fail(tool: str, msg: str, code: int = 1) -> NoReturn:
    """Print the failure loudly and exit with ``code``.

    Raises SystemExit rather than os._exit, so context managers and
    finally-blocks (process teardown) run on the way out.
    """
    print(f"{tool}: FAIL {msg}", file=sys.stderr, flush=True)
    raise SystemExit(code)
