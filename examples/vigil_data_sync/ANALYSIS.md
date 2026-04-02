# VigilDataSync — FFI Module Stress Test & Critical Analysis

## 1. Architecture

```
vigil_data_sync/
├── main.vigil              # Entry point, orchestrates all tests
├── vigil.toml              # Project manifest
└── lib/
    ├── ffi_bindings.vigil  # libc + libm FFI wrappers
    ├── ffi_tests.vigil     # Safety tests + performance benchmarks
    └── data_pipeline.vigil # Data processing using stdlib + unsafe
```

The program exercises the FFI module (`ffi` + `unsafe`) through:
- **10 safety/edge-case tests**: null pointers, bounds checking, double-free, use-after-free, errno, sizeof, callbacks, large buffers, peek/poke types, C malloc/free
- **5 performance benchmarks**: FFI sin() calls, buffer alloc/free, string roundtrips, peek/poke, C malloc/free
- **Data pipeline**: JSON generation, byte-level parsing via unsafe, gzip compression round-trip

## 2. Toolchain Test Plan

### Help output (verified):
```
vigil - The VIGIL Scripting Language

Usage:
  vigil <command> [options]

Commands:
  run                  Run a VIGIL script
  check                Type-check a VIGIL script
  new                  Create a new VIGIL project
  debug                Debug a VIGIL script
  doc                  Show documentation for modules, builtins, or source files
  fmt                  Format VIGIL source files
  repl                 Start interactive REPL
  lsp                  Start Language Server Protocol server
  version              Print version information
  embed                Embed files as VIGIL source code
  test                 Run tests
  get                  Manage dependencies
  package              Package a VIGIL program as a standalone binary
```

### Commands against this project:

| Command | Expected Result | FFI Validation |
|---------|----------------|----------------|
| `vigil run examples/vigil_data_sync/main.vigil` | 10/10 safety tests pass, benchmarks print, pipeline completes, exit 0 | Full FFI call chain: open→bind→call→close, unsafe buffer ops, peek/poke, errno |
| `vigil check examples/vigil_data_sync/main.vigil` | **CRASHES (segfault)** with 5+ user modules | Reveals compiler bug #1: module count limit |
| `vigil fmt examples/vigil_data_sync/main.vigil` | Formats the file | No FFI-specific validation |
| `vigil doc ffi` | `'ffi' not found` | **Bug #2**: ffi module has no doc entries |
| `vigil doc unsafe` | `'unsafe' not found` | **Bug #3**: unsafe module has no doc entries |
| `vigil test` | No test files in this project | N/A |
| `vigil package` | Would package with FFI deps | Not tested (requires platform-specific setup) |

## 3. Critical Analysis & Roadmap

### Production Readiness Score: 4/10

The FFI module is a functional prototype that can call simple C functions, but it is far from production-ready for real-world C library integration.

### What Works Well (Strengths)

1. **Managed buffers are safe**: `unsafe.alloc/free/get/set` provides bounds-checked memory with slot invalidation on free. Out-of-bounds and use-after-free are caught at runtime. This is genuinely better than raw C.

2. **Signature-based binding is clever**: `ffi.bind(lib, "sin", "f64(f64)")` is a clean API for simple functions. No header files needed.

3. **Type coverage is adequate for primitives**: i32, i64, u8, u32, f32, f64, ptr, void, string — covers most C function signatures.

4. **peek/poke API is complete**: All primitive types can be read/written at arbitrary offsets. This enables struct field access.

5. **Performance is reasonable**: ~1.7M FFI sin() calls/sec, ~5.9M peek/poke ops/sec. The libffi overhead is acceptable for most use cases.

6. **Callback trampoline pool exists**: 8 callback slots with dispatch function. The mechanism works.

### Critical Weaknesses

#### W1: No struct mapping (BLOCKER)
There is no way to define C struct layouts in Vigil. To access `sqlite3_stmt`, `curl_easy`, `z_stream`, or `cJSON` objects, you must manually calculate byte offsets using `unsafe.sizeof`/`unsafe.offsetof` and `peek/poke`. This is:
- Error-prone (wrong offsets = silent corruption)
- Unreadable (magic numbers everywhere)
- Unmaintainable (changes if struct layout changes)

**Needed**: `extern struct` declarations or a struct layout DSL.

#### W2: No callback-to-Vigil bridge (BLOCKER)
`unsafe.cb_alloc()` returns a C function pointer, but there is **no way from Vigil code to register a Vigil closure as the callback target**. The dispatch function must be set from C via `vigil_ffi_callback_set_dispatch()`. This means:
- curl progress callbacks: impossible from Vigil
- SQLite custom functions: impossible from Vigil
- Any C API requiring callbacks: impossible from Vigil

