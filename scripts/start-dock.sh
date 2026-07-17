#!/usr/bin/env bash
# Start (or restart) the staged dock against MY REAL user config - the
# daily-driver entry point. Wraps restart-staged.sh, which kills any
# stale instance safely (including SIGSTOPped orphans), restages QML,
# and launches detached from this terminal.
#
#   scripts/start-dock.sh        # real config, quiet logs
#   scripts/start-dock.sh -d     # real config, debug logging to stdout
#
# For the THROWAWAY config (never touches ~/.config) use
# restart-staged.sh directly without --user-config.
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
exec "$repo/scripts/restart-staged.sh" --user-config "$@"
