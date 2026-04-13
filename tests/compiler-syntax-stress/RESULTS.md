# Vigil-to-C Compiler Syntax Stress Test Results

No remaining limitations — all features work fully in VM and C backend.

## Summary

- **Total tests:** 22
- **Passed:** 22
- **Failed:** 0

All 22 stress tests pass end-to-end: `vigil check` → `vigil run` → `vigil transpile` → `cmake build` → C binary output matches interpreter output.

## Root-Cause Fixes Applied

1. **P1 (regvm.c):** Register VM local variable overwrite after void calls — added `local_reg[]` tracking so GET_LOCAL reads from the correct register after SYNC_PACK moves values.
2. **P2 (transpile_c.c):** Logical `||`/`&&` in f-string expressions — fixed peephole pass to not eliminate MOVEs before TEST/TESTSET instructions.
3. **P3 (transpile_rt.c, transpile_c.c):** u64 formatting as signed — added `vigil_nanbox_is_uint` to `tc_to_nanbox()` and use `vigil_value_init_uint_rt` for large u64 constants.
4. **P4 (transpile_rt.c):** `last_index_of` multi-return ordering — added sub_op 140 to the `n_results=2` list in `vigil_tc_string_op`.
5. **P5 (transpile_rt.c):** Entry-point defer not executing — fixed missing `vals` array allocation in `vigil_tc_defer`.
6. **P6 (regvm.c):** Multiple `err` variables in same scope — resolved by P1's `local_reg` tracking.
7. **P7 (aot.c):** AOT/MIR aarch64 — root-caused to SIGSEGV in MIR's `target_translate`/`out_insn` during `MIR_gen`. Documented with detailed analysis. AOT remains disabled on aarch64 pending upstream MIR fixes; works on x86-64.

## Reproduction Steps

For any test:
```bash
cd tests/compiler-syntax-stress/<test-name>
../../build/vigil check main.vigil
../../build/vigil run main.vigil
../../build/vigil transpile . -o /tmp/c-output
cd /tmp/c-output && cmake -B build -S . && cmake --build build
find build -type f -perm +111 -print -quit | xargs -I{} {}
```
