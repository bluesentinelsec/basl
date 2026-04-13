# Vigil-to-C Compiler Syntax Stress Test Results

Date: 2026-04-13T13:45:03-04:00

Vigil version: vigil 0.2.3

## Summary

- **Total tests:** 22
- **Passed:** 22
- **Failed:** 0

## Results Table

| # | Test | Result |
|---|------|--------|
| 1 | t01-primitives | ✓ PASS |
| 2 | t02-numeric-literals | ✓ PASS |
| 3 | t03-string-literals | ✓ PASS |
| 4 | t04-arrays-maps | ✓ PASS |
| 5 | t05-variables-constants | ✓ PASS |
| 6 | t06-operators | ✓ PASS |
| 7 | t07-type-conversions | ✓ PASS |
| 8 | t08-functions | ✓ PASS |
| 9 | t09-control-flow | ✓ PASS |
| 10 | t10-for-in | ✓ PASS |
| 11 | t11-switch-break-continue | ✓ PASS |
| 12 | t12-defer-guard | ✓ PASS |
| 13 | t13-error-handling | ✓ PASS |
| 14 | t14-structs | ✓ PASS |
| 15 | t15-classes | ✓ PASS |
| 16 | t16-interfaces | ✓ PASS |
| 17 | t17-enums | ✓ PASS |
| 18 | t18-string-methods | ✓ PASS |
| 19 | t19-array-map-methods | ✓ PASS |
| 20 | t20-function-types | ✓ PASS |
| 21 | large-comprehensive-one | ✓ PASS |
| 22 | large-comprehensive-two | ✓ PASS |

## Reproduction Steps (General)

For any test:
```bash
cd tests/compiler-syntax-stress/<test-name>
../../build/vigil check main.vigil
../../build/vigil run main.vigil
../../build/vigil transpile . -o /tmp/c-output
cd /tmp/c-output && cmake -B build -S . && cmake --build build
find build -type f -executable -print -quit | xargs -I{} {}
```
