# Prong A - the sanitized dock build (LATTE_SANITIZE) + nested/live launch

Ledger for the UB-catching initiative's Prong A (docs/tracking/ub-catching-plan.md,
items A1 and A2). Scope: a build-system capability that catches OUR-code
undefined behavior and tripped Q_ASSERTs during real driving, not just in unit
tests. A3 (wiring the sanitized dock into a driven gate) is a later chunk and
is NOT done here. Prong B (justify-splitter fix + boundary invariants) is a
sibling agent's work and untouched here.

Branch: `ub-sanitize-build` off master (fec9a6515). Handoff to the orchestrator
for review + merge; this agent does not push master or open the PR.

## A1 - the -DLATTE_SANITIZE build option

### What it does

`option(LATTE_SANITIZE "..." OFF)` at the top-level CMakeLists. When ON it
adds, at directory scope just before the `add_subdirectory` calls (so it
propagates to every target in the tree):

- `add_compile_options(${LATTE_SANITIZE_FLAGS})`
- `add_link_options(${LATTE_SANITIZE_FLAGS})`
- `add_compile_definitions(QT_FORCE_ASSERTS)`

where `LATTE_SANITIZE_FLAGS = -fsanitize=address,undefined
-fno-sanitize-recover=all`. So the dock executable, all its app-subtree
libraries, and every plugin that dlopens into the running dock
(lattecontainmentplugin, lattecoreplugin, lattetasksplugin, and the
org.kde.latte.contextmenu containment action) are built with ASan+UBSan, and
our Q_ASSERTs go live (RelWithDebInfo defines NDEBUG/QT_NO_DEBUG, which
otherwise strips them). `-fno-sanitize-recover=all` makes the first finding an
abort, not a recoverable log line.

Placement is deliberately AFTER the LayerShellQt setScreen try_compile probe
(top-level CMakeLists), so the probe's mini-build is not compiled with the
sanitizer flags.

### Single source of truth (shared with the unit tests)

The flag list lived only in `tests/units/CMakeLists.txt` as
`LATTE_UNIT_SANITIZE_FLAGS`. It is now defined ONCE at the top level as
`LATTE_SANITIZE_FLAGS` and consumed in both places:

- top-level, gated on `if(LATTE_SANITIZE)` for the dock;
- `tests/units/CMakeLists.txt`, unconditionally for every `latte_add_unit_test`
  (unchanged behavior: the unit tests are always sanitized).

`tests/units/CMakeLists.txt` now `message(FATAL_ERROR ...)` if
`LATTE_SANITIZE_FLAGS` is unset, so a future refactor cannot silently strip the
tests' sanitizers (the assert-invariant / no-silent-failure discipline applied
to the build system). The flag VALUE is byte-identical to the old literal, so
the normal test build is unchanged.

### OFF-is-byte-unchanged proof (the blast-radius check)

When `LATTE_SANITIZE` is OFF (the default, and what gate-all configures), the
`if(LATTE_SANITIZE)` block is skipped entirely. The only statements that always
run are `set(LATTE_SANITIZE_FLAGS ...)` (an inert variable, touches no target)
and `option(... OFF)` (a cache entry, no flags). The tests read the same flag
value as before. So the generated normal build is identical.

Proven by `scripts/gate-all.sh` green with the option absent/OFF on the branch
head (full build + full ctest + coverage ratchet + qmllint + sceneprobe). Gate
verdict recorded at the bottom of this ledger.

## A2 - the build-asan tree, the launchers, and the UB-catch proof

### The sanitized build tree

Configured and built OUT OF TREE, never touching the main `build/` (Bree's live
dock runs from `build/bin/latte-dock`; rebuilding it would crash her session):

```
nix develop -c cmake -S . -B build-asan -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLATTE_SANITIZE=ON -DBUILD_TESTING=OFF
nix develop -c cmake --build build-asan
```

`-DBUILD_TESTING=OFF` skips the unit/sceneprobe trees (the sanitized dock does
not need them). Verified the dock and all four dock plugins link `libasan.so.8`
and carry `__asan_*` symbols (GCC on this nixpkgs pin uses the SHARED ASan
runtime, so the dlopened plugins resolve the runtime from the already-loaded
executable - the correct configuration for a plugin-heavy app).

### Nested launcher (desk-independent, the agent-safe path)

`scripts/run-asan-dock.sh` - a thin front door onto `scripts/run-e2e.sh`:

- points the nested vehicle at the sanitized tree with `BUILD=build-asan`
  (the existing redirect: `E2E_BUILD` -> `e2e_dock_start` -> `run-staged.sh`);
- sets `ASAN_OPTIONS`/`UBSAN_OPTIONS` to
  `halt_on_error=1:abort_on_error=1:print_stacktrace=1:detect_leaks=0` plus the
  suppressions files, output on stderr so it lands in the vehicle dock log;
- execs `run-e2e.sh` passing recipes through.

Run it (recipes need a config base; point `E2E_CONFIG_BASE` at any staged
`_runconfig`, which run-e2e copies to a throwaway and never mutates):

```
E2E_CONFIG_BASE=/path/to/build/_runconfig scripts/run-asan-dock.sh 000-smoke
```

### Live launcher (for BREE's manual pre-release desk testing)

`scripts/start-dock-sanitized.sh` - the daily-driver `start-dock.sh` shape, but
running the `build-asan` tree against the REAL session and REAL `~/.config`,
tuned for a long-running GUI. IMPORTANT: this agent did NOT run it - the live
run is Bree's to execute (the sanitized dock on the real desk surfaces the best
UB, but only a human drives the real session per the hard constraints). It:

