Start by reading two documents end to end, in this order, before touching anything:
CLAUDE.md (the big picture - what this project is, and the binding rigor: root-cause
law, regression discipline, code-clarity and Q_ASSERT rules, commit shape and the
definition of done, gate discipline) and docs/prompts/stabilization-execution-prompt.md
(the session contract - re-runnable priority list, documentation duties, merge
protocol). Then execute that prompt top to bottom, autonomously, with a long horizon.

Item 0 is mandatory and blocks everything: the machine was rebuilt and rebooted since
the last session (incident record at the top of docs/tracking/session-handoff.md - read that
entry too before running any gate). Re-pin the dev flake to the nixpkgs revision the
system ACTUALLY runs now (readlink /run/current-system; the system flake.lock at
/persist/etc/nixos is the machine-readable source), full rebuild, expect a sceneprobe
golden re-bless with the two-run determinism check, verify the nested vehicle end to
end, add the pin-vs-system lockstep guard to scripts/gate-all.sh, restart my real dock
(scripts/start-dock.sh) and confirm lifecycleState + viewsData settle. The last few
pushes went out --no-verify while the sceneprobe gate was environmentally down - once
gates are green again, run gate-all so the stamp catches up to HEAD before your first
code push.

Then the open items in the prompt's order: keyboard focus mode (the a11y P0 gate),
Accessible.* rollout, e2e promotion (fold run-e2e.sh + the nested recipes into
tests/e2e with KWin ScreenShot2 pixel assertions - item c and P4 together), P3b
transplants (shortcutstest and storagetest first), Phase 9 color-group audit, and the
remaining extraction-ledger recipes (EX-15/17/14 in the nested vehicle; EX-12 needs my
real config, only at an idle desk).

Standing rules, all binding: worktree subagents for disjoint items (max 4, ledgers in
docs/agent-logs/, merge serially, post-rebase hashes at tick time); gates are exit
codes only via scripts/gate-all.sh - never scrape logs, never bypass the pre-push hook;
use the D-Bus surface (docs/reference/dbus-interface-reference.md) for state instead of pixels or
sleeps, and document any new surface in all three places; prefer the nested vehicle,
but real-session verification is fine whenever a recipe needs it - throwaway first,
restore --user-config after, back off if you see desk activity; tick
docs/tracking/PORTING_PLAN.md, update the prompt's status list and the README on major landings
(timeless register, no session narration); push after each verified chunk; keep
docs/tracking/session-handoff.md current; route anything needing my hands to
docs/reference/manual-flake-removal-testing.md with a recipe. When a check finds a defect,
root-cause it before returning to the checklist. Do not stop until the open list is
exhausted or every remaining item genuinely needs my hands.