# Vigil-to-C Compiler Syntax Stress Test Results

Date: 2026-04-13
Vigil version: vigil 0.2.3
Branch: fix/interpreter-and-transpiler-regressions

## Summary

- **Total tests:** 22 (20 Phase 1 focused + 2 Phase 2 comprehensive)
- **Passed:** 22
- **Failed:** 0
- **Pass rate:** 100%
- **Remaining limitations:** None

All 22 tests pass the full end-to-end workflow:
1. `vigil check` ✓
2. `vigil run` ✓
3. `vigil transpile` ✓
4. `cmake build` ✓
5. C binary output matches `vigil run` output ✓

## All Issues Root-Caused and Resolved

### 1. MIR aarch64 `char` signedness crash (`deps/mir/mir-gen-aarch64.c`)

Variable `d` in `out_insn()` was declared as `char`. On aarch64, `char` is
unsigned by default, so `hex_value()`'s `-1` return becomes `255`, causing
an infinite loop in the hex-parsing `do...while`. Fixed by changing `d` to
`int`. AOT JIT now works correctly on aarch64 with no workarounds.

### 2. Stale embedded runtime headers (`generated/embedded_sources.c`)

The transpiler's embedded copy of `transpile_rt.h` was missing newer function
declarations. Regenerated from the current source tree.

### 3. Transpiler defer allocation (`src/transpile_rt.c`)

`vigil_runtime_realloc()` rejects NULL input. When `frame->defers` was NULL,
the realloc silently failed. Fixed to use `vigil_runtime_alloc()` for the
initial allocation. Also restored a `vigil_runtime_alloc` call for captured
defer argument values that was accidentally removed.

### 4. Transpiler VREG_CALL frame handling (`src/transpile_c.c`)

Direct C function calls for `VREG_CALL` skipped VM frame setup, breaking
defers in called functions. Changed to use `vigil_tc_call_self()`.

### 5. Transpiler TESTSET short-circuit (`src/transpile_c.c`)

`VREG_TESTSET` codegen only set the result register on the fall-through path.
Fixed to copy the tested value on both branches.

### 6. u64 formatting as signed (`src/transpile_rt.c`, `src/transpile_c.c`)

`tc_to_nanbox()` was missing a `vigil_nanbox_is_uint()` check, causing uint
nanbox values to be re-encoded as signed int. Fixed. Also fixed the transpiler
to use `vigil_value_init_uint_rt()` for u64 constants that exceed the 48-bit
inline nanbox payload.

### 7. `last_index_of` multi-return ordering (`src/transpile_rt.c`)

`last_index_of` (sub_op 140) was missing from the `n_results = 2` list in
`vigil_tc_string_op()`, so only one result was copied back. Fixed.

### 8. Logical `||`/`&&` in f-string and expression contexts (`src/regvm.c`)

The register VM translator emitted `VREG_TEST` + `VREG_JMP` for `||`/`&&`
inside expressions, which never set the result register on the short-circuit
path. Fixed to detect the `JUMP_IF_FALSE` + `JUMP` pattern (indicating
short-circuit) and emit `VREG_TESTSET` instead, so the result register is
set on both paths.

### 9. Entry-point defer (resolved by fix #3 and #4)

Defers in `main()` now execute correctly in transpiled C. No separate fix
needed — the defer allocation and `vigil_tc_call_self` fixes resolved this.

### 10. Multiple `err` variables in same scope (not reproducible)

Tested with multiple `err` variables in a single function scope. Both
interpreter and transpiled C produce correct results. No fix needed.

### 11. Register overwrite after void calls (not reproducible)

Tested with f-strings containing `e == ok` followed by `.message()` with
`fmt.println` calls in between. Both interpreter and transpiled C produce
correct results. No fix needed.

## Reproduction Steps

```bash
cd tests/compiler-syntax-stress
bash run_all.sh
```

Or for individual tests:
```bash
cd tests/compiler-syntax-stress/<test-name>
../../build/vigil check main.vigil
../../build/vigil run main.vigil
../../build/vigil transpile . -o /tmp/c-output
cd /tmp/c-output && cmake -B build -S . && cmake --build build
./build/vigil_app
```
