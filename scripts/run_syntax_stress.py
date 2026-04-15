#!/usr/bin/env python3
"""Run compiler/stdlib stress test suites.

For each test project (directory containing main.vigil) under each suite dir:
  1. vigil check main.vigil
  2. vigil run main.vigil  (capture stdout)
  3. vigil transpile . -o <work-dir>/c-<name>
  4. cmake configure + build the transpiled C project
  5. Run the C binary  (capture stdout)
  6. Compare interpreter vs C output (must match exactly)

Suites are processed in the order given. Within each suite, tests are
sorted alphabetically for deterministic execution.

Use -j N to run tests in parallel (default: sequential).

Exit code is the number of failed tests (0 = all pass).
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed


def transpiled_executable_target(c_dir):
    """Read the generated CMakeLists.txt to determine the app target name."""
    cmake_lists = os.path.join(c_dir, "CMakeLists.txt")
    with open(cmake_lists, "r", encoding="utf-8") as f:
        content = f.read()
    match = re.search(r"add_executable\(\s*([A-Za-z0-9_+-]+)\b", content)
    if match is None:
        raise ValueError(f"no add_executable target found in {cmake_lists}")
    return match.group(1)


def find_executable(build_dir, target_name):
    """Find the built executable for a known CMake target."""
    exe_name = f"{target_name}.exe" if sys.platform == "win32" else target_name
    candidates = [
        os.path.join(build_dir, exe_name),
        os.path.join(build_dir, "Release", exe_name),
        os.path.join(build_dir, "Debug", exe_name),
    ]
    for c in candidates:
        if os.path.isfile(c):
            return c

    for root, _dirs, files in os.walk(build_dir):
        if "CMakeFiles" in root:
            continue
        for f in files:
            if f == exe_name:
                return os.path.join(root, f)
    return None


def run_test(vigil_bin, test_dir, work_dir):
    """Run a single test project. Returns (passed, detail)."""
    name = os.path.basename(test_dir)
    main_vigil = os.path.join(test_dir, "main.vigil")

    r = subprocess.run([vigil_bin, "check", main_vigil],
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=30)
    if r.returncode != 0:
        return False, f"vigil check failed:\n{r.stderr}"

    r = subprocess.run([vigil_bin, "run", main_vigil],
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=60, cwd=test_dir)
    if r.returncode != 0:
        return False, f"vigil run failed (exit {r.returncode}):\n{r.stderr}"
    interp_out = r.stdout

    c_dir = os.path.join(work_dir, f"c-{name}")
    if os.path.exists(c_dir):
        shutil.rmtree(c_dir)
    r = subprocess.run([vigil_bin, "transpile", test_dir, "-o", c_dir],
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=60)
    if r.returncode != 0:
        return False, f"vigil transpile failed:\n{r.stderr}"

    build_dir = os.path.join(c_dir, "build")
    r = subprocess.run(["cmake", "-B", build_dir, "-S", c_dir,
                        "-DCMAKE_BUILD_TYPE=Release"],
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=120)
    if r.returncode != 0:
        return False, f"cmake configure failed:\n{r.stdout}\n{r.stderr}"

    r = subprocess.run(["cmake", "--build", build_dir, "--config", "Release"],
                       capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=120)
    if r.returncode != 0:
        return False, f"cmake build failed:\n{r.stdout}\n{r.stderr}"

    target_name = transpiled_executable_target(c_dir)
    exe = find_executable(build_dir, target_name)
    if exe is None:
        return False, f"no executable found for target {target_name} in {build_dir}"

    r = subprocess.run([exe], capture_output=True, text=True, encoding="utf-8",
                       errors="replace", timeout=30)
    c_out = r.stdout

    interp_lines = interp_out.rstrip().splitlines()
    c_lines = c_out.rstrip().splitlines()
    if interp_lines != c_lines:
        detail = "output mismatch:\n--- interpreter\n"
        for line in interp_lines:
            detail += f"  {line}\n"
        detail += "+++ C binary\n"
        for line in c_lines:
            detail += f"  {line}\n"
        return False, detail

    return True, f"{len(interp_lines)} lines match"


def collect_tests(suite_dirs):
    """Collect test directories from all suites, in order."""
    tests = []
    for suite_dir in suite_dirs:
        suite_dir = os.path.abspath(suite_dir)
        entries = sorted(
            d for d in os.listdir(suite_dir)
            if os.path.isfile(os.path.join(suite_dir, d, "main.vigil"))
        )
        for d in entries:
            tests.append((os.path.basename(suite_dir), os.path.join(suite_dir, d)))
    return tests


def run_single(vigil_bin, suite_name, test_dir, work_dir):
    """Wrapper for parallel execution. Returns (suite_name, test_name, ok, detail)."""
    name = os.path.basename(test_dir)
    try:
        ok, detail = run_test(vigil_bin, test_dir, work_dir)
    except subprocess.TimeoutExpired:
        ok, detail = False, "timeout"
    except Exception as e:
        ok, detail = False, str(e)
    return suite_name, name, ok, detail


def run_sequential(vigil_bin, tests, work_dir):
    """Run all tests sequentially (original behavior)."""
    passed = failed = 0
    current_suite = None

    for suite_name, test_dir in tests:
        if suite_name != current_suite:
            current_suite = suite_name
            print(f"\n-- {suite_name} --")

        name = os.path.basename(test_dir)
        try:
            ok, detail = run_test(vigil_bin, test_dir, work_dir)
        except subprocess.TimeoutExpired:
            ok, detail = False, "timeout"
        except Exception as e:
            ok, detail = False, str(e)

        if ok:
            passed += 1
            print(f"  PASS  {name}  ({detail})")
        else:
            failed += 1
            print(f"  FAIL  {name}")
            for line in detail.strip().splitlines():
                print(f"        {line}")

    return passed, failed


def run_parallel(vigil_bin, tests, work_dir, jobs):
    """Run all tests in parallel, print results grouped by suite."""
    results = []

    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {
            pool.submit(run_single, vigil_bin, suite_name, test_dir, work_dir): (suite_name, test_dir)
            for suite_name, test_dir in tests
        }
        for future in as_completed(futures):
            results.append(future.result())

    # Sort results to match the original deterministic order (suite then test name).
    suite_order = {}
    for i, (suite_name, _) in enumerate(tests):
        if suite_name not in suite_order:
            suite_order[suite_name] = i

    results.sort(key=lambda r: (suite_order.get(r[0], 0), r[1]))

    passed = failed = 0
    current_suite = None

    for suite_name, name, ok, detail in results:
        if suite_name != current_suite:
            current_suite = suite_name
            print(f"\n-- {suite_name} --")

        if ok:
            passed += 1
            print(f"  PASS  {name}  ({detail})")
        else:
            failed += 1
            print(f"  FAIL  {name}")
            for line in detail.strip().splitlines():
                print(f"        {line}")

    return passed, failed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vigil-bin", required=True)
    parser.add_argument("--suite-dir", required=True, action="append",
                        help="Test suite directory (may be repeated; processed in order)")
    parser.add_argument("--work-dir", default=None)
    parser.add_argument("-j", "--jobs", type=int, default=1,
                        help="Number of parallel test workers (default: 1, sequential)")
    args = parser.parse_args()

    vigil_bin = os.path.abspath(args.vigil_bin)
    work_dir = args.work_dir or tempfile.mkdtemp(prefix="syntax_stress_")
    os.makedirs(work_dir, exist_ok=True)

    tests = collect_tests(args.suite_dir)
    if not tests:
        print("ERROR: no test projects found", file=sys.stderr)
        return 1

    if args.jobs > 1:
        print(f"Running {len(tests)} tests with {args.jobs} parallel workers")
        passed, failed = run_parallel(vigil_bin, tests, work_dir, args.jobs)
    else:
        passed, failed = run_sequential(vigil_bin, tests, work_dir)

    print(f"\n{passed} passed, {failed} failed out of {len(tests)}")
    return failed


if __name__ == "__main__":
    sys.exit(main())