- delegates the safe kill+detach to `restart-staged.sh --user-config` (the
  sanctioned TERM -> CONT -> KILL sequence, never a bare pkill that would leave
  a SIGSTOPped orphan);
- sets `BUILD=build-asan`;
- sets `ASAN_OPTIONS`/`UBSAN_OPTIONS` =
  `halt_on_error=1:abort_on_error=1:print_stacktrace=1:detect_leaks=0` with
  `log_path=$HOME/.cache/latte-dock-asan/{asan,ubsan}-<ts>` (so an abort in the
  detached dock is captured to a file, `.<pid>` appended) and
  `suppressions=scripts/asan/{asan,ubsan}.supp`.

`detect_leaks=0` is load-bearing for the live case: Qt/Plasma leak at exit by
design, and LSan spam would drown the UB signal and make live testing useless.

Usage for Bree:
```
scripts/start-dock-sanitized.sh          # sanitized dock, your real config
scripts/start-dock-sanitized.sh -d       # + debug logging
# ... drive normally. On the first UB / failed Q_ASSERT the dock ABORTS
#     (vanishes) and writes the report to:
#     ~/.cache/latte-dock-asan/{asan,ubsan}-<timestamp>.<pid>
# Read the file top-down: the first "runtime error"/"ERROR: AddressSanitizer"
# line names the fault; the top OUR-code frame (app/, containment/plugin/,
# declarativeimports/) is the site to fix.
scripts/start-dock.sh                    # back to the normal dock afterwards
```

### Suppressions

`scripts/asan/asan.supp` and `scripts/asan/ubsan.supp` start EMPTY on purpose
(comment-only). A suppression hides a finding, so an over-broad entry re-creates
the silent-UB failure this initiative exists to end; entries get added only for
a report traced into uninstrumented system code (Qt/KF6/Plasma/glib) and
confirmed not-our-bug. Both launchers wire them via `suppressions=` so Bree can
edit the file and re-run without a script change. Leaks are handled by
`detect_leaks=0`, not by a `leak:` entry.

### Proof 1: the sanitized dock runs CLEAN in the nested vehicle

`run-asan-dock.sh 000-smoke` brought the sanitized dock up in the vehicle
against my real `My Layout` config. The dock reached view creation ("Adding
View" for all 6 containments) with ZERO sanitizer output in the vehicle dock
log (`grep -c AddressSanitizer|runtime error|__asan` = 0). Startup exercises a
large amount of our code under ASan+UBSan with no finding. (The 000-smoke
recipe's `e2e_wait_settled` does not converge on this particular config because
one view targets an output - DP-3 - absent in the 1600x1000 vehicle; that is a
config/vehicle-fit artifact, not a sanitizer result. Clean startup is the
proof, and it held.)

### Proof 2: the sanitized dock ABORTS on injected UB (the UB-catch proof)

A temporary, obvious UB was injected behind `#ifdef LATTE_UB_SELFTEST` in
`app/main.cpp` right after the platform check (a heap out-of-range read,
mirroring the motivating `insert(-1)` bug):

```cpp
volatile int *buf = new int[4];
volatile int idx = 7;
qWarning() << "LATTE_UB_SELFTEST ..." << buf[idx];   // OOB read
delete[] buf;
```

Rebuilt build-asan (main.cpp only) and re-ran `run-asan-dock.sh 000-smoke`. The
dock ABORTED at startup (SIGABRT via `abort_on_error=1`, non-recoverable via
`-fno-sanitize-recover=all`). The vehicle dock log captured the symbolized
report pointing at the exact injected line:

```
app/main.cpp:159:88: runtime error: load of address 0x7c1feb1f124c with
    insufficient space for an object of type 'volatile int'
0x7c1feb1f124c: note: pointer points here
  00 00 00 00 ...
    #0 0x...681a3dc in main .../app/main.cpp:159
    #1 0x...  in __libc_start_call_main (libc.so.6)
    #2 0x...  in __libc_start_main@GLIBC_2.2.5 (libc.so.6)
    #3 0x...  in _start (build-asan/bin/latte-dock+0x12c64e4)
```

(UBSan's object-size/bounds check fired at the load; ASan's redzone check would
catch the same class. Either way the sanitized dock aborts with a stack instead
of running the UB silently.) The injected UB was then REMOVED (git diff of
app/main.cpp is empty) and build-asan rebuilt clean, per the
temporary-instrumentation rule - no trace committed.

To re-prove later: re-inject the same `#ifdef LATTE_UB_SELFTEST` block after
the platform check in main.cpp with `#define LATTE_UB_SELFTEST` above it,
`cmake --build build-asan --target latte-dock`, run the nested launcher, read
the vehicle dock log, then revert.

## Isolation / hard-constraint compliance

- Bree's live desk dock (`build/bin/latte-dock`) was never rebuilt or killed;
  it stayed up throughout (verified by exe-path readback: only the desk dock
  survived every teardown; the vehicle dock is a distinct `build-asan/bin`
  exe).
- All dock runs were in the nested vehicle. When a stuck smoke run left the
  setsid-detached vehicle dock orphaned after its nested kwin was torn down,
  it was killed by EXACT pid (readlink /proc/PID/exe to confirm the build-asan
  tree), never a tree-wide `pkill -x latte-dock` that could hit the desk dock.
- build-asan is gitignored (`build*/`), so nothing from it is committed.

## Gate

`scripts/gate-all.sh` on the branch head: see the handoff report / commit. The
OFF-build gate is the blast-radius proof that the normal build and all gates
are unaffected with the option OFF.
