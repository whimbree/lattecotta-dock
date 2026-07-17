# Pure-core unit tests

Tests for the pure cores extracted out of QML and out of live-manager
C++ by docs/QML_EXTRACTION_PLAN.md. One test file per core, named
`<unit>test.cpp` after the core's header basename: the core
`activitysetalgebra.h` pairs with `activitysetalgebratest.cpp` here.
That pairing is mechanical on purpose - tests/coverage/coverage-ratchet.sh
enforces it (every unit header must have its paired test registered in
ctest), so a unit cannot land untested without failing build-check.

Rules, from the plan's section C conventions and docs/TESTING.md:

- Tests include the core header directly. No QML engine, no corona, no
  mocks of Latte logic - a core that needs any of those is not pure
  and belongs back in design.
- The honest-coverage standard applies verbatim: real assertions,
  no construction-only credit, no swallowed throws. Reference tables
  generated from a current implementation must name their generation
  method in a comment.
- Qt5 fidelity cases come from reading the f0ad7b23 ancestor named in
  the unit's spec, not from assuming the port is faithful.
- capt (latte-dock-qt6) test slots that a spec says to port are case
  lists to re-derive against OUR tree, never code to paste.
