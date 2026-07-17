# Coverage ratchet

Structural coverage enforcement, no instrumentation dependency, plain
shell (CI-portable). Location mirrors the latte-dock-qt6 reference
fork's tests/coverage/ so cross-reading stays cheap.

- `coverage-ratchet.sh` - two checks: every pure-core header under the
  units/ directories (plus tests/units/app-subtree-units.list) must
  have its paired, ctest-registered test; and the ctest entry list
  must match `ratchet-baseline` exactly.
- `ratchet-baseline` - the committed ctest entry list. Adding a test
  means adding its line in the same commit (routine); removing one is
  only possible by deliberately editing this file, which makes silent
  coverage loss un-mergeable. First non-comment line is the count;
  regenerate the list with the command in the file header.

Runs inside scripts/build-check.sh and therefore scripts/gate-all.sh;
standalone: `tests/coverage/coverage-ratchet.sh [build-dir]`.
