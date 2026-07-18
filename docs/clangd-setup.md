# Editor code intelligence (clangd)

Goal: open this repo in an editor with the clangd language server and have
it resolve every Qt6/KF6/Plasma and project header correctly, with zero
false "file not found" or "use of undeclared identifier" noise. The tree
builds green; clangd should agree.

## TL;DR

```bash
nix develop                 # gives you cmake, ninja AND clangd (clang-tools)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
code .                      # launch VSCode FROM this shell, so clangd and
                            # the nix g++ wrapper are both on PATH
```

Install the recommended extension when VSCode prompts
(`llvm-vs-code-extensions.vscode-clangd`), reload once, and clangd resolves
everything. The `build/compile_commands.json` is emitted automatically by
the configure step, and `.vscode/settings.json` + repo-root `.clangd` are
already committed with the right arguments.

The one thing that matters: **launch the editor from inside `nix develop`.**
The query-driver step below reads the nix environment, so the editor process
needs it.

## Why the extra configuration is needed (first principles)

clangd parses each translation unit with its own clang frontend. To parse a
file the way the real build does, it needs the exact include paths, defines
and C++ standard the build used. Three pieces deliver that here:

1. **`build/compile_commands.json`** - CMake writes this when
   `CMAKE_EXPORT_COMPILE_COMMANDS` is `ON` (set unconditionally in the
   top-level `CMakeLists.txt`). It records the per-file compile command:
   the `-D` defines, the `-std=gnu++20`, and every `-I`/`-isystem` for the
   project's own dirs and for Qt6/KF6/Plasma. Without this file clangd
   parses each file standalone and every `#include <QHash>` / `<QObject>`
   is "file not found", cascading into hundreds of bogus
   undeclared-identifier errors. Setting the flag does not change
   compilation at all - it only emits the JSON as a side artifact, so
   binaries stay byte-identical.

2. **repo-root `.clangd`** - `CompileFlags.CompilationDatabase: build`
   tells clangd to read the database from `build/`. The path is relative
   to the repo root, so it stays correct wherever the tree is checked out
   and survives `build/` being wiped and reconfigured. This is the robust,
   conventional discovery mechanism (no fragile root symlink, and
   `/compile_commands.json` at the root is gitignored anyway).

3. **`--query-driver`** - the crux, and the nix-specific part. The
   recorded command has `-isystem .../include/QtCore`, but the nix GCC
   wrapper injects the **parent** Qt/KF6 umbrella include dirs (the ones
   that make `#include <QtCore/qhash.h>` resolve) plus the libstdc++ and
   compiler-builtin paths through the `NIX_CFLAGS_COMPILE` environment
   variable at build time. Those injected paths are never written into
   `compile_commands.json`, so clangd cannot see them from the database
   alone. `--query-driver=<glob>` lets clangd **run** the recorded g++
   wrapper (`g++ -E -v ...`) and read the real search paths out of it.
   Because the wrapper reads `NIX_CFLAGS_COMPILE` from its environment, the
   editor must be launched from inside `nix develop` for the Qt/KF6 umbrella
   paths to come through.

   The glob is `/nix/store/*/bin/*gcc,/nix/store/*/bin/*g++`. It is
   deliberately hash- and version-tolerant: clangd's glob matches `*`
   across `/`, so it keeps matching after any `nix flake update` rewrites
   the `/nix/store/<hash>-gcc-wrapper-<version>` path. Never bake a
   concrete store hash here - it would go stale on the next toolchain bump.
   Both `g++` (C++ TUs) and `gcc` (C TUs) drivers appear in the database.

## Editors other than VSCode

`.clangd` (discovery) is editor-agnostic and already committed. The only
per-editor piece is passing clangd the two command-line arguments that have
no `.clangd` config equivalent:

```
--compile-commands-dir=build
--query-driver=/nix/store/*/bin/*gcc,/nix/store/*/bin/*g++
```

- **Neovim / any LSP client**: set these in the clangd `cmd`, e.g.
  `cmd = { "clangd", "--compile-commands-dir=build", "--query-driver=/nix/store/*/bin/*gcc,/nix/store/*/bin/*g++" }`,
  and start the editor from `nix develop`.
- **Kate / KDevelop**: add the same two arguments to the clangd invocation
  in the LSP settings.

## Verifying it headlessly

You do not need an editor to confirm clangd resolves the tree. `clangd
--check` parses one file through the compilation database and prints its
diagnostics, so it reproduces exactly what the editor would show:

```bash
nix develop -c clangd \
  --compile-commands-dir=build \
  --query-driver='/nix/store/*/bin/*gcc,/nix/store/*/bin/*g++' \
  --check=containment/plugin/layoutmanager.cpp 2>&1 | grep -E '^E\['
```

A correct setup prints **no** `[pp_file_not_found]`, `[undeclared_var_use]`,
`[unknown_typename]` or `[no_template]` lines. (`clangd --check` also prints
a trailing "N errors" summary that counts its refactoring-availability
probes such as `tweak: ExtractFunction ==> FAIL: ...`; those are not
compiler diagnostics - only the bracketed `E[...] [category]` lines are.)

Baseline for comparison, captured on this tree with no database present
(what a contributor sees before this setup), versus after:

| file | before (no DB) | after (DB + query-driver) |
| --- | --- | --- |
| `containment/plugin/layoutmanager.cpp` | 21 real diagnostics (`QHash` not found -> cascade) | 0 |
| `app/view/effects.cpp` | 21 real diagnostics (`QObject` not found -> cascade) | 0 |
| `containment/plugin/units/justifysplitters.h` | 5 real diagnostics (`QList` not found) | 0 |
```
