# Vigil-to-C Compiler Syntax Stress Test Results

Date: 2026-04-12
Vigil version: vigil 0.2.3

## Summary

- **Total tests:** 22 (20 Phase 1 focused + 2 Phase 2 comprehensive)
- **Passed:** 0
- **Failed:** 22
- **Pass rate:** 0%

### Failure Breakdown

| Stage | Count | Tests |
|-------|-------|-------|
| `vigil run` segfault | 3 | t08-functions, t20-function-types, large-comprehensive-one |
| `vigil run` runtime error | 1 | t01-primitives (partial output before crash) |
| C build fails (missing symbols) | 18 | All remaining tests |

**Every single transpiled C project fails to compile.** The generated `vigil_generated.c` references functions and struct members that do not exist in the bundled `vigil_tc` runtime.

## Results Table

| # | Test | vigil check | vigil run | transpile | C build | Output match |
|---|------|-------------|-----------|-----------|---------|--------------|
| 1 | t01-primitives | ✓ | ✗ runtime error | ✓ | ✗ | N/A |
| 2 | t02-numeric-literals | ✓ | ✓ | ✓ | ✗ | N/A |
| 3 | t03-string-literals | ✓ | ✓ | ✓ | ✗ | N/A |
| 4 | t04-arrays-maps | ✓ | ✓ | ✓ | ✗ | N/A |
| 5 | t05-variables-constants | ✓ | ✓ | ✓ | ✗ | N/A |
| 6 | t06-operators | ✓ | ✓ | ✓ | ✗ | N/A |
| 7 | t07-type-conversions | ✓ | ✓ | ✓ | ✗ | N/A |
| 8 | t08-functions | ✓ | ✗ segfault | N/A | N/A | N/A |
| 9 | t09-control-flow | ✓ | ✓ | ✓ | ✗ | N/A |
| 10 | t10-for-in | ✓ | ✓ | ✓ | ✗ | N/A |
| 11 | t11-switch-break-continue | ✓ | ✓ | ✓ | ✗ | N/A |
| 12 | t12-defer-guard | ✓ | ✓ | ✓ | ✗ | N/A |
| 13 | t13-error-handling | ✓ | ✓ | ✓ | ✗ | N/A |
| 14 | t14-structs | ✓ | ✓ | ✓ | ✗ | N/A |
| 15 | t15-classes | ✓ | ✓ | ✓ | ✗ | N/A |
| 16 | t16-interfaces | ✓ | ✓ | ✓ | ✗ | N/A |
| 17 | t17-enums | ✓ | ✓ | ✓ | ✗ | N/A |
| 18 | t18-string-methods | ✓ | ✓ | ✓ | ✗ | N/A |
| 19 | t19-array-map-methods | ✓ | ✓ | ✓ | ✗ | N/A |
| 20 | t20-function-types | ✓ | ✗ segfault | N/A | N/A | N/A |
| 21 | large-comprehensive-one | ✓ | ✗ segfault | N/A | N/A | N/A |
| 22 | large-comprehensive-two | ✓ | ✓ | ✓ | ✗ | N/A |

## Bug #1 (Critical): Generated C code references missing runtime functions

**Every** transpiled C project fails to compile because `vigil_generated.c` calls functions that don't exist in the transpiler runtime (`vigil_rt`).

### Missing functions (implicit declaration errors)

| Missing function | Suggested by compiler | Occurrences |
|---|---|---|
| `vigil_tc_move_reg` | `vigil_tc_defer` | 15+ tests |
| `vigil_tc_generic_add` | (none) | 8+ tests |
| `vigil_tc_values_equal` | `vigil_vm_values_equal` | 6+ tests |
| `vigil_tc_is_truthy` | (none) | 6+ tests |
| `vigil_tc_string_op` | `vigil_tc_vm_op` | 2+ tests |
| `vigil_tc_negate` | `vigil_tc_defer` | 2+ tests |
| `vigil_tc_to_i32_value` | `vigil_tc_call_value` | 2+ tests |
| `vigil_tc_values_lt` | (none) | 1+ tests |

### Missing struct members

| Missing member | Struct | Occurrences |
|---|---|---|
| `ret_count` | `vigil_tc_t` (`struct vigil_tc`) | Every test |
| `ret_buf` | `vigil_tc_t` (`struct vigil_tc`) | Every test with multi-return |

### Reproduction

```bash
cd tests/compiler-syntax-stress/t02-numeric-literals
../../build/vigil transpile . -o /tmp/c-t02
cd /tmp/c-t02
cmake -B build -S .
cmake --build build  # FAILS with missing symbol errors
```

## Bug #2 (Critical): `vigil run` segfaults on lambda/closure and function-type programs

Three test programs cause `vigil run` to segfault (SIGSEGV):

- **t08-functions**: Uses anonymous functions (`|x| x * 10`) and multi-statement lambdas
- **t20-function-types**: Uses `fn(i32, i32) -> i32` typed variables, indirect calls, and `array<fn(...)>`
- **large-comprehensive-one**: Combines lambdas, closures, local functions with classes/interfaces

### Reproduction

```bash
cd tests/compiler-syntax-stress/t08-functions
../../build/vigil check main.vigil   # passes
../../build/vigil run main.vigil     # SIGSEGV
```

```bash
cd tests/compiler-syntax-stress/t20-function-types
../../build/vigil check main.vigil   # passes
../../build/vigil run main.vigil     # SIGSEGV
```

## Bug #3 (Minor): `vigil run` runtime error on err value access

t01-primitives crashes at runtime with:
```
execution failed: main.vigil:4:17: error message access requires an err value
```

The program constructs `err e2 = err("test error", err.arg)` and then calls `e2.message()`. The checker accepts this but the VM rejects it at runtime.

### Reproduction

```bash
cd tests/compiler-syntax-stress/t01-primitives
../../build/vigil run main.vigil
# Output stops after "err ok: true" with runtime error
```

## Bug #4 (Minor): `vigil check` crashes (double free) on `i32(enum_value)` cast

When attempting to cast an enum value to i32 with `i32(c)` where `c` is an enum, `vigil check` crashes with:
```
free(): double free detected in tcache 2
```

This was discovered during test development and worked around by using helper functions instead.

## Priority Assessment

1. **P0 — Missing transpiler runtime functions**: Blocks ALL transpiled programs from compiling. The code generator emits calls to `vigil_tc_move_reg`, `vigil_tc_generic_add`, `vigil_tc_values_equal`, etc. but these are not defined in the runtime library. The `vigil_tc_t` struct also lacks `ret_count` and `ret_buf` members needed for multi-return value passing.

2. **P1 — VM segfaults on lambdas/function types**: Programs using anonymous functions, closures, or function-type variables pass `vigil check` but segfault during `vigil run`. This affects core language features.

3. **P2 — Runtime err value access error**: The VM rejects `.message()` calls on constructed `err` values despite the checker accepting them.

4. **P2 — Checker double-free on enum-to-i32 cast**: `vigil check` crashes when casting enum values to integers.

## Reproduction Steps (General)

For any test:
```bash
cd tests/compiler-syntax-stress/<test-name>

# 1. Type-check
../../build/vigil check main.vigil

# 2. Run with Vigil VM
../../build/vigil run main.vigil

# 3. Transpile to C
../../build/vigil transpile . -o /tmp/c-output

# 4. Build the C project
cd /tmp/c-output
cmake -B build -S .
cmake --build build

# 5. Run the C binary
find build -type f -executable ! -path '*/CMakeFiles/*' -print -quit | xargs -I{} {}
```
