#!/usr/bin/env bash
set -euo pipefail

VIGIL="$(realpath /home/michael/projects/vigil/build/vigil)"
STRESS_DIR="$(cd "$(dirname "$0")" && pwd)"
RESULTS_FILE="$STRESS_DIR/RESULTS.md"

# Collect results
declare -a TEST_NAMES=()
declare -a TEST_RESULTS=()
declare -a TEST_DETAILS=()

pass_count=0
fail_count=0

run_test() {
    local test_dir="$1"
    local test_name="$(basename "$test_dir")"
    local c_dir="$STRESS_DIR/c-${test_name}"
    local log_file="$STRESS_DIR/${test_name}.log"

    TEST_NAMES+=("$test_name")

    echo ""
    echo "================================================================"
    echo "  RUNNING: $test_name"
    echo "================================================================"

    # Step 1: vigil check
    echo "  [1/6] vigil check..."
    if ! (cd "$test_dir" && "$VIGIL" check main.vigil) > "$log_file" 2>&1; then
        echo "  FAIL: vigil check failed"
        cat "$log_file"
        TEST_RESULTS+=("FAIL")
        TEST_DETAILS+=("vigil check failed:\n$(cat "$log_file")")
        ((fail_count++)) || true
        return
    fi

    # Step 2: vigil run — capture output
    echo "  [2/6] vigil run..."
    local vigil_stdout vigil_stderr vigil_combined
    vigil_stdout="$STRESS_DIR/${test_name}_vigil_stdout.txt"
    vigil_stderr="$STRESS_DIR/${test_name}_vigil_stderr.txt"
    if ! (cd "$test_dir" && "$VIGIL" run main.vigil) > "$vigil_stdout" 2> "$vigil_stderr"; then
        echo "  FAIL: vigil run failed"
        cat "$vigil_stderr"
        TEST_RESULTS+=("FAIL")
        TEST_DETAILS+=("vigil run failed:\nstdout:\n$(cat "$vigil_stdout")\nstderr:\n$(cat "$vigil_stderr")")
        ((fail_count++)) || true
        rm -rf "$c_dir"
        return
    fi

    # Step 3: vigil transpile
    echo "  [3/6] vigil transpile..."
    rm -rf "$c_dir"
    if ! (cd "$test_dir" && "$VIGIL" transpile . -o "$c_dir") > "$log_file" 2>&1; then
        echo "  FAIL: vigil transpile failed"
        cat "$log_file"
        TEST_RESULTS+=("FAIL")
        TEST_DETAILS+=("vigil transpile failed:\n$(cat "$log_file")")
        ((fail_count++)) || true
        rm -rf "$c_dir"
        return
    fi

    # Step 4: cmake build
    echo "  [4/6] cmake build..."
    if ! (cd "$c_dir" && cmake -B build -S . -DCMAKE_BUILD_TYPE=Release) > "$log_file" 2>&1; then
        echo "  FAIL: cmake configure failed"
        cat "$log_file"
        TEST_RESULTS+=("FAIL")
        TEST_DETAILS+=("cmake configure failed:\n$(cat "$log_file")")
        ((fail_count++)) || true
        rm -rf "$c_dir"
        return
    fi

    if ! (cd "$c_dir" && cmake --build build --config Release) >> "$log_file" 2>&1; then
        echo "  FAIL: cmake build failed"
        cat "$log_file"
        TEST_RESULTS+=("FAIL")
        TEST_DETAILS+=("cmake build failed:\n$(cat "$log_file")")
        ((fail_count++)) || true
        rm -rf "$c_dir"
        return
    fi

    # Step 5: find and run executable
    echo "  [5/6] running C binary..."
    local exe
    exe=$(find "$c_dir/build" -type f -executable ! -name '*.cmake' ! -name 'Makefile' ! -path '*/CMakeFiles/*' -print -quit 2>/dev/null || true)
    if [ -z "$exe" ]; then
        # Try common names
        for candidate in "$c_dir/build/${test_name}" "$c_dir/build/Release/${test_name}" "$c_dir/build/main"; do
            if [ -x "$candidate" ]; then
                exe="$candidate"
                break
            fi
        done
    fi

    if [ -z "$exe" ]; then
        echo "  FAIL: no executable found in $c_dir/build/"
        find "$c_dir/build" -type f 2>/dev/null | head -20
        TEST_RESULTS+=("FAIL")
        TEST_DETAILS+=("no executable found in build directory")
        ((fail_count++)) || true
        rm -rf "$c_dir"
        return
    fi

    local c_stdout c_stderr
    c_stdout="$STRESS_DIR/${test_name}_c_stdout.txt"
    c_stderr="$STRESS_DIR/${test_name}_c_stderr.txt"
    if ! "$exe" > "$c_stdout" 2> "$c_stderr"; then
        echo "  WARN: C binary exited non-zero (may be expected)"
    fi

    # Step 6: compare outputs
    echo "  [6/6] comparing outputs..."

    # Normalize trailing whitespace/newlines
    sed -e 's/[[:space:]]*$//' "$vigil_stdout" > "$STRESS_DIR/${test_name}_vigil_norm.txt"
    sed -e 's/[[:space:]]*$//' "$c_stdout" > "$STRESS_DIR/${test_name}_c_norm.txt"

    local diff_file="$STRESS_DIR/${test_name}_diff.txt"
    if diff -u "$STRESS_DIR/${test_name}_vigil_norm.txt" "$STRESS_DIR/${test_name}_c_norm.txt" > "$diff_file" 2>&1; then
        echo "  PASS ✓"
        TEST_RESULTS+=("PASS")
        TEST_DETAILS+=("")
        ((pass_count++)) || true
    else
        echo "  FAIL ✗ — output mismatch"
        cat "$diff_file"
        TEST_RESULTS+=("FAIL")
        local detail="Output mismatch:\n\n\`\`\`diff\n$(cat "$diff_file")\n\`\`\`\n\nVigil stdout:\n\`\`\`\n$(cat "$vigil_stdout")\n\`\`\`\n\nC stdout:\n\`\`\`\n$(cat "$c_stdout")\n\`\`\`"
        # Also check stderr
        if [ -s "$vigil_stderr" ] || [ -s "$c_stderr" ]; then
            detail="$detail\n\nVigil stderr:\n\`\`\`\n$(cat "$vigil_stderr")\n\`\`\`\n\nC stderr:\n\`\`\`\n$(cat "$c_stderr")\n\`\`\`"
        fi
        TEST_DETAILS+=("$detail")
        ((fail_count++)) || true
    fi

    # Cleanup C build
    rm -rf "$c_dir"
    # Cleanup temp files
    rm -f "$STRESS_DIR/${test_name}_vigil_norm.txt" "$STRESS_DIR/${test_name}_c_norm.txt"
}

