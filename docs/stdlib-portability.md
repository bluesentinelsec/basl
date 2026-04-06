# Standard Library Portability Guide

This document describes the portability contract VIGIL is working toward and the build knobs that enforce it today.

The main goal is straightforward:

1. `vigil_core` must stay pure C11.
2. Platform-sensitive stdlib modules must be optional at build time.
3. A reduced build must still compile cleanly and fail in VIGIL with clear diagnostics when unavailable modules are imported.

## Current Status

VIGIL now has two build profiles that matter for portability work:

- Full desktop builds: all platform-sensitive stdlib modules enabled and tested on Linux, macOS, and Windows.
- Reduced portability builds: platform-sensitive stdlib modules disabled so the codebase can still build cleanly on constrained or unusual targets.

The implementation is not fully at the ideal end state yet. The most important remaining gaps are:

- The portability checks are still heuristic. They enforce layering for the current stdlib surface, but they do not prove that every helper path uses the ideal allocator or ownership pattern.
- Emscripten now builds against `src/platform/platform_posix.c` with `__EMSCRIPTEN__` guards disabling unsupported features (terminal control, process spawning, dlopen). Android and iOS also use `platform_posix.c` directly.

Those constraints should be treated as the current contract, not as reasons to drift back toward platform-specific stdlib code.

## Principles

1. The core language implementation must compile with only ISO C11 headers.
2. Platform-sensitive stdlib modules must be individually switchable from CMake.
3. Importing a known-but-disabled stdlib module must produce a VIGIL diagnostic such as:

```text
error: stdlib module 'net' is not available in this build
```

4. Desktop CI must exercise both:
   - all optional modules enabled
   - all optional modules disabled
5. New platform behavior belongs in `src/platform/` unless there is a strong reason otherwise.

## Module Groups

### Always available

These modules are built in every configuration today:

| Module | Notes |
|--------|-------|
| `args` | Pure runtime/module logic |
| `atomic` | Uses C11 atomics |
| `compress` | Vendored compression backends |
| `crypto` | Vendored crypto backend |
| `csv` | Pure parsing/formatting logic |
| `fmt` | Printing/formatting |
| `log` | Runtime logging helpers |
| `math` | `<math.h>` and numeric helpers |
| `parse` | String-to-number parsing |
| `random` | Pseudorandom helpers |
| `regex` | Vendored regex engine |
| `test` | Test support module |
| `unsafe` | Raw memory primitives |
| `url` | URL parsing/formatting |
| `yaml` | YAML parsing/formatting |

### Optional, platform-sensitive modules

These modules are build-time toggles. They default `ON` for desktop builds and `OFF` for Emscripten, iOS, Android, and unknown platforms.

| Module | CMake option | Why it is optional |
|--------|--------------|--------------------|
| `ffi` | `VIGIL_STDLIB_FFI` | Dynamic loading / foreign-call support |
| `fs` | `VIGIL_STDLIB_FS` | Filesystem and host path access |
| `http` | `VIGIL_STDLIB_HTTP` | Host networking and HTTP client/server support |
| `net` | `VIGIL_STDLIB_NET` | Raw sockets |
| `readline` | `VIGIL_STDLIB_READLINE` | Interactive terminal editing/history |
| `thread` | `VIGIL_STDLIB_THREAD` | Host threading primitives |
| `time` | `VIGIL_STDLIB_TIME` | Host timers / wall-clock helpers |

## Build Options

```cmake
option(VIGIL_STDLIB_FFI "Build ffi stdlib module" ON)
option(VIGIL_STDLIB_FS "Build fs stdlib module" ON)
option(VIGIL_STDLIB_HTTP "Build http stdlib module" ON)
option(VIGIL_STDLIB_NET "Build net stdlib module" ON)
option(VIGIL_STDLIB_READLINE "Build readline stdlib module" ON)
option(VIGIL_STDLIB_THREAD "Build thread stdlib module" ON)
option(VIGIL_STDLIB_TIME "Build time stdlib module" ON)
```

On non-desktop targets, the default for those options is `OFF`.

