# latte-harness

The typed Python test/gate harness for the latte-dock Plasma 6 port. This
package is the destination of the BP (bash-to-python) migration; the plan of
record is `docs/tracking/bash-python-migration-plan.md`.

## Running

Inside the devShell (`nix develop`), or anywhere with uv installed:

    uv run --locked --project harness latte-harness-check

runs the full harness gate leg: ruff lint, ruff format check, basedpyright at
strict mode, the harness unit tests, and the retained-bash allowlist ratchet.
The exit code is the verdict.

Off-nix, uv provisions the interpreter pinned in `.python-version` and the
locked dependencies; no system Python or packages are required.

## Dependency policy

Minimal by contract: pydantic is the only runtime dependency (busctl stays
the D-Bus transport, argparse the CLI parser). Dev tools are pytest plus ruff
and basedpyright pinned to the same versions the devShell provides from
nixpkgs; on NixOS the nix binaries run (the PyPI wheels of both ship
foreign-glibc executables NixOS cannot run unpatched), off-nix the locked
wheels run, and the pins keep the two worlds identical.