echo "========================================"
echo "  VIGIL-TO-C SYNTAX STRESS TEST SUITE"
echo "========================================"
echo "Vigil binary: $VIGIL"
echo ""

# Phase 1: Small focused tests
for test_dir in "$STRESS_DIR"/t[0-9][0-9]-*/; do
    if [ -f "$test_dir/main.vigil" ]; then
        run_test "$test_dir"
    fi
done

# Phase 2: Large comprehensive tests
for test_dir in "$STRESS_DIR"/large-*/; do
    if [ -f "$test_dir/main.vigil" ]; then
        run_test "$test_dir"
    fi
done

# Generate RESULTS.md
echo ""
echo "========================================"
echo "  GENERATING RESULTS.md"
echo "========================================"

{
    echo "# Vigil-to-C Compiler Syntax Stress Test Results"
    echo ""
    echo "Date: $(date -Iseconds)"
    echo ""
    echo "Vigil version: $($VIGIL version 2>&1)"
    echo ""
    echo "## Summary"
    echo ""
    echo "- **Total tests:** ${#TEST_NAMES[@]}"
    echo "- **Passed:** $pass_count"
    echo "- **Failed:** $fail_count"
    echo ""
    echo "## Results Table"
    echo ""
    echo "| # | Test | Result |"
    echo "|---|------|--------|"
    for i in "${!TEST_NAMES[@]}"; do
        local_result="${TEST_RESULTS[$i]}"
        local_icon="✓"
        if [ "$local_result" = "FAIL" ]; then
            local_icon="✗"
        fi
        echo "| $((i+1)) | ${TEST_NAMES[$i]} | $local_icon $local_result |"
    done
    echo ""

    # Failure details
    has_failures=false
    for i in "${!TEST_NAMES[@]}"; do
        if [ "${TEST_RESULTS[$i]}" = "FAIL" ]; then
            has_failures=true
            break
        fi
    done

    if [ "$has_failures" = true ]; then
        echo "## Failure Details"
        echo ""
        for i in "${!TEST_NAMES[@]}"; do
            if [ "${TEST_RESULTS[$i]}" = "FAIL" ]; then
                echo "### ${TEST_NAMES[$i]}"
                echo ""
                echo -e "${TEST_DETAILS[$i]}"
                echo ""
                echo "**Reproduction:**"
                echo "\`\`\`bash"
                echo "cd tests/compiler-syntax-stress/${TEST_NAMES[$i]}"
                echo "vigil run main.vigil > /tmp/vigil_out.txt 2>&1"
                echo "vigil transpile . -o /tmp/c-${TEST_NAMES[$i]}"
                echo "cd /tmp/c-${TEST_NAMES[$i]} && cmake -B build -S . && cmake --build build"
                echo "find build -type f -executable -print -quit | xargs -I{} sh -c '{} > /tmp/c_out.txt 2>&1'"
                echo "diff /tmp/vigil_out.txt /tmp/c_out.txt"
                echo "\`\`\`"
                echo ""
            fi
        done
    fi

    echo "## Reproduction Steps (General)"
    echo ""
    echo "For any test:"
    echo "\`\`\`bash"
    echo "cd tests/compiler-syntax-stress/<test-name>"
    echo "../../build/vigil check main.vigil"
    echo "../../build/vigil run main.vigil"
    echo "../../build/vigil transpile . -o /tmp/c-output"
    echo "cd /tmp/c-output && cmake -B build -S . && cmake --build build"
    echo "find build -type f -executable -print -quit | xargs -I{} {}"
    echo "\`\`\`"
} > "$RESULTS_FILE"

echo ""
echo "Results written to: $RESULTS_FILE"
echo ""
echo "========================================"
echo "  FINAL: $pass_count PASSED, $fail_count FAILED out of ${#TEST_NAMES[@]}"
echo "========================================"