Minimal portability build:

```bash
cmake -S . -B build \
  -DVIGIL_STDLIB_FFI=OFF \
  -DVIGIL_STDLIB_FS=OFF \
  -DVIGIL_STDLIB_HTTP=OFF \
  -DVIGIL_STDLIB_NET=OFF \
  -DVIGIL_STDLIB_READLINE=OFF \
  -DVIGIL_STDLIB_THREAD=OFF \
  -DVIGIL_STDLIB_TIME=OFF
```

Full desktop build:

```bash
cmake -S . -B build \
  -DVIGIL_STDLIB_FFI=ON \
  -DVIGIL_STDLIB_FS=ON \
  -DVIGIL_STDLIB_HTTP=ON \
  -DVIGIL_STDLIB_NET=ON \
  -DVIGIL_STDLIB_READLINE=ON \
  -DVIGIL_STDLIB_THREAD=ON \
  -DVIGIL_STDLIB_TIME=ON
```

## Import Resolver Rules

The stdlib import helpers now distinguish between:

- known stdlib modules
- stdlib modules available in the current build

That matters in two places:

1. The CLI import scanner skips recursive source resolution for any known stdlib module, even if it is disabled.
2. The compiler emits a specific “not available in this build” diagnostic for known-but-disabled stdlib modules.

This prevents portability builds from failing with misleading errors such as `imported source is not registered`.

## Platform Layer Expectations

The intended layering remains:

- `src/platform/platform_posix.c`
- `src/platform/platform_win32.c`
- `src/platform/platform_stub.c`

Stdlib modules should prefer `platform/platform.h` instead of direct platform headers.

Current exceptions that still need cleanup:

- None in the current stdlib tree. Direct platform headers in `src/stdlib/` are now treated as portability violations.

If you add new host-sensitive behavior, prefer moving the platform split downward into `src/platform/` rather than adding more direct `#ifdef _WIN32` logic in the stdlib layer.

## Testing And CI

CI now covers two important desktop configurations on Linux, macOS, and Windows:

1. Full-feature native build and test run with all optional modules enabled.
2. Reduced native build and test run with all optional modules disabled.

The full-feature matrix is also used for the Python integration suite so the opt-in modules are exercised in actual CLI execution, not just at link time.

In reduced builds, module-specific integration tests are skipped automatically, and a dedicated availability test verifies that disabled stdlib imports fail with the expected diagnostic.

The portability CI job now runs two static checks:

1. `scripts/check_core_portability.py` keeps `vigil_core` restricted to ISO C11 headers.
2. `scripts/check_stdlib_portability.py` ensures the stdlib layer does not pull platform headers directly and flags raw heap allocation in the portability-sensitive stdlib modules that are expected to use runtime/platform allocation paths instead.

Emscripten builds use the stub platform backend and currently serve as build validation, not feature-parity execution coverage.

## Guidance For New Modules

When adding a stdlib module:

1. Decide whether it is always-available or platform-sensitive.
2. If it is platform-sensitive, add a CMake option and compile definition.
3. Register it conditionally in `include/vigil/stdlib.h`.
4. Ensure the compiler reports a language-level error when the module is imported but disabled.
5. Gate module-specific tests using the same option.
6. Update this document if the portability surface changes.

## Emscripten `fs` Module — Virtual Filesystem Caveat

The `fs` module on Emscripten operates on Emscripten's virtual filesystem (MEMFS by default), not the host OS filesystem. Key implications:

- Files written via `fs.write` exist only in memory and are **lost when the WASM module exits**.
- To persist files in the browser, mount IDBFS (`FS.mount(IDBFS, {}, '/persist')` from JavaScript).
- To access real files in Node.js, mount NODEFS (`FS.mount(NODEFS, { root: '.' }, '/local')`).
- `fs.symlink` and `fs.readlink` return `VIGIL_STATUS_UNSUPPORTED` on Emscripten (MEMFS does not support symlinks).

Vigil scripts using `fs` on web should be aware that the filesystem is ephemeral unless explicitly configured otherwise from the JavaScript host.
