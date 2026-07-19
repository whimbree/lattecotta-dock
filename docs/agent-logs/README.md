# Agent progress logs

One file per extraction unit executed by a worktree agent
(docs/archive/qml-extraction-execution-prompt.md governs the work).
Each agent updates its EX-NN.md at every commit: what landed with
hashes, what remains, and every live-recipe step recorded as PENDING
(agents never touch the live dock; the orchestrator runs the live
checks at merge). If an agent is interrupted, its log is the resume
point.
