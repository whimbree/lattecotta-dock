# Pure cores for the containment plugin

Behavioral logic extracted out of the containment package QML per
docs/tracking/QML_EXTRACTION_PLAN.md (section C conventions). Everything in
this directory is a pure core:

- Plain value structs in, plain values out.
- No QObject, no QQuickItem, no KConfig, no timers, no clock reads -
  time arrives as a parameter.
- QML keeps ownership of timers, bindings, and scenegraph items; the
  core owns decisions and math. The thin QML-facing wrapper lives in
  the plugin proper, not here.

Every header here must have a paired test in tests/units/
(<basename>test.cpp, registered in ctest) - enforced by
scripts/coverage-ratchet.sh. Cutover commits delete the QML logic
body they replace in the same commit; one revert restores it.
