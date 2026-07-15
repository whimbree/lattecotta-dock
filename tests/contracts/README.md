# Upstream contract tests

Latte leans on a wide surface of upstream behavior: libplasma applet and
containment lifecycle, PlasmaQuick representation management and dialogs,
KSvg, KWin wayland protocols, Qt Quick effect and type semantics. The
port's history shows that when those behaviors shift between versions,
Latte does not fail loudly at the API boundary - it misbehaves somewhere
far away (a popup sized from a feedback loop, a widget ghost that lives
exactly as long as an upstream undo timer, a colorizer that silently
paints nothing).

Tests in this directory pin the upstream behaviors Latte RELIES ON, at
the versions pinned by the flake. They are not tests of Latte code; when
one fails after a pin bump, it localizes an upstream semantic change to
the exact assumption that broke, before any dock misbehaves.

Rules for a contract test:

- Each test names, in a comment, the Latte code that depends on the
  contract and the incident commit (if any) where the assumption was
  first earned. A contract nobody depends on is not worth pinning.
- Tests run headless (QT_QPA_PLATFORM=offscreen) through qmltestrunner
  or ecm_add_test; nothing here may launch or touch a running dock.
- Assert the behavior actually relied on, not incidental details -
  contract tests should survive upstream refactors that keep semantics.

Run via ctest (test name: qmlcontracts) or directly:

    nix develop -c scripts/qml-interaction-tests.sh tests/contracts
