#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# P9 / C-I10 acceptance (HC3): prove the fakepointer `key` verb's Escape
# actually CANCELS an in-flight drag, observed as the drag's effect being
# ABORTED - not merely that a keysym was delivered.
#
# WHY THIS DRAG. A standalone `key` invocation cannot inject into a
# pointer-button-held drag: a fake_input client holds its button only while
# connected, and KWin drops held input the instant the tool exits, so the
# button would release before a separate `key` process could run. The
# in-flight drag a STANDALONE key verb can genuinely cancel is a PERSISTENT
# compositor mode: KWin's keyboard interactive window-move ("Window Move").
# It stays in-mode across input-client boundaries; arrow keys nudge the window
# live (Window::keyPressEvent, delta 8px, no modifiers); Return commits at the
# nudged spot; Escape cancels and RESTORES the pre-move geometry
# (finishInteractiveMoveResize(cancel=true) -> moveResize(initialGeometry)).
# That restore is the observable cancel effect this recipe asserts. The real
# A2/A3 pointer-drag Escape sub-paths (P6/P7) will interleave the key WITHIN
# one drag client; this recipe proves the key verb and its cancel effect on
# the simplest in-flight drag constructible from the primitives that exist
# today, per the C-I10 brief.
#
# TRIPWIRE (HC3 observes-a-rejection). The recipe drives BOTH outcomes and
# proves it tells them apart: a Return trial COMMITS (window ends moved) and an
# Escape trial CANCELS (window returns to its pre-move spot). A key verb that
# silently did nothing leaves the Return trial unmoved (the recipe fails); an
# Escape that did not cancel leaves the window moved like a commit (the restore
# assertion fails). So a green run requires the cancel to have actually taken.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"

fp() { "$E2E_FAKEPOINTER" "$@"; }

#! id|x|y|w|h of the konsole window from the compositor's own truth (KWin
#! updates frameGeometry live during an interactive move, so this reads the
#! in-flight position too). e2e_kwin_js has already stripped the run tag.
konsole_geo() {
    e2e_kwin_js 'for (const w of workspace.windowList()) {
        if (w.resourceClass == "org.kde.konsole") {
            print("@TAG@|" + w.internalId + "|" + Math.round(w.frameGeometry.x) + "|" + Math.round(w.frameGeometry.y) + "|" + Math.round(w.frameGeometry.width) + "|" + Math.round(w.frameGeometry.height));
        }
    }' | tail -1
}
active_id() {
    e2e_kwin_js 'print("@TAG@|" + (workspace.activeWindow ? workspace.activeWindow.internalId : "none"));' | tail -1
}
konsole_count() { e2e_dumpwins | grep -c '|org.kde.konsole|' || true; }

#! Start KWin's keyboard interactive move on the active window. Component
#! "kwin", shortcut unique name "Window Move" (useractions.cpp initShortcut);
#! invokeShortcut fires it whether or not a key is bound.
invoke_window_move() {
    busctl --user call org.kde.kglobalaccel /component/kwin \
        org.kde.kglobalaccel.Component invokeShortcut s "Window Move" >/dev/null 2>&1
}

# ---- a normal toplevel to move (the dock is a layer-shell surface, not
#      interactively movable). konsole is the vehicle's proven client (020). --
[[ "$(konsole_count)" -eq 0 ]] || e2e_fail "konsole already present; this recipe owns its client"
setsid konsole >/dev/null 2>&1 &
kpid=$!
cleanup() { kill "$kpid" 2>/dev/null || true; }
trap cleanup EXIT
for _ in $(seq 1 20); do [[ "$(konsole_count)" -ge 1 ]] && break; sleep 1; done
[[ "$(konsole_count)" -ge 1 ]] || e2e_fail "konsole never mapped in the vehicle"
sleep 2   #! let it settle to a stable geometry

read -r kid kx0 ky0 kw kh <<< "$(konsole_geo | tr '|' ' ')"
[[ -n "$kid" && -n "$kx0" && -n "$ky0" ]] || e2e_fail "could not read konsole geometry"
echo "konsole $kid at ${kx0},${ky0} ${kw}x${kh}"

#! slotWindowMove targets the ACTIVE window; a click activates konsole
fp click "$(( kx0 + kw / 2 ))" "$(( ky0 + kh / 2 ))"
sleep 0.6
[[ "$(active_id)" == "$kid" ]] || e2e_fail "konsole did not become the active window (got $(active_id))"

# nudges: 5 x Up = 5 x 8px = 40px upward (y decreases); comfortably on-screen
# from a roughly centred konsole, and large enough to read unambiguously.
NUDGES=5
STEP=8
EXPECT_DY=$(( NUDGES * STEP ))
TOL=2   #! rounding only; the move math is integer

nudge_up() { for _ in $(seq 1 "$NUDGES"); do fp key Up; sleep 0.15; done; }
abs() { local v="$1"; echo "$(( v < 0 ? -v : v ))"; }

# ---- trial 1: COMMIT control (Return). Proves keys reach the move mode and a
#      commit is observable. Without a working key verb this trial fails. -----
base_x="$kx0"; base_y="$ky0"
invoke_window_move; sleep 0.5
nudge_up
read -r _ mx my _ _ <<< "$(konsole_geo | tr '|' ' ')"
moved_dy=$(( base_y - my ))
echo "commit trial: nudged ${base_y} -> ${my} (dy=${moved_dy}, expect ~${EXPECT_DY})"
[[ "$(abs $(( moved_dy - EXPECT_DY )))" -le "$TOL" ]] \
    || e2e_fail "keyboard nudge did not move the window as expected (dy=${moved_dy}, expected ${EXPECT_DY}); the move mode did not start or Up keys were not delivered"
fp key Return; sleep 0.5
read -r _ cx cy _ _ <<< "$(konsole_geo | tr '|' ' ')"
echo "commit trial: after Return at ${cx},${cy}"
[[ "$(abs $(( cy - my )))" -le "$TOL" ]] \
    || e2e_fail "Return did not commit the move (window at ${cy}, expected it to stay near ${my})"
echo "COMMIT observed: Return left the window at the nudged position"

# ---- trial 2: CANCEL (Escape). The window is moved in-flight, then Escape must
#      restore it to THIS trial's pre-move baseline. This is the observed cancel.
base_x="$cx"; base_y="$cy"
invoke_window_move; sleep 0.5
nudge_up
read -r _ ix iy _ _ <<< "$(konsole_geo | tr '|' ' ')"
inflight_dy=$(( base_y - iy ))
echo "cancel trial: in-flight nudged ${base_y} -> ${iy} (dy=${inflight_dy})"
[[ "$(abs $(( inflight_dy - EXPECT_DY )))" -le "$TOL" ]] \
    || e2e_fail "cancel trial: the in-flight nudge did not move the window (dy=${inflight_dy}); nothing to cancel"
fp key Escape; sleep 0.5
read -r _ ex ey _ _ <<< "$(konsole_geo | tr '|' ' ')"
echo "cancel trial: after Escape at ${ex},${ey} (pre-move baseline was ${base_x},${base_y})"
[[ "$(abs $(( ey - base_y )))" -le "$TOL" && "$(abs $(( ex - base_x )))" -le "$TOL" ]] \
    || e2e_fail "Escape did NOT cancel: window at ${ex},${ey}, expected restore to ${base_x},${base_y} (in-flight moved to ${ix},${iy})"

echo "CANCEL observed: Escape aborted the in-flight move and restored ${base_x},${base_y}"
echo "fakepointer key Escape cancels an in-flight drag (move committed on Return, aborted+restored on Escape)"
