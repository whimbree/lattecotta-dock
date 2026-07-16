# Visible-index twin equality harness (EX-06 generator)

Drives the SHIPPED indexer twins offscreen - the real containment
`abilities/Indexer.qml` (over its `IndexerPrivate.qml`) and the real
client `abilities/client/Indexer.qml`, wired to each other through a
mock bridge exactly the way the live communicator wires them (the
client assigns itself as `bridge.indexer.client`; the containment twin
reads its `visibleItemsCount` through that assignment; the client's
`visibleIndex` resolves its host base through `bridge.indexer.host`).
The `AppletItem.qml`/`BasicItem.qml` walk bodies cannot be instantiated
outside a full dock, so the harness executes VERBATIM copies of them
(source lines and hash cited at each copy) against the real twins'
arrays - including the live client-delegation branch through
`getClientBridge` and the row-edge fallback through the bridge.

Prints one `CASE|<name>|<json>` line per scenario. The vectors baked
into `tests/units/visibleindextest.cpp` came from this harness at
b0e606b2:

    scripts/qml-interaction-tests.sh tests/generators/visibleindex

NOT wired into ctest: it is a generation tool, and after the EX-06
cutovers delete the QML bodies it documents where the reference vectors
came from rather than remaining runnable against them. Re-run it BEFORE
the cutover commits if the case matrix ever needs extending.

Two shipped behaviors the harness recorded that the core deliberately
does NOT reproduce (deviations documented in
`declarativeimports/core/units/visibleindex.h` and pinned by unit
tests):

- `hiddenClient`: a HIDDEN multi-item applet with 3 tasks answered
  `visibleIndexBelongsAtApplet` true for visible indexes 0 and 1
  (Qt5's -1 base fell through to the range branch: [-1, 2)). The core
  answers false - an invisible entry owns no visible index.
- an index present in no entry answered `visibleIndex` as
  1 + countBefore (a phantom position); the core answers "none".