**Needed**: `ffi.callback(fn_type, vigil_closure) -> i64` that returns a C function pointer.

#### W3: ffi.call is limited to 6 i64 args (SEVERE)
`ffi.call(handle, a0, a1, a2, a3, a4, a5)` takes exactly 7 parameters (handle + 6 args). All args are i64. This means:
- Can't pass more than 6 arguments
- Can't mix i64 and f64 in the same call (must use `call_f` which only takes 2 f64 args)
- Can't pass strings directly (must convert to pointer first)

**Needed**: Variadic `ffi.call` or a builder pattern: `ffi.call(h).arg_i64(x).arg_f64(y).arg_ptr(z).invoke()`

#### W4: No error recovery (SEVERE)
FFI errors (invalid handle, dlopen failure) propagate as fatal runtime errors. There is no `try/catch` or `guard` mechanism to handle them gracefully. This means:
- Can't probe for optional libraries
- Can't handle partial failures
- Can't write robust wrappers

**Needed**: Error return values or integration with Vigil's `err` type.

#### W5: No `extern "C"` declarations (MODERATE)
There is no way to declare C function signatures at the type level. Everything is stringly-typed via signature strings like `"i32(i32,i32)"`. The compiler cannot type-check FFI calls.

**Needed**: `extern fn malloc(i64) -> ptr;` declarations that the compiler can verify.

#### W6: No const/macro/enum support (MODERATE)
C libraries define hundreds of constants (`SQLITE_OK`, `CURLE_OK`, `Z_OK`). There is no way to import or define these. Users must hardcode magic numbers.

**Needed**: `extern const SQLITE_OK: i32 = 0;` or a constant import mechanism.

#### W7: Global mutable state for bindings (MODERATE)
`ffi.bind` uses a global array of 256 slots. This is:
- Not thread-safe
- Limited to 256 bound functions total across all libraries
- No way to unbind or reclaim slots

#### W8: ffi/unsafe modules not documented (MINOR)
`vigil doc ffi` and `vigil doc unsafe` return "not found". These are the most complex modules and need the most documentation.

### Bugs Found During This Exercise

1. **Compiler segfault with 5+ user module imports**: Importing 5 or more `.vigil` files from `lib/` causes a segfault. Workaround: consolidate into ≤4 files.

2. **Compiler segfault with `http.get` multi-return in check mode**: `vigil check` on a file using `i32 s, string b, string h = http.get(url)` segfaults.

3. **f-strings don't support escaped quotes**: `f"sizeof({\"i32\"})"` fails with "unterminated f-string interpolation". Must use variables.

4. **No JSON module**: Despite being a common need, there's no `json` stdlib module.

### Concrete Proposals (Priority Order)

1. **Add `extern struct` declarations** — Allow defining C struct layouts:
   ```vigil
   extern struct z_stream {
       ptr next_in;
       u32 avail_in;
       u64 total_in;
       ptr next_out;
       u32 avail_out;
       u64 total_out;
   }
   ```

2. **Add `ffi.callback(closure) -> i64`** — Bridge Vigil closures to C function pointers:
   ```vigil
   fn my_progress(i64 total, i64 now) -> i32 { return 0; }
   i64 cb_ptr = ffi.callback(my_progress);
   ```

3. **Make `ffi.call` variadic with mixed types** — Or add `ffi.call_mixed`:
   ```vigil
   ffi.call(h, i64(42), 3.14, ptr(buf));  // mixed arg types
   ```

4. **Add error recovery for FFI calls** — Return `(result, err)` tuples:
   ```vigil
   i64 lib, err e = ffi.open("libfoo.so");
   if e { fmt.println("library not found"); }
   ```

5. **Add `extern fn` declarations** — Type-safe FFI at compile time:
   ```vigil
   extern "C" fn sqlite3_open(string path, ptr db) -> i32;
   ```

6. **Add doc entries for ffi and unsafe modules** — These are critical for adoption.

7. **Fix the 5-module import segfault** — This blocks any non-trivial project.

### Summary

The FFI module provides a working foundation for calling simple C functions with primitive arguments. The `unsafe` module's managed buffers with bounds checking are a genuine safety improvement over raw C. However, the lack of struct mapping, callback bridging, and mixed-type variadic calls makes it impossible to bind any real-world C library (libcurl, sqlite3, zlib, cJSON) from pure Vigil code today. The module is suitable for calling leaf functions (sin, malloc, strlen) but not for integrating with complex C APIs that use structs, callbacks, or more than 6 arguments.
