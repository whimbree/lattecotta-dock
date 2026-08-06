# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""scripts/tools/dumpwins.sh ported (BP-5a, the dev-tools chunk).

Dump every KWin window - resource class, caption, frame geometry, output, and
stacking layer - via a transient KWin script, one `DUMPWIN|...` line each. The
line format is a documented live-surface contract (the latte-live-verification
skill and the e2e recipes parse it), so the output is byte-identical to the
bash original:

    DUMPWIN|<class>|<caption>|<x>,<y> <w>x<h>|<output>|layer=<N>

The whole dump is `recipe.e2e_dumpwins` already: the transient-script
load/run/stop/unload dance, the run-tag isolation, and the mode branch (the
nested vehicle reads E2E_KWIN_LOG, a live session reads the journal) all live
there and are proven byte-identical against lib.sh's e2e_dumpwins. This tool is
the thin front door that adds only the bash's empty-result fallback.
"""

from __future__ import annotations

from latte_harness import recipe


def main() -> None:
    """Print the window dump, or the bash's `no output captured` when empty.

    The bash was `journalctl ... | grep "DUMPWIN|" || echo "no output captured"`:
    grep emits the matching lines, and the `||` fallback fires only when nothing
    matched. recipe.dumpwins() returns those same lines joined (no trailing
    newline) or "" when none were captured, so print() reproduces both the
    populated dump and the fallback exactly.
    """
    dump = recipe.dumpwins()
    print(dump if dump else "no output captured")


if __name__ == "__main__":
    main()
