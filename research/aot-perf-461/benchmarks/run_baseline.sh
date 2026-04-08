#!/bin/bash
# AOT Performance Research - Baseline Benchmarks
# Runs each benchmark 3 times, reports min/median/max

set -e

VIGIL_BIN="/Users/michaellong/projects/vigil/build/vigil"
BENCH_DIR="/tmp/bench"
RESULTS_FILE="/tmp/bench/results_baseline.txt"

benchmarks=("fib" "arith" "bitwise" "nested" "ack")
runs=3

time_cmd() {
    # Returns wall-clock seconds using bash built-in
    local start end
    start=$(python3 -c "import time; print(f'{time.monotonic():.6f}')")
    eval "$@" > /dev/null 2>&1
    end=$(python3 -c "import time; print(f'{time.monotonic():.6f}')")
    python3 -c "print(f'{$end - $start:.3f}')"
}

run_bench() {
    local label="$1"
    local cmd="$2"
    local times=()
    for i in $(seq 1 $runs); do
        t=$(time_cmd "$cmd")
        times+=("$t")
    done
    # Sort and pick min
    min=$(printf '%s\n' "${times[@]}" | sort -n | head -1)
    printf "%-25s %s  (runs: %s)\n" "$label" "$min" "${times[*]}"
}

echo "=== AOT Performance Baseline ==="
echo "Date: $(date)"
echo "Machine: $(uname -m) $(sysctl -n machdep.cpu.brand_string 2>/dev/null || echo 'unknown')"
echo "Vigil: $($VIGIL_BIN version 2>&1 | head -1)"
echo "Lua: $(lua -v 2>&1)"
echo "Python: $(python3 --version 2>&1)"
echo "Dart: $(dart --version 2>&1)"
echo ""

for bench in "${benchmarks[@]}"; do
    echo "--- $bench ---"
    run_bench "Vigil AOT" "$VIGIL_BIN run $BENCH_DIR/$bench.vigil"
    run_bench "Vigil Interp" "VIGIL_NO_AOT=1 $VIGIL_BIN run $BENCH_DIR/$bench.vigil"
    run_bench "Lua 5.4" "lua $BENCH_DIR/$bench.lua"
    run_bench "Python 3.12" "python3 $BENCH_DIR/$bench.py"
    run_bench "Dart AOT" "$BENCH_DIR/${bench}_dart"
    echo ""
done
