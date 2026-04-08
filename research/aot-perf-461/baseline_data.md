# Raw Baseline Results

Date: Wed Apr  8 08:18:28 EDT 2026
Machine: Apple M1 Max (arm64), macOS
Vigil: 0.2.3
Lua: 5.4.7
Python: 3.12.3
Dart SDK: 3.11.4 (AOT compiled)

Each benchmark run 3 times, best-of-3 reported.

## fib(35)

| Runtime | Run 1 | Run 2 | Run 3 | Best |
|---------|-------|-------|-------|------|
| Vigil AOT | 0.576 | 0.574 | 0.574 | 0.574 |
| Vigil Interp | 0.600 | 0.606 | 0.598 | 0.598 |
| Lua 5.4 | 0.562 | 0.560 | 0.566 | 0.560 |
| Python 3.12 | 0.940 | 0.938 | 0.955 | 0.938 |
| Dart AOT | 0.083 | 0.083 | 0.080 | 0.080 |

## arith (100M i64 ops)

| Runtime | Run 1 | Run 2 | Run 3 | Best |
|---------|-------|-------|-------|------|
| Vigil AOT | 2.552 | 2.516 | 2.528 | 2.516 |
| Vigil Interp | 2.535 | 2.571 | 2.523 | 2.523 |
| Lua 5.4 | 3.561 | 3.617 | 3.572 | 3.561 |
| Python 3.12 | 10.719 | 10.559 | 10.877 | 10.559 |
| Dart AOT | 0.079 | 0.071 | 0.070 | 0.070 |

## bitwise (10M i32 ops)

| Runtime | Run 1 | Run 2 | Run 3 | Best |
|---------|-------|-------|-------|------|
| Vigil AOT | 0.498 | 0.497 | 0.494 | 0.494 |
| Vigil Interp | 0.496 | 0.494 | 0.494 | 0.494 |
| Lua 5.4 | 0.166 | 0.165 | 0.164 | 0.164 |
| Python 3.12 | 1.647 | 1.650 | 1.643 | 1.643 |
| Dart AOT | 0.039 | 0.035 | 0.034 | 0.034 |

## nested (1M iterations, i32)

| Runtime | Run 1 | Run 2 | Run 3 | Best |
|---------|-------|-------|-------|------|
| Vigil AOT | 0.041 | 0.040 | 0.041 | 0.040 |
| Vigil Interp | 0.041 | 0.040 | 0.039 | 0.039 |
| Lua 5.4 | 0.030 | 0.029 | 0.029 | 0.029 |
| Python 3.12 | 0.125 | 0.125 | 0.131 | 0.125 |
| Dart AOT | 0.390 | 0.031 | 0.031 | 0.031 |

## ack(3, 10)

| Runtime | Run 1 | Run 2 | Run 3 | Best |
|---------|-------|-------|-------|------|
| Vigil AOT | 2.851 | 2.848 | 2.893 | 2.848 |
| Vigil Interp | 2.173 | 2.159 | 2.220 | 2.159 |
| Lua 5.4 | 0.772 | 0.775 | 0.776 | 0.772 |
| Python 3.12 | 2.818 | 2.861 | 2.836 | 2.818 |
| Dart AOT | 0.221 | 0.223 | 0.222 | 0.221 |

## Notes

- Dart AOT first-run includes process startup; best-of-3 excludes cold start.
- Vigil `arith` benchmark uses i64 types which are NOT in the AOT subset —
  both AOT and Interp times are identical because AOT falls back to interpreter.
- Vigil `ack` AOT is 32% SLOWER than interpreter due to C helper call overhead
  in `vigil_aot_numeric_call_self`.
- Vigil `bitwise` AOT equals interpreter — NaN-box encode/decode overhead
  negates any benefit from native code generation.
